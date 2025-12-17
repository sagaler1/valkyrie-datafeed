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

// Handle panel (satu dialog satu panel)
static HWND g_hHeaderPanel = NULL;

extern HMODULE g_hDllModule;

// Global font
static HFONT g_hFont = NULL;

static bool g_bDataReady = false;

// Global pointer ke Client (dari plugin.cpp)
extern std::shared_ptr<OrderbookClient> g_obClient;

// Handle Instance (biar nggak buka double dialog)
static HWND g_hDlgOrderbook = NULL;

// Sublass untuk edit control
static WNDPROC g_wpOrigEditProc = NULL;

COLORREF OrderbookDlg::GetPriceColor(const OrderbookSnapshot& data) {
  if (data.last_price > data.prev_close)
    return RGB(0, 180, 0);   // hijau
  else if (data.last_price < data.prev_close)
    return RGB(200, 0, 0);   // merah
  else
    return RGB(0, 0, 0);     // hitam
}

// =========================================================
// [NEW] SUBCLASS PROCEDURE (PENANGAN ENTER KEY)
// =========================================================
LRESULT CALLBACK EditSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
  // Tangkap tombol ENTER (VK_RETURN)
  if (uMsg == WM_KEYDOWN && wParam == VK_RETURN) {
    // Kirim perintah IDOK ke Dialog Parent (seolah-olah tombol GO dipencet)
    PostMessage(GetParent(hWnd), WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), (LPARAM)hWnd);
    return 0; // Makan event-nya biar gak bunyi "beep"
  }
  // Lanjutkan pesan lain ke prosedur asli
  return CallWindowProc(g_wpOrigEditProc, hWnd, uMsg, wParam, lParam);
}

void OrderbookDlg::Show(HINSTANCE hInst, HWND hParent) {
  if (g_hDlgOrderbook && IsWindow(g_hDlgOrderbook)) {
    SetForegroundWindow(g_hDlgOrderbook);
    return;
  }
  // Ganti IDD_ORDERBOOK sesuai nama ID dialog di Resource
  g_hDlgOrderbook = CreateDialog(hInst, MAKEINTRESOURCE(IDD_ORDERBOOK_DIALOG), hParent, DlgProc);
  ShowWindow(g_hDlgOrderbook, SW_SHOW);
}

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
      // Pesan ini dikirim oleh OrderbookClient
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

  LOGFONT lf = { 0 };
  lf.lfHeight = -12;               // ~9pt
  lf.lfWeight = FW_BOLD;
  strcpy_s(lf.lfFaceName, "Arial");

  g_hFont = CreateFontIndirect(&lf);
  // Apply ke controls
  SendMessage(GetDlgItem(hWnd, IDC_LIST1), WM_SETFONT, (WPARAM)g_hFont, TRUE);
  SendMessage(GetDlgItem(hWnd, IDC_EDIT_TICKER), WM_SETFONT, (WPARAM)g_hFont, TRUE);
  SendMessage(GetDlgItem(hWnd, IDC_BTN_GO), WM_SETFONT, (WPARAM)g_hFont, TRUE);
  SendMessage(GetDlgItem(hWnd, IDC_STATIC_LNAME), WM_SETFONT, (WPARAM)g_hFont, TRUE);

  // 1. Setup ListView (Grid)
  InitListView(hWnd);

  // 2. Register Window Handle ke Client agar bisa terima notifikasi
  if (g_obClient) {
    g_obClient->setWindowHandle(hWnd);
  }

  // =========================================================
  // [NEW] SETUP EDIT CONTROL (UPPERCASE & ENTER KEY)
  // =========================================================
  HWND hEdit = GetDlgItem(hWnd, IDC_EDIT_TICKER);
  
  // A. Force Uppercase
  DWORD dwStyle = GetWindowLong(hEdit, GWL_STYLE);
  SetWindowLong(hEdit, GWL_STYLE, dwStyle | ES_UPPERCASE);

  // B. Subclassing untuk Handle Enter Key
  g_wpOrigEditProc = (WNDPROC)SetWindowLongPtr(hEdit, GWLP_WNDPROC, (LONG_PTR)EditSubclassProc);

  // 3. Set Focus & Limit Text (6 char max buat ticker Indo)
  SendMessage(hEdit, EM_SETLIMITTEXT, 6, 0);
  
  // 3. Set Focus ke Edit Box biar user langsung bisa ketik
  SetFocus(hEdit);

  // === BUAT HEADER PANEL (ganti STATIC lama) ===
  g_hHeaderPanel = CreateHeaderPanel(
    hWnd,
    g_hDllModule,
    5,   // x
    75,  // y (posisi header lama)
    360, // width
    60   // height (3 baris)
  );
}

