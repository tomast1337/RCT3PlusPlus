// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/ovl.hpp"

#include <array>
#include <algorithm>
#include <bit>
#include <cstring>

namespace rct3::ovl {

static_assert(std::endian::native == std::endian::little, "OVL parsing assumes a little-endian host");

namespace {

// Bounds-checked forward cursor over the file bytes.
class Cursor {
 public:
  explicit Cursor(std::span<const std::byte> data) : data_(data) {}

  [[nodiscard]] std::size_t position() const noexcept { return pos_; }
  [[nodiscard]] std::size_t remaining() const noexcept { return data_.size() - pos_; }

  template <typename T>
  [[nodiscard]] std::expected<T, Error> read() {
    if (data_.size() - pos_ < sizeof(T)) return std::unexpected(Error::Truncated);
    T value;
    std::memcpy(&value, data_.data() + pos_, sizeof(T));
    pos_ += sizeof(T);
    return value;
  }

  // u16 length + bytes, no terminator.
  [[nodiscard]] std::expected<std::string, Error> read_string() {
    auto len = read<std::uint16_t>();
    if (!len) return std::unexpected(len.error());
    if (data_.size() - pos_ < *len) return std::unexpected(Error::Truncated);
    std::string s(reinterpret_cast<const char*>(data_.data() + pos_), *len);
    pos_ += *len;
    return s;
  }

  [[nodiscard]] std::expected<std::vector<std::byte>, Error> read_bytes(std::size_t n) {
    if (remaining() < n) return std::unexpected(Error::Truncated);
    std::vector<std::byte> out(data_.begin() + static_cast<std::ptrdiff_t>(pos_),
                               data_.begin() + static_cast<std::ptrdiff_t>(pos_ + n));
    pos_ += n;
    return out;
  }

  [[nodiscard]] std::expected<void, Error> seek(std::size_t pos) {
    if (pos > data_.size()) return std::unexpected(Error::Truncated);
    pos_ = pos;
    return {};
  }

  // Bytes up to a NUL terminator inside [pos_, limit). Leaves the cursor after the terminator.
  [[nodiscard]] std::expected<std::string, Error> read_cstring(std::size_t limit) {
    if (limit > data_.size()) return std::unexpected(Error::Truncated);
    std::string s;
    while (pos_ < limit) {
      const char ch = static_cast<char>(data_[pos_++]);
      if (ch == '\0') return s;
      s.push_back(ch);
    }
    return std::unexpected(Error::BadStreamBlock);
  }

  // Every counted entry costs at least `min_entry_size` bytes, so the count cannot exceed what is left.
  [[nodiscard]] bool plausible_count(std::uint32_t count, std::size_t min_entry_size) const noexcept {
    return count <= (data_.size() - pos_) / min_entry_size;
  }

