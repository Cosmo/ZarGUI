#include "window.hpp"

void Window::Create(const wchar_t *className, const std::wstring &title, int width, int height, HMENU menu) {
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{sizeof(wc)};
    if (!GetClassInfoExW(instance, className, &wc)) {
        wc.lpfnWndProc = &Window::Procedure;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
        wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
        wc.lpszClassName = className;
        RegisterClassExW(&wc);
    }
    constexpr DWORD style = WS_OVERLAPPEDWINDOW;
    CreateWindowExW(0, className, title.c_str(), style, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                    nullptr, menu, instance, this);
    if (!hwnd_) return;

    UINT dpi = GetDpiForWindow(hwnd_);
    RECT rect{0, 0, MulDiv(width, dpi, 96), MulDiv(height, dpi, 96)};
    AdjustWindowRectExForDpi(&rect, style, menu != nullptr, 0, dpi);
    SetWindowPos(hwnd_, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

LRESULT Window::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

LRESULT CALLBACK Window::Procedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    Window *self = nullptr;
    if (message == WM_NCCREATE) {
        self = static_cast<Window *>(reinterpret_cast<CREATESTRUCTW *>(lParam)->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        WindowOpened();
    } else {
        self = reinterpret_cast<Window *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (!self) return DefWindowProcW(hwnd, message, wParam, lParam);

    LRESULT result = self->HandleMessage(message, wParam, lParam);
    if (message == WM_NCDESTROY) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        delete self;
        WindowClosed();
    }
    return result;
}
