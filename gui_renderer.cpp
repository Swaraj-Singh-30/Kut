#include "gui_renderer.h"

#include "command.h"
#include "editor.h"

#include <SFML/Graphics.hpp>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace {
struct GlyphInfo {
  sf::IntRect rect;
  float advance = 0.0f;
  float left = 0.0f;
  float top = 0.0f;
  float width = 0.0f;
  float height = 0.0f;
};

struct Theme {
  sf::Color background = sf::Color(0x28, 0x2C, 0x34);
  sf::Color text = sf::Color(0xAB, 0xB2, 0xBF);
  sf::Color keyword1 = sf::Color(0xE0, 0x6C, 0x75);
  sf::Color keyword2 = sf::Color(0x61, 0xAF, 0xEF);
  sf::Color string = sf::Color(0x98, 0xC3, 0x79);
  sf::Color number = sf::Color(0xD1, 0x9A, 0x66);
  sf::Color comment = sf::Color(0x5C, 0x63, 0x70);
  sf::Color match = sf::Color(0xE5, 0xC0, 0x7B);
  sf::Color lineNumber = sf::Color(0x4B, 0x52, 0x63);
  sf::Color lineNumberActive = sf::Color(0xAB, 0xB2, 0xBF);
  sf::Color tabBar = sf::Color(0x21, 0x25, 0x2B);
  sf::Color tabActive = sf::Color(0x28, 0x2C, 0x34);
  sf::Color tabInactiveText = sf::Color(0x5C, 0x63, 0x70);
  sf::Color tabAccent = sf::Color(0x61, 0xAF, 0xEF);
  sf::Color statusBar = sf::Color(0x21, 0x25, 0x2B);
  sf::Color statusText = sf::Color(0x9D, 0xA5, 0xB4);
  sf::Color cursor = sf::Color(0x52, 0x8B, 0xFF);
  sf::Color separator = sf::Color(0x3E, 0x44, 0x51);
  sf::Color currentLine = sf::Color(0x2C, 0x31, 0x3A);
};

int countDigits(int n) {
  if (n == 0) return 1;
  int d = 0;
  while (n > 0) {
    n /= 10;
    d++;
  }
  return d;
}

std::string baseName(const char *path) {
  if (!path) return "[No Name]";
  std::string p(path);
  size_t slash = p.find_last_of("/\\");
  if (slash == std::string::npos) return p;
  return p.substr(slash + 1);
}

sf::Color colorForHL(unsigned char hl, const Theme &theme) {
  switch (hl) {
    case HL_COMMENT:
    case HL_MLCOMMENT:
      return theme.comment;
    case HL_KEYWORD1:
      return theme.keyword1;
    case HL_KEYWORD2:
      return theme.keyword2;
    case HL_STRING:
      return theme.string;
    case HL_NUMBER:
      return theme.number;
    case HL_MATCH:
      return theme.match;
    default:
      return theme.text;
  }
}

bool loadFont(sf::Font &font) {
  std::vector<std::string> candidates;
  candidates.push_back("JetBrainsMono-Regular.ttf");
  candidates.push_back((std::filesystem::current_path() / "JetBrainsMono-Regular.ttf").string());
#ifdef _WIN32
  candidates.push_back("C:\\Windows\\Fonts\\consola.ttf");
#elif __APPLE__
  candidates.push_back("/Library/Fonts/Menlo.ttc");
  candidates.push_back("/System/Library/Fonts/Menlo.ttc");
#else
  candidates.push_back("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf");
  candidates.push_back("/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf");
#endif

  for (const auto &path : candidates) {
    if (!std::filesystem::exists(path)) continue;
    if (font.loadFromFile(path)) {
      return true;
    }
  }
  return false;
}
}

GUIRenderer::GUIRenderer(BufferManager &buffersRef, Config &configRef)
  : buffers(buffersRef), config(configRef) {}

