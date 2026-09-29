#pragma once

#include "pch.h"

#include <string>
#include <unordered_map>

/// File Explorer's icons and type names for file names, cached per extension.
class ShellIcons {
public:
    struct Info {
        winrt::Microsoft::UI::Xaml::Media::ImageSource icon{nullptr};
        std::wstring typeName;
    };

    const Info &For(const std::wstring &name, bool isFolder);

private:
    std::unordered_map<std::wstring, Info> cache_;
};
