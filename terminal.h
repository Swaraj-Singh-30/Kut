#pragma once

#include "config.h"
#include "buffermanager.h"
#include "editor.h"
#include <termios.h>

class Terminal {
public:
  BufferManager &buffers;  // reference to the buffer manager, not a copy cause it can not be NULL
  struct termios orig_termios;
  int screenrows, screencols;
  Config config;

  Terminal(BufferManager &bm);
  ~Terminal();

  // setup
  void enableRawMode();
  void disableRawMode();
  int  getWindowSize(int *rows, int *cols);
  void drawTabBar(struct abuf *ab);

  // input
  int   readKey();
  char *prompt(const char *promptStr, void (*callback)(Editor &, char *, int));

  // output
  void scroll();
  void drawRows(struct abuf *ab);
  void drawStatusBar(struct abuf *ab);
  void drawMessageBar(struct abuf *ab);
  void refreshScreen();

  // input handling
  void moveCursor(int key);
  void processKeypress();

  // save + find (live here because they need prompt())
  void save();
  void find();

private:
  int  getCursorPosition(int *rows, int *cols);
  static void findCallback(Editor &e, char *query, int key);
};