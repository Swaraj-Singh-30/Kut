#pragma once

#include "buffermanager.h"
#include "config.h"

class GUIRenderer {
public:
  GUIRenderer(BufferManager &buffers, Config &config);
  void run();

private:
  BufferManager &buffers;
  Config &config;
};
