#pragma once

#include "pch.h"

#include <functional>

/// The Win32 handle of a WinUI window (for file pickers, drag and drop, DPI).
/// Pass the window object itself (inside a window class: `m_inner`).
HWND WindowHandle(winrt::Windows::Foundation::IInspectable const &window);

/// Windows 11 look: Mica background and content drawn into the title bar, with `titleBar`
/// as the draggable caption area.
void ApplyWindowStyle(winrt::Microsoft::UI::Xaml::Window const &window,
                      winrt::Microsoft::UI::Xaml::UIElement const &titleBar);

/// Sets the window size in effective pixels (scaled for the monitor's DPI).
void ResizeWindow(winrt::Microsoft::UI::Xaml::Window const &window, int width, int height);
