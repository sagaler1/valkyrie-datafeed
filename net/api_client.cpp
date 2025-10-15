// api_client.cpp
#include "api_client.h"
#include "config.h"
#include <windows.h>
#include <vector>
#include <string>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <mutex>
#include <stdexcept>
#include <winhttp.h>

// ---- simdjson (ondemand)
#include <simdjson.h>
using namespace std::chrono;

// ---- Fungsi Logging & Helper
void LogApi(const std::string& msg) {
    SYSTEMTIME t;
    GetLocalTime(&t);
    char buf[64];
    sprintf_s(buf, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    OutputDebugStringA((std::string(buf) + msg + "\n").c_str());
}

// ---- WinHTTP Helper for GET request
std::string WinHttpGetData( const std::string& url) {
    std::string responseBody;
    HINTERNET hSession = NULL, hConnect = NULL, hRequest = NULL;

    // 1. Crack URL to obtain Host dan Path
    URL_COMPONENTS urlComp;
    ZeroMemory(&urlComp, sizeof(urlComp));
    urlComp.dwStructSize = sizeof(urlComp);
    
    // Buffer Allocation for Host & Path
    const int BUF_SIZE = 1024;
    wchar_t wsHostName[BUF_SIZE];
    wchar_t wsUrlPath[BUF_SIZE];
    
    urlComp.lpszHostName = wsHostName;
    urlComp.dwHostNameLength = BUF_SIZE;
    urlComp.lpszUrlPath = wsUrlPath;
    urlComp.dwUrlPathLength = BUF_SIZE;

    // WinHttpCrackUrl only accepts LPWSTR/LPCWSTR (Wide String), but our input is std::string (ANSI/UTF-8).
    // Since the URL only contains standard ASCII characters, we can use a simple conversion.
    // However, WinHttpCrackUrl originally accepts LPCTSTR, so on Windows we use WinHttpCrackUrlA/W.
    // We force WinHttpCrackUrlW with conversion:
    
    std::wstring wsUrl(url.begin(), url.end());     // Convert string to wstring (C++11)

    if (!WinHttpCrackUrl(wsUrl.c_str(), (DWORD)wsUrl.length(), 0, &urlComp)) {
        LogApi("[WinHTTP] ERROR: WinHttpCrackUrl failed.");
        return "";
    }

    // 2. WinHttpOpen - Open a session (use a clear User Agent)
    hSession = WinHttpOpen(L"ValkyrieDataFeed/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, 
                           WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (hSession) {
        // 3. WinHttpConnect - Connect to Host
        hConnect = WinHttpConnect(hSession, urlComp.lpszHostName, urlComp.nPort, 0);
    } else {
        LogApi("[WinHTTP] ERROR: WinHttpOpen failed.");
    }

    if (hConnect) {
        // 4. WinHttpOpenRequest - Open request (GET)
        DWORD dwFlags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
        
        hRequest = WinHttpOpenRequest(hConnect, L"GET", urlComp.lpszUrlPath, NULL, 
                                      WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, dwFlags);
    } else {
        LogApi("[WinHTTP] ERROR: WinHttpConnect failed.");
    }
    
    // Set timeout (20 seconds)
    DWORD dwTimeout = 20000; // 20000 ms
    if (hRequest) WinHttpSetOption(hRequest, WINHTTP_OPTION_CONNECT_TIMEOUT, &dwTimeout, sizeof(dwTimeout));
    if (hRequest) WinHttpSetOption(hRequest, WINHTTP_OPTION_RECEIVE_TIMEOUT, &dwTimeout, sizeof(dwTimeout));

    if (hRequest) {
        // 5. WinHttpSendRequest
        if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
            // 6. WinHttpReceiveResponse
            if (WinHttpReceiveResponse(hRequest, NULL)) {
                DWORD dwSize = 0;
                DWORD dwDownloaded = 0;
                LPSTR pszOutBuffer = nullptr;
                
                // 7. WinHttpReadData - Loop untuk membaca seluruh response
                do {
                    if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
                    if (dwSize == 0) break;

                    pszOutBuffer = new CHAR[dwSize + 1];
                    if (!pszOutBuffer) break;

                    if (!WinHttpReadData(hRequest, (LPVOID)pszOutBuffer, dwSize, &dwDownloaded)) {
                        delete[] pszOutBuffer;
                        break;
                    }
                    
                    if (dwDownloaded == 0) {
                        delete[] pszOutBuffer;
                        break;
                    }

                    responseBody.append(pszOutBuffer, dwDownloaded);
                    delete[] pszOutBuffer;
                    
                } while (dwSize > 0);
            } else {
                LogApi("[WinHTTP] ERROR: WinHttpReceiveResponse failed.");
            }
        } else {
            LogApi("[WinHTTP] ERROR: WinHttpSendRequest failed.");
        }
    } else {
        LogApi("[WinHTTP] ERROR: Could not open request handle.");
    }

    // 8. Cleanup
    if (hRequest) WinHttpCloseHandle(hRequest);
    if (hConnect) WinHttpCloseHandle(hConnect);
    if (hSession) WinHttpCloseHandle(hSession);
    
    return responseBody;

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

// --- FUNGSI UTAMA: fetchHistorical dengan WinHTTP & simdjson ondemand ---
// Retrieve all data from the API in one full call.
// The “from” and “to” parameters are sent directly to the backend endpoint.
std::vector<Candle> fetchHistorical(const std::string& symbol, const std::string& from, const std::string& to) {
    std::string host = Config::getInstance().getHost();

    std::vector<Candle> candles;
    std::string readBuffer;

    // Create API url (just use std::string)
    std::string url = host + "/api/amibroker/historical?"
        "symbol=" + symbol +
        "&from=" + from +
        "&to=" + to;

    auto t0 = high_resolution_clock::now();
    LogApi("[API_WinHTTP] Fetching " + symbol + " from " + from + " to " + to);

    readBuffer = WinHttpGetData(url);
    if (readBuffer.empty()) {
        LogApi("[API_WinHTTP] Error: Failed to retrieve data or empty response.");
        return candles;
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
                float fb = 0.0f, fs = 0.0f;

                auto obj = item.get_object();
                for (auto field : obj) {
                    std::string_view key = field.unescaped_key();
                    auto val = field.value();

                    if (key == "date") {
                        std::string_view sv = val.get_string().value();
                        c.date.assign(sv.data(), sv.size());
                    } 
                    else if (key == "open") {
                        c.open = static_cast<float>(val.get_double().value());
                    }
                    else if (key == "high") {
                        c.high = static_cast<float>(val.get_double().value());
                    }
                    else if (key == "low") {
                        c.low = static_cast<float>(val.get_double().value());
                    }
                    else if (key == "close") {
                        c.close = static_cast<float>(val.get_double().value());
                    }
                    else if (key == "volume") {
                        c.volume = static_cast<float>(val.get_double().value());
                    }
                    else if (key == "frequency") {
                        c.frequency = static_cast<float>(val.get_double().value());
                    }
                    else if (key == "value") {
                        c.value = static_cast<float>(val.get_double().value());
                    }
                    else if (key == "foreignbuy") {
                        fb = static_cast<float>(val.get_double().value());
                    }
                    else if (key == "foreignsell") {
                        fs = static_cast<float>(val.get_double().value());
                    }
                }

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
