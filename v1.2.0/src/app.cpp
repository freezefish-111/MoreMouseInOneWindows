// app.cpp - device tracking, per-mouse cursors, click replay, tray UI
#include "app.h"

#include <wtsapi32.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace mc {

namespace {

App* g_app = nullptr;

struct ButtonDef {
    USHORT downFlag;
    USHORT upFlag;
    int index;
    DWORD downEvent;
    DWORD upEvent;
    DWORD data;
};

const ButtonDef kButtonDefs[] = {
    { RI_MOUSE_LEFT_BUTTON_DOWN,   RI_MOUSE_LEFT_BUTTON_UP,   0,
      MOUSEEVENTF_LEFTDOWN,   MOUSEEVENTF_LEFTUP,   0 },
    { RI_MOUSE_RIGHT_BUTTON_DOWN,  RI_MOUSE_RIGHT_BUTTON_UP,  1,
      MOUSEEVENTF_RIGHTDOWN,  MOUSEEVENTF_RIGHTUP,  0 },
    { RI_MOUSE_MIDDLE_BUTTON_DOWN, RI_MOUSE_MIDDLE_BUTTON_UP, 2,
      MOUSEEVENTF_MIDDLEDOWN, MOUSEEVENTF_MIDDLEUP, 0 },
    { RI_MOUSE_BUTTON_4_DOWN,      RI_MOUSE_BUTTON_4_UP,      3,
      MOUSEEVENTF_XDOWN,      MOUSEEVENTF_XUP,      XBUTTON1 },
    { RI_MOUSE_BUTTON_5_DOWN,      RI_MOUSE_BUTTON_5_UP,      4,
      MOUSEEVENTF_XDOWN,      MOUSEEVENTF_XUP,      XBUTTON2 },
};

struct PhysicalButton {
    int vk;
    DWORD upEvent;
    DWORD data;
};

const PhysicalButton kPhysicalButtons[] = {
    { VK_LBUTTON,  MOUSEEVENTF_LEFTUP,   0 },
    { VK_RBUTTON,  MOUSEEVENTF_RIGHTUP,  0 },
    { VK_MBUTTON,  MOUSEEVENTF_MIDDLEUP, 0 },
    { VK_XBUTTON1, MOUSEEVENTF_XUP,      XBUTTON1 },
    { VK_XBUTTON2, MOUSEEVENTF_XUP,      XBUTTON2 },
};

const UINT kButtonDownMsgs[5] = {
    WM_LBUTTONDOWN, WM_RBUTTONDOWN, WM_MBUTTONDOWN, WM_XBUTTONDOWN, WM_XBUTTONDOWN
};
const UINT kButtonUpMsgs[5] = {
    WM_LBUTTONUP, WM_RBUTTONUP, WM_MBUTTONUP, WM_XBUTTONUP, WM_XBUTTONUP
};
const DWORD kButtonMK[5] = {
    MK_LBUTTON, MK_RBUTTON, MK_MBUTTON, MK_XBUTTON1, MK_XBUTTON2
};

std::vector<HWND>* g_windowList = nullptr;

bool IsInternalWindowClass(HWND hwnd) {
    wchar_t cls[64] = {};
    GetClassNameW(hwnd, cls, (int)std::size(cls));
    return wcscmp(cls, kHostClass) == 0 || wcscmp(cls, kOverlayClass) == 0 ||
           wcscmp(cls, L"MultiCursorStyleEditor") == 0;
}

BOOL CALLBACK EnumWindowProc(HWND hwnd, LPARAM) {
    if (!IsWindowVisible(hwnd) || !GetWindowTextLengthW(hwnd)) return TRUE;
    if (IsInternalWindowClass(hwnd)) return TRUE;
    if (g_windowList && g_windowList->size() < 40) g_windowList->push_back(hwnd);
    return TRUE;
}

std::wstring GetRawDeviceName(HANDLE hDevice) {
    UINT size = 0;
    if (GetRawInputDeviceInfoW(hDevice, RIDI_DEVICENAME, nullptr, &size) != 0 || size == 0) {
        return L"(unknown)";
    }
    std::wstring name((size_t)size, L'\0');
    UINT got = GetRawInputDeviceInfoW(hDevice, RIDI_DEVICENAME, name.data(), &size);
    if (got == (UINT)-1) return L"(unknown)";
    if (name.size() > got) name.resize(got);
    while (!name.empty() && name.back() == L'\0') name.pop_back();
    return name.empty() ? L"(unknown)" : name;
}

bool RawDeviceIsMouse(HANDLE hDevice) {
    RID_DEVICE_INFO info = {};
    info.cbSize = sizeof(info);
    UINT size = sizeof(info);
    UINT got = GetRawInputDeviceInfoW(hDevice, RIDI_DEVICEINFO, &info, &size);
    if (got == (UINT)-1) return false;
    return info.dwType == RIM_TYPEMOUSE;
}

} // namespace

// ---------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------

App::~App() {
    Shutdown();
}

bool App::Init(HINSTANCE hInst, const std::wstring& forcedMode, bool preview) {
    hInst_ = hInst;
    preview_ = preview;
    config_ = LoadConfig();
    QueryPerformanceFrequency((LARGE_INTEGER*)&qpcFreq_);
    if (qpcFreq_ <= 0) qpcFreq_ = 1;

    cursorSize_ = GetSystemMetrics(SM_CXCURSOR);
    if (cursorSize_ < 16) cursorSize_ = 32;
    if (cursorSize_ > 128) cursorSize_ = 128;

    if (!forcedMode.empty()) {
        if (forcedMode == L"on") mode_ = Mode::On;
        else if (forcedMode == L"off") mode_ = Mode::Off;
        else mode_ = Mode::Auto;
    } else if (config_.mode == L"on") {
        mode_ = Mode::On;
    } else if (config_.mode == L"off") {
        mode_ = Mode::Off;
    } else {
        mode_ = Mode::Auto;
    }

    if (config_.gameMode == L"on") gameMode_ = GameMode::On;
    else if (config_.gameMode == L"off") gameMode_ = GameMode::Off;
    else gameMode_ = GameMode::Auto;

    VirtualScreen vs = GetVirtualScreen();
    LogInit();
    Log(L"==== MultiCursor starting ====");
    Log(L"exe=%ls", GetExePath().c_str());
    Log(L"virtual screen=(%d,%d) %dx%d, cursor size=%d, mode=%ls, sensitivity=%.2f, acceleration=%.2f",
        vs.x, vs.y, vs.w, vs.h, cursorSize_, mode_ == Mode::On ? L"on" :
        mode_ == Mode::Off ? L"off" : L"auto", config_.sensitivity, config_.acceleration);
    Log(L"game mode setting=%ls", config_.gameMode.c_str());

    // Snapshot the real system cursors before any mode can blank them.
    LoadInitialStyles();

    if (!CreateHostWindow()) return false;

    EnumerateDevices();

    if (preview_) {
        RunPreview();
        initializing_ = false;
        return true;
    }

    if (!RegisterRawInput()) {
        MessageBoxW(nullptr, L"无法注册鼠标原始输入（Raw Input）。\n程序无法运行。",
                    kAppTitle, MB_OK | MB_ICONERROR);
        return false;
    }

    if (WTSRegisterSessionNotification(hwnd_, NOTIFY_FOR_THIS_SESSION)) {
        sessionNotify_ = true;
        Log(L"Session notifications registered");
    } else {
        Log(L"WTSRegisterSessionNotification failed, err=%lu", GetLastError());
    }

    AddTrayIcon();
    RegisterHotkeys();

    gameCheckTimer_ = SetTimer(hwnd_, 3, 500, nullptr);
    UpdateGameMode(L"init");

    initializing_ = false;
    UpdateMode(L"startup");
    UpdateTrayTip();

    Log(L"Initialization complete: %llu mouse device(s), multi=%d",
        (unsigned long long)mice_.size(), (int)multi_);
    return true;
}

int App::Run() {
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}

