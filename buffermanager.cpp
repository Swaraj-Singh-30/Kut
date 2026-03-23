#include "buffermanager.h"
#include <cstring>

BufferManager::BufferManager() : active(0) {
  // start with one empty buffer
  buffers.push_back(std::make_unique<Editor>());
}

Editor &BufferManager::current() {
  return *buffers[active];
}

int BufferManager::activeIndex() const {
  return active;
}

int BufferManager::count() const {
  return (int)buffers.size();
}

const std::vector<std::unique_ptr<Editor>> &BufferManager::getBuffers() const {
  return buffers;
}

void BufferManager::newBuffer() {
  buffers.push_back(std::make_unique<Editor>());
  active = (int)buffers.size() - 1;
}

void BufferManager::openFile(const char *filename) {
  // check if file is already open, don't open it twice
  for (int i = 0; i < (int)buffers.size(); i++) {
    if (buffers[i]->filename && strcmp(buffers[i]->filename, filename) == 0) {
      active = i;
      return;
    }
  }
  // open in current buffer if it's empty and unmodified
  if (current().numrows == 0 && !current().dirty) {
    current().openFile(filename);
  } else {
    // otherwise open in a new buffer
    newBuffer();
    current().openFile(filename);
  }
}

void BufferManager::closeBuffer() {
  // always keep at least one buffer open
  if (buffers.size() == 1) {
    // just clear the current buffer instead of closing
    buffers[0] = std::make_unique<Editor>();
    return;
  }
  buffers.erase(buffers.begin() + active);
  // make sure active stays in bounds
  if (active >= (int)buffers.size())
    active = (int)buffers.size() - 1;
}

void BufferManager::next() {
  active = (active + 1) % (int)buffers.size();
}

void BufferManager::prev() {
  active = (active - 1 + (int)buffers.size()) % (int)buffers.size();
}