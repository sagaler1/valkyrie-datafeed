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

extern std::unique_ptr<WsClient> g_wsClient;
extern HWND g_hAmiBrokerWnd;

static std::mutex g_fetchMtx;
static std::map<std::string, bool> g_isFetching;
static std::mutex g_dataStoreMtx;

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
  std::vector<Candle> new_candles = fetchHistorical(symbol, from_date, to_date);
  LogIfDebug("Async fetch finished. Got " + std::to_string(new_candles.size()) + " bars.");

  {
    std::lock_guard<std::mutex> dslock(g_dataStoreMtx);

    // Merge existing preload candles (from AmiBroker) if any
    if (!existing_candles.empty()) {
      gDataStore.mergeHistorical(symbol, existing_candles);
    }

    // Merge new candles fetched from API
    if (!new_candles.empty()) {
      gDataStore.mergeHistorical(symbol, new_candles);
    }

    // If both empty, still create empty cache entry to mark as checked
    if (existing_candles.empty() && new_candles.empty()) {
      gDataStore.setHistorical(symbol, {});
    }
  }

  {
    std::lock_guard<std::mutex> lock(g_fetchMtx);
    g_isFetching.erase(symbol);
  }

  if (g_hAmiBrokerWnd) PostMessage(g_hAmiBrokerWnd, WM_USER_STREAMING_UPDATE, 0, 0);
  LogIfDebug("Async fetch COMPLETE for: " + symbol);
}

int GetQuotesEx_Bridge(LPCTSTR pszTicker, int nPeriodicity, int nLastValid, int nSize, struct Quotation* pQuotes)
{
  std::string symbol(pszTicker);
  if (nPeriodicity != PERIODICITY_EOD) return nLastValid + 1;

  std::vector<Candle> final_candles;
  bool hasHist = false;
  {
    std::lock_guard<std::mutex> dslock(g_dataStoreMtx);
    hasHist = gDataStore.hasHistorical(symbol);
  }

  if (hasHist) {
    LogIfDebug("Cache HIT for " + symbol);
    {
      std::lock_guard<std::mutex> dslock(g_dataStoreMtx);
      final_candles = gDataStore.getHistorical(symbol);
    }

    if (g_wsClient && g_wsClient->isConnected()) {
      std::lock_guard<std::mutex> dslock(g_dataStoreMtx);
      gDataStore.mergeLiveToHistorical(symbol);
      final_candles = gDataStore.getHistorical(symbol);
    }
  } else {
    LogIfDebug("Cache MISS for " + symbol);

    bool is_already_fetching = false;
    {
      std::lock_guard<std::mutex> lock(g_fetchMtx);
      is_already_fetching = (g_isFetching.find(symbol) != g_isFetching.end());
    }

    if (!is_already_fetching) {
      {
        std::lock_guard<std::mutex> lock(g_fetchMtx);
        g_isFetching[symbol] = true;
      }

      // --- PRELOAD from AmiBroker ---
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
        {
            std::lock_guard<std::mutex> dslock(g_dataStoreMtx);
            gDataStore.setHistorical(symbol, preload);
        }
        LogIfDebug("Preloaded cache for " + symbol + " with " + std::to_string(preload.size()) + " bars.");
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
        auto next_day_tp = std::chrono::system_clock::from_time_t(last_tt) + std::chrono::hours(24);
        from_date = timePointToString(next_day_tp);
      } else {
        int lookback_days = 365 * 2;
        from_date = timePointToString(now - std::chrono::hours(24 * lookback_days));
      }

      std::thread(fetchAndCache, symbol, from_date, to_date, std::move(preload)).detach();
    }

      LogIfDebug("Returning nLastValid+1 temporarily while async fetch runs.");
      return nLastValid + 1;
    }

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
