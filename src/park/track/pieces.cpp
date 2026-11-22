// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/track/pieces.hpp"

#include "core/maybe.hpp"
#include "core/types.hpp"
#include "math/portable.hpp"
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
#include <cstdint>
#include <format>
#include <iterator>
#include <limits>
#include <map>
#include <numbers>
#include <ranges>
#include <tuple>

namespace rct3::park::track
{

    namespace
    {
        constexpr f32 kDegrees = 180.0f / std::numbers::pi_v<f32>;
        constexpr f32 kStepDegrees = 45.0f; ///< Of a bank or heading step
        /// Steepest pitch, in degrees, of each slope step: level pieces are built a little out of true, a
        /// gentle slope rises about 27 degrees (20 across a diagonal), a steep one about 63 (54 across).
        constexpr std::array<f32, kSlopeSteps> kSlopeLimits { 8, 40, 75 };
        /// Ends steeper than this point straight up or down; their heading comes from the rails instead.
        constexpr f32 kUpright = 0.97f;
        /// Ends meet when this close, in park units: the pieces' splines join to well within it.
        constexpr f32 kMeet = 0.1f;

        // The curve pair: 1 to 3 on one side and 4 to 6 on the other for curves of one of three sizes, 0 for
        // pieces that do not turn, 7 for the rest.
        constexpr u32 kLastCurveJoin = 6;

        /// `steps` wrapped to -3..4, upside down being 4.
        i32 wrap_bank(i32 steps)
        {
            const i32 wrapped = ((steps + kBankSteps) & 7) - kBankSteps; // Eight steps round a whole turn
            return wrapped == -kBankSteps ? kUpsideDown : wrapped;
        }

        i32 slope_steps(f32 pitch)
        {
            const f32 a = std::abs(pitch);
            i32 steps = kSlopeSteps;
            for (i32 i = 0; i < kSlopeSteps; ++i)
            {
                if (a < kSlopeLimits.at(static_cast<usize>(i)))
                {
                    steps = i;
                    break;
                }
            }
            return pitch < 0 ? -steps : steps;
        }

        Heading8 heading_steps(const glm::vec3& flat)
        {
            return Heading8::from(static_cast<i32>(std::lround(math::atan2(flat.z, -flat.x) * kDegrees / kStepDegrees)));
        }

        /// How plain a piece is: fewer flags first, then the shorter name.
        auto plainness(const PieceShape& p)
        {
            return std::make_tuple(std::popcount(p.record.flags & ~kPieceCommon), p.record.name.size());
        }

        glm::vec3 turned(const glm::vec3& v, ModelTurn turn)
        {
            const glm::vec2 x = object::turned_x(turn);
            const glm::vec2 z { -x.y, x.x };
            return { x.x * v.x + z.x * v.z, v.y, x.y * v.x + z.y * v.z };
        }

        glm::vec3 vec(const std::array<f32, 3>& a)
        {
            return { a[0], a[1], a[2] };
        }

