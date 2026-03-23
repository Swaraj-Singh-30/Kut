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
- [ ] Undo/redo
- [ ] Multiple buffers
- [ ] Config file (`~/.kutrc`)
- [ ] GUI mode via Dear ImGui
- [ ] ncurses backend for broader terminal support

## Notes 
- It uses VT100 escape sequences
- It uses ANSI escape codes for colors 
- Inorder to support maximum amount of terminals, I will be using ncurses library, which uses the terminfo database to figure out the capabilities of a terminal and what escape sequences to use for that particular terminal. {right now it using VT100 escape sequences, which is supported by most terminals, but not all}