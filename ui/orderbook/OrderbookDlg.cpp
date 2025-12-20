#include "OrderbookDlg.h"
#include "OrderbookClient.h"
#include "HeaderPanel.h"
#include "plugin.h"
#include "grid_control.h"
#include <CommCtrl.h>
#include <vector>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <windows.h>
#include <string>
#include <cmath>

// =========================================================
// CACHED DATA STRUCTURE (Simpan di GWLP_USERDATA)
// =========================================================
struct DialogData {
  OrderbookSnapshot cachedSnapshot;
  bool dataReady = false;

  HWND hFooterBidFreq = NULL; // label "T. Bid"
  HWND hFooterBidLot = NULL;   // Value Bid
  HWND hFooterOffFreq = NULL;
  HWND hFooterOffLot = NULL;   // Value Offer
};

// Globals
static HWND g_hHeaderPanel = NULL;
extern HMODULE g_hDllModule;
static HFONT g_hFont = NULL;
extern std::shared_ptr<OrderbookClient> g_obClient;
static HWND g_hDlgOrderbook = NULL;
static WNDPROC g_wpOrigEditProc = NULL;
static HIMAGELIST g_hImageList = NULL;

// =========================================================
// HELPER: Get/Set Cached Data
// =========================================================
static DialogData* GetDialogData(HWND hWnd) {
  return (DialogData*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
}

static void SetDialogData(HWND hWnd, DialogData* pData) {
  SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)pData);
}

void SetTextIfChanged(HWND hDlg, int nID, const std::string& newText) {
  char currentText[512]; // Buffer
  GetDlgItemTextA(hDlg, nID, currentText, 512);
  
  // hanya update jika berbeda. 
  // Jika sama, skip total (hemat CPU & GPU, anti-flicker)
  if (newText != currentText) {
    SetDlgItemTextA(hDlg, nID, newText.c_str());
  }
}

// =========================================================
// UTILITY FUNCTIONS 
// =========================================================
std::string FormatNumberWithComma(long long val) {
  if (val == 0) return "0";
  bool negative = (val < 0);
  unsigned long long abs_val = negative ? -val : val;
  std::string s = std::to_string(abs_val);
  int n = s.length() - 3;
  while (n > 0) {
    s.insert(n, ",");
    n -= 3;
  }
  return negative ? "-" + s : s;
}

COLORREF OrderbookDlg::GetPriceColor(const OrderbookSnapshot& data) {
  if (data.last_price > data.prev_close)
    return RGB(0, 180, 0);
  else if (data.last_price < data.prev_close)
    return RGB(200, 0, 0);
  else
    return RGB(0, 0, 0);
}

LRESULT CALLBACK EditSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
  if (uMsg == WM_KEYDOWN && wParam == VK_RETURN) {
    PostMessage(GetParent(hWnd), WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), (LPARAM)hWnd);
    return 0;
  }
  return CallWindowProc(g_wpOrigEditProc, hWnd, uMsg, wParam, lParam);
}

void OrderbookDlg::Show(HINSTANCE hInst, HWND hParent) {
  if (g_hDlgOrderbook && IsWindow(g_hDlgOrderbook)) {
    SetForegroundWindow(g_hDlgOrderbook);
    return;
  }
  g_hDlgOrderbook = CreateDialog(hInst, MAKEINTRESOURCE(IDD_ORDERBOOK_DIALOG), hParent, DlgProc);
  ShowWindow(g_hDlgOrderbook, SW_SHOW);
}

