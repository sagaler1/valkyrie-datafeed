#ifndef ORDERBOOK_DATA_H
#define ORDERBOOK_DATA_H

#include <string>
#include <vector>

struct OrderLevel {
  long price = 0;
  long queue = 0;
  long volume = 0;
};

struct OrderbookSnapshot {
  std::string symbol;
  std::string company_type;
  std::string company_name;
  
  // Header Info (dari LiveQuote / HTTP Snapshot)
  double prev_close = 0;
  double last_price = 0;
  double change = 0;
  double open = 0;
  double high = 0;
  double low = 0; 
  double percent = 0;
  double volume = 0;
  double value = 0; 
  double frequency = 0;

  long long total_bid_vol = 0;   // Total Lot Bid
  long total_bid_freq = 0;
  long long total_offer_vol = 0; // Total Lot Offer
  long total_offer_freq = 0;
  
  // The Orderbook Rows
  std::vector<OrderLevel> bids;
  std::vector<OrderLevel> offers;

  void clear() {
    symbol.clear();
    bids.clear();
    offers.clear();
    last_price = prev_close = open = high = low = change = percent = volume = value = frequency = 0;
    total_bid_vol = total_bid_freq = 0;
    total_offer_vol = total_offer_freq = 0;
  }
};

#endif // ORDERBOOK_DATA_H