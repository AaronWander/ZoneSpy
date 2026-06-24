#include "pch.h"
#include "ThumbnailCropAndLockWindow.h"
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

// Global click-through flag, defined in main.cpp
extern bool g_clickThroughEnabled;

const std::wstring ThumbnailCropAndLockWindow::ClassName = L"CropAndLock.ThumbnailCropAndLockWindow";
std::once_flag ThumbnailCropAndLockWindowClassRegistration;
constexpr int SnapDistance = 12;

#pragma pack(push, 1)
struct FrameHeader {
    DWORD frameCount;
    ULONGLONG timestamp;
    DWORD width;
    DWORD height;
};
#pragma pack(pop)

int ThumbnailCropAndLockWindow::s_nextStreamId{0};


float ComputeScaleFactor(RECT const& windowRect, RECT const& contentRect)
{
    auto windowWidth = static_cast<float>(windowRect.right - windowRect.left);
    auto windowHeight = static_cast<float>(windowRect.bottom - windowRect.top);
    auto contentWidth = static_cast<float>(contentRect.right - contentRect.left);
    auto contentHeight = static_cast<float>(contentRect.bottom - contentRect.top);

    auto windowRatio = windowWidth / windowHeight;
    auto contentRatio = contentWidth / contentHeight;

    auto scaleFactor = windowWidth / contentWidth;
    if (windowRatio > contentRatio)
    {
        scaleFactor = windowHeight / contentHeight;
    }

    return scaleFactor;
}

RECT ComputeDestRect(RECT const& windowRect, RECT const& contentRect)
{
    auto scaleFactor = ComputeScaleFactor(windowRect, contentRect);

    auto windowWidth = static_cast<float>(windowRect.right - windowRect.left);
    auto windowHeight = static_cast<float>(windowRect.bottom - windowRect.top);
    auto contentWidth = static_cast<float>(contentRect.right - contentRect.left) * scaleFactor;
    auto contentHeight = static_cast<float>(contentRect.bottom - contentRect.top) * scaleFactor;

    auto remainingWidth = windowWidth - contentWidth;
    auto remainingHeight = windowHeight - contentHeight;

    auto left = static_cast<LONG>(remainingWidth / 2.0f);
    auto top = static_cast<LONG>(remainingHeight / 2.0f);
    auto right = left + static_cast<LONG>(contentWidth);
    auto bottom = top + static_cast<LONG>(contentHeight);

    return RECT{ left, top, right, bottom };
}

void ThumbnailCropAndLockWindow::RegisterWindowClass()
{
    auto instance = winrt::check_pointer(GetModuleHandleW(nullptr));
    WNDCLASSEXW wcex = {};
    wcex.cbSize = sizeof(wcex);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = instance;
    wcex.hIcon = LoadIconW(instance, MAKEINTRESOURCE(IDI_ICON1));
    wcex.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wcex.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wcex.lpszClassName = ClassName.c_str();
    wcex.hIconSm = LoadIconW(wcex.hInstance, IDI_APPLICATION);
    winrt::check_bool(RegisterClassExW(&wcex));
}

ThumbnailCropAndLockWindow::ThumbnailCropAndLockWindow(std::wstring const& titleString, int width, int height)
{
    auto instance = winrt::check_pointer(GetModuleHandleW(nullptr));

    std::call_once(ThumbnailCropAndLockWindowClassRegistration, []() { RegisterWindowClass(); });

    auto exStyle = WS_EX_TOPMOST;
    auto style = WS_POPUP | WS_CLIPCHILDREN;

    RECT rect = { 0, 0, width, height};
    winrt::check_bool(AdjustWindowRectEx(&rect, style, false, exStyle));
    auto adjustedWidth = rect.right - rect.left;
    auto adjustedHeight = rect.bottom - rect.top;

    winrt::check_bool(CreateWindowExW(exStyle, ClassName.c_str(), titleString.c_str(), style,
        CW_USEDEFAULT, CW_USEDEFAULT, adjustedWidth, adjustedHeight, nullptr, nullptr, instance, this));
    WINRT_ASSERT(m_window);
}

ThumbnailCropAndLockWindow::~ThumbnailCropAndLockWindow()
{
    StopWatchdog();
    DisconnectTarget();
    DestroyWindow(m_window);
}

