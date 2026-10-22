# OVL archive format

The game keeps most of its content (textures, models, rides, scenery, interface skins, text) in OVL
archives. This page describes the container: how an archive is laid out and how its parts point at each
other. What each item type holds inside its data is a separate topic.

The parser that follows this description is `src/ovl/ovl.hpp`; `ovl-dump` prints what it reads. Every claim
here holds for all the archives in a full install. Where we do not know what a field means, we say so.

## Conventions

- All numbers are little-endian. `u8`, `u16`, `u32` are unsigned of that many bits, `i16` signed.
- A **short string** is a `u16` length followed by that many bytes, with no terminator.
- A **C string** is bytes up to and including a zero byte.
- Offsets in tables are from the start of the structure they describe.

## Pairs of files

Archives come in pairs with the same base name:

- `Name.common.ovl`, which holds the string table and the parts other archives may share.
- `Name.unique.ovl`, which holds the rest.

The two are loaded as one. Their data blocks share a single **address space**: the common file's blocks
start at address 0, and the unique file's blocks start where the common file's data ends. A pointer in
either file may name data in either. Item names, for example, usually sit in the common file's string table
even when the item itself is in the unique file.

## Versions

Three versions occur: 1, 4 and 5. The layout is the same skeleton in all three, with fields added in the
later ones. The sections below mark which version has what.

## Layout

A file is these sections, in this order:

1. Header
2. Reference lists
3. Loader list (and, in version 5, symbol counts)
4. Block list
5. Extra header data (versions 4 and 5)
6. Block data
7. Relocation table
8. Stream relocation tables (versions 4 and 5)
9. Extra data chunks
10. Trailing bytes we do not understand yet

### 1. Header

| Offset | Type     | Meaning                                       |
| ------ | -------- | --------------------------------------------- |
| 0      | 4 bytes  | Magic: the letters `FGRK`                     |
| 4      | `u8`     | Flags. Purpose unknown; version 1 files have 0 |
| 5      | 3 bytes  | Padding                                       |
| 8      | `u32`    | Version: 1, 4 or 5                            |

Versions 4 and 5 then have one more `u32`, which held 16 in every file we read and which the parser skips.

Version 5 then has:

| Type  | Meaning                                                      |
| ----- | ------------------------------------------------------------ |
| `i16` | Stream count: how many data streams the file has, usually 0 |
| `i16` | A second value of unknown purpose                            |

When the stream count is not 0, a self-sized stream block follows: a `u32` size counted from the start of
the block (at least 16), two `u32` that we have only seen as 0, a C string tag (possibly empty), and padding
up to the size.

Version 4 files always have one stream, version 1 files none.

### 2. Reference lists

Two lists of other archives this one depends on, each a `u32` count followed by that many short strings.
We call them the external and the secondary references; how the game uses the second list is unknown.

### 3. Loader list

A `u32` count, then one entry per item type the archive contains:

| Type           | Meaning                                                    |
| -------------- | ---------------------------------------------------------- |
| short string   | Family the type belongs to, for example `FGDK` or `RCT3`   |
| short string   | Type name, for example `FlexiTexture`                      |
| `u32`          | Type id                                                    |
| short string   | Tag: the short name used after item names, for example `ftx` |

In version 5, a table follows with one row per loader entry: a `u32` index into the loader list and a `u32`
count of symbols of that type. Read each row by its index rather than by its position.

### 4. Block list

Data is split into blocks, grouped into 9 block types (0 to 8). For each type, in order:

| Type  | Present            | Meaning                                    |
| ----- | ------------------ | ------------------------------------------ |
| `u32` | always             | Number of blocks of this type              |
| `u32` | version 4 and 5    | Unknown                                    |
| `u32` | version 5 with streams | Unknown                                |
| `u32` × count | version 4 and 5 | Size of each block                     |

Version 1 has no sizes here: each block's size is a `u32` just before that block's data.

### 5. Extra header data

- Version 4: two `u32` of unknown purpose.
- Version 5: a `u32` byte count and that many bytes, then a `u32` word count and that many `u32`. Purpose
  unknown.

### 6. Block data