// =========================================================
// DIALOG PROC
// =========================================================
INT_PTR CALLBACK OrderbookDlg::DlgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
    case WM_INITDIALOG:
      OnInitDialog(hWnd);
      return (INT_PTR)TRUE; 

    case WM_COMMAND:
      OnCommand(hWnd, wParam);
      break;

    case WM_NOTIFY:
    {
      LPNMHDR pHeader = (LPNMHDR)lParam;
      if (pHeader->idFrom == IDC_LIST1 && pHeader->code == NM_CUSTOMDRAW) {
        // Panggil fungsi logic warna
        LRESULT lResult = OnListViewCustomDraw(hWnd, lParam);

        // =====================
        // FIX UNTUK DIALOG
        // =====================
        // Set DWLP_MSGRESULT supaya Windows tau hasil real-nya (CDRF_NEWFONT)
        SetWindowLongPtr(hWnd, DWLP_MSGRESULT, (LONG_PTR)lResult);
        
        // Return TRUE supaya Windows tahu kita sudah handle message ini, 
        // dia akan baca value dari DWLP_MSGRESULT di atas.
        return TRUE; 
      }
      break;
    }

    case WM_USER_STREAMING_UPDATE: 
      OnStreamingUpdate(hWnd, wParam, lParam);
      break;

    case WM_CLOSE:
      OnClose(hWnd);
      DestroyWindow(hWnd);
      g_hDlgOrderbook = NULL;
      return (INT_PTR)TRUE;
  }

  return (INT_PTR)FALSE;
}

void OrderbookDlg::OnInitDialog(HWND hWnd) {
  // =========================================================
  // ALOKASI DIALOG DATA (CACHE)
  // =========================================================
  DialogData* pData = new DialogData();
  SetDialogData(hWnd, pData);
  
  // Font Setup
  LOGFONT lf = { 0 };
  lf.lfHeight = -12;
  lf.lfWeight = FW_BOLD;
  lf.lfQuality = CLEARTYPE_QUALITY;
  strcpy_s(lf.lfFaceName, "Arial");
  g_hFont = CreateFontIndirect(&lf);

  // 1. Ambil Handle ListView
  HWND hList = GetDlgItem(hWnd, IDC_LIST1);
  
  // 2. Hitung size & pos ListView sekarang
  RECT rcList;
  GetWindowRect(hList, &rcList);
  POINT pt = { rcList.left, rcList.top };
  ScreenToClient(hWnd, &pt); // Konversi ke koordinat dialog
  
  int w = rcList.right - rcList.left;
  int h = rcList.bottom - rcList.top;
  
  // 3. KECILIN ListView (Kurangi 25px dari bawah untuk Footer)
  int footerHeight = 25;
  SetWindowPos(hList, NULL, 0, 0, w, h - footerHeight, SWP_NOMOVE | SWP_NOZORDER);

  // 4. BIKIN FOOTER MANUAL (Static Text)
  // Posisi Y = (Top Listview) + (Tinggi Baru Listview) + (Padding dikit)
  int yFooter = pt.y + (h - footerHeight) + 4; 
  int xOffer = w / 2 + 5; // Mulai dari tengah

  // ==============
  // Footer Setup
  // ==============
  pData->hFooterBidFreq = CreateWindow("STATIC", "", WS_CHILD | WS_VISIBLE | SS_CENTER, 
      0, yFooter, 40, 20, hWnd, NULL, g_hDllModule, NULL);
  SendMessage(pData->hFooterBidFreq, WM_SETFONT, (WPARAM)g_hFont, TRUE);

  pData->hFooterBidLot = CreateWindow("STATIC", "", WS_CHILD | WS_VISIBLE | SS_CENTER, 
      100, yFooter, 70, 20, hWnd, NULL, g_hDllModule, NULL);
  SendMessage(pData->hFooterBidLot, WM_SETFONT, (WPARAM)g_hFont, TRUE);

  // ---- OFFER
  //CreateWindow("STATIC", "T. Off:", WS_CHILD | WS_VISIBLE | SS_RIGHT, 
  //    xOffer, yFooter, 50, 20, hWnd, NULL, g_hDllModule, NULL);
      
  pData->hFooterOffLot = CreateWindow("STATIC", "", WS_CHILD | WS_VISIBLE | SS_CENTER, 
      xOffer + 50, yFooter, 70, 20, hWnd, NULL, g_hDllModule, NULL);
  SendMessage(pData->hFooterOffLot, WM_SETFONT, (WPARAM)g_hFont, TRUE);
  
  pData->hFooterOffFreq = CreateWindow("STATIC", "", WS_CHILD | WS_VISIBLE | SS_CENTER, 
    w - 60, yFooter, 40, 20, hWnd, NULL, g_hDllModule, NULL);
  SendMessage(pData->hFooterOffFreq, WM_SETFONT, (WPARAM)g_hFont, TRUE);
  
  SendMessage(GetDlgItem(hWnd, IDC_LIST1), WM_SETFONT, (WPARAM)g_hFont, TRUE); 

  SendMessage(GetDlgItem(hWnd, IDC_EDIT_TICKER), WM_SETFONT, (WPARAM)g_hFont, TRUE);
  SendMessage(GetDlgItem(hWnd, IDC_BTN_GO), WM_SETFONT, (WPARAM)g_hFont, TRUE);
  SendMessage(GetDlgItem(hWnd, IDC_STATIC_LNAME), WM_SETFONT, (WPARAM)g_hFont, TRUE);

  InitListView(hWnd);

  if (g_obClient) {
    g_obClient->setWindowHandle(hWnd);
  }

  // Edit Control Setup
  HWND hEdit = GetDlgItem(hWnd, IDC_EDIT_TICKER);
  DWORD dwStyle = GetWindowLong(hEdit, GWL_STYLE);
  SetWindowLong(hEdit, GWL_STYLE, dwStyle | ES_UPPERCASE);
  g_wpOrigEditProc = (WNDPROC)SetWindowLongPtr(hEdit, GWLP_WNDPROC, (LONG_PTR)EditSubclassProc);
  SendMessage(hEdit, EM_SETLIMITTEXT, 6, 0);
  SetFocus(hEdit);

  // Header Panel
  g_hHeaderPanel = CreateHeaderPanel(hWnd, g_hDllModule, 5, 53, 320, 60);
}

