#pragma once

#include <stack>
#include <memory>

class Editor; // forward declaration, breaks circular include

/*** Abstract base ***/
class Command {
public:
  virtual void execute(Editor &e) = 0;
  virtual void undo(Editor &e)    = 0;
  virtual ~Command() = default;
};

/*** Concrete commands ***/
class InsertCharCommand : public Command {
public:
  InsertCharCommand(int cx, int cy, int ch) : cx(cx), cy(cy), ch(ch) {}
  void execute(Editor &e) override;
  void undo(Editor &e)    override;
private:
  int cx, cy, ch;
};

class DeleteCharCommand : public Command {
public:
  DeleteCharCommand(int cx, int cy, char deleted, bool wasNewline)
    : cx(cx), cy(cy), deleted(deleted), wasNewline(wasNewline) {}
  void execute(Editor &e) override;
  void undo(Editor &e)    override;
private:
  int  cx, cy;
  char deleted;
  bool wasNewline; // true if backspace merged two lines
};

class InsertNewlineCommand : public Command {
public:
  InsertNewlineCommand(int cx, int cy) : cx(cx), cy(cy) {}
  void execute(Editor &e) override;
  void undo(Editor &e)    override;
private:
  int cx, cy;
};

/*** Command history ***/
using CommandStack = std::deque<std::unique_ptr<Command>>;
#define KUT_MAX_UNDO 500