        /// The line whose ends `shape` meets the pieces before and after it at.
        const PieceLine& meeting_line(const PieceShape& shape)
        {
            return shape.record.join_line ? *shape.record.join_line : shape.line;
        }

    } // namespace

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
        constexpr f32 kNone = 1e-6f;
        if (glm::length(line.start_direction) < kNone)
            line.start_direction = middle(l[1], r[1]) - line.start;
        if (glm::length(line.end_direction) < kNone)
            line.end_direction = line.end - middle(l[l.size() - 2], r[r.size() - 2]);
        if (glm::length(line.start_direction) < kNone || glm::length(line.end_direction) < kNone)
            return std::nullopt;
        line.length = (left.length + right.length) / 2;
        line.start_side = vec(r.front().position) - vec(l.front().position);
        line.end_side = vec(r.back().position) - vec(l.back().position);
        std::vector<glm::vec3> sides; // From the left rail to the right, at each point of the path
        // Each rail is walked by the same share of its own length, so the points paired are abreast even where one
        // rail runs faster along its curve than the other (round a roll or a tilt), or the rails are split into a
        // different number of parts (as a funnel's are, one part apart). A rail that stores its curve parameter
        // at marks along each part is read between those marks, as the park places cars; any other rail's length
        // is measured over many short chords.
        struct Walked
        {
            std::vector<glm::vec3> points { };
            std::vector<f32> lengths { }; ///< From the rail's start to each point
        };
        const auto marked = [](const ovl::Spline& rail) { return rail.length > 0 && rail.segment_parameters.size() == rail.nodes.size() - 1 && rail.segment_lengths.size() == rail.nodes.size() - 1; };
        const auto walk = [&](const ovl::Spline& rail) {
            constexpr usize kFine = 64; ///< Chords per part, so a chord's sag is a fraction of a millimetre
            Walked w { .points = { vec(rail.nodes.front().position) }, .lengths = { 0 } };
            for (usize part = 0; !marked(rail) && part + 1 < rail.nodes.size(); ++part)
                for (usize i = 1; i <= kFine; ++i)
                {
                    w.points.push_back(vec(ovl::spline_point(rail, part, static_cast<f32>(i) / kFine)));
                    w.lengths.push_back(w.lengths.back() + glm::distance(w.points[w.points.size() - 2], w.points.back()));
                }
            return w;
        };
        const auto rail_at = [&](const ovl::Spline& rail, const Walked& walked, f32 share) {
            if (marked(rail))
            {
                f32 along = share * rail.length;
                usize part = 0;
                while (part + 1 < rail.segment_lengths.size() && along > rail.segment_lengths[part])
                    along -= rail.segment_lengths[part++];
                constexpr auto kSpans = static_cast<f32>(ovl::kLengthMarks + 1); // Between the part's ends and its marks
                const f32 mark = std::clamp(rail.segment_lengths[part] > 0 ? along / rail.segment_lengths[part] : 0.0f, 0.0f, 1.0f) * kSpans;
                const auto parameter = [&](usize m) { return m == 0 ? 0.0f : m > ovl::kLengthMarks ? 1.0f : static_cast<f32>(rail.segment_parameters[part][m - 1]) / 255.0f; };
                const usize below = std::min(static_cast<usize>(mark), ovl::kLengthMarks);
                return vec(ovl::spline_point(rail, part, glm::mix(parameter(below), parameter(below + 1), mark - static_cast<f32>(below))));
            }
            const f32 along = share * walked.lengths.back();
            const usize i = std::clamp<usize>(static_cast<usize>(std::ranges::upper_bound(walked.lengths, along) - walked.lengths.begin()), 1, walked.lengths.size() - 1);
            const f32 chord = walked.lengths[i] - walked.lengths[i - 1];
            return glm::mix(walked.points[i - 1], walked.points[i], chord > 0 ? (along - walked.lengths[i - 1]) / chord : 0.0f);
        };
        const Walked walked_left = walk(left);
        const Walked walked_right = walk(right);
        constexpr usize kPerPart = 8;
        const usize steps = (std::max(l.size(), r.size()) - 1) * kPerPart;
        for (usize k = 0; k < steps; ++k)
        {
            const f32 share = static_cast<f32>(k) / static_cast<f32>(steps);
            const glm::vec3 a = rail_at(left, walked_left, share);
            const glm::vec3 b = rail_at(right, walked_right, share);
            line.path.push_back((a + b) / 2.0f);
            sides.push_back(b - a);
        }
        line.path.push_back(line.end);
        sides.push_back(line.end_side);
        // Up is square to the way the track runs and to the line across its rails.
        for (usize k = 0; k < line.path.size(); ++k)
        {
            const glm::vec3 way = k == 0 ? line.start_direction : k + 1 == line.path.size() ? line.end_direction : line.path[k + 1] - line.path[k - 1];
            line.ups.push_back(glm::normalize(glm::cross(glm::normalize(way), glm::normalize(sides[k]))));
        }
        return line;
    }

    std::vector<std::string> named_overlays(const ovl::Overlay& ride, std::string_view path)
    {
        std::vector<std::string> out;
        for (const auto part : { core::maybe(&ride.common()), ride.unique() })
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

    std::vector<std::filesystem::path> piece_files_beside(const std::filesystem::path& file)
    {
        constexpr std::string_view kCommon = ".common.ovl";
        std::vector<std::filesystem::path> beside;
        std::error_code error;
        for (auto it = std::filesystem::directory_iterator(file.parent_path(), error); !error && it != std::filesystem::directory_iterator(); it.increment(error))
        {
            const std::string name = it->path().filename().string();
            if (it->path() == file || name.size() <= kCommon.size() || !strings::equals_ignoring_case(std::string_view(name).substr(name.size() - kCommon.size()), kCommon))
                continue;
            if (ovl::lists_loader(it->path(), ovl::kTrackSectionTag))
                beside.push_back(it->path());
        }
        std::ranges::sort(beside);
        return beside;
    }

    std::vector<std::vector<glm::vec3>> slide_climbs(std::span<const glm::vec3> loop, std::span<const std::vector<glm::vec3>> paths, std::span<const std::vector<glm::vec3>> slides)
    {
        std::vector<std::vector<glm::vec3>> ways;
        if (loop.empty())
            return ways;
        for (const auto& slide : slides)
        {
            if (slide.empty())
                continue;
            const auto stub = std::ranges::min_element(paths, { }, [&](const std::vector<glm::vec3>& p) { return p.empty() ? std::numeric_limits<f32>::max() : glm::distance(p.back(), slide.front()); });
            const bool by_path = stub != paths.end() && !stub->empty();
            const glm::vec3 off = by_path ? stub->front() : slide.front();
            const auto cut = std::ranges::min_element(loop, { }, [&](const glm::vec3& p) { return glm::distance(p, off); });
            auto& way = ways.emplace_back(loop.begin(), std::next(cut));
            if (by_path)
                way.insert(way.end(), stub->begin(), stub->end());
            way.insert(way.end(), slide.begin(), slide.end());
        }
        return ways;
    }

    std::expected<PieceCatalogue, std::string> load_piece_catalogue(const std::filesystem::path& file, std::string overlay)
    {
        auto first = ovl::Overlay::load(file);
        if (!first)
            return std::unexpected(std::string(ovl::to_string(first.error())));
        std::vector<ovl::Overlay> loaded;
        loaded.push_back(std::move(*first));
        for (const std::filesystem::path& path : piece_files_beside(file))
            if (auto more = ovl::Overlay::load(path))
                loaded.push_back(std::move(*more));
        // Sections name their splines and items in any case.
        std::map<std::string, ovl::Spline, strings::LessIgnoringCase> splines;
        std::map<std::string, ovl::SceneryItem, strings::LessIgnoringCase> items;
        std::vector<ovl::TrackSectionItem> sections;
        for (const ovl::Overlay& o : loaded)
            for (const auto part : { core::maybe(&o.common()), o.unique() })
            {
                if (!part)
                    continue;
                for (ovl::SplineItem& s : ovl::read_splines(o, *part))
                {
                    if (s.spline)
                        splines.try_emplace(std::string(s.name), std::move(*s.spline));
                }
                for (ovl::SceneryItemEntry& e : ovl::read_scenery_items(o, *part))
                {
                    if (e.item)
                        items.try_emplace(std::string(e.name), std::move(*e.item));
                }
                for (ovl::TrackSectionItem& t : ovl::read_track_sections(o, *part))
                    if (std::ranges::none_of(sections, [&](const ovl::TrackSectionItem& had) { return strings::equals_ignoring_case(had.name, t.name); }))
                        sections.push_back(std::move(t));
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
            r.squares = { static_cast<i32>(item->second.x_squares), static_cast<i32>(item->second.z_squares) };
            r.positioning = static_cast<object::Positioning>(item->second.positioning);
            r.cost = item->second.cost;
            for (usize k = 0; k < r.colours.size(); ++k)
                r.colours[k] = static_cast<u8>(item->second.default_colours[k]);
            const auto join = [&](std::string_view name, const ovl::JoinTags& tags, u32 width) {
                const auto copied = [](const std::vector<std::string_view>& names) { return std::vector<std::string>(names.begin(), names.end()); };
                return PieceJoin { std::string(name), copied(tags.offers), copied(tags.needs), copied(tags.forbids), width };
            };
            r.joins = { join(section.start_join, section.tags[0], section.widths[0]), join(section.end_join, section.tags[1], section.widths[1]) };
            r.side = section.side;
            r.partner = section.partner.substr(0, section.partner.find(':'));
            r.group = section.group;
            r.platform_run = section.platform_run;
            r.drop_off = section.drop_off;
            r.water = (section.flags[0] & kPieceWater) != 0 || !section.water_splines[0].empty();
            for (const ovl::SpeedPush& p : section.pushes)
                r.pushes.push_back({ p.least, p.most, p.force, p.from, p.to, r.water && p.least <= 0 && p.most > 0 ? p.force : 0 });
            if ((r.flags & kPieceLift) != 0)
            {
                const auto join_left = splines.find(section.splines[2]);
                const auto join_right = splines.find(section.splines[3]);
                if (join_left != splines.end() && join_right != splines.end())
                    r.join_line = middle_line(join_left->second, join_right->second);
            }
            // Unturned, a model's +X faces the column before (faced_side), its +Z the row before.
            constexpr std::array<std::pair<u32, Side>, 4> kDrawnSides { { { 0x1, Side::kColBefore }, { 0x2, Side::kNextRow }, { 0x4, Side::kNextCol }, { 0x8, Side::kRowBefore } } };
            for (const ovl::TrackSquare& q : section.squares)
                if ((q.flags & (ovl::TrackSquare::kJoinsPool | ovl::TrackSquare::kOverPool)) != 0)
                    for (const auto& [bit, side] : kDrawnSides)
                        if ((q.flags & bit) != 0)
                            r.pool_sides.push_back({ .x = q.x, .z = q.z, .height = q.height, .side = side });
            // A slide climbed on foot: its walk up, named after the section, and its two rails as its two slides.
            if (const auto loop = splines.find(std::format("{}Loop", t.name)); loop != splines.end())
            {
                const auto points = [](const ovl::Spline& spline) {
                    constexpr i32 kPerPart = 4;
                    std::vector<glm::vec3> out;
                    for (usize i = 0; i + 1 < spline.nodes.size(); ++i)
                        for (i32 k = 0; k < kPerPart; ++k)
                            out.push_back(vec(ovl::spline_point(spline, i, static_cast<f32>(k) / kPerPart)));
                    if (!spline.nodes.empty())
                        out.push_back(vec(spline.nodes.back().position));
                    return out;
                };
                std::vector<std::vector<glm::vec3>> paths;
                for (i32 n = 1;; ++n)
                {
                    const auto path = splines.find(std::format("{}Path{:02}", t.name, n));
                    if (path == splines.end())
                        break;
                    paths.push_back(points(path->second));
                }
                const std::array slides { points(left->second), points(right->second) };
                r.climbs = slide_climbs(points(loop->second), paths, slides);
            }
            lines.push_back(*line);
        }
        return PieceCatalogue { std::move(overlay), shape_pieces(records, lines) };
    }

    f32 bank_degrees(const glm::vec3& direction, const glm::vec3& side)
    {
        const glm::vec3 forward = glm::normalize(direction);
        if (std::abs(forward.y) > kUpright)
            return 0;
        const glm::vec3 level = glm::normalize(glm::cross(glm::vec3 { 0, 1, 0 }, forward));
        const glm::vec3 up = glm::cross(forward, level);
        return math::atan2(glm::dot(side, up), glm::dot(side, level)) * kDegrees;
    }

    PieceEnd measure_end(const glm::vec3& direction, const glm::vec3& side)
    {
        const glm::vec3 forward = glm::normalize(direction);
        PieceEnd end;
        end.slope = slope_steps(math::atan2(forward.y, std::sqrt(forward.x * forward.x + forward.z * forward.z)) * kDegrees);
        end.bank = wrap_bank(static_cast<i32>(std::lround(bank_degrees(direction, side) / kStepDegrees)));
        // Straight up or down, the track's way across the ground is the rails' level line turned back.
        const glm::vec3 flat = std::abs(forward.y) > kUpright ? glm::cross(side, glm::vec3 { 0, 1, 0 }) : forward;
        end.heading = heading_steps(flat);
        return end;
    }

    PieceShape shape_piece(PieceRecord record, const PieceLine& line)
    {
        PieceShape shape;
        shape.line = line;
        const PieceLine& meets = record.join_line ? *record.join_line : line;
        // Where a lift car's rails run straight up the tower, so does the track joining there, though some rides'
        // joining rails end a little off true.
        const auto meeting_end = [&](glm::vec3 direction, const glm::vec3& side, const glm::vec3& rails) {
            constexpr f32 kClimbing = 0.5f; ///< Steeper than this, chosen by eye: past a gentle slope
            const auto steepness = [](const glm::vec3& d) { return glm::normalize(d).y; };
            if (record.join_line && std::abs(steepness(rails)) > kUpright && std::abs(steepness(direction)) > kClimbing)
                direction = { 0, direction.y > 0 ? 1 : -1, 0 };
            return measure_end(direction, side);
        };
        shape.entry = meeting_end(meets.start_direction, meets.start_side, line.start_direction);
        shape.exit = meeting_end(meets.end_direction, meets.end_side, line.end_direction);
        for (const auto& [flag, toggle] : { std::pair { kPieceChain, PieceToggle::Chain }, { kPieceEnclosed, PieceToggle::Enclosed }, { kPieceWater, PieceToggle::Water }, { kPieceBooster, PieceToggle::Booster } })
        {
            if ((record.flags & flag) != 0)
                shape.toggles |= toggle_bit(toggle);
        }
        if (record.joins[1].width != 0)
            shape.toggles |= toggle_bit(PieceToggle::Wide);
        const i32 turn = shape.exit.heading.eighths_from(shape.entry.heading);
        const u32 join = record.join_a;
        if ((record.flags & kPieceStation) != 0)
            shape.use = PieceUse::Station;
        // A lift tower's pieces go only one after the other, so they are picked by name.
        else if ((record.flags & kPieceLift) != 0)
            shape.use = PieceUse::Special;
        else if (join == 0 && turn == 0)
            shape.curve = 0;
        else if (join >= 1 && join <= kLastCurveJoin && turn != 0)
        {
            const i32 size = static_cast<i32>(join <= kCurveSizes ? join : join - kCurveSizes);
            shape.curve = turn < 0 ? -size : size;
        }
        else
            shape.use = PieceUse::Special;
        shape.record = std::move(record);
        return shape;
    }

    std::vector<PieceShape> shape_pieces(std::span<const PieceRecord> records, std::span<const PieceLine> lines, std::function_ref<bool(const PieceRecord&)> listed)
    {
        std::vector<PieceShape> pieces;
        for (usize i = 0; i < records.size() && i < lines.size(); ++i)
            pieces.push_back(shape_piece(records[i], lines[i]));
        const auto station = std::ranges::find_if(pieces, [&](const PieceShape& p) { return p.use == PieceUse::Station && listed(p.record); });
        const i32 level = station != pieces.end() ? station->entry.bank : 0;
        for (PieceShape& p : pieces)
        {
            p.entry.bank = wrap_bank(p.entry.bank - level);
            p.exit.bank = wrap_bank(p.exit.bank - level);
        }
        // One plain piece per shape: the plainest keeps it, the others are offered by name.
        using Key = std::tuple<i32, i32, i32, i32, i32, i32, i32, u8>;
        std::map<Key, usize> plainest;
        for (usize i = 0; i < pieces.size(); ++i)
        {
            const PieceShape& p = pieces[i];
            if (p.use != PieceUse::Plain || !listed(p.record))
                continue;
            const Key key { p.entry.slope, p.entry.bank, p.entry.heading.value, p.exit.slope, p.exit.bank, p.exit.heading.value, p.curve, p.toggles };
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
        return { -shape.exit.slope, wrap_bank(-shape.exit.bank), shape.exit.heading.reversed() };
    }

    PieceEnd laid_exit(const PieceShape& shape, bool reversed)
    {
        if (!reversed)
            return shape.exit;
        return { -shape.entry.slope, wrap_bank(-shape.entry.bank), shape.entry.heading.reversed() };
    }

    i32 laid_curve(const PieceShape& shape, bool reversed)
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

    TrackTip tip_of(const save::model::PlacedObject& object, const PieceShape& shape, bool reversed, const save::model::TerrainGrid& grid)
    {
        const glm::mat4 m = object::object_transform(object, shape.record.positioning, grid, shape.record.squares);
        TrackTip tip;
        tip.position = glm::vec3(m * glm::vec4(reversed ? meeting_line(shape).start : meeting_line(shape).end, 1));
        tip.end = laid_exit(shape, reversed);
        tip.end.heading = tip.end.heading.turned(ModelTurn::from(object.rotation));
        return tip;
    }

    TrackTip start_of(const save::model::PlacedObject& object, const PieceShape& shape, bool reversed, const save::model::TerrainGrid& grid)
    {
        TrackTip tip = tip_of(object, shape, !reversed, grid);
        tip.end = { -tip.end.slope, wrap_bank(-tip.end.bank), tip.end.heading.reversed() };
        return tip;
    }

    std::optional<PiecePlacement> place_after(const TrackTip& tip, const PieceShape& shape, bool reversed, const save::model::TerrainGrid& grid)
    {
        const PieceEnd entry = laid_entry(shape, reversed);
        if (!fits_after(tip.end, entry) || shape.record.positioning != object::Positioning::Tile || grid.tile_size_x <= 0 || grid.tile_size_z <= 0)
            return std::nullopt;
        // The turn that brings the piece's entry round to the tip: whole quarters, as both run along the grid or
        // both across it.
        const std::optional<ModelTurn> turn = Heading8::from(tip.end.heading.value - entry.heading.value).quarter_turns();
        if (!turn)
            return std::nullopt;
        const PieceLine& meets = meeting_line(shape);
        const glm::vec3 start = turned(reversed ? meets.end : meets.start, *turn);
        const bool across = (turn->value & 1) != 0;
        const auto span_x = static_cast<f32>(shape.record.squares[across ? 1 : 0]);
        const auto span_z = static_cast<f32>(shape.record.squares[across ? 0 : 1]);
        const f32 col = (tip.position.x - start.x - grid.world_x(0)) / grid.tile_size_x - span_x / 2;
        const f32 row = (tip.position.z - start.z - grid.world_z(0)) / grid.tile_size_z - span_z / 2;
        const f32 height = tip.position.y - start.y;
        const auto whole = [](f32 v) { return std::abs(v - std::round(v)) < kMeet; };
        if (!whole(col) || !whole(row) || !whole(height))
            return std::nullopt;
        PiecePlacement placed;
        save::model::PlacedObject& o = placed.object;
        o.symbol = shape.record.item;
        o.col = static_cast<i32>(std::lround(col));
        o.row = static_cast<i32>(std::lround(row));
        o.rotation = turn->value;
        o.base_height = static_cast<i32>(std::lround(height));
        o.force_absolute_height = true;
        // Checked against the transform the park draws it with, so what is placed is what shows.
        const glm::mat4 m = object::object_transform(o, shape.record.positioning, grid, shape.record.squares);
        const glm::vec3 met = glm::vec3(m * glm::vec4(reversed ? meets.end : meets.start, 1));
        if (glm::length(met - tip.position) > kMeet)
            return std::nullopt;
        placed.next = tip_of(o, shape, reversed, grid);
        return placed;
    }

    std::vector<LaidPiece> pieces_after(std::span<const PieceShape> pieces, const PieceEnd& tip, std::optional<LaidPiece> last)
    {
        const std::optional<const PieceShape&> before = last && last->index < pieces.size() ? core::maybe(&pieces[last->index]) : std::nullopt;
        std::vector<LaidPiece> fit;
        for (usize i = 0; i < pieces.size(); ++i)
        {
            for (const bool reversed : { false, true })
            {
                // A lift tower's car carries trains only up it.
                if (reversed && (pieces[i].record.flags & kPieceLift) != 0)
                    continue;
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
        for (usize i = 0; i < pieces.size(); ++i)
        {
            const u32 f = pieces[i].record.flags;
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
        return found != catalogue.pieces.end() ? core::maybe(&*found) : std::nullopt;
    }

} // namespace rct3::park::track
