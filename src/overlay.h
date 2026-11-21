// overlay.h - one click-through layered window per software cursor
#pragma once

#include "cursor_image.h"

namespace mc {

class CursorOverlay {
public:
    CursorOverlay() = default;
    ~CursorOverlay();
    CursorOverlay(const CursorOverlay&) = delete;
    CursorOverlay& operator=(const CursorOverlay&) = delete;

    bool Create(const CursorImage& img);
    void MoveTo(int hotspotX, int hotspotY);
    void Show(bool show);
    void Destroy();
    bool valid() const { return hwnd_ != nullptr; }
    HWND hwnd() const { return hwnd_; }

private:
    HWND hwnd_ = nullptr;
    int hotspotX_ = 0;
    int hotspotY_ = 0;
};

} // namespace mc