void OrderbookDlg::OnClose(HWND hWnd) {
  // =========================================================
  // CLEANUP DIALOG DATA
  // =========================================================
  DialogData* pData = GetDialogData(hWnd);
  delete pData;
  SetDialogData(hWnd, nullptr);

  if (g_obClient) {
    g_obClient->setWindowHandle(NULL);
  }
  g_hHeaderPanel = NULL;
}

void OrderbookDlg::OnCommand(HWND hWnd, WPARAM wParam) {
  int id = LOWORD(wParam);
  
  if (id == IDC_BTN_GO || id == IDOK) {
    char buffer[32];
    GetDlgItemTextA(hWnd, IDC_EDIT_TICKER, buffer, 32);
    std::string ticker = buffer;

    if (!ticker.empty() && g_obClient) {
      for (auto& c : ticker) {
        c = (char)toupper((unsigned char)c);
      }
      SetDlgItemTextA(hWnd, IDC_EDIT_TICKER, ticker.c_str());
      g_obClient->requestTicker(ticker);
    }
  }
}

// =========================================================
// STREAMING UPDATE (Update Cache)
// =========================================================
void OrderbookDlg::OnStreamingUpdate(HWND hWnd, WPARAM wParam, LPARAM lParam) {
  DialogData* pData = GetDialogData(hWnd);
  if (!pData) return;

  if (wParam == 1) {
    // Loading State
    ClearDisplay(hWnd);
    SetDlgItemTextA(hWnd, IDC_STATIC_STATUS, "Loading orderbook...");
    pData->dataReady = false;
    pData->cachedSnapshot.clear();
  } 
  else if (wParam == 2) {
    // Invalid Symbol
    ClearDisplay(hWnd);
    SetWindowTextA(hWnd, "Orderbook - Invalid Symbol");
    SetDlgItemTextA(hWnd, IDC_STATIC_STATUS, "Invalid / Non-stock symbol");
    pData->dataReady = false;
    pData->cachedSnapshot.clear();
  } 
  else {
    // =========================================================
    // DATA READY: UPDATE CACHE + TRIGGER PAINT
    // =========================================================
    if (g_obClient) {
      pData->cachedSnapshot = g_obClient->getData(); // Copy sekali aja
      pData->dataReady = true;
    }
    UpdateDisplay(hWnd);
    SetDlgItemTextA(hWnd, IDC_STATIC_STATUS, "Live");
  }

  // ---- DEBUG
  /*
  char buf[128];
  sprintf_s(buf, "[DEBUG] prev_close: %.2f, last: %.2f", 
    pData->cachedSnapshot.prev_close, 
    pData->cachedSnapshot.last_price);
  OutputDebugStringA(buf);
  */
  
}

