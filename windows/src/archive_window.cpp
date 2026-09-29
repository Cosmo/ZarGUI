#include "archive_window.hpp"

#include "archive.hpp"
#include "drag_drop.hpp"
#include "settings.hpp"

#include <shellapi.h>
#include <shlwapi.h>
#include <uxtheme.h>
#include <windowsx.h>

#include <algorithm>

namespace {

enum Control { kUp = 100, kExtractSelected, kExtractAll, kList, kCancel };
enum MenuItem { kMenuOpen = 1, kMenuExtract };
enum Column { kName, kSize, kType };

} // namespace

void ArchiveWindow::Show(const std::wstring &path) {
    std::wstring error;
    auto archive = Archive::Open(path, error);
    if (!archive) {
        ShowError(nullptr, L"Can’t open " + Quoted(FileName(path)), error);
        return;
    }
    auto *window = new ArchiveWindow(std::move(archive));
    window->Create(L"ZarGUI.Archive", FileName(path) + L" - ZarGUI", 640, 440);
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
        Layout();
        return 0;
    case WM_GETMINMAXINFO: {
        auto *info = reinterpret_cast<MINMAXINFO *>(lParam);
        info->ptMinTrackSize = {Scale(hwnd_, 480), Scale(hwnd_, 280)};
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
    case WM_CTLCOLORSTATIC:
        SetTextColor(reinterpret_cast<HDC>(wParam),
                     GetSysColor(reinterpret_cast<HWND>(lParam) == status_ ? COLOR_GRAYTEXT : COLOR_WINDOWTEXT));
        SetBkMode(reinterpret_cast<HDC>(wParam), TRANSPARENT);
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
    case WM_CTLCOLORBTN:
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case kUp: Navigate(folder_ < 0 ? -1 : archive_->entries()[static_cast<size_t>(folder_)].parent); break;
        case kExtractSelected: ExtractSelected(); break;
        case kExtractAll: ExtractAll(); break;
        case kCancel: job_.Cancel(); break;
        case IDOK: OpenSelected(); break; // Enter in the list
        }
        return 0;
    case WM_NOTIFY:
        return OnNotify(reinterpret_cast<NMHDR *>(lParam));
    case WM_CONTEXTMENU:
        if (reinterpret_cast<HWND>(wParam) == list_) ShowContextMenu({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
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
    }
    return Window::HandleMessage(message, wParam, lParam);
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
        if (key->wVKey == VK_BACK) SendMessageW(hwnd_, WM_COMMAND, kUp, 0);
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
    up_ = CreateChild(hwnd_, WC_BUTTONW, L"↑ Up", WS_TABSTOP | BS_PUSHBUTTON, kUp);
    location_ = CreateChild(hwnd_, WC_STATICW, L"", SS_NOPREFIX | SS_CENTERIMAGE | SS_PATHELLIPSIS);
    extractSelected_ = CreateChild(hwnd_, WC_BUTTONW, L"Extract selected…", WS_TABSTOP | BS_PUSHBUTTON,
                                   kExtractSelected);
    extractAll_ = CreateChild(hwnd_, WC_BUTTONW, L"Extract all…", WS_TABSTOP | BS_PUSHBUTTON, kExtractAll);

    list_ = CreateChild(hwnd_, WC_LISTVIEWW, L"",
                        WS_TABSTOP | LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS, kList);
    SetWindowTheme(list_, L"Explorer", nullptr);
    ListView_SetExtendedListViewStyle(list_, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    SHFILEINFOW info{};
    auto images = reinterpret_cast<HIMAGELIST>(SHGetFileInfoW(
        L"file", FILE_ATTRIBUTE_NORMAL, &info, sizeof(info), SHGFI_USEFILEATTRIBUTES | SHGFI_SYSICONINDEX | SHGFI_SMALLICON));
    ListView_SetImageList(list_, images, LVSIL_SMALL);

    struct { const wchar_t *title; int width; int format; } columns[] = {
        {L"Name", 300, LVCFMT_LEFT}, {L"Size", 90, LVCFMT_RIGHT}, {L"Type", 170, LVCFMT_LEFT}};
    for (int i = 0; i < 3; i++) {
        LVCOLUMNW column{LVCF_TEXT | LVCF_WIDTH | LVCF_FMT};
        column.pszText = const_cast<wchar_t *>(columns[i].title);
        column.cx = Scale(hwnd_, columns[i].width);
        column.fmt = columns[i].format;
        ListView_InsertColumn(list_, i, &column);
    }

    status_ = CreateChild(hwnd_, WC_STATICW, L"", SS_NOPREFIX | SS_CENTERIMAGE | SS_ENDELLIPSIS);
    progress_ = CreateChild(hwnd_, PROGRESS_CLASSW, L"", 0);
    cancel_ = CreateChild(hwnd_, WC_BUTTONW, L"Cancel", WS_TABSTOP | BS_PUSHBUTTON, kCancel);
    ShowWindow(progress_, SW_HIDE);
    ShowWindow(cancel_, SW_HIDE);
    UpdateFonts();
}

void ArchiveWindow::UpdateFonts() {
    HFONT old = font_;
    font_ = CreateUiFont(hwnd_);
    ApplyFont(hwnd_, font_);
    if (old) DeleteObject(old);
}

void ArchiveWindow::Layout() {
    RECT client;
    GetClientRect(hwnd_, &client);
    int margin = Scale(hwnd_, 8), gap = Scale(hwnd_, 6), row = Scale(hwnd_, 28);
    int width = client.right - 2 * margin;

    int upWidth = Scale(hwnd_, 64), allWidth = Scale(hwnd_, 100), selectedWidth = Scale(hwnd_, 130);
    int x = margin;
    MoveWindow(up_, x, margin, upWidth, row, TRUE);
    x += upWidth + gap;
    int right = client.right - margin;
    MoveWindow(extractAll_, right - allWidth, margin, allWidth, row, TRUE);
    right -= allWidth + gap;
    MoveWindow(extractSelected_, right - selectedWidth, margin, selectedWidth, row, TRUE);
    right -= selectedWidth + gap;
    MoveWindow(location_, x, margin, std::max(0, right - x), row, TRUE);

    int top = margin + row + gap;
    int bottom = client.bottom - margin - row;
    MoveWindow(list_, margin, top, width, std::max(0, bottom - gap - top), TRUE);

    bool extracting = IsWindowVisible(cancel_) != FALSE;
    int cancelWidth = Scale(hwnd_, 80), barWidth = Scale(hwnd_, 160);
    int statusWidth = extracting ? width - cancelWidth - barWidth - 2 * gap : width;
    MoveWindow(status_, margin, bottom, statusWidth, row, TRUE);
    MoveWindow(progress_, margin + statusWidth + gap, bottom + (row - Scale(hwnd_, 8)) / 2, barWidth, Scale(hwnd_, 8),
               TRUE);
    MoveWindow(cancel_, client.right - margin - cancelWidth, bottom, cancelWidth, row, TRUE);
}

void ArchiveWindow::Navigate(int64_t folder) {
    folder_ = folder;
    shown_ = archive_->Children(folder);
    ListView_SetItemState(list_, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_SetItemCountEx(list_, static_cast<int>(shown_.size()), 0);
    if (!shown_.empty()) ListView_SetItemState(list_, 0, LVIS_FOCUSED, LVIS_FOCUSED);
    InvalidateRect(list_, nullptr, TRUE);

    std::wstring location;
    for (int64_t f = folder; f >= 0; f = archive_->entries()[static_cast<size_t>(f)].parent)
        location = L" › " + archive_->entries()[static_cast<size_t>(f)].name + location;
    SetText(location_, FileName(archive_->path()) + location);
    EnableWindow(up_, folder >= 0);
    UpdateStatus();
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
    if (folder) AppendMenuW(menu, MF_STRING, kMenuOpen, L"Open");
    AppendMenuW(menu, MF_STRING | (job_.Running() ? MF_GRAYED : 0), kMenuExtract, L"Extract…");
    if (folder) SetMenuDefaultItem(menu, kMenuOpen, FALSE);
    int chosen = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    if (chosen == kMenuOpen) OpenSelected();
    if (chosen == kMenuExtract) ExtractSelected();
}

void ArchiveWindow::UpdateStatus() {
    if (job_.Running()) return;
    UINT selected = ListView_GetSelectedCount(list_);
    size_t files = archive_->fileCount();
    SetText(status_, selected > 0 ? std::to_wstring(selected) + L" of " + std::to_wstring(shown_.size()) + L" selected"
                                  : std::to_wstring(files) + (files == 1 ? L" file, " : L" files, ") +
                                        FormatBytes(archive_->totalSize()));
    EnableWindow(extractSelected_, selected > 0 && !job_.Running());
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
    case kSize: displayText_ = FormatBytes(e.size); break;
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
    SetExtracting(true);
    auto archive = archive_;
    std::vector<size_t> indices = extraction_.indices;
    std::string destination = ToUtf8(extraction_.destination);
    job_.Start(hwnd_, [=](zarpack_progress_fn progress, void *user, std::string &message) {
        std::string error(1024, '\0');
        zarpack_status status = zarpack_extract(archive->handle(), indices.data(), indices.size(), destination.c_str(),
                                                overwrite ? 1 : 0, progress, user, error.data(), error.size());
        message = error.c_str();
        return status;
    });
}

void ArchiveWindow::SetExtracting(bool extracting) {
    ShowWindow(progress_, extracting ? SW_SHOWNA : SW_HIDE);
    ShowWindow(cancel_, extracting ? SW_SHOWNA : SW_HIDE);
    EnableWindow(extractAll_, !extracting);
    EnableWindow(extractSelected_, !extracting && ListView_GetSelectedCount(list_) > 0);
    if (extracting) {
        SetProgress(progress_, -1);
        SetText(status_, L"Extracting…");
    }
    Layout();
}

void ArchiveWindow::OnProgress() {
    Job::Progress p = job_.Latest();
    if (!IsWindowVisible(progress_)) return;
    if (p.total > 0) SetProgress(progress_, static_cast<double>(p.done) / static_cast<double>(p.total));
    SetText(status_, p.file.empty() ? L"Extracting…" : L"Extracting " + p.file);
}

void ArchiveWindow::OnDone() {
    std::wstring message;
    zarpack_status status = job_.Finish(message);
    SetExtracting(false);
    UpdateStatus();
    switch (status) {
    case ZARPACK_OK:
        if (Settings::Load().revealAfterExtract) RevealInExplorer(extraction_.reveal);
        else if (Confirm(hwnd_, L"Extraction complete", L"Extracted to " + Quoted(FileName(extraction_.reveal)) + L".",
                         L"Show in Explorer", L"Close"))
            RevealInExplorer(extraction_.reveal);
        break;
    case ZARPACK_ERR_OUTPUT_EXISTS:
        if (Confirm(hwnd_, L"Replace existing items?",
                    message + L"\n\nSome items already exist in the destination. Replacing them can’t be undone.",
                    L"Replace"))
            StartExtraction(extraction_, true);
        break;
    case ZARPACK_CANCELLED:
        break;
    default:
        ShowError(hwnd_, L"Couldn’t extract", message);
        break;
    }
}
