#include "ami_bridge.h"
#include "data_store.h"
#include "api_client.h"
#include "ws_client.h"
#include "ownership_fetcher.h"
#include "FinancialFetcher.h"
#include "ritel_fetcher.h"
#include <windows.h>
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

extern std::shared_ptr<WsClient> g_wsClient;
extern HWND g_hAmiBrokerWnd;
extern std::atomic<bool> g_bWorkerThreadRun;

static std::mutex g_fetchMtx;
static std::map<std::string, bool> g_isFetching;
static std::mutex g_dataStoreMtx;

// --- MUTEX & CV Definition (.h)
std::mutex g_fetchQueueMtx;
std::condition_variable g_fetchQueueCV;
static std::map<std::string, FetchTask> g_fetchQueue;

static void LogBridge(const std::string& msg) {
  SYSTEMTIME t;
  GetLocalTime(&t);
  char buf[64];
  sprintf_s(buf, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
  OutputDebugStringA((std::string(buf) + "[Bridge] " + msg + "\n").c_str());
}

bool QueueFetchTask(FetchTask task) {
  std::string task_key;

  // Bikin Kunci Unik
  switch (task.type) {
    case FetchTaskType::GET_CANDLES:
      task_key = "CANDLES_" + task.symbol;
      break;
    case FetchTaskType::GET_OWNERSHIP_INDIV:
      task_key = "OWN_INDIV_" + task.symbol;
      break;
    case FetchTaskType::GET_OWNERSHIP_CORP:
      task_key = "OWN_CORP_" + task.symbol;
      break;
    case FetchTaskType::GET_FINANCIALS:
      task_key = "FINANCIALS_" + task.symbol;
      break;
    case FetchTaskType::GET_RITEL_FLOW:
      task_key = "RITEL_" + task.symbol;
      break;
    case FetchTaskType::GET_BROKER_FLOW:
      task_key = "BROKER_" + task.extra_param + "_" + task.symbol;  // Buat key unik biar nggak tabrakan antrian
      break;
  }

  if (task_key.empty()) return false;

  // Cek g_isFetching (Mutex g_fetchMtx)
  {
    std::lock_guard<std::mutex> lock(g_fetchMtx);
    if (g_isFetching.count(task_key)) {
        return false; // Udah ada yang ngerjain
    }
  }

  // Cek g_fetchQueue (Mutex g_fetchQueueMtx)
  {
    std::lock_guard<std::mutex> lock(g_fetchQueueMtx);
    if (g_fetchQueue.count(task_key)) {
      return false; // Udah ada di antrian
    }
    
    // Kalo aman, baru masukin antrian
    g_fetchQueue[task_key] = std::move(task);
    LogBridge("Queued Task: " + task_key);
    g_fetchQueueCV.notify_one();
  }
  return true;
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
    std::string task_key;
    FetchTask task;

    {
      std::unique_lock<std::mutex> lock(g_fetchQueueMtx);
      g_fetchQueueCV.wait(lock, [&] {
        return !g_fetchQueue.empty() || !g_bWorkerThreadRun;
      });

      if (!g_bWorkerThreadRun) break;
      if (g_fetchQueue.empty()) continue; 

      auto it = g_fetchQueue.begin();
      task_key = it->first;
      task = std::move(it->second);
      g_fetchQueue.erase(it);
    } 

    // Tandai "Lagi Dikerjain"
    {
      std::lock_guard<std::mutex> lock(g_fetchMtx);
      g_isFetching[task_key] = true;
    }

    // --- DISPATCHER UTAMA WORKER ---
    switch (task.type) {
      case FetchTaskType::GET_CANDLES:
        LogBridge("Worker processing CANDLES: " + task.symbol);
        // fetchAndCache udah ngurus g_isFetching-nya sendiri,
        // tapi kita harus ubah key-nya
        fetchAndCache(task.symbol, task.from_date, task.to_date, task.preload); 
        break;

      case FetchTaskType::GET_OWNERSHIP_INDIV:
        LogBridge("Worker processing OWN_INDIV: " + task.symbol);
        OwnershipFetcher::fetch(task.symbol, "Individual");
        break;

      case FetchTaskType::GET_OWNERSHIP_CORP:
        LogBridge("Worker processing OWN_CORP: " + task.symbol);
        OwnershipFetcher::fetch(task.symbol, "Perusahaan");
        break;
      
      case FetchTaskType::GET_FINANCIALS:
        LogBridge("Worker processing FINANCIALS: " + task.symbol);
        FinancialFetcher::fetch(task.symbol);
        break;
      
      case FetchTaskType::GET_RITEL_FLOW:
        LogBridge("Worker processing RITEL_FLOW: " + task.symbol);
        RitelFetcher::fetch(task.symbol);
        break;

      case FetchTaskType::GET_BROKER_FLOW:
        LogBridge("Worker processing BROKER_FLOW (" + task.extra_param + "): " + task.symbol);
        RitelFetcher::fetch(task.symbol, task.extra_param);   // Panggil fetcher pakai parameter broker
        break;
    }
    
    // Hapus tanda "Lagi Dikerjain"
    {
        std::lock_guard<std::mutex> lock(g_fetchMtx);
        g_isFetching.erase(task_key);
    }
    
    // Kasih tau AmiBroker buat refresh (penting!)
    if (g_hAmiBrokerWnd) PostMessage(g_hAmiBrokerWnd, WM_USER_STREAMING_UPDATE, 0, 0);

    std::this_thread::sleep_for(std::chrono::milliseconds(400)); // Jeda API
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

    {
      std::shared_ptr<WsClient> wsClient = g_wsClient;

      if (wsClient && wsClient->isConnected()) {
        std::lock_guard<std::mutex> dslock(g_dataStoreMtx);
        gDataStore.mergeLiveToHistorical(symbol);
        final_candles = gDataStore.getHistorical(symbol);
      }
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

      FetchTask task;
      task.type = FetchTaskType::GET_CANDLES;
      task.symbol = symbol;
      task.from_date = from_date;
      task.to_date = to_date;
      task.preload = preload;
      QueueFetchTask(std::move(task)); // Pake fungsi queuer baru kita

      if (!preload.empty()) {
        gDataStore.setHistorical(symbol, preload);
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
