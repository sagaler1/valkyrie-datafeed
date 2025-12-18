#include "OrderbookDlg.h"
#include "OrderbookClient.h"
#include "HeaderPanel.h"
#include "../../plugin.h" 
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
};

// Globals
static HWND g_hHeaderPanel = NULL;
extern HMODULE g_hDllModule;
static HFONT g_hFont = NULL;
extern std::shared_ptr<OrderbookClient> g_obClient;
static HWND g_hDlgOrderbook = NULL;
static WNDPROC g_wpOrigEditProc = NULL;

// =========================================================
// HELPER: Get/Set Cached Data
// =========================================================
static DialogData* GetDialogData(HWND hWnd) {
  return (DialogData*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
}

static void SetDialogData(HWND hWnd, DialogData* pData) {
  SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)pData);
}

// =========================================================
// UTILITY FUNCTIONS (Unchanged)
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
      LPNMHDR hdr = (LPNMHDR)lParam;
      if (hdr->idFrom == IDC_LIST1 && hdr->code == NM_CUSTOMDRAW) {
        return OnListViewCustomDraw(hWnd, lParam);
      }
    }
    break;

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
  strcpy_s(lf.lfFaceName, "Arial");
  g_hFont = CreateFontIndirect(&lf);

  //SendMessage(GetDlgItem(hWnd, IDC_LIST1), WM_SETFONT, (WPARAM)g_hFont, TRUE);
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
  g_hHeaderPanel = CreateHeaderPanel(hWnd, g_hDllModule, 5, 75, 360, 60);
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

  // DEBUG
  /*char buf[128];
  sprintf_s(buf, "[DEBUG] prev_close: %.2f, last: %.2f", 
    pData->cachedSnapshot.prev_close, 
    pData->cachedSnapshot.last_price);
  OutputDebugStringA(buf);
  */
}

// =========================================================
// UPDATE DISPLAY (Baca dari Cache)
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
  SetDlgItemTextA(hWnd, IDC_STATIC_LNAME, data.company_name.c_str());

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
      ListView_SetItemText(hList, i, 0, (LPSTR)FormatNumber(data.bids[i].queue).c_str());
      ListView_SetItemText(hList, i, 1, (LPSTR)FormatNumber((data.bids[i].volume) / 100).c_str());
      ListView_SetItemText(hList, i, 2, (LPSTR)FormatNumber(data.bids[i].price).c_str());
    } else {
      ListView_SetItemText(hList, i, 0, "");
      ListView_SetItemText(hList, i, 1, "");
      ListView_SetItemText(hList, i, 2, "");
    }

    if (i < (int)data.offers.size()) {
      ListView_SetItemText(hList, i, 3, (LPSTR)FormatNumber(data.offers[i].price).c_str());
      ListView_SetItemText(hList, i, 4, (LPSTR)FormatNumber((data.offers[i].volume) / 100).c_str());
      ListView_SetItemText(hList, i, 5, (LPSTR)FormatNumber(data.offers[i].queue).c_str());
    } else {
      ListView_SetItemText(hList, i, 3, "");
      ListView_SetItemText(hList, i, 4, "");
      ListView_SetItemText(hList, i, 5, "");
    }
  }

  if (currentRows > requiredRows) {
    for (int i = currentRows - 1; i >= requiredRows; --i) {
      ListView_DeleteItem(hList, i);
    }
  }

  SendMessage(hList, WM_SETREDRAW, TRUE, 0);
  InvalidateRect(hList, NULL, FALSE); // ← TRIGGER CUSTOM DRAW!
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
// CUSTOM DRAW (Baca dari Cache)
// =========================================================
LRESULT OrderbookDlg::OnListViewCustomDraw(HWND hWnd, LPARAM lParam) {
  LPNMLVCUSTOMDRAW cd = (LPNMLVCUSTOMDRAW)lParam;

  switch (cd->nmcd.dwDrawStage) {
    case CDDS_PREPAINT:
      return CDRF_NOTIFYITEMDRAW;

    case CDDS_ITEMPREPAINT:
      return CDRF_NOTIFYSUBITEMDRAW;

    case CDDS_ITEMPREPAINT | CDDS_SUBITEM:
    {
      // =========================================================
      // AMBIL DATA DARI CACHE (Bukan Fetch Ulang!)
      // =========================================================
      DialogData* pData = GetDialogData(hWnd);
      if (!pData || !pData->dataReady) {
        cd->clrText = RGB(0, 0, 0);
        cd->clrTextBk = RGB(255, 255, 255);
        return CDRF_DODEFAULT;
      }

      const OrderbookSnapshot& data = pData->cachedSnapshot;
      int item = (int)cd->nmcd.dwItemSpec;
      int subitem = cd->iSubItem;

      cd->clrText = RGB(0, 0, 0);
      cd->clrTextBk = RGB(255, 255, 255);

      // Hanya warnai kolom harga: Bid (2) dan Offer (3)
      if (subitem != 2 && subitem != 3) {
        return CDRF_DODEFAULT;
      }

      long price = 0;
      bool valid = false;

      if (subitem == 2 && item < (int)data.bids.size()) {
        price = data.bids[item].price;
        valid = true;
      }
      else if (subitem == 3 && item < (int)data.offers.size()) {
        price = data.offers[item].price;
        valid = true;
      }

      if (!valid || price == 0) {
        return CDRF_DODEFAULT;
      }

      long prev = (long)data.prev_close;

      // =========================================================
      // LOGIC WARNA (Sekarang prev_close pasti valid!)
      // =========================================================
      if (prev == 0) {
        // Fallback: kalau prev masih 0, warnain hitam dulu
        cd->clrText = RGB(0, 0, 0);
      }
      else if (price > prev) {
        cd->clrText = RGB(0, 180, 0);   // Hijau
        OutputDebugStringA("[CustomDraw] HIJAU applied");
      }
      else if (price < prev) {
        cd->clrText = RGB(200, 0, 0);   // Merah
      }
      else {
        cd->clrText = RGB(0, 0, 0);     // Hitam (sama)
      }

      return CDRF_DODEFAULT;
    }
  }

  return CDRF_DODEFAULT;
}

// =========================================================
// HELPER FUNCTIONS (Unchanged)
// =========================================================
void OrderbookDlg::InitListView(HWND hWnd) {
  HWND hList = GetDlgItem(hWnd, IDC_LIST1);
  SetWindowLong(hList, GWL_STYLE, GetWindowLong(hList, GWL_STYLE) | LVS_REPORT);
  ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

  RECT rc;
  GetClientRect(hList, &rc);
  int scrollWidth = GetSystemMetrics(SM_CXVSCROLL);
  int totalWidth = rc.right - scrollWidth;
  int w = totalWidth / 6;
  int wLast = totalWidth - (w * 5);

  LVCOLUMNA lvc;
  lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
  lvc.fmt = LVCFMT_RIGHT;

  char* cols[] = { "Freq", "Lot", "Bid", "Offer", "Lot", "Freq" };
  
  for (int i = 0; i < 6; i++) {
    lvc.iSubItem = i;
    lvc.pszText = cols[i];
    lvc.cx = (i == 5) ? wLast : w;
    ListView_InsertColumn(hList, i, &lvc);
  }
}

std::string OrderbookDlg::FormatNumber(long val) {
  if (val == 0) return "";
  std::string s = std::to_string(val);
  int n = s.length() - 3;
  while (n > 0) {
    s.insert(n, ",");
    n -= 3;
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