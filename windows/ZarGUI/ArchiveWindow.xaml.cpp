#include "pch.h"

#include "ArchiveWindow.xaml.h"
#if __has_include("ArchiveWindow.g.cpp")
#include "ArchiveWindow.g.cpp"
#endif

#include "ArchiveItem.h"
#include "WindowHelpers.h"
#include "archive.hpp"
#include "common.hpp"
#include "drag_out.hpp"
#include "settings.hpp"

#include <algorithm>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Input;

namespace winrt::ZarGUI::implementation {

namespace {

/// Open archive windows; WinUI windows need an owner while they are shown.
std::vector<ZarGUI::ArchiveWindow> openWindows;

} // namespace

void ArchiveWindow::Show(std::wstring const &path) {
    auto window = make_self<ArchiveWindow>();
    window->Load(path);
    auto projected = window.as<ZarGUI::ArchiveWindow>();
    openWindows.push_back(projected);
    projected.Closed([weak = make_weak(projected)](auto &&, auto &&) {
        if (auto closed = weak.get()) std::erase(openWindows, closed);
    });
    projected.Activate();
}

ArchiveWindow::ArchiveWindow() {
    InitializeComponent();
    ApplyWindowStyle(m_inner.as<Window>(), AppTitleBar());
    ResizeWindow(m_inner.as<Window>(), 760, 540);
    Closed([this](auto &&, auto &&) { job_.Cancel(); });
}

HWND ArchiveWindow::Hwnd() const { return WindowHandle(m_inner); }

void ArchiveWindow::Load(std::wstring const &path) {
    path_ = path;
    Title(FileName(path));
    TitleText().Text(FileName(path));
    std::wstring error;
    archive_ = Archive::Open(path, error);
    if (!archive_) {
        ShowResult(InfoBarSeverity::Error, L"Can’t open this archive", hstring(error), {});
        ResultBar().IsClosable(false);
        return;
    }
    Navigate(-1);
}

// Navigation

void ArchiveWindow::Navigate(int64_t folder) {
    folder_ = folder;
    auto items = single_threaded_vector<IInspectable>();
    for (size_t index : archive_->Children(folder)) {
        const auto &entry = archive_->entries()[index];
        const auto &shell = icons_.For(entry.name, entry.isDir);
        items.Append(make<ArchiveItem>(index, hstring(entry.name),
                                       hstring(entry.isDir ? std::wstring() : FormatBytes(entry.size)),
                                       hstring(shell.typeName), entry.isDir, shell.icon));
    }
    List().ItemsSource(items);
    EmptyText().Visibility(items.Size() == 0 ? Visibility::Visible : Visibility::Collapsed);

    crumbs_.clear();
    auto names = single_threaded_vector<IInspectable>();
    for (int64_t f = folder; f >= 0; f = archive_->entries()[static_cast<size_t>(f)].parent) crumbs_.push_back(f);
    crumbs_.push_back(-1);
    std::reverse(crumbs_.begin(), crumbs_.end());
    for (int64_t f : crumbs_)
        names.Append(box_value(hstring(f < 0 ? FileName(path_) : archive_->entries()[static_cast<size_t>(f)].name)));
    Breadcrumb().ItemsSource(names);
    UpdateCommands();
}

void ArchiveWindow::GoUp() {
    if (archive_ && folder_ >= 0) Navigate(archive_->entries()[static_cast<size_t>(folder_)].parent);
}

void ArchiveWindow::OpenSelected() {
    auto selection = Selection();
    if (selection.size() == 1 && archive_->entries()[selection[0]].isDir) Navigate(static_cast<int64_t>(selection[0]));
}

std::vector<size_t> ArchiveWindow::Selection() const {
    std::vector<size_t> selection;
    for (auto const &item : List().SelectedItems())
        selection.push_back(static_cast<size_t>(item.as<ZarGUI::ArchiveItem>().Index()));
    return selection;
}

void ArchiveWindow::UpdateCommands() {
    bool ready = archive_ != nullptr && !job_.Running();
    uint32_t selected = List().SelectedItems().Size();
    UpButton().IsEnabled(folder_ >= 0);
    ExtractSelectedButton().IsEnabled(ready && selected > 0);
    ExtractAllButton().IsEnabled(ready);
    if (!archive_) return;

    uint32_t shown = List().Items().Size();
    std::wstring status = selected > 0 ? std::to_wstring(selected) + L" of " + std::to_wstring(shown) + L" selected"
                                       : std::to_wstring(shown) + (shown == 1 ? L" item" : L" items");
    size_t files = archive_->fileCount();
    status += L"  ·  Archive: " + std::to_wstring(files) + (files == 1 ? L" file, " : L" files, ") +
              FormatBytes(archive_->totalSize());
    StatusText().Text(status);
}

void ArchiveWindow::OnUp(IInspectable const &, RoutedEventArgs const &) { GoUp(); }

void ArchiveWindow::OnUpAccelerator(KeyboardAccelerator const &, KeyboardAcceleratorInvokedEventArgs const &e) {
    e.Handled(true);
    GoUp();
}

void ArchiveWindow::OnBreadcrumbClicked(BreadcrumbBar const &, BreadcrumbBarItemClickedEventArgs const &e) {
    auto index = static_cast<size_t>(e.Index());
    if (index < crumbs_.size()) Navigate(crumbs_[index]);
}

void ArchiveWindow::OnDoubleTapped(IInspectable const &, DoubleTappedRoutedEventArgs const &) { OpenSelected(); }

void ArchiveWindow::OnOpenItem(IInspectable const &, RoutedEventArgs const &) { OpenSelected(); }

/// Right-clicking an item that isn't selected selects it first, like File Explorer.
void ArchiveWindow::OnRightTapped(IInspectable const &, RightTappedRoutedEventArgs const &e) {
    auto element = e.OriginalSource().try_as<FrameworkElement>();
    if (!element) return;
    auto item = element.DataContext().try_as<ZarGUI::ArchiveItem>();
    if (!item) return;
    uint32_t position = 0;
    if (!List().SelectedItems().IndexOf(item, position)) List().SelectedItem(item);
}

void ArchiveWindow::OnListKeyDown(IInspectable const &, KeyRoutedEventArgs const &e) {
    if (e.Key() == Windows::System::VirtualKey::Enter) {
        OpenSelected();
        e.Handled(true);
    } else if (e.Key() == Windows::System::VirtualKey::Back) {
        GoUp();
        e.Handled(true);
    }
}

void ArchiveWindow::OnSelectionChanged(IInspectable const &, SelectionChangedEventArgs const &) { UpdateCommands(); }

void ArchiveWindow::OnMenuOpening(IInspectable const &, IInspectable const &) {
    auto selection = Selection();
    bool folder = selection.size() == 1 && archive_->entries()[selection[0]].isDir;
    OpenMenuItem().Visibility(folder ? Visibility::Visible : Visibility::Collapsed);
    ExtractMenuItem().IsEnabled(!selection.empty() && !job_.Running());
}

// Dragging items out

void ArchiveWindow::OnDragItemsStarting(IInspectable const &, DragItemsStartingEventArgs const &e) {
    // WinUI's own drag can only offer real files. Explorer-style virtual files let the drop
    // target read straight from the archive, so hand the drag to the shell instead.
    e.Cancel(true);
    std::vector<size_t> dragged;
    for (auto const &item : e.Items()) dragged.push_back(static_cast<size_t>(item.as<ZarGUI::ArchiveItem>().Index()));
    std::wstring error;
    if (!DragArchiveEntries(Hwnd(), archive_, archive_->TopLevel(dragged), error) && !error.empty())
        ShowResult(InfoBarSeverity::Warning, L"Can’t drag these items", hstring(error), {});
}

// Extracting

std::optional<std::wstring> ArchiveWindow::ChooseDestination() {
    if (Settings::Load().extractDestination == ExtractDestination::NextToArchive) return ParentFolder(path_);
    return PickFolder(Hwnd(), L"Choose where to extract");
}

void ArchiveWindow::OnExtractSelected(IInspectable const &, RoutedEventArgs const &) {
    auto roots = archive_->TopLevel(Selection());
    if (roots.empty() || job_.Running()) return;
    auto folder = ChooseDestination();
    if (!folder) return;
    std::wstring reveal = roots.size() == 1 ? JoinPath(*folder, archive_->entries()[roots[0]].name) : *folder;
    StartExtraction({roots, *folder, reveal}, false);
}

void ArchiveWindow::OnExtractAll(IInspectable const &, RoutedEventArgs const &) {
    if (job_.Running()) return;
    auto folder = ChooseDestination();
    if (!folder) return;
    std::wstring target = UniquePath(*folder, FileStem(path_), L"");
    StartExtraction({{}, target, target}, false);
}

void ArchiveWindow::StartExtraction(Extraction extraction, bool overwrite) {
    extraction_ = std::move(extraction);
    auto archive = archive_;
    std::vector<size_t> indices = extraction_.indices;
    std::string destination = ToUtf8(extraction_.destination);

    ResultBar().IsOpen(false);
    ProgressInfo().Message(hstring(L"To " + extraction_.destination));
    ExtractProgress().IsIndeterminate(true);
    ExtractFile().Text(L"");
    ProgressInfo().IsOpen(true);

    auto weak = get_weak();
    auto queue = DispatcherQueue();
    job_.Start(
        [=](zarpack_progress_fn progress, void *user, std::string &message) {
            std::string error(1024, '\0');
            zarpack_status status = zarpack_extract(archive->handle(), indices.data(), indices.size(),
                                                    destination.c_str(), overwrite ? 1 : 0, progress, user,
                                                    error.data(), error.size());
            message = error.c_str();
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
    UpdateCommands();
}

void ArchiveWindow::OnProgress() {
    Job::Progress p = job_.Latest();
    if (double fraction = p.Fraction(); fraction >= 0) {
        ExtractProgress().IsIndeterminate(false);
        ExtractProgress().Value(fraction);
    }
    ExtractFile().Text(p.file);
}

void ArchiveWindow::OnCancel(IInspectable const &, RoutedEventArgs const &) { job_.Cancel(); }

fire_and_forget ArchiveWindow::OnDone() {
    auto lifetime = get_strong();
    std::wstring message;
    zarpack_status status = job_.Finish(message);
    ProgressInfo().IsOpen(false);
    UpdateCommands();

    switch (status) {
    case ZARPACK_OK:
        ShowResult(InfoBarSeverity::Success, L"Extracted", hstring(FileName(extraction_.reveal)), extraction_.reveal);
        if (Settings::Load().revealAfterExtract) RevealInExplorer(extraction_.reveal);
        break;
    case ZARPACK_CANCELLED:
        break;
    case ZARPACK_ERR_OUTPUT_EXISTS: {
        ContentDialog dialog;
        dialog.XamlRoot(Root().XamlRoot());
        dialog.Title(box_value(hstring(L"Replace existing items?")));
        dialog.Content(box_value(hstring(message + L"\n\nSome items already exist in the destination. "
                                                   L"Replacing them can’t be undone.")));
        dialog.PrimaryButtonText(L"Replace");
        dialog.CloseButtonText(L"Cancel");
        dialog.DefaultButton(ContentDialogButton::Close);
        if (co_await dialog.ShowAsync() == ContentDialogResult::Primary) StartExtraction(extraction_, true);
        break;
    }
    default:
        ShowResult(InfoBarSeverity::Error, L"Couldn’t extract", hstring(message), {});
        break;
    }
}

void ArchiveWindow::ShowResult(InfoBarSeverity severity, hstring const &title, hstring const &message,
                               std::wstring reveal) {
    reveal_ = std::move(reveal);
    ResultBar().Severity(severity);
    ResultBar().Title(title);
    ResultBar().Message(message);
    ShowInFolderButton().Visibility(reveal_.empty() ? Visibility::Collapsed : Visibility::Visible);
    ResultBar().IsOpen(true);
}

void ArchiveWindow::OnShowInFolder(IInspectable const &, RoutedEventArgs const &) {
    if (!reveal_.empty()) RevealInExplorer(reveal_);
}

} // namespace winrt::ZarGUI::implementation