void App::Shutdown() {
    if (shuttingDown_) return;
    shuttingDown_ = true;
    Log(L"Shutdown requested");

    if (gameCheckTimer_) {
        KillTimer(hwnd_, gameCheckTimer_);
        gameCheckTimer_ = 0;
    }
    if (multi_) ExitMulti(L"shutdown");
    ReleaseOwnedButtons(nullptr, true);
    UninstallHook();
    if (sessionNotify_ && hwnd_) {
        WTSUnRegisterSessionNotification(hwnd_);
        sessionNotify_ = false;
    }
    RemoveTrayIcon();

    for (auto& ov : previewOverlays_) ov->Destroy();
    previewOverlays_.clear();

    RestoreSystemCursor();
    // icon_ may be the shared IDI_APPLICATION fallback, so leave it to process exit.
    icon_ = nullptr;
    Log(L"==== MultiCursor stopped ====");
}

// ---------------------------------------------------------------------------
// Host window and tray
// ---------------------------------------------------------------------------

bool App::CreateHostWindow() {
    CursorImage iconImg = MakeCursorImage(32, CursorColor(0));
    icon_ = CreateCursorIcon(iconImg, 32);
    if (!icon_) icon_ = LoadIconW(nullptr, IDI_APPLICATION);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProcStatic;
    wc.hInstance = hInst_;
    wc.hIcon = icon_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kHostClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        Log(L"RegisterClassExW(host) failed, err=%lu", GetLastError());
        return false;
    }

    g_app = this;
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, kHostClass, kAppTitle, WS_POPUP,
                            0, 0, 0, 0, nullptr, nullptr, hInst_, nullptr);
    if (!hwnd_) {
        Log(L"CreateWindowExW(host) failed, err=%lu", GetLastError());
        return false;
    }
    taskbarCreatedMsg_ = RegisterWindowMessageW(L"TaskbarCreated");
    return true;
}

void App::RegisterHotkeys() {
    if (!RegisterHotKey(hwnd_, HOTKEY_EXIT, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'Q')) {
        Log(L"RegisterHotKey(Ctrl+Alt+Q) failed, err=%lu", GetLastError());
    }
    if (!RegisterHotKey(hwnd_, HOTKEY_TOGGLE, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'M')) {
        Log(L"RegisterHotKey(Ctrl+Alt+M) failed, err=%lu", GetLastError());
    }
    if (!RegisterHotKey(hwnd_, HOTKEY_GAME, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'G')) {
        Log(L"RegisterHotKey(Ctrl+Alt+G) failed, err=%lu", GetLastError());
    }
}

std::wstring App::StatusText() const {
    const wchar_t* gm = gameModeActive_ ? L"游戏模式" :
                        gameMode_ == GameMode::On ? L"游戏模式(强制)" :
                        gameMode_ == GameMode::Off ? L"游戏模式(关)" : L"游戏模式(自动)";
    wchar_t buf[256];
    swprintf(buf, std::size(buf), L"已检测到 %llu 个鼠标 · %ls · %ls",
             (unsigned long long)mice_.size(),
             multi_ ? L"多光标模式" : L"单光标模式", gm);
    return buf;
}

void App::AddTrayIcon() {
    if (trayAdded_ || !hwnd_) return;
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd_;
    nid.uID = 1;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = icon_;
    std::wstring tip = L"MultiCursor：" + (multi_ ? std::wstring(L"多光标") : std::wstring(L"单光标"))
                     + L" · " + std::to_wstring(mice_.size()) + L" 个鼠标";
    lstrcpynW(nid.szTip, tip.c_str(), (int)std::size(nid.szTip));
    if (Shell_NotifyIconW(NIM_ADD, &nid)) {
        trayAdded_ = true;
    } else {
        Log(L"Shell_NotifyIcon(NIM_ADD) failed, err=%lu", GetLastError());
    }
}

void App::RemoveTrayIcon() {
    if (!trayAdded_) return;
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd_;
    nid.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    trayAdded_ = false;
}

