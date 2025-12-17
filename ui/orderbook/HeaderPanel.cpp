#include "HeaderPanel.h"
#include <windows.h>

// =======================================================
// GLOBAL / STATIC
// =======================================================

// Font dipakai bersama (1x create, reuse)
static HFONT g_hHeaderFont = NULL;

static PriceBaseColor CalcBaseColor(double value, double prev) {
  if (prev <= 0 || value >= 0) return PriceBaseColor::NEUTRAL;

  if (value > prev) return PriceBaseColor::UP;
  if (value < prev) return PriceBaseColor::DOWN;

  return PriceBaseColor::FLAT;
}

static COLORREF GetBaseColor(PriceBaseColor c) {
  switch (c) {
    case PriceBaseColor::UP:    return RGB(0, 170, 0);
    case PriceBaseColor::DOWN:  return RGB(200, 0, 0);
    case PriceBaseColor::FLAT:  return RGB(255, 140, 0);
    default:  return RGB(0, 0, 0);
  }
}

// Helper ambil state dari window
static HeaderState* GetState(HWND hWnd) {
  return (HeaderState*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
}

// =======================================================
// HEADER PANEL WINDOW PROCEDURE
// =======================================================
static LRESULT CALLBACK HeaderPanelProc( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam ) {
    switch (msg) {
      case WM_CREATE:
      {
        // =========================================
        // INIT FONT (sekali saja per panel)
        // =========================================
        LOGFONT lf = {};
        lf.lfHeight = -12;          // ~9pt
        lf.lfWeight = FW_BOLD;
        strcpy_s(lf.lfFaceName, "Arial");

        g_hHeaderFont = CreateFontIndirect(&lf);

        // =========================================
        // ALOKASI STATE (DATA HEADER)
        // Disimpan di GWLP_USERDATA
        // =========================================
        HeaderState* state = new HeaderState();
        SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)state);

        return 0;
      }

      case WM_ERASEBKGND:
        // SUPER PENTING
        // Matikan erase background bawaan Windows
        // = NO FLICKER
        return TRUE;

      case WM_PAINT:
      {
        HeaderState* state = GetState(hWnd);
        if (!state) break;

        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        // =========================================
        // DOUBLE BUFFER SETUP
        // =========================================
        RECT rc;
        GetClientRect(hWnd, &rc);

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(
          hdc,
          rc.right - rc.left,
          rc.bottom - rc.top
        );

        HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, bmp);

        SelectObject(memDC, g_hHeaderFont);
        SetBkMode(memDC, TRANSPARENT);

        // Background (ikut tema dialog)
        FillRect(memDC, &rc, (HBRUSH)(COLOR_BTNFACE + 1));

        // =========================================
        // LAYOUT GRID MANUAL (3x3)
        // =========================================
        int colW = (rc.right - rc.left) / 3;
        int rowH = (rc.bottom - rc.top) / 3;
        double space = colW * 0.1;

        RECT r;

        // ---------- ROW 1 ----------
        // Last
        r = { 5, 0, colW, rowH };
        DrawTextA( memDC, ("Last "), -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE );

        r = { 0, 0, static_cast<LONG>(colW * 0.9), rowH };
        SetTextColor(memDC, GetBaseColor(state->last_base));
        DrawTextA(
          memDC,
          state->last.c_str(),
          -1,
          &r,
          DT_RIGHT | DT_VCENTER | DT_SINGLELINE
        );

        // Open
        r = { static_cast<LONG>(colW + space), 0, static_cast<LONG>((colW * 2) + space), rowH };
        DrawTextA( memDC, ("Open "), -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE );

        r = { colW, 0, static_cast<LONG>((colW * 2) - space), rowH };
        SetTextColor(memDC, GetBaseColor(state->open_base));
        DrawTextA(
          memDC,
          (state->open).c_str(),
          -1,
          &r,
          DT_RIGHT | DT_VCENTER | DT_SINGLELINE
        );

        // Lot
        r = { static_cast<LONG>((colW * 2) + space), 0, rc.right, rowH };
        DrawTextA( memDC, ("Lot "), -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE );

        r = { static_cast<LONG>((colW * 2) + space), 0, rc.right, rowH };
        DrawTextA(
          memDC,
          state->lot.c_str(),
          -1,
          &r,
          DT_RIGHT | DT_VCENTER | DT_SINGLELINE
        );

        /*r = { colW * 2, 0, rc.right, rowH };
        DrawTextA(
          memDC,
          state->pct.c_str(),
          -1,
          &r,
          DT_RIGHT | DT_VCENTER | DT_SINGLELINE
        );*/

        // ---------- ROW 2 ----------
        r = { 5, rowH, colW, rowH * 2 };
        DrawTextA( memDC, ("Prev "), -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE );

        r = { 5, rowH, static_cast<LONG>(colW * 0.9), rowH * 2 };
        DrawTextA(
          memDC,
          (state->prev).c_str(),
          -1,
          &r,
          DT_RIGHT | DT_VCENTER | DT_SINGLELINE
        );

        // High
        r = { static_cast<LONG>(colW + space), rowH, static_cast<LONG>((colW * 2) + space), rowH * 2 };
        DrawTextA( memDC, ("High "), -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE );

        r = { colW, rowH, static_cast<LONG>((colW * 2) - space), rowH * 2 };
        DrawTextA(
          memDC,
          (state->high).c_str(),
          -1,
          &r,
          DT_RIGHT | DT_VCENTER | DT_SINGLELINE
        );

        // Val
        r = { static_cast<LONG>((colW * 2) + space), rowH, rc.right, rowH * 2 };
        DrawTextA( memDC, ("Val "), -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE );

        r = { static_cast<LONG>((colW * 2) - space), rowH, rc.right, rowH * 2 };
        DrawTextA(
          memDC,
          (state->val).c_str(),
          -1,
          &r,
          DT_RIGHT | DT_VCENTER | DT_SINGLELINE
        );

        // ---------- ROW 3 ----------
        // Chg
        r = { 5, rowH * 2, colW, rc.bottom };
        DrawTextA( memDC, ("Chg "), -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE );

        r = { 5, rowH * 2, static_cast<LONG>(colW * 0.9), rc.bottom };
        DrawTextA(
          memDC,
          state->pct.c_str(),
          -1,
          &r,
          DT_RIGHT | DT_VCENTER | DT_SINGLELINE
        );

        // Low
        r = { static_cast<LONG>(colW + space), rowH * 2, static_cast<LONG>((colW * 2) + space), rc.bottom };
        DrawTextA( memDC, ("Low "), -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE );

        r = { colW, rowH * 2, static_cast<LONG>((colW * 2) - space), rc.bottom };
        DrawTextA(
          memDC,
          state->low.c_str(),
          -1,
          &r,
          DT_RIGHT | DT_VCENTER | DT_SINGLELINE
        );

        // Freq
        r = { static_cast<LONG>((colW * 2) + space), rowH * 2, rc.right, rc.bottom };
        DrawTextA( memDC, ("Freq "), -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE );

        r = { colW * 2, rowH * 2, rc.right, rc.bottom };
        DrawTextA(
          memDC,
          state->freq.c_str(),
          -1,
          &r,
          DT_RIGHT | DT_VCENTER | DT_SINGLELINE
        );

        // =========================================
        // BLIT SEKALI KE SCREEN
        // =========================================
        BitBlt(
          hdc,
          0, 0,
          rc.right - rc.left,
          rc.bottom - rc.top,
          memDC,
          0, 0,
          SRCCOPY
        );

        // Cleanup GDI
        SelectObject(memDC, oldBmp);
        DeleteObject(bmp);
        DeleteDC(memDC);

        EndPaint(hWnd, &ps);
        return 0;
      }

      case WM_DESTROY:
      {
        // =========================================
        // CLEANUP STATE
        // =========================================
        HeaderState* state = GetState(hWnd);
        delete state;
        SetWindowLongPtr(hWnd, GWLP_USERDATA, 0);
        return 0;
      }
      }

    return DefWindowProc(hWnd, msg, wParam, lParam);
}

