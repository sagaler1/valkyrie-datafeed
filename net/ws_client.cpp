#include "ws_client.h"
#include "data_store.h"
#include "plugin.h"
#include "pluginstate.h"
#include "dotenv.h"
#include <windows.h>
#include <sstream>
#include <iostream>
#include <fstream>
#include <chrono>
#include "api_client.h" // WinHttpGetData declaration
#include <nlohmann/json.hpp>
#include "handshake.pb.h"
#include "ping.pb.h"
#include "subscribe.pb.h"
#include "feed.pb.h"
#include "pong.pb.h"
#include "config.h"

using json = nlohmann::json;
void LogWS(const std::string& msg) {
    OutputDebugStringA((msg + "\n").c_str());
}

// === Redirect std::cout to DebugView ===
class DebugStreamBuf : public std::streambuf {
protected:
    virtual int overflow(int c) override {
        if (c != EOF) {
            char buf[2] = { (char)c, 0 };
            OutputDebugStringA(buf);
        }
        return c;
    }
};

static DebugStreamBuf dbgBuf;
static std::ostream dbgOut(&dbgBuf);
#define stdcout dbgOut
#define stdcerr dbgOut

// --- Implementasi WsClient ---
WsClient::WsClient() : m_run(false), m_isConnected(false), m_hAmiBrokerWnd(NULL), m_pStatus(nullptr) {}

WsClient::~WsClient() { stop(); }

void WsClient::setAmiBrokerWindow(HWND hWnd, std::atomic<int>* pStatus) {
    m_hAmiBrokerWnd = hWnd;
    m_pStatus = pStatus;
}

void WsClient::start(const std::string& userId, const std::string& wsKeyUrl) {
    if (m_run) {
        LogWS("[WS] Client is already running.");
        return;
    }
    LogWS("[WS] Start command received.");
    //stdcout << "[WS] Start command received." << std::endl;
    m_userId = userId;
    m_wsKeyUrl = wsKeyUrl;
    m_run = true;
    m_thread = std::thread(&WsClient::run, this);
}

void WsClient::stop() {
    if (!m_run) return;
    LogWS("[WS] Stop command received.");
    m_run = false;
    m_isConnected = false;

    if (m_ws) m_ws->stop(1000, "Client shutdown");
    if (m_pingThread.joinable()) m_pingThread.join();
    if (m_thread.joinable()) m_thread.join();

    LogWS("[WS] Client stopped.");
}

bool WsClient::isConnected() const { return m_isConnected; }

