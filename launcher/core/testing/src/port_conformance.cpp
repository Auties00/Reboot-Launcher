#include "reboot/testing/port_conformance.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "messages.hpp"
#include "reboot/foundation/random.hpp"
#include "reboot/testing/port_binder.hpp"

namespace reboot::testing {
namespace {

[[nodiscard]] bool is_normal_absolute(const NativePath& path) {
    return path.is_absolute() && path == path.lexically_normal();
}

[[nodiscard]] bool is_within(const NativePath& path, const NativePath& dir) {
    const NativePath relative = path.lexically_normal().lexically_relative(dir.lexically_normal());
    return !relative.empty() && *relative.begin() != "..";
}

[[nodiscard]] std::string utf8_of(const NativePath& path) {
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

[[nodiscard]] bool same_context(const ports::CallerContext& a, const ports::CallerContext& b) {
    return a.os_session == b.os_session && a.elevated == b.elevated && a.interactive == b.interactive &&
           a.display_env == b.display_env;
}

}  // namespace

ConformanceReport run_platform_paths_conformance(const ports::IPlatformPaths& paths) {
    ConformanceReport report("platform_paths");
    const NativePath data = paths.default_data_root();
    report.expect("data root is absolute and normal", is_normal_absolute(data));
    report.expect("cache root is absolute and normal", is_normal_absolute(paths.default_cache_root()));
    report.expect("logs root is absolute and normal", is_normal_absolute(paths.default_logs_root()));
    report.expect("ipc runtime base is absolute and normal", is_normal_absolute(paths.ipc_runtime_base()));
    report.expect("exe dir is absolute and normal", is_normal_absolute(paths.exe_dir()));
    report.expect("data root differs from the exe dir", data != paths.exe_dir());
    const std::optional<NativePath> velopack = paths.velopack_package_dir();
    report.expect("velopack package dir is set exactly for Velopack installs",
                  velopack.has_value() == (paths.install_kind() == ports::InstallKind::Velopack));
    if (velopack) report.expect("data root is outside the Velopack package dir", !is_within(data, *velopack));
    return report;
}

ConformanceReport run_secret_store_conformance(ports::ISecretStore& store, IRandom& random) {
    ConformanceReport report("secret_store");
    const std::string key = "reboot-conformance-" + random_token_hex(random, 8);
    const std::vector<u8> first{0x00, 0x01, 0x00, 0xFF, 0x7F};
    const std::vector<u8> second{0xFF, 0x00, 0x00};

    if (store.kind() == ports::SecretStoreKind::Unavailable) {
        report.expect("put fails when unavailable", !store.put(key, first).has_value());
        report.expect("get fails when unavailable", !store.get(key).has_value());
        report.expect("erase fails when unavailable", !store.erase(key).has_value());
        return report;
    }

    if (report.expect_ok("put", store.put(key, first))) {
        const auto read = store.get(key);
        report.expect("get returns the binary value", read && *read && (*read)->reveal() == first);
        report.expect_ok("overwrite", store.put(key, second));
        const auto again = store.get(key);
        report.expect("get returns the overwritten value", again && *again && (*again)->reveal() == second);
    }
    report.expect_ok("erase", store.erase(key));
    const auto gone = store.get(key);
    report.expect("get after erase finds nothing", gone && !gone->has_value());
    return report;
}

ConformanceReport run_caller_context_conformance(ports::ICallerContextProbe& probe, u32 own_pid) {
    ConformanceReport report("caller_context");
    const ports::CallerContext first = probe.capture();
    report.expect("capture is stable", same_context(first, probe.capture()));
    std::set<std::string> names;
    bool unique = true;
    for (const auto& [name, value] : first.display_env) unique = names.insert(name).second && unique;
    report.expect("display variable names are unique", unique);
    probe.allow_foreground(own_pid);
    report.expect("allow_foreground of our own pid is harmless", same_context(first, probe.capture()));
    return report;
}

ConformanceReport run_port_inspector_conformance(ports::IPortInspector& inspector, IPortBinder& binder) {
    ConformanceReport report("port_inspector");
    if (const auto tcp = binder.bind_tcp(); report.expect_ok("bind tcp", tcp)) {
        const auto owner = inspector.tcp_owner(*tcp);
        report.expect("a bound tcp port is owned by its binder",
                      owner && *owner && (*owner)->pid == binder.owner_pid());
        binder.release(tcp->port);
        const auto released = inspector.tcp_owner(*tcp);
        report.expect("a released tcp port has no owner", released && !released->has_value());
    }
    if (const auto udp = binder.bind_udp(); report.expect_ok("bind udp", udp)) {
        const auto owner = inspector.udp_owner(*udp);
        report.expect("a bound udp port is owned by its binder",
                      owner && *owner && (*owner)->pid == binder.owner_pid());
        binder.release(*udp);
        const auto released = inspector.udp_owner(*udp);
        report.expect("a released udp port has no owner", released && !released->has_value());
    }
    return report;
}

ConformanceReport run_loopback_peer_inspector_conformance(ports::ILoopbackPeerInspector& inspector,
                                                          IPortBinder& binder, std::optional<u32> expected_uid) {
    ConformanceReport report("loopback_peer_inspector");
    const auto pair = binder.connect_loopback();
    if (!report.expect_ok("connect a loopback pair", pair)) return report;
    const auto uid = inspector.peer_uid(pair->first, pair->second);
    if (expected_uid) {
        report.expect("the peer has the expected uid", uid && *uid == expected_uid);
    } else if (report.expect_error("an unsupported inspector fails", uid, ErrorKind::Unsupported)) {
        report.expect("it fails with platform.not_supported", uid.error().is(msg::kNotSupported));
    }
    return report;
}

ConformanceReport run_runner_platform_conformance(ports::IRunnerPlatform& runner,
                                                  const RunnerConformanceSubject& subject) {
    ConformanceReport report("runner_platform");
    const std::vector<ports::RunnerKind> kinds = runner.supported();
    report.expect("the kind is supported", std::ranges::contains(kinds, subject.kind));
    const ports::RuntimeDirs& dirs = subject.runtime;
    const auto layout = runner.layout(subject.kind, dirs);
    if (!report.expect_ok("layout", layout)) return report;
    report.expect("the layout root lies inside the runtime dir",
                  layout->root == dirs.runtime || is_within(layout->root, dirs.runtime));
    const bool entry_inside =
        is_within(layout->entry, dirs.runtime) || (dirs.launcher && is_within(layout->entry, *dirs.launcher));
    report.expect("the entry lies inside a runtime dir", entry_inside);

    const auto launch = runner.runner_launch(*layout, subject.prefix, subject.winhost_exe, {});
    if (report.expect_ok("runner_launch", launch)) {
        report.expect("it runs the layout's entry", launch->exe == layout->entry);
        report.expect("winhost is its program", std::ranges::contains(launch->args, utf8_of(subject.winhost_exe)));
        const auto prefix = std::ranges::find(launch->env.vars, std::string("WINEPREFIX"),
                                              &std::pair<std::string, std::string>::first);
        report.expect("WINEPREFIX names the prefix",
                      prefix != launch->env.vars.end() && prefix->second == utf8_of(subject.prefix));
    }
    std::vector<NativePath> extracted{dirs.runtime};
    if (dirs.launcher) extracted.push_back(*dirs.launcher);
    for (const NativePath& dir : extracted) {
        report.expect_ok("post_extract", runner.post_extract(dir));
        report.expect_ok("post_extract is idempotent", runner.post_extract(dir));
    }
    return report;
}

ConformanceReport run_disk_info_conformance(ports::IDiskInfo& disk, const ConformanceEnv& env) {
    ConformanceReport report("disk_info");
    const auto volumes = disk.volumes();
    if (report.expect_ok("volumes", volumes)) report.expect("volumes is not empty", !volumes->empty());
    const auto scratch = disk.volume_of(env.scratch);
    if (!report.expect_ok("volume_of the scratch dir", scratch) || !volumes) return report;
    report.expect("volume_of names a listed volume",
                  std::ranges::contains(*volumes, scratch->mount, &ports::VolumeInfo::mount));
    report.expect("free bytes never exceed total bytes", scratch->free_bytes <= scratch->total_bytes);
    return report;
}

ConformanceReport run_integration_registrar_conformance(ports::IIntegrationRegistrar& registrar, const NativePath& exe,
                                                        std::span<const ports::IntegrationKind> kinds) {
    ConformanceReport report("integration_registrar");
    for (const ports::IntegrationKind kind : kinds) {
        const std::string name = std::to_string(static_cast<int>(kind));
        if (!report.expect_ok("apply " + name, registrar.apply(kind, exe))) continue;
        const auto applied = registrar.status(kind);
        report.expect("apply makes " + name + " ours", applied && applied->state == ports::IntegrationState::Ours);
        const auto again = registrar.status(kind);
        report.expect("status of " + name + " is stable", applied && again && applied->state == again->state);
        report.expect_ok("remove " + name, registrar.remove(kind));
        const auto removed = registrar.status(kind);
        report.expect("remove makes " + name + " absent",
                      removed && removed->state == ports::IntegrationState::Absent);
        report.expect_ok("remove " + name + " again", registrar.remove(kind));
    }
    return report;
}

ConformanceReport run_prerequisite_probe_conformance(ports::IPrerequisiteProbe& probe) {
    ConformanceReport report("prerequisite_probe");
    const std::vector<ports::PrerequisiteStatus> statuses = probe.check();
    std::set<std::string> ids;
    bool named = true;
    bool unique = true;
    bool remediable = true;
    for (const ports::PrerequisiteStatus& status : statuses) {
        named = named && !status.id.empty();
        unique = ids.insert(status.id).second && unique;
        remediable = remediable && (status.met || status.remediation_message_id.has_value());
    }
    report.expect("ids are not empty", named);
    report.expect("ids are unique", unique);
    report.expect("every unmet prerequisite names a remediation", remediable);
    report.expect("an unknown id fails remediate", !probe.remediate("reboot.conformance.unknown").has_value());
    return report;
}

ConformanceReport run_system_info_conformance(const ports::ISystemInfo& system) {
    ConformanceReport report("system_info");
    const ports::OsInfo os = system.os();
    report.expect("os facts are not empty", !os.name.empty() && !os.version.empty() && !os.arch.empty());
    const ports::OsInfo again = system.os();
    report.expect("os facts are stable", os.name == again.name && os.version == again.version &&
                                             os.build == again.build && os.arch == again.arch);
    report.expect("the os session is not empty", !system.os_session().empty());
    const std::optional<NativePath> bundle = system.ca_bundle();
    report.expect("a named CA bundle is absolute", !bundle || bundle->is_absolute());
    return report;
}

ConformanceReport run_random_conformance(IRandom& random) {
    ConformanceReport report("random");
    std::array<u8, 32> first{};
    std::array<u8, 32> second{};
    random.fill(first);
    random.fill(second);
    report.expect("consecutive fills differ", first != second);
    report.expect("a fill is not all zero", std::ranges::any_of(first, [](u8 byte) { return byte != 0; }));
    random.fill(std::span<u8>());
    report.expect("an empty fill is fine", true);
    return report;
}

}  // namespace reboot::testing
