#include "data_store.h"
#include <algorithm>
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

  // Gunakan std::map untuk menjaga urutan tanggal dan update otomatis
  std::map<std::string, Candle> merged_map;

  // Selalu ambil referensi ke vector (akan buat entry kosong bila belum ada)
  auto& existingVec = m_historicalData[symbol];
  for (const auto& old_candle : existingVec) {
    merged_map[old_candle.date] = old_candle;
  }

  for (const auto& new_candle : new_candles) {
    merged_map[new_candle.date] = new_candle;
  }

  std::vector<Candle> final_candles;
  final_candles.reserve(merged_map.size());
  for (const auto& pair : merged_map) {
    final_candles.push_back(pair.second);
  }

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
  if (!feed.has_stock_data) return;
  const auto& s = feed.stock_data;
  std::string symbol = s.symbol;

  std::lock_guard<std::mutex> lock(m_mtx);
  LiveQuote& q = m_liveQuotes[symbol];

  q.symbol = symbol;
  q.lastprice = s.close;
  q.previous = s.previous;
  q.open = s.open;
  q.high = s.high;
  q.low = s.low;
  q.volume = s.volume;
  q.value = s.value;
  q.frequency = s.frequency;
  q.timestamp = s.date;
  q.netforeign = s.foreignbuy - s.foreignsell;

  if (s.has_change) {
    q.changeValue = s.change.value;
    q.changePercent = s.change.percentage;
  }

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

  if (!m_liveQuotes.count(symbol)) {
    return;
  }

  auto& candles = m_historicalData[symbol]; // otomatis buat entry kosong
  auto& live = m_liveQuotes.at(symbol);

  auto now = std::chrono::system_clock::now();
  auto in_time_t = std::chrono::system_clock::to_time_t(now);
  std::stringstream ss;
  std::tm buf;
  localtime_s(&buf, &in_time_t);
  ss << std::put_time(&buf, "%Y-%m-%d");
  std::string today_date_str = ss.str();

  if (!candles.empty() && candles.back().date == today_date_str) {
    Candle& lastCandle = candles.back();
    lastCandle.close = live.lastprice;
    lastCandle.high = std::max(lastCandle.high, live.high);
    lastCandle.low = std::min(lastCandle.low, live.low);
    lastCandle.volume = live.volume;
    lastCandle.value = live.value;
    lastCandle.frequency = live.frequency;
    lastCandle.netforeign = live.netforeign;
  } else {
    Candle newCandle;
    newCandle.date = today_date_str;
    newCandle.open = live.open;
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

void DataStore::updateEodBar(const std::string& symbol, const Candle& candle) {
  // Versi baru: lebih aman dan idempotent
  mergeHistorical(symbol, { candle });
}
