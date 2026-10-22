// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/ride_placement.hpp"

#include "park/boundary_fence.hpp"
#include "park/water_fill.hpp"
#include "strings/ascii.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <format>
#include <numeric>
#include <optional>
#include <vector>

namespace rct3::park
{

    namespace
    {
        bool has_path(std::span<const save::PathTile> paths, std::int32_t col, std::int32_t row)
        {
            return std::ranges::any_of(paths, [&](const save::PathTile& p) { return p.col == col && p.row == row; });
        }

        SiteProblem tile_problem(std::int32_t col, std::int32_t row, std::int32_t height, const save::TerrainGrid& grid, std::span<const save::PathTile> paths, const SolidTiles& solids)
        {
            if (!grid.contains(col, row) || !owned_outright(grid.at(col, row)))
                return SiteProblem::NotOwned;
            if (has_path(paths, col, row))
                return SiteProblem::Path;
            // shortcut: anything solid reaching over the base blocks, however high; give rides a height to
            // check against once their models' heights are read.
            if (const auto spans = solids.tiles.find(static_cast<std::uint32_t>(row) << 16 | static_cast<std::uint32_t>(col)); spans != solids.tiles.end())
            {
                if (std::ranges::any_of(spans->second, [&](const SolidSpan& s) { return s.high > static_cast<float>(height); }))
                    return SiteProblem::Blocked;
            }
            return SiteProblem::None;
        }
    } // namespace

    std::string numbered_name(std::span<const save::Ride> rides, std::string_view symbol)
    {
        constexpr std::string_view kNumbered = "%ME% ";
        std::vector<int> taken;
        for (const save::Ride& r : rides)
            if (strings::equals_ignoring_case(r.base.symbol, symbol) && r.base.name.starts_with(kNumbered))
                taken.push_back(std::atoi(r.base.name.c_str() + kNumbered.size()));
        int number = 1;
        while (std::ranges::contains(taken, number))
            ++number;
        return std::format("{}{}", kNumbered, number);
    }

    std::vector<save::ShowTrick> show_tricks(std::string_view kind, std::span<const std::string> names, std::span<const std::string> animations, std::span<const float> seconds)
    {
        // Lower case, without "anim" and the zeros that lead a number.
        const auto plain = [](std::string_view text) {
            std::string out;
            for (std::size_t i = 0; i < text.size(); ++i)
            {
                const char c = strings::ascii_lower(text[i]);
                if (c == '0' && (out.empty() || !std::isdigit(static_cast<unsigned char>(out.back()))) && i + 1 < text.size() && std::isdigit(static_cast<unsigned char>(text[i + 1])))
                    continue;
                out.push_back(c);
            }
            for (std::size_t at; (at = out.find("anim")) != std::string::npos;)
                out.erase(at, 4);
            return out;
        };
        const auto lasting = [&](std::string_view tail) -> std::optional<float> {
            for (std::size_t i = 0; i < animations.size() && i < seconds.size(); ++i)
                if (plain(animations[i]).ends_with(plain(tail)))
                    return seconds[i];
            return std::nullopt;
        };
        std::vector<std::string> known;
        for (const std::string& name : names)
            if (const auto show = name.find("Show"); show != std::string::npos && plain(kind).ends_with(plain(std::string_view(name).substr(0, show + 4))))
                known.push_back(name);
        std::ranges::sort(known);
        std::stable_partition(known.begin(), known.end(), [&](const std::string& n) { return plain(n).contains("idle"); });
        std::vector<save::ShowTrick> tricks;
        for (const std::string& name : known)
        {
            const std::string_view tail = std::string_view(name).substr(name.find("Show") + 4);
            auto lasts = lasting(tail);
            if (!lasts && tricks.empty())
                lasts = lasting("loop");
            tricks.push_back({ .name = name, .seconds = lasts.value_or(0), .available = true, .invented = true });
        }
        return tricks;
    }

    RideSite ride_site(save::Tile tile, std::array<std::int32_t, 2> squares, std::int32_t turns, const save::TerrainGrid& grid)
    {
        const auto [col, row] = tile;
        RideSite site;
        site.turns = turns & 3;
        site.span = site.turns % 2 == 0 ? squares : std::array { squares[1], squares[0] };
        site.col = col - (site.span[0] - 1) / 2;
        site.row = row - (site.span[1] - 1) / 2;
        if (grid.contains(col, row))
        {
            const auto& corners = grid.at(col, row).corners;
            site.height = static_cast<std::int32_t>(std::lround(std::accumulate(corners.begin(), corners.end(), 0.0f) / 4));
        }
        return site;
    }

    SiteProblem site_problem(const RideSite& site, const save::TerrainGrid& grid, std::span<const save::PathTile> paths, const SolidTiles& solids)
    {
        for (std::int32_t r = site.row; r < site.row + site.span[1]; ++r)
        {
            for (std::int32_t c = site.col; c < site.col + site.span[0]; ++c)
            {
                if (const SiteProblem p = tile_problem(c, r, site.height, grid, paths, solids); p != SiteProblem::None)
                    return p;
            }
        }
        return SiteProblem::None;
    }

