#include "editor.h"
#include "terminal.h"
#include "buffermanager.h"

int main(int argc, char *argv[]) {
  BufferManager buffers;
  Terminal terminal(buffers);

  terminal.enableRawMode();

  int rows, cols;
  if (terminal.getWindowSize(&rows, &cols) == -1) {
    perror("getWindowSize"); return 1;
  }
  terminal.screenrows = rows - 3;
  terminal.screencols = cols;

  // open all files passed as arguments
  for (int i = 1; i < argc; i++)
    buffers.openFile(argv[i]);

  buffers.current().setStatusMessage(
    "HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find | Ctrl-Z = undo | Ctrl-Y = redo | Ctrl-Tab = next buffer");

  while (1) {
    terminal.refreshScreen();
    terminal.processKeypress();
  }
  return 0;
}