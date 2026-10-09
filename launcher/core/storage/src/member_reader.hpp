#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/json/array.hpp>
#include <boost/json/object.hpp>
#include <boost/json/value.hpp>

#include "reboot/foundation/diag.hpp"
#include "reboot/storage/load_report.hpp"

namespace rb::storage {

[[nodiscard]] Diagnostic wrong_type(std::string_view expected);
[[nodiscard]] Diagnostic missing_member(std::string_view member);
[[nodiscard]] Result<const boost::json::object*> object_from_json(const boost::json::value& value);
// storage.out_of_range above 2^32 - 1.
[[nodiscard]] Result<u32> u32_from_json(const boost::json::value& value);

// A member whose absence drops the record that holds it.
template <class Decode>
[[nodiscard]] auto required(const boost::json::object& object, std::string_view name, Decode&& decode)
    -> decltype(decode(std::declval<const boost::json::value&>())) {
    const boost::json::value* raw = object.if_contains(boost::json::string_view(name.data(), name.size()));
    if (raw == nullptr) return std::unexpected(missing_member(name));
    return decode(*raw);
}

// A bad member keeps its default and records a ValueIssue; members never asked for are unknown().
class MemberReader {
public:
    MemberReader(const boost::json::object& values, std::vector<ValueIssue>& issues, std::string prefix = {})
        : values_(values), issues_(issues), prefix_(std::move(prefix)) {}

    // nullptr when absent.
    [[nodiscard]] const boost::json::value* find(std::string_view name);

    template <class T, class Decode>
    void read(std::string_view name, T& out, Decode&& decode) {
        const boost::json::value* raw = find(name);
        if (raw == nullptr) return;
        auto value = decode(*raw);
        if (value) out = std::move(*value);
        else issue(name, std::move(value.error()));
    }

    // JSON null and an absent member both read as unset.
    template <class T, class Decode>
    void read_optional(std::string_view name, std::optional<T>& out, Decode&& decode) {
        const boost::json::value* raw = find(name);
        if (raw == nullptr || raw->is_null()) return;
        auto value = decode(*raw);
        if (value) out = std::move(*value);
        else issue(name, std::move(value.error()));
    }

    // `decode(raw, path)` returns Result<T>; a failed element is dropped with a ValueIssue.
    template <class T, class Decode>
    void read_list(std::string_view name, std::vector<T>& out, Decode&& decode) {
        const boost::json::value* raw = find(name);
        if (raw == nullptr) return;
        const boost::json::array* array = raw->if_array();
        if (array == nullptr) {
            issue(name, wrong_type("array"));
            return;
        }
        out.clear();
        for (std::size_t i = 0; i < array->size(); ++i) {
            const std::string element = path(name) + "[" + std::to_string(i) + "]";
            auto value = decode((*array)[i], element);
            if (value) out.push_back(std::move(*value));
            else issues_.push_back({element, std::move(value.error())});
        }
    }

    void issue(std::string_view name, Diagnostic reason) { issues_.push_back({path(name), std::move(reason)}); }
    [[nodiscard]] std::string path(std::string_view name) const { return prefix_ + std::string(name); }
    [[nodiscard]] std::vector<ValueIssue>& issues() noexcept { return issues_; }
    [[nodiscard]] boost::json::object unknown() const;

private:
    const boost::json::object& values_;
    std::vector<ValueIssue>& issues_;
    std::string prefix_;
    std::vector<std::string> known_;
};

// required() for a record whose unknown members are kept: the member counts as known.
template <class Decode>
[[nodiscard]] auto required(MemberReader& reader, std::string_view name, Decode&& decode)
    -> decltype(decode(std::declval<const boost::json::value&>())) {
    const boost::json::value* raw = reader.find(name);
    if (raw == nullptr) return std::unexpected(missing_member(name));
    return decode(*raw);
}

// Adds the members of `unknown` that `out` does not already have.
void append_unknown(boost::json::object& out, const boost::json::object& unknown);

}  // namespace rb::storage
