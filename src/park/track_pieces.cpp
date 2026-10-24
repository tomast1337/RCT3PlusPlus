// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/track_pieces.hpp"

#include "core/maybe.hpp"
#include "ovl/overlay.hpp"
#include "ovl/ovl.hpp"
#include "ovl/scenery_item.hpp"
#include "ovl/spline.hpp"
#include "ovl/track_section.hpp"
#include "park/asset_index.hpp"
#include "strings/ascii.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <iterator>
#include <map>
#include <numbers>
#include <ranges>
#include <tuple>

namespace rct3::park
{

    namespace
    {
        constexpr float kDegrees = 180.0f / std::numbers::pi_v<float>;
        constexpr float kStepDegrees = 45.0f; ///< Of a bank or heading step
        /// Steepest pitch, in degrees, of each slope step: level pieces are built a little out of true, a
        /// gentle slope rises about 27 degrees (20 across a diagonal), a steep one about 63 (54 across).
        constexpr std::array<float, kSlopeSteps> kSlopeLimits { 8, 40, 75 };
        /// Ends steeper than this point straight up or down; their heading comes from the rails instead.
        constexpr float kUpright = 0.97f;
        /// Ends meet when this close, in park units: the pieces' splines join to well within it.
        constexpr float kMeet = 0.1f;

        // The curve pair: 1 to 3 on one side and 4 to 6 on the other for curves of one of three sizes, 0 for
        // pieces that do not turn, 7 for the rest.
        constexpr std::uint32_t kLastCurveJoin = 6;

        int wrap_heading(int heading)
        {
            return ((heading % kHeadings) + kHeadings) % kHeadings;
        }

        /// `steps` wrapped to -3..4, upside down being 4.
        int wrap_bank(int steps)
        {
            const int wrapped = wrap_heading(steps + kBankSteps) - kBankSteps;
            return wrapped == -kBankSteps ? kUpsideDown : wrapped;
        }

        /// A turn of `steps` eighths wrapped to -4..3.
        int wrap_turn(int steps)
        {
            return wrap_heading(steps + kHeadings / 2) - kHeadings / 2;
        }

        int slope_steps(float pitch)
        {
            const float a = std::abs(pitch);
            int steps = kSlopeSteps;
            for (int i = 0; i < kSlopeSteps; ++i)
            {
                if (a < kSlopeLimits.at(static_cast<std::size_t>(i)))
                {
                    steps = i;
                    break;
                }
            }
            return pitch < 0 ? -steps : steps;
        }

        int heading_steps(const glm::vec3& flat)
        {
            return wrap_heading(static_cast<int>(std::lround(std::atan2(flat.z, -flat.x) * kDegrees / kStepDegrees)));
        }

        /// How plain a piece is: fewer flags first, then the shorter name.
        auto plainness(const PieceShape& p)
        {
            return std::make_tuple(std::popcount(p.record.flags & ~kPieceCommon), p.record.name.size());
        }

        glm::vec3 turned(const glm::vec3& v, std::int32_t turns)
        {
            const glm::vec2 x = turned_x(turns);
            const glm::vec2 z { -x.y, x.x };
            return { x.x * v.x + z.x * v.z, v.y, x.y * v.x + z.y * v.z };
        }

        glm::vec3 vec(const std::array<float, 3>& a)
        {
            return { a[0], a[1], a[2] };
        }

