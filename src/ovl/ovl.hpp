// SPDX-License-Identifier: GPL-3.0-or-later
//
// OVL container parser: header, reference lists, loader table, block layout and relocation table.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rct3::ovl
{

    /// One entry of the loader metadata (type section).
    struct Loader
    {
        std::string loader;             ///< SDK namespace, e.g. "FGDK", "RCT3"
        std::string name;               ///< Type name, e.g. "FlexiTexture"
        std::uint32_t loader_type;      ///< Numeric type id passed to the engine's handler lookup
        std::string tag;                ///< File extension, e.g. "ftx"
        std::uint32_t symbol_count = 0; ///< v5 only; 0 otherwise
    };

    struct Header
    {
        std::uint8_t flags = 0;
        std::uint32_t version = 0;     ///< 1, 4 or 5
        std::int16_t stream_count = 0; ///< v5 only: number of data streams. v4 always has one, v1 none.
        std::int16_t extra_flag = 0;   ///< v5 only, purpose unknown
        std::string stream_tag;        ///< v5 with streams only; may be empty
        std::vector<std::string> external_refs;
        std::vector<std::string> secondary_refs;
        std::vector<Loader> loaders;
        std::size_t end_offset = 0; ///< Stream position after the loader (and v5 symbol count) tables
    };

    enum class Error
    {
        Truncated,
        BadMagic,
        UnsupportedVersion,
        BadStreamBlock,
        BadLoaderIndex,
        ImplausibleCount,
        BadSymbolTable,
        BadLoaderTable,
        BadReferenceTable,
        Io,          ///< A file could not be read
        MissingFile, ///< A required file of a common/unique pair does not exist
    };

    [[nodiscard]] const char* to_string(Error e) noexcept;

    /// Parses the header and type section of an OVL file held in memory.
    [[nodiscard]] std::expected<Header, Error> parse_header(std::span<const std::byte> data);

    /// Block types are always 0..8. Type 0 holds the string table, type 2 the symbol table.
    inline constexpr std::size_t kBlockTypeCount = 9;

    /// One data block. Offsets are positions in the file's own relative address space, which starts at 0 at
    /// the first block of type 0 and grows in type then block order.
    struct Block
    {
        std::uint32_t size = 0;
        std::uint32_t relative_offset = 0; ///< Start of the block in the relative address space
        std::size_t file_offset = 0;       ///< Start of the block bytes in the file
    };

    struct BlockType
    {
        std::uint32_t unknown = 0; ///< Present for version > 1, purpose unknown
        std::uint32_t extra = 0;   ///< v5 with a sub-version block only, purpose unknown
        std::vector<Block> blocks;
    };

    /// Where a relative address falls.
    struct BlockLocation
    {
        std::size_t type;
        std::size_t index;
        std::uint32_t offset; ///< Byte offset inside the block
    };

    /// A relocation of a streamed data section: the 32-bit pointer at `location` is set to `value`. Both are
    /// addresses in the shared address space.
    struct StreamRelocation
    {
        std::uint32_t location = 0;
        std::uint32_t value = 0;
    };

    /// Everything up to and including the relocation tables. Loader extra data chunks that follow are not
    /// parsed by `parse`, see `read_loaders`.
    struct File
    {
        Header header;
        std::array<BlockType, kBlockTypeCount> block_types;

        /// Post-block metadata: v4 has two words, v5 has a byte run and a word list. Purpose unknown.
        std::vector<std::byte> unknown_bytes;
        std::vector<std::uint32_t> unknown_words;

        std::uint32_t data_size = 0;            ///< Total bytes of all blocks, the size of the relative address space
        std::vector<std::uint32_t> relocations; ///< Relative addresses of 32-bit pointers to patch
        /// One table per data stream (v4: one, v5: `header.stream_count`, v1: none).
        std::vector<std::vector<StreamRelocation>> stream_relocations;
        std::size_t end_offset = 0; ///< Stream position after the last relocation table

        /// Finds the block holding `address`, or nothing when it is outside every block.
        ///
        /// A unique file shares one address space with its common file and starts where the common file's
        /// data ends. Pass the common file's `data_size` as `base` to locate addresses of the unique file.
        [[nodiscard]] std::optional<BlockLocation> locate(std::uint32_t address, std::uint32_t base = 0) const noexcept;
    };

    /// Parses a whole OVL file's structure. Block bytes are not copied, use `Block::file_offset`.
    [[nodiscard]] std::expected<File, Error> parse(std::span<const std::byte> data);

    /// Files that share one address space: a common file at base 0 and, optionally, its unique file starting
    /// where the common data ends. Pointers inside either file can name data in either. Holds non-owning
    /// references, so the files and their bytes must outlive it.
    class AddressSpace
    {
    public:
        /// Registers `file`, whose first block sits at address `base`.
        void add(const File& file, std::span<const std::byte> bytes, std::uint32_t base);

        /// `length` bytes at `address`, if they lie inside a single block.
        [[nodiscard]] std::optional<std::span<const std::byte>> bytes_at(std::uint32_t address, std::size_t length) const noexcept;

        /// NUL-terminated string at `address`, if it ends inside its block.
        [[nodiscard]] std::optional<std::string_view> cstring_at(std::uint32_t address) const noexcept;

        /// UTF-16 little-endian string at `address`, up to a 16-bit NUL that must lie inside the same block.
        /// The terminator is not part of the result. Code units are not validated.
        [[nodiscard]] std::optional<std::u16string> utf16_at(std::uint32_t address) const;

    private:
        struct Entry
        {
            const File* file;
            std::span<const std::byte> bytes;
            std::uint32_t base;
        };
        std::vector<Entry> entries_;
    };

    /// One row of the symbol table (block type 2, block 0).
    struct Symbol
    {
        std::uint32_t name_address = 0;
        std::uint32_t data = 0; ///< Address of the data when `is_pointer`, otherwise the value itself
        bool is_pointer = false;
        std::uint16_t unknown = 0; ///< v4/v5 only
        std::uint32_t hash = 0;    ///< v4/v5 only
        std::string_view name;     ///< Full stored text, e.g. "Something:ftx". Empty if the pointer is bad.
    };

    /// Splits "Name:tag" at the last colon. A name without a colon has an empty tag.
    struct SymbolName
    {
        std::string_view name;
        std::string_view tag;
    };
    [[nodiscard]] SymbolName split_symbol_name(std::string_view full) noexcept;

    /// Reads `file`'s symbol table. Names are resolved through `space`, which must contain `file` and, for
    /// a unique file, its common file, because names usually live in the common file's string table.
    /// The returned `name` views point into `bytes` and live as long as it does.
    [[nodiscard]] std::expected<std::vector<Symbol>, Error> read_symbols(const File& file, std::span<const std::byte> bytes, const AddressSpace& space);

    /// One row of the reference table (block type 2, block 2): the 32-bit field at `location` names an item
    /// that another file or another part of this one provides, by symbol name (for example "Wall:ftx").
    struct Reference
    {
        std::uint32_t location = 0;     ///< Address of the field that holds the reference
        std::uint32_t name_address = 0; ///< Address of the referenced item's full symbol name
        std::uint32_t owner = 0;        ///< Address next to the loader row of the item the field belongs to
        std::uint32_t hash = 0;         ///< v4/v5 only
        std::string_view name;          ///< Full symbol name. Empty if the pointer is bad.
    };

    /// Reads `file`'s reference table. Rows are 12 bytes in v1 and 16 bytes later. A file without the table
    /// has no references. Names are resolved through `space` as in `read_symbols`.
    [[nodiscard]] std::expected<std::vector<Reference>, Error> read_references(const File& file, std::span<const std::byte> bytes, const AddressSpace& space);

    /// One row of the loader table (block type 2, block 1): ties a loaded item to its type handler.
    struct LoaderEntry
    {
        std::uint32_t loader_type = 0;    ///< Index into Header::loaders
        std::uint32_t data = 0;           ///< Address of the item's data
        std::uint32_t extra_count = 0;    ///< Number of extra data chunks that follow the relocations
        std::uint16_t unknown = 0;        ///< v5 only: upper half of the field holding extra_count
        std::uint32_t symbol_address = 0; ///< Address of the item's symbol row, or 0
        std::uint32_t symbols_to_resolve = 0;
    };

    /// An extra data chunk: `size` bytes at `file_offset`.
    struct Chunk
    {
        std::size_t file_offset = 0;
        std::uint32_t size = 0;
    };

    struct LoaderTable
    {
        std::vector<LoaderEntry> entries;
        std::vector<std::vector<Chunk>> extra; ///< Chunks per entry, same order as `entries`
        std::size_t end_offset = 0;            ///< First byte after the chunk stream
    };

    /// Reads the loader table and the extra data chunk stream that follows the relocation table.
    /// Bytes after `end_offset` are not understood yet.
    [[nodiscard]] std::expected<LoaderTable, Error> read_loaders(const File& file, std::span<const std::byte> bytes);

} // namespace rct3::ovl
