#include "ownership_fetcher.h"
#include "ownership_store.h"
#include "ownership_parser.h"
#include "api_client.h"   // WinHttpGetData
#include "config.h"

bool OwnershipFetcher::fetch(const std::string& symbol, const std::string& ownerType)
{
  std::string url = Config::getInstance().getHost() +
      "/api/amibroker/ownership?symbol=" + symbol + "&value_year=60&shareholder_type=local";

  std::string json = WinHttpGetData(url);
  if (json.empty()) return false;

  auto data = OwnershipParser::parse(json, ownerType);

  OwnershipStore::set(symbol, ownerType, data);
  return true;
}
