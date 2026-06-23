#include "pch.h"
#include <shlobj.h>
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
}

void handleTheme()
{
    auto theme = theme_listener.AppTheme;
    auto isDark = theme == Theme::Dark;
    Logger::info(L"Theme is now {}", isDark ? L"Dark" : L"Light");
    for (auto&& croppedWindow : croppedWindows)
    {
        ThemeHelpers::SetImmersiveDarkMode(croppedWindow->Handle(), isDark);
    }
}

// Hidden window for system tray (Exit menu)
const wchar_t TRAY_WINDOW_CLASS[] = L"CropAndLock_TrayWindow";
const UINT WM_TRAY_CALLBACK = WM_APP + 1;
const UINT_PTR TRAY_ICON_ID = 100;

LRESULT CALLBACK TrayWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_DESTROY:
    {
        NOTIFYICONDATAW nid = { sizeof(nid) };
        nid.hWnd = hwnd;
        nid.uID = TRAY_ICON_ID;
        Shell_NotifyIconW(NIM_DELETE, &nid);
        PostQuitMessage(0);
        return 0;
    }
    case WM_TRAY_CALLBACK:
        if (lparam == WM_RBUTTONUP || lparam == WM_LBUTTONUP)
        {
            POINT pt;
            GetCursorPos(&pt);
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING | (g_clickThroughEnabled ? MF_CHECKED : MF_UNCHECKED), 101, L"Click-Through");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING | (g_launchAtStartup ? MF_CHECKED : MF_UNCHECKED), 102, L"Launch at startup");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, 100, L"Exit");
            SetForegroundWindow(hwnd);
            int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
            if (cmd == 100)
            {
                DestroyWindow(hwnd);
            }
        else if (cmd == 101)
        {
            g_clickThroughEnabled = !g_clickThroughEnabled;
            for (auto& window : croppedWindows)
            {
                auto thumbnail = std::dynamic_pointer_cast<ThumbnailCropAndLockWindow>(window);
                if (thumbnail)
                {
                    thumbnail->SetClickThrough(g_clickThroughEnabled);
                }
            }
        }
        else if (cmd == 102)
        {
            SetLaunchAtStartup(!g_launchAtStartup);
        }
        }
        return 0;
    }
    return DefWindowProc(hwnd, msg, wparam, lparam);
}

