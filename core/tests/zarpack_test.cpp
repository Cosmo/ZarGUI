#include "zarpack.h"
#include "zarchive/zarchivereader.h"
#include "zarchive/zarchivewriter.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>
#include <chrono>
#include <thread>

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

static std::string Read(const fs::path &p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}

// Writes an archive directly with ZArchiveWriter (for hostile inputs).
static void RawArchive(const fs::path &out, const std::vector<std::pair<std::string, std::string>> &files) {
    struct Ctx { std::ofstream f; } ctx;
    ZArchiveWriter w([](int32_t, void *c) {},
                     [](const void *d, size_t n, void *c) { static_cast<Ctx *>(c)->f.write((const char *)d, n); }, &ctx);
    ctx.f.open(out, std::ios::binary);
    for (auto &[p, data] : files) {
        if (auto slash = p.rfind('/'); slash != std::string::npos) w.MakeDir(p.substr(0, slash).c_str(), true);
        CHECK(w.StartNewFile(p.c_str()));
        w.AppendData(data.data(), data.size());
    }
    w.Finalize();
}

static long FindEntry(zarpack_archive *a, const std::string &path) {
    for (size_t i = 0; i < zarpack_entry_count(a); i++) {
        zarpack_entry e; zarpack_entry_get(a, i, &e);
        if (path == e.path) return (long)i;
    }
    return -1;
}

static size_t CountFiles(const fs::path &dir) {
    size_t n = 0;
    for (auto &e : fs::recursive_directory_iterator(dir)) n += e.is_regular_file();
    return n;
}

