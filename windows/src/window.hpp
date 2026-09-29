#pragma once

#include "common.hpp"

#include <string>

/// A top-level window owned by its C++ object, which deletes itself when the window is destroyed.
class Window {
public:
    Window(const Window &) = delete;
    Window &operator=(const Window &) = delete;
    HWND hwnd() const { return hwnd_; }

protected:
    Window() = default;
    virtual ~Window() = default;

    /// Creates the window with a client area of `width` x `height` at 96 DPI, scaled for the monitor.
    void Create(const wchar_t *className, const std::wstring &title, int width, int height);
    virtual LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    HWND hwnd_ = nullptr;

private:
    static LRESULT CALLBACK Procedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
};