int WINAPI wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ PWSTR /*lpCmdLine*/, _In_ int)
{
    // Initialize COM
    winrt::init_apartment(winrt::apartment_type::single_threaded);

    Trace::CropAndLock::RegisterProvider();

    theme_listener.AddChangedHandler(handleTheme);

    Shared::Trace::ETWTrace trace;
    trace.UpdateState(true);

    // Initialize logger automatic logging of exceptions.
    LoggerHelpers::init_logger(NonLocalizable::ModuleKey, L"", LogSettings::cropAndLockLoggerName);
    InitUnhandledExceptionHandler();

    // Before we do anything, check to see if we're already running. If we are,
    // the hotkey won't register and we'll fail. Instead, we should tell the user
    // to kill the other instance and exit this one.
    auto mutex = CreateMutex(nullptr, true, instanceMutexName.c_str());
    if (mutex == nullptr)
    {
        Logger::error(L"Failed to create mutex. {}", get_last_error_or_default(GetLastError()));
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        // CropAndLock is already open.
        return 1;
    }

    // NOTE: reparenting a window with a different DPI context has consequences.
    //       See https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setparent#remarks
    //       for more info.
    winrt::check_bool(SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));

    // Create the DispatcherQueue that the compositor needs to run
    auto controller = util::CreateDispatcherQueueControllerForCurrentThread();

    // Setup Composition
    auto compositor = winrt::Compositor();

    // Create our overlay window
    std::unique_ptr<OverlayWindow> overlayWindow;


    // Hotkey IDs for standalone operation
    const int HOTKEY_THUMBNAIL = 1;
    // const int HOTKEY_REPARENT = 2;
    // const int HOTKEY_SCREENSHOT = 3;

    std::function<void(HWND)> removeWindowCallback = [&](HWND windowHandle) {
        if (!m_running)
        {
            // If we're not running, the reference to croppedWindows might no longer be valid and cause a crash at exit time, due to being called by destructors after wWinMain returns.
            return;
        }

        auto pos = std::find_if(croppedWindows.begin(), croppedWindows.end(), [windowHandle](auto window) { return window->Handle() == windowHandle; });
        if (pos != croppedWindows.end())
        {
            croppedWindows.erase(pos);
        }

        // Save states when a window is closed
        SaveAllWindowStates();
        UpdateStreamConfig();
    };

    // Restore previous window states after setting up the callback
    RestorePreviousWindowStates(removeWindowCallback);
    UpdateStreamConfig();

    // Check startup registry on init
    g_launchAtStartup = IsLaunchAtStartupEnabled();
    if (g_launchAtStartup)
    {
        // Refresh registry path in case exe was moved
        SetLaunchAtStartup(true);
    }

    std::function<void(CropAndLockType)> ProcessCommand = [&](CropAndLockType mode) {
        std::function<void(HWND, RECT)> windowCroppedCallback = [&, mode](HWND targetWindow, RECT cropRect) {
            auto targetInfo = util::WindowInfo(targetWindow);
            // TODO: Fix WindowInfo.h to not contain the null char at the end.
            auto nullCharIndex = std::wstring::npos;
            do
            {
                nullCharIndex = targetInfo.Title.rfind(L'\0');
                if (nullCharIndex != std::wstring::npos)
                {
                    targetInfo.Title.erase(nullCharIndex);
                }
            } while (nullCharIndex != std::wstring::npos);

            std::wstringstream titleStream;
            titleStream << targetInfo.Title << L" (Cropped)";
            auto title = titleStream.str();

            std::shared_ptr<CropAndLockWindow> croppedWindow;
            switch (mode)
            {
            // case CropAndLockType::Reparent:
            //     croppedWindow = std::make_shared<ReparentCropAndLockWindow>(title, 800, 600);
            //     Logger::trace(L"Creating a reparent window");
            //     Trace::CropAndLock::CreateReparentWindow();
            //     break;
            case CropAndLockType::Thumbnail:
                croppedWindow = std::make_shared<ThumbnailCropAndLockWindow>(title, 800, 600);
                Logger::trace(L"Creating a thumbnail window");
                Trace::CropAndLock::CreateThumbnailWindow();
                break;
            // case CropAndLockType::Screenshot:
            //     croppedWindow = std::make_shared<ScreenshotCropAndLockWindow>(title, 800, 600);
            //     Logger::trace(L"Creating a screenshot window");
            //     Trace::CropAndLock::CreateScreenshotWindow();
            //     break;
            default:
                return;
            }
            croppedWindow->CropAndLock(targetWindow, cropRect);
            croppedWindow->OnClosed(removeWindowCallback);
            croppedWindows.push_back(croppedWindow);
            handleTheme();

            // Save states when a new window is created
            SaveAllWindowStates();
            UpdateStreamConfig();
        };

        overlayWindow.reset();

        // Get the current window with focus
        auto foregroundWindow = GetForegroundWindow();
        if (foregroundWindow != nullptr)
        {
            bool match = false;
            for (auto&& croppedWindow : croppedWindows)
            {
                if (foregroundWindow == croppedWindow->Handle())
                {
                    match = true;
                    break;
                }
            }
            if (!match)
            {
                overlayWindow = std::make_unique<OverlayWindow>(compositor, foregroundWindow, windowCroppedCallback);
            }
        }
    };

    // Register global hotkeys (same defaults as PowerToys: Win+Ctrl+Shift+T/R/S)
    RegisterHotKey(nullptr, HOTKEY_THUMBNAIL, MOD_WIN | MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'T');
    // RegisterHotKey(nullptr, HOTKEY_REPARENT, MOD_WIN | MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'R');
    // RegisterHotKey(nullptr, HOTKEY_SCREENSHOT, MOD_WIN | MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'S');

    // Register system tray window class
    auto instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = TrayWndProc;
    wc.hInstance = instance;
    wc.lpszClassName = TRAY_WINDOW_CLASS;
    RegisterClassExW(&wc);

    // Create hidden tray window
    HWND trayHwnd = CreateWindowExW(0, TRAY_WINDOW_CLASS, L"CropAndLock", 0,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        nullptr, nullptr, instance, nullptr);

    // Clean up any stale icon from a previous crash
    NOTIFYICONDATAW clean = { sizeof(clean) };
    clean.hWnd = trayHwnd;
    clean.uID = TRAY_ICON_ID;
    Shell_NotifyIconW(NIM_DELETE, &clean);

    // Add tray icon
    NOTIFYICONDATAW nid = { sizeof(nid) };
    nid.hWnd = trayHwnd;
    nid.uID = TRAY_ICON_ID;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_TRAY_CALLBACK;
    nid.hIcon = LoadIconW(instance, MAKEINTRESOURCE(IDI_ICON1));
    wcscpy_s(nid.szTip, L"CropAndLock - Window Cropping Utility");
    Shell_NotifyIconW(NIM_ADD, &nid);

    // Message pump - handles hotkeys and window messages
    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        if (msg.message == WM_HOTKEY)
        {
            auto dispatch = [&](CropAndLockType type) {
                if (!controller.DispatcherQueue().TryEnqueue([&]() { ProcessCommand(type); }))
                {
                    Logger::error("Couldn't enqueue hotkey command");
                }
            };

            if (msg.wParam == HOTKEY_THUMBNAIL)
                dispatch(CropAndLockType::Thumbnail);
            // else if (msg.wParam == HOTKEY_REPARENT)
                // dispatch(CropAndLockType::Reparent);
            // else if (msg.wParam == HOTKEY_SCREENSHOT)
                // dispatch(CropAndLockType::Screenshot);
        }
        else
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    // Save window states before exiting
    SaveAllWindowStates();

    trace.Flush();

    Trace::CropAndLock::UnregisterProvider();

    m_running = false;

    // Cleanup hotkeys
    UnregisterHotKey(nullptr, HOTKEY_THUMBNAIL);
    // UnregisterHotKey(nullptr, HOTKEY_REPARENT);
    // UnregisterHotKey(nullptr, HOTKEY_SCREENSHOT);

    return util::ShutdownDispatcherQueueControllerAndWait(controller, static_cast<int>(msg.wParam));
}
