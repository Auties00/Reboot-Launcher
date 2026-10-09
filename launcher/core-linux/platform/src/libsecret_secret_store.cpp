#include "reboot/os_linux/platform/libsecret_secret_store.hpp"

#include <array>
#include <atomic>
#include <mutex>
#include <utility>
#include <vector>

#include "base64.hpp"
#include "glib_runtime.hpp"
#include "messages.hpp"
#include "reboot/foundation/sha256.hpp"
#include "reboot/ports/file_system.hpp"

namespace reboot::os_linux::platform {

namespace {

constexpr const char* kLibrary = "libsecret-1.so.0";
constexpr const char* kSchemaName = "dev.projectreboot.Launcher";
// SECRET_SERVICE_OPEN_SESSION: fails at once when no provider owns org.freedesktop.secrets.
constexpr int kOpenSession = 1 << 1;

// SecretSchema and SecretSchemaAttribute, part of libsecret's stable ABI.
struct SchemaAttribute {
    const char* name;
    int type;
};

struct SecretSchemaLayout {
    const char* name;
    int flags;
    std::array<SchemaAttribute, 32> attributes;
    int reserved;
    std::array<void*, 7> reserved_pointers;
};

const SecretSchemaLayout kSchema{
    .name = kSchemaName,
    .flags = 0,
    .attributes = {{{"root", 0}, {"key", 0}, {nullptr, 0}}},
    .reserved = 0,
    .reserved_pointers = {},
};

// The libsecret calls, plus GHashTable for their attribute maps.
struct SecretApi {
    GLibRuntime glib;
    void* (*service_get_sync)(int, void*, GErrorView**) = nullptr;
    int (*storev_sync)(const SecretSchemaLayout*, void*, const char*, const char*, const char*, void*,
                       GErrorView**) = nullptr;
    char* (*lookupv_sync)(const SecretSchemaLayout*, void*, void*, GErrorView**) = nullptr;
    int (*clearv_sync)(const SecretSchemaLayout*, void*, void*, GErrorView**) = nullptr;
    void (*password_free)(char*) = nullptr;
    void* (*hash_table_new)(void*, void*) = nullptr;
    int (*hash_table_insert)(void*, void*, void*) = nullptr;
    void (*hash_table_unref)(void*) = nullptr;
    void* str_hash = nullptr;
    void* str_equal = nullptr;

    [[nodiscard]] static std::optional<SecretApi> load() {
        std::optional<GLibRuntime> glib = GLibRuntime::load(kLibrary);
        if (!glib) return std::nullopt;
        SecretApi api{.glib = *glib};
        const bool complete = resolve(api.glib, "secret_service_get_sync", api.service_get_sync) &&
                              resolve(api.glib, "secret_password_storev_sync", api.storev_sync) &&
                              resolve(api.glib, "secret_password_lookupv_sync", api.lookupv_sync) &&
                              resolve(api.glib, "secret_password_clearv_sync", api.clearv_sync) &&
                              resolve(api.glib, "secret_password_free", api.password_free) &&
                              resolve(api.glib, "g_hash_table_new", api.hash_table_new) &&
                              resolve(api.glib, "g_hash_table_insert", api.hash_table_insert) &&
                              resolve(api.glib, "g_hash_table_unref", api.hash_table_unref);
        api.str_hash = api.glib.symbol("g_str_hash");
        api.str_equal = api.glib.symbol("g_str_equal");
        if (!complete || api.str_hash == nullptr || api.str_equal == nullptr) return std::nullopt;
        return api;
    }
};

// root=<hash16> and key=<key>; the strings must outlive the table.
class Attributes {
public:
    Attributes(const SecretApi& api, const std::string& root, const std::string& key)
        : api_(api), table_(api.hash_table_new(api.str_hash, api.str_equal)) {
        api_.hash_table_insert(table_, const_cast<char*>("root"), const_cast<char*>(root.c_str()));
        api_.hash_table_insert(table_, const_cast<char*>("key"), const_cast<char*>(key.c_str()));
    }
    ~Attributes() { api_.hash_table_unref(table_); }
    Attributes(const Attributes&) = delete;
    Attributes& operator=(const Attributes&) = delete;

