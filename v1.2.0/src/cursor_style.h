// cursor_style.h - cursor states, custom images and style storage
#pragma once

#include "common.h"
#include "cursor_image.h"

#include <array>

namespace mc {

enum class CursorState {
    Normal = 0,
    Text,
    Wait,
    AppStarting,
    Cross,
    Help,
    Hand,
    No,
    SizeNS,
    SizeWE,
    SizeNWSE,
    SizeNESW,
    SizeAll,
    Up,
    Pin,
    Person,
    Count
};

constexpr int CursorStateCount = (int)CursorState::Count;

const wchar_t* CursorStateName(CursorState state);
const wchar_t* CursorStateIniKey(CursorState state);
UINT CursorStateSystemId(CursorState state);

struct CursorStyleSet {
    std::array<CursorImage, CursorStateCount> images;
    std::array<bool, CursorStateCount> custom{};
};

// Path to the style database next to the executable.
std::wstring CursorStylesIniPath();
std::wstring CursorStylesDir();

// Initialize GDI+ once (safe to call repeatedly).
void EnsureGdiplus();

// Load the real system cursor images. Call before hiding the system cursors.
CursorStyleSet LoadBuiltinStyles();

// Load Cursors.ini; missing/invalid entries fall back to builtin images.
CursorStyleSet LoadCustomStyles(const CursorStyleSet& builtin);

// Load a PNG/JPG/GIF/BMP file into a premultiplied BGRA image.
bool LoadImageFile(const std::wstring& path, CursorImage& img);

// Save a single state as PNG and write it into Cursors.ini.
bool SaveCursorStyle(CursorState state, const CursorImage& img);

// Remove a custom override and go back to the builtin image.
void DeleteCursorStyle(CursorState state);

// Add a small colored badge so different mice remain distinguishable.
CursorImage WithMouseBadge(const CursorImage& src, COLORREF color);

} // namespace mc
