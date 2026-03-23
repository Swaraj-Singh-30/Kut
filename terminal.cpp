#include "terminal.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

/*** append buffer ***/
struct abuf {
  char *b;
  int len;
};
#define ABUF_INIT {NULL, 0}

static void abAppend(struct abuf *ab, const char *s, int len) {
  char *newbuf = (char *)realloc(ab->b, ab->len + len);
  if (newbuf == NULL) return;
  memcpy(&newbuf[ab->len], s, len);
  ab->b = newbuf;
  ab->len += len;
}
static void abFree(struct abuf *ab) { free(ab->b); }

/*** lifecycle ***/
Terminal::Terminal(Editor &e) : editor(e) {}

Terminal::~Terminal() {
  disableRawMode();
}

void Terminal::enableRawMode() {
  if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) {
    perror("tcgetattr"); exit(1);
  }
  struct termios raw = orig_termios;
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= (CS8);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
    perror("tcsetattr"); exit(1);
  }
}

void Terminal::disableRawMode() {
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

/*** input ***/
int Terminal::getCursorPosition(int *rows, int *cols) {
  char buf[32];
  unsigned int i = 0;
  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;
  while (i < sizeof(buf) - 1) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
    if (buf[i] == 'R') break;
    i++;
  }
  buf[i] = '\0';
  if (buf[0] != '\x1b' || buf[1] != '[') return -1;
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;
  return 0;
}

int Terminal::getWindowSize(int *rows, int *cols) {
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
    return getCursorPosition(rows, cols);
  }
  *cols = ws.ws_col;
  *rows = ws.ws_row;
  return 0;
}

int Terminal::readKey() {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN) { perror("read"); exit(1); }
  }
  if (c == '\x1b') {
    char seq[3];
    if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
    if (seq[0] == '[') {
      if (seq[1] >= '0' && seq[1] <= '9') {
        if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
        if (seq[2] == '~') {
          switch (seq[1]) {
            case '1': return HOME_KEY;
            case '4': return END_KEY;
            case '5': return PAGE_UP;
            case '6': return PAGE_DOWN;
            case '7': return HOME_KEY;
            case '8': return END_KEY;
          }
        }
      } else {
        switch (seq[1]) {
          case 'A': return ARROW_UP;
          case 'B': return ARROW_DOWN;
          case 'C': return ARROW_RIGHT;
          case 'D': return ARROW_LEFT;
          case 'H': return HOME_KEY;
          case 'F': return END_KEY;
        }
      }
    } else if (seq[0] == 'O') {
      switch (seq[1]) {
        case 'H': return HOME_KEY;
        case 'F': return END_KEY;
      }
    }
    return '\x1b';
  }
  return c;
}

char *Terminal::prompt(const char *promptStr,
                       void (*callback)(Editor &, char *, int)) {
  size_t bufsize = 128;
  char *buf = (char *)malloc(bufsize);
  size_t buflen = 0;
  buf[0] = '\0';
  while (1) {
    editor.setStatusMessage(promptStr, buf);
    refreshScreen();
    int c = readKey();
    if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
      if (buflen != 0) buf[--buflen] = '\0';
    } else if (c == '\x1b') {
      editor.setStatusMessage("");
      if (callback) callback(editor, buf, c);
      free(buf);
      return NULL;
    } else if (c == '\r') {
      if (buflen != 0) {
        editor.setStatusMessage("");
        if (callback) callback(editor, buf, c);
        return buf;
      }
    } else if (!iscntrl(c) && c < 128) {
      if (buflen == bufsize - 1) {
        bufsize *= 2;
        buf = (char *)realloc(buf, bufsize);
      }
      buf[buflen++] = c;
      buf[buflen] = '\0';
    }
    if (callback) callback(editor, buf, c);
  }
}

/*** output ***/
void Terminal::scroll() {
  editor.rx = 0;
  if (editor.cy < editor.numrows)
    editor.rx = editor.rowCxToRx(&editor.row[editor.cy], editor.cx);

  if (editor.cy < editor.rowoff) editor.rowoff = editor.cy;
  if (editor.cy >= editor.rowoff + editor.screenrows)
    editor.rowoff = editor.cy - editor.screenrows + 1;
  if (editor.rx < editor.coloff) editor.coloff = editor.rx;
  if (editor.rx >= editor.coloff + editor.screencols)
    editor.coloff = editor.rx - editor.screencols + 1;
}