LRESULT ThumbnailCropAndLockWindow::MessageHandler(UINT const message, WPARAM const wparam, LPARAM const lparam)
{
    switch (message)
    {
    case WM_DESTROY:
        if (m_closedCallback != nullptr && !m_destroyed)
        {
            m_destroyed = true;
            m_closedCallback(m_window);
        }
        break;
    case WM_TIMER:
        if (wparam == WATCHDOG_TIMER_ID && !m_destroyed)
        {
            if (m_currentTarget != nullptr && !IsWindow(m_currentTarget))
            {
                KillTimer(m_window, WATCHDOG_TIMER_ID);
                m_watchdogTimer = 0;
                Logger::trace(L"Target window destroyed, attempting reconnection...");
                if (!ReconnectToTarget())
                {
                    Logger::warn(L"Target window reconnection failed, will retry in {} ms", WATCHDOG_INTERVAL_MS);
                }
                m_watchdogTimer = SetTimer(m_window, WATCHDOG_TIMER_ID, WATCHDOG_INTERVAL_MS, nullptr);
            }
        }

        }
        break;
    case WM_SIZE:
    case WM_SIZING:
    {
        if (m_thumbnail != nullptr)
        {
            RECT clientRect = {};
            winrt::check_bool(GetClientRect(m_window, &clientRect));

            m_destRect = ComputeDestRect(clientRect, m_sourceRect);

            DWM_THUMBNAIL_PROPERTIES properties = {};
            properties.dwFlags = DWM_TNP_RECTDESTINATION;
            properties.rcDestination = m_destRect;
            winrt::check_hresult(DwmUpdateThumbnailProperties(m_thumbnail.get(), &properties));
        }

    if (message == WM_SIZING)
    {
        auto windowRect = reinterpret_cast<RECT*>(lparam);

        // Constrain aspect ratio when Shift is held
        if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
        {
            auto contentWidth = m_sourceRect.right - m_sourceRect.left;
            auto contentHeight = m_sourceRect.bottom - m_sourceRect.top;
            if (contentWidth > 0 && contentHeight > 0)
            {
                float aspect = static_cast<float>(contentWidth) / static_cast<float>(contentHeight);
                LONG width = windowRect->right - windowRect->left;
                LONG height = windowRect->bottom - windowRect->top;

                switch (wparam)
                {
                case WMSZ_LEFT:
                case WMSZ_RIGHT:
                    // Drag left/right edge → width changes → constrain height
                    windowRect->bottom = windowRect->top + static_cast<LONG>(width / aspect);
                    break;
                case WMSZ_TOP:
                case WMSZ_BOTTOM:
                    // Drag top/bottom edge → height changes → constrain width
                    windowRect->right = windowRect->left + static_cast<LONG>(height * aspect);
                    break;
                case WMSZ_TOPLEFT:
                case WMSZ_BOTTOMRIGHT:
                case WMSZ_TOPRIGHT:
                case WMSZ_BOTTOMLEFT:
                    if (static_cast<float>(width) / height > aspect)
                        windowRect->right = windowRect->left + static_cast<LONG>(height * aspect);
                    else
                        windowRect->bottom = windowRect->top + static_cast<LONG>(width / aspect);
                    break;
                }
            }
        }

        SnapSizingRect(*windowRect, wparam);
        return TRUE;
    }
    }
    break;
    case WM_ENTERSIZEMOVE:
    {
        // User started dragging or resizing the window
        m_isDragging = true;

        // Get current cursor and window positions
        POINT cursor = {};
        GetCursorPos(&cursor);

        RECT windowRect = {};
        winrt::check_bool(GetWindowRect(m_window, &windowRect));

        // Reset snap state
        m_snapX = {};
        m_snapY = {};

        // Check if X axis is currently snapped
        // Check screen edges
        HMONITOR monitor = MonitorFromRect(&windowRect, MONITOR_DEFAULTTONEAREST);
        if (monitor != nullptr)
        {
            MONITORINFO monitorInfo = {};
            monitorInfo.cbSize = sizeof(monitorInfo);
            if (GetMonitorInfoW(monitor, &monitorInfo))
            {
                if (windowRect.left == monitorInfo.rcWork.left)
                {
                    m_snapX.snapped = true;
                    m_snapX.snapCursorPos = cursor.x;
                    m_snapX.snapTargetPos = monitorInfo.rcWork.left;
                }
                else if (windowRect.right == monitorInfo.rcWork.right)
                {
                    m_snapX.snapped = true;
                    m_snapX.snapCursorPos = cursor.x;
                    m_snapX.snapTargetPos = monitorInfo.rcWork.right;
                }

                if (windowRect.top == monitorInfo.rcWork.top)
                {
                    m_snapY.snapped = true;
                    m_snapY.snapCursorPos = cursor.y;
                    m_snapY.snapTargetPos = monitorInfo.rcWork.top;
                }
                else if (windowRect.bottom == monitorInfo.rcWork.bottom)
                {
                    m_snapY.snapped = true;
                    m_snapY.snapCursorPos = cursor.y;
                    m_snapY.snapTargetPos = monitorInfo.rcWork.bottom;
                }
            }
        }

        // Check if snapped to other thumbnail windows
        // TODO: Enumerate other windows and check for exact edge alignment
        // For now, simplified check omitted
    }
    break;
    case WM_EXITSIZEMOVE:
    {
        // User finished dragging or resizing
        m_isDragging = false;
    }
    break;
    case WM_MOVING:
    {
        auto windowRect = reinterpret_cast<RECT*>(lparam);
        SnapMovingRect(*windowRect);
        return TRUE;
    }
    case WM_NCHITTEST:
    {
        if (g_clickThroughEnabled)
        {
            return HTTRANSPARENT;
        }
        POINT point = { GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
        return HitTest(point);
    }
    case WM_CONTEXTMENU:
    {
        POINT point = { GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
        if (point.x == -1 && point.y == -1)
        {
            RECT windowRect = {};
            winrt::check_bool(GetWindowRect(m_window, &windowRect));
            point.x = windowRect.left + 16;
            point.y = windowRect.top + 16;
        }

        ShowContextMenu(point);
        return 0;
    }
    case WM_NCRBUTTONUP:
    {
        POINT point = { GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
        ShowContextMenu(point);
        return 0;
    }
    default:
        return base_type::MessageHandler(message, wparam, lparam);
    }
    return 0;
}

void ThumbnailCropAndLockWindow::CropAndLock(HWND windowToCrop, RECT cropRect)
{
    DisconnectTarget();
    m_currentTarget = windowToCrop;

    // Save the original crop rect (before DWM adjustment)
    m_originalCropRect = cropRect;

    // Adjust the crop rect from client-area-relative to window-frame-relative
    // rcSource with fSourceClientAreaOnly=false uses window-frame coordinates
    RECT windowRect = {};
    winrt::check_hresult(DwmGetWindowAttribute(m_currentTarget, DWMWA_EXTENDED_FRAME_BOUNDS, reinterpret_cast<void*>(&windowRect), sizeof(windowRect)));
    auto clientRect = ClientAreaInScreenSpace(m_currentTarget);
    auto diffX = clientRect.left - windowRect.left;
    auto diffY = clientRect.top - windowRect.top;
    cropRect.left   += diffX;
    cropRect.top    += diffY;
    cropRect.right  += diffX;
    cropRect.bottom += diffY;

    // Resize our window
    auto width = cropRect.right - cropRect.left;
    auto height = cropRect.bottom - cropRect.top;
    windowRect = { 0, 0, width, height };
    auto exStyle = static_cast<DWORD>(GetWindowLongPtrW(m_window, GWL_EXSTYLE));
    auto style = static_cast<DWORD>(GetWindowLongPtrW(m_window, GWL_STYLE));
    winrt::check_bool(AdjustWindowRectEx(&windowRect, style, false, exStyle));
    auto adjustedWidth = windowRect.right - windowRect.left;
    auto adjustedHeight = windowRect.bottom - windowRect.top;
    winrt::check_bool(SetWindowPos(m_window, m_isTopMost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, adjustedWidth, adjustedHeight, SWP_NOMOVE | SWP_SHOWWINDOW));

    // Set up the thumbnail
    winrt::check_hresult(DwmRegisterThumbnail(m_window, m_currentTarget, m_thumbnail.addressof()));

    clientRect = {};
    winrt::check_bool(GetClientRect(m_window, &clientRect));
    m_destRect = clientRect;
    m_sourceRect = cropRect;

    DWM_THUMBNAIL_PROPERTIES properties = {};
    properties.dwFlags = DWM_TNP_SOURCECLIENTAREAONLY | DWM_TNP_VISIBLE | DWM_TNP_OPACITY | DWM_TNP_RECTDESTINATION | DWM_TNP_RECTSOURCE;
    properties.fSourceClientAreaOnly = false;
    properties.fVisible = true;
    properties.opacity = 255;
    properties.rcDestination = m_destRect;
    properties.rcSource = m_sourceRect;
    winrt::check_hresult(DwmUpdateThumbnailProperties(m_thumbnail.get(), &properties));

    // Capture target window info for reconnection when window is closed/reopened
    CaptureTargetWindowInfo();

    // Start watchdog to monitor target window lifetime
    StartWatchdog();

    // Start shared memory streaming
    m_streamId = s_nextStreamId++;
    StartFrameCapture();

    // Apply click-through state if enabled
    if (g_clickThroughEnabled)
    {
        SetClickThrough(true);
    }
}

LRESULT ThumbnailCropAndLockWindow::HitTest(POINT const& point)
{
    RECT windowRect = {};
    winrt::check_bool(GetWindowRect(m_window, &windowRect));

    UINT dpi = GetDpiForWindow(m_window);
    auto resizeBorder = MulDiv(8, dpi, USER_DEFAULT_SCREEN_DPI);

    bool left = point.x >= windowRect.left && point.x < windowRect.left + resizeBorder;
    bool right = point.x < windowRect.right && point.x >= windowRect.right - resizeBorder;
    bool top = point.y >= windowRect.top && point.y < windowRect.top + resizeBorder;
    bool bottom = point.y < windowRect.bottom && point.y >= windowRect.bottom - resizeBorder;

    if (top && left)
    {
        return HTTOPLEFT;
    }
    if (top && right)
    {
        return HTTOPRIGHT;
    }
    if (bottom && left)
    {
        return HTBOTTOMLEFT;
    }
    if (bottom && right)
    {
        return HTBOTTOMRIGHT;
    }
    if (left)
    {
        return HTLEFT;
    }
    if (right)
    {
        return HTRIGHT;
    }
    if (top)
    {
        return HTTOP;
    }
    if (bottom)
    {
        return HTBOTTOM;
    }

    return HTCAPTION;
}

void ThumbnailCropAndLockWindow::ShowContextMenu(POINT point)
{
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr)
    {
        return;
    }

    constexpr UINT_PTR toggleTopMostCommand = 1;
    constexpr UINT_PTR closeCommand = 2;

    AppendMenuW(menu, MF_STRING | (m_isTopMost ? MF_CHECKED : MF_UNCHECKED), toggleTopMostCommand, L"Always on top");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, closeCommand, L"Close");

    auto command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, m_window, nullptr);
    DestroyMenu(menu);

    if (command == toggleTopMostCommand)
    {
        m_isTopMost = !m_isTopMost;
        winrt::check_bool(SetWindowPos(m_window, m_isTopMost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE));
    }
    else if (command == closeCommand)
    {
        DestroyWindow(m_window);
    }
}

