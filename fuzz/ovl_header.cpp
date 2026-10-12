// SPDX-License-Identifier: GPL-3.0-or-later
//
// libFuzzer target: parse_header must return a value or an error for any input, never crash,
// read out of bounds or trip a sanitizer. Run with `./nob fuzz ovl_header`.
// Exercises the whole structure parser, which includes the header.
#include "ovl/ovl.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    const auto bytes = std::as_bytes(std::span(data, size));
    const auto file = rct3::ovl::parse(bytes);
    if (!file)
        return 0;

    // A successful parse never claims more bytes than it was given, and every block lies inside the input.
    if (file->end_offset > size)
        __builtin_trap();
    const auto streams = file->header.version == 4 ? 1u : file->header.version == 5 ? file->header.stream_count : 0;
    if (file->stream_relocations.size() != static_cast<std::size_t>(streams))
        __builtin_trap();
    for (const auto& type : file->block_types)
    {
        for (const auto& block : type.blocks)
        {
            if (block.file_offset > size || block.size > size - block.file_offset)
                __builtin_trap();
        }
    }

    // locate must never fault on arbitrary addresses.
    for (const auto address : file->relocations)
        (void)file->locate(address);

    // Symbols: names resolve through the file's own space. A resolved name must stay inside the input.
    rct3::ovl::AddressSpace space;
    space.add(*file, bytes, 0);
    if (const auto symbols = rct3::ovl::read_symbols(*file, bytes, space))
    {
        const auto* begin = reinterpret_cast<const std::byte*>(bytes.data());
        for (const auto& sym : *symbols)
        {
            if (sym.name.empty())
                continue;
            const auto* p = reinterpret_cast<const std::byte*>(sym.name.data());
            if (p < begin || p + sym.name.size() > begin + size)
                __builtin_trap();
        }
    }

    // Loader table and extra chunks: every chunk lies inside the input and the stream never moves backwards.
    if (const auto loaders = rct3::ovl::read_loaders(*file, bytes))
    {
        if (loaders->end_offset < file->end_offset || loaders->end_offset > size)
            __builtin_trap();
        for (const auto& list : loaders->extra)
        {
            for (const auto& chunk : list)
            {
                if (chunk.file_offset > size || chunk.size > size - chunk.file_offset)
                    __builtin_trap();
            }
        }
    }
    return 0;
}