// =========================================================
// UPDATE DISPLAY (Baca dari cache)
// =========================================================
void OrderbookDlg::UpdateDisplay(HWND hWnd) {
  DialogData* pData = GetDialogData(hWnd);
  if (!pData || !pData->dataReady || !g_hHeaderPanel) return;

  const OrderbookSnapshot& data = pData->cachedSnapshot;
  if (data.symbol.empty()) return;

  // Update Header Panel
  HeaderState hs;
  hs.prev = FormatPrice(data.prev_close);
  hs.last = FormatPrice(data.last_price);
  hs.chg  = FormatPrice(data.change);
  hs.pct  = FormatPercent(data.percent);
  hs.open = FormatPrice(data.open);
  hs.high = FormatPrice(data.high);
  hs.low  = FormatPrice(data.low);
  hs.lot  = FormatValue((long)(data.volume / 100));
  hs.val  = FormatValue(data.value);
  hs.freq = FormatNumber((long)data.frequency);

  hs.last_num = data.last_price;
  hs.prev_num = data.prev_close;
  hs.open_num = data.open;
  hs.high_num = data.high;
  hs.low_num  = data.low;
  hs.chg_num  = data.change;

  UpdateHeaderPanel(g_hHeaderPanel, hs);
  
  // Update ListView
  HWND hList = GetDlgItem(hWnd, IDC_LIST1);
  SendMessage(hList, WM_SETREDRAW, FALSE, 0);

  std::string title = "Orderbook: " + data.symbol;
  SetWindowTextA(hWnd, title.c_str());
  SetTextIfChanged(hWnd, IDC_STATIC_LNAME, data.company_name);
  //SetDlgItemTextA(hWnd, IDC_STATIC_LNAME, data.company_name.c_str());

  int requiredRows = (std::max)((int)data.bids.size(), (int)data.offers.size());
  int currentRows = ListView_GetItemCount(hList);

  if (currentRows < requiredRows) {
    for (int i = currentRows; i < requiredRows; ++i) {
      LVITEMA lvI = { 0 };
      lvI.mask = LVIF_TEXT;
      lvI.iItem = i;
      lvI.pszText = "";
      ListView_InsertItem(hList, &lvI);
    }
  }

  for (int i = 0; i < requiredRows; ++i) {
    if (i < (int)data.bids.size()) {
      ListView_SetItemText(hList, i, 1, (LPSTR)FormatNumber(data.bids[i].queue).c_str());

      // lot_change BID
      long chg = data.bids[i].lot_change;
      std::string sChg = (chg > 0 ? "+" : "") + FormatNumber(chg);
      if (chg == 0) sChg = "";
      ListView_SetItemText(hList, i, 2, (LPSTR)sChg.c_str());   // Kolom +/-

      ListView_SetItemText(hList, i, 3, (LPSTR)FormatNumber((data.bids[i].volume) / 100).c_str());
      ListView_SetItemText(hList, i, 4, (LPSTR)FormatNumber(data.bids[i].price).c_str());
    } else {
      ListView_SetItemText(hList, i, 1, "");
      ListView_SetItemText(hList, i, 2, "");  // Kosongkan +/-
      ListView_SetItemText(hList, i, 3, "");
      ListView_SetItemText(hList, i, 4, "");
    }

    if (i < (int)data.offers.size()) {
      ListView_SetItemText(hList, i, 5, (LPSTR)FormatNumber(data.offers[i].price).c_str());
      ListView_SetItemText(hList, i, 6, (LPSTR)FormatNumber((data.offers[i].volume) / 100).c_str());

      // lot_change OFFER
      long chg = data.offers[i].lot_change;
      std::string sChg = (chg > 0 ? "+" : "") + FormatNumber(chg);
      if (chg == 0) sChg = ""; 
      ListView_SetItemText(hList, i, 7, (LPSTR)sChg.c_str()); // Kolom +/-

      ListView_SetItemText(hList, i, 8, (LPSTR)FormatNumber(data.offers[i].queue).c_str());
    } else {
      ListView_SetItemText(hList, i, 5, "");
      ListView_SetItemText(hList, i, 6, "");
      ListView_SetItemText(hList, i, 7, "");
      ListView_SetItemText(hList, i, 8, "");
    }
  }

  if (currentRows > requiredRows) {
    for (int i = currentRows - 1; i >= requiredRows; --i) {
      ListView_DeleteItem(hList, i);
    }
  }

  SendMessage(hList, WM_SETREDRAW, TRUE, 0);
  InvalidateRect(hList, NULL, FALSE); // <- TRIGGER CUSTOM DRAW!

  // ---- UPDATE FOOTER
  if (pData->hFooterBidLot) {
    // Format: "Freq / Lot"
    std::string sVol = FormatNumber(pData->cachedSnapshot.total_bid_vol / 100); // Bagi 100 jadi Lot
    std::string sFreq = FormatNumber(pData->cachedSnapshot.total_bid_freq);
    
    //std::string text = sFreq + " / " + sVol;
    std::string text = sVol;
    std::string text2 = sFreq;
    SetWindowTextA(pData->hFooterBidLot, text.c_str());
    SetWindowTextA(pData->hFooterBidFreq, text2.c_str());
  }

  if (pData->hFooterOffLot) {
    std::string sVol = FormatNumber(pData->cachedSnapshot.total_offer_vol / 100);
    std::string sFreq = FormatNumber(pData->cachedSnapshot.total_offer_freq);
    
    //std::string text = sFreq + " / " + sVol;
    std::string text = sVol;
    std::string text2 = sFreq;
    SetWindowTextA(pData->hFooterOffLot, text.c_str());
    SetWindowTextA(pData->hFooterOffFreq, text2.c_str());
  }
  
}

