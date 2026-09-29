#include "pch.h"

#include "WindowHelpers.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;

HWND WindowHandle(Windows::Foundation::IInspectable const &window) {
    HWND hwnd = nullptr;
    window.as<IWindowNative>()->get_WindowHandle(&hwnd);
    return hwnd;
}

void ApplyWindowStyle(Window const &window, UIElement const &titleBar) {
    window.SystemBackdrop(Media::MicaBackdrop());
    window.ExtendsContentIntoTitleBar(true);
    window.SetTitleBar(titleBar);
    window.AppWindow().TitleBar().PreferredHeightOption(Microsoft::UI::Windowing::TitleBarHeightOption::Tall);
}

void ResizeWindow(Window const &window, int width, int height) {
    double scale = GetDpiForWindow(WindowHandle(window)) / 96.0;
    window.AppWindow().Resize({static_cast<int32_t>(width * scale), static_cast<int32_t>(height * scale)});
}
