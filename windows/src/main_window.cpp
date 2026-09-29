#include "main_window.hpp"

#include "archive_window.hpp"
#include "drag_drop.hpp"
#include "settings.hpp"
#include "settings_dialog.hpp"

#include <algorithm>

namespace {

enum Control { kPrimary = 100, kSecondary, kReset, kChoose, kSettings };

constexpr wchar_t kNextToFolder[] = L"Next to the original folder";

int TextHeight(HWND control, HFONT font, int width) {
    std::wstring text = GetText(control);
    if (text.empty()) text = L" ";
    HDC dc = GetDC(control);
    auto old = SelectObject(dc, font);
    RECT rect{0, 0, width, 0};
    DrawTextW(dc, text.c_str(), -1, &rect, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, old);
    ReleaseDC(control, dc);
    return rect.bottom;
}

int ButtonWidth(HWND button, HFONT font) {
    std::wstring text = GetText(button);
    HDC dc = GetDC(button);
    auto old = SelectObject(dc, font);
    SIZE size{};
    GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &size);
    SelectObject(dc, old);
    ReleaseDC(button, dc);
    return std::max<int>(size.cx + Scale(button, 24), Scale(button, 88));
}

} // namespace

MainWindow *MainWindow::Show(const std::vector<std::wstring> &items) {
    auto *window = new MainWindow();
    window->Create(L"ZarGUI.Main", L"ZarGUI", 420, 240);
    if (!window->hwnd_) return nullptr;
    ShowWindow(window->hwnd_, SW_SHOWNORMAL);
    window->Open(items);
    return window;
}

MainWindow::~MainWindow() {
    if (font_) DeleteObject(font_);
    if (boldFont_) DeleteObject(boldFont_);
}

void MainWindow::Open(const std::vector<std::wstring> &paths) {
    for (const auto &path : paths) {
        if (IsArchive(path)) ArchiveWindow::Show(path);
        else if (IsFolder(path)) queue_.push_back(path);
        else if (!job_.Running()) ShowView(View::Failed, L"Couldn’t open that", L"Drop a folder to archive it, or a .zar file to open it.");
    }
    UpdateQueueText();
    StartNextIfIdle();
}

void MainWindow::OpenArchives() { Open(PickArchives(hwnd_)); }

LRESULT MainWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        CreateControls();
        dropTarget_ = new FileDropTarget({
            [this](bool on) {
                highlighted_ = on;
                InvalidateRect(hwnd_, nullptr, TRUE);
            },
            [this](std::vector<std::wstring> paths) { Open(paths); },
        });
        RegisterDragDrop(hwnd_, dropTarget_);
        return 0;
    case WM_SIZE:
        Layout();
        InvalidateRect(hwnd_, nullptr, TRUE);
        return 0;
    case WM_GETMINMAXINFO: {
        auto *info = reinterpret_cast<MINMAXINFO *>(lParam);
        info->ptMinTrackSize = {Scale(hwnd_, 380), Scale(hwnd_, 260)};
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
    case WM_PAINT:
        Paint();
        return 0;
    case WM_CTLCOLORSTATIC: {
        auto dc = reinterpret_cast<HDC>(wParam);
        auto control = reinterpret_cast<HWND>(lParam);
        bool secondary = control == caption_ || control == queueText_ || control == savePath_;
        SetTextColor(dc, GetSysColor(secondary ? COLOR_GRAYTEXT : COLOR_WINDOWTEXT));
        SetBkMode(dc, TRANSPARENT);
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
    }
    case WM_CTLCOLORBTN:
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
    case WM_COMMAND:
        OnCommand(LOWORD(wParam));
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
        RevokeDragDrop(hwnd_);
        if (dropTarget_) dropTarget_->Release();
        return 0;
    }
    return Window::HandleMessage(message, wParam, lParam);
}

