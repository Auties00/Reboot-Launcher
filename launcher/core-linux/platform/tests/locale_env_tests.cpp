#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "locale_env.hpp"
#include "reboot/contracts/ipc.hpp"
#include "reboot/os_linux/platform/locale_env_language_source.hpp"

using namespace rb::os_linux::platform;
using rb::contracts::ipc::EnvVar;

using Tags = std::vector<std::string>;
using Env = std::vector<EnvVar>;

namespace {

// The engine's own locale variables for one test, restored after.
class EngineLocale {
public:
    explicit EngineLocale(const char* lang) {
        for (const char* name : {"LANGUAGE", "LC_ALL", "LC_MESSAGES", "LANG"}) {
            const char* old = std::getenv(name);
            saved_.emplace_back(name, old != nullptr ? std::optional<std::string>(old) : std::nullopt);
            ::unsetenv(name);
        }
        ::setenv("LANG", lang, 1);
    }
    ~EngineLocale() {
        for (const auto& [name, value] : saved_) {
            if (value)
                ::setenv(name, value->c_str(), 1);
            else
                ::unsetenv(name);
        }
    }
    EngineLocale(const EngineLocale&) = delete;
    EngineLocale& operator=(const EngineLocale&) = delete;

private:
    std::vector<std::pair<const char*, std::optional<std::string>>> saved_;
};

}  // namespace

TEST_CASE("the messages locale is the first of LC_ALL, LC_MESSAGES and LANG that is set", "[locale]") {
    CHECK(preferred_languages({.lang = "de_DE.UTF-8"}) == Tags{"de_DE.UTF-8"});
    CHECK(preferred_languages({.lc_messages = "fr_FR.UTF-8", .lang = "de_DE.UTF-8"}) == Tags{"fr_FR.UTF-8"});
    CHECK(preferred_languages({.lc_all = "it_IT", .lc_messages = "fr_FR", .lang = "de_DE"}) == Tags{"it_IT"});
    CHECK(preferred_languages({.lc_all = "", .lang = "de_DE"}) == Tags{"de_DE"});
    CHECK(preferred_languages({}).empty());
}

TEST_CASE("LANGUAGE's list comes first, without C entries or repeats", "[locale]") {
    CHECK(preferred_languages({.language = "pt_BR:fr::C:pt_BR", .lang = "de_DE.UTF-8"}) ==
          Tags{"pt_BR", "fr", "de_DE.UTF-8"});
    CHECK(preferred_languages({.language = "de_DE.UTF-8", .lang = "de_DE.UTF-8"}) == Tags{"de_DE.UTF-8"});
}

TEST_CASE("a client's display_env gives its locale variables, the first of each name", "[locale]") {
    const Env env{{"DISPLAY", ":0"},          {"LANG", "de_DE.UTF-8"}, {"LANGUAGE", "fr:de"},
                  {"LANG", "it_IT.UTF-8"},    {"LC_MESSAGES", "pt_BR"}, {"LC_TIME", "en_GB"}};
    const std::optional<LocaleVariables> found = locale_variables_in(std::span<const EnvVar>(env));
    REQUIRE(found);
    CHECK(found->lang == "de_DE.UTF-8");
    CHECK(found->language == "fr:de");
    CHECK(found->lc_messages == "pt_BR");
    CHECK_FALSE(found->lc_all);
    CHECK(preferred_languages(*found) == Tags{"fr", "de", "pt_BR"});

    const Env without{{"DISPLAY", ":0"}, {"LC_TIME", "en_GB"}, {"LANG", ""}};
    CHECK_FALSE(locale_variables_in(std::span<const EnvVar>(without)));
}

TEST_CASE("a client's session language wins over the engine's own", "[locale]") {
    // The user manager's environment, as a systemd-started engine sees it.
    const EngineLocale engine("C.UTF-8");
    LocaleEnvLanguageSource source;
    CHECK(source.preferred_ui_languages().empty());

    const Env session{{"WAYLAND_DISPLAY", "wayland-0"}, {"LANG", "de_DE.UTF-8"}, {"LANGUAGE", "de:en"}};
    CHECK(source.caller_ui_languages(session) == Tags{"de", "en", "de_DE.UTF-8"});

    const Env c_session{{"LANG", "C"}};
    CHECK(source.caller_ui_languages(c_session).empty());

    // Without locale variables the engine's own answer stands.
    const EngineLocale italian("it_IT.UTF-8");
    CHECK(source.caller_ui_languages(Env{{"DISPLAY", ":0"}}) == Tags{"it_IT.UTF-8"});
}

TEST_CASE("a C or POSIX messages locale gives no language, LANGUAGE included", "[locale]") {
    CHECK(preferred_languages({.language = "de", .lang = "C"}).empty());
    CHECK(preferred_languages({.language = "de", .lc_all = "C.UTF-8", .lang = "fr_FR"}).empty());
    CHECK(preferred_languages({.lang = "POSIX"}).empty());
}
