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
#include "api_client.h"         // WinHttpGetData declaration

#include "pb_common.h"
#include "pb_decode.h"
#include "pb_encode.h"
#include "handshake.pb.h"
#include "ping.pb.h"
#include "subscribe.pb.h"
#include "feed.pb.h"
#include "pong.pb.h"
#include "config.h"

void LogWS(const std::string& msg) {
    OutputDebugStringA((msg + "\n").c_str());
}

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

    LogWS(std::string("[WS] Worker thread started. Fetching wskey from") + m_wsKeyUrl);
    std::string wskey = fetchWsKey(m_wsKeyUrl);
    if (wskey.empty()) {
        LogWS(std::string("[WS] CRITICAL: Failed to fetch wskey. Stopping thread"));
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
                // ---- nanopb parser for PongReceive ----
                // 1. Buat input stream dari data biner
                pb_istream_t stream = pb_istream_from_buffer(
                    reinterpret_cast<const pb_byte_t*>(msg->str.data()),
                    msg->str.size()
                );

                // 2. Inisialisasi struct PongReceive
                PongReceive pong = PongReceive_init_zero;

                // 3. Decode pesannya
                bool status = pb_decode(&stream, PongReceive_fields, &pong);

                // 4. Cek apakah decode berhasil dan field 'pong' ada isinya
                if (status && pong.has_pong) {
                    LogWS(std::string("[WS] Handshake confirmed. Subscribing to " + std::to_string(symbols.size()) + " symbols"));
                    if (!symbols.empty()) m_ws->sendBinary(buildSubscribeBinary(m_userId, wskey, symbols));
                    isSubscribed = true;
                } else {
                    LogWS(std::string("[WS] WARNING: Message before handshake confirmed."));
                }
                // ---- end nanopb parser ----
            } else {
               // ---- nanopb parser for StockFeed ----
                // 1. Buat input stream dari data biner websocket
                pb_istream_t stream = pb_istream_from_buffer(
                    reinterpret_cast<const pb_byte_t*>(msg->str.data()), 
                    msg->str.size()
                );

                // 2. Inisialisasi struct StockFeed
                StockFeed feed = StockFeed_init_zero;

                // 3. Decode pesannya
                bool status = pb_decode(&stream, StockFeed_fields, &feed);

                // 4. Proses jika decode berhasil DAN sub-pesan stock_data ada isinya
                if (status && feed.has_stock_data) {
                    gDataStore.updateLiveQuote(feed); // Kirim struct nanopb ke DataStore
                    if (m_hAmiBrokerWnd) PostMessage(m_hAmiBrokerWnd, WM_USER_STREAMING_UPDATE, 0, 0);
                } else {
                    // Opsional: Log jika ada error decoding
                    LogWS(std::string("[WS] ERROR: Nanopb decoding failed for StockFeed: ") + PB_GET_ERROR(&stream));
                }
                // ---- end nanopb parser ----
            }
        } else if (msg->type == ix::WebSocketMessageType::Close) {
            LogWS(std::string("[WS] ==> EVENT: Close. Code: " + std::to_string(msg->closeInfo.code) + " Reason: " + msg->closeInfo.reason));
            
            m_isConnected = false;
            if (m_pStatus) *m_pStatus = STATE_DISCONNECTED;

        } else if (msg->type == ix::WebSocketMessageType::Error) {
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
    std::string data = WinHttpGetData(url);
    if (data.empty()) return "";

    // cari "key":"xxxxx" manual
    size_t pos = data.find("\"key\":\"");
    if (pos == std::string::npos) return "";

    pos += 7;
    size_t end = data.find("\"", pos);
    if (end == std::string::npos) return "";

    return data.substr(pos, end - pos);
}

