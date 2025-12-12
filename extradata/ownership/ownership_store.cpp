#include "ownership_store.h"

std::mutex OwnershipStore::mtx;
std::map<std::string, std::map<std::string, std::vector<DataPoint>>> OwnershipStore::store;

void OwnershipStore::set(const std::string& symbol, const std::string& ownerType, const std::vector<DataPoint>& data) {
  std::lock_guard<std::mutex> lock(mtx);
  store[symbol][ownerType] = data;
}

std::vector<DataPoint> OwnershipStore::get(const std::string& symbol, const std::string& ownerType) {
  std::lock_guard<std::mutex> lock(mtx);

  if (store.count(symbol) && store[symbol].count(ownerType))
    return store[symbol][ownerType];

  return {};
}