void MainWindow::CreateControls() {
    title_ = CreateChild(hwnd_, WC_STATICW, L"", SS_CENTER | SS_NOPREFIX | SS_ENDELLIPSIS);
    progress_ = CreateChild(hwnd_, PROGRESS_CLASSW, L"", 0);
    caption_ = CreateChild(hwnd_, WC_STATICW, L"", SS_CENTER | SS_NOPREFIX);
    queueText_ = CreateChild(hwnd_, WC_STATICW, L"", SS_CENTER | SS_NOPREFIX);
    primary_ = CreateChild(hwnd_, WC_BUTTONW, L"", WS_TABSTOP | BS_PUSHBUTTON, kPrimary);
    secondary_ = CreateChild(hwnd_, WC_BUTTONW, L"", WS_TABSTOP | BS_PUSHBUTTON, kSecondary);

    saveLabel_ = CreateChild(hwnd_, WC_STATICW, L"Save in:", SS_NOPREFIX | SS_CENTERIMAGE);
    savePath_ = CreateChild(hwnd_, WC_STATICW, L"", SS_PATHELLIPSIS | SS_NOPREFIX | SS_CENTERIMAGE);
    reset_ = CreateChild(hwnd_, WC_BUTTONW, L"Reset", WS_TABSTOP | BS_PUSHBUTTON, kReset);
    choose_ = CreateChild(hwnd_, WC_BUTTONW, L"Choose…", WS_TABSTOP | BS_PUSHBUTTON, kChoose);
    settings_ = CreateChild(hwnd_, WC_BUTTONW, L"Settings…", WS_TABSTOP | BS_PUSHBUTTON, kSettings);

    UpdateFonts();
    UpdateSaveRow();
    ShowView(View::Idle, L"", L"");
}

void MainWindow::UpdateFonts() {
    HFONT oldFont = font_, oldBold = boldFont_;
    font_ = CreateUiFont(hwnd_);
    boldFont_ = CreateUiFont(hwnd_, true);
    ApplyFont(hwnd_, font_);
    SendMessageW(title_, WM_SETFONT, reinterpret_cast<WPARAM>(boldFont_), TRUE);
    if (oldFont) DeleteObject(oldFont);
    if (oldBold) DeleteObject(oldBold);
}

RECT MainWindow::DropZone() const {
    RECT client;
    GetClientRect(hwnd_, &client);
    int margin = Scale(hwnd_, 12);
    int row = Scale(hwnd_, 28);
    return {margin, margin, client.right - margin, client.bottom - margin - row - margin};
}

/// Centers the visible controls of the current view in the drop zone, and lays out the save row.
void MainWindow::Layout() {
    RECT zone = DropZone();
    int pad = Scale(hwnd_, 16), gap = Scale(hwnd_, 8), buttonHeight = Scale(hwnd_, 28);
    int width = zone.right - zone.left - 2 * pad;

    struct Item { HWND hwnd; int height; };
    std::vector<Item> items;
    auto visible = [](HWND h) { return IsWindowVisible(h) != FALSE; };
    for (HWND h : {title_, progress_, caption_, queueText_}) {
        if (!visible(h)) continue;
        int height = h == progress_ ? Scale(hwnd_, 6) : TextHeight(h, h == title_ ? boldFont_ : font_, width);
        items.push_back({h, height});
    }
    bool buttons = visible(primary_);
    int total = buttons ? buttonHeight : -gap;
    for (auto &item : items) total += item.height + gap;

    int y = zone.top + std::max<int>(pad, (zone.bottom - zone.top - total) / 2);
    for (auto &item : items) {
        MoveWindow(item.hwnd, zone.left + pad, y, width, item.height, TRUE);
        y += item.height + gap;
    }
    if (buttons) {
        int w1 = ButtonWidth(primary_, font_);
        int w2 = visible(secondary_) ? ButtonWidth(secondary_, font_) : 0;
        int x = (zone.left + zone.right - w1 - (w2 ? w2 + gap : 0)) / 2;
        MoveWindow(primary_, x, y, w1, buttonHeight, TRUE);
        if (w2) MoveWindow(secondary_, x + w1 + gap, y, w2, buttonHeight, TRUE);
    }

    RECT client;
    GetClientRect(hwnd_, &client);
    int margin = Scale(hwnd_, 12);
    int rowY = client.bottom - margin - buttonHeight;
    int x = client.right - margin;
    for (HWND button : {settings_, choose_, reset_}) {
        if (!visible(button)) continue;
        int w = ButtonWidth(button, font_);
        x -= w;
        MoveWindow(button, x, rowY, w, buttonHeight, TRUE);
        x -= gap;
    }
    int labelWidth = Scale(hwnd_, 52);
    MoveWindow(saveLabel_, margin, rowY, labelWidth, buttonHeight, TRUE);
    MoveWindow(savePath_, margin + labelWidth, rowY, std::max(0, x - margin - labelWidth), buttonHeight, TRUE);
}

