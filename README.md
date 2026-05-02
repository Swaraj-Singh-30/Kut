# Kut

A lightweight terminal text editor written in C++, inspired by [kilo](https://viewsourcecode.org/snaptoken/kilo/).

## Features
- Syntax highlighting for C/C++
- Search with `Ctrl-F`
- Save with `Ctrl-S`
- Line numbers in status bar
- Handles tabs, special characters, and multi-line comments

## Build
```bash
mkdir build
cd build
cmake ..
make
```

## Usage
```bash
./kut filename.cpp
```

Open multiple files at launch:
```bash
./kut file1.cpp file2.h file3.txt
```

Switch between open buffers:
- `Ctrl-N` → next buffer
- `Ctrl-P` → previous buffer

## Keybindings

| Key | Action |
|-----|--------|
| `Ctrl-S` | Save |
| `Ctrl-F` | Find |
| `Ctrl-Q` | Quit (3x if unsaved) |
| Arrow keys | Move cursor |
| `Page Up/Down` | Scroll |
| `Home/End` | Start/end of line |

## Architecture

The codebase is split into two clear layers:

- **`Editor`** — text buffer, cursor, file I/O, syntax highlighting. No terminal knowledge.
- **`Terminal`** — raw mode, rendering, input handling. Reads from `Editor` to draw state.

This separation means a GUI renderer can be added later without touching the editor core.

## Roadmap
- [x] Undo/redo
- [x] Multiple buffers
- [x] Config file (`~/.kutrc`)
- [x] Mouse support (click, scroll, tab switching)
- [x] Line numbers 
- [ ] GUI mode via Dear ImGui
- [ ] ncurses backend for broader terminal support

## Notes 
- It uses VT100 escape sequences
- It uses ANSI escape codes for colors 
- Inorder to support maximum amount of terminals, I will be using ncurses library, which uses the terminfo database to figure out the capabilities of a terminal and what escape sequences to use for that particular terminal. {right now it using VT100 escape sequences, which is supported by most terminals, but not all}

### What is the purpose of each file at this point of time of the project?

#### High-level flow
1. `main.cpp` loads config, creates the `BufferManager` and `Terminal`.
2. `Terminal` enables raw mode + mouse support and loads any files into buffers.
3. The main loop calls `Terminal::refreshScreen()` then `Terminal::processKeypress()`.

#### Core architecture
- **`Editor`** (text engine) — owns the text buffer, cursor state, file I/O, syntax highlighting, and undo/redo. It has no terminal-specific logic.
- **`Terminal`** (UI/input) — owns raw mode, rendering, cursor placement, and key/mouse handling. It reads `Editor` state to draw the screen.
- **`BufferManager`** (multi-buffer) — owns the list of `Editor` instances and manages tab switching.

#### Buffers (multiple files)
- `buffermanager.h/cpp` keeps a `std::vector<std::unique_ptr<Editor>>` and tracks the active index.
- `openFile()` reuses the current buffer if it’s empty, otherwise opens in a new buffer.
- `next()` / `prev()` cycle tabs; `Terminal` draws tabs via `drawTabBar()`.

#### Text model (rows + rendering)
- Each file is stored as an array of `erow` structs (one row per line).
- `erow::chars` holds raw text; `erow::render` expands tabs for display; `erow::hl` stores syntax highlight per rendered char.
- `Editor::updateRow()` rebuilds `render` + highlighting after changes.

#### Line numbers
- `Terminal::refreshScreen()` computes `lineNumWidth = digits(numrows) + 3` for the gutter.
- `Terminal::drawRows()` prepends the gutter (e.g. `"  12 | "`) and renders rows.
- Cursor placement offsets by `lineNumWidth` so the caret lines up with text.

#### Undo / redo
- `command.h/cpp` defines `Command` objects (`InsertChar`, `DeleteChar`, `InsertNewline`).
- `Editor::applyCommand()` executes and pushes to `undoStack`, clearing `redoStack`.
- `Editor::undo()` / `redo()` pop and re-execute via the command objects.
- Max history size: `KUT_MAX_UNDO` (500 commands).

#### Search
- `Terminal::find()` opens a prompt and uses `findCallback()` to search.
- Matches are highlighted temporarily and the cursor jumps to the match.

#### File I/O
- `Editor::openFile()` reads a file into rows.
- `Editor::rowsToString()` builds a full buffer for saving.
- `Terminal::save()` writes to disk and updates status.

#### Config
- `config.h/cpp` parses `~/.kutrc` (tab stop, quit confirmations, theme, line numbers).
- Values are applied to the active buffer on startup in `main.cpp`.

#### Status & messages
- `Editor::setStatusMessage()` sets the message.
- `Terminal::drawMessageBar()` renders it for 5 seconds.

#### File responsibilities (quick list)
- `main.cpp` — bootstraps config, buffer manager, terminal, and the main loop.
- `buffermanager.h/cpp` — multi-buffer management and tab switching.
- `editor.h/cpp` — text buffer, cursor, file I/O, syntax highlighting, undo/redo.
- `command.h/cpp` — command objects used by undo/redo.
- `terminal.h/cpp` — raw terminal I/O, rendering, cursor placement, key/mouse handling.
- `config.h/cpp` — configuration loading from `~/.kutrc`.
