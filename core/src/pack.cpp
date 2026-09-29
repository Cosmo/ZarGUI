#include "common.hpp"

#include "zarchive/zarchivewriter.h"

#include <algorithm>
#include <fstream>
#include <optional>
#include <unordered_set>
#include <vector>

namespace zarpack {
namespace {

// Names of 128 bytes or more use a two-byte length that the upstream ZArchive
// reader decodes incorrectly, so other tools could not read those entries.
constexpr size_t kMaxNameBytes = 127;

constexpr int kDefaultLevel = 6;

/// Metadata that file systems and file managers create on their own.
bool IsSystemFile(std::string_view name) {
    static constexpr std::string_view kNames[] = {
        ".DS_Store", ".localized", ".apdisk", ".Spotlight-V100", ".Trashes", ".fseventsd", ".TemporaryItems",
        ".DocumentRevisions-V100", ".VolumeIcon.icns", "Icon\r", "Thumbs.db", "ehthumbs.db", "desktop.ini",
        "$RECYCLE.BIN", "System Volume Information"};
    if (name.starts_with("._")) return true; // AppleDouble resource forks
    return std::find(std::begin(kNames), std::end(kNames), name) != std::end(kNames);
}

struct InputEntry {
    std::string path; // relative, forward slashes, as stored in the archive
    fs::path source;
    bool isDir;
    uint64_t size;
};

std::optional<fs::path> ResolveOutput(const fs::path &input, const char *output) {
    fs::path name = input.filename();
    if (name.empty()) return std::nullopt; // a drive or filesystem root
    fs::path archiveName = name.u8string() + u8".zar";
    if (!output || !*output) return input.parent_path() / archiveName;
    fs::path out = FromUtf8(output);
    std::error_code ec;
    return fs::is_directory(out, ec) ? out / archiveName : out;
}

void PrepareOutput(const fs::path &output, bool overwrite) {
    std::error_code ec;
    if (fs::is_directory(output, ec)) Fail(ZARPACK_ERR_IO, "The output path is an existing folder.");
    if (fs::exists(output, ec) && !overwrite) Fail(ZARPACK_ERR_OUTPUT_EXISTS, QuotedPath(output) + " already exists.");
    fs::path folder = output.parent_path();
    if (folder.empty()) return;
    fs::create_directories(folder, ec);
    if (!fs::is_directory(folder, ec)) Fail(ZARPACK_ERR_IO, "Cannot create output folder " + QuotedPath(folder) + ".");
}

/// Everything below `input` except the given paths, sorted so that parents
/// come before their children and the output is deterministic.
std::vector<InputEntry> ScanInput(const fs::path &input, const fs::path &skip, const fs::path &skipToo,
                                  bool keepSystemFiles) {
    std::vector<InputEntry> entries;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(input, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        std::error_code e;
        bool isDir = it->is_directory(e);
        if (!keepSystemFiles && IsSystemFile(ToUtf8(it->path().filename()))) {
            if (isDir) it.disable_recursion_pending();
            continue;
        }
        if (!isDir && !it->is_regular_file(e)) continue; // sockets, broken links, ...
        if (fs::equivalent(it->path(), skip, e) || fs::equivalent(it->path(), skipToo, e)) continue;
        entries.push_back({ToUtf8(fs::relative(it->path(), input, e)), it->path(), isDir, isDir ? 0 : it->file_size(e)});
    }
    if (ec) Fail(ZARPACK_ERR_IO, "Failed to read folder: " + ec.message());
    std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) { return a.path < b.path; });
    return entries;
}

void CheckNames(const std::vector<InputEntry> &entries) {
    std::unordered_set<std::string> seen;
    for (const InputEntry &entry : entries) {
        std::string_view name = entry.path;
        name.remove_prefix(name.rfind('/') + 1); // npos + 1 == 0
        if (name.size() > kMaxNameBytes)
            Fail(ZARPACK_ERR_INPUT, "The name of " + Quoted(entry.path) +
                                        " is too long for a .zar archive (at most 127 bytes per file or folder name).");
        if (!seen.insert(FoldCase(entry.path)).second)
            Fail(ZARPACK_ERR_INPUT, Quoted(entry.path) + " differs from another item only in upper/lower case. "
                                                         "Names in a .zar archive are not case-sensitive.");
    }
}

/// Output sink for ZArchiveWriter, which reports through C callbacks.
class ArchiveFile {
public:
    explicit ArchiveFile(fs::path path) : path_(std::move(path)) {}

    static void OnNewFile(int32_t, void *self) {
        auto *f = static_cast<ArchiveFile *>(self);
        f->file_.open(f->path_, std::ios::binary | std::ios::trunc);
        if (!f->file_.is_open()) f->error_ = "Cannot create " + QuotedPath(f->path_) + ".";
    }

