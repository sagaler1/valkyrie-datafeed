#ifndef TYPES_H
#define TYPES_H

#include <string>

// ---- Struct for a single bar/candle (from historical API)
// ---- Struct untuk satu bar/candle (dari API historis)
struct Candle {
  std::string date;
  double open = 0;
  double high = 0;
  double low = 0;
  double close = 0;
  double volume = 0;
  double frequency = 0;
  double value = 0;
  double netforeign = 0;
};

// ---- Struct for latest quote data from API (market closed)
// ---- Struct untuk data quote terakhir dari API (saat market tutup)
struct LatestQuote {
  std::string symbol;
  std::string name;
  double open = 0;
  double high = 0;
  double low = 0;
  double close = 0;
  double volume = 0;
  double value = 0;
  double frequency = 0;
  double netforeign = 0;
  double change = 0;
  double change_pct = 0;
  std::string last_update;
};

// ---- Struct for live feed data from websocket
// ---- Struct untuk data live feed dari websocket
struct LiveQuote {
  std::string symbol;
  double lastprice = 0.0;
  double previous = 0.0;
  double open = 0.0;
  double high = 0.0;
  double low = 0.0;
  double volume = 0.0;
  double value = 0.0;
  double frequency = 0.0;
  double netforeign = 0.0;
  double changeValue = 0.0;
  double changePercent = 0.0;
  std::string timestamp;
};

// ---- Struct untuk Metadata Simbol (dari API Emiten List)
struct SymbolInfo {
  std::string code;
  std::string name;
};

#endif // TYPES_H

