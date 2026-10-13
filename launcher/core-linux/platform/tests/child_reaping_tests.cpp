#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cerrno>
#include <chrono>
#include <signal.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "child_reaping.hpp"

using rb::u32;
using namespace rb::os_linux::platform;

namespace {

[[nodiscard]] pid_t exiting_child(int code) {
    const pid_t pid = ::fork();
    REQUIRE(pid >= 0);
    if (pid == 0) ::_exit(code);
    return pid;
}

// Until `pid` is a zombie, without reaping it.
void await_zombie(pid_t pid) {
    for (int attempt = 0; attempt < 1000; ++attempt) {
        siginfo_t info{};
        REQUIRE(::waitid(P_PID, static_cast<id_t>(pid), &info, WEXITED | WNOHANG | WNOWAIT) == 0);
        if (info.si_pid == pid) return;
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    FAIL("the child did not exit");
}

}  // namespace

TEST_CASE("children files list space-separated decimal pids", "[child_reaping]") {
    CHECK(parse_child_pids("").empty());
    CHECK(parse_child_pids("\n").empty());
    CHECK(parse_child_pids("412 9 ") == std::vector<u32>{412, 9});
    CHECK(parse_child_pids("77\n") == std::vector<u32>{77});
    CHECK(parse_child_pids("5 x 6") == std::vector<u32>{5});
}

TEST_CASE("reap_zombies reaps unlisted zombies and leaves listed ones to their waiters", "[child_reaping]") {
    if (::access("/proc/thread-self/children", F_OK) != 0)
        SKIP("this kernel has no children files, so reap_zombies reaps every zombie");
    pid_t listed = 0;
    {
        const auto held = ChildTable::lock();
        listed = exiting_child(7);
        ChildTable::add(static_cast<u32>(listed));
    }
    const pid_t unlisted = exiting_child(9);
    await_zombie(listed);
    await_zombie(unlisted);

    reap_zombies();

    siginfo_t info{};
    CHECK(::waitid(P_PID, static_cast<id_t>(unlisted), &info, WEXITED | WNOHANG) == -1);
    CHECK(errno == ECHILD);
    // Still a zombie, so its pid and group id stay reserved for the group kill.
    const PeekResult peeked = peek_listed_child(static_cast<u32>(listed));
    CHECK(peeked.zombie);
    CHECK(peeked.waited.outcome == WaitOutcome::Ended);
    CHECK(peeked.waited.status.code == CLD_EXITED);
    CHECK(peeked.waited.status.status == 7);
    const WaitResult reaped = reap_listed_child(static_cast<u32>(listed));
    CHECK(reaped.outcome == WaitOutcome::Ended);
    CHECK(reaped.status.status == 7);
}