void App::UpdateTrayTip() {
    if (!trayAdded_ || shuttingDown_) return;
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd_;
    nid.uID = 1;
    nid.uFlags = NIF_TIP;
    std::wstring tip = L"MultiCursor：" + (multi_ ? std::wstring(L"多光标") : std::wstring(L"单光标"))
                     + L" · " + std::to_wstring(mice_.size()) + L" 个鼠标"
                     + (gameModeActive_ ? L" · 游戏模式" : L"") + L" · Ctrl+Alt+Q 退出";
    lstrcpynW(nid.szTip, tip.c_str(), (int)std::size(nid.szTip));
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void App::ShowBalloon(const std::wstring& title, const std::wstring& text) {
    if (!trayAdded_ || shuttingDown_) return;
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd_;
    nid.uID = 1;
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = NIIF_INFO;
    nid.uTimeout = 6000;
    lstrcpynW(nid.szInfoTitle, title.c_str(), (int)std::size(nid.szInfoTitle));
    lstrcpynW(nid.szInfo, text.c_str(), (int)std::size(nid.szInfo));
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void App::OnTrayMessage(LPARAM lp) {
    switch (LOWORD(lp)) {
    case WM_RBUTTONUP:
    case WM_CONTEXTMENU:
    case WM_LBUTTONDBLCLK:
        ShowTrayMenu();
        break;
    default:
        break;
    }
}

void App::ShowTrayMenu() {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    AppendMenuW(menu, MF_STRING | MF_GRAYED, MENU_INFO, StatusText().c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, MENU_MODE_AUTO, L"自动模式（2 个及以上鼠标时启用）");
    AppendMenuW(menu, MF_STRING, MENU_MODE_ON, L"始终启用多光标");
    AppendMenuW(menu, MF_STRING, MENU_MODE_OFF, L"关闭多光标（普通鼠标）");
    int selected = mode_ == Mode::On ? MENU_MODE_ON : mode_ == Mode::Off ? MENU_MODE_OFF : MENU_MODE_AUTO;
    CheckMenuRadioItem(menu, MENU_MODE_AUTO, MENU_MODE_OFF, selected, MF_BYCOMMAND);
    AppendMenuW(menu, MF_STRING, MENU_GAME_AUTO, L"全屏游戏模式：自动检测");
    AppendMenuW(menu, MF_STRING, MENU_GAME_ON, L"全屏游戏模式：始终开启（相对输入）");
    AppendMenuW(menu, MF_STRING, MENU_GAME_OFF, L"全屏游戏模式：关闭");
    int gameSelected = gameMode_ == GameMode::On ? MENU_GAME_ON :
                       gameMode_ == GameMode::Off ? MENU_GAME_OFF : MENU_GAME_AUTO;
    CheckMenuRadioItem(menu, MENU_GAME_AUTO, MENU_GAME_OFF, gameSelected, MF_BYCOMMAND);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, MENU_SHOW_DEVICES, L"显示设备信息...");
    AppendMenuW(menu, MF_STRING, MENU_OPEN_LOG, L"打开日志文件");
    AppendMenuW(menu, MF_STRING, MENU_EDIT_STYLES, L"编辑光标样式...");

    // Per-mouse window binding: enumerate visible titled windows.
    std::vector<HWND> windows;
    g_windowList = &windows;
    EnumWindows(EnumWindowProc, 0);
    g_windowList = nullptr;

    HMENU bindMenu = CreatePopupMenu();
    if (bindMenu) {
        for (size_t i = 0; i < mice_.size(); ++i) {
            HMENU mouseMenu = CreatePopupMenu();
            if (!mouseMenu) continue;
            MouseDevice& m = *mice_[i];
            for (size_t j = 0; j < windows.size() && j < 99; ++j) {
                wchar_t title[256] = {};
                GetWindowTextW(windows[j], title, (int)std::size(title));
                if (!title[0]) lstrcpynW(title, L"(无标题窗口)", (int)std::size(title));
                wchar_t label[320];
                swprintf(label, std::size(label), L"%llu. %ls%s",
                         (unsigned long long)(j + 1), title,
                         m.boundWindow == windows[j] ? L"  [当前]" : L"");
                UINT id = MENU_BIND_BASE + (UINT)(i * 100 + j);
                AppendMenuW(mouseMenu, MF_STRING, id, label);
            }
            if (!windows.empty()) AppendMenuW(mouseMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(mouseMenu, MF_STRING,
                        MENU_BIND_BASE + (UINT)(i * 100 + 99),
                        m.boundWindow ? L"解除绑定" : L"未绑定任何窗口");
            wchar_t mouseLabel[128];
            swprintf(mouseLabel, std::size(mouseLabel), L"鼠标 #%d → 选择窗口",
                     m.colorIndex + 1);
            AppendMenuW(bindMenu, MF_POPUP, (UINT_PTR)mouseMenu, mouseLabel);
        }
        AppendMenuW(bindMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(bindMenu, MF_STRING, MENU_UNBIND_ALL, L"解除所有绑定");
        AppendMenuW(menu, MF_POPUP, (UINT_PTR)bindMenu,
                    L"窗口绑定（不同鼠标控制不同窗口）...");
    }

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, MENU_EXIT, L"退出并恢复系统光标");

    POINT pt;
    GetCursorPos(&pt);
    lastForeground_ = GetForegroundWindow();
    SetForegroundWindow(hwnd_);
    UINT cmd = (UINT)TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
                                    pt.x, pt.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    PostMessageW(hwnd_, WM_NULL, 0, 0);

    if (cmd >= MENU_BIND_BASE &&
        cmd < MENU_BIND_BASE + (UINT)mice_.size() * 100) {
        UINT off = cmd - MENU_BIND_BASE;
        size_t mouseIndex = off / 100;
        size_t windowIndex = off % 100;
        if (mouseIndex < mice_.size()) {
            MouseDevice& m = *mice_[mouseIndex];
            if (windowIndex == 99) {
                UnbindMouse(m);
            } else if (windowIndex < windows.size()) {
                BindMouseToWindow(m, windows[windowIndex]);
            }
        }
        return;
    }

    switch (cmd) {
    case MENU_MODE_AUTO:
        mode_ = Mode::Auto;
        UpdateMode(L"menu: auto");
        UpdateTrayTip();
        break;
    case MENU_MODE_ON:
        mode_ = Mode::On;
        UpdateMode(L"menu: on");
        UpdateTrayTip();
        break;
    case MENU_MODE_OFF:
        mode_ = Mode::Off;
        UpdateMode(L"menu: off");
        UpdateTrayTip();
        break;
    case MENU_GAME_AUTO:
        gameMode_ = GameMode::Auto;
        UpdateGameMode(L"menu: game auto");
        break;
    case MENU_GAME_ON:
        gameMode_ = GameMode::On;
        UpdateGameMode(L"menu: game on");
        break;
    case MENU_GAME_OFF:
        gameMode_ = GameMode::Off;
        UpdateGameMode(L"menu: game off");
        break;
    case MENU_SHOW_DEVICES:
        ShowDeviceInfo();
        break;
    case MENU_OPEN_LOG: {
        HINSTANCE r = ShellExecuteW(hwnd_, L"open", GetLogPath().c_str(), nullptr, nullptr,
                                    SW_SHOWNORMAL);
        if ((INT_PTR)r <= 32) {
            MessageBoxW(hwnd_, L"无法打开日志文件。", kAppTitle, MB_OK | MB_ICONWARNING);
        }
        break;
    }
    case MENU_EDIT_STYLES:
        EditStyles();
        break;
    case MENU_UNBIND_ALL:
        UnbindAllWindows();
        break;
    case MENU_EXIT:
        PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        break;
    default:
        break;
    }
}

void App::ShowDeviceInfo() {
    std::wstring text = L"检测到 " + std::to_wstring(mice_.size()) + L" 个鼠标设备。\r\n";
    text += gameModeActive_ ? L"当前：全屏游戏模式（相对输入）\r\n\r\n"
                            : L"当前：普通多光标模式\r\n\r\n";
    if (mice_.empty()) {
        text += L"（没有检测到鼠标）\r\n";
    }
    for (size_t i = 0; i < mice_.size(); ++i) {
        const MouseDevice& m = *mice_[i];
        wchar_t line[2048];
        swprintf(line, std::size(line),
                 L"#%llu  %ls\r\n位置: (%d, %d)\r\n设备: %ls\r\n绑定窗口: %ls\r\n\r\n",
                 (unsigned long long)(i + 1), m.displayName.c_str(),
                 (int)std::llround(m.x), (int)std::llround(m.y), m.name.c_str(),
                 m.boundWindow ? m.boundTitle.c_str() : L"(未绑定)");
        text += line;
    }
    text += L"快捷键: Ctrl+Alt+Q 退出，Ctrl+Alt+M 切换多光标，Ctrl+Alt+G 切换游戏模式。";
    MessageBoxW(hwnd_, text.c_str(), L"MultiCursor 设备信息", MB_OK | MB_ICONINFORMATION);
}

// ---------------------------------------------------------------------------
// Cursor styles
// ---------------------------------------------------------------------------

void App::LoadInitialStyles() {
    EnsureGdiplus();
    // Must run before HideSystemCursor(): LoadBuiltinStyles reads the real
    // system cursor bitmaps, which are blanked once multi mode is entered.
    builtinStyles_ = LoadBuiltinStyles();
    styles_ = LoadCustomStyles(builtinStyles_);
    CaptureSystemCursorHandles();
    RecordStyleFileTime();
}

void App::CaptureSystemCursorHandles() {
    for (int i = 0; i < CursorStateCount; ++i) {
        HCURSOR h = (HCURSOR)LoadCursorW(nullptr,
            MAKEINTRESOURCE(CursorStateSystemId((CursorState)i)));
        systemCursorHandles_[i] = h;
    }
    Log(L"Captured %d system cursor handles for state detection", CursorStateCount);
}

void App::RecordStyleFileTime() {
    WIN32_FILE_ATTRIBUTE_DATA attr = {};
    if (GetFileAttributesExW(CursorStylesIniPath().c_str(), GetFileExInfoStandard, &attr)) {
        hasStyleFile_ = true;
        styleFileTime_ = attr.ftLastWriteTime;
    } else {
        hasStyleFile_ = false;
        styleFileTime_ = {};
    }
}

void App::CheckStylesFile() {
    WIN32_FILE_ATTRIBUTE_DATA attr = {};
    BOOL ok = GetFileAttributesExW(CursorStylesIniPath().c_str(), GetFileExInfoStandard, &attr);
    if (!ok) {
        if (hasStyleFile_) {
            hasStyleFile_ = false;
            styleFileTime_ = {};
            ReloadStyles();
        }
        return;
    }
    if (!hasStyleFile_) {
        hasStyleFile_ = true;
        styleFileTime_ = attr.ftLastWriteTime;
        return;
    }
    if (CompareFileTime(&styleFileTime_, &attr.ftLastWriteTime) != 0) {
        styleFileTime_ = attr.ftLastWriteTime;
        Log(L"Cursor styles file changed; reloading");
        ReloadStyles();
    }
}

void App::ReloadStyles() {
    // Keep the builtin snapshots taken at startup; only user overrides change.
    styles_ = LoadCustomStyles(builtinStyles_);
    RecordStyleFileTime();
    for (auto& m : mice_) m->stateValid = false;
    UpdateCursorStates();
    ShowBalloon(L"光标样式已更新", L"已重新加载 Cursors.ini 中的自定义样式。");
}

CursorState App::DetectCursorState() const {
    CURSORINFO ci = {};
    ci.cbSize = sizeof(ci);
    if (!GetCursorInfo(&ci) || !ci.hCursor) return CursorState::Normal;
    for (int i = 0; i < CursorStateCount; ++i) {
        if (systemCursorHandles_[i] && systemCursorHandles_[i] == ci.hCursor) {
            return (CursorState)i;
        }
    }
    return CursorState::Normal;
}

void App::ApplyCursorImage(MouseDevice& m) {
    if (!m.overlay.valid()) return;
    CursorImage img = styles_.images[(int)m.state];
    if (img.pixels.empty()) img = builtinStyles_.images[(int)m.state];
    if (img.pixels.empty()) return;
    img = WithMouseBadge(img, CursorColor(m.colorIndex));
    if (!m.overlay.UpdateImage(img)) {
        Log(L"Failed to update overlay image for cursor #%d", m.colorIndex + 1);
    }
}

void App::UpdateCursorStates() {
    if (!multi_ || !hwnd_ || shuttingDown_) return;
    CheckStylesFile();
    ValidateWindowBindings();

    // In game mode the system cursor shape no longer follows any virtual mouse.
    CursorState detected = gameModeActive_ ? CursorState::Normal : DetectCursorState();
    for (auto& m : mice_) {
        // A window-bound mouse never moves the system cursor, so it stays normal.
        bool followsSystem = !gameModeActive_ && !m->boundWindow &&
                             activeCursorHandle_ && m->hDevice == activeCursorHandle_;
        CursorState next = followsSystem ? detected : CursorState::Normal;
        if (!m->stateValid || m->state != next) {
            m->state = next;
            m->stateValid = true;
            ApplyCursorImage(*m);
        }
    }
}

void App::EditStyles() {
    std::wstring exe = GetExePath();
    HINSTANCE r = ShellExecuteW(hwnd_, L"open", exe.c_str(), L"--create", nullptr, SW_SHOWNORMAL);
    if ((INT_PTR)r <= 32) {
        MessageBoxW(hwnd_, L"无法打开光标样式编辑器。", kAppTitle, MB_OK | MB_ICONWARNING);
    }
}

// ---------------------------------------------------------------------------
// Game mode and per-mouse window binding
// ---------------------------------------------------------------------------

bool App::IsOurWindow(HWND h) const {
    if (!h) return false;
    if (h == hwnd_) return true;
    for (const auto& ov : previewOverlays_) {
        if (ov->hwnd() == h) return true;
    }
    for (const auto& m : mice_) {
        if (m->overlay.hwnd() == h) return true;
    }
    wchar_t cls[64] = {};
    GetClassNameW(h, cls, (int)std::size(cls));
    return wcscmp(cls, kHostClass) == 0 || wcscmp(cls, kOverlayClass) == 0 ||
           wcscmp(cls, L"MultiCursorStyleEditor") == 0;
}

bool App::ForegroundIsFullscreen() const {
    HWND fg = GetForegroundWindow();
    if (!fg || IsOurWindow(fg) || !IsWindowVisible(fg)) return false;
    LONG style = GetWindowLongW(fg, GWL_STYLE);
    if (style & WS_CAPTION) return false;

    RECT wr = {};
    if (!GetWindowRect(fg, &wr)) return false;
    HMONITOR mon = MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(mon, &mi)) return false;

    const RECT& mr = mi.rcMonitor;
    const int tol = 4;
    return wr.left <= mr.left + tol && wr.top <= mr.top + tol &&
           wr.right >= mr.right - tol && wr.bottom >= mr.bottom - tol;
}

void App::UpdateGameMode(const wchar_t* reason) {
    bool desired = false;
    if (gameMode_ == GameMode::On) desired = true;
    else if (gameMode_ == GameMode::Auto) desired = ForegroundIsFullscreen();
    if (desired == gameModeActive_) return;

    gameModeActive_ = desired;
    Log(L"Game mode %s (%ls)", desired ? L"ON" : L"OFF", reason);
    if (desired) {
        UpdateAllOverlays();
        ShowBalloon(L"全屏游戏模式已启用", L"使用相对鼠标移动，游戏可直接接收输入。");
    } else {
        if (multi_ && !mice_.empty()) MoveSystemCursorTo(*mice_[0]);
        ShowBalloon(L"全屏游戏模式已关闭", L"已恢复绝对光标定位。");
    }
    UpdateTrayTip();
}

void App::SendRelativeMotion(MouseDevice& m, double dx, double dy) {
    m.pendingDx += dx;
    m.pendingDy += dy;
    LONG ix = (LONG)m.pendingDx;
    LONG iy = (LONG)m.pendingDy;
    if (ix == 0 && iy == 0) return;
    m.pendingDx -= ix;
    m.pendingDy -= iy;

    INPUT in = {};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = MOUSEEVENTF_MOVE;
    in.mi.dx = ix;
    in.mi.dy = iy;
    in.mi.dwExtraInfo = kInjectedTag;
    SendInput(1, &in, sizeof(in));
}

void App::BindMouseToWindow(MouseDevice& m, HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd) || IsOurWindow(hwnd)) {
        ShowBalloon(L"绑定失败", L"当前前台窗口不可用，请先点选目标窗口。");
        return;
    }
    wchar_t title[256] = {};
    GetWindowTextW(hwnd, title, (int)std::size(title));
    m.boundWindow = hwnd;
    m.boundTitle = title[0] ? title : L"(无标题窗口)";
    Log(L"Mouse #%d bound to window %p: %ls", m.colorIndex + 1,
        (void*)hwnd, m.boundTitle.c_str());
    ShowBalloon(L"窗口已绑定", L"鼠标 #" + std::to_wstring(m.colorIndex + 1) +
                               L" → " + m.boundTitle);
    UpdateTrayTip();
}

