#include <catch2/catch_test_macros.hpp>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "reboot/process/line_reader.hpp"

using namespace rb;
using namespace rb::process;

namespace {

struct Line {
    std::string text;
    bool continued = false;

    bool operator==(const Line&) const = default;
};

struct Collector {
    std::vector<Line> lines;
    LineReader reader{[this](std::string_view text, bool continued) { lines.push_back({std::string(text), continued}); }};

    void feed(std::string_view chunk) {
        reader.feed(std::span(reinterpret_cast<const u8*>(chunk.data()), chunk.size()));
    }
};

std::vector<std::string> texts(const std::vector<Line>& lines) {
    std::vector<std::string> out;
    for (const Line& line : lines) out.push_back(line.text);
    return out;
}

constexpr std::string_view kReplacement = "\xEF\xBF\xBD";

}  // namespace

TEST_CASE("LF, CR and CRLF each end one line without the terminator", "[process][line_reader]") {
    Collector c;
    c.feed("a\nb\rc\r\nd\n");
    CHECK(texts(c.lines) == std::vector<std::string>{"a", "b", "c", "d"});
}

TEST_CASE("a CRLF straddling two chunks ends one line", "[process][line_reader]") {
    Collector c;
    c.feed("first\r");
    c.feed("\nsecond\n");
    CHECK(texts(c.lines) == std::vector<std::string>{"first", "second"});
}

TEST_CASE("consecutive terminators keep the empty lines between them", "[process][line_reader]") {
    Collector c;
    c.feed("a\n\n\r\rb\n");
    CHECK(texts(c.lines) == std::vector<std::string>{"a", "", "", "", "b"});
}

TEST_CASE("a UTF-8 sequence split across chunks is decoded whole", "[process][line_reader]") {
    Collector c;
    const std::string euro = "\xE2\x82\xAC";
    c.feed(euro.substr(0, 1));
    c.feed(euro.substr(1, 1));
    c.feed(euro.substr(2) + "\n");
    CHECK(texts(c.lines) == std::vector<std::string>{euro});
}

TEST_CASE("each invalid sequence becomes one U+FFFD", "[process][line_reader]") {
    Collector c;
    // A lone continuation byte, a truncated sequence before ASCII, an overlong lead and a surrogate.
    c.feed("\x80" "x\xE2\x82y\xC0z\xED\xA0\x80\n");
    const std::string r(kReplacement);
    CHECK(texts(c.lines) == std::vector<std::string>{r + "x" + r + "y" + r + "z" + r + r + r});
}

TEST_CASE("a truncated sequence before a terminator is replaced, then the line ends", "[process][line_reader]") {
    Collector c;
    c.feed("ab\xF0\x9F\n");
    CHECK(texts(c.lines) == std::vector<std::string>{"ab" + std::string(kReplacement)});
}

TEST_CASE("a line longer than 64 KiB is cut and marked continued", "[process][line_reader]") {
    Collector c;
    c.feed(std::string(LineReader::kMaxLineBytes + 10, 'x') + "\n");
    REQUIRE(c.lines.size() == 2);
    CHECK(c.lines[0].text.size() == LineReader::kMaxLineBytes);
    CHECK(c.lines[0].continued);
    CHECK(c.lines[1].text == std::string(10, 'x'));
    CHECK_FALSE(c.lines[1].continued);
}

TEST_CASE("a line of exactly 64 KiB is not cut", "[process][line_reader]") {
    Collector c;
    c.feed(std::string(LineReader::kMaxLineBytes, 'x') + "\n");
    REQUIRE(c.lines.size() == 1);
    CHECK_FALSE(c.lines[0].continued);
}

TEST_CASE("the cut falls on a code point boundary", "[process][line_reader]") {
    Collector c;
    const std::string euro = "\xE2\x82\xAC";
    c.feed(std::string(LineReader::kMaxLineBytes - 1, 'x') + euro + "\n");
    REQUIRE(c.lines.size() == 2);
    CHECK(c.lines[0].text.size() == LineReader::kMaxLineBytes - 1);
    CHECK(c.lines[0].continued);
    CHECK(c.lines[1].text == euro);
}

TEST_CASE("finish flushes the unterminated tail", "[process][line_reader]") {
    Collector c;
    c.feed("done\nrest");
    c.reader.finish();
    CHECK(texts(c.lines) == std::vector<std::string>{"done", "rest"});
}

TEST_CASE("finish adds no empty line after a final terminator", "[process][line_reader]") {
    Collector c;
    c.feed("only\r\n");
    c.reader.finish();
    CHECK(texts(c.lines) == std::vector<std::string>{"only"});
}

TEST_CASE("finish replaces a pending partial sequence", "[process][line_reader]") {
    Collector c;
    c.feed("\xE2\x82");
    c.reader.finish();
    CHECK(texts(c.lines) == std::vector<std::string>{std::string(kReplacement)});
}

TEST_CASE("finish resets a pending CR", "[process][line_reader]") {
    Collector c;
    c.feed("a\r");
    c.reader.finish();
    c.feed("\nb\n");
    CHECK(texts(c.lines) == std::vector<std::string>{"a", "", "b"});
}
