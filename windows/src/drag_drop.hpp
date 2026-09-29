#pragma once

#include "common.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

class Archive;

/// Accepts files and folders dropped from File Explorer, showing the shell's drag image over the window.
class FileDropTarget final : public IDropTarget {
public:
    using DropHandler = std::function<void(std::vector<std::wstring>)>;

    FileDropTarget(HWND window, DropHandler drop);
    ~FileDropTarget();

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **object) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;

    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject *data, DWORD keys, POINTL point, DWORD *effect) override;
    HRESULT STDMETHODCALLTYPE DragOver(DWORD keys, POINTL point, DWORD *effect) override;
    HRESULT STDMETHODCALLTYPE DragLeave() override;
    HRESULT STDMETHODCALLTYPE Drop(IDataObject *data, DWORD keys, POINTL point, DWORD *effect) override;

private:
    HWND window_;
    DropHandler drop_;
    IDropTargetHelper *helper_ = nullptr;
    LONG refs_ = 1;
    bool accepts_ = false;
};

/// Starts dragging archive entries out of `window`. Drop targets such as File Explorer
/// receive them as virtual files and read their contents straight from the archive
/// while copying, so nothing is extracted up front. Returns false (and sets `error`)
/// if the items can't be offered this way, e.g. because a path is too long.
bool DragArchiveEntries(HWND window, const std::shared_ptr<Archive> &archive, const std::vector<size_t> &roots,
                        std::wstring &error);