void OrderbookDlg::ClearDisplay(HWND hWnd) {
  if (g_hHeaderPanel) {
    HeaderState empty;
    UpdateHeaderPanel(g_hHeaderPanel, empty);
  }

  HWND hList = GetDlgItem(hWnd, IDC_LIST1);
  ListView_DeleteAllItems(hList);
  SetWindowTextA(hWnd, "Orderbook - Loading...");
}

// =========================================================
// CUSTOM DRAW (Baca dari cache)
// =========================================================
LRESULT OrderbookDlg::OnListViewCustomDraw(HWND hWnd, LPARAM lParam) {
  LPNMLVCUSTOMDRAW cd = (LPNMLVCUSTOMDRAW)lParam;

  // 1. Fase Awal: Minta notifikasi per-item
  if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) 
    return CDRF_NOTIFYITEMDRAW;

  // 2. Fase Item: Minta notifikasi per-subitem (kolom)
  if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) 
    return CDRF_NOTIFYSUBITEMDRAW;

  // 3. Fase SubItem: Saatnya mewarnai!
  if (cd->nmcd.dwDrawStage == (CDDS_ITEMPREPAINT | CDDS_SUBITEM)) {
    DialogData* pData = GetDialogData(hWnd);
        
    int row = (int)cd->nmcd.dwItemSpec; // Baris ke berapa ?
    int col = cd->iSubItem;             // Kolom ke berapa ?
        
    // Ambil data terbaru (Pastikan pointer m_client valid!)
    // const auto& data = m_client->getData(); 
    const OrderbookSnapshot& data = pData->cachedSnapshot;
    double prev = data.prev_close;

    // Definisi Warna
    COLORREF clrUp   = RGB(0, 166, 62);           // Hijau Soft
    COLORREF clrDown = RGB(231, 0, 11);           // Merah Soft
    COLORREF clrSame = RGB(151, 145, 52);         // Kuning Dark
    COLORREF clrBlue = RGB(100, 100, 255);        // Biru (buat Freq)
    COLORREF clrTextDefault = RGB(49, 65, 88);    // Slate

    // Default text color
    cd->clrText = clrTextDefault;

    // Mapping Index:
    // 0: Dummy
    // 1: Freq (Kiri) -> Biru
    // 2: Lot
    // 3: Bid Price   -> Colorful
    // 4: Offer Price -> Colorful
    // 5: Lot
    // 6: Freq (Kanan) -> Biru

    // ---- LOGIC PER KOLOM
    // A. Kolom Freq (Misal col 0 dan 5) -> Biru
    if (col == 1 || col == 8) {
      cd->clrText = clrBlue;
    }

    // B. Kolom Change (+/-) -> Hijau/Merah
    else if (col == 2) { // Change Bid
      if (row < (int)data.bids.size()) {
        long chg = data.bids[row].lot_change;
        if (chg < 0) cd->clrText = clrUp;
        else if (chg > 0) cd->clrText = clrDown;
        else cd->clrText = RGB(180, 180, 180); // Abu2 kalau 0 (atau hide di UpdateDisplay)
      }
    }
    else if (col == 7) { // Change Offer
      if (row < (int)data.offers.size()) {
        long chg = data.offers[row].lot_change;
        if (chg < 0) cd->clrText = clrUp;
        else if (chg > 0) cd->clrText = clrDown;
        else cd->clrText = RGB(180, 180, 180);
      }
    }

    // C. Kolom BID Price
    else if (col == 4) {
      // Cek bounds vector supaya no crash
      if (row < (int)data.bids.size()) {
        double price = data.bids[row].price;
          
        if (price > 0 && prev > 0) {
          if (price > prev)      cd->clrText = clrUp;
          else if (price < prev) cd->clrText = clrDown;
          else                   cd->clrText = clrSame;
        }
      }
    }

    // D. Kolom OFFER Price
    else if (col == 5) {
      // Cek bounds vector
      if (row < (int)data.offers.size()) {
        double price = data.offers[row].price;
          
        if (price > 0 && prev > 0) {
          if (price > prev)      cd->clrText = clrUp;
          else if (price < prev) cd->clrText = clrDown;
          else                   cd->clrText = clrSame;
        }
      }
    }

    // PENTING!!: Return CDRF_NEWFONT supaya warna diaplikasikan
    return CDRF_NEWFONT;
  }

  return CDRF_DODEFAULT;
}

