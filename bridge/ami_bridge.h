#pragma once

#include "plugin.h"       // Struct Quotation dan LPCTSTR
#include "types.h"        // Struct Candle
#include "data_point.h"
#include <mutex>
#include <condition_variable>
#include <string>
#include <vector>

// 1. Tipe Tugas
enum class FetchTaskType {
    GET_CANDLES,
    GET_OWNERSHIP_INDIV,
    GET_OWNERSHIP_CORP,
    GET_FINANCIALS,
    GET_RITEL_FLOW,
    GET_BROKER_FLOW
};

// 2. Struct Tugas Generik
struct FetchTask {
  FetchTaskType type;
  std::string symbol;
  
  // ----- Khusus untuk GET_CANDLES
  std::string from_date;
  std::string to_date;
  std::vector<Candle> preload;

  // ---- Param lain 
  std::string extra_param;
};

// ---- Worker Thread 2
extern std::mutex g_fetchQueueMtx;
extern std::condition_variable g_fetchQueueCV;

int GetQuotesEx_Bridge(LPCTSTR pszTicker, int nPeriodicity, int nLastValid, int nSize, struct Quotation* pQuotes);
void ProcessFetchQueue();
bool QueueFetchTask(FetchTask task);  // Tambah tugas

