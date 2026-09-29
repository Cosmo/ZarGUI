#pragma once

#include "job.hpp"
#include "window.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class Archive;

/// Lists an archive's contents with folder navigation, and extracts or drags items out.
class ArchiveWindow final : public Window {
public:
    /// Opens `path`; shows an error instead if it isn't a valid archive.
    static void Show(const std::wstring &path);

private:
    struct FileType {
        int icon;
        std::wstring name;
    };

    struct Extraction {
        std::vector<size_t> indices; // empty: everything
        std::wstring destination;
        std::wstring reveal;
    };

    explicit ArchiveWindow(std::shared_ptr<Archive> archive);
    ~ArchiveWindow() override;

    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) override;
    LRESULT OnNotify(NMHDR *header);
    void CreateControls();
    void UpdateFonts();
    void Layout();

    void Navigate(int64_t folder);
    void OpenSelected();
    void ShowContextMenu(POINT screen);
    std::vector<size_t> Selection() const;
    void UpdateStatus();
    const FileType &TypeOf(size_t entry);
    void GetDisplayInfo(NMLVDISPINFOW *info);

    std::optional<std::wstring> ChooseDestination();
    void ExtractSelected();
    void ExtractAll();
    void StartExtraction(Extraction extraction, bool overwrite);
    void OnProgress();
    void OnDone();
    void SetExtracting(bool extracting);

    std::shared_ptr<Archive> archive_;
    int64_t folder_ = -1; // -1: the archive's top level
    std::vector<size_t> shown_;
    std::unordered_map<std::wstring, FileType> types_;
    std::wstring displayText_; // backs the text handed to the list view

    HFONT font_ = nullptr;
    HWND up_ = nullptr, location_ = nullptr, extractSelected_ = nullptr, extractAll_ = nullptr;
    HWND list_ = nullptr, status_ = nullptr, progress_ = nullptr, cancel_ = nullptr;

    Job job_;
    Extraction extraction_;
};