    static void OnWrite(const void *data, size_t length, void *self) {
        auto *f = static_cast<ArchiveFile *>(self);
        if (!f->error_.empty()) return;
        f->file_.write(static_cast<const char *>(data), static_cast<std::streamsize>(length));
        if (!f->file_) f->error_ = "Error writing the archive (disk full?).";
    }

    void Check() const {
        if (!error_.empty()) Fail(ZARPACK_ERR_IO, error_);
    }

    void Close() {
        file_.close();
        if (file_.fail() && error_.empty()) error_ = "Error writing the archive (disk full?).";
        Check();
    }

private:
    fs::path path_;
    std::ofstream file_;
    std::string error_;
};

void AppendFile(ZArchiveWriter &writer, const InputEntry &entry, const ArchiveFile &out,
                ProgressReporter &progress, std::vector<char> &buffer) {
    progress.BeginFile(entry.path);
    if (!writer.StartNewFile(entry.path.c_str())) Fail(ZARPACK_ERR_IO, "Failed to add " + Quoted(entry.path) + ".");
    std::ifstream in(entry.source, std::ios::binary);
    if (!in.is_open()) Fail(ZARPACK_ERR_IO, "Cannot read " + Quoted(entry.path) + ".");
    for (uint64_t remaining = entry.size; remaining > 0;) {
        in.read(buffer.data(), static_cast<std::streamsize>(std::min<uint64_t>(buffer.size(), remaining)));
        auto n = static_cast<uint64_t>(in.gcount());
        if (n == 0) Fail(ZARPACK_ERR_IO, "Error reading " + Quoted(entry.path) + ".");
        writer.AppendData(buffer.data(), static_cast<size_t>(n));
        out.Check();
        remaining -= n;
        progress.AddBytes(n);
    }
    progress.EndFile();
}

void WriteArchive(const std::vector<InputEntry> &entries, const fs::path &path, int level,
                  ProgressReporter &progress) {
    ArchiveFile out(path);
    ZArchiveWriter writer(ArchiveFile::OnNewFile, ArchiveFile::OnWrite, &out);
    out.Check();
    writer.SetCompressionLevel(level);
    std::vector<char> buffer(256 * 1024);
    progress.Start();
    for (const InputEntry &entry : entries) {
        if (!entry.isDir)
            AppendFile(writer, entry, out, progress, buffer);
        else if (!writer.MakeDir(entry.path.c_str(), false))
            Fail(ZARPACK_ERR_IO, "Failed to add folder " + Quoted(entry.path) + ".");
    }
    writer.Finalize();
    out.Close();
}

} // namespace
} // namespace zarpack

using namespace zarpack;

extern "C" size_t zarpack_resolve_output(const char *input_dir, const char *output, char *buf, size_t buf_size) {
    if (!input_dir) return 0;
    try {
        auto result = ResolveOutput(Normalize(FromUtf8(input_dir)), output);
        if (!result) return 0;
        std::string s = NativeUtf8(*result);
        CopyOut(s, buf, buf_size);
        return s.size() + 1;
    } catch (...) {
        return 0;
    }
}

extern "C" zarpack_status zarpack_pack(const zarpack_options *opt, char *out_path, size_t out_path_size,
                                       char *err_msg, size_t err_msg_size) {
    return Guard(err_msg, err_msg_size, [&] {
        if (!opt || !opt->input_dir) Fail(ZARPACK_ERR_INPUT, "No input directory given.");
        int level = opt->compression_level == 0 ? kDefaultLevel : opt->compression_level;
        if (level < 1 || level > 19) Fail(ZARPACK_ERR_INPUT, "Compression level must be between 1 and 19.");
        fs::path input = Normalize(FromUtf8(opt->input_dir));
        std::error_code ec;
        if (!fs::is_directory(input, ec)) Fail(ZARPACK_ERR_INPUT, QuotedPath(input) + " is not a folder.");
        auto output = ResolveOutput(input, opt->output);
        if (!output) Fail(ZARPACK_ERR_INPUT, "Cannot archive a drive root; pick a folder inside it.");

        PrepareOutput(*output, opt->overwrite != 0);
        fs::path partial = *output;
        TempFile temp(partial += ".part");
        auto entries = ScanInput(input, *output, temp.path(), opt->keep_system_files != 0);
        CheckNames(entries);

        uint64_t bytes = 0, files = 0;
        for (const InputEntry &e : entries)
            if (!e.isDir) { bytes += e.size; files++; }
        ProgressReporter progress(opt->progress, opt->user, bytes, files);
        WriteArchive(entries, temp.path(), level, progress);
        temp.MoveTo(*output);
        progress.Finish();

        CopyOut(NativeUtf8(*output), out_path, out_path_size);
    });
}
