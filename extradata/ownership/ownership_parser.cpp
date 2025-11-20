#include "ownership_parser.h"
#include <simdjson.h>
#include <windows.h>
#include <string>         // Untuk std::string, std::stoll
#include <stdexcept>      // Untuk std::exception

// Fungsi logging kecil
static void LogParser(const std::string& msg) {
  SYSTEMTIME t;
  GetLocalTime(&t);
  char buf[64];
  sprintf_s(buf, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
  OutputDebugStringA((std::string(buf) + "[OwnershipParser] " + msg + "\n").c_str());
}

std::vector<DataPoint> OwnershipParser::parse(const std::string& json, const std::string& ownerName) {
  std::vector<DataPoint> out;
  
  // 1. Bungkus semua pakai try-catch
  try {
    simdjson::ondemand::parser parser;
    simdjson::padded_string ps(json);
    auto doc = parser.iterate(ps);

    auto legends = doc["data"]["legend"].get_array();

    for (auto legend : legends) {
      simdjson::ondemand::value val;
      auto error = legend["item_name"].get(val);
      if (error || val.type() != simdjson::ondemand::json_type::string) continue;
      
      std::string_view name_sv = val.get_string().value();
      std::string name(name_sv); // Konversi ke std::string biar gampang

      // 2. Cek nama "Corporation" atau "Perusahaan"
      bool name_match = false;
      if (ownerName == "Corporation" && name == "Perusahaan") {
        name_match = true;
      } else if (name == ownerName) {
        name_match = true;
      }

      if (name_match) {
        auto arr = legend["chart_data"].get_array();
        for (auto item : arr) {
          DataPoint p;
          
          // 3. Fix parsing "unix_date" dari STRING ke ANGKA
          auto ts_err = item["unix_date"].get(val);
          if (ts_err || val.type() != simdjson::ondemand::json_type::string) continue;
          
          // Ambil sebagai string_view, lalu konversi ke int64
          std::string_view ts_sv = val.get_string().value();
          p.ts = std::stoll(std::string(ts_sv)); // std::stoll = String to Long Long (int64)

          // Parsing value
          auto val_err = item["value"].get(val);
          if (val_err) continue;
          p.value = static_cast<float>(val.get_double().value());
          
          out.push_back(p);
        }
      }
    }
  } 
  catch (const simdjson::simdjson_error &e) {
    LogParser("[Extra Parser] SIMDJSON ERROR: " + std::string(e.what()) + ". JSON: " + json.substr(0, 200));
  } 
  catch (const std::exception &e) {
    // Catch error dari std::stoll kalau gagal
    LogParser("[Extra Parser] STD::EXCEPTION ERROR: " + std::string(e.what()));
  }

  return out;
}
