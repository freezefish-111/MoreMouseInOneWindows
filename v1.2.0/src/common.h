// common.h - shared helpers and constants for MultiCursor
#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef OEMRESOURCE
#define OEMRESOURCE  // exposes OCR_* system cursor ids in winuser.h
#endif

#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <cstdint>
#include <cmath>

namespace mc {

constexpr const wchar_t* kAppName        = L"MultiCursor";
constexpr const wchar_t* kAppTitle       = L"MultiCursor - 多鼠标独立光标";
constexpr const wchar_t* kMutexName      = L"Local\\MultiCursor_SingleInstance_v1";
constexpr const wchar_t* kOverlayClass   = L"MultiCursorOverlayWindow";
constexpr const wchar_t* kHostClass      = L"MultiCursorHostWindow";

constexpr ULONG_PTR kInjectedTag = 0x4D435552534F52ULL;  // "MCURSOR" marker for our SendInput events
constexpr UINT WM_TRAYICON       = WM_APP + 1;
constexpr UINT WM_APP_TRAY_RECREATED = WM_APP + 2;

constexpr int HOTKEY_EXIT   = 0x4D43;  // Ctrl+Alt+Q
constexpr int HOTKEY_TOGGLE = 0x4D44;  // Ctrl+Alt+M
constexpr int HOTKEY_GAME   = 0x4D45;  // Ctrl+Alt+G

constexpr int MENU_INFO         = 1001;
constexpr int MENU_MODE_AUTO    = 1002;
constexpr int MENU_MODE_ON      = 1003;
constexpr int MENU_MODE_OFF     = 1004;
constexpr int MENU_SHOW_DEVICES = 1005;
constexpr int MENU_OPEN_LOG     = 1006;
constexpr int MENU_EXIT         = 1007;
constexpr int MENU_EDIT_STYLES  = 1008;
constexpr int MENU_GAME_AUTO    = 1009;
constexpr int MENU_GAME_ON      = 1010;
constexpr int MENU_GAME_OFF     = 1011;
constexpr int MENU_UNBIND_ALL   = 1012;
constexpr int MENU_BIND_BASE    = 2000;  // + index * 2 (bind), + index * 2 + 1 (unbind)

struct PointD {
    double x = 0.0;
    double y = 0.0;
};

struct VirtualScreen {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

struct AppConfig {
    double sensitivity = 0.80;   // raw mouse counts -> pixels multiplier
    double acceleration = 0.90;  // extra gain at high speed
    std::wstring mode = L"auto";     // auto | on | off
    std::wstring gameMode = L"auto"; // auto | on | off (fullscreen relative input)
};

VirtualScreen GetVirtualScreen();
POINT ClampScreenPoint(int x, int y);
PointD ClampScreenPointD(double x, double y);

std::wstring GetExePath();
std::wstring GetExeDir();
std::wstring GetIniPath();
std::wstring GetLogPath();
AppConfig LoadConfig();

void LogInit();
void Log(const wchar_t* fmt, ...);
void LogLine(const std::wstring& text);

std::string WideToUtf8(const std::wstring& s);
std::wstring Utf8ToWide(const std::string& s);

} // namespace mc
