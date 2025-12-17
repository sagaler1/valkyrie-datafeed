#pragma once
#include <windows.h>
#include <string>

enum class PriceBaseColor {
  NEUTRAL,
  UP,
  DOWN,
  FLAT
};

// Data yang akan dirender
struct HeaderState {
  std::string prev;
  std::string last, chg, pct, open, high, low;
  std::string lot, val, freq;

  double last_num = 0;
  double open_num = 0;
  double high_num = 0;
  double low_num  = 0;
  double chg_num  = 0;
  double prev_num = 0;

  // base color
  PriceBaseColor last_base = PriceBaseColor::NEUTRAL;
  PriceBaseColor open_base = PriceBaseColor::NEUTRAL;
  PriceBaseColor high_base = PriceBaseColor::NEUTRAL;
  PriceBaseColor low_base  = PriceBaseColor::NEUTRAL;
  PriceBaseColor chg_base  = PriceBaseColor::NEUTRAL;
};

// Register class & create panel
HWND CreateHeaderPanel(HWND hParent, HINSTANCE hInst, int x, int y, int w, int h);

// Update data + repaint
void UpdateHeaderPanel(HWND hPanel, const HeaderState& data);
