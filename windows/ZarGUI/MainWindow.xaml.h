#pragma once

#include "MainWindow.g.h"

#include "job.hpp"

#include <deque>
#include <string>
#include <vector>

namespace winrt::ZarGUI::implementation {

/// The drop window: folders dropped on it are archived, .zar files open in an ArchiveWindow.
/// Settings are a second view in the same window, as in Windows 11 apps.
struct MainWindow : MainWindowT<MainWindow> {
    using DragEventArgs = Microsoft::UI::Xaml::DragEventArgs;
    using RoutedEventArgs = Microsoft::UI::Xaml::RoutedEventArgs;
    using Accelerator = Microsoft::UI::Xaml::Input::KeyboardAccelerator;
    using AcceleratorArgs = Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs;

    MainWindow();

    /// Folders are queued for archiving; archives open in their own window.
    void Open(std::vector<std::wstring> const &paths);

    fire_and_forget OnDragEnter(IInspectable const &sender, DragEventArgs const &e);
    void OnDragOver(IInspectable const &sender, DragEventArgs const &e);
    void OnDragLeave(IInspectable const &sender, DragEventArgs const &e);
    fire_and_forget OnDrop(IInspectable const &sender, DragEventArgs const &e);

    void OnCreateArchive(IInspectable const &sender, RoutedEventArgs const &e);
    void OnOpenArchive(IInspectable const &sender, RoutedEventArgs const &e);
    void OnCreateArchiveAccelerator(Accelerator const &sender, AcceleratorArgs const &e);
    void OnOpenArchiveAccelerator(Accelerator const &sender, AcceleratorArgs const &e);
    void OnCancel(IInspectable const &sender, RoutedEventArgs const &e);
    void OnShowInFolder(IInspectable const &sender, RoutedEventArgs const &e);

    void OnOpenSettings(IInspectable const &sender, RoutedEventArgs const &e);
    void OnBack(IInspectable const &sender, RoutedEventArgs const &e);
    void OnBrowseLocation(IInspectable const &sender, RoutedEventArgs const &e);
    void OnResetLocation(IInspectable const &sender, RoutedEventArgs const &e);
    void OnSettingChanged(IInspectable const &sender, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const &e);
    void OnSettingToggled(IInspectable const &sender, RoutedEventArgs const &e);

private:
    enum class DropKind { None, Folders, Archives, Both };

    HWND Hwnd() const;
    void ChooseFolderToArchive();
    void ChooseArchivesToOpen();

    void ShowDropHint(bool visible);
    void ShowBusy(bool busy);
    void ShowResult(Microsoft::UI::Xaml::Controls::InfoBarSeverity severity, hstring const &title,
                    hstring const &message, std::wstring reveal);
    void UpdateSaveLocation();

    void ShowSettings(bool visible);
    void LoadSettings();
    void SaveSettings();

    void StartNextIfIdle();
    std::wstring OutputFor(std::wstring const &input) const;
    void OnProgress();
    fire_and_forget OnDone();

    DropKind dragKind_ = DropKind::None;
    std::deque<std::wstring> queue_;
    Job job_;
    std::wstring current_;
    std::wstring reveal_;
    bool replaceNext_ = false;
    bool dialogOpen_ = false;
    bool loadingSettings_ = false;
};

} // namespace winrt::ZarGUI::implementation

namespace winrt::ZarGUI::factory_implementation {

struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};

} // namespace winrt::ZarGUI::factory_implementation
