// Click-through, non-activating HUD, using the same layered-window approach
// as the reinforcement overlay. All calls run on the game window thread.
static HWND speed_hud;
static ATOM speed_hud_class;
static HFONT speed_hud_font;
static int hud_height, hud_percent;
static bool hud_highlight, hud_slowmo;
static ULONGLONG speed_changed_at;
static bool speed_was_changed;

static LRESULT CALLBACK speed_hud_proc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_NCHITTEST) return HTTRANSPARENT;
    if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_PAINT) {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(window, &paint);
        RECT area;
        GetClientRect(window, &area);
        // Black is the transparency key; only the percentage is visible.
        FillRect(dc, &area, (HBRUSH)GetStockObject(BLACK_BRUSH));
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, hud_highlight ? RGB(255, 219, 139) : RGB(231, 229, 219));
        HGDIOBJ previous = SelectObject(dc, speed_hud_font ? speed_hud_font : GetStockObject(DEFAULT_GUI_FONT));
        char label[16];
        snprintf(label, sizeof(label), "%d%%", hud_slowmo ? 30 : hud_percent);
        DrawTextA(dc, label, -1, &area, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, previous);
        EndPaint(window, &paint);
        return 0;
    }
    return DefWindowProcW(window, message, wp, lp);
}

static void update_speed_hud(HWND owner, bool active) {
    ULONGLONG age = GetTickCount64() - speed_changed_at;
    if (!active || (!slowmo_override && percent == 100 && (!speed_was_changed || age >= 2000))) {
        if (speed_hud && IsWindowVisible(speed_hud)) ShowWindow(speed_hud, SW_HIDE);
        return;
    }
    if (!speed_hud) {
        if (!speed_hud_class) {
            WNDCLASSW cls = {0};
            cls.lpfnWndProc = speed_hud_proc;
            cls.hInstance = self;
            cls.lpszClassName = L"DK2SimulationSpeedHUD";
            speed_hud_class = RegisterClassW(&cls);
            if (!speed_hud_class) return;
        }
        speed_hud = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
            MAKEINTATOM(speed_hud_class), L"", WS_POPUP, 0, 0, 1, 1, owner, NULL, self, NULL);
        if (!speed_hud) return;
        if (!SetLayeredWindowAttributes(speed_hud, RGB(0, 0, 0), 255, LWA_COLORKEY)) {
            DestroyWindow(speed_hud);
            speed_hud = NULL;
            return;
        }
    }
    RECT client;
    POINT origin = {0, 0};
    if (!GetClientRect(owner, &client) || !ClientToScreen(owner, &origin)) return;
    int height = MulDiv(client.bottom, 16, 1440);
    if (height < 14) height = 14;
    int width = height * 3;
    bool repaint = hud_slowmo != slowmo_override || hud_percent != percent || hud_highlight != (speed_was_changed && age < 700);
    if (height != hud_height) {
        if (speed_hud_font) DeleteObject(speed_hud_font);
        speed_hud_font = CreateFontW(-MulDiv(height, 85, 100), 0, 0, 0, FW_SEMIBOLD,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        hud_height = height;
        repaint = true;
    }
    hud_percent = percent;
    hud_slowmo = slowmo_override;
    hud_highlight = speed_was_changed && age < 700;
    // Match the bottom-centred clock field in the supplied HUD screenshot:
    // its right bracket is about 117 virtual units right of centre, and
    // its bottom is 30 units above the screen edge. Keep the percentage above the frame line.
    int right = origin.x + client.right / 2 + MulDiv(client.bottom, 108, 1440);
    int x = right - width;
    int bottom_gap = MulDiv(client.bottom, 44, 1440);
    int y = origin.y + client.bottom - height - bottom_gap;
    RECT previous;
    if (!IsWindowVisible(speed_hud) || !GetWindowRect(speed_hud, &previous) ||
        previous.left != x || previous.top != y || previous.right - previous.left != width ||
        previous.bottom - previous.top != height) {
        SetWindowPos(speed_hud, HWND_TOP, x, y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        repaint = true;
    }
    if (repaint) {
        InvalidateRect(speed_hud, NULL, FALSE);
        UpdateWindow(speed_hud);
    }
}

static void destroy_speed_hud(void) {
    if (speed_hud) DestroyWindow(speed_hud);
    if (speed_hud_font) DeleteObject(speed_hud_font);
    if (speed_hud_class) UnregisterClassW(MAKEINTATOM(speed_hud_class), self);
}
