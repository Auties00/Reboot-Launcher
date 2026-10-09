#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "reboot/testing/child_misbehaviour.hpp"
#include "reboot/testing/fake_game_main.hpp"
#include "reboot/testing/fake_game_script.hpp"

#ifdef _WIN32
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>

#include "peer_process.hpp"
#include "raw_stdio.hpp"
#include "reboot/contracts/game_client.hpp"
#include "reboot/foundation/text.hpp"
#include "reboot/testing/fake_client_dll.hpp"
#include "reboot/testing/game_control_bootstrap.hpp"
#include "win_modules.hpp"
#endif

namespace rb::testing {

#ifdef _WIN32
namespace {

constexpr std::string_view kScriptFlag = "--script=";
constexpr std::chrono::milliseconds kPoll{10};
constexpr std::chrono::milliseconds kCloseLinger{200};

void print_line(std::string_view line) {
    std::string text(line);
    text.push_back('\n');
    (void)raw_stdio::write_all(1, reinterpret_cast<const unsigned char*>(text.data()), static_cast<unsigned long>(text.size()));
}

[[nodiscard]] std::wstring wide(std::string_view utf8) {
    const std::u16string text = utf8_to_utf16(utf8);
    return {text.begin(), text.end()};
}

// Our DLL erases its bootstrap once read, so nothing else in the game sees the token.
void erase_bootstrap() {
    namespace gc = contracts::game_client;
    for (const std::string_view name : {gc::kEnvCtl, gc::kEnvCtlToken, gc::kEnvSession, gc::kEnvRole})
        (void)_putenv_s(std::string(name).c_str(), "");
}

}  // namespace

int fake_game_main(int argc, char** argv) {
    raw_stdio::make_binary();
    std::optional<NativePath> script_file;
    std::vector<std::string> game_args;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (i == 1 && arg.starts_with(kScriptFlag)) {
            script_file = NativePath(std::string(arg.substr(kScriptFlag.size())));
            continue;
        }
        game_args.emplace_back(arg);
    }
    for (const std::string& arg : game_args) print_line(arg);

    if (!script_file && argc > 0) {
        NativePath beside(argv[0]);
        beside += ".script.json";
        std::error_code error;
        if (std::filesystem::is_regular_file(beside, error)) script_file = std::move(beside);
    }
    FakeGameScript script;
    if (script_file) {
        auto loaded = load_fake_game_script(*script_file);
        if (!loaded) {
            report_bad_script(loaded.error());
            return kBadInvocationExitCode;
        }
        script = std::move(*loaded);
    }

    // Reported once our DLL is welcomed, ahead of the scripted steps.
    std::vector<ClientDllStep> hook_failures;
    for (const std::string& name : script.expect_modules)
        if (!win_modules::loaded(wide(name).c_str())) hook_failures.emplace_back(contracts::game_client::HookFailed{name, true});
    for (const std::string& name : script.load_dlls)
        if (!win_modules::load(wide(name).c_str())) hook_failures.emplace_back(contracts::game_client::HookFailed{name, false});
    auto& steps = script.client_dll.after_welcome;
    steps.insert(steps.begin(), hook_failures.begin(), hook_failures.end());

    Strand strand;
    SystemClock clock;
    boost::asio::io_context io;
    auto work = boost::asio::make_work_guard(io);
    std::thread([&io] { io.run(); }).detach();
    std::thread([&strand] { strand.run(); }).detach();

    std::unique_ptr<FakeClientDll> dll;
    if (script.act_as_client_dll) {
        auto bootstrap = read_own_game_control_bootstrap();
        erase_bootstrap();
        if (bootstrap) {
            dll = std::make_unique<FakeClientDll>(strand, clock, script.client_dll);
            if (auto connected = dll->connect(io, *bootstrap); !connected) {
                report_bad_script(connected.error());
                dll.reset();
            }
        } else {
            report_bad_script(bootstrap.error());
        }
    }

    std::thread([lines = script.output_lines, interval = script.output_interval] {
        for (const std::string& line : lines) {
            print_line(line);
            if (interval > std::chrono::milliseconds::zero()) std::this_thread::sleep_for(interval);
        }
    }).detach();

    const auto began = std::chrono::steady_clock::now();
    for (;;) {
        if (script.exit_after && std::chrono::steady_clock::now() - began >= *script.exit_after) break;
        // GcShutdown ends the game through our DLL's disconnect; without either, only the Job ends it.
        if (dll && dll->closed()) {
            // The disconnect only queued the close behind our last frames, e.g. GcShutdown's reply.
            std::this_thread::sleep_for(kCloseLinger);
            break;
        }
        std::this_thread::sleep_for(kPoll);
    }
    std::fflush(stdout);
    std::_Exit(script.exit_code);
}

#else

int fake_game_main(int, char**) {
    std::fprintf(stderr, "reboot-fake-game stands in for a Windows game; build it on Windows\n");
    return kBadInvocationExitCode;
}

#endif

}  // namespace rb::testing
