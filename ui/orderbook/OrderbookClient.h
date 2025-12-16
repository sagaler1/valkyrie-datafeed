#ifndef ORDERBOOK_CLIENT_H
#define ORDERBOOK_CLIENT_H

#include <string>
#include <thread>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>
#include <windows.h>

// Library WebSocket
#include "ixwebsocket/IXWebSocket.h"

// Data Structure
#include "OrderbookData.h"

class OrderbookClient {
public:
    OrderbookClient();
    ~OrderbookClient();

    // Lifecycle
    void start(const std::string& userId, const std::string& wsKeyUrl);
    void stop();

    // Interaction
    // Fungsi ini dipanggil Main Thread saat pindah ticker
    void setActiveTicker(const std::string& ticker);
    
    // Set handle window dialog untuk notifikasi update (PostMessage)
    void setWindowHandle(HWND hWnd);
    
    // Thread-safe getter untuk UI
    OrderbookSnapshot getData();

private:
    // Core Components
    std::unique_ptr<ix::WebSocket> m_ws;
    std::thread m_workerThread;
    std::thread m_pingThread;
    
    // State Flags
    std::atomic<bool> m_run;
    std::atomic<bool> m_isConnected;
    std::atomic<bool> m_isSubscribed;

    // Context Data
    std::string m_userId;
    std::string m_activeTicker;
    std::string m_wsKeyUrl;
    HWND m_hOrderbookWnd;

    // Data Storage (Protected by Mutex)
    OrderbookSnapshot m_data;
    std::mutex m_dataMutex;

    // --- INTERNAL LOGIC ---
    
    // Worker Entry Point (Fetch Snapshot -> Connect WS)
    void connectAndStream(); 
    
    // HTTP Snapshot Logic
    void fetchSnapshot(const std::string& symbol);
    bool parseSnapshotJson(const std::string& jsonResponse, const std::string& symbol);

    // WebSocket Logic
    void pingLoop();
    void handleMessage(const std::string& msg);
    
    // Stream Parsing Logic
    void parseStreamBody(const std::string& body);
    void parseRow(const std::string& rawRow, std::vector<OrderLevel>& targetVec);

    // Helpers
    std::string fetchWsKey(const std::string& url);
    
    // Proto Builders
    std::string buildHandshake(const std::string& userId, const std::string& key);
    std::string buildSubscribe(const std::string& userId, const std::string& key, const std::string& symbol);
    std::string buildPing();
};

#endif // ORDERBOOK_CLIENT_H