std::string WsClient::buildHandshakeBinary(const std::string& userId, const std::string& key) {     //ini line 186
    // ---- nanopb parser
    // 1. Buat instance struct Handshake
    Handshake hs = Handshake_init_zero;

    // 2. Siapkan buffer untuk menampung hasil encode
    // Ukuran buffer bisa diperkirakan. userId(32) + key(128) + overhead protobuf (sekitar 10 bytes)
    uint8_t buffer[200];

    // 3. Buat output stream dari buffer
    pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));

    // 4. Salin data dari std::string ke field struct
    // Pakai strncpy biar aman, sesuai max_size di .options
    strncpy_s(hs.userId, userId.c_str(), sizeof(hs.userId) - 1);
    hs.userId[sizeof(hs.userId) - 1] = '\0'; // Pastikan null-terminated

    strncpy_s(hs.key, key.c_str(), sizeof(hs.key) - 1);
    hs.key[sizeof(hs.key) - 1] = '\0'; // Pastikan null-terminated

    // 5. Proses encoding
    bool status = pb_encode(&stream, Handshake_fields, &hs);
    if (!status) {
        LogWS("[WS] ERROR: Nanopb encoding failed for Handshake.");
        return ""; // Return string kosong jika gagal
    }

    // 6. Buat std::string dari buffer yang sudah diisi
    // Ambil ukurannya dari stream.bytes_written
    std::string out(reinterpret_cast<char*>(buffer), stream.bytes_written);

    return out;
    // ---- end nanopb parser

}

std::string WsClient::buildPingBinary() {
    // ---- nanopb parser
    // 1. Inisialisasi struct pembungkus (wrapper)
    WSWrapper wrapper = WSWrapper_init_zero;
    
    // Buffer untuk hasil encode. Ukurannya kecil saja sudah cukup.
    uint8_t buffer[32];
    pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));
    
    // 2. Isi data untuk sub-pesan Ping
    // Kita set pesannya "ping", sesuai max_size:16
    strncpy_s(wrapper.ping.message, sizeof(wrapper.ping.message), "ping", _TRUNCATE);
    
    // 3. PENTING: Beri tahu encoder bahwa sub-pesan 'ping' ini ada isinya
    wrapper.has_ping = true;
    
    // 4. Encode struct pembungkusnya
    bool status = pb_encode(&stream, WSWrapper_fields, &wrapper);
    if (!status) {
        LogWS("[WS] ERROR: Nanopb encoding failed for Ping.");
        return "";
    }
    
    // 5. Buat std::string dari buffer
    std::string out(reinterpret_cast<char*>(buffer), stream.bytes_written);
    return out;
    // ---- end nanopb parser
}

std::string WsClient::buildSubscribeBinary(const std::string& userId, const std::string& key, const std::vector<std::string>& symbols) {
    // ---- nanopb parser ----
    // 1. Inisialisasi struct utama
    SymbolSubscribe sub = SymbolSubscribe_init_zero;

    // Buffer yang cukup besar. 20 simbol * 16 char = 320, ditambah userId, key, dan overhead.
    // 1024 bytes sangat aman.
    uint8_t buffer[1024];
    pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));

    // 2. Isi field-field simpel
    strncpy_s(sub.userId, sizeof(sub.userId), userId.c_str(), _TRUNCATE);
    strncpy_s(sub.key, sizeof(sub.key), key.c_str(), _TRUNCATE);

    // 3. PENTING: Set 'has_subs' untuk nested message
    sub.has_subs = true;

    // 4. Isi 'repeated string' (livequote) dengan loop
    sub.subs.livequote_count = 0; // Mulai counter dari 0
    for (const auto& sym : symbols) {
        // Cek agar tidak melebihi max_count yang didefinisikan di .options
        if (sub.subs.livequote_count >= (sizeof(sub.subs.livequote) / sizeof(sub.subs.livequote[0]))) {
            LogWS("[WS] WARNING: Symbol count exceeds max_count(20) for subscription. Truncating list.");
            break; // Stop menambahkan jika sudah penuh
        }

        // Salin simbol ke dalam array 2D
        char* dest = sub.subs.livequote[sub.subs.livequote_count];
        strncpy_s(dest, sizeof(sub.subs.livequote[0]), sym.c_str(), _TRUNCATE);

        // Naikkan counter
        sub.subs.livequote_count++;
    }

    // 5. Encode struct utamanya
    bool status = pb_encode(&stream, SymbolSubscribe_fields, &sub);
    if (!status) {
        LogWS("[WS] ERROR: Nanopb encoding failed for SymbolSubscribe.");
        return "";
    }

    // 6. Buat std::string dari buffer
    std::string out(reinterpret_cast<char*>(buffer), stream.bytes_written);
    return out;
    // ---- end nanopb parser ----
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
