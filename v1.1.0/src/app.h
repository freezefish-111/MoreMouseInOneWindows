// app.h - MultiCursor application
#pragma once

#include "common.h"
#include "cursor_image.h"
#include "cursor_style.h"
#include "overlay.h"
#include "system_cursor.h"

#include <array>
#include <memory>

namespace mc {

enum class Mode { Auto, On, Off };

struct MouseDevice {
    HANDLE hDevice = nullptr;
    std::wstring name;
    std::wstring displayName;
    double x = 0.0;
    double y = 0.0;
    int shownX = INT_MIN;
    int shownY = INT_MIN;
    int colorIndex = 0;
    CursorState state = CursorState::Normal;
    bool stateValid = false;
    CursorOverlay overlay;
    POINT lastAbs = { 0, 0 };
    bool hasLastAbs = false;
    double speed = 0.0;
    LARGE_INTEGER lastQpc = {};
    bool buttons[5] = {};
    bool initialized = false;
};

class App {
public:
    App() = default;
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    bool Init(HINSTANCE hInst, const std::wstring& forcedMode, bool preview);
    int Run();
    void Shutdown();

private:
    static LRESULT CALLBACK WndProcStatic(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK HookProcStatic(int, WPARAM, LPARAM);
    LRESULT WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HookProc(int, WPARAM, LPARAM);

    bool CreateHostWindow();
    void RegisterHotkeys();

    void AddTrayIcon();
    void RemoveTrayIcon();
    void UpdateTrayTip();
    void ShowTrayMenu();
    void OnTrayMessage(LPARAM lp);
    void ShowBalloon(const std::wstring& title, const std::wstring& text);
    void ShowDeviceInfo();

    bool RegisterRawInput();
    void EnumerateDevices();
    bool AddDevice(HANDLE hDevice);
    void RemoveDevice(HANDLE hDevice);
    MouseDevice* FindDevice(HANDLE hDevice) const;
    void OnDeviceArrival(HANDLE hDevice);
    void OnDeviceRemoval(HANDLE hDevice);
    void OnRawInput(HRAWINPUT raw);
    void HandleMouseRaw(MouseDevice& m, const RAWMOUSE& rm);
    void ApplyMovement(MouseDevice& m, double dx, double dy);
    void RenderCursor(MouseDevice& m, bool moveSystemCursor);
    void HandleButtons(MouseDevice& m, const RAWMOUSE& rm);
    void HandleWheel(MouseDevice& m, const RAWMOUSE& rm, bool horizontal);

    bool WantMulti() const;
    void UpdateMode(const wchar_t* reason);
    void EnterMulti(const wchar_t* reason);
    void ExitMulti(const wchar_t* reason);
    bool EnsureOverlay(MouseDevice& m);
    void LayoutNewDevice(MouseDevice& m);

    bool InstallHook();
    void UninstallHook();
    void SendMouseInput(DWORD flags, DWORD mouseData = 0);
    void MoveSystemCursorTo(const MouseDevice& m);
    void ReleaseOwnedButtons(MouseDevice* onlyDevice, bool all);
    void ResetPhysicalButtons();
    bool AnyButtonOwned() const;

    void RunPreview();
    void HandleDisplayChange();
    void UpdateAllOverlays();

    void LoadInitialStyles();
    void ReloadStyles();
    void CaptureSystemCursorHandles();
    void RecordStyleFileTime();
    void CheckStylesFile();
    CursorState DetectCursorState() const;
    void UpdateCursorStates();
    void ApplyCursorImage(MouseDevice& m);
    void EditStyles();

    std::wstring DeviceDisplayName(const std::wstring& path) const;
    std::wstring StatusText() const;

    HINSTANCE hInst_ = nullptr;
    HWND hwnd_ = nullptr;
    HHOOK hook_ = nullptr;
    HICON icon_ = nullptr;
    UINT taskbarCreatedMsg_ = 0;
    bool trayAdded_ = false;
    bool multi_ = false;
    bool shuttingDown_ = false;
    bool internalMove_ = false;
    bool preview_ = false;
    bool balloonShown_ = false;
    bool rawRegistered_ = false;
    bool initializing_ = true;
    bool watchdogStarted_ = false;
    bool sessionNotify_ = false;
    bool suspended_ = false;
    Mode mode_ = Mode::Auto;
    AppConfig config_;
    std::vector<std::unique_ptr<MouseDevice>> mice_;
    std::vector<std::unique_ptr<CursorOverlay>> previewOverlays_;
    HANDLE buttonOwner_[5] = {};
    LONGLONG qpcFreq_ = 1;
    int cursorSize_ = 32;
    int nextColor_ = 0;

    CursorStyleSet builtinStyles_;
    CursorStyleSet styles_;
    std::array<HCURSOR, CursorStateCount> systemCursorHandles_{};
    HANDLE activeCursorHandle_ = nullptr;
    UINT_PTR stateTimer_ = 0;
    bool hasStyleFile_ = false;
    FILETIME styleFileTime_ = {};
};

// Standalone diagnostic: write the detected raw mouse devices to the log.
int RunDiagnose();

} // namespace mc