The blocks' bytes, in block type order and, within a type, in block order. This order also gives every
block its place in the address space: the first block starts at the file's base address (0 for a common
file), and each next block starts where the one before ended. The file's **data size** is the total
of all block sizes; a unique file's base is its common file's data size.

What the blocks hold, where we know it:

| Block          | Holds                       |
| -------------- | --------------------------- |
| type 0         | The string table: C strings |
| type 2, block 0 | Symbol table              |
| type 2, block 1 | Loader table              |
| type 2, block 2 | Reference table           |

The other blocks hold item data, reached through the tables below.

### 7. Relocation table

A `u32` count, then that many `u32` addresses. Each address is the location of a 32-bit field in the block
data that holds another address; a loader turns these into real pointers when it places the blocks in
memory.

To find the bytes at an address: subtract the file's base, then find the block whose range
(`start ≤ address < start + size`) contains it. The bytes are at that block's position in the file plus the
distance from the block's start. An address below the unique file's base belongs to the common file.

### 8. Stream relocation tables

One table per data stream (version 4: one, version 5: the stream count, version 1: none). Each is a `u32`
count, then that many pairs of `u32`: the address of a field, and the address it should hold.

### 9. Extra data chunks

Right after the relocation tables come raw data chunks that some items carry outside the blocks. Each chunk
is a `u32` size followed by that many bytes. The loader table (below) says how many chunks belong to each
item, in loader table order.

### 10. Trailing bytes

Many files have more bytes after the chunks. We do not know what they hold, and nothing we load needs them.

## The tables in block type 2

All three tables are arrays of fixed-size rows filling their block exactly.

### Symbol table (block 0)

One row per named item.

| Version | Row size | Fields                                                                      |
| ------- | -------- | --------------------------------------------------------------------------- |
| 1       | 12       | `u32` name address, `u32` data, `u32` is-pointer flag                        |
| 4, 5    | 16       | `u32` name address, `u32` data, `u16` is-pointer flag, `u16` unknown, `u32` hash |

The name address points at a C string of the form `Name:tag`, where the tag is a loader's tag, for example
`Wall:ftx`. Split it at the last colon. When the is-pointer flag is set, `data` is the address of the
item's data; otherwise it is the value itself.

### Loader table (block 1)

One 20-byte row per loaded item:

| Offset | Type  | Meaning                                                  |
| ------ | ----- | -------------------------------------------------------- |
| 0      | `u32` | Index into the loader list                               |
| 4      | `u32` | Address of the item's data                               |
| 8      | `u32` | Number of extra data chunks (version 5: low 16 bits only; the high 16 bits are unknown) |
| 12     | `u32` | Address of the item's symbol row, or 0                   |
| 16     | `u32` | Number of the item's references to resolve               |

### Reference table (block 2)

One row per field that names an item held elsewhere, in this archive or in another one.

| Version | Row size | Fields                                                                       |
| ------- | -------- | ---------------------------------------------------------------------------- |
| 1       | 12       | `u32` field address, `u32` name address, `u32` owner                          |
| 4, 5    | 16       | `u32` field address, `u32` name address, `u32` owner, `u32` hash              |

The name is a full symbol name (`Name:tag`): the item the field should point at once both are loaded. The
owner is an address next to the loader table row of the
item the field belongs to.

## A small example

An invented version 5 common file holding one texture's name:

- header: `FGRK`, flags, version 5, the skipped `u32`, stream count 0, second value 0;
- both reference lists empty;
- one loader: family `FGDK`, name `FlexiTexture`, type 1, tag `ftx`; then the symbol count row
  (loader 0, 1 symbol);
- block list: type 0 has one block of 10 bytes, `Brick:ftx` and its zero byte; type 2 has one 16-byte
  block, the symbol table; the other types have none;
- block data: the string at address 0, the symbol table at address 10, its one row naming address 0;
- a relocation table listing address 10, the row's name field.

Its unique file would start its blocks at address 26, the common file's data size.

## Other descriptions

Other projects have documented this format before us, among them the RCT3 Importer's archive dumper and
the open-rct3 project. Where they disagree with this page, the game's files decide.
