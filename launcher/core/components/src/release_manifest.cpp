#include "reboot/components/release_manifest.hpp"

#include <limits>
#include <string>
#include <utility>

#include <boost/json/array.hpp>
#include <boost/json/parse.hpp>
#include <boost/json/value.hpp>

#include "json_fields.hpp"
#include "messages.hpp"

namespace reboot::components {

namespace {

namespace json = boost::json;
using json_fields::index_path;
using json_fields::malformed;
using json_fields::Reader;

[[nodiscard]] Result<Reader> element(const json::array& array, std::size_t index, std::string_view parent) {
    return Reader::of(array[index], index_path(parent, index));
}

// nullopt for an entry built for a platform this build cannot name.
[[nodiscard]] Result<std::optional<AppEntry>> read_app(const Reader& in) {
    auto platform_value = in.required("platform");
    if (!platform_value) return std::unexpected(std::move(platform_value.error()));
    auto platform_in = Reader::of(**platform_value, in.at("platform"));
    if (!platform_in) return std::unexpected(std::move(platform_in.error()));
    auto platform = json_fields::read_platform(*platform_in);
    if (!platform) return std::unexpected(std::move(platform.error()));

    AppEntry entry;
    auto channel = in.nonempty_string_at("channel");
    if (!channel) return std::unexpected(std::move(channel.error()));
    entry.channel = std::move(*channel);
    auto version = in.semver_at("version");
    if (!version) return std::unexpected(std::move(version.error()));
    entry.version = std::move(*version);
    auto min_supported = in.optional_semver("min_supported");
    if (!min_supported) return std::unexpected(std::move(min_supported.error()));
    entry.min_supported = std::move(*min_supported);

    auto kind = in.string_at("kind");
    if (!kind) return std::unexpected(std::move(kind.error()));
    const auto known_kind = json_fields::find_named(json_fields::kAppKindNames, *kind);
    if (!known_kind) return std::unexpected(malformed(in.at("kind")));
    entry.kind = *known_kind;

    auto package_value = in.required("package");
    if (!package_value) return std::unexpected(std::move(package_value.error()));
    auto package_in = Reader::of(**package_value, in.at("package"));
    if (!package_in) return std::unexpected(std::move(package_in.error()));
    auto package = json_fields::read_remote_file(*package_in);
    if (!package) return std::unexpected(std::move(package.error()));
    entry.package = std::move(*package);

    auto downgrade_ok = in.optional_bool("downgrade_ok");
    if (!downgrade_ok) return std::unexpected(std::move(downgrade_ok.error()));
    entry.downgrade_ok = *downgrade_ok;

    if (!*platform) return std::optional<AppEntry>{};
    entry.platform = **platform;
    return std::optional<AppEntry>(std::move(entry));
}

[[nodiscard]] Result<PayloadEntry> read_payload(const Reader& in) {
    PayloadEntry entry;
    auto version = in.semver_at("version");
    if (!version) return std::unexpected(std::move(version.error()));
    entry.version = std::move(*version);
    auto abi = in.u64_at("payload_abi");
    if (!abi) return std::unexpected(std::move(abi.error()));
    if (*abi > std::numeric_limits<u16>::max()) return std::unexpected(malformed(in.at("payload_abi")));
    entry.payload_abi = static_cast<u16>(*abi);

    auto files = in.required("files");
    if (!files) return std::unexpected(std::move(files.error()));
    const json::array* list = (*files)->if_array();
    if (list == nullptr) return std::unexpected(malformed(in.at("files")));
    for (std::size_t i = 0; i < list->size(); ++i) {
        auto file_in = element(*list, i, in.at("files"));
        if (!file_in) return std::unexpected(std::move(file_in.error()));
        auto role = file_in->string_at("role");
        if (!role) return std::unexpected(std::move(role.error()));
        auto remote = json_fields::read_remote_file(*file_in);
        if (!remote) return std::unexpected(std::move(remote.error()));
        const auto known_role = json_fields::find_named(json_fields::kRoleNames, *role);
        if (!known_role) continue;
        if (entry.find(*known_role) != nullptr) return std::unexpected(malformed(file_in->at("role")));
        entry.files.push_back(PayloadFile{*known_role, std::move(*remote)});
    }
    if (entry.find(PayloadRole::ClientDll) == nullptr) return std::unexpected(malformed(in.at("files")));
    return entry;
}

// nullopt for a kind or platform this build cannot name.
[[nodiscard]] Result<std::optional<RuntimeEntry>> read_runtime(const Reader& in) {
    RuntimeEntry entry;
    auto id = in.nonempty_string_at("id");
    if (!id) return std::unexpected(std::move(id.error()));
    entry.id = std::move(*id);
    auto kind = in.string_at("kind");
    if (!kind) return std::unexpected(std::move(kind.error()));
    auto version = in.nonempty_string_at("version");
    if (!version) return std::unexpected(std::move(version.error()));
    entry.version = std::move(*version);

    auto archive_value = in.required("archive");
    if (!archive_value) return std::unexpected(std::move(archive_value.error()));
    auto archive_in = Reader::of(**archive_value, in.at("archive"));
    if (!archive_in) return std::unexpected(std::move(archive_in.error()));
    auto archive = json_fields::read_remote_file(*archive_in);
    if (!archive) return std::unexpected(std::move(archive.error()));
    entry.archive = std::move(*archive);

    bool unknown_platform = false;
    if (const json::value* platform_value = in.find("platform")) {
        auto platform_in = Reader::of(*platform_value, in.at("platform"));
        if (!platform_in) return std::unexpected(std::move(platform_in.error()));
        auto platform = json_fields::read_platform(*platform_in);
        if (!platform) return std::unexpected(std::move(platform.error()));
        unknown_platform = !platform->has_value();
        entry.platform = *platform;
    }

    const auto known_kind = json_fields::find_named(json_fields::kRuntimeKindNames, *kind);
    if (!known_kind) return std::optional<RuntimeEntry>{};
    entry.kind = *known_kind;
    const bool has_platform = entry.platform.has_value() || unknown_platform;
    if ((entry.kind == RuntimeKind::VcRedist) == has_platform) return std::unexpected(malformed(in.at("platform")));
    if (unknown_platform) return std::optional<RuntimeEntry>{};
    return std::optional<RuntimeEntry>(std::move(entry));
}

[[nodiscard]] Result<EndpointOverride> read_endpoint(const Reader& in) {
    auto host = in.nonempty_string_at("host");
    if (!host) return std::unexpected(std::move(host.error()));
    auto port = in.u64_at("port");
    if (!port) return std::unexpected(std::move(port.error()));
    if (*port == 0 || *port > std::numeric_limits<u16>::max()) return std::unexpected(malformed(in.at("port")));
    return EndpointOverride{std::move(*host), Port{static_cast<u16>(*port)}};
}

}  // namespace

Result<ReleaseManifest> parse_release_manifest(std::span<const u8> body) {
    boost::system::error_code error;
    const json::value document =
        json::parse(std::string_view(reinterpret_cast<const char*>(body.data()), body.size()), error);
    if (error) return std::unexpected(malformed("manifest"));
    auto root = Reader::of(document, "");
    if (!root) return std::unexpected(malformed("manifest"));

    ReleaseManifest manifest;
    auto schema = root->u64_at("schema");
    if (!schema) return std::unexpected(std::move(schema.error()));
    if (*schema != VersionStreams::manifest_schema)
        return make_diag(ErrorDomain::Components, kManifestSchemaUnsupported)
            .arg("schema", *schema)
            .kind(ErrorKind::Unsupported)
            .fail();
    manifest.schema = VersionStreams::manifest_schema;

    auto serial = root->u64_at("serial");
    if (!serial) return std::unexpected(std::move(serial.error()));
    manifest.serial = *serial;
    auto expires_at = root->unix_ms_at("expires_unix_ms");
    if (!expires_at) return std::unexpected(std::move(expires_at.error()));
    manifest.expires_at = *expires_at;

    auto apps = root->optional_array("apps");
    if (!apps) return std::unexpected(std::move(apps.error()));
    for (std::size_t i = 0; i < (*apps)->size(); ++i) {
        auto in = element(**apps, i, "apps");
        if (!in) return std::unexpected(std::move(in.error()));
        auto app = read_app(*in);
        if (!app) return std::unexpected(std::move(app.error()));
        if (!*app) continue;
        for (const AppEntry& other : manifest.apps)
            if (other.platform == (*app)->platform && other.channel == (*app)->channel)
                return std::unexpected(malformed(in->path()));
        manifest.apps.push_back(std::move(**app));
    }

    auto payloads = root->optional_array("payloads");
    if (!payloads) return std::unexpected(std::move(payloads.error()));
    for (std::size_t i = 0; i < (*payloads)->size(); ++i) {
        auto in = element(**payloads, i, "payloads");
        if (!in) return std::unexpected(std::move(in.error()));
        auto payload = read_payload(*in);
        if (!payload) return std::unexpected(std::move(payload.error()));
        for (const PayloadEntry& other : manifest.payloads)
            if (other.version == payload->version) return std::unexpected(malformed(in->at("version")));
        manifest.payloads.push_back(std::move(*payload));
    }

    auto runtimes = root->optional_array("runtimes");
    if (!runtimes) return std::unexpected(std::move(runtimes.error()));
    std::vector<std::string> runtime_ids;
    for (std::size_t i = 0; i < (*runtimes)->size(); ++i) {
        auto in = element(**runtimes, i, "runtimes");
        if (!in) return std::unexpected(std::move(in.error()));
        auto runtime = read_runtime(*in);
        if (!runtime) return std::unexpected(std::move(runtime.error()));
        // Ids are unique across the manifest, including entries this build skips.
        auto id = in->string_at("id");
        for (const std::string& other : runtime_ids)
            if (other == *id) return std::unexpected(malformed(in->at("id")));
        runtime_ids.push_back(std::move(*id));
        if (*runtime) manifest.runtimes.push_back(std::move(**runtime));
    }

    if (const json::value* endpoint_value = root->find("endpoint")) {
        auto in = Reader::of(*endpoint_value, "endpoint");
        if (!in) return std::unexpected(std::move(in.error()));
        auto endpoint = read_endpoint(*in);
        if (!endpoint) return std::unexpected(std::move(endpoint.error()));
        manifest.endpoint = std::move(*endpoint);
    }
    return manifest;
}

}  // namespace reboot::components
