Just trying to make a text editor

Future goals
- [ ] Inorder to support maximum amount of terminals, I will be using ncurses library, which uses the terminfo database to figure out the capabilities of a terminal and what escape sequences to use for that particular terminal. {right now it using VT100 escape sequences, which is supported by most terminals, but not all}

Notes 
- It uses VT100 escape sequences
- It uses ANSI escape codes for colors 
