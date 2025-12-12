#pragma once
#include <string>

namespace RitelFetcher {
  bool fetch(const std::string& symbol, const std::string& brokers = "");
}