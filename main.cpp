#include "editor.h"
#include "terminal.h"
#include "buffermanager.h"
#include "config.h"

int main(int argc, char *argv[]) {
  // load config first — before anything else
  Config config;
  config.load();

  BufferManager buffers;
  Terminal terminal(buffers);

  terminal.enableRawMode();

  int rows, cols;
  if (terminal.getWindowSize(&rows, &cols) == -1) {
    perror("getWindowSize"); return 1;
  }
  terminal.screenrows = rows - 3;
  terminal.screencols = cols;

  // apply config to the initial buffer
  buffers.current().tab_stop    = config.tab_stop;
  buffers.current().quit_times  = config.quit_times;
  buffers.current().quit_times_cfg = config.quit_times;

  // store config on terminal for theme use later
  terminal.config = config;

  for (int i = 1; i < argc; i++)
    buffers.openFile(argv[i]);

  buffers.current().setStatusMessage(
    "HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find | Ctrl-Z = undo | Ctrl-Y = redo");

  while (1) {
    terminal.refreshScreen();
    terminal.processKeypress();
  }
  return 0;
}