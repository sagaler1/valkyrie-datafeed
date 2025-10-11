#include "ami_bridge.h"
#include "data_store.h"
#include "api_client.h"
#include <windows.h>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <vector>
#include <chrono>

// === Redirect std::cout / std::cerr ke DebugView ===
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

// Forward declaration
int GetQuotesEx_Bridge(LPCTSTR pszTicker, int nPeriodicity, int nLastValid, int nSize, struct Quotation* pQuotes);

int GetQuotesEx_Bridge(LPCTSTR pszTicker, int nPeriodicity, int nLastValid, int nSize, struct Quotation* pQuotes)
{
    stdcout << "[Bridge] GetQuotesEx called for Ticker: " << pszTicker
            << ", Periodicity: " << nPeriodicity
            << ", nLastValid: " << nLastValid
            << ", nSize: " << nSize << std::endl;

    if (nPeriodicity != PERIODICITY_EOD) {
        stdcout << "[Bridge] Ignoring non-EOD request." << std::endl;
        return nLastValid + 1;
    }

    std::string symbol(pszTicker);
    std::vector<Candle> candles = gDataStore.getHistorical(symbol);
    stdcout << "[Bridge] Checked cache for " << symbol
            << ". Found " << candles.size() << " bars." << std::endl;

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
        stdcout << "[Bridge] Cache empty or outdated. Fetching from API for "
                << symbol << " (lookback " << lookback_days << " days)" << std::endl;

        auto now = std::chrono::system_clock::now();
        auto end_time = now;
        auto start_time = end_time - std::chrono::hours(24 * lookback_days);

        std::string to_date = timePointToString(end_time);
        std::string from_date = timePointToString(start_time);

        candles = fetchHistorical(symbol, from_date, to_date);
        stdcout << "[Bridge] API call finished. Received " << candles.size() << " bars." << std::endl;

        if (!candles.empty()) {
            gDataStore.setHistorical(symbol, candles);
            stdcout << "[Bridge] Data for " << symbol << " cached." << std::endl;
        }
    }

    if (candles.empty()) {
        stdcout << "[Bridge] No data available for " << symbol << std::endl;
        return nLastValid + 1;
    }

    // --- Sort data dari lama ke baru ---
    std::sort(candles.begin(), candles.end(), [](const Candle& a, const Candle& b) {
        return a.date < b.date;
    });

    int numToCopy = (std::min)((int)candles.size(), nSize);
    stdcout << "[Bridge] Copying " << numToCopy << " bars to AmiBroker." << std::endl;

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
                stdcerr << "[Bridge] ERROR: Invalid date format: " << candle.date << std::endl;
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

    stdcout << "[Bridge] Finished copying. Returning count: " << numToCopy << std::endl;
    return numToCopy;
}