void OrderbookDlg::InitListView(HWND hWnd) {
  HWND hList = GetDlgItem(hWnd, IDC_LIST1);
  SetWindowLong(hList, GWL_STYLE, GetWindowLong(hList, GWL_STYLE) | LVS_REPORT);
  ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

  // Dummy Column
  LVCOLUMNA lvcDummy = {0};
  lvcDummy.mask = LVCF_WIDTH;
  lvcDummy.cx = 0;    // Width 0
  ListView_InsertColumn(hList, 0 , &lvcDummy);

  RECT rc;
  GetClientRect(hList, &rc);
  int scrollWidth = GetSystemMetrics(SM_CXVSCROLL);
  int totalWidth = rc.right - scrollWidth;

  // update, kolom jadi 8 -> ada lot_change
  double weights[8] = {
    1.0, 1.2, 1.7, 1.3,   // BID side 
    1.3, 1.7, 1.2, 1.0    // OFFER side
  };

  double totalWeight = 0;
  for (double w : weights) totalWeight += w;

  // Hitung pixel width per kolom
  int colWidth[8];
  int used = 0;
  for (int i = 0; i < 8; i++) {
    colWidth[i] = (int)(totalWidth * (weights[i] / totalWeight));
    used += colWidth[i];
  }

  // Koreksi rounding error -> kolom terakhir
  colWidth[7] += (totalWidth - used);

  LVCOLUMNA lvc = {};
  lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
  lvc.fmt  = LVCFMT_CENTER;

  char* cols[] = { "Freq", "+/-", "Lot", "Bid", "Offer", "Lot", "+/-", "Freq" };

  for (int i = 0; i < 8; i++) {
    lvc.iSubItem = i + 1;
    lvc.pszText = cols[i];
    lvc.cx = colWidth[i];
    ListView_InsertColumn(hList, i + 1, &lvc);
  }

  /* Jika width dibagi rata
  int w = totalWidth / 6;
  int wLast = totalWidth - (w * 5);

  LVCOLUMNA lvc;
  lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
  lvc.fmt = LVCFMT_RIGHT;

  char* cols[] = { "Freq", "Lot", "Bid", "Offer", "Lot", "Freq" };
  
  for (int i = 0; i < 6; i++) {
    lvc.iSubItem = i + 1;
    lvc.pszText = cols[i];
    lvc.cx = (i == 5) ? wLast : w;
    ListView_InsertColumn(hList, i + 1, &lvc);
  }
  */

  if (!g_hImageList) {
    // width bebas, height = tinggi row yang kita mau
    g_hImageList = ImageList_Create(1, 22, ILC_COLOR32 | ILC_MASK, 1, 1);

    // Set ke ListView (SMALL image is enough)
    ListView_SetImageList(hList, g_hImageList, LVSIL_SMALL);
  }
}