    [[nodiscard]] void* get() const noexcept { return table_; }

private:
    const SecretApi& api_;
    void* table_;
};

// Wipes the base64 form of a value once the call that needed it is over.
class WipedString {
public:
    explicit WipedString(std::string text) : text_(std::move(text)) {}
    ~WipedString() { secure_wipe(text_.data(), text_.size()); }
    WipedString(const WipedString&) = delete;
    WipedString& operator=(const WipedString&) = delete;

    [[nodiscard]] const std::string& get() const noexcept { return text_; }

private:
    std::string text_;
};

[[nodiscard]] NativePath fallback_file(const NativePath& dir, std::string_view key) {
    const auto digest = sha256({reinterpret_cast<const u8*>(key.data()), key.size()});
    return dir / (to_hex(digest) + ".secret");
}

}  // namespace

struct LibsecretSecretStore::Impl {
    std::string root;
    NativePath fallback_dir;
    ports::IFileSystem& fs;
    std::mutex mutex;
    std::optional<SecretApi> api;
    bool load_tried = false;
    std::atomic<ports::SecretStoreKind> kind{ports::SecretStoreKind::File};

    Impl(std::string root_hash16, NativePath dir, ports::IFileSystem& files)
        : root(std::move(root_hash16)), fallback_dir(std::move(dir)), fs(files) {}

    [[nodiscard]] Diagnostic timeout() const {
        return make_diag(ErrorDomain::Platform, kSecretServiceTimeout)
            .arg("deadline", std::chrono::milliseconds{kPromptDeadline})
            .retryable();
    }

    [[nodiscard]] Diagnostic failed(std::string_view call, GErrorView* error) const {
        return make_diag(ErrorDomain::Platform, kCallFailed).arg("call", call).detail(api->glib.take_error(error));
    }

    // Reaches the Secret Service within kConnectDeadline; sets `kind`. Requires `mutex`.
    void probe() {
        if (!load_tried) {
            load_tried = true;
            api = SecretApi::load();
        }
        if (!api) {
            kind = ports::SecretStoreKind::File;
            return;
        }
        const Cancellable cancellable(api->glib);
        GErrorView* error = nullptr;
        void* service = nullptr;
        {
            const CancelAfter deadline(api->glib, cancellable.get(), kConnectDeadline);
            service = api->service_get_sync(kOpenSession, cancellable.get(), &error);
        }
        if (service == nullptr) {
            (void)api->glib.take_error(error);
            kind = ports::SecretStoreKind::File;
            return;
        }
        api->glib.object_unref(service);
        kind = ports::SecretStoreKind::Os;
    }

    [[nodiscard]] Result<void> store_os(const std::string& key, std::span<const u8> value) {
        const WipedString encoded(base64_encode(value));
        const Attributes attributes(*api, root, key);
        const std::string label = "Reboot Launcher " + key;
        const Cancellable cancellable(api->glib);
        GErrorView* error = nullptr;
        int stored = 0;
        bool fired = false;
        {
            const CancelAfter deadline(api->glib, cancellable.get(), kPromptDeadline);
            stored = api->storev_sync(&kSchema, attributes.get(), nullptr, label.c_str(), encoded.get().c_str(),
                                      cancellable.get(), &error);
            fired = deadline.fired();
        }
        if (stored != 0) return {};
        if (fired) {
            (void)api->glib.take_error(error);
            return std::unexpected(timeout());
        }
        return std::unexpected(failed("secret_password_storev_sync", error));
    }

    [[nodiscard]] Result<std::optional<SecretBytes>> lookup_os(const std::string& key) {
        const Attributes attributes(*api, root, key);
        const Cancellable cancellable(api->glib);
        GErrorView* error = nullptr;
        char* found = nullptr;
        bool fired = false;
        {
            const CancelAfter deadline(api->glib, cancellable.get(), kPromptDeadline);
            found = api->lookupv_sync(&kSchema, attributes.get(), cancellable.get(), &error);
            fired = deadline.fired();
        }
        if (found == nullptr) {
            if (error == nullptr) return std::nullopt;
            if (fired) {
                (void)api->glib.take_error(error);
                return std::unexpected(timeout());
            }
            return std::unexpected(failed("secret_password_lookupv_sync", error));
        }
        std::optional<std::vector<u8>> decoded = base64_decode(found);
        api->password_free(found);
        if (!decoded) return make_diag(ErrorDomain::Platform, kSecretUnreadable).arg("key", key).fail();
        return SecretBytes{std::move(*decoded)};
    }