        /// The middle line of a piece whose rails follow `left` and `right`. Where an end stores no tangent,
        /// the line to the next node stands in for it.
        std::optional<PieceLine> middle_line(const ovl::Spline& left, const ovl::Spline& right)
        {
            const auto& l = left.nodes;
            const auto& r = right.nodes;
            if (l.size() < 2 || r.size() < 2)
                return std::nullopt;
            const auto middle = [](const ovl::SplineNode& a, const ovl::SplineNode& b) { return (vec(a.position) + vec(b.position)) / 2.0f; };
            PieceLine line;
            line.start = middle(l.front(), r.front());
            line.end = middle(l.back(), r.back());
            line.start_direction = vec(l.front().tangent_out) + vec(r.front().tangent_out);
            line.end_direction = -(vec(l.back().tangent_in) + vec(r.back().tangent_in));
            constexpr float kNone = 1e-6f;
            if (glm::length(line.start_direction) < kNone)
                line.start_direction = middle(l[1], r[1]) - line.start;
            if (glm::length(line.end_direction) < kNone)
                line.end_direction = line.end - middle(l[l.size() - 2], r[r.size() - 2]);
            if (glm::length(line.start_direction) < kNone || glm::length(line.end_direction) < kNone)
                return std::nullopt;
            line.start_side = vec(r.front().position) - vec(l.front().position);
            line.end_side = vec(r.back().position) - vec(l.back().position);
            std::vector<glm::vec3> sides; // From the left rail to the right, at each point of the path
            if (l.size() == r.size())
            {
                constexpr int kPerPart = 8;
                for (std::size_t i = 0; i + 1 < l.size(); ++i)
                {
                    for (int k = 0; k < kPerPart; ++k)
                    {
                        const float t = static_cast<float>(k) / kPerPart;
                        const glm::vec3 a = vec(ovl::spline_point(left, i, t));
                        const glm::vec3 b = vec(ovl::spline_point(right, i, t));
                        line.path.push_back((a + b) / 2.0f);
                        sides.push_back(b - a);
                    }
                }
                line.path.push_back(line.end);
            }
            else
                line.path = { line.start, line.end };
            sides.resize(line.path.size() - 1, line.start_side);
            sides.push_back(line.end_side);
            // Up is square to the way the track runs and to the line across its rails.
            for (std::size_t k = 0; k < line.path.size(); ++k)
            {
                const glm::vec3 way = k == 0 ? line.start_direction : k + 1 == line.path.size() ? line.end_direction : line.path[k + 1] - line.path[k - 1];
                line.ups.push_back(glm::normalize(glm::cross(glm::normalize(way), glm::normalize(sides[k]))));
            }
            return line;
        }
    } // namespace

    std::vector<std::string> named_overlays(const ovl::Overlay& ride, std::string_view path)
    {
        std::vector<std::string> out;
        for (const auto part : { maybe(&ride.common()), ride.unique() })
        {
            if (!part)
                continue;
            for (const ovl::Block& block : part->file.block_types[0].blocks)
            {
                if (block.file_offset > part->bytes.size() || part->bytes.size() - block.file_offset < block.size)
                    continue;
                const std::string_view text(reinterpret_cast<const char*>(part->bytes.data() + block.file_offset), block.size);
                for (const auto word : std::views::split(text, '\0'))
                {
                    const std::string_view s(word.begin(), word.end());
                    if (!s.starts_with("../"))
                        continue;
                    if (auto named = referenced_overlay(path, s); named && !std::ranges::contains(out, *named))
                        out.push_back(std::move(*named));
                }
            }
        }
        return out;
    }

