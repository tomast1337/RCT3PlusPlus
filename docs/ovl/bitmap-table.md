# Bitmap tables (btbl)

A bitmap table holds the pixel data of the textures in one file as a list of bitmaps with mip chains. The
parser is `src/ovl/bitmap_table.hpp`.

## Pixel formats

| Value | Layout | Name |
|------:|--------|------|
| 2 | 4 bytes per pixel in the order blue, green, red, alpha | uncompressed B8G8R8A8 |
| 18 | 4x4 pixel blocks of 8 bytes | DXT1 block compression |
| 19 | 4x4 pixel blocks of 16 bytes | DXT3 block compression (explicit 4-bit alpha) |

Other values do not occur. Mip levels follow each other from the largest down, each level half the size of
the one before until 1 pixel. Block formats store no level below 4x4.

## Reserved and flag words

The table's row data is a reserved word followed by the bitmap count. The description list that comes
first in the table's data chunk starts with two more words. All of these are 0 in all 188 tables in a
full install. The reader of the original game ignores the reserved word, requires the first prefix word
to be 0 and accepts 0 or 1 in the second (a flag that is 0 in shipped files). Write 0 in all three if you
build a table.
