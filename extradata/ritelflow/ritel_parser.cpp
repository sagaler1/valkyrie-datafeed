#include "ritel_parser.h"
#include "ritel_store.h"
#include "data_point.h"
#include "plugin.h"
#include <simdjson.h>
#include <windows.h>
#include <string>
#include <map>
#include <ctime>
#include <iomanip>
#include <sstream>

static void LogRParser(const std::string& msg) {
  SYSTEMTIME t;
  GetLocalTime(&t);
  char buf[64];
  sprintf_s(buf, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
  OutputDebugStringA((std::string(buf) + "[RitelParser] " + msg + "\n").c_str());
}

// Helper konversi "YYYY-MM-DD" ke Unix timestamp
static time_t StringToUnix(std::string_view date_sv) {
  std::string date_str(date_sv);
  int y, m, d;
  if (sscanf_s(date_str.c_str(), "%d-%d-%d", &y, &m, &d) != 3) return 0;

  std::tm tm = {};
  tm.tm_year = y - 1900;
  tm.tm_mon = m - 1;
  tm.tm_mday = d;
  tm.tm_isdst = -1;
  return std::mktime(&tm);
}

// --- Core Logic
bool RitelParser::parseAndStore(const std::string& json, const std::string& symbol) {
  try {
    simdjson::ondemand::parser parser;
    simdjson::padded_string ps(json);
    auto doc = parser.iterate(ps);

    // Navigasi ke deep layer: data -> broker_chart_data[0] -> charts
    auto charts_array = doc["data"]["broker_chart_data"].at(0)["charts"].get_array();

    // AGGREGATOR: Map buat jumlahin value dari berbagai broker di tanggal yg sama
    // Key: Timestamp (detik), Value: Total Flow
    std::map<time_t, double> daily_totals;

    // 1. Loop setiap broker
    for (auto broker_entry : charts_array) {
      // Ambil array chart dari broker
      auto chart_points = broker_entry["chart"].get_array();

      // 2. Loop Setiap Hari di chart broker
      for (auto point : chart_points) {
        // Ambil Tanggal
        std::string_view date_sv = point["date"].get_string().value();
        time_t ts = StringToUnix(date_sv);
        if (ts == 0) continue;

        // Ambil Value RAW (String -> Double)
        // Struktur: point -> value -> raw
        std::string_view val_sv = point["value"]["raw"].get_string().value();
        double val = std::stod(std::string(val_sv));

        // 3. AKUMULASI (Summing)
        daily_totals[ts] += val;
      }
    }

    // 4. Konversi Map ke Vector<DataPoint> untuk disimpan
    std::vector<DataPoint> final_data;
    final_data.reserve(daily_totals.size());

    for (const auto& [ts, val] : daily_totals) {
      DataPoint dp;
      dp.ts = (DATE_TIME_INT)ts;
      dp.value = (float) val;
      final_data.push_back(dp);
    }

    if (!final_data.empty()) {
      RitelStore::set(symbol, final_data);
      LogRParser("Aggregated flow for " + symbol + ": " + std::to_string(final_data.size()) + " days.");
      return true;
    }
  }
  catch (const std::exception& e) {
    LogRParser("Error parsing: " + std::string(e.what()));
  }
  return false;
}