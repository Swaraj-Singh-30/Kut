#pragma once

#include "editor.h"
#include <vector>
#include <memory>

class BufferManager {
public:
  BufferManager();

  // buffer access
  Editor &current();
  int activeIndex() const;
  int count() const;

  // buffer operations  
  void newBuffer();
  void openFile(const char *filename);
  void closeBuffer();

  // navigation
  void next();
  void prev();

  // expose buffers for tab bar rendering
  const std::vector<std::unique_ptr<Editor>> &getBuffers() const;

private:
  std::vector<std::unique_ptr<Editor>> buffers;
  int active;
};