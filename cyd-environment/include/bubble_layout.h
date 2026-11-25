#pragma once
#include <stddef.h>
#include <string.h>

struct BubbleLine {
  size_t start = 0;
  size_t end = 0;
  bool ellipsis = false;
};

// The display font has proportional glyphs. Keep whole UTF-8 characters and
// only reserve room for the ellipsis when there really is another line.
template <class Measure>
int layoutBubble(const char* text, int width, BubbleLine (&lines)[3], Measure measure) {
  size_t pos = 0;
  for (int row = 0; row < 3; ++row) {
    BubbleLine& line = lines[row];
    line.start = pos;
    size_t dotsEnd = pos;
    int used = 0;
    const int dotsWidth = measure("…");
    while (text[pos] && text[pos] != '\n' && text[pos] != '\r') {
      size_t end = pos + 1;
      while ((static_cast<unsigned char>(text[end]) & 0xc0) == 0x80) ++end;
      char glyph[5] = {};
      size_t bytes = end - pos;
      if (bytes > 4) { end = pos + 1; bytes = 1; }
      memcpy(glyph, text + pos, bytes);
      int advance = measure(glyph);
      if (used + advance > width) break;
      used += advance;
      pos = end;
      if (used + dotsWidth <= width) dotsEnd = pos;
    }
    line.end = pos;
    if (text[pos] == '\r') { ++pos; if (text[pos] == '\n') ++pos; }
    else if (text[pos] == '\n') ++pos;
    if (!text[pos]) return row + 1;
    if (row == 2) { line.end = dotsEnd; line.ellipsis = true; }
  }
  return 3;
}
