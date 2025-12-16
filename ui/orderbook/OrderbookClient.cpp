#include "OrderbookClient.h"

// 1. Core & Network Includes
#include "config.h"
#include "plugin.h"       // WM_USER_STREAMING_UPDATE
#include "api_client.h"   // WinHttpGetData

// 2. JSON Parser (Nlohmann)
#include "../../core/json.hpp"
using json = nlohmann::json;

// 3. Protobuf Includes (Generated Nanopb)
#include "handshake.pb.h"
#include "ping.pb.h"
#include "orderbook_req.pb.h"
#include "orderbook_resp.pb.h"
#include "feed.pb.h"

#include "pb_encode.h"
#include "pb_decode.h"

#include <sstream>
#include <chrono>

// ---- ORDERBOOK SNAPSHOT EXAMPLE
// {"data":{"average":336,"bid":[{"price":"332","que_num":"195","volume":"17717900","change_percentage":""},{"price":"330","que_num":"930","volume":"59499700","change_percentage":""},{"price":"328","que_num":"265","volume":"73190700","change_percentage":""},{"price":"326","que_num":"283","volume":"54836500","change_percentage":""},{"price":"324","que_num":"212","volume":"23970900","change_percentage":""},{"price":"322","que_num":"167","volume":"17789700","change_percentage":""},{"price":"320","que_num":"579","volume":"46597200","change_percentage":""},{"price":"318","que_num":"138","volume":"41080200","change_percentage":""},{"price":"316","que_num":"109","volume":"32248700","change_percentage":""},{"price":"314","que_num":"131","volume":"11590600","change_percentage":""},{"price":"312","que_num":"59","volume":"3578400","change_percentage":""},{"price":"310","que_num":"239","volume":"37173500","change_percentage":""},{"price":"308","que_num":"56","volume":"34822800","change_percentage":""},{"price":"306","que_num":"64","volume":"51688000","change_percentage":""},{"price":"304","que_num":"57","volume":"27050000","change_percentage":""},{"price":"302","que_num":"74","volume":"6051000","change_percentage":""},{"price":"300","que_num":"433","volume":"72320400","change_percentage":""},{"price":"298","que_num":"69","volume":"2785400","change_percentage":""},{"price":"296","que_num":"54","volume":"31476800","change_percentage":""},{"price":"294","que_num":"193","volume":"42085700","change_percentage":""}],"change":-10,"close":334,"country":"ID","domestic":"100.00","down":"110","exchange":"IDX","fbuy":1116972892800,"fnet":-621754172400,"foreign":"0.00","frequency":11483,"fsell":1738727065200,"high":340,"id":"BUMI-0","lastprice":334,"low":332,"offer":[{"price":"334","que_num":"90","volume":"9269000","change_percentage":""},{"price":"336","que_num":"184","volume":"17134900","change_percentage":""},{"price":"338","que_num":"414","volume":"15706200","change_percentage":""},{"price":"340","que_num":"809","volume":"33544100","change_percentage":""},{"price":"342","que_num":"353","volume":"29091500","change_percentage":""},{"price":"344","que_num":"1740","volume":"37744600","change_percentage":""},{"price":"346","que_num":"247","volume":"17660500","change_percentage":""},{"price":"348","que_num":"194","volume":"15123100","change_percentage":""},{"price":"350","que_num":"335","volume":"27340600","change_percentage":""},{"price":"352","que_num":"150","volume":"37509500","change_percentage":""},{"price":"354","que_num":"166","volume":"40512900","change_percentage":""},{"price":"356","que_num":"149","volume":"19230900","change_percentage":""},{"price":"358","que_num":"141","volume":"15229600","change_percentage":""},{"price":"360","que_num":"354","volume":"51912700","change_percentage":""},{"price":"362","que_num":"159","volume":"38492800","change_percentage":""},{"price":"364","que_num":"195","volume":"42794300","change_percentage":""},{"price":"366","que_num":"163","volume":"11903800","change_percentage":""},{"price":"368","que_num":"223","volume":"15696100","change_percentage":""},{"price":"370","que_num":"529","volume":"88020300","change_percentage":""},{"price":"372","que_num":"260","volume":"42279500","change_percentage":""},{"price":"374","que_num":"258","volume":"46845800","change_percentage":""},{"price":"376","que_num":"235","volume":"20068300","change_percentage":""},{"price":"378","que_num":"256","volume":"22153100","change_percentage":""},{"price":"380","que_num":"753","volume":"40230200","change_percentage":""},{"price":"382","que_num":"177","volume":"10838100","change_percentage":""},{"price":"384","que_num":"192","volume":"18822000","change_percentage":""},{"price":"386","que_num":"212","volume":"21243700","change_percentage":""},{"price":"388","que_num":"220","volume":"18735700","change_percentage":""},{"price":"390","que_num":"555","volume":"42275500","change_percentage":""},{"price":"392","que_num":"157","volume":"15559000","change_percentage":""},{"price":"394","que_num":"178","volume":"10504100","change_percentage":""},{"price":"396","que_num":"248","volume":"41339800","change_percentage":""},{"price":"398","que_num":"438","volume":"70765600","change_percentage":""},{"price":"400","que_num":"2478","volume":"155963800","change_percentage":""},{"price":"402","que_num":"144","volume":"14434500","change_percentage":""},{"price":"404","que_num":"123","volume":"23591600","change_percentage":""},{"price":"406","que_num":"99","volume":"6154000","change_percentage":""},{"price":"408","que_num":"85","volume":"10877900","change_percentage":""},{"price":"410","que_num":"304","volume":"33786500","change_percentage":""},{"price":"412","que_num":"65","volume":"6930800","change_percentage":""},{"price":"414","que_num":"51","volume":"2442900","change_percentage":""},{"price":"416","que_num":"58","volume":"2393000","change_percentage":""},{"price":"418","que_num":"37","volume":"3959500","change_percentage":""},{"price":"420","que_num":"237","volume":"17078800","change_percentage":""},{"price":"422","que_num":"32","volume":"1975400","change_percentage":""},{"price":"424","que_num":"37","volume":"5165400","change_percentage":""},{"price":"426","que_num":"45","volume":"3183400","change_percentage":""},{"price":"428","que_num":"44","volume":"4074900","change_percentage":""},{"price":"430","que_num":"331","volume":"34422600","change_percentage":""}],"open":340,"percentage_change":-2.91,"previous":344,"status":"Active","symbol":"BUMI","symbol_2":"BUMI","symbol_3":"BUMI","tradable":true,"unchanged":"1118","up":"265","value":117391426400,"volume":349068700,"corp_action":{"active":false,"icon":"https://assets.stockbit.com/images/corp_action_event_icon.svg","text":"Perusahaan Memiliki Corporate Action"},"notation":[],"uma":false,"has_foreign_bs":false,"iepiev":{"symbol":"BUMI","status":"STATUS_OPEN","iep":{"raw":340,"formatted":"340"},"iev":{"raw":859683,"formatted":"859.68K"},"time_left_seconds":245,"best_bid_offer":null,"title":"Pre-Opening","iep_changes":{"price":{"raw":-4,"formatted":"-4"},"percentage":{"raw":-1.1627907,"formatted":"-1.16"}}},"market_data":[{"label":"All Market","frequency":{"raw":"11483","formatted":"11.5 K"},"volume":{"raw":"349068700","formatted":"349 M"},"value":{"raw":"117391426400","formatted":"117 B"}},{"label":"Regular","frequency":{"raw":"11483","formatted":"11.5 K"},"volume":{"raw":"349068700","formatted":"349 M"},"value":{"raw":"117391426400","formatted":"117 B"}},{"label":"Nego","frequency":{"raw":"0","formatted":"0"},"volume":{"raw":"0","formatted":"0"},"value":{"raw":"0","formatted":"0"}},{"label":"Cash","frequency":{"raw":"0","formatted":"0"},"volume":{"raw":"0","formatted":"0"},"value":{"raw":"0","formatted":"0"}}],"name":"Bumi Resources Tbk","icon_url":"https://assets.stockbit.com/logos/companies/BUMI.png","ara":{"value":"430","visible":true},"arb":{"value":"294","visible":true},"company_type":"Saham","total_bid_offer":{"bid":{"freq":"4,307","lot":"687,554,100"},"offer":{"freq":"14,904","lot":"1,312,012,800"}},"next_ara":{"value":"430","visible":true},"next_arb":{"value":"294","visible":true},"autoreject_time_left_in_sec":0,"auto_reject_estimation":[{"value":53900,"change_to_prev":{"value":8975,"percentage":19.98},"change_to_base":{"value":53556,"percentage":15568.6},"type":"AUTO_REJECT_TYPE_POSITIVE"},{"value":44925,"change_to_prev":{"value":7475,"percentage":19.96},"change_to_base":{"value":44581,"percentage":12959.59},"type":"AUTO_REJECT_TYPE_POSITIVE"},{"value":37450,"change_to_prev":{"value":6225,"percentage":19.94},"change_to_base":{"value":37106,"percentage":10786.63},"type":"AUTO_REJECT_TYPE_POSITIVE"},{"value":31225,"change_to_prev":{"value":5200,"percentage":19.98},"change_to_base":{"value":30881,"percentage":8977.03},"type":"AUTO_REJECT_TYPE_POSITIVE"},{"value":26025,"change_to_prev":{"value":4325,"percentage":19.93},"change_to_base":{"value":25681,"percentage":7465.41},"type":"AUTO_REJECT_TYPE_POSITIVE"},{"value":21700,"change_to_prev":{"value":3600,"percentage":19.89},"change_to_base":{"value":21356,"percentage":6208.14},"type":"AUTO_REJECT_TYPE_POSITIVE"},{"value":18100,"change_to_prev":{"value":3000,"percentage":19.87},"change_to_base":{"value":17756,"percentage":5161.63},"type":"AUTO_REJECT_TYPE_POSITIVE"},{"value":15100,"change_to_prev":{"value":2500,"percentage":19.84},"change_to_base":{"value":14756,"percentage":4289.53},"type":"AUTO_REJECT_TYPE_POSITIVE"},{"value":12600,"change_to_prev":{"value":2100,"percentage":20},"change_to_base":{"value":12256,"percentage":3562.79},"type":"AUTO_REJECT_TYPE_POSITIVE"},{"value":10500,"change_to_prev":{"value":1750,"percentage":20},"change_to_base":{"value":10156,"percentage":2952.33},"type":"AUTO_REJECT_TYPE_POSITIVE"},{"value":8750,"change_to_prev":{"value":1450,"percentage":19.86},"change_to_base":{"value":8406,"percentage":2443.6},"type":"AUTO_REJECT_TYPE_POSITIVE"},{"value":7300,"change_to_prev":{"value":1200,"percentage":19.67},"change_to_base":{"value":6956,"percentage":2022.09},"type":"AUTO_REJECT_TYPE_POSITIVE"},{"value":6100,"change_to_prev":{"value":1220,"percentage":25},"change_to_base":{"value":5756,"percentage":1673.26},"type":"AUTO_REJECT_TYPE_POSITIVE"},{"value":4880,"change_to_prev":{"value":970,"percentage":24.81},"change_to_base":{"value":4536,"percentage":1318.6},"type":"AUTO_REJECT_TYPE_POSITIVE"},{"value":3910,"change_to_prev":{"value":780,"percentage":24.92},"change_to_base":{"value":3566,"percentage":1036.63},"type":"AUTO_REJECT_TYPE_POSITIVE"},{"value":3130,"change_to_prev":{"value":620,"percentage":24.7},"change_to_base":{"value":2786,"percentage":809.88},"type":"AUTO_REJECT_TYPE_POSITIVE"},{"value":2510,"change_to_prev":{"value":500,"percentage":24.88},"change_to_base":{"value":2166,"percentage":629.65},"type":"AUTO_REJECT_TYPE_POSITIVE"},{"value":2010,"change_to_prev":{"value":400,"percentage":24.84},"change_to_base":{"value":1666,"percentage":484.3},"type":"AUTO_REJECT_TYPE_POSITIVE"},{"value":1610,"change_to_prev":{"value":320,"percentage":24.81},"change_to_base":{"value":1266,"percentage":368.02},"type":"AUTO_REJECT_TYPE_POSITIVE"},{"value":1290,"change_to_prev":{"value":255,"percentage":24.64},"change_to_base":{"value":946,"percentage":275},"type":"AUTO_REJECT_TYPE_POSITIVE"},{"value":1035,"change_to_prev":{"value":205,"percentage":24.7},"change_to_base":{"value":691,"percentage":200.87},"type":"AUTO_REJECT_TYPE_POSITIVE"},{"value":830,"change_to_prev":{"value":165,"percentage":24.81},"change_to_base":{"value":486,"percentage":141.28},"type":"AUTO_REJECT_TYPE_POSITIVE"},{"value":665,"change_to_prev":{"value":130,"percentage":24.3},"change_to_base":{"value":321,"percentage":93.31},"type":"AUTO_REJECT_TYPE_POSITIVE"},{"value":535,"change_to_prev":{"value":105,"percentage":24.42},"change_to_base":{"value":191,"percentage":55.52},"type":"AUTO_REJECT_TYPE_POSITIVE"},{"value":430,"change_to_prev":{"value":86,"percentage":25},"change_to_base":{"value":86,"percentage":25},"type":"AUTO_REJECT_TYPE_POSITIVE"},{"value":344,"change_to_prev":{"value":0,"percentage":0},"change_to_base":{"value":0,"percentage":0},"type":"AUTO_REJECT_TYPE_BASE"},{"value":294,"change_to_prev":{"value":-50,"percentage":-14.53},"change_to_base":{"value":-50,"percentage":-14.53},"type":"AUTO_REJECT_TYPE_NEGATIVE"},{"value":250,"change_to_prev":{"value":-44,"percentage":-14.97},"change_to_base":{"value":-94,"percentage":-27.33},"type":"AUTO_REJECT_TYPE_NEGATIVE"},{"value":214,"change_to_prev":{"value":-36,"percentage":-14.4},"change_to_base":{"value":-130,"percentage":-37.79},"type":"AUTO_REJECT_TYPE_NEGATIVE"},{"value":182,"change_to_prev":{"value":-32,"percentage":-14.95},"change_to_base":{"value":-162,"percentage":-47.09},"type":"AUTO_REJECT_TYPE_NEGATIVE"},{"value":155,"change_to_prev":{"value":-27,"percentage":-14.84},"change_to_base":{"value":-189,"percentage":-54.94},"type":"AUTO_REJECT_TYPE_NEGATIVE"},{"value":132,"change_to_prev":{"value":-23,"percentage":-14.84},"change_to_base":{"value":-212,"percentage":-61.63},"type":"AUTO_REJECT_TYPE_NEGATIVE"},{"value":113,"change_to_prev":{"value":-19,"percentage":-14.39},"change_to_base":{"value":-231,"percentage":-67.15},"type":"AUTO_REJECT_TYPE_NEGATIVE"},{"value":97,"change_to_prev":{"value":-16,"percentage":-14.16},"change_to_base":{"value":-247,"percentage":-71.8},"type":"AUTO_REJECT_TYPE_NEGATIVE"},{"value":83,"change_to_prev":{"value":-14,"percentage":-14.43},"change_to_base":{"value":-261,"percentage":-75.87},"type":"AUTO_REJECT_TYPE_NEGATIVE"},{"value":71,"change_to_prev":{"value":-12,"percentage":-14.46},"change_to_base":{"value":-273,"percentage":-79.36},"type":"AUTO_REJECT_TYPE_NEGATIVE"},{"value":61,"change_to_prev":{"value":-10,"percentage":-14.08},"change_to_base":{"value":-283,"percentage":-82.27},"type":"AUTO_REJECT_TYPE_NEGATIVE"},{"value":52,"change_to_prev":{"value":-9,"percentage":-14.75},"change_to_base":{"value":-292,"percentage":-84.88},"type":"AUTO_REJECT_TYPE_NEGATIVE"},{"value":50,"change_to_prev":{"value":-2,"percentage":-3.85},"change_to_base":{"value":-294,"percentage":-85.47},"type":"AUTO_REJECT_TYPE_NEGATIVE"}],"orderbook_active_feature_mobile":"ORDER_BOOK_FEATURE_IEP_IEV"},"message":"Successfully get company orderbook"}
//
// ---- ORDERBOOK STREAM EXAMPLE
// 
// ====
// BID:
// ====
/*
{
  "data": {
    "symbol": "BUMI",
    "body": "#O|BUMI|BID|332;161;12624800|330;945;57868800|328;268;73295800|326;295;55277300|324;215;24082200|322;171;18209500|320;584;46600200|318;138;41080200|316;111;32300200|314;132;11660600|312;59;3578400|310;240;37172000|308;56;34822800|306;64;51688000|304;56;27049900|302;75;6051100|300;435;72175800|298;70;2785500|296;53;31476700|294;198;42273800|1765850461-4326&682073600",
    "sequenceNumber": 29921,
    "orderbookId": 135,
    "datetime": "2025-12-16T09:01:01.532929+07:00",
    "bookType": 1
  }
}
//
// =====
// OFFER
// =====
{
  "data": {
    "symbol": "BUMI",
    "body": "#O|BUMI|OFFER|334;126;2721100|336;221;18619300|338;404;15019900|340;783;32263700|342;344;28935800|344;1727;35564700|346;248;27846900|348;196;15150100|350;333;27164400|352;149;37480700|354;167;40532600|356;149;19233500|358;140;15229100|360;355;52003800|362;159;38492800|364;194;42790900|366;163;11903800|368;223;15696100|370;529;87793100|372;260;42279500|374;259;46846200|376;235;20068300|378;255;22118100|380;753;40231700|382;178;10839100|384;191;18767000|386;212;21243700|388;220;18735700|390;552;42063900|392;156;15384000|394;177;10499100|396;247;41189800|398;438;70765600|400;2482;156228300|402;144;14434500|404;122;23591200|406;99;6154000|408;85;10877900|410;304;33786500|412;65;6930800|414;51;2442900|416;58;2393000|418;37;3959500|420;238;17080700|422;32;1975400|424;37;5165400|426;45;3183400|428;44;4074900|430;332;34425700|1765850461-14918&1312178100",
    "sequenceNumber": 29937,
    "orderbookId": 135,
    "datetime": "2025-12-16T09:01:01.620482+07:00",
    "bookType": 1
  }
}
*/

