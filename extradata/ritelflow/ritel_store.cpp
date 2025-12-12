#include "ritel_store.h"

std::mutex RitelStore::mtx;
std::map<std::string, std::vector<DataPoint>> RitelStore::store;

void RitelStore::set(const std::string& symbol, const std::vector<DataPoint>& data) {
  std::lock_guard<std::mutex> lock(mtx);
  store[symbol] = data;
}

std::vector<DataPoint> RitelStore::get(const std::string& symbol) {
  std::lock_guard<std::mutex> lock(mtx);
  if (store.count(symbol)) {
    return store[symbol];
  }
  return {};
}
