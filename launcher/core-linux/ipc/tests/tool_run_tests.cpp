#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "tool_run.hpp"

using rb::os_linux::ipc::run_tool;
using rb::os_linux::ipc::ToolRun;
using namespace std::chrono_literals;

namespace {

using Argv = std::vector<std::string>;

}  // namespace

TEST_CASE("a tool's exit code and stdout come back; stderr does not", "[tool_run]") {
    const std::optional<ToolRun> run = run_tool(Argv{"sh", "-c", "echo LoadState=loaded; echo noise >&2; exit 3"}, 5s);
    REQUIRE(run);
    CHECK(run->exit_code == 3);
    CHECK(run->output == "LoadState=loaded\n");
}

TEST_CASE("the tool reads /dev/null on stdin", "[tool_run]") {
    const std::optional<ToolRun> run = run_tool(Argv{"sh", "-c", "cat; echo done"}, 5s);
    REQUIRE(run);
    CHECK(run->exit_code == 0);
    CHECK(run->output == "done\n");
}

TEST_CASE("a tool that cannot start, dies by a signal or overruns its deadline gives nothing", "[tool_run]") {
    CHECK_FALSE(run_tool(Argv{"reboot-launcher-no-such-tool"}, 5s));
    CHECK_FALSE(run_tool(Argv{}, 5s));
    CHECK_FALSE(run_tool(Argv{"sh", "-c", "kill -9 $$"}, 5s));

    const auto started = std::chrono::steady_clock::now();
    CHECK_FALSE(run_tool(Argv{"sh", "-c", "exec sleep 30"}, 200ms));
    CHECK(std::chrono::steady_clock::now() - started < 10s);
}

TEST_CASE("a tool that closes stdout but keeps running is killed at the deadline", "[tool_run]") {
    const auto started = std::chrono::steady_clock::now();
    CHECK_FALSE(run_tool(Argv{"sh", "-c", "exec >/dev/null; exec sleep 30"}, 200ms));
    CHECK(std::chrono::steady_clock::now() - started < 10s);
}
