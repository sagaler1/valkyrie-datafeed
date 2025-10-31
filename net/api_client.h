#ifndef API_CLIENT_H
#define API_CLIENT_H

#include <string>
#include <vector>
#include <chrono>
#include "types.h" // <-- Pastikan file 'types.h' ada
#include <map>

// --- Function Declarations ---

// Fungsi utama untuk mengambil data historis
std::vector<Candle> fetchHistorical(const std::string& symbol, const std::string& from, const std::string& to);

// Deklarasi fungsi helper (hanya "janji", tidak ada isi)
std::string timePointToString(const std::chrono::system_clock::time_point& tp);

// Fungsi helper untuk melakukan GET request menggunakan WinHTTP
std::string WinHttpGetData(const std::string& url);

// Fungsi untuk mengambil daftar semua simbol yang terdaftar di bursa
std::vector<SymbolInfo> fetchSymbolList();

#endif // API_CLIENT_H

