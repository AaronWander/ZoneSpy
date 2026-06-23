#include "pch.h"
#include "WindowState.h"
#include "ModuleConstants.h"
#include <common/utils/json.h>
#include <common/logger/logger.h>
#include <shlobj.h>
#include <winrt/Windows.Foundation.h>

WindowStateManager& WindowStateManager::Instance()
{
    static WindowStateManager instance;
    return instance;
}

std::wstring WindowStateManager::GetStateFolderPath()
{
    wchar_t* localAppDataPath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppDataPath)))
    {
        std::wstring path = localAppDataPath;
        CoTaskMemFree(localAppDataPath);
        path += L"\\Microsoft\\PowerToys\\CropAndLock";

        // Create directory recursively if it doesn't exist
        std::wstring currentPath;
        size_t pos = 0;
        while (pos < path.length())
        {
            pos = path.find_first_of(L'\\', pos + 1);
            if (pos == std::wstring::npos)
            {
                pos = path.length();
            }
            currentPath = path.substr(0, pos);
            CreateDirectoryW(currentPath.c_str(), nullptr);
        }

        return path;
    }
    return L"";
}

std::wstring WindowStateManager::GetStateFilePath()
{
    auto folder = GetStateFolderPath();
    if (!folder.empty())
    {
        return folder + L"\\window-states.json";
    }
    return L"";
}

void WindowStateManager::SaveStates(const std::vector<WindowState>& states)
{
    try
    {
        auto filePath = GetStateFilePath();
        if (filePath.empty())
        {
            Logger::error(L"Failed to get state file path");
            return;
        }

        // Always save, even if states is empty (to clear old state)
        json::JsonObject root;
        json::JsonArray windowsArray;

        for (const auto& state : states)
        {
            json::JsonObject windowObj;

            // Basic info
            windowObj.SetNamedValue(L"title", json::value(state.title));
            windowObj.SetNamedValue(L"type", json::value(static_cast<int>(state.windowType)));
            windowObj.SetNamedValue(L"isTopMost", json::value(state.isTopMost));

            // Window rect
            json::JsonObject windowRectObj;
            windowRectObj.SetNamedValue(L"left", json::value(state.windowRect.left));
            windowRectObj.SetNamedValue(L"top", json::value(state.windowRect.top));
            windowRectObj.SetNamedValue(L"right", json::value(state.windowRect.right));
            windowRectObj.SetNamedValue(L"bottom", json::value(state.windowRect.bottom));
            windowObj.SetNamedValue(L"windowRect", windowRectObj);

            // Original crop rect (before DWM adjustment)
            json::JsonObject cropRectObj;
            cropRectObj.SetNamedValue(L"left", json::value(state.originalCropRect.left));
            cropRectObj.SetNamedValue(L"top", json::value(state.originalCropRect.top));
            cropRectObj.SetNamedValue(L"right", json::value(state.originalCropRect.right));
            cropRectObj.SetNamedValue(L"bottom", json::value(state.originalCropRect.bottom));
            windowObj.SetNamedValue(L"originalCropRect", cropRectObj);

            // Target window info
            windowObj.SetNamedValue(L"targetWindowTitle", json::value(state.targetWindowTitle));
            windowObj.SetNamedValue(L"targetWindowClass", json::value(state.targetWindowClass));
            windowObj.SetNamedValue(L"targetProcessPath", json::value(state.targetProcessPath));
            windowObj.SetNamedValue(L"targetProcessName", json::value(state.targetProcessName));
            windowObj.SetNamedValue(L"targetProcessId", json::value(static_cast<double>(state.targetProcessId)));

            windowsArray.Append(windowObj);
        }

        root.SetNamedValue(L"windows", windowsArray);
        root.SetNamedValue(L"version", json::value(1.0));

        json::to_file(filePath, root);
        Logger::trace(L"Saved {} window states to {}", states.size(), filePath);
    }
    catch (const std::exception& ex)
    {
        Logger::error(L"Failed to save window states: {}", winrt::to_hstring(ex.what()));
    }
    catch (...)
    {
        Logger::error(L"Failed to save window states: unknown error");
    }
}

