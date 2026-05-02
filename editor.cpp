// #define _DEFAULT_SOURCE
// #define _BSD_SOURCE
// #define _GNU_SOURCE

#include "editor.h"

#include "command.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/*** filetypes ***/
const char *C_HL_extensions[] = { ".c", ".h", ".cpp", NULL };
const char *C_HL_keywords[] = {
  "switch", "if", "while", "for", "break", "continue", "return", "else",
  "struct", "union", "typedef", "static", "enum", "class", "case",
  "int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|",
  "void|", NULL
};

struct editorSyntax HLDB[] = {
  {
    "c",
    C_HL_extensions,
    C_HL_keywords,
    "//", "/*", "*/",
    HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
  },
};
#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

/*** lifecycle ***/
Editor::Editor() {
  cx = cy = rx = 0;
  rowoff = coloff = 0;
  numrows = 0;
  row = NULL;
  dirty = 0;
  filename = NULL;
  statusmsg[0] = '\0';
  statusmsg_time = 0;
  syntax = NULL; 
  tab_stop = 4;        
  quit_times = 3;     
  quit_times_cfg = 3; 
  line_numbers = true;
}

Editor::~Editor() {
  // free all rows
  for (int i = 0; i < numrows; i++)
    freeRow(&row[i]);
  free(row);
  free(filename);
}

void Editor::init() {
  // nothing here yet, but will keep it for future use
}

/*** syntax highlighting ***/
static int is_separator(int c) {
  return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];", c) != NULL;
}

void Editor::updateSyntax(erow *row) {
  row->hl = (unsigned char *)realloc(row->hl, row->rsize);
  memset(row->hl, HL_NORMAL, row->rsize);
  if (syntax == NULL) return;

  const char **keywords = syntax->keywords;
  const char *scs = syntax->singleline_comment_start;
  const char *mcs = syntax->multiline_comment_start;
  const char *mce = syntax->multiline_comment_end;

  int scs_len = scs ? strlen(scs) : 0;
  int mcs_len = mcs ? strlen(mcs) : 0;
  int mce_len = mce ? strlen(mce) : 0;

  int prev_sep = 1;
  int in_string = 0;
  int in_comment = (row->idx > 0 && this->row[row->idx - 1].hl_open_comment);

  int i = 0;
  while (i < row->rsize) {
    char c = row->render[i];
    unsigned char prev_hl = (i > 0) ? row->hl[i - 1] : (unsigned char)HL_NORMAL;

    if (scs_len && !in_string && !in_comment) {
      if (!strncmp(&row->render[i], scs, scs_len)) {
        memset(&row->hl[i], HL_COMMENT, row->rsize - i);
        break;
      }
    }
    if (mcs_len && mce_len && !in_string) {
      if (in_comment) {
        row->hl[i] = HL_MLCOMMENT;
        if (!strncmp(&row->render[i], mce, mce_len)) {
          memset(&row->hl[i], HL_MLCOMMENT, mce_len);
          i += mce_len;
          in_comment = 0;
          prev_sep = 1;
          continue;
        } else { i++; continue; }
      } else if (!strncmp(&row->render[i], mcs, mcs_len)) {
        memset(&row->hl[i], HL_MLCOMMENT, mcs_len);
        i += mcs_len;
        in_comment = 1;
        continue;
      }
    }
    if (syntax->flags & HL_HIGHLIGHT_STRINGS) {
      if (in_string) {
        row->hl[i] = HL_STRING;
        if (c == '\\' && i + 1 < row->rsize) {
          row->hl[i + 1] = HL_STRING;
          i += 2;
          continue;
        }
        if (c == in_string) in_string = 0;
        i++; prev_sep = 1; continue;
      } else {
        if (c == '"' || c == '\'') {
          in_string = c;
          row->hl[i] = HL_STRING;
          i++; continue;
        }
      }
    }
    if (syntax->flags & HL_HIGHLIGHT_NUMBERS) {
      if ((isdigit(c) && (prev_sep || prev_hl == HL_NUMBER)) ||
          (c == '.' && prev_hl == HL_NUMBER)) {
        row->hl[i] = HL_NUMBER;
        i++; prev_sep = 0; continue;
      }
    }
    if (prev_sep) {
      int j;
      for (j = 0; keywords[j]; j++) {
        int klen = strlen(keywords[j]);
        int kw2 = keywords[j][klen - 1] == '|';
        if (kw2) klen--;
        if (!strncmp(&row->render[i], keywords[j], klen) &&
            is_separator(row->render[i + klen])) {
          memset(&row->hl[i], kw2 ? HL_KEYWORD2 : HL_KEYWORD1, klen);
          i += klen;
          break;
        }
      }
      if (keywords[j] != NULL) { prev_sep = 0; continue; }
    }
    prev_sep = is_separator(c);
    i++;
  }
  int changed = (row->hl_open_comment != in_comment);
  row->hl_open_comment = in_comment;
  if (changed && row->idx + 1 < numrows)
    updateSyntax(&this->row[row->idx + 1]);
}