void App::UnbindMouse(MouseDevice& m) {
    if (!m.boundWindow) return;
    Log(L"Mouse #%d unbound from window %ls", m.colorIndex + 1, m.boundTitle.c_str());
    m.boundWindow = nullptr;
    m.boundTitle.clear();
    UpdateTrayTip();
}

void App::UnbindAllWindows() {
    int count = 0;
    for (auto& m : mice_) {
        if (m->boundWindow) {
            m->boundWindow = nullptr;
            m->boundTitle.clear();
            ++count;
        }
    }
    Log(L"Unbound all windows (%d)", count);
    ShowBalloon(L"已解除所有窗口绑定", L"所有鼠标恢复全局输入模式。");
    UpdateTrayTip();
}

void App::ValidateWindowBindings() {
    for (auto& m : mice_) {
        if (m->boundWindow && !IsWindow(m->boundWindow)) {
            Log(L"Mouse #%d bound window was destroyed", m->colorIndex + 1);
            m->boundWindow = nullptr;
            m->boundTitle.clear();
        }
    }
}

POINT App::MouseClientPoint(const MouseDevice& m, HWND hwnd) const {
    POINT p = { (int)std::llround(m.x), (int)std::llround(m.y) };
    ScreenToClient(hwnd, &p);
    RECT rc = {};
    GetClientRect(hwnd, &rc);
    if (rc.right <= 0 || rc.bottom <= 0) return { 0, 0 };
    p.x = std::clamp(p.x, (LONG)0, (LONG)rc.right - 1);
    p.y = std::clamp(p.y, (LONG)0, (LONG)rc.bottom - 1);
    return p;
}

DWORD App::MouseKeyState(const MouseDevice& m) const {
    DWORD keys = 0;
    if (m.buttons[0]) keys |= MK_LBUTTON;
    if (m.buttons[1]) keys |= MK_RBUTTON;
    if (m.buttons[2]) keys |= MK_MBUTTON;
    if (m.buttons[3]) keys |= MK_XBUTTON1;
    if (m.buttons[4]) keys |= MK_XBUTTON2;
    return keys;
}

void App::PostWindowMove(MouseDevice& m) {
    HWND h = m.boundWindow;
    if (!h || !IsWindow(h)) return;
    POINT p = MouseClientPoint(m, h);
    PostMessageW(h, WM_MOUSEMOVE, (WPARAM)MouseKeyState(m), MAKELPARAM(p.x, p.y));
}

