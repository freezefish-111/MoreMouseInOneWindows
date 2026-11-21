// cursor_image.h - software cursor rasterizer
#pragma once

#include "common.h"

namespace mc {

struct CursorImage {
    int width = 0;
    int height = 0;
    int hotspotX = 0;
    int hotspotY = 0;
    std::vector<uint32_t> pixels;  // premultiplied BGRA, top-down
};

// Build a classic arrow cursor at roughly the system cursor size.
CursorImage MakeCursorImage(int cursorSizePx, COLORREF outline);
HBITMAP CreateCursorDib(const CursorImage& img, void** bitsOut);
HICON CreateCursorIcon(const CursorImage& img, int canvasSize);
COLORREF CursorColor(int index);

} // namespace mc
