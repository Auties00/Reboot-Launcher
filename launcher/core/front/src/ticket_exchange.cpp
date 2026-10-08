#include "reboot/front/ticket_exchange.hpp"

#include <utility>
#include <vector>

#include "form_body.hpp"
#include "reboot/foundation/sha256.hpp"

namespace reboot::front {

namespace {

[[nodiscard]] std::span<const u8> bytes_of(std::string_view text) noexcept {
    return {reinterpret_cast<const u8*>(text.data()), text.size()};
}

// The single field named `name`; nullptr when it is absent or repeated, so no parser disagrees on it.
[[nodiscard]] const FormField* single(const FormFields& form, std::string_view name) {
    const FormField* found = nullptr;
    for (const FormField& field : form.fields) {
        if (field.name != name) continue;
        if (found) return nullptr;
        found = &field;
    }
    return found;
}

}  // namespace

TicketExchange::TicketExchange(SecretString ticket, UpstreamLogin login, TicketBinding binding)
    : ticket_(std::move(ticket)), login_(std::move(login)), binding_(binding) {}

bool TicketExchange::applies(std::string_view method, std::string_view path) noexcept {
    return method == "POST" && path.substr(0, path.find('?')) == kOauthTokenPath;
}

TicketSwap TicketExchange::swap(std::span<const u8> form, PeerUser peer) {
    FormFields fields;
    if (!parse_form(form, fields)) return {};
    const FormField* grant = single(fields, "grant_type");
    const FormField* password = single(fields, "password");
    if (!grant || grant->value != "password" || !password || ticket_.reveal().empty()) return {};
    if (!constant_time_equal(bytes_of(password->value), bytes_of(ticket_.reveal()))) return {};

    if (peer == PeerUser::Other) return {TicketSwapOutcome::Refused, {}};
    if (binding_ == TicketBinding::Unbound) {
        if (state_ != TicketState::Available) return {TicketSwapOutcome::Refused, {}};
        state_ = TicketState::Reserved;
    }

    // Sized for the worst-case escaping up front, so no reallocation leaves an unwiped copy behind.
    std::vector<u8> body;
    body.reserve(3 * (form.size() + login_.username.size() + login_.password.reveal().size()) + 16);
    bool has_username = false;
    for (const FormField& field : fields.fields) {
        if (field.name == "username") {
            if (has_username) continue;
            has_username = true;
            append_form_field(body, field.name, login_.username);
        } else if (field.name == "password") {
            append_form_field(body, field.name, login_.password.reveal());
        } else {
            append_form_field(body, field.name, field.value);
        }
    }
    if (!has_username) append_form_field(body, "username", login_.username);
    return {TicketSwapOutcome::Swapped, SecretBytes{std::move(body)}};
}

void TicketExchange::settle(std::optional<u32> upstream_status) noexcept {
    if (state_ != TicketState::Reserved) return;
    const bool success = upstream_status && *upstream_status >= 200 && *upstream_status < 300;
    state_ = success ? TicketState::Spent : TicketState::Available;
}

}  // namespace reboot::front
