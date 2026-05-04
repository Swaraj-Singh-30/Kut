#include "terminal.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fstream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <conio.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#include <termios.h>
#endif

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

static int termWrite(const char *s, int len) {
#ifdef _WIN32
  DWORD written = 0;
  HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
  if (hOut == INVALID_HANDLE_VALUE) return -1;
  if (!WriteFile(hOut, s, (DWORD)len, &written, NULL)) return -1;
  return (int)written;
#else
  return (int)write(STDOUT_FILENO, s, len);
#endif
}

static int countDigits(int n) {
  if (n == 0) return 1;
  int d = 0;
  while (n > 0) { n /= 10; d++; }
  return d;
}

/*** lifecycle ***/
Terminal::Terminal(BufferManager &bm) : buffers(bm), screenrows(0), screencols(0), lineNumWidth(0), config(), mouseScrolled(false) {}

Terminal::~Terminal() {
  disableMouse();
  disableRawMode();
}

void Terminal::enableRawMode() {
#ifdef _WIN32
  state.hIn = GetStdHandle(STD_INPUT_HANDLE);
  state.hOut = GetStdHandle(STD_OUTPUT_HANDLE);
  if (state.hIn == INVALID_HANDLE_VALUE || state.hOut == INVALID_HANDLE_VALUE) {
    fprintf(stderr, "GetStdHandle failed\n");
    exit(1);
  }
  if (!GetConsoleMode(state.hIn, &state.inMode)) {
    fprintf(stderr, "GetConsoleMode failed\n");
    exit(1);
  }
  if (!GetConsoleMode(state.hOut, &state.outMode)) {
    fprintf(stderr, "GetConsoleMode failed\n");
    exit(1);
  }
  DWORD inMode = state.inMode;
  inMode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);
  inMode |= ENABLE_EXTENDED_FLAGS;
  if (!SetConsoleMode(state.hIn, inMode)) {
    fprintf(stderr, "SetConsoleMode failed\n");
    exit(1);
  }
  DWORD outMode = state.outMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
  SetConsoleMode(state.hOut, outMode);
#else
  if (tcgetattr(STDIN_FILENO, &state.orig_termios) == -1) {
    perror("tcgetattr"); exit(1);
  }
  struct termios raw = state.orig_termios;
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= (CS8);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
    perror("tcsetattr"); exit(1);
  }
#endif
}

void Terminal::disableRawMode() {
#ifdef _WIN32
  if (state.hIn) SetConsoleMode(state.hIn, state.inMode);
  if (state.hOut) SetConsoleMode(state.hOut, state.outMode);
#else
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &state.orig_termios);
#endif
}

/*** input ***/
int Terminal::getCursorPosition(int *rows, int *cols) {
#ifdef _WIN32
  CONSOLE_SCREEN_BUFFER_INFO info;
  if (!GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info))
    return -1;
  *rows = info.dwCursorPosition.Y + 1;
  *cols = info.dwCursorPosition.X + 1;
  return 0;
#else
  char buf[32];
  unsigned int i = 0;
  if (termWrite("\x1b[6n", 4) != 4) return -1;
  while (i < sizeof(buf) - 1) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
    if (buf[i] == 'R') break;
    i++;
  }
  buf[i] = '\0';
  if (buf[0] != '\x1b' || buf[1] != '[') return -1;
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;
  return 0;
#endif
}

int Terminal::getWindowSize(int *rows, int *cols) {
#ifdef _WIN32
  CONSOLE_SCREEN_BUFFER_INFO info;
  if (!GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info))
    return -1;
  *cols = info.srWindow.Right - info.srWindow.Left + 1;
  *rows = info.srWindow.Bottom - info.srWindow.Top + 1;
  return 0;
#else
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    if (termWrite("\x1b[999C\x1b[999B", 12) != 12) return -1;
    return getCursorPosition(rows, cols);
  }
  *cols = ws.ws_col;
  *rows = ws.ws_row;
  return 0;
#endif
}

