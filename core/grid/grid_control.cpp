#include "grid_control.h"
#include <windowsx.h>
#include <algorithm>
#include <stdio.h>

const char* GRID_CLASS_NAME = "ValkyrieGridCtrl";

// --- Internal State Wrapper ---
struct GridState {
  std::vector<GridColumn> columns;
  std::vector<GridRow> rows;
  GridFooter footer;
  
  // Resources
  HFONT hFont = NULL;
  HFONT hFontBold = NULL;

  // Scrolling
  int scrollY = 0;
  int totalContentHeight = 0;
  int rowHeight = 22;
  int headerHeight = 26;

  // Resizing
  bool isResizing = false;
  int resizeColIdx = -1;
  int resizeStartX = 0;
  int resizeStartWidth = 0;
  int currentXorLineX = -1;
  
  // Selection (NEW!)
  int selectedRow = -1; // -1 = tidak ada yang selected
};

GridState* GetGridState(HWND hWnd) {
  return (GridState*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
}

void GridControl::Register(HINSTANCE hInst) {
  WNDCLASSEX wc = { 0 };
  if (GetClassInfoEx(hInst, GRID_CLASS_NAME, &wc)) return;

  wc.cbSize = sizeof(WNDCLASSEX);
  wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS | CS_GLOBALCLASS;
  wc.lpfnWndProc = GridWndProc;
  wc.hInstance = hInst;
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);
  wc.lpszClassName = GRID_CLASS_NAME;
  wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

  RegisterClassEx(&wc);
}

void GridControl::Unregister(HINSTANCE hInst) {
  UnregisterClass(GRID_CLASS_NAME, hInst);
}

HWND GridControl::Create(HWND hParent, int x, int y, int w, int h, int id) {
  HINSTANCE hInst = (HINSTANCE)GetWindowLongPtr(hParent, GWLP_HINSTANCE);
  
  HWND hWnd = CreateWindowEx(
    NULL, GRID_CLASS_NAME, NULL,
    WS_BORDER | WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_VSCROLL | WS_TABSTOP, 
    x, y, w, h, hParent, (HMENU)(UINT_PTR)id, hInst, NULL
  );

  return hWnd;
}

// --- Setters ---

void GridControl::SetColumns(HWND hWnd, const std::vector<GridColumn>& cols) {
  GridState* s = GetGridState(hWnd);
  if (s) { 
    s->columns = cols; 
    Redraw(hWnd); 
  }
}

void GridControl::SetRows(HWND hWnd, const std::vector<GridRow>& rows) {
  GridState* s = GetGridState(hWnd);
  if (s) { 
    s->rows = rows;
    // Reset selection kalau index out of bounds
    if (s->selectedRow >= (int)rows.size()) {
      s->selectedRow = -1;
    }
    UpdateScrollBars(hWnd);
    Redraw(hWnd); 
  }
}

void GridControl::SetFooter(HWND hWnd, const GridFooter& footer) {
  GridState* s = GetGridState(hWnd);
  if (s) { s->footer = footer; Redraw(hWnd); }
}

void GridControl::SetSelectedRow(HWND hWnd, int rowIndex) {
  GridState* s = GetGridState(hWnd);
  if (!s) return;
  
  if (rowIndex < -1 || rowIndex >= (int)s->rows.size()) {
    s->selectedRow = -1;
  } else {
    s->selectedRow = rowIndex;
    EnsureRowVisible(hWnd, rowIndex);
  }
  Redraw(hWnd);
}

int GridControl::GetSelectedRow(HWND hWnd) {
  GridState* s = GetGridState(hWnd);
  return s ? s->selectedRow : -1;
}

void GridControl::Redraw(HWND hWnd) {
  if (IsWindow(hWnd)) {
    InvalidateRect(hWnd, NULL, FALSE); 
    UpdateWindow(hWnd);
  }
}

void GridControl::UpdateScrollBars(HWND hWnd) {
  GridState* s = GetGridState(hWnd);
  if (!s) return;

  RECT rc;
  GetClientRect(hWnd, &rc);

  int viewHeight = rc.bottom - s->headerHeight;
  if (s->footer.visible) viewHeight -= s->footer.height;

  s->totalContentHeight = (int)s->rows.size() * s->rowHeight;

  SCROLLINFO si = {0};
  si.cbSize = sizeof(SCROLLINFO);
  si.fMask = SIF_ALL;
  si.nMin = 0;
  si.nMax = s->totalContentHeight;
  si.nPage = (viewHeight > 0) ? viewHeight : 0;
  si.nPos = s->scrollY;

  if (s->totalContentHeight <= viewHeight) {
    s->scrollY = 0;
    si.nPos = 0;
  }

  SetScrollInfo(hWnd, SB_VERT, &si, TRUE);
}

