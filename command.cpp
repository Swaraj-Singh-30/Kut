#include "command.h"
#include "editor.h"

/*** InsertCharCommand ***/
void InsertCharCommand::execute(Editor &e) {
  // move cursor to where insert should happen then insert
  e.cx = cx;
  e.cy = cy;
  e.insertChar(ch);
}

void InsertCharCommand::undo(Editor &e) {
  // move cursor to where the char ended up (one ahead) and delete it
  e.cx = cx + 1;
  e.cy = cy;
  e.delChar();
}

/*** DeleteCharCommand ***/
void DeleteCharCommand::execute(Editor &e) {
  e.cx = cx;
  e.cy = cy;
  e.delChar();
}

void DeleteCharCommand::undo(Editor &e) {
  if (wasNewline) {
    // undo a newline merge
    e.cx = cx;
    e.cy = cy - 1;
    e.insertNewline();
  } else {
    // put the deleted character back
    e.cx = cx - 1;
    e.cy = cy;
    e.insertChar(deleted);
    // insertChar moves cx forward, put it back
    e.cx = cx - 1;
  }
}

/*** InsertNewlineCommand ***/
void InsertNewlineCommand::execute(Editor &e) {
  e.cx = cx;
  e.cy = cy;
  e.insertNewline();
}

void InsertNewlineCommand::undo(Editor &e) {
  // undo a newline: delete the line break by going to
  // start of the new line and hitting backspace
  e.cx = 0;
  e.cy = cy + 1;
  e.delChar();
}