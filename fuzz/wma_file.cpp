// SPDX-License-Identifier: GPL-3.0-or-later
//
// libFuzzer target: a music file through the ASF reader and the built-in WMA decoder, as the music player
// reads it. The stream reads from a file, so each input is written to a temporary one first.
#include "audio/wma/stream.hpp"
#include "core/types.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) // c-api
{
    static const std::filesystem::path path = std::filesystem::temp_directory_path() / std::format("rct3pp_fuzz_wma_{}", getpid());
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    }
    auto stream = rct3::audio::wma::WmaStream::open(path);
    if (!stream)
        return 0;

    // Bounded: a few chunks, a rewind, then a few more.
    std::vector<f32> chunk(4096 * static_cast<usize>(stream->channels()));
    volatile f32 sink = 0;
    for (i32 round = 0; round < 2; ++round)
    {
        for (i32 i = 0; i < 16; ++i)
        {
            const auto read = stream->read(chunk);
            if (!read || *read == 0)
                break;
            sink += chunk[0];
        }
        if (!stream->rewind())
            break;
    }
    return 0;
}
