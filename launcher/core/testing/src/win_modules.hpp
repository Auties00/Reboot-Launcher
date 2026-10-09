#pragma once

// Module lookups for reboot-fake-game, Windows only. Plain types, so win_modules.cpp can declare
// the kernel32 calls itself.
namespace reboot::testing::win_modules {

// Whether a module of this file name is loaded in this process.
[[nodiscard]] bool loaded(const wchar_t* file_name) noexcept;
// LoadLibraryW; false when it fails.
bool load(const wchar_t* path) noexcept;

}  // namespace reboot::testing::win_modules