static void ReadTests(const fs::path &root, const fs::path &archive, const std::map<std::string, std::string> &files) {
    char err[512];
    zarpack_archive *a = nullptr;
    CHECK(zarpack_open(Str(archive).c_str(), &a, err, sizeof err) == ZARPACK_OK);
    if (!a) { std::printf("open err: %s\n", err); return; }

    // Listing: all files and dirs, parents before children, dirs first, sizes summed.
    size_t fileCount = 0;
    for (size_t i = 0; i < zarpack_entry_count(a); i++) {
        zarpack_entry e; CHECK(zarpack_entry_get(a, i, &e));
        if (e.parent >= 0) CHECK((size_t)e.parent < i);
        if (!e.is_dir) {
            fileCount++;
            auto f = files.find(e.path);
            CHECK(f != files.end());
            if (f != files.end()) CHECK(e.size == f->second.size());
        }
    }
    CHECK(fileCount == files.size());
    zarpack_entry e0; zarpack_entry_get(a, 0, &e0);
    CHECK(e0.is_dir); // directories first
    long data = FindEntry(a, "data");
    CHECK(data >= 0);
    { zarpack_entry e; zarpack_entry_get(a, data, &e); CHECK(e.size == 65536 + 65537 + 12); CHECK(std::string(e.name) == "data"); }
    CHECK(FindEntry(a, "empty_dir") >= 0);
    zarpack_entry dummy;
    CHECK(!zarpack_entry_get(a, zarpack_entry_count(a), &dummy));

    // Extract all.
    fs::path all = root / "x_all";
    CHECK(zarpack_extract(a, nullptr, 0, Str(all).c_str(), 0, nullptr, nullptr, err, sizeof err) == ZARPACK_OK);
    for (auto &[rel, content] : files) CHECK(Read(all / fs::path(std::u8string(rel.begin(), rel.end()))) == content);
    CHECK(fs::is_directory(all / "empty_dir"));
    CHECK(CountFiles(all) == files.size());

    // Again without overwrite: refused, nothing changed.
    std::ofstream(all / "eboot.bin", std::ios::trunc) << "changed";
    CHECK(zarpack_extract(a, nullptr, 0, Str(all).c_str(), 0, nullptr, nullptr, err, sizeof err) == ZARPACK_ERR_OUTPUT_EXISTS);
    CHECK(Read(all / "eboot.bin") == "changed");
    // With overwrite: replaced.
    CHECK(zarpack_extract(a, nullptr, 0, Str(all).c_str(), 1, nullptr, nullptr, err, sizeof err) == ZARPACK_OK);
    CHECK(Read(all / "eboot.bin") == files.at("eboot.bin"));
    CHECK(CountFiles(all) == files.size()); // no leftover temp files

    // Selection: a nested directory, a file inside it (covered, skipped) and a top-level file.
    fs::path sel = root / "x_sel";
    long deep = FindEntry(a, "data/deep"), inner = FindEntry(a, "data/deep/er/file.dat"), json = FindEntry(a, "sce_sys/param.json");
    CHECK(deep >= 0 && inner >= 0 && json >= 0);
    size_t pick[] = {(size_t)inner, (size_t)json, (size_t)deep};
    CHECK(zarpack_extract(a, pick, 3, Str(sel).c_str(), 0, nullptr, nullptr, err, sizeof err) == ZARPACK_OK);
    CHECK(Read(sel / "deep/er/file.dat") == files.at("data/deep/er/file.dat"));
    CHECK(Read(sel / "deep/er/other.dat") == files.at("data/deep/er/other.dat"));
    CHECK(Read(sel / "param.json") == files.at("sce_sys/param.json"));
    CHECK(!fs::exists(sel / "file.dat"));
    CHECK(CountFiles(sel) == 3);

    // Invalid index.
    size_t bad[] = {zarpack_entry_count(a)};
    CHECK(zarpack_extract(a, bad, 1, Str(sel).c_str(), 0, nullptr, nullptr, err, sizeof err) == ZARPACK_ERR_INPUT);

    // Cancel: everything created is removed, including the new destination folder.
    fs::path cancelDir = root / "x_cancel" / "nested";
    auto cancelNow = [](const zarpack_progress *, void *) -> int { return 1; };
    CHECK(zarpack_extract(a, nullptr, 0, Str(cancelDir).c_str(), 0, cancelNow, nullptr, err, sizeof err) == ZARPACK_CANCELLED);
    CHECK(!fs::exists(root / "x_cancel"));
    // Cancel midway (after some files were written).
    struct Mid { int calls = 0; } mid;
    auto cancelLater = [](const zarpack_progress *p, void *u) -> int {
        std::this_thread::sleep_for(std::chrono::milliseconds(60)); // outlast the 50 ms progress throttle
        return ++static_cast<Mid *>(u)->calls >= 3 ? 1 : 0;
    };
    CHECK(zarpack_extract(a, nullptr, 0, Str(cancelDir).c_str(), 0, cancelLater, &mid, err, sizeof err) == ZARPACK_CANCELLED);
    CHECK(mid.calls >= 3);
    CHECK(!fs::exists(root / "x_cancel"));

    // A file where a folder should go is an error, never deleted.
    fs::path clash = root / "x_clash";
    fs::create_directories(clash);
    std::ofstream(clash / "data") << "i am a file";
    CHECK(zarpack_extract(a, nullptr, 0, Str(clash).c_str(), 1, nullptr, nullptr, err, sizeof err) == ZARPACK_ERR_IO);
    CHECK(Read(clash / "data") == "i am a file");

    // Symlinked target is refused.
    fs::path linkDest = root / "x_link";
    fs::create_directories(linkDest);
    fs::create_directory_symlink(root / "x_all", linkDest / "data");
    CHECK(zarpack_extract(a, nullptr, 0, Str(linkDest).c_str(), 1, nullptr, nullptr, err, sizeof err) == ZARPACK_ERR_IO);

    // Single entry to an exact path (drag and drop), including a new name.
    fs::path dropped = root / "x_drop" / "renamed deep";
    CHECK(zarpack_extract_entry(a, deep, Str(dropped).c_str(), 0, nullptr, nullptr, err, sizeof err) == ZARPACK_OK);
    CHECK(Read(dropped / "er/file.dat") == files.at("data/deep/er/file.dat"));
    CHECK(CountFiles(root / "x_drop") == 2);
    fs::path droppedFile = root / "x_drop" / "copy.json";
    CHECK(zarpack_extract_entry(a, json, Str(droppedFile).c_str(), 0, nullptr, nullptr, err, sizeof err) == ZARPACK_OK);
    CHECK(Read(droppedFile) == files.at("sce_sys/param.json"));
    CHECK(zarpack_extract_entry(a, json, Str(droppedFile).c_str(), 0, nullptr, nullptr, err, sizeof err) == ZARPACK_ERR_OUTPUT_EXISTS);
    CHECK(zarpack_extract_entry(a, zarpack_entry_count(a), Str(droppedFile).c_str(), 0, nullptr, nullptr, err, sizeof err) == ZARPACK_ERR_INPUT);

    // Streaming reads.
    {
        const std::string &eboot = files.at("eboot.bin");
        long idx = FindEntry(a, "eboot.bin");
        std::string streamed;
        std::vector<char> chunk(70000); // not a multiple of the 64 KiB block size
        for (uint64_t off = 0;;) {
            int64_t n = zarpack_read(a, idx, off, chunk.data(), chunk.size());
            CHECK(n >= 0);
            if (n <= 0) break;
            streamed.append(chunk.data(), static_cast<size_t>(n));
            off += static_cast<uint64_t>(n);
        }
        CHECK(streamed == eboot);
        CHECK(zarpack_read(a, deep, 0, chunk.data(), chunk.size()) == -1); // a directory
        CHECK(zarpack_read(a, idx, eboot.size() + 5, chunk.data(), chunk.size()) == 0);
    }

    zarpack_close(a);

    // Not an archive.
    zarpack_archive *b = nullptr;
    CHECK(zarpack_open(Str(all / "eboot.bin").c_str(), &b, err, sizeof err) == ZARPACK_ERR_INPUT && !b);
    CHECK(zarpack_open(Str(root / "missing.zar").c_str(), &b, err, sizeof err) == ZARPACK_ERR_INPUT && !b);
}

