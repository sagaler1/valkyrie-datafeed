#pragma once
#include <string>
#include "plugin.h"

namespace ExtraDispatcher {
  void Handle(LPCTSTR pszTicker, LPCTSTR pszName, ExtraData* pData, float* outArr);
}
