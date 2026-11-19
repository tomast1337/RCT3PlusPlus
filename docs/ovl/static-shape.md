# Static and bone shape meshes (shs, bsh)

Parsers: `src/ovl/static_shape.hpp`, `src/ovl/bone_shape.hpp`. Both use the same per-mesh fields.

## Transparency and sides

- `transparency`: 0 opaque, 1 alpha tested (cut-out), 2 alpha blended (drawn in a later pass). It
  follows the texture style's name in every file.
- `sides` is the cull mode of the face. 1 draws both sides (no culling), 3 draws only the front faces
  (counter-clockwise winding is the front). 2 does not occur.

## Texture flags

A bit mask, 0 for most parts. When the engine builds a part's draw call it checks the advertisement (0x8000)
and terrain (0x1000, with 0x2000 and 0x4000 choosing surface or cliff) bits first, then bit 0x4, and with 0x4
set bit 0x8, each choosing a different way to prepare the part. We did not follow what the two 0x4 paths
differ in, so the descriptions below come from which pieces carry the bits:

- 0x4 is set in every non-zero value below 0x1000.
- 0x8 with 0x4: the board of a sign or entrance (unlit, words shown on it). All such parts also use
  the unlit opaque styles.
- 0x10 with 0x4: lift chains, chain-like ride pieces and flowing water, whose texture scrolls.
- 0x20 or 0x40 with 0x4: wheel parts. We could not find what the engine does for these two bits.

Bit 0x1 is never stored. 0x2 does not appear below 0x1000.

## Vertex colour

Both kinds of shape store a vertex colour in the same place: four bytes in the order blue, green, red,
alpha. In a full install nearly every vertex (99.95 percent of the 1.4 million bone-shape vertices) is opaque
white; the rest are a few tints (black, grey, a handful of saturated colours) with alpha 255. Whether the
engine multiplies it into lighting for bone shapes was not confirmed, so draw white-tinted and treat
other values as optional.

## Support type

0xFFFFFFFF for ordinary parts. Parts of path, queue and track pieces that belong to one kind of support
carry a single bit (1, 2, 4 or 8). Which bit is chosen at run time is not confirmed.
