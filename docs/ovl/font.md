# Font items (fct)

Parser: `src/ovl/font.hpp`. A font holds a list of glyph rows over one or more texture pages.

- **Metrics** are three fractions of the font's cell size. The first is one over the cell size in
  pixels. The second is the height of a line and the third the distance from the bottom of a line to
  the baseline. Layout multiplies them by the text size in pixels.
- **Flag word**: 1 means the glyph data in the file is used as is. 0 means the face is meant to be
  drawn by the system when the named typeface is installed, and the file data is the fallback.
- **Glyph advance** is a fraction of the cell size and the **lead** is added after the glyph on top of
  it (often 0 or 1/16 of a cell, occasionally negative). It is not a kerning pair.
- **Shift** places the glyph's rectangle: the first value is the offset of its left edge from the pen
  position, the second the offset of its bottom edge above the baseline, in texels.
- **Texture rectangle** is {u0, v0, u1, v1}, normalised, on a 256-texel page, so every edge is a whole texel.
  Pages are stored bottom row first, so v grows towards the top of a glyph on screen.
- A rectangle with no area is a glyph that draws nothing but still advances.
