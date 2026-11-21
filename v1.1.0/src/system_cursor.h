// system_cursor.h - hide/restore the shared Windows cursor and crash watchdog
#pragma once

#include "common.h"

namespace mc {

bool HideSystemCursor();
void ReapplySystemCursorHiding();
void RestoreSystemCursor();
void ForceRestoreSystemCursors();
bool IsSystemCursorHidden();

// Spawn a copy of this executable that restores the system cursor if the main
// process dies without cleaning up (including Task Manager / crash).
bool StartCursorWatchdog();
int RunCursorWatchdog(unsigned long parentPid);

} // namespace mc
