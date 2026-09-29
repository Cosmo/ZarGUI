#include "archive.hpp"
#include "common.hpp"

#include <algorithm>
#include <stdexcept>
#include <unordered_set>

namespace zarpack {
namespace {

constexpr int kMaxDepth = 256;

[[noreturn]] void FailDamaged() {
    Fail(ZARPACK_ERR_INPUT, "This file is not a valid .zar archive, or it is damaged.");
}

struct Child {
    std::string name;
    ZArchiveNodeHandle node;
    bool isDir;
    uint64_t size;
};

/// The validated children of `dir`, directories first, then by name.
std::vector<Child> ReadChildren(ZArchiveReader &reader, ZArchiveNodeHandle dir,
                                std::unordered_set<ZArchiveNodeHandle> &visited) {
    std::vector<Child> children;
    std::unordered_set<std::string> names;
    uint32_t count = reader.GetDirEntryCount(dir);
    for (uint32_t i = 0; i < count; i++) {
        ZArchiveReader::DirEntry entry;
        ZArchiveNodeHandle node = reader.GetDirEntryNode(dir, i);
        if (node == ZARCHIVE_INVALID_NODE || !reader.GetDirEntry(dir, i, entry)) FailDamaged();
        bool unique = visited.insert(node).second && names.insert(FoldCase(entry.name)).second;
        if (!unique || !IsSafeName(entry.name)) FailDamaged();
        children.push_back({std::string(entry.name), node, entry.isDirectory, entry.isDirectory ? 0 : entry.size});
    }
    std::sort(children.begin(), children.end(), [](const Child &a, const Child &b) {
        if (a.isDir != b.isDir) return a.isDir;
        return std::make_pair(FoldCase(a.name), a.name) < std::make_pair(FoldCase(b.name), b.name);
    });
    return children;
}

/// Appends the subtree of `dir` to the archive's items; returns its total file size.
uint64_t LoadDir(zarpack_archive &archive, ZArchiveNodeHandle dir, int64_t parent, const std::string &prefix,
                 int depth, std::unordered_set<ZArchiveNodeHandle> &visited) {
    if (depth > kMaxDepth) FailDamaged();
    uint64_t total = 0;
    for (Child &child : ReadChildren(*archive.reader, dir, visited)) {
        size_t index = archive.items.size();
        archive.items.push_back({prefix + child.name, prefix.size(), child.size, parent, 0, child.node, child.isDir});
        if (child.isDir) {
            std::string path = archive.items[index].path + "/";
            archive.items[index].size = LoadDir(archive, child.node, static_cast<int64_t>(index), path, depth + 1, visited);
        }
        archive.items[index].subtreeEnd = archive.items.size();
        total += archive.items[index].size;
    }
    return total;
}

std::unique_ptr<zarpack_archive> Open(const fs::path &path) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) Fail(ZARPACK_ERR_INPUT, QuotedPath(path) + " is not a file.");
    auto archive = std::make_unique<zarpack_archive>();
    try {
        archive->reader.reset(ZArchiveReader::OpenFromFile(path));
        if (!archive->reader) FailDamaged();
        ZArchiveNodeHandle root = archive->reader->LookUp("", false, true);
        if (root == ZARCHIVE_INVALID_NODE) FailDamaged();
        std::unordered_set<ZArchiveNodeHandle> visited{root};
        LoadDir(*archive, root, -1, "", 0, visited);
    } catch (const std::out_of_range &) { // the reader's bounds checks on a malformed tree
        FailDamaged();
    } catch (const std::length_error &) {
        FailDamaged();
    }
    return archive;
}

} // namespace
} // namespace zarpack

using namespace zarpack;

extern "C" zarpack_status zarpack_open(const char *archive_path, zarpack_archive **out_archive,
                                       char *err_msg, size_t err_msg_size) {
    if (out_archive) *out_archive = nullptr;
    return Guard(err_msg, err_msg_size, [&] {
        if (!archive_path || !out_archive) Fail(ZARPACK_ERR_INPUT, "No archive given.");
        *out_archive = Open(FromUtf8(archive_path)).release();
    });
}

extern "C" void zarpack_close(zarpack_archive *archive) { delete archive; }

extern "C" size_t zarpack_entry_count(const zarpack_archive *archive) {
    return archive ? archive->items.size() : 0;
}

extern "C" int zarpack_entry_get(const zarpack_archive *archive, size_t index, zarpack_entry *out) {
    if (!archive || !out || index >= archive->items.size()) return 0;
    const auto &item = archive->items[index];
    *out = {item.path.c_str(), item.path.c_str() + item.nameOffset, item.size, item.parent, item.isDir ? 1 : 0};
    return 1;
}

extern "C" int64_t zarpack_read(zarpack_archive *archive, size_t index, uint64_t offset, void *buffer,
                                uint64_t length) {
    if (!archive || !buffer || index >= archive->items.size()) return -1;
    const auto &item = archive->items[index];
    if (item.isDir) return -1;
    if (offset >= item.size || length == 0) return 0;
    uint64_t wanted = std::min<uint64_t>(length, item.size - offset);
    try {
        uint64_t got = archive->reader->ReadFromFile(item.node, offset, wanted, buffer);
        return got == wanted ? static_cast<int64_t>(got) : -1;
    } catch (...) {
        return -1;
    }
}
