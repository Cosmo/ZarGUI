#include "pch.h"

#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include "ArchiveWindow.xaml.h"
#include "WindowHelpers.h"
#include "common.hpp"
#include "settings.hpp"

#include <algorithm>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Windows::ApplicationModel::DataTransfer;

namespace winrt::ZarGUI::implementation {

MainWindow::MainWindow() {
    InitializeComponent();
    ApplyWindowStyle(m_inner.as<Window>(), AppTitleBar());
    ResizeWindow(m_inner.as<Window>(), 560, 500);
    UpdateSaveLocation();
    Closed([this](auto &&, auto &&) { job_.Cancel(); });
}

HWND MainWindow::Hwnd() const { return WindowHandle(m_inner); }

void MainWindow::Open(std::vector<std::wstring> const &paths) {
    for (auto const &path : paths) {
        if (IsArchive(path)) ArchiveWindow::Show(path);
        else if (IsFolder(path)) queue_.push_back(path);
    }
    StartNextIfIdle();
}

// Dragging onto the window

fire_and_forget MainWindow::OnDragEnter(IInspectable const &, DragEventArgs const &e) {
    auto lifetime = get_strong();
    dragKind_ = DropKind::None;
    if (!e.DataView().Contains(StandardDataFormats::StorageItems())) co_return;

    auto deferral = e.GetDeferral();
    auto items = co_await e.DataView().GetStorageItemsAsync();
    bool folders = false, archives = false;
    for (auto const &item : items) {
        std::wstring path{item.Path()};
        folders |= IsFolder(path);
        archives |= IsArchive(path);
    }
    dragKind_ = folders && archives ? DropKind::Both
                : folders           ? DropKind::Folders
                : archives          ? DropKind::Archives
                                    : DropKind::None;
    OnDragOver(nullptr, e);
    deferral.Complete();
}

void MainWindow::OnDragOver(IInspectable const &, DragEventArgs const &e) {
    if (dragKind_ == DropKind::None) {
        e.AcceptedOperation(DataPackageOperation::None);
        return;
    }
    e.AcceptedOperation(DataPackageOperation::Copy);
    auto ui = e.DragUIOverride();
    ui.IsGlyphVisible(false);
    ui.Caption(dragKind_ == DropKind::Folders    ? L"Create archive"
               : dragKind_ == DropKind::Archives ? L"Open"
                                                 : L"Create archive and open");
    ShowDropHint(true);
}

void MainWindow::OnDragLeave(IInspectable const &, DragEventArgs const &) { ShowDropHint(false); }

fire_and_forget MainWindow::OnDrop(IInspectable const &, DragEventArgs const &e) {
    auto lifetime = get_strong();
    ShowDropHint(false);
    if (!e.DataView().Contains(StandardDataFormats::StorageItems())) co_return;
    auto deferral = e.GetDeferral();
    auto items = co_await e.DataView().GetStorageItemsAsync();
    deferral.Complete();
    std::vector<std::wstring> paths;
    for (auto const &item : items) paths.emplace_back(item.Path());
    Open(paths);
}

void MainWindow::ShowDropHint(bool visible) {
    auto show = [](UIElement const &element, bool on) {
        element.Visibility(on ? Visibility::Visible : Visibility::Collapsed);
    };
    bool busy = job_.Running();
    show(DropOutline(), !visible);
    show(DropOutlineActive(), visible);
    show(DropHintPanel(), visible);
    show(IdlePanel(), !visible && !busy);
    show(BusyPanel(), !visible && busy);
    if (!visible) return;
    switch (dragKind_) {
    case DropKind::Folders:
        DropHintIcon().Glyph(L"");
        DropHintText().Text(L"Drop to create an archive");
        break;
    case DropKind::Archives:
        DropHintIcon().Glyph(L"");
        DropHintText().Text(L"Drop to open");
        break;
    default:
        DropHintIcon().Glyph(L"");
        DropHintText().Text(L"Drop to archive the folders and open the archives");
        break;
    }
}

// Buttons and shortcuts

void MainWindow::ChooseFolderToArchive() {
    if (auto folder = PickFolder(Hwnd(), L"Choose a folder to archive")) Open({*folder});
}

void MainWindow::ChooseArchivesToOpen() { Open(PickArchives(Hwnd())); }

void MainWindow::OnCreateArchive(IInspectable const &, RoutedEventArgs const &) { ChooseFolderToArchive(); }
void MainWindow::OnOpenArchive(IInspectable const &, RoutedEventArgs const &) { ChooseArchivesToOpen(); }

void MainWindow::OnCreateArchiveAccelerator(Accelerator const &, AcceleratorArgs const &e) {
    e.Handled(true);
    ChooseFolderToArchive();
}

void MainWindow::OnOpenArchiveAccelerator(Accelerator const &, AcceleratorArgs const &e) {
    e.Handled(true);
    ChooseArchivesToOpen();
}

void MainWindow::OnCancel(IInspectable const &, RoutedEventArgs const &) {
    queue_.clear();
    job_.Cancel();
}

void MainWindow::OnShowInFolder(IInspectable const &, RoutedEventArgs const &) {
    if (!reveal_.empty()) RevealInExplorer(reveal_);
}

void MainWindow::ShowResult(InfoBarSeverity severity, hstring const &title, hstring const &message,
                            std::wstring reveal) {
    reveal_ = std::move(reveal);
    ResultBar().Severity(severity);
    ResultBar().Title(title);
    ResultBar().Message(message);
    ShowInFolderButton().Visibility(reveal_.empty() ? Visibility::Collapsed : Visibility::Visible);
    ResultBar().IsOpen(true);
}

void MainWindow::UpdateSaveLocation() {
    std::wstring folder = Settings::Load().outputFolder;
    SaveLocationText().Text(folder.empty() ? std::wstring(L"Archives are saved next to the original folder")
                                           : L"Archives are saved to " + folder);
}

// Creating archives

void MainWindow::ShowBusy(bool busy) {
    IdlePanel().Visibility(busy ? Visibility::Collapsed : Visibility::Visible);
    BusyPanel().Visibility(busy ? Visibility::Visible : Visibility::Collapsed);
}

std::wstring MainWindow::OutputFor(std::wstring const &input) const {
    Settings settings = Settings::Load();
    if (settings.existingArchive != ExistingArchive::KeepBoth) return settings.outputFolder;
    std::string output = ToUtf8(settings.outputFolder);
    std::string resolved(32768, '\0');
    zarpack_resolve_output(ToUtf8(input).c_str(), output.empty() ? nullptr : output.c_str(), resolved.data(),
                           resolved.size());
    std::wstring archive = ToWide(resolved.c_str());
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

    ResultBar().IsOpen(false);
    BusyTitle().Text(L"Creating " + Quoted(FileName(current_) + L".zar"));
    BusyProgress().IsIndeterminate(true);
    BusyFile().Text(L"");
    QueueText().Text(queue_.empty() ? std::wstring() : std::to_wstring(queue_.size()) + L" more waiting");
    ShowBusy(true);

    auto weak = get_weak();
    auto queue = DispatcherQueue();
    job_.Start(
        [=](zarpack_progress_fn progress, void *user, std::string &message) {
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
        },
        [weak, queue] {
            queue.TryEnqueue([weak] {
                if (auto self = weak.get()) self->OnProgress();
            });
        },
        [weak, queue] {
            queue.TryEnqueue([weak] {
                if (auto self = weak.get()) self->OnDone();
            });
        });
}

void MainWindow::OnProgress() {
    Job::Progress p = job_.Latest();
    if (double fraction = p.Fraction(); fraction >= 0) {
        BusyProgress().IsIndeterminate(false);
        BusyProgress().Value(fraction);
    }
    BusyFile().Text(p.file);
}

fire_and_forget MainWindow::OnDone() {
    auto lifetime = get_strong();
    std::wstring message;
    zarpack_status status = job_.Finish(message);
    ShowBusy(false);

    switch (status) {
    case ZARPACK_OK:
        ShowResult(InfoBarSeverity::Success, L"Archive created", hstring(FileName(message)), message);
        break;
    case ZARPACK_CANCELLED:
        break;
    case ZARPACK_ERR_OUTPUT_EXISTS: {
        dialogOpen_ = true;
        ContentDialog dialog;
        dialog.XamlRoot(Root().XamlRoot());
        dialog.Title(box_value(hstring(L"Replace existing archive?")));
        dialog.Content(box_value(hstring(Quoted(FileName(current_) + L".zar") +
                                         L" already exists in this location. Replacing it can’t be undone.")));
        dialog.PrimaryButtonText(L"Replace");
        dialog.CloseButtonText(L"Cancel");
        dialog.DefaultButton(ContentDialogButton::Close);
        if (co_await dialog.ShowAsync() == ContentDialogResult::Primary) {
            queue_.push_front(current_);
            replaceNext_ = true;
        }
        dialogOpen_ = false;
        break;
    }
    default:
        ShowResult(InfoBarSeverity::Error, L"Couldn’t create the archive", hstring(message), {});
        break;
    }
    StartNextIfIdle();
}

// Settings

void MainWindow::OnOpenSettings(IInspectable const &, RoutedEventArgs const &) { ShowSettings(true); }
void MainWindow::OnBack(IInspectable const &, RoutedEventArgs const &) { ShowSettings(false); }

void MainWindow::ShowSettings(bool visible) {
    if (visible) LoadSettings();
    else UpdateSaveLocation();
    SettingsView().Visibility(visible ? Visibility::Visible : Visibility::Collapsed);
    HomeView().Visibility(visible ? Visibility::Collapsed : Visibility::Visible);
    BackButton().Visibility(visible ? Visibility::Visible : Visibility::Collapsed);
    TitleText().Text(visible ? L"Settings" : L"ZarGUI");
}

void MainWindow::LoadSettings() {
    loadingSettings_ = true;
    Settings s = Settings::Load();
    SettingsLocationText().Text(s.outputFolder.empty() ? std::wstring(L"Next to the original folder") : s.outputFolder);
    ResetLocationButton().Visibility(s.outputFolder.empty() ? Visibility::Collapsed : Visibility::Visible);
    ExistingCombo().SelectedIndex(static_cast<int32_t>(s.existingArchive));
    CompressionCombo().SelectedIndex(static_cast<int32_t>(s.compression));
    SkipSystemToggle().IsOn(s.skipSystemFiles);
    DestinationCombo().SelectedIndex(static_cast<int32_t>(s.extractDestination));
    RevealToggle().IsOn(s.revealAfterExtract);
    loadingSettings_ = false;
}

void MainWindow::SaveSettings() {
    if (loadingSettings_) return;
    Settings s = Settings::Load();
    s.existingArchive = static_cast<ExistingArchive>(std::max(0, ExistingCombo().SelectedIndex()));
    s.compression = static_cast<Compression>(std::max(0, CompressionCombo().SelectedIndex()));
    s.skipSystemFiles = SkipSystemToggle().IsOn();
    s.extractDestination = static_cast<ExtractDestination>(std::max(0, DestinationCombo().SelectedIndex()));
    s.revealAfterExtract = RevealToggle().IsOn();
    s.Save();
}

void MainWindow::OnSettingChanged(IInspectable const &, SelectionChangedEventArgs const &) { SaveSettings(); }
void MainWindow::OnSettingToggled(IInspectable const &, RoutedEventArgs const &) { SaveSettings(); }

void MainWindow::OnBrowseLocation(IInspectable const &, RoutedEventArgs const &) {
    if (auto folder = PickFolder(Hwnd(), L"Choose where to save new archives")) {
        Settings s = Settings::Load();
        s.outputFolder = *folder;
        s.Save();
        LoadSettings();
    }
}

void MainWindow::OnResetLocation(IInspectable const &, RoutedEventArgs const &) {
    Settings s = Settings::Load();
    s.outputFolder.clear();
    s.Save();
    LoadSettings();
}

} // namespace winrt::ZarGUI::implementation