void OrderbookDlg::OnClose(HWND hWnd) {
  // Cabut handle biar client gak kirim pesan ke window mati
  if (g_obClient) {
    g_obClient->setWindowHandle(NULL);
  }
  g_hHeaderPanel = NULL;
}

// =========================================================
// INTERAKSI USER (TOMBOL GO / ENTER)
// =========================================================

void OrderbookDlg::OnCommand(HWND hWnd, WPARAM wParam) {
  int id = LOWORD(wParam);
  
  // Handle Tombol GO atau Enter Key (IDOK)
  if (id == IDC_BTN_GO || id == IDOK) {
    char buffer[32];
    GetDlgItemTextA(hWnd, IDC_EDIT_TICKER, buffer, 32);
    std::string ticker = buffer;

    if (!ticker.empty() && g_obClient) {
      // Trim & uppercase
      for (auto& c : ticker) {
        c = (char)toupper((unsigned char)c);
      }

      // Set return ke edit box
      SetDlgItemTextA(hWnd, IDC_EDIT_TICKER, ticker.c_str());

      // [CORE LOGIC] Request ke Client
      // Client akan fetch snapshot -> validasi -> subscribe socket
      g_obClient->requestTicker(ticker);
    }
  }
}

// =========================================================
// RENDER & UPDATE LOGIC
// =========================================================

void OrderbookDlg::OnStreamingUpdate(HWND hWnd, WPARAM wParam, LPARAM lParam) {
  // wParam 1 = Loading / Clear Data
  // wParam 0 = Data Update Available
  // wParam 2 = Invalid symbol
  
  if (wParam == 1) {
    ClearDisplay(hWnd);
    // Bisa update status bar: "Loading..."
    SetDlgItemTextA(hWnd, IDC_STATIC_STATUS, "Loading orderbook...");
    g_bDataReady = false;
  } else if (wParam == 2) {
    ClearDisplay(hWnd);
    SetWindowTextA(hWnd, "Orderbook - Invalid Symbol");
    SetDlgItemTextA(hWnd, IDC_STATIC_STATUS, "Invalid / Non-stock symbol");
    g_bDataReady = false;
  } else {
    UpdateDisplay(hWnd);
    SetDlgItemTextA(hWnd, IDC_STATIC_STATUS, "Live");
    g_bDataReady = true;
  }
}

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

std::string OrderbookDlg::FormatPrice(double val) {
  if (val == 0) return "-";
  // Pake FormatNumber yg lama tapi cast ke long
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
        // Di bawah 1 juta: tampilkan integer full dengan koma ribuan
        long long int_val = std::llround(val);  // rounding biar akurat
        return FormatNumberWithComma(int_val);
    }

    // Untuk kasus M atau B: ambil string dengan 2 desimal
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << display_val;
    std::string s = oss.str();

    // Sekarang tambahkan koma ribuan di bagian integer (sebelum titik desimal)
    size_t dot_pos = s.find('.');
    if (dot_pos != std::string::npos) {
        std::string integer_part = s.substr(0, dot_pos);
        std::string decimal_part = s.substr(dot_pos);  // termasuk titiknya

        int n = integer_part.length() - 3;
        while (n > 0) {
            integer_part.insert(n, ",");
            n -= 3;
        }
        s = integer_part + decimal_part;
    }

    return s + suffix;
}