static void SystemFileTests(const fs::path &root) {
    fs::path in = root / "meta";
    Write(in / "keep.txt", "k");
    Write(in / "sub/.hidden-but-real", "h");
    for (const char *junk : {".DS_Store", "._keep.txt", "sub/.DS_Store", "sub/._x", "Thumbs.db", "desktop.ini",
                             ".Spotlight-V100/Store-V2/db", ".fseventsd/log", ".Trashes/501/old"})
        Write(in / junk, "junk");
    std::string inStr = Str(in);
    char out[1024], err[512];
    for (int keep : {0, 1}) {
        zarpack_options opt{};
        opt.input_dir = inStr.c_str();
        opt.overwrite = 1;
        opt.keep_system_files = keep;
        CHECK(zarpack_pack(&opt, out, sizeof out, err, sizeof err) == ZARPACK_OK);
        zarpack_archive *a = nullptr;
        CHECK(zarpack_open(out, &a, err, sizeof err) == ZARPACK_OK);
        if (!a) continue;
        size_t fileCount = 0;
        for (size_t i = 0; i < zarpack_entry_count(a); i++) {
            zarpack_entry e; zarpack_entry_get(a, i, &e);
            fileCount += !e.is_dir;
        }
        CHECK(FindEntry(a, "keep.txt") >= 0 && FindEntry(a, "sub/.hidden-but-real") >= 0);
        CHECK((FindEntry(a, ".DS_Store") >= 0) == (keep == 1));
        CHECK((FindEntry(a, ".Spotlight-V100") >= 0) == (keep == 1));
        CHECK(fileCount == (keep ? 11u : 2u));
        zarpack_close(a);
    }
}

static void CompressionTests(const fs::path &root) {
    fs::path in = root / "levels";
    // Text-like data with a large vocabulary, so stronger levels find more to gain.
    std::mt19937 rng(7);
    std::vector<std::string> words;
    for (int i = 0; i < 3000; i++) {
        std::string w;
        for (int k = 0, n = 3 + rng() % 8; k < n; k++) w += static_cast<char>('a' + rng() % 26);
        words.push_back(w);
    }
    std::string text;
    while (text.size() < 2'000'000) text += words[std::min<size_t>(rng() % 3000, rng() % 3000)] + ' ';
    Write(in / "a.txt", text);
    std::string inStr = Str(in);
    char out[1024], err[512];
    uint64_t sizes[3];
    int levels[] = {1, 0, 19};
    for (int i = 0; i < 3; i++) {
        fs::path target = root / ("level" + std::to_string(levels[i]) + ".zar");
        std::string targetStr = Str(target);
        zarpack_options opt{};
        opt.input_dir = inStr.c_str();
        opt.output = targetStr.c_str();
        opt.compression_level = levels[i];
        CHECK(zarpack_pack(&opt, out, sizeof out, err, sizeof err) == ZARPACK_OK);
        sizes[i] = fs::file_size(target);
        zarpack_archive *a = nullptr;
        CHECK(zarpack_open(targetStr.c_str(), &a, err, sizeof err) == ZARPACK_OK);
        if (a) {
            fs::path x = root / ("level_out" + std::to_string(i));
            CHECK(zarpack_extract(a, nullptr, 0, Str(x).c_str(), 0, nullptr, nullptr, err, sizeof err) == ZARPACK_OK);
            CHECK(Read(x / "a.txt") == text);
            zarpack_close(a);
        }
    }
    CHECK(sizes[0] > sizes[1] && sizes[1] > sizes[2]);
    if (!(sizes[0] > sizes[1] && sizes[1] > sizes[2]))
        std::printf("  sizes: level1=%llu default=%llu level19=%llu\n", (unsigned long long)sizes[0],
                    (unsigned long long)sizes[1], (unsigned long long)sizes[2]);
    zarpack_options bad{};
    bad.input_dir = inStr.c_str();
    bad.compression_level = 20;
    CHECK(zarpack_pack(&bad, out, sizeof out, err, sizeof err) == ZARPACK_ERR_INPUT);
}

