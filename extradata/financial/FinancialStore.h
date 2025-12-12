#pragma once
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include "data_point.h" // <-- Struct DataPoint

class FinancialStore {
public:
  static void set(const std::string& symbol, int fitem_id, const std::vector<DataPoint>& data);       // Set data per item_id
  static std::vector<DataPoint> get(const std::string& symbol, int fitem_id);                         // Get data per item_id

private:
  static std::mutex mtx;
  static std::map<std::string, std::map<int, std::vector<DataPoint>>> store;                          // Peta data: Symbol -> item_id -> Vector Data
};