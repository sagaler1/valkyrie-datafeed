#include "OrderbookDlg.h"
#include "OrderbookClient.h"
#include "HeaderPanel.h"
#include "plugin.h"
#include "../../core/grid/grid_control.h" 

#include <CommCtrl.h>
#include <vector>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <windows.h>
#include <string>
#include <cmath>
#include <stdio.h>

// ... (Struktur & Global Vars sama seperti sebelumnya) ...
struct DialogData {
    OrderbookSnapshot cachedSnapshot;
    bool dataReady = false;
};

// Global handles
static HWND g_hHeaderPanel = NULL;
extern HMODULE g_hDllModule; 
static HFONT g_hFont = NULL;
extern std::shared_ptr<OrderbookClient> g_obClient;
static HWND g_hDlgOrderbook = NULL;
static WNDPROC g_wpOrigEditProc = NULL;

static DialogData* GetDialogData(HWND hWnd) {
  return (DialogData*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
}
static void SetDialogData(HWND hWnd, DialogData* pData) {
  SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)pData);
}

void SetTextIfChanged(HWND hDlg, int nID, const std::string& newText) {
  char currentText[512];
  GetDlgItemTextA(hDlg, nID, currentText, 512);
  if (newText != currentText) {
    SetDlgItemTextA(hDlg, nID, newText.c_str());
  }
}

LRESULT CALLBACK EditSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
  if (uMsg == WM_KEYDOWN && wParam == VK_RETURN) {
    PostMessage(GetParent(hWnd), WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), (LPARAM)hWnd);
    return 0;
  }
  return CallWindowProc(g_wpOrigEditProc, hWnd, uMsg, wParam, lParam);
}

// ... Public Show/DlgProc 
void OrderbookDlg::Show(HINSTANCE hInst, HWND hParent) {
  if (g_hDlgOrderbook && IsWindow(g_hDlgOrderbook)) {
    SetForegroundWindow(g_hDlgOrderbook);
    if (IsIconic(g_hDlgOrderbook)) ShowWindow(g_hDlgOrderbook, SW_RESTORE);
    return;
  }
  g_hDlgOrderbook = CreateDialog(hInst, MAKEINTRESOURCE(IDD_ORDERBOOK_DIALOG), hParent, DlgProc);
  ShowWindow(g_hDlgOrderbook, SW_SHOW);
}

INT_PTR CALLBACK OrderbookDlg::DlgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
    case WM_INITDIALOG: OnInitDialog(hWnd); return TRUE;
    case WM_COMMAND: OnCommand(hWnd, wParam); break;
    case WM_USER_STREAMING_UPDATE: OnStreamingUpdate(hWnd, wParam, lParam); break;
    case WM_CLOSE: OnClose(hWnd); DestroyWindow(hWnd); g_hDlgOrderbook = NULL; return TRUE;
  }
  return FALSE;
}

