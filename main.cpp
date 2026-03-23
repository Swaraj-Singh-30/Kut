#include "editor.h"
#include "terminal.h"

int main(int argc, char *argv[]) {
  Editor editor;
  Terminal terminal(editor);

  terminal.enableRawMode();

  int rows, cols;
  if (terminal.getWindowSize(&rows, &cols) == -1) {
    perror("getWindowSize"); return 1;
  }

  terminal.screenrows = rows - 2;
  terminal.screencols = cols;
  editor.init(); 

  if (argc >= 2) editor.openFile(argv[1]);

  editor.setStatusMessage(
  "HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find | Ctrl-Z = undo | Ctrl-Y = redo");
  
  while (1) {
    terminal.refreshScreen();
    terminal.processKeypress();
  }
  return 0;
}