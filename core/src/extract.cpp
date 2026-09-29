#include "archive.hpp"
#include "common.hpp"

#include <algorithm>
#include <fstream>
#include <vector>

namespace zarpack {
namespace {

using Item = zarpack_archive::Item;

struct Step {
    const Item *item;
    fs::path target;
    bool existed;
};

struct Plan {
    std::vector<Step> steps;
    uint64_t bytes = 0;
    uint64_t files = 0;
};

/// Indices of the subtrees to extract, without items already inside a selected folder.
std::vector<size_t> SelectRoots(const zarpack_archive &archive, const size_t *indices, size_t count) {
    std::vector<size_t> roots;
    if (count == 0) {
        for (size_t i = 0; i < archive.items.size(); i = archive.items[i].subtreeEnd) roots.push_back(i);
        return roots;
    }
    std::vector<size_t> selected(indices, indices + count);
    std::sort(selected.begin(), selected.end());
    size_t coveredUntil = 0;
    for (size_t i : selected) {
        if (i >= archive.items.size()) Fail(ZARPACK_ERR_INPUT, "Invalid selection.");
        if (i < coveredUntil) continue;
        roots.push_back(i);
        coveredUntil = archive.items[i].subtreeEnd;
    }
    return roots;
}

Step PlanStep(const Item &item, fs::path target, bool overwrite) {
    if (!IsNativeFileName(std::string_view(item.path).substr(item.nameOffset)))
        Fail(ZARPACK_ERR_IO, Quoted(item.path) + " has a name that is not allowed on this system.");
    Step step{&item, std::move(target), false};

    std::error_code ec;
    auto status = fs::symlink_status(step.target, ec);
    if (fs::is_symlink(status)) Fail(ZARPACK_ERR_IO, QuotedPath(step.target) + " is a symbolic link.");
    step.existed = fs::exists(status);
    if (!step.existed) return step;

    bool isFolder = fs::is_directory(status);
    if (item.isDir && !isFolder) Fail(ZARPACK_ERR_IO, QuotedPath(step.target) + " exists and is not a folder.");
    if (!item.isDir && isFolder) Fail(ZARPACK_ERR_IO, QuotedPath(step.target) + " exists and is a folder.");
    if (!item.isDir && !overwrite) Fail(ZARPACK_ERR_OUTPUT_EXISTS, QuotedPath(step.target) + " already exists.");
    return step;
}

/// Adds the entry `r` (and everything inside it) to the plan, placing the entry itself at `rootTarget`.
void AddSubtree(Plan &plan, const zarpack_archive &archive, size_t r, const fs::path &rootTarget, bool overwrite) {
    const Item &root = archive.items[r];
    for (size_t i = r; i < root.subtreeEnd; i++) {
        const Item &item = archive.items[i];
        fs::path target = i == r ? rootTarget : rootTarget / FromUtf8(std::string_view(item.path).substr(root.path.size() + 1));
        plan.steps.push_back(PlanStep(item, std::move(target), overwrite));
        if (!item.isDir) {
            plan.bytes += item.size;
            plan.files++;
        }
    }
}

/// Records what an extraction creates and removes it again unless committed.
class Transaction {
public:
    Transaction() = default;
    Transaction(const Transaction &) = delete;
    Transaction &operator=(const Transaction &) = delete;

    ~Transaction() {
        if (committed_) return;
        std::error_code ec;
        for (const fs::path &f : files_) fs::remove(f, ec);
        for (auto d = dirs_.rbegin(); d != dirs_.rend(); ++d) fs::remove(*d, ec); // only removes empty folders
    }

    /// Creates `dest` and any missing parents.
    void CreateDestination(const fs::path &dest) {
        std::error_code ec;
        if (fs::exists(dest, ec)) {
            if (!fs::is_directory(dest, ec)) Fail(ZARPACK_ERR_IO, QuotedPath(dest) + " is not a folder.");
            return;
        }
        if (dest.has_parent_path() && dest.parent_path() != dest) CreateDestination(dest.parent_path());
        CreateDirectory(dest);
    }

    void CreateDirectory(const fs::path &dir) {
        std::error_code ec;
        if (!fs::create_directory(dir, ec)) Fail(ZARPACK_ERR_IO, "Cannot create folder " + QuotedPath(dir) + ".");
        dirs_.push_back(dir);
    }

