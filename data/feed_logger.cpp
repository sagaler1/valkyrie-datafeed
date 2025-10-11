#include "feed_logger.h"
#include <fstream>
#include <string>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <map>
#include <mutex>
#include <iostream>

namespace FeedLogger {

    // Mutex untuk melindungi akses ke file DAN ke map di bawah ini.
    // Ini krusial untuk thread-safety.
    std::mutex logger_mtx;
    
    // Map untuk menyimpan state terakhir per simbol
    static std::map<std::string, double> lastLoggedPrice;
    static std::map<std::string, std::chrono::steady_clock::time_point> lastLoggedTime;
    
    // Tentukan interval "snapshot". Log akan ditulis jika sudah lebih dari 5 detik,
    // meskipun harga tidak berubah. Bisa diubah sesuai kebutuhan.
    const auto LOG_INTERVAL = std::chrono::seconds(60);

    void append(const LiveQuote& q, bool useThrottling) {
        // Kunci mutex segera saat masuk fungsi untuk memastikan operasi atomik.
        std::lock_guard<std::mutex> lock(logger_mtx);

        if (useThrottling) {
            auto now = std::chrono::steady_clock::now(); // Gunakan steady_clock untuk interval

            // Cek apakah kita sudah pernah log simbol ini sebelumnya
            if (lastLoggedTime.count(q.symbol)) {
                
                // Kondisi 1: Apakah harga berubah?
                bool priceChanged = (q.lastprice != lastLoggedPrice.at(q.symbol));
                
                // Kondisi 2: Apakah interval waktu sudah terlewati?
                auto timeSinceLastLog = now - lastLoggedTime.at(q.symbol);
                bool intervalElapsed = (timeSinceLastLog > LOG_INTERVAL);

                // Jika harga TIDAK berubah DAN interval waktu BELUM lewat, maka SKIP.
                if (!priceChanged && !intervalElapsed) {
                    return; // Keluar dari fungsi, tidak ada yang di-log.
                }
            }
            // Jika ini pertama kali simbol di-log, maka lanjutkan eksekusi.
            
            // Jika kita lolos dari filter, update state terakhir untuk simbol ini.
            lastLoggedPrice[q.symbol] = q.lastprice;
            lastLoggedTime[q.symbol] = now;
        }

        // --- Bagian ini adalah logika penulisan file, dieksekusi jika tidak di-skip ---
        try {
            // Gunakan system_clock untuk mendapatkan tanggal dan waktu kalender
            auto now_sys = std::chrono::system_clock::now();
            auto in_time_t = std::chrono::system_clock::to_time_t(now_sys);

            std::stringstream ss;
            // Gunakan localtime_s di Windows untuk thread-safety
            #ifdef _WIN32
                std::tm buf;
                localtime_s(&buf, &in_time_t);
                ss << std::put_time(&buf, "%Y-%m-%d");
            #else
                ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d");
            #endif
            
            std::string date_str = ss.str();
            
            std::string dir = "logs";
            std::filesystem::create_directory(dir);
            std::string filepath = dir + "/" + date_str + "_feed.csv";
            
            bool file_exists = std::filesystem::exists(filepath);

            std::ofstream file(filepath, std::ios_base::app);
            if (!file.is_open()) {
                std::cerr << "Error: Could not open log file: " << filepath << std::endl;
                return;
            }

            if (!file_exists) {
                file << "Timestamp,Symbol,LastPrice,Previous,Open,High,Low,Volume,Value,Frequency,ChangeValue,ChangePercent\n";
            }

            file << q.timestamp << ","
                 << q.symbol << ","
                 << q.lastprice << ","
                 << q.previous << ","
                 << q.open << ","
                 << q.high << ","
                 << q.low << ","
                 << q.volume << ","
                 << q.value << ","
                 << q.frequency << ","
                 << q.changeValue << ","
                 << q.changePercent << "\n";

        } catch (const std::filesystem::filesystem_error& e) {
            std::cerr << "Filesystem Error in Logger: " << e.what() << std::endl;
        }
    }
}

