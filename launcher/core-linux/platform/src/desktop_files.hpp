#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "reboot/foundation/native_path.hpp"

namespace reboot::os_linux::platform {

// The keys of a .desktop file's [Desktop Entry] group this package reads, values unescaped.
struct DesktopEntryKeys {
    std::optional<std::string> exec;
    std::optional<std::string> hidden;
    std::optional<std::string> autostart_enabled;
};

[[nodiscard]] DesktopEntryKeys parse_desktop_entry(std::string_view text);

// Whether the user switched an autostart entry off (Hidden=true or X-GNOME-Autostart-enabled=false).
[[nodiscard]] bool opted_out(const DesktopEntryKeys& keys) noexcept;

// An Exec value per the Desktop Entry spec: arguments quoted where needed, '%' doubled except in
// the field codes listed in `field_codes`, then the string escapes of the file format.
[[nodiscard]] std::string desktop_exec(const std::vector<std::string>& args,
                                       const std::vector<std::string>& field_codes = {});
// The arguments of an unescaped Exec value, with "%%" back to "%"; nullopt when malformed.
[[nodiscard]] std::optional<std::vector<std::string>> split_desktop_exec(std::string_view exec);

// Lines of a [Desktop Entry] group; Exec is written with desktop_exec. `keep` holds the opt-out
// keys of an existing autostart entry.
[[nodiscard]] std::string render_desktop_entry(std::string_view name, const std::vector<std::string>& exec_args,
                                               const std::vector<std::string>& extra_lines,
                                               const DesktopEntryKeys& keep = {});

// A command line for IntegrationStatus::detail, which integration::split_entry_command reads back.
[[nodiscard]] std::string join_command(const std::vector<std::string>& args);

// The program an Exec or ExecStart runs: past a leading `env` and its NAME=value assignments.
[[nodiscard]] std::optional<NativePath> program_of(const std::vector<std::string>& args);

// mimeapps.list's [Default Applications] entry for `mime`; the first desktop id when it lists several.
[[nodiscard]] std::optional<std::string> mimeapps_default(std::string_view text, std::string_view mime);
// `text` with `mime` set to `desktop_id` (or removed when nullopt) in [Default Applications].
[[nodiscard]] std::string mimeapps_with_default(std::string_view text, std::string_view mime,
                                                const std::optional<std::string>& desktop_id);

}  // namespace reboot::os_linux::platform
