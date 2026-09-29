#include "main_window.hpp"

#include "archive_window.hpp"
#include "drag_drop.hpp"
#include "resource.h"
#include "settings.hpp"
#include "settings_dialog.hpp"

namespace {

constexpr UINT_PTR kCancelTimer = 1;

} // namespace

MainWindow *MainWindow::Show(const std::vector<std::wstring> &items) {
    auto *window = new MainWindow();
    HWND hwnd = CreateDialogParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_MAIN), nullptr,
                                   &MainWindow::Procedure, reinterpret_cast<LPARAM>(window));
    if (!hwnd) {
        delete window;
        return nullptr;
    }
    ShowWindow(hwnd, SW_SHOWNORMAL);
    window->Open(items);
    return window;
}

MainWindow::~MainWindow() {
    if (icon_) DestroyIcon(icon_);
}

INT_PTR CALLBACK MainWindow::Procedure(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_INITDIALOG) {
        auto *self = reinterpret_cast<MainWindow *>(lParam);
        self->hwnd_ = dialog;
        SetWindowLongPtrW(dialog, DWLP_USER, lParam);
        WindowOpened();
        self->OnInit();
        return TRUE;
    }
    auto *self = reinterpret_cast<MainWindow *>(GetWindowLongPtrW(dialog, DWLP_USER));
    if (!self) return FALSE;
    INT_PTR result = self->HandleMessage(message, wParam, lParam);
    if (message == WM_NCDESTROY) {
        SetWindowLongPtrW(dialog, DWLP_USER, 0);
        delete self;
        WindowClosed();
    }
    return result;
}

INT_PTR MainWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM) {
    switch (message) {
    case WM_COMMAND:
        OnCommand(LOWORD(wParam));
        return TRUE;
    case WM_TIMER:
        if (wParam == kCancelTimer && progress_.Cancelled()) {
            queue_.clear();
            job_.Cancel();
        }
        return TRUE;
    case WM_APP_PROGRESS:
        OnProgress();
        return TRUE;
    case WM_APP_DONE:
        OnDone();
        return TRUE;
    case WM_CLOSE:
        job_.Cancel();
        DestroyWindow(hwnd_);
        return TRUE;
    case WM_DESTROY:
        RevokeDragDrop(hwnd_);
        if (dropTarget_) dropTarget_->Release();
        progress_.Close();
        return TRUE;
    }
    return FALSE;
}

void MainWindow::OnInit() {
    SHSTOCKICONINFO stock{sizeof(stock)};
    if (SUCCEEDED(SHGetStockIconInfo(SIID_ZIPFILE, SHGSI_ICON | SHGSI_LARGEICON, &stock))) {
        icon_ = stock.hIcon;
        SendDlgItemMessageW(hwnd_, IDC_ICON, STM_SETICON, reinterpret_cast<WPARAM>(icon_), 0);
    }

    // The dialog template leaves room at the bottom; the status bar places itself there.
    status_ = CreateWindowExW(0, STATUSCLASSNAMEW, L"Ready", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, nullptr,
                              GetModuleHandleW(nullptr), nullptr);
    SendMessageW(status_, WM_SIZE, 0, 0);

    ShowSaveLocation();
    dropTarget_ = new FileDropTarget(hwnd_, [this](std::vector<std::wstring> paths) { Open(paths); });
    RegisterDragDrop(hwnd_, dropTarget_);
}

void MainWindow::Open(const std::vector<std::wstring> &paths) {
    for (const auto &path : paths) {
        if (IsArchive(path)) ArchiveWindow::Show(path);
        else if (IsFolder(path)) queue_.push_back(path);
        else SetStatus(Quoted(FileName(path)) + L" is not a folder or a .zar archive.");
    }
    StartNextIfIdle();
}

void MainWindow::OnCommand(int id) {
    switch (id) {
    case IDM_CREATE:
        if (auto folder = PickFolder(hwnd_, L"Choose a folder to archive")) Open({*folder});
        break;
    case IDM_OPEN:
        Open(PickArchives(hwnd_));
        break;
    case IDM_SETTINGS:
        ShowSettingsDialog(hwnd_);
        break;
    case IDM_ABOUT:
        ShowAbout(hwnd_);
        break;
    case IDM_EXIT:
        CloseAllWindows();
        break;
    case IDC_NEXT_TO: {
        Settings settings = Settings::Load();
        settings.outputFolder.clear();
        settings.Save();
        ShowSaveLocation();
        break;
    }
    case IDC_IN_FOLDER:
        if (GetWindowTextLengthW(GetDlgItem(hwnd_, IDC_FOLDER)) == 0) {
            ChooseSaveFolder();
        } else {
            Settings settings = Settings::Load();
            settings.outputFolder = GetText(GetDlgItem(hwnd_, IDC_FOLDER));
            settings.Save();
            ShowSaveLocation();
        }
        break;
    case IDC_BROWSE:
        ChooseSaveFolder();
        break;
    }
}

/// Reflects the saved setting in the radio buttons; the last chosen folder stays in the field.
void MainWindow::ShowSaveLocation() {
    std::wstring folder = Settings::Load().outputFolder;
    CheckRadioButton(hwnd_, IDC_NEXT_TO, IDC_IN_FOLDER, folder.empty() ? IDC_NEXT_TO : IDC_IN_FOLDER);
    if (!folder.empty()) SetDlgItemTextW(hwnd_, IDC_FOLDER, folder.c_str());
    EnableWindow(GetDlgItem(hwnd_, IDC_FOLDER), !folder.empty());
    EnableWindow(GetDlgItem(hwnd_, IDC_BROWSE), !folder.empty());
}

void MainWindow::ChooseSaveFolder() {
    if (auto folder = PickFolder(hwnd_, L"Choose where to save new archives")) {
        Settings settings = Settings::Load();
        settings.outputFolder = *folder;
        settings.Save();
    }
    ShowSaveLocation();
}

void MainWindow::SetStatus(const std::wstring &text) { SetWindowTextW(status_, text.c_str()); }

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

    std::wstring what = L"Archiving " + Quoted(FileName(current_));
    if (!queue_.empty()) what += L" (" + std::to_wstring(queue_.size()) + L" more in queue)";
    SetStatus(what + L"…");
    progress_.Open(hwnd_, L"Creating archive", what);
    SetTimer(hwnd_, kCancelTimer, 200, nullptr);

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
    progress_.Update(p.done, p.total, p.file);
}

void MainWindow::OnDone() {
    KillTimer(hwnd_, kCancelTimer);
    progress_.Close();
    std::wstring message;
    switch (job_.Finish(message)) {
    case ZARPACK_OK:
        SetStatus(L"Created " + message);
        break;
    case ZARPACK_CANCELLED:
        SetStatus(L"Cancelled.");
        break;
    case ZARPACK_ERR_OUTPUT_EXISTS:
        SetStatus(L"Ready");
        dialogOpen_ = true;
        if (Confirm(hwnd_, L"Replace existing archive?", message + L"\n\nReplacing it can’t be undone.",
                    L"Replace")) {
            queue_.push_front(current_);
            replaceNext_ = true;
        }
        dialogOpen_ = false;
        break;
    default:
        SetStatus(L"Couldn’t create the archive.");
        ShowError(hwnd_, L"Couldn’t create " + Quoted(FileName(current_) + L".zar"), message);
        break;
    }
    StartNextIfIdle();
}
