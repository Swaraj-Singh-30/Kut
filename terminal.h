#pragma once

#include "buffermanager.h"
#include "config.h"
#include <termios.h>

/*** input event ***/
struct MouseEvent {
  int button;  // 0=left, 1=middle, 2=right, 64=scroll up, 65=scroll down
  int col;     // 1-based terminal column
  int row;     // 1-based terminal row
};

struct InputEvent {
  enum Type { KEY, MOUSE } type;
  int key;           // valid when type == KEY
  MouseEvent mouse;  // valid when type == MOUSE
};

#define MOUSE_PRESS  2000
#define MOUSE_SCROLL 2001

// forward declare abuf so, don't need to define it in the header
struct abuf;

class Terminal {
public:
  BufferManager &buffers;
  struct termios orig_termios;
  int screenrows, screencols;
  int lineNumWidth;
  Config config;

  Terminal(BufferManager &bm);
  ~Terminal();

  // setup
  void enableRawMode();
  void disableRawMode();
  void enableMouse();
  void disableMouse();
  int  getWindowSize(int *rows, int *cols);

  // input
  InputEvent readInput();
  char *prompt(const char *promptStr, void (*callback)(Editor &, char *, int));


  // output
  void scroll();
  void drawRows(struct abuf *ab);
  void drawTabBar(struct abuf *ab);
  void drawStatusBar(struct abuf *ab);
  void drawMessageBar(struct abuf *ab);
  void refreshScreen();

  // input handling
  void moveCursor(int key);
  void processKeypress();
  void handleMouse(const MouseEvent &mouse);

  // save + find
  void save();
  void find();

private:
  bool mouseScrolled;
  InputEvent lastEvent;
  int  getCursorPosition(int *rows, int *cols);
  int  readKey();
  void handleTabClick(int col);
  static void findCallback(Editor &e, char *query, int key);
};