int Editor::syntaxToColor(int hl) {
  switch (hl) {
    case HL_COMMENT:
    case HL_MLCOMMENT: return 36;
    case HL_KEYWORD1:  return 33;
    case HL_KEYWORD2:  return 32;
    case HL_STRING:    return 35;
    case HL_NUMBER:    return 31;
    case HL_MATCH:     return 34;
    default:           return 37;
  }
}

void Editor::selectSyntaxHighlight() {
  syntax = NULL;
  if (filename == NULL) return;
  char *ext = strrchr(filename, '.');
  for (unsigned int j = 0; j < HLDB_ENTRIES; j++) {
    struct editorSyntax *s = &HLDB[j];
    unsigned int i = 0;
    while (s->filematch[i]) {
      int is_ext = (s->filematch[i][0] == '.');
      if ((is_ext && ext && !strcmp(ext, s->filematch[i])) ||
          (!is_ext && strstr(filename, s->filematch[i]))) {
        syntax = s;
        for (int filerow = 0; filerow < numrows; filerow++)
          updateSyntax(&this->row[filerow]);
        return;
      }
      i++;
    }
  }
}

/*** row operations ***/
int Editor::rowCxToRx(erow *row, int cx) {
  int rx = 0;
  for (int j = 0; j < cx; j++) {
    if (row->chars[j] == '\t')
      rx += (tab_stop - 1) - (rx % tab_stop);
    rx++;
  }
  return rx;
}

int Editor::rowRxToCx(erow *row, int rx) {
  int cur_rx = 0;
  int cx;
  for (cx = 0; cx < row->size; cx++) {
    if (row->chars[cx] == '\t')
      cur_rx += (tab_stop - 1) - (cur_rx % tab_stop);
    cur_rx++;
    if (cur_rx > rx) return cx;
  }
  return cx;
}

void Editor::updateRow(erow *row) {
  int tabs = 0;
  for (int j = 0; j < row->size; j++)
    if (row->chars[j] == '\t') tabs++;
  free(row->render);
  row->render = (char *)malloc(row->size + tabs * (tab_stop - 1) + 1);
  int idx = 0;
  for (int j = 0; j < row->size; j++) {
    if (row->chars[j] == '\t') {
      row->render[idx++] = ' ';
      while (idx % tab_stop != 0) row->render[idx++] = ' ';
    } else {
      row->render[idx++] = row->chars[j];
    }
  }
  row->render[idx] = '\0';
  row->rsize = idx;
  updateSyntax(row);
}

void Editor::insertRow(int at, const char *s, size_t len) {
  if (at < 0 || at > numrows) return;
  row = (erow *)realloc(row, sizeof(erow) * (numrows + 1));
  memmove(&row[at + 1], &row[at], sizeof(erow) * (numrows - at));
  for (int j = at + 1; j <= numrows; j++) row[j].idx++;
  row[at].idx = at;
  row[at].size = len;
  row[at].chars = (char *)malloc(len + 1);
  memcpy(row[at].chars, s, len);
  row[at].chars[len] = '\0';
  row[at].rsize = 0;
  row[at].render = NULL;
  row[at].hl = NULL;
  row[at].hl_open_comment = 0;
  updateRow(&row[at]);
  numrows++;
  dirty++;
}

void Editor::freeRow(erow *row) {
  free(row->render);
  free(row->chars);
  free(row->hl);
}

