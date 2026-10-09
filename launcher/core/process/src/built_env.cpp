#include "reboot/process/built_env.hpp"

#include <algorithm>
#include <utility>

#include "reboot/foundation/text.hpp"
#include "wipe.hpp"

namespace rb::process {

namespace {

[[nodiscard]] char upper_ascii(char c) { return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c; }

}  // namespace

BuiltEnv::BuiltEnv(BuiltEnv&& other) noexcept
    : syntax_(other.syntax_),
      vars_(std::move(other.vars_)),
      sensitive_(std::move(other.sensitive_)),
      denied_(std::move(other.denied_)) {
    other.vars_.vars.clear();
}

BuiltEnv& BuiltEnv::operator=(BuiltEnv&& other) noexcept {
    if (this != &other) {
        wipe();
        syntax_ = other.syntax_;
        vars_ = std::move(other.vars_);
        sensitive_ = std::move(other.sensitive_);
        denied_ = std::move(other.denied_);
        other.vars_.vars.clear();
    }
    return *this;
}

BuiltEnv::~BuiltEnv() { wipe(); }

void BuiltEnv::wipe() noexcept {
    for (auto& [name, value] : vars_.vars) wipe_string(value);
    vars_.vars.clear();
}

bool BuiltEnv::sensitive(std::string_view name) const noexcept {
    return std::ranges::any_of(sensitive_, [&](const std::string& secret) {
        return EnvNamePattern{secret}.matches(name, syntax_);
    });
}

SecretBytes BuiltEnv::windows_block() const {
    // Sized up front so no reallocation leaves secret bytes behind; UTF-16 never needs more
    // units than UTF-8 has bytes.
    std::size_t units = 2;
    std::size_t longest = 0;
    for (const auto& [name, value] : vars_.vars) {
        units += name.size() + value.size() + 2;
        longest = std::max(longest, name.size() + value.size() + 1);
    }
    std::vector<u8> block;
    block.reserve(units * 2);
    const auto append = [&block](char16_t unit) {
        block.push_back(static_cast<u8>(unit & 0xFF));
        block.push_back(static_cast<u8>(unit >> 8));
    };
    // A POSIX-syntax block is sorted case-sensitively; Windows wants case-insensitive order.
    std::vector<const std::pair<std::string, std::string>*> ordered;
    ordered.reserve(vars_.vars.size());
    for (const auto& var : vars_.vars) ordered.push_back(&var);
    std::ranges::stable_sort(ordered, [](const auto* a, const auto* b) {
        return std::ranges::lexicographical_compare(a->first, b->first, [](char x, char y) {
            return upper_ascii(x) < upper_ascii(y);
        });
    });
    std::string entry;
    entry.reserve(longest);
    for (const auto* var : ordered) {
        const auto& [name, value] = *var;
        entry.assign(name).append(1, '=').append(value);
        std::u16string wide = utf8_to_utf16(entry);
        for (const char16_t unit : wide) append(unit);
        append(u'\0');
        secure_wipe(wide.data(), wide.size() * sizeof(char16_t));
    }
    wipe_string(entry);
    // An empty block still needs its double NUL.
    if (vars_.vars.empty()) append(u'\0');
    append(u'\0');
    return SecretBytes(std::move(block));
}

ports::EnvBlock BuiltEnv::copy() const { return vars_; }

}  // namespace rb::process