// --- Helper: Auto-scroll ke row yang dipilih ---
void GridControl::EnsureRowVisible(HWND hWnd, int rowIndex) {
  GridState* s = GetGridState(hWnd);
  if (!s || rowIndex < 0 || rowIndex >= (int)s->rows.size()) return;

  RECT rc;
  GetClientRect(hWnd, &rc);
  
  int viewHeight = rc.bottom - s->headerHeight;
  if (s->footer.visible) viewHeight -= s->footer.height;

  int rowTop = rowIndex * s->rowHeight;
  int rowBottom = rowTop + s->rowHeight;

  // Kalau row di atas layar, scroll ke atas
  if (rowTop < s->scrollY) {
    s->scrollY = rowTop;
  }
  // Kalau row di bawah layar, scroll ke bawah
  else if (rowBottom > s->scrollY + viewHeight) {
    s->scrollY = rowBottom - viewHeight;
  }

  // Clamp scroll
  SCROLLINFO si = {0};
  si.cbSize = sizeof(SCROLLINFO);
  si.fMask = SIF_ALL;
  GetScrollInfo(hWnd, SB_VERT, &si);
  
  int maxScroll = si.nMax - (int)si.nPage;
  if (maxScroll < 0) maxScroll = 0;
  if (s->scrollY < 0) s->scrollY = 0;
  if (s->scrollY > maxScroll) s->scrollY = maxScroll;

  SetScrollPos(hWnd, SB_VERT, s->scrollY, TRUE);
}

// --- Helpers Logic ---
void GridControl::DrawXorLine(HWND hWnd, int x) {
  HDC hdc = GetDC(hWnd);
  SetROP2(hdc, R2_NOT);
  HPEN hPen = CreatePen(PS_DOT, 1, RGB(0, 0, 0));
  HGDIOBJ hOld = SelectObject(hdc, hPen);

  RECT rc;
  GetClientRect(hWnd, &rc);
  MoveToEx(hdc, x, 0, NULL);
  LineTo(hdc, x, rc.bottom);

  SelectObject(hdc, hOld);
  DeleteObject(hPen);
  ReleaseDC(hWnd, hdc);
}