// --- Helper Split String (Untuk parsing pipe | stream) ---
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
  : m_run(false), m_isConnected(false), m_isSubscribed(false), m_hOrderbookWnd(NULL) 
{}

OrderbookClient::~OrderbookClient() { 
  stop(); 
}

void OrderbookClient::setWindowHandle(HWND hWnd) { 
  m_hOrderbookWnd = hWnd; 
}

void OrderbookClient::start(const std::string& userId, const std::string& wsKeyUrl) {
  if (m_run) return;
  m_userId = userId;
  m_wsKeyUrl = wsKeyUrl;
  m_run = true;
  // Worker thread diam (lazy start) sampai setActiveTicker dipanggil
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

// =========================================================
// TRIGGER LOGIC (MAIN INTERFACE)
// =========================================================

void OrderbookClient::setActiveTicker(const std::string& ticker) {
  if (!m_run) return;

  {
    std::lock_guard<std::mutex> lock(m_dataMutex);
    if (m_activeTicker == ticker) return; // Ignore kalau sama
      
    m_activeTicker = ticker;
    m_data.clear();
    m_data.symbol = ticker;
      
    // Notify UI data kosong (loading state)
    if (m_hOrderbookWnd) PostMessage(m_hOrderbookWnd, WM_USER_STREAMING_UPDATE, 1, 0);
  }

  // Restart Worker Thread untuk Ticker baru
  // Join thread lama, lalu spawn baru. Ini cara paling clean untuk reset state.
  if (m_workerThread.joinable()) m_workerThread.join();
  m_workerThread = std::thread(&OrderbookClient::connectAndStream, this);
}

OrderbookSnapshot OrderbookClient::getData() {
  std::lock_guard<std::mutex> lock(m_dataMutex);
  return m_data;
}

// =========================================================
// MAIN WORKER LOGIC
// =========================================================

void OrderbookClient::connectAndStream() {
  std::string currentSymbol;
  {
    std::lock_guard<std::mutex> lock(m_dataMutex);
    currentSymbol = m_activeTicker;
  }

  if (currentSymbol.empty()) return;

  // --- STEP 1: FETCH HTTP SNAPSHOT (Initial Data) ---
  // Supaya user langsung lihat angka tanpa nunggu websocket connect
  fetchSnapshot(currentSymbol);

  // --- STEP 2: CONNECT WEBSOCKET ---
  // Fetch Key Socket baru (Dedicated Key)
  std::string wskey = fetchWsKey(m_wsKeyUrl);
  if (wskey.empty()) return; // Gagal dapet key, abort.

  m_ws = std::make_unique<ix::WebSocket>();
  m_ws->setUrl(Config::getInstance().getSocketUrl());

  // Callback WebSocket
  m_ws->setOnMessageCallback([this, wskey, currentSymbol](const ix::WebSocketMessagePtr& msg) {
        
    if (msg->type == ix::WebSocketMessageType::Open) {
      m_isConnected = true;
      m_isSubscribed = false;

      // 1. Handshake
      m_ws->sendBinary(buildHandshake(m_userId, wskey));

      // 2. Subscribe (Delay dikit 100ms biar server ready)
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      std::string subPayload = buildSubscribe(m_userId, wskey, currentSymbol);
      if (!subPayload.empty()) {
        m_ws->sendBinary(subPayload);
        m_isSubscribed = true;
      }

      // 3. Start Ping Loop
      if (m_pingThread.joinable()) m_pingThread.join();
      m_pingThread = std::thread(&OrderbookClient::pingLoop, this);

    } else if (msg->type == ix::WebSocketMessageType::Message) {
        handleMessage(msg->str);
    } else if (msg->type == ix::WebSocketMessageType::Close || msg->type == ix::WebSocketMessageType::Error) {
        m_isConnected = false;
    }
  });

  m_ws->start();

  // Loop Keep Alive di Thread ini
  while (m_run && m_isConnected) {
    // Cek jika ticker berubah (thread ini harus mati)
    {
      std::lock_guard<std::mutex> lock(m_dataMutex);
      if (m_activeTicker != currentSymbol) break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }
}

// =========================================================
// HTTP SNAPSHOT (JSON PARSING WITH NLOHMANN)
// =========================================================

void OrderbookClient::fetchSnapshot(const std::string& symbol) {
  std::string host = Config::getInstance().getHost();
  std::string url = host + "/api/amibroker/orderbook?symbol=" + symbol;
  
  // Request Synchronous ke API
  std::string resp = WinHttpGetData(url);
  
  if (resp.empty()) return;

  if (parseSnapshotJson(resp, symbol)) {
    // Notify UI update awal
    if (m_hOrderbookWnd) PostMessage(m_hOrderbookWnd, WM_USER_STREAMING_UPDATE, 1, 0);
  }
}

bool OrderbookClient::parseSnapshotJson(const std::string& jsonResponse, const std::string& symbol) {
  try {
    auto j = json::parse(jsonResponse);

    // Cek struktur "data"
    if (!j.contains("data")) return false;
    auto& data = j["data"];

    // Validasi Company Type
    // Note: Gunakan .value("key", default) supaya tidak throw exception kalau key tidak ada
    std::string type = data.value("company_type", "");
    if (type != "Saham") return false; 

    // Lock Mutex sebelum update
    std::lock_guard<std::mutex> lock(m_dataMutex);
    m_data.clear();
    m_data.symbol = symbol;
    m_data.company_type = type;

    // Parse Header (Average, dll)
    // Kadang API balikin angka sebagai string, kita handle dua-duanya
    if (data.contains("average")) {
      if (data["average"].is_string()) 
        m_data.last_price = std::stod(data["average"].get<std::string>());
      else 
        m_data.last_price = data.value("average", 0.0);
    }

    // Helper lambda untuk parse array bid/offer
    auto parseList = [](const json& arr, std::vector<OrderLevel>& vec) {
      if (!arr.is_array()) return;
      for (const auto& item : arr) {
        OrderLevel lvl;
        // Parse Price
        if (item.contains("price")) {
          if (item["price"].is_string()) lvl.price = std::stol(item["price"].get<std::string>());
          else lvl.price = item["price"].get<long>();
        }
        // Parse Queue (que_num)
        if (item.contains("que_num")) {
          if (item["que_num"].is_string()) lvl.queue = std::stol(item["que_num"].get<std::string>());
          else lvl.queue = item["que_num"].get<long>();
        }
        // Parse Volume
        if (item.contains("volume")) {
          if (item["volume"].is_string()) lvl.volume = std::stol(item["volume"].get<std::string>());
          else lvl.volume = item["volume"].get<long>();
        }
        vec.push_back(lvl);
      }
    };

    if (data.contains("bid")) parseList(data["bid"], m_data.bids);
    if (data.contains("offer")) parseList(data["offer"], m_data.offers); // API kadang pake "offer"

    return true;

  } catch (const std::exception&) {
    // Log error if needed: e.what()
    return false;
  }
}

// =========================================================
// WEBSOCKET & PROTOBUF LOGIC
// =========================================================

void OrderbookClient::pingLoop() {
  while (m_run && m_isConnected) {
    std::this_thread::sleep_for(std::chrono::seconds(5));
    std::string payload = buildPing();
    if(!payload.empty() && m_ws) m_ws->sendBinary(payload);
  }
}

void OrderbookClient::handleMessage(const std::string& msg) {
  if (msg.empty()) return;
  const pb_byte_t* data = (const pb_byte_t*)msg.data();
  size_t len = msg.size();

  // --- 1. Decode Orderbook Stream (StockOrderbook) ---
  {
    StockOrderbook obMsg = StockOrderbook_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(data, len);
    if (pb_decode(&stream, StockOrderbook_fields, &obMsg)) {
      if (obMsg.has_data) {
        std::string body(obMsg.data.body);
        // Parse Pipe String: #O|BBCA|BID|...
        parseStreamBody(body);
        // Trigger UI Update
        if (m_hOrderbookWnd) PostMessage(m_hOrderbookWnd, WM_USER_STREAMING_UPDATE, 1, 0);
        return;
      }
    }
  }

  // --- 2. Decode Header Stream (StockFeed / Livequote) ---
  {
    StockFeed feedMsg = StockFeed_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(data, len);
    if (pb_decode(&stream, StockFeed_fields, &feedMsg)) {
      if (feedMsg.has_stock_data) {
        std::lock_guard<std::mutex> lock(m_dataMutex);
        if (m_data.symbol == feedMsg.stock_data.symbol) {
          m_data.last_price = feedMsg.stock_data.lastprice;
          m_data.prev_close = feedMsg.stock_data.previous;
          if(feedMsg.stock_data.has_change) {
            m_data.change = feedMsg.stock_data.change.value;
            m_data.percent = feedMsg.stock_data.change.percentage;
          }
        }
        if (m_hOrderbookWnd) PostMessage(m_hOrderbookWnd, WM_USER_STREAMING_UPDATE, 1, 0);
        return;
      }
    }
  }
}

// =========================================================
// STREAM STRING PARSING (#O|SYMBOL|BID|...)
// =========================================================

void OrderbookClient::parseStreamBody(const std::string& body) {
  // Format: #O|BBCA|BID|Price;Queue;Vol|Price;Queue;Vol|...
  auto tokens = ob_split(body, '|');
  if (tokens.size() < 3) return;

  if (tokens[0] != "#O") return;
  std::string symbol = tokens[1];
  std::string type = tokens[2]; // BID atau OFFER

  // Pastikan symbol sesuai context
  {
    std::lock_guard<std::mutex> lock(m_dataMutex);
    if (symbol != m_data.symbol) return;
  }

  std::vector<OrderLevel> tempVec;
    
  // Parse Rows (mulai index 3 sampai akhir)
  for (size_t i = 3; i < tokens.size(); i++) {
    // Skip checksum footer yg aneh2
    if (tokens[i].find('&') != std::string::npos || tokens[i].empty()) continue;
    parseRow(tokens[i], tempVec);
  }

  // Update Store
  {
    std::lock_guard<std::mutex> lock(m_dataMutex);
    if (type == "BID") m_data.bids = tempVec;
    else if (type == "OFFER") m_data.offers = tempVec;
  }
}

void OrderbookClient::parseRow(const std::string& rawRow, std::vector<OrderLevel>& targetVec) {
  // Format: "3600;120;5000" (Price;Queue;Volume)
  auto cols = ob_split(rawRow, ';');
  if (cols.size() < 3) return;
  
  try {
    OrderLevel lvl;
    lvl.price = std::stol(cols[0]);
    lvl.queue = std::stol(cols[1]);
    lvl.volume = std::stol(cols[2]);
    targetVec.push_back(lvl);
  } catch (...) {
    // Ignore error parsing
  }
}

// =========================================================
// PROTO BUILDERS (HANDSHAKE & SUBSCRIBE)
// =========================================================

std::string OrderbookClient::fetchWsKey(const std::string& url) {
  // Reuse logic dari API Client (WinHttpGetData)
  // Asumsi balikan API string polos key.
  return WinHttpGetData(url);
}

std::string OrderbookClient::buildHandshake(const std::string& userId, const std::string& key) {
  Handshake hs = Handshake_init_default;
  strncpy_s(hs.userId, sizeof(hs.userId), userId.c_str(), _TRUNCATE);
  strncpy_s(hs.key, sizeof(hs.key), key.c_str(), _TRUNCATE);

  uint8_t buffer[256];
  pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));
  if (pb_encode(&stream, Handshake_fields, &hs)) {
    return std::string((char*)buffer, stream.bytes_written);
  }
  return "";
}

