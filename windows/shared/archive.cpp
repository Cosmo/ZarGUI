#include "archive.hpp"

#include "common.hpp"

#include <algorithm>
#include <unordered_set>

std::shared_ptr<Archive> Archive::Open(const std::wstring &path, std::wstring &error) {
    zarpack_archive *handle = nullptr;
    char message[1024] = {};
    if (zarpack_open(ToUtf8(path).c_str(), &handle, message, sizeof(message)) != ZARPACK_OK) {
        error = ToWide(message);
        return nullptr;
    }
    return std::shared_ptr<Archive>(new Archive(handle, path));
}

Archive::Archive(zarpack_archive *handle, std::wstring path) : handle_(handle), path_(std::move(path)) {
    size_t count = zarpack_entry_count(handle);
    entries_.reserve(count);
    for (size_t i = 0; i < count; i++) {
        zarpack_entry e{};
        zarpack_entry_get(handle, i, &e);
        entries_.push_back({i, e.parent, ToWide(e.name), e.size, e.is_dir != 0, {}});
        if (e.parent >= 0) entries_[static_cast<size_t>(e.parent)].children.push_back(i); // parents come first
        else roots_.push_back(i);
        if (!e.is_dir) fileCount_++;
    }
    for (size_t r : roots_) totalSize_ += entries_[r].size;
}

Archive::~Archive() { zarpack_close(handle_); }

std::vector<size_t> Archive::TopLevel(const std::vector<size_t> &indices) const {
    std::unordered_set<size_t> selected(indices.begin(), indices.end());
    std::vector<size_t> result;
    for (size_t i : indices) {
        bool nested = false;
        for (int64_t p = entries_[i].parent; p >= 0 && !nested; p = entries_[static_cast<size_t>(p)].parent)
            nested = selected.count(static_cast<size_t>(p)) > 0;
        if (!nested) result.push_back(i);
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

std::wstring Archive::RelativePath(size_t index, size_t ancestor) const {
    std::wstring path = entries_[index].name;
    for (size_t i = index; i != ancestor;) {
        i = static_cast<size_t>(entries_[i].parent);
        path = entries_[i].name + L'\\' + path;
    }
    return path;
}