// =========================================================
// HELPER FUNCTIONS (Number)
// =========================================================
std::string OrderbookDlg::FormatNumber(long val) {
  if (val == 0) return "";

  bool negative = (val < 0);
  unsigned long abs_val = negative ? -val : val;

  std::string s = std::to_string(abs_val);
  int n = s.length() - 3;
  while (n > 0) {
    s.insert(n, ",");
    n -= 3;
  }

  if (negative) {
    s = "-" + s;
  }

  return s;
}

std::string OrderbookDlg::FormatPrice(double val) {
  if (val == 0) return "-";
  return OrderbookDlg::FormatNumber((long)val);
}

std::string OrderbookDlg::FormatPercent(double val) {
  char buf[32];
  sprintf_s(buf, "%+.2f %%", val);
  return std::string(buf);
}

std::string OrderbookDlg::FormatValue(double val) {
  if (val == 0.0) return "-";

  const double BILLION = 1000000000.0;
  const double MILLION = 1000000.0;

  std::string suffix = "";
  double display_val = val;

  if (val >= BILLION) {
    display_val = val / BILLION;
    suffix = " B";
  } else if (val >= MILLION) {
    display_val = val / MILLION;
    suffix = " M";
  } else {
    long long int_val = std::llround(val);
    return FormatNumberWithComma(int_val);
  }

  std::ostringstream oss;
  oss << std::fixed << std::setprecision(2) << display_val;
  std::string s = oss.str();

  size_t dot_pos = s.find('.');
  if (dot_pos != std::string::npos) {
    std::string integer_part = s.substr(0, dot_pos);
    std::string decimal_part = s.substr(dot_pos);

    int n = integer_part.length() - 3;
    while (n > 0) {
      integer_part.insert(n, ",");
      n -= 3;
    }
    s = integer_part + decimal_part;
  }

  return s + suffix;
}