void GUIRenderer::run() {
  Theme theme;
  const unsigned int fontSize = 14;
  const float lineHeight = 20.0f;
  const float tabBarHeight = 32.0f;
  const float statusBarHeight = 24.0f;
  const float searchBarHeight = 24.0f;
  const float cursorWidth = 2.0f;

  sf::RenderWindow window(sf::VideoMode(1200, 800), "Kut", sf::Style::Default);
  window.setFramerateLimit(60);

  sf::Font font;
  if (!loadFont(font)) {
    fprintf(stderr, "Failed to load a monospace font. Place JetBrainsMono-Regular.ttf next to the executable.\n");
    return;
  }

  const sf::Texture &fontTexture = font.getTexture(fontSize);
  std::vector<GlyphInfo> glyphs(127);
  for (int c = 32; c <= 126; ++c) {
    sf::Glyph g = font.getGlyph(static_cast<sf::Uint32>(c), fontSize, false);
    glyphs[c].rect = g.textureRect;
    glyphs[c].advance = g.advance;
    glyphs[c].left = g.bounds.left;
    glyphs[c].top = g.bounds.top;
    glyphs[c].width = g.bounds.width;
    glyphs[c].height = g.bounds.height;
  }
  float charWidth = glyphs['M'].advance > 0 ? glyphs['M'].advance : glyphs[' '].advance;
  float lineSpacing = font.getLineSpacing(fontSize);
  float ascent = -font.getGlyph('A', fontSize, false).bounds.top;
  float baselineOffset = (lineHeight - lineSpacing) / 2.0f + ascent;

  sf::Clock blinkClock;
  bool cursorVisible = true;
  bool windowFocused = true;

  bool searchMode = false;
  std::string searchQuery;

  auto ensureCursorVisible = [&](Editor &editor, int visibleRows, int visibleCols) {
    editor.rx = 0;
    if (editor.cy < editor.numrows)
      editor.rx = editor.rowCxToRx(&editor.row[editor.cy], editor.cx);

    if (editor.cy < editor.rowoff) editor.rowoff = editor.cy;
    if (editor.cy >= editor.rowoff + visibleRows)
      editor.rowoff = editor.cy - visibleRows + 1;
    if (editor.rx < editor.coloff) editor.coloff = editor.rx;
    if (editor.rx >= editor.coloff + visibleCols)
      editor.coloff = editor.rx - visibleCols + 1;
  };

  auto handleQuit = [&](Editor &editor) {
    if (editor.dirty && editor.quit_times > 0) {
      editor.setStatusMessage("WARNING!!! Unsaved changes. Press Ctrl-Q %d more times to quit.",
                              editor.quit_times);
      editor.quit_times--;
      return false;
    }
    return true;
  };

  auto saveBuffer = [&](Editor &editor) {
    if (!editor.filename) {
      editor.setStatusMessage("Save aborted (no filename)");
      return;
    }
    int len = 0;
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
  };

  auto findNextMatch = [&](Editor &editor, const std::string &query) {
    if (query.empty()) return;
    int current = editor.cy;
    for (int i = 0; i < editor.numrows; i++) {
      current++;
      if (current >= editor.numrows) current = 0;
      erow *row = &editor.row[current];
      char *match = strstr(row->render, query.c_str());
      if (match) {
        editor.cy = current;
        editor.cx = editor.rowRxToCx(row, match - row->render);
        return;
      }
    }
  };

  auto moveCursor = [&](Editor &editor, int key) {
    erow *row = (editor.cy >= editor.numrows) ? nullptr : &editor.row[editor.cy];
    switch (key) {
      case ARROW_LEFT:
        if (editor.cx != 0) {
          editor.cx--;
        } else if (editor.cy > 0) {
          editor.cy--;
          editor.cx = editor.row[editor.cy].size;
        }
        break;
      case ARROW_RIGHT:
        if (row && editor.cx < row->size) {
          editor.cx++;
        } else if (row && editor.cx == row->size) {
          editor.cy++;
          editor.cx = 0;
        }
        break;
      case ARROW_UP:
        if (editor.cy != 0) editor.cy--;
        break;
      case ARROW_DOWN:
        if (editor.cy < editor.numrows) editor.cy++;
        break;
    }
    row = (editor.cy >= editor.numrows) ? nullptr : &editor.row[editor.cy];
    int rowlen = row ? row->size : 0;
    if (editor.cx > rowlen) editor.cx = rowlen;
  };

  while (window.isOpen()) {
    Editor &editor = buffers.current();

    sf::Event event;
    while (window.pollEvent(event)) {
      if (event.type == sf::Event::Closed) {
        if (handleQuit(editor)) window.close();
      }
      if (event.type == sf::Event::GainedFocus) {
        windowFocused = true;
      }
      if (event.type == sf::Event::LostFocus) {
        windowFocused = false;
      }
      if (event.type == sf::Event::MouseWheelScrolled) {
        if (event.mouseWheelScroll.wheel == sf::Mouse::VerticalWheel) {
          int delta = static_cast<int>(event.mouseWheelScroll.delta);
          editor.rowoff -= delta * 3;
          if (editor.rowoff < 0) editor.rowoff = 0;
          int maxRowoff = std::max(0, editor.numrows - 1);
          if (editor.rowoff > maxRowoff) editor.rowoff = maxRowoff;
        }
      }
      if (event.type == sf::Event::MouseButtonPressed) {
        if (event.mouseButton.button == sf::Mouse::Left) {
          float mx = static_cast<float>(event.mouseButton.x);
          float my = static_cast<float>(event.mouseButton.y);

          int totalRows = editor.numrows;
          int gutterChars = editor.line_numbers ? countDigits(totalRows) + 3 : 0;
          float gutterWidth = gutterChars * charWidth;
          float separatorWidth = editor.line_numbers ? 1.0f : 0.0f;

          float tabBottom = tabBarHeight;
          float statusTop = static_cast<float>(window.getSize().y) - statusBarHeight;
          float searchTop = statusTop - (searchMode ? searchBarHeight : 0.0f);

          if (my <= tabBottom) {
            // tab clicks
            float x = 0.0f;
            const auto &bufs = buffers.getBuffers();
            for (int i = 0; i < (int)bufs.size(); i++) {
              std::string name = baseName(bufs[i]->filename);
              if (bufs[i]->dirty) name += " *";
              float tabWidth = (name.size() + 2) * charWidth + 32.0f;
              float closeSize = 12.0f;
              float closePadding = 6.0f;
              float closeX = x + tabWidth - closeSize - 10.0f;
              float closeY = (tabBarHeight - closeSize) / 2.0f;
              if (mx >= closeX - closePadding && mx <= closeX + closeSize + closePadding
                  && my >= closeY - closePadding && my <= closeY + closeSize + closePadding) {
                buffers.setActive(i);
                buffers.closeBuffer();
                break;
              }
              if (mx >= x && mx <= x + tabWidth) {
                buffers.setActive(i);
                break;
              }
              x += tabWidth;
            }
          } else if (my >= tabBottom && my < searchTop) {
            float textStartX = gutterWidth + separatorWidth + 8.0f;
            float textStartY = tabBarHeight;
            int clickedRow = static_cast<int>((my - textStartY) / lineHeight);
            int filerow = clickedRow + editor.rowoff;
            if (filerow >= 0 && filerow < editor.numrows) {
              float rx = (mx - textStartX) / charWidth;
              if (rx < 0) rx = 0;
              editor.cy = filerow;
              editor.cx = editor.rowRxToCx(&editor.row[filerow], static_cast<int>(rx) + editor.coloff);
            }
          }
        }
      }
      if (event.type == sf::Event::KeyPressed) {
        bool ctrl = event.key.control;
        if (ctrl && event.key.code == sf::Keyboard::S) {
          saveBuffer(editor);
        } else if (ctrl && event.key.code == sf::Keyboard::Z) {
          editor.undo();
        } else if (ctrl && event.key.code == sf::Keyboard::Y) {
          editor.redo();
        } else if (ctrl && event.key.code == sf::Keyboard::F) {
          searchMode = true;
          searchQuery.clear();
        } else if (ctrl && event.key.code == sf::Keyboard::Q) {
          if (handleQuit(editor)) window.close();
        } else if (ctrl && event.key.code == sf::Keyboard::N) {
          buffers.next();
        } else if (ctrl && event.key.code == sf::Keyboard::P) {
          buffers.prev();
        } else if (event.key.code == sf::Keyboard::Escape) {
          searchMode = false;
        } else if (event.key.code == sf::Keyboard::Enter) {
          if (searchMode) {
            findNextMatch(editor, searchQuery);
            searchMode = false;
          } else {
            editor.applyCommand(
              std::make_unique<InsertNewlineCommand>(editor.cx, editor.cy));
          }
        } else if (event.key.code == sf::Keyboard::Backspace) {
          if (searchMode) {
            if (!searchQuery.empty()) searchQuery.pop_back();
          } else {
            bool wasNewline = (editor.cx == 0 && editor.cy > 0);
            char deleted = '\0';
            if (!wasNewline && editor.cx > 0)
              deleted = editor.row[editor.cy].chars[editor.cx - 1];
            editor.applyCommand(
              std::make_unique<DeleteCharCommand>(editor.cx, editor.cy, deleted, wasNewline));
          }
        } else if (event.key.code == sf::Keyboard::Delete) {
          if (!searchMode) {
            moveCursor(editor, ARROW_RIGHT);
            bool wasNewline = (editor.cx == 0 && editor.cy > 0);
            char deleted = '\0';
            if (!wasNewline && editor.cx > 0)
              deleted = editor.row[editor.cy].chars[editor.cx - 1];
            editor.applyCommand(
              std::make_unique<DeleteCharCommand>(editor.cx, editor.cy, deleted, wasNewline));
          }
        } else if (event.key.code == sf::Keyboard::Up) {
          moveCursor(editor, ARROW_UP);
        } else if (event.key.code == sf::Keyboard::Down) {
          moveCursor(editor, ARROW_DOWN);
        } else if (event.key.code == sf::Keyboard::Left) {
          moveCursor(editor, ARROW_LEFT);
        } else if (event.key.code == sf::Keyboard::Right) {
          moveCursor(editor, ARROW_RIGHT);
        } else if (event.key.code == sf::Keyboard::PageUp) {
          editor.cy = editor.rowoff;
        } else if (event.key.code == sf::Keyboard::PageDown) {
          editor.cy = editor.rowoff + static_cast<int>(window.getSize().y / lineHeight) - 1;
          if (editor.cy > editor.numrows) editor.cy = editor.numrows;
        } else if (event.key.code == sf::Keyboard::Home) {
          editor.cx = 0;
        } else if (event.key.code == sf::Keyboard::End) {
          if (editor.cy < editor.numrows)
            editor.cx = editor.row[editor.cy].size;
        } else if (event.key.code == sf::Keyboard::Tab) {
          if (!searchMode) {
            editor.applyCommand(
              std::make_unique<InsertCharCommand>(editor.cx, editor.cy, '\t'));
          }
        }
      }
      if (event.type == sf::Event::TextEntered) {
        if (!searchMode) {
          if (event.text.unicode >= 32 && event.text.unicode < 127) {
            editor.applyCommand(std::make_unique<InsertCharCommand>(
              editor.cx, editor.cy, static_cast<int>(event.text.unicode)));
          }
        } else {
          if (event.text.unicode >= 32 && event.text.unicode < 127) {
            searchQuery.push_back(static_cast<char>(event.text.unicode));
          }
        }
      }
    }

    float windowWidth = static_cast<float>(window.getSize().x);
    float windowHeight = static_cast<float>(window.getSize().y);

    int gutterChars = editor.line_numbers ? countDigits(editor.numrows) + 3 : 0;
    float gutterWidth = gutterChars * charWidth;
    float separatorWidth = editor.line_numbers ? 1.0f : 0.0f;
    float textStartX = gutterWidth + separatorWidth + 8.0f;
    float textStartY = tabBarHeight;

    float textAreaHeight = windowHeight - tabBarHeight - statusBarHeight - (searchMode ? searchBarHeight : 0.0f);
    int visibleRows = static_cast<int>(textAreaHeight / lineHeight);
    int visibleCols = std::max(1, static_cast<int>((windowWidth - textStartX - 8.0f) / charWidth));
    if (visibleRows < 1) visibleRows = 1;

    ensureCursorVisible(editor, visibleRows, visibleCols);

    if (windowFocused && blinkClock.getElapsedTime().asMilliseconds() > 500) {
      cursorVisible = !cursorVisible;
      blinkClock.restart();
    }
    if (!windowFocused) cursorVisible = false;

    window.clear(theme.background);

    // Tab bar
    sf::RectangleShape tabBar(sf::Vector2f(windowWidth, tabBarHeight));
    tabBar.setFillColor(theme.tabBar);
    window.draw(tabBar);

    float tabX = 0.0f;
    const auto &bufs = buffers.getBuffers();
    for (int i = 0; i < (int)bufs.size(); i++) {
      std::string name = baseName(bufs[i]->filename);
      if (bufs[i]->dirty) name += " *";
      float tabWidth = (name.size() + 2) * charWidth + 32.0f;
      sf::RectangleShape tabRect(sf::Vector2f(tabWidth, tabBarHeight));
      tabRect.setPosition(tabX, 0.0f);
      tabRect.setFillColor(i == buffers.activeIndex() ? theme.tabActive : theme.tabBar);
      window.draw(tabRect);

      if (i == buffers.activeIndex()) {
        sf::RectangleShape accent(sf::Vector2f(tabWidth, 2.0f));
        accent.setPosition(tabX, 0.0f);
        accent.setFillColor(theme.tabAccent);
        window.draw(accent);
      }

      sf::Text tabText(name, font, fontSize);
      tabText.setFillColor(i == buffers.activeIndex() ? theme.text : theme.tabInactiveText);
      tabText.setPosition(tabX + 8.0f, 6.0f);
      window.draw(tabText);

  float closeSize = 12.0f;
  float closeX = tabX + tabWidth - closeSize - 10.0f;
  float closeY = (tabBarHeight - closeSize) / 2.0f;
  sf::VertexArray closeLines(sf::Lines, 4);
  closeLines[0].position = sf::Vector2f(closeX, closeY);
  closeLines[1].position = sf::Vector2f(closeX + closeSize, closeY + closeSize);
  closeLines[2].position = sf::Vector2f(closeX + closeSize, closeY);
  closeLines[3].position = sf::Vector2f(closeX, closeY + closeSize);
  closeLines[0].color = theme.tabInactiveText;
  closeLines[1].color = theme.tabInactiveText;
  closeLines[2].color = theme.tabInactiveText;
  closeLines[3].color = theme.tabInactiveText;
  window.draw(closeLines);

      tabX += tabWidth;
    }

    // Current line highlight
    if (editor.cy >= editor.rowoff && editor.cy < editor.rowoff + visibleRows) {
      int screenRow = editor.cy - editor.rowoff;
      sf::RectangleShape highlight(sf::Vector2f(windowWidth, lineHeight));
      highlight.setPosition(0.0f, textStartY + screenRow * lineHeight);
      highlight.setFillColor(theme.currentLine);
      window.draw(highlight);
    }

    // Line numbers + separator
    if (editor.line_numbers) {
      sf::RectangleShape sep(sf::Vector2f(separatorWidth, textAreaHeight));
      sep.setPosition(gutterWidth, textStartY);
      sep.setFillColor(theme.separator);
      window.draw(sep);
    }

    sf::VertexArray vertices(sf::Quads);
    vertices.clear();

    auto addGlyph = [&](int ch, float x, float y, const sf::Color &color) {
      if (ch < 32 || ch > 126) return;
      const GlyphInfo &g = glyphs[ch];
      if (g.width == 0.0f || g.height == 0.0f) return;
      float x0 = x + g.left;
      float y0 = y + g.top;
      float x1 = x0 + g.width;
      float y1 = y0 + g.height;

      sf::Vertex v0(sf::Vector2f(x0, y0), color, sf::Vector2f(g.rect.left, g.rect.top));
      sf::Vertex v1(sf::Vector2f(x1, y0), color, sf::Vector2f(g.rect.left + g.rect.width, g.rect.top));
      sf::Vertex v2(sf::Vector2f(x1, y1), color, sf::Vector2f(g.rect.left + g.rect.width, g.rect.top + g.rect.height));
      sf::Vertex v3(sf::Vector2f(x0, y1), color, sf::Vector2f(g.rect.left, g.rect.top + g.rect.height));
      vertices.append(v0);
      vertices.append(v1);
      vertices.append(v2);
      vertices.append(v3);
    };

    for (int y = 0; y < visibleRows; y++) {
      int filerow = y + editor.rowoff;
      float lineY = textStartY + y * lineHeight;
      float baseline = lineY + baselineOffset;

      if (filerow >= editor.numrows) {
        if (editor.line_numbers) {
          std::string gutter = "~";
          float x = gutterWidth - (gutterChars - 1) * charWidth;
          addGlyph('~', x, baseline, theme.lineNumber);
        }
        continue;
      }

      // line number
      if (editor.line_numbers) {
        std::ostringstream oss;
        oss << (filerow + 1);
        std::string num = oss.str();
        float numX = gutterWidth - charWidth * 3 - num.size() * charWidth;
        sf::Color numColor = (filerow == editor.cy) ? theme.lineNumberActive : theme.lineNumber;
        for (char ch : num) {
          addGlyph(ch, numX, baseline, numColor);
          numX += charWidth;
        }
        addGlyph(' ', gutterWidth - charWidth * 2, baseline, numColor);
        addGlyph('|', gutterWidth - charWidth, baseline, theme.separator);
      }

      erow *row = &editor.row[filerow];
      int len = row->rsize - editor.coloff;
      if (len < 0) len = 0;
      if (len > visibleCols) len = visibleCols;
      char *c = &row->render[editor.coloff];
      unsigned char *hl = &row->hl[editor.coloff];

      // search highlights
      if (!searchQuery.empty()) {
        std::string renderStr = row->render;
        size_t pos = renderStr.find(searchQuery);
        while (pos != std::string::npos) {
          int start = static_cast<int>(pos);
          int end = start + static_cast<int>(searchQuery.size());
          if (end >= editor.coloff && start <= editor.coloff + len) {
            int visibleStart = std::max(start - editor.coloff, 0);
            int visibleEnd = std::min(end - editor.coloff, len);
            sf::RectangleShape matchRect(sf::Vector2f((visibleEnd - visibleStart) * charWidth, lineHeight));
            matchRect.setPosition(textStartX + visibleStart * charWidth, lineY);
            sf::Color matchColor = theme.match;
            matchColor.a = 80;
            matchRect.setFillColor(matchColor);
            window.draw(matchRect);
          }
          pos = renderStr.find(searchQuery, pos + 1);
        }
      }

      for (int j = 0; j < len; j++) {
        int ch = c[j];
        if (ch < 32 || ch > 126) ch = ' ';
        sf::Color color = colorForHL(hl[j], theme);
        float x = textStartX + j * charWidth;
        addGlyph(ch, x, baseline, color);
      }
    }

    window.draw(vertices, &fontTexture);

    // Status bar
    float statusTop = windowHeight - statusBarHeight;
    sf::RectangleShape status(sf::Vector2f(windowWidth, statusBarHeight));
    status.setPosition(0.0f, statusTop);
    status.setFillColor(theme.statusBar);
    window.draw(status);

    std::ostringstream statusLeft;
    statusLeft << baseName(editor.filename) << " - " << editor.numrows << " lines";
    if (editor.dirty) statusLeft << " (modified)";
    sf::Text statusText(statusLeft.str(), font, fontSize);
    statusText.setFillColor(theme.statusText);
    statusText.setPosition(8.0f, statusTop + 4.0f);
    window.draw(statusText);

    std::ostringstream statusRight;
    statusRight << (editor.syntax ? editor.syntax->filetype : "no ft")
                << " | " << (editor.cy + 1) << "/" << editor.numrows;
    sf::Text rightText(statusRight.str(), font, fontSize);
    rightText.setFillColor(theme.statusText);
    float rightWidth = rightText.getLocalBounds().width;
    rightText.setPosition(windowWidth - rightWidth - 8.0f, statusTop + 4.0f);
    window.draw(rightText);

    if (searchMode) {
      float searchTop = statusTop - searchBarHeight;
      sf::RectangleShape searchBar(sf::Vector2f(windowWidth, searchBarHeight));
      searchBar.setPosition(0.0f, searchTop);
      searchBar.setFillColor(theme.statusBar);
      window.draw(searchBar);

      std::string prompt = "Search: " + searchQuery;
      sf::Text searchText(prompt, font, fontSize);
      searchText.setFillColor(theme.statusText);
      searchText.setPosition(8.0f, searchTop + 4.0f);
      window.draw(searchText);
    }

    // Cursor
    if (windowFocused && cursorVisible && editor.cy >= editor.rowoff && editor.cy < editor.rowoff + visibleRows) {
      int screenRow = editor.cy - editor.rowoff;
      float cursorY = textStartY + screenRow * lineHeight + 2.0f;
      float cursorX = textStartX + (editor.rx - editor.coloff) * charWidth;
      sf::RectangleShape cursor(sf::Vector2f(cursorWidth, lineHeight - 4.0f));
      cursor.setPosition(cursorX, cursorY);
      cursor.setFillColor(theme.cursor);
      window.draw(cursor);
    }

    window.display();
  }
}