void Editor::delRow(int at) {
  if (at < 0 || at >= numrows) return;
  freeRow(&row[at]);
  memmove(&row[at], &row[at + 1], sizeof(erow) * (numrows - at - 1));
  for (int j = at; j < numrows - 1; j++) row[j].idx--;
  numrows--;
  dirty++;
}

void Editor::rowInsertChar(erow *row, int at, int c) {
  if (at < 0 || at > row->size) at = row->size;
  row->chars = (char *)realloc(row->chars, row->size + 2);
  memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
  row->size++;
  row->chars[at] = c;
  updateRow(row);
  dirty++;
}

void Editor::rowAppendString(erow *row, char *s, size_t len) {
  row->chars = (char *)realloc(row->chars, row->size + len + 1);
  memcpy(&row->chars[row->size], s, len);
  row->size += len;
  row->chars[row->size] = '\0';
  updateRow(row);
  dirty++;
}

void Editor::rowDelChar(erow *row, int at) {
  if (at < 0 || at >= row->size) return;
  memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
  row->size--;
  updateRow(row);
  dirty++;
}

/*** editor operations ***/
void Editor::insertChar(int c) {
  if (cy == numrows) insertRow(numrows, "", 0);
  rowInsertChar(&row[cy], cx, c);
  cx++;
}

void Editor::insertNewline() {
  if (cx == 0) {
    insertRow(cy, "", 0);
  } else {
    erow *r = &row[cy];
    insertRow(cy + 1, &r->chars[cx], r->size - cx);
    r = &row[cy];
    r->size = cx;
    r->chars[r->size] = '\0';
    updateRow(r);
  }
  cy++;
  cx = 0;
}

void Editor::delChar() {
  if (cy == numrows) return;
  if (cx == 0 && cy == 0) return;
  erow *r = &row[cy];
  if (cx > 0) {
    rowDelChar(r, cx - 1);
    cx--;
  } else {
    cx = row[cy - 1].size;
    rowAppendString(&row[cy - 1], r->chars, r->size);
    delRow(cy);
    cy--;
  }
}

void Editor::applyCommand(std::unique_ptr<Command> cmd) {
  cmd->execute(*this);
  undoStack.push_back(std::move(cmd));

  // drop oldest if over limit
  if (undoStack.size() > KUT_MAX_UNDO)
    undoStack.pop_front();

  // clear redo
  redoStack.clear();
}

void Editor::undo() {
  if (undoStack.empty()) return;
  undoStack.back()->undo(*this);
  redoStack.push_back(std::move(undoStack.back()));
  undoStack.pop_back();
}

void Editor::redo() {
  if (redoStack.empty()) return;
  redoStack.back()->execute(*this);
  undoStack.push_back(std::move(redoStack.back()));
  redoStack.pop_back();
}


/*** file i/o ***/
char *Editor::rowsToString(int *buflen) {
  int totlen = 0;
  for (int j = 0; j < numrows; j++)
    totlen += row[j].size + 1;
  *buflen = totlen;
  char *buf = (char *)malloc(totlen);
  char *p = buf;
  for (int j = 0; j < numrows; j++) {
    memcpy(p, row[j].chars, row[j].size);
    p += row[j].size;
    *p = '\n';
    p++;
  }
  return buf;
}

void Editor::openFile(const char *fname) {
  free(filename);
  filename = strdup(fname);
  selectSyntaxHighlight();
  FILE *fp = fopen(fname, "r");
  if (!fp) {
    if (errno == ENOENT) {
      setStatusMessage("New file: %s", fname);
      return;
    }
    setStatusMessage("Can't open %s: %s", fname, strerror(errno));
    return;
  }
  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;
  while ((linelen = getline(&line, &linecap, fp)) != -1) {
    while (linelen > 0 && (line[linelen-1] == '\n' || line[linelen-1] == '\r'))
      linelen--;
    insertRow(numrows, line, linelen);
  }
  free(line);
  fclose(fp);
  dirty = 0;
}

/*** status message ***/
void Editor::setStatusMessage(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(statusmsg, sizeof(statusmsg), fmt, ap);
  va_end(ap);
  statusmsg_time = time(NULL);
}