 private:
  std::span<const std::byte> data_;
  std::size_t pos_ = 0;
};

#define RCT3_TRY(var, expr)                    \
  auto var = (expr);                           \
  if (!var) return std::unexpected(var.error())

std::expected<std::vector<std::string>, Error> read_string_list(Cursor& c) {
  RCT3_TRY(count, c.read<std::uint32_t>());
  if (!c.plausible_count(*count, sizeof(std::uint16_t))) return std::unexpected(Error::ImplausibleCount);
  std::vector<std::string> out;
  out.reserve(*count);
  for (std::uint32_t i = 0; i < *count; ++i) {
    RCT3_TRY(s, c.read_string());
    out.push_back(std::move(*s));
  }
  return out;
}

std::expected<Header, Error> read_header(Cursor& c);

}  // namespace

const char* to_string(Error e) noexcept {
  switch (e) {
    case Error::Truncated: return "file truncated";
    case Error::BadMagic: return "bad magic, expected FGRK";
    case Error::UnsupportedVersion: return "unsupported OVL version";
    case Error::BadStreamBlock: return "malformed v5 stream block";
    case Error::BadLoaderIndex: return "symbol count references unknown loader";
    case Error::ImplausibleCount: return "entry count exceeds remaining file size";
    case Error::BadSymbolTable: return "symbol table size is not a whole number of entries";
    case Error::BadLoaderTable: return "loader table size is not a whole number of entries";
    case Error::BadReferenceTable: return "reference table size is not a whole number of entries";
    case Error::Io: return "cannot read file";
    case Error::MissingFile: return "file not found";
  }
  return "unknown error";
}

namespace {

std::expected<Header, Error> read_header(Cursor& c) {
  Header h;

  RCT3_TRY(magic, c.read<std::uint32_t>());
  if (*magic != std::bit_cast<std::uint32_t>(std::array<char, 4>{'F', 'G', 'R', 'K'})) {
    return std::unexpected(Error::BadMagic);
  }

  RCT3_TRY(flags, c.read<std::uint8_t>());
  h.flags = *flags;
  for (int i = 0; i < 3; ++i) {
    RCT3_TRY(pad, c.read<std::uint8_t>());
  }

  RCT3_TRY(version, c.read<std::uint32_t>());
  h.version = *version;
  if (h.version != 1 && h.version != 4 && h.version != 5) return std::unexpected(Error::UnsupportedVersion);

  if (h.version >= 4) {
    RCT3_TRY(discarded, c.read<std::uint32_t>());  // always 0x10 in observed files
  }
  if (h.version == 5) {
    RCT3_TRY(sub, c.read<std::int16_t>());
    RCT3_TRY(extra, c.read<std::int16_t>());
    if (*sub < 0) return std::unexpected(Error::ImplausibleCount);
    h.stream_count = *sub;
    h.extra_flag = *extra;
    if (h.stream_count != 0) {
      // Self-sized block: u32 size (counted from its own start), two reserved u32, a NUL-terminated
      // tag, then padding up to the size. Seen with size 20 + tag and size 16 + empty tag.
      const std::size_t start = c.position();
      RCT3_TRY(size, c.read<std::uint32_t>());
      if (*size < 16) return std::unexpected(Error::BadStreamBlock);
      RCT3_TRY(reserved_a, c.read<std::uint32_t>());
      RCT3_TRY(reserved_b, c.read<std::uint32_t>());
      RCT3_TRY(tag, c.read_cstring(start + *size));
      h.stream_tag = std::move(*tag);
      RCT3_TRY(sought, c.seek(start + *size));
    }
  }

  RCT3_TRY(ext, read_string_list(c));
  h.external_refs = std::move(*ext);
  RCT3_TRY(sec, read_string_list(c));
  h.secondary_refs = std::move(*sec);

  RCT3_TRY(loader_count, c.read<std::uint32_t>());
  // Smallest entry: 3 empty strings (2 bytes each) + u32.
  if (!c.plausible_count(*loader_count, 10)) return std::unexpected(Error::ImplausibleCount);
  h.loaders.reserve(*loader_count);
  for (std::uint32_t i = 0; i < *loader_count; ++i) {
    Loader l;
    RCT3_TRY(loader, c.read_string());
    RCT3_TRY(name, c.read_string());
    RCT3_TRY(type, c.read<std::uint32_t>());
    RCT3_TRY(tag, c.read_string());
    l.loader = std::move(*loader);
    l.name = std::move(*name);
    l.loader_type = *type;
    l.tag = std::move(*tag);
    h.loaders.push_back(std::move(l));
  }

  if (h.version == 5) {
    // fileTypeCount x { loaderIdx, symbolCount }, index-mapped.
    for (std::uint32_t i = 0; i < *loader_count; ++i) {
      RCT3_TRY(idx, c.read<std::uint32_t>());
      RCT3_TRY(count, c.read<std::uint32_t>());
      if (*idx >= h.loaders.size()) return std::unexpected(Error::BadLoaderIndex);
      h.loaders[*idx].symbol_count = *count;
    }
  }

  h.end_offset = c.position();
  return h;
}

}  // namespace

std::expected<Header, Error> parse_header(std::span<const std::byte> data) {
  Cursor c(data);
  return read_header(c);
}

std::optional<BlockLocation> File::locate(std::uint32_t address, std::uint32_t base) const noexcept {
  if (address < base) return std::nullopt;
  const std::uint32_t relative_address = address - base;
  for (std::size_t t = 0; t < block_types.size(); ++t) {
    const auto& blocks = block_types[t].blocks;
    for (std::size_t i = 0; i < blocks.size(); ++i) {
      const Block& b = blocks[i];
      if (relative_address >= b.relative_offset && relative_address - b.relative_offset < b.size) {
        return BlockLocation{t, i, relative_address - b.relative_offset};
      }
    }
  }
  return std::nullopt;
}

std::expected<File, Error> parse(std::span<const std::byte> data) {
  Cursor c(data);
  RCT3_TRY(header, read_header(c));

  File f;
  f.header = std::move(*header);
  const std::uint32_t version = f.header.version;
  const bool has_extra = version == 5 && f.header.stream_count != 0;

  // Block definitions. v1 keeps sizes inline with the data instead of here.
  for (auto& type : f.block_types) {
    RCT3_TRY(count, c.read<std::uint32_t>());
    if (version > 1) {
      RCT3_TRY(unknown, c.read<std::uint32_t>());
      type.unknown = *unknown;
      if (has_extra) {
        RCT3_TRY(extra, c.read<std::uint32_t>());
        type.extra = *extra;
      }
    }
    // Each block costs at least a u32, either its size here or inline in the data section.
    if (!c.plausible_count(*count, sizeof(std::uint32_t))) return std::unexpected(Error::ImplausibleCount);
    type.blocks.resize(*count);
    if (version > 1) {
      for (auto& block : type.blocks) {
        RCT3_TRY(size, c.read<std::uint32_t>());
        block.size = *size;
      }
    }
  }

  // Post-block metadata.
  if (version == 4) {
    for (int i = 0; i < 2; ++i) {
      RCT3_TRY(word, c.read<std::uint32_t>());
      f.unknown_words.push_back(*word);
    }
  } else if (version == 5) {
    RCT3_TRY(byte_count, c.read<std::uint32_t>());
    if (!c.plausible_count(*byte_count, 1)) return std::unexpected(Error::ImplausibleCount);
    RCT3_TRY(bytes, c.read_bytes(*byte_count));
    f.unknown_bytes = std::move(*bytes);

    RCT3_TRY(word_count, c.read<std::uint32_t>());
    if (!c.plausible_count(*word_count, sizeof(std::uint32_t))) return std::unexpected(Error::ImplausibleCount);
    f.unknown_words.reserve(*word_count);
    for (std::uint32_t i = 0; i < *word_count; ++i) {
      RCT3_TRY(word, c.read<std::uint32_t>());
      f.unknown_words.push_back(*word);
    }
  }

  // Block data, in type then block order. Assigns each block its place in the relative address space.
  std::uint64_t relative = 0;
  for (auto& type : f.block_types) {
    for (auto& block : type.blocks) {
      if (version == 1) {
        RCT3_TRY(size, c.read<std::uint32_t>());
        block.size = *size;
      }
      if (c.remaining() < block.size) return std::unexpected(Error::Truncated);
      block.relative_offset = static_cast<std::uint32_t>(relative);
      block.file_offset = c.position();
      relative += block.size;
      if (relative > UINT32_MAX) return std::unexpected(Error::ImplausibleCount);
      RCT3_TRY(sought, c.seek(c.position() + block.size));
    }
  }
  f.data_size = static_cast<std::uint32_t>(relative);

  // Relocation table.
  RCT3_TRY(relocation_count, c.read<std::uint32_t>());
  if (!c.plausible_count(*relocation_count, sizeof(std::uint32_t))) {
    return std::unexpected(Error::ImplausibleCount);
  }
  f.relocations.reserve(*relocation_count);
  for (std::uint32_t i = 0; i < *relocation_count; ++i) {
    RCT3_TRY(address, c.read<std::uint32_t>());
    f.relocations.push_back(*address);
  }

  // One table per data stream: u32 count, then count pairs of u32 (location, value).
  const std::size_t streams = version == 4 ? 1 : version == 5 ? static_cast<std::size_t>(f.header.stream_count) : 0;
  f.stream_relocations.resize(streams);
  for (auto& table : f.stream_relocations) {
    RCT3_TRY(count, c.read<std::uint32_t>());
    if (!c.plausible_count(*count, 2 * sizeof(std::uint32_t))) return std::unexpected(Error::ImplausibleCount);
    table.reserve(*count);
    for (std::uint32_t i = 0; i < *count; ++i) {
      RCT3_TRY(location, c.read<std::uint32_t>());
      RCT3_TRY(value, c.read<std::uint32_t>());
      table.push_back({*location, *value});
    }
  }

  f.end_offset = c.position();
  return f;
}

void AddressSpace::add(const File& file, std::span<const std::byte> bytes, std::uint32_t base) {
  entries_.push_back({&file, bytes, base});
}

std::optional<std::span<const std::byte>> AddressSpace::bytes_at(std::uint32_t address,
                                                                  std::size_t length) const noexcept {
  for (const auto& e : entries_) {
    const auto at = e.file->locate(address, e.base);
    if (!at) continue;
    const Block& block = e.file->block_types[at->type].blocks[at->index];
    if (length > block.size - at->offset) return std::nullopt;
    return e.bytes.subspan(block.file_offset + at->offset, length);
  }
  return std::nullopt;
}

std::optional<std::string_view> AddressSpace::cstring_at(std::uint32_t address) const noexcept {
  for (const auto& e : entries_) {
    const auto at = e.file->locate(address, e.base);
    if (!at) continue;
    const Block& block = e.file->block_types[at->type].blocks[at->index];
    const auto rest = e.bytes.subspan(block.file_offset + at->offset, block.size - at->offset);
    const auto nul = std::find(rest.begin(), rest.end(), std::byte{0});
    if (nul == rest.end()) return std::nullopt;
    return std::string_view(reinterpret_cast<const char*>(rest.data()),
                            static_cast<std::size_t>(nul - rest.begin()));
  }
  return std::nullopt;
}

std::optional<std::u16string> AddressSpace::utf16_at(std::uint32_t address) const {
  for (const auto& e : entries_) {
    const auto at = e.file->locate(address, e.base);
    if (!at) continue;
    const Block& block = e.file->block_types[at->type].blocks[at->index];
    const auto rest = e.bytes.subspan(block.file_offset + at->offset, block.size - at->offset);

    std::u16string out;
    for (std::size_t i = 0; i + 1 < rest.size(); i += 2) {
      const auto unit = static_cast<char16_t>(std::to_integer<unsigned>(rest[i]) |
                                              (std::to_integer<unsigned>(rest[i + 1]) << 8));
      if (unit == 0) return out;
      out.push_back(unit);
    }
    return std::nullopt;
  }
  return std::nullopt;
}

SymbolName split_symbol_name(std::string_view full) noexcept {
  const auto colon = full.rfind(':');
  if (colon == std::string_view::npos) return {full, {}};
  return {full.substr(0, colon), full.substr(colon + 1)};
}

std::expected<std::vector<Symbol>, Error> read_symbols(const File& file, std::span<const std::byte> bytes,
                                                        const AddressSpace& space) {
  constexpr std::size_t kSymbolTableType = 2;
  const auto& blocks = file.block_types[kSymbolTableType].blocks;
  if (blocks.empty()) return std::vector<Symbol>{};

  // v1 rows are {name, data, isPointer:u32}. v4 and v5 rows are {name, data, isPointer:u16, unknown:u16, hash}.
  const bool wide = file.header.version != 1;
  const std::size_t stride = wide ? 16 : 12;

  const Block& table = blocks[0];
  if (table.size % stride != 0) return std::unexpected(Error::BadSymbolTable);
  if (bytes.size() < table.file_offset || bytes.size() - table.file_offset < table.size) {
    return std::unexpected(Error::Truncated);
  }

  std::vector<Symbol> out;
  out.reserve(table.size / stride);
  for (std::size_t off = 0; off < table.size; off += stride) {
    const std::byte* row = bytes.data() + table.file_offset + off;
    Symbol sym;
    std::memcpy(&sym.name_address, row, 4);
    std::memcpy(&sym.data, row + 4, 4);
    if (wide) {
      std::uint16_t is_pointer = 0;
      std::memcpy(&is_pointer, row + 8, 2);
      sym.is_pointer = is_pointer != 0;
      std::memcpy(&sym.unknown, row + 10, 2);
      std::memcpy(&sym.hash, row + 12, 4);
    } else {
      std::uint32_t is_pointer = 0;
      std::memcpy(&is_pointer, row + 8, 4);
      sym.is_pointer = is_pointer != 0;
    }
    if (const auto name = space.cstring_at(sym.name_address)) sym.name = *name;
    out.push_back(sym);
  }
  return out;
}

std::expected<std::vector<Reference>, Error> read_references(const File& file, std::span<const std::byte> bytes,
                                                              const AddressSpace& space) {
  constexpr std::size_t kTableType = 2;
  constexpr std::size_t kTableBlock = 2;

  const auto& blocks = file.block_types[kTableType].blocks;
  if (blocks.size() <= kTableBlock) return std::vector<Reference>{};

  const bool wide = file.header.version != 1;
  const std::size_t stride = wide ? 16 : 12;

  const Block& table = blocks[kTableBlock];
  if (table.size % stride != 0) return std::unexpected(Error::BadReferenceTable);
  if (bytes.size() < table.file_offset || bytes.size() - table.file_offset < table.size) {
    return std::unexpected(Error::Truncated);
  }

  std::vector<Reference> out;
  out.reserve(table.size / stride);
  for (std::size_t off = 0; off < table.size; off += stride) {
    const std::byte* row = bytes.data() + table.file_offset + off;
    Reference ref;
    std::memcpy(&ref.location, row, 4);
    std::memcpy(&ref.name_address, row + 4, 4);
    std::memcpy(&ref.owner, row + 8, 4);
    if (wide) std::memcpy(&ref.hash, row + 12, 4);
    if (const auto name = space.cstring_at(ref.name_address)) ref.name = *name;
    out.push_back(ref);
  }
  return out;
}

std::expected<LoaderTable, Error> read_loaders(const File& file, std::span<const std::byte> bytes) {
  constexpr std::size_t kLoaderTableType = 2;
  constexpr std::size_t kLoaderTableBlock = 1;
  constexpr std::size_t kRowSize = 20;

  LoaderTable out;
  out.end_offset = file.end_offset;

  const auto& blocks = file.block_types[kLoaderTableType].blocks;
  if (blocks.size() <= kLoaderTableBlock) return out;

  const Block& table = blocks[kLoaderTableBlock];
  if (table.size % kRowSize != 0) return std::unexpected(Error::BadLoaderTable);
  if (bytes.size() < table.file_offset || bytes.size() - table.file_offset < table.size) {
    return std::unexpected(Error::Truncated);
  }

  const bool v5 = file.header.version == 5;
  out.entries.reserve(table.size / kRowSize);
  for (std::size_t off = 0; off < table.size; off += kRowSize) {
    const std::byte* row = bytes.data() + table.file_offset + off;
    std::uint32_t extra_field = 0;
    LoaderEntry e;
    std::memcpy(&e.loader_type, row, 4);
    std::memcpy(&e.data, row + 4, 4);
    std::memcpy(&extra_field, row + 8, 4);
    std::memcpy(&e.symbol_address, row + 12, 4);
    std::memcpy(&e.symbols_to_resolve, row + 16, 4);
    // v5 packs a 16-bit chunk count with a 16-bit unknown. Older versions use the whole field.
    e.extra_count = v5 ? (extra_field & 0xFFFF) : extra_field;
    e.unknown = v5 ? static_cast<std::uint16_t>(extra_field >> 16) : 0;
    out.entries.push_back(e);
  }

  // The chunk stream starts right after the relocation tables.
  Cursor c(bytes);
  RCT3_TRY(sought, c.seek(file.end_offset));

  out.extra.resize(out.entries.size());
  for (std::size_t i = 0; i < out.entries.size(); ++i) {
    // Each chunk costs at least its size field.
    if (!c.plausible_count(out.entries[i].extra_count, sizeof(std::uint32_t))) {
      return std::unexpected(Error::ImplausibleCount);
    }
    for (std::uint32_t k = 0; k < out.entries[i].extra_count; ++k) {
      RCT3_TRY(size, c.read<std::uint32_t>());
      if (c.remaining() < *size) return std::unexpected(Error::Truncated);
      out.extra[i].push_back({c.position(), *size});
      RCT3_TRY(skip, c.seek(c.position() + *size));
    }
  }
  out.end_offset = c.position();
  return out;
}

}  // namespace rct3::ovl
