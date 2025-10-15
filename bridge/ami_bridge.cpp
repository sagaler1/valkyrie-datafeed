#include "ami_bridge.h"
#include "data_store.h"
#include "api_client.h"
#include "ws_client.h"      // for WsClient access
#include <windows.h>
#include <algorithm>
#include <vector>
#include <chrono>
#include <string>
#include <memory>           // for std::unique_ptr

// ---- Deklarasi Variabel Global dari plugin.cpp ----
extern std::unique_ptr<WsClient> g_wsClient;

// ---- Logging
void LogBridge(const std::string& msg) {
    OutputDebugStringA(("[Bridge] " + msg + "\n").c_str());
}

inline void LogIfDebug(const std::string& msg) {
#ifdef _DEBUG
    LogBridge(msg);
#endif
}

// ---- Helper untuk konversi dari format tanggal AmiBroker ke string YYYY-MM-DD
std::string AmiDateToString(const PackedDate& pd) {
    char buffer[12];
    sprintf_s(buffer, sizeof(buffer), "%04d-%02d-%02d", pd.Year, pd.Month, pd.Day);
    return std::string(buffer);
}

// ---- FUNGSI UTAMA YANG DIPANGGIL AMIBROKER ----
int GetQuotesEx_Bridge(LPCTSTR pszTicker, int nPeriodicity, int nLastValid, int nSize, struct Quotation* pQuotes)
{
    LogIfDebug("GetQuotesEx called for Ticker: " + std::string(pszTicker) +
        ", Periodicity: " + std::to_string(nPeriodicity) +
        ", nLastValid: " + std::to_string(nLastValid)
    );

    if (nPeriodicity != PERIODICITY_EOD) {
        LogIfDebug("Ignoring non-EOD request.");
        return nLastValid + 1;
    }

    std::string symbol(pszTicker);
    std::vector<Candle> final_candles;

    // =================================================================================
    // ---- PEMISAHAN LOGIKA: REAL-TIME vs BACKFILL ----
    // =================================================================================

    if (g_wsClient && g_wsClient->isConnected())
    {
        // ---- JALUR 1: REAL-TIME (WebSocket Aktif)
        LogIfDebug("WebSocket is ACTIVE. Using in-memory cache.");
        
        // ---- FIX: SINKRONISASI SATU KALI JIKA CACHE KOSONG
        if (!gDataStore.hasHistorical(symbol) && nLastValid >= 0) {
            LogIfDebug("Cache is empty for " + symbol + ". Syncing from AmiBroker's data (pQuotes)...");
            std::vector<Candle> existing_candles;
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
                existing_candles.push_back(c);
            }
            gDataStore.mergeHistorical(symbol, existing_candles);
            LogIfDebug("Sync complete. Cache now has " + std::to_string(existing_candles.size()) + " bars.");
        }
        
        // ---- Lanjutkan dengan logika real-time seperti biasa
        final_candles = gDataStore.getHistorical(symbol);
        if (!final_candles.empty()) {
            gDataStore.mergeLiveToHistorical(symbol);
            final_candles = gDataStore.getHistorical(symbol); // Ambil lagi versi terupdate
        }
    }
    else
    {
        // ---- JALUR 2: BACKFILL / FALLBACK (WebSocket Mati)
        LogIfDebug("WebSocket is INACTIVE. Using HTTP backfill logic.");

        std::string from_date, to_date;
        auto now = std::chrono::system_clock::now();
        to_date = timePointToString(now);

        if (nLastValid >= 0) {
            const auto& lastQuoteDate = pQuotes[nLastValid].DateTime.PackDate;
            std::string last_date_str = AmiDateToString(lastQuoteDate);
            std::tm last_tm = {};
            sscanf_s(last_date_str.c_str(), "%d-%d-%d", &last_tm.tm_year, &last_tm.tm_mon, &last_tm.tm_mday);
            last_tm.tm_year -= 1900; last_tm.tm_mon -= 1;
            auto last_tp = std::chrono::system_clock::from_time_t(std::mktime(&last_tm));
            from_date = timePointToString(last_tp + std::chrono::hours(24));
            
            if (from_date > to_date) {
                 LogIfDebug("Data is up-to-date. No fetch needed.");
                 return nLastValid + 1;
            }
            LogIfDebug("Data exists. Fetching delta from: " + from_date);
        } else {
            int initial_lookback_days = 365 * 1; // 1 Tahun saja
            from_date = timePointToString(now - std::chrono::hours(24 * initial_lookback_days));
            LogIfDebug("No data. Performing initial fetch from: " + from_date);
        }

        std::vector<Candle> new_candles = fetchHistorical(symbol, from_date, to_date);
        LogIfDebug("API call finished. Received " + std::to_string(new_candles.size()) + " new bars.");
        
        std::vector<Candle> combined_candles;
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
            combined_candles.push_back(c);
        }
        if (!new_candles.empty()) {
            combined_candles.insert(combined_candles.end(), new_candles.begin(), new_candles.end());
        }
        
        gDataStore.setHistorical(symbol, {});
        gDataStore.mergeHistorical(symbol, combined_candles);
        final_candles = gDataStore.getHistorical(symbol);
    }

    // ---- BAGIAN AKHIR: MENYALIN DATA KE AMIBROKER ----
    if (final_candles.empty()) {
        LogIfDebug("No data available for symbol after processing. Returning 0.");
        return 0;
    }

    size_t numToCopy = std::min<size_t>(final_candles.size(), (nSize > 0) ? static_cast<size_t>(nSize) : 0);
    size_t startIndex = (final_candles.size() > numToCopy) ? (final_candles.size() - numToCopy) : 0;
    LogIfDebug("Copying latest " + std::to_string(numToCopy) + " bars to AmiBroker.");

    for (size_t i = 0; i < numToCopy; ++i) {
        const auto& candle = final_candles[startIndex + i];
        struct Quotation& qt = pQuotes[i];
        qt.DateTime.Date = 0;
        int year, month, day;
        if (sscanf_s(candle.date.c_str(), "%d-%d-%d", &year, &month, &day) == 3) {
            qt.DateTime.PackDate.Year = year; qt.DateTime.PackDate.Month = month; qt.DateTime.PackDate.Day = day;
            qt.DateTime.PackDate.Minute = DATE_EOD_MINUTES; qt.DateTime.PackDate.Hour = DATE_EOD_HOURS;
        } else { continue; }
        qt.Price = static_cast<float>(candle.close); 
        qt.Open = static_cast<float>(candle.open);
        qt.High = static_cast<float>(candle.high); 
        qt.Low = static_cast<float>(candle.low);
        qt.Volume = static_cast<float>(candle.volume);
        qt.OpenInterest = static_cast<float>(candle.frequency);
        qt.AuxData1 = static_cast<float>(candle.value);
        qt.AuxData2 = static_cast<float>(candle.netforeign);
    }

    LogIfDebug("Finished. Returning final count: " + std::to_string(numToCopy));
    return static_cast<int>(numToCopy);
}