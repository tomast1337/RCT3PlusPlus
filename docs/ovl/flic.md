# Flic items

Flics have no symbol name. Each is a loader row whose data chunk holds the payload. The parser is
`src/ovl/flic.hpp`.

- **Revision 2** flics hold one image directly: a 32-byte header, then the pixels of each mip level.
- **Revision 3** flics hold a list of frame numbers. Every number is an index into the bitmaps of the bitmap
  table that comes before it in the same file. A textured item reaches its pixels by pointing at the flic
  record: the first words of the flic are the same record the texture's node points to. So the number of
  a frame is a bitmap table index and nothing else.

The `rate` field is 1.0 for 793 of 794 flics in a full install and 30.0 for one. We could not confirm
what the engine does with it, so treat it as unconfirmed.
