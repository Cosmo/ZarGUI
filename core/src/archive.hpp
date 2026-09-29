#pragma once

#include "zarpack.h"

#include "zarchive/zarchivereader.h"

#include <memory>
#include <string>
#include <vector>

/// An opened archive with its validated tree, in depth-first order.
struct zarpack_archive {
    struct Item {
        std::string path;
        size_t nameOffset; // start of the last component in `path`
        uint64_t size;     // file size, or total file size of a directory
        int64_t parent;    // -1 for top level
        size_t subtreeEnd; // one past the last descendant
        ZArchiveNodeHandle node;
        bool isDir;
    };

    std::unique_ptr<ZArchiveReader> reader;
    std::vector<Item> items;
};
