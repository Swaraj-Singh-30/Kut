#pragma once

#include <string>
#include <vector>
#include <termios.h>
#include <time.h>

/*** defines ***/
#define KUT_VERSION "0.0.1"
#define KUT_TAB_STOP 8
#define KUT_QUIT_TIMES 3
#define CTRL_KEY(k) ((k) & 0x1f)

/*** enums ***/
enum editorKey {
  BACKSPACE = 127,
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  DEL_KEY,
  HOME_KEY,
  END_KEY,
  PAGE_UP,
  PAGE_DOWN
};

enum editorHighlight {
  HL_NORMAL = 0,
  HL_COMMENT,
  HL_MLCOMMENT,
  HL_KEYWORD1,
  HL_KEYWORD2,
  HL_STRING,
  HL_NUMBER,
  HL_MATCH
};

#define HL_HIGHLIGHT_NUMBERS (1<<0)
#define HL_HIGHLIGHT_STRINGS (1<<1)

/*** structs ***/
struct editorSyntax {
  const char *filetype;
  const char **filematch;
  const char **keywords;
  const char *singleline_comment_start;
  const char *multiline_comment_start;
  const char *multiline_comment_end;
  int flags;
};

struct erow {
  int idx;
  int size;
  int rsize;
  char *chars;
  char *render;
  unsigned char *hl;
  int hl_open_comment;
};

/*** Editor class ***/
class Editor {
public:
  // state (Terminal needs to read these for rendering)
  int cx, cy;
  int rx;
  int rowoff, coloff;
  int screenrows, screencols;
  int numrows;
  erow *row;
  int dirty;
  char *filename;
  char statusmsg[80];
  time_t statusmsg_time;
  struct editorSyntax *syntax;

  // lifecycle
  Editor();
  ~Editor();
  void init(int rows, int cols);

  // row operations
  int  rowCxToRx(erow *row, int cx);
  int  rowRxToCx(erow *row, int rx);
  void updateRow(erow *row);
  void insertRow(int at, const char *s, size_t len);
  void freeRow(erow *row);
  void delRow(int at);
  void rowInsertChar(erow *row, int at, int c);
  void rowAppendString(erow *row, char *s, size_t len);
  void rowDelChar(erow *row, int at);

  // editor operations
  void insertChar(int c);
  void insertNewline();
  void delChar();

  // file i/o
  char *rowsToString(int *buflen);
  void  openFile(const char *filename);
  void  save();

  // find
  void find(char *query, int key);  // callback
  void startFind();

  // status
  void setStatusMessage(const char *fmt, ...);

  // syntax
  void updateSyntax(erow *row);
  int  syntaxToColor(int hl);
  void selectSyntaxHighlight();

private:
  // nothing private yet — we'll move things here in the C++ification phase
};