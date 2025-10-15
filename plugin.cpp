#include "plugin.h"
#include "ws_client.h"
#include "data_store.h"
#include "ami_bridge.h"
#include "resource.h"
#include "pluginstate.h"
#include "config.h"
#include <memory>
#include <atomic>
#include <iostream>
#include <windows.h>

// --- Global Variables ---
std::unique_ptr<WsClient> g_wsClient;
DataStore gDataStore;
HWND g_hAmiBrokerWnd = NULL;
HMODULE g_hDllModule = NULL;                // To store our DLL handle

std::atomic<int> g_nStatus = STATE_IDLE;


// --- DllMain Entry Point ---
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        g_hDllModule = hModule;
        break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}


// --- Main Plugin Functions ---

PLUGINAPI int GetPluginInfo(struct PluginInfo* pInfo) {
    pInfo->nStructSize = sizeof(PluginInfo);
    pInfo->nType = PLUGIN_TYPE_DATA;
    pInfo->nVersion = 100; // v0.1.0
    pInfo->nIDCode = PIDCODE('K', 'B', 'B', ' ');
    strcpy_s(pInfo->szName, "Valkyrie Datafeed");
    strcpy_s(pInfo->szVendor, "Dhani");
    pInfo->nMinAmiVersion = 52700; // Requires AmiBroker 5.27+
    return 1;
}

PLUGINAPI int Init(void) {
    g_wsClient = std::make_unique<WsClient>();
    g_nStatus = STATE_IDLE;
    return 1;
}

PLUGINAPI int Release(void) {
    if (g_wsClient) {
        g_wsClient->stop();
    }
    g_wsClient.reset();
    g_nStatus = STATE_IDLE;
    return 1;
}

PLUGINAPI struct RecentInfo* GetRecentInfo(LPCTSTR pszTicker) {
    static RecentInfo ri;
    memset(&ri, 0, sizeof(ri));

    LiveQuote q = gDataStore.getLiveQuote(pszTicker);

    if (q.symbol.empty()) return nullptr;

    strcpy_s(ri.Name, sizeof(ri.Name), q.symbol.c_str());
    ri.nStructSize = sizeof(RecentInfo);
    ri.fLast = static_cast<float>(q.lastprice);
    ri.fOpen = static_cast<float>(q.open);
    ri.fHigh = static_cast<float>(q.high);
    ri.fLow = static_cast<float>(q.low);
    ri.fTotalVol = static_cast<float>(q.volume);
    ri.fOpenInt = static_cast<float>(q.frequency);
    
    ri.nBitmap = RI_LAST | RI_OPEN | RI_HIGHLOW | RI_TOTALVOL | RI_OPENINT;

    return &ri;
}

PLUGINAPI int GetStatus(struct PluginStatus* status) {
    status->nStructSize = sizeof(PluginStatus);
    int current_status = g_nStatus;

    switch (current_status) {
        case STATE_IDLE:
            status->nStatusCode = 0x10000000;
            strcpy_s(status->szShortMessage, "IDLE");
            strcpy_s(status->szLongMessage, "Disconnected. Right-click to connect.");
            status->clrStatusColor = RGB(255, 128, 0);
            break;
        case STATE_CONNECTING:
            // ... (kode status lainnya) ...
            status->nStatusCode = 0x10000000;
            strcpy_s(status->szShortMessage, "...");
            strcpy_s(status->szLongMessage, "Connecting to WebSocket server...");
            status->clrStatusColor = RGB(255, 255, 0);
            break;
        case STATE_CONNECTED:
            status->nStatusCode = 0x00000000;
            strcpy_s(status->szShortMessage, "OK");
            strcpy_s(status->szLongMessage, "Connected to WebSocket.");
            status->clrStatusColor = RGB(0, 255, 0);
            break;
        case STATE_DISCONNECTED:
            status->nStatusCode = 0x20000000;
            strcpy_s(status->szShortMessage, "ERR");
            strcpy_s(status->szLongMessage, "Connection lost. Will attempt to reconnect.");
            status->clrStatusColor = RGB(255, 0, 0);
            break;
    }
    return 1;
}

PLUGINAPI int Notify(struct PluginNotification* pn) {
    if (pn->nReason == REASON_DATABASE_LOADED) {
        g_hAmiBrokerWnd = pn->hMainWnd;
        if (g_wsClient) {
            g_wsClient->setAmiBrokerWindow(g_hAmiBrokerWnd, &g_nStatus);
        }
    }

    if (pn->nReason == REASON_DATABASE_UNLOADED) {
        if (g_wsClient) {
            g_wsClient->stop();
        }
        g_hAmiBrokerWnd = NULL;
    }
    
    if (pn->nReason == REASON_STATUS_RMBCLICK) {
        std::string username = Config::getInstance().getUsername();
        std::string api_host = Config::getInstance().getHost();

        HMENU hMenu = LoadMenu(g_hDllModule, MAKEINTRESOURCE(IDR_STATUS_MENU));
        HMENU hSubMenu = GetSubMenu(hMenu, 0);

        if (hSubMenu) {
            // ... (kode menu) ...
            int current_status = g_nStatus;
            if (current_status == STATE_CONNECTED || current_status == STATE_CONNECTING) {
                EnableMenuItem(hSubMenu, ID_STATUS_CONNECT, MF_BYCOMMAND | MF_DISABLED | MF_GRAYED);
            } else {
                EnableMenuItem(hSubMenu, ID_STATUS_DISCONNECT, MF_BYCOMMAND | MF_DISABLED | MF_GRAYED);
            }
            
            POINT pos;
            GetCursorPos(&pos);
            SetForegroundWindow(g_hAmiBrokerWnd);
            int selection = TrackPopupMenu(hSubMenu, TPM_NONOTIFY | TPM_RETURNCMD, pos.x, pos.y, 0, g_hAmiBrokerWnd, NULL);
            PostMessage(g_hAmiBrokerWnd, WM_NULL, 0, 0);

            switch (selection) {
                case ID_STATUS_CONNECT:
                    if(g_wsClient) {
                        g_nStatus = STATE_CONNECTING;
                        g_wsClient->start(username, api_host + "/api/amibroker/socketkey");
                    }
                    break;
                case ID_STATUS_DISCONNECT:
                    if(g_wsClient) {
                        g_wsClient->stop();
                    }
                    g_nStatus = STATE_IDLE;
                    break;
                case ID_STATUS_CONFIGURE:
                    break;
            }
        }
        DestroyMenu(hMenu);
    }
    return 1;
}

// ---- GetQuotesEx() function is a basic function that all data plugins must export and it is called each time AmiBroker wants to get new quotes.
PLUGINAPI int GetQuotesEx(LPCTSTR pszTicker, int nPeriodicity, int nLastValid, int nSize, struct Quotation* pQuotes, GQEContext* pContext)
{
    return GetQuotesEx_Bridge(pszTicker, nPeriodicity, nLastValid, nSize, pQuotes);
}