    SiteProblem water_problem(const RideSite& site, bool water_only, std::span<const save::WaterBody> bodies)
    {
        for (std::int32_t r = site.row; r < site.row + site.span[1]; ++r)
        {
            for (std::int32_t c = site.col; c < site.col + site.span[0]; ++c)
            {
                const auto lake = std::ranges::find_if(bodies, [&](const save::WaterBody& b) { return holds(b, { c, r }); });
                if (water_only && lake == bodies.end())
                    return SiteProblem::OffWater;
                if (!water_only && lake != bodies.end() && lake->level > static_cast<float>(site.height))
                    return SiteProblem::UnderWater;
            }
        }
        return SiteProblem::None;
    }

    void level_ground(save::TerrainGrid& grid, const RideSite& site)
    {
        const auto height = static_cast<float>(site.height);
        // Every corner point of the footprint, in each of the up to four tiles that share it.
        for (std::int32_t r = site.row; r <= site.row + site.span[1]; ++r)
        {
            for (std::int32_t c = site.col; c <= site.col + site.span[0]; ++c)
            {
                for (std::int32_t dr = 0; dr < 2; ++dr)
                {
                    for (std::int32_t dc = 0; dc < 2; ++dc)
                    {
                        // The tile whose corner (1 - dc, 1 - dr) is this point; corners go (col,row),
                        // (col+1,row), (col,row+1), (col+1,row+1).
                        const std::int32_t tc = c - 1 + dc;
                        const std::int32_t tr = r - 1 + dr;
                        if (grid.contains(tc, tr))
                            grid.at(tc, tr).corners[static_cast<std::size_t>((1 - dc) + 2 * (1 - dr))] = height;
                    }
                }
            }
        }
    }

    std::int32_t shaped_height(const save::TerrainCell& cell, bool up)
    {
        constexpr float kSlack = 0.01f; // Heights read back as floats sit a hair off whole units
        if (up)
        {
            const auto top = static_cast<std::int32_t>(std::ceil(cell.max_height() - kSlack));
            return cell.min_height() >= static_cast<float>(top) - kSlack ? top + 1 : top;
        }
        const auto bottom = static_cast<std::int32_t>(std::floor(cell.min_height() + kSlack));
        return cell.max_height() <= static_cast<float>(bottom) + kSlack ? bottom - 1 : bottom;
    }

    DoorSpot door_spot(const RideSite& site, save::Tile tile)
    {
        const auto [col, row] = tile;
        const std::int32_t last_col = site.col + site.span[0] - 1;
        const std::int32_t last_row = site.row + site.span[1] - 1;
        const std::int32_t c = std::clamp(col, site.col, last_col);
        const std::int32_t r = std::clamp(row, site.row, last_row);
        // How far the tile is past (or, inside, short of) each side; the nearest side takes the door.
        const std::array<std::int32_t, 4> past {
            site.col - col, // -x side
            row - last_row, // +z side
            col - last_col, // +x side
            site.row - row, // -z side
        };
        const auto side = static_cast<std::size_t>(std::ranges::max_element(past) - past.begin());
        switch (side)
        {
            case 0:
                return { site.col - 1, r, 2 };
            case 1:
                return { c, last_row + 1, 3 };
            case 2:
                return { last_col + 1, r, 0 };
            default:
                return { c, site.row - 1, 1 };
        }
    }

    std::optional<DoorSpot> station_door_spot(std::span<const save::TrackPiece> stations, save::Tile tile)
    {
        std::optional<DoorSpot> best;
        std::int64_t nearest = 0;
        for (const save::TrackPiece& p : stations)
        {
            // A track facing 0 or 2 runs along x, so its sides are across z; facing 1 or 3 the other way.
            const bool along_x = p.direction % 2 == 0;
            for (const std::int32_t side : { -1, 1 })
            {
                const DoorSpot spot {
                    along_x ? p.tile_x : p.tile_x + side,
                    along_x ? p.tile_z + side : p.tile_z,
                    static_cast<std::uint8_t>(along_x ? (side > 0 ? 3 : 1) : (side > 0 ? 0 : 2)),
                    p.height,
                };
                const std::int64_t dc = spot.col - tile.col, dr = spot.row - tile.row;
                if (!best || dc * dc + dr * dr < nearest)
                {
                    best = spot;
                    nearest = dc * dc + dr * dr;
                }
            }
        }
        return best;
    }

    std::string platform_symbol(std::string_view type, const save::TrackPiece& piece, bool station_before, bool station_after, std::span<const DoorSpot> doors)
    {
        bool left = false, right = false;
        for (const DoorSpot& d : doors)
        {
            if (d.height != piece.height || std::abs(d.col - piece.tile_x) + std::abs(d.row - piece.tile_z) != 1)
                continue;
            // A door faces the piece, so the side it stands on is where it faces from.
            const int turn = (d.direction - piece.direction + 4) % 4;
            left = left || turn == 3;
            right = right || turn == 1;
        }
        const std::string_view stairs = left && right ? "2stair" : left ? "1stairLeft" : right ? "1stairRight" : "";
        const std::string_view run = station_before && station_after ? "middle" : station_after ? "start" : station_before ? "end" : "startEnd";
        return std::format("{}{}{}", type, run, stairs);
    }

    SiteProblem door_problem(const RideSite& site, const DoorSpot& door, std::optional<DoorSpot> taken, const save::TerrainGrid& grid, std::span<const save::PathTile> paths, const SolidTiles& solids)
    {
        if (taken && taken->col == door.col && taken->row == door.row)
            return SiteProblem::Blocked;
        return tile_problem(door.col, door.row, site.height, grid, paths, solids);
    }

} // namespace rct3::park
