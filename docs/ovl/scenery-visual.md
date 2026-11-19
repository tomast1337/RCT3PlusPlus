# Scenery visual items (svd)

Parser: `src/ovl/scenery_visual.hpp`. A visual lists the levels of detail of a scenery object plus a few
shared values.

## Flag bits

| Bit | Seen on | Notes |
|----:|---------|-------|
| 0x1 | bushes, trees, light glows | 217 items. About half also sway. |
| 0x2 | litter, track data pieces, vomit and the like | 58 items. None sway. |
| 0x4 | trees and bushes | 70 items, 60 of them swaying |
| 0x10, 0x20, 0x40 | stalls, buildings, crates | Appear together as 0x70 or 0x60 |
| 0x800 | trains and carriage parts | 25 items |
| 0x1000 | one item (a giant ferris wheel) | The engine treats such an object separately when it picks the level of detail |
| 0x01000000, 0x02000000 | the first and second expansion's items | One of these is set on every item of those files |

The plant bits (0x1, 0x4) go with a non-zero `sway`, a brightness below 1 and a scale variation. The groups
0x2, 0x4 and 0x70 are consistent with what the objects are but the engine's use of them was not confirmed
beyond 0x1000, so they are descriptions of the data, not of behaviour.

## Level distance

The distance of a level is the farthest view distance at which that level is drawn, in world units, and the
values grow from the finest to the coarsest level. The last level of a multi-level item is a large value
(hundreds to thousands) so it covers the rest. The exact way the engine combines it with the object's size
and the view distance setting was not confirmed.
