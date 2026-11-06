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
#include <string>
#include <utility>
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

        template<typename Attraction> std::string lowest_free_name(std::span<const Attraction> attractions, std::string_view symbol)
        {
            constexpr std::string_view kNumbered = "%ME% ";
            std::vector<int> taken;
            for (const Attraction& a : attractions)
                if (strings::equals_ignoring_case(a.base.symbol, symbol) && a.base.name.starts_with(kNumbered))
                    taken.push_back(std::atoi(a.base.name.c_str() + kNumbered.size()));
            int number = 1;
            while (std::ranges::contains(taken, number))
                ++number;
            return std::format("{}{}", kNumbered, number);
        }
    } // namespace

    std::string numbered_name(std::span<const save::Ride> rides, std::string_view symbol)
    {
        return lowest_free_name(rides, symbol);
    }

    std::string numbered_name(std::span<const save::Stall> stalls, std::string_view symbol)
    {
        return lowest_free_name(stalls, symbol);
    }

    void copy_menu(std::span<save::Stall> stalls, std::size_t from)
    {
        if (from >= stalls.size())
            return;
        const save::Stall& source = stalls[from];
        for (std::size_t i = 0; i < stalls.size(); ++i)
        {
            save::Stall& to = stalls[i];
            if (i == from || to.base.symbol != source.base.symbol || to.base.overlay_path != source.base.overlay_path)
                continue;
            std::vector<save::StallItem> items;
            for (const save::StallItem& item : source.items)
            {
                const auto kept = std::ranges::find(to.items, item.kind_symbol, &save::StallItem::kind_symbol);
                save::StallItem& copy = items.emplace_back(kept != to.items.end() ? *kept : save::StallItem { .item_asset = item.item_asset, .kind_symbol = item.kind_symbol });
                copy.price = item.price;
                copy.name = item.name;
                copy.colours = item.colours;
                copy.extras = item.extras;
            }
            to.items = std::move(items);
            for (save::StallExtra& extra : to.extras)
                if (const auto same = std::ranges::find(source.extras, extra.kind_symbol, &save::StallExtra::kind_symbol); same != source.extras.end())
                    extra.level = same->level;
        }
    }

    void offer_extras(save::Stall& stall, std::span<const CarriedKind> items)
    {
        for (const save::StallItem& item : stall.items)
        {
            const auto kind = std::ranges::find_if(items, [&](const CarriedKind& k) { return strings::equals_ignoring_case(k.symbol, item.kind_symbol); });
            if (kind == items.end())
                continue;
            for (const std::string& extra : kind->extras)
                if (std::ranges::none_of(stall.extras, [&](const save::StallExtra& e) { return strings::equals_ignoring_case(e.kind_symbol, extra); }))
                    stall.extras.push_back({ .kind_symbol = extra, .level = save::StallExtra::kSome });
        }
    }

    void move_item(save::Stall& stall, std::size_t from, std::size_t to)
    {
        if (from >= stall.items.size())
            return;
        to = std::min(to, stall.items.size() - 1);
        const auto at = stall.items.begin();
        if (from < to)
            std::rotate(at + static_cast<std::ptrdiff_t>(from), at + static_cast<std::ptrdiff_t>(from) + 1, at + static_cast<std::ptrdiff_t>(to) + 1);
        else
            std::rotate(at + static_cast<std::ptrdiff_t>(to), at + static_cast<std::ptrdiff_t>(from), at + static_cast<std::ptrdiff_t>(from) + 1);
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

    RideSite ride_site(save::Tile tile, std::array<std::int32_t, 2> squares, ModelTurn turn, const save::TerrainGrid& grid)
    {
        const auto [col, row] = tile;
        RideSite site;
        site.turn = ModelTurn::from(turn.value);
        site.span = site.turn.value % 2 == 0 ? squares : std::array { squares[1], squares[0] };
        site.col = col - (site.span[0] - 1) / 2;
        site.row = row - (site.span[1] - 1) / 2;
        float sum = 0;
        std::int32_t corners = 0;
        for (std::int32_t r = site.row; r < site.row + site.span[1]; ++r)
        {
            for (std::int32_t c = site.col; c < site.col + site.span[0]; ++c)
            {
                if (!grid.contains(c, r))
                    continue;
                const auto& at = grid.at(c, r).corners;
                sum += std::accumulate(at.begin(), at.end(), 0.0f);
                corners += 4;
            }
        }
        if (corners > 0)
            site.height = static_cast<std::int32_t>(std::floor(sum / static_cast<float>(corners) + 0.5f));
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

    SiteProblem ground_problem(const RideSite& site, const save::TerrainGrid& grid)
    {
        constexpr float kSlack = 0.05f; // Ground at the base is under it
        for (std::int32_t r = site.row; r < site.row + site.span[1]; ++r)
        {
            for (std::int32_t c = site.col; c < site.col + site.span[0]; ++c)
            {
                if (grid.contains(c, r) && grid.at(c, r).max_height() > static_cast<float>(site.height) + kSlack)
                    return SiteProblem::IntoGround;
            }
        }
        return SiteProblem::None;
    }

    std::int32_t clear_height(const RideSite& site, const save::TerrainGrid& grid)
    {
        constexpr float kSlack = 0.01f; // Heights read back as floats sit a hair off whole units
        float top = static_cast<float>(site.height);
        for (std::int32_t r = site.row; r < site.row + site.span[1]; ++r)
            for (std::int32_t c = site.col; c < site.col + site.span[0]; ++c)
                if (grid.contains(c, r))
                    top = std::max(top, grid.at(c, r).max_height());
        return static_cast<std::int32_t>(std::ceil(top - kSlack));
    }

    GroundCuts levelled_ground(const RideSite& site, Flatten flatten, const save::TerrainGrid& grid)
    {
        GroundCuts out;
        if (flatten == Flatten::Off)
            return out;
        const auto height = static_cast<float>(site.height);
        const std::int32_t reach = flatten == Flatten::Smooth ? 1 : 0;
        for (std::int32_t r = site.row - reach; r < site.row + site.span[1] + reach; ++r)
        {
            for (std::int32_t c = site.col - reach; c < site.col + site.span[0] + reach; ++c)
            {
                if (!grid.contains(c, r))
                    continue;
                const bool inside = c >= site.col && c < site.col + site.span[0] && r >= site.row && r < site.row + site.span[1];
                GroundCut cut { { c, r }, grid.at(c, r).corners };
                // Corners go (col,row), (col+1,row), (col,row+1), (col+1,row+1); a tile round the footprint
                // moves those on its edge.
                for (std::size_t k = 0; k < 4; ++k)
                {
                    const std::int32_t x = c + static_cast<std::int32_t>(k & 1);
                    const std::int32_t z = r + static_cast<std::int32_t>(k >> 1);
                    if (inside || (x >= site.col && x <= site.col + site.span[0] && z >= site.row && z <= site.row + site.span[1]))
                        cut.corners[k] = height;
                }
                if (cut.corners != grid.at(c, r).corners)
                    (inside ? out.footprint : out.rim).push_back(cut);
            }
        }
        return out;
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
        // How far the tile is past (or, inside, short of) each side; the nearest side takes the door, on the tile
        // across it from the rim, facing back.
        const std::array<std::pair<std::int32_t, Side>, 4> past { {
            { site.col - col, Side::kColBefore },
            { row - last_row, Side::kNextRow },
            { col - last_col, Side::kNextCol },
            { site.row - row, Side::kRowBefore },
        } };
        const Side side = std::ranges::max_element(past, { }, &std::pair<std::int32_t, Side>::first)->second;
        const save::Tile rim = side == Side::kColBefore ? save::Tile { site.col, r } : side == Side::kNextRow ? save::Tile { c, last_row } : side == Side::kNextCol ? save::Tile { last_col, r } : save::Tile { c, site.row };
        const save::Tile at = side.beyond(rim);
        return { at.col, at.row, turn_facing(side.opposite()) };
    }

    namespace
    {
        /// The two door spots beside station piece `p`, across the sides its track does not run through, each
        /// facing the piece: the one towards lower rows or columns first.
        std::array<DoorSpot, 2> beside_station(const save::TrackPiece& p)
        {
            // A track turned 0 or 2 runs along x, so its sides are across z; turned 1 or 3 the other way.
            const bool along_x = p.direction % 2 == 0;
            std::array<DoorSpot, 2> spots;
            for (std::size_t i = 0; i < 2; ++i)
            {
                const Side side = along_x ? std::array { Side::kRowBefore, Side::kNextRow }[i] : std::array { Side::kColBefore, Side::kNextCol }[i];
                const save::Tile at = side.beyond({ p.tile_x, p.tile_z });
                spots[i] = { at.col, at.row, turn_facing(side.opposite()), p.height };
            }
            return spots;
        }
    } // namespace

    std::optional<DoorSpot> station_door_spot(std::span<const save::TrackPiece> stations, save::Tile tile)
    {
        std::optional<DoorSpot> best;
        std::int64_t nearest = 0;
        for (const save::TrackPiece& p : stations)
        {
            for (const DoorSpot& spot : beside_station(p))
            {
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

    std::size_t door_station(std::span<const save::TrackPiece> stations, std::span<const save::Station> saved, const DoorSpot& door)
    {
        // A piece goes on from the one before when it stands on the next tile along that one's track.
        const auto follows = [](const save::TrackPiece& a, const save::TrackPiece& b) {
            const bool along_x = a.direction % 2 == 0;
            return a.height == b.height && std::abs(b.tile_x - a.tile_x) == (along_x ? 1 : 0) && std::abs(b.tile_z - a.tile_z) == (along_x ? 0 : 1);
        };
        const auto run_of = [&](std::int32_t col, std::int32_t row, ModelTurn turn) -> std::optional<std::size_t> {
            std::size_t run = 0;
            std::optional<std::size_t> found;
            for (std::size_t k = 0; k < stations.size(); ++k)
            {
                if (k > 0 && !follows(stations[k - 1], stations[k]))
                    ++run;
                for (const DoorSpot& spot : beside_station(stations[k]))
                    if (!found && col == spot.col && row == spot.row && turn == spot.turn)
                        found = run;
            }
            if (found && run > 0 && *found == run && follows(stations.back(), stations.front()))
                return 0;
            return found;
        };
        const auto run = run_of(door.col, door.row, door.turn);
        const auto at_run = [&](const std::optional<save::PlacementSpot>& spot) { return spot && run_of(spot->col, spot->row, spot_turn(*spot)) == run; };
        for (std::size_t k = 0; k < saved.size(); ++k)
            if (at_run(saved[k].entrance_spot) || at_run(saved[k].exit_spot))
                return k;
        return static_cast<std::size_t>(std::ranges::find_if(saved, [](const save::Station& s) { return !s.entrance_spot && !s.exit_spot; }) - saved.begin());
    }

    std::optional<std::string_view> platform_symbol(std::string_view type, const save::TrackPiece& piece, const PlatformPlace& place, std::span<const DoorSpot> doors, std::span<const std::string> names)
    {
        bool left = false, right = false;
        for (const DoorSpot& d : doors)
        {
            if (d.height != piece.height || std::abs(d.col - piece.tile_x) + std::abs(d.row - piece.tile_z) != 1)
                continue;
            // A door faces the piece, so the side it stands on is where it faces from.
            const ModelTurn turn = d.turn.turned(-piece.direction);
            left = left || turn == ModelTurn { 3 };
            right = right || turn == ModelTurn { 1 };
        }
        const std::string_view stairs = left && right ? "2stair" : left ? "1stairLeft" : right ? "1stairRight" : "";
        const bool before = place.station_before, after = place.station_after;
        const std::string_view run = !place.run.empty() ? place.run : before && after ? "middle" : after ? "start" : before ? "end" : "startEnd";
        const std::string_view track = !place.track_before && !place.track_after ? "trackStartEnd" : !place.track_before ? "trackStart" : !place.track_after ? "trackEnd" : "";
        const std::string_view side = place.side == 1 ? "_L" : place.side == 2 ? "_R" : "";
        for (const std::string_view r : { run, std::string_view("middle") })
            for (const auto& [with_run, with_track] : { std::pair { true, true }, { true, false }, { false, true }, { false, false } })
            {
                const std::string want = std::format("{}{}{}{}{}", type, with_run ? r : "", stairs, with_track ? track : "", side);
                if (const auto found = std::ranges::find_if(names, [&](const std::string& n) { return strings::equals_ignoring_case(n, want); }); found != names.end())
                    return *found;
            }
        return std::nullopt;
    }

    SiteProblem door_problem(const RideSite& site, const DoorSpot& door, std::optional<DoorSpot> taken, const save::TerrainGrid& grid, std::span<const save::PathTile> paths, const SolidTiles& solids)
    {
        if (taken && taken->col == door.col && taken->row == door.row)
            return SiteProblem::Blocked;
        return tile_problem(door.col, door.row, site.height, grid, paths, solids);
    }

} // namespace rct3::park
