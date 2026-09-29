#include "pch.h"

#include "App.xaml.h"
#include "MainWindow.xaml.h"

#include <shellapi.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::ZarGUI::implementation {

App::App() {
#if defined _DEBUG && !defined DISABLE_XAML_GENERATED_BREAK_ON_UNHANDLED_EXCEPTION
    UnhandledException([](IInspectable const &, UnhandledExceptionEventArgs const &e) {
        if (IsDebuggerPresent()) {
            auto message = e.Message();
            __debugbreak();
        }
    });
#endif
}

void App::OnLaunched(LaunchActivatedEventArgs const &) {
    // OLE drag and drop out of archive windows needs an OLE-initialized UI thread.
    OleInitialize(nullptr);

    // Folders and .zar files passed on the command line ("Open with", a drop on ZarGUI.exe, shortcuts).
    int argc = 0;
    PWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::wstring> items(argv + 1, argv + argc);
    LocalFree(argv);

    auto main = make_self<MainWindow>();
    window_ = main.as<Window>();
    window_.Activate();
    main->Open(items);
}

} // namespace winrt::ZarGUI::implementation
