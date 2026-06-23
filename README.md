# ZoneSpy

独立裁剪窗口工具，从 [Microsoft PowerToys](https://github.com/microsoft/PowerToys) 的 CropAndLock 模块独立而来。

## 功能

- **窗口裁剪**：选取任意窗口的任意区域，生成一个始终保持在最前的小窗口，实时显示该区域内容
- **三种模式**：
  - 缩略图（Thumbnail）：浮动小窗口，实时投射
  - 嵌入（Reparent）：将目标窗口嵌入自己创建的窗口
  - 截图（Screenshot）：截取目标窗口区域并固定显示
- **等比例缩放**：拖拽窗口边缘时按住 `Shift` 保持宽高比
- **自动重连**：源窗口关闭后重开，裁剪窗口自动重新连接
- **Click-Through**：托盘菜单开启后，鼠标可以穿透裁剪窗口操作底层窗口
- **热键操作**：默认 `Win+Ctrl+Shift+T` 缩略图 / `R` 嵌入 / `S` 截图

## 使用

解压后运行 `PowerToys.CropAndLock.exe`，系统托盘会出现图标。右键托盘可选择退出或开启 Click-Through。

热键直接在任意窗口上按：

| 热键 | 模式 |
|------|------|
| Win+Ctrl+Shift+T | 缩略图裁剪 |
| Win+Ctrl+Shift+R | 嵌入模式 |
| Win+Ctrl+Shift+S | 截图模式 |

## 构建

需要 Visual Studio 2022+ 和 Windows 10 SDK。

```powershell
.\\tools\\build\\build.ps1 -Path src\\modules\\CropAndLock\\CropAndLock -Platform x64 -Configuration Release
```

GitHub Actions 自动构建产物在 [Actions](https://github.com/AaronWander/ZoneSpy/actions) 页面下载。

## License

[MIT](LICENSE)

ZoneSpy is based on the [CropAndLock](https://github.com/microsoft/PowerToys/tree/main/src/modules/CropAndLock) module from [Microsoft PowerToys](https://github.com/microsoft/PowerToys), licensed under the MIT License.
