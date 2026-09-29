#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <commctrl.h>
#include <shlobj.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Messages posted by background jobs to their window.
constexpr UINT WM_APP_PROGRESS = WM_APP + 1;
constexpr UINT WM_APP_DONE = WM_APP + 2;

/// Ctrl+O, sent as WM_COMMAND to the active window.
constexpr int kCommandOpenArchive = 200;

std::string ToUtf8(std::wstring_view s);
std::wstring ToWide(std::string_view s);

std::wstring FileName(const std::wstring &path);
std::wstring FileStem(const std::wstring &path);
std::wstring ParentFolder(const std::wstring &path);
std::wstring JoinPath(const std::wstring &folder, const std::wstring &name);
bool IsFolder(const std::wstring &path);
bool Exists(const std::wstring &path);
bool IsArchive(const std::wstring &path);

/// "Name", or "Name (2)", "Name (3)", … if taken, like File Explorer.
std::wstring UniquePath(const std::wstring &folder, const std::wstring &stem, const std::wstring &extension);

/// Sizes formatted the way File Explorer shows them.
std::wstring FormatBytes(uint64_t bytes);
std::wstring Quoted(const std::wstring &s);

/// Pixels for a length given at 96 DPI.
int Scale(HWND hwnd, int value);

/// The system message font (Segoe UI) at the window's DPI, optionally bold. Owned by the caller.
HFONT CreateUiFont(HWND hwnd, bool bold = false);
void ApplyFont(HWND parent, HFONT font);

HWND CreateChild(HWND parent, const wchar_t *className, const wchar_t *text, DWORD style, int id = 0,
                 DWORD exStyle = 0);
void SetText(HWND hwnd, const std::wstring &text);
/// A fraction from 0 to 1, or a negative value for an indeterminate (marquee) bar.
void SetProgress(HWND bar, double fraction);
std::wstring GetText(HWND hwnd);

std::optional<std::wstring> PickFolder(HWND owner, const wchar_t *title);
std::vector<std::wstring> PickArchives(HWND owner);

void RevealInExplorer(const std::wstring &path);

/// A task dialog with a primary action and a dismiss button (Cancel unless named); true if the action was chosen.
bool Confirm(HWND owner, const std::wstring &title, const std::wstring &message, const wchar_t *action,
             const wchar_t *dismiss = nullptr);
void ShowError(HWND owner, const std::wstring &title, const std::wstring &message);

/// The windows of the app; the process ends when the last one closes.
void WindowOpened();
void WindowClosed();
