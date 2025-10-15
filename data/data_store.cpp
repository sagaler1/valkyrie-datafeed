#include "data_store.h"
#include <algorithm>                    // Diperlukan untuk std::max/min
#include <map>
#include <chrono>
#include <sstream>
#include <iomanip>

void DataStore::setHistorical(const std::string& symbol, const std::vector<Candle>& candles) {
    std::lock_guard<std::mutex> lock(m_mtx);
    m_historicalData[symbol] = candles;
}

void DataStore::mergeHistorical(const std::string& symbol, const std::vector<Candle>& new_candles) {
    std::lock_guard<std::mutex> lock(m_mtx);

    if (new_candles.empty()) {
        return; // Tidak ada yang perlu di-merge
    }

    // Gunakan std::map untuk secara otomatis menangani data unik berdasarkan tanggal
    // dan juga mengurutkannya secara otomatis.
    std::map<std::string, Candle> merged_map;

    // 1. Masukkan semua data lama (yang ada di cache) ke dalam map
    if (m_historicalData.count(symbol)) {
        for (const auto& old_candle : m_historicalData.at(symbol)) {
            merged_map[old_candle.date] = old_candle;
        }
    }

    // 2. Masukkan semua data baru ke dalam map.
    // Jika ada tanggal yang sama, data lama akan otomatis ditimpa (di-update).
    for (const auto& new_candle : new_candles) {
        merged_map[new_candle.date] = new_candle;
    }

    // 3. Konversi kembali map yang sudah ter-merge dan terurut ke dalam vector
    std::vector<Candle> final_candles;
    final_candles.reserve(merged_map.size());
    for (const auto& pair : merged_map) {
        final_candles.push_back(pair.second);
    }

    // 4. Simpan hasil akhirnya kembali ke cache utama kita
    m_historicalData[symbol] = final_candles;
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
    if (!m_historicalData.count(symbol) || !m_liveQuotes.count(symbol)) {
        return;         // Keluar jika alah satu data tidak ada
    }

    auto& candles = m_historicalData.at(symbol);
    auto& live = m_liveQuotes.at(symbol);

    if (candles.empty()) {
        return;         // Keluar jika tidak ada data historis sama sekali
    }

    // ---- NEW LOGIC: SADAR TANGGAL ----
    
    // 1. Dapatkan tanggal hari ini YYYY-MM-DD
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    std::tm buf;
    localtime_s(&buf, &in_time_t);      // localtime_s is thread-safe on Windows
    ss << std::put_time(&buf, "%Y-%m-%d");
    std::string today_date_str = ss.str();

    Candle& lastCandle = candles.back();

    // 2. bandingkan tanggal bar terakhir dengan tanggal hari ini
    if (lastCandle.date == today_date_str) {
        // KASUS A. Tick untuk hari yang sama. Cukup UPDATE bar terakhir.
        lastCandle.close = live.lastprice;
        lastCandle.high = std::max(lastCandle.high, live.high);
        lastCandle.low = std::min(lastCandle.low, live.low);
        lastCandle.volume = live.volume;
        lastCandle.value = live.value;
        lastCandle.frequency = live.frequency;
        lastCandle.netforeign = live.netforeign;
    } else {
        // KASUS B: Ini tick pertama untuk HARI BARU. Buat bar BARU.
        Candle newCandle;
        newCandle.date = today_date_str;
        newCandle.open = live.open; // Open hari ini adalah open dari live feed
        newCandle.high = live.high;
        newCandle.low = live.low;
        newCandle.close = live.lastprice;
        newCandle.volume = live.volume;
        newCandle.value = live.value;
        newCandle.frequency = live.frequency;
        newCandle.netforeign = live.netforeign;
        candles.push_back(newCandle);
    }
}