void App::PostButtonToWindow(MouseDevice& m, int index, bool down) {
    if (index < 0 || index >= (int)std::size(kButtonDefs)) return;
    HWND h = m.boundWindow;
    if (!h || !IsWindow(h)) return;

    POINT p = MouseClientPoint(m, h);
    DWORD keys = MouseKeyState(m) | kButtonMK[index];
    WPARAM wp = (index >= 3) ? MAKEWPARAM(keys, index == 3 ? XBUTTON1 : XBUTTON2)
                             : (WPARAM)keys;
    UINT msg = down ? kButtonDownMsgs[index] : kButtonUpMsgs[index];
    PostMessageW(h, msg, wp, MAKELPARAM(p.x, p.y));
}

void App::PostWheelToWindow(MouseDevice& m, DWORD delta, bool horizontal) {
    HWND h = m.boundWindow;
    if (!h || !IsWindow(h)) return;
    // WM_MOUSEWHEEL/WHEEL lParam uses screen coordinates.
    int sx = (int)std::llround(m.x);
    int sy = (int)std::llround(m.y);
    PostMessageW(h, horizontal ? WM_MOUSEHWHEEL : WM_MOUSEWHEEL,
                 MAKEWPARAM(0, (WORD)(SHORT)delta), MAKELPARAM(sx, sy));
}

void App::RouteButton(MouseDevice& m, int index, bool down) {
    if (m.boundWindow && IsWindow(m.boundWindow)) {
        PostButtonToWindow(m, index, down);
        return;
    }
    if (!gameModeActive_) MoveSystemCursorTo(m);
    const ButtonDef& b = kButtonDefs[index];
    SendMouseInput(down ? b.downEvent : b.upEvent, b.data);
}

void App::RouteWheel(MouseDevice& m, DWORD delta, bool horizontal) {
    if (m.boundWindow && IsWindow(m.boundWindow)) {
        PostWheelToWindow(m, delta, horizontal);
        return;
    }
    if (!gameModeActive_) MoveSystemCursorTo(m);
    SendMouseInput(horizontal ? MOUSEEVENTF_HWHEEL : MOUSEEVENTF_WHEEL, delta);
}

// ---------------------------------------------------------------------------
// Device tracking
// ---------------------------------------------------------------------------

bool App::RegisterRawInput() {
    RAWINPUTDEVICE rid = {};
    rid.usUsagePage = 0x01;
    rid.usUsage = 0x02;  // mouse
    rid.dwFlags = RIDEV_INPUTSINK | RIDEV_DEVNOTIFY;
    rid.hwndTarget = hwnd_;
    if (!RegisterRawInputDevices(&rid, 1, sizeof(rid))) {
        Log(L"RegisterRawInputDevices failed, err=%lu", GetLastError());
        return false;
    }
    rawRegistered_ = true;
    Log(L"Raw input registered (mouse, INPUTSINK | DEVNOTIFY)");
    return true;
}

void App::EnumerateDevices() {
    UINT count = 0;
    if (GetRawInputDeviceList(nullptr, &count, sizeof(RAWINPUTDEVICELIST)) != 0) {
        Log(L"GetRawInputDeviceList(size) failed, err=%lu", GetLastError());
        return;
    }
    if (!count) {
        Log(L"No raw input devices found");
        return;
    }
    std::vector<RAWINPUTDEVICELIST> list(count);
    UINT got = GetRawInputDeviceList(list.data(), &count, sizeof(RAWINPUTDEVICELIST));
    if (got == (UINT)-1) {
        Log(L"GetRawInputDeviceList failed, err=%lu", GetLastError());
        return;
    }
    list.resize(got);
    int mouseCount = 0;
    for (const auto& dev : list) {
        if (dev.dwType != RIM_TYPEMOUSE) continue;
        ++mouseCount;
        AddDevice(dev.hDevice);
    }
    Log(L"Enumerated %llu raw devices, %d mouse device(s)",
        (unsigned long long)list.size(), mouseCount);
}

bool App::AddDevice(HANDLE hDevice) {
    if (!hDevice || FindDevice(hDevice)) return false;
    std::wstring name = GetRawDeviceName(hDevice);

    auto m = std::make_unique<MouseDevice>();
    m->hDevice = hDevice;
    m->name = name;
    m->displayName = DeviceDisplayName(name);
    m->colorIndex = nextColor_++;
    LayoutNewDevice(*m);
    Log(L"Mouse added: %ls (%ls), cursor #%d at (%d,%d)",
        m->displayName.c_str(), m->name.c_str(), m->colorIndex + 1,
        (int)std::llround(m->x), (int)std::llround(m->y));
    bool wasMulti = multi_;
    mice_.push_back(std::move(m));

    if (!initializing_) {
        UpdateMode(L"device added");
        UpdateTrayTip();
        if (wasMulti && multi_ && !preview_ && !suspended_) {
            ShowBalloon(L"新鼠标已接入", L"已为这个鼠标添加独立光标。");
        }
    }
    return true;
}

void App::RemoveDevice(HANDLE hDevice) {
    auto it = std::find_if(mice_.begin(), mice_.end(),
                           [hDevice](const std::unique_ptr<MouseDevice>& m) {
                               return m->hDevice == hDevice;
                           });
    if (it == mice_.end()) return;

    MouseDevice* m = it->get();
    if (multi_) ReleaseOwnedButtons(m, false);
    Log(L"Mouse removed: %ls", m->displayName.c_str());
    mice_.erase(it);

    if (!initializing_) {
        UpdateMode(L"device removed");
        UpdateTrayTip();
    }
}

MouseDevice* App::FindDevice(HANDLE hDevice) const {
    for (const auto& m : mice_) {
        if (m->hDevice == hDevice) return m.get();
    }
    return nullptr;
}

void App::OnDeviceArrival(HANDLE hDevice) {
    if (!hDevice) return;
    if (FindDevice(hDevice)) return;
    if (!RawDeviceIsMouse(hDevice)) {
        // Device notifications are registered for the mouse usage only, so an
        // unreadable type is still almost certainly a mouse. Keep it.
        Log(L"Arrival %p did not report RIM_TYPEMOUSE; adding anyway", (void*)hDevice);
    }
    Log(L"WM_INPUT_DEVICE_CHANGE arrival: %p", (void*)hDevice);
    AddDevice(hDevice);
}

void App::OnDeviceRemoval(HANDLE hDevice) {
    Log(L"WM_INPUT_DEVICE_CHANGE removal: %p", (void*)hDevice);
    RemoveDevice(hDevice);
}

void App::LayoutNewDevice(MouseDevice& m) {
    POINT cur;
    GetCursorPos(&cur);
    double bx = cur.x, by = cur.y;
    if (!mice_.empty()) {
        bx = mice_[0]->x;
        by = mice_[0]->y;
    }
    m.x = bx + 48.0 * (double)mice_.size();
    m.y = by + 48.0 * (double)mice_.size();
    PointD p = ClampScreenPointD(m.x, m.y);
    m.x = p.x;
    m.y = p.y;
    m.initialized = true;

    if (multi_ && !preview_) {
        if (EnsureOverlay(m)) {
            m.overlay.MoveTo((int)std::llround(m.x), (int)std::llround(m.y));
            m.overlay.Show(true);
            m.shownX = (int)std::llround(m.x);
            m.shownY = (int)std::llround(m.y);
        }
    }
}

std::wstring App::DeviceDisplayName(const std::wstring& path) const {
    size_t v = path.find(L"VID_");
    size_t p = path.find(L"PID_");
    if (v != std::wstring::npos && p != std::wstring::npos && p > v) {
        size_t end = path.find(L'#', p);
        if (end == std::wstring::npos) end = path.size();
        return L"鼠标 " + path.substr(v, end - v);
    }
    if (path.size() > 72) return path.substr(0, 69) + L"...";
    return path;
}

// ---------------------------------------------------------------------------
// Mode switching
// ---------------------------------------------------------------------------

