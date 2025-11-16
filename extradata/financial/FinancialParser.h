#pragma once
#include <string>

namespace FinancialParser {
  bool parseAndStore(const std::string& json, const std::string& symbol);   // Parse JSON dan langsung simpan ke Store
}