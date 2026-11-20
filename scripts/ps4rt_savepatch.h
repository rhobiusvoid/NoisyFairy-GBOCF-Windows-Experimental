// Save-data patches for the launcher (header only, no SDL/ImGui, so tests/test_savepatch.cpp can build it anywhere).
//
// Link battles: the six Situation Battles that the console version unlocks by linking GBO2 save data are
// controlled by bits 0-5 of the 32-bit value at offset 0x1F3F9C of GAMESAVEFILE.DAT. The save has no checksum,
// so setting those bits is all the game needs.
#pragma once
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace savepatch {
namespace fs = std::filesystem;

constexpr uintmax_t kGameSaveSize = 2069656;
constexpr uint64_t kLinkMaskOffset = 0x1F3F9C;
constexpr uint32_t kLinkBits = 0x3F;

enum class Result { Patched, AlreadyUnlocked, NoSave, BadSize, IoError };
struct Outcome { Result result = Result::NoSave; int saves = 0; std::string detail; };

// Saves live in <out>/user/savedata/<user>/<title>/GAMESAVE/GAMESAVEFILE.DAT (one folder per user).
inline std::vector<fs::path> find_saves(const fs::path& user_dir, const std::string& title) {
    std::vector<fs::path> found;
    std::error_code ec;
    for (const auto& u : fs::directory_iterator(user_dir / "savedata", ec)) {
        fs::path p = u.path() / title / "GAMESAVE" / "GAMESAVEFILE.DAT";
        if (fs::is_regular_file(p, ec)) found.push_back(p);
    }
    return found;
}

// Sets the six link bits in one save. Keeps a backup next to it (GAMESAVEFILE.DAT.before-link-unlock, written once).
inline Result patch_file(const fs::path& path) {
    std::error_code ec;
    if (fs::file_size(path, ec) != kGameSaveSize || ec) return Result::BadSize;
    std::vector<char> data(kGameSaveSize);
    {
        std::ifstream in(path, std::ios::binary);
        if (!in.read(data.data(), (std::streamsize)data.size())) return Result::IoError;
    }
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= (uint32_t)(uint8_t)data[kLinkMaskOffset + i] << (8 * i);
    if ((v & kLinkBits) == kLinkBits) return Result::AlreadyUnlocked;
    fs::path backup = path;
    backup += ".before-link-unlock";
    if (!fs::exists(backup, ec)) {
        fs::copy_file(path, backup, ec);
        if (ec) return Result::IoError;
    }
    v |= kLinkBits;
    for (int i = 0; i < 4; ++i) data[kLinkMaskOffset + i] = (char)((v >> (8 * i)) & 0xFF);
    // Write a temporary file first, then replace, so a failed write can't leave a half-written save.
    fs::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out.write(data.data(), (std::streamsize)data.size())) return Result::IoError;
    }
    fs::rename(tmp, path, ec);
    return ec ? Result::IoError : Result::Patched;
}

inline Outcome unlock_link_battles(const fs::path& user_dir, const std::string& title) {
    Outcome o;
    o.result = Result::NoSave;
    int patched = 0, already = 0, bad = 0, io = 0;
    for (const auto& p : find_saves(user_dir, title)) {
        ++o.saves;
        switch (patch_file(p)) {
            case Result::Patched: ++patched; break;
            case Result::AlreadyUnlocked: ++already; break;
            case Result::BadSize: ++bad; break;
            default: ++io; break;
        }
    }
    if (!o.saves) { o.detail = "No save found yet. Start the game once so it creates one, then try again."; return o; }
    if (io) { o.result = Result::IoError; o.detail = "Could not write the save file."; return o; }
    if (patched) { o.result = Result::Patched; o.detail = "Link battles unlocked. A backup was kept next to the save."; return o; }
    if (already) { o.result = Result::AlreadyUnlocked; o.detail = "The link battles are already unlocked in your save."; return o; }
    (void)bad;
    o.result = Result::BadSize;
    o.detail = "The save file is not the expected size (a different game version?). Nothing was changed.";
    return o;
}
}  // namespace savepatch
