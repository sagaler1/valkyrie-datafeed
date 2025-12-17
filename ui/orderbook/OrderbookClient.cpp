#include "OrderbookClient.h"

// 1. Core & Network Includes
#include "config.h"
#include "plugin.h"       // WM_USER_STREAMING_UPDATE
#include "api_client.h"   // WinHttpGetData
#include "../../core/SessionContext.h" // Global Key

// 2. JSON Parser (Nlohmann)
#include "../../core/json.hpp"
using json = nlohmann::json;

// 3. Protobuf Includes
#include "handshake.pb.h"
#include "ping.pb.h"
#include "orderbook_req.pb.h"
#include "orderbook_resp.pb.h"
#include "feed.pb.h"

#include "pb_encode.h"
#include "pb_decode.h"

#include <sstream>
#include <chrono>

// Helper Split String
std::vector<std::string> ob_split(const std::string& str, char delimiter) {
  std::vector<std::string> tokens;
  std::string token;
  std::istringstream tokenStream(str);
  while (std::getline(tokenStream, token, delimiter)) {
    tokens.push_back(token);
  }
  return tokens;
}

// =========================================================
// LIFECYCLE
// =========================================================

OrderbookClient::OrderbookClient() 
  : m_run(false), m_isConnected(false), m_hOrderbookWnd(NULL) 
{}

OrderbookClient::~OrderbookClient() { 
  stop(); 
}

void OrderbookClient::setWindowHandle(HWND hWnd) { 
  m_hOrderbookWnd = hWnd; 
}

// =========================================================
// STANDBY CONNECTION LOGIC
// =========================================================

void OrderbookClient::connectStandby() {
  if (m_run) return; // Sudah jalan
  m_run = true;
  
  // Jalankan thread maintenance socket
  m_workerThread = std::thread(&OrderbookClient::connectionLoop, this);
}

void OrderbookClient::stop() {
  m_run = false;
  
  if (m_ws) {
    m_ws->stop();
    m_ws.reset();
  }
  
  if (m_workerThread.joinable()) m_workerThread.join();
  if (m_pingThread.joinable()) m_pingThread.join();
}

