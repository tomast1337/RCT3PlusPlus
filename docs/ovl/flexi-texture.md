# Flexi textures (ftx)

Palette-indexed textures with an optional alpha plane. The parser is `src/ovl/flexi_texture.hpp`.

## Palette byte order

Each frame has a 256-entry palette of 4 bytes per entry. The order in the file is blue, green, red, then a
fourth byte that is not alpha (zero in most textures). Decoding in the order red, green, blue gives wrong
colours. Transparency comes from the separate alpha plane, never from the palette.

## Animation value

`animation` is 0 for single-frame textures and 3 to 25 for the 20 animated textures in an install. It is
the playback rate in sequence steps per second. At game clock `t` seconds the sequence position shown is

    position = (floor(animation * t) + offset) mod sequence_length

and the frame drawn is `sequence[position]`. The offset is a per-use phase, 0 unless something
deliberately starts copies of a texture out of step. Variants named "slow" carry half the value of
their normal twins.

## Flag bits

`flags` is a mask of 0 to 7. It says which recolourable regions the artwork contains. The regions are
painted in solid marker colours that the game replaces with the player's colour choices: bit 0 is the
blue region, bit 1 magenta, bit 2 yellow. The value equals the `flags` of every frame.
