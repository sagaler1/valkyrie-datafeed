#ifndef CONFIGURE_DLG_H
#define CONFIGURE_DLG_H

#include <windows.h>
#include <map>
#include <string>
#include "plugin.h"       // Untuk InfoSite dan StockInfo
#include "types.h"        // Untuk SymbolInfo
#include <vector>

class CConfigureDlg
{
  public:
    // Menerima pointer InfoSite dari AmiBroker saat inisialisasi
    CConfigureDlg(struct InfoSite* pSite);
    
    // Fungsi utama untuk menampilkan dialog
    void DoModal(HWND hParent);

  private:
    struct InfoSite* m_pSite; 

    // NEW: Variabel untuk menyimpan data yang sudah di-fetch di background
    std::vector<SymbolInfo> m_fetchedSymbolList; 
    std::map<std::string, int> m_sectorMap;
    std::map<std::string, int> m_industryMap;

    static INT_PTR CALLBACK DialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
    
    BOOL OnInitDialog(HWND hDlg);
    void OnRetrieveClicked(HWND hDlg);
    void UpdateSymbols(HWND hDlg, const std::vector<SymbolInfo>& symbol_list);

    // Helper untuk mendapatkan index category (memastikan kategori tidak duplikat)
    int GetCategoryIndex(struct InfoSite* pSite, int nCategory, const std::string& categoryName, std::map<std::string, int>& categoryMap);

    // NEW: Fungsi yang dipanggil di GUI thread untuk memproses data
    void ProcessFetchedSymbols(HWND hDlg);
};


#endif // CONFIGURE_DLG_H