void OrderbookClient::connectionLoop() {
  // 1. Ambil URL Socket
  std::string socketUrl = Config::getInstance().getSocketUrl();
    
  // Setup WebSocket
  m_ws = std::make_unique<ix::WebSocket>();
  m_ws->setUrl(socketUrl);

    // Callback
  m_ws->setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) {
    if (msg->type == ix::WebSocketMessageType::Open) {
      m_isConnected = true;
      OutputDebugStringA("[Orderbook] Socket Connected. Sending Handshake...");

      // 2. Ambil Key & User dari Global Context
      std::string key = SessionContext::instance().getWsKey();
      std::string user = SessionContext::instance().getUsername();

      // Kalau key belum ada (karena fetch di Init belum kelar), tunggu bentar
      int retry = 0;
      while(key.empty() && retry < 10) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        key = SessionContext::instance().getWsKey();
        user = SessionContext::instance().getUsername();
        retry++;
      }

      if (!key.empty()) {
        // Kirim Handshake
        m_ws->sendBinary(buildHandshake(user, key));
          
        // Start Ping Loop
        if (m_pingThread.joinable()) m_pingThread.join();
        m_pingThread = std::thread(&OrderbookClient::pingLoop, this);
      } else {
        OutputDebugStringA("[Orderbook] ERROR: No Global Key found for Handshake!");
      }

    } else if ( msg->type == ix::WebSocketMessageType::Message) {
      handleMessage(msg->str);
    } else if ( msg->type == ix::WebSocketMessageType::Close || 
                msg->type == ix::WebSocketMessageType::Error) {
      m_isConnected = false;
      OutputDebugStringA("[Orderbook] Socket Disconnected.");
    }
  });

    m_ws->start();

    // Reconnect Loop (Persistent)
    while (m_run) {
      if (!m_isConnected) {
        OutputDebugStringA("[Orderbook] Reconnecting...");
        m_ws->start();
        std::this_thread::sleep_for(std::chrono::seconds(5));
      }
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

// =========================================================
// ON DEMAND LOGIC (TRIGGERED BY USER)
// =========================================================

void OrderbookClient::requestTicker(const std::string& ticker) {
  if (ticker.empty()) return;

  // 1. Clear Data Lama & Set Loading
  clearData();
  if (m_hOrderbookWnd) PostMessage(m_hOrderbookWnd, WM_USER_STREAMING_UPDATE, 1, 0);

  // 2. Fetch HTTP Snapshot & Validasi (Synchronous biar UI dapet feedback langsung)
  bool isValid = fetchAndValidateSnapshot(ticker);

  if (isValid) {
    // Update Active Ticker
    {
      std::lock_guard<std::mutex> lock(m_dataMutex);
      m_activeTicker = ticker;
    }

    // Stream
    if (m_hOrderbookWnd)
      PostMessage(m_hOrderbookWnd, WM_USER_STREAMING_UPDATE, 0, 0);

    // 3. Subscribe ke Socket (Jika socket hidup)
    if (m_isConnected) {
      sendSubscription(ticker);
    } else {
      OutputDebugStringA("[Orderbook] Cannot subscribe. Socket not connected.");
    }
  } else {
    // Kalau tidak valid / bukan saham, biarkan kosong atau beri notif error
    OutputDebugStringA(("[Orderbook] Validation failed for: " + ticker).c_str());
    if (m_hOrderbookWnd)
      PostMessage(m_hOrderbookWnd, WM_USER_STREAMING_UPDATE, 2, 0);
  }
}

bool OrderbookClient::fetchAndValidateSnapshot(const std::string& ticker) {
  std::string host = Config::getInstance().getHost();
  std::string url = host + "/api/amibroker/orderbook?symbol=" + ticker;

  // Fetch API
  std::string resp = WinHttpGetData(url);
  if (resp.empty()) return false;

  // Parse JSON
  return parseSnapshotJson(resp, ticker);
}

void OrderbookClient::sendSubscription(const std::string& ticker) {
  std::string key = SessionContext::instance().getWsKey();
  std::string user = SessionContext::instance().getUsername();

  if (key.empty() || user.empty()) return;

  std::string payload = buildSubscribe(user, key, ticker);
  if (!payload.empty()) {
    m_ws->sendBinary(payload);
    OutputDebugStringA(("[Orderbook] Subscribed to: " + ticker).c_str());
  }
}

void OrderbookClient::clearData() {
  std::lock_guard<std::mutex> lock(m_dataMutex);
  m_data.clear();
}

OrderbookSnapshot OrderbookClient::getData() {
  std::lock_guard<std::mutex> lock(m_dataMutex);
  return m_data;
}

// =========================================================
// PARSING & HANDLING
// =========================================================

bool OrderbookClient::parseSnapshotJson(const std::string& jsonResponse, const std::string& symbol) {
  try {
    auto j = json::parse(jsonResponse);
    if (!j.contains("data") || j["data"].is_null()) return false;
    
    auto& data = j["data"];

    // Validasi Company Type
    std::string type = data.value("company_type", "");
    if (type != "Saham") return false; 

    // Lock Mutex sebelum update
    std::lock_guard<std::mutex> lock(m_dataMutex);
    m_data.clear();
    m_data.symbol = symbol;
    m_data.company_type = type;

    // Parse Header
    if (data.contains("average")) {
      if (data["average"].is_string()) 
        m_data.last_price = std::stod(data["average"].get<std::string>());
      else 
        m_data.last_price = data.value("average", 0.0);
    }

    // Helper lambda
    auto parseList = [](const json& arr, std::vector<OrderLevel>& vec) {
      if (!arr.is_array()) return;
      for (const auto& item : arr) {
        OrderLevel lvl;
        if (item.contains("price")) {
          if (item["price"].is_string()) lvl.price = std::stol(item["price"].get<std::string>());
          else lvl.price = item["price"].get<long>();
        }
        if (item.contains("que_num")) {
          if (item["que_num"].is_string()) lvl.queue = std::stol(item["que_num"].get<std::string>());
          else lvl.queue = item["que_num"].get<long>();
        }
        if (item.contains("volume")) {
          if (item["volume"].is_string()) lvl.volume = std::stol(item["volume"].get<std::string>());
          else lvl.volume = item["volume"].get<long>();
        }
        vec.push_back(lvl);
      }
    };

    if (data.contains("bid")) parseList(data["bid"], m_data.bids);
    if (data.contains("offer")) parseList(data["offer"], m_data.offers);

    if (data.contains("name")) m_data.company_name = data.value("name", "");

    if (data.contains("previous")) m_data.prev_close = data.value("previous", 0.0);
    if (data.contains("change")) m_data.change = data.value("change", 0.0);
    if (data.contains("percentage_change")) m_data.percent = data.value("percentage_change", 0.0);
    if (data.contains("open")) m_data.open = data.value("open", 0.0);
    if (data.contains("high")) m_data.high = data.value("high", 0.0);
    if (data.contains("low")) m_data.low = data.value("low", 0.0);
    if (data.contains("volume")) m_data.volume = data.value("volume", 0.0);
    if (data.contains("value")) m_data.value = data.value("value", 0.0);
    if (data.contains("frequency")) m_data.frequency = data.value("frequency", 0.0);
    if (data.contains("volume")) {
      if (data["volume"].is_string()) m_data.volume = std::stod(data["volume"].get<std::string>());
      else m_data.volume = data.value("volume", 0.0);
    }

    return true;
  } catch (...) {
    return false;
  }
}

void OrderbookClient::handleMessage(const std::string& msg) {
  if (msg.empty()) return;
  const pb_byte_t* data = (const pb_byte_t*)msg.data();
  size_t len = msg.size();

  // 1. Decode Orderbook Stream
  {
    StockOrderbook obMsg = StockOrderbook_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(data, len);
    if (pb_decode(&stream, StockOrderbook_fields, &obMsg) && obMsg.has_data) {
      std::string body(obMsg.data.body);
      parseStreamBody(body);
      if (m_hOrderbookWnd) PostMessage(m_hOrderbookWnd, WM_USER_STREAMING_UPDATE, 0, 0);
      return;
    }
  }

  // 2. Decode LiveQuote (Header update)
  {
    StockFeed feedMsg = StockFeed_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(data, len);
    if (pb_decode(&stream, StockFeed_fields, &feedMsg) && feedMsg.has_stock_data) {
      std::lock_guard<std::mutex> lock(m_dataMutex);
      // Pastikan update hanya untuk symbol yang sedang aktif
      if (m_data.symbol == feedMsg.stock_data.symbol) {
        m_data.last_price = feedMsg.stock_data.lastprice;
        m_data.prev_close = feedMsg.stock_data.previous;
        m_data.open = feedMsg.stock_data.open;
        m_data.high = feedMsg.stock_data.high;
        m_data.low = feedMsg.stock_data.low;
        m_data.volume = feedMsg.stock_data.volume;
        m_data.value = feedMsg.stock_data.value;
        m_data.frequency = feedMsg.stock_data.frequency;
        if(feedMsg.stock_data.has_change) {
          m_data.change = feedMsg.stock_data.change.value;
          m_data.percent = feedMsg.stock_data.change.percentage;
        }
      }
      if (m_hOrderbookWnd) PostMessage(m_hOrderbookWnd, WM_USER_STREAMING_UPDATE, 0, 0);
      return;
    }
  }
}

void OrderbookClient::parseStreamBody(const std::string& body) {
  // Format: #O|BBCA|BID|...
  auto tokens = ob_split(body, '|');
  if (tokens.size() < 3) return;
  if (tokens[0] != "#O") return;
  
  std::string symbol = tokens[1];
  std::string type = tokens[2]; // BID or OFFER

  {
    std::lock_guard<std::mutex> lock(m_dataMutex);
    // Ignore stream dari ticker lain (sisa stream lama sebelum server switch)
    if (symbol != m_data.symbol) return;
  }

  std::vector<OrderLevel> tempVec;
  for (size_t i = 3; i < tokens.size(); i++) {
    if (tokens[i].find('&') != std::string::npos || tokens[i].empty()) continue;
    parseRow(tokens[i], tempVec);
  }

  {
    std::lock_guard<std::mutex> lock(m_dataMutex);
    if (type == "BID") m_data.bids = tempVec;
    else if (type == "OFFER") m_data.offers = tempVec;
  }
}

void OrderbookClient::parseRow(const std::string& rawRow, std::vector<OrderLevel>& targetVec) {
  auto cols = ob_split(rawRow, ';');
  if (cols.size() < 3) return;
  try {
    OrderLevel lvl;
    lvl.price = std::stol(cols[0]);
    lvl.queue = std::stol(cols[1]);
    lvl.volume = std::stol(cols[2]);
    targetVec.push_back(lvl);
  } catch (...) {}
}

void OrderbookClient::pingLoop() {
  while (m_run && m_isConnected) {
    std::this_thread::sleep_for(std::chrono::seconds(5));
    std::string payload = buildPing();
    if(!payload.empty() && m_ws) m_ws->sendBinary(payload);
  }
}

// =========================================================
// PROTO BUILDERS
// =========================================================

std::string OrderbookClient::buildHandshake(const std::string& userId, const std::string& key) {
  Handshake hs = Handshake_init_default;
  strncpy_s(hs.userId, sizeof(hs.userId), userId.c_str(), _TRUNCATE);
  strncpy_s(hs.key, sizeof(hs.key), key.c_str(), _TRUNCATE);
  uint8_t buffer[256];
  pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));
  if (pb_encode(&stream, Handshake_fields, &hs)) return std::string((char*)buffer, stream.bytes_written);
  return "";
}