void Terminal::drawRows(struct abuf *ab) {
  for (int y = 0; y < editor.screenrows; y++) {
    int filerow = y + editor.rowoff;
    if (filerow >= editor.numrows) {
      if (editor.numrows == 0 && y == editor.screenrows / 3) {
        char welcome[80];
        int welcomelen = snprintf(welcome, sizeof(welcome),
          "Kut editor -- version %s", KUT_VERSION);
        if (welcomelen > editor.screencols) welcomelen = editor.screencols;
        int padding = (editor.screencols - welcomelen) / 2;
        if (padding) { abAppend(ab, "~", 1); padding--; }
        while (padding--) abAppend(ab, " ", 1);
        abAppend(ab, welcome, welcomelen);
      } else {
        abAppend(ab, "~", 1);
      }
    } else {
      int len = editor.row[filerow].rsize - editor.coloff;
      if (len < 0) len = 0;
      if (len > editor.screencols) len = editor.screencols;
      char *c = &editor.row[filerow].render[editor.coloff];
      unsigned char *hl = &editor.row[filerow].hl[editor.coloff];
      int current_color = -1;
      for (int j = 0; j < len; j++) {
        if (iscntrl(c[j])) {
          char sym = (c[j] <= 26) ? '@' + c[j] : '?';
          abAppend(ab, "\x1b[7m", 4);
          abAppend(ab, &sym, 1);
          abAppend(ab, "\x1b[m", 3);
          if (current_color != -1) {
            char buf[16];
            int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", current_color);
            abAppend(ab, buf, clen);
          }
        } else if (hl[j] == HL_NORMAL) {
          if (current_color != -1) {
            abAppend(ab, "\x1b[39m", 5);
            current_color = -1;
          }
          abAppend(ab, &c[j], 1);
        } else {
          int color = editor.syntaxToColor(hl[j]);
          if (color != current_color) {
            current_color = color;
            char buf[16];
            int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", color);
            abAppend(ab, buf, clen);
          }
          abAppend(ab, &c[j], 1);
        }
      }
      abAppend(ab, "\x1b[39m", 5);
    }
    abAppend(ab, "\x1b[K", 3);
    abAppend(ab, "\r\n", 2);
  }
}

void Terminal::drawStatusBar(struct abuf *ab) {
  abAppend(ab, "\x1b[7m", 4);
  char status[80], rstatus[80];
  int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
    editor.filename ? editor.filename : "[No Name]", editor.numrows,
    editor.dirty ? "(modified)" : "");
  int rlen = snprintf(rstatus, sizeof(rstatus), "%s | %d/%d",
    editor.syntax ? editor.syntax->filetype : "no ft",
    editor.cy + 1, editor.numrows);
  if (len > editor.screencols) len = editor.screencols;
  abAppend(ab, status, len);
  while (len < editor.screencols) {
    if (editor.screencols - len == rlen) {
      abAppend(ab, rstatus, rlen);
      break;
    }
    abAppend(ab, " ", 1);
    len++;
  }
  abAppend(ab, "\x1b[m", 3);
  abAppend(ab, "\r\n", 2);
}

void Terminal::drawMessageBar(struct abuf *ab) {
  abAppend(ab, "\x1b[K", 3);
  int msglen = strlen(editor.statusmsg);
  if (msglen > editor.screencols) msglen = editor.screencols;
  if (msglen && time(NULL) - editor.statusmsg_time < 5)
    abAppend(ab, editor.statusmsg, msglen);
}

void Terminal::refreshScreen() {
  scroll();
  struct abuf ab = ABUF_INIT;
  abAppend(&ab, "\x1b[?25l", 6);
  abAppend(&ab, "\x1b[H", 3);
  drawRows(&ab);
  drawStatusBar(&ab);
  drawMessageBar(&ab);
  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH",
    (editor.cy - editor.rowoff) + 1,
    (editor.rx - editor.coloff) + 1);
  abAppend(&ab, buf, strlen(buf));
  abAppend(&ab, "\x1b[?25h", 6);
  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}

/*** find ***/
void Terminal::findCallback(Editor &e, char *query, int key) {
  static int last_match = -1;
  static int direction  = 1;
  static int saved_hl_line;
  static char *saved_hl = NULL;

  if (saved_hl) {
    memcpy(e.row[saved_hl_line].hl, saved_hl, e.row[saved_hl_line].rsize);
    free(saved_hl);
    saved_hl = NULL;
  }
  if (key == '\r' || key == '\x1b') {
    last_match = -1; direction = 1; return;
  } else if (key == ARROW_RIGHT || key == ARROW_DOWN) { direction = 1;
  } else if (key == ARROW_LEFT  || key == ARROW_UP)   { direction = -1;
  } else { last_match = -1; direction = 1; }

  if (last_match == -1) direction = 1;
  int current = last_match;
  for (int i = 0; i < e.numrows; i++) {
    current += direction;
    if (current == -1) current = e.numrows - 1;
    else if (current == e.numrows) current = 0;
    erow *row = &e.row[current];
    char *match = strstr(row->render, query);
    if (match) {
      last_match = current;
      e.cy = current;
      e.cx = e.rowRxToCx(row, match - row->render);
      e.rowoff = e.numrows;
      saved_hl_line = current;
      saved_hl = (char *)malloc(row->rsize);
      memcpy(saved_hl, row->hl, row->rsize);
      memset(&row->hl[match - row->render], HL_MATCH, strlen(query));
      break;
    }
  }
}

