#pragma once

#include <string>

enum class ExistingArchive { Ask, Replace, KeepBoth };
enum class ExtractDestination { Ask, NextToArchive };
enum class Compression { Faster, Standard, Smaller };

/// User settings, stored under HKEY_CURRENT_USER\Software\ZarGUI.
struct Settings {
    std::wstring outputFolder; // empty: next to the original folder
    ExistingArchive existingArchive = ExistingArchive::Ask;
    Compression compression = Compression::Standard;
    bool skipSystemFiles = true;
    ExtractDestination extractDestination = ExtractDestination::Ask;
    bool revealAfterExtract = false;

    /// zstd level for the core; 0 is its default.
    int CompressionLevel() const;

    static Settings Load();
    void Save() const;
};