int Terminal::readKey() {
#ifdef _WIN32
  while (1) {
    int ch = _getch();
    if (ch == 0 || ch == 224) {
      int code = _getch();
      switch (code) {
        case 72: return ARROW_UP;
        case 80: return ARROW_DOWN;
        case 75: return ARROW_LEFT;
        case 77: return ARROW_RIGHT;
        case 71: return HOME_KEY;
        case 79: return END_KEY;
        case 73: return PAGE_UP;
        case 81: return PAGE_DOWN;
        case 83: return DEL_KEY;
        default: return '\x1b';
      }
    }
    return ch;
  }
#else
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN) { perror("read"); exit(1); }
  }
  if (c == '\x1b') {
    char seq[8];
    if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

    if (seq[0] == '[') {
      // check for SGR mouse sequence: \x1b[
      if (seq[1] == '<') {
        char buf[32];
        int i = 0;
        while (i < (int)sizeof(buf) - 1) {
          char ch;
          if (read(STDIN_FILENO, &ch, 1) != 1) break;
          buf[i++] = ch;
          if (ch == 'M' || ch == 'm') break;
        }
        buf[i] = '\0';
        int button, col, row;
        char final;
        if (sscanf(buf, "%d;%d;%d%c", &button, &col, &row, &final) == 4
            && final == 'M') {
          lastEvent.type         = InputEvent::MOUSE;
          lastEvent.mouse.button = button;
          lastEvent.mouse.col    = col;
          lastEvent.mouse.row    = row;
          if (button == 64 || button == 65)
            return MOUSE_SCROLL;
          return MOUSE_PRESS;
        }
        return '\x1b';
      }

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
#endif
}

InputEvent Terminal::readInput() {
  lastEvent.type = InputEvent::KEY;
  lastEvent.key  = readKey();
  if (lastEvent.type == InputEvent::MOUSE)
    return lastEvent;
  return lastEvent;
}

char *Terminal::prompt(const char *promptStr,
                       void (*callback)(Editor &, char *, int)) {
  size_t bufsize = 128;
  Editor &editor = buffers.current();
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
  Editor &editor = buffers.current();
  int availcols = screencols - lineNumWidth;  // text area width

  editor.rx = 0;
  if (editor.cy < editor.numrows)
    editor.rx = editor.rowCxToRx(&editor.row[editor.cy], editor.cx);

  if (editor.cy < editor.rowoff) editor.rowoff = editor.cy;
  if (editor.cy >= editor.rowoff + screenrows)
    editor.rowoff = editor.cy - screenrows + 1;
  if (editor.rx < editor.coloff) editor.coloff = editor.rx;
  if (editor.rx >= editor.coloff + availcols) 
    editor.coloff = editor.rx - availcols + 1;
}

void Terminal::drawTabBar(struct abuf *ab) {
  abAppend(ab, "\x1b[0;44m", 7);  // blue background

  int len = 0;
  const auto &bufs = buffers.getBuffers();

  for (int i = 0; i < (int)bufs.size(); i++) {
    const char *name = bufs[i]->filename
      ? strrchr(bufs[i]->filename, '/')  // get just the filename, not full path
      : NULL;
    name = (name && *(name + 1)) ? name + 1 : (bufs[i]->filename ? bufs[i]->filename : "[No Name]");

    // build the tab label
    char tab[64];
    int tablen = snprintf(tab, sizeof(tab), " %s%s ",
      name,
      bufs[i]->dirty ? " *" : "");

    if (len + tablen > screencols) break;  // don't overflow the line

    if (i == buffers.activeIndex()) {
      // active tab, white background, dark text
      abAppend(ab, "\x1b[0;47;30m", 10);
    } else {
      // inactive tab, blue background, white text
      abAppend(ab, "\x1b[0;44;37m", 10);
    }

    abAppend(ab, tab, tablen);
    len += tablen;

    // separator between tabs
    if (i < (int)bufs.size() - 1) {
      abAppend(ab, "\x1b[0;44m|", 8);
      len++;
    }
  }

  // fill the rest of the line with blue background
  abAppend(ab, "\x1b[0;44m", 7);
  while (len < screencols) {
    abAppend(ab, " ", 1);
    len++;
  }

  abAppend(ab, "\x1b[m", 3);
  abAppend(ab, "\r\n", 2);
}

void Terminal::drawRows(struct abuf *ab) {
  Editor &editor = buffers.current();

  for (int y = 0; y < screenrows; y++) {
    int filerow = y + editor.rowoff;

    if (filerow >= editor.numrows) {
      if (editor.line_numbers) {
        char gutter[16];
        snprintf(gutter, sizeof(gutter), "%*s | ", lineNumWidth - 3, "~");
        abAppend(ab, "\x1b[90m", 5);
        abAppend(ab, gutter, strlen(gutter));
        abAppend(ab, "\x1b[m", 3);
      } else {
        abAppend(ab, "~", 1);
      }

      if (editor.numrows == 0 && y == screenrows / 3) {
        char welcome[80];
        int welcomelen = snprintf(welcome, sizeof(welcome),
          "Kut editor -- version %s", KUT_VERSION);
        int availcols = screencols - lineNumWidth;
        if (welcomelen > availcols) welcomelen = availcols;
        int padding = (availcols - welcomelen) / 2;
        while (padding--) abAppend(ab, " ", 1);
        abAppend(ab, welcome, welcomelen);
      }
    } else {
      // draw line number
      if (editor.line_numbers) {
        char num[64];
        if (filerow == editor.cy) {
          snprintf(num, sizeof(num), "\x1b[97m%*d\x1b[90m | \x1b[m",
            lineNumWidth - 3, filerow + 1);
        } else {
          snprintf(num, sizeof(num), "\x1b[90m%*d | \x1b[m",
            lineNumWidth - 3, filerow + 1);
        }
        abAppend(ab, num, strlen(num));
      }

      // draw file content
      int availcols = screencols - lineNumWidth;
      int len = editor.row[filerow].rsize - editor.coloff;
      if (len < 0) len = 0;
      if (len > availcols) len = availcols;
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
  Editor &editor = buffers.current();
  abAppend(ab, "\x1b[7m", 4);
  char status[80], rstatus[80];

  // strip directory path, show just the filename
  const char *displayname = editor.filename ? strrchr(editor.filename, '/') : NULL;
  displayname = (displayname && *(displayname + 1))
    ? displayname + 1
    : (editor.filename ? editor.filename : "[No Name]");

  int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
    displayname, editor.numrows, editor.dirty ? "(modified)" : "");
  int rlen = snprintf(rstatus, sizeof(rstatus), "%s | %d/%d",
    editor.syntax ? editor.syntax->filetype : "no ft",
    editor.cy + 1, editor.numrows);
  if (len > screencols) len = screencols;
  abAppend(ab, status, len);
  while (len < screencols) {
    if (screencols - len == rlen) {
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
  Editor &editor = buffers.current();
  abAppend(ab, "\x1b[K", 3);
  int msglen = strlen(editor.statusmsg);
  if (msglen > screencols) msglen = screencols;
  if (msglen && time(NULL) - editor.statusmsg_time < 5)
    abAppend(ab, editor.statusmsg, msglen);
}

void Terminal::refreshScreen() {
  Editor &editor = buffers.current();
  // compute gutter width for the frame
  lineNumWidth = editor.line_numbers ? countDigits(editor.numrows) + 3 : 0;

  if (!mouseScrolled)
    scroll();
  mouseScrolled = false;

  struct abuf ab = ABUF_INIT;
  abAppend(&ab, "\x1b[?25l", 6);
  abAppend(&ab, "\x1b[H", 3);
  drawRows(&ab);
  drawTabBar(&ab);
  drawStatusBar(&ab);
  drawMessageBar(&ab);

  // only show cursor if it's within the visible viewport
  bool cursorVisible = (editor.cy >= editor.rowoff && 
                        editor.cy < editor.rowoff + screenrows);
  if (cursorVisible) {
    char buf[32];
    // account for the gutter width so the cursor lines up with rendered text
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH",
      (editor.cy - editor.rowoff) + 1,
      lineNumWidth + (editor.rx - editor.coloff) + 1);
    abAppend(&ab, buf, strlen(buf));
    abAppend(&ab, "\x1b[?25h", 6); // show cursor
  }
  // if cursor not visible, leave it hidden (\x1b[?25l already sent above)

  termWrite(ab.b, ab.len);
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
  Editor &editor = buffers.current();
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
  Editor &editor = buffers.current();
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
  std::ofstream out(editor.filename, std::ios::binary | std::ios::trunc);
  if (out) {
    out.write(buf, len);
    if (out.good()) {
      free(buf);
      editor.dirty = 0;
      editor.setStatusMessage("%d bytes written to disk", len);
      return;
    }
  }
  free(buf);
  editor.setStatusMessage("Can't save! I/O error");
}

/*** input ***/
void Terminal::moveCursor(int key) {
  Editor &editor = buffers.current();
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

void Terminal::enableMouse() {
#ifdef _WIN32
  // mouse input not enabled in the Windows console backend (yet)
  return;
#else
  // SGR extended mouse mode, handles terminals wider than 223 cols
  termWrite("\x1b[?1000h", 8); // enable mouse clicks
  termWrite("\x1b[?1006h", 8); // enable SGR extended mode
#endif
}

void Terminal::disableMouse() {
#ifdef _WIN32
  return;
#else
  termWrite("\x1b[?1006l", 8);
  termWrite("\x1b[?1000l", 8);
#endif
}


void Terminal::handleMouse(const MouseEvent &mouse) {
  Editor &editor = buffers.current();

  int tabBarRow   = screenrows + 1;
  int textAreaTop = 1;
  int textAreaBot = screenrows;

  if (mouse.button == 64) {
    editor.rowoff -= 3;
    if (editor.rowoff < 0) editor.rowoff = 0;
    if (editor.cy < editor.rowoff)
      editor.cy = editor.rowoff;
    return;
  }
  if (mouse.button == 65) {
    int maxRowoff = editor.numrows - screenrows;
    if (maxRowoff < 0) maxRowoff = 0;
    editor.rowoff += 3;
    if (editor.rowoff > maxRowoff) editor.rowoff = maxRowoff;
    if (editor.cy < editor.rowoff)
      editor.cy = editor.rowoff;
    if (editor.cy >= editor.rowoff + screenrows)
      editor.cy = editor.rowoff + screenrows - 1;
    if (editor.cy >= editor.numrows)
      editor.cy = editor.numrows - 1;
    return;
  }

  if (mouse.button == 0) {
    if (mouse.row == tabBarRow) {
      handleTabClick(mouse.col);
      return;
    }
    if (mouse.row >= textAreaTop && mouse.row <= textAreaBot) {
      int filerow = (mouse.row - textAreaTop) + editor.rowoff;
      int filecol = (mouse.col - 1 - lineNumWidth) + editor.coloff;
      if (filecol < 0) filecol = 0;
      if (filerow >= editor.numrows) filerow = editor.numrows - 1;
      if (filerow < 0) filerow = 0;
      editor.cy = filerow;
      if (filerow < editor.numrows)
        editor.cx = editor.rowRxToCx(&editor.row[filerow], filecol);
      else
        editor.cx = 0;
    }
  }
}

void Terminal::handleTabClick(int col) {
  const auto &bufs = buffers.getBuffers();
  int x = 1;
  for (int i = 0; i < (int)bufs.size(); i++) {
    const char *name = bufs[i]->filename
      ? strrchr(bufs[i]->filename, '/') : NULL;
    name = (name && *(name + 1)) ? name + 1
         : (bufs[i]->filename ? bufs[i]->filename : "[No Name]");
    int tabwidth = strlen(name) + 2 + (bufs[i]->dirty ? 2 : 0);
    if (col >= x && col < x + tabwidth) {
      buffers.setActive(i);  // use the setter, after i is defined
      return;
    }
    x += tabwidth + 1;
  }
}


void Terminal::processKeypress() {
  Editor &editor = buffers.current();
  InputEvent event = readInput();

  if (event.type == InputEvent::MOUSE) {
    handleMouse(event.mouse);
    return;
  }

  int c = event.key;
  switch (c) {
    case '\r':
      editor.applyCommand(
        std::make_unique<InsertNewlineCommand>(editor.cx, editor.cy));
      break;

    case CTRL_KEY('q'):
      if (editor.dirty && editor.quit_times > 0) {
        editor.setStatusMessage("WARNING!!! Unsaved changes. "
          "Press Ctrl-Q %d more times to quit.", editor.quit_times);
        editor.quit_times--;
        return;
      }
      disableMouse(); 
      disableRawMode();
  termWrite("\x1b[2J", 4);
  termWrite("\x1b[H", 3);
      exit(0);

    case CTRL_KEY('s'):
      save();
      break;

    case HOME_KEY:
      editor.cx = 0;
      break;

    case END_KEY:
      if (editor.cy < editor.numrows)
        editor.cx = editor.row[editor.cy].size;
      break;

    case CTRL_KEY('f'):
      find();
      break;

    case BACKSPACE:
    case CTRL_KEY('h'):
    case DEL_KEY: {
      if (c == DEL_KEY) moveCursor(ARROW_RIGHT);
      bool wasNewline = (editor.cx == 0 && editor.cy > 0);
      char deleted = '\0';
      if (!wasNewline && editor.cx > 0)
        deleted = editor.row[editor.cy].chars[editor.cx - 1];
      editor.applyCommand(
        std::make_unique<DeleteCharCommand>(editor.cx, editor.cy, deleted, wasNewline));
      break;
    }

    case CTRL_KEY('z'):
      editor.undo();
      break;

    case CTRL_KEY('y'):
      editor.redo();
      break;

    case CTRL_KEY('n'):
      buffers.next();
      break;

    case CTRL_KEY('p'):
      buffers.prev();
      break;

    case PAGE_UP:
    case PAGE_DOWN: {
      if (c == PAGE_UP) editor.cy = editor.rowoff;
      else {
        editor.cy = editor.rowoff + screenrows - 1;
        if (editor.cy > editor.numrows) editor.cy = editor.numrows;
      }
      int times = screenrows;
      while (times--) moveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
      break;
    }

    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
      moveCursor(c);
      break;

    case CTRL_KEY('l'):
    case '\x1b':
      break;

    default:
      editor.applyCommand(
        std::make_unique<InsertCharCommand>(editor.cx, editor.cy, c));
      break;
  }
}