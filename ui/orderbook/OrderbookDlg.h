#ifndef ORDERBOOK_DLG_H
#define ORDERBOOK_DLG_H

#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include "OrderbookData.h" 

class OrderbookDlg {
public:
  OrderbookDlg();
  ~OrderbookDlg();

  // Singleton access supaya gampang dipanggil dari mana aja
  static OrderbookDlg& instance();

  // Main functions
  void Show(HWND hParent);
  void Hide();
  bool IsVisible() const;
  HWND GetHwnd() const { return m_hWnd; }
  
  // Dipanggil saat ada update data dari Client
  void UpdateUI(); 

  // Message Processor
  static INT_PTR CALLBACK DialogProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

private:
  HWND m_hWnd;
  HWND m_hListBid;
  HWND m_hListOffer;

  // Helper untuk inisialisasi kolom List View
  void InitListViews();
  // Helper update baris
  void UpdateListContent(const OrderbookSnapshot& data);
  
  // Helper set text listview biar rapi
  void SetListText(HWND hList, int row, int col, const std::string& text);
};

#endif // ORDERBOOK_DLG_H