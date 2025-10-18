#include "ConfigureDlg.h"
#include "resource.h"
#include "api_client.h" // untuk fetchSymbolList()
#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <windows.h>
#include <thread>

#define MAX_SYMBOL_LEN 48
#define WM_USER_PROCESS_SYMBOLS (WM_USER + 500) 
extern HWND g_hAmiBrokerWnd; // dari plugin.cpp

static bool IsInfoSiteValid(struct InfoSite* pSite) {
    if (!pSite) return false;

    bool ok = false;
    __try {
        if (pSite->GetStockQty() >= 0)
            ok = true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    return ok;
}

// --- Logging helper ---
void LogSymbols(const std::string& msg) {
    SYSTEMTIME t;
    GetLocalTime(&t);
    char buf[64];
    sprintf_s(buf, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    OutputDebugStringA((std::string(buf) + msg + "\n").c_str());
}

// --- UI helper untuk update status teks ---
static void SetStatusText(HWND hDlg, const std::string& text) {
    SetDlgItemTextA(hDlg, IDC_STATUS_SYMBOL, text.c_str());
}

// --- Konstruktor ---
CConfigureDlg::CConfigureDlg(struct InfoSite* pSite)
    : m_pSite(pSite) {}

// --- Menampilkan dialog ---
void CConfigureDlg::DoModal(HWND hParent) {
    extern HMODULE g_hDllModule;
    DialogBoxParam(g_hDllModule, MAKEINTRESOURCE(IDD_CONFIGURE_PLUGIN), hParent, DialogProc, (LPARAM)this);
}

// --- Ambil/buat index kategori dengan aman ---
int CConfigureDlg::GetCategoryIndex(struct InfoSite* pSite, int nCategory, const std::string& categoryName, std::map<std::string, int>& categoryMap) {
    if (!pSite || categoryName.empty()) return 0;

    // Jika sudah ada di map
    if (categoryMap.count(categoryName)) {
        return categoryMap.at(categoryName);
    }

    for (int i = 1; i < 200; i++) {
        const char* existingName = pSite->GetCategoryName(nCategory, i);
        if (!existingName || existingName[0] == 0) {
            // slot kosong
            if (pSite->SetCategoryName(nCategory, i, categoryName.c_str())) {
                categoryMap[categoryName] = i;
                return i;
            }
        } else if (strcmp(existingName, categoryName.c_str()) == 0) {
            categoryMap[categoryName] = i;
            return i;
        }
    }
    return 0;
}

// --- Fungsi utama untuk update simbol ---
void CConfigureDlg::UpdateSymbols(HWND hDlg, const std::vector<SymbolInfo>& symbol_list) {
    if (!m_pSite || !IsInfoSiteValid(m_pSite)) {
        SetStatusText(hDlg, "ERROR: Invalid InfoSite or database locked.");
        return;
    }

    int updated_count = 0;
    for (const auto& info : symbol_list) {
        // Validasi dasar
        if (info.code.empty() || info.code.size() >= MAX_SYMBOL_LEN || info.name.empty() || info.name.size() >= 128) {
            LogSymbols("ERROR: Invalid symbol - code: " + info.code + ", name: " + info.name);
            continue;
        }

        // Tambah symbol
        struct StockInfo* pCurrentSINew = m_pSite->AddStockNew(info.code.c_str());
        if (pCurrentSINew) {
            // Zero-out struct
            memset(pCurrentSINew, 0, sizeof(StockInfo));

            // Set field wajib
            strncpy_s(pCurrentSINew->ShortName, MAX_SYMBOL_LEN, info.code.c_str(), _TRUNCATE);
            strncpy_s(pCurrentSINew->FullName, sizeof(pCurrentSINew->FullName), info.name.c_str(), _TRUNCATE);
            pCurrentSINew->DataSource = 0; // Hardcode default plugin ID
            pCurrentSINew->MarketID = 0;
            pCurrentSINew->GroupID = 0;
            pCurrentSINew->IndustryID = 0;

            // Log
            LogSymbols("Added symbol: " + info.code + ", Name: " + info.name);
            updated_count++;
            if (updated_count % 100 == 0) {
                std::stringstream ss;
                ss << "Updating symbols... (" << updated_count << " of " << symbol_list.size() << ") Current: " << info.code;
                SetStatusText(hDlg, ss.str());
            }
        } else {
            LogSymbols("ERROR: AddStockNew failed for " + info.code);
        }
    }

    if (g_hAmiBrokerWnd) {
        PostMessage(g_hAmiBrokerWnd, WM_USER_STREAMING_UPDATE, 0, 0);
    }
    std::string final_message = "Database update complete! Added/Updated " + std::to_string(updated_count) + " symbols.";
    SetStatusText(hDlg, final_message);
}

// --- Saat tombol Retrieve diklik ---
void CConfigureDlg::OnRetrieveClicked(HWND hDlg) {
    if (!m_pSite) {
        SetStatusText(hDlg, "ERROR: AmiBroker InfoSite is NULL.");
        return;
    }
    
    EnableWindow(GetDlgItem(hDlg, IDC_RETRIEVE_BUTTON), FALSE);
    SetStatusText(hDlg, "Fetching ALL symbols from API in background... Please wait.");

    // Jalankan proses fetch di thread terpisah (Background Thread)
    std::thread([this, hDlg]() {
        // 1. FETCH DATA (SAFE TO DO IN BACKGROUND)
        m_fetchedSymbolList = fetchSymbolList();

        if (m_fetchedSymbolList.empty()) {
            SetStatusText(hDlg, "Failed to fetch symbol list or list is empty.");
            EnableWindow(GetDlgItem(hDlg, IDC_RETRIEVE_BUTTON), TRUE);
            return;
        }

        // 2. KIRIM SINYAL KE GUI THREAD SETELAH FETCH SELESAI
        // PostMessage aman dilakukan dari background thread
        PostMessage(hDlg, WM_USER_PROCESS_SYMBOLS, 0, 0); 

    }).detach(); 
}

// NEW: Fungsi ini dijalankan di GUI THREAD untuk melakukan UPDATE API AmiBroker
void CConfigureDlg::ProcessFetchedSymbols(HWND hDlg) {
    SetStatusText(hDlg, "Symbol list received. Updating AmiBroker database on GUI thread...");
    
    // Panggil fungsi UpdateSymbols (yang memanggil AddStockNew) di GUI thread!
    this->UpdateSymbols(hDlg, m_fetchedSymbolList);

    std::string final_message = "Database update complete! Added/Updated " + std::to_string(m_fetchedSymbolList.size()) + " symbols.";
    SetStatusText(hDlg, final_message);
    
    // Beri tahu AmiBroker untuk refresh database
    if (g_hAmiBrokerWnd) {
        PostMessage(g_hAmiBrokerWnd, WM_USER_STREAMING_UPDATE, 0, 0); 
    }

    EnableWindow(GetDlgItem(hDlg, IDC_RETRIEVE_BUTTON), TRUE);
    m_fetchedSymbolList.clear(); // Bersihkan buffer setelah selesai
}

// --- Inisialisasi dialog ---
BOOL CConfigureDlg::OnInitDialog(HWND hDlg) {
    SetStatusText(hDlg, "Click 'Retrieve ALL Symbols' to download the latest emiten list.");
    return TRUE;
}

// --- Dialog procedure utama ---
INT_PTR CALLBACK CConfigureDlg::DialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    CConfigureDlg* pDlg = nullptr;

    if (message == WM_INITDIALOG) {
        // Simpan pointer 'this' ke window agar bisa diakses nanti
        SetWindowLongPtr(hDlg, GWLP_USERDATA, (LONG_PTR)lParam);
        pDlg = (CConfigureDlg*)lParam;
        pDlg->OnInitDialog(hDlg);
        return (INT_PTR)TRUE;
    }

    pDlg = (CConfigureDlg*)GetWindowLongPtr(hDlg, GWLP_USERDATA);

    if (pDlg == nullptr) {
        return (INT_PTR)FALSE;
    }

    // --- PENANGANAN CUSTOM MESSAGE (THREAD MARSHALLING) ---
    if (message == WM_USER_PROCESS_SYMBOLS) {
        // Panggilan ini DIJAMIN BERADA DI GUI THREAD (Solusi Thread Safety!)
        pDlg->ProcessFetchedSymbols(hDlg);
        return (INT_PTR)TRUE;
    }
    // ----------------------------------------------------

    switch (message) {
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_RETRIEVE_BUTTON:
                    pDlg->OnRetrieveClicked(hDlg);
                    return (INT_PTR)TRUE;
                case IDCANCEL:
                    EndDialog(hDlg, LOWORD(wParam));
                    return (INT_PTR)TRUE;
            }
            break;
        case WM_CLOSE:
            EndDialog(hDlg, 0);
            return (INT_PTR)TRUE;
    }
    return (INT_PTR)FALSE;
}
