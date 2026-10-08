#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace reboot::compat {

// GameUserSettings.ini under the prefix user's AppData/Local/FortniteGame/Saved/Config/WindowsClient.
inline constexpr std::string_view kGameUserSettingsFile = "GameUserSettings.ini";

// Covers no capability ids (decision macos-compat-layer).
// The ini with PreferredRHI=dx11 under [D3DRHIPreference], since DXMT serves D3D11 only and never
// D3D12. dx11 and dx10 are kept; any other value, carried over or imported, is rewritten, and a
// missing key or section is added. nullopt when nothing changes. Line endings and every other
// line are left as they were.
[[nodiscard]] std::optional<std::string> seed_preferred_rhi(std::string_view ini);

}  // namespace reboot::compat
