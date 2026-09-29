#pragma once

#include "common.hpp"

#include <memory>
#include <string>
#include <vector>

class Archive;

/// Starts dragging archive entries out of `window`. Drop targets such as File Explorer
/// receive them as virtual files and read their contents straight from the archive
/// while copying, so nothing is extracted up front. Call while the left mouse button is
/// down; blocks until the drop or cancel. Returns false and sets `error` if the items
/// can't be offered this way (e.g. a path is too long) or the drag fails to start.
bool DragArchiveEntries(HWND window, const std::shared_ptr<Archive> &archive, const std::vector<size_t> &roots,
                        std::wstring &error);
