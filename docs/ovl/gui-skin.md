# GUI skin items (gsi)

A skin item says which parts of a texture make up a stretchable panel or button. The parser is
`src/ovl/gui_skin.hpp`.

Layout: a `u32` kind, a pointer to the texture item the pixels come from, and a pointer to a list of
`i32` coordinates.

| Kind | Name | Coordinates |
|-----:|------|-------------|
| 0 | Rect | x0, y0, x1, y1 |
| 1 | Whole texture | none (null pointer) |
| 2 | Nine patch | four x lines, four y lines |
| 3 | Frame | as kind 2, the centre cell is not drawn |
| 4 | Horizontal three | four x lines, two y lines |
| 5 | Vertical three | two x lines, four y lines |

## Kind 1

Kind 1 is not a special fill mode. It means the rectangle covering the whole texture, so no coordinates
are stored. The engine treats it as kind 0 with the corners at the origin and the texture size. Exactly one
item in a full install uses it, and its texture is a plain colour, so it draws a solid fill.

## Pixels to texture coordinates

Coordinates are in pixels counted from the top left of the image. Images are stored bottom row first, so the
engine converts a pixel (x, y) in a texture of size w by h to

    u = x / w
    v = 1 - y / h

Drawing a rectangle whose y values are swapped mirrors the piece in that direction.
