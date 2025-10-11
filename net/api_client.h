#ifndef API_CLIENT_H
#define API_CLIENT_H

#include <string>
#include <vector>
#include <chrono>
#include "types.h" // <-- Pastikan file 'types.h' ada

// --- Function Declarations ---

// Fungsi utama untuk mengambil data historis
std::vector<Candle> fetchHistorical(const std::string& symbol, const std::string& from, const std::string& to);

// Deklarasi fungsi helper (hanya "janji", tidak ada isi)
std::string timePointToString(const std::chrono::system_clock::time_point& tp);

// NEW: Fungsi helper untuk melakukan GET request menggunakan WinHTTP
std::string WinHttpGetData(const std::string& url);

#endif // API_CLIENT_H

