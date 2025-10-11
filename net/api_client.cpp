// api_client.cpp
#include "api_client.h"
#include "config.h"
#include <windows.h>
#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <mutex>
#include <stdexcept>
#include <curl/curl.h>

// simdjson (ondemand)
#include <simdjson.h>
using namespace std::chrono;

// ---- Global reusable CURL handle 
static CURL* g_curl = nullptr;
static std::mutex g_curl_mutex;

// ---- Init Once
void initCurlGlobal() {
    static bool initialized = false;
    if (!initialized) {
        curl_global_init(CURL_GLOBAL_ALL);
        g_curl = curl_easy_init();

        // Basic persistent options
        curl_easy_setopt(g_curl, CURLOPT_TCP_KEEPALIVE, 1L);
        curl_easy_setopt(g_curl, CURLOPT_TCP_KEEPIDLE, 30L);
        curl_easy_setopt(g_curl, CURLOPT_TCP_KEEPINTVL, 15L);
        curl_easy_setopt(g_curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(g_curl, CURLOPT_MAXREDIRS, 3L);
        curl_easy_setopt(g_curl, CURLOPT_FRESH_CONNECT, 0L);
        curl_easy_setopt(g_curl, CURLOPT_FORBID_REUSE, 0L);
        curl_easy_setopt(g_curl, CURLOPT_NOSIGNAL, 1L);

        initialized = true;
    }
}

// ---- Clean up curl saat exit
void cleanupCurlGlobal() {
    if (g_curl) {
        curl_easy_cleanup(g_curl);
        g_curl = nullptr;
    }
    curl_global_cleanup();
}

// ---- Fungsi Logging & Helper
void LogApi(const std::string& msg) {
    SYSTEMTIME t;
    GetLocalTime(&t);
    char buf[64];
    sprintf_s(buf, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    OutputDebugStringA((std::string(buf) + msg + "\n").c_str());
}

// ---- Helper Functions untuk Manipulasi Tanggal 

static std::chrono::system_clock::time_point stringToTimePoint(const std::string& date_str) {
    std::tm tm = {};
    std::stringstream ss(date_str);
    ss >> std::get_time(&tm, "%Y-%m-%d");
    // mktime uses localtime; consistent with earlier code
    return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

std::string timePointToString(const std::chrono::system_clock::time_point& tp) {
    std::time_t tt = std::chrono::system_clock::to_time_t(tp);
    std::tm tm = *std::localtime(&tt);
    std::stringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%d");
    return ss.str();
}

// --- CURL callback ---
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    std::string* s = reinterpret_cast<std::string*>(userp);
    s->append(reinterpret_cast<char*>(contents), size * nmemb);
    return size * nmemb;
}

// --- Utility: estimate days between two YYYY-MM-DD strings, + small margin ---
static size_t estimate_days_between(const std::string& from, const std::string& to) {
    try {
        auto a = stringToTimePoint(from);
        auto b = stringToTimePoint(to);
        auto diff = b - a;
        auto days = std::chrono::duration_cast<std::chrono::hours>(diff).count() / 24;
        if (days < 0) days = 0;
        // add small headroom for weekends/holidays or extra items
        size_t estimate = static_cast<size_t>(days) + 8;
        // clamp to reasonable max to avoid insane reserve
        if (estimate > 100000) estimate = 100000;
        return estimate;
    } catch (...) {
        return 1024; // fallback guess
    }
}

// --- FUNGSI UTAMA: fetchHistorical dengan simdjson ondemand ---
// Ambil semua data dari API dalam satu panggilan penuh.
// Parameter "from" dan "to" dikirim langsung ke endpoint backend.
// Fungsi fetchHistorical dengan connection reuse
std::vector<Candle> fetchHistorical(const std::string& symbol, const std::string& from, const std::string& to) {
    initCurlGlobal(); // pastikan handle global sudah siap
    std::string host = Config::getInstance().getHost();

    std::vector<Candle> candles;
    std::string readBuffer;

    // Buat URL API
    std::string url;
    {
        CURL* esc = curl_easy_init();
        if (esc) {
            char* sym_enc = curl_easy_escape(esc, symbol.c_str(), 0);
            char* from_enc = curl_easy_escape(esc, from.c_str(), 0);
            char* to_enc   = curl_easy_escape(esc, to.c_str(), 0);

            url = host + "/api/amibroker/historical?"
                "symbol=" + std::string(sym_enc ? sym_enc : symbol) +
                "&from=" + std::string(from_enc ? from_enc : from) +
                "&to=" + std::string(to_enc ? to_enc : to);

            if (sym_enc) curl_free(sym_enc);
            if (from_enc) curl_free(from_enc);
            if (to_enc) curl_free(to_enc);
            curl_easy_cleanup(esc);
        } else {
            url = host + "/api/amibroker/historical?"
                "symbol=" + symbol +
                "&from=" + from +
                "&to=" + to;
        }
    }

    auto t0 = high_resolution_clock::now();
    LogApi("[API_simdjson] Fetching " + symbol + " from " + from + " to " + to);

    {
        std::lock_guard<std::mutex> lock(g_curl_mutex);
        curl_easy_setopt(g_curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(g_curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(g_curl, CURLOPT_WRITEDATA, &readBuffer);
        curl_easy_setopt(g_curl, CURLOPT_TIMEOUT, 20L);

        CURLcode res = curl_easy_perform(g_curl);
        if (res != CURLE_OK) {
            LogApi("[API_curl] Error: " + std::string(curl_easy_strerror(res)));
            return candles;
        }
    }

    auto t_fetch = high_resolution_clock::now();

    try {
        size_t estimated = estimate_days_between(from, to);
        candles.reserve(std::min<size_t>(estimated, 200000));

        simdjson::ondemand::parser parser;
        simdjson::padded_string ps(readBuffer);
        auto doc = parser.iterate(ps);

        auto data_field = doc.find_field("data");
        simdjson::ondemand::value chartbit_val;
        if (!data_field.error()) {
            auto data_obj = data_field.value();
            auto cb = data_obj.find_field("chartbit");
            if (cb.error()) return candles;
            chartbit_val = cb.value();
        } else {
            auto cb = doc.find_field("chartbit");
            if (cb.error()) return candles;
            chartbit_val = cb.value();
        }

        auto arr = chartbit_val.get_array();
        for (simdjson::ondemand::value item : arr) {
            try {
                Candle c;
                auto date_res = item.find_field("date");
                if (!date_res.error()) {
                    std::string_view sv = date_res.value().get_string().value();
                    c.date.assign(sv.data(), sv.size());
                }
                auto open_res = item.find_field("open");
                c.open = (!open_res.error()) ? static_cast<float>(open_res.value().get_double().value()) : 0.0f;
                auto high_res = item.find_field("high");
                c.high = (!high_res.error()) ? static_cast<float>(high_res.value().get_double().value()) : 0.0f;
                auto low_res = item.find_field("low");
                c.low = (!low_res.error()) ? static_cast<float>(low_res.value().get_double().value()) : 0.0f;
                auto close_res = item.find_field("close");
                c.close = (!close_res.error()) ? static_cast<float>(close_res.value().get_double().value()) : 0.0f;
                auto vol_res = item.find_field("volume");
                c.volume = (!vol_res.error()) ? static_cast<float>(vol_res.value().get_double().value()) : 0.0f;
                auto freq_res = item.find_field("frequency");
                c.frequency = (!freq_res.error()) ? static_cast<float>(freq_res.value().get_double().value()) : 0.0f;
                auto value_res = item.find_field("value");
                c.value = (!value_res.error()) ? static_cast<float>(value_res.value().get_double().value()) : 0.0f;
                float fb = 0.0f, fs = 0.0f;
                auto fb_res = item.find_field("foreignbuy");
                if (!fb_res.error()) fb = static_cast<float>(fb_res.value().get_double().value());
                auto fs_res = item.find_field("foreignsell");
                if (!fs_res.error()) fs = static_cast<float>(fs_res.value().get_double().value());
                c.netforeign = fb - fs;
                candles.emplace_back(std::move(c));
            } catch (const simdjson::simdjson_error&) {
                continue;
            }
        }

        auto t_parse = high_resolution_clock::now();
        duration<double, std::milli> fetch_ms = t_fetch - t0;
        duration<double, std::milli> parse_ms = t_parse - t_fetch;
        LogApi("[API_simdjson] Fetched in " + std::to_string(fetch_ms.count()) +
            " ms, parsed " + std::to_string(candles.size()) +
            " items in " + std::to_string(parse_ms.count()) +
            " ms (total " + std::to_string((fetch_ms + parse_ms).count()) + " ms)");

    } catch (const std::exception &e) {
        LogApi(std::string("[API_simdjson] Exception: ") + e.what());
        return candles;
    }

    return candles;
}
