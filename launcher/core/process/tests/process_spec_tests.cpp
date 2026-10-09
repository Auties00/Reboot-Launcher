#include <catch2/catch_test_macros.hpp>
#include <string>
#include <utility>
#include <vector>

#include "reboot/process/env_builder.hpp"
#include "reboot/process/log_string.hpp"
#include "reboot/process/process_spec.hpp"
#include "reboot/process/restart_policy.hpp"
#include "reboot/process/windows_command_line.hpp"

using namespace rb;
using namespace rb::process;

namespace {

std::string quote(std::vector<std::string> argv) { return quote_windows_args(argv); }

NativePath absolute(std::string_view leaf) {
#ifdef _WIN32
    return NativePath("C:\\game") / leaf;
#else
    return NativePath("/game") / leaf;
#endif
}

ProcessSpec spec_with(std::vector<std::string> args) {
    ProcessSpec spec;
    spec.role = ChildRole::Backend;
    spec.exe = absolute("reboot-backend.exe");
    spec.args = std::move(args);
    return spec;
}

}  // namespace

TEST_CASE("plain arguments are joined unquoted", "[process][command_line]") {
    CHECK(quote({"C:\\g\\game.exe", "-epicapp=Fortnite", "-skippatchcheck"}) ==
          "C:\\g\\game.exe -epicapp=Fortnite -skippatchcheck");
}

TEST_CASE("the program name is quoted without escapes", "[process][command_line]") {
    CHECK(quote({"C:\\Program Files\\g\\game.exe"}) == "\"C:\\Program Files\\g\\game.exe\"");
}

TEST_CASE("spaces, quotes and backslashes follow the CRT rules", "[process][command_line]") {
    CHECK(quote({"p", "a b"}) == "p \"a b\"");
    CHECK(quote({"p", ""}) == "p \"\"");
    CHECK(quote({"p", "say \"hi\""}) == "p \"say \\\"hi\\\"\"");
    CHECK(quote({"p", "C:\\dir with space\\"}) == "p \"C:\\dir with space\\\\\"");
    CHECK(quote({"p", "C:\\no\\space"}) == "p C:\\no\\space");
}

TEST_CASE("a -KEY=value argument quotes only its value", "[process][command_line]") {
    CHECK(quote({"p", "-AUTH_LOGIN=a b"}) == "p -AUTH_LOGIN=\"a b\"");
    CHECK(quote({"p", "-path=C:\\x y\\"}) == "p -path=\"C:\\x y\\\\\"");
}

TEST_CASE("the log string masks -AUTH_PASSWORD in any case", "[process][spec]") {
    const ProcessSpec spec = spec_with({"-AUTH_LOGIN=me", "-auth_password=s3cret value", "-AUTH_PASSWORD=x"});
    const std::string logged = to_log_string(spec);
    CHECK(logged.find("s3cret") == std::string::npos);
    CHECK(logged.find("-auth_password=***") != std::string::npos);
    CHECK(logged.find("-AUTH_PASSWORD=***") != std::string::npos);
    CHECK(logged.find("-AUTH_LOGIN=me") != std::string::npos);
}

TEST_CASE("the working directory defaults to the exe's directory", "[process][spec]") {
    ProcessSpec spec = spec_with({});
    CHECK(spec.working_directory() == spec.exe.parent_path());
    spec.cwd = absolute("data");
    CHECK(spec.working_directory() == absolute("data"));
}

TEST_CASE("validate rejects relative paths and bad arguments", "[process][spec]") {
    CHECK(spec_with({"--control=stdio"}).validate().has_value());

    ProcessSpec relative = spec_with({});
    relative.exe = NativePath("backend.exe");
    CHECK(relative.validate().error().id == "process.spec_relative_path");

    ProcessSpec relative_cwd = spec_with({});
    relative_cwd.cwd = NativePath("data");
    CHECK(relative_cwd.validate().error().id == "process.spec_relative_path");

    CHECK(spec_with({"a", ""}).validate().error().id == "process.spec_empty_argument");
    CHECK(spec_with({std::string("a\0b", 3)}).validate().error().id == "process.spec_invalid_argument");
    CHECK(spec_with({"\xFF"}).validate().error().id == "process.spec_invalid_argument");
}

TEST_CASE("to_launch copies the spec into its own process group", "[process][spec]") {
    EnvBuilder builder(EnvSyntax::Posix);
    builder.channel("REBOOT_BACKEND_DATA", "/data");
    Result<BuiltEnv> env = std::move(builder).build();
    REQUIRE(env.has_value());

    ProcessSpec spec = spec_with({"--control=stdio"});
    spec.env = std::move(*env);
    spec.scope_name = "reboot-session-1";
    const WipingLaunch launch = spec.to_launch();
    CHECK(launch.get().exe == spec.exe);
    CHECK(launch.get().args == spec.args);
    CHECK(launch.get().cwd == spec.exe.parent_path());
    CHECK(launch.get().own_group);
    CHECK(launch.get().stdio == ports::StdioMode::ControlChannel);
    CHECK(launch.get().scope_name == spec.scope_name);
    REQUIRE(launch.get().env.vars.size() == 1);
    CHECK(launch.get().env.vars[0].second == "/data");
}

TEST_CASE("the restart delay doubles up to its cap and the window limits restarts", "[process][restart]") {
    constexpr RestartPolicy policy;
    STATIC_CHECK(policy.delay(0) == std::chrono::seconds{1});
    STATIC_CHECK(policy.delay(4) == std::chrono::seconds{16});
    STATIC_CHECK(policy.delay(9) == std::chrono::seconds{30});
    STATIC_CHECK(policy.allows(4));
    STATIC_CHECK(!policy.allows(5));
}
