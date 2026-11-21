// common.cpp - logging, paths, geometry, configuration
#include "common.h"

#include <algorithm>
#include <mutex>
#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <cwctype>
#include <iterator>

namespace mc {

VirtualScreen GetVirtualScreen() {
    VirtualScreen vs;
    vs.x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    vs.y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    vs.w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    vs.h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (vs.w <= 0) vs.w = GetSystemMetrics(SM_CXSCREEN);
    if (vs.h <= 0) vs.h = GetSystemMetrics(SM_CYSCREEN);
    if (vs.w <= 0) vs.w = 1024;
    if (vs.h <= 0) vs.h = 768;
    return vs;
}

POINT ClampScreenPoint(int x, int y) {
    VirtualScreen vs = GetVirtualScreen();
    POINT p;
    p.x = std::clamp(x, vs.x, vs.x + vs.w - 1);
    p.y = std::clamp(y, vs.y, vs.y + vs.h - 1);
    return p;
}

PointD ClampScreenPointD(double x, double y) {
    VirtualScreen vs = GetVirtualScreen();
    PointD p;
    p.x = std::clamp(x, (double)vs.x, (double)(vs.x + vs.w - 1));
    p.y = std::clamp(y, (double)vs.y, (double)(vs.y + vs.h - 1));
    return p;
}

std::wstring GetExePath() {
    std::wstring path;
    path.resize(32768);
    DWORD n = GetModuleFileNameW(nullptr, path.data(), (DWORD)path.size());
    if (n == 0) return L"MultiCursor.exe";
    path.resize(n);
    return path;
}

std::wstring GetExeDir() {
    std::wstring path = GetExePath();
    size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return L".";
    return path.substr(0, pos);
}

std::wstring GetIniPath() {
    return GetExeDir() + L"\\MultiCursor.ini";
}

static std::wstring GetLogDir() {
    wchar_t localAppData[MAX_PATH] = {};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        std::wstring dir = std::wstring(localAppData) + L"\\MultiCursor";
        CreateDirectoryW(dir.c_str(), nullptr);
        return dir;
    }
    return GetExeDir();
}

std::wstring GetLogPath() {
    return GetLogDir() + L"\\multicursor.log";
}

static double ReadDouble(const wchar_t* section, const wchar_t* key, double def,
                         double lo, double hi) {
    wchar_t buf[64] = {};
    GetPrivateProfileStringW(section, key, L"", buf, (DWORD)std::size(buf), GetIniPath().c_str());
    if (!buf[0]) return def;
    wchar_t* end = nullptr;
    double v = wcstod(buf, &end);
    if (end == buf || !std::isfinite(v)) return def;
    return std::clamp(v, lo, hi);
}

AppConfig LoadConfig() {
    AppConfig cfg;
    cfg.sensitivity = ReadDouble(L"MultiCursor", L"sensitivity", cfg.sensitivity, 0.10, 5.0);
    cfg.acceleration = ReadDouble(L"MultiCursor", L"acceleration", cfg.acceleration, 0.0, 3.0);

    wchar_t mode[32] = {};
    GetPrivateProfileStringW(L"MultiCursor", L"mode", L"auto", mode, (DWORD)std::size(mode),
                             GetIniPath().c_str());
    for (wchar_t* p = mode; *p; ++p) *p = (wchar_t)towlower(*p);
    if (wcscmp(mode, L"on") == 0 || wcscmp(mode, L"off") == 0 || wcscmp(mode, L"auto") == 0) {
        cfg.mode = mode;
    }
    return cfg;
}

static std::mutex g_logMutex;
static bool g_logReady = false;

void LogInit() {
    std::lock_guard<std::mutex> lock(g_logMutex);
    std::wstring path = GetLogPath();
    WIN32_FILE_ATTRIBUTE_DATA fad = {};
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) {
        ULARGE_INTEGER size;
        size.LowPart = fad.nFileSizeLow;
        size.HighPart = fad.nFileSizeHigh;
        if (size.QuadPart > 1024 * 1024) {
            std::wstring old = path + L".old";
            DeleteFileW(old.c_str());
            MoveFileW(path.c_str(), old.c_str());
        }
    }
    g_logReady = true;
}

static void WriteLogLine(const std::wstring& line) {
    std::string utf8 = WideToUtf8(line) + "\r\n";
    HANDLE h = CreateFileW(GetLogPath().c_str(), FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(h, utf8.data(), (DWORD)utf8.size(), &written, nullptr);
    CloseHandle(h);
}

void LogLine(const std::wstring& text) {
    if (!g_logReady) LogInit();
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t prefix[64];
    swprintf(prefix, std::size(prefix), L"[%02u:%02u:%02u.%03u] ", st.wHour, st.wMinute,
             st.wSecond, st.wMilliseconds);
    std::wstring line = prefix + text;
    OutputDebugStringW((line + L"\n").c_str());
    std::lock_guard<std::mutex> lock(g_logMutex);
    WriteLogLine(line);
}

void Log(const wchar_t* fmt, ...) {
    wchar_t buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vswprintf(buf, std::size(buf), fmt, ap);
    va_end(ap);
    buf[std::size(buf) - 1] = 0;
    LogLine(buf);
}

std::string WideToUtf8(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0) return {};
    std::wstring out((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n);
    return out;
}

} // namespace mc
