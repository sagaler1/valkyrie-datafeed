#include <string>
#include <ctime>
#include <map>
#include <unordered_map>
#include "extra_dispatcher.h"
#include "ownership_store.h"
#include "FinancialStore.h"
#include "ami_bridge.h"

// ---- Helper untuk konversi format tanggal AmiBroker (PackDate) ke Unix Timestamp (time_t / detik)
static DATE_TIME_INT AmiDateToUnix(DATE_TIME_INT amiDate) {
  AmiDate dateUnion;
  dateUnion.Date = amiDate;

  std::tm tm = {};
  tm.tm_year = dateUnion.PackDate.Year - 1900;
  tm.tm_mon = dateUnion.PackDate.Month - 1;       // tm_mon itu 0-11
  tm.tm_mday = dateUnion.PackDate.Day;
  tm.tm_isdst = -1;                               // Supaya mktime otomatis deteksi DST

  // mktime ngasih kita Unix timestamp (tipe time_t)
  std::time_t unix_ts = std::mktime(&tm);
  
  // Cast ke unsigned __int64 (tipe DATE_TIME_INT) -> aman
  return (DATE_TIME_INT)unix_ts;
}

// ---- MAPPING AFL STRING KE FITEM_ID
// ---- Lihat manual financial_metrics!
static std::map<std::string, int> g_financialMetricsMap = {
  // Nama di AFL : item_id
  {"SHAREHOLDERS_NUM", 21334},
  {"FREE_FLOAT", 21334}
};

static void fillOwnership(const std::string& symbol, const std::string& type, ExtraData* pData, float* outArr) {
  auto data = OwnershipStore::get(symbol, type);

  if (data.empty()) {
    // 1. Buat tugas baru
    FetchTask task;
    task.symbol = symbol;
    if (type == "Individual") {
      task.type = FetchTaskType::GET_OWNERSHIP_INDIV;
    } else if (type == "Perusahaan") {
      task.type = FetchTaskType::GET_OWNERSHIP_CORP;
    } else {
      // Tipe tidak dikenal, no queue
      for (int i = 0; i < pData->nArraySize; i++) outArr[i] = EMPTY_VAL;
      return;
    }

    // 2. Masukkan ke antrian
    QueueFetchTask(std::move(task));

    // 3. Langsung keluar (Non-blocking)
    for (int i = 0; i < pData->nArraySize; i++) {
      outArr[i] = EMPTY_VAL;
    }
    return;
  }
  
  // ---- LOGIKA FORWARD-FILL (Bulanan/Kuartalan)
  size_t data_idx = 0;
  float current_value = EMPTY_VAL; 
  for (int i = 0; i < pData->nArraySize; i++) {
    DATE_TIME_INT barTs_Unix = AmiDateToUnix(pData->anTimestamps[i]);
    while (data_idx < data.size() && data[data_idx].ts <= barTs_Unix) {
      if(data[data_idx].value != EMPTY_VAL) {
        current_value = data[data_idx].value;
      }
      data_idx++;
    }
    outArr[i] = current_value;
  }
}

// ---- Filler untuk data Financial (Harian/Kuartalan)
static void fillFinancial(const std::string& symbol, int fitem_id, ExtraData* pData, float* outArr) {
  auto data = FinancialStore::get(symbol, fitem_id);

  if (data.empty()) {
    FetchTask task;
    task.symbol = symbol;
    task.type = FetchTaskType::GET_FINANCIALS;
    
    // Cuma di-queue SEKALI. Queuer akan tolak jika sudah ada
    QueueFetchTask(std::move(task)); 
    
    for (int i = 0; i < pData->nArraySize; i++) outArr[i] = EMPTY_VAL;
    return;
  }

  // ---- LOGIKA FORWARD-FILL (Data bisa harian/kuartalan)
  // (Kita pake logika yang sama persis kayak ownership, ini super robust)
  size_t data_idx = 0;
  float current_value = EMPTY_VAL; 
  for (int i = 0; i < pData->nArraySize; i++) {
    DATE_TIME_INT barTs_Unix = AmiDateToUnix(pData->anTimestamps[i]);

    while (data_idx < data.size() && data[data_idx].ts <= barTs_Unix) {
      // Cek kalo datanya null (EMPTY_VAL)
      if (data[data_idx].value != EMPTY_VAL) {
        current_value = data[data_idx].value; // Update nilai terakhir
      }
      // Kalo null, kita tidak update current_value,
      // jadi dia akan pakai nilai valid terakhir (forward-fill)
      data_idx++;
    }
    outArr[i] = current_value;
  }
}

void ExtraDispatcher::Handle(LPCTSTR pszTicker, LPCTSTR pszName, ExtraData* pData, float* outArr) {
  std::string sym(pszTicker);
  std::string field(pszName);

  if (field == "OWN_INDIV") {
    fillOwnership(sym, "Individual", pData, outArr);
    return;
  }

  if (field == "OWN_CORP") {
    fillOwnership(sym, "Perusahaan", pData, outArr);
    return;
  }

  if (g_financialMetricsMap.count(field)) {
    int fitem_id = g_financialMetricsMap.at(field);
    fillFinancial(sym, fitem_id, pData, outArr);
    return;
  }

  // Fallback, isi semua pake EMPTY
  for (int i = 0; i < pData->nArraySize; i++)
    outArr[i] = EMPTY_VAL;
}