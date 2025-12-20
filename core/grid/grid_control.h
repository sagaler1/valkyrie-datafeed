#pragma once
#include <windows.h>
#include <string>
#include <vector>

// Struktur Cell support text & warna
struct GridCell {
  std::string text; 
  COLORREF textColor = RGB(0,0,0);
  COLORREF bgColor = RGB(255,255,255); // Default putih
};

struct GridColumn {
  std::string title;
  int width;
};

struct GridRow {
  std::vector<GridCell> cells;
};

struct GridFooter {
  std::vector<GridCell> cells;
};

class GridControl {
public:
  static void Register(HINSTANCE hInst);
  static HWND Create(HWND parent, int x, int y, int w, int h, int id);
  
  // Setter Data
  static void SetColumns(const std::vector<GridColumn>& cols);
  static void SetRows(const std::vector<GridRow>& newRows);
  static void SetFooter(const GridFooter& newFooter);
  
  // Trigger Repaint
  static void Redraw(HWND hWnd);

  static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

private:
  static void OnPaint(HWND hWnd);
  static void DrawHeader(HDC hdc, RECT& rc);
  static void DrawBody(HDC hdc, RECT& rc);
  static void DrawFooter(HDC hdc, RECT& rc);

  // Mouse Interaction (Resize Column, etc - Basic skeleton)
  static void OnMouseMove(HWND, int x, int y);
  static void OnLButtonDown(HWND, int x, int y);
  static void OnLButtonUp(HWND);

  // Helpers
  static std::wstring s2ws(const std::string& str);
  
  static const int ROW_HEIGHT = 22;
  static const int HEADER_HEIGHT = 24;
  static const int FOOTER_HEIGHT = 24;

  // Data Storage (Static Singleton style per request)
  static inline std::vector<GridColumn> columns;
  static inline std::vector<GridRow> rows;
  static inline GridFooter footer;
  
  // State
  static inline int draggingColumn = -1;
  static inline int dragStartX = 0;
};