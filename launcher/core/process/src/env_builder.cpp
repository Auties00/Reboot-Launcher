#include "reboot/process/env_builder.hpp"

#include <algorithm>
#include <span>
#include <utility>

#include "messages.hpp"
#include "reboot/foundation/text.hpp"
#include "reboot/storage/settings_keys.hpp"
#include "wipe.hpp"

namespace reboot::process {

namespace {

constexpr std::string_view kChannelPrefix = "REBOOT_";

[[nodiscard]] bool valid_name(std::string_view name) {
    return !name.empty() && name.find('=') == std::string_view::npos && name.find('\0') == std::string_view::npos &&
           is_valid_utf8(name);
}

[[nodiscard]] bool valid_value(std::string_view value) {
    return value.find('\0') == std::string_view::npos && is_valid_utf8(value);
}

[[nodiscard]] bool any_matches(std::span<const EnvNamePattern> patterns, std::string_view name, EnvSyntax syntax) {
    return std::ranges::any_of(patterns, [&](const EnvNamePattern& pattern) { return pattern.matches(name, syntax); });
}

[[nodiscard]] bool same_name(std::string_view a, std::string_view b, EnvSyntax syntax) {
    return EnvNamePattern{a}.matches(b, syntax);
}

[[nodiscard]] std::string_view layer_name(EnvLayer layer) {
    switch (layer) {
        case EnvLayer::DaemonBase: return "daemon_base";
        case EnvLayer::ClientAllowList: return "client";
        case EnvLayer::ProfilePassThrough: return "profile";
        case EnvLayer::Runner: return "runner";
        case EnvLayer::Channel: return "channel";
    }
    return "unknown";
}

[[nodiscard]] char upper_ascii(char c) { return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c; }

[[nodiscard]] bool name_less(std::string_view a, std::string_view b, EnvSyntax syntax) {
    if (syntax == EnvSyntax::Posix) return a < b;
    return std::ranges::lexicographical_compare(a, b, [](char x, char y) { return upper_ascii(x) < upper_ascii(y); });
}

}  // namespace

EnvBuilder::EnvBuilder(EnvSyntax syntax) : syntax_(syntax) {}

EnvBuilder::EnvBuilder(EnvBuilder&& other) noexcept
    : syntax_(other.syntax_),
      entries_(std::move(other.entries_)),
      runner_launch_(other.runner_launch_),
      openssl_ia32cap_(other.openssl_ia32cap_),
      proton_log_dir_(std::move(other.proton_log_dir_)) {
    other.entries_.clear();
}

EnvBuilder& EnvBuilder::operator=(EnvBuilder&& other) noexcept {
    if (this != &other) {
        wipe();
        syntax_ = other.syntax_;
        entries_ = std::move(other.entries_);
        runner_launch_ = other.runner_launch_;
        openssl_ia32cap_ = other.openssl_ia32cap_;
        proton_log_dir_ = std::move(other.proton_log_dir_);
        other.entries_.clear();
    }
    return *this;
}

EnvBuilder::~EnvBuilder() { wipe(); }

void EnvBuilder::wipe() noexcept {
    for (Entry& entry : entries_) wipe_string(entry.value);
    entries_.clear();
}

EnvBuilder EnvBuilder::fork() const {
    EnvBuilder copy(syntax_);
    copy.entries_ = entries_;
    copy.runner_launch_ = runner_launch_;
    copy.openssl_ia32cap_ = openssl_ia32cap_;
    copy.proton_log_dir_ = proton_log_dir_;
    return copy;
}

void EnvBuilder::set(EnvLayer layer, std::string_view name, std::string value, bool sensitive) {
    for (Entry& entry : entries_) {
        if (entry.layer != layer || !same_name(entry.name, name, syntax_)) continue;
        wipe_string(entry.value);
        entry.value = std::move(value);
        entry.sensitive = sensitive;
        return;
    }
    entries_.push_back(Entry{layer, std::string(name), std::move(value), sensitive});
}

EnvBuilder& EnvBuilder::daemon_base(const ports::EnvBlock& user_environment) {
    for (const auto& [name, value] : user_environment.vars) {
        if (!valid_name(name) || !valid_value(value)) continue;
        if (syntax_ == EnvSyntax::Posix && !any_matches(kPosixBaseNames, name, syntax_)) continue;
        set(EnvLayer::DaemonBase, name, value, false);
    }
    return *this;
}

EnvBuilder& EnvBuilder::client(const contracts::ipc::CallerContext& caller) {
    for (const contracts::ipc::EnvVar& var : caller.display_env)
        if (any_matches(kClientAllowList, var.name, syntax_)) set(EnvLayer::ClientAllowList, var.name, var.value, false);
    return *this;
}

EnvBuilder& EnvBuilder::profile(const ports::EnvBlock& pass_through) {
    for (const auto& [name, value] : pass_through.vars)
        if (any_matches(kProfilePassThrough, name, syntax_)) set(EnvLayer::ProfilePassThrough, name, value, false);
    return *this;
}

EnvBuilder& EnvBuilder::runner(const ports::EnvBlock& vars) {
    runner_launch_ = true;
    for (const auto& [name, value] : vars.vars) set(EnvLayer::Runner, name, value, false);
    return *this;
}

EnvBuilder& EnvBuilder::channel(std::string_view name, std::string value) {
    set(EnvLayer::Channel, name, std::move(value), false);
    return *this;
}

EnvBuilder& EnvBuilder::channel_secret(std::string_view name, SecretString value) {
    set(EnvLayer::Channel, name, value.reveal(), true);
    return *this;
}

EnvBuilder& EnvBuilder::openssl_ia32cap(bool opted_in) {
    openssl_ia32cap_ = opted_in;
    return *this;
}

EnvBuilder& EnvBuilder::proton_log(const NativePath& log_dir) {
    proton_log_dir_ = log_dir;
    return *this;
}

EnvBuilder& EnvBuilder::play_settings(const storage::PlaySettings& play, const NativePath& wine_log_dir) {
    // Validated when set, so a failure only means a value this build cannot read; it adds nothing.
    if (Result<ports::EnvBlock> pass_through = storage::parse_env_lines(play.env)) profile(*pass_through);
    if (play.verbose_wine_log) proton_log(wine_log_dir);
    return *this;
}

Result<BuiltEnv> EnvBuilder::build() && {
    for (const Entry& entry : entries_) {
        if (entry.layer == EnvLayer::DaemonBase) continue;
        if (!valid_name(entry.name))
            return make_diag(ErrorDomain::Process, msg::kEnvInvalidName)
                .arg("name", entry.name)
                .arg("layer", layer_name(entry.layer))
                .kind(ErrorKind::InvalidInput)
                .fail();
        if (!valid_value(entry.value))
            return make_diag(ErrorDomain::Process, msg::kEnvInvalidValue)
                .arg("name", entry.name)
                .kind(ErrorKind::InvalidInput)
                .fail();
        if (entry.layer != EnvLayer::Channel) continue;
        const bool allowed = runner_launch_ ? any_matches(kWineChannelNames, entry.name, syntax_)
                                            : EnvNamePattern{kChannelPrefix, true}.matches(entry.name, syntax_);
        if (!allowed)
            return make_diag(ErrorDomain::Process, msg::kEnvChannelName)
                .arg("name", entry.name)
                .kind(ErrorKind::InvalidInput)
                .fail();
    }

    // Owned names never come from an input; the profile's WINEDEBUG is the one exception.
    const auto owned = [&](const Entry& entry) {
        if (same_name(entry.name, kWineDebugName, syntax_))
            return !runner_launch_ || entry.layer != EnvLayer::ProfilePassThrough;
        return same_name(entry.name, kOpensslIa32capName, syntax_) || same_name(entry.name, kProtonLogName, syntax_) ||
               same_name(entry.name, kProtonLogDirName, syntax_);
    };

    // entries_ is in call order; a stable sort by layer lets the later layer win below.
    std::vector<Entry*> ordered;
    ordered.reserve(entries_.size());
    for (Entry& entry : entries_)
        if (!owned(entry)) ordered.push_back(&entry);
    std::ranges::stable_sort(ordered, {}, [](const Entry* entry) { return entry->layer; });

    std::vector<Entry*> merged;
    for (Entry* entry : ordered) {
        const auto same =
            std::ranges::find_if(merged, [&](const Entry* m) { return same_name(m->name, entry->name, syntax_); });
        if (same == merged.end()) merged.push_back(entry);
        else *same = entry;
    }

    BuiltEnv env;
    env.syntax_ = syntax_;
    bool profile_wine_debug = false;
    for (Entry* merged_entry : merged) {
        Entry& entry = *merged_entry;
        const auto denied = std::ranges::find_if(kDenyList, [&](const EnvDenyRule& rule) {
            return rule.name.matches(entry.name, syntax_) && rule.exempt != entry.layer;
        });
        if (denied != kDenyList.end()) {
            env.denied_.push_back(entry.name);
            continue;
        }
        if (same_name(entry.name, kWineDebugName, syntax_)) profile_wine_debug = true;
        if (entry.sensitive) env.sensitive_.push_back(entry.name);
        env.vars_.vars.emplace_back(entry.name, std::move(entry.value));
    }

    if (openssl_ia32cap_) env.vars_.vars.emplace_back(kOpensslIa32capName, kOpensslIa32capValue);
    if (runner_launch_) {
        if (!profile_wine_debug) env.vars_.vars.emplace_back(kWineDebugName, kWineDebugDefault);
        if (proton_log_dir_) {
            const std::u8string dir = proton_log_dir_->u8string();
            std::string value(dir.begin(), dir.end());
            if (!valid_value(value))
                return make_diag(ErrorDomain::Process, msg::kEnvInvalidValue)
                    .arg("name", kProtonLogDirName)
                    .kind(ErrorKind::InvalidInput)
                    .fail();
            env.vars_.vars.emplace_back(kProtonLogName, "1");
            env.vars_.vars.emplace_back(kProtonLogDirName, std::move(value));
        }
    }

    std::ranges::sort(env.vars_.vars, [&](const auto& a, const auto& b) { return name_less(a.first, b.first, syntax_); });
    wipe();
    return env;
}

}  // namespace reboot::process