bool App::WantMulti() const {
    if (preview_) return false;
    switch (mode_) {
    case Mode::On:   return !mice_.empty();
    case Mode::Off:  return false;
    case Mode::Auto: return mice_.size() >= 2;
    }
    return false;
}

void App::UpdateMode(const wchar_t* reason) {
    if (initializing_ || preview_ || shuttingDown_ || suspended_) return;
    bool want = WantMulti();
    if (want && !multi_) {
        EnterMulti(reason);
    } else if (!want && multi_) {
        ExitMulti(reason);
    }
}

void App::EnterMulti(const wchar_t* reason) {
    if (multi_ || mice_.empty() || shuttingDown_) return;
    Log(L"Entering multi-cursor mode (%ls), %llu mouse(s)", reason,
        (unsigned long long)mice_.size());

    multi_ = true;
    if (!InstallHook()) {
        multi_ = false;
        Log(L"EnterMulti aborted: mouse hook installation failed");
        return;
    }
    if (!watchdogStarted_) {
        watchdogStarted_ = StartCursorWatchdog();
        if (!watchdogStarted_) {
            Log(L"Warning: cursor watchdog could not be started");
        }
    }
    if (!HideSystemCursor()) {
        UninstallHook();
        multi_ = false;
        ForceRestoreSystemCursors();
        Log(L"EnterMulti aborted: could not hide the system cursor");
        MessageBoxW(hwnd_, L"无法隐藏系统光标，多光标模式未启用。", kAppTitle,
                    MB_OK | MB_ICONWARNING);
        return;
    }

    ResetPhysicalButtons();

    POINT cur;
    GetCursorPos(&cur);
    bool anyOverlay = false;
    for (size_t i = 0; i < mice_.size(); ++i) {
        MouseDevice& m = *mice_[i];
        m.x = (double)cur.x + 48.0 * (double)i;
        m.y = (double)cur.y + 48.0 * (double)i;
        PointD p = ClampScreenPointD(m.x, m.y);
        m.x = p.x;
        m.y = p.y;
        m.initialized = true;
        m.speed = 0.0;
        m.pendingDx = 0.0;
        m.pendingDy = 0.0;
        m.lastQpc.QuadPart = 0;
        m.hasLastAbs = false;
        m.lastAbs = { 0, 0 };
        if (EnsureOverlay(m)) {
            anyOverlay = true;
            m.overlay.MoveTo((int)std::llround(m.x), (int)std::llround(m.y));
            m.overlay.Show(true);
            m.shownX = (int)std::llround(m.x);
            m.shownY = (int)std::llround(m.y);
        }
    }
    if (!anyOverlay) {
        Log(L"EnterMulti aborted: no overlay window could be created");
        UninstallHook();
        multi_ = false;
        RestoreSystemCursor();
        return;
    }

    activeCursorHandle_ = mice_.empty() ? nullptr : mice_[0]->hDevice;
    stateTimer_ = SetTimer(hwnd_, 2, 120, nullptr);
    UpdateCursorStates();

    UpdateTrayTip();
    if (!balloonShown_) {
        balloonShown_ = true;
        ShowBalloon(L"多光标模式已启用",
                    L"每个鼠标都有自己的光标。\n按 Ctrl+Alt+Q 可退出并恢复系统光标。");
    }
    Log(L"Multi-cursor mode active");
}

void App::ExitMulti(const wchar_t* reason) {
    if (!multi_) return;
    Log(L"Leaving multi-cursor mode (%ls)", reason);

    if (stateTimer_) {
        KillTimer(hwnd_, stateTimer_);
        stateTimer_ = 0;
    }
    activeCursorHandle_ = nullptr;

    ReleaseOwnedButtons(nullptr, true);
    UninstallHook();

    for (auto& m : mice_) {
        m->overlay.Destroy();
        m->shownX = INT_MIN;
        m->shownY = INT_MIN;
    }

    multi_ = false;
    POINT restore;
    if (!mice_.empty()) {
        restore = ClampScreenPoint((int)std::llround(mice_[0]->x), (int)std::llround(mice_[0]->y));
    } else {
        GetCursorPos(&restore);
    }
    SetCursorPos(restore.x, restore.y);
    RestoreSystemCursor();
    UpdateTrayTip();
    Log(L"Single-cursor mode restored");
}

bool App::EnsureOverlay(MouseDevice& m) {
    if (m.overlay.valid()) return true;
    CursorImage img = styles_.images[(int)m.state];
    if (img.pixels.empty()) img = builtinStyles_.images[(int)m.state];
    if (img.pixels.empty()) img = MakeCursorImage(cursorSize_, CursorColor(m.colorIndex));
    img = WithMouseBadge(img, CursorColor(m.colorIndex));
    if (!m.overlay.Create(img)) {
        Log(L"Failed to create overlay for cursor #%d", m.colorIndex + 1);
        return false;
    }
    m.shownX = INT_MIN;
    m.shownY = INT_MIN;
    return true;
}

// ---------------------------------------------------------------------------
// Low-level hook and click replay
// ---------------------------------------------------------------------------

LRESULT CALLBACK App::HookProcStatic(int code, WPARAM wp, LPARAM lp) {
    if (g_app) return g_app->HookProc(code, wp, lp);
    return CallNextHookEx(nullptr, code, wp, lp);
}

LRESULT App::HookProc(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && multi_) {
        const MSLLHOOKSTRUCT* ms = (const MSLLHOOKSTRUCT*)lp;
        bool injected = (ms->flags & LLMHF_INJECTED) != 0;
        bool pointerInput = (ms->dwExtraInfo & 0xFFFFFF00ULL) == 0xFF515700ULL;
        if (!injected && !pointerInput && ms->dwExtraInfo != kInjectedTag && !internalMove_) {
            // Physical mouse input is swallowed; raw input replays it at the
            // correct software cursor.
            return 1;
        }
    }
    return CallNextHookEx(hook_, code, wp, lp);
}

bool App::InstallHook() {
    if (hook_) return true;
    hook_ = SetWindowsHookExW(WH_MOUSE_LL, HookProcStatic, hInst_, 0);
    if (!hook_) {
        Log(L"SetWindowsHookEx(WH_MOUSE_LL) failed, err=%lu", GetLastError());
        return false;
    }
    Log(L"Low-level mouse hook installed");
    return true;
}

void App::UninstallHook() {
    if (!hook_) return;
    UnhookWindowsHookEx(hook_);
    hook_ = nullptr;
    Log(L"Low-level mouse hook removed");
}

void App::SendMouseInput(DWORD flags, DWORD mouseData) {
    INPUT in = {};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = flags;
    in.mi.mouseData = mouseData;
    in.mi.dwExtraInfo = kInjectedTag;
    SendInput(1, &in, sizeof(in));
}

void App::MoveSystemCursorTo(const MouseDevice& m) {
    int ix = (int)std::llround(m.x);
    int iy = (int)std::llround(m.y);
    activeCursorHandle_ = m.hDevice;
    internalMove_ = true;
    SetCursorPos(ix, iy);
    internalMove_ = false;
}

void App::ReleaseOwnedButtons(MouseDevice* onlyDevice, bool all) {
    for (int i = 0; i < (int)std::size(kButtonDefs); ++i) {
        HANDLE owner = buttonOwner_[i];
        if (!owner) continue;
        if (!all && (!onlyDevice || owner != onlyDevice->hDevice)) continue;
        MouseDevice* dev = FindDevice(owner);
        if (dev) {
            RouteButton(*dev, i, false);
            dev->buttons[i] = false;
        } else {
            SendMouseInput(kButtonDefs[i].upEvent, kButtonDefs[i].data);
        }
        buttonOwner_[i] = nullptr;
    }
    if (all) {
        for (auto& m : mice_) {
            for (bool& b : m->buttons) b = false;
        }
    } else if (onlyDevice) {
        for (bool& b : onlyDevice->buttons) b = false;
    }
}

bool App::AnyButtonOwned() const {
    for (HANDLE h : buttonOwner_) {
        if (h) return true;
    }
    return false;
}

