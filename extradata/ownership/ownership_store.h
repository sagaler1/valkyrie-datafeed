#pragma once
#include "plugin.h"
#include "data_point.h"
#include <string>
#include <vector>
#include <map>
#include <mutex>

class OwnershipStore {
public:
  static void set(const std::string& symbol, const std::string& ownerType, const std::vector<DataPoint>& data);
  static std::vector<DataPoint> get(const std::string& symbol, const std::string& ownerType);

private:
  static std::mutex mtx;
  static std::map<std::string, std::map<std::string, std::vector<DataPoint>>> store;
};
