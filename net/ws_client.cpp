#include <windows.h>
#include <chrono>
#include "ws_client.h"
#include "data_store.h"
#include "plugin.h"
#include "pluginstate.h"
#include "dotenv.h"
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

// ---- INCLUDE UNTUK OLE / COM ----
#include <vector>
#include <atlbase.h>
#include <comdef.h>

void LogWS(const std::string& msg) {
  SYSTEMTIME t;
  GetLocalTime(&t);
  char buf[64];
  sprintf_s(buf, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
  OutputDebugStringA((std::string(buf) + msg + "\n").c_str());
}

// ---- OLE Helper ----
// (Dicopy dari ConfigDlg.cpp untuk konsistensi)

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

// ---- Dapatkan symbols dari DB ----
std::vector<std::string> WsClient::getDBSymbols() {
  std::vector<std::string> symbols;
  CoInitialize(NULL);
  LogWS("[OLE] CoInitialize OK. Fetching DB symbols...");

  IDispatch* pApp = GetBrokerApplication();
  if (!pApp) {
    LogWS("[OLE] ERROR: Cannot create Broker.Application instance.");
    CoUninitialize();
    return symbols;
  }

  IDispatch* pStocks = GetStocksCollection(pApp);
  if (!pStocks) {
    LogWS("[OLE] ERROR: Cannot get Stocks collection.");
    pApp->Release();
    CoUninitialize();
    return symbols;
  }

  // 1. Get Stocks.Count
  OLECHAR* propCount = L"Count";
  DISPID dispidCount;
  long count = 0;
  if (SUCCEEDED(pStocks->GetIDsOfNames(IID_NULL, &propCount, 1, LOCALE_USER_DEFAULT, &dispidCount))) {
    VARIANT result;
    VariantInit(&result);
    DISPPARAMS noArgs = { nullptr, nullptr, 0, 0 };
    if (SUCCEEDED(pStocks->Invoke(dispidCount, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_PROPERTYGET, &noArgs, &result, NULL, NULL))) {
      count = result.lVal;
    }
  }

  if (count == 0) {
    LogWS("[OLE] Stocks collection has 0 items.");
    pStocks->Release();
    pApp->Release();
    CoUninitialize();
    return symbols;
  }

  // 2. Loop dan Get Stocks.Item(i).Ticker
  symbols.reserve(count);
  for (long i = 0; i < count; i++) {
    IDispatch* pStock = nullptr;
    
    // --- Get Stocks.Item(i) ---
    OLECHAR* methodItem = L"Item";
    DISPID dispidItem;
    if (FAILED(pStocks->GetIDsOfNames(IID_NULL, &methodItem, 1, LOCALE_USER_DEFAULT, &dispidItem))) continue;

    VARIANTARG arg;
    VariantInit(&arg);
    arg.vt = VT_I4; // Tipe argumen adalah Long (32-bit int)
    arg.lVal = i;
    DISPPARAMS params = { &arg, nullptr, 1, 0 };
    VARIANT resultStock;
    VariantInit(&resultStock);
    
    // Panggil 'Item' sebagai PROPERTYGET (bukan METHOD)
    if (SUCCEEDED(pStocks->Invoke(dispidItem, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_PROPERTYGET, &params, &resultStock, NULL, NULL)) && resultStock.vt == VT_DISPATCH) {
      pStock = resultStock.pdispVal;
    } else {
      continue; // Gagal dapet stock item
    }

    // --- Get Stock.Ticker ---
    OLECHAR* propTicker = L"Ticker";
    DISPID dispidTicker;
    if (SUCCEEDED(pStock->GetIDsOfNames(IID_NULL, &propTicker, 1, LOCALE_USER_DEFAULT, &dispidTicker))) {
      VARIANT resultTicker;
      VariantInit(&resultTicker);
      DISPPARAMS noArgs = { nullptr, nullptr, 0, 0 };
      if (SUCCEEDED(pStock->Invoke(dispidTicker, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_PROPERTYGET, &noArgs, &resultTicker, NULL, NULL)) && resultTicker.vt == VT_BSTR) {
          
        // Convert BSTR (wide string) to std::string (narrow)
        _bstr_t b(resultTicker.bstrVal, false); // 'false' = jangan copy, cuma wrap
        std::string ticker = (const char*)b;
        if (!ticker.empty()) {
          symbols.push_back(ticker);
        }
      }
      VariantClear(&resultTicker);
    }
    
    pStock->Release();
  }

  LogWS("[OLE] Finished. Found " + std::to_string(symbols.size()) + " symbols in DB.");
  pStocks->Release();
  pApp->Release();
  CoUninitialize();
  return symbols;
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

  LogWS(std::string("[WS] Worker thread started. Fetching wskey from: ") + m_wsKeyUrl);
  std::string wskey = fetchWsKey(m_wsKeyUrl);
  if (wskey.empty()) {
    LogWS(std::string("[WS] CRITICAL: Failed to fetch wskey. Stopping thread"));
    if (m_pStatus) *m_pStatus = 2;    // STATE_DISCONNECTED
    m_run = false;
    return;
  }
  LogWS("[WS] wskey successfully fetched.");

  // auto symbols = loadSymbols();        // Load symbols dari file
  m_subscribedSymbols = getDBSymbols();   // Load symbols dari DB

  std::atomic<bool> isSubscribed{false};

  // ---- THROTTLING ----
  auto last_ui_update = std::chrono::steady_clock::now();
  const auto ui_update_interval = std::chrono::milliseconds(100); // 4x per detik

  m_ws->setOnMessageCallback([&](const ix::WebSocketMessagePtr& msg) {
    if (msg->type == ix::WebSocketMessageType::Open) {
      LogWS("[WS] ==> EVENT: Open. Connection established.");
      m_isConnected = true;
      if (m_pStatus) *m_pStatus = STATE_CONNECTED;
      m_ws->sendBinary(buildHandshakeBinary(m_userId, wskey));
      LogWS("[WS] Handshake sent.");
      m_ws->sendBinary(buildPingBinary());
      LogWS("[WS] Sending first ping..");

      if (m_pingThread.joinable()) m_pingThread.join();
      m_pingThread = std::thread(&WsClient::pingLoop, this);
    } else if (msg->type == ix::WebSocketMessageType::Message) {

        // LogWS(std::string("[WS] ==> EVENT: Message. Size: " + std::to_string(msg->str.size()) + " bytes"));
        if (!isSubscribed) {
          // ---- nanopb parser for PongReceive ----
          // 1. Buat input stream dari data biner
          pb_istream_t stream = pb_istream_from_buffer(
            reinterpret_cast<const pb_byte_t*>(msg->str.data()),
            msg->str.size()
          );

          // 2. Inisialisasi struct PongReceive
          PongReceive pong = PongReceive_init_zero;

          // 3. Decode pesan
          bool status = pb_decode(&stream, PongReceive_fields, &pong);

          // 4. Cek apakah decode berhasil dan field 'pong' ada isinya
          if (status && pong.has_pong) {
            LogWS(std::string("[WS] Handshake confirmed. Subscribing to " + std::to_string(m_subscribedSymbols.size()) + " symbols"));
            // if (!symbols.empty()) m_ws->sendBinary(buildSubscribeBinary(m_userId, wskey, symbols));
            if (!m_subscribedSymbols.empty()) {
                // Kirim SEMUA simbol sekaligus
                m_ws->sendBinary(buildSubscribeBinary(m_userId, wskey, m_subscribedSymbols));
            }
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

            // 3. Decode pesan
            bool status = pb_decode(&stream, StockFeed_fields, &feed);

            // 4. Proses jika decode berhasil DAN sub-pesan stock_data ada isinya
            if (status && feed.has_stock_data) {
                gDataStore.updateLiveQuote(feed); // Kirim struct nanopb ke DataStore
                // if (m_hAmiBrokerWnd) PostMessage(m_hAmiBrokerWnd, WM_USER_STREAMING_UPDATE, 0, 0);
                auto now = std::chrono::steady_clock::now();
                if (now - last_ui_update > ui_update_interval) 
                {
                    if (m_hAmiBrokerWnd) {
                        PostMessage(m_hAmiBrokerWnd, WM_USER_STREAMING_UPDATE, 0, 0);
                    }
                    last_ui_update = now; // Reset timer
                }
            } else {
                if (msg->str.size() < 12) {
                    return;     // heartbeat / pong
                }
                std::string payload(msg->str.begin(), msg->str.end());
                if(payload.find("none") != std::string::npos) {
                    return;
                }
                LogWS(std::string("[WS] ERROR: Decoding failed for StockFeed: ") + PB_GET_ERROR(&stream));
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

  // ---- BUFFER STATIS (Load File txt) ----
  // Buffer yang cukup besar. 20 simbol * 16 char = 320, ditambah userId, key, dan overhead.
  // 1024 bytes sangat aman.
  // uint8_t buffer[1024];
  // pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));

  // ---- BUFFER DINAMIS / BESAR ----
  // Ukuran buffer statis tidak akan cukup untuk 1000 simbol
  // (1000 simbol * 6 char avg) + overhead = 6KB+
  // Alokasikan 32KB biar aman
  std::vector<uint8_t> buffer(32768); // 32KB
  pb_ostream_t stream = pb_ostream_from_buffer(buffer.data(), buffer.size());

  // 2. Isi field-field simpel
  strncpy_s(sub.userId, sizeof(sub.userId), userId.c_str(), _TRUNCATE);
  strncpy_s(sub.key, sizeof(sub.key), key.c_str(), _TRUNCATE);

  // 3. PENTING: Set 'has_subs' untuk nested message
  sub.has_subs = true;

  // 4. Isi 'repeated string' (livequote) dengan loop
  sub.subs.livequote_count = 0; // Mulai counter dari 0
  for (const auto& sym : symbols) {
    // Cek agar tidak melebihi max_count yang didefinisikan di .options
    // if (sub.subs.livequote_count >= (sizeof(sub.subs.livequote) / sizeof(sub.subs.livequote[0]))) {
    //  LogWS("[WS] WARNING: Symbol count exceeds max_count(20) for subscription. Truncating list.");
    //  break; // Stop menambahkan jika sudah penuh
    // }

    // Cek agar tidak melebihi max_count (PASTIKAN .options UDAH DIUPDATE)
    size_t max_count_arr = (sizeof(sub.subs.livequote) / sizeof(sub.subs.livequote[0]));
    if (sub.subs.livequote_count >= max_count_arr) {
      LogWS("[WS] WARNING: Symbol count exceeds max_count(" + std::to_string(max_count_arr) + ") Truncating list.");
      break; 
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
  std::string out(reinterpret_cast<char*>(buffer.data()), stream.bytes_written);
  return out;
  // ---- end nanopb parser ----
}
