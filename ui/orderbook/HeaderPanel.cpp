#include "HeaderPanel.h"
#include <windows.h>

// =======================================================
// GLOBAL / STATIC
// =======================================================

static HFONT g_hHeaderFont = NULL;

// Logic perbandingan harga
static PriceBaseColor CalcBaseColor(double value, double prev) {
  if (prev <= 0.0001 || value <= 0.0001) return PriceBaseColor::NEUTRAL;
  
  if (value > prev) return PriceBaseColor::UP;
  if (value < prev) return PriceBaseColor::DOWN;
  
  return PriceBaseColor::FLAT; // Harga sama (Kuning)
}

// Helper khusus buat Change (vs 0, bukan prev)
static PriceBaseColor CalcChgColor(double chg) {
  if (chg > 0) return PriceBaseColor::UP;
  if (chg < 0) return PriceBaseColor::DOWN;
  return PriceBaseColor::NEUTRAL; // 0
}

static COLORREF GetBaseColor(PriceBaseColor c) {
  switch (c) {
    case PriceBaseColor::UP:    return RGB(0, 166, 62);    // Hijau lebih deep
    case PriceBaseColor::DOWN:  return RGB(220, 0, 0);    // Merah
    case PriceBaseColor::FLAT:  return RGB(151, 145, 52);  // Kuning/Emas
    default:                    return RGB(0, 0, 0);      // Hitam (Netral)
  }
}

static HeaderState* GetState(HWND hWnd) {
  return (HeaderState*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
}

// =======================================================
// HEADER PANEL WINDOW PROCEDURE
// =======================================================
static LRESULT CALLBACK HeaderPanelProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_CREATE: {
      LOGFONT lf = {};
      lf.lfHeight = -12; // ~9pt
      lf.lfWeight = FW_BOLD;
      strcpy_s(lf.lfFaceName, "Arial");

      g_hHeaderFont = CreateFontIndirect(&lf);

      HeaderState* state = new HeaderState();
      SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)state);
      return 0;
    }

    case WM_ERASEBKGND:
      return TRUE; // Cegah flicker

    case WM_PAINT: {
      HeaderState* state = GetState(hWnd);
      if (!state) break;

      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hWnd, &ps);

      // --- Double Buffer Setup ---
      RECT rc;
      GetClientRect(hWnd, &rc);
      
      // Optimasi: CreateCompatibleDC itu ringan, tapi Bitmap agak berat.
      // Tapi untuk UI sekecil header, recreate tiap frame masih sangat aman (microsecond).
      HDC memDC = CreateCompatibleDC(hdc);
      HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
      HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, bmp);

      SelectObject(memDC, g_hHeaderFont);
      SetBkMode(memDC, TRANSPARENT);

      // Fill Background (Warna Dialog Standard)
      FillRect(memDC, &rc, GetSysColorBrush(COLOR_BTNFACE));

      // --- Warna Label (Constant) ---
      COLORREF clrLabel = RGB(80, 80, 80); // Abu Tua biar elegan

      // --- Layout Grid Setup (3 Kolom x 3 Baris) ---
      int W = rc.right;
      int H = rc.bottom;
      int colW = W / 3;
      int rowH = H / 3;
      
      // Padding biar teks gak nempel pinggir
      int padL = 5; // Padding Left
      int padR = 5; // Padding Right

      // Helper Macro buat gambar biar kodenya rapi
      // LABEL (Kiri, Abu2) | VALUE (Kanan, Warna Warni)
      #define DRAW_CELL(row, col, label, valText, colorEnum) \
      { \
        RECT rLabel = { col * colW + padL, row * rowH, (col + 1) * colW, (row + 1) * rowH }; \
        RECT rVal   = { col * colW, row * rowH, (col + 1) * colW - padR, (row + 1) * rowH }; \
        \
        /* 1. Gambar Label */ \
        SetTextColor(memDC, clrLabel); \
        DrawTextA(memDC, label, -1, &rLabel, DT_LEFT | DT_VCENTER | DT_SINGLELINE); \
        \
        /* 2. Gambar Value */ \
        SetTextColor(memDC, GetBaseColor(colorEnum)); \
        DrawTextA(memDC, valText.c_str(), -1, &rVal, DT_RIGHT | DT_VCENTER | DT_SINGLELINE); \
      }

      // --- ROW 1 ---
      DRAW_CELL(0, 0, "Last", state->last, state->last_base);
      DRAW_CELL(0, 1, "Open", state->open, state->open_base);
      DRAW_CELL(0, 2, "Lot",  state->lot,  PriceBaseColor::NEUTRAL);

      // --- ROW 2 ---
      DRAW_CELL(1, 0, "Prev", state->prev, PriceBaseColor::NEUTRAL);
      DRAW_CELL(1, 1, "High", state->high, state->high_base);
      DRAW_CELL(1, 2, "Val",  state->val,  PriceBaseColor::NEUTRAL);

      // --- ROW 3 ---
      DRAW_CELL(2, 0, "Chg",  state->pct,  state->chg_base); // Pake pct/chg base
      DRAW_CELL(2, 1, "Low",  state->low,  state->low_base);
      DRAW_CELL(2, 2, "Freq", state->freq, PriceBaseColor::NEUTRAL); // Atau mau biru? :D

        // --- Blit ke Layar ---
        BitBlt(hdc, 0, 0, W, H, memDC, 0, 0, SRCCOPY);

        // Cleanup
        SelectObject(memDC, oldBmp);
        DeleteObject(bmp);
        DeleteDC(memDC);

        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_DESTROY: {
      HeaderState* state = GetState(hWnd);
      if (state) delete state;
      SetWindowLongPtr(hWnd, GWLP_USERDATA, 0);
      
      // Hapus global font kalau perlu (biasanya sih di AppExit, tapi gpp disini kalau panel cuma 1)
      if (g_hHeaderFont) { 
        DeleteObject(g_hHeaderFont); 
        g_hHeaderFont = NULL; 
      }
      return 0;
    }
  }
  return DefWindowProc(hWnd, msg, wParam, lParam);
}