    std::expected<PieceCatalogue, std::string> load_piece_catalogue(const std::filesystem::path& file, std::string overlay)
    {
        auto loaded = ovl::Overlay::load(file);
        if (!loaded)
            return std::unexpected(std::string(ovl::to_string(loaded.error())));
        // Sections name their splines and items in any case.
        std::map<std::string, ovl::Spline, strings::LessIgnoringCase> splines;
        std::map<std::string, ovl::SceneryItem, strings::LessIgnoringCase> items;
        std::vector<ovl::TrackSectionItem> sections;
        for (const auto part : { maybe(&loaded->common()), loaded->unique() })
        {
            if (!part)
                continue;
            for (ovl::SplineItem& s : ovl::read_splines(*loaded, *part))
            {
                if (s.spline)
                    splines.try_emplace(std::string(s.name), std::move(*s.spline));
            }
            for (ovl::SceneryItemEntry& e : ovl::read_scenery_items(*loaded, *part))
            {
                if (e.item)
                    items.try_emplace(std::string(e.name), std::move(*e.item));
            }
            std::ranges::move(ovl::read_track_sections(*loaded, *part), std::back_inserter(sections));
        }
        std::vector<PieceRecord> records;
        std::vector<PieceLine> lines;
        for (const ovl::TrackSectionItem& t : sections)
        {
            if (!t.section)
                continue;
            const ovl::TrackSection& section = *t.section;
            const auto left = splines.find(section.splines[0]);
            const auto right = splines.find(section.splines[1]);
            const std::string_view item_name = section.scenery_item.empty() ? t.name : section.scenery_item;
            const auto item = items.find(item_name);
            if (left == splines.end() || right == splines.end() || item == items.end())
                continue;
            const auto line = middle_line(left->second, right->second);
            if (!line)
                continue;
            PieceRecord& r = records.emplace_back();
            r.name = t.name;
            r.item = item->first;
            r.join_a = section.join_a;
            r.join_b = section.join_b;
            r.flags = section.flags[0];
            r.role = section.role;
            r.squares = { static_cast<std::int32_t>(item->second.x_squares), static_cast<std::int32_t>(item->second.z_squares) };
            r.positioning = static_cast<Positioning>(item->second.positioning);
            r.cost = item->second.cost;
            const auto join = [&](std::string_view name, const ovl::JoinTags& tags, std::uint32_t width) {
                const auto copied = [](const std::vector<std::string_view>& names) { return std::vector<std::string>(names.begin(), names.end()); };
                return PieceJoin { std::string(name), copied(tags.offers), copied(tags.needs), copied(tags.forbids), width };
            };
            r.joins = { join(section.start_join, section.tags[0], section.widths[0]), join(section.end_join, section.tags[1], section.widths[1]) };
            r.side = section.side;
            r.group = section.group;
            lines.push_back(*line);
        }
        return PieceCatalogue { std::move(overlay), shape_pieces(records, lines) };
    }

    float bank_degrees(const glm::vec3& direction, const glm::vec3& side)
    {
        const glm::vec3 forward = glm::normalize(direction);
        if (std::abs(forward.y) > kUpright)
            return 0;
        const glm::vec3 level = glm::normalize(glm::cross(glm::vec3 { 0, 1, 0 }, forward));
        const glm::vec3 up = glm::cross(forward, level);
        return std::atan2(glm::dot(side, up), glm::dot(side, level)) * kDegrees;
    }

    PieceEnd measure_end(const glm::vec3& direction, const glm::vec3& side, float base_bank)
    {
        const glm::vec3 forward = glm::normalize(direction);
        PieceEnd end;
        end.slope = slope_steps(std::atan2(forward.y, std::hypot(forward.x, forward.z)) * kDegrees);
        end.bank = wrap_bank(static_cast<int>(std::lround((bank_degrees(direction, side) - base_bank) / kStepDegrees)));
        // Straight up or down, the track's way across the ground is the rails' level line turned back.
        const glm::vec3 flat = std::abs(forward.y) > kUpright ? glm::cross(side, glm::vec3 { 0, 1, 0 }) : forward;
        end.heading = heading_steps(flat);
        return end;
    }

    PieceShape shape_piece(PieceRecord record, const PieceLine& line, float base_bank)
    {
        PieceShape shape;
        shape.line = line;
        shape.entry = measure_end(line.start_direction, line.start_side, base_bank);
        shape.exit = measure_end(line.end_direction, line.end_side, base_bank);
        for (const auto& [flag, toggle] : { std::pair { kPieceChain, PieceToggle::Chain }, { kPieceEnclosed, PieceToggle::Enclosed }, { kPieceWater, PieceToggle::Water }, { kPieceBooster, PieceToggle::Booster } })
        {
            if ((record.flags & flag) != 0)
                shape.toggles |= toggle_bit(toggle);
        }
        if (record.joins[1].width != 0)
            shape.toggles |= toggle_bit(PieceToggle::Wide);
        const int turn = wrap_turn(shape.exit.heading - shape.entry.heading);
        const std::uint32_t join = record.join_a;
        if ((record.flags & kPieceStation) != 0)
            shape.use = PieceUse::Station;
        else if (join == 0 && turn == 0)
            shape.curve = 0;
        else if (join >= 1 && join <= kLastCurveJoin && turn != 0)
        {
            const int size = static_cast<int>(join <= kCurveSizes ? join : join - kCurveSizes);
            shape.curve = turn < 0 ? -size : size;
        }
        else
            shape.use = PieceUse::Special;
        shape.record = std::move(record);
        return shape;
    }

