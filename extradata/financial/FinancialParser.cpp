#include <windows.h>
#include <string>
#include <simdjson.h>
#include "FinancialParser.h"
#include "FinancialStore.h"
#include "data_point.h"
#include "plugin.h"           // <-- Untuk EMPTY_VAL


// ---- Fungsi Logging
static void LogFinParser(const std::string& msg) {
  SYSTEMTIME t;
  GetLocalTime(&t);
  char buf[64];
  sprintf_s(buf, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
  OutputDebugStringA((std::string(buf) + "[FinancialParser] " + msg + "\n").c_str());
}

bool FinancialParser::parseAndStore(const std::string& json, const std::string& symbol) {
  try {
    simdjson::ondemand::parser parser;
    simdjson::padded_string ps(json);
    auto doc = parser.iterate(ps);
    auto ratios = doc["data"].at(0)["ratios"].get_array();                    // Struktur: data -> [0] -> "ratios"
    
    int items_parsed = 0;
    for (auto ratio : ratios) {
      int fitem_id = (int)ratio["item_id"].get_int64();                       // 1. Get item_id
      
      std::vector<DataPoint> data_points;
      auto chart_data = ratio["chart_data"].get_array();

      for (auto item : chart_data) {
        DataPoint p;
        p.ts = item["date"].get_int64();                                      // 2. Ambil timestamp (Sudah angka Unix, bukan string)
        
        simdjson::ondemand::value val;                                        // 3. Ambil value (penting: cek kalo "value" nya null)
        auto err = item["value"].get(val);

        if (!err && val.type() == simdjson::ondemand::json_type::number) {
          p.value = static_cast<float>(val.get_double());
        } else {
          p.value = EMPTY_VAL;                                                // ---- Kalo "value": null atau bukan angka, pakai EMPTY_VAL
        }
        data_points.push_back(p);
      }
      
      if (!data_points.empty()) {                                             // 4. Simpan data vector ke Store
        FinancialStore::set(symbol, fitem_id, data_points);
        items_parsed++;
      }
    }
    LogFinParser("Parsed " + std::to_string(items_parsed) + " financial items for " + symbol);
    return items_parsed > 0;
  } 
  catch (const simdjson::simdjson_error &e) {
    LogFinParser("SIMDJSON ERROR: " + std::string(e.what()));
    return false;
  } 
  catch (const std::exception &e) {
    LogFinParser("STD::EXCEPTION ERROR: " + std::string(e.what()));
    return false;
  }
}