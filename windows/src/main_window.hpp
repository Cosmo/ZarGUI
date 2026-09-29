#pragma once

#include "job.hpp"
#include "window.hpp"

#include <deque>
#include <string>
#include <vector>

class FileDropTarget;

/// The drop window: folders dropped on it are archived, .zar files open in an ArchiveWindow.
class MainWindow final : public Window {
public:
    static MainWindow *Show(const std::vector<std::wstring> &items);

    /// Folders are queued for archiving; archives open in their own window.
    void Open(const std::vector<std::wstring> &paths);
    void OpenArchives();

private:
    enum class View { Idle, Packing, Done, Failed };

    MainWindow() = default;
    ~MainWindow() override;

    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) override;
    void CreateControls();
    void UpdateFonts();
    void Layout();
    void Paint();
    RECT DropZone() const;

    void ShowView(View view, const std::wstring &title, const std::wstring &caption);
    void UpdateSaveRow();
    void UpdateQueueText();
    void OnCommand(int id);
    void OnProgress();
    void OnDone();

    void StartNextIfIdle();
    std::wstring OutputFor(const std::wstring &input) const;

    View view_ = View::Idle;
    bool highlighted_ = false;
    FileDropTarget *dropTarget_ = nullptr;
    HFONT font_ = nullptr;
    HFONT boldFont_ = nullptr;

    HWND title_ = nullptr, caption_ = nullptr, queueText_ = nullptr, progress_ = nullptr;
    HWND primary_ = nullptr, secondary_ = nullptr;
    HWND saveLabel_ = nullptr, savePath_ = nullptr, reset_ = nullptr, choose_ = nullptr, settings_ = nullptr;

    std::deque<std::wstring> queue_;
    Job job_;
    std::wstring current_;
    std::wstring lastArchive_;
    bool replaceNext_ = false;
    bool dialogOpen_ = false;
};
