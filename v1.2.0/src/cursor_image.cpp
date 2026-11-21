// cursor_image.cpp - anti-aliased software cursor rasterizer
#include "cursor_image.h"

#include <algorithm>
#include <cstring>

namespace mc {

namespace {

struct Pt { double x, y; };

// Classic arrow silhouette in a 12 x 20 design box, tip at (0,0).
const Pt kArrow[] = {
    { 0.0,  0.0},
    { 0.0, 16.0},
    { 4.0, 12.4},
    { 6.9, 20.0},
    { 9.9, 18.6},
    { 7.0, 11.2},
    {12.0, 11.2},
};
constexpr int kArrowCount = (int)(sizeof(kArrow) / sizeof(kArrow[0]));
constexpr double kArrowW = 12.0;
constexpr double kArrowH = 20.0;
constexpr double kOutline = 0.95;

bool PointInPolygon(double x, double y) {
    bool inside = false;
    for (int i = 0, j = kArrowCount - 1; i < kArrowCount; j = i++) {
        const Pt& a = kArrow[i];
        const Pt& b = kArrow[j];
        if (((a.y > y) != (b.y > y)) &&
            (x < (b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x)) {
            inside = !inside;
        }
    }
    return inside;
}

double DistToSegment(double px, double py, const Pt& a, const Pt& b) {
    double vx = b.x - a.x;
    double vy = b.y - a.y;
    double wx = px - a.x;
    double wy = py - a.y;
    double len2 = vx * vx + vy * vy;
    double t = len2 > 0.0 ? (wx * vx + wy * vy) / len2 : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    double dx = px - (a.x + t * vx);
    double dy = py - (a.y + t * vy);
    return std::sqrt(dx * dx + dy * dy);
}

double DistToOutline(double x, double y) {
    double best = 1e9;
    for (int i = 0, j = kArrowCount - 1; i < kArrowCount; j = i++) {
        best = std::min(best, DistToSegment(x, y, kArrow[j], kArrow[i]));
    }
    return best;
}

struct Rgb { int r, g, b; };

Rgb ToRgb(COLORREF c) {
    return { (int)GetRValue(c), (int)GetGValue(c), (int)GetBValue(c) };
}

} // namespace

COLORREF CursorColor(int index) {
    static const COLORREF colors[] = {
        RGB( 24,  24,  24),  // 1: near black (classic)
        RGB(  0, 120, 215),  // 2: blue
        RGB( 20, 140,  60),  // 3: green
        RGB(220, 100,   0),  // 4: orange
        RGB(130,  50, 180),  // 5: purple
        RGB(205,  35,  45),  // 6: red
        RGB(  0, 135, 130),  // 7: teal
        RGB(200,  30, 110),  // 8: magenta
        RGB( 90, 110, 125),  // 9: slate
        RGB(150, 110,   0),  // 10: dark amber
    };
    constexpr int n = (int)(sizeof(colors) / sizeof(colors[0]));
    return colors[((index % n) + n) % n];
}

CursorImage MakeCursorImage(int cursorSizePx, COLORREF outline) {
    CursorImage img;
    if (cursorSizePx < 16) cursorSizePx = 16;
    if (cursorSizePx > 128) cursorSizePx = 128;

    const double scale = cursorSizePx / 32.0;
    const int pad = (int)std::ceil(2.0 * scale) + 1;
    const int w = (int)std::ceil(kArrowW * scale) + pad * 2;
    const int h = (int)std::ceil(kArrowH * scale) + pad * 2;

    img.width = w;
    img.height = h;
    img.hotspotX = pad;
    img.hotspotY = pad;
    img.pixels.assign((size_t)w * h, 0u);

    const Rgb line = ToRgb(outline);
    const Rgb fill = { 255, 255, 255 };
    constexpr int SS = 8;
    constexpr int N2 = SS * SS;

    for (int py = 0; py < h; ++py) {
        for (int px = 0; px < w; ++px) {
            int sumR = 0, sumG = 0, sumB = 0, insideCount = 0;
            for (int sy = 0; sy < SS; ++sy) {
                for (int sx = 0; sx < SS; ++sx) {
                    double dx = (px + (sx + 0.5) / SS - pad) / scale;
                    double dy = (py + (sy + 0.5) / SS - pad) / scale;
                    if (!PointInPolygon(dx, dy)) continue;
                    const Rgb& c = (DistToOutline(dx, dy) <= kOutline) ? line : fill;
                    sumR += c.r;
                    sumG += c.g;
                    sumB += c.b;
                    ++insideCount;
                }
            }
            if (!insideCount) continue;
            int a = (insideCount * 255 + N2 / 2) / N2;
            int r = (sumR + N2 / 2) / N2;
            int g = (sumG + N2 / 2) / N2;
            int b = (sumB + N2 / 2) / N2;
            img.pixels[(size_t)py * w + px] =
                ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
    }
    return img;
}

HBITMAP CreateCursorDib(const CursorImage& img, void** bitsOut) {
    if (bitsOut) *bitsOut = nullptr;
    if (img.width <= 0 || img.height <= 0) return nullptr;

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = img.width;
    bi.bmiHeader.biHeight = -img.height;  // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP hbmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!hbmp || !bits) {
        if (hbmp) DeleteObject(hbmp);
        return nullptr;
    }
    std::memcpy(bits, img.pixels.data(), img.pixels.size() * sizeof(uint32_t));
    if (bitsOut) *bitsOut = bits;
    return hbmp;
}

HICON CreateCursorIcon(const CursorImage& img, int canvasSize) {
    if (canvasSize < 16) canvasSize = 16;
    if (img.width <= 0 || img.height <= 0) return nullptr;

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = canvasSize;
    bi.bmiHeader.biHeight = -canvasSize;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP color = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!color || !bits) {
        if (color) DeleteObject(color);
        return nullptr;
    }
    std::memset(bits, 0, (size_t)canvasSize * canvasSize * 4);

    int ox = (canvasSize - img.width) / 2;
    int oy = (canvasSize - img.height) / 2;
    uint32_t* dst = (uint32_t*)bits;
    for (int y = 0; y < img.height; ++y) {
        int dy = y + oy;
        if (dy < 0 || dy >= canvasSize) continue;
        for (int x = 0; x < img.width; ++x) {
            int dx = x + ox;
            if (dx < 0 || dx >= canvasSize) continue;
            uint32_t p = img.pixels[(size_t)y * img.width + x];
            uint32_t a = p >> 24;
            if (!a) continue;
            // Icons use straight alpha; our source is premultiplied.
            uint32_t r = (p >> 16) & 0xFF;
            uint32_t g = (p >> 8) & 0xFF;
            uint32_t b = p & 0xFF;
            r = std::min<uint32_t>(255, (r * 255 + a / 2) / a);
            g = std::min<uint32_t>(255, (g * 255 + a / 2) / a);
            b = std::min<uint32_t>(255, (b * 255 + a / 2) / a);
            dst[(size_t)dy * canvasSize + dx] =
                (a << 24) | (r << 16) | (g << 8) | b;
        }
    }

    const int maskStride = ((canvasSize + 31) / 32) * 4;
    std::vector<uint8_t> maskBits((size_t)maskStride * canvasSize, 0);
    HBITMAP mask = CreateBitmap(canvasSize, canvasSize, 1, 1, maskBits.data());

    ICONINFO ii = {};
    ii.fIcon = TRUE;
    ii.hbmColor = color;
    ii.hbmMask = mask;
    HICON icon = CreateIconIndirect(&ii);

    DeleteObject(mask);
    DeleteObject(color);
    return icon;
}

} // namespace mc
