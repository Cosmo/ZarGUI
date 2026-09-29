#include "common.hpp"
#include "main_window.hpp"
#include "resource.h"

#include <shellapi.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    if (FAILED(OleInitialize(nullptr))) return 1; // COM plus drag and drop
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&controls);

    // Folders and .zar files passed on the command line ("Open with", a drop on ZarGUI.exe, shortcuts).
    int argc = 0;
    PWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::wstring> items(argv + 1, argv + argc);
    LocalFree(argv);

    if (!MainWindow::Show(items)) return 1;

    ACCEL keys[] = {
        {FCONTROL | FVIRTKEY, 'N', IDM_CREATE},
        {FCONTROL | FVIRTKEY, 'O', IDM_OPEN},
        {FALT | FVIRTKEY, VK_UP, IDM_UP},
    };
    HACCEL accelerators = CreateAcceleratorTableW(keys, ARRAYSIZE(keys));
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        HWND root = GetAncestor(message.hwnd, GA_ROOT);
        bool key = message.message == WM_KEYDOWN || message.message == WM_SYSKEYDOWN;
        if (root && key && TranslateAcceleratorW(root, accelerators, &message)) continue;
        if (root && IsDialogMessageW(root, &message)) continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    DestroyAcceleratorTable(accelerators);
    OleUninitialize();
    return 0;
}
