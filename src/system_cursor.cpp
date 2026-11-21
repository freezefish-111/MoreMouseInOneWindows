// system_cursor.cpp - replace all system cursors with a transparent cursor
#include "system_cursor.h"

#include <cwchar>
#include <iterator>
#include <vector>

#ifndef OCR_HELP
#define OCR_HELP 32651
#endif
#ifndef OCR_PIN
#define OCR_PIN 32671
#endif
#ifndef OCR_PERSON
#define OCR_PERSON 32672
#endif

namespace mc {

namespace {

bool g_hidden = false;

const DWORD kCursorIds[] = {
    OCR_NORMAL,
    OCR_IBEAM,
    OCR_WAIT,
    OCR_CROSS,
    OCR_UP,
    OCR_SIZENWSE,
    OCR_SIZENESW,
    OCR_SIZEWE,
    OCR_SIZENS,
    OCR_SIZEALL,
    OCR_NO,
    OCR_HAND,
    OCR_APPSTARTING,
    OCR_HELP,
    OCR_PIN,
    OCR_PERSON,
};

} // namespace

bool IsSystemCursorHidden() {
    return g_hidden;
}

bool HideSystemCursor() {
    if (g_hidden) return true;

    const int w = 32, h = 32;
    const int stride = ((w + 15) / 16) * 2;
    std::vector<BYTE> andMask((size_t)stride * h, 0xFF);  // keep screen
    std::vector<BYTE> xorMask((size_t)stride * h, 0x00);  // no inversion
    HCURSOR blank = CreateCursor(GetModuleHandleW(nullptr), 0, 0, w, h,
                                 andMask.data(), xorMask.data());
    if (!blank) {
        Log(L"HideSystemCursor: CreateCursor failed, err=%lu", GetLastError());
        return false;
    }

    bool normalOk = false;
    int failures = 0;
    for (DWORD id : kCursorIds) {
        HCURSOR copy = (HCURSOR)CopyIcon((HICON)blank);
        if (!copy) {
            ++failures;
            continue;
        }
        // SetSystemCursor destroys the supplied handle, even on failure.
        if (!SetSystemCursor(copy, id)) {
            ++failures;
            continue;
        }
        if (id == OCR_NORMAL) normalOk = true;
    }
    DestroyCursor(blank);

    if (!normalOk) {
        Log(L"HideSystemCursor: SetSystemCursor(OCR_NORMAL) failed, err=%lu", GetLastError());
        g_hidden = false;
        return false;
    }

    g_hidden = true;
    if (failures) Log(L"HideSystemCursor: %d cursor ids could not be replaced", failures);
    Log(L"System cursor hidden (blank cursor installed)");
    return true;
}

void ReapplySystemCursorHiding() {
    // The scheme was reloaded by somebody else; install our blank cursors again.
    g_hidden = false;
    HideSystemCursor();
}

void ForceRestoreSystemCursors() {
    if (!SystemParametersInfoW(SPI_SETCURSORS, 0, nullptr, SPIF_SENDCHANGE)) {
        Log(L"ForceRestoreSystemCursors: SPI_SETCURSORS failed, err=%lu", GetLastError());
    } else {
        Log(L"System cursors reloaded from the user's scheme");
    }
    g_hidden = false;
}

void RestoreSystemCursor() {
    if (!g_hidden) return;
    ForceRestoreSystemCursors();
}

bool StartCursorWatchdog() {
    std::wstring exe = GetExePath();
    wchar_t cmd[32768];
    swprintf(cmd, std::size(cmd), L"\"%ls\" --watchdog %lu", exe.c_str(),
             (unsigned long)GetCurrentProcessId());

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    BOOL ok = CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE,
                             CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                             nullptr, nullptr, &si, &pi);
    if (!ok) {
        Log(L"StartCursorWatchdog: CreateProcess failed, err=%lu", GetLastError());
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    Log(L"Cursor watchdog started, pid=%lu", (unsigned long)pi.dwProcessId);
    return true;
}

int RunCursorWatchdog(unsigned long parentPid) {
    HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, parentPid);
    if (!parent) {
        // Parent already gone; make sure the cursor is not left blank.
        ForceRestoreSystemCursors();
        return 0;
    }
    WaitForSingleObject(parent, INFINITE);
    CloseHandle(parent);
    ForceRestoreSystemCursors();
    return 0;
}

} // namespace mc
