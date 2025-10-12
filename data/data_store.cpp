#include "data_store.h"
#include <algorithm>                    // Diperlukan untuk std::max/min

void DataStore::setHistorical(const std::string& symbol, const std::vector<Candle>& candles) {
    std::lock_guard<std::mutex> lock(m_mtx);
    m_historicalData[symbol] = candles;
}

std::vector<Candle> DataStore::getHistorical(const std::string& symbol) {
    std::lock_guard<std::mutex> lock(m_mtx);
    if (m_historicalData.count(symbol)) {
        return m_historicalData[symbol];
    }
    return {};
}

bool DataStore::hasHistorical(const std::string& symbol) {
    std::lock_guard<std::mutex> lock(m_mtx);
    return m_historicalData.count(symbol) > 0;
}

void DataStore::updateLiveQuote(const StockFeed& feed) {
    // Parameter 'feed' sekarang adalah struct Nanopb
    if (!feed.has_stock_data) return;
    const auto& s = feed.stock_data;      // 's' adalah referensi ke struct StockData
    std::string symbol = s.symbol;

    std::lock_guard<std::mutex> lock(m_mtx);
    LiveQuote& q = m_liveQuotes[symbol];    // Get reference to update
    
    q.symbol = symbol;
    q.lastprice = s.close;                  // Di proto baru, lastprice adalah 'close'
    q.previous = s.previous;
    q.open = s.open;
    q.high = s.high;
    q.low = s.low;
    q.volume = s.volume;
    q.value = s.value;
    q.frequency = s.frequency;
    q.timestamp = s.date;
    q.netforeign = s.foreignbuy - s.foreignsell;

    // Cek flag 'has_change' untuk sub-pesan PriceChange
    if (s.has_change) {
        q.changeValue = s.change.value;
        q.changePercent = s.change.percentage;
    }

    // Proto baru tidak punya field 'previous'. Kita bisa hitung manual.
    // Previous = Close - Change
    q.previous = s.close - q.changeValue;
}

LiveQuote DataStore::getLiveQuote(const std::string& symbol) {
    std::lock_guard<std::mutex> lock(m_mtx);
    if (m_liveQuotes.count(symbol)) {
        return m_liveQuotes[symbol];
    }
    return {};
}

void DataStore::mergeLiveToHistorical(const std::string& symbol) {
    std::lock_guard<std::mutex> lock(m_mtx);
    if (m_historicalData.count(symbol) && m_liveQuotes.count(symbol)) {
        auto& candles = m_historicalData[symbol];
        auto& live = m_liveQuotes[symbol];

        if (!candles.empty()) {
            // Update bar terakhir dengan data live
            Candle& lastCandle = candles.back();
            lastCandle.close = live.lastprice;
            lastCandle.high = std::max(lastCandle.high, live.high);
            lastCandle.low = std::min(lastCandle.low, live.low);
            lastCandle.volume = live.volume;
            lastCandle.value = live.value;
            lastCandle.frequency = live.frequency;
            lastCandle.netforeign = live.netforeign;
        }
    }
}

