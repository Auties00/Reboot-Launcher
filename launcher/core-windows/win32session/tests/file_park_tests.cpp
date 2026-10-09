#include "win32.hpp"  // first: sets _WIN32_WINNT before any std header pulls in <windows.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
#include <vector>

#include "file_park.hpp"
#include "wide.hpp"

using namespace rb::os_windows::win32session;

namespace {

Bytes utf16(const std::wstring& text) {
    Bytes out;
    out.reserve(text.size() * 2);
    for (wchar_t c : text) {
        out.push_back(static_cast<rb::u8>(c & 0xFF));
        out.push_back(static_cast<rb::u8>((c >> 8) & 0xFF));
    }
    return out;
}

// A unique directory under the temp path, removed with its contents on destruction.
struct TempDir {
    std::wstring path;
    TempDir() {
        std::array<wchar_t, MAX_PATH> temp{};
        GetTempPathW(static_cast<DWORD>(temp.size()), temp.data());
        std::array<wchar_t, MAX_PATH> dir{};
        GetTempFileNameW(temp.data(), L"rbp", 0, dir.data());
        DeleteFileW(dir.data());  // GetTempFileNameW makes a file; replace it with a directory
        path = dir.data();
        CreateDirectoryW(path.c_str(), nullptr);
    }
    ~TempDir() {
        const std::wstring pattern = path + L"\\*";
        WIN32_FIND_DATAW find{};
        HANDLE h = FindFirstFileW(pattern.c_str(), &find);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (find.cFileName[0] == L'.' &&
                    (find.cFileName[1] == 0 || (find.cFileName[1] == L'.' && find.cFileName[2] == 0)))
                    continue;
                DeleteFileW((path + L"\\" + find.cFileName).c_str());
            } while (FindNextFileW(h, &find) != 0);
            FindClose(h);
        }
        RemoveDirectoryW(path.c_str());
    }
    std::wstring file(const wchar_t* name) const { return path + L"\\" + name; }
};

void write_file(const std::wstring& path, const char* contents) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    REQUIRE(h != INVALID_HANDLE_VALUE);
    DWORD written = 0;
    WriteFile(h, contents, static_cast<DWORD>(std::char_traits<char>::length(contents)), &written, nullptr);
    CloseHandle(h);
}

bool exists(const std::wstring& path) {
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

}  // namespace

TEST_CASE("parking moves a file aside and restores it") {
    TempDir dir;
    const std::wstring original = dir.file(L"game.ini");
    write_file(original, "live");
    {
        FilePark park({utf16(original)});
        CHECK_FALSE(exists(original));
        CHECK(exists(original + L".reboot-parked"));
    }
    CHECK(exists(original));
    CHECK_FALSE(exists(original + L".reboot-parked"));
}

TEST_CASE("a leftover parked copy beside a live original is cleared before parking") {
    TempDir dir;
    const std::wstring original = dir.file(L"game.ini");
    write_file(original, "live");
    write_file(original + L".reboot-parked", "stale");
    { FilePark park({utf16(original)}); }
    CHECK(exists(original));
    CHECK_FALSE(exists(original + L".reboot-parked"));
}

TEST_CASE("an orphaned parked copy from a crash is adopted and restored") {
    TempDir dir;
    const std::wstring original = dir.file(L"game.ini");
    write_file(original + L".reboot-parked", "survivor");
    {
        FilePark park({utf16(original)});
        CHECK_FALSE(exists(original));  // nothing to park, but the orphan stays parked
    }
    CHECK(exists(original));
    CHECK_FALSE(exists(original + L".reboot-parked"));
}

TEST_CASE("a missing file is skipped") {
    TempDir dir;
    const std::wstring original = dir.file(L"absent.ini");
    { FilePark park({utf16(original)}); }
    CHECK_FALSE(exists(original));
    CHECK_FALSE(exists(original + L".reboot-parked"));
}

TEST_CASE("the restored file wins over one recreated during the session") {
    TempDir dir;
    const std::wstring original = dir.file(L"game.ini");
    write_file(original, "user");
    {
        FilePark park({utf16(original)});
        write_file(original, "session");  // the game wrote its own copy while ours was parked
    }
    CHECK(exists(original));
    HANDLE h = CreateFileW(original.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    REQUIRE(h != INVALID_HANDLE_VALUE);
    std::array<char, 16> buffer{};
    DWORD read = 0;
    ReadFile(h, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr);
    CloseHandle(h);
    CHECK(std::string(buffer.data(), read) == "user");
}