void App::ResetPhysicalButtons() {
    int released = 0;
    for (const auto& pb : kPhysicalButtons) {
        if (GetAsyncKeyState(pb.vk) & 0x8000) {
            SendMouseInput(pb.upEvent, pb.data);
            ++released;
        }
    }
    // Also release any held buttons in bound windows.
    for (auto& m : mice_) {
        if (!m->boundWindow || !IsWindow(m->boundWindow)) continue;
        for (int i = 0; i < (int)std::size(kButtonDefs); ++i) {
            if (m->buttons[i]) PostButtonToWindow(*m, i, false);
        }
    }
    for (HANDLE& h : buttonOwner_) h = nullptr;
    for (auto& m : mice_) {
        for (bool& b : m->buttons) b = false;
    }
    if (released) Log(L"Reset %d physically held mouse button(s) on mode entry", released);
}

// ---------------------------------------------------------------------------
// Raw input
// ---------------------------------------------------------------------------

void App::OnRawInput(HRAWINPUT raw) {
    BYTE buffer[256] = {};
    UINT size = sizeof(buffer);
    UINT got = GetRawInputData(raw, RID_INPUT, buffer, &size, sizeof(RAWINPUTHEADER));
    if (got == (UINT)-1 || got == 0) return;
    RAWINPUT* ri = (RAWINPUT*)buffer;
    if (ri->header.dwType != RIM_TYPEMOUSE) return;
    if (!ri->header.hDevice) return;  // SendInput-generated event (ours or another app's)

    // Add a mouse on its first real input even if the device-arrival
    // notification was missed. This may switch Auto mode into multi-cursor.
    MouseDevice* m = FindDevice(ri->header.hDevice);
    if (!m) {
        AddDevice(ri->header.hDevice);
        m = FindDevice(ri->header.hDevice);
        if (!m) return;
    }
    if (!multi_) return;
    HandleMouseRaw(*m, ri->data.mouse);
}

void App::HandleMouseRaw(MouseDevice& m, const RAWMOUSE& rm) {
    double dx = 0.0, dy = 0.0;
    if (rm.usFlags & MOUSE_MOVE_ABSOLUTE) {
        if (m.hasLastAbs) {
            VirtualScreen vs = GetVirtualScreen();
            dx = (double)((int)rm.lLastX - (int)m.lastAbs.x) * vs.w / 65535.0;
            dy = (double)((int)rm.lLastY - (int)m.lastAbs.y) * vs.h / 65535.0;
        }
        m.lastAbs.x = (LONG)rm.lLastX;
        m.lastAbs.y = (LONG)rm.lLastY;
        m.hasLastAbs = true;
    } else {
        dx = (double)rm.lLastX;
        dy = (double)rm.lLastY;
        m.hasLastAbs = false;
    }
    if (dx != 0.0 || dy != 0.0) ApplyMovement(m, dx, dy);

    HandleButtons(m, rm);
    if (rm.usButtonFlags & RI_MOUSE_WHEEL) HandleWheel(m, rm, false);
    if (rm.usButtonFlags & RI_MOUSE_HWHEEL) HandleWheel(m, rm, true);
}

void App::ApplyMovement(MouseDevice& m, double dx, double dy) {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    double dt = 8.0;
    if (m.lastQpc.QuadPart != 0) {
        dt = (double)(now.QuadPart - m.lastQpc.QuadPart) * 1000.0 / (double)qpcFreq_;
    }
    m.lastQpc = now;
    dt = std::clamp(dt, 1.0, 50.0);

    double dist = std::sqrt(dx * dx + dy * dy);
    double inst = dist / dt;
    if (m.speed <= 0.0) m.speed = inst;
    else m.speed = m.speed * 0.70 + inst * 0.30;

    double gain = config_.sensitivity *
                  (1.0 + std::min(m.speed / 25.0, 2.0) * config_.acceleration);
    double moveX = dx * gain;
    double moveY = dy * gain;
    m.x += moveX;
    m.y += moveY;
    PointD p = ClampScreenPointD(m.x, m.y);
    m.x = p.x;
    m.y = p.y;

    bool otherOwnsButton = false;
    for (HANDLE owner : buttonOwner_) {
        if (owner && owner != m.hDevice) {
            otherOwnsButton = true;
            break;
        }
    }

    if (m.boundWindow && IsWindow(m.boundWindow)) {
        PostWindowMove(m);  // windowed game controlled by this mouse
    } else if (gameModeActive_) {
        SendRelativeMotion(m, moveX, moveY);  // fullscreen game uses relative input
    }
    RenderCursor(m, !otherOwnsButton && !m.boundWindow && !gameModeActive_);
}

void App::RenderCursor(MouseDevice& m, bool moveSystemCursor) {
    if (multi_ && !m.overlay.valid()) EnsureOverlay(m);
    int ix = (int)std::llround(m.x);
    int iy = (int)std::llround(m.y);
    if (m.overlay.valid() && (ix != m.shownX || iy != m.shownY)) {
        m.overlay.MoveTo(ix, iy);
        m.shownX = ix;
        m.shownY = iy;
    }
    if (moveSystemCursor) MoveSystemCursorTo(m);
}

void App::HandleButtons(MouseDevice& m, const RAWMOUSE& rm) {
    for (int i = 0; i < (int)std::size(kButtonDefs); ++i) {
        const ButtonDef& b = kButtonDefs[i];
        if (rm.usButtonFlags & b.downFlag) {
            m.buttons[i] = true;
            if (buttonOwner_[i] && buttonOwner_[i] != m.hDevice) {
                Log(L"Button %d down ignored: another mouse is dragging", i);
                continue;
            }
            if (buttonOwner_[i] == m.hDevice) continue;
            RouteButton(m, i, true);
            buttonOwner_[i] = m.hDevice;
        }
        if (rm.usButtonFlags & b.upFlag) {
            m.buttons[i] = false;
            if (buttonOwner_[i] != m.hDevice) continue;
            RouteButton(m, i, false);
            buttonOwner_[i] = nullptr;
        }
    }
}

void App::HandleWheel(MouseDevice& m, const RAWMOUSE& rm, bool horizontal) {
    if (AnyButtonOwned()) {
        return;  // do not disturb an active drag
    }
    DWORD delta = (DWORD)(SHORT)rm.usButtonData;
    RouteWheel(m, delta, horizontal);
}

// ---------------------------------------------------------------------------
// Display changes / preview
// ---------------------------------------------------------------------------

void App::HandleDisplayChange() {
    VirtualScreen vs = GetVirtualScreen();
    Log(L"Display change: virtual screen=(%d,%d) %dx%d", vs.x, vs.y, vs.w, vs.h);
    for (auto& m : mice_) {
        PointD p = ClampScreenPointD(m->x, m->y);
        m->x = p.x;
        m->y = p.y;
        if (multi_ && m->overlay.valid()) {
            int ix = (int)std::llround(m->x);
            int iy = (int)std::llround(m->y);
            m->overlay.MoveTo(ix, iy);
            m->shownX = ix;
            m->shownY = iy;
        }
    }
    if (multi_ && !mice_.empty()) MoveSystemCursorTo(*mice_[0]);
}

void App::UpdateAllOverlays() {
    for (auto& m : mice_) {
        if (!multi_ || !m->overlay.valid()) continue;
        int ix = (int)std::llround(m->x);
        int iy = (int)std::llround(m->y);
        m->overlay.MoveTo(ix, iy);
        m->shownX = ix;
        m->shownY = iy;
    }
}

