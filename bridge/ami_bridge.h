#ifndef AMI_BRIDGE_H
#define AMI_BRIDGE_H

#include "plugin.h" // Diperlukan untuk struct Quotation dan LPCTSTR

int GetQuotesEx_Bridge(LPCTSTR pszTicker, int nPeriodicity, int nLastValid, int nSize, struct Quotation* pQuotes);
void ProcessFetchQueue();

#endif // AMI_BRIDGE_H
