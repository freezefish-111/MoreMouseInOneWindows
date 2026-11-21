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

    VirtualScreen vs = GetVirtualScreen();
    LogInit();
    Log(L"==== MultiCursor starting ====");
    Log(L"exe=%ls", GetExePath().c_str());
    Log(L"virtual screen=(%d,%d) %dx%d, cursor size=%d, mode=%ls, sensitivity=%.2f, acceleration=%.2f",
        vs.x, vs.y, vs.w, vs.h, cursorSize_, mode_ == Mode::On ? L"on" :
        mode_ == Mode::Off ? L"off" : L"auto", config_.sensitivity, config_.acceleration);

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
}

std::wstring App::StatusText() const {
    wchar_t buf[256];
    swprintf(buf, std::size(buf), L"已检测到 %llu 个鼠标 · %ls",
             (unsigned long long)mice_.size(),
             multi_ ? L"多光标模式" : L"单光标模式");
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
                     + L" · " + std::to_wstring(mice_.size()) + L" 个鼠标 · Ctrl+Alt+Q 退出";
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
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, MENU_SHOW_DEVICES, L"显示设备信息...");
    AppendMenuW(menu, MF_STRING, MENU_OPEN_LOG, L"打开日志文件");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, MENU_EXIT, L"退出并恢复系统光标");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd_);
    UINT cmd = (UINT)TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
                                    pt.x, pt.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    PostMessageW(hwnd_, WM_NULL, 0, 0);

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
    case MENU_EXIT:
        PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        break;
    default:
        break;
    }
}

void App::ShowDeviceInfo() {
    std::wstring text = L"检测到 " + std::to_wstring(mice_.size()) + L" 个鼠标设备。\r\n\r\n";
    if (mice_.empty()) {
        text += L"（没有检测到鼠标）\r\n";
    }
    for (size_t i = 0; i < mice_.size(); ++i) {
        const MouseDevice& m = *mice_[i];
        wchar_t line[2048];
        swprintf(line, std::size(line),
                 L"#%llu  %ls\r\n位置: (%d, %d)\r\n设备: %ls\r\n\r\n",
                 (unsigned long long)(i + 1), m.displayName.c_str(),
                 (int)std::llround(m.x), (int)std::llround(m.y), m.name.c_str());
        text += line;
    }
    text += L"快捷键: Ctrl+Alt+Q 退出，Ctrl+Alt+M 切换多光标模式。";
    MessageBoxW(hwnd_, text.c_str(), L"MultiCursor 设备信息", MB_OK | MB_ICONINFORMATION);
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
    CursorImage img = MakeCursorImage(cursorSize_, CursorColor(m.colorIndex));
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
        if (dev) MoveSystemCursorTo(*dev);
        SendMouseInput(kButtonDefs[i].upEvent, kButtonDefs[i].data);
        if (dev) dev->buttons[i] = false;
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
    m.x += dx * gain;
    m.y += dy * gain;
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
    RenderCursor(m, !otherOwnsButton);
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
            MoveSystemCursorTo(m);
            SendMouseInput(b.downEvent, b.data);
            buttonOwner_[i] = m.hDevice;
        }
        if (rm.usButtonFlags & b.upFlag) {
            m.buttons[i] = false;
            if (buttonOwner_[i] != m.hDevice) continue;
            MoveSystemCursorTo(m);
            SendMouseInput(b.upEvent, b.data);
            buttonOwner_[i] = nullptr;
        }
    }
}

void App::HandleWheel(MouseDevice& m, const RAWMOUSE& rm, bool horizontal) {
    if (AnyButtonOwned()) {
        return;  // do not disturb an active drag
    }
    MoveSystemCursorTo(m);
    DWORD delta = (DWORD)(SHORT)rm.usButtonData;
    SendMouseInput(horizontal ? MOUSEEVENTF_HWHEEL : MOUSEEVENTF_WHEEL, delta);
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
