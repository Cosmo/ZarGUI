#pragma once

#include "ArchiveWindow.g.h"

#include "ShellIcons.h"
#include "job.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

class Archive;

namespace winrt::ZarGUI::implementation {

/// Lists an archive's contents like File Explorer, with folder navigation, extraction and drag-out.
struct ArchiveWindow : ArchiveWindowT<ArchiveWindow> {
    using RoutedEventArgs = Microsoft::UI::Xaml::RoutedEventArgs;

    ArchiveWindow();

    /// Opens `path` in a new window. If it isn't a valid archive, the window says why.
    static void Show(std::wstring const &path);

    void OnUp(IInspectable const &sender, RoutedEventArgs const &e);
    void OnUpAccelerator(Microsoft::UI::Xaml::Input::KeyboardAccelerator const &sender,
                         Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const &e);
    void OnBreadcrumbClicked(Microsoft::UI::Xaml::Controls::BreadcrumbBar const &sender,
                             Microsoft::UI::Xaml::Controls::BreadcrumbBarItemClickedEventArgs const &e);
    void OnExtractSelected(IInspectable const &sender, RoutedEventArgs const &e);
    void OnExtractAll(IInspectable const &sender, RoutedEventArgs const &e);
    void OnCancel(IInspectable const &sender, RoutedEventArgs const &e);
    void OnShowInFolder(IInspectable const &sender, RoutedEventArgs const &e);
    void OnOpenItem(IInspectable const &sender, RoutedEventArgs const &e);

    void OnDoubleTapped(IInspectable const &sender, Microsoft::UI::Xaml::Input::DoubleTappedRoutedEventArgs const &e);
    void OnRightTapped(IInspectable const &sender, Microsoft::UI::Xaml::Input::RightTappedRoutedEventArgs const &e);
    void OnListKeyDown(IInspectable const &sender, Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const &e);
    void OnSelectionChanged(IInspectable const &sender,
                            Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const &e);
    void OnMenuOpening(IInspectable const &sender, IInspectable const &e);
    void OnDragItemsStarting(IInspectable const &sender,
                             Microsoft::UI::Xaml::Controls::DragItemsStartingEventArgs const &e);

private:
    struct Extraction {
        std::vector<size_t> indices; // empty: everything
        std::wstring destination;
        std::wstring reveal;
    };

    void Load(std::wstring const &path);
    HWND Hwnd() const;

    void Navigate(int64_t folder);
    void GoUp();
    void OpenSelected();
    std::vector<size_t> Selection();
    void UpdateCommands();
    void ShowResult(Microsoft::UI::Xaml::Controls::InfoBarSeverity severity, hstring const &title,
                    hstring const &message, std::wstring reveal);

    std::optional<std::wstring> ChooseDestination();
    void StartExtraction(Extraction extraction, bool overwrite);
    void OnProgress();
    fire_and_forget OnDone();

    std::shared_ptr<Archive> archive_;
    std::wstring path_;
    int64_t folder_ = -1; // -1: the archive's top level
    std::vector<int64_t> crumbs_;
    ShellIcons icons_;

    Job job_;
    Extraction extraction_;
    std::wstring reveal_;
};

} // namespace winrt::ZarGUI::implementation

namespace winrt::ZarGUI::factory_implementation {

struct ArchiveWindow : ArchiveWindowT<ArchiveWindow, implementation::ArchiveWindow> {};

} // namespace winrt::ZarGUI::factory_implementation
