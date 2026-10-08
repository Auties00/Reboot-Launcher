#include "reboot/compat/path_mapper.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <system_error>
#include <utility>

#include "messages.hpp"
#include "reboot/foundation/text.hpp"

namespace reboot::compat {

namespace {

[[nodiscard]] char to_lower_ascii(char c) noexcept { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

[[nodiscard]] Diagnostic not_mapped(const NativePath& host) {
    return make_diag(ErrorDomain::Compat, msg::kPathNotMapped).arg("path", host).kind(ErrorKind::InvalidInput).build();
}

// Path::begin() yields a root name and root directory first; both stay part of the target.
[[nodiscard]] std::size_t component_count(const NativePath& path) {
    return static_cast<std::size_t>(std::distance(path.begin(), path.end()));
}

[[nodiscard]] bool is_within(const NativePath& host, const NativePath& target) {
    return std::mismatch(target.begin(), target.end(), host.begin(), host.end()).first == target.end();
}

[[nodiscard]] bool spellable_on_windows(std::string_view name) {
    if (!is_valid_utf8(name)) return false;
    return std::ranges::none_of(name, [](char c) {
        return static_cast<unsigned char>(c) < 0x20 || std::string_view("\\:*?\"<>|").find(c) != std::string_view::npos;
    });
}

[[nodiscard]] std::string as_utf8(const NativePath& component) {
    const std::u8string text = component.u8string();
    return {text.begin(), text.end()};
}

}  // namespace

PathMapper::PathMapper(std::vector<DosDevice> devices) : devices_(std::move(devices)) {}

Result<PathMapper> PathMapper::load(const NativePath& prefix) {
    const NativePath dosdevices = prefix / "dosdevices";
    std::error_code error;
    std::filesystem::directory_iterator it(dosdevices, error);
    std::vector<DosDevice> devices;
    for (; !error && it != std::filesystem::directory_iterator(); it.increment(error)) {
        const std::string name = as_utf8(it->path().filename());
        if (name.size() != 2 || name[1] != ':') continue;
        const char letter = to_lower_ascii(name[0]);
        if (letter < 'a' || letter > 'z') continue;
        std::error_code resolve_error;
        NativePath target = std::filesystem::canonical(it->path(), resolve_error);
        if (resolve_error) continue;
        devices.push_back({letter, std::move(target)});
    }
    if (error) {
        return make_diag(ErrorDomain::Compat, msg::kDosdevicesUnreadable)
            .arg("path", dosdevices)
            .os({SystemError::Origin::Host, error.value()})
            .fail();
    }
    std::ranges::sort(devices, {}, &DosDevice::letter);
    return PathMapper(std::move(devices));
}

Result<std::u16string> PathMapper::to_windows(const NativePath& host) const {
    if (!host.is_absolute()) return std::unexpected(not_mapped(host));
    const DosDevice* best = nullptr;
    std::size_t best_depth = 0;
    for (const DosDevice& device : devices_) {
        if (!is_within(host, device.target)) continue;
        const std::size_t depth = component_count(device.target);
        if (best == nullptr || depth > best_depth) {
            best = &device;
            best_depth = depth;
        }
    }
    if (best == nullptr) return std::unexpected(not_mapped(host));

    std::u16string windows{static_cast<char16_t>(best->letter - 'a' + 'A'), u':'};
    auto it = host.begin();
    std::advance(it, static_cast<std::ptrdiff_t>(best_depth));
    if (it == host.end()) windows += u'\\';
    for (; it != host.end(); ++it) {
        const std::string name = as_utf8(*it);
        if (name.empty()) continue;
        if (!spellable_on_windows(name)) {
            return make_diag(ErrorDomain::Compat, msg::kPathNotUtf8).arg("path", host).kind(ErrorKind::InvalidInput).fail();
        }
        windows += u'\\';
        windows += utf8_to_utf16(name);
    }
    return windows;
}

}  // namespace reboot::compat
