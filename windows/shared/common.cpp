#include "common.hpp"

#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>

namespace {

template <class T>
struct ComPtr {
    T *p = nullptr;
    ~ComPtr() {
        if (p) p->Release();
    }
    T **operator&() { return &p; }
    T *operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

std::optional<std::wstring> ItemPath(IShellItem *item) {
    PWSTR raw = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &raw))) return std::nullopt;
    std::wstring path(raw);
    CoTaskMemFree(raw);
    return path;
}

} // namespace

std::string ToUtf8(std::wstring_view s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring ToWide(std::string_view s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

std::wstring FileName(const std::wstring &path) {
    std::wstring trimmed = path;
    while (trimmed.size() > 3 && (trimmed.back() == L'\\' || trimmed.back() == L'/')) trimmed.pop_back();
    return PathFindFileNameW(trimmed.c_str());
}

std::wstring FileStem(const std::wstring &path) {
    std::wstring name = FileName(path);
    size_t dot = name.rfind(L'.');
    return dot == std::wstring::npos || dot == 0 ? name : name.substr(0, dot);
}

std::wstring ParentFolder(const std::wstring &path) {
    std::wstring parent = path;
    while (parent.size() > 3 && (parent.back() == L'\\' || parent.back() == L'/')) parent.pop_back();
    size_t slash = parent.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : parent.substr(0, slash == 2 ? 3 : slash);
}

std::wstring JoinPath(const std::wstring &folder, const std::wstring &name) {
    if (folder.empty()) return name;
    wchar_t last = folder.back();
    return last == L'\\' || last == L'/' ? folder + name : folder + L'\\' + name;
}

bool IsFolder(const std::wstring &path) {
    DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

bool Exists(const std::wstring &path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

bool IsArchive(const std::wstring &path) {
    return !IsFolder(path) && Exists(path) && _wcsicmp(PathFindExtensionW(path.c_str()), L".zar") == 0;
}

std::wstring UniquePath(const std::wstring &folder, const std::wstring &stem, const std::wstring &extension) {
    std::wstring candidate = JoinPath(folder, stem + extension);
    for (int n = 2; Exists(candidate); n++)
        candidate = JoinPath(folder, stem + L" (" + std::to_wstring(n) + L")" + extension);
    return candidate;
}

std::wstring FormatBytes(uint64_t bytes) {
    wchar_t buffer[64];
    StrFormatByteSizeEx(bytes, SFBS_FLAGS_ROUND_TO_NEAREST_DISPLAYED_DIGIT, buffer, ARRAYSIZE(buffer));
    return buffer;
}

std::wstring Quoted(const std::wstring &s) { return L"“" + s + L"”"; }

std::optional<std::wstring> PickFolder(HWND owner, const wchar_t *title) {
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
        return std::nullopt;
    FILEOPENDIALOGOPTIONS options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    dialog->SetTitle(title);
    if (dialog->Show(owner) != S_OK) return std::nullopt;
    ComPtr<IShellItem> item;
    if (FAILED(dialog->GetResult(&item))) return std::nullopt;
    return ItemPath(item.p);
}

std::vector<std::wstring> PickArchives(HWND owner) {
    std::vector<std::wstring> paths;
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
        return paths;
    FILEOPENDIALOGOPTIONS options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_ALLOWMULTISELECT | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST);
    COMDLG_FILTERSPEC filter[] = {{L"ZArchive (*.zar)", L"*.zar"}};
    dialog->SetFileTypes(ARRAYSIZE(filter), filter);
    if (dialog->Show(owner) != S_OK) return paths;
    ComPtr<IShellItemArray> items;
    if (FAILED(dialog->GetResults(&items))) return paths;
    DWORD count = 0;
    items->GetCount(&count);
    for (DWORD i = 0; i < count; i++) {
        ComPtr<IShellItem> item;
        if (SUCCEEDED(items->GetItemAt(i, &item)))
            if (auto path = ItemPath(item.p)) paths.push_back(*path);
    }
    return paths;
}

void RevealInExplorer(const std::wstring &path) {
    PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(path.c_str());
    if (!pidl) return;
    SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
    ILFree(pidl);
}