void ThumbnailCropAndLockWindow::SnapMovingRect(RECT& windowRect)
{
    constexpr int SnapDist = SnapDistance;      // 12px - snap threshold
    constexpr int DetachDist = SnapDistance * 2; // 24px - detach threshold

    // Get current cursor position
    POINT cursor = {};
    GetCursorPos(&cursor);

    auto width = windowRect.right - windowRect.left;
    auto height = windowRect.bottom - windowRect.top;

    // === X Axis Processing ===

    // Check if should detach from current snap
    if (m_snapX.snapped)
    {
        int cursorDelta = abs(cursor.x - m_snapX.snapCursorPos);
        if (cursorDelta >= DetachDist)
        {
            // Detach and move window away from snap edge by DetachDist
            // This prevents immediate re-snap on next frame
            m_snapX.snapped = false;

            // Determine which edge we were snapped to and move away
            if (abs(windowRect.left - m_snapX.snapTargetPos) < abs(windowRect.right - m_snapX.snapTargetPos))
            {
                // Was snapping left edge - move right by DetachDist
                int offset = (cursor.x > m_snapX.snapCursorPos) ? DetachDist : -DetachDist;
                windowRect.left = m_snapX.snapTargetPos + offset;
                windowRect.right = windowRect.left + width;
            }
            else
            {
                // Was snapping right edge - move based on cursor direction
                int offset = (cursor.x > m_snapX.snapCursorPos) ? DetachDist : -DetachDist;
                windowRect.right = m_snapX.snapTargetPos + offset;
                windowRect.left = windowRect.right - width;
            }
        }
        else
        {
            // Still snapped, maintain the snap by reapplying the target position
            if (abs(windowRect.left - m_snapX.snapTargetPos) < abs(windowRect.right - m_snapX.snapTargetPos))
            {
                // Snapping left edge
                windowRect.left = m_snapX.snapTargetPos;
                windowRect.right = windowRect.left + width;
            }
            else
            {
                // Snapping right edge
                windowRect.right = m_snapX.snapTargetPos;
                windowRect.left = windowRect.right - width;
            }
        }
    }

    // If not snapped, collect all X-axis snap candidates
    if (!m_snapX.snapped)
    {
        std::vector<SnapCandidateX> xCandidates;

        // Collect screen edge candidates
        HMONITOR monitor = MonitorFromRect(&windowRect, MONITOR_DEFAULTTONEAREST);
        if (monitor != nullptr)
        {
            MONITORINFO monitorInfo = {};
            monitorInfo.cbSize = sizeof(monitorInfo);
            if (GetMonitorInfoW(monitor, &monitorInfo))
            {
                // Screen left edge
                int leftDist = abs(windowRect.left - monitorInfo.rcWork.left);
                if (leftDist <= SnapDist)
                {
                    xCandidates.push_back({ leftDist, monitorInfo.rcWork.left, true });
                }

                // Screen right edge
                int rightDist = abs(windowRect.right - monitorInfo.rcWork.right);
                if (rightDist <= SnapDist)
                {
                    xCandidates.push_back({ rightDist, monitorInfo.rcWork.right, false });
                }
            }
        }

        // Collect window-to-window X-axis candidates
        CollectWindowSnapCandidatesX(windowRect, xCandidates);

        // Choose closest candidate
        SnapCandidateX* bestCandidate = nullptr;
        for (auto& candidate : xCandidates)
        {
            if (bestCandidate == nullptr || candidate.distance < bestCandidate->distance)
            {
                bestCandidate = &candidate;
            }
        }

        // Apply best candidate if found
        if (bestCandidate != nullptr)
        {
            if (bestCandidate->isLeft)
            {
                windowRect.left = bestCandidate->targetPos;
                windowRect.right = windowRect.left + width;
            }
            else
            {
                windowRect.right = bestCandidate->targetPos;
                windowRect.left = windowRect.right - width;
            }

            m_snapX.snapped = true;
            m_snapX.snapCursorPos = cursor.x;
            m_snapX.snapTargetPos = bestCandidate->targetPos;
        }
    }

    // === Y Axis Processing (symmetric to X) ===

    // Check if should detach from current snap
    if (m_snapY.snapped)
    {
        int cursorDelta = abs(cursor.y - m_snapY.snapCursorPos);
        if (cursorDelta >= DetachDist)
        {
            // Detach and move window away from snap edge by DetachDist
            // This prevents immediate re-snap on next frame
            m_snapY.snapped = false;

            // Determine which edge we were snapped to and move away
            if (abs(windowRect.top - m_snapY.snapTargetPos) < abs(windowRect.bottom - m_snapY.snapTargetPos))
            {
                // Was snapping top edge - move based on cursor direction
                int offset = (cursor.y > m_snapY.snapCursorPos) ? DetachDist : -DetachDist;
                windowRect.top = m_snapY.snapTargetPos + offset;
                windowRect.bottom = windowRect.top + height;
            }
            else
            {
                // Was snapping bottom edge - move based on cursor direction
                int offset = (cursor.y > m_snapY.snapCursorPos) ? DetachDist : -DetachDist;
                windowRect.bottom = m_snapY.snapTargetPos + offset;
                windowRect.top = windowRect.bottom - height;
            }
        }
        else
        {
            // Still snapped, maintain the snap by reapplying the target position
            if (abs(windowRect.top - m_snapY.snapTargetPos) < abs(windowRect.bottom - m_snapY.snapTargetPos))
            {
                // Snapping top edge
                windowRect.top = m_snapY.snapTargetPos;
                windowRect.bottom = windowRect.top + height;
            }
            else
            {
                // Snapping bottom edge
                windowRect.bottom = m_snapY.snapTargetPos;
                windowRect.top = windowRect.bottom - height;
            }
        }
    }

    // If not snapped, collect all Y-axis snap candidates
    if (!m_snapY.snapped)
    {
        std::vector<SnapCandidateY> yCandidates;

        // Collect screen edge candidates
        HMONITOR monitor = MonitorFromRect(&windowRect, MONITOR_DEFAULTTONEAREST);
        if (monitor != nullptr)
        {
            MONITORINFO monitorInfo = {};
            monitorInfo.cbSize = sizeof(monitorInfo);
            if (GetMonitorInfoW(monitor, &monitorInfo))
            {
                // Screen top edge
                int topDist = abs(windowRect.top - monitorInfo.rcWork.top);
                if (topDist <= SnapDist)
                {
                    yCandidates.push_back({ topDist, monitorInfo.rcWork.top, true });
                }

                // Screen bottom edge
                int bottomDist = abs(windowRect.bottom - monitorInfo.rcWork.bottom);
                if (bottomDist <= SnapDist)
                {
                    yCandidates.push_back({ bottomDist, monitorInfo.rcWork.bottom, false });
                }
            }
        }

        // Collect window-to-window Y-axis candidates
        CollectWindowSnapCandidatesY(windowRect, yCandidates);

        // Choose closest candidate
        SnapCandidateY* bestCandidate = nullptr;
        for (auto& candidate : yCandidates)
        {
            if (bestCandidate == nullptr || candidate.distance < bestCandidate->distance)
            {
                bestCandidate = &candidate;
            }
        }

        // Apply best candidate if found
        if (bestCandidate != nullptr)
        {
            if (bestCandidate->isTop)
            {
                windowRect.top = bestCandidate->targetPos;
                windowRect.bottom = windowRect.top + height;
            }
            else
            {
                windowRect.bottom = bestCandidate->targetPos;
                windowRect.top = windowRect.bottom - height;
            }

            m_snapY.snapped = true;
            m_snapY.snapCursorPos = cursor.y;
            m_snapY.snapTargetPos = bestCandidate->targetPos;
        }
    }
}

