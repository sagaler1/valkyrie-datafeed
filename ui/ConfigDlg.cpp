#include "ConfigDlg.h"
#include "resource.h"
#include "api_client.h"
#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <windows.h>
#include <thread>
#include <atlbase.h>
#include <comdef.h>

#define MAX_SYMBOL_LEN 48
extern HWND g_hAmiBrokerWnd;

// ---- Utility Helpers ----
static void LogOLE(const std::string& msg) {
  SYSTEMTIME t;
  GetLocalTime(&t);
  char buf[64];
  sprintf_s(buf, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
  OutputDebugStringA((std::string(buf) + msg + "\n").c_str());
}

static void SetStatusText(HWND hDlg, const std::string& text) {
  SetDlgItemTextA(hDlg, IDC_STATUS_SYMBOL, text.c_str());
}

static std::wstring to_wstring_ascii(const std::string& s) {
  return std::wstring(s.begin(), s.end());
}

// ---- Core OLE Logic ----

static IDispatch* GetBrokerApplication() {
  CLSID clsid;
  if (FAILED(CLSIDFromProgID(L"Broker.Application", &clsid))) return nullptr;

  IDispatch* pDisp = nullptr;
  if (SUCCEEDED(CoCreateInstance(clsid, NULL, CLSCTX_LOCAL_SERVER, IID_IDispatch, (void**)&pDisp))) {
    return pDisp;
  }
  return nullptr;
}

static IDispatch* GetStocksCollection(IDispatch* pApp) {
  if (!pApp) return nullptr;
  OLECHAR* propStocks = L"Stocks";
  DISPID dispidStocks;
  if (FAILED(pApp->GetIDsOfNames(IID_NULL, &propStocks, 1, LOCALE_USER_DEFAULT, &dispidStocks))) return nullptr;

  VARIANT result;
  VariantInit(&result);
  DISPPARAMS noArgs = { nullptr, nullptr, 0, 0 };
  if (SUCCEEDED(pApp->Invoke(dispidStocks, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_PROPERTYGET, &noArgs, &result, NULL, NULL))) {
    return result.pdispVal;
  }
  return nullptr;
}

// ---- (MODIFIKASI 1) ----
// Ubah fungsi ini biar nerima struct SymbolInfo
static bool AddSymbolToStocks(IDispatch* pStocks, const SymbolInfo& info) {
  if (!pStocks) return false;

  OLECHAR* methodAdd = L"Add";
  DISPID dispidAdd;
  if (FAILED(pStocks->GetIDsOfNames(IID_NULL, &methodAdd, 1, LOCALE_USER_DEFAULT, &dispidAdd))) return false;

  // --- Argumen Ticker (info.code) ---
  std::wstring wticker = to_wstring_ascii(info.code);
  VARIANTARG arg;
  VariantInit(&arg);
  arg.vt = VT_BSTR;
  arg.bstrVal = SysAllocString(wticker.c_str());

  DISPPARAMS params = { &arg, nullptr, 1, 0 };
  VARIANT stockResult;
  VariantInit(&stockResult);

  HRESULT hrInvoke = pStocks->Invoke(dispidAdd, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &params, &stockResult, NULL, NULL);
  VariantClear(&arg);

  if (FAILED(hrInvoke) || stockResult.vt != VT_DISPATCH) return false;

  IDispatch* pStock = stockResult.pdispVal;

  // --- Set FullName (info.name) ---
  OLECHAR* propFullName = L"FullName";
  DISPID dispidFullName;
  if (SUCCEEDED(pStock->GetIDsOfNames(IID_NULL, &propFullName, 1, LOCALE_USER_DEFAULT, &dispidFullName))) {
    std::wstring wname = to_wstring_ascii(info.name);
    VARIANTARG val;
    VariantInit(&val);
    val.vt = VT_BSTR;
    val.bstrVal = SysAllocString(wname.c_str());
    DISPID dispidNamed = DISPID_PROPERTYPUT;
    DISPPARAMS dispParams = { &val, &dispidNamed, 1, 1 };
    pStock->Invoke(dispidFullName, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_PROPERTYPUT, &dispParams, NULL, NULL, NULL);
    VariantClear(&val);
  }

  // ---- BLOK Sector & Industry ----
  if (info.sector_id >= 0)
  {
    OLECHAR* propSectorID = L"SectorID";
    DISPID dispidSectorID;
    if (SUCCEEDED(pStock->GetIDsOfNames(IID_NULL, &propSectorID, 1, LOCALE_USER_DEFAULT, &dispidSectorID))) {
      VARIANTARG val;
      VariantInit(&val);
      val.vt = VT_I4; // "Long" untuk OLE property
      val.lVal = info.sector_id;

      DISPID dispidNamed = DISPID_PROPERTYPUT;
      DISPPARAMS dispParams = { &val, &dispidNamed, 1, 1 };
      pStock->Invoke(dispidSectorID, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_PROPERTYPUT, &dispParams, NULL, NULL, NULL);
      VariantClear(&val);
    }
  }

  if (info.industry_id >= 0)
  {
    OLECHAR* propIndustryID = L"IndustryID";
    DISPID dispidIndustryID;
    if (SUCCEEDED(pStock->GetIDsOfNames(IID_NULL, &propIndustryID, 1, LOCALE_USER_DEFAULT, &dispidIndustryID))) {
      VARIANTARG val;
      VariantInit(&val);
      val.vt = VT_I4; // VT_I4 adalah C++ OLE type untuk "Long" 32-bit
      val.lVal = info.industry_id;

      DISPID dispidNamed = DISPID_PROPERTYPUT;
      DISPPARAMS dispParams = { &val, &dispidNamed, 1, 1 };
      pStock->Invoke(dispidIndustryID, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_PROPERTYPUT, &dispParams, NULL, NULL, NULL);
      VariantClear(&val);
    }
  }

  // ---- Mark Modified =  ----
  OLECHAR* propModified = L"Modified";
  DISPID dispidModified;
  if (SUCCEEDED(pStock->GetIDsOfNames(IID_NULL, &propModified, 1, LOCALE_USER_DEFAULT, &dispidModified))) {
    VARIANTARG valMod;
    VariantInit(&valMod);
    valMod.vt = VT_BOOL;
    valMod.boolVal = VARIANT_TRUE;
    DISPID dispidNamed = DISPID_PROPERTYPUT;
    DISPPARAMS dispParams = { &valMod, &dispidNamed, 1, 1 };
    pStock->Invoke(dispidModified, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_PROPERTYPUT, &dispParams, NULL, NULL, NULL);
    VariantClear(&valMod);
  }

  pStock->Release();
  VariantClear(&stockResult);
  return true;
}
// ------------------------------------------

static void SaveDatabase(IDispatch* pApp) {
  if (!pApp) return;
  OLECHAR* methodSaveDb = L"SaveDatabase";
  DISPID dispidSave;
  if (SUCCEEDED(pApp->GetIDsOfNames(IID_NULL, &methodSaveDb, 1, LOCALE_USER_DEFAULT, &dispidSave))) {
    DISPPARAMS noArgs = { nullptr, nullptr, 0, 0 };
    pApp->Invoke(dispidSave, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &noArgs, NULL, NULL, NULL);
  }
}

// ---- Batch entry point with UI progress
static bool AddSymbolsViaOLEBatch(HWND hDlg, const std::vector<SymbolInfo>& list) {
  if (list.empty()) return false;

  CoInitialize(NULL);
  LogOLE("[BATCH] COM Initialized");

  IDispatch* pApp = GetBrokerApplication();
  if (!pApp) {
    SetStatusText(hDlg, "ERROR: Cannot create Broker.Application instance.");
    CoUninitialize();
    return false;
  }

  IDispatch* pStocks = GetStocksCollection(pApp);
  if (!pStocks) {
    SetStatusText(hDlg, "ERROR: Cannot get Stocks collection.");
    pApp->Release();
    CoUninitialize();
    return false;
  }

  int count = 0;
  for (const auto& info : list) {
    if (info.code.empty() || info.name.empty()) continue;

    bool ok = false;
    try {
      // ---- (MODIFIKASI 3) ----
      // Panggil fungsi yang udah di-update
      ok = AddSymbolToStocks(pStocks, info);
    } catch (...) {
      LogOLE("[BATCH] Exception while adding symbol: " + info.code);
      continue;
    }

    if (ok) count++;
    if (count % 100 == 0) {
      std::stringstream ss;
      ss << "Added via OLE (Batch): " << count << " / " << list.size();
      SetStatusText(hDlg, ss.str());
      UpdateWindow(hDlg);
    }
  }

    SaveDatabase(pApp);
    LogOLE("[BATCH] SaveDatabase complete");

    pStocks->Release();
    pApp->Release();
    CoUninitialize();

    SetStatusText(hDlg, "Batch update complete! Added " + std::to_string(count) + " symbols.");
    return true;
}

// ---- Dialog Logic ----
CConfigureDlg::CConfigureDlg(struct InfoSite* pSite) : m_pSite(pSite) {}

void CConfigureDlg::DoModal(HWND hParent) {
  extern HMODULE g_hDllModule;
  DialogBoxParam(g_hDllModule, MAKEINTRESOURCE(IDD_CONFIGURE_PLUGIN), hParent, DialogProc, (LPARAM)this);
}

BOOL CConfigureDlg::OnInitDialog(HWND hDlg) {
  SetStatusText(hDlg, "Click 'Retrieve ALL Symbols' to download the latest emiten list.");
  return TRUE;
}

void CConfigureDlg::OnRetrieveClicked(HWND hDlg) {
  HWND hBtn = GetDlgItem(hDlg, IDC_RETRIEVE_BUTTON);
  EnableWindow(hBtn, FALSE);

  SetStatusText(hDlg, "Fetching symbols... Please Wait.");
  UpdateWindow(hDlg);

  m_fetchedSymbolList = fetchSymbolList();

  if (m_fetchedSymbolList.empty()) {
    SetStatusText(hDlg, "Failed to fetch symbol list or list is empty.");
  } else {
    std::stringstream ss;
    ss << "Fetched " << m_fetchedSymbolList.size() << " symbols. Adding symbols...";
    SetStatusText(hDlg, ss.str());
    UpdateWindow(hDlg);

    AddSymbolsViaOLEBatch(hDlg, m_fetchedSymbolList);
  }

  EnableWindow(hBtn, TRUE);
  m_fetchedSymbolList.clear();

  if (g_hAmiBrokerWnd) {
    PostMessage(g_hAmiBrokerWnd, WM_USER_STREAMING_UPDATE, 0, 0);
  }
}

INT_PTR CALLBACK CConfigureDlg::DialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
  CConfigureDlg* pDlg = nullptr;

  if (message == WM_INITDIALOG) {
    SetWindowLongPtr(hDlg, GWLP_USERDATA, (LONG_PTR)lParam);
    pDlg = (CConfigureDlg*)lParam;
    if (pDlg) pDlg->OnInitDialog(hDlg);
    return (INT_PTR)TRUE;
  }

  pDlg = (CConfigureDlg*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
  if (!pDlg) return (INT_PTR)FALSE;

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
