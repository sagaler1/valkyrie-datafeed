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
    
    // Header Info (dari LiveQuote / HTTP Snapshot)
    double prev_close = 0;
    double last_price = 0;
    double change = 0;
    double percent = 0;
    
    // The Orderbook Rows
    std::vector<OrderLevel> bids;
    std::vector<OrderLevel> offers;

    void clear() {
        symbol.clear();
        bids.clear();
        offers.clear();
    }
};

#endif // ORDERBOOK_DATA_H