void Terminal::find() {
  int saved_cx = editor.cx, saved_cy = editor.cy;
  int saved_coloff = editor.coloff, saved_rowoff = editor.rowoff;
  char *query = prompt("Search: %s (ESC/Arrows/Enter)", findCallback);
  if (query) { free(query); }
  else {
    editor.cx = saved_cx; editor.cy = saved_cy;
    editor.coloff = saved_coloff; editor.rowoff = saved_rowoff;
  }
}

/*** save ***/
void Terminal::save() {
  if (editor.filename == NULL) {
    editor.filename = prompt("Save as: %s (ESC to cancel)", NULL);
    if (editor.filename == NULL) {
      editor.setStatusMessage("Save aborted");
      return;
    }
    editor.selectSyntaxHighlight();
  }
  int len;
  char *buf = editor.rowsToString(&len);
  int fd = open(editor.filename, O_RDWR | O_CREAT, 0644);
  if (fd != -1) {
    if (ftruncate(fd, len) != -1) {
      if (write(fd, buf, len) == len) {
        close(fd); free(buf);
        editor.dirty = 0;
        editor.setStatusMessage("%d bytes written to disk", len);
        return;
      }
    }
    close(fd);
  }
  free(buf);
  editor.setStatusMessage("Can't save! I/O error: %s", strerror(errno));
}

/*** input ***/
void Terminal::moveCursor(int key) {
  erow *row = (editor.cy >= editor.numrows) ? NULL : &editor.row[editor.cy];
  switch (key) {
    case ARROW_LEFT:
      if (editor.cx != 0) { editor.cx--;
      } else if (editor.cy > 0) {
        editor.cy--;
        editor.cx = editor.row[editor.cy].size;
      }
      break;
    case ARROW_RIGHT:
      if (row && editor.cx < row->size) { editor.cx++;
      } else if (row && editor.cx == row->size) {
        editor.cy++; editor.cx = 0;
      }
      break;
    case ARROW_UP:
      if (editor.cy != 0) editor.cy--;
      break;
    case ARROW_DOWN:
      if (editor.cy < editor.numrows) editor.cy++;
      break;
  }
  row = (editor.cy >= editor.numrows) ? NULL : &editor.row[editor.cy];
  int rowlen = row ? row->size : 0;
  if (editor.cx > rowlen) editor.cx = rowlen;
}

void Terminal::processKeypress() {
  static int quit_times = KUT_QUIT_TIMES;
  int c = readKey();
  switch (c) {
    case '\r':
      editor.applyCommand(
        std::make_unique<InsertNewlineCommand>(editor.cx, editor.cy));
      break;

    case CTRL_KEY('q'):
      if (editor.dirty && quit_times > 0) {
        editor.setStatusMessage("WARNING!!! Unsaved changes. "
          "Press Ctrl-Q %d more times to quit.", quit_times);
        quit_times--;
        return;
      }
      write(STDOUT_FILENO, "\x1b[2J", 4);
      write(STDOUT_FILENO, "\x1b[H", 3);
      exit(0);
    case CTRL_KEY('s'): save(); break;
    case HOME_KEY: editor.cx = 0; break;
    case END_KEY:
      if (editor.cy < editor.numrows)
        editor.cx = editor.row[editor.cy].size;
      break;
    case CTRL_KEY('f'): find(); break;
    case BACKSPACE:
    case CTRL_KEY('h'):
    case DEL_KEY: {
      if (c == DEL_KEY) moveCursor(ARROW_RIGHT);
      // capture what's about to be deleted before deleting it
      bool wasNewline = (editor.cx == 0 && editor.cy > 0);
      char deleted = '\0';
      if (!wasNewline && editor.cx > 0)
        deleted = editor.row[editor.cy].chars[editor.cx - 1];
      editor.applyCommand(
        std::make_unique<DeleteCharCommand>(editor.cx, editor.cy, deleted, wasNewline));
      break;
    }
    case PAGE_UP:
    case PAGE_DOWN: {
      if (c == PAGE_UP) editor.cy = editor.rowoff;
      else {
        editor.cy = editor.rowoff + editor.screenrows - 1;
        if (editor.cy > editor.numrows) editor.cy = editor.numrows;
      }
      int times = editor.screenrows;
      while (times--) moveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
      break;
    }
    case CTRL_KEY('z'): editor.undo(); break;
    case CTRL_KEY('y'): editor.redo(); break;
    case ARROW_UP: case ARROW_DOWN:
    case ARROW_LEFT: case ARROW_RIGHT:
      moveCursor(c); break;
    case CTRL_KEY('l'):
    case '\x1b': break;
    default:
    editor.applyCommand(
      std::make_unique<InsertCharCommand>(editor.cx, editor.cy, c));
    break;
  }
}