    void AddFile(const fs::path &file) { files_.push_back(file); }
    void Commit() { committed_ = true; }

private:
    std::vector<fs::path> dirs_;
    std::vector<fs::path> files_;
    bool committed_ = false;
};

void ExtractFile(ZArchiveReader &reader, const Step &step, Transaction &transaction, ProgressReporter &progress,
                 std::vector<char> &buffer) {
    const Item &item = *step.item;
    progress.BeginFile(item.path);
    fs::path partial = step.target;
    TempFile temp(partial += ".zargui-part");
    {
        std::ofstream out(temp.path(), std::ios::binary | std::ios::trunc);
        if (!out.is_open()) Fail(ZARPACK_ERR_IO, "Cannot write " + QuotedPath(step.target) + ".");
        for (uint64_t offset = 0; offset < item.size;) {
            uint64_t length = std::min<uint64_t>(buffer.size(), item.size - offset);
            if (reader.ReadFromFile(item.node, offset, length, buffer.data()) != length)
                Fail(ZARPACK_ERR_IO, Quoted(item.path) + " could not be read; the archive is damaged.");
            out.write(buffer.data(), static_cast<std::streamsize>(length));
            if (!out) Fail(ZARPACK_ERR_IO, "Error writing " + QuotedPath(step.target) + " (disk full?).");
            offset += length;
            progress.AddBytes(length);
        }
        out.close();
        if (out.fail()) Fail(ZARPACK_ERR_IO, "Error writing " + QuotedPath(step.target) + " (disk full?).");
    }
    temp.MoveTo(step.target);
    if (!step.existed) transaction.AddFile(step.target); // replaced files are not removed on rollback
    progress.EndFile();
}

/// Checks were done while planning; this writes everything or, on failure, nothing.
void Execute(zarpack_archive &archive, const Plan &plan, const fs::path &parent, zarpack_progress_fn progressFn,
             void *user) {
    Transaction transaction;
    transaction.CreateDestination(parent);
    ProgressReporter progress(progressFn, user, plan.bytes, plan.files);
    std::vector<char> buffer(256 * 1024);
    progress.Start();
    for (const Step &step : plan.steps) {
        if (!step.item->isDir)
            ExtractFile(*archive.reader, step, transaction, progress, buffer);
        else if (!step.existed)
            transaction.CreateDirectory(step.target);
    }
    transaction.Commit();
    progress.Finish();
}

} // namespace
} // namespace zarpack

using namespace zarpack;

extern "C" zarpack_status zarpack_extract(zarpack_archive *archive, const size_t *indices, size_t count,
                                          const char *dest_dir, int overwrite, zarpack_progress_fn progress_fn,
                                          void *user, char *err_msg, size_t err_msg_size) {
    return Guard(err_msg, err_msg_size, [&] {
        if (!archive || !dest_dir || (count > 0 && !indices)) Fail(ZARPACK_ERR_INPUT, "Nothing to extract.");
        fs::path dest = Normalize(FromUtf8(dest_dir));
        Plan plan;
        for (size_t r : SelectRoots(*archive, indices, count)) {
            const Item &root = archive->items[r];
            AddSubtree(plan, *archive, r, dest / FromUtf8(std::string_view(root.path).substr(root.nameOffset)),
                       overwrite != 0);
        }
        Execute(*archive, plan, dest, progress_fn, user);
    });
}

extern "C" zarpack_status zarpack_extract_entry(zarpack_archive *archive, size_t index, const char *target_path,
                                                int overwrite, zarpack_progress_fn progress_fn, void *user,
                                                char *err_msg, size_t err_msg_size) {
    return Guard(err_msg, err_msg_size, [&] {
        if (!archive || !target_path || index >= archive->items.size()) Fail(ZARPACK_ERR_INPUT, "Nothing to extract.");
        fs::path target = Normalize(FromUtf8(target_path));
        if (!target.has_filename()) Fail(ZARPACK_ERR_INPUT, "Invalid target path.");
        Plan plan;
        AddSubtree(plan, *archive, index, target, overwrite != 0);
        Execute(*archive, plan, target.parent_path(), progress_fn, user);
    });
}
