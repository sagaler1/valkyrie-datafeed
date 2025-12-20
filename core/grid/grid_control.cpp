#include "grid_control.h"
#include <windowsx.h>
#include <algorithm>

#define GRID_CLASS "OrderbookGrid"

// =========================================================
// HELPER
// =========================================================
std::wstring GridControl::s2ws(const std::string& str) {
  if (str.empty()) return L"";
  int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
  std::wstring wstrTo(size_needed, 0);
  MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
  return wstrTo;
}

void GridControl::Register(HINSTANCE hInst) {
  WNDCLASS wc{};
  wc.lpfnWndProc = GridControl::WndProc;
  wc.hInstance = hInst;
  wc.lpszClassName = GRID_CLASS;
  wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
  wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);
  RegisterClass(&wc);
}

HWND GridControl::Create(HWND parent, int x, int y, int w, int h, int id) {
  return CreateWindowEx(
    0, GRID_CLASS, "",
    WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
    x, y, w, h,
    parent, (HMENU)(UINT_PTR)id, GetModuleHandle(NULL), NULL
  );
}

// =========================================================
// SETTERS
// =========================================================
void GridControl::SetColumns(const std::vector<GridColumn>& cols) {
  columns = cols;
}

void GridControl::SetRows(const std::vector<GridRow>& newRows) {
  rows = newRows;
}

void GridControl::SetFooter(const GridFooter& newFooter) {
  footer = newFooter;
}

void GridControl::Redraw(HWND hWnd) {
  // InvalidateRect FALSE artinya: Jangan erase background dulu (biar handle di MemDC aja)
  InvalidateRect(hWnd, NULL, FALSE);
}

// =========================================================
// PAINTING (DOUBLE BUFFERED)
// =========================================================
void GridControl::OnPaint(HWND hWnd) {
  PAINTSTRUCT ps;
  HDC hdc = BeginPaint(hWnd, &ps);

  RECT rcClient;
  GetClientRect(hWnd, &rcClient);
  int width = rcClient.right - rcClient.left;
  int height = rcClient.bottom - rcClient.top;

  // 1. Create Memory DC
  HDC memDC = CreateCompatibleDC(hdc);
    
  // 2. Create Bitmap seukuran layar
  HBITMAP hBitmap = CreateCompatibleBitmap(hdc, width, height);
    
  // 3. Select Bitmap ke MemDC (Pasang kertas ke meja gambar)
  HBITMAP hOldBitmap = (HBITMAP)SelectObject(memDC, hBitmap);

  // 4. Fill Background Putih (Reset Kanvas)
  HBRUSH bgBrush = CreateSolidBrush(RGB(255, 255, 255));
  FillRect(memDC, &rcClient, bgBrush);
  DeleteObject(bgBrush);

  // Setup Font Standard
  HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT); 
  HFONT hOldFont = (HFONT)SelectObject(memDC, hFont);

  // 5. Calculate Layout
  RECT rcHeader = rcClient;
  rcHeader.bottom = rcHeader.top + HEADER_HEIGHT;

  RECT rcFooter = rcClient;
  rcFooter.top = rcFooter.bottom - FOOTER_HEIGHT;

  RECT rcBody = rcClient;
  rcBody.top = rcHeader.bottom;
  rcBody.bottom = rcFooter.top; // Body tidak boleh timpa Footer

  // 6. Draw Components ke MemDC
  DrawHeader(memDC, rcHeader);
  DrawBody(memDC, rcBody);
  DrawFooter(memDC, rcFooter);

  // 7. BITBLT: Copy RAM ke Layar
  BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);

  // 8. CLEANUP
  SelectObject(memDC, hOldFont);   // Balikin Font
  SelectObject(memDC, hOldBitmap); // Balikin Bitmap
  DeleteObject(hBitmap);           // Hapus Bitmap
  DeleteDC(memDC);                 // Hapus DC

  EndPaint(hWnd, &ps);
}

