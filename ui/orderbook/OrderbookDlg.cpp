#include "OrderbookDlg.h"
#include "resource.h"
#include "plugin.h"                 // WM_USER_STREAMING_UPDATE
#include "OrderbookClient.h"        // Akses ke g_orderbookClient (asumsi global/singleton)
#include <cstdio>

// Pointer global untuk instance client (sesuaikan dengan kode main)
extern std::shared_ptr<OrderbookClient> g_obClient; 
extern HMODULE g_hDllModule;

OrderbookDlg::OrderbookDlg() : m_hWnd(NULL), m_hListBid(NULL), m_hListOffer(NULL) {}

OrderbookDlg::~OrderbookDlg() {
  if (IsWindow(m_hWnd)) DestroyWindow(m_hWnd);
}

OrderbookDlg& OrderbookDlg::instance() {
  static OrderbookDlg inst;
  return inst;
}

void OrderbookDlg::Show(HWND hParent) {
  if (IsWindow(m_hWnd)) {
    ShowWindow(m_hWnd, SW_SHOW);
    SetForegroundWindow(m_hWnd);
    return;
  }

  // Create Modeless Dialog
  m_hWnd = CreateDialogParam(
    g_hDllModule,
    MAKEINTRESOURCE(IDD_ORDERBOOK_DIALOG),
    hParent,
    DialogProc,
    (LPARAM)this
  );
  
  if (m_hWnd) {
    ShowWindow(m_hWnd, SW_SHOW);
    
    // Register Handle ke Client supaya bisa dipost message
    if (g_obClient) g_obClient->setWindowHandle(m_hWnd);
  }
}

void OrderbookDlg::Hide() {
  if (IsWindow(m_hWnd)) ShowWindow(m_hWnd, SW_HIDE);
}

bool OrderbookDlg::IsVisible() const {
  return IsWindow(m_hWnd) && IsWindowVisible(m_hWnd);
}