// =======================================================
// PUBLIC API
// =======================================================

// Register class + create panel
HWND CreateHeaderPanel( HWND hParent, HINSTANCE hInst, int x, int y, int w, int h ) {
  static bool registered = false;

  if (!registered) {
    WNDCLASS wc = {};
    wc.lpfnWndProc = HeaderPanelProc;
    wc.hInstance = hInst;
    wc.lpszClassName = "OrderbookHeaderPanel";
    wc.hbrBackground = NULL; // kita gambar sendiri
    RegisterClass(&wc);

    registered = true;
  }

  HWND hWnd = CreateWindowEx(
    0,
    "OrderbookHeaderPanel",
    NULL,
    WS_CHILD | WS_VISIBLE,
    x, y, w, h,
    hParent,
    NULL,
    hInst,
    NULL
  );

  return hWnd;
}

// Update data + repaint
void UpdateHeaderPanel(HWND hPanel, const HeaderState& data) {
  if (!hPanel) return;

  HeaderState* state = (HeaderState*)GetWindowLongPtr(hPanel, GWLP_USERDATA);

  if (!state) return;

  state->last_base = CalcBaseColor(data.last_num, data.prev_num);
  state->open_base = CalcBaseColor(data.open_num, data.prev_num);
  state->high_base = CalcBaseColor(data.high_num, data.prev_num);
  state->low_base = CalcBaseColor(data.low_num, data.prev_num);
  state->chg_base = CalcBaseColor(data.chg_num, 0);

  // Copy data
  *state = data;

  // Trigger repaint (NO ERASE)
  InvalidateRect(hPanel, NULL, FALSE);
}
