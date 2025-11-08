#include "ami_bridge.h"
#include "data_store.h"
#include "api_client.h"
#include "ws_client.h"
#include <windows.h>
#include <algorithm>
#include <vector>
#include <chrono>
#include <string>
#include <memory>
#include <thread>
#include <mutex>
#include <map>
#include <atomic>
#include <set>
#include <condition_variable>

extern std::unique_ptr<WsClient> g_wsClient;
extern HWND g_hAmiBrokerWnd;
extern std::atomic<bool> g_bWorkerThreadRun;

static std::mutex g_fetchMtx;
static std::map<std::string, bool> g_isFetching;
static std::mutex g_dataStoreMtx;

// ---- FETCH & QUEUE STRUCT ----
struct FetchRequest {
  std::string from_date;
  std::string to_date;
  std::vector<Candle> preload;
};

// --- MUTEX & CV Definition (.h)
std::mutex g_fetchQueueMtx;
std::condition_variable g_fetchQueueCV;
static std::map<std::string, FetchRequest> g_fetchQueue;

void LogBridge(const std::string& msg) {
  OutputDebugStringA(("[Bridge] " + msg + "\n").c_str());
}

inline void LogIfDebug(const std::string& msg) {
  #ifdef _DEBUG
    LogBridge(msg);
  #endif
}

std::string AmiDateToString(const PackedDate& pd) {
  char buffer[12];
  sprintf_s(buffer, sizeof(buffer), "%04d-%02d-%02d", pd.Year, pd.Month, pd.Day);
  return std::string(buffer);
}

void fetchAndCache(std::string symbol, std::string from_date, std::string to_date, std::vector<Candle> existing_candles) {
  LogIfDebug("Async fetch START for: " + symbol);
  {
    // Tandai sebagai "sedang fetching"
    std::lock_guard<std::mutex> lock(g_fetchMtx);
    g_isFetching[symbol] = true;
  }
  std::vector<Candle> new_candles = fetchHistorical(symbol, from_date, to_date);
  LogIfDebug("Async fetch finished. Got " + std::to_string(new_candles.size()) + " bars.");

  // Merge existing preload candles from AmiBroker (jika tersedia)
  if (!existing_candles.empty()) {
    gDataStore.mergeHistorical(symbol, existing_candles);
  }

  // Merge new candles dari API
  if (!new_candles.empty()) {
    gDataStore.mergeHistorical(symbol, new_candles);
  }

  // Jika semua kosong, buat empty cache supaya mark as checked
  if (existing_candles.empty() && new_candles.empty()) {
    gDataStore.setHistorical(symbol, {});
  }

  {
    std::lock_guard<std::mutex> lock(g_fetchMtx);
    g_isFetching.erase(symbol);   // Hapus tanda "sedang fetching"
  }

  if (g_hAmiBrokerWnd) PostMessage(g_hAmiBrokerWnd, WM_USER_STREAMING_UPDATE, 0, 0);
  LogIfDebug("Async fetch COMPLETE for: " + symbol);
}

void ProcessFetchQueue() {
  // ---- Fix Main Loop: Worker akan tidur sampai ada kerjaan
  while (g_bWorkerThreadRun) {
    std::string symbol_to_fetch;
    FetchRequest request;

    // 1. Ambil lock & tunggu kerjaan
    {
      std::unique_lock<std::mutex> lock(g_fetchQueueMtx);
      // Main changes:
      // Worker akan 'tidur' sampai
      // 1. Antrian (g_fetchQueue) udah tidak kosong, OR
      // 2. g_bWorkerThreadRun == false
      g_fetchQueueCV.wait(lock, [&] {
        return !g_fetchQueue.empty() || !g_bWorkerThreadRun;
      });

      // Jika worker bangun tapi disuruh stop, maka langsung keluar
      if (!g_bWorkerThreadRun) {
        break;
      }

      // Kalau dia bangun karena ada pekerjaan, ambil kerjaannya
      if (!g_fetchQueue.empty()) {
        auto it = g_fetchQueue.begin();
        symbol_to_fetch = it->first;
        request = std::move(it->second);
        g_fetchQueue.erase(it);
      }
    } // Lock ter-release disini

    // 2. Proses kerjaan (DI LUAR LOCK)
    if (!symbol_to_fetch.empty()) {
      LogBridge("Worker processing: " + symbol_to_fetch);
      // Panggil fungsi yang sudah ada, tapi secara SERIAL
      fetchAndCache(symbol_to_fetch, request.from_date, request.to_date, request.preload);
      
      // Beri jeda sedikit antar API call
      std::this_thread::sleep_for(std::chrono::milliseconds(400)); // Jeda 400ms
    } 
  }
  LogBridge("Worker thread shutting down.");
}

