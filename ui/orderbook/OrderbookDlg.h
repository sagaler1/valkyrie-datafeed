#pragma once
#include <windows.h>
#include <string>
#include "OrderbookData.h"

// Resource
#include "../../core/resource.h" 

class OrderbookDlg {
public:
  static INT_PTR CALLBACK DlgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
  static void Show(HINSTANCE hInst, HWND hParent);

private:
  static void OnInitDialog(HWND hWnd);
  static void OnClose(HWND hWnd);
  static void OnCommand(HWND hWnd, WPARAM wParam);

  static LRESULT OnListViewCustomDraw(HWND hWnd, LPARAM lParam);
  static COLORREF GetPriceColor(const OrderbookSnapshot& data);
  
  // Handler Pesan Custom dari Client
  static void OnStreamingUpdate(HWND hWnd, WPARAM wParam, LPARAM lParam);

  // Fungsi Render UI
  static void InitListView(HWND hWnd);
  static void UpdateDisplay(HWND hWnd); // Render Full Table
  static void ClearDisplay(HWND hWnd);  // Kosongkan Table (Loading State)
  
  // Helper
  static std::string FormatPrice(double val);
  static std::string FormatPercent(double val);
  static std::string FormatNumber(long val);
  static std::string FormatValue(double val);

  HWND m_hFooterBid = NULL;
  HWND m_hFooterOffer = NULL;
};