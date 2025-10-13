#include "ami_bridge.h"
#include "data_store.h"
#include "api_client.h"
#include <windows.h>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <vector>
#include <chrono>
#include <string>

// ---- Logging
void LogBridge(const std::string& msg) {
    OutputDebugStringA((msg + "\n").c_str());
}

// Forward declaration
int GetQuotesEx_Bridge(LPCTSTR pszTicker, int nPeriodicity, int nLastValid, int nSize, struct Quotation* pQuotes);

int GetQuotesEx_Bridge(LPCTSTR pszTicker, int nPeriodicity, int nLastValid, int nSize, struct Quotation* pQuotes)
{   
    LogBridge("[Bridge] GetQuotesEx called for Ticker: " + std::string(pszTicker) +
              ", Periodicity: " + std::to_string(nPeriodicity) +
              ", nLastValid: " + std::to_string(nLastValid) + 
              ", nSize: " + std::to_string(nSize)
            );

    if (nPeriodicity != PERIODICITY_EOD) {
        LogBridge("[Bridge] Ignoring non-EOD request.");
        //stdcout << "[Bridge] Ignoring non-EOD request." << std::endl;
        return nLastValid + 1;
    }

    std::string symbol(pszTicker);

    // ---- SMART BACKFILL LOGIC v3 (with MERGE) ----
    int lookback_days = (nLastValid <= 100) ? 260 : 14;
    LogBridge( "[Bridge] Smart Backfill decision: nLastValid = " + std::to_string(nLastValid)
                + ". Fetching " + std::to_string(lookback_days) + " days of data for " + symbol );

    auto now = std::chrono::system_clock::now();
    std::string to_date = timePointToString(now);
    std::string from_date = timePointToString(now - std::chrono::hours(24 * lookback_days));

    // 1. Fetch data terbaru dari API
    std::vector<Candle> recent_candles = fetchHistorical(symbol, from_date, to_date);
    LogBridge("[Bridge] API call finished. Received " + std::to_string(recent_candles.size()) + " recent bars.");

    // 2. GABUNGKAN (MERGE) data baru dengan cache yang ada, BUKAN TIMPA (OVERWRITE)
    gDataStore.mergeHistorical(symbol, recent_candles);
    LogBridge("[Bridge] Merged recent data into the cache.");

    // 3. Ambil KESELURUHAN data yang sudah ter-merge dari cache
    std::vector<Candle> all_candles = gDataStore.getHistorical(symbol);

    if (all_candles.empty()) {
        LogBridge("[Bridge] No data available for " + symbol + " after merge.");
        return 0; // Return 0 karena tidak ada data sama sekali
    }

    // Data dari gDataStore (setelah di-merge via map) sudah pasti terurut berdasarkan tanggal
    // jadi kita tidak perlu panggil std::sort lagi di sini.

    int numToCopy = (std::min)((int)all_candles.size(), nSize);
    LogBridge("[Bridge] Copying " + std::to_string(numToCopy) + " bars to AmiBroker.");

    if (numToCopy > 0) {
        int start_index = all_candles.size() - numToCopy;
        for (int i = 0; i < numToCopy; ++i) {
            const auto& candle = all_candles[start_index + i];
            pQuotes[i].DateTime.Date = 0;

            int year, month, day;
            if (sscanf_s(candle.date.c_str(), "%d-%d-%d", &year, &month, &day) == 3) {
                pQuotes[i].DateTime.PackDate.Year = year;
                pQuotes[i].DateTime.PackDate.Month = month;
                pQuotes[i].DateTime.PackDate.Day = day;
                pQuotes[i].DateTime.PackDate.Minute = 63;
                pQuotes[i].DateTime.PackDate.Hour = 31;
            } else {
                LogBridge("[Bridge] ERROR: Invalid date format: " + candle.date);
                continue;
            }

            pQuotes[i].Price = static_cast<float>(candle.close);
            pQuotes[i].Open = static_cast<float>(candle.open);
            pQuotes[i].High = static_cast<float>(candle.high);
            pQuotes[i].Low = static_cast<float>(candle.low);
            pQuotes[i].Volume = static_cast<float>(candle.volume);
            pQuotes[i].OpenInterest = static_cast<float>(candle.frequency);
            pQuotes[i].AuxData1 = static_cast<float>(candle.value);
            pQuotes[i].AuxData2 = static_cast<float>(candle.netforeign);
        }
    }

    LogBridge("[Bridge] Finished copying. Returning count: " + std::to_string(numToCopy));
    return numToCopy;
}