void ThumbnailCropAndLockWindow::SnapSizingRect(RECT& windowRect, WPARAM sizingEdge)
{
    HMONITOR monitor = MonitorFromRect(&windowRect, MONITOR_DEFAULTTONEAREST);
    if (monitor == nullptr)
    {
        return;
    }

    MONITORINFO monitorInfo = {};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (!GetMonitorInfoW(monitor, &monitorInfo))
    {
        return;
    }

    auto snapLeft = [&]() {
        if (abs(windowRect.left - monitorInfo.rcWork.left) <= SnapDistance)
        {
            windowRect.left = monitorInfo.rcWork.left;
        }
    };
    auto snapTop = [&]() {
        if (abs(windowRect.top - monitorInfo.rcWork.top) <= SnapDistance)
        {
            windowRect.top = monitorInfo.rcWork.top;
        }
    };
    auto snapRight = [&]() {
        if (abs(windowRect.right - monitorInfo.rcWork.right) <= SnapDistance)
        {
            windowRect.right = monitorInfo.rcWork.right;
        }
    };
    auto snapBottom = [&]() {
        if (abs(windowRect.bottom - monitorInfo.rcWork.bottom) <= SnapDistance)
        {
            windowRect.bottom = monitorInfo.rcWork.bottom;
        }
    };

    switch (sizingEdge)
    {
    case WMSZ_LEFT:
        snapLeft();
        break;
    case WMSZ_RIGHT:
        snapRight();
        break;
    case WMSZ_TOP:
        snapTop();
        break;
    case WMSZ_BOTTOM:
        snapBottom();
        break;
    case WMSZ_TOPLEFT:
        snapTop();
        snapLeft();
        break;
    case WMSZ_TOPRIGHT:
        snapTop();
        snapRight();
        break;
    case WMSZ_BOTTOMLEFT:
        snapBottom();
        snapLeft();
        break;
    case WMSZ_BOTTOMRIGHT:
        snapBottom();
        snapRight();
        break;
    default:
        break;
    }
}

