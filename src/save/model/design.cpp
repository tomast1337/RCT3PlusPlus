// SPDX-License-Identifier: GPL-3.0-or-later
#include "save/model/design.hpp"

#include "core/types.hpp"
#include "save/value.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace rct3::save::model
{

    namespace
    {

        // Class and attribute names of a design's header and picture, as the format names them.
        constexpr std::string_view kHeaderClass = "TrackedRideInstanceSaveHeader";
        constexpr std::string_view kPicture = "TrackedRidePreview";
        constexpr u32 kLargestPicture = 1024; ///< A bound on what a file may claim
        constexpr u8 kLevels = 63;            ///< Levels of a packed pixel
        constexpr u8 kFirstSkip = 64;         ///< Packed bytes from here skip clear pixels
        constexpr usize kLongestSkip = 255 - (kFirstSkip - 1);
        /// The header words every design of the newer generation carries.
        constexpr Header kDesignHeader { .words = { 2, 0, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0xFFFFFFFFu, 0xFFFFFFFFu, 0, 0 } };

        i32 integer(const Value& v, std::string_view name, i32 fallback = 0)
        {
            const auto f = v.field(name);
            if (!f || f->kind() != ValueKind::Integer)
                return fallback;
            return static_cast<i32>(std::clamp<i64>(f->as_integer(), std::numeric_limits<i32>::min(), std::numeric_limits<i32>::max()));
        }

        f32 number(const Value& v, std::string_view name)
        {
            const auto f = v.field(name);
            if (!f)
                return 0;
            const f32 x = f->kind() == ValueKind::Float ? f->as_float() : f->kind() == ValueKind::Integer ? static_cast<f32>(f->as_integer()) : 0.0f;
            return std::isfinite(x) ? x : 0.0f;
        }

        std::vector<u8> bytes(const Value& v, std::string_view name)
        {
            std::vector<u8> out;
            const auto f = v.field(name);
            if (!f || f->kind() != ValueKind::List)
                return out;
            out.reserve(f->items().size());
            for (const Value& item : f->items())
                out.push_back(item.kind() == ValueKind::Integer ? static_cast<u8>(item.as_integer()) : 0);
            return out;
        }

    } // namespace

    const char* to_string(DesignError e) noexcept
    {
        switch (e)
        {
            case DesignError::NotASave:
                return "not a save";
            case DesignError::NoHeader:
                return "no design header in the save";
            case DesignError::NoTrack:
                return "the ride has no track";
            case DesignError::NoAsset:
                return "a piece or door names no asset";
        }
        return "unknown";
    }

    std::vector<u8> design_picture(u32 width, u32 height, std::span<const u8> packed, std::span<const u8> unpacked)
    {
        const usize pixels = usize { width } * height;
        if (width == 0 || height == 0 || width > kLargestPicture || height > kLargestPicture)
            return { };
        std::vector<u8> rgba(pixels * 4, 0);
        const auto put = [&](usize at, u8 level) {
            rgba[at * 4] = rgba[at * 4 + 1] = rgba[at * 4 + 2] = level;
            rgba[at * 4 + 3] = 255;
        };
        if (packed.empty())
        {
            if (unpacked.size() != pixels)
                return { };
            for (usize i = 0; i < pixels; ++i)
                if (unpacked[i] != 0)
                    put(i, unpacked[i]);
            return rgba;
        }
        usize at = 0;
        for (usize i = 0; i < packed.size(); ++i)
        {
            const u8 b = packed[i];
            if (b == 0)
            {
                if (i + 1 < packed.size() && packed[i + 1] == 0)
                    break;
                ++i; // a lone zero takes the byte after it along
            }
            else if (b < kFirstSkip)
            {
                if (at < pixels)
                    put(at, static_cast<u8>(std::lround(static_cast<f32>(b) * 255.0f / kLevels)));
                ++at;
            }
            else
                at += b - (kFirstSkip - 1);
        }
        return rgba;
    }

    std::expected<DesignSummary, DesignError> read_design_summary(std::span<const std::byte> data)
    {
        const auto file = read_save(data, { .verify_checksum = false });
        if (!file)
            return std::unexpected(DesignError::NotASave);
        std::optional<u32> header_class;
        for (usize i = 0; i < file->classes.size() && !header_class; ++i)
            if (file->classes[i].name == kHeaderClass)
                header_class = static_cast<u32>(i);
        const auto object = header_class ? std::ranges::find(file->objects, *header_class, &Object::class_index) : file->objects.end();
        if (object == file->objects.end())
            return std::unexpected(DesignError::NoHeader);
        const auto header = decode_object(*file, *object);
        if (!header)
            return std::unexpected(DesignError::NoHeader);
        const Value& h = header->fields;
        DesignSummary s;
        if (const auto symbol = h.field("TrackedRideSymbolName"); symbol && symbol->kind() == ValueKind::String)
            s.ride_symbol = symbol->text();
        s.width = integer(h, "Cols");
        s.length = integer(h, "Rows");
        if (const auto cost = h.field("Cost"); cost && cost->kind() == ValueKind::Integer)
            s.cost = { cost->as_integer() };
        // The file keeps -1 for none.
        if (const i32 scenario = integer(h, "NeedScenarioComplete", -1); scenario >= 0)
            s.scenario = scenario;
        s.level = integer(h, "CompletionLevel");
        s.train = integer(h, "TrainSelection");
        if (const auto a = h.field("RideAssessmentResults"); a && a->kind() == ValueKind::Struct)
        {
            const auto valid = a->field("AssessmentIsValid");
            const auto ratings = a->field("AssessmentRatings");
            if (valid && valid->as_bool() && ratings && ratings->floats().size() == 3)
                s.assessment = { .valid = true, .excitement = ratings->floats()[0], .intensity = ratings->floats()[1], .nausea = ratings->floats()[2], .value = number(*a, "AssessmentValue") };
        }
        if (const auto r = h.field("TestResults"); r && r->kind() == ValueKind::Struct && (integer(*r, "Valid") & 1) != 0)
            s.results = TestResults {
                .max_speed = number(*r, "MaxSpeed"),
                .average_speed = number(*r, "AverageSpeed"),
                .ride_time = { number(*r, "RideTime") },
                .ride_length = { number(*r, "RideLength") },
                .max_positive_vertical = number(*r, "MaxPositiveVerticalG"),
                .max_negative_vertical = number(*r, "MaxNegativeVerticalG"),
                .max_lateral = number(*r, "MaxLateralG"),
                .air_time = { number(*r, "AirTime") },
                .drop_count = integer(*r, "NDrops"),
                .highest_drop = { number(*r, "HighestDropHeight") },
                .inversion_count = integer(*r, "NInversions"),
                .lift_hill_count = integer(*r, "NLiftHills"),
                .valid = true,
            };
        if (const auto ref = h.field(kPicture); ref && ref->kind() == ValueKind::Reference)
            if (const auto target = ObjectIndex(*file).find(ref->as_reference()))
                if (const auto picture = decode_object(*file, *target))
                {
                    const Value& p = picture->fields;
                    const auto width = static_cast<u32>(std::max(integer(p, "Width"), 0));
                    const auto height = static_cast<u32>(std::max(integer(p, "Height"), 0));
                    s.picture = design_picture(width, height, bytes(p, "Compact"), bytes(p, "Red"));
                    if (!s.picture.empty())
                    {
                        s.picture_width = width;
                        s.picture_height = height;
                    }
                }
        return s;
    }

    std::vector<u8> pack_design_picture(std::span<const u8> rgba)
    {
        std::vector<u8> out;
        usize clear = 0;
        for (usize at = 0; at + 3 < rgba.size(); at += 4)
        {
            if (rgba[at + 3] == 0)
            {
                ++clear;
                continue;
            }
            for (; clear > 0; clear -= std::min(clear, kLongestSkip))
                out.push_back(static_cast<u8>(std::min(clear, kLongestSkip) + (kFirstSkip - 1)));
            out.push_back(static_cast<u8>(std::clamp<i32>(static_cast<i32>(std::lround(rgba[at] * f32 { kLevels } / 255.0f)), 1, kLevels)));
        }
        out.insert(out.end(), { 0, 0 });
        return out;
    }

    namespace
    {

        /// One object's attributes and values, added together so its class's layout and its values cannot part.
        /// Names are the format's own, kept as literals.
        struct ObjectBuilder
        {
            std::string_view class_name;
            u64 id = 0;
            std::vector<Attribute> attributes { };
            std::vector<std::string_view> names { };
            std::vector<Value> values { };

            void add(Attribute a, Value v)
            {
                names.push_back(a.name);
                attributes.push_back(std::move(a));
                values.push_back(std::move(v));
            }
            void integer(std::string_view name, i64 v)
            {
                add({ .name = name, .type = "int32", .size = 4 }, Value::make_integer(v));
            }
            void number(std::string_view name, f32 v)
            {
                add({ .name = name, .type = "float32", .size = 4 }, Value::make_float(v));
            }
            void boolean(std::string_view name, bool v)
            {
                add({ .name = name, .type = "bool", .size = 1 }, Value::make_bool(v));
            }
            void reference(std::string_view name, u64 v)
            {
                add({ .name = name, .type = "managedobjectptr", .size = 8 }, Value::make_reference(v));
            }
            void text(std::string_view name, std::string v)
            {
                add({ .name = name, .type = "string" }, Value::make_string(std::move(v)));
            }
            void colours(std::string_view name, const RideColours& c)
            {
                std::vector<Value> v;
                for (const u8 x : c)
                    v.push_back(Value::make_integer(x));
                add({ .name = name, .type = "struct", .size = 12, .children = { int32("COL0"), int32("COL1"), int32("COL2") } }, Value::make_struct({ "COL0", "COL1", "COL2" }, std::move(v)));
            }
            /// Where a piece, platform or door stands.
            void placement(i32 col, i32 row, i32 height, i32 direction, i32 corner)
            {
                std::vector<Value> v;
                for (const i32 x : { corner, direction, height, col, row })
                    v.push_back(Value::make_integer(x));
                add({ .name = "SceneryItemDataField", .type = "struct", .size = 20, .children = { int32("CORNER"), int32("DIRECTION"), int32("HEIGHT"), int32("POSX"), int32("POSZ") } }, Value::make_struct({ "CORNER", "DIRECTION", "HEIGHT", "POSX", "POSZ" }, std::move(v)));
            }
            /// The ratings, as a ride and a design's header keep them.
            void assessment(const Assessment& a)
            {
                std::vector<Value> v;
                v.push_back(Value::make_bool(a.valid));
                v.push_back(Value::make_floats(ValueKind::Vector3, { a.excitement, a.intensity, a.nausea }));
                v.push_back(Value::make_float(a.reliability_base));
                v.push_back(Value::make_float(a.under_cover.value));
                v.push_back(Value::make_float(a.value));
                add({ .name = "RideAssessmentResults", .type = "struct", .size = 25, .children = { { .name = "AssessmentIsValid", .type = "bool", .size = 1 }, { .name = "AssessmentRatings", .type = "vector3", .size = 12 }, float32("AssessmentReliability"), float32("AssessmentUnderCover"), float32("AssessmentValue") } }, Value::make_struct({ "AssessmentIsValid", "AssessmentRatings", "AssessmentReliability", "AssessmentUnderCover", "AssessmentValue" }, std::move(v)));
            }

            static Attribute int32(std::string_view name)
            {
                return { .name = name, .type = "int32", .size = 4 };
            }
            static Attribute float32(std::string_view name)
            {
                return { .name = name, .type = "float32", .size = 4 };
            }
        };

        /// The file's overlay paths use the other slash.
        std::string overlay_path(std::string_view overlay)
        {
            std::string out(overlay);
            std::ranges::replace(out, '/', '\\');
            return out;
        }

    } // namespace

    std::expected<std::vector<std::byte>, DesignError> write_design(const Ride& ride, std::span<const PlacedObject> objects, const DesignSummary& summary)
    {
        if (!ride.track || ride.track->pieces.empty())
            return std::unexpected(DesignError::NoTrack);
        const Track& track = *ride.track;
        const std::vector<TrackPiece>& pieces = track.pieces;
        std::unordered_map<u64, const PlacedObject*> by_id;
        std::unordered_map<u64, const PlacedObject*> by_owner; // a platform's object names the platform as its owner
        for (const PlacedObject& o : objects)
        {
            by_id.try_emplace(o.id, &o);
            if (o.owner_ref != o.id)
                by_owner.try_emplace(o.owner_ref, &o);
        }

        // ---- 1. Ids: the header first, as the root; the rest in any order, as a reader remaps them ----
        u64 next_id = 1;
        const u64 header_id = next_id++, ride_id = next_id++, picture_id = next_id++, stations_id = next_id++, track_id = next_id++;
        std::vector<u64> segment_ids(std::max<usize>(track.segments.size(), 1));
        for (u64& s : segment_ids)
            s = next_id++;
        std::vector<u64> piece_ids(pieces.size());
        for (u64& p : piece_ids)
            p = next_id++;
        std::vector<ObjectBuilder> built;

        // An asset entry per symbol and overlay, shared by all that name it.
        std::map<std::pair<std::string, std::string>, u64> entries;
        const auto entry = [&](const PlacedObject& o) -> std::optional<u64> {
            if (o.symbol.empty() || o.overlay.empty())
                return std::nullopt;
            const auto [at, added] = entries.try_emplace({ o.symbol, overlay_path(o.overlay) }, next_id);
            if (added)
            {
                ObjectBuilder e { .class_name = "SIDDatabaseEntry", .id = next_id++ };
                e.text("OVERLAYFILENAME", at->first.second);
                e.text("SYMBOLNAME", at->first.first);
                built.push_back(std::move(e));
            }
            return at->second;
        };

        // ---- 2. The header ----
        {
            ObjectBuilder h { .class_name = kHeaderClass, .id = header_id };
            h.integer("Cols", summary.width);
            h.integer("CompletionLevel", summary.level);
            h.integer("Cost", summary.cost.value);
            h.integer("NeedScenarioComplete", summary.scenario.value_or(-1));
            h.assessment(summary.assessment);
            h.integer("Rows", summary.length);
            const TestResults results = summary.results.value_or(TestResults { });
            std::vector<Value> r;
            for (const f32 x : { results.air_time.value, results.average_speed, results.highest_drop.value, results.max_lateral, results.max_negative_vertical, results.max_positive_vertical, results.max_speed })
                r.push_back(Value::make_float(x));
            for (const i32 x : { results.drop_count, results.inversion_count, results.lift_hill_count })
                r.push_back(Value::make_integer(x));
            r.push_back(Value::make_float(results.ride_length.value));
            r.push_back(Value::make_float(results.ride_time.value));
            r.push_back(Value::make_integer(summary.results ? 1 : 0));
            using B = ObjectBuilder;
            h.add(
                { .name = "TestResults", .type = "struct", .size = 52, .children = { B::float32("AirTime"), B::float32("AverageSpeed"), B::float32("HighestDropHeight"), B::float32("MaxLateralG"), B::float32("MaxNegativeVerticalG"), B::float32("MaxPositiveVerticalG"), B::float32("MaxSpeed"), B::int32("NDrops"), B::int32("NInversions"), B::int32("NLiftHills"), B::float32("RideLength"), B::float32("RideTime"), B::int32("Valid") } },
                Value::make_struct({ "AirTime", "AverageSpeed", "HighestDropHeight", "MaxLateralG", "MaxNegativeVerticalG", "MaxPositiveVerticalG", "MaxSpeed", "NDrops", "NInversions", "NLiftHills", "RideLength", "RideTime", "Valid" }, std::move(r)));
            h.reference("TrackedRideInstance", ride_id);
            h.reference(kPicture, picture_id);
            h.text("TrackedRideSymbolName", summary.ride_symbol);
            h.integer("TrainSelection", summary.train);
            built.push_back(std::move(h));
        }

        // ---- 3. The ride, as it runs ----
        {
            ObjectBuilder r { .class_name = "TrackedRideInstance", .id = ride_id };
            const OperatingOptions& o = ride.options;
            r.text("Name", ride.base.name);
            r.text("TrackedRideSymbolName", ride.base.symbol);
            r.text("TrackedRideOverlayName", overlay_path(ride.base.overlay_path));
            r.integer("OpMode", std::to_underlying(ride.mode));
            r.boolean("BlockSectionedMode", o.block_sectioned);
            r.boolean("SyncTrains", o.sync_trains);
            r.boolean("LeaveIfAnotherTrainEntersStation", o.leave_if_other_train_enters_station);
            r.integer("MinWaitTime", o.min_wait.value_or(-1));
            r.integer("MaxWaitTime", o.max_wait.value_or(-1));
            r.integer("WaitForLoad", o.wait_for_load);
            r.number("LaunchSpeed", o.launch_speed.value);
            r.number("LiftChainSpeed", o.lift_speed.value);
            r.number("BrakeSpeed", o.brake_speed.value);
            r.number("BlockBrakeOffSpeed", o.block_brake_off_speed.value);
            r.number("FixedSpeed", o.fixed_speed.value_or(units::Pace { -1 }).value);
            r.integer("NumCircuits", o.circuits);
            r.colours("CarFlexiColours", ride.car_colours);
            r.colours("FenceFlexiColours", ride.fence_colours);
            r.colours("QueueFlexiColours", ride.queue_colours);
            r.text("PlatformStyle", ride.platform_style);
            r.text("PlatformType", ride.platform_type);
            r.assessment(ride.assessment);
            if (ride.train_count)
                r.integer("NTrains", *ride.train_count);
            if (ride.cars_per_train)
                r.integer("NCarsPerTrain", *ride.cars_per_train);
            r.integer("TrainSelection", ride.train_selection);
            r.boolean("Prototype", true);
            r.reference("Track", track_id);
            r.reference("RideStationsObj", stations_id);
            built.push_back(std::move(r));
        }

        // ---- 4. The picture ----
        {
            ObjectBuilder p { .class_name = kPicture, .id = picture_id };
            std::vector<Value> packed;
            for (const u8 b : pack_design_picture(summary.picture))
                packed.push_back(Value::make_integer(b));
            p.add({ .name = "Compact", .type = "array", .children = { { .name = "Compact", .type = "uint8", .size = 1 } } }, Value::make_list(std::move(packed)));
            p.integer("Height", summary.picture_height);
            p.integer("Width", summary.picture_width);
            built.push_back(std::move(p));
        }

        // ---- 5. The stations and their doors ----
        {
            ObjectBuilder s { .class_name = "RideStations", .id = stations_id };
            std::vector<Value> list;
            for (const Station& station : ride.stations)
            {
                std::array<u64, 2> doors { };
                for (usize k = 0; k < doors.size(); ++k)
                {
                    const u64 placed = k == 0 ? station.entrance : station.exit;
                    if (placed == 0)
                        continue;
                    const auto door = by_id.find(placed);
                    const auto asset = door != by_id.end() ? entry(*door->second) : std::nullopt;
                    if (!asset)
                        return std::unexpected(DesignError::NoAsset);
                    const PlacedObject& d = *door->second;
                    ObjectBuilder placement { .class_name = "SceneryItemPlacementSingle", .id = next_id++ };
                    placement.colours("FlexiColourField", { static_cast<u8>(d.colours[0]), static_cast<u8>(d.colours[1]), static_cast<u8>(d.colours[2]) });
                    placement.reference("Owner", ride_id);
                    placement.reference("SIDDatabaseEntry", *asset);
                    placement.placement(d.col, d.row, d.base_height.value_or(0), d.rotation, d.quadrant);
                    doors[k] = placement.id;
                    built.push_back(std::move(placement));
                }
                std::vector<Value> v;
                v.push_back(Value::make_reference(doors[0]));
                v.push_back(Value::make_float(0));
                v.push_back(Value::make_reference(doors[1]));
                v.push_back(Value::make_float(0));
                v.push_back(Value::make_list({ }));
                v.push_back(Value::make_reference(0));
                v.push_back(Value::make_float(0));
                list.push_back(Value::make_struct({ "Entrance", "EntranceTimeRemaining", "Exit", "ExitTimeRemaining", "Queue", "QueueLine", "m_queueTimeSecs" }, std::move(v)));
            }
            using B = ObjectBuilder;
            s.add({ .name = "RideStations", .type = "array", .children = { { .name = "Entrance", .type = "managedobjectptr", .size = 8 }, B::float32("EntranceTimeRemaining"), { .name = "Exit", .type = "managedobjectptr", .size = 8 }, B::float32("ExitTimeRemaining"), { .name = "Queue", .type = "list", .children = { { .name = "Guest", .type = "managedobjectptr", .size = 8 } } }, { .name = "QueueLine", .type = "reference", .size = 8 }, B::float32("m_queueTimeSecs") } }, Value::make_list(std::move(list)));
            built.push_back(std::move(s));
        }

        // ---- 6. The track, its stretches and its pieces ----
        // A track saved before it had stretches is one stretch of all its pieces.
        std::vector<TrackSegment> segments = track.segments;
        if (segments.empty())
            segments.push_back({ .first_piece = 0, .piece_count = pieces.size(), .closed = track.circuit });
        {
            ObjectBuilder t { .class_name = "Track", .id = track_id };
            t.integer("Direction", track.facing);
            t.reference("FirstSegment", segment_ids.front());
            t.reference("LastSegment", segment_ids.back());
            t.boolean("Prototype", true);
            t.colours("TrackFlexiColours", track.colours);
            t.reference("TrackedRideInstance", ride_id);
            built.push_back(std::move(t));
        }
        for (usize k = 0; k < segments.size(); ++k)
        {
            const TrackSegment& stretch = segments[k];
            if (stretch.piece_count == 0 || stretch.first_piece + stretch.piece_count > pieces.size())
                return std::unexpected(DesignError::NoTrack);
            ObjectBuilder s { .class_name = "TrackSegment", .id = segment_ids[k] };
            s.integer("Direction", track.facing);
            s.reference("FirstPiece", piece_ids[stretch.first_piece]);
            s.reference("LastPiece", piece_ids[stretch.first_piece + stretch.piece_count - 1]);
            s.reference("NextSegment", k + 1 < segments.size() ? segment_ids[k + 1] : 0);
            s.reference("PrevSegment", k > 0 ? segment_ids[k - 1] : 0);
            s.boolean("Prototype", true);
            s.reference("Track", track_id);
            built.push_back(std::move(s));

            for (usize i = stretch.first_piece; i < stretch.first_piece + stretch.piece_count; ++i)
            {
                const TrackPiece& piece = pieces[i];
                const auto object = by_id.find(piece.scenery);
                const auto asset = object != by_id.end() ? entry(*object->second) : std::nullopt;
                if (!asset)
                    return std::unexpected(DesignError::NoAsset);
                // Round its own stretch when that closes on itself, else on into the next and, on a circuit,
                // from the last back to the first.
                const usize first = stretch.first_piece, last = stretch.first_piece + stretch.piece_count - 1;
                const bool round = stretch.closed || (track.circuit && segments.size() == 1);
                u64 next = 0, prev = 0;
                if (i < last)
                    next = piece_ids[i + 1];
                else if (round)
                    next = piece_ids[first];
                else if (k + 1 < segments.size())
                    next = piece_ids[segments[k + 1].first_piece];
                else if (track.circuit)
                    next = piece_ids[segments.front().first_piece];
                if (i > first)
                    prev = piece_ids[i - 1];
                else if (round)
                    prev = piece_ids[last];
                else if (k > 0)
                    prev = piece_ids[segments[k - 1].first_piece + segments[k - 1].piece_count - 1];
                else if (track.circuit)
                    prev = piece_ids[segments.back().first_piece + segments.back().piece_count - 1];

                ObjectBuilder p { .class_name = "TrackPiece", .id = piece_ids[i] };
                p.text("SYMBOLNAME", piece.symbol);
                p.reference("SIDDatabaseEntry", *asset);
                p.placement(piece.tile_x, piece.tile_z, piece.height, piece.direction, piece.corner);
                p.reference("Next", next);
                p.reference("Prev", prev);
                p.reference("Owner", segment_ids[k]);
                p.reference("Segment", segment_ids[k]);
                p.boolean("Reversed", piece.reversed);
                p.number("StartDistance", piece.start_distance.value);
                p.colours("FlexiColourField", piece.colours);
                p.integer("BrakeSpeed", piece.brake_speed.value_or(-1));
                p.integer("HoldingTime", piece.holding_time);
                p.integer("Speed", piece.speed_class.value_or(-1));
                {
                    std::vector<Value> angles;
                    for (const f32 a : piece.arm_angles.value_or(std::array<f32, 4> { }))
                        angles.push_back(Value::make_float(a));
                    std::vector<Value> arm;
                    arm.push_back(Value::make_list(std::move(angles)));
                    arm.push_back(Value::make_bool(piece.arm_angles.has_value()));
                    p.add({ .name = "RobotArmAngles", .type = "struct", .children = { { .name = "Angles", .type = "array", .children = { ObjectBuilder::float32("Angle") } }, { .name = "Set", .type = "bool", .size = 1 } } }, Value::make_struct({ "Angles", "Set" }, std::move(arm)));
                }
                // A station's platform stands with it, as the object the platform owns shows it.
                const auto shown = piece.platform ? by_owner.find(piece.platform->id) : by_owner.end();
                const auto platform_asset = shown != by_owner.end() ? entry(*shown->second) : std::nullopt;
                if (platform_asset)
                {
                    const TrackPlatform& at = *piece.platform;
                    ObjectBuilder f { .class_name = "TrackPlatformPiece", .id = next_id++ };
                    f.integer("Flags", at.flags);
                    f.colours("FlexiColourField", at.colours);
                    f.reference("Owner", segment_ids[k]);
                    f.reference("SIDDatabaseEntry", *platform_asset);
                    f.placement(at.tile_x, at.tile_z, at.height, at.direction, at.corner);
                    f.reference("TrackPiece", piece_ids[i]);
                    p.reference("PlatformPiece", f.id);
                    built.push_back(std::move(f));
                }
                else
                    p.reference("PlatformPiece", 0);
                built.push_back(std::move(p));
            }
        }

        // ---- 7. The container: one class per kind of object, its layout that of its first, which every other
        // of its kind shares as they are built alike ----
        std::vector<Class> classes;
        std::vector<u32> class_of(built.size());
        for (usize i = 0; i < built.size(); ++i)
        {
            const auto found = std::ranges::find(classes, built[i].class_name, &Class::name);
            if (found == classes.end())
                classes.push_back({ .name = built[i].class_name, .attributes = built[i].attributes });
            class_of[i] = static_cast<u32>(std::ranges::find(classes, built[i].class_name, &Class::name) - classes.begin());
        }
        std::vector<std::vector<std::byte>> payloads;
        std::vector<ObjectRecord> records;
        payloads.reserve(built.size());
        for (usize i = 0; i < built.size(); ++i)
        {
            ObjectBuilder& b = built[i];
            auto payload = encode_object(classes[class_of[i]], Value::make_struct(std::move(b.names), std::move(b.values)));
            if (!payload)
                return std::unexpected(DesignError::NotASave);
            payloads.push_back(std::move(*payload));
            records.push_back({ .class_index = class_of[i], .id = b.id, .payload = payloads.back() });
        }
        auto file = write_save(kDesignHeader, classes, records);
        if (!file)
            return std::unexpected(DesignError::NotASave);
        return std::move(*file);
    }

} // namespace rct3::save::model
