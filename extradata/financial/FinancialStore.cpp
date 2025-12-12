#include "FinancialStore.h"

std::mutex FinancialStore::mtx;
std::map<std::string, std::map<int, std::vector<DataPoint>>> FinancialStore::store;

void FinancialStore::set(const std::string& symbol, int fitem_id, const std::vector<DataPoint>& data) {
  std::lock_guard<std::mutex> lock(mtx);
  store[symbol][fitem_id] = data;
}

std::vector<DataPoint> FinancialStore::get(const std::string& symbol, int fitem_id) {
  std::lock_guard<std::mutex> lock(mtx);
  if (store.count(symbol) && store[symbol].count(fitem_id))
    return store[symbol][fitem_id];
  return {};
}