void ThumbnailCropAndLockWindow::CollectWindowSnapCandidatesX(const RECT& windowRect, std::vector<SnapCandidateX>& candidates)
{
    constexpr int SnapDist = SnapDistance;

    struct EnumState
    {
        HWND self = nullptr;
        const RECT* windowRect = nullptr;
        std::vector<SnapCandidateX>* candidates = nullptr;
    } state{ m_window, &windowRect, &candidates };

    auto callback = [](HWND window, LPARAM lparam) -> BOOL {
        auto state = reinterpret_cast<EnumState*>(lparam);
        if (window == state->self || !IsWindowVisible(window))
        {
            return TRUE;
        }

        wchar_t className[256] = {};
        if (GetClassNameW(window, className, static_cast<int>(_countof(className))) == 0)
        {
            return TRUE;
        }
        if (ThumbnailCropAndLockWindow::ClassName != className)
        {
            return TRUE;
        }

        RECT otherRect = {};
        if (!GetWindowRect(window, &otherRect))
        {
            return TRUE;
        }

        auto rect = state->windowRect;
        auto verticalOverlap = rect->bottom > otherRect.top && rect->top < otherRect.bottom;

        if (verticalOverlap)
        {
            // Left edge to other's right edge
            int dist = abs(rect->left - otherRect.right);
            if (dist <= SnapDist)
            {
                state->candidates->push_back({ dist, otherRect.right, true });
            }

            // Right edge to other's left edge
            dist = abs(rect->right - otherRect.left);
            if (dist <= SnapDist)
            {
                state->candidates->push_back({ dist, otherRect.left, false });
            }

            // Left edge to other's left edge
            dist = abs(rect->left - otherRect.left);
            if (dist <= SnapDist)
            {
                state->candidates->push_back({ dist, otherRect.left, true });
            }

            // Right edge to other's right edge
            dist = abs(rect->right - otherRect.right);
            if (dist <= SnapDist)
            {
                state->candidates->push_back({ dist, otherRect.right, false });
            }
        }

        return TRUE;
    };

    EnumWindows(callback, reinterpret_cast<LPARAM>(&state));
}

void ThumbnailCropAndLockWindow::CollectWindowSnapCandidatesY(const RECT& windowRect, std::vector<SnapCandidateY>& candidates)
{
    constexpr int SnapDist = SnapDistance;

    struct EnumState
    {
        HWND self = nullptr;
        const RECT* windowRect = nullptr;
        std::vector<SnapCandidateY>* candidates = nullptr;
    } state{ m_window, &windowRect, &candidates };

    auto callback = [](HWND window, LPARAM lparam) -> BOOL {
        auto state = reinterpret_cast<EnumState*>(lparam);
        if (window == state->self || !IsWindowVisible(window))
        {
            return TRUE;
        }

        wchar_t className[256] = {};
        if (GetClassNameW(window, className, static_cast<int>(_countof(className))) == 0)
        {
            return TRUE;
        }
        if (ThumbnailCropAndLockWindow::ClassName != className)
        {
            return TRUE;
        }

        RECT otherRect = {};
        if (!GetWindowRect(window, &otherRect))
        {
            return TRUE;
        }

        auto rect = state->windowRect;
        auto horizontalOverlap = rect->right > otherRect.left && rect->left < otherRect.right;

        if (horizontalOverlap)
        {
            // Top edge to other's bottom edge
            int dist = abs(rect->top - otherRect.bottom);
            if (dist <= SnapDist)
            {
                state->candidates->push_back({ dist, otherRect.bottom, true });
            }

            // Bottom edge to other's top edge
            dist = abs(rect->bottom - otherRect.top);
            if (dist <= SnapDist)
            {
                state->candidates->push_back({ dist, otherRect.top, false });
            }

            // Top edge to other's top edge
            dist = abs(rect->top - otherRect.top);
            if (dist <= SnapDist)
            {
                state->candidates->push_back({ dist, otherRect.top, true });
            }

            // Bottom edge to other's bottom edge
            dist = abs(rect->bottom - otherRect.bottom);
            if (dist <= SnapDist)
            {
                state->candidates->push_back({ dist, otherRect.bottom, false });
            }
        }

        return TRUE;
    };

    EnumWindows(callback, reinterpret_cast<LPARAM>(&state));
}

void ThumbnailCropAndLockWindow::Hide()
{
    DisconnectTarget();
    ShowWindow(m_window, SW_HIDE);
}

void ThumbnailCropAndLockWindow::DisconnectTarget()
{
    StopFrameCapture();
    StopWatchdog();
    if (m_currentTarget != nullptr)
    {
        m_thumbnail.reset();
        m_currentTarget = nullptr;
    }
}

void ThumbnailCropAndLockWindow::CaptureTargetWindowInfo()
{
    m_savedTargetInfo = {};
    if (m_currentTarget == nullptr)
    {
        return;
    }

    wchar_t targetTitle[256] = {};
    GetWindowTextW(m_currentTarget, targetTitle, static_cast<int>(_countof(targetTitle)));
    m_savedTargetInfo.targetWindowTitle = targetTitle;

    wchar_t targetClass[256] = {};
    GetClassNameW(m_currentTarget, targetClass, static_cast<int>(_countof(targetClass)));
    m_savedTargetInfo.targetWindowClass = targetClass;

    DWORD processId = 0;
    GetWindowThreadProcessId(m_currentTarget, &processId);
    m_savedTargetInfo.targetProcessId = processId;

    HANDLE processHandle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (processHandle != nullptr)
    {
        wchar_t processPath[MAX_PATH] = {};
        DWORD pathSize = MAX_PATH;
        if (QueryFullProcessImageNameW(processHandle, 0, processPath, &pathSize))
        {
            m_savedTargetInfo.targetProcessPath = processPath;
            std::wstring pathStr(processPath);
            auto lastSlash = pathStr.find_last_of(L"\\/");
            if (lastSlash != std::wstring::npos)
            {
                m_savedTargetInfo.targetProcessName = pathStr.substr(lastSlash + 1);
            }
        }
        CloseHandle(processHandle);
    }
}

void ThumbnailCropAndLockWindow::StartWatchdog()
{
    StopWatchdog();
    m_watchdogTimer = SetTimer(m_window, WATCHDOG_TIMER_ID, WATCHDOG_INTERVAL_MS, nullptr);
    Logger::trace(L"Started target watchdog (interval={}ms)", WATCHDOG_INTERVAL_MS);
}

void ThumbnailCropAndLockWindow::StopWatchdog()
{
    if (m_watchdogTimer != 0)
    {
        KillTimer(m_window, WATCHDOG_TIMER_ID);
        m_watchdogTimer = 0;
    }
}

