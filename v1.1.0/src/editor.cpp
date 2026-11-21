// editor.cpp - Win32 cursor style editor
#include "editor.h"

#include "cursor_style.h"

#include <gdiplus.h>
#include <commdlg.h>

#include <algorithm>
#include <array>
#include <cwchar>
#include <memory>
#include <vector>

using namespace Gdiplus;

namespace mc {

namespace {

constexpr int ID_STATE_LIST   = 100;
constexpr int ID_BRUSH_EDIT   = 101;
constexpr int ID_BTN_IMPORT   = 200;
constexpr int ID_BTN_DRAW     = 201;
constexpr int ID_BTN_ERASE    = 202;
constexpr int ID_BTN_CLEAR    = 203;
constexpr int ID_BTN_DEFAULT  = 204;
constexpr int ID_BTN_SAVE     = 205;
constexpr int ID_BTN_CANCEL   = 206;

constexpr int PREVIEW_X = 226;
constexpr int PREVIEW_Y = 44;
constexpr int PREVIEW_W = 430;
constexpr int PREVIEW_H = 420;

struct StateEdit {
    CursorImage image;
    bool custom = false;
    bool dirty = false;
};

struct EditorCtx {
    HWND hwnd = nullptr;
    HWND list = nullptr;
    HWND brushEdit = nullptr;
    HWND hotspotLabel = nullptr;
    CursorStyleSet builtin;
    CursorStyleSet styles;
    std::array<StateEdit, CursorStateCount> edits;
    int current = 0;
    int brush = 6;
    bool drawMode = false;
    bool eraseMode = false;
    bool dragging = false;
};

EditorCtx* GetCtx(HWND hwnd) {
    return (EditorCtx*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
}

int g_editorResult = 0;

uint32_t UnpremulPixel(uint32_t px) {
    int a = (int)((px >> 24) & 0xFF);
    int r = (int)((px >> 16) & 0xFF);
    int g = (int)((px >> 8) & 0xFF);
    int b = (int)(px & 0xFF);
    if (a > 0 && a < 255) {
        r = std::min(255, r * 255 / a);
        g = std::min(255, g * 255 / a);
        b = std::min(255, b * 255 / a);
    } else if (a == 0) {
        r = g = b = 0;
    }
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

std::unique_ptr<Bitmap> MakeGdiBitmap(const CursorImage& img) {
    if (img.width <= 0 || img.height <= 0 || img.pixels.empty()) return nullptr;
    auto bmp = std::unique_ptr<Bitmap>(new Bitmap(img.width, img.height, PixelFormat32bppARGB));
    Rect rect(0, 0, img.width, img.height);
    BitmapData data = {};
    if (bmp->LockBits(&rect, ImageLockModeWrite, PixelFormat32bppARGB, &data) != Ok) {
        return nullptr;
    }
    for (int y = 0; y < img.height; ++y) {
        uint32_t* row = (uint32_t*)((BYTE*)data.Scan0 + (size_t)y * data.Stride);
        for (int x = 0; x < img.width; ++x) {
            row[x] = UnpremulPixel(img.pixels[(size_t)y * img.width + x]);
        }
    }
    bmp->UnlockBits(&data);
    return bmp;
}

struct PreviewLayout {
    int x = PREVIEW_X;
    int y = PREVIEW_Y;
    int w = PREVIEW_W;
    int h = PREVIEW_H;
    double scale = 1.0;
    int drawX = 0;
    int drawY = 0;
};

PreviewLayout GetPreviewLayout(const CursorImage& img) {
    PreviewLayout l;
    if (img.width > 0 && img.height > 0) {
        l.scale = std::min(6.0, std::min((double)l.w / img.width, (double)l.h / img.height));
        l.scale = std::max(1.0, l.scale);
        l.drawX = l.x + (int)((l.w - img.width * l.scale) / 2.0);
        l.drawY = l.y + (int)((l.h - img.height * l.scale) / 2.0);
    }
    return l;
}

bool PreviewToImage(const CursorImage& img, const PreviewLayout& l, int px, int py,
                    int& ix, int& iy) {
    if (img.width <= 0 || img.height <= 0 || l.scale <= 0) return false;
    ix = (int)((px - l.drawX) / l.scale);
    iy = (int)((py - l.drawY) / l.scale);
    return ix >= 0 && iy >= 0 && ix < img.width && iy < img.height;
}

void PaintPixel(CursorImage& img, int x, int y, uint32_t color) {
    if (x < 0 || y < 0 || x >= img.width || y >= img.height) return;
    img.pixels[(size_t)y * img.width + x] = color;
}

void PaintBrush(CursorImage& img, int cx, int cy, int diameter, bool erase, COLORREF color) {
    if (img.width <= 0 || img.height <= 0 || img.pixels.empty()) return;
    int r = std::max(1, diameter / 2);
    uint32_t px = erase ? 0u
                        : (0xFF000000u |
                           ((uint32_t)GetRValue(color) << 16) |
                           ((uint32_t)GetGValue(color) << 8) |
                           (uint32_t)GetBValue(color));
    for (int y = cy - r; y <= cy + r; ++y) {
        for (int x = cx - r; x <= cx + r; ++x) {
            int dx = x - cx, dy = y - cy;
            if (dx * dx + dy * dy <= r * r) PaintPixel(img, x, y, px);
        }
    }
}

CursorImage LimitImageSize(const CursorImage& src, int maxSize) {
    if (src.width <= maxSize && src.height <= maxSize) return src;
    double s = std::min((double)maxSize / src.width, (double)maxSize / src.height);
    int w = std::max(1, (int)(src.width * s));
    int h = std::max(1, (int)(src.height * s));
    CursorImage out;
    out.width = w;
    out.height = h;
    out.hotspotX = std::min(w - 1, (int)(src.hotspotX * s));
    out.hotspotY = std::min(h - 1, (int)(src.hotspotY * s));
    out.pixels.resize((size_t)w * h);
    for (int y = 0; y < h; ++y) {
        int sy = std::min(src.height - 1, y * src.height / h);
        for (int x = 0; x < w; ++x) {
            int sx = std::min(src.width - 1, x * src.width / w);
            out.pixels[(size_t)y * w + x] = src.pixels[(size_t)sy * src.width + sx];
        }
    }
    return out;
}

HWND MakeControl(HWND parent, const wchar_t* cls, const wchar_t* text, int id,
                 int x, int y, int w, int h, DWORD style) {
    return CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                           x, y, w, h, parent, (HMENU)(INT_PTR)id,
                           GetModuleHandleW(nullptr), nullptr);
}

void RefreshButtons(EditorCtx* ctx) {
    SetDlgItemTextW(ctx->hwnd, ID_BTN_DRAW,
                    ctx->drawMode ? L"画笔 (开)" : L"画笔");
    SetDlgItemTextW(ctx->hwnd, ID_BTN_ERASE,
                    ctx->eraseMode ? L"橡皮 (开)" : L"橡皮");
}

void UpdateHotspotLabel(EditorCtx* ctx) {
    if (!ctx->hotspotLabel) return;
    const CursorImage& img = ctx->edits[ctx->current].image;
    wchar_t buf[128];
    swprintf(buf, std::size(buf), L"热点: (%d, %d)   大小: %dx%d",
             img.hotspotX, img.hotspotY, img.width, img.height);
    SetWindowTextW(ctx->hotspotLabel, buf);
}

void PaintPreview(EditorCtx* ctx, HDC hdc) {
    Graphics g(hdc);
    g.SetSmoothingMode(SmoothingModeHighQuality);
    g.SetInterpolationMode(InterpolationModeNearestNeighbor);

    Rect preview(PREVIEW_X, PREVIEW_Y, PREVIEW_W, PREVIEW_H);
    SolidBrush light(Color(255, 245, 245, 245));
    SolidBrush dark(Color(255, 215, 215, 215));
    const int cell = 16;
    for (int y = 0; y < PREVIEW_H; y += cell) {
        for (int x = 0; x < PREVIEW_X + PREVIEW_W; x += cell) {
            bool useLight = ((x / cell) + (y / cell)) % 2 == 0;
            g.FillRectangle(useLight ? &light : &dark, x, PREVIEW_Y + y, cell, cell);
        }
    }
    Pen border(Color(255, 120, 120, 120), 1.0f);
    g.DrawRectangle(&border, preview);

    const CursorImage& img = ctx->edits[ctx->current].image;
    if (img.width <= 0 || img.height <= 0) return;
    auto bmp = MakeGdiBitmap(img);
    if (!bmp) return;

    PreviewLayout l = GetPreviewLayout(img);
    g.DrawImage(bmp.get(), l.drawX, l.drawY,
                (int)(img.width * l.scale), (int)(img.height * l.scale));

    int hx = l.drawX + (int)(img.hotspotX * l.scale);
    int hy = l.drawY + (int)(img.hotspotY * l.scale);
    Pen hotPen(Color(255, 220, 40, 40), 1.0f);
    g.DrawLine(&hotPen, hx - 8, hy, hx + 8, hy);
    g.DrawLine(&hotPen, hx, hy - 8, hx, hy + 8);
}

void OnImport(EditorCtx* ctx) {
    wchar_t file[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = ctx->hwnd;
    ofn.lpstrFilter = L"图片文件\0*.png;*.jpg;*.jpeg;*.gif;*.bmp\0所有文件\0*.*\0\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    ofn.lpstrTitle = L"导入光标图片";
    if (!GetOpenFileNameW(&ofn)) return;

    CursorImage img;
    if (!LoadImageFile(file, img) || img.pixels.empty()) {
        MessageBoxW(ctx->hwnd, L"无法读取该图片。请使用 PNG、JPG、GIF 或 BMP 文件。",
                    L"导入失败", MB_OK | MB_ICONWARNING);
        return;
    }
    img = LimitImageSize(img, 128);
    StateEdit& edit = ctx->edits[ctx->current];
    edit.image = img;
    edit.custom = true;
    edit.dirty = true;
    UpdateHotspotLabel(ctx);
    InvalidateRect(ctx->hwnd, nullptr, FALSE);
}

void OnSave(EditorCtx* ctx) {
    int saved = 0;
    for (int i = 0; i < CursorStateCount; ++i) {
        StateEdit& edit = ctx->edits[i];
        if (!edit.dirty) continue;
        CursorState state = (CursorState)i;
        bool ok = edit.custom ? SaveCursorStyle(state, edit.image)
                              : (DeleteCursorStyle(state), true);
        if (ok) ++saved;
    }
    if (saved) {
        wchar_t buf[128];
        swprintf(buf, std::size(buf), L"已保存 %d 个光标样式。\n重新启动程序或切换模式即可生效。", saved);
        MessageBoxW(ctx->hwnd, buf, L"保存成功", MB_OK | MB_ICONINFORMATION);
    }
    g_editorResult = 1;
    DestroyWindow(ctx->hwnd);
}

LRESULT CALLBACK EditorProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    EditorCtx* ctx = GetCtx(hwnd);

    switch (msg) {
    case WM_CREATE: {
        ctx = new EditorCtx();
        ctx->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)ctx);

        ctx->builtin = LoadBuiltinStyles();
        ctx->styles = LoadCustomStyles(ctx->builtin);
        for (int i = 0; i < CursorStateCount; ++i) {
            ctx->edits[i].image = ctx->styles.images[i];
            ctx->edits[i].custom = ctx->styles.custom[i];
        }

        ctx->list = MakeControl(hwnd, L"ListBox", nullptr, ID_STATE_LIST,
                                12, 44, 196, 400,
                                WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_HASSTRINGS);
        for (int i = 0; i < CursorStateCount; ++i) {
            SendMessageW(ctx->list, LB_ADDSTRING, 0, (LPARAM)CursorStateName((CursorState)i));
        }
        SendMessageW(ctx->list, LB_SETCURSEL, 0, 0);

        MakeControl(hwnd, L"Static", L"状态", 0, 12, 16, 80, 20, 0);
        MakeControl(hwnd, L"Static", L"预览（单击设置热点）", 0, PREVIEW_X, 16, 300, 20, 0);

        MakeControl(hwnd, L"Button", L"导入图片...", ID_BTN_IMPORT,
                    12, 460, 196, 32, BS_PUSHBUTTON);
        MakeControl(hwnd, L"Button", L"画笔", ID_BTN_DRAW,
                    226, 460, 78, 32, BS_PUSHBUTTON);
        MakeControl(hwnd, L"Button", L"橡皮", ID_BTN_ERASE,
                    310, 460, 78, 32, BS_PUSHBUTTON);
        MakeControl(hwnd, L"Button", L"清空", ID_BTN_CLEAR,
                    394, 460, 78, 32, BS_PUSHBUTTON);
        MakeControl(hwnd, L"Button", L"恢复默认", ID_BTN_DEFAULT,
                    478, 460, 88, 32, BS_PUSHBUTTON);
        MakeControl(hwnd, L"Button", L"保存", ID_BTN_SAVE,
                    572, 460, 78, 32, BS_DEFPUSHBUTTON);
        MakeControl(hwnd, L"Button", L"取消", ID_BTN_CANCEL,
                    656, 460, 78, 32, BS_PUSHBUTTON);

        MakeControl(hwnd, L"Static", L"画笔大小", 0, 226, 506, 66, 20, 0);
        ctx->brushEdit = MakeControl(hwnd, L"Edit", L"6", ID_BRUSH_EDIT,
                                     294, 503, 50, 24, WS_BORDER | ES_AUTOHSCROLL | ES_NUMBER);
        ctx->hotspotLabel = MakeControl(hwnd, L"Static", L"", 0,
                                        356, 506, 300, 20, 0);
        UpdateHotspotLabel(ctx);
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp);
        int code = HIWORD(wp);
        if (id == ID_STATE_LIST && code == LBN_SELCHANGE) {
            int sel = (int)SendMessageW(ctx->list, LB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < CursorStateCount) ctx->current = sel;
            UpdateHotspotLabel(ctx);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        switch (id) {
        case ID_BTN_IMPORT:
            OnImport(ctx);
            return 0;
        case ID_BTN_DRAW:
            ctx->drawMode = !ctx->drawMode;
            ctx->eraseMode = false;
            RefreshButtons(ctx);
            return 0;
        case ID_BTN_ERASE:
            ctx->eraseMode = !ctx->eraseMode;
            ctx->drawMode = false;
            RefreshButtons(ctx);
            return 0;
        case ID_BTN_CLEAR: {
            StateEdit& edit = ctx->edits[ctx->current];
            edit.image = CursorImage();
            edit.image.width = 32;
            edit.image.height = 32;
            edit.image.pixels.assign((size_t)32 * 32, 0u);
            edit.custom = true;
            edit.dirty = true;
            UpdateHotspotLabel(ctx);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case ID_BTN_DEFAULT: {
            StateEdit& edit = ctx->edits[ctx->current];
            edit.image = ctx->builtin.images[ctx->current];
            edit.custom = false;
            edit.dirty = true;
            UpdateHotspotLabel(ctx);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case ID_BTN_SAVE:
            OnSave(ctx);
            return 0;
        case ID_BTN_CANCEL:
            DestroyWindow(hwnd);
            return 0;
        default:
            break;
        }
        break;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        PaintPreview(ctx, hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        int mx = LOWORD(lp), my = HIWORD(lp);
        const CursorImage& img = ctx->edits[ctx->current].image;
        PreviewLayout l = GetPreviewLayout(img);
        int ix = 0, iy = 0;
        if (PreviewToImage(img, l, mx, my, ix, iy)) {
            StateEdit& edit = ctx->edits[ctx->current];
            edit.image.hotspotX = ix;
            edit.image.hotspotY = iy;
            edit.custom = true;
            edit.dirty = true;
            if (ctx->drawMode || ctx->eraseMode) {
                wchar_t buf[32];
                GetWindowTextW(ctx->brushEdit, buf, std::size(buf));
                ctx->brush = std::clamp(_wtoi(buf), 1, 64);
                PaintBrush(edit.image, ix, iy, ctx->brush, ctx->eraseMode, RGB(0, 0, 0));
                ctx->dragging = true;
                SetCapture(hwnd);
            }
            UpdateHotspotLabel(ctx);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSEMOVE:
        if (ctx->dragging && (wp & MK_LBUTTON)) {
            int mx = LOWORD(lp), my = HIWORD(lp);
            CursorImage& img = ctx->edits[ctx->current].image;
            PreviewLayout l = GetPreviewLayout(img);
            int ix = 0, iy = 0;
            if (PreviewToImage(img, l, mx, my, ix, iy)) {
                PaintBrush(img, ix, iy, ctx->brush, ctx->eraseMode, RGB(0, 0, 0));
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        return 0;
    case WM_LBUTTONUP:
        if (ctx->dragging) {
            ctx->dragging = false;
            ReleaseCapture();
        }
        return 0;
    case WM_DESTROY:
        if (ctx) {
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            delete ctx;
        }
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

int RunCursorEditor() {
    EnsureGdiplus();
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = EditorProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"MultiCursorStyleEditor";
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"无法注册编辑器窗口类。", L"MultiCursor", MB_OK | MB_ICONERROR);
        return 0;
    }

    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE,
                                L"MultiCursorStyleEditor",
                                L"MultiCursor - 光标样式编辑器",
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                CW_USEDEFAULT, CW_USEDEFAULT, 760, 570,
                                nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hwnd) {
        MessageBoxW(nullptr, L"无法创建编辑器窗口。", L"MultiCursor", MB_OK | MB_ICONERROR);
        return 0;
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    g_editorResult = 0;
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return g_editorResult;
}

} // namespace mc
