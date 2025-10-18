#include "EODBackfillDlg.h"
#include "plugin.h"
#include "resource.h"
#include "api_client.h" // Diperlukan untuk fetchDailyBackfill
#include "data_store.h" // Diperlukan untuk gDataStore
#include <CommCtrl.h>   // Diperlukan untuk kontrol Date Picker
#include <string>
#include <vector>
#include <map>
#include <thread>       // Diperlukan untuk menjalankan fetch di background
#include <windows.h>

// Deklarasi variabel global dari file lain
extern DataStore gDataStore;
extern HWND g_hAmiBrokerWnd;

// Fungsi untuk mengupdate teks status di dialog
static void SetStatusText(HWND hDlg, const std::string& text) {
    SetDlgItemTextA(hDlg, IDC_STATUS_TEXT, text.c_str());
}

// Implementasi Class CEODBackfillDlg
void CEODBackfillDlg::DoModal(HWND hParent) {
    // Tampilkan dialog dan tunggu sampai ditutup
    // DialogBoxParam(NULL, MAKEINTRESOURCE(IDD_EOD_BACKFILL), hParent, DialogProc, (LPARAM)this);

    extern HMODULE g_hDllModule; // tambahkan ini di sini
    OutputDebugStringA("[EODDlg] Opening EOD Backfill dialog...");
    DialogBoxParam(
        g_hDllModule,                 // <-- ini penting, bukan NULL
        MAKEINTRESOURCE(IDD_EOD_BACKFILL),
        hParent,
        DialogProc,
        (LPARAM)this
    );
}

// Fungsi yang menangani klik tombol "Fetch Data"
void CEODBackfillDlg::OnFetchClicked(HWND hDlg) {
    // Nonaktifkan tombol agar tidak diklik dua kali
    EnableWindow(GetDlgItem(hDlg, IDC_FETCH_BUTTON), FALSE);
    SetStatusText(hDlg, "Fetching data from API... This may take a moment.");

    // Ambil tanggal dari Date Picker
    SYSTEMTIME st = {0};
    DateTime_GetSystemtime(GetDlgItem(hDlg, IDC_DATEPICKER_EOD), &st);

    char date_buffer[11];
    sprintf_s(date_buffer, "%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
    std::string selected_date(date_buffer);

    // Jalankan proses fetch di thread terpisah agar UI tidak freeze
    std::thread([hDlg, selected_date]() {
        // Panggil fungsi API yang sudah kita buat di Step A
        std::map<std::string, Candle> backfill_data = fetchDailyBackfill(selected_date);

        if (backfill_data.empty()) {
            SetStatusText(hDlg, "Failed to fetch data or no data available for the selected date.");
            EnableWindow(GetDlgItem(hDlg, IDC_FETCH_BUTTON), TRUE); // Aktifkan lagi tombolnya
            return;
        }

        SetStatusText(hDlg, "Data received. Updating in-memory cache for all symbols...");

        // ==========================================================
        // ---- FIX: GUNAKAN FUNGSI BARU updateEodBar ----
        // ==========================================================
        for (const auto& pair : backfill_data) {
            const std::string& symbol = pair.first;
            const Candle& candle = pair.second;
            
            // Panggil fungsi baru yang lebih aman
            gDataStore.updateEodBar(symbol, candle);
        }
        // ==========================================================

        std::string final_message = "Backfill complete! Updated " + std::to_string(backfill_data.size()) + " symbols. \nPlease refresh your charts to see the changes.";
        SetStatusText(hDlg, final_message);
        
        // Beri tahu AmiBroker untuk refresh semua data
        if (g_hAmiBrokerWnd) {
            PostMessage(g_hAmiBrokerWnd, WM_USER_STREAMING_UPDATE, 0, 0);
        }

        EnableWindow(GetDlgItem(hDlg, IDC_FETCH_BUTTON), TRUE); // Aktifkan lagi tombolnya
    }).detach(); // .detach() agar thread berjalan di background tanpa perlu di-join
}

// Fungsi yang dijalankan saat dialog pertama kali dibuat
BOOL CEODBackfillDlg::OnInitDialog(HWND hDlg) {
    // Set tanggal default di date picker ke hari ini
    SYSTEMTIME st;
    GetLocalTime(&st);
    DateTime_SetSystemtime(GetDlgItem(hDlg, IDC_DATEPICKER_EOD), GDT_VALID, &st);
    return TRUE;
}

// Prosedur dialog utama yang dipanggil oleh Windows
INT_PTR CALLBACK CEODBackfillDlg::DialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    CEODBackfillDlg* pDlg = nullptr;

    if (message == WM_INITDIALOG) {
        // Simpan pointer 'this' ke window agar bisa diakses nanti
        SetWindowLongPtr(hDlg, GWLP_USERDATA, (LONG_PTR)lParam);
        pDlg = (CEODBackfillDlg*)lParam;
        pDlg->OnInitDialog(hDlg);
        return (INT_PTR)TRUE;
    }

    // Ambil pointer 'this' dari window
    pDlg = (CEODBackfillDlg*)GetWindowLongPtr(hDlg, GWLP_USERDATA);

    if (pDlg == nullptr) {
        return (INT_PTR)FALSE;
    }

    switch (message) {
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_FETCH_BUTTON:
                    pDlg->OnFetchClicked(hDlg);
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