    float station_bank(std::span<const PieceRecord> records, std::span<const PieceLine> lines)
    {
        for (std::size_t i = 0; i < records.size() && i < lines.size(); ++i)
        {
            if ((records[i].flags & kPieceStation) != 0)
                return std::abs(bank_degrees(lines[i].start_direction, lines[i].start_side)) > 2 * kStepDegrees ? 4 * kStepDegrees : 0;
        }
        return 0;
    }

    std::vector<PieceShape> shape_pieces(std::span<const PieceRecord> records, std::span<const PieceLine> lines)
    {
        const float base = station_bank(records, lines);
        std::vector<PieceShape> pieces;
        for (std::size_t i = 0; i < records.size() && i < lines.size(); ++i)
            pieces.push_back(shape_piece(records[i], lines[i], base));
        // One plain piece per shape: the plainest keeps it, the others are offered by name.
        using Key = std::tuple<int, int, int, int, int, int, int, std::uint8_t>;
        std::map<Key, std::size_t> plainest;
        for (std::size_t i = 0; i < pieces.size(); ++i)
        {
            const PieceShape& p = pieces[i];
            if (p.use != PieceUse::Plain)
                continue;
            const Key key { p.entry.slope, p.entry.bank, p.entry.heading, p.exit.slope, p.exit.bank, p.exit.heading, p.curve, p.toggles };
            const auto [found, added] = plainest.try_emplace(key, i);
            if (added)
                continue;
            if (plainness(p) < plainness(pieces[found->second]))
            {
                pieces[found->second].use = PieceUse::Special;
                found->second = i;
            }
            else
                pieces[i].use = PieceUse::Special;
        }
        return pieces;
    }

    PieceEnd laid_entry(const PieceShape& shape, bool reversed)
    {
        if (!reversed)
            return shape.entry;
        return { -shape.exit.slope, wrap_bank(-shape.exit.bank), wrap_heading(shape.exit.heading + kHeadings / 2) };
    }

    PieceEnd laid_exit(const PieceShape& shape, bool reversed)
    {
        if (!reversed)
            return shape.exit;
        return { -shape.entry.slope, wrap_bank(-shape.entry.bank), wrap_heading(shape.entry.heading + kHeadings / 2) };
    }

    int laid_curve(const PieceShape& shape, bool reversed)
    {
        return reversed ? -shape.curve : shape.curve;
    }

    bool fits_after(const PieceEnd& tip, const PieceEnd& entry)
    {
        return tip.slope == entry.slope && tip.bank == entry.bank && diagonal(tip) == diagonal(entry);
    }

    bool ends_meet(const PieceJoin& before, const PieceJoin& next)
    {
        const auto any_in = [](const std::vector<std::string>& names, const std::vector<std::string>& in) { return std::ranges::any_of(names, [&](const std::string& n) { return std::ranges::any_of(in, [&](const std::string& i) { return strings::equals_ignoring_case(n, i); }); }); };
        const auto allows = [&](const PieceJoin& a, const PieceJoin& b) { return (a.needs.empty() || any_in(a.needs, b.offers)) && !any_in(a.forbids, b.offers); };
        return strings::equals_ignoring_case(before.name, next.name) && before.width == next.width && allows(before, next) && allows(next, before);
    }

    const PieceJoin& laid_start_join(const PieceShape& shape, bool reversed)
    {
        return shape.record.joins[reversed ? 1 : 0];
    }

    const PieceJoin& laid_end_join(const PieceShape& shape, bool reversed)
    {
        return shape.record.joins[reversed ? 0 : 1];
    }

    TrackTip tip_of(const save::PlacedObject& object, const PieceShape& shape, bool reversed, const save::TerrainGrid& grid)
    {
        const glm::mat4 m = object_transform(object, shape.record.positioning, grid, shape.record.squares);
        TrackTip tip;
        tip.position = glm::vec3(m * glm::vec4(reversed ? shape.line.start : shape.line.end, 1));
        tip.end = laid_exit(shape, reversed);
        tip.end.heading = wrap_heading(tip.end.heading + 2 * object.rotation);
        return tip;
    }

