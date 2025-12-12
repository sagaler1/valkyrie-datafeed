#pragma once
#include <string>

namespace FinancialFetcher {
  bool fetch(const std::string& symbol);    // Cukup 1 fungsi. Dia akan fetch SEMUA metric buat 1 simbol
}