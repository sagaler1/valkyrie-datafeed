#include "ritel_fetcher.h"
#include "ritel_parser.h"
#include "api_client.h"
#include "config.h"

bool RitelFetcher::fetch(const std::string& symbol) {
  std::string url = Config::getInstance().getHost() +
      "/api/amibroker/ritelflow?symbol=" + symbol +
      "&period=RT_PERIOD_LAST_1_YEAR"
      "&broker_code=XC&broker_code=YP&broker_code=XC&broker_code=PD&broker_code=KK";

  std::string json = WinHttpGetData(url);
  if (json.empty()) return false;

  return RitelParser::parseAndStore(json, symbol);
}