void GridControl::DrawHeader(HDC hdc, RECT& rc) {
  // Background Header (Abu-abu gradient style dikit)
  HBRUSH hBrush = CreateSolidBrush(RGB(230, 230, 230));
  FillRect(hdc, &rc, hBrush);
  DeleteObject(hBrush);

  // Garis Bawah Header
  HPEN hPen = CreatePen(PS_SOLID, 1, RGB(180, 180, 180));
  HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
    
  MoveToEx(hdc, rc.left, rc.bottom - 1, NULL);
  LineTo(hdc, rc.right, rc.bottom - 1);

  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, RGB(0, 0, 0));

  int x = rc.left;
  for (const auto& col : columns) {
    RECT rcCell = { x, rc.top, x + col.width, rc.bottom };
      
    // Draw Text (Center)
    std::wstring ws = s2ws(col.title);
    DrawTextW(hdc, ws.c_str(), -1, &rcCell, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      
    // Garis Pemisah Kolom
    MoveToEx(hdc, x + col.width - 1, rc.top, NULL);
    LineTo(hdc, x + col.width - 1, rc.bottom);

    x += col.width;
  }

  SelectObject(hdc, hOldPen);
  DeleteObject(hPen);
}

void GridControl::DrawBody(HDC hdc, RECT& rc) {
    // Clip Region
    HRGN hRgn = CreateRectRgn(rc.left, rc.top, rc.right, rc.bottom);
    SelectClipRgn(hdc, hRgn);

    SetBkMode(hdc, TRANSPARENT);

    // Brushes untuk Zebra Striping
    HBRUSH brushEven = CreateSolidBrush(RGB(255, 255, 255)); // Putih
    HBRUSH brushOdd  = CreateSolidBrush(RGB(248, 250, 252)); // Biru super muda
    
    // Pen untuk Grid Lines
    HPEN penGrid = CreatePen(PS_SOLID, 1, RGB(240, 240, 240));
    HPEN oldPen = (HPEN)SelectObject(hdc, penGrid);

    int y = rc.top;
    
    for (size_t r = 0; r < rows.size(); r++) {
      // Stop gambar jika sudah lewat batas bawah body
      if (y >= rc.bottom) break;

      int rowHeight = ROW_HEIGHT;
      RECT rcRow = { rc.left, y, rc.right, y + rowHeight };

      // Background Row
      FillRect(hdc, &rcRow, (r % 2 == 0) ? brushEven : brushOdd);

      // Draw Cells
      int x = rc.left;
      for (size_t c = 0; c < columns.size(); c++) {
        if (c >= rows[r].cells.size()) break;
        
        int w = columns[c].width;
        RECT rcCell = { x, y, x + w, y + rowHeight };
        RECT rcText = rcCell; 
        rcText.left += 4; rcText.right -= 4; // Padding text

        // Grid Line Vertical tipis
        MoveToEx(hdc, x + w - 1, y, NULL);
        LineTo(hdc, x + w - 1, y + rowHeight);

        // Set Warna Text
        SetTextColor(hdc, rows[r].cells[c].textColor);
        
        // Draw Text
        std::wstring ws = s2ws(rows[r].cells[c].text);
        DrawTextW(hdc, ws.c_str(), -1, &rcText, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

        x += w;
      }

      // Grid Line Horizontal tipis
      MoveToEx(hdc, rc.left, y + rowHeight - 1, NULL);
      LineTo(hdc, rc.right, y + rowHeight - 1);

      y += rowHeight;
    }

    // Cleanup
    SelectObject(hdc, oldPen);
    DeleteObject(penGrid);
    DeleteObject(brushEven);
    DeleteObject(brushOdd);
    
    SelectClipRgn(hdc, NULL); // Remove Clip
    DeleteObject(hRgn);
}

void GridControl::DrawFooter(HDC hdc, RECT& rc) {
  // Background Footer (Kuning tipis atau abu gelap biar beda)
  HBRUSH hBrush = CreateSolidBrush(RGB(245, 245, 245));
  FillRect(hdc, &rc, hBrush);
  DeleteObject(hBrush);

  // Garis Atas Footer
  HPEN hPen = CreatePen(PS_SOLID, 1, RGB(100, 100, 100));
  HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
  
  MoveToEx(hdc, rc.left, rc.top, NULL);
  LineTo(hdc, rc.right, rc.top);

  SetBkMode(hdc, TRANSPARENT);
  
  int x = rc.left;
  for (size_t c = 0; c < columns.size(); c++) {
    if (c >= footer.cells.size()) break;

    int w = columns[c].width;
    RECT rcCell = { x, rc.top, x + w, rc.bottom };
    RECT rcText = rcCell;
    rcText.left += 4; rcText.right -= 4;

    // Warna Text Footer
    SetTextColor(hdc, footer.cells[c].textColor);
    
    // Bold Font untuk Footer (Optional, pake font yg ada dulu)
    std::wstring ws = s2ws(footer.cells[c].text);
    
    // Kalau kolom Freq/Lot (angka) rata kanan, Label rata kiri/tengah
    // Simple logic: Kalau isi angka -> Right, else -> Center/Left
    UINT fmt = DT_RIGHT | DT_VCENTER | DT_SINGLELINE;
    if (ws.find_first_of(L"0123456789") == std::wstring::npos && !ws.empty()) {
      fmt = DT_CENTER | DT_VCENTER | DT_SINGLELINE;
    }
    
    DrawTextW(hdc, ws.c_str(), -1, &rcText, fmt);

    x += w;
  }

  SelectObject(hdc, hOldPen);
  DeleteObject(hPen);
}

// =========================================================
// WNDPROC & INPUT
// =========================================================
LRESULT CALLBACK GridControl::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
  case WM_PAINT:
    OnPaint(hWnd);
    return 0;
  
  case WM_ERASEBKGND:
    return 1; // Penting! Prevent Windows hapus background (reduce flicker)

  case WM_LBUTTONDOWN:
    OnLButtonDown(hWnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
    return 0;
      
  case WM_MOUSEMOVE:
    OnMouseMove(hWnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
    break;
      
  case WM_LBUTTONUP:
    OnLButtonUp(hWnd);
    break;

  case WM_DESTROY:
    // Cleanup global resources if any
    break;
  }
  return DefWindowProc(hWnd, message, wParam, lParam);
}

// Placeholder interactions
void GridControl::OnLButtonDown(HWND hWnd, int x, int y) {
  // 1. Cek apakah klik di area Header
  if (y > HEADER_HEIGHT) return;

  // 2. Cek apakah klik di garis pembatas kolom (Resize Grip)
  int currentX = 0;
  for (int i = 0; i < (int)columns.size(); i++) {
    currentX += columns[i].width;
      
    // Area toleransi +/- 4 pixel
    if (abs(x - currentX) <= 4) {
      draggingColumn = i;
      dragStartX = x;
      SetCapture(hWnd); // Tangkap mouse biar bisa drag sampai keluar area window
      return;
    }
  }
}

void GridControl::OnLButtonUp(HWND hWnd) {
  if (draggingColumn != -1) {
    draggingColumn = -1;
    ReleaseCapture(); // Lepas tangkapan mouse
  }
}

void GridControl::OnMouseMove(HWND hWnd, int x, int y) {
  // A. LOGIC DRAGGING (Resize Kolom)
  if (draggingColumn != -1) {
    int delta = x - dragStartX;
    
    // Hitung lebar baru
    int newWidth = columns[draggingColumn].width + delta;
    if (newWidth < 20) newWidth = 20; // Minimum width biar tidak hilang

    // Apply perubahan jika ada beda
    if (newWidth != columns[draggingColumn].width) {
      columns[draggingColumn].width = newWidth;
      dragStartX = x; // Reset start point untuk delta selanjutnya
        
      // Redraw Grid (Double Buffer supaya smooth)
      Redraw(hWnd);   
    }
    return;
  }

    // B. LOGIC HOVER (Ganti Cursor)
    // Cek cuma kalau di area Header
    if (y <= HEADER_HEIGHT) {
      int currentX = 0;
      bool onSplitter = false;
      
      for (const auto& col : columns) {
        currentX += col.width;
        if (abs(x - currentX) <= 4) {
          onSplitter = true;
          break;
        }
      }

      if (onSplitter) {
        SetCursor(LoadCursor(NULL, IDC_SIZEWE)); // Cursor: <->
      } else {
        // Optional: Kembalikan ke Arrow jika perlu, tapi biasanya diurus Window Class
        SetCursor(LoadCursor(NULL, IDC_ARROW)); 
      }
    }
}