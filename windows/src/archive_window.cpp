#include "archive_window.hpp"

#include "archive.hpp"
#include "drag_drop.hpp"
#include "resource.h"
#include "settings.hpp"

#include <shellapi.h>
#include <shlwapi.h>
#include <uxtheme.h>
#include <windowsx.h>

#include <algorithm>

namespace {

enum Column { kName, kSize, kType };
constexpr UINT_PTR kCancelTimer = 1;

} // namespace

void ArchiveWindow::Show(const std::wstring &path) {
    std::wstring error;
    auto archive = Archive::Open(path, error);
    if (!archive) {
        ShowError(nullptr, L"Can’t open " + Quoted(FileName(path)), error);
        return;
    }
    auto *window = new ArchiveWindow(std::move(archive));
    window->Create(L"ZarGUI.Archive", FileName(path) + L" - ZarGUI", 680, 460,
                   LoadMenuW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDR_ARCHIVE_MENU)));
    if (window->hwnd_) ShowWindow(window->hwnd_, SW_SHOWNORMAL);
}

ArchiveWindow::ArchiveWindow(std::shared_ptr<Archive> archive) : archive_(std::move(archive)) {}

ArchiveWindow::~ArchiveWindow() {
    if (font_) DeleteObject(font_);
}

LRESULT ArchiveWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        CreateControls();
        Navigate(-1);
        return 0;
    case WM_SIZE:
        SendMessageW(status_, WM_SIZE, 0, 0);
        Layout();
        return 0;
    case WM_GETMINMAXINFO: {
        auto *info = reinterpret_cast<MINMAXINFO *>(lParam);
        info->ptMinTrackSize = {Scale(hwnd_, 420), Scale(hwnd_, 260)};
        return 0;
    }
    case WM_DPICHANGED: {
        auto *rect = reinterpret_cast<RECT *>(lParam);
        SetWindowPos(hwnd_, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        UpdateFonts();
        Layout();
        return 0;
    }
    case WM_SETFOCUS:
        SetFocus(list_);
        return 0;
    case WM_INITMENUPOPUP:
        UpdateCommands();
        return 0;
    case WM_COMMAND:
        OnCommand(LOWORD(wParam));
        return 0;
    case WM_NOTIFY:
        return OnNotify(reinterpret_cast<NMHDR *>(lParam));
    case WM_CONTEXTMENU:
        if (reinterpret_cast<HWND>(wParam) == list_) ShowContextMenu({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
        return 0;
    case WM_TIMER:
        if (wParam == kCancelTimer && progress_.Cancelled()) job_.Cancel();
        return 0;
    case WM_APP_PROGRESS:
        OnProgress();
        return 0;
    case WM_APP_DONE:
        OnDone();
        return 0;
    case WM_CLOSE:
        job_.Cancel();
        DestroyWindow(hwnd_);
        return 0;
    case WM_DESTROY:
        progress_.Close();
        return 0;
    }
    return Window::HandleMessage(message, wParam, lParam);
}

void ArchiveWindow::OnCommand(int id) {
    switch (id) {
    case IDM_UP: GoUp(); break;
    case IDM_OPEN_FOLDER:
    case IDOK: OpenSelected(); break; // IDOK: Enter in the list
    case IDM_EXTRACT_SELECTED: ExtractSelected(); break;
    case IDM_EXTRACT_ALL: ExtractAll(); break;
    case IDM_CLOSE: PostMessageW(hwnd_, WM_CLOSE, 0, 0); break;
    case IDM_ABOUT: ShowAbout(hwnd_); break;
    case IDM_OPEN:
        for (const auto &path : PickArchives(hwnd_)) Show(path);
        break;
    }
}

LRESULT ArchiveWindow::OnNotify(NMHDR *header) {
    if (header->hwndFrom != list_) return 0;
    switch (header->code) {
    case LVN_GETDISPINFOW:
        GetDisplayInfo(reinterpret_cast<NMLVDISPINFOW *>(header));
        return 0;
    case LVN_ITEMCHANGED:
    case LVN_ODSTATECHANGED:
        UpdateStatus();
        return 0;
    case NM_DBLCLK:
        OpenSelected();
        return 0;
    case LVN_KEYDOWN: {
        auto *key = reinterpret_cast<NMLVKEYDOWN *>(header);
        if (key->wVKey == VK_BACK) GoUp();
        if (key->wVKey == 'A' && (GetKeyState(VK_CONTROL) & 0x8000))
            ListView_SetItemState(list_, -1, LVIS_SELECTED, LVIS_SELECTED);
        return 0;
    }
    case LVN_BEGINDRAG: {
        std::wstring error;
        auto roots = archive_->TopLevel(Selection());
        if (!DragArchiveEntries(hwnd_, archive_, roots, error) && !error.empty())
            ShowError(hwnd_, L"Can’t drag these items", error);
        return 0;
    }
    }
    return 0;
}

void ArchiveWindow::CreateControls() {
    CreateToolbar();
    address_ = CreateChild(hwnd_, WC_EDITW, L"", ES_READONLY | ES_AUTOHSCROLL, 0, WS_EX_CLIENTEDGE);

    list_ = CreateChild(hwnd_, WC_LISTVIEWW, L"",
                        WS_TABSTOP | LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS);
    SetWindowTheme(list_, L"Explorer", nullptr);
    ListView_SetExtendedListViewStyle(list_, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    SHFILEINFOW info{};
    auto images = reinterpret_cast<HIMAGELIST>(SHGetFileInfoW(L"file", FILE_ATTRIBUTE_NORMAL, &info, sizeof(info),
                                                              SHGFI_USEFILEATTRIBUTES | SHGFI_SYSICONINDEX |
                                                                  SHGFI_SMALLICON));
    ListView_SetImageList(list_, images, LVSIL_SMALL);

    struct { const wchar_t *title; int width; int format; } columns[] = {
        {L"Name", 300, LVCFMT_LEFT}, {L"Size", 90, LVCFMT_RIGHT}, {L"Type", 180, LVCFMT_LEFT}};
    for (int i = 0; i < 3; i++) {
        LVCOLUMNW column{LVCF_TEXT | LVCF_WIDTH | LVCF_FMT};
        column.pszText = const_cast<wchar_t *>(columns[i].title);
        column.cx = Scale(hwnd_, columns[i].width);
        column.fmt = columns[i].format;
        ListView_InsertColumn(list_, i, &column);
    }

    status_ = CreateChild(hwnd_, STATUSCLASSNAMEW, L"", SBARS_SIZEGRIP);
    UpdateFonts();
}

/// A text-only toolbar, like the command bars of Windows' own apps.
void ArchiveWindow::CreateToolbar() {
    toolbar_ = CreateChild(hwnd_, TOOLBARCLASSNAMEW, L"",
                           TBSTYLE_FLAT | TBSTYLE_LIST | TBSTYLE_TOOLTIPS | CCS_NODIVIDER | CCS_NORESIZE |
                               CCS_NOPARENTALIGN);
    SendMessageW(toolbar_, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    SendMessageW(toolbar_, TB_SETIMAGELIST, 0, 0);
    TBBUTTON buttons[] = {
        {I_IMAGENONE, IDM_UP, TBSTATE_ENABLED, BTNS_AUTOSIZE | BTNS_SHOWTEXT, {}, 0,
         reinterpret_cast<INT_PTR>(L"Up")},
        {0, 0, TBSTATE_ENABLED, BTNS_SEP, {}, 0, 0},
        {I_IMAGENONE, IDM_EXTRACT_ALL, TBSTATE_ENABLED, BTNS_AUTOSIZE | BTNS_SHOWTEXT, {}, 0,
         reinterpret_cast<INT_PTR>(L"Extract all…")},
        {I_IMAGENONE, IDM_EXTRACT_SELECTED, TBSTATE_ENABLED, BTNS_AUTOSIZE | BTNS_SHOWTEXT, {}, 0,
         reinterpret_cast<INT_PTR>(L"Extract selected…")},
    };
    SendMessageW(toolbar_, TB_ADDBUTTONSW, ARRAYSIZE(buttons), reinterpret_cast<LPARAM>(buttons));
}

void ArchiveWindow::UpdateFonts() {
    HFONT old = font_;
    font_ = CreateUiFont(hwnd_);
    ApplyFont(hwnd_, font_);
    SendMessageW(toolbar_, TB_AUTOSIZE, 0, 0);
    if (old) DeleteObject(old);
}

void ArchiveWindow::Layout() {
    RECT client, bar;
    GetClientRect(hwnd_, &client);
    GetWindowRect(status_, &bar);
    int statusHeight = bar.bottom - bar.top;
    int margin = Scale(hwnd_, 4);

    SIZE toolbarSize{};
    SendMessageW(toolbar_, TB_GETMAXSIZE, 0, reinterpret_cast<LPARAM>(&toolbarSize));
    int rowHeight = std::max<int>(toolbarSize.cy, Scale(hwnd_, 24));
    MoveWindow(toolbar_, 0, margin, toolbarSize.cx, rowHeight, TRUE);
    int addressX = toolbarSize.cx + margin;
    MoveWindow(address_, addressX, margin + (rowHeight - Scale(hwnd_, 22)) / 2,
               std::max<int>(0, client.right - addressX - margin), Scale(hwnd_, 22), TRUE);

    int top = margin + rowHeight + margin;
    MoveWindow(list_, 0, top, client.right, std::max<int>(0, client.bottom - statusHeight - top), TRUE);
}

void ArchiveWindow::UpdateCommands() {
    bool busy = job_.Running();
    bool selected = ListView_GetSelectedCount(list_) > 0;
    SendMessageW(toolbar_, TB_ENABLEBUTTON, IDM_UP, MAKELONG(folder_ >= 0, 0));
    SendMessageW(toolbar_, TB_ENABLEBUTTON, IDM_EXTRACT_ALL, MAKELONG(!busy, 0));
    SendMessageW(toolbar_, TB_ENABLEBUTTON, IDM_EXTRACT_SELECTED, MAKELONG(!busy && selected, 0));
    HMENU menu = GetMenu(hwnd_);
    EnableMenuItem(menu, IDM_UP, folder_ >= 0 ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(menu, IDM_EXTRACT_ALL, !busy ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(menu, IDM_EXTRACT_SELECTED, !busy && selected ? MF_ENABLED : MF_GRAYED);
}

void ArchiveWindow::Navigate(int64_t folder) {
    folder_ = folder;
    shown_ = archive_->Children(folder);
    ListView_SetItemState(list_, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_SetItemCountEx(list_, static_cast<int>(shown_.size()), 0);
    if (!shown_.empty()) ListView_SetItemState(list_, 0, LVIS_FOCUSED, LVIS_FOCUSED);
    InvalidateRect(list_, nullptr, TRUE);

    std::wstring location = archive_->path();
    std::wstring inside;
    for (int64_t f = folder; f >= 0; f = archive_->entries()[static_cast<size_t>(f)].parent)
        inside = L"\\" + archive_->entries()[static_cast<size_t>(f)].name + inside;
    SetText(address_, location + inside);
    UpdateStatus();
    UpdateCommands();
}

void ArchiveWindow::GoUp() {
    if (folder_ >= 0) Navigate(archive_->entries()[static_cast<size_t>(folder_)].parent);
}

void ArchiveWindow::OpenSelected() {
    auto selection = Selection();
    if (selection.size() == 1 && archive_->entries()[selection[0]].isDir)
        Navigate(static_cast<int64_t>(selection[0]));
}

std::vector<size_t> ArchiveWindow::Selection() const {
    std::vector<size_t> selection;
    for (int i = ListView_GetNextItem(list_, -1, LVNI_SELECTED); i >= 0; i = ListView_GetNextItem(list_, i, LVNI_SELECTED))
        selection.push_back(shown_[static_cast<size_t>(i)]);
    return selection;
}

void ArchiveWindow::ShowContextMenu(POINT screen) {
    auto selection = Selection();
    if (selection.empty()) return;
    if (screen.x == -1 && screen.y == -1) { // from the keyboard
        RECT rect{};
        ListView_GetItemRect(list_, std::max(0, ListView_GetNextItem(list_, -1, LVNI_FOCUSED)), &rect, LVIR_LABEL);
        screen = {rect.left, rect.bottom};
        ClientToScreen(list_, &screen);
    }
    HMENU menu = CreatePopupMenu();
    bool folder = selection.size() == 1 && archive_->entries()[selection[0]].isDir;
    if (folder) {
        AppendMenuW(menu, MF_STRING, IDM_OPEN_FOLDER, L"&Open");
        SetMenuDefaultItem(menu, IDM_OPEN_FOLDER, FALSE);
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    }
    AppendMenuW(menu, MF_STRING | (job_.Running() ? MF_GRAYED : 0), IDM_EXTRACT_SELECTED, L"&Extract…");
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, screen.x, screen.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
}

void ArchiveWindow::UpdateStatus() {
    if (!status_) return; // list notifications can arrive while the controls are being created
    UpdateCommands();
    if (job_.Running()) return;
    UINT selected = ListView_GetSelectedCount(list_);
    size_t files = archive_->fileCount();
    SetText(status_, selected > 0 ? std::to_wstring(selected) + (selected == 1 ? L" item selected" : L" items selected")
                                  : std::to_wstring(shown_.size()) + (shown_.size() == 1 ? L" item" : L" items") +
                                        L"    (archive: " + std::to_wstring(files) +
                                        (files == 1 ? L" file, " : L" files, ") + FormatBytes(archive_->totalSize()) +
                                        L")");
}

const ArchiveWindow::FileType &ArchiveWindow::TypeOf(size_t entry) {
    const auto &e = archive_->entries()[entry];
    std::wstring key = e.isDir ? L"/" : std::wstring(PathFindExtensionW(e.name.c_str()));
    CharLowerW(key.data());
    auto found = types_.find(key);
    if (found != types_.end()) return found->second;
    SHFILEINFOW info{};
    SHGetFileInfoW(e.isDir ? L"folder" : (L"file" + key).c_str(),
                   e.isDir ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL, &info, sizeof(info),
                   SHGFI_USEFILEATTRIBUTES | SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_TYPENAME);
    return types_.emplace(key, FileType{info.iIcon, info.szTypeName}).first->second;
}

void ArchiveWindow::GetDisplayInfo(NMLVDISPINFOW *info) {
    LVITEMW &item = info->item;
    if (item.iItem < 0 || static_cast<size_t>(item.iItem) >= shown_.size()) return;
    size_t entry = shown_[static_cast<size_t>(item.iItem)];
    const auto &e = archive_->entries()[entry];
    if (item.mask & LVIF_IMAGE) item.iImage = TypeOf(entry).icon;
    if (!(item.mask & LVIF_TEXT)) return;
    switch (item.iSubItem) {
    case kName: displayText_ = e.name; break;
    case kSize: displayText_ = e.isDir ? L"" : FormatBytes(e.size); break; // like File Explorer
    case kType: displayText_ = TypeOf(entry).name; break;
    default: displayText_.clear();
    }
    wcsncpy_s(item.pszText, static_cast<size_t>(item.cchTextMax), displayText_.c_str(), _TRUNCATE);
}

std::optional<std::wstring> ArchiveWindow::ChooseDestination() {
    if (Settings::Load().extractDestination == ExtractDestination::NextToArchive) return ParentFolder(archive_->path());
    return PickFolder(hwnd_, L"Choose where to extract");
}

void ArchiveWindow::ExtractSelected() {
    auto roots = archive_->TopLevel(Selection());
    if (roots.empty() || job_.Running()) return;
    auto folder = ChooseDestination();
    if (!folder) return;
    std::wstring reveal = roots.size() == 1 ? JoinPath(*folder, archive_->entries()[roots[0]].name) : *folder;
    StartExtraction({roots, *folder, reveal}, false);
}

void ArchiveWindow::ExtractAll() {
    if (job_.Running()) return;
    auto folder = ChooseDestination();
    if (!folder) return;
    std::wstring target = UniquePath(*folder, FileStem(archive_->path()), L"");
    StartExtraction({{}, target, target}, false);
}

void ArchiveWindow::StartExtraction(Extraction extraction, bool overwrite) {
    extraction_ = std::move(extraction);
    auto archive = archive_;
    std::vector<size_t> indices = extraction_.indices;
    std::string destination = ToUtf8(extraction_.destination);

    SetText(status_, L"Extracting to " + extraction_.destination + L"…");
    progress_.Open(hwnd_, L"Extracting", L"Extracting from " + Quoted(FileName(archive_->path())));
    SetTimer(hwnd_, kCancelTimer, 200, nullptr);
    job_.Start(hwnd_, [=](zarpack_progress_fn progress, void *user, std::string &message) {
        std::string error(1024, '\0');
        zarpack_status status = zarpack_extract(archive->handle(), indices.data(), indices.size(), destination.c_str(),
                                                overwrite ? 1 : 0, progress, user, error.data(), error.size());
        message = error.c_str();
        return status;
    });
    UpdateCommands();
}

void ArchiveWindow::OnProgress() {
    Job::Progress p = job_.Latest();
    progress_.Update(p.done, p.total, p.file);
}

void ArchiveWindow::OnDone() {
    KillTimer(hwnd_, kCancelTimer);
    progress_.Close();
    std::wstring message;
    zarpack_status status = job_.Finish(message);
    UpdateStatus();
    switch (status) {
    case ZARPACK_OK:
        SetText(status_, L"Extracted to " + extraction_.reveal);
        if (Settings::Load().revealAfterExtract) RevealInExplorer(extraction_.reveal);
        break;
    case ZARPACK_ERR_OUTPUT_EXISTS:
        if (Confirm(hwnd_, L"Replace existing items?",
                    message + L"\n\nSome items already exist in the destination. Replacing them can’t be undone.",
                    L"Replace"))
            StartExtraction(extraction_, true);
        break;
    case ZARPACK_CANCELLED:
        SetText(status_, L"Cancelled.");
        break;
    default:
        ShowError(hwnd_, L"Couldn’t extract", message);
        break;
    }
}
