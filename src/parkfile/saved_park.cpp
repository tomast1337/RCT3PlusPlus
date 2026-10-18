// SPDX-License-Identifier: GPL-3.0-or-later
#include "parkfile/saved_park.hpp"

#include "parkfile/binary.hpp"

#include <algorithm>
#include <format>
#include <unordered_set>

namespace rct3::parkfile
{

    using namespace detail;

    namespace
    {
        /// Every chunk, by tag and the member that holds it; all at version 1.
        template<typename F> void each_chunk(auto& park, F&& f)
        {
            f("BASE", park.base);
            f("SUMM", park.summary);
            f("TERR", park.grid);
            f("PATH", park.paths);
            f("OBJS", park.objects);
            f("BINS", park.bins);
            f("SIGN", park.signs);
            f("RIDE", park.rides);
            f("PARK", park.progress);
            f("VIEW", park.camera);
        }
        constexpr std::uint32_t kChunks = 10;
    } // namespace

    std::vector<save::PlacedObject> kept_objects(std::span<const save::PlacedObject> objects, std::size_t own, std::span<const save::PathTile> paths, const save::RideModel& rides)
    {
        std::unordered_set<std::uint64_t> built;
        for (const save::PathTile& p : paths)
        {
            if (p.elevation)
                built.insert(p.elevation->scenery_ref);
        }
        for (const save::Ride& ride : rides.rides)
        {
            if (ride.track)
                for (const save::TrackPiece& piece : ride.track->pieces)
                    built.insert(piece.scenery);
        }
        std::vector<save::PlacedObject> kept;
        for (std::size_t i = 0; i < objects.size(); ++i)
        {
            if (i < own || built.contains(objects[i].id))
                kept.push_back(objects[i]);
        }
        return kept;
    }

    std::vector<std::byte> write_saved_park(const SavedPark& park)
    {
        Writer w;
        write_header(w, kChunks);
        each_chunk(park, [&](std::string_view tag, const auto& value) { put_chunk(w, tag, 1, value); });
        return w.take();
    }

    std::expected<SavedPark, std::string> read_saved_park(std::span<const std::byte> bytes)
    {
        SavedPark park;
        std::uint32_t found = 0;
        auto read = read_chunks(bytes, [&](std::string_view tag, std::uint32_t version, Reader& body) -> std::expected<void, std::string> {
            std::expected<void, std::string> result;
            each_chunk(park, [&](std::string_view name, auto& value) {
                if (name != tag)
                    return;
                ++found;
                if (version != 1)
                    result = std::unexpected(std::format("{}: version {} is not one this build reads", tag, version));
                else
                    result = get_chunk(tag, body, value);
            });
            return result;
        });
        if (!read)
            return std::unexpected(read.error());
        if (found != kChunks)
            return std::unexpected("not a saved park: chunks are missing or repeated");
        return park;
    }

    bool is_park_file(std::span<const std::byte> bytes)
    {
        return bytes.size() >= kMagic.size() && std::ranges::equal(bytes.first(kMagic.size()), kMagic, [](std::byte b, char c) { return b == static_cast<std::byte>(c); });
    }

} // namespace rct3::parkfile
