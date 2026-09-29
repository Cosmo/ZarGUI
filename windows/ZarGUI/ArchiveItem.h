#pragma once

#include "ArchiveItem.g.h"

namespace winrt::ZarGUI::implementation {

struct ArchiveItem : ArchiveItemT<ArchiveItem> {
    ArchiveItem(uint64_t index, hstring name, hstring size, hstring kind, bool isFolder,
                Microsoft::UI::Xaml::Media::ImageSource icon)
        : index_(index), name_(std::move(name)), size_(std::move(size)), kind_(std::move(kind)), isFolder_(isFolder),
          icon_(std::move(icon)) {}

    uint64_t Index() const { return index_; }
    hstring Name() const { return name_; }
    hstring Size() const { return size_; }
    hstring Kind() const { return kind_; }
    bool IsFolder() const { return isFolder_; }
    Microsoft::UI::Xaml::Media::ImageSource Icon() const { return icon_; }

private:
    uint64_t index_;
    hstring name_, size_, kind_;
    bool isFolder_;
    Microsoft::UI::Xaml::Media::ImageSource icon_;
};

} // namespace winrt::ZarGUI::implementation
