#include "pch.h"

#include "ShellIcons.h"

#include <commoncontrols.h>
#include <shellapi.h>
#include <shlwapi.h>

#include <vector>

using namespace winrt;
using namespace Microsoft::UI::Xaml::Media::Imaging;

namespace {

/// Converts an icon to a bitmap WinUI can show (premultiplied BGRA).
WriteableBitmap ToBitmap(HICON icon) {
    ICONINFO info{};
    if (!GetIconInfo(icon, &info)) return nullptr;
    BITMAP bitmap{};
    GetObjectW(info.hbmColor, sizeof(bitmap), &bitmap);
    int width = bitmap.bmWidth, height = bitmap.bmHeight;

    BITMAPINFO format{};
    format.bmiHeader = {sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB};
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
    HDC dc = GetDC(nullptr);
    GetDIBits(dc, info.hbmColor, 0, height, pixels.data(), &format, DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);
    DeleteObject(info.hbmColor);
    DeleteObject(info.hbmMask);

    WriteableBitmap result(width, height);
    uint8_t *out = result.PixelBuffer().data();
    for (size_t i = 0; i < pixels.size(); i += 4) {
        uint8_t alpha = pixels[i + 3];
        for (size_t c = 0; c < 3; c++) out[i + c] = static_cast<uint8_t>(pixels[i + c] * alpha / 255);
        out[i + 3] = alpha;
    }
    result.Invalidate();
    return result;
}

} // namespace

const ShellIcons::Info &ShellIcons::For(const std::wstring &name, bool isFolder) {
    std::wstring key = isFolder ? L"/" : std::wstring(PathFindExtensionW(name.c_str()));
    CharLowerW(key.data());
    if (auto found = cache_.find(key); found != cache_.end()) return found->second;

    SHFILEINFOW file{};
    std::wstring sample = isFolder ? L"folder" : L"file" + key;
    SHGetFileInfoW(sample.c_str(), isFolder ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL, &file, sizeof(file),
                   SHGFI_USEFILEATTRIBUTES | SHGFI_SYSICONINDEX | SHGFI_TYPENAME);

    Info info;
    info.typeName = file.szTypeName;
    // 32 px icons, shown at 16 epx, stay sharp up to 200 % scaling.
    com_ptr<IImageList> images;
    if (SUCCEEDED(SHGetImageList(SHIL_LARGE, IID_PPV_ARGS(images.put())))) {
        HICON icon = nullptr;
        if (SUCCEEDED(images->GetIcon(file.iIcon, ILD_TRANSPARENT, &icon)) && icon) {
            info.icon = ToBitmap(icon);
            DestroyIcon(icon);
        }
    }
    return cache_.emplace(key, std::move(info)).first->second;
}
