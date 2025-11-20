#pragma once
#include <vector>
#include <map>
#include <string>
#include <mutex>
#include "data_point.h"

class RitelStore {
public:
  static void set(const std::string& symbol, const std::vector<DataPoint>& data);
  static std::vector<DataPoint> get(const std::string& symbol);

private:
  static std::mutex mtx;
  static std::map<std::string, std::vector<DataPoint>> store;
};
