#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>
#include <vector>

#include "reboot/compat/path_mapper.hpp"
#include "reboot/foundation/text.hpp"
#include "reboot/play/game_arg.hpp"
#include "reboot/play/launch_args.hpp"
#include "reboot/testing/fake_platform_paths.hpp"

using namespace reboot;
using namespace reboot::play;

namespace {

std::vector<GameArg> parsed(std::string_view text) {
    auto args = parse_custom_args(text);
    REQUIRE(args);
    return std::move(*args);
}

CustomArgsIssue failed(std::string_view text) {
    auto args = parse_custom_args(text);
    REQUIRE_FALSE(args);
    return args.error();
}

identity::LoginPlan login(identity::AuthType type = identity::AuthType::Epic) {
    identity::LoginPlan plan;
    plan.auth_login = "Player-abc123@projectreboot.dev";
    plan.auth_type = type;
    return plan;
}

LaunchArgs built(std::vector<GameArg> custom = {}, std::string_view culture = "system") {
    Result<LaunchArgs> args = build_launch_args(login(), SecretString{std::string("hunter2-credential")}, culture, std::move(custom));
    REQUIRE(args);
    return std::move(*args);
}

const GameArg* find(const LaunchArgs& args, std::string_view key) {
    for (const GameArg& arg : args.args())
        if (iequals_ascii(arg.key, key)) return &arg;
    return nullptr;
}

}  // namespace

TEST_CASE("a game argument writes its value after an equals sign, or the bare key", "[play][args]") {
    CHECK(GameArg{"-nobe", std::nullopt, false}.text() == "-nobe");
    CHECK(GameArg{"-epicportal", std::string(), false}.text() == "-epicportal");
    CHECK(GameArg{"-epicapp", std::string("Fortnite"), false}.text() == "-epicapp=Fortnite");
}

TEST_CASE("custom arguments split on whitespace and at the first equals sign", "[play][args]") {
    const std::vector<GameArg> args = parsed("  -log -map=Athena=Terrain\t-NOSPLASH\n");
    REQUIRE(args.size() == 3);
    CHECK(args[0] == GameArg{"-log", std::nullopt, false});
    CHECK(args[1] == GameArg{"-map", std::string("Athena=Terrain"), false});
    CHECK(args[2] == GameArg{"-NOSPLASH", std::nullopt, false});
}

TEST_CASE("quotes group spaces and keep Windows backslashes", "[play][args]") {
    const std::vector<GameArg> args = parsed(R"(-path="C:\Program Files\Fortnite" -name='a "b"' -quote=\"x\" -k="")");
    REQUIRE(args.size() == 4);
    CHECK(args[0].value == std::string(R"(C:\Program Files\Fortnite)"));
    CHECK(args[1].value == std::string(R"(a "b")"));
    CHECK(args[2].value == std::string(R"("x")"));
    CHECK(args[3] == GameArg{"-k", std::string(), false});
}

TEST_CASE("an empty string or an empty quoted token never becomes an argument", "[play][args]") {
    CHECK(parsed("").empty());
    CHECK(parsed("   \t ").empty());
    CHECK(parsed(R"("" '')").empty());
}

TEST_CASE("an unclosed quote is refused at the token that opens it", "[play][args]") {
    const CustomArgsIssue issue = failed(R"(-ok -path="C:\games)");
    CHECK(issue.error == CustomArgsError::UnbalancedQuote);
    CHECK(issue.offset == 4);
    const Diagnostic diag = to_diagnostic(issue);
    CHECK(diag.id == "play.custom_args_unbalanced_quote");
    CHECK(diag.kind == ErrorKind::InvalidInput);
}

TEST_CASE("NUL or invalid UTF-8 is refused", "[play][args]") {
    const std::string with_nul{"-a -b\0c", 7};
    CustomArgsIssue issue = failed(with_nul);
    CHECK(issue.error == CustomArgsError::InvalidText);
    CHECK(issue.offset == 3);
    issue = failed("-x=\xC3\x28");
    CHECK(issue.error == CustomArgsError::InvalidText);
    CHECK(to_diagnostic(issue).id == "play.custom_args_invalid");
}

TEST_CASE("an -AUTH_ key in any case is reserved for the engine", "[play][args]") {
    for (const std::string_view text : {"-AUTH_LOGIN=me", "-auth_password=x", "-Auth_Type=epic"}) {
        const CustomArgsIssue issue = failed(std::string("-log ") + std::string(text));
        CHECK(issue.error == CustomArgsError::ReservedKey);
        CHECK(issue.offset == 5);
        const Diagnostic diag = to_diagnostic(issue);
        CHECK(diag.id == "play.custom_args_reserved");
        REQUIRE(diag.find_arg("key") != nullptr);
        CHECK(std::get<std::string>(*diag.find_arg("key")) == text.substr(0, text.find('=')));
    }
}