std::vector<WindowState> WindowStateManager::LoadStates()
{
    std::vector<WindowState> states;

    try
    {
        auto filePath = GetStateFilePath();
        if (filePath.empty())
        {
            Logger::error(L"Failed to get state file path");
            return states;
        }

        auto rootOpt = json::from_file(filePath);
        if (!rootOpt.has_value())
        {
            Logger::info(L"No saved window states found");
            return states;
        }

        auto root = rootOpt.value();
        if (!root.HasKey(L"windows"))
        {
            Logger::warn(L"Invalid state file format");
            return states;
        }

        auto windowsArray = root.GetNamedArray(L"windows");
        for (uint32_t i = 0; i < windowsArray.Size(); i++)
        {
            try
            {
                auto windowObj = windowsArray.GetObjectAt(i);
                WindowState state;

                // Basic info
                json::get(windowObj, L"title", state.title, std::wstring(L""));
                int typeInt = 0;
                json::get(windowObj, L"type", typeInt, 1);
                state.windowType = static_cast<CropAndLockType>(typeInt);
                json::get(windowObj, L"isTopMost", state.isTopMost, true);

                // Window rect
                if (windowObj.HasKey(L"windowRect"))
                {
                    auto windowRectObj = windowObj.GetNamedObject(L"windowRect");
                    json::get(windowRectObj, L"left", state.windowRect.left, 0L);
                    json::get(windowRectObj, L"top", state.windowRect.top, 0L);
                    json::get(windowRectObj, L"right", state.windowRect.right, 800L);
                    json::get(windowRectObj, L"bottom", state.windowRect.bottom, 600L);
                }

                // Original crop rect (before DWM adjustment)
                if (windowObj.HasKey(L"originalCropRect"))
                {
                    auto cropRectObj = windowObj.GetNamedObject(L"originalCropRect");
                    json::get(cropRectObj, L"left", state.originalCropRect.left, 0L);
                    json::get(cropRectObj, L"top", state.originalCropRect.top, 0L);
                    json::get(cropRectObj, L"right", state.originalCropRect.right, 800L);
                    json::get(cropRectObj, L"bottom", state.originalCropRect.bottom, 600L);
                }
                // Backward compatibility: try sourceRect if originalCropRect doesn't exist
                else if (windowObj.HasKey(L"sourceRect"))
                {
                    auto sourceRectObj = windowObj.GetNamedObject(L"sourceRect");
                    json::get(sourceRectObj, L"left", state.originalCropRect.left, 0L);
                    json::get(sourceRectObj, L"top", state.originalCropRect.top, 0L);
                    json::get(sourceRectObj, L"right", state.originalCropRect.right, 800L);
                    json::get(sourceRectObj, L"bottom", state.originalCropRect.bottom, 600L);
                }

                // Target window info
                json::get(windowObj, L"targetWindowTitle", state.targetWindowTitle, std::wstring(L""));
                json::get(windowObj, L"targetWindowClass", state.targetWindowClass, std::wstring(L""));
                json::get(windowObj, L"targetProcessPath", state.targetProcessPath, std::wstring(L""));
                json::get(windowObj, L"targetProcessName", state.targetProcessName, std::wstring(L""));
                double pidDouble = 0;
                json::get(windowObj, L"targetProcessId", pidDouble, 0.0);
                state.targetProcessId = static_cast<DWORD>(pidDouble);

                states.push_back(state);
            }
            catch (...)
            {
                Logger::warn(L"Failed to parse window state at index {}", i);
            }
        }

        Logger::trace(L"Loaded {} window states from {}", states.size(), filePath);
    }
    catch (const std::exception& ex)
    {
        Logger::error(L"Failed to load window states: {}", winrt::to_hstring(ex.what()));
    }
    catch (...)
    {
        Logger::error(L"Failed to load window states: unknown error");
    }

    return states;
}
