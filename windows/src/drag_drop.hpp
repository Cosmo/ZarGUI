#pragma once

#include "common.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

class Archive;

/// Accepts files and folders dropped from File Explorer.
class FileDropTarget final : public IDropTarget {
public:
    struct Callbacks {
        std::function<void(bool)> highlight;
        std::function<void(std::vector<std::wstring>)> drop;
    };

    explicit FileDropTarget(Callbacks callbacks) : callbacks_(std::move(callbacks)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **object) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;

    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject *data, DWORD keys, POINTL point, DWORD *effect) override;
    HRESULT STDMETHODCALLTYPE DragOver(DWORD keys, POINTL point, DWORD *effect) override;
    HRESULT STDMETHODCALLTYPE DragLeave() override;
    HRESULT STDMETHODCALLTYPE Drop(IDataObject *data, DWORD keys, POINTL point, DWORD *effect) override;

private:
    Callbacks callbacks_;
    LONG refs_ = 1;
    bool accepts_ = false;
};

/// Starts dragging archive entries out of `window`. Drop targets such as File Explorer
/// receive them as virtual files and read their contents straight from the archive
/// while copying, so nothing is extracted up front. Returns false (and sets `error`)
/// if the items can't be offered this way, e.g. because a path is too long.
bool DragArchiveEntries(HWND window, const std::shared_ptr<Archive> &archive, const std::vector<size_t> &roots,
                        std::wstring &error);
