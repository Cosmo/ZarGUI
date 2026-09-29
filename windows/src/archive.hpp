#pragma once

#include "zarpack.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

/// An opened .zar archive. Shared, because a drag and drop may still read from
/// it after its window has closed.
class Archive {
public:
    struct Entry {
        size_t index;
        int64_t parent; // -1 for top level
        std::wstring name;
        uint64_t size;
        bool isDir;
        std::vector<size_t> children;
    };

    /// Returns null and sets `error` if the file can't be opened.
    static std::shared_ptr<Archive> Open(const std::wstring &path, std::wstring &error);

    Archive(const Archive &) = delete;
    Archive &operator=(const Archive &) = delete;
    ~Archive();

    zarpack_archive *handle() const { return handle_; }
    const std::wstring &path() const { return path_; }
    const std::vector<Entry> &entries() const { return entries_; }
    const std::vector<size_t> &roots() const { return roots_; }
    size_t fileCount() const { return fileCount_; }
    uint64_t totalSize() const { return totalSize_; }

    const std::vector<size_t> &Children(int64_t folder) const {
        return folder < 0 ? roots_ : entries_[static_cast<size_t>(folder)].children;
    }

    /// The given entries without those inside another given folder.
    std::vector<size_t> TopLevel(const std::vector<size_t> &indices) const;

    /// One past the last entry inside `index` (entries are in depth-first order).
    size_t SubtreeEnd(size_t index) const {
        const auto &children = entries_[index].children;
        return children.empty() ? index + 1 : SubtreeEnd(children.back());
    }

    /// Path of `index` relative to the parent of `ancestor`, with backslashes.
    std::wstring RelativePath(size_t index, size_t ancestor) const;

private:
    Archive(zarpack_archive *handle, std::wstring path);

    zarpack_archive *handle_;
    std::wstring path_;
    std::vector<Entry> entries_;
    std::vector<size_t> roots_;
    size_t fileCount_ = 0;
    uint64_t totalSize_ = 0;
};