    TrackTip start_of(const save::PlacedObject& object, const PieceShape& shape, bool reversed, const save::TerrainGrid& grid)
    {
        TrackTip tip = tip_of(object, shape, !reversed, grid);
        tip.end = { -tip.end.slope, wrap_bank(-tip.end.bank), wrap_heading(tip.end.heading + kHeadings / 2) };
        return tip;
    }

    std::optional<PiecePlacement> place_after(const TrackTip& tip, const PieceShape& shape, bool reversed, const save::TerrainGrid& grid)
    {
        const PieceEnd entry = laid_entry(shape, reversed);
        if (!fits_after(tip.end, entry) || shape.record.positioning != Positioning::Tile || grid.tile_size_x <= 0 || grid.tile_size_z <= 0)
            return std::nullopt;
        // A quarter turn moves the heading two eighths.
        const std::int32_t turns = wrap_heading(tip.end.heading - entry.heading) / 2;
        const glm::vec3 start = turned(reversed ? shape.line.end : shape.line.start, turns);
        const bool across = (turns & 1) != 0;
        const auto span_x = static_cast<float>(shape.record.squares[across ? 1 : 0]);
        const auto span_z = static_cast<float>(shape.record.squares[across ? 0 : 1]);
        const float col = (tip.position.x - start.x - grid.world_x(0)) / grid.tile_size_x - span_x / 2;
        const float row = (tip.position.z - start.z - grid.world_z(0)) / grid.tile_size_z - span_z / 2;
        const float height = tip.position.y - start.y;
        const auto whole = [](float v) { return std::abs(v - std::round(v)) < kMeet; };
        if (!whole(col) || !whole(row) || !whole(height))
            return std::nullopt;
        PiecePlacement placed;
        save::PlacedObject& o = placed.object;
        o.symbol = shape.record.item;
        o.col = static_cast<std::int32_t>(std::lround(col));
        o.row = static_cast<std::int32_t>(std::lround(row));
        o.rotation = turns;
        o.base_height = static_cast<std::int32_t>(std::lround(height));
        o.force_absolute_height = true;
        // Checked against the transform the park draws it with, so what is placed is what shows.
        const glm::mat4 m = object_transform(o, shape.record.positioning, grid, shape.record.squares);
        const glm::vec3 meets = glm::vec3(m * glm::vec4(reversed ? shape.line.end : shape.line.start, 1));
        if (glm::length(meets - tip.position) > kMeet)
            return std::nullopt;
        placed.next = tip_of(o, shape, reversed, grid);
        return placed;
    }

    std::vector<LaidPiece> pieces_after(std::span<const PieceShape> pieces, const PieceEnd& tip, std::optional<LaidPiece> last)
    {
        const std::optional<const PieceShape&> before = last && last->index < pieces.size() ? maybe(&pieces[last->index]) : std::nullopt;
        std::vector<LaidPiece> fit;
        for (std::size_t i = 0; i < pieces.size(); ++i)
        {
            for (const bool reversed : { false, true })
            {
                if (!fits_after(tip, laid_entry(pieces[i], reversed)))
                    continue;
                if (before && (before->record.side != pieces[i].record.side || !ends_meet(laid_end_join(*before, last->reversed), laid_start_join(pieces[i], reversed))))
                    continue;
                fit.push_back({ i, reversed });
            }
        }
        return fit;
    }

    std::vector<LaidPiece> starting_pieces(std::span<const PieceShape> pieces)
    {
        std::vector<LaidPiece> out;
        for (std::size_t i = 0; i < pieces.size(); ++i)
        {
            const std::uint32_t f = pieces[i].record.flags;
            if ((f & kPieceStarts) != 0)
                out.push_back({ i, false });
            if ((f & kPieceStarts) != 0 && (f & kPieceStartsBackward) != 0)
                out.push_back({ i, true });
        }
        return out;
    }

    std::optional<const PieceShape&> shape_named(const PieceCatalogue& catalogue, std::string_view symbol)
    {
        const std::string_view name = symbol.substr(0, symbol.find(':'));
        const auto found = std::ranges::find_if(catalogue.pieces, [&](const PieceShape& p) { return strings::equals_ignoring_case(p.record.name, name); });
        return found != catalogue.pieces.end() ? maybe(&*found) : std::nullopt;
    }

} // namespace rct3::park
