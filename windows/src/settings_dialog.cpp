#include "settings_dialog.hpp"

#include "resource.h"
#include "settings.hpp"

namespace {

void FillCombo(HWND dialog, int id, std::initializer_list<const wchar_t *> items, int selected) {
    HWND combo = GetDlgItem(dialog, id);
    for (const wchar_t *item : items) SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
    SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(selected), 0);
}

int ComboSelection(HWND dialog, int id) {
    auto selected = static_cast<int>(SendDlgItemMessageW(dialog, id, CB_GETCURSEL, 0, 0));
    return selected < 0 ? 0 : selected;
}

void Load(HWND dialog, const Settings &s) {
    FillCombo(dialog, IDC_EXISTING, {L"Ask", L"Replace it", L"Keep both"}, static_cast<int>(s.existingArchive));
    FillCombo(dialog, IDC_COMPRESSION, {L"Faster", L"Standard", L"Smaller"}, static_cast<int>(s.compression));
    CheckDlgButton(dialog, IDC_SKIP_SYSTEM, s.skipSystemFiles ? BST_CHECKED : BST_UNCHECKED);
    FillCombo(dialog, IDC_DESTINATION, {L"Ask each time", L"The folder containing the archive"},
              static_cast<int>(s.extractDestination));
    CheckDlgButton(dialog, IDC_REVEAL, s.revealAfterExtract ? BST_CHECKED : BST_UNCHECKED);
}

void Store(HWND dialog, Settings &s) {
    s.existingArchive = static_cast<ExistingArchive>(ComboSelection(dialog, IDC_EXISTING));
    s.compression = static_cast<Compression>(ComboSelection(dialog, IDC_COMPRESSION));
    s.skipSystemFiles = IsDlgButtonChecked(dialog, IDC_SKIP_SYSTEM) == BST_CHECKED;
    s.extractDestination = static_cast<ExtractDestination>(ComboSelection(dialog, IDC_DESTINATION));
    s.revealAfterExtract = IsDlgButtonChecked(dialog, IDC_REVEAL) == BST_CHECKED;
}

INT_PTR CALLBACK Procedure(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
    auto *settings = reinterpret_cast<Settings *>(GetWindowLongPtrW(dialog, DWLP_USER));
    switch (message) {
    case WM_INITDIALOG:
        SetWindowLongPtrW(dialog, DWLP_USER, lParam);
        Load(dialog, *reinterpret_cast<Settings *>(lParam));
        return TRUE;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDOK:
            Store(dialog, *settings);
            EndDialog(dialog, IDOK);
            return TRUE;
        case IDCANCEL:
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

} // namespace

bool ShowSettingsDialog(HWND owner) {
    Settings settings = Settings::Load();
    if (DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_SETTINGS), owner, Procedure,
                        reinterpret_cast<LPARAM>(&settings)) != IDOK)
        return false;
    settings.Save();
    return true;
}