void ThumbnailCropAndLockWindow::SetClickThrough(bool enable)
{
    auto exStyle = GetWindowLongPtrW(m_window, GWL_EXSTYLE);
    if (enable)
    {
        exStyle |= WS_EX_LAYERED;
        exStyle |= WS_EX_TRANSPARENT;
    }
    else
    {
        exStyle &= ~WS_EX_LAYERED;
        exStyle &= ~WS_EX_TRANSPARENT;
    }
    SetWindowLongPtrW(m_window, GWL_EXSTYLE, exStyle);

    if (enable)
    {
        // Fully opaque window - clicks pass through via WS_EX_LAYERED + WS_EX_TRANSPARENT
        SetLayeredWindowAttributes(m_window, 0, 255, LWA_ALPHA);
    }

    // Force window to re-evaluate hit testing
    SetWindowPos(m_window, nullptr, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
}


void ThumbnailCropAndLockWindow::CaptureFrame()
{
    // Capturing is now handled by OnFrameArrived via the D3D11 capture session.
    // This stub is kept for callers that may invoke CaptureFrame() directly.
}

void ThumbnailCropAndLockWindow::OnFrameArrived(
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool const& pool,
    winrt::Windows::Foundation::IInspectable const&)
{
    if (m_destroyed || !m_frameData) return;

    auto frame = pool.TryGetNextFrame();
    if (!frame) return;

    // Rate limit: skip if we sent a frame too recently.
    ULONGLONG now = GetTickCount64();
    if (now - m_lastFrameTimeMs < static_cast<ULONGLONG>(m_rateLimitIntervalMs))
    {
        return;
    }
    m_lastFrameTimeMs = now;

    // Get the ID3D11Texture2D from the capture frame.
    auto frameSurface = frame.Surface();
    auto dxgiAccess = frameSurface.as<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
    com_ptr<ID3D11Texture2D> srcTexture;
    winrt::check_hresult(dxgiAccess->GetInterface(__uuidof(ID3D11Texture2D), srcTexture.put_void()));

    D3D11_TEXTURE2D_DESC srcDesc{};
    srcTexture->GetDesc(&srcDesc);

    // Create staging texture for CPU readback.
    auto d3dDevice = D3D11Device();
    auto d3dContext = D3D11Context();

    D3D11_TEXTURE2D_DESC stagingDesc = srcDesc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.MiscFlags = 0;

    com_ptr<ID3D11Texture2D> stagingTexture;
    winrt::check_hresult(d3dDevice->CreateTexture2D(&stagingDesc, nullptr, stagingTexture.put()));

    // Copy GPU texture → staging (D3D handles the GPU→CPU transfer).
    d3dContext->CopySubresourceRegion(stagingTexture.get(), 0, 0, 0, 0, srcTexture.get(), 0, nullptr);

    // Map for CPU read.
    D3D11_MAPPED_SUBRESOURCE mapped{};
    winrt::check_hresult(d3dContext->Map(stagingTexture.get(), 0, D3D11_MAP_READ, 0, &mapped));

    // Write pixels to shared memory.
    BYTE* pixelBuf = static_cast<BYTE*>(m_frameData) + sizeof(FrameHeader);
    int w = static_cast<int>(srcDesc.Width);
    int h = static_cast<int>(srcDesc.Height);
    int srcStride = mapped.RowPitch;
    int dstStride = w * 4;

    if (srcStride == dstStride)
    {
        memcpy(pixelBuf, mapped.pData, static_cast<size_t>(h) * dstStride);
    }
    else
    {
        // Handle row-pitch mismatch (safe copy row by row).
        for (int y = 0; y < h; ++y)
        {
            memcpy(pixelBuf + static_cast<size_t>(y) * dstStride,
                   static_cast<const BYTE*>(mapped.pData) + static_cast<size_t>(y) * srcStride,
                   dstStride);
        }
    }

    d3dContext->Unmap(stagingTexture.get(), 0);

    // Check content change.
    bool changed = HasContentChanged(pixelBuf, w, h, dstStride);

    auto hdr = static_cast<FrameHeader*>(m_frameData);
    if (changed)
    {
        m_consecutiveIdenticalFrames = 0;

        DWORD nextFrame = (hdr->frameCount + 1) | 1;
        hdr->frameCount = nextFrame;
        std::atomic_thread_fence(std::memory_order_release);

        hdr->timestamp = GetTickCount64();
        hdr->width = static_cast<DWORD>(w);
        hdr->height = static_cast<DWORD>(h);

        std::atomic_thread_fence(std::memory_order_release);
        hdr->frameCount = nextFrame & ~1;
        std::atomic_thread_fence(std::memory_order_release);

        SetEvent(m_frameEvent);
    }
    else
    {
        m_consecutiveIdenticalFrames++;
    }

    // Adjust rate limit: static → 500ms, dynamic → 66ms.
    if (m_consecutiveIdenticalFrames >= 15)
    {
        m_rateLimitIntervalMs = 500;
    }
    else if (m_consecutiveIdenticalFrames == 0)
    {
        m_rateLimitIntervalMs = 66;
    }
}


// ── Static (shared) D3D11 device ──────────────────────────────────

com_ptr<ID3D11Device> ThumbnailCropAndLockWindow::s_d3dDevice;
com_ptr<ID3D11DeviceContext> ThumbnailCropAndLockWindow::s_d3dContext;

com_ptr<ID3D11Device> ThumbnailCropAndLockWindow::D3D11Device()
{
    if (!s_d3dDevice)
    {
        D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
        winrt::check_hresult(D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            levels, ARRAYSIZE(levels),
            D3D11_SDK_VERSION,
            s_d3dDevice.put(), nullptr, s_d3dContext.put()));
    }
    return s_d3dDevice;
}

com_ptr<ID3D11DeviceContext> ThumbnailCropAndLockWindow::D3D11Context()
{
    D3D11Device(); // ensure device & context are created
    return s_d3dContext;
}


// ── Start / Stop D3D11 capture ────────────────────────────────────

