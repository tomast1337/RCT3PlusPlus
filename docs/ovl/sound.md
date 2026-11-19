# Sound items (snd)

A sound item is an 80-byte descriptor in a data block. The symbol is a pointer symbol tagged `snd`.
The parser is `src/ovl/sound.hpp`.

| Offset | Type | Meaning |
|-------:|------|---------|
| 0 | u16 | Wave format tag, 1 for PCM |
| 2 | u16 | Channels |
| 4 | u32 | Sample rate |
| 8 | u32 | Bytes per second |
| 12 | u16 | Block align |
| 14 | u16 | Bits per sample |
| 16 | u32 | Not used by any reader we found, 0 in every file |
| 20 | u32 | Curve of the fade in, 0 in every file |
| 24 | f32 | Fade-in time, seconds |
| 28 | f32 | Rise time to the peak, seconds |
| 32 | f32 | Peak level, a fraction of full volume |
| 36 | u32 | Curve after the peak, 0 in every file |
| 40 | f32 | Hold time at the peak, seconds |
| 44 | u32 | Curve of the fade out, 0 in every file |
| 48 | f32 | Fade-out time, seconds |
| 52 | f32 | Near distance |
| 56 | f32 | Far distance |
| 60 | u32 | Loop flag |
| 64 | u32 | Address of the samples |
| 68 | u32 | Size of the samples in bytes |

## What the fields do

- **Loop flag** (offset 60): non-zero starts playback in looping mode, so the samples repeat until the
  sound is stopped. 0 plays once. A quarter of the shipped sounds loop (engines, ambience, crowds).
- **Distance fade** (52 and 56): a sound placed in the world is full volume up to the near distance and
  falls off linearly to silence at the far distance. The result scales the item's own volume.
- **Envelope** (20 to 48): the engine fades a started sound in over the fade-in time and fades a stopped
  sound out over the fade-out time instead of cutting it. The hold time is so large in shipped files (1.5
  million seconds) that the sound simply stays at the peak until stopped. The roles of the rise time, the
  peak level and the three curve words follow from how the engine's envelope steps through its phases,
  but only the fade-in and fade-out times, the loop flag and the two distances were also checked against
  data variation (see below).

## What the install shows

All 587 sounds in a full install carry the same envelope and distance values: fade in and fade out 0.05
seconds, rise time 0.00004, peak level 1, hold 1.5 million, near distance 2 and far distance 30. Only the
loop flag differs between sounds (129 of 587 are set). So the shapes of the curves cannot be told apart
from data; treat the curve words as unconfirmed.