// --- Main Window Proc ---
LRESULT CALLBACK GridControl::GridWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
  GridState* s = GetGridState(hWnd);

  switch (message) {
    case WM_NCCREATE: {
      GridState* newState = new GridState();
      SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)newState);
      
      LOGFONT lf = { 0 };
      lf.lfHeight = -12;
      lf.lfWeight = FW_NORMAL;
      strcpy_s(lf.lfFaceName, "Arial");
      newState->hFont = CreateFontIndirect(&lf);
      lf.lfWeight = FW_BOLD;
      newState->hFontBold = CreateFontIndirect(&lf);
      return TRUE;
    }

    case WM_DESTROY: {
      if (s) {
        if (s->hFont) DeleteObject(s->hFont);
        if (s->hFontBold) DeleteObject(s->hFontBold);
        delete s;
        SetWindowLongPtr(hWnd, GWLP_USERDATA, 0);
      }
      return 0;
    }

    case WM_SIZE:
      UpdateScrollBars(hWnd);
      return 0;

    // -------------------------------------------------------------
    // FITUR KEYBOARD NAVIGATION (NEW!)
    // -------------------------------------------------------------
    case WM_KEYDOWN: {
      if (!s) break;
      
      switch (wParam) {
        case VK_UP: {
          if (s->selectedRow > 0) {
            s->selectedRow--;
            EnsureRowVisible(hWnd, s->selectedRow);
            InvalidateRect(hWnd, NULL, FALSE);
            UpdateWindow(hWnd);
          } else if (s->selectedRow == -1 && s->rows.size() > 0) {
            // Kalau belum ada yang selected, pilih row terakhir
            s->selectedRow = (int)s->rows.size() - 1;
            EnsureRowVisible(hWnd, s->selectedRow);
            InvalidateRect(hWnd, NULL, FALSE);
            UpdateWindow(hWnd);
          }
          return 0;
        }
        
        case VK_DOWN: {
          if (s->selectedRow < (int)s->rows.size() - 1) {
            s->selectedRow++;
            EnsureRowVisible(hWnd, s->selectedRow);
            InvalidateRect(hWnd, NULL, FALSE);
            UpdateWindow(hWnd);
          } else if (s->selectedRow == -1 && s->rows.size() > 0) {
            // Kalau belum ada yang selected, pilih row pertama
            s->selectedRow = 0;
            EnsureRowVisible(hWnd, s->selectedRow);
            InvalidateRect(hWnd, NULL, FALSE);
            UpdateWindow(hWnd);
          }
          return 0;
        }
        
        case VK_HOME: {
          if (s->rows.size() > 0) {
            s->selectedRow = 0;
            EnsureRowVisible(hWnd, s->selectedRow);
            InvalidateRect(hWnd, NULL, FALSE);
            UpdateWindow(hWnd);
          }
          return 0;
        }
        
        case VK_END: {
          if (s->rows.size() > 0) {
            s->selectedRow = (int)s->rows.size() - 1;
            EnsureRowVisible(hWnd, s->selectedRow);
            InvalidateRect(hWnd, NULL, FALSE);
            UpdateWindow(hWnd);
          }
          return 0;
        }
        
        case VK_PRIOR: { // Page Up
          if (s->rows.size() > 0) {
            RECT rc;
            GetClientRect(hWnd, &rc);
            int viewHeight = rc.bottom - s->headerHeight;
            if (s->footer.visible) viewHeight -= s->footer.height;
            
            int pageRows = viewHeight / s->rowHeight;
            s->selectedRow = max(0, s->selectedRow - pageRows);
            
            EnsureRowVisible(hWnd, s->selectedRow);
            InvalidateRect(hWnd, NULL, FALSE);
            UpdateWindow(hWnd);
          }
          return 0;
        }
        
        case VK_NEXT: { // Page Down
          if (s->rows.size() > 0) {
            RECT rc;
            GetClientRect(hWnd, &rc);
            int viewHeight = rc.bottom - s->headerHeight;
            if (s->footer.visible) viewHeight -= s->footer.height;
            
            int pageRows = viewHeight / s->rowHeight;
            s->selectedRow = min((int)s->rows.size() - 1, s->selectedRow + pageRows);
            
            EnsureRowVisible(hWnd, s->selectedRow);
            InvalidateRect(hWnd, NULL, FALSE);
            UpdateWindow(hWnd);
          }
          return 0;
        }
      }
      break;
    }

    // -------------------------------------------------------------
    // FITUR SCROLLBAR VERTIKAL
    // -------------------------------------------------------------
    case WM_VSCROLL: {
        if (!s) break;
        SCROLLINFO si = {0};
        si.cbSize = sizeof(SCROLLINFO);
        si.fMask = SIF_ALL;
        GetScrollInfo(hWnd, SB_VERT, &si);

        int newPos = si.nPos;
        switch (LOWORD(wParam)) {
          case SB_TOP: newPos = si.nMin; break;
          case SB_BOTTOM: newPos = si.nMax; break;
          case SB_LINEUP: newPos -= s->rowHeight; break;
          case SB_LINEDOWN: newPos += s->rowHeight; break;
          case SB_PAGEUP: newPos -= si.nPage; break;
          case SB_PAGEDOWN: newPos += si.nPage; break;
          case SB_THUMBTRACK: newPos = si.nTrackPos; break;
        }

        if (newPos < 0) newPos = 0;
        int maxScroll = si.nMax - (int)si.nPage;
        if (maxScroll < 0) maxScroll = 0;
        if (newPos > maxScroll) newPos = maxScroll;

        if (newPos != s->scrollY) {
          s->scrollY = newPos;
          SetScrollPos(hWnd, SB_VERT, s->scrollY, TRUE);
          InvalidateRect(hWnd, NULL, FALSE);
          UpdateWindow(hWnd);
        }
        return 0;
    }

    // -------------------------------------------------------------
    // FITUR MOUSE WHEEL
    // -------------------------------------------------------------
    case WM_MOUSEWHEEL: {
      if (!s) break;
      
      short zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
      int scrollAmount = -1 * (zDelta / WHEEL_DELTA) * s->rowHeight * 3; 

      SCROLLINFO si = {0};
      si.cbSize = sizeof(SCROLLINFO);
      si.fMask = SIF_ALL;
      GetScrollInfo(hWnd, SB_VERT, &si);
      
      int targetPos = s->scrollY + scrollAmount;
      int maxScroll = si.nMax - (int)si.nPage;
      if (targetPos < 0) targetPos = 0;
      if (targetPos > maxScroll) targetPos = maxScroll;
      
      if (targetPos != s->scrollY) {
        s->scrollY = targetPos;
        SetScrollPos(hWnd, SB_VERT, s->scrollY, TRUE);
        InvalidateRect(hWnd, NULL, FALSE);
        UpdateWindow(hWnd);
      }
      return 0;
    }

    case WM_LBUTTONDOWN: {
      SetFocus(hWnd); 

      if (!s) break;
      int x = GET_X_LPARAM(lParam);
      int y = GET_Y_LPARAM(lParam);

      // Header Resize Logic
      if (y < s->headerHeight) {
        int curX = 0;
        int hScroll = GetScrollPos(hWnd, SB_HORZ); 
        curX -= hScroll;

        for (size_t i = 0; i < s->columns.size(); i++) {
          curX += s->columns[i].width;
          if (abs(x - curX) < 4) {
            s->isResizing = true;
            s->resizeColIdx = (int)i;
            s->resizeStartX = x;
            s->resizeStartWidth = s->columns[i].width;
            s->currentXorLineX = x;
            
            SetCapture(hWnd);
            DrawXorLine(hWnd, x);
            return 0;
          }
        }
      }
      // SELECTION LOGIC (NEW!) - Klik di body area
      else {
        RECT rc;
        GetClientRect(hWnd, &rc);
        int bodyTop = s->headerHeight;
        int bodyBottom = s->footer.visible ? (rc.bottom - s->footer.height) : rc.bottom;
        
        if (y >= bodyTop && y < bodyBottom) {
          // Hitung row index dari posisi klik
          int relativeY = y - bodyTop + s->scrollY;
          int clickedRow = relativeY / s->rowHeight;
          
          if (clickedRow >= 0 && clickedRow < (int)s->rows.size()) {
            s->selectedRow = clickedRow;
            InvalidateRect(hWnd, NULL, FALSE);
            UpdateWindow(hWnd);
          }
        }
      }
      break;
    }

    case WM_MOUSEMOVE: {
      if (!s) break;
      int x = GET_X_LPARAM(lParam);
      
      if (s->isResizing) {
        if (s->currentXorLineX != -1) DrawXorLine(hWnd, s->currentXorLineX);
        s->currentXorLineX = x;
        DrawXorLine(hWnd, s->currentXorLineX);
      } 
      else {
        int curX = 0; 
        int y = GET_Y_LPARAM(lParam);
        
        if (y < s->headerHeight) {
          bool onDivider = false;
          for (size_t i = 0; i < s->columns.size(); i++) {
            curX += s->columns[i].width;
            if (abs(x - curX) < 4) {
              SetCursor(LoadCursor(NULL, IDC_SIZEWE));
              onDivider = true;
              break;
            }
          }
          if (!onDivider) SetCursor(LoadCursor(NULL, IDC_ARROW));
        } else {
          SetCursor(LoadCursor(NULL, IDC_ARROW));
        }
      }
      return 0;
    }

    case WM_LBUTTONUP: {
      if (s && s->isResizing) {
        if (s->currentXorLineX != -1) DrawXorLine(hWnd, s->currentXorLineX);
        
        int diff = GET_X_LPARAM(lParam) - s->resizeStartX;
        int newWidth = s->resizeStartWidth + diff;
        
        if (newWidth < s->columns[s->resizeColIdx].minWidth) 
            newWidth = s->columns[s->resizeColIdx].minWidth;
        
        s->columns[s->resizeColIdx].width = newWidth;
        
        s->isResizing = false;
        s->resizeColIdx = -1;
        s->currentXorLineX = -1;
        ReleaseCapture();
          
        InvalidateRect(hWnd, NULL, FALSE);
      }
      break;
    }

    case WM_PAINT: {
      GridState* s = GetGridState(hWnd);
      if (!s) break;

      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hWnd, &ps);

      RECT rcClient;
      GetClientRect(hWnd, &rcClient);

      // --- DOUBLE BUFFERING SETUP ---
      HDC hdcMem = CreateCompatibleDC(hdc);
      HBITMAP hbmMem = CreateCompatibleBitmap(hdc, rcClient.right, rcClient.bottom);
      HBITMAP hbmOld = (HBITMAP)SelectObject(hdcMem, hbmMem);

      FillRect(hdcMem, &rcClient, (HBRUSH)GetStockObject(WHITE_BRUSH));

      RECT rcHeader = rcClient;
      rcHeader.bottom = s->headerHeight;

      RECT rcFooter = rcClient;
      if (s->footer.visible) {
        rcFooter.top = rcClient.bottom - s->footer.height;
      } else {
        rcFooter.top = rcClient.bottom;
      }

      RECT rcBody = rcClient;
      rcBody.top = rcHeader.bottom;
      rcBody.bottom = rcFooter.top;

      // --------------------------------------------------------
      // DRAW HEADER
      // --------------------------------------------------------
      {
        int x = 0;
        SelectObject(hdcMem, s->hFontBold);
        SetBkMode(hdcMem, TRANSPARENT);

        HBRUSH hBr = CreateSolidBrush(RGB(240, 240, 240)); 
        FillRect(hdcMem, &rcHeader, hBr);
        DeleteObject(hBr);

        HPEN hPen = CreatePen(PS_SOLID, 1, RGB(180, 180, 180));
        HPEN hOldP = (HPEN)SelectObject(hdcMem, hPen);
        MoveToEx(hdcMem, 0, rcHeader.bottom - 1, NULL);
        LineTo(hdcMem, rcClient.right, rcHeader.bottom - 1);
        SelectObject(hdcMem, hOldP);
        DeleteObject(hPen);
        
        for (const auto& col : s->columns) {
          RECT rcCol = { x, 0, x + col.width, s->headerHeight };
          
          HPEN hDivPen = CreatePen(PS_SOLID, 1, RGB(220, 220, 220));
          HPEN hOldDiv = (HPEN)SelectObject(hdcMem, hDivPen);
          MoveToEx(hdcMem, x + col.width - 1, 0, NULL);
          LineTo(hdcMem, x + col.width - 1, rcHeader.bottom - 1);
          SelectObject(hdcMem, hOldDiv);
          DeleteObject(hDivPen);
          
          RECT rcText = rcCol; InflateRect(&rcText, -4, 0);
          SetTextColor(hdcMem, RGB(0,0,0));
          DrawText(hdcMem, col.header.c_str(), -1, &rcText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
          
          x += col.width;
        }

        if (x < rcClient.right) {
          RECT rcRest = { x, 0, rcClient.right, s->headerHeight };
          HBRUSH hBr = CreateSolidBrush(RGB(230, 230, 230));
          FillRect(hdcMem, &rcRest, hBr);
          DeleteObject(hBr);
        }
      }

      // --------------------------------------------------------
      // DRAW BODY (WITH SELECTION HIGHLIGHT!)
      // --------------------------------------------------------
      HRGN hRgnBody = CreateRectRgn(rcBody.left, rcBody.top, rcBody.right, rcBody.bottom);
      SelectClipRgn(hdcMem, hRgnBody);

      {
        int x = 0;
        int y = rcBody.top - s->scrollY; 
        
        SelectObject(hdcMem, s->hFont);
        
        int rowIdx = 0;
        for (const auto& row : s->rows) {
          if (y + s->rowHeight < rcBody.top) {
            y += s->rowHeight;
            rowIdx++;
            continue;
          }
          if (y > rcBody.bottom) break;

          x = 0;
          
          // HIGHLIGHT SELECTED ROW (NEW!)
          bool isSelected = (rowIdx == s->selectedRow);
          if (isSelected) {
            RECT rcRowBg = { 0, y, rcClient.right, y + s->rowHeight };
            HBRUSH hSelBrush = CreateSolidBrush(RGB(200, 220, 255)); // Light blue
            FillRect(hdcMem, &rcRowBg, hSelBrush);
            DeleteObject(hSelBrush);
          }
          
          for (size_t i = 0; i < row.cells.size(); i++) {
            if (i >= s->columns.size()) break;
            const auto& col = s->columns[i];
            const auto& cell = row.cells[i];

            RECT rcCell = { x, y, x + col.width, y + s->rowHeight };

            // Bg Cell (skip kalau selected, karena sudah di-fill di atas)
            if (!isSelected && cell.bgColor != RGB(255,255,255)) {
              HBRUSH hBr = CreateSolidBrush(cell.bgColor);
              FillRect(hdcMem, &rcCell, hBr);
              DeleteObject(hBr);
            }

            // Separator Line
            HPEN hPen = CreatePen(PS_SOLID, 1, RGB(240, 240, 240));
            HPEN hOldPen = (HPEN)SelectObject(hdcMem, hPen);
            MoveToEx(hdcMem, rcCell.left, rcCell.bottom-1, NULL);
            LineTo(hdcMem, rcCell.right, rcCell.bottom-1);
            SelectObject(hdcMem, hOldPen);
            DeleteObject(hPen);

            // Vertical Line
            HPEN vPen = CreatePen(PS_SOLID, 1, RGB(240, 240, 240));
            HPEN vOldPen = (HPEN)SelectObject(hdcMem, vPen);
            MoveToEx(hdcMem, rcCell.right, rcCell.top, NULL);
            LineTo(hdcMem, rcCell.right, rcCell.bottom);
            SelectObject(hdcMem, vOldPen);
            DeleteObject(vPen);

            // Text
            RECT rcText = rcCell; InflateRect(&rcText, -4, 0);
            SetTextColor(hdcMem, cell.textColor);

            //histogram
            if (cell.barPercent > 0.0f) {
              // hitung lebar (persentase)
              int barWidth = (int)((float)(col.width) * cell.barPercent);
              // pastikan tidak melebar
              if (barWidth > col.width) barWidth = col.width;

              RECT rcBar = rcCell;

              // barAlign (jika diset), -1 (fallback)
              int align = (cell.barAlign != -1) ? cell.barAlign : col.align;

              // Kalau mau bar rata kanan (misal buat sisi Bid)
              if (align == DT_RIGHT) {
                rcBar.left = rcBar.right - barWidth;
              } 
              // Kalau rata kiri/center (sisi Offer)
              else {
                rcBar.right = rcBar.left + barWidth;
              }
              
              // Sisakan padding atas bawah dikit biar manis
              rcBar.top += 2;
              rcBar.bottom -= 2;

              HBRUSH hBrBar = CreateSolidBrush(cell.barColor);
              FillRect(hdcMem, &rcBar, hBrBar);
              DeleteObject(hBrBar);
            }

            DrawText(hdcMem, cell.text.c_str(), -1, &rcText, col.align | DT_VCENTER | DT_SINGLELINE);

            x += col.width;
          }
          y += s->rowHeight;
          rowIdx++;
        }
      }
      SelectClipRgn(hdcMem, NULL);
      DeleteObject(hRgnBody);

      // --------------------------------------------------------
      // DRAW FOOTER
      // --------------------------------------------------------
      if (s->footer.visible) {
        int x = 0;
        int y = rcFooter.top;
        SelectObject(hdcMem, s->hFontBold);
        
        HPEN hPen = CreatePen(PS_SOLID, 1, RGB(180, 180, 180));
        HPEN hOldP = (HPEN)SelectObject(hdcMem, hPen);
        MoveToEx(hdcMem, 0, y, NULL);
        LineTo(hdcMem, rcClient.right, y);
        SelectObject(hdcMem, hOldP);
        DeleteObject(hPen);

        for (size_t i = 0; i < s->footer.cells.size(); i++) {
          if (i >= s->columns.size()) break;
          const auto& col = s->columns[i];
          const auto& cell = s->footer.cells[i];

          RECT rcCell = { x, y, x + col.width, rcFooter.bottom };

          HBRUSH hBr = CreateSolidBrush(cell.bgColor);
          FillRect(hdcMem, &rcCell, hBr);
          DeleteObject(hBr);

          RECT rcText = rcCell; InflateRect(&rcText, -4, 0);
          SetTextColor(hdcMem, cell.textColor);
          DrawText(hdcMem, cell.text.c_str(), -1, &rcText, col.align | DT_VCENTER | DT_SINGLELINE);

          x += col.width;
        }
        if (x < rcClient.right) {
          RECT rcRest = { x, y, rcClient.right, rcFooter.bottom };
          HBRUSH hBr = CreateSolidBrush(RGB(240, 240, 240));
          FillRect(hdcMem, &rcRest, hBr);
          DeleteObject(hBr);
        }
      }

      BitBlt(hdc, 0, 0, rcClient.right, rcClient.bottom, hdcMem, 0, 0, SRCCOPY);

      SelectObject(hdcMem, hbmOld);
      DeleteObject(hbmMem);
      DeleteDC(hdcMem);

      EndPaint(hWnd, &ps);
      return 0;
    }

    case WM_ERASEBKGND:
      return 1; 

  }
  return DefWindowProc(hWnd, message, wParam, lParam);
}