void ThumbnailCropAndLockWindow::StartFrameCapture()
{
    StopFrameCapture();

    // --- Set up shared memory (same as before) ---
    const DWORD shmSize = sizeof(FrameHeader) + 3840 * 2160 * 4;
    auto shmName = L"ZoneSpy_FrameData_" + std::to_wstring(m_streamId);
    auto evtName = L"ZoneSpy_FrameReady_" + std::to_wstring(m_streamId);
    HANDLE h = OpenEventW(EVENT_ALL_ACCESS, FALSE, evtName.c_str());
    if (h) { CloseHandle(h); }
    m_frameShm = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, shmSize, shmName.c_str());
    if (!m_frameShm) { Logger::error(L"Failed to create shared memory for stream {}", m_streamId); return; }
    m_frameData = MapViewOfFile(m_frameShm, FILE_MAP_WRITE, 0, 0, shmSize);
    if (!m_frameData) { Logger::error(L"Failed to map shared memory"); return; }
    ZeroMemory(m_frameData, sizeof(FrameHeader));
    m_frameEvent = CreateEventW(nullptr, FALSE, FALSE, evtName.c_str());
    if (!m_frameEvent) { Logger::error(L"Failed to create frame event"); return; }

    // --- D3D11 capture setup ---
    auto d3dDevice = D3D11Device();
    if (!d3dDevice) { Logger::error(L"No D3D11 device"); return; }

    // Create GraphicsCaptureItem from our own window via IGraphicsCaptureItemInterop.
    try
    {
        auto factory = winrt::get_activation_factory<winrt::Windows::Graphics::Capture::GraphicsCaptureItem>();
        auto interop = factory.as<IGraphicsCaptureItemInterop>();
        winrt::check_hresult(interop->CreateForWindow(
            m_window,
            winrt::guid_of<winrt::Windows::Graphics::Capture::GraphicsCaptureItem>(),
            reinterpret_cast<void**>(winrt::put_abi(m_captureItem))));
    }
    catch (winrt::hresult_error const& e)
    {
        Logger::error(L"CreateCaptureItem failed: {} (0x{:08x})", e.message(), e.code());
        return;
    }

    // Wrap our ID3D11Device as IDirect3DDevice.
    auto dxgiDevice = d3dDevice.as<IDXGIDevice>();
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice direct3DDevice{ nullptr };
    winrt::check_hresult(::CreateDirect3D11DeviceFromDXGIDevice(
        dxgiDevice.get(),
        reinterpret_cast<::IInspectable**>(winrt::put_abi(direct3DDevice))));

    // Determine initial frame size from our window's client area.
    RECT clientRect = {};
    GetClientRect(m_window, &clientRect);
    int cw = clientRect.right - clientRect.left;
    int ch = clientRect.bottom - clientRect.top;
    if (cw <= 0 || ch <= 0) { cw = 800; ch = 600; }

    // Create frame pool (1 buffer, BGRA).
    m_framePool = winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool::Create(
        direct3DDevice,
        winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
        1,
        { cw, ch });

    // Register FrameArrived callback.
    auto self = this;
    m_frameArrivedToken = m_framePool.FrameArrived(
        [self](auto const& pool, auto const&) { self->OnFrameArrived(pool, nullptr); });

    // Register SizeChanged → recreate pool at new size.
    m_sizeChangedToken = m_framePool.SizeChanged(
        [self](winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool const& pool, auto const&) {
            RECT cr = {};
            GetClientRect(self->m_window, &cr);
            int w = cr.right - cr.left;
            int h = cr.bottom - cr.top;
            if (w > 0 && h > 0)
            {
                pool.Recreate(
                    pool.Device(),
                    winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
                    1,
                    { w, h });
            }
        });

    // Start capture session.
    m_captureSession = m_framePool.CreateCaptureSession(m_captureItem);
    m_captureSession.StartCapture();

    // Reset state.
    m_previousFrameHash = 0;
    m_consecutiveIdenticalFrames = 0;
    m_rateLimitIntervalMs = 66;
    m_lastFrameTimeMs = GetTickCount64();

    Logger::info(L"D3D11 GPU capture started for stream {}", m_streamId);
}

void ThumbnailCropAndLockWindow::StopFrameCapture()
{
    // Stop capture session first.
    if (m_captureSession)
    {
        m_captureSession.Close();
        m_captureSession = nullptr;
    }

    // Unregister event handlers, then close pool.
    if (m_framePool)
    {
        m_framePool.FrameArrived(m_frameArrivedToken);
        m_framePool.SizeChanged(m_sizeChangedToken);
        m_framePool.Close();
        m_framePool = nullptr;
    }

    m_captureItem = nullptr;

    // Shared memory cleanup.
    if (m_frameData) { UnmapViewOfFile(m_frameData); m_frameData = nullptr; }
    if (m_frameShm) { CloseHandle(m_frameShm); m_frameShm = nullptr; }
    if (m_frameEvent) { CloseHandle(m_frameEvent); m_frameEvent = nullptr; }

    m_previousFrameHash = 0;
    m_consecutiveIdenticalFrames = 0;
    m_rateLimitIntervalMs = 66;
    m_lastFrameTimeMs = 0;

    Logger::info(L"D3D11 GPU capture stopped for stream {}", m_streamId);
}

void ThumbnailCropAndLockWindow::UpdateStreamConfig()
{
}

uint32_t ThumbnailCropAndLockWindow::ComputeFrameHash(const BYTE* pixels, int w, int h, int stride) const
{
    if (!pixels || w <= 0 || h <= 0) return 0;

    uint32_t hash = 0;
    int stepX = std::max(1, w / 32);
    int stepY = std::max(1, h / 32);

    for (int y = 0; y < h; y += stepY)
    {
        for (int x = 0; x < w; x += stepX)
        {
            // Read 4 bytes (BGRA) at this pixel
            uint32_t pixel = *reinterpret_cast<const uint32_t*>(pixels + y * stride + x * 4);
            hash = ((hash << 5) + hash) ^ pixel;
        }
    }
    return hash;
}

// Returns true if the frame content has meaningfully changed since the last capture.
bool ThumbnailCropAndLockWindow::HasContentChanged(const BYTE* pixels, int w, int h, int stride)
{
    uint32_t currentHash = ComputeFrameHash(pixels, w, h, stride);
    bool changed = (currentHash != m_previousFrameHash);
    m_previousFrameHash = currentHash;
    return changed;
}

// Dynamically adjusts the capture interval based on content stability.
bool ThumbnailCropAndLockWindow::ReconnectToTarget()
{
    if (m_destroyed)
    {
        return false;
    }

    HWND targetWindow = FindTargetWindow(m_savedTargetInfo);
    if (targetWindow == nullptr)
    {
        Logger::trace(L"Reconnect: target window not found yet, will retry");
        return false;
    }

    Logger::info(L"Reconnecting to target window: {}", m_savedTargetInfo.targetWindowTitle);

    // Re-register thumbnail with the new target window
    // CropAndLock will call StartWatchdog internally
    CropAndLock(targetWindow, m_originalCropRect);
    return true;
}

