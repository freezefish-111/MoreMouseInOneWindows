# MultiCursor - 多鼠标独立光标 / Independent Cursors for Multiple Mice

[中文](#中文) | [English](#english)

> 最新版本 / Latest release: **v1.2.0** — [下载 / Download](https://github.com/freezefish-111/MoreMouseInOneWindows/releases/latest)
>
> 源码目录 / Source folders: `v1.2.0/`（最新 / latest）、`v1.1.0/`、根目录（v1.0.0）。根目录保留 v1.0.0 源码；新功能请使用 `v1.2.0/`。

## 中文

一个 Windows 10/11 的 C++ 小工具：为每个鼠标设备显示自己的光标。接入第二个鼠标后，两个鼠标不再共同控制同一个系统指针，而是各自移动自己的彩色箭头光标。程序不需要安装驱动，使用 Win32 Raw Input、低级鼠标钩子和分层窗口实现。

### 下载

从 [GitHub Releases](https://github.com/freezefish-111/MoreMouseInOneWindows/releases) 下载最新版 `MultiCursor.exe`。

### 功能

- 自动检测 Raw Input 鼠标设备，支持热插拔。
- 自动模式：检测到 2 个及以上鼠标时启用多光标；只有 1 个鼠标时保持普通单光标。
- 每个鼠标一个置顶、点击穿透的光标窗口。光标颜色依次为黑、蓝、绿、橙、紫、红等。
- 每个鼠标独立处理移动、左/右/中键、侧键、滚轮和横向滚轮。
- 隐藏系统共享光标（替换系统光标资源，而不是 `ShowCursor`），退出或进程崩溃时恢复。
- 托盘菜单；`Ctrl+Alt+M` 切换多光标，`Ctrl+Alt+Q` 退出并恢复系统光标。
- 单实例运行；崩溃看门狗会在主进程异常结束时恢复系统光标。
- 提供预览和诊断模式，方便在没有第二个鼠标时检查效果。
- **自定义光标样式**：内置样式编辑器，可为 16 种光标状态（普通/文字/等待/十字/手型/禁止/缩放/移动等）分别设置图片。
- 支持导入本地 `PNG/JPG/GIF/BMP` 图片，或用画笔/橡皮直接编辑；可单击预览设置热点。
- 自定义样式保存在 `Cursors.ini` 和 `Cursors\` 目录，运行中的程序会自动检测改动并热更新。
- 每个虚拟光标仍带有颜色徽章，自定义样式下也能区分不同鼠标。
- **全屏游戏模式**：自动检测无边框全屏窗口，改用相对鼠标移动（`MOUSEEVENTF_MOVE`），让使用 Raw Input / DirectInput 的游戏也能收到鼠标移动；可用 `Ctrl+Alt+G` 切换自动/强制/关闭。
- **窗口绑定**：托盘菜单可为每个鼠标单独绑定一个窗口，把移动、按键和滚轮以 `WM_MOUSE*` 消息投递到该窗口，实现“不同鼠标玩不同窗口游戏”。
- 修复样式编辑器预览棋盘格偏移、保存提示文案、PNG 保存失败回退 BMP，以及游戏/窗口绑定状态下光标状态检测误判等问题。

### 运行

直接双击 `MultiCursor.exe`。默认是自动模式：检测到两个鼠标后自动进入多光标模式。

建议第一次先运行预览，确认光标样式：

```bat
MultiCursor.exe --preview
```

预览会显示 3 个示例光标，8 秒后自动退出，不会接管鼠标输入。

打开光标样式编辑器（可作为独立进程运行，即使主程序已经在运行）：

```bat
MultiCursor.exe --create
```

也可从托盘菜单选择“编辑光标样式...”。

查看当前检测到的鼠标设备：

```bat
MultiCursor.exe --diagnose
```

诊断信息会写入日志：

```text
%LOCALAPPDATA%\MultiCursor\multicursor.log
```

#### 命令行选项

```text
MultiCursor.exe [选项]

  --mode auto|on|off   光标模式（默认 auto）
  --multi              等同于 --mode on
  --off                等同于 --mode off
  --preview            预览 3 个示例光标，8 秒后退出
  --diagnose           列出 Raw Input 设备后退出
  --create             打开光标样式编辑器（独立进程）
  --help               显示帮助
```

#### 托盘菜单

- 自动模式（2 个及以上鼠标时启用）
- 始终启用多光标
- 关闭多光标（普通鼠标）
- 全屏游戏模式：自动检测 / 始终开启 / 关闭（`Ctrl+Alt+G` 循环切换）
- 显示设备信息
- 打开日志文件
- 编辑光标样式...
- 窗口绑定（不同鼠标控制不同窗口）...
- 退出并恢复系统光标

### 配置

程序启动时读取可执行文件同目录下的 `MultiCursor.ini`。文件不存在时使用默认值。

```ini
[MultiCursor]
sensitivity=0.80
acceleration=0.90
mode=auto
gameMode=auto
```

- `sensitivity`：原始鼠标计数到屏幕像素的倍率，范围 0.10 - 5.00。默认 0.80。
- `acceleration`：高速移动时的额外增益，范围 0.00 - 3.00。默认 0.90。
- `mode`：`auto`、`on` 或 `off`。命令行参数优先于配置文件。
- `gameMode`：`auto`（检测无边框全屏窗口时启用相对输入）、`on`（始终相对输入）、`off`（始终绝对定位）。默认 `auto`。

如果觉得光标移动太快，把 `sensitivity` 调低；觉得太慢则调高。

#### 自定义光标样式

运行 `MultiCursor.exe --create`（或从托盘菜单选择“编辑光标样式...”）打开样式编辑器。可为 16 种状态分别导入图片、绘制、设置热点；保存后写入可执行文件同目录下的 `Cursors.ini` 和 `Cursors\` 文件夹：

```ini
[normal]
image=Cursors\normal.png
hotspotX=8
hotspotY=4
custom=1
```

状态键名：`normal`、`text`、`wait`、`appstarting`、`cross`、`help`、`hand`、`no`、`sizens`、`sizewe`、`sizenwse`、`sizenesw`、`sizeall`、`up`、`pin`、`person`。在编辑器中点击“恢复默认”后保存即可删除对应自定义样式。主程序会轮询 `Cursors.ini`，保存后自动热更新，无需重启。如果 PNG 编码器不可用，会自动回退保存为 BMP。

### 构建

最新功能在 `v1.2.0/` 目录中；根目录的 `build.bat` 对应 v1.0.0 源码。

#### 使用 build.bat

```bat
cd v1.2.0
build.bat
```

脚本会优先使用 `g++`（MinGW-w64），否则尝试 MSVC `cl`。

#### 使用 CMake

MinGW：

```bat
cmake -S v1.2.0 -B build-v120 -G "MinGW Makefiles"
cmake --build build-v120
```

Visual Studio：

```bat
cmake -S v1.2.0 -B build-v120
cmake --build build-v120 --config Release
```

输出为 `MultiCursor.exe`。v1.2.0 项目使用 C++17，依赖 `user32`、`gdi32`、`shell32`、`wtsapi32`、`gdiplus`、`comdlg32`、`ole32`。

#### 手动 MinGW 编译（v1.2.0）

```bat
cd v1.2.0
g++ -std=c++17 -O2 -municode -mwindows ^
    -DUNICODE -D_UNICODE -DOEMRESOURCE ^
    -finput-charset=UTF-8 -Wall -Wextra -Wno-cast-function-type ^
    -static -s -o MultiCursor.exe ^
    src\common.cpp src\cursor_image.cpp src\cursor_style.cpp ^
    src\overlay.cpp src\system_cursor.cpp src\editor.cpp ^
    src\app.cpp src\main.cpp ^
    -luser32 -lgdi32 -lshell32 -lwtsapi32 -lgdiplus -lcomdlg32 -lole32
```

### 实现原理

1. 使用 `RegisterRawInputDevices` 注册鼠标原始输入（Usage Page 0x01 / Usage 0x02），并开启 `RIDEV_INPUTSINK | RIDEV_DEVNOTIFY`，所以程序在后台也能收到输入和设备热插拔通知。
2. 用 `GetRawInputDeviceList` 枚举已有鼠标；用 `WM_INPUT_DEVICE_CHANGE` 处理新接入和移除。
3. 多光标模式下安装 `WH_MOUSE_LL` 低级鼠标钩子，吞掉物理鼠标事件；Raw Input 仍会到达程序，于是每个设备按自己的位置移动。
4. 用 `SetSystemCursor` 把所有系统光标替换成透明光标，避免系统指针和软件光标同时出现。
5. 每个鼠标对应一个 `WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST` 窗口，用 `UpdateLayeredWindow` 绘制箭头；图片来自内置系统光标快照或 `Cursors.ini` 自定义样式，并叠加颜色徽章。
6. 点击时先把隐藏的系统指针移动到当前软件光标位置，再用 `SendInput` 发送带标记的鼠标事件，使目标窗口收到正确的点击、拖拽和滚轮。
7. 退出时卸载钩子、释放按下的按键、销毁覆盖窗口，并用 `SystemParametersInfo(SPI_SETCURSORS)` 重新加载用户的系统光标方案。
8. 定时器比较 `GetCursorInfo().hCursor` 与启动时记录的系统光标句柄，识别当前光标状态（普通/文字/禁止/缩放等）并更新对应鼠标的覆盖图片；`Cursors.ini` 变化时自动热更新。
9. 全屏游戏模式检测无标题栏且覆盖整块显示器的前台窗口；用 `MOUSEEVENTF_MOVE` 发送带小数累积的相对移动，避免 Raw Input / DirectInput 游戏收不到 `SetCursorPos` 产生的绝对移动。
10. 窗口绑定模式下，每个鼠标的虚拟光标坐标会转换为目标窗口客户区坐标，并用 `PostMessageW` 发送 `WM_MOUSEMOVE`、按键和滚轮消息，实现多鼠标分别控制多个窗口；绑定会定期校验，窗口关闭后自动解除。

### 已知限制

- **管理员权限窗口**：Windows 的 UIPI 会阻止普通权限进程向管理员权限窗口发送合成输入。如果需要在任务管理器等管理员窗口中操作，请以管理员身份运行本程序。
- **UAC / 安全桌面**：UAC 提权提示可能位于另一个桌面，覆盖光标不会显示在那里。如果看到系统光标消失，请先按 `Ctrl+Alt+Q` 退出，或完成 UAC 操作后回到桌面。
- **锁屏 / 切换用户**：程序会监听会话锁定，在锁屏前恢复系统光标，解锁后重新隐藏；如果使用远程桌面或特殊安全策略，仍建议锁屏前按 `Ctrl+Alt+Q` 退出。
- **独占全屏游戏**：全屏游戏模式会把物理鼠标位移转成带标记的相对 `SendInput`，多数 Raw Input / DirectInput 游戏可以接收；但使用反作弊或完全绕过系统输入的游戏仍可能无法响应，覆盖光标也可能被独占全屏挡住。
- **窗口绑定**：绑定通过 `PostMessageW` 发送窗口消息实现，适用于处理 `WM_MOUSE*` 的窗口和部分窗口化游戏；使用 Raw Input / DirectInput 的游戏会忽略这些消息，请改用全屏游戏模式，或把该鼠标保持全局输入。
- **自定义光标状态识别**：状态识别依赖系统光标句柄；如果某个程序使用自有光标资源且无法匹配系统状态，覆盖层会保持普通箭头，但你仍可在编辑器中为系统状态设置自定义图片。
- **多接口鼠标**：部分鼠标会暴露多个 Raw Input 鼠标设备（例如多个 HID 集合），程序会把每个设备当作一个鼠标，可能显示多个光标。
- **指针速度设置**：程序直接使用 Raw Input，不经过 Windows 的“提高指针精确度”曲线；请通过 `sensitivity` / `acceleration` 调整手感。
- **杀毒软件**：低级鼠标钩子 + 合成输入可能被安全软件关注；如有拦截，请将程序加入信任列表。

### 安全退出

- 按 `Ctrl+Alt+Q` 退出并恢复系统光标。
- 托盘菜单选择“退出并恢复系统光标”。
- 如果主进程崩溃或被任务管理器结束，随程序启动的看门狗进程会等待主进程结束，然后强制重新加载系统光标方案。
- 如果光标意外消失，可以注销/重新登录，或运行一次 `MultiCursor.exe --off` 后再退出；看门狗也会在进程结束时恢复。

### 文件结构

```text
src/common.h/.cpp        常量、路径、配置、日志、坐标工具
src/cursor_image.h/.cpp  箭头位图光栅化、DIB/图标
src/cursor_style.h/.cpp  光标状态、图片导入、Cursors.ini 存储
src/overlay.h/.cpp       分层窗口光标
src/system_cursor.h/.cpp 系统光标隐藏/恢复、看门狗
src/editor.h/.cpp        光标样式编辑器窗口
src/app.h/.cpp           设备管理、模式切换、钩子、Raw Input、托盘
src/main.cpp             入口、命令行、DPI、单实例
CMakeLists.txt           CMake 构建
build.bat                MinGW/MSVC 一键构建
```

## English

A small Windows 10/11 C++ utility that gives every connected mouse its own on-screen cursor. Once a second mouse is connected, the two mice no longer share one system pointer; each moves its own colored arrow cursor. No driver is required — the program uses Win32 Raw Input, a low-level mouse hook, and layered windows.

### Download

Download the latest `MultiCursor.exe` from [GitHub Releases](https://github.com/freezefish-111/MoreMouseInOneWindows/releases).

### Features

- Automatically detects Raw Input mice and supports hot-plug.
- Auto mode: enables multi-cursor when at least two mice are detected; stays in normal single-cursor mode with one mouse.
- One topmost, click-through cursor window per mouse. Cursor colors are black, blue, green, orange, purple, red, and so on.
- Independent movement, left/right/middle buttons, side buttons, vertical wheel, and horizontal wheel for each mouse.
- Hides the shared system cursor by replacing system cursor resources (not `ShowCursor`), and restores them on exit or crash.
- Tray menu; `Ctrl+Alt+M` toggles multi-cursor mode, `Ctrl+Alt+Q` exits and restores the system cursor.
- Single-instance operation; a watchdog restores the system cursor if the main process exits abnormally.
- Preview and diagnose modes for checking the effect without a second mouse.
- **Custom cursor styles**: a built-in style editor lets you set a different image for each of 16 cursor states (normal/text/wait/cross/hand/no/resize/move, etc.).
- Import local `PNG/JPG/GIF/BMP` images, or draw/erase directly in the editor; click the preview to set the hotspot.
- Styles are stored in `Cursors.ini` and the `Cursors\` folder; the running app detects changes and hot-reloads them.
- Every virtual cursor keeps a color badge, so mice remain distinguishable even with custom images.
- **Fullscreen game mode**: detects borderless fullscreen windows and switches to relative mouse movement (`MOUSEEVENTF_MOVE`) so Raw Input / DirectInput games receive mouse motion. Toggle auto/forced/off with `Ctrl+Alt+G`.
- **Window binding**: the tray menu can bind each mouse to its own window and deliver movement, buttons, and wheel events as `WM_MOUSE*` messages, enabling different mice to control different windowed games.
- Bug fixes: editor preview checkerboard offset, save-message wording, BMP fallback when PNG encoding fails, and cursor-state misdetection in game/bound modes.

### Usage

Double-click `MultiCursor.exe`. The default mode is auto: multi-cursor is enabled when two mice are detected.

It is a good idea to run the preview first to check the cursor styles:

```bat
MultiCursor.exe --preview
```

The preview shows three sample cursors and exits after 8 seconds without taking over mouse input.

Open the cursor style editor (a separate process, so it works even while the main app is running):

```bat
MultiCursor.exe --create
```

You can also choose “Edit cursor styles...” from the tray menu.

List the detected mouse devices:

```bat
MultiCursor.exe --diagnose
```

Diagnostic information is written to:

```text
%LOCALAPPDATA%\MultiCursor\multicursor.log
```

#### Command-line options

```text
MultiCursor.exe [options]

  --mode auto|on|off   cursor mode (default: auto)
  --multi              same as --mode on
  --off                same as --mode off
  --preview            preview 3 sample cursors, exit after 8 seconds
  --diagnose           list Raw Input devices and exit
  --create             open the cursor style editor (separate process)
  --help               show help
```

#### Tray menu

- Auto mode (enabled when 2 or more mice are detected)
- Always enable multi-cursor
- Disable multi-cursor (normal mouse)
- Fullscreen game mode: auto detect / always on / off (`Ctrl+Alt+G` cycles)
- Show device information
- Open log file
- Edit cursor styles...
- Window binding (different mice control different windows)...
- Exit and restore system cursor

### Configuration

On startup the app reads `MultiCursor.ini` next to the executable. Missing files use defaults.

```ini
[MultiCursor]
sensitivity=0.80
acceleration=0.90
mode=auto
gameMode=auto
```

- `sensitivity`: multiplier from raw mouse counts to screen pixels, range 0.10 - 5.00. Default 0.80.
- `acceleration`: extra gain at high speed, range 0.00 - 3.00. Default 0.90.
- `mode`: `auto`, `on`, or `off`. Command-line arguments override the config file.
- `gameMode`: `auto` (relative input when a borderless fullscreen window is detected), `on` (always relative input), or `off` (always absolute positioning). Default `auto`.

If the cursor moves too fast, lower `sensitivity`; if too slow, raise it.

#### Custom cursor styles

Run `MultiCursor.exe --create` (or choose “Edit cursor styles...” from the tray menu) to open the style editor. You can import images, draw, and set hotspots for 16 states. Saving writes `Cursors.ini` and the `Cursors\` folder next to the executable:

```ini
[normal]
image=Cursors\normal.png
hotspotX=8
hotspotY=4
custom=1
```

State keys: `normal`, `text`, `wait`, `appstarting`, `cross`, `help`, `hand`, `no`, `sizens`, `sizewe`, `sizenwse`, `sizenesw`, `sizeall`, `up`, `pin`, `person`. Click “Restore default” and save to remove a custom style. The main app polls `Cursors.ini` and hot-reloads saved changes without a restart. If the PNG encoder is unavailable, the editor falls back to BMP.

### Build

The latest features are in `v1.2.0/`; the root-level `build.bat` builds the historical v1.0.0 source.

#### Using build.bat

```bat
cd v1.2.0
build.bat
```

The script prefers `g++` (MinGW-w64) and falls back to MSVC `cl`.

#### Using CMake

MinGW:

```bat
cmake -S v1.2.0 -B build-v120 -G "MinGW Makefiles"
cmake --build build-v120
```

Visual Studio:

```bat
cmake -S v1.2.0 -B build-v120
cmake --build build-v120 --config Release
```

The output is `MultiCursor.exe`. v1.2.0 uses C++17 and depends on `user32`, `gdi32`, `shell32`, `wtsapi32`, `gdiplus`, `comdlg32`, and `ole32`.

#### Manual MinGW build (v1.2.0)

```bat
cd v1.2.0
g++ -std=c++17 -O2 -municode -mwindows ^
    -DUNICODE -D_UNICODE -DOEMRESOURCE ^
    -finput-charset=UTF-8 -Wall -Wextra -Wno-cast-function-type ^
    -static -s -o MultiCursor.exe ^
    src\common.cpp src\cursor_image.cpp src\cursor_style.cpp ^
    src\overlay.cpp src\system_cursor.cpp src\editor.cpp ^
    src\app.cpp src\main.cpp ^
    -luser32 -lgdi32 -lshell32 -lwtsapi32 -lgdiplus -lcomdlg32 -lole32
```

### How it works

1. `RegisterRawInputDevices` registers raw mouse input (Usage Page 0x01 / Usage 0x02) with `RIDEV_INPUTSINK | RIDEV_DEVNOTIFY`, so input and device changes are received even in the background.
2. `GetRawInputDeviceList` enumerates existing mice; `WM_INPUT_DEVICE_CHANGE` handles arrival and removal.
3. In multi-cursor mode a `WH_MOUSE_LL` hook swallows physical mouse messages; Raw Input still reaches the app, so each device moves its own software cursor.
4. `SetSystemCursor` replaces all system cursors with a transparent cursor so the real pointer does not appear alongside the software cursors.
5. Each mouse gets a `WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST` window drawn with `UpdateLayeredWindow`; images come from a startup system-cursor snapshot or `Cursors.ini` custom styles, plus a color badge.
6. On click, the hidden system pointer is moved to the active software cursor, then a tagged `SendInput` event is sent so the target window receives correct clicks, drags, and wheel input.
7. On exit the hook is removed, held buttons are released, overlay windows are destroyed, and `SystemParametersInfo(SPI_SETCURSORS)` reloads the user's cursor scheme.
8. A timer compares `GetCursorInfo().hCursor` with the startup cursor handles to detect cursor state (normal/text/no/resize, etc.) and updates the corresponding overlay; `Cursors.ini` changes are hot-reloaded.
9. Fullscreen game mode detects a borderless foreground window covering a whole monitor and sends relative `MOUSEEVENTF_MOVE` with fractional accumulation, avoiding games that ignore absolute `SetCursorPos` motion.
10. In window-binding mode, each mouse's virtual screen position is converted to the target window's client coordinates and sent as `WM_MOUSEMOVE`, button, and wheel messages via `PostMessageW`. Bindings are validated periodically and cleared when the window closes.

### Known limitations

- **Elevated windows**: UIPI prevents a normal-privilege process from sending synthetic input to elevated windows. Run MultiCursor as administrator to interact with windows such as Task Manager.
- **UAC / secure desktop**: UAC prompts live on another desktop, where overlays are not shown. If the system cursor disappears, press `Ctrl+Alt+Q` to exit or complete the UAC operation and return to the desktop.
- **Lock / switch user**: the app listens for session lock, restores the system cursor before locking, and hides it again after unlock. With remote desktop or special policies, press `Ctrl+Alt+Q` before locking.
- **Exclusive fullscreen games**: fullscreen game mode converts physical motion into tagged relative `SendInput`, which most Raw Input / DirectInput games accept. Games with anti-cheat or fully custom input may still ignore it, and overlays may be hidden by exclusive fullscreen.
- **Window binding**: binding uses `PostMessageW` window messages and works with windows that handle `WM_MOUSE*` (and some windowed games). Raw Input / DirectInput games ignore these messages; use fullscreen game mode or keep that mouse in global mode.
- **Custom cursor state detection**: detection relies on system cursor handles. If an app uses its own cursor resource that cannot be matched to a system state, the overlay stays normal; you can still set custom images for system states in the editor.
- **Multi-interface mice**: some mice expose multiple Raw Input mouse devices (for example multiple HID collections); each is treated as a separate mouse and may show an extra cursor.
- **Pointer speed**: the app reads Raw Input directly and does not use Windows “Enhance pointer precision”; tune the feel with `sensitivity` / `acceleration`.
- **Antivirus**: low-level hooks plus synthetic input may be flagged by security software; if blocked, add the program to the trust list.

### Safe exit

- Press `Ctrl+Alt+Q` to exit and restore the system cursor.
- Choose “Exit and restore system cursor” from the tray menu.
- If the main process crashes or is killed, the watchdog waits for it to exit and force-reloads the system cursor scheme.
- If the cursor disappears unexpectedly, log off/log on, or run `MultiCursor.exe --off` once and exit; the watchdog also restores on process end.

### Project structure

```text
src/common.h/.cpp        constants, paths, config, logging, geometry
src/cursor_image.h/.cpp  arrow rasterization, DIB/icon helpers
src/cursor_style.h/.cpp  cursor states, image import, Cursors.ini storage
src/overlay.h/.cpp       layered-window cursor overlays
src/system_cursor.h/.cpp system cursor hiding/restoring, watchdog
src/editor.h/.cpp        cursor style editor window
src/app.h/.cpp           device tracking, mode switching, hook, Raw Input, tray
src/main.cpp             entry point, CLI, DPI, single instance
CMakeLists.txt           CMake build
build.bat                one-click MinGW/MSVC build
```
