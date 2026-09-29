#define ZARPACK_BUILDING
#include "zarpack.h"

#include "zarchive/zarchivewriter.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

fs::path FromUtf8(const char *s) {
    std::string str(s);
    return fs::path(std::u8string(str.begin(), str.end()));
}

std::string ToUtf8(const fs::path &p) {
    auto u = p.generic_u8string();
    return std::string(u.begin(), u.end());
}

std::string NativeUtf8(const fs::path &p) {
    auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

// Strips trailing separators so "/a/b/" behaves like "/a/b".
fs::path Normalize(fs::path p) {
    p = p.lexically_normal();
    if (!p.has_filename() && p.has_parent_path())
        p = p.parent_path();
    return p;
}

bool ResolveOutput(const fs::path &inputDir, const char *output, fs::path &result) {
    fs::path input = Normalize(inputDir);
    fs::path name = input.filename();
    if (name.empty())
        return false; // e.g. a drive or filesystem root
    std::error_code ec;
    if (output == nullptr || output[0] == '\0') {
        result = input.parent_path() / (name.u8string() + u8".zar");
    } else {
        fs::path out = FromUtf8(output);
        if (fs::is_directory(out, ec))
            result = out / (name.u8string() + u8".zar");
        else
            result = out;
    }
    return true;
}

void CopyOut(const std::string &s, char *buf, size_t size) {
    if (!buf || size == 0) return;
    size_t n = std::min(s.size(), size - 1);
    std::memcpy(buf, s.data(), n);
    buf[n] = '\0';
}

struct Sink {
    fs::path path;
    std::ofstream file;
    bool failed = false;
};

void OnNewFile(int32_t, void *ctx) {
    auto *s = static_cast<Sink *>(ctx);
    s->file = std::ofstream(s->path, std::ios::binary | std::ios::trunc);
    if (!s->file.is_open()) s->failed = true;
}

void OnWrite(const void *data, size_t len, void *ctx) {
    auto *s = static_cast<Sink *>(ctx);
    if (s->failed) return;
    s->file.write(static_cast<const char *>(data), static_cast<std::streamsize>(len));
    if (!s->file.good()) s->failed = true;
}

struct Entry {
    std::string rel; // generic (forward-slash) UTF-8 path inside the archive
    fs::path abs;
    bool isDir;
    uint64_t size;
};

} // namespace