// =========================================================
// INIT DIALOG (DENGAN DEBUG)
// =========================================================
void OrderbookDlg::OnInitDialog(HWND hWnd) {
  DialogData* pData = new DialogData();
  SetDialogData(hWnd, pData);

  LOGFONT lf = { 0 };
  lf.lfHeight = -12;
  lf.lfWeight = FW_BOLD;
  lf.lfQuality = CLEARTYPE_QUALITY;
  strcpy_s(lf.lfFaceName, "Arial");
  g_hFont = CreateFontIndirect(&lf);

  // 1. REGISTER CLASS
  // Pastikan g_hDllModule valid. Jika NULL, fallback ke GetModuleHandle
  HINSTANCE hInstToUse = g_hDllModule ? g_hDllModule : GetModuleHandle(NULL);
  GridControl::Register(hInstToUse);

  // 2. PLACEHOLDER REPLACEMENT
  HWND hPlaceholder = GetDlgItem(hWnd, IDC_LIST1);
  RECT rcGrid = { 0, 100, 300, 250 }; 

  if (hPlaceholder) {
    GetWindowRect(hPlaceholder, &rcGrid);
    MapWindowPoints(NULL, hWnd, (LPPOINT)&rcGrid, 2); 
    DestroyWindow(hPlaceholder);
  } else {
    OutputDebugStringA("[Orderbook] WARNING: IDC_LIST1 placeholder not found in .rc!\n");
  }

  // 3. CREATE GRID
  // Gunakan Parent window (hWnd) sebagai referensi instance
  s_hGridWnd = GridControl::Create(hWnd, 
    rcGrid.left, rcGrid.top, 
    rcGrid.right - rcGrid.left, rcGrid.bottom - rcGrid.top, 
    IDC_LIST1);

  // DEBUGGING HASIL CREATE
  if (s_hGridWnd == NULL) {
    DWORD err = GetLastError();
    char buf[128];
    sprintf_s(buf, "[Orderbook] CRITICAL: Grid Creation FAILED! Err: %lu. Check ClassName match.\n", err);
    OutputDebugStringA(buf);
    MessageBoxA(hWnd, buf, "Error", MB_ICONERROR);
  } else {
    OutputDebugStringA("[Orderbook] Grid Created Successfully!\n");
    ShowWindow(s_hGridWnd, SW_SHOW);
    UpdateWindow(s_hGridWnd);
    InitGrid(hWnd);
  }

  // 4. HEADER PANEL
  int headerHeight = 60;
  int headerY = rcGrid.top - headerHeight - 4;
  if (headerY < 0) headerY = 2;

  // Header Panel juga butuh hInst yang valid
  g_hHeaderPanel = CreateHeaderPanel(hWnd, hInstToUse, 
      rcGrid.left + 7, headerY, 
      (rcGrid.right - rcGrid.left) - 15, headerHeight);
  
  if (g_hHeaderPanel) {
    SetWindowPos(g_hHeaderPanel, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
  }

  // 5. SETUP INPUTS
  HWND hEdit = GetDlgItem(hWnd, IDC_EDIT_TICKER);
  if (hEdit) {
    DWORD dwStyle = GetWindowLong(hEdit, GWL_STYLE);
    SetWindowLong(hEdit, GWL_STYLE, dwStyle | ES_UPPERCASE);
    g_wpOrigEditProc = (WNDPROC)SetWindowLongPtr(hEdit, GWLP_WNDPROC, (LONG_PTR)EditSubclassProc);
    SendMessage(hEdit, EM_SETLIMITTEXT, 6, 0);
    SendMessage(hEdit, WM_SETFONT, (WPARAM)g_hFont, TRUE);
  }

  SendMessage(GetDlgItem(hWnd, IDC_BTN_GO), WM_SETFONT, (WPARAM)g_hFont, TRUE);
  SendMessage(GetDlgItem(hWnd, IDC_STATIC_LNAME), WM_SETFONT, (WPARAM)g_hFont, TRUE);

  if (g_obClient) g_obClient->setWindowHandle(hWnd);
}

void OrderbookDlg::OnClose(HWND hWnd) {
  DialogData* pData = GetDialogData(hWnd);
  if (pData) {
    delete pData;
    SetDialogData(hWnd, nullptr);
  }
  if (g_obClient) g_obClient->setWindowHandle(NULL);
  if (g_hFont) DeleteObject(g_hFont);
  
  // Jangan lupa unregister saat benar-benar tutup aplikasi (bukan saat tutup dialog saja)
  // Tapi karena ini DLL plugin, biasanya OS yang cleanup saat Unload.
  // GridControl::Unregister(g_hDllModule); 

  g_hHeaderPanel = NULL;
  s_hGridWnd = NULL;
}

void OrderbookDlg::OnCommand(HWND hWnd, WPARAM wParam) {
  int id = LOWORD(wParam);
  if (id == IDC_BTN_GO || id == IDOK) {
    char buffer[32];
    GetDlgItemTextA(hWnd, IDC_EDIT_TICKER, buffer, 32);
    std::string ticker = buffer;
    if (!ticker.empty() && g_obClient) {
      std::transform(ticker.begin(), ticker.end(), ticker.begin(), ::toupper);
      SetDlgItemTextA(hWnd, IDC_EDIT_TICKER, ticker.c_str());
      SetWindowTextA(hWnd, ("Orderbook: " + ticker + " (Loading...)").c_str());
      g_obClient->requestTicker(ticker);
      ClearGridDisplay(hWnd);
    }
  }
}

void OrderbookDlg::InitGrid(HWND hWnd) {
  if (!s_hGridWnd) return;

  RECT rc;
  GetClientRect(s_hGridWnd, &rc);
  int totalwidth = rc.right - 30;

  // Setup Header Kolom
  std::vector<GridColumn> cols = {
    {"Freq", static_cast<int>(totalwidth * 0.1)},
    {"+/-", static_cast<int>(totalwidth * 0.12)},
    {"Lot", static_cast<int>(totalwidth * 0.17)},  
    {"Bid", static_cast<int>(totalwidth * 0.13)},  
    {"Offer", static_cast<int>(totalwidth * 0.13)},
    {"Lot", static_cast<int>(totalwidth * 0.17)},  
    {"+/-", static_cast<int>(totalwidth * 0.12)},
    {"Freq", static_cast<int>(totalwidth * 0.1)}
  };

  // UPDATE: Pass hWnd ke SetColumns
  GridControl::SetColumns(s_hGridWnd, cols);

  // Setup Footer
  GridFooter footer;
  footer.cells.resize(8);
  footer.height = 24; // Tinggi footer fix
  footer.visible = true;
  
  // Default Footer Colors
  for(auto& c : footer.cells) c.bgColor = RGB(245, 245, 245);

  GridControl::SetFooter(s_hGridWnd, footer);
}

void OrderbookDlg::OnStreamingUpdate(HWND hWnd, WPARAM wParam, LPARAM lParam) {
  DialogData* pData = GetDialogData(hWnd);
  if (!pData) return;
  if (wParam == 1) SetDlgItemTextA(hWnd, IDC_STATIC_STATUS, "Connecting...");
  else if (wParam == 2) { SetDlgItemTextA(hWnd, IDC_STATIC_STATUS, "Not Found"); ClearGridDisplay(hWnd); }
  else if (g_obClient) {
    pData->cachedSnapshot = g_obClient->getData();
    pData->dataReady = true;
    UpdateGridDisplay(hWnd);
    SetDlgItemTextA(hWnd, IDC_STATIC_STATUS, "Live");
  }
}

void OrderbookDlg::UpdateGridDisplay(HWND hWnd) {
  DialogData* pData = GetDialogData(hWnd);
  if (!pData || !pData->dataReady || !s_hGridWnd) return;
  const OrderbookSnapshot& data = pData->cachedSnapshot;

  if (g_hHeaderPanel) {
    HeaderState hs;
    hs.prev = FormatPrice(data.prev_close); hs.last = FormatPrice(data.last_price);
    hs.chg = FormatPrice(data.change); hs.pct = FormatPercent(data.percent);
    hs.open = FormatPrice(data.open); hs.high = FormatPrice(data.high);
    hs.low = FormatPrice(data.low); hs.lot = FormatValue((long)(data.volume / 100));
    hs.val = FormatValue(data.value); hs.freq = FormatNumber((long)data.frequency);
    hs.last_num = data.last_price; hs.prev_num = data.prev_close; hs.chg_num = data.change;
    UpdateHeaderPanel(g_hHeaderPanel, hs);
  }

  int maxRows = (std::max)((int)data.bids.size(), (int)data.offers.size());
  if (maxRows > 50) maxRows = 50;

  std::vector<GridRow> rows;
  rows.reserve(maxRows);

  // Cari max volume dulu buat normalisasi
  long maxVol = 1;
  for(auto& b : data.bids) if(b.volume > maxVol) maxVol = b.volume;
  for(auto& o : data.offers) if(o.volume > maxVol) maxVol = o.volume;

  for (int i = 0; i < maxRows; i++) {
    GridRow row; row.cells.resize(8);
    if (i < (int)data.bids.size()) {
      const auto& bid = data.bids[i];
      row.cells[0] = {FormatNumber(bid.queue), RGB(0,0,255)};
      long chg = bid.lot_change;
      row.cells[1] = {(chg == 0 ? "" : ((chg > 0 ? "+" : "")+FormatNumber(chg))), GetChangeColorForGrid(chg)};
      row.cells[2] = {FormatNumber(bid.volume/100), RGB(0,0,0)};
      row.cells[2].barPercent = (float)bid.volume / (float)maxVol; // Rasio
      row.cells[2].barColor = RGB(220, 252, 231);
      row.cells[2].barAlign = DT_LEFT;
      row.cells[3] = {FormatNumber(bid.price), GetPriceColorForGrid(bid.price, data.prev_close)};
    }
    if (i < (int)data.offers.size()) {
      const auto& offer = data.offers[i];
      row.cells[4] = {FormatNumber(offer.price), GetPriceColorForGrid(offer.price, data.prev_close)};
      row.cells[5] = {FormatNumber(offer.volume/100), RGB(0,0,0)};
      row.cells[5].barPercent = (float)offer.volume / (float)maxVol;
      row.cells[5].barColor = RGB(255, 226, 226);
      row.cells[5].barAlign = DT_RIGHT;
      long chg = offer.lot_change;
      row.cells[6] = {(chg == 0 ? "" : ((chg > 0 ? "+" : "")+FormatNumber(chg))), GetChangeColorForGrid(chg)};
      row.cells[7] = {FormatNumber(offer.queue), RGB(0,0,255)};
    }
    rows.push_back(row);
  }

  GridControl::SetRows(s_hGridWnd, rows);

  GridFooter footer; 
  footer.cells.resize(8);
  footer.height = 24;
  footer.visible = true;

  // Isi data footer
  footer.cells[0].text = FormatNumber(data.total_bid_freq);
  footer.cells[0].textColor = RGB(0,0,255);
  footer.cells[0].bgColor = RGB(240,240,240);

  footer.cells[2].text = FormatNumber(data.total_bid_vol/100);
  footer.cells[2].textColor = RGB(0,0,0);
  footer.cells[2].bgColor = RGB(240,240,240);

  footer.cells[5].text = FormatNumber(data.total_offer_vol/100);
  footer.cells[5].textColor = RGB(0,0,0);
  footer.cells[5].bgColor = RGB(240,240,240);

  footer.cells[7].text = FormatNumber(data.total_offer_freq);
  footer.cells[7].textColor = RGB(0,0,255);
  footer.cells[7].bgColor = RGB(240,240,240);

  // Sisa cell kosong diberi warna background supaya rapi
  for(size_t k=0; k<8; k++) {
    if(footer.cells[k].text.empty()) footer.cells[k].bgColor = RGB(240,240,240);
  }

  // Pass hWnd ke SetFooter
  GridControl::SetFooter(s_hGridWnd, footer);
  // Trigger Redraw
  GridControl::Redraw(s_hGridWnd);

  std::string title = "Orderbook: " + data.symbol;
  SetWindowTextA(hWnd, title.c_str());
  SetTextIfChanged(hWnd, IDC_STATIC_LNAME, data.company_name);
}

void OrderbookDlg::ClearGridDisplay(HWND hWnd) {
  if (s_hGridWnd) {
    std::vector<GridRow> empty;
    GridControl::SetRows(s_hGridWnd, empty); //pass hwnd
  }
}

COLORREF OrderbookDlg::GetPriceColorForGrid(double price, double prev) {
  if (price <= 0.01 || prev <= 0.01) return RGB(0,0,0);
  if (price > prev) return RGB(0, 160, 0);
  if (price < prev) return RGB(220, 0, 0);
  return RGB(180, 180, 0);
}

COLORREF OrderbookDlg::GetChangeColorForGrid(long change) {
  if (change > 0) return RGB(0, 160, 0);
  if (change < 0) return RGB(220, 0, 0);
  return RGB(128, 128, 128);
}

std::string OrderbookDlg::FormatNumber(long val) {
  if (val == 0) return "-";
  std::string s = std::to_string(std::abs(val));
  int n = s.length() - 3;
  while (n > 0) { s.insert(n, ","); n -= 3; }
  if (val < 0) s = "-" + s;
  return s;
}
std::string OrderbookDlg::FormatPrice(double val) { return (val==0) ? "-" : FormatNumber((long)val); }
std::string OrderbookDlg::FormatPercent(double val) { char buf[32]; sprintf_s(buf, "%+.2f %%", val); return std::string(buf); }
std::string OrderbookDlg::FormatValue(double val) {
  if (val == 0.0) return "-";
  char buf[64];
  if (val >= 1e9) sprintf_s(buf, "%.2f B", val/1e9);
  else if (val >= 1e6) sprintf_s(buf, "%.2f M", val/1e6);
  else return FormatNumber((long)val);
  return std::string(buf);
}