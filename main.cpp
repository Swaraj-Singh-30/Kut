#include "editor.h"
#include "terminal.h"
#include "buffermanager.h"
#include "config.h"
#include "gui_renderer.h"
#include <vector>
#include <string>

int main(int argc, char *argv[]) {
  // load config first  
  Config config;
  config.load();

  bool guiMode = false;
  std::vector<const char *> files;
  for (int i = 1; i < argc; i++) {
    if (std::string(argv[i]) == "--gui") {
      guiMode = true;
    } else {
      files.push_back(argv[i]);
    }
  }

#ifdef _WIN32
  guiMode = true;
#endif

  BufferManager buffers;
  // apply config to the initial buffer
  buffers.current().tab_stop    = config.tab_stop;
  buffers.current().line_numbers = config.line_numbers;
  buffers.current().quit_times  = config.quit_times;
  buffers.current().quit_times_cfg = config.quit_times;

  for (const auto *file : files)
    buffers.openFile(file);

  buffers.current().setStatusMessage(
    "HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find | Ctrl-Z = undo | Ctrl-Y = redo");

  if (guiMode) {
    GUIRenderer gui(buffers, config);
    gui.run();
    return 0;
  }

  Terminal terminal(buffers);

  terminal.enableRawMode();
  terminal.enableMouse();

  int rows, cols;
  if (terminal.getWindowSize(&rows, &cols) == -1) {
    perror("getWindowSize"); return 1;
  }
  terminal.screenrows = rows - 3;
  terminal.screencols = cols;

  // store config on terminal for theme use later
  terminal.config = config;

  while (1) {
    terminal.refreshScreen();
    terminal.processKeypress();
  }
  return 0;
}