extern "C" {

size_t zarpack_resolve_output(const char *input_dir, const char *output, char *buf, size_t buf_size) {
    if (!input_dir) return 0;
    fs::path result;
    if (!ResolveOutput(FromUtf8(input_dir), output, result)) return 0;
    std::string s = NativeUtf8(result);
    CopyOut(s, buf, buf_size);
    return s.size() + 1;
}

zarpack_status zarpack_pack(const zarpack_options *opt, char *out_path, size_t out_path_size,
                            char *err_msg, size_t err_msg_size) {
    auto fail = [&](zarpack_status st, const std::string &msg) {
        CopyOut(msg, err_msg, err_msg_size);
        return st;
    };
    try {
        if (!opt || !opt->input_dir) return fail(ZARPACK_ERR_INPUT, "No input directory given.");

        std::error_code ec;
        fs::path input = Normalize(FromUtf8(opt->input_dir));
        if (!fs::is_directory(input, ec))
            return fail(ZARPACK_ERR_INPUT, "\"" + NativeUtf8(input) + "\" is not a folder.");

        fs::path output;
        if (!ResolveOutput(input, opt->output, output))
            return fail(ZARPACK_ERR_INPUT, "Cannot archive a drive root; pick a folder inside it.");

        if (fs::exists(output, ec) && !opt->overwrite)
            return fail(ZARPACK_ERR_OUTPUT_EXISTS, "\"" + NativeUtf8(output) + "\" already exists.");
        if (fs::is_directory(output, ec))
            return fail(ZARPACK_ERR_IO, "The output path is an existing folder.");

        fs::path outDir = output.parent_path();
        if (!outDir.empty()) {
            fs::create_directories(outDir, ec);
            if (!fs::is_directory(outDir, ec))
                return fail(ZARPACK_ERR_IO, "Cannot create output folder \"" + NativeUtf8(outDir) + "\".");
        }
        fs::path tmp = output;
        tmp += ".part";

        // Scan.
        std::vector<Entry> entries;
        uint64_t totalBytes = 0, totalFiles = 0;
        for (fs::recursive_directory_iterator it(input, fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec)) {
            const fs::directory_entry &de = *it;
            std::error_code e2;
            bool isDir = de.is_directory(e2);
            if (!isDir && !de.is_regular_file(e2)) continue; // sockets, broken links, ...
            if (fs::equivalent(de.path(), output, e2) || fs::equivalent(de.path(), tmp, e2)) continue;
            Entry en;
            en.rel = ToUtf8(fs::relative(de.path(), input, e2));
            en.abs = de.path();
            en.isDir = isDir;
            en.size = isDir ? 0 : de.file_size(e2);
            if (!isDir) { totalBytes += en.size; totalFiles++; }
            entries.push_back(std::move(en));
        }
        if (ec) return fail(ZARPACK_ERR_IO, "Failed to read folder: " + ec.message());
        // Deterministic output; parents sort before their children.
        std::sort(entries.begin(), entries.end(),
                  [](const Entry &a, const Entry &b) { return a.rel < b.rel; });

        zarpack_progress prog{0, totalBytes, 0, totalFiles, ""};
        auto lastReport = std::chrono::steady_clock::time_point{};
        auto report = [&](bool force) {
            if (!opt->progress) return false;
            auto now = std::chrono::steady_clock::now();
            if (!force && now - lastReport < std::chrono::milliseconds(50)) return false;
            lastReport = now;
            return opt->progress(&prog, opt->user) != 0;
        };

        Sink sink;
        sink.path = tmp;
        bool cancelled = false;
        std::string error;
        {
            ZArchiveWriter writer(OnNewFile, OnWrite, &sink);
            if (sink.failed) {
                fs::remove(tmp, ec);
                return fail(ZARPACK_ERR_IO, "Cannot create \"" + NativeUtf8(tmp) + "\".");
            }
            std::vector<char> buf(256 * 1024);
            cancelled = report(true);
            for (const Entry &en : entries) {
                if (cancelled) break;
                if (en.isDir) {
                    if (!writer.MakeDir(en.rel.c_str(), false)) { error = "Failed to add folder " + en.rel; break; }
                    continue;
                }
                prog.current_file = en.rel.c_str();
                if (!writer.StartNewFile(en.rel.c_str())) { error = "Failed to add " + en.rel; break; }
                std::ifstream in(en.abs, std::ios::binary);
                if (!in.is_open()) { error = "Cannot read " + en.rel; break; }
                uint64_t remaining = en.size;
                while (remaining > 0) {
                    in.read(buf.data(), static_cast<std::streamsize>(std::min<uint64_t>(buf.size(), remaining)));
                    std::streamsize n = in.gcount();
                    if (n <= 0) { error = "Error reading " + en.rel; break; }
                    writer.AppendData(buf.data(), static_cast<size_t>(n));
                    remaining -= static_cast<uint64_t>(n);
                    prog.bytes_done += static_cast<uint64_t>(n);
                    if (sink.failed) { error = "Error writing the archive (disk full?)."; break; }
                    if (report(false)) { cancelled = true; break; }
                }
                if (cancelled || !error.empty()) break;
                prog.files_done++;
                if (report(false)) { cancelled = true; break; }
            }
            if (!cancelled && error.empty()) writer.Finalize();
        }
        if (sink.file.is_open()) { sink.file.flush(); sink.file.close(); }
        if (error.empty() && !cancelled && (sink.failed || sink.file.fail()))
            error = "Error writing the archive (disk full?).";

        if (cancelled || !error.empty()) {
            fs::remove(tmp, ec);
            return cancelled ? fail(ZARPACK_CANCELLED, "Cancelled.") : fail(ZARPACK_ERR_IO, error);
        }

        prog.current_file = "";
        report(true);

        if (opt->overwrite) fs::remove(output, ec);
        fs::rename(tmp, output, ec);
        if (ec) {
            fs::remove(tmp, ec);
            return fail(ZARPACK_ERR_IO, "Cannot write \"" + NativeUtf8(output) + "\": " + ec.message());
        }
        CopyOut(NativeUtf8(output), out_path, out_path_size);
        return ZARPACK_OK;
    } catch (const std::exception &e) {
        return fail(ZARPACK_ERR_INTERNAL, e.what());
    } catch (...) {
        return fail(ZARPACK_ERR_INTERNAL, "Unknown error.");
    }
}

} // extern "C"
