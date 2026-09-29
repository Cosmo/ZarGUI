#include "settings.hpp"

#include "common.hpp"

namespace {

constexpr wchar_t kKey[] = L"Software\\ZarGUI";

DWORD ReadNumber(HKEY key, const wchar_t *name, DWORD fallback) {
    DWORD value = 0, size = sizeof(value);
    return RegGetValueW(key, nullptr, name, RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS ? value
                                                                                                       : fallback;
}

std::wstring ReadString(HKEY key, const wchar_t *name) {
    DWORD size = 0;
    if (RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, nullptr, nullptr, &size) != ERROR_SUCCESS) return {};
    std::wstring value(size / sizeof(wchar_t), L'\0');
    if (RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, nullptr, value.data(), &size) != ERROR_SUCCESS) return {};
    value.resize(wcslen(value.c_str()));
    return value;
}

void WriteNumber(HKEY key, const wchar_t *name, DWORD value) {
    RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE *>(&value), sizeof(value));
}

void WriteString(HKEY key, const wchar_t *name, const std::wstring &value) {
    RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE *>(value.c_str()),
                   static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
}

template <class Enum>
Enum ReadEnum(HKEY key, const wchar_t *name, Enum fallback, Enum last) {
    DWORD value = ReadNumber(key, name, static_cast<DWORD>(fallback));
    return value <= static_cast<DWORD>(last) ? static_cast<Enum>(value) : fallback;
}

} // namespace

int Settings::CompressionLevel() const {
    switch (compression) {
    case Compression::Faster: return 1;
    case Compression::Smaller: return 12;
    default: return 0;
    }
}

Settings Settings::Load() {
    Settings s;
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kKey, 0, KEY_READ, &key) != ERROR_SUCCESS) return s;
    s.outputFolder = ReadString(key, L"OutputFolder");
    s.existingArchive = ReadEnum(key, L"ExistingArchive", s.existingArchive, ExistingArchive::KeepBoth);
    s.compression = ReadEnum(key, L"Compression", s.compression, Compression::Smaller);
    s.skipSystemFiles = ReadNumber(key, L"SkipSystemFiles", 1) != 0;
    s.extractDestination = ReadEnum(key, L"ExtractDestination", s.extractDestination, ExtractDestination::NextToArchive);
    s.revealAfterExtract = ReadNumber(key, L"RevealAfterExtract", 0) != 0;
    RegCloseKey(key);
    return s;
}

void Settings::Save() const {
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kKey, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return;
    WriteString(key, L"OutputFolder", outputFolder);
    WriteNumber(key, L"ExistingArchive", static_cast<DWORD>(existingArchive));
    WriteNumber(key, L"Compression", static_cast<DWORD>(compression));
    WriteNumber(key, L"SkipSystemFiles", skipSystemFiles ? 1 : 0);
    WriteNumber(key, L"ExtractDestination", static_cast<DWORD>(extractDestination));
    WriteNumber(key, L"RevealAfterExtract", revealAfterExtract ? 1 : 0);
    RegCloseKey(key);
}
