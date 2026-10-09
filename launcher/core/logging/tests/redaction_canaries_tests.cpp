#include <boost/json.hpp>
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>

#include "reboot/foundation/log.hpp"
#include "reboot/logging/log_format.hpp"

using namespace rb;
using namespace rb::logging;

namespace {

boost::json::object load_canaries() {
    std::ifstream in(REBOOT_LOGGING_TEST_DATA "/redaction_canaries.json", std::ios::binary);
    REQUIRE(in);
    const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    return boost::json::parse(text).as_object();
}

std::span<const u8> bytes_of(std::string_view text) {
    return {reinterpret_cast<const u8*>(text.data()), text.size()};
}

// Replaces each {name} with that secret's canary.
std::string expand(std::string_view context, const boost::json::object& secrets) {
    std::string out(context);
    for (const auto& [name, canary] : secrets) {
        const std::string placeholder = "{" + std::string(name) + "}";
        for (std::size_t at = out.find(placeholder); at != std::string::npos; at = out.find(placeholder, at))
            out.replace(at, placeholder.size(), canary.as_string());
    }
    return out;
}

// What a sink writes: the redacted record, formatted.
std::string logged(const Redactor& redactor, std::string_view text) {
    LogRecord record;
    record.text = redactor.apply(text);
    return format_log_line(record);
}

}  // namespace

TEST_CASE("every registered secret has a context in the golden", "[logging][redaction]") {
    const boost::json::object canaries = load_canaries();
    const auto& secrets = canaries.at("registered_secrets").as_object();
    for (const auto& [name, canary] : secrets) {
        const std::string placeholder = "{" + std::string(name) + "}";
        bool used = false;
        for (const auto& context : canaries.at("contexts").as_array())
            if (std::string_view(context.as_string()).find(placeholder) != std::string_view::npos) used = true;
        CHECK(used);
        CHECK(canary.as_string().size() >= 4);
    }
}

TEST_CASE("no canary survives into a logged line", "[logging][redaction]") {
    const boost::json::object canaries = load_canaries();
    const auto& secrets = canaries.at("registered_secrets").as_object();
    Redactor redactor;
    for (const auto& [name, canary] : secrets) redactor.add_secret(bytes_of(canary.as_string()));

    for (const auto& context : canaries.at("contexts").as_array()) {
        const std::string line = logged(redactor, expand(context.as_string(), secrets));
        for (const auto& [name, canary] : secrets) CHECK(line.find(canary.as_string()) == std::string::npos);
    }
    for (const auto& arg : canaries.at("free_text_args").as_array()) {
        const std::string line = logged(redactor, arg.at("input").as_string());
        CHECK(line.find(arg.at("masked_value").as_string()) == std::string::npos);
    }
}

TEST_CASE("public constants are never masked", "[logging][redaction]") {
    const boost::json::object canaries = load_canaries();
    Redactor redactor;
    for (const auto& [name, canary] : canaries.at("registered_secrets").as_object())
        redactor.add_secret(bytes_of(canary.as_string()));
    for (const auto& constant : canaries.at("public_constants_kept").as_array()) {
        const std::string text = "-AUTH_TYPE=epic " + std::string(constant.as_string());
        CHECK(logged(redactor, text).find(constant.as_string()) != std::string::npos);
    }
}
