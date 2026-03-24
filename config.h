#pragma once
#include <string>

struct Config {
  // defaults
  int tab_stop    = 4;
  int quit_times  = 3;
  std::string theme = "dark";
  bool line_numbers = true;

  // reads ~/.kutrc and applies settings
  void load();
};