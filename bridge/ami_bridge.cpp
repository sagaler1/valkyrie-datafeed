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
    std::vector<Candle> candles = gDataStore.getHistorical(symbol);
    LogBridge("[Bridge] Checked cache for " + symbol + ". Found " + std::to_string(candles.size()) + " bars.");

    // === SMART BACKFILL ===
    bool need_fetch = candles.empty();
    int lookback_days = 0;

    if (candles.empty()) {
        // symbol baru
        lookback_days = 260; // 1 tahun
    } else if (nLastValid < 100) {
        // database masih sedikit
        lookback_days = 260; // 1 tahun
    } else {
        // update rutin
        lookback_days = 14; // 2 minggu
    }

    if (need_fetch) {
        LogBridge(
            "[Bridge] Cache empty. Fetching from API for " + symbol +
            " (lookback " + std::to_string(lookback_days) + " days)"
            );

        auto now = std::chrono::system_clock::now();
        auto end_time = now;
        auto start_time = end_time - std::chrono::hours(24 * lookback_days);

        std::string to_date = timePointToString(end_time);
        std::string from_date = timePointToString(start_time);

        candles = fetchHistorical(symbol, from_date, to_date);
        LogBridge("[Bridge] API call finished. Received " + std::to_string(candles.size()) + " bars.");

        if (!candles.empty()) {
            gDataStore.setHistorical(symbol, candles);
            LogBridge("[Bridge] Data for " + symbol + " cached.");
        }
    }

    if (candles.empty()) {
        LogBridge("[Bridge] No data available for " + symbol);
        return nLastValid + 1;
    }

    // --- Sort data dari lama ke baru ---
    std::sort(candles.begin(), candles.end(), [](const Candle& a, const Candle& b) {
        return a.date < b.date;
    });

    int numToCopy = (std::min)((int)candles.size(), nSize);
    LogBridge("[Bridge] Copying " + std::to_string(numToCopy) + " bars to AmiBroker.");

    if (numToCopy > 0) {
        int start_index = candles.size() - numToCopy;
        for (int i = 0; i < numToCopy; ++i) {
            const auto& candle = candles[start_index + i];
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
