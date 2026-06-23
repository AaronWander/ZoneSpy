#pragma once
#include <robmikh.common/DesktopWindow.h>
#include "CropAndLockWindow.h"
#include "WindowState.h"
#include <vector>

// Snap candidate structures
struct SnapCandidateX
{
	int distance;
	LONG targetPos;
	bool isLeft;
};

struct SnapCandidateY
{
	int distance;
	LONG targetPos;
	bool isTop;
};

struct ThumbnailCropAndLockWindow : robmikh::common::desktop::DesktopWindow<ThumbnailCropAndLockWindow>, CropAndLockWindow
{
	static const std::wstring ClassName;
	ThumbnailCropAndLockWindow(std::wstring const& titleString, int width, int height);
	~ThumbnailCropAndLockWindow() override;
	LRESULT MessageHandler(UINT const message, WPARAM const wparam, LPARAM const lparam);

	HWND Handle() override { return m_window; }
	void CropAndLock(HWND windowToCrop, RECT cropRect) override;
	void OnClosed(std::function<void(HWND)> callback) override { m_closedCallback = callback; }

	// Target window search and reconnection
	static HWND FindTargetWindow(const WindowState& state);
	void StopWatchdog();
	void SetClickThrough(bool enable);

	// State management
	WindowState GetState() const;
	bool RestoreState(const WindowState& state);

private:
	static void RegisterWindowClass();

	void Hide();
	void DisconnectTarget();
	bool ReconnectToTarget();
	LRESULT HitTest(POINT const& point);
	void ShowContextMenu(POINT point);
	void SnapMovingRect(RECT& windowRect);
	void SnapSizingRect(RECT& windowRect, WPARAM sizingEdge);

	// Helper functions for collecting snap candidates
	void CollectWindowSnapCandidatesX(const RECT& windowRect, std::vector<SnapCandidateX>& candidates);
	void CollectWindowSnapCandidatesY(const RECT& windowRect, std::vector<SnapCandidateY>& candidates);

	void StartWatchdog();
	void CaptureTargetWindowInfo();

private:
	HWND m_currentTarget = nullptr;
	POINT m_previousPosition = {};

	// Snap state per axis
	struct AxisSnapState
	{
		bool snapped = false;         // Currently snapped on this axis
		LONG snapCursorPos = 0;       // Cursor position when snap occurred
		LONG snapTargetPos = 0;       // The actual edge position we snapped to (left/right for X, top/bottom for Y)
	};

	bool m_isDragging = false;
	AxisSnapState m_snapX = {};
	AxisSnapState m_snapY = {};

	unique_hthumbnail m_thumbnail;
	RECT m_destRect = {};
	RECT m_sourceRect = {};
	RECT m_originalCropRect = {};

	// Watchdog timer for target window reconnection
	UINT_PTR m_watchdogTimer = 0;
	static constexpr UINT_PTR WATCHDOG_TIMER_ID = 1001;
	static constexpr UINT WATCHDOG_INTERVAL_MS = 5000;

	// Saved target window info for reconnection
	WindowState m_savedTargetInfo;

	bool m_destroyed = false;
	bool m_isTopMost = true;
	std::function<void(HWND)> m_closedCallback;

};
