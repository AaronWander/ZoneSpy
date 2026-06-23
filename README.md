# ZoneSpy

A standalone window cropping utility, forked from the [CropAndLock](https://github.com/microsoft/PowerToys/tree/main/src/modules/CropAndLock) module of [Microsoft PowerToys](https://github.com/microsoft/PowerToys).

Unlike the original PowerToys module, ZoneSpy runs independently—no PowerToys installation required.

## What's different from PowerToys CropAndLock

| Feature | PowerToys CropAndLock | ZoneSpy |
|---------|----------------------|---------|
| Runtime | Requires PowerToys runner | Standalone executable |
| Borderless windows | ❌ | ✓ Draggable floating windows, no title bar, edge snapping |
| Aspect-ratio lock | ❌ | ✓ Hold `Shift` while resizing to maintain content ratio |
| Click-Through | ❌ | ✓ Toggle from tray menu, clicks pass through to windows behind |
| Auto-reconnect | ❌ | ✓ Reconnects when source window is closed and reopened |
| Window snapping | ❌ | ✓ Snap to screen edges and other ZoneSpy windows |
| System tray | Managed by PowerToys | ✓ Built-in tray icon with Exit and Click-Through toggle |
| Hotkey management | Managed by PowerToys | ✓ Self-registered via `RegisterHotKey` |
| Window state persistence | ✓ | ✓ Both save/restore across sessions |

## Features

- **Window cropping**: Select any region of any window and display it in an always-on-top floating window with live DWM thumbnail content
- **Three modes**:
  - **Thumbnail**: Floating window with live DWM thumbnail rendering
  - **Reparent**: Embeds the target window into a custom frame
  - **Screenshot**: Captures and freezes the selected region
- **Borderless & snapping**: Cropped windows have no title bar—drag and snap to screen edges or other ZoneSpy windows
- **Aspect-ratio lock**: Hold `Shift` while resizing to preserve the original content aspect ratio
- **Auto-reconnect**: When the source window is closed and reopened, the cropped window automatically reconnects within seconds
- **Click-Through**: Toggle from the tray menu to make mouse clicks pass through the cropped window—ideal for monitoring content underneath
- **Always on top**: Cropped windows stay above all other windows by default (toggleable per window: right-click → "Always on top")

## Hotkeys

| Hotkey | Mode |
|--------|------|
| Win+Ctrl+Shift+T | Thumbnail crop |
| Win+Ctrl+Shift+R | Reparent mode |
| Win+Ctrl+Shift+S | Screenshot mode |

Press a hotkey while the target window is in focus, then click and drag to select the region.

## Building

Requires Visual Studio 2022+ (or 2026) and Windows 10 SDK.

```powershell
.\tools\build\build.ps1 -Path src\modules\CropAndLock\CropAndLock -Platform x64 -Configuration Release
```

Pre-built binaries are available under the [Actions](https://github.com/AaronWander/ZoneSpy/actions) tab.

## License

[MIT](LICENSE)

ZoneSpy is based on the [CropAndLock](https://github.com/microsoft/PowerToys/tree/main/src/modules/CropAndLock) module from [Microsoft PowerToys](https://github.com/microsoft/PowerToys), licensed under the MIT License.