TEST_CASE("the launch starts with the fixed tokens, then the credential, in order", "[play][args]") {
    const LaunchArgs args = built();
    const std::vector<std::string> argv = args.argv();
    REQUIRE(argv.size() == kFixedGameArgs.size() + 3);
    CHECK(argv[0] == "-epicapp=Fortnite");
    CHECK(argv[1] == "-epicenv=Prod");
    CHECK(argv[2] == "-epiclocale=en-us");
    CHECK(argv[3] == "-epicportal");
    CHECK(argv[4] == "-skippatchcheck");
    CHECK(argv[5] == "-nobe");
    CHECK(argv[6] == "-fromfl=eac");
    CHECK(argv[7] == "-fltoken=3db3ba5dcbd2e16703f3978d");
    CHECK(argv[8] == "-caldera=" + std::string(kCalderaToken));
    CHECK(argv[9] == "-AUTH_LOGIN=Player-abc123@projectreboot.dev");
    CHECK(argv[10] == "-AUTH_PASSWORD=hunter2-credential");
    CHECK(argv[11] == "-AUTH_TYPE=epic");
    CHECK(find(args, "-caldera")->secret);
    CHECK(find(args, "-AUTH_PASSWORD")->secret);
}

TEST_CASE("the game culture replaces en-us unless it is the system language", "[play][args]") {
    CHECK(find(built({}, "de"), "-epiclocale")->value == std::string("de"));
    CHECK(find(built({}, "system"), "-epiclocale")->value == std::string("en-us"));
    CHECK(built({}, "fr").argv()[2] == "-epiclocale=fr");
}

TEST_CASE("exchange-code builds take -AUTH_TYPE=exchangecode", "[play][args]") {
    Result<LaunchArgs> args = build_launch_args(login(identity::AuthType::ExchangeCode),
                                                SecretString{std::string("code-1234")}, "system", {});
    REQUIRE(args);
    CHECK(args->argv().back() == "-AUTH_TYPE=exchangecode");
}

TEST_CASE("a custom argument replaces a fixed token in place and others append", "[play][args]") {
    const LaunchArgs args = built(parsed("-EPICENV=Staging -log -nobe= -log=2"));
    const std::vector<std::string> argv = args.argv();
    REQUIRE(argv.size() == kFixedGameArgs.size() + 4);
    CHECK(argv[1] == "-epicenv=Staging");
    CHECK(argv[5] == "-nobe");
    CHECK(argv.back() == "-log=2");
}

TEST_CASE("a custom -caldera stays masked", "[play][args]") {
    const LaunchArgs args = built(parsed("-caldera=mine"));
    const GameArg* caldera = find(args, "-caldera");
    REQUIRE(caldera != nullptr);
    CHECK(caldera->value == std::string("mine"));
    CHECK(caldera->secret);
    CHECK(to_log_string(args).find("mine") == std::string::npos);
}

TEST_CASE("building refuses an empty credential and a reserved custom key", "[play][args]") {
    Result<LaunchArgs> empty = build_launch_args(login(), SecretString{}, "system", {});
    REQUIRE_FALSE(empty);
    CHECK(empty.error().id == "internal.bug");

    std::vector<GameArg> custom{GameArg{"-auth_login", std::string("x"), false}};
    Result<LaunchArgs> reserved = build_launch_args(login(), SecretString{std::string("pw-value")}, "system", custom);
    REQUIRE_FALSE(reserved);
    CHECK(reserved.error().id == "play.custom_args_reserved");
}

TEST_CASE("the log line masks every secret value and quotes spaces", "[play][args]") {
    const LaunchArgs args = built(parsed(R"(-path="C:\a b")"));
    const std::string line = to_log_string(args);
    CHECK(line.find("hunter2-credential") == std::string::npos);
    CHECK(line.find(std::string(kCalderaToken)) == std::string::npos);
    CHECK(line.find("-AUTH_PASSWORD=***") != std::string::npos);
    CHECK(line.find("-caldera=***") != std::string::npos);
    CHECK(line.find("-AUTH_LOGIN=Player-abc123@projectreboot.dev") != std::string::npos);
    CHECK(line.find(R"(-path="C:\a b")") != std::string::npos);
    CHECK(line.find("-epicportal ") != std::string::npos);
}

TEST_CASE("moving launch arguments leaves the source empty", "[play][args]") {
    LaunchArgs first = built();
    LaunchArgs second = std::move(first);
    CHECK(first.args().empty());
    CHECK(second.args().size() == kFixedGameArgs.size() + 3);
    LaunchArgs third;
    third = std::move(second);
    CHECK(second.args().empty());
    CHECK(third.argv()[10] == "-AUTH_PASSWORD=hunter2-credential");
}

TEST_CASE("Wine maps absolute host paths in values and leaves the rest", "[play][args]") {
    const NativePath root = testing::default_fake_root();
    const NativePath games = (root / "games").lexically_normal();
    const compat::PathMapper mapper({compat::DosDevice{'d', games}});
    const std::u8string inside = (games / "maps" / "athena.umap").u8string();
    const std::u8string outside = (root / "elsewhere" / "x.txt").u8string();
    std::vector<GameArg> args{
        GameArg{"-map", std::string(inside.begin(), inside.end()), false},
        GameArg{"-other", std::string(outside.begin(), outside.end()), false},
        GameArg{"-relative", std::string("maps/x"), false},
        GameArg{"-flag", std::nullopt, false},
    };
    map_host_paths(args, mapper);
    CHECK(args[0].value == std::string(R"(D:\maps\athena.umap)"));
    CHECK(args[1].value == std::string(outside.begin(), outside.end()));
    CHECK(args[2].value == std::string("maps/x"));
    CHECK_FALSE(args[3].value);
}
