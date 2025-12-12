#include "ritel_fetcher.h"
#include "ritel_parser.h"
#include "api_client.h"
#include "config.h"
#include <sstream>
#include <string>

bool RitelFetcher::fetch(const std::string& symbol, const std::string& brokers) {
  std::string store_key;
  std::string brokers_to_process;

  if (brokers.empty()) {
    brokers_to_process = "YP,PD,XC,KK",   // default ritel flow
    store_key = symbol;                   // Key di store: "BBRI"
  } else {
    brokers_to_process = brokers;         // Specific Broker Flow
    store_key = symbol + "_" + brokers;   // Key di store harus beda supaya tidak overwrite! Misal: "BBRI_XL"
  }

  // Konstruksi query parameter
  std::string broker_query_str;
  std::stringstream ss(brokers_to_process);
  std::string segment;

  while (std::getline(ss, segment, ',')) {
    if (!segment.empty()) {
      broker_query_str += "&broker_code=" + segment;
    }
  }
  
  // Full URL
  std::string url = Config::getInstance().getHost() +
      "/api/amibroker/ritelflow?symbol=" + symbol +
      "&period=RT_PERIOD_LAST_1_YEAR" +
      broker_query_str;

  std::string json = WinHttpGetData(url);
  if (json.empty()) return false;

  return RitelParser::parseAndStore(json, store_key);
}
