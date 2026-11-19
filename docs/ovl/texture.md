# Texture items (tex)

A texture item binds a texture style to the pixels of a flic. The parser is `src/ovl/texture/texture.hpp`.

Record, in order: sixteen `u16` values, three `u32` parameter words, a pointer to the style item, a `u16`
`first`, a `u16` `count`, then (when `first` is non-zero) a pointer to a three-word node.

## Style and node

The style pointer picks the texture style, which is the set of render settings the texture is drawn with. The
node is the start of a flic's record. It is how a texture reaches its pixels: the flic found there lists
the frame numbers of the table of bitmaps. A texture without a node (`first` 0) has no pixels of its own and
is used by the code that creates it at run time (for example render targets).

## What each field showed

- The three parameter words correlate with other fields on every one of the 806 textures in a full install.
  The first equals `first` and the second equals 4 + 4 * `first`: they are derived values, so they carry
  nothing extra. The third is 0, 13 or 16. 13 is used by water and environment cube-map textures and 16
  by ordinary textures, 0 by a few interface ones. We could not confirm what the engine does with it.
- The sixteen `u16` values are 7 in every file. No reader was found, so they are unconfirmed.
- `count` is 0, 1 or 2 and is stored by the engine with the style. Its meaning is not confirmed.
- Node words: the first is 0 everywhere, the second is 1 for nearly all (up to 32 for a few) and the scale
  is 1.0 for all but one animation, which is 30.0. Meaning of the last two is not confirmed.