std::string OrderbookClient::buildPing() {
  WSWrapper wrapper = WSWrapper_init_default;
  wrapper.has_ping = true;
  strncpy_s(wrapper.ping.message, sizeof(wrapper.ping.message), "PING", _TRUNCATE);

  uint8_t buffer[64];
  pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));
  if (pb_encode(&stream, WSWrapper_fields, &wrapper)) {
    return std::string((char*)buffer, stream.bytes_written);
  }
  return "";
}

std::string OrderbookClient::buildSubscribe(const std::string& userId, const std::string& key, const std::string& symbol) {
  OrderbookSubscribe req = OrderbookSubscribe_init_default;
  strncpy_s(req.userId, sizeof(req.userId), userId.c_str(), _TRUNCATE);
  strncpy_s(req.key, sizeof(req.key), key.c_str(), _TRUNCATE);
  
  req.has_subs = true;
  
  // 1. Subscribe Orderbook Stream
  req.subs.orderbook_count = 1;
  strncpy_s(req.subs.orderbook[0], sizeof(req.subs.orderbook[0]), symbol.c_str(), _TRUNCATE);
  
  // 2. Subscribe LiveQuote Header
  req.subs.livequote_count = 1;
  strncpy_s(req.subs.livequote[0], sizeof(req.subs.livequote[0]), symbol.c_str(), _TRUNCATE);

  uint8_t buffer[512];
  pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));
  if (pb_encode(&stream, OrderbookSubscribe_fields, &req)) {
    return std::string((char*)buffer, stream.bytes_written);
  }
  return "";
}