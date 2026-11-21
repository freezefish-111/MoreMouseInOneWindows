// overlay.cpp - layered topmost cursor windows
#include "overlay.h"

namespace mc {

namespace {

LRESULT CALLBACK OverlayProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }
    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

void EnsureClass() {
    static bool registered = false;
    if (registered) return;
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = OverlayProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = nullptr;
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kOverlayClass;
    RegisterClassExW(&wc);
    registered = true;
}

} // namespace

CursorOverlay::~CursorOverlay() {
    Destroy();
}

bool CursorOverlay::Create(const CursorImage& img) {
    Destroy();
    EnsureClass();

    hwnd_ = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        kOverlayClass, L"", WS_POPUP, 0, 0, img.width, img.height,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hwnd_) {
        Log(L"CursorOverlay::CreateWindowEx failed, err=%lu", GetLastError());
        return false;
    }

    void* bits = nullptr;
    HBITMAP dib = CreateCursorDib(img, &bits);
    if (!dib) {
        Log(L"CursorOverlay::CreateCursorDib failed, err=%lu", GetLastError());
        Destroy();
        return false;
    }

    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    HGDIOBJ old = SelectObject(mem, dib);

    POINT dst = { 0, 0 };
    SIZE size = { img.width, img.height };
    POINT src = { 0, 0 };
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    BOOL ok = UpdateLayeredWindow(hwnd_, screen, &dst, &size, mem, &src, 0, &blend, ULW_ALPHA);

    SelectObject(mem, old);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    DeleteObject(dib);

    if (!ok) {
        Log(L"CursorOverlay::UpdateLayeredWindow failed, err=%lu", GetLastError());
        Destroy();
        return false;
    }

    hotspotX_ = img.hotspotX;
    hotspotY_ = img.hotspotY;
    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    // The caller positions the window before showing it, avoiding a flash at (0,0).
    return true;
}

void CursorOverlay::MoveTo(int hotspotX, int hotspotY) {
    if (!hwnd_) return;
    SetWindowPos(hwnd_, HWND_TOPMOST, hotspotX - hotspotX_, hotspotY - hotspotY_, 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOSENDCHANGING);
}

void CursorOverlay::Show(bool show) {
    if (!hwnd_) return;
    ShowWindow(hwnd_, show ? SW_SHOWNOACTIVATE : SW_HIDE);
}

void CursorOverlay::Destroy() {
    if (!hwnd_) return;
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
}

} // namespace mc