HWND ThumbnailCropAndLockWindow::FindTargetWindow(const WindowState& state)
{
    HWND targetWindow = nullptr;

    // Strategy 1: Match by process path + window class + partial title
    if (!state.targetProcessPath.empty())
    {
        struct EnumState
        {
            std::wstring targetPath;
            std::wstring targetClass;
            std::wstring targetTitle;
            HWND foundWindow = nullptr;
        } enumState{ state.targetProcessPath, state.targetWindowClass, state.targetWindowTitle, nullptr };

        auto callback = [](HWND window, LPARAM lparam) -> BOOL {
            auto state = reinterpret_cast<EnumState*>(lparam);
            if (!IsWindowVisible(window))
            {
                return TRUE;
            }

            wchar_t windowClass[256] = {};
            if (GetClassNameW(window, windowClass, static_cast<int>(_countof(windowClass))) == 0)
            {
                return TRUE;
            }
            if (state->targetClass != windowClass)
            {
                return TRUE;
            }

            DWORD windowPid = 0;
            GetWindowThreadProcessId(window, &windowPid);
            HANDLE processHandle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, windowPid);
            if (processHandle != nullptr)
            {
                wchar_t processPath[MAX_PATH] = {};
                DWORD pathSize = MAX_PATH;
                if (QueryFullProcessImageNameW(processHandle, 0, processPath, &pathSize))
                {
                    if (state->targetPath == processPath)
                    {
                        wchar_t windowTitle[256] = {};
                        GetWindowTextW(window, windowTitle, static_cast<int>(_countof(windowTitle)));

                        if (state->targetTitle == windowTitle)
                        {
                            CloseHandle(processHandle);
                            state->foundWindow = window;
                            return FALSE;
                        }

                        if (state->foundWindow == nullptr)
                        {
                            state->foundWindow = window;
                        }
                    }
                }
                CloseHandle(processHandle);
            }
            return TRUE;
        };

        EnumWindows(callback, reinterpret_cast<LPARAM>(&enumState));
        targetWindow = enumState.foundWindow;
    }

    // Strategy 2: Fallback to process name + class if path search failed
    if (targetWindow == nullptr && !state.targetProcessName.empty())
    {
        struct EnumState
        {
            std::wstring targetName;
            std::wstring targetClass;
            HWND foundWindow = nullptr;
        } enumState{ state.targetProcessName, state.targetWindowClass, nullptr };

        auto callback = [](HWND window, LPARAM lparam) -> BOOL {
            auto state = reinterpret_cast<EnumState*>(lparam);
            if (!IsWindowVisible(window))
            {
                return TRUE;
            }

            wchar_t windowClass[256] = {};
            if (GetClassNameW(window, windowClass, static_cast<int>(_countof(windowClass))) == 0)
            {
                return TRUE;
            }
            if (state->targetClass != windowClass)
            {
                return TRUE;
            }

            DWORD windowPid = 0;
            GetWindowThreadProcessId(window, &windowPid);
            HANDLE processHandle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, windowPid);
            if (processHandle != nullptr)
            {
                wchar_t processPath[MAX_PATH] = {};
                DWORD pathSize = MAX_PATH;
                if (QueryFullProcessImageNameW(processHandle, 0, processPath, &pathSize))
                {
                    std::wstring pathStr(processPath);
                    auto lastSlash = pathStr.find_last_of(L"\\/");
                    if (lastSlash != std::wstring::npos)
                    {
                        std::wstring processName = pathStr.substr(lastSlash + 1);
                        if (state->targetName == processName && state->foundWindow == nullptr)
                        {
                            state->foundWindow = window;
                        }
                    }
                }
                CloseHandle(processHandle);
            }
            return TRUE;
        };

        EnumWindows(callback, reinterpret_cast<LPARAM>(&enumState));
        targetWindow = enumState.foundWindow;
    }

    return targetWindow;
}

WindowState ThumbnailCropAndLockWindow::GetState() const
{
    WindowState state;

    // Get window title
    wchar_t title[256] = {};
    GetWindowTextW(m_window, title, static_cast<int>(_countof(title)));
    state.title = title;

    // Get window position
    winrt::check_bool(GetWindowRect(m_window, &state.windowRect));

    // Save original crop rect (NOT the DWM-adjusted one)
    state.originalCropRect = m_originalCropRect;

    // Save window properties
    state.isTopMost = m_isTopMost;
    state.windowType = CropAndLockType::Thumbnail;

    // Save target window information if available
    if (m_currentTarget != nullptr)
    {
        wchar_t targetTitle[256] = {};
        GetWindowTextW(m_currentTarget, targetTitle, static_cast<int>(_countof(targetTitle)));
        state.targetWindowTitle = targetTitle;

        wchar_t targetClass[256] = {};
        GetClassNameW(m_currentTarget, targetClass, static_cast<int>(_countof(targetClass)));
        state.targetWindowClass = targetClass;

        DWORD processId = 0;
        GetWindowThreadProcessId(m_currentTarget, &processId);
        state.targetProcessId = processId;

        // Get process path and name
        HANDLE processHandle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
        if (processHandle != nullptr)
        {
            wchar_t processPath[MAX_PATH] = {};
            DWORD pathSize = MAX_PATH;
            if (QueryFullProcessImageNameW(processHandle, 0, processPath, &pathSize))
            {
                state.targetProcessPath = processPath;

                // Extract exe name from path
                std::wstring pathStr(processPath);
                auto lastSlash = pathStr.find_last_of(L"\\/");
                if (lastSlash != std::wstring::npos)
                {
                    state.targetProcessName = pathStr.substr(lastSlash + 1);
                }
            }
            CloseHandle(processHandle);
        }
    }

    return state;
}

bool ThumbnailCropAndLockWindow::RestoreState(const WindowState& state)
{
    HWND targetWindow = nullptr;

    targetWindow = FindTargetWindow(state);

    if (targetWindow == nullptr)
    {
        Logger::warn(L"Could not find target window for restoration: {} ({})", state.targetWindowTitle, state.targetProcessPath);
        return false;
    }

    // Restore window properties
    m_isTopMost = state.isTopMost;

    // Set up the crop and lock with the ORIGINAL crop rect (not DWM-adjusted)
    CropAndLock(targetWindow, state.originalCropRect);

    // Restore window position AFTER CropAndLock (which may set its own size)
    auto width = state.windowRect.right - state.windowRect.left;
    auto height = state.windowRect.bottom - state.windowRect.top;

    winrt::check_bool(SetWindowPos(
        m_window,
        m_isTopMost ? HWND_TOPMOST : HWND_NOTOPMOST,
        state.windowRect.left,
        state.windowRect.top,
        width,
        height,
        SWP_SHOWWINDOW));

    Logger::trace(L"Restored window state: {}", state.title);
    return true;
}