void App::RunPreview() {
    Log(L"Preview mode: showing 3 sample cursors for 8 seconds");
    VirtualScreen vs = GetVirtualScreen();
    int cx = vs.x + vs.w / 2;
    int cy = vs.y + vs.h / 2;
    struct Sample { int x, y, color; };
    const Sample samples[] = {
        { cx - 220, cy - 120, 0 },
        { cx,       cy,       1 },
        { cx + 220, cy + 120, 2 },
    };
    for (const auto& s : samples) {
        auto ov = std::make_unique<CursorOverlay>();
        CursorImage img = MakeCursorImage(cursorSize_, CursorColor(s.color));
        if (!ov->Create(img)) continue;
        ov->MoveTo(s.x, s.y);
        ov->Show(true);
        previewOverlays_.push_back(std::move(ov));
    }
    SetTimer(hwnd_, 1, 8000, nullptr);
}

// ---------------------------------------------------------------------------
// Window procedure
// ---------------------------------------------------------------------------

LRESULT CALLBACK App::WndProcStatic(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (g_app) return g_app->WndProc(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT App::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (taskbarCreatedMsg_ && msg == taskbarCreatedMsg_) {
        Log(L"Explorer restarted; re-adding tray icon");
        trayAdded_ = false;
        AddTrayIcon();
        return 0;
    }

    switch (msg) {
    case WM_INPUT:
        OnRawInput((HRAWINPUT)lp);
        return 0;
    case WM_INPUT_DEVICE_CHANGE:
        if (wp == GIDC_ARRIVAL) OnDeviceArrival((HANDLE)lp);
        else if (wp == GIDC_REMOVAL) OnDeviceRemoval((HANDLE)lp);
        return 0;
    case WM_HOTKEY:
        if (wp == HOTKEY_EXIT) {
            Log(L"Exit hotkey pressed");
            PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        } else if (wp == HOTKEY_TOGGLE) {
            if (multi_) {
                mode_ = Mode::Off;
            } else {
                mode_ = Mode::On;
            }
            UpdateMode(L"hotkey toggle");
            UpdateTrayTip();
        } else if (wp == HOTKEY_GAME) {
            if (gameMode_ == GameMode::Auto) gameMode_ = GameMode::On;
            else if (gameMode_ == GameMode::On) gameMode_ = GameMode::Off;
            else gameMode_ = GameMode::Auto;
            UpdateGameMode(L"hotkey game");
        }
        return 0;
    case WM_TRAYICON:
        OnTrayMessage(lp);
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case MENU_MODE_AUTO:
            mode_ = Mode::Auto;
            UpdateMode(L"command: auto");
            UpdateTrayTip();
            return 0;
        case MENU_MODE_ON:
            mode_ = Mode::On;
            UpdateMode(L"command: on");
            UpdateTrayTip();
            return 0;
        case MENU_MODE_OFF:
            mode_ = Mode::Off;
            UpdateMode(L"command: off");
            UpdateTrayTip();
            return 0;
        case MENU_GAME_AUTO:
            gameMode_ = GameMode::Auto;
            UpdateGameMode(L"command: game auto");
            return 0;
        case MENU_GAME_ON:
            gameMode_ = GameMode::On;
            UpdateGameMode(L"command: game on");
            return 0;
        case MENU_GAME_OFF:
            gameMode_ = GameMode::Off;
            UpdateGameMode(L"command: game off");
            return 0;
        case MENU_UNBIND_ALL:
            UnbindAllWindows();
            return 0;
        case MENU_SHOW_DEVICES:
            ShowDeviceInfo();
            return 0;
        case MENU_OPEN_LOG: {
            HINSTANCE r = ShellExecuteW(hwnd_, L"open", GetLogPath().c_str(), nullptr, nullptr,
                                        SW_SHOWNORMAL);
            if ((INT_PTR)r <= 32) {
                MessageBoxW(hwnd_, L"无法打开日志文件。", kAppTitle, MB_OK | MB_ICONWARNING);
            }
            return 0;
        }
        case MENU_EDIT_STYLES:
            EditStyles();
            return 0;
        case MENU_EXIT:
            PostMessageW(hwnd_, WM_CLOSE, 0, 0);
            return 0;
        default:
            break;
        }
        break;
    case WM_DISPLAYCHANGE:
        HandleDisplayChange();
        return 0;
    case WM_WTSSESSION_CHANGE:
        if (wp == WTS_SESSION_LOCK) {
            if (multi_ && !suspended_) {
                suspended_ = true;
                Log(L"Session locked; restoring system cursor");
                RestoreSystemCursor();
            }
        } else if (wp == WTS_SESSION_UNLOCK) {
            if (multi_ && suspended_) {
                suspended_ = false;
                Log(L"Session unlocked; hiding system cursor again");
                if (!HideSystemCursor()) {
                    Log(L"Could not hide system cursor after unlock; leaving multi mode");
                    ExitMulti(L"unlock hide failed");
                }
            }
        }
        return 0;
    case WM_SETTINGCHANGE:
        if (wp == SPI_SETCURSORS && multi_ && !shuttingDown_ && !suspended_) {
            Log(L"System cursor scheme changed externally; hiding cursor again");
            ReapplySystemCursorHiding();
        }
        break;
    case WM_TIMER:
        if (preview_ && wp == 1) {
            KillTimer(hwnd_, 1);
            PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        } else if (wp == 2) {
            UpdateCursorStates();
        } else if (wp == 3) {
            UpdateGameMode(L"timer");
            ValidateWindowBindings();
            if (gameModeActive_) UpdateAllOverlays();
        }
        return 0;
    case WM_QUERYENDSESSION:
        return TRUE;
    case WM_ENDSESSION:
        if (wp) {
            Shutdown();
            PostQuitMessage(0);
        }
        return 0;
    case WM_CLOSE:
        Shutdown();
        DestroyWindow(hwnd_);
        return 0;
    case WM_DESTROY:
        hwnd_ = nullptr;
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

int RunDiagnose() {
    LogInit();
    Log(L"==== MultiCursor diagnose ====");
    Log(L"exe=%ls", GetExePath().c_str());
    VirtualScreen vs = GetVirtualScreen();
    Log(L"virtual screen=(%d,%d) %dx%d", vs.x, vs.y, vs.w, vs.h);
    Log(L"cursor size=%d, sensitivity=%.2f, acceleration=%.2f",
        GetSystemMetrics(SM_CXCURSOR), LoadConfig().sensitivity, LoadConfig().acceleration);

    UINT count = 0;
    if (GetRawInputDeviceList(nullptr, &count, sizeof(RAWINPUTDEVICELIST)) != 0) {
        Log(L"GetRawInputDeviceList(size) failed, err=%lu", GetLastError());
        return 1;
    }
    std::vector<RAWINPUTDEVICELIST> list(count);
    UINT got = GetRawInputDeviceList(list.data(), &count, sizeof(RAWINPUTDEVICELIST));
    if (got == (UINT)-1) {
        Log(L"GetRawInputDeviceList failed, err=%lu", GetLastError());
        return 1;
    }
    list.resize(got);

    int mice = 0;
    for (const auto& dev : list) {
        const wchar_t* type = dev.dwType == RIM_TYPEMOUSE ? L"mouse" :
                              dev.dwType == RIM_TYPEKEYBOARD ? L"keyboard" :
                              dev.dwType == RIM_TYPEHID ? L"hid" : L"other";
        if (dev.dwType == RIM_TYPEMOUSE) {
            ++mice;
            std::wstring name = GetRawDeviceName(dev.hDevice);
            RID_DEVICE_INFO info = {};
            info.cbSize = sizeof(info);
            UINT sz = sizeof(info);
            GetRawInputDeviceInfoW(dev.hDevice, RIDI_DEVICEINFO, &info, &sz);
            Log(L"device %d: %ls handle=%p buttons=%lu name=%ls", mice, type, (void*)dev.hDevice,
                info.dwType == RIM_TYPEMOUSE ? (unsigned long)info.mouse.dwNumberOfButtons : 0,
                name.c_str());
        } else {
            Log(L"device: %ls handle=%p", type, (void*)dev.hDevice);
        }
    }
    Log(L"diagnose: %llu raw devices, %d mouse device(s), log=%ls",
        (unsigned long long)list.size(), mice, GetLogPath().c_str());
    return 0;
}

} // namespace mc
