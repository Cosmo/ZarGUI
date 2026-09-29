#pragma once

#include "common.hpp"
#include "job.hpp"

#include <deque>
#include <string>
#include <vector>

class FileDropTarget;

/// The main window, a standard dialog: folders dropped on it are archived,
/// .zar files open in an ArchiveWindow.
class MainWindow {
public:
    static MainWindow *Show(const std::vector<std::wstring> &items);

    /// Folders are queued for archiving; archives open in their own window.
    void Open(const std::vector<std::wstring> &paths);

private:
    MainWindow() = default;
    MainWindow(const MainWindow &) = delete;
    MainWindow &operator=(const MainWindow &) = delete;
    ~MainWindow();

    static INT_PTR CALLBACK Procedure(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam);
    INT_PTR HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void OnInit();
    void OnCommand(int id);

    void ShowSaveLocation();
    void ChooseSaveFolder();
    void SetStatus(const std::wstring &text);

    void StartNextIfIdle();
    std::wstring OutputFor(const std::wstring &input) const;
    void OnProgress();
    void OnDone();

    HWND hwnd_ = nullptr;
    HWND status_ = nullptr;
    HICON icon_ = nullptr;
    FileDropTarget *dropTarget_ = nullptr;

    std::deque<std::wstring> queue_;
    Job job_;
    ProgressWindow progress_;
    std::wstring current_;
    bool replaceNext_ = false;
    bool dialogOpen_ = false;
};
