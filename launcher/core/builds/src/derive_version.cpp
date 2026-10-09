#include "reboot/builds/detect_version.hpp"

#include <charconv>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include "reboot/builds/cl_table.hpp"

namespace rb::builds {

namespace {

constexpr std::string_view kClSuffix = "-CL-";

// Removes a trailing "-CL-<n>" from `tail` and returns n.
[[nodiscard]] std::optional<Changelist> take_cl_suffix(std::string_view& tail) {
    const std::size_t at = tail.rfind(kClSuffix);
    if (at == std::string_view::npos) return std::nullopt;
    const std::string_view digits = tail.substr(at + kClSuffix.size());
    if (digits.empty()) return std::nullopt;
    u32 value = 0;
    const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
    if (error != std::errc{} || end != digits.data() + digits.size()) return std::nullopt;
    tail = tail.substr(0, at);
    return Changelist{value};
}

[[nodiscard]] std::optional<GameVersion> named_version(std::string_view text, const catalog::AliasTable& aliases) {
    if (const auto match = aliases.resolve(text)) return match->version;
    if (const auto parsed = GameVersion::parse(text)) return *parsed;
    return std::nullopt;
}

}  // namespace

std::optional<DetectedVersion> derive_version(const ReleaseMarker& marker, VersionSource source,
                                              const DetectionTables& tables) {
    std::string_view tail = marker.tail;
    std::optional<Changelist> cl = take_cl_suffix(tail);
    if (!cl) cl = marker.engine_cl;

    if (tail == "Cert" || tail == "Next") {
        if (!marker.engine_cl) return std::nullopt;
        const std::optional<GameVersion> version = tables.cl_table.lookup(*marker.engine_cl);
        if (!version) return std::nullopt;
        return DetectedVersion{
            .version = *version, .cl = cl, .source = VersionSource::ClTable, .raw = marker.tail, .file = std::nullopt};
    }

    const std::optional<GameVersion> version = named_version(tail, tables.aliases);
    if (!version) return std::nullopt;
    return DetectedVersion{.version = *version, .cl = cl, .source = source, .raw = marker.tail, .file = std::nullopt};
}

}  // namespace rb::builds
