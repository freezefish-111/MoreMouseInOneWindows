// cursor_style.cpp - system cursor snapshots, image import/export and style storage
#include "cursor_style.h"

#include <gdiplus.h>

#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <iterator>
#include <vector>

using namespace Gdiplus;

namespace mc {

namespace {

#ifndef OCR_HELP
#define OCR_HELP 32651
#endif
#ifndef OCR_PIN
#define OCR_PIN 32671
#endif
#ifndef OCR_PERSON
#define OCR_PERSON 32672
#endif

struct StateInfo {
    const wchar_t* name;
    const wchar_t* iniKey;
    UINT systemId;
};

const StateInfo kStates[CursorStateCount] = {
    { L"普通箭头",   L"normal",      OCR_NORMAL     },
    { L"文字输入",   L"text",        OCR_IBEAM      },
    { L"等待",       L"wait",        OCR_WAIT       },
    { L"后台运行",   L"appstarting", OCR_APPSTARTING},
    { L"十字准星",   L"cross",       OCR_CROSS      },
    { L"帮助",       L"help",        OCR_HELP       },
    { L"链接/手型",  L"hand",        OCR_HAND       },
    { L"禁止",       L"no",          OCR_NO         },
    { L"垂直缩放",   L"sizens",      OCR_SIZENS     },
    { L"水平缩放",   L"sizewe",      OCR_SIZEWE     },
    { L"斜向缩放 \\", L"sizenwse",    OCR_SIZENWSE   },
    { L"斜向缩放 /", L"sizenesw",    OCR_SIZENESW   },
    { L"移动",       L"sizeall",     OCR_SIZEALL    },
    { L"向上箭头",   L"up",          OCR_UP         },
    { L"图钉",       L"pin",         OCR_PIN        },
    { L"人物",       L"person",      OCR_PERSON     },
};

bool g_gdiplusReady = false;
ULONG_PTR g_gdiplusToken = 0;

uint32_t Premultiply(uint32_t argb) {
    int a = (int)((argb >> 24) & 0xFF);
    int r = (int)((argb >> 16) & 0xFF);
    int g = (int)((argb >> 8) & 0xFF);
    int b = (int)(argb & 0xFF);
    r = r * a / 255;
    g = g * a / 255;
    b = b * a / 255;
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

uint32_t Unpremultiply(uint32_t bgra) {
    int a = (int)((bgra >> 24) & 0xFF);
    int r = (int)((bgra >> 16) & 0xFF);
    int g = (int)((bgra >> 8) & 0xFF);
    int b = (int)(bgra & 0xFF);
    if (a > 0 && a < 255) {
        r = std::min(255, r * 255 / a);
        g = std::min(255, g * 255 / a);
        b = std::min(255, b * 255 / a);
    } else if (a == 0) {
        r = g = b = 0;
    }
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

int ClampInt(int v, int lo, int hi) {
    return std::max(lo, std::min(hi, v));
}

CursorImage ScaleImage(const CursorImage& src, int targetW, int targetH) {
    if (src.width <= 0 || src.height <= 0 || targetW <= 0 || targetH <= 0) return src;
    if (src.width == targetW && src.height == targetH) return src;
    CursorImage out;
    out.width = targetW;
    out.height = targetH;
    out.pixels.resize((size_t)targetW * targetH);
    for (int y = 0; y < targetH; ++y) {
        int sy = std::min(src.height - 1, y * src.height / targetH);
        for (int x = 0; x < targetW; ++x) {
            int sx = std::min(src.width - 1, x * src.width / targetW);
            out.pixels[(size_t)y * targetW + x] = src.pixels[(size_t)sy * src.width + sx];
        }
    }
    out.hotspotX = ClampInt(src.hotspotX * targetW / src.width, 0, targetW - 1);
    out.hotspotY = ClampInt(src.hotspotY * targetH / src.height, 0, targetH - 1);
    return out;
}

CursorImage IconToCursorImage(HICON icon, int targetSize) {
    CursorImage img;
    if (!icon) return img;
    ICONINFO ii = {};
    if (!GetIconInfo(icon, &ii)) return img;

    BITMAP bm = {};
    int w = 0, h = 0;
    if (ii.hbmColor && GetObject(ii.hbmColor, sizeof(bm), &bm) > 0) {
        w = bm.bmWidth;
        h = bm.bmHeight;
    } else if (ii.hbmMask && GetObject(ii.hbmMask, sizeof(bm), &bm) > 0) {
        w = bm.bmWidth;
        h = bm.bmHeight / 2;
    }
    if (w <= 0 || h <= 0 || w > 256 || h > 256) {
        if (ii.hbmColor) DeleteObject(ii.hbmColor);
        if (ii.hbmMask) DeleteObject(ii.hbmMask);
        return img;
    }

    HDC dc = GetDC(nullptr);
    std::vector<uint32_t> color((size_t)w * h, 0);
    std::vector<uint8_t> mask;
    BITMAPINFOHEADER bi = {};
    bi.biSize = sizeof(bi);
    bi.biWidth = w;
    bi.biHeight = -h;
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;

    if (ii.hbmColor) {
        GetDIBits(dc, ii.hbmColor, 0, h, color.data(), (BITMAPINFO*)&bi, DIB_RGB_COLORS);
    }
    if (ii.hbmMask) {
        int stride = ((w + 31) / 32) * 4;
        mask.assign((size_t)stride * h, 0);
        BITMAPINFOHEADER mi = {};
        mi.biSize = sizeof(mi);
        mi.biWidth = w;
        mi.biHeight = -h;
        mi.biPlanes = 1;
        mi.biBitCount = 1;
        mi.biCompression = BI_RGB;
        GetDIBits(dc, ii.hbmMask, 0, h, mask.data(), (BITMAPINFO*)&mi, DIB_RGB_COLORS);
    }
    ReleaseDC(nullptr, dc);

    img.width = w;
    img.height = h;
    img.hotspotX = ClampInt(ii.xHotspot, 0, w - 1);
    img.hotspotY = ClampInt(ii.yHotspot, 0, h - 1);
    img.pixels.resize((size_t)w * h);

    int maskStride = ((w + 31) / 32) * 4;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            uint32_t c = color[(size_t)y * w + x];  // 0xAARRGGBB
            int a = (int)((c >> 24) & 0xFF);
            if (!mask.empty()) {
                uint8_t bit = mask[(size_t)y * maskStride + x / 8];
                bool transparent = (bit & (0x80 >> (x % 8))) != 0;
                if (transparent) a = 0;
                else if (a == 0) a = 255;
            }
            int r = (int)((c >> 16) & 0xFF);
            int g = (int)((c >> 8) & 0xFF);
            int b = (int)(c & 0xFF);
            r = r * a / 255;
            g = g * a / 255;
            b = b * a / 255;
            img.pixels[(size_t)y * w + x] =
                ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
    }

    if (ii.hbmColor) DeleteObject(ii.hbmColor);
    if (ii.hbmMask) DeleteObject(ii.hbmMask);

    if (targetSize > 0) img = ScaleImage(img, targetSize, targetSize);
    return img;
}

} // namespace

const wchar_t* CursorStateName(CursorState state) {
    int i = (int)state;
    if (i < 0 || i >= CursorStateCount) return L"";
    return kStates[i].name;
}

const wchar_t* CursorStateIniKey(CursorState state) {
    int i = (int)state;
    if (i < 0 || i >= CursorStateCount) return L"";
    return kStates[i].iniKey;
}

UINT CursorStateSystemId(CursorState state) {
    int i = (int)state;
    if (i < 0 || i >= CursorStateCount) return OCR_NORMAL;
    return kStates[i].systemId;
}

void EnsureGdiplus() {
    if (g_gdiplusReady) return;
    GdiplusStartupInput input;
    input.GdiplusVersion = 1;
    input.DebugEventCallback = nullptr;
    input.SuppressBackgroundThread = FALSE;
    input.SuppressExternalCodecs = FALSE;
    if (GdiplusStartup(&g_gdiplusToken, &input, nullptr) == Ok) {
        g_gdiplusReady = true;
    }
}

std::wstring CursorStylesIniPath() {
    return GetExeDir() + L"\\Cursors.ini";
}

std::wstring CursorStylesDir() {
    return GetExeDir() + L"\\Cursors";
}

CursorStyleSet LoadBuiltinStyles() {
    CursorStyleSet set;
    int size = GetSystemMetrics(SM_CXCURSOR);
    if (size < 16) size = 32;
    if (size > 128) size = 128;

    for (int i = 0; i < CursorStateCount; ++i) {
        CursorState state = (CursorState)i;
        HICON icon = (HICON)LoadCursorW(nullptr, MAKEINTRESOURCE(CursorStateSystemId(state)));
        if (!icon) icon = (HICON)LoadCursorW(nullptr, IDC_ARROW);
        CursorImage img = IconToCursorImage(icon, size);
        if (img.pixels.empty()) img = MakeCursorImage(size, RGB(0, 0, 0));
        set.images[i] = img;
    }
    Log(L"Loaded %d builtin cursor states (size=%d)", CursorStateCount, size);
    return set;
}

bool LoadImageFile(const std::wstring& path, CursorImage& img) {
    EnsureGdiplus();
    Image source(path.c_str());
    if (source.GetLastStatus() != Ok) return false;
    int w = source.GetWidth();
    int h = source.GetHeight();
    if (w <= 0 || h <= 0) return false;

    Bitmap bmp(w, h, PixelFormat32bppARGB);
    {
        Graphics g(&bmp);
        g.Clear(Color(0, 0, 0, 0));
        g.DrawImage(&source, 0, 0, w, h);
    }

    Rect rect(0, 0, w, h);
    BitmapData data = {};
    if (bmp.LockBits(&rect, ImageLockModeRead, PixelFormat32bppARGB, &data) != Ok) return false;

    img = CursorImage();
    img.width = w;
    img.height = h;
    img.hotspotX = 0;
    img.hotspotY = 0;
    img.pixels.resize((size_t)w * h);
    for (int y = 0; y < h; ++y) {
        uint32_t* row = (uint32_t*)((BYTE*)data.Scan0 + (size_t)y * data.Stride);
        for (int x = 0; x < w; ++x) {
            img.pixels[(size_t)y * w + x] = Premultiply(row[x]);
        }
    }
    bmp.UnlockBits(&data);
    Log(L"Loaded cursor image %ls (%dx%d)", path.c_str(), w, h);
    return true;
}

bool SaveCursorStyle(CursorState state, const CursorImage& img) {
    if (img.width <= 0 || img.height <= 0 || img.pixels.empty()) return false;
    EnsureGdiplus();

    std::wstring dir = CursorStylesDir();
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring key = CursorStateIniKey(state);
    std::wstring rel = L"Cursors\\" + key + L".png";
    std::wstring full = dir + L"\\" + key + L".png";

    Bitmap bmp(img.width, img.height, PixelFormat32bppARGB);
    Rect rect(0, 0, img.width, img.height);
    BitmapData data = {};
    if (bmp.LockBits(&rect, ImageLockModeWrite, PixelFormat32bppARGB, &data) != Ok) return false;
    for (int y = 0; y < img.height; ++y) {
        uint32_t* row = (uint32_t*)((BYTE*)data.Scan0 + (size_t)y * data.Stride);
        for (int x = 0; x < img.width; ++x) {
            row[x] = Unpremultiply(img.pixels[(size_t)y * img.width + x]);
        }
    }
    bmp.UnlockBits(&data);

    CLSID pngClsid = {};
    HRESULT hr = CLSIDFromString(L"{B96B3CAE-0728-11D3-9D7B-0000F81EF32E}", &pngClsid);
    Status saveStatus = GenericError;
    if (SUCCEEDED(hr)) saveStatus = bmp.Save(full.c_str(), &pngClsid, nullptr);

    if (FAILED(hr) || saveStatus != Ok) {
        Log(L"PNG save failed (hr=0x%08lx, status=%d); falling back to BMP",
            (unsigned long)hr, (int)saveStatus);
        DeleteFileW(full.c_str());

        CLSID bmpClsid = {};
        hr = CLSIDFromString(L"{B96B3CAB-0728-11D3-9D7B-0000F81EF32E}", &bmpClsid);
        if (FAILED(hr)) {
            Log(L"BMP encoder CLSID lookup failed, hr=0x%08lx", (unsigned long)hr);
            return false;
        }
        rel = L"Cursors\\" + key + L".bmp";
        full = dir + L"\\" + key + L".bmp";
        saveStatus = bmp.Save(full.c_str(), &bmpClsid, nullptr);
        if (saveStatus != Ok) {
            Log(L"Failed to save cursor BMP: %ls (status=%d)", full.c_str(), (int)saveStatus);
            return false;
        }
    }

    std::wstring ini = CursorStylesIniPath();
    WritePrivateProfileStringW(key.c_str(), L"image", rel.c_str(), ini.c_str());
    WritePrivateProfileStringW(key.c_str(), L"hotspotX", std::to_wstring(img.hotspotX).c_str(), ini.c_str());
    WritePrivateProfileStringW(key.c_str(), L"hotspotY", std::to_wstring(img.hotspotY).c_str(), ini.c_str());
    WritePrivateProfileStringW(key.c_str(), L"custom", L"1", ini.c_str());
    Log(L"Saved cursor style %ls -> %ls", key.c_str(), rel.c_str());
    return true;
}

void DeleteCursorStyle(CursorState state) {
    std::wstring key = CursorStateIniKey(state);
    std::wstring ini = CursorStylesIniPath();
    WritePrivateProfileStringW(key.c_str(), nullptr, nullptr, ini.c_str());
    std::wstring dir = CursorStylesDir();
    for (const wchar_t* ext : { L".png", L".bmp" }) {
        std::wstring file = dir + L"\\" + key + ext;
        DeleteFileW(file.c_str());
    }
    Log(L"Removed custom cursor style %ls", key.c_str());
}

CursorStyleSet LoadCustomStyles(const CursorStyleSet& builtin) {
    CursorStyleSet out = builtin;
    std::wstring ini = CursorStylesIniPath();
    std::wstring exeDir = GetExeDir();

    for (int i = 0; i < CursorStateCount; ++i) {
        std::wstring key = CursorStateIniKey((CursorState)i);
        wchar_t value[MAX_PATH] = {};
        GetPrivateProfileStringW(key.c_str(), L"image", L"", value, MAX_PATH, ini.c_str());
        if (!value[0]) continue;

        std::wstring path = value;
        if (path.find(L":\\") == std::wstring::npos && path.compare(0, 2, L"\\\\") != 0) {
            path = exeDir + L"\\" + path;
        }
        CursorImage img;
        if (!LoadImageFile(path, img) || img.pixels.empty()) {
            Log(L"Custom cursor image for %ls not loaded: %ls", key.c_str(), path.c_str());
            continue;
        }
        int hx = GetPrivateProfileIntW(key.c_str(), L"hotspotX", img.hotspotX, ini.c_str());
        int hy = GetPrivateProfileIntW(key.c_str(), L"hotspotY", img.hotspotY, ini.c_str());
        img.hotspotX = ClampInt(hx, 0, img.width - 1);
        img.hotspotY = ClampInt(hy, 0, img.height - 1);
        out.images[i] = img;
        out.custom[i] = true;
    }
    return out;
}

CursorImage WithMouseBadge(const CursorImage& src, COLORREF color) {
    CursorImage out = src;
    if (out.pixels.empty()) return out;
    int r = std::max(3, std::min(out.width, out.height) / 9);
    int cx = out.width - r - 1;
    int cy = out.height - r - 1;
    uint32_t badge = 0xFF000000u |
                     ((uint32_t)GetRValue(color) << 16) |
                     ((uint32_t)GetGValue(color) << 8) |
                     (uint32_t)GetBValue(color);
    uint32_t white = 0xFFFFFFFFu;

    for (int y = cy - r - 1; y <= cy + r + 1; ++y) {
        for (int x = cx - r - 1; x <= cx + r + 1; ++x) {
            if (x < 0 || y < 0 || x >= out.width || y >= out.height) continue;
            int dx = x - cx, dy = y - cy;
            int d2 = dx * dx + dy * dy;
            if (d2 <= r * r) out.pixels[(size_t)y * out.width + x] = badge;
            else if (d2 <= (r + 1) * (r + 1)) out.pixels[(size_t)y * out.width + x] = white;
        }
    }
    return out;
}

} // namespace mc