void OrderbookDlg::InitListViews() {
  // Setup kolom Bid (Vol | Queue | Price)
  // Align Right semua biar angka rapi
  LVCOLUMN lvc;
  lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
  lvc.fmt = LVCFMT_RIGHT; 
  
  lvc.iSubItem = 0; lvc.cx = 60; lvc.pszText = (LPSTR)"Vol";
  ListView_InsertColumn(m_hListBid, 0, &lvc);
  lvc.iSubItem = 1; lvc.cx = 40; lvc.pszText = (LPSTR)"Que";
  ListView_InsertColumn(m_hListBid, 1, &lvc);
  lvc.iSubItem = 2; lvc.cx = 60; lvc.pszText = (LPSTR)"Bid";
  ListView_InsertColumn(m_hListBid, 2, &lvc);

  // Setup kolom Offer (Price | Queue | Vol)
  lvc.fmt = LVCFMT_LEFT; // Kolom pertama (Price) biasanya align left di listview standard, tapi kita coba hack nanti
  lvc.iSubItem = 0; lvc.cx = 60; lvc.pszText = (LPSTR)"Offer";
  ListView_InsertColumn(m_hListOffer, 0, &lvc);
  
  lvc.fmt = LVCFMT_RIGHT;
  lvc.iSubItem = 1; lvc.cx = 40; lvc.pszText = (LPSTR)"Que";
  ListView_InsertColumn(m_hListOffer, 1, &lvc);
  lvc.iSubItem = 2; lvc.cx = 60; lvc.pszText = (LPSTR)"Vol";
  ListView_InsertColumn(m_hListOffer, 2, &lvc);

  // Style: Grid lines + Full row select
  ListView_SetExtendedListViewStyle(m_hListBid, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
  ListView_SetExtendedListViewStyle(m_hListOffer, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
}

// Helper formatting angka (thousand separator)
std::string fmtNum(long val) {
  if (val == 0) return "";
  char buf[32];
  sprintf_s(buf, "%ld", val);
  // Logic kasih koma ribuan opsional di sini kalau mau cantik
  return std::string(buf);
}

void OrderbookDlg::SetListText(HWND hList, int row, int col, const std::string& text) {
  LVITEM lvi;
  lvi.iItem = row;
  lvi.iSubItem = col;
  lvi.mask = LVIF_TEXT;
  lvi.pszText = (LPSTR)text.c_str();
  
  if (col == 0) {
    // Kolom pertama pakai SetItem
    // Cek dulu apakah baris sudah ada?
    if (ListView_GetItemCount(hList) <= row) {
      // Insert baru
      lvi.mask = LVIF_TEXT;
      ListView_InsertItem(hList, &lvi);
    } else {
      // Update
      ListView_SetItem(hList, &lvi);
    }
  } else {
    // Subitem pakai SetItemText
    ListView_SetItemText(hList, row, col, (LPSTR)text.c_str());
  }
}

void OrderbookDlg::UpdateUI() {
  if (!g_obClient) return;

  OrderbookSnapshot data = g_obClient->getData();
    
  // Update Header
  SetDlgItemText(m_hWnd, IDC_STATIC_SYMBOL, data.symbol.c_str());

  // Update BIDS
  // Kita asumsikan max row 20 biar aman, atau sesuai vector size
  int maxRows = 20; 
    
  // Tips: Jangan ListView_DeleteAllItems() -> Bikin kedip parah
  // Overwrite saja yang ada.
    
  for (int i = 0; i < maxRows; i++) {
    if ((size_t)i < data.bids.size()) {
      const auto& row = data.bids[i];
      SetListText(m_hListBid, i, 0, fmtNum(row.volume));
      SetListText(m_hListBid, i, 1, fmtNum(row.queue));
      SetListText(m_hListBid, i, 2, fmtNum(row.price));
    } else {
      // Kosongkan baris sisa kalau data < 20
      SetListText(m_hListBid, i, 0, "");
      SetListText(m_hListBid, i, 1, "");
      SetListText(m_hListBid, i, 2, "");
    }
  }

  // Update OFFERS
  for (int i = 0; i < maxRows; i++) {
    if ((size_t)i < data.offers.size()) {
      const auto& row = data.offers[i];
      SetListText(m_hListOffer, i, 0, fmtNum(row.price));
      SetListText(m_hListOffer, i, 1, fmtNum(row.queue));
      SetListText(m_hListOffer, i, 2, fmtNum(row.volume));
    } else {
      SetListText(m_hListOffer, i, 0, "");
      SetListText(m_hListOffer, i, 1, "");
      SetListText(m_hListOffer, i, 2, "");
    }
  }
}

INT_PTR CALLBACK OrderbookDlg::DialogProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
  OrderbookDlg* pThis = NULL;

  if (uMsg == WM_INITDIALOG) {
    pThis = (OrderbookDlg*)lParam;
    SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)pThis);
    
    pThis->m_hWnd = hWnd;
    pThis->m_hListBid = GetDlgItem(hWnd, IDC_LIST_BID);
    pThis->m_hListOffer = GetDlgItem(hWnd, IDC_LIST_OFFER);
    
    pThis->InitListViews();
    
    // PENTING: Set WS_EX_TOPMOST lagi untuk memastikan
    SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    
    return TRUE;
  }

  pThis = (OrderbookDlg*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
  if (!pThis) return FALSE;

  switch (uMsg) {
    case WM_CLOSE:
      // Tidak perlu destroy, hide saja biar state terjaga
      pThis->Hide();
      return TRUE;

    case WM_USER_STREAMING_UPDATE: // 0x0400 + ...
      // Pesan dari OrderbookClient (Thread worker -> UI)
      if (wParam == 1) { // 1 = Orderbook Update
        pThis->UpdateUI();
      }
      return TRUE;
        
    case WM_COMMAND:
      if (LOWORD(wParam) == IDCANCEL) {
        pThis->Hide();
        return TRUE;
      }
      break;
  }
  return FALSE;
}