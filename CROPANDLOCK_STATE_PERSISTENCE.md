# CropAndLock 状态持久化功能

## 功能概述

为 CropAndLock 模块添加了状态持久化功能，使得窗口在关闭后重新打开时能够记住：
- 窗口位置和大小
- 裁剪区域（源矩形）
- 窗口属性（是否置顶）
- 目标窗口信息

## 实现细节

### 新增文件

1. **WindowState.h** - 窗口状态管理头文件
   - 定义 `WindowState` 结构体，保存窗口的完整状态
   - 定义 `WindowStateManager` 单例类，管理状态的保存和加载

2. **WindowState.cpp** - 窗口状态管理实现
   - 使用 JSON 格式保存窗口状态到本地文件
   - 文件位置：`%LocalAppData%\Microsoft\PowerToys\CropAndLock\window-states.json`
   - 支持保存多个窗口的状态

### 修改的文件

1. **ThumbnailCropAndLockWindow.h**
   - 添加 `GetState()` 方法：导出当前窗口状态
   - 添加 `RestoreState()` 方法：从保存的状态恢复窗口

2. **ThumbnailCropAndLockWindow.cpp**
   - 实现状态导出功能，包括窗口位置、大小、裁剪区域
   - 实现状态恢复功能，尝试找到原始目标窗口并重新建立连接

3. **main.cpp**
   - 添加 `SaveAllWindowStates()` 函数：保存所有窗口状态
   - 添加 `RestorePreviousWindowStates()` 函数：启动时恢复之前的窗口
   - 在窗口创建、关闭和程序退出时自动保存状态
   - 在程序启动时自动恢复上次的窗口状态

4. **CropAndLock.vcxproj**
   - 添加 WindowState.cpp 和 WindowState.h 到项目编译列表

## 工作流程

### 保存状态
1. 当用户创建新的 CropAndLock 窗口时 → 自动保存
2. 当用户关闭某个窗口时 → 自动保存
3. 当程序退出时 → 保存所有窗口状态

### 恢复状态
1. 程序启动时自动读取 `window-states.json`
2. 对于每个保存的窗口状态：
   - 尝试通过进程 ID、窗口标题和窗口类名查找原始目标窗口
   - 如果找到目标窗口，恢复窗口位置、大小和裁剪区域
   - 如果找不到目标窗口，记录警告并跳过

## 数据格式

JSON 文件格式示例：
```json
{
  "version": 1.0,
  "windows": [
    {
      "title": "Chrome (Cropped)",
      "type": 1,
      "isTopMost": true,
      "windowRect": {
        "left": 100,
        "top": 100,
        "right": 900,
        "bottom": 700
      },
      "sourceRect": {
        "left": 0,
        "top": 50,
        "right": 800,
        "bottom": 650
      },
      "targetWindowTitle": "Chrome",
      "targetWindowClass": "Chrome_WidgetWin_1",
      "targetProcessId": 12345
    }
  ]
}
```

## 注意事项

1. **目标窗口匹配**：恢复时通过进程 ID、标题和类名匹配目标窗口，如果目标程序已关闭则无法恢复
2. **目前仅支持 Thumbnail 类型**：Reparent 和 Screenshot 类型的窗口暂未实现状态恢复
3. **自动保存**：每次窗口变化时都会保存，确保状态始终是最新的
4. **错误处理**：如果加载或保存失败，会记录错误日志但不影响程序正常运行

## 测试建议

1. 创建多个 Thumbnail 窗口，调整位置和大小
2. 关闭 CropAndLock 程序
3. 重新启动 CropAndLock
4. 验证窗口是否恢复到之前的位置和大小
5. 检查 `%LocalAppData%\Microsoft\PowerToys\CropAndLock\window-states.json` 文件内容

## 未来改进

1. 支持 Reparent 和 Screenshot 类型窗口的状态持久化
2. 添加设置选项，允许用户禁用自动恢复功能
3. 支持保存窗口的不透明度等其他属性
4. 优化目标窗口匹配算法，提高恢复成功率
