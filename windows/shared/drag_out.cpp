#include "drag_out.hpp"

#include "archive.hpp"
#include "common.hpp"

#include <shlobj.h>
#include <shobjidl.h>


namespace {

HGLOBAL CopyGlobal(HGLOBAL source) {
    SIZE_T size = GlobalSize(source);
    HGLOBAL copy = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!copy) return nullptr;
    void *from = GlobalLock(source);
    void *to = GlobalLock(copy);
    if (from && to) memcpy(to, from, size);
    GlobalUnlock(copy);
    GlobalUnlock(source);
    return copy;
}

/// Reads one file entry from the archive on demand. Explorer may call it from
/// any thread; the core's reads are thread-safe.
class EntryStream final : public IStream {
public:
    EntryStream(std::shared_ptr<Archive> archive, size_t index, uint64_t size, std::wstring name)
        : archive_(std::move(archive)), index_(index), size_(size), name_(std::move(name)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **object) override {
        if (riid == IID_IUnknown || riid == IID_ISequentialStream || riid == IID_IStream) {
            *object = static_cast<IStream *>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs_); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG refs = InterlockedDecrement(&refs_);
        if (refs == 0) delete this;
        return refs;
    }

    HRESULT STDMETHODCALLTYPE Read(void *buffer, ULONG length, ULONG *read) override {
        ULONG total = 0;
        while (total < length && position_ < size_) {
            int64_t n = zarpack_read(archive_->handle(), index_, position_, static_cast<char *>(buffer) + total,
                                     length - total);
            if (n <= 0) {
                if (read) *read = total;
                return STG_E_READFAULT; // damaged archive
            }
            total += static_cast<ULONG>(n);
            position_ += static_cast<uint64_t>(n);
        }
        if (read) *read = total;
        return total < length ? S_FALSE : S_OK;
    }

    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER *newPosition) override {
        int64_t base = origin == STREAM_SEEK_SET ? 0
                       : origin == STREAM_SEEK_CUR ? static_cast<int64_t>(position_)
                       : origin == STREAM_SEEK_END ? static_cast<int64_t>(size_)
                                                   : -1;
        if (base < 0 || base + move.QuadPart < 0) return STG_E_INVALIDFUNCTION;
        position_ = static_cast<uint64_t>(base + move.QuadPart);
        if (newPosition) newPosition->QuadPart = position_;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Stat(STATSTG *stat, DWORD flags) override {
        *stat = {};
        stat->type = STGTY_STREAM;
        stat->cbSize.QuadPart = size_;
        stat->grfMode = STGM_READ;
        if (!(flags & STATFLAG_NONAME)) {
            size_t bytes = (name_.size() + 1) * sizeof(wchar_t);
            stat->pwcsName = static_cast<LPOLESTR>(CoTaskMemAlloc(bytes));
            if (!stat->pwcsName) return STG_E_INSUFFICIENTMEMORY;
            memcpy(stat->pwcsName, name_.c_str(), bytes);
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Write(const void *, ULONG, ULONG *) override { return STG_E_ACCESSDENIED; }
    HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER) override { return STG_E_ACCESSDENIED; }
    HRESULT STDMETHODCALLTYPE CopyTo(IStream *, ULARGE_INTEGER, ULARGE_INTEGER *, ULARGE_INTEGER *) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE Commit(DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Revert() override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Clone(IStream **) override { return E_NOTIMPL; }

private:
    std::shared_ptr<Archive> archive_;
    size_t index_;
    uint64_t size_;
    std::wstring name_;
    uint64_t position_ = 0;
    LONG refs_ = 1;
};

/// Offers archive entries as CFSTR_FILEDESCRIPTOR / CFSTR_FILECONTENTS, like File
/// Explorer's own zip folders. Supports asynchronous transfers so the drop target
/// copies on its own thread with its standard progress and conflict dialogs.
class VirtualFiles final : public IDataObject, public IDataObjectAsyncCapability {
public:
    struct Item {
        size_t index;
        std::wstring relativePath;
        uint64_t size;
        bool isDir;
    };

    VirtualFiles(std::shared_ptr<Archive> archive, std::vector<Item> items)
        : archive_(std::move(archive)), items_(std::move(items)) {}

    ~VirtualFiles() {
        for (auto &stored : stored_) ReleaseStgMedium(&stored.medium);
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **object) override {
        if (riid == IID_IUnknown || riid == IID_IDataObject) *object = static_cast<IDataObject *>(this);
        else if (riid == IID_IDataObjectAsyncCapability) *object = static_cast<IDataObjectAsyncCapability *>(this);
        else {
            *object = nullptr;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs_); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG refs = InterlockedDecrement(&refs_);
        if (refs == 0) delete this;
        return refs;
    }

    HRESULT STDMETHODCALLTYPE GetData(FORMATETC *format, STGMEDIUM *medium) override {
        *medium = {};
        if (format->cfFormat == DescriptorFormat() && (format->tymed & TYMED_HGLOBAL)) {
            medium->tymed = TYMED_HGLOBAL;
            medium->hGlobal = Descriptors();
            return medium->hGlobal ? S_OK : E_OUTOFMEMORY;
        }
        if (format->cfFormat == ContentsFormat() && (format->tymed & TYMED_ISTREAM)) {
            if (format->lindex < 0 || static_cast<size_t>(format->lindex) >= items_.size()) return DV_E_LINDEX;
            const Item &item = items_[static_cast<size_t>(format->lindex)];
            if (item.isDir) return DV_E_LINDEX;
            medium->tymed = TYMED_ISTREAM;
            medium->pstm = new EntryStream(archive_, item.index, item.size, FileName(item.relativePath));
            return S_OK;
        }
        for (auto &stored : stored_) {
            if (stored.format.cfFormat == format->cfFormat && (stored.format.tymed & format->tymed)) {
                medium->tymed = TYMED_HGLOBAL;
                medium->hGlobal = CopyGlobal(stored.medium.hGlobal);
                return medium->hGlobal ? S_OK : E_OUTOFMEMORY;
            }
        }
        return DV_E_FORMATETC;
    }

    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC *, STGMEDIUM *) override { return E_NOTIMPL; }

    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC *format) override {
        if (format->cfFormat == DescriptorFormat()) return (format->tymed & TYMED_HGLOBAL) ? S_OK : DV_E_TYMED;
        if (format->cfFormat == ContentsFormat()) return (format->tymed & TYMED_ISTREAM) ? S_OK : DV_E_TYMED;
        for (auto &stored : stored_)
            if (stored.format.cfFormat == format->cfFormat) return S_OK;
        return DV_E_FORMATETC;
    }

    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC *, FORMATETC *out) override {
        out->ptd = nullptr;
        return E_NOTIMPL;
    }

    /// The shell stores its drag image and drop results here.
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC *format, STGMEDIUM *medium, BOOL release) override {
        if (medium->tymed != TYMED_HGLOBAL) {
            if (release) ReleaseStgMedium(medium);
            return E_NOTIMPL;
        }
        Stored stored{*format, {}};
        stored.format.ptd = nullptr;
        stored.medium.tymed = TYMED_HGLOBAL;
        // Take the memory only if it is ours to free outright; otherwise keep a copy.
        bool take = release && !medium->pUnkForRelease;
        stored.medium.hGlobal = take ? medium->hGlobal : CopyGlobal(medium->hGlobal);
        if (release && !take) ReleaseStgMedium(medium);
        for (auto &existing : stored_) {
            if (existing.format.cfFormat == format->cfFormat) {
                ReleaseStgMedium(&existing.medium);
                existing = stored;
                return S_OK;
            }
        }
        stored_.push_back(stored);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD direction, IEnumFORMATETC **formats) override {
        if (direction != DATADIR_GET) return E_NOTIMPL;
        FORMATETC offered[] = {
            {DescriptorFormat(), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL},
            {ContentsFormat(), nullptr, DVASPECT_CONTENT, -1, TYMED_ISTREAM},
        };
        return SHCreateStdEnumFmtEtc(ARRAYSIZE(offered), offered, formats);
    }

    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC *, DWORD, IAdviseSink *, DWORD *) override {
        return OLE_E_ADVISENOTSUPPORTED;
    }
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA **) override { return OLE_E_ADVISENOTSUPPORTED; }

    HRESULT STDMETHODCALLTYPE SetAsyncMode(BOOL async) override {
        async_ = async;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetAsyncMode(BOOL *async) override {
        *async = async_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE StartOperation(IBindCtx *) override {
        inOperation_ = TRUE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE InOperation(BOOL *inOperation) override {
        *inOperation = inOperation_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE EndOperation(HRESULT, IBindCtx *, DWORD) override {
        inOperation_ = FALSE;
        return S_OK;
    }

private:
    struct Stored {
        FORMATETC format;
        STGMEDIUM medium;
    };

    static CLIPFORMAT DescriptorFormat() {
        static auto format = static_cast<CLIPFORMAT>(RegisterClipboardFormatW(CFSTR_FILEDESCRIPTORW));
        return format;
    }
    static CLIPFORMAT ContentsFormat() {
        static auto format = static_cast<CLIPFORMAT>(RegisterClipboardFormatW(CFSTR_FILECONTENTS));
        return format;
    }

    HGLOBAL Descriptors() const {
        size_t bytes = sizeof(FILEGROUPDESCRIPTORW) + (items_.size() - 1) * sizeof(FILEDESCRIPTORW);
        HGLOBAL global = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
        auto *group = static_cast<FILEGROUPDESCRIPTORW *>(GlobalLock(global));
        if (!group) return nullptr;
        group->cItems = static_cast<UINT>(items_.size());
        for (size_t i = 0; i < items_.size(); i++) {
            const Item &item = items_[i];
            FILEDESCRIPTORW &d = group->fgd[i];
            d.dwFlags = FD_ATTRIBUTES | FD_PROGRESSUI | (item.isDir ? 0 : FD_FILESIZE);
            d.dwFileAttributes = item.isDir ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
            d.nFileSizeLow = static_cast<DWORD>(item.size);
            d.nFileSizeHigh = static_cast<DWORD>(item.size >> 32);
            wcsncpy_s(d.cFileName, item.relativePath.c_str(), _TRUNCATE);
        }
        GlobalUnlock(global);
        return global;
    }

    std::shared_ptr<Archive> archive_;
    std::vector<Item> items_;
    std::vector<Stored> stored_;
    LONG refs_ = 1;
    BOOL async_ = TRUE;
    BOOL inOperation_ = FALSE;
};

} // namespace

bool DragArchiveEntries(HWND window, const std::shared_ptr<Archive> &archive, const std::vector<size_t> &roots,
                        std::wstring &error) {
    std::vector<VirtualFiles::Item> items;
    const auto &entries = archive->entries();
    for (size_t root : roots) {
        for (size_t i = root, end = archive->SubtreeEnd(root); i < end; i++) {
            std::wstring relative = archive->RelativePath(i, root);
            if (relative.size() >= MAX_PATH) {
                error = Quoted(relative) + L" has a path that is too long to drag. Use Extract instead.";
                return false;
            }
            items.push_back({i, relative, entries[i].size, entries[i].isDir});
        }
    }
    if (items.empty()) return false;

    auto *data = new VirtualFiles(archive, std::move(items));
    DWORD effect = DROPEFFECT_NONE;
    SHDoDragDrop(window, data, nullptr, DROPEFFECT_COPY, &effect);
    data->Release();
    return true;
}
