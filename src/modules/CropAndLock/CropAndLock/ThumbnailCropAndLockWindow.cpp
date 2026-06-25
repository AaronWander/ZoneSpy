
// ── Resize Dialog ──────────────────────────────────────────────────

static const wchar_t RESIZE_DLG_CLASS[] = L"ZoneSpy_ResizeDlg";

struct ResizeDlgState
{
    ThumbnailCropAndLockWindow* parent;
    bool locked = false;
    double ratio = 1.0;
    bool updating = false;
    HWND hWidth = nullptr;
    HWND hHeight = nullptr;
    HWND hLock = nullptr;
    HWND hOk = nullptr;
    HWND hCancel = nullptr;
};

LRESULT CALLBACK ResizeDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    auto* state = reinterpret_cast<ResizeDlgState*>(GetWindowLongPtrW(hDlg, GWLP_USERDATA));

    switch (msg)
    {
    case WM_CREATE:
    {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto* initState = static_cast<ResizeDlgState*>(cs->lpCreateParams);
        SetWindowLongPtrW(hDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(initState));
        state = initState;

        HINSTANCE hInst = cs->hInstance;
        int dpi = GetDpiForWindow(hDlg);
        int labelW = MulDiv(50, dpi, 96);
        int editW = MulDiv(160, dpi, 96);
        int editH = MulDiv(24, dpi, 96);
        int btnW = MulDiv(80, dpi, 96);
        int btnH = MulDiv(26, dpi, 96);
        int lockW = MulDiv(30, dpi, 96);
        int margin = MulDiv(12, dpi, 96);
        int row1y = MulDiv(20, dpi, 96);
        int row2y = row1y + MulDiv(35, dpi, 96);
        int btnRowY = row2y + MulDiv(50, dpi, 96);

        RECT pr = {};
        GetWindowRect(state->parent->Handle(), &pr);
        int curW = pr.right - pr.left;
        int curH = pr.bottom - pr.top;

        auto mkWin = [&](const wchar_t* cls, const wchar_t* text, int x, int y, int w, int h, DWORD style, int id) {
            return CreateWindowExW(0, cls, text, style | WS_CHILD | WS_VISIBLE,
                                   x, y, w, h,
                                   hDlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hInst, nullptr);
        };

        // "Width:" label
        mkWin(L"STATIC", L"Width:", margin, row1y + 4, labelW, editH, SS_RIGHT, 0);
        // Width edit
        state->hWidth = mkWin(L"EDIT", std::to_wstring(curW).c_str(),
                              margin + labelW + MulDiv(6, dpi, 96), row1y, editW, editH,
                              WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_NUMBER | ES_RIGHT, 101);
        // "Height:" label
        mkWin(L"STATIC", L"Height:", margin, row2y + 4, labelW, editH, SS_RIGHT, 0);
        // Height edit
        state->hHeight = mkWin(L"EDIT", std::to_wstring(curH).c_str(),
                               margin + labelW + MulDiv(6, dpi, 96), row2y, editW, editH,
                               WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_NUMBER | ES_RIGHT, 102);
        // Lock button (unlocked)
        state->hLock = mkWin(L"BUTTON", L"\xf0\x9f\x94\x93",
                             margin + labelW + MulDiv(6, dpi, 96) + editW + MulDiv(8, dpi, 96), row2y, lockW, editH,
                             WS_CHILD | WS_VISIBLE | BS_PUSHLIKE | BS_CENTER | WS_TABSTOP, 103);
        // OK button
        state->hOk = mkWin(L"BUTTON", L"OK",
                           margin + MulDiv(150, dpi, 96), btnRowY, btnW, btnH,
                           WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP, 104);
        // Cancel button
        state->hCancel = mkWin(L"BUTTON", L"Cancel",
                               margin + MulDiv(150, dpi, 96) + btnW + MulDiv(8, dpi, 96), btnRowY, btnW, btnH,
                               WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, 105);

        if (curW > 0 && curH > 0)
            state->ratio = static_cast<double>(curW) / curH;

        // Set initial font for edit controls
        HFONT hFont = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        SendMessageW(state->hWidth, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);
        SendMessageW(state->hHeight, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);
        SendMessageW(state->hLock, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);
        SendMessageW(state->hOk, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);
        SendMessageW(state->hCancel, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);

        return 0;
    }

    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);
        if (!state) break;

        if (id == 103 && code == BN_CLICKED)
        {
            state->locked = !state->locked;
            if (state->locked)
            {
                wchar_t buf[64];
                GetWindowTextW(state->hWidth, buf, 64);
                int w = _wtoi(buf);
                GetWindowTextW(state->hHeight, buf, 64);
                int h = _wtoi(buf);
                if (w > 0 && h > 0)
                    state->ratio = static_cast<double>(w) / h;
                SetWindowTextW(state->hLock, L"\xf0\x9f\x94\x92");
            }
            else
            {
                SetWindowTextW(state->hLock, L"\xf0\x9f\x94\x93");
            }
        }
        else if (id == 104 && code == BN_CLICKED)
        {
            wchar_t buf[64];
            GetWindowTextW(state->hWidth, buf, 64);
            int w = _wtoi(buf);
            GetWindowTextW(state->hHeight, buf, 64);
            int h = _wtoi(buf);
            if (w > 0 && h > 0 && w <= 3840 && h <= 2160)
            {
                SetWindowPos(state->parent->Handle(), nullptr, 0, 0, w, h, SWP_NOMOVE | SWP_NOZORDER);
                DestroyWindow(hDlg);
            }
            else
            {
                MessageBeep(MB_ICONWARNING);
            }
        }
        else if (id == 105 && code == BN_CLICKED)
        {
            DestroyWindow(hDlg);
        }
        else if ((id == 101 || id == 102) && code == EN_CHANGE)
        {
            if (state->updating || !state->locked) break;
            state->updating = true;

            wchar_t buf[64];
            if (id == 101)
            {
                GetWindowTextW(state->hWidth, buf, 64);
                int w = _wtoi(buf);
                if (w > 0)
                {
                    int newH = static_cast<int>(w / state->ratio);
                    if (newH < 1) newH = 1;
                    if (newH > 2160) newH = 2160;
                    SetWindowTextW(state->hHeight, std::to_wstring(newH).c_str());
                }
            }
            else
            {
                GetWindowTextW(state->hHeight, buf, 64);
                int h = _wtoi(buf);
                if (h > 0)
                {
                    int newW = static_cast<int>(h * state->ratio);
                    if (newW < 1) newW = 1;
                    if (newW > 3840) newW = 3840;
                    SetWindowTextW(state->hWidth, std::to_wstring(newW).c_str());
                }
            }
            state->updating = false;
        }
        return 0;
    }

    case WM_CLOSE:
        DestroyWindow(hDlg);
        return 0;

    case WM_DESTROY:
        delete state;
        return 0;
    }
    return DefWindowProcW(hDlg, msg, wParam, lParam);
}

void ThumbnailCropAndLockWindow::ShowResizeDialog()
{
    static bool registered = false;
    if (!registered)
    {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = ResizeDlgProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = RESIZE_DLG_CLASS;
        RegisterClassExW(&wc);
        registered = true;
    }

    auto* state = new ResizeDlgState();
    state->parent = this;

    int dpi = GetDpiForWindow(m_window);
    int dlgW = MulDiv(300, dpi, 96);
    int dlgH = MulDiv(140, dpi, 96);

    RECT pr = {};
    GetWindowRect(m_window, &pr);
    int x = pr.left + ((pr.right - pr.left) - dlgW) / 2;
    int y = pr.top + ((pr.bottom - pr.top) - dlgH) / 2;

    HWND hDlg = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_APPWINDOW,
                                RESIZE_DLG_CLASS, L"Resize Window",
                                WS_CAPTION | WS_POPUP | WS_SYSMENU | WS_VISIBLE,
                                x, y, dlgW, dlgH,
                                m_window, nullptr, GetModuleHandleW(nullptr), state);

    if (hDlg)
    {
        SetForegroundWindow(hDlg);
        SetFocus(state->hWidth);
        EnableWindow(m_window, FALSE);
    }
    else
    {
        delete state;
    }
}
