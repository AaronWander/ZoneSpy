#include "pch.h"
#include <shlobj.h>
#include <common/utils/json.h>
#include <fstream>
#include "SettingsWindow.h"
#include "OverlayWindow.h"
#include "CropAndLockWindow.h"
#include "ScreenshotCropAndLockWindow.h"
#include "ThumbnailCropAndLockWindow.h"
#include "ReparentCropAndLockWindow.h"
#include "ModuleConstants.h"
#include "WindowState.h"
#include "trace.h"

#include <common/utils/logger_helper.h>
#include <common/utils/UnhandledExceptionHandler.h>
#include <common/utils/winapi_error.h>

#include <common/Telemetry/EtwTrace/EtwTrace.h>

#include <common/Themes/theme_helpers.h>
#include <common/Themes/theme_listener.h>

#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace winrt
{
    using namespace Windows::Foundation;
    using namespace Windows::Foundation::Numerics;
    using namespace Windows::UI;
    using namespace Windows::UI::Composition;
}

namespace util
{
    using namespace robmikh::common::desktop;
}

const std::wstring instanceMutexName = L"Local\\PowerToys_CropAndLock_InstanceMutex";
bool m_running = true;

// Click-through mode: when enabled, mouse events pass through cropped windows
bool g_clickThroughEnabled = false;

// Auto-start on boot
bool g_launchAtStartup = false;

bool IsLaunchAtStartupEnabled()
{
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return false;
    wchar_t value[MAX_PATH] = {};
    DWORD size = sizeof(value);
    bool exists = RegQueryValueExW(hKey, L"ZoneSpy", nullptr, nullptr, reinterpret_cast<LPBYTE>(value), &size) == ERROR_SUCCESS;
    RegCloseKey(hKey);
    return exists;
}

void SetLaunchAtStartup(bool enable)
{
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS)
        return;
    if (enable)
    {
        wchar_t exePath[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        RegSetValueExW(hKey, L"ZoneSpy", 0, REG_SZ, reinterpret_cast<LPBYTE>(exePath), (static_cast<DWORD>(wcslen(exePath)) + 1) * sizeof(wchar_t));
    }
    else
    {
        RegDeleteValueW(hKey, L"ZoneSpy");
    }
    RegCloseKey(hKey);
    g_launchAtStartup = enable;
}

// Theming
ThemeListener theme_listener{};
// Keep a list of our cropped windows
std::vector<std::shared_ptr<CropAndLockWindow>> croppedWindows;

void SaveAllWindowStates()
{
    std::vector<WindowState> states;
    for (const auto& window : croppedWindows)
    {
        // Only save thumbnail windows for now
        auto thumbnailWindow = std::dynamic_pointer_cast<ThumbnailCropAndLockWindow>(window);
        if (thumbnailWindow)
        {
            states.push_back(thumbnailWindow->GetState());
        }
    }

    // Always save, even if empty (to clear old state when all windows are closed)
    WindowStateManager::Instance().SaveStates(states);
    Logger::info(L"Saved {} window states", states.size());
}

void RestorePreviousWindowStates(std::function<void(HWND)> removeWindowCallback)
{
    auto states = WindowStateManager::Instance().LoadStates();
    if (states.empty())
    {
        Logger::info(L"No previous window states to restore");
        return;
    }

    Logger::info(L"Restoring {} window states", states.size());

    for (const auto& state : states)
    {
        try
        {
            if (state.windowType == CropAndLockType::Thumbnail)
            {
                auto window = std::make_shared<ThumbnailCropAndLockWindow>(state.title, 800, 600);

                // Only add if restoration was successful (found target window and CropAndLock succeeded)
                if (window->RestoreState(state))
                {
                    window->OnClosed(removeWindowCallback);
                    croppedWindows.push_back(window);
                    Logger::trace(L"Successfully restored window: {}", state.title);
                }
                else
                {
                    Logger::warn(L"Failed to restore window: {}", state.title);
                }
            }
            // TODO: Add restoration for other window types if needed
        }
        catch (const std::exception& ex)
        {
            Logger::error(L"Failed to restore window: {}", winrt::to_hstring(ex.what()));
        }
        catch (...)
        {
            Logger::error(L"Failed to restore window: unknown error");
        }
    }
}

void UpdateStreamConfig()
{
    wchar_t* localPath = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localPath))) return;
    std::wstring dir = localPath + std::wstring(L"\\ZoneSpy");
    CoTaskMemFree(localPath);
    CreateDirectoryW(dir.c_str(), nullptr);

    json::JsonArray arr;
    for (const auto& win : croppedWindows)
    {
        auto t = std::dynamic_pointer_cast<ThumbnailCropAndLockWindow>(win);
        if (t && t->GetStreamId() >= 0)
        {
            json::JsonObject obj;
            obj.SetNamedValue(L"id", json::value(t->GetStreamId()));
            wchar_t title[256] = {};
            GetWindowTextW(t->Handle(), title, 256);
            obj.SetNamedValue(L"name", json::value(std::wstring(title)));
            RECT r = {};
            GetClientRect(t->Handle(), &r);
            obj.SetNamedValue(L"width", json::value(static_cast<int>(r.right - r.left)));
            obj.SetNamedValue(L"height", json::value(static_cast<int>(r.bottom - r.top)));
            auto idStr = std::to_wstring(t->GetStreamId());
            obj.SetNamedValue(L"shm", json::value(L"ZoneSpy_FrameData_" + idStr));
            obj.SetNamedValue(L"evt", json::value(L"ZoneSpy_FrameReady_" + idStr));
            arr.Append(obj);
        }
    }
    json::JsonObject root;
    root.SetNamedValue(L"streams", arr);
    json::to_file(dir + L"\\streams.json", root);
    Logger::trace(L"Updated streams config");
{
    wchar_t* localPath = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localPath))) return;
    std::wstring dir = localPath + std::wstring(L"\\ZoneSpy");
    CoTaskMemFree(localPath);
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring path = dir + L"\\streams.json";

    std::wstring json = L"{\n  \"streams\": [\n";
    bool first = true;
    for (const auto& win : croppedWindows)
    {
        auto t = std::dynamic_pointer_cast<ThumbnailCropAndLockWindow>(win);
        if (t && t->GetStreamId() >= 0)
        {
            if (!first) json += L",\n";
            first = false;
            wchar_t title[256] = {};
            GetWindowTextW(t->Handle(), title, 256);
            RECT r = {};
            GetClientRect(t->Handle(), &r);
            auto idStr = std::to_wstring(t->GetStreamId());
            json += L"    {\n";
            json += L"      \"id\": " + std::to_wstring(t->GetStreamId()) + L",\n";
            json += L"      \"name\": \"" + std::wstring(title) + L"\",\n";
            json += L"      \"width\": " + std::to_wstring(static_cast<int>(r.right - r.left)) + L",\n";
            json += L"      \"height\": " + std::to_wstring(static_cast<int>(r.bottom - r.top)) + L",\n";
            json += L"      \"shm\": \"ZoneSpy_FrameData_" + idStr + L"\",\n";
            json += L"      \"evt\": \"ZoneSpy_FrameReady_" + idStr + L"\"\n";
            json += L"    }";
        }
    }
    json += L"\n  ]\n}";

    std::wofstream file(path);
    file << json;
    file.close();
}
