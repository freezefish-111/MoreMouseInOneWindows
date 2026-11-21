// main.cpp - entry point, command line, single-instance guard
#include "app.h"
#include "editor.h"

#include <windows.h>
#include <shellapi.h>
#include <cwchar>
#include <string>

namespace {

void EnableDpiAwareness() {
    typedef BOOL (WINAPI *SetDpiCtxFn)(HANDLE);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        auto fn = (SetDpiCtxFn)(void*)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
        if (fn) {
            // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 / _PER_MONITOR_AWARE
            if (fn((HANDLE)-4)) return;
            if (fn((HANDLE)-3)) return;
        }
    }
    SetProcessDPIAware();
}

void ShowUsage() {
    MessageBoxW(nullptr,
        L"MultiCursor - 多鼠标独立光标\n\n"
        L"用法：MultiCursor.exe [选项]\n\n"
        L"  --mode auto|on|off   光标模式（默认 auto：2 个及以上鼠标时启用）\n"
        L"  --multi              等同于 --mode on\n"
        L"  --off                等同于 --mode off\n"
        L"  --preview            预览 3 个示例光标（8 秒后自动退出，不接管鼠标）\n"
        L"  --diagnose           把检测到的鼠标设备写入日志后退出\n"
        L"  --help               显示本帮助\n\n"
        L"运行后可在任务栏托盘图标中切换模式、绑定窗口。\n"
        L"快捷键：Ctrl+Alt+M 切换多光标，Ctrl+Alt+G 切换游戏模式，Ctrl+Alt+Q 退出并恢复系统光标。",
        L"MultiCursor", MB_OK | MB_ICONINFORMATION);
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
    EnableDpiAwareness();

    bool preview = false;
    bool diagnose = false;
    bool help = false;
    bool createEditor = false;
    bool watchdog = false;
    unsigned long watchdogPid = 0;
    std::wstring forcedMode;

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            std::wstring a = argv[i];
            if (a == L"--watchdog" && i + 1 < argc) {
                watchdog = true;
                watchdogPid = wcstoul(argv[++i], nullptr, 10);
            } else if (a == L"--preview") {
                preview = true;
            } else if (a == L"--diagnose") {
                diagnose = true;
            } else if (a == L"--create") {
                createEditor = true;
            } else if (a == L"--help" || a == L"-h" || a == L"/?") {
                help = true;
            } else if (a == L"--multi") {
                forcedMode = L"on";
            } else if (a == L"--off") {
                forcedMode = L"off";
            } else if (a == L"--mode" && i + 1 < argc) {
                forcedMode = argv[++i];
            }
        }
        LocalFree(argv);
    }

    if (watchdog) return mc::RunCursorWatchdog(watchdogPid);
    if (diagnose) return mc::RunDiagnose();
    if (help) {
        ShowUsage();
        return 0;
    }
    if (createEditor) return mc::RunCursorEditor();

    HANDLE mutex = nullptr;
    if (!preview) {
        mutex = CreateMutexW(nullptr, FALSE, mc::kMutexName);
        if (!mutex) {
            MessageBoxW(nullptr, L"无法创建单实例互斥量。", mc::kAppTitle,
                        MB_OK | MB_ICONERROR);
            return 1;
        }
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            MessageBoxW(nullptr,
                        L"MultiCursor 已经在运行。\n\n"
                        L"请使用任务栏托盘图标退出，或按 Ctrl+Alt+Q。",
                        mc::kAppTitle, MB_OK | MB_ICONINFORMATION);
            CloseHandle(mutex);
            return 0;
        }
    }

    int result = 0;
    {
        mc::App app;
        if (!app.Init(hInstance, forcedMode, preview)) {
            result = 1;
        } else {
            result = app.Run();
        }
    }

    if (mutex) CloseHandle(mutex);
    return result;
}
