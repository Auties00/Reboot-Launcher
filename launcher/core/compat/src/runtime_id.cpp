#include "reboot/compat/runtime_id.hpp"

#include <algorithm>
#include <cstddef>

namespace rb::compat {

namespace {

[[nodiscard]] bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }

[[nodiscard]] std::size_t run_end(std::string_view text, std::size_t from, bool digits) noexcept {
    while (from < text.size() && is_digit(text[from]) == digits) ++from;
    return from;
}

[[nodiscard]] std::strong_ordering compare_numbers(std::string_view a, std::string_view b) noexcept {
    a.remove_prefix(std::min(a.find_first_not_of('0'), a.size()));
    b.remove_prefix(std::min(b.find_first_not_of('0'), b.size()));
    if (a.size() != b.size()) return a.size() <=> b.size();
    return a.compare(b) <=> 0;
}

}  // namespace

std::strong_ordering compare_runtime_versions(std::string_view a, std::string_view b) noexcept {
    std::size_t i = 0;
    std::size_t j = 0;
    while (i < a.size() && j < b.size()) {
        const bool digits = is_digit(a[i]);
        if (digits != is_digit(b[j])) return digits ? std::strong_ordering::less : std::strong_ordering::greater;
        const std::size_t a_end = run_end(a, i, digits);
        const std::size_t b_end = run_end(b, j, digits);
        const std::string_view a_run = a.substr(i, a_end - i);
        const std::string_view b_run = b.substr(j, b_end - j);
        const std::strong_ordering order = digits ? compare_numbers(a_run, b_run) : a_run.compare(b_run) <=> 0;
        if (order != 0) return order;
        i = a_end;
        j = b_end;
    }
    if (i < a.size() || j < b.size()) return (a.size() - i) <=> (b.size() - j);
    // Equal up to leading zeros; the byte order keeps the ordering strong.
    return a.compare(b) <=> 0;
}

}  // namespace rb::compat
