@echo off
setlocal
cd /d "%~dp0"

where g++ >nul 2>nul
if not errorlevel 1 (
    echo Building with MinGW g++ ...
    g++ -std=c++17 -O2 -municode -mwindows ^
        -DUNICODE -D_UNICODE -DOEMRESOURCE ^
        -finput-charset=UTF-8 -Wall -Wextra -Wno-cast-function-type ^
        -static -s ^
        -o MultiCursor.exe ^
        src\common.cpp src\cursor_image.cpp src\overlay.cpp ^
        src\system_cursor.cpp src\app.cpp src\main.cpp ^
        -luser32 -lgdi32 -lshell32 -lwtsapi32
    if errorlevel 1 (
        echo Build failed.
        exit /b 1
    )
    echo Built MultiCursor.exe
    exit /b 0
)

where cl >nul 2>nul
if not errorlevel 1 (
    echo Building with MSVC cl ...
    cl /nologo /std:c++17 /O2 /EHsc /utf-8 /W4 ^
        /DUNICODE /D_UNICODE /DOEMRESOURCE ^
        /Fe:MultiCursor.exe ^
        src\common.cpp src\cursor_image.cpp src\overlay.cpp ^
        src\system_cursor.cpp src\app.cpp src\main.cpp ^
        /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib shell32.lib wtsapi32.lib
    if errorlevel 1 (
        echo Build failed.
        exit /b 1
    )
    echo Built MultiCursor.exe
    exit /b 0
)

echo No C++ compiler found. Install MinGW-w64 (g++) or Visual Studio Build Tools,
echo or use CMake:
echo   cmake -S . -B build
echo   cmake --build build --config Release
exit /b 1
