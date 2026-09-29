#include "zarpack.h"
#include "zarchive/zarchivereader.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <random>
#include <string>

namespace fs = std::filesystem;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static std::string Bytes(size_t n, unsigned seed) {
    std::mt19937 rng(seed);
    std::string s(n, '\0');
    for (auto &c : s) c = static_cast<char>(rng() % 7 == 0 ? rng() : 'a' + rng() % 4); // mixed compressibility
    return s;
}

static void Write(const fs::path &p, const std::string &data) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << data;
}

static std::string Str(const fs::path &p) { auto u = p.u8string(); return {u.begin(), u.end()}; }

int main() {
    fs::path root = fs::current_path() / "zarpack_test_tmp";
    fs::remove_all(root);
    fs::path in = root / "Game Dump";
    std::map<std::string, std::string> files = {
        {"eboot.bin", Bytes(300000, 1)},           // several 64K blocks
        {"empty.txt", ""},
        {"sce_sys/param.json", "{\"a\":1}"},
        {"data/deep/er/file.dat", Bytes(65536, 2)}, // exactly one block
        {"data/deep/er/other.dat", Bytes(65537, 3)},
        {"data/\xC3\xA4\xC3\xB6\xC3\xBC-\xE6\x97\xA5\xE6\x9C\xAC.txt", "unicode name"},
    };
    for (auto &[rel, data] : files) Write(in / fs::path(std::u8string(rel.begin(), rel.end())), data);
    fs::create_directories(in / "empty_dir");

    // Default output: sibling of the folder.
    char buf[1024], out[1024], err[512];
    CHECK(zarpack_resolve_output(Str(in).c_str(), nullptr, buf, sizeof buf) > 0);
    CHECK(fs::path(buf) == root / "Game Dump.zar");
    // Trailing separator behaves the same.
    CHECK(zarpack_resolve_output((Str(in) + "/").c_str(), "", buf, sizeof buf) > 0);
    CHECK(fs::path(buf) == root / "Game Dump.zar");

    struct Ctx { int calls = 0; uint64_t last = 0; bool cancelAt = false; } ctx;
    auto cb = [](const zarpack_progress *p, void *u) -> int {
        auto *c = static_cast<Ctx *>(u);
        c->calls++;
        c->last = p->bytes_done;
        return c->cancelAt ? 1 : 0;
    };

    zarpack_options opt{};
    std::string inStr = Str(in);
    opt.input_dir = inStr.c_str();
    opt.progress = cb;
    opt.user = &ctx;

    // Pack.
    zarpack_status st = zarpack_pack(&opt, out, sizeof out, err, sizeof err);
    CHECK(st == ZARPACK_OK);
    if (st != ZARPACK_OK) std::printf("err: %s\n", err);
    CHECK(fs::path(out) == root / "Game Dump.zar");
    CHECK(ctx.calls >= 2);
    uint64_t total = 0;
    for (auto &[k, v] : files) total += v.size();
    CHECK(ctx.last == total);
    CHECK(!fs::exists(root / "Game Dump.zar.part"));

    // Verify with the reference reader.
    {
        std::unique_ptr<ZArchiveReader> r(ZArchiveReader::OpenFromFile(fs::path(out)));
        CHECK(r != nullptr);
        if (r) {
            for (auto &[rel, data] : files) {
                ZArchiveNodeHandle h = r->LookUp(rel, true, false);
                CHECK(h != ZARCHIVE_INVALID_NODE);
                if (h == ZARCHIVE_INVALID_NODE) continue;
                CHECK(r->GetFileSize(h) == data.size());
                std::string got(data.size(), '\0');
                uint64_t n = data.empty() ? 0 : r->ReadFromFile(h, 0, data.size(), got.data());
                CHECK(n == data.size());
                CHECK(got == data);
            }
            CHECK(r->LookUp("empty_dir", false, true) != ZARCHIVE_INVALID_NODE);
        }
    }

    // Deterministic: packing twice yields identical bytes.
    std::string first;
    { std::ifstream f(out, std::ios::binary); first.assign(std::istreambuf_iterator<char>(f), {}); }
    opt.overwrite = 1;
    CHECK(zarpack_pack(&opt, out, sizeof out, err, sizeof err) == ZARPACK_OK);
    std::string second;
    { std::ifstream f(out, std::ios::binary); second.assign(std::istreambuf_iterator<char>(f), {}); }
    CHECK(first == second);

    // Existing output without overwrite is refused and untouched.
    opt.overwrite = 0;
    CHECK(zarpack_pack(&opt, out, sizeof out, err, sizeof err) == ZARPACK_ERR_OUTPUT_EXISTS);

    // Output folder.
    fs::path dest = root / "elsewhere";
    fs::create_directories(dest);
    std::string destStr = Str(dest);
    opt.output = destStr.c_str();
    CHECK(zarpack_pack(&opt, out, sizeof out, err, sizeof err) == ZARPACK_OK);
    CHECK(fs::exists(dest / "Game Dump.zar"));

    // Explicit file path in a folder that doesn't exist yet.
    std::string explicitOut = Str(root / "new" / "x.zar");
    opt.output = explicitOut.c_str();
    CHECK(zarpack_pack(&opt, out, sizeof out, err, sizeof err) == ZARPACK_OK);
    CHECK(fs::exists(root / "new" / "x.zar"));

    // Cancel leaves nothing behind.
    fs::path cancelOut = root / "cancel.zar";
    std::string cancelStr = Str(cancelOut);
    opt.output = cancelStr.c_str();
    ctx.cancelAt = true;
    CHECK(zarpack_pack(&opt, out, sizeof out, err, sizeof err) == ZARPACK_CANCELLED);
    CHECK(!fs::exists(cancelOut));
    CHECK(!fs::exists(cancelOut.string() + ".part"));
    ctx.cancelAt = false;

    // Archive written inside the input folder is not archived into itself.
    std::string inside = Str(in / "self.zar");
    opt.output = inside.c_str();
    CHECK(zarpack_pack(&opt, out, sizeof out, err, sizeof err) == ZARPACK_OK);
    opt.overwrite = 1;
    CHECK(zarpack_pack(&opt, out, sizeof out, err, sizeof err) == ZARPACK_OK);
    {
        std::unique_ptr<ZArchiveReader> r(ZArchiveReader::OpenFromFile(fs::path(out)));
        CHECK(r && r->LookUp("self.zar", true, false) == ZARCHIVE_INVALID_NODE);
    }

    // Bad input.
    std::string notDir = Str(in / "eboot.bin");
    opt.input_dir = notDir.c_str();
    opt.output = nullptr;
    CHECK(zarpack_pack(&opt, out, sizeof out, err, sizeof err) == ZARPACK_ERR_INPUT);

    fs::remove_all(root);
    std::printf(failures ? "%d failure(s)\n" : "all tests passed\n", failures);
    return failures ? 1 : 0;
}