void MainWindow::Paint() {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd_, &ps);
    RECT zone = DropZone();
    int radius = Scale(hwnd_, 12);
    COLORREF color = GetSysColor(highlighted_ ? COLOR_HIGHLIGHT : COLOR_GRAYTEXT);
    LOGBRUSH brush{BS_SOLID, color, 0};
    DWORD style = PS_GEOMETRIC | (highlighted_ ? PS_SOLID : PS_DASH) | PS_ENDCAP_FLAT;
    HPEN pen = ExtCreatePen(style, static_cast<DWORD>(Scale(hwnd_, highlighted_ ? 2 : 1)), &brush, 0, nullptr);
    auto oldPen = SelectObject(dc, pen);
    auto oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, zone.left, zone.top, zone.right, zone.bottom, radius, radius);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
    EndPaint(hwnd_, &ps);
}

void MainWindow::ShowView(View view, const std::wstring &title, const std::wstring &caption) {
    view_ = view;
    auto show = [](HWND h, bool on) { ShowWindow(h, on ? SW_SHOWNA : SW_HIDE); };
    switch (view) {
    case View::Idle:
        SetText(title_, L"Drop a folder or a .zar archive");
        SetText(caption_, L"Folders are archived, archives are opened.");
        SetText(primary_, L"Choose folder…");
        SetText(secondary_, L"Open archive…");
        break;
    case View::Packing:
        SetText(title_, title);
        SetText(caption_, caption);
        SetText(primary_, L"Cancel");
        break;
    case View::Done:
        SetText(title_, title);
        SetText(caption_, L"Drop another item to continue.");
        SetText(primary_, L"Show in Explorer");
        break;
    case View::Failed:
        SetText(title_, title);
        SetText(caption_, caption);
        break;
    }
    show(progress_, view == View::Packing);
    show(queueText_, view == View::Packing && !queue_.empty());
    show(primary_, view != View::Failed);
    show(secondary_, view == View::Idle);
    for (HWND h : {reset_, choose_, settings_}) EnableWindow(h, view != View::Packing);
    Layout();
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void MainWindow::UpdateSaveRow() {
    std::wstring folder = Settings::Load().outputFolder;
    SetText(savePath_, folder.empty() ? kNextToFolder : folder);
    ShowWindow(reset_, folder.empty() ? SW_HIDE : SW_SHOWNA);
    Layout();
}

void MainWindow::UpdateQueueText() {
    SetText(queueText_, std::to_wstring(queue_.size()) + L" more in queue");
    ShowWindow(queueText_, view_ == View::Packing && !queue_.empty() ? SW_SHOWNA : SW_HIDE);
    Layout();
}

void MainWindow::OnCommand(int id) {
    switch (id) {
    case kPrimary:
        if (view_ == View::Idle) {
            if (auto folder = PickFolder(hwnd_, L"Choose a folder to archive")) Open({*folder});
        } else if (view_ == View::Packing) {
            queue_.clear();
            UpdateQueueText();
            job_.Cancel();
        } else if (view_ == View::Done) {
            RevealInExplorer(lastArchive_);
        }
        break;
    case kSecondary:
    case kCommandOpenArchive:
        OpenArchives();
        break;
    case kChoose:
        if (auto folder = PickFolder(hwnd_, L"Choose where to save new archives")) {
            Settings settings = Settings::Load();
            settings.outputFolder = *folder;
            settings.Save();
            UpdateSaveRow();
        }
        break;
    case kReset: {
        Settings settings = Settings::Load();
        settings.outputFolder.clear();
        settings.Save();
        UpdateSaveRow();
        break;
    }
    case kSettings:
        if (ShowSettingsDialog(hwnd_)) UpdateSaveRow();
        break;
    }
}

std::wstring MainWindow::OutputFor(const std::wstring &input) const {
    Settings settings = Settings::Load();
    if (settings.existingArchive != ExistingArchive::KeepBoth) return settings.outputFolder;
    char resolved[32768] = {};
    std::string output = ToUtf8(settings.outputFolder);
    zarpack_resolve_output(ToUtf8(input).c_str(), output.empty() ? nullptr : output.c_str(), resolved,
                           sizeof(resolved));
    std::wstring archive = ToWide(resolved);
    return UniquePath(ParentFolder(archive), FileStem(archive), L".zar");
}

void MainWindow::StartNextIfIdle() {
    if (job_.Running() || dialogOpen_ || queue_.empty()) return;
    current_ = queue_.front();
    queue_.pop_front();

    Settings settings = Settings::Load();
    bool replace = replaceNext_ || settings.existingArchive == ExistingArchive::Replace;
    std::string output = ToUtf8(replaceNext_ ? settings.outputFolder : OutputFor(current_));
    replaceNext_ = false;
    std::string input = ToUtf8(current_);
    int level = settings.CompressionLevel();
    bool keepSystemFiles = !settings.skipSystemFiles;

    SetProgress(progress_, -1);
    ShowView(View::Packing, L"Archiving " + Quoted(FileName(current_)), L"");
    job_.Start(hwnd_, [=](zarpack_progress_fn progress, void *user, std::string &message) {
        zarpack_options options{};
        options.input_dir = input.c_str();
        options.output = output.empty() ? nullptr : output.c_str();
        options.overwrite = replace ? 1 : 0;
        options.progress = progress;
        options.user = user;
        options.keep_system_files = keepSystemFiles ? 1 : 0;
        options.compression_level = level;
        std::string archive(32768, '\0'), error(1024, '\0');
        zarpack_status status = zarpack_pack(&options, archive.data(), archive.size(), error.data(), error.size());
        message = (status == ZARPACK_OK ? archive : error).c_str(); // the archive path, or why it failed
        return status;
    });
}

void MainWindow::OnProgress() {
    Job::Progress p = job_.Latest();
    if (view_ != View::Packing) return;
    if (p.total > 0) SetProgress(progress_, static_cast<double>(p.done) / static_cast<double>(p.total));
    SetText(caption_, p.file);
}

void MainWindow::OnDone() {
    std::wstring message;
    zarpack_status status = job_.Finish(message);
    switch (status) {
    case ZARPACK_OK:
        lastArchive_ = message;
        ShowView(View::Done, L"Created " + Quoted(FileName(message)), L"");
        break;
    case ZARPACK_CANCELLED:
        ShowView(View::Idle, L"", L"");
        break;
    case ZARPACK_ERR_OUTPUT_EXISTS:
        ShowView(View::Idle, L"", L"");
        dialogOpen_ = true;
        if (Confirm(hwnd_, L"Replace existing archive?", message + L"\n\nReplacing it can’t be undone.",
                    L"Replace")) {
            queue_.push_front(current_);
            replaceNext_ = true;
        }
        dialogOpen_ = false;
        break;
    default:
        ShowView(View::Failed, L"Couldn’t create the archive", message);
        break;
    }
    UpdateQueueText();
    StartNextIfIdle();
}
