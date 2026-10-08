#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "reboot/foundation/types.hpp"

namespace reboot::front {

struct FormField {
    std::string name;
    std::string value;
};

// Wipes every name and value when it goes out of scope, since the fields carry passwords.
class FormFields {
public:
    FormFields() = default;
    FormFields(const FormFields&) = delete;
    FormFields& operator=(const FormFields&) = delete;
    ~FormFields();

    std::vector<FormField> fields;
};

// application/x-www-form-urlencoded; false for a malformed escape.
[[nodiscard]] bool parse_form(std::span<const u8> body, FormFields& out);

void append_form_field(std::vector<u8>& out, std::string_view name, std::string_view value);

}  // namespace reboot::front
