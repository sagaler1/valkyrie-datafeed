#pragma once
#include <windows.h>
#include <vector>
#include <string>

// Struktur Data Grid
struct GridCell {
  std::string text;
  COLORREF textColor = RGB(0, 0, 0);
  COLORREF bgColor = RGB(255, 255, 255);

  // histogram
  float barPercent = 0.0f;  // 0.0 sampai 1.0
  COLORREF barColor = RGB(200, 200, 200);
  int barAlign = -1; // -1 (inherit), DT_LEFT, DT_RIGHT
};

struct GridRow {
  std::vector<GridCell> cells;
  void* userData = nullptr;
};

struct GridColumn {
  std::string header;
  int width;
  int align = DT_CENTER; 
  int minWidth = 30; // Min width biar gak bisa di-collapse sampai 0
};

struct GridFooter {
  std::vector<GridCell> cells;
  bool visible = true;
  int height = 24;
};

class GridControl {
public:
  static void Register(HINSTANCE hInst);
  static void Unregister(HINSTANCE hInst);
  
  // Create Control
  static HWND Create(HWND hParent, int x, int y, int w, int h, int id);

  // Data Setters
  static void SetColumns(HWND hWnd, const std::vector<GridColumn>& cols);
  static void SetRows(HWND hWnd, const std::vector<GridRow>& rows);
  static void SetFooter(HWND hWnd, const GridFooter& footer);
  
  // Selection
  static void SetSelectedRow(HWND hWnd, int rowIndex);
  static int GetSelectedRow(HWND hWnd);
  
  // Commands
  static void Redraw(HWND hWnd);

  // Proc
  static LRESULT CALLBACK GridWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

private:
  // Internal Helper
  static void UpdateScrollBars(HWND hWnd);
  static void DrawXorLine(HWND hWnd, int x);
  static void EnsureRowVisible(HWND hWnd, int rowIndex);
};