static void HostileTests(const fs::path &root) {
    char err[512];
    // Hostile names: build a normal archive, then overwrite a name in the
    // (uncompressed) name table with same-length bytes, as an attacker could.
    struct Case { std::string path, marker, replacement; };
    const Case cases[] = {
        {"QQ/evil.txt", "QQ", ".."},                        // parent directory
        {"Q/evil.txt", "Q", "."},                           // current directory
        {"xQy.txt", "xQy", "x/y"},                          // separator inside a name
        {"xQy.txt", "xQy", "x\\y"},
        {"xQy.txt", "xQy", std::string("x\0y", 3)},       // NUL inside a name
    };
    for (const Case &c : cases) {
        const std::string &marker = c.marker, &repl = c.replacement;
        fs::path z = root / "evil.zar";
        RawArchive(z, {{"ok.txt", "fine"}, {c.path, "pwned"}});
        std::string bytes = Read(z);
        size_t pos = bytes.rfind(marker); // the name table sits near the end
        CHECK(pos != std::string::npos);
        if (pos == std::string::npos) continue;
        bytes.replace(pos, marker.size(), repl);
        std::ofstream(z, std::ios::binary | std::ios::trunc) << bytes;
        zarpack_archive *a = nullptr;
        zarpack_status st = zarpack_open(Str(z).c_str(), &a, err, sizeof err);
        CHECK(st == ZARPACK_ERR_INPUT);
        if (st != ZARPACK_ERR_INPUT) std::printf("  hostile case not rejected: %s\n", c.path.c_str());
        if (a) zarpack_close(a);
    }
    CHECK(!fs::exists(root.parent_path() / "evil.txt"));

    // Random corruption must never crash; valid-looking results are fine.
    std::map<std::string, std::string> small = {{"a/b.bin", Bytes(200000, 9)}, {"c.txt", "hello"}};
    fs::path good = root / "good.zar";
    std::vector<std::pair<std::string, std::string>> v(small.begin(), small.end());
    RawArchive(good, v);
    std::string bytes = Read(good);
    std::mt19937 rng(42);
    int opened = 0;
    for (int iter = 0; iter < 3000; iter++) {
        std::string m = bytes;
        int flips = 1 + rng() % 8;
        bool tail = rng() % 2; // bias towards the footer/tables at the end
        for (int k = 0; k < flips; k++) {
            size_t pos = tail ? m.size() - 1 - rng() % std::min<size_t>(m.size(), 400) : rng() % m.size();
            m[pos] = static_cast<char>(rng());
        }
        if (iter % 50 == 0) m.resize(rng() % m.size());
        fs::path z = root / "fuzz.zar";
        std::ofstream(z, std::ios::binary | std::ios::trunc) << m;
        zarpack_archive *a = nullptr;
        if (zarpack_open(Str(z).c_str(), &a, err, sizeof err) == ZARPACK_OK) {
            opened++;
            fs::path out = root / "fuzz_out";
            zarpack_extract(a, nullptr, 0, Str(out).c_str(), 1, nullptr, nullptr, err, sizeof err);
            // Anything extracted must be inside the destination.
            if (fs::exists(out))
                for (auto &e : fs::recursive_directory_iterator(out)) {
                    auto rel = fs::relative(e.path(), out);
                    CHECK(!rel.empty() && *rel.begin() != "..");
                }
            fs::remove_all(out);
            zarpack_close(a);
        }
    }
    std::printf("fuzz: %d/3000 corrupted archives still opened\n", opened);
}

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

    // Read / extract.
    fs::path readArchive = root / "read.zar";
    std::string readOut = Str(readArchive);
    opt.input_dir = inStr.c_str();
    opt.output = readOut.c_str();
    opt.overwrite = 0;
    fs::remove(in / "self.zar");
    CHECK(zarpack_pack(&opt, out, sizeof out, err, sizeof err) == ZARPACK_OK);
    ReadTests(root, readArchive, files);
    HostileTests(root);
    SystemFileTests(root);
    CompressionTests(root);

    // Names of 128+ bytes are refused (other readers can't decode them).
    fs::path longIn = root / "long";
    Write(longIn / (std::string(128, 'n') + ".bin"), "x");
    std::string longStr = Str(longIn);
    opt.input_dir = longStr.c_str();
    opt.output = nullptr;
    CHECK(zarpack_pack(&opt, out, sizeof out, err, sizeof err) == ZARPACK_ERR_INPUT);
    CHECK(!fs::exists(root / "long.zar"));
    fs::remove_all(longIn);
    Write(longIn / (std::string(127, 'n')), "x"); // 127 is fine
    CHECK(zarpack_pack(&opt, out, sizeof out, err, sizeof err) == ZARPACK_OK);

    // Bad input.
    std::string notDir = Str(in / "eboot.bin");
    opt.input_dir = notDir.c_str();
    opt.output = nullptr;
    CHECK(zarpack_pack(&opt, out, sizeof out, err, sizeof err) == ZARPACK_ERR_INPUT);

    fs::remove_all(root);
    std::printf(failures ? "%d failure(s)\n" : "all tests passed\n", failures);
    return failures ? 1 : 0;
}
