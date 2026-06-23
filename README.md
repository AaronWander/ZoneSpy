# ZoneSpy

A standalone window cropping utility forked from the [CropAndLock](https://github.com/microsoft/PowerToys/tree/main/src/modules/CropAndLock) module of [Microsoft PowerToys](https://github.com/microsoft/PowerToys).

## Features

- **Window cropping**: Select any region of any window and display it in a always-on-top floating window with live content
- **Three modes**:
  - **Thumbnail**: Floating window with live DWM thumbnail rendering
  - **Reparent**: Embeds the target window into a custom frame
  - **Screenshot**: Captures and freezes the selected region
- **Aspect-ratio lock**: Hold `Shift` while resizing to maintain the original aspect ratio
- **Auto-reconnect**: When the source window is closed and reopened, the cropped window automatically reconnects
- **Click-Through**: Toggle from the tray menu to make mouse clicks pass through the cropped window
- **Hotkeys**: Default `Win+Ctrl+Shift+T` (thumbnail) / `R` (reparent) / `S` (screenshot)

## Usage

Run `PowerToys.CropAndLock.exe`. The app sits in the system tray. Right-click the tray icon to exit or toggle Click-Through.

Press a hotkey on any window to start cropping:

| Hotkey | Mode |
|--------|------|
| Win+Ctrl+Shift+T | Thumbnail crop |
| Win+Ctrl+Shift+R | Reparent mode |
| Win+Ctrl+Shift+S | Screenshot mode |

## Building

Requires Visual Studio 2022+ and Windows 10 SDK.

```powershell
.\tools\build\build.ps1 -Path src\modules\CropAndLock\CropAndLock -Platform x64 -Configuration Release
```

Pre-built binaries are available from [Actions](https://github.com/AaronWander/ZoneSpy/actions).

## License

[MIT](LICENSE)

ZoneSpy is based on the [CropAndLock](https://github.com/microsoft/PowerToys/tree/main/src/modules/CropAndLock) module from [Microsoft PowerToys](https://github.com/microsoft/PowerToys), licensed under the MIT License.