void WsClient::run() {
    std::string socket_url = Config::getInstance().getSocketUrl();
    m_ws = std::make_unique<ix::WebSocket>();
    m_ws->setUrl(socket_url);

    //stdcout << "[WS] Worker thread started. Fetching wskey from " << m_wsKeyUrl << std::endl;
    LogWS(std::string("[WS] Worker thread started. Fetching wskey from") + m_wsKeyUrl);
    std::string wskey = fetchWsKey(m_wsKeyUrl);
    if (wskey.empty()) {
        stdcerr << "[WS] CRITICAL: Failed to fetch wskey. Stopping thread." << std::endl;
        if (m_pStatus) *m_pStatus = 2;
        m_run = false;
        return;
    }
    LogWS("[WS] wskey successfully fetched.");

    auto symbols = loadSymbols();
    std::atomic<bool> isSubscribed{false};

    m_ws->setOnMessageCallback([&](const ix::WebSocketMessagePtr& msg) {
        if (msg->type == ix::WebSocketMessageType::Open) {
            LogWS("[WS] ==> EVENT: Open. Connection established.");
            m_isConnected = true;
            if (m_pStatus) *m_pStatus = STATE_CONNECTED;
            m_ws->sendBinary(buildHandshakeBinary(m_userId, wskey));
            LogWS("[WS] Handshake binary sent.");

            if (m_pingThread.joinable()) m_pingThread.join();
            m_pingThread = std::thread(&WsClient::pingLoop, this);

        } else if (msg->type == ix::WebSocketMessageType::Message) {
            LogWS(std::string("[WS] ==> EVENT: Message. Size: " + std::to_string(msg->str.size()) + " bytes"));
            if (!isSubscribed) {
                PongReceive pong;
                if (pong.ParseFromString(msg->str) && pong.has_pong()) {
                    LogWS(std::string("[WS] Handshake confirmed. Subscribing to " + std::to_string(symbols.size()) + " symbols"));
                    if (!symbols.empty()) m_ws->sendBinary(buildSubscribeBinary(m_userId, wskey, symbols));
                    isSubscribed = true;
                } else {
                    stdcerr << "[WS] WARNING: Message before handshake confirmed." << std::endl;
                }
            } else {
                StockFeed feed;
                if (feed.ParseFromString(msg->str) && feed.has_stock_data()) {
                    gDataStore.updateLiveQuote(feed);
                    if (m_hAmiBrokerWnd) PostMessage(m_hAmiBrokerWnd, WM_USER_STREAMING_UPDATE, 0, 0);
                }
            }
        } else if (msg->type == ix::WebSocketMessageType::Close) {
            //stdcout << "[WS] ==> EVENT: Close. Code: " << msg->closeInfo.code << " Reason: " << msg->closeInfo.reason << std::endl;
            LogWS(std::string("[WS] ==> EVENT: Close. Code: " + std::to_string(msg->closeInfo.code) + " Reason: " + msg->closeInfo.reason));
            
            m_isConnected = false;
            if (m_pStatus) *m_pStatus = STATE_DISCONNECTED;

        } else if (msg->type == ix::WebSocketMessageType::Error) {
            //stdcerr << "[WS] ==> EVENT: Error. Reason: " << msg->errorInfo.reason << std::endl;
            LogWS(std::string("[WS] ==> EVENT: Error. Reason: " + msg->errorInfo.reason));
            m_isConnected = false;
            if (m_pStatus) *m_pStatus = STATE_DISCONNECTED;
        }
    });

    LogWS("[WS] Starting connection loop.");
    while (m_run) {
        if (!m_isConnected) {
            LogWS("[WS] Not connected. Attempting to start connection...");
            m_ws->start();
            std::this_thread::sleep_for(std::chrono::seconds(5));
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }
    LogWS("[WS] Connection loop finished.");
}

void WsClient::pingLoop() {
    LogWS("[Ping] Ping thread started.");
    while (m_isConnected && m_run) {
        for (int i = 0; i < 100 && m_isConnected && m_run; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (m_isConnected && m_run) {
            LogWS("[Ping] Sending ping.");
            m_ws->sendBinary(buildPingBinary());
        }
    }
    LogWS("[Ping] Ping thread finished.");
}

// --- Helper Implementations ---
std::string WsClient::fetchWsKey(const std::string& url) {
    std::string readBuffer = WinHttpGetData(url);

    if (readBuffer.empty()) return "";

    try {
        auto j = json::parse(readBuffer);
        if (j.contains("data") && j["data"].contains("key"))
            return j["data"]["key"].get<std::string>();
    } catch (...) {
        return "";
    }
    return "";
}

std::string WsClient::buildHandshakeBinary(const std::string& userId, const std::string& key) {
    Handshake hs;
    hs.set_userid(userId);
    hs.set_key(key);
    std::string out;
    hs.SerializeToString(&out);
    return out;
}

std::string WsClient::buildPingBinary() {
    WSWrapper wrapper;
    Ping* ping = wrapper.mutable_ping();
    ping->set_message("ping");
    std::string out;
    wrapper.SerializeToString(&out);
    return out;
}

std::string WsClient::buildSubscribeBinary(const std::string& userId, const std::string& key, const std::vector<std::string>& symbols) {
    SymbolSubscribe sub;
    sub.set_userid(userId);
    sub.set_key(key);
    LiveFeedSub* liveSub = sub.mutable_subs();
    for (const auto& sym : symbols) liveSub->add_livequote(sym);
    std::string out;
    sub.SerializeToString(&out);
    return out;
}

std::vector<std::string> WsClient::loadSymbols(const std::string& filename) {
    std::vector<std::string> symbols;
    char path[MAX_PATH] = { 0 };

    HMODULE hModule = GetModuleHandleA("Valkyrie.dll");
    if (hModule == NULL) {
        LogWS("[WS] CRITICAL: GetModuleHandleA for Valkyrie.dll failed.");
        return symbols;
    }
    GetModuleFileNameA(hModule, path, MAX_PATH);

    std::string dirPath(path);
    size_t last_slash = dirPath.find_last_of("\\/");
    if (std::string::npos != last_slash) {
        dirPath = dirPath.substr(0, last_slash);
    }

    std::string fullPath = dirPath + "\\" + filename;
    LogWS("[WS] Attempting to load symbols from: " + fullPath );

    std::ifstream file(fullPath);
    std::string line;
    if (!file.is_open()) {
        LogWS("[WS] ERROR: Could not open symbols file: " + fullPath );
        return symbols;
    }
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty()) {
            symbols.push_back(line);
        }
    }
    LogWS("[WS] Loaded " + std::to_string(symbols.size()) + " symbols.");
    return symbols;
}
