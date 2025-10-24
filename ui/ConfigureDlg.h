#ifndef CONFIGURE_DLG_H
#define CONFIGURE_DLG_H

#include <windows.h>
#include <map>
#include <string>
#include "plugin.h"       // For InfoSite dan StockInfo
#include "types.h"        // For SymbolInfo
#include <vector>

class CConfigureDlg
{
  public:
    // Receives InfoSite pointer from AmiBroker on initialization
    CConfigureDlg(struct InfoSite* pSite);
    
    // The main function for displaying dialogs
    void DoModal(HWND hParent);

  private:
    struct InfoSite* m_pSite; 

    std::vector<SymbolInfo> m_fetchedSymbolList; 
    std::map<std::string, int> m_sectorMap;
    std::map<std::string, int> m_industryMap;

    static INT_PTR CALLBACK DialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
    
    BOOL OnInitDialog(HWND hDlg);
    void OnRetrieveClicked(HWND hDlg);

    // ---- TODO: Get category index
    // int GetCategoryIndex(struct InfoSite* pSite, int nCategory, const std::string& categoryName, std::map<std::string, int>& categoryMap);
};


#endif // CONFIGURE_DLG_H