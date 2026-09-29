#include "common.hpp"

#include <shlwapi.h>
#include <shobjidl.h>

namespace {

int openWindows = 0;

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

int Scale(HWND hwnd, int value) { return MulDiv(value, static_cast<int>(GetDpiForWindow(hwnd)), 96); }

HFONT CreateUiFont(HWND hwnd) {
    NONCLIENTMETRICSW metrics{sizeof(metrics)};
    if (!SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, GetDpiForWindow(hwnd))) {
        LOGFONTW font{};
        font.lfHeight = -MulDiv(9, static_cast<int>(GetDpiForWindow(hwnd)), 72);
        font.lfWeight = FW_NORMAL;
        font.lfQuality = CLEARTYPE_QUALITY;
        wcscpy_s(font.lfFaceName, L"Segoe UI");
        return CreateFontIndirectW(&font);
    }
    return CreateFontIndirectW(&metrics.lfMessageFont);
}

void ApplyFont(HWND parent, HFONT font) {
    EnumChildWindows(
        parent,
        [](HWND child, LPARAM font) {
            SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(font), TRUE);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(font));
}

HWND CreateChild(HWND parent, const wchar_t *className, const wchar_t *text, DWORD style, int id, DWORD exStyle) {
    return CreateWindowExW(exStyle, className, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, parent,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
}

void SetText(HWND hwnd, const std::wstring &text) { SetWindowTextW(hwnd, text.c_str()); }

std::wstring GetText(HWND hwnd) {
    std::wstring text(GetWindowTextLengthW(hwnd) + 1, L'\0');
    text.resize(GetWindowTextW(hwnd, text.data(), static_cast<int>(text.size())));
    return text;
}

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

bool Confirm(HWND owner, const std::wstring &title, const std::wstring &message, const wchar_t *action,
             const wchar_t *dismiss) {
    TASKDIALOG_BUTTON buttons[] = {{IDYES, action}, {IDCANCEL, dismiss}};
    TASKDIALOGCONFIG config{sizeof(config)};
    config.hwndParent = owner;
    config.dwFlags = TDF_POSITION_RELATIVE_TO_WINDOW | TDF_ALLOW_DIALOG_CANCELLATION;
    config.pszWindowTitle = L"ZarGUI";
    config.pszMainInstruction = title.c_str();
    config.pszContent = message.c_str();
    config.pButtons = buttons;
    config.cButtons = dismiss ? 2 : 1;
    config.dwCommonButtons = dismiss ? 0 : TDCBF_CANCEL_BUTTON;
    config.nDefaultButton = IDCANCEL;
    int chosen = IDCANCEL;
    TaskDialogIndirect(&config, &chosen, nullptr, nullptr);
    return chosen == IDYES;
}

void ShowError(HWND owner, const std::wstring &title, const std::wstring &message) {
    TaskDialog(owner, nullptr, L"ZarGUI", title.c_str(), message.c_str(), TDCBF_OK_BUTTON, TD_WARNING_ICON,
               nullptr);
}

void ShowAbout(HWND owner) {
    TaskDialog(owner, nullptr, L"About ZarGUI", L"ZarGUI 0.2.0",
               L"Creates, browses and extracts ZArchive (.zar) files.\n\n"
               L"MIT License. Uses ZArchive and Zstandard; see THIRD_PARTY_NOTICES.md.",
               TDCBF_OK_BUTTON, TD_INFORMATION_ICON, nullptr);
}

void WindowOpened() { openWindows++; }

void WindowClosed() {
    if (--openWindows == 0) PostQuitMessage(0);
}

void CloseAllWindows() {
    EnumThreadWindows(
        GetCurrentThreadId(),
        [](HWND hwnd, LPARAM) {
            if (!GetWindow(hwnd, GW_OWNER) && IsWindowVisible(hwnd)) PostMessageW(hwnd, WM_CLOSE, 0, 0);
            return TRUE;
        },
        0);
}

void ProgressWindow::Open(HWND owner, const std::wstring &title, const std::wstring &line) {
    Close();
    if (FAILED(CoCreateInstance(CLSID_ProgressDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog_)))) {
        dialog_ = nullptr;
        return;
    }
    dialog_->SetTitle(title.c_str());
    dialog_->SetLine(1, line.c_str(), FALSE, nullptr);
    dialog_->SetCancelMsg(L"Cancelling\u2026", nullptr);
    dialog_->StartProgressDialog(owner, nullptr, PROGDLG_NORMAL | PROGDLG_AUTOTIME | PROGDLG_NOMINIMIZE, nullptr);
    dialog_->Timer(PDTIMER_RESET, nullptr);
}

void ProgressWindow::Update(uint64_t done, uint64_t total, const std::wstring &detail) {
    if (!dialog_) return;
    if (total > 0) dialog_->SetProgress64(done, total);
    dialog_->SetLine(2, detail.c_str(), TRUE, nullptr);
}

bool ProgressWindow::Cancelled() const { return dialog_ && dialog_->HasUserCancelled(); }

void ProgressWindow::Close() {
    if (!dialog_) return;
    dialog_->StopProgressDialog();
    dialog_->Release();
    dialog_ = nullptr;
}
