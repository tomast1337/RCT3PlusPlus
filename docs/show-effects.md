# Firework display, laser and water jet files

The game keeps the parts of a firework display in loose files beside its archives, in the install and in the
player's own folders:

| Extension | What it holds | Reader |
|-----------|---------------|--------|
| `.frw` | A firework or other particle effect | `src/particles/effect.hpp` |
| `.fwd` | A display: launchers, shots and music | `src/particles/display.hpp` |
| `.las` | A laser effect | `src/particles/show_effect.hpp` |
| `.wjt` | A water jet effect | `src/particles/show_effect.hpp` |
| `.lwr` | A laser drawing: one picture a laser can trace | `src/particles/show_effect.hpp` |

All five are the save container that parks use (`src/save/save_file.hpp`): a class table that names every
field, then the objects. Laser and water jet files start with the full 80-byte header and end with its
checksum; laser drawing files start with the short 64-byte form of the header; display files have no header.
The class table of a drawing file lists far more classes than it uses, as a park's would, but it holds one
object.

The readers find their object by its class name, not by its place in the file, and read fields by name, so a
field missing from a file takes a default.

## Timelines

Lasers and water jets change over their run through timelines. A timeline is an object of class `Blender`:

| Field | Type | Meaning |
|-------|------|---------|
| `FixedPts` | array | Slots for control points. Each is a reference to a value object (`BlenderElement`) and a time in seconds (`PointTime`) |
| `NumPts` | int32 | How many of the slots are control points, counting from the first |
| `MaxTime` | float32 | The length of the timeline in seconds; the effect's own duration in every file we have |
| `DurationTransform` | bool | Whether the points move with the duration when the player changes it in the designer |

- Every shipped file has 20 slots. The slots past the count hold leftovers (a repeated reference and a filler
  time) and are not points.
- The control points are kept in the order they were added, not in time order: a laser whose rotation timeline
  reads, say, 0.5 s, 6 s, 3 s is valid. The reader sorts them.
- A value object is `FloatBlenderElement` (`Float`, float32) or `ColourBlenderElement` (`Colour`, int32). A
  colour packs opacity, red, green and blue, one byte each from the highest byte down: bytes 0x80, 0xFF, 0x40, 0x00 make an
  orange at half opacity.
- Between points the value runs straight from one to the next; before the first and after the last it holds.

## Laser (`.las`)

One object of class `LaserDesc` or `LaserBaseDesc` (the two names have the same fields):

| Field | Type | Meaning |
|-------|------|---------|
| `Name` | string | Shown name, for example `Sweep` |
| `Duration` | float32 | Length of one run, seconds |
| `RenderType` | int32 | 0 a cone of beams, 1 a flat fan of beams, 2 one beam tracing the drawings |
| `NumBeams` | int32 | How many beams; the shipped lasers use 1 to 50 |
| `Power` | float32 | Read as how far the beams reach, meters; 20 to 500 in the shipped lasers, the short ones broad or many-beamed |
| `TrailDuration` | float32 | How long a moving beam leaves a glow behind it, seconds |
| `ImageSpread` | float32 | How large the drawings are thrown |
| `ColourBlender` | reference | Colour timeline |
| `ElevationBlender` | reference | Angle of the beams up from level (the plane across the launcher's tube), radians; the shipped values stay between 0 and just under a quarter turn. Measured from level, not from the tube: shipped cones of many beams spin fast at 0, which would put every beam in one line up the tube |
| `RotVelBlender` | reference | How fast the beams turn round the base, radians a second; below 0 the other way |
| `SpreadBlender` | reference | How wide each beam spreads: about 1 for a thin beam, up to about 1.6 for a broad searchlight |
| `LaserWord` | reference | The drawings, as a `LaserWritingWord` |

A laser kept in a park has one more field, `DoCentre` (int32), whether its drawing is thrown centred.

A `LaserWritingWord` holds `Letters`, an array of references to `LaserWritingLetter` objects, each one
drawing, and `LetterNames`, an array of strings beside them (empty in the shipped files). Lasers that draw
nothing still have a word with no letters.

## Laser drawing (`.lwr`, and inside a laser)

One `LaserWritingLetter`:

| Field | Type | Meaning |
|-------|------|---------|
| `DragPoints` | array | Slots for points, each `X` and `Y` (float32) on the drawing pad, which runs about 0 to 32 across and 0 to 24 down |
| `NumPts` | int32 | How many of the slots are points, counting from the first |
| `SolidConnection` | array of bool | One per slot: whether the beam shines on its way to that point from the one before. Off, the beam jumps there dark, which is how a drawing lifts the pen |
| `Duration` | float32 | Seconds to trace the drawing once |
| `SimplificationConst` | int32 | A setting of the drawing pad; 1 in every file we have |
| `Preview` | image | A small picture of the drawing for the game's lists; not read |

The shipped drawings fill 1022 slots and use several hundred of them. The first point of every drawing is a
jump.

## Water jet (`.wjt`)

One object of class `WaterJetDesc` or `WaterJetBaseDesc`:

| Field | Type | Meaning |
|-------|------|---------|
| `Name` | string | Shown name |
| `Duration` | float32 | Length of one run, seconds |
| `Type` | int32 | 1 in every file we have; not read |
| `PressureBlender` | reference | Speed the water leaves the nozzle, meters a second; the shipped jets go up to about 17 |
| `ElevationBlender` | reference | Lean from straight up, radians; a jet that swings from side to side runs from below 0 to above it |
| `DiameterDeltaBlender` | reference | The size of the stream; read as what its width is multiplied by each second it flies |
| `ColourBlender` | reference | Colour timeline |

## In a park

A park keeps its displays as `FireworkDisplay` objects (see `src/particles/display.hpp`). Each laser or water
jet shot names the file it came from (a path, which may point outside the install or name a file that no
longer exists) and refers, through `ParticleSourceRef`, to a source object: `LaserSource` with a reference
`LaserBaseDesc`, or `WaterJetSource` with a reference `WaterJetBaseDesc`. That reference leads to a full laser
or water jet description, with the same fields as the loose file. So a park plays its shows without the files
its shots name.

The shipped display files (`.fwd`) hold fireworks only.
