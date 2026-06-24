# ZoneSpy

A standalone window thumbnail streaming utility, forked from the [CropAndLock](https://github.com/microsoft/PowerToys/tree/main/src/modules/CropAndLock) module of [Microsoft PowerToys](https://github.com/microsoft/PowerToys).

Unlike the original PowerToys module, ZoneSpy runs independently—no PowerToys installation required. It focuses exclusively on the **thumbnail streaming** mode with real-time DWM content projection and shared memory output for external AI/vision programs.

## What's different from PowerToys CropAndLock

| Feature | PowerToys CropAndLock | ZoneSpy |
|---------|----------------------|---------|
| Runtime | Requires PowerToys runner | Standalone executable |
| Modes | Thumbnail, Reparent, Screenshot | **Thumbnail only** (simplified) |
| Shared memory streaming | ❌ | ✓ Real-time frame data via `ZoneSpy_FrameData_{id}` |
| Python viewer | ❌ | ✓ `tools/stream-viewer.py` — display all feeds in one window |
| Borderless windows | ❌ | ✓ Draggable floating windows, no title bar, edge snapping |
| Aspect-ratio lock | ❌ | ✓ Hold `Shift` while resizing to maintain content ratio |
| Click-Through | ❌ | ✓ Toggle from tray menu, clicks pass through to windows behind |
| Auto-reconnect | ❌ | ✓ Reconnects when source window is closed and reopened |
| Window snapping | ❌ | ✓ Snap to screen edges and other ZoneSpy windows |
| System tray | Managed by PowerToys | ✓ Built-in tray icon with Exit, Click-Through and auto-start |
| Auto-start | ❌ | ✓ Launch at startup via tray menu |
| Hotkey management | Managed by PowerToys | ✓ Self-registered via `RegisterHotKey` |

## Features

- **Live window thumbnail**: Select any region of any window and display it in an always-on-top floating window with real-time DWM content projection
- **Shared memory streaming** (`ZoneSpy_FrameData_{id}`): Each thumbnail window exposes its content as BGRA pixel data in a named shared memory buffer at ~15 fps, with a frame-ready event (`ZoneSpy_FrameReady_{id}`) for synchronization
- **Python stream viewer**: `tools/stream-viewer.py` connects to all active streams and displays them in a single OpenCV window
- **Borderless & snapping**: Cropped windows have no title bar—drag and snap to screen edges or other ZoneSpy windows
- **Aspect-ratio lock**: Hold `Shift` while resizing to preserve the original content aspect ratio
- **Auto-reconnect**: When the source window is closed and reopened, the cropped window automatically reconnects within seconds
- **Click-Through**: Toggle from the tray menu to make mouse clicks pass through the cropped window—ideal for monitoring content underneath
- **Always on top**: Cropped windows stay above all other windows by default (toggleable per window: right-click → "Always on top")
- **Auto-start**: Enable from tray menu to launch at Windows boot
- **Window state persistence**: Cropped window positions are saved and restored across sessions

## Shared Memory API

Each cropped window creates a named shared memory and a frame-ready event. Stream information is written to `%LocalAppData%\ZoneSpy\streams.json`.

```json
{
  "streams": [
    {
      "id": 0,
      "name": "Untitled - Notepad (Cropped)",
      "width": 800, "height": 600,
      "shm": "ZoneSpy_FrameData_0",
      "evt": "ZoneSpy_FrameReady_0"
    }
  ]
}
```

**FrameHeader** (20 bytes, packed):
| Offset | Type | Field |
|--------|------|-------|
| 0 | uint32 | frameCount |
| 4 | uint64 | timestamp (ms) |
| 12 | uint32 | width |
| 16 | uint32 | height |
| 20+ | BGRA pixels | width × height × 4 bytes |

Python consumption example:

```python
import mmap, struct, numpy as np, win32event

buf = mmap.mmap(-1, 3840*2160*4+20, tagname="ZoneSpy_FrameData_0")
evt = win32event.OpenEvent(win32event.SYNCHRONIZE, False, "ZoneSpy_FrameReady_0")

while True:
    win32event.WaitForSingleObject(evt, win32event.INFINITE)
    buf.seek(0)
    fc, ts, w, h = struct.unpack_from("<I Q I I", buf, 0)
    pixels = np.frombuffer(buf.read(w * h * 4), dtype=np.uint8).reshape(h, w, 4)
    # pixels is BGRA, ready for model inference
```

See [tools/stream-viewer.py](tools/stream-viewer.py) for a complete working example.

## Hotkey

| Hotkey | Mode |
|--------|------|
| Win+Ctrl+Shift+T | Thumbnail crop |

Press the hotkey while the target window is in focus, then click and drag to select the region.

## Tray Menu

Right-click the tray icon:
- **Click-Through** — toggle mouse passthrough
- **Launch at startup** — auto-start with Windows
- **Exit** — quit ZoneSpy

## Building

Requires Visual Studio 2022+ (or 2026) and Windows 10 SDK.

```powershell
.\tools\build\build.ps1 -Path src\modules\CropAndLock\CropAndLock -Platform x64 -Configuration Release
```

Pre-built binaries are available under the [Actions](https://github.com/AaronWander/ZoneSpy/actions) tab.

## License

[MIT](LICENSE)

ZoneSpy is based on the [CropAndLock](https://github.com/microsoft/PowerToys/tree/main/src/modules/CropAndLock) module from [Microsoft PowerToys](https://github.com/microsoft/PowerToys), licensed under the MIT License.