// =======================================================
// PUBLIC API
// =======================================================

HWND CreateHeaderPanel(HWND hParent, HINSTANCE hInst, int x, int y, int w, int h) {
    static bool registered = false;
    if (!registered) {
        WNDCLASS wc = {};
        wc.lpfnWndProc = HeaderPanelProc;
        wc.hInstance = hInst;
        wc.lpszClassName = "OrderbookHeaderPanel";
        wc.hbrBackground = NULL; // No background brush (kita handle di WM_PAINT)
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        RegisterClass(&wc);
        registered = true;
    }

    return CreateWindowEx(0, "OrderbookHeaderPanel", NULL,
        WS_CHILD | WS_VISIBLE, x, y, w, h, hParent, NULL, hInst, NULL);
}

void UpdateHeaderPanel(HWND hPanel, const HeaderState& data) {
    if (!hPanel) return;
    HeaderState* state = (HeaderState*)GetWindowLongPtr(hPanel, GWLP_USERDATA);
    if (!state) return;

    // [FIX 2] Copy DATA DULUAN, baru hitung warna!
    // Supaya hasil hitungan gak ketimpa data mentah (yang basenya NEUTRAL semua)
    *state = data; 

    // Hitung warna
    state->last_base = CalcBaseColor(state->last_num, state->prev_num);
    state->open_base = CalcBaseColor(state->open_num, state->prev_num);
    state->high_base = CalcBaseColor(state->high_num, state->prev_num);
    state->low_base  = CalcBaseColor(state->low_num,  state->prev_num);
    
    // Logic khusus Chg (vs 0)
    state->chg_base  = CalcChgColor(state->chg_num);

    // Repaint
    InvalidateRect(hPanel, NULL, FALSE);
}