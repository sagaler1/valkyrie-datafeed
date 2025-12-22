#pragma once
#include <windows.h>
#include <string>
#include "OrderbookData.h"

// Resource ID
#include "../../core/resource.h" 

// Include Grid Control Custom
#include "../../core/grid/grid_control.h"

class OrderbookDlg {
public:
    static void Show(HINSTANCE hInst, HWND hParent);
    static INT_PTR CALLBACK DlgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

private:
    // --- GLOBAL STATE ---
    static inline HWND s_hGridWnd = NULL; // Handle ke Custom Grid

    // --- EVENT HANDLERS ---
    static void OnInitDialog(HWND hWnd);
    static void OnClose(HWND hWnd);
    static void OnCommand(HWND hWnd, WPARAM wParam);
    static void OnStreamingUpdate(HWND hWnd, WPARAM wParam, LPARAM lParam);

    // --- UI LOGIC ---
    static void InitGrid(HWND hWnd);
    static void UpdateGridDisplay(HWND hWnd);
    static void ClearGridDisplay(HWND hWnd);

    // --- FORMATTING HELPERS ---
    static std::string FormatNumber(long val);
    static std::string FormatPrice(double val);
    static std::string FormatPercent(double val);
    static std::string FormatValue(double val);

    // --- COLOR LOGIC ---
    static COLORREF GetPriceColorForGrid(double price, double prev);
    static COLORREF GetChangeColorForGrid(long change);
};