#pragma once
#include "plugin.h"

// ---- Struct generik untuk data time-series
// ---- Bisa untuk data bulanan (Ownership) atau harian/kuartalan (Financial)
struct DataPoint {
  DATE_TIME_INT ts;   // Selalu simpan sebagai Unix Timestamp (detik)
  float value;
};