    [[nodiscard]] Result<void> clear_os(const std::string& key) {
        const Attributes attributes(*api, root, key);
        const Cancellable cancellable(api->glib);
        GErrorView* error = nullptr;
        bool fired = false;
        {
            const CancelAfter deadline(api->glib, cancellable.get(), kPromptDeadline);
            (void)api->clearv_sync(&kSchema, attributes.get(), cancellable.get(), &error);
            fired = deadline.fired();
        }
        // FALSE without an error means nothing matched.
        if (error == nullptr) return {};
        if (fired) {
            (void)api->glib.take_error(error);
            return std::unexpected(timeout());
        }
        return std::unexpected(failed("secret_password_clearv_sync", error));
    }

    [[nodiscard]] Result<void> store_file(std::string_view key, std::span<const u8> value) {
        if (auto made = fs.create_dirs_owner_only(fallback_dir); !made) return made;
        const NativePath file = fallback_file(fallback_dir, key);
        if (auto written = fs.atomic_replace(file, value, false); !written) return written;
        return fs.restrict_to_owner(file);
    }

    [[nodiscard]] Result<std::optional<SecretBytes>> lookup_file(std::string_view key) {
        Result<std::vector<u8>> bytes = fs.read_all(fallback_file(fallback_dir, key));
        if (!bytes) {
            if (bytes.error().kind == ErrorKind::NotFound) return std::nullopt;
            return std::unexpected(std::move(bytes.error()));
        }
        return SecretBytes{std::move(*bytes)};
    }

    [[nodiscard]] Result<void> clear_file(std::string_view key) {
        return fs.remove_tree(fallback_file(fallback_dir, key));
    }

    // In File mode the service may have come up since; requires `mutex`.
    [[nodiscard]] bool os_reachable() {
        if (kind == ports::SecretStoreKind::File) probe();
        return kind == ports::SecretStoreKind::Os;
    }

    // After an Os call failed: true when the provider has gone since the probe, so the file takes
    // over; a deadline passed never falls back. Requires `mutex`.
    [[nodiscard]] bool fell_back(const Diagnostic& failure) {
        if (failure.is(kSecretServiceTimeout)) return false;
        probe();
        return kind == ports::SecretStoreKind::File;
    }
};

LibsecretSecretStore::LibsecretSecretStore(std::string root_hash16, NativePath fallback_dir, ports::IFileSystem& fs)
    : impl_(std::make_unique<Impl>(std::move(root_hash16), std::move(fallback_dir), fs)) {
    const std::lock_guard lock(impl_->mutex);
    impl_->probe();
}

LibsecretSecretStore::~LibsecretSecretStore() = default;

ports::SecretStoreKind LibsecretSecretStore::kind() const { return impl_->kind; }

Result<void> LibsecretSecretStore::put(std::string_view key, std::span<const u8> value) {
    const std::lock_guard lock(impl_->mutex);
    const std::string name{key};
    if (impl_->os_reachable()) {
        Result<void> stored = impl_->store_os(name, value);
        if (stored) return impl_->clear_file(key);
        if (!impl_->fell_back(stored.error())) return stored;
    }
    return impl_->store_file(key, value);
}

Result<std::optional<SecretBytes>> LibsecretSecretStore::get(std::string_view key) {
    const std::lock_guard lock(impl_->mutex);
    const std::string name{key};
    if (impl_->kind == ports::SecretStoreKind::Os) {
        Result<std::optional<SecretBytes>> found = impl_->lookup_os(name);
        if (found && *found) return found;
        if (!found && !impl_->fell_back(found.error())) return found;
        return impl_->lookup_file(key);
    }
    Result<std::optional<SecretBytes>> found = impl_->lookup_file(key);
    if (!found || *found) return found;
    if (!impl_->os_reachable()) return std::nullopt;
    return impl_->lookup_os(name);
}

Result<void> LibsecretSecretStore::erase(std::string_view key) {
    const std::lock_guard lock(impl_->mutex);
    const std::string name{key};
    if (impl_->os_reachable()) {
        if (Result<void> cleared = impl_->clear_os(name); !cleared && !impl_->fell_back(cleared.error())) return cleared;
    }
    return impl_->clear_file(key);
}

}  // namespace reboot::os_linux::platform