void OrderbookDlg::UpdateDisplay(HWND hWnd) {
  if (!g_obClient || !g_hHeaderPanel) return;

  // 1. Ambil Data
  OrderbookSnapshot data = g_obClient->getData();
  if (data.symbol.empty()) return;

  // ---- UPDATE HEADER PANEL (1x repaint)
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

  UpdateHeaderPanel(g_hHeaderPanel, hs);


  HWND hList = GetDlgItem(hWnd, IDC_LIST1);

  // [TRICK 1] Matikan Redraw sementara biar GDI gak gambar setengah-setengah
  SendMessage(hList, WM_SETREDRAW, FALSE, 0);

  // Update Header/Title
  std::string title = "Orderbook: " + data.symbol;
  SetWindowTextA(hWnd, title.c_str());

  SetDlgItemTextA(hWnd, IDC_STATIC_LNAME, data.company_name.c_str());
  
  /*
  SetDlgItemTextA(hWnd, IDC_STATIC_LAST, FormatPrice(data.last_price).c_str());
  SetDlgItemTextA(hWnd, IDC_STATIC_PREV, FormatPrice(data.prev_close).c_str());
  SetDlgItemTextA(hWnd, IDC_STATIC_OPEN, FormatPrice(data.open).c_str());
  SetDlgItemTextA(hWnd, IDC_STATIC_HIGH, FormatPrice(data.high).c_str());
  SetDlgItemTextA(hWnd, IDC_STATIC_LOW,  FormatPrice(data.low).c_str());
  SetDlgItemTextA(hWnd, IDC_STATIC_VAL,  (FormatPrice(data.value / 1000000)+ " M").c_str());
  SetDlgItemTextA(hWnd, IDC_STATIC_FREQ,  FormatPrice(data.frequency).c_str());
  

  // Change & Percent
  SetDlgItemTextA(hWnd, IDC_STATIC_CHG, FormatPrice(data.change).c_str());
  SetDlgItemTextA(hWnd, IDC_STATIC_PCT, FormatPercent(data.percent).c_str());

  // Volume (Format Lot: Bagi 100 kalau data API dalam lembar)
  // Asumsi data.volume dari API adalah LEMBAR saham
  long lot = (long)(data.volume / 100); 
  SetDlgItemTextA(hWnd, IDC_STATIC_LOT, FormatNumber(lot).c_str());
  */

  // 2. Hitung Row yang Dibutuhkan
  // Ambil max row antara bid dan offer (biasanya orderbook punya kedalaman tetap, misal 20)
  int requiredRows = (std::max)((int)data.bids.size(), (int)data.offers.size());
  int currentRows = ListView_GetItemCount(hList);

  // [TRICK 2] Jangan DeleteAllItems! Cukup tambah row kalau kurang.
  if (currentRows < requiredRows) {
    for (int i = currentRows; i < requiredRows; ++i) {
      LVITEMA lvI = { 0 };
      lvI.mask = LVIF_TEXT;
      lvI.iItem = i;
      lvI.pszText = ""; // Init kosong dulu
      ListView_InsertItem(hList, &lvI);
    }
  }

  // 3. Update Isi Cell (In-Place Update)
  // Kita loop row yang ada dan ganti text-nya
  for (int i = 0; i < requiredRows; ++i) {
    // --- BID SIDE (Kolom 0, 1, 2) ---
    if (i < (int)data.bids.size()) {
      ListView_SetItemText(hList, i, 1, (LPSTR)FormatNumber(data.bids[i].queue).c_str());
      ListView_SetItemText(hList, i, 0, (LPSTR)FormatNumber((data.bids[i].volume) / 100).c_str());
      ListView_SetItemText(hList, i, 2, (LPSTR)FormatNumber(data.bids[i].price).c_str());
    } else {
      // Kalau data kosong di baris ini (misal offer ada 10, bid cuma 5)
      ListView_SetItemText(hList, i, 1, "");
      ListView_SetItemText(hList, i, 0, "");
      ListView_SetItemText(hList, i, 2, "");
    }

    // --- OFFER SIDE (Kolom 3, 4, 5) ---
    if (i < (int)data.offers.size()) {
      ListView_SetItemText(hList, i, 3, (LPSTR)FormatNumber(data.offers[i].price).c_str());
      ListView_SetItemText(hList, i, 5, (LPSTR)FormatNumber((data.bids[i].volume) / 100).c_str());
      ListView_SetItemText(hList, i, 4, (LPSTR)FormatNumber(data.offers[i].queue).c_str());
    } else {
      ListView_SetItemText(hList, i, 3, "");
      ListView_SetItemText(hList, i, 5, "");
      ListView_SetItemText(hList, i, 4, "");
    }
  }

  // [TRICK 3] Hapus Row Berlebih (Jarang terjadi, tapi buat jaga-jaga kalau ganti dari saham rame ke sepi)
  if (currentRows > requiredRows) {
      for (int i = currentRows - 1; i >= requiredRows; --i) {
          ListView_DeleteItem(hList, i);
      }
  }

  // [TRICK 1] Nyalakan Redraw kembali
  SendMessage(hList, WM_SETREDRAW, TRUE, 0);
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
// HELPER (INIT & FORMAT)
// =========================================================

void OrderbookDlg::InitListView(HWND hWnd) {
  HWND hList = GetDlgItem(hWnd, IDC_LIST1);
  
  // Set Style Report & Double Buffer (Anti Flicker)
  SetWindowLong(hList, GWL_STYLE, GetWindowLong(hList, GWL_STYLE) | LVS_REPORT);
  ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

  // Hitung Lebar Client Area untuk Full Stretch
  RECT rc;
  GetClientRect(hList, &rc);
  // Kurangi dengan lebar Scrollbar Vertical System
  int scrollWidth = GetSystemMetrics(SM_CXVSCROLL);
  int totalWidth = rc.right - scrollWidth; // Lebar total area listview (pixel)
  
  // Ada 6 Kolom. Kita bagi rata (atau bisa diproporsikan)
  // Vol, Q, Bid | Offer, Q, Vol
  int w = totalWidth / 6;
  
  // Koreksi sedikit untuk kolom terakhir biar pas banget (sisa pembagian)
  int wLast = totalWidth - (w * 5); 

  LVCOLUMNA lvc;
  lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
  lvc.fmt = LVCFMT_RIGHT; // Rata Kanan (Angka)

  char* cols[] = { "Freq", "Lot", "Bid", "Offer", "Lot", "Freq" };
  
  for (int i = 0; i < 6; i++) {
    lvc.iSubItem = i;
    lvc.pszText = cols[i];
    
    // Atur lebar: Kolom terakhir pakai sisa lebar
    if (i == 5) lvc.cx = wLast;
    else lvc.cx = w;

    ListView_InsertColumn(hList, i, &lvc);
  }
}

std::string OrderbookDlg::FormatNumber(long val) {
  // Format angka ribuan (simple)
  // Bisa diganti pakai logic locale yang lebih canggih
  if (val == 0) return "";
  std::string s = std::to_string(val);
  int n = s.length() - 3;
  while (n > 0) {
      s.insert(n, ",");
      n -= 3;
  }
  return s;
}

LRESULT OrderbookDlg::OnListViewCustomDraw(HWND hWnd, LPARAM lParam) {
    LPNMLVCUSTOMDRAW cd = (LPNMLVCUSTOMDRAW)lParam;

    switch (cd->nmcd.dwDrawStage) {
        case CDDS_PREPAINT:
            return CDRF_NOTIFYITEMDRAW;

        case CDDS_ITEMPREPAINT:
            return CDRF_NOTIFYSUBITEMDRAW;

        case CDDS_ITEMPREPAINT | CDDS_SUBITEM:
        {
            // Jangan proses kalau data belum siap
            if (!g_bDataReady || !g_obClient) {
                cd->clrText = RGB(0, 0, 0);
                cd->clrTextBk = RGB(255, 255, 255);
                return CDRF_DODEFAULT;
            }

            const OrderbookSnapshot& data = g_obClient->getData();
            int item = (int)cd->nmcd.dwItemSpec;
            int subitem = cd->iSubItem;

            // Default
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

            if (price > prev) {
                cd->clrText = RGB(0, 180, 0);
            }
            else if (price < prev) {
                cd->clrText = RGB(200, 0, 0);
            }
            // else: sama → hitam (default)

            return CDRF_DODEFAULT;
        }
    }

    return CDRF_DODEFAULT;
}
