#include "message_template.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

#include "ascii.hpp"

namespace reboot::ux {

namespace {

constexpr int kMaxDepth = 16;

constexpr std::array<std::string_view, 6> kPluralKeywords{"zero", "one", "two", "few", "many", "other"};

class TemplateParser {
public:
    explicit TemplateParser(std::string_view text) : text_(text) {}

    std::expected<TemplateInfo, std::string> run() {
        if (!message(0, '\0')) return std::unexpected(std::move(error_));
        return std::move(info_);
    }

private:
    [[nodiscard]] bool at_end() const { return pos_ >= text_.size(); }
    [[nodiscard]] char peek(std::size_t ahead = 0) const {
        return pos_ + ahead < text_.size() ? text_[pos_ + ahead] : '\0';
    }

    bool fail(std::string_view what) {
        error_ = std::string(what) + " at offset " + std::to_string(pos_);
        return false;
    }

    void skip_space() {
        while (!at_end() && (peek() == ' ' || peek() == '\t' || peek() == '\n' || peek() == '\r')) ++pos_;
    }

    std::string_view read_while(bool (*accept)(char)) {
        const std::size_t start = pos_;
        while (!at_end() && accept(peek())) ++pos_;
        return text_.substr(start, pos_ - start);
    }

    bool expect(char c) {
        skip_space();
        if (peek() != c) return fail(std::string("expected '") + c + "'");
        ++pos_;
        return true;
    }

    void add_arg(std::string_view name) {
        if (std::ranges::find(info_.args, name) == info_.args.end()) info_.args.emplace_back(name);
    }

    // Stops before a '}' that closes an enclosing argument, or at the end of the text at depth 0.
    // `quotable` is the extra character an apostrophe quotes: '#' directly inside a plural, '|' inside a choice.
    bool message(int depth, char quotable) {
        while (!at_end()) {
            const char c = peek();
            if (c == '\'') {
                if (!apostrophe(quotable)) return false;
            } else if (c == '{') {
                if (!argument(depth + 1)) return false;
            } else if (c == '}') {
                if (depth == 0) return fail("unmatched '}'");
                return true;
            } else {
                ++pos_;
            }
        }
        if (depth > 0) return fail("unclosed '{'");
        return true;
    }

    bool apostrophe(char quotable) {
        const char next = peek(1);
        if (next == '\'') {
            pos_ += 2;
            return true;
        }
        if (next != '{' && next != '}' && (quotable == '\0' || next != quotable)) {
            ++pos_;
            return true;
        }
        ++pos_;
        while (!at_end()) {
            if (peek() == '\'') {
                if (peek(1) == '\'') {
                    pos_ += 2;
                    continue;
                }
                ++pos_;
                return true;
            }
            ++pos_;
        }
        return fail("unterminated quote");
    }

    bool argument(int depth) {
        if (depth > kMaxDepth) return fail("arguments nested too deep");
        ++pos_;
        skip_space();
        const std::string_view name = read_while([](char c) { return is_alnum(c) || c == '_'; });
        if (name.empty()) return fail("missing argument name");
        add_arg(name);
        skip_space();
        if (peek() == '}') {
            ++pos_;
            return true;
        }
        if (peek() != ',') return fail("expected ',' or '}' after argument name");
        ++pos_;
        skip_space();
        const std::string_view type = read_while([](char c) { return is_alpha(c); });
        if (type == "plural" || type == "selectordinal") {
            if (type == "selectordinal") info_.unsupported.push_back(std::string(name) + ": selectordinal");
            return expect(',') && plural_style(name, depth);
        }
        if (type == "select") return expect(',') && select_style(depth);
        if (type == "choice") {
            info_.unsupported.push_back(std::string(name) + ": choice");
            return expect(',') && skip_style(true);
        }
        if (type == "number" || type == "date" || type == "time" || type == "spellout" || type == "ordinal" ||
            type == "duration") {
            skip_space();
            if (peek() == '}') {
                ++pos_;
                return true;
            }
            return expect(',') && skip_style(false);
        }
        return fail(type.empty() ? "missing argument type" : "unknown argument type '" + std::string(type) + "'");
    }

    // An argStyle: text up to the '}' that closes the argument, braces balanced and quotes honoured.
    // In a simple style (number, date, ...) every apostrophe quotes up to the next one, as in ICU.
    bool skip_style(bool choice) {
        int open = 0;
        while (!at_end()) {
            const char c = peek();
            if (c == '\'' && choice) {
                if (!apostrophe('|')) return false;
                continue;
            }
            if (c == '\'') {
                const std::size_t close = text_.find('\'', pos_ + 1);
                if (close == std::string_view::npos) return fail("unterminated quote");
                pos_ = close + 1;
                continue;
            }
            ++pos_;
            if (c == '{') ++open;
            if (c == '}' && open-- == 0) return true;
        }
        return fail("unclosed '{'");
    }

    bool plural_style(std::string_view name, int depth) {
        skip_space();
        if (text_.substr(pos_).starts_with("offset:")) {
            info_.unsupported.push_back(std::string(name) + ": offset");
            pos_ += 7;
            skip_space();
            if (read_while([](char c) { return is_digit(c); }).empty()) return fail("missing plural offset");
        }
        std::vector<std::string> selectors;
        while (true) {
            skip_space();
            if (at_end()) return fail("unclosed '{'");
            if (peek() == '}') break;
            std::string selector;
            if (peek() == '=') {
                ++pos_;
                const std::string_view value = read_while([](char c) { return is_digit(c) || c == '.'; });
                if (value.empty()) return fail("missing explicit plural value");
                selector = "=" + std::string(value);
            } else {
                selector = std::string(read_while([](char c) { return is_alpha(c); }));
                if (std::ranges::find(kPluralKeywords, selector) == kPluralKeywords.end())
                    return fail(selector.empty() ? "missing plural selector" : "unknown plural keyword '" + selector + "'");
            }
            if (std::ranges::find(selectors, selector) != selectors.end())
                return fail("duplicate plural selector '" + selector + "'");
            if (selector != "one" && selector != "other")
                info_.unsupported.push_back(std::string(name) + ": selector " + selector);
            selectors.push_back(selector);
            if (!expect('{') || !message(depth, '#')) return false;
            ++pos_;
        }
        if (std::ranges::find(selectors, std::string_view("other")) == selectors.end()) return fail("plural without 'other'");
        ++pos_;
        return true;
    }

    bool select_style(int depth) {
        std::vector<std::string_view> selectors;
        while (true) {
            skip_space();
            if (at_end()) return fail("unclosed '{'");
            if (peek() == '}') break;
            const std::string_view selector = read_while([](char c) { return is_alnum(c) || c == '_' || c == '-'; });
            if (selector.empty()) return fail("missing select keyword");
            if (std::ranges::find(selectors, selector) != selectors.end())
                return fail("duplicate select keyword '" + std::string(selector) + "'");
            selectors.push_back(selector);
            if (!expect('{') || !message(depth, '\0')) return false;
            ++pos_;
        }
        if (std::ranges::find(selectors, std::string_view("other")) == selectors.end()) return fail("select without 'other'");
        ++pos_;
        return true;
    }

    std::string_view text_;
    std::size_t pos_ = 0;
    TemplateInfo info_;
    std::string error_;
};

}  // namespace

std::expected<TemplateInfo, std::string> parse_template(std::string_view text) { return TemplateParser(text).run(); }

}  // namespace reboot::ux
