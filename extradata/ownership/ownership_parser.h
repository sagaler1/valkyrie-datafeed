#pragma once
#include <string>
#include <vector>
#include "ownership_store.h"
#include "data_point.h"

namespace OwnershipParser {
  std::vector<DataPoint> parse(const std::string& json, const std::string& ownerName);
}
