#include "../scripts/ps4rt_savepatch.h"
#include <cstdio>
#include <cstdlib>
using namespace savepatch;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

static std::vector<char> slurp(const fs::path& p) { std::ifstream f(p, std::ios::binary); return {std::istreambuf_iterator<char>(f), {}}; }

int main(int argc, char** argv) {
    fs::path root = argc > 1 ? argv[1] : "savepatch_test";
    fs::remove_all(root);
    fs::path dir = root / "savedata" / "1" / "CUSA28763" / "GAMESAVE";
    fs::create_directories(dir);
    fs::path save = dir / "GAMESAVEFILE.DAT";
    std::vector<char> orig(kGameSaveSize);
    for (size_t i = 0; i < orig.size(); ++i) orig[i] = (char)(i * 31 + 7);
    for (int i = 0; i < 4; ++i) orig[kLinkMaskOffset + i] = 0;
    orig[kLinkMaskOffset] = 0x40;                       // an unrelated high bit must survive
    { std::ofstream(save, std::ios::binary).write(orig.data(), (std::streamsize)orig.size()); }

    Outcome o = unlock_link_battles(root, "CUSA28763");
    CHECK(o.result == Result::Patched && o.saves == 1);
    auto now = slurp(save);
    CHECK(now.size() == orig.size());
    size_t diff = 0, at = 0;
    for (size_t i = 0; i < now.size(); ++i) if (now[i] != orig[i]) { ++diff; at = i; }
    CHECK(diff == 1 && at == kLinkMaskOffset && (uint8_t)now[at] == 0x7F);
    CHECK(slurp(fs::path(save) += ".before-link-unlock") == orig);
    CHECK(!fs::exists(fs::path(save) += ".tmp"));

    CHECK(unlock_link_battles(root, "CUSA28763").result == Result::AlreadyUnlocked);
    CHECK(slurp(fs::path(save) += ".before-link-unlock") == orig);   // the backup is never overwritten

    { std::ofstream(save, std::ios::binary).write("short", 5); }
    CHECK(unlock_link_battles(root, "CUSA28763").result == Result::BadSize);
    fs::remove_all(root);
    CHECK(unlock_link_battles(root, "CUSA28763").result == Result::NoSave);
    std::puts("savepatch ok");
    return 0;
}
