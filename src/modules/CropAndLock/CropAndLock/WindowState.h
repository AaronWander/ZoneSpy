#pragma once
#include <string>
#include <vector>
#include <optional>
#include "SettingsWindow.h"

struct WindowState
{
    // Window information
    std::wstring title;
    HWND targetWindow = nullptr;
    CropAndLockType windowType = CropAndLockType::Thumbnail;

    // Window position and size
    RECT windowRect = {};

    // Original crop rectangle (before DWM adjustment, in screen space)
    RECT originalCropRect = {};

    // Window properties
    bool isTopMost = true;

    // Target window identification (for restoration)
    std::wstring targetWindowTitle;
    std::wstring targetWindowClass;
    std::wstring targetProcessPath;  // Full path to exe
    std::wstring targetProcessName;  // Just exe name
    DWORD targetProcessId = 0;  // Hint only, not reliable across restarts
};

class WindowStateManager
{
public:
    static WindowStateManager& Instance();

    // Save/Load all window states
    void SaveStates(const std::vector<WindowState>& states);
    std::vector<WindowState> LoadStates();

    // Get the state file path
    std::wstring GetStateFilePath();

private:
    WindowStateManager() = default;
    std::wstring GetStateFolderPath();
};
