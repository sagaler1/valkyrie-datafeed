#ifndef AMI_BRIDGE_H
#define AMI_BRIDGE_H

#include "plugin.h" // Diperlukan untuk struct Quotation dan LPCTSTR
#include <mutex>
#include <condition_variable>

// ---- Worker Thread 2
extern std::mutex g_fetchQueueMtx;
extern std::condition_variable g_fetchQueueCV;

int GetQuotesEx_Bridge(LPCTSTR pszTicker, int nPeriodicity, int nLastValid, int nSize, struct Quotation* pQuotes);
void ProcessFetchQueue();

#endif // AMI_BRIDGE_H