int GetQuotesEx_Bridge(LPCTSTR pszTicker, int nPeriodicity, int nLastValid, int nSize, struct Quotation* pQuotes)
{
  std::string symbol(pszTicker);
  if (nPeriodicity != PERIODICITY_EOD) return nLastValid + 1;

  std::vector<Candle> final_candles;
  bool hasHist = gDataStore.hasHistorical(symbol);

  if (hasHist) {
    LogIfDebug("Cache HIT for " + symbol);
    final_candles = gDataStore.getHistorical(symbol);

    if (g_wsClient && g_wsClient->isConnected()) {
      std::lock_guard<std::mutex> dslock(g_dataStoreMtx);
      gDataStore.mergeLiveToHistorical(symbol);
      final_candles = gDataStore.getHistorical(symbol);
    }
  } else {
    // CACHE MISS
    LogIfDebug("Cache MISS for " + symbol);

    bool is_already_fetching_or_queued = false;
    {
      std::lock_guard<std::mutex> lock(g_fetchMtx);
      is_already_fetching_or_queued = (g_isFetching.find(symbol) != g_isFetching.end());
    }
    {
      std::lock_guard<std::mutex> lock(g_fetchQueueMtx);
      if (g_fetchQueue.find(symbol) != g_fetchQueue.end()) {
        is_already_fetching_or_queued = true;
      }
    }

    if (!is_already_fetching_or_queued) {
      // ---- PRELOAD from AmiBroker ----
      std::vector<Candle> preload;
      if (nLastValid >= 0) {
        preload.reserve(nLastValid + 1);
        for (int i = 0; i <= nLastValid; ++i) {
          Candle c;
          c.date = AmiDateToString(pQuotes[i].DateTime.PackDate);
          c.open = pQuotes[i].Open;
          c.high = pQuotes[i].High;
          c.low = pQuotes[i].Low;
          c.close = pQuotes[i].Price;
          c.volume = pQuotes[i].Volume;
          c.frequency = pQuotes[i].OpenInterest;
          c.value = pQuotes[i].AuxData1;
          c.netforeign = pQuotes[i].AuxData2;
          preload.push_back(c);
        }
        LogBridge("Preload data captured for " + symbol + " with " + std::to_string(preload.size()) + " bars.");
      }

      // Tentukan range tanggal fetch
      std::string from_date, to_date;
      auto now = std::chrono::system_clock::now();
      to_date = timePointToString(now);

      if (nLastValid >= 0) {
        const auto& lastQuoteDate = pQuotes[nLastValid].DateTime.PackDate;
        std::tm last_tm = {};
        last_tm.tm_year = lastQuoteDate.Year - 1900;
        last_tm.tm_mon  = lastQuoteDate.Month - 1;
        last_tm.tm_mday = lastQuoteDate.Day;
        std::time_t last_tt = std::mktime(&last_tm);
        // auto next_day_tp = std::chrono::system_clock::from_time_t(last_tt) + std::chrono::hours(24);
        // from_date = timePointToString(next_day_tp);
        from_date = timePointToString(std::chrono::system_clock::from_time_t(last_tt));
      } else {
        int lookback_days = 365 * 2;  // 2 tahun
        from_date = timePointToString(now - std::chrono::hours(24 * lookback_days));
      }

      // ---- PERUBAHAN UTAMA: Masukkan ke Antrian ---
      {
        std::lock_guard<std::mutex> lock(g_fetchQueueMtx);
        FetchRequest req;
        req.from_date = from_date;
        req.to_date = to_date;
        req.preload = std::move(preload);
        g_fetchQueue[symbol] = std::move(req); // Pake std::move
        LogBridge("Queued " + symbol + " for fetching.");
      }

      // ---- BANGUNKAN WORKER-NYA!
      g_fetchQueueCV.notify_one();
    }

      // Kembalikan data preload (jika ada) agar chart tidak kosong
      if (!gDataStore.hasHistorical(symbol) && nLastValid >= 0)
        {
          // Ambil dari antrian (agak boros, tapi aman)
          std::lock_guard<std::mutex> lock(g_fetchQueueMtx);
          auto it = g_fetchQueue.find(symbol);
          if(it != g_fetchQueue.end() && !it->second.preload.empty()) {
            gDataStore.setHistorical(symbol, it->second.preload);
          }
        }
        final_candles = gDataStore.getHistorical(symbol); // (Mungkin kosong jika nLastValid < 0)
        
    } // end cache miss

    if (final_candles.empty()) return 0;

    size_t numToCopy = std::min<size_t>(final_candles.size(), (nSize > 0) ? static_cast<size_t>(nSize) : 0);
    size_t startIndex = (final_candles.size() > numToCopy) ? (final_candles.size() - numToCopy) : 0;

    for (size_t i = 0; i < numToCopy; ++i) {
      const auto& candle = final_candles[startIndex + i];
      struct Quotation& qt = pQuotes[i];
      qt.DateTime.Date = 0;
      int year, month, day;
      if (sscanf_s(candle.date.c_str(), "%d-%d-%d", &year, &month, &day) == 3) {
        qt.DateTime.PackDate.Year = year;
        qt.DateTime.PackDate.Month = month;
        qt.DateTime.PackDate.Day = day;
        qt.DateTime.PackDate.Minute = DATE_EOD_MINUTES;
        qt.DateTime.PackDate.Hour = DATE_EOD_HOURS;
      }
      qt.Price = static_cast<float>(candle.close);
      qt.Open = static_cast<float>(candle.open);
      qt.High = static_cast<float>(candle.high);
      qt.Low = static_cast<float>(candle.low);
      qt.Volume = static_cast<float>(candle.volume);
      qt.OpenInterest = static_cast<float>(candle.frequency);
      qt.AuxData1 = static_cast<float>(candle.value);
      qt.AuxData2 = static_cast<float>(candle.netforeign);
    }

    return static_cast<int>(numToCopy);
}
