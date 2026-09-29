#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

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
/// Wraps `s` in typographic quotes.
std::wstring Quoted(const std::wstring &s);

/// The standard Windows folder and file pickers.
std::optional<std::wstring> PickFolder(HWND owner, const wchar_t *title);
std::vector<std::wstring> PickArchives(HWND owner);

/// Opens File Explorer with the item selected.
void RevealInExplorer(const std::wstring &path);