std::string OrderbookClient::buildSubscribe(const std::string& userId, const std::string& key, const std::string& symbol) {
  OrderbookSubscribe req = OrderbookSubscribe_init_default;
  strncpy_s(req.userId, sizeof(req.userId), userId.c_str(), _TRUNCATE);
  strncpy_s(req.key, sizeof(req.key), key.c_str(), _TRUNCATE);
  req.has_subs = true;
  req.subs.orderbook_count = 1;
  strncpy_s(req.subs.orderbook[0], sizeof(req.subs.orderbook[0]), symbol.c_str(), _TRUNCATE);
  req.subs.livequote_count = 1;
  strncpy_s(req.subs.livequote[0], sizeof(req.subs.livequote[0]), symbol.c_str(), _TRUNCATE);
  uint8_t buffer[512];
  pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));
  if (pb_encode(&stream, OrderbookSubscribe_fields, &req)) return std::string((char*)buffer, stream.bytes_written);
  return "";
}

std::string OrderbookClient::buildPing() {
  WSWrapper wrapper = WSWrapper_init_default;
  wrapper.has_ping = true;
  strncpy_s(wrapper.ping.message, sizeof(wrapper.ping.message), "PING", _TRUNCATE);
  uint8_t buffer[64];
  pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));
  if (pb_encode(&stream, WSWrapper_fields, &wrapper)) return std::string((char*)buffer, stream.bytes_written);
  return "";
}