#ifndef DATA_STORE_H
#define DATA_STORE_H

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include "types.h"
#include "feed.pb.h" // Diperlukan untuk StockFeed

class DataStore {
private:
    std::map<std::string, std::vector<Candle>> m_historicalData;
    std::map<std::string, LiveQuote> m_liveQuotes;
    std::mutex m_mtx;

public:
    // Untuk data historis dari API
    void setHistorical(const std::string& symbol, const std::vector<Candle>& candles);

    // NEW: Fungsi cerdas untuk menggabungkan data baru dengan cache yang ada
    void mergeHistorical(const std::string& symbol, const std::vector<Candle>& new_candles);

    std::vector<Candle> getHistorical(const std::string& symbol);
    bool hasHistorical(const std::string& symbol);

    // Untuk data live dari WebSocket
    void updateLiveQuote(const StockFeed& feed);
    LiveQuote getLiveQuote(const std::string& symbol);

    // Untuk menggabungkan data live ke bar historis terakhir
    void mergeLiveToHistorical(const std::string& symbol);
};

extern DataStore gDataStore;

#endif // DATA_STORE_H