#include "FinancialFetcher.h"
#include "FinancialParser.h"
#include "api_client.h"   // WinHttpGetData
#include "config.h"       // Config::getInstance

const std::string g_fitem_list = "21334,21535,1461,2896,1474,1516";
    // SHAREHOLDERS_NUM, FREE_FLOAT

bool FinancialFetcher::fetch(const std::string& symbol)
{
  std::string url = Config::getInstance().getHost() +
    "/api/amibroker/financials?item=" + g_fitem_list +
    "&companies=" + symbol + "&timeframe=5y";

  std::string json = WinHttpGetData(url);
  if (json.empty()) return false;

  return FinancialParser::parseAndStore(json, symbol);    // Kirim JSON ke parser. Parser akan simpan ke Store.
}