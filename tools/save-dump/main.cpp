// SPDX-License-Identifier: GPL-3.0-or-later
//
// save-dump: print the container structure of a park or scenario save file.
//
//   save-dump --people PATH...   (people and animals model: counts and validation summary)
//   save-dump --trains PATH...   (saved trains and cars grouped by state number, with what each shows)
//   save-dump --paths PATH...    (raised paths and queues grouped by stored slope, with their pieces and heights)
//   save-dump --effect PATH...   (particle effects: emitters, particles, the pictures they use and the most alive at once)
//   save-dump [--classes] [--objects] [--values CLASS] [--object ID] [--check] PATH
//   save-dump [--classes] [--objects] [--values CLASS] [--object ID] [--check] [--rides] PATH
//
// Prints the section table, header words, the class table summary and per-class object counts.
// --values CLASS and --object ID print decoded attribute values of the matching objects. --check decodes
// every object and reports how many values were produced, left raw or failed to fit their layout, and how
// many references resolve to an object in the file.
// Exit status is 0 when the file parses and its checksum (if any) matches.
#include "particles/effect.hpp"
#include "particles/simulation.hpp"
#include "save/model/park.hpp"
#include "save/model/people.hpp"
#include "save/model/rides.hpp"
#include "save/save_file.hpp"
#include "save/value.hpp"
#include "simulation/park_rating.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <span>
#include <sstream>
#include <string>
#include <vector>

namespace
{

    using rct3::save::Value;
    using rct3::save::ValueKind;

    void print_value(const Value& v, int indent, const char* name)
    {
        std::printf("%*s", indent, "");
        if (name)
            std::printf("%s = ", name);
        switch (v.kind())
        {
            case ValueKind::Raw:
            {
                const auto raw = v.raw();
                std::printf("raw[%zu]", raw.size());
                for (std::size_t i = 0; i < raw.size() && i < 16; ++i)
                    std::printf(" %02x", static_cast<unsigned>(raw[i]));
                if (raw.size() > 16)
                    std::printf(" ...");
                std::printf("\n");
                return;
            }
            case ValueKind::Bool:
                std::printf("%s\n", v.as_bool() ? "true" : "false");
                return;
            case ValueKind::Integer:
                std::printf("%lld\n", static_cast<long long>(v.as_integer()));
                return;
            case ValueKind::Float:
                std::printf("%g\n", static_cast<double>(v.as_float()));
                return;
            case ValueKind::String:
                std::printf("\"%.*s\"\n", static_cast<int>(v.text().size()), v.text().data());
                return;
            case ValueKind::Reference:
                if (v.as_reference() == 0)
                    std::printf("null\n");
                else
                    std::printf("ref %llu\n", static_cast<unsigned long long>(v.as_reference()));
                return;
            case ValueKind::Vector3:
            case ValueKind::Orientation:
            case ValueKind::Matrix44:
            case ValueKind::FloatArray:
            {
                std::printf("%s[", rct3::save::to_string(v.kind()));
                const auto f = v.floats();
                for (std::size_t i = 0; i < f.size() && i < 24; ++i)
                    std::printf("%s%g", i ? " " : "", static_cast<double>(f[i]));
                if (f.size() > 24)
                    std::printf(" ... (%zu)", f.size());
                std::printf("]\n");
                return;
            }
            case ValueKind::List:
            {
                std::printf("list[%zu]\n", v.items().size());
                std::size_t shown = 0;
                for (const auto& item : v.items())
                {
                    if (++shown > 8)
                    {
                        std::printf("%*s...\n", indent + 2, "");
                        break;
                    }
                    print_value(item, indent + 2, nullptr);
                }
                return;
            }
            case ValueKind::Struct:
            {
                std::printf("{\n");
                for (std::size_t i = 0; i < v.items().size(); ++i)
                {
                    const auto n = std::string(v.names()[i]);
                    print_value(v.items()[i], indent + 2, n.c_str());
                }
                std::printf("%*s}\n", indent, "");
                return;
            }
        }
    }

    struct RefCount
    {
        std::size_t total = 0;
        std::size_t null = 0;
        std::size_t resolved = 0;
    };

    void count_refs(const Value& v, const rct3::save::ObjectIndex& index, RefCount& rc)
    {
        if (v.kind() == ValueKind::Reference)
        {
            ++rc.total;
            if (v.as_reference() == 0)
                ++rc.null;
            else if (index.find(v.as_reference()))
                ++rc.resolved;
        }
        for (const auto& item : v.items())
            count_refs(item, index, rc);
    }

} // namespace

// ---- world report (--world, --world-png) begin ---------------------------------------------------------------
#include "save/model/world.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <utility>

namespace
{

    // Minimal PNG writer: 8-bit grayscale, stored (uncompressed) deflate blocks.
    std::uint32_t png_crc(const unsigned char* d, std::size_t n, std::uint32_t crc = 0xffffffffu)
    {
        for (std::size_t i = 0; i < n; ++i)
        {
            crc ^= d[i];
            for (int k = 0; k < 8; ++k)
                crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
        return crc;
    }

    void png_chunk(std::vector<unsigned char>& out, const char* type, const std::vector<unsigned char>& data)
    {
        const auto put32 = [&](std::uint32_t v) {
            for (int s = 24; s >= 0; s -= 8)
                out.push_back(static_cast<unsigned char>(v >> s));
        };
        put32(static_cast<std::uint32_t>(data.size()));
        std::vector<unsigned char> body(type, type + 4);
        body.insert(body.end(), data.begin(), data.end());
        out.insert(out.end(), body.begin(), body.end());
        put32(png_crc(body.data(), body.size()) ^ 0xffffffffu);
    }

    bool write_gray_png(const char* path, int w, int h, const std::vector<unsigned char>& pixels)
    {
        std::vector<unsigned char> raw;
        raw.reserve(static_cast<std::size_t>(h) * (static_cast<std::size_t>(w) + 1));
        for (int y = 0; y < h; ++y)
        {
            raw.push_back(0); // filter: none
            raw.insert(raw.end(), pixels.begin() + static_cast<std::ptrdiff_t>(y) * w, pixels.begin() + static_cast<std::ptrdiff_t>(y + 1) * w);
        }
        std::vector<unsigned char> z { 0x78, 0x01 };
        for (std::size_t pos = 0; pos < raw.size() || pos == 0;)
        {
            const std::size_t n = std::min<std::size_t>(65535, raw.size() - pos);
            const bool last = pos + n >= raw.size();
            z.push_back(last ? 1 : 0);
            z.push_back(static_cast<unsigned char>(n & 0xff));
            z.push_back(static_cast<unsigned char>(n >> 8));
            z.push_back(static_cast<unsigned char>(~n & 0xff));
            z.push_back(static_cast<unsigned char>((~n >> 8) & 0xff));
            z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(pos), raw.begin() + static_cast<std::ptrdiff_t>(pos + n));
            pos += n;
            if (last)
                break;
        }
        std::uint32_t a = 1, b = 0;
        for (const unsigned char c : raw)
        {
            a = (a + c) % 65521u;
            b = (b + a) % 65521u;
        }
        const std::uint32_t adler = (b << 16) | a;
        for (int s = 24; s >= 0; s -= 8)
            z.push_back(static_cast<unsigned char>(adler >> s));

        std::vector<unsigned char> out { 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a };
        std::vector<unsigned char> ihdr;
        for (const int v : { w, h })
        {
            for (int s = 24; s >= 0; s -= 8)
                ihdr.push_back(static_cast<unsigned char>(v >> s));
        }
        ihdr.insert(ihdr.end(), { 8, 0, 0, 0, 0 });
        png_chunk(out, "IHDR", ihdr);
        png_chunk(out, "IDAT", z);
        png_chunk(out, "IEND", { });
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
        return static_cast<bool>(f);
    }

    // Prints the world model summary of one save. Returns false when loading fails or issues were found.
    bool world_report(const rct3::save::SaveFile& file, const char* path, const char* png_path)
    {
        using namespace rct3::save;
        const ObjectIndex index(file);
        const auto world = load_world(file, index);
        if (!world)
        {
            std::printf("world %s: load failed: %s\n", path, to_string(world.error()));
            return false;
        }
        const WorldModel& w = *world;
        const TerrainGrid& g = w.terrain;
        std::printf("world %s\n", path);
        if (g.empty())
        {
            std::printf("  terrain      none\n");
        }
        else
        {
            float lo = g.cells[0].corners[0], hi = lo;
            std::array<std::size_t, 6> own { };
            std::set<int> surfaces, cliffs;
            std::size_t flat = 0;
            std::map<int, std::size_t> flag_values;
            for (const auto& c : g.cells)
            {
                lo = std::min(lo, c.min_height());
                hi = std::max(hi, c.max_height());
                ++own[static_cast<std::size_t>(c.ownership())];
                surfaces.insert(c.surface);
                cliffs.insert(c.cliff);
                if (c.min_height() == c.max_height())
                    ++flat;
                ++flag_values[c.flags];
            }
            std::printf("  terrain      %dx%d cells, %s layout, trailing %zu, tile %g x %g, origin (%g, %g)\n", g.width, g.height, g.layout == TerrainCellLayout::Cell24 ? "24-byte" : "20-byte", g.trailing_bytes, static_cast<double>(g.tile_size_x), static_cast<double>(g.tile_size_z), static_cast<double>(g.origin_x), static_cast<double>(g.origin_z));
            std::printf("  heights      %g .. %g, flat tiles %zu, surfaces %zu distinct, cliff styles %zu distinct\n", static_cast<double>(lo), static_cast<double>(hi), flat, surfaces.size(), cliffs.size());
            std::printf("  ownership   ");
            for (std::size_t i = 0; i < own.size(); ++i)
            {
                std::printf(" %s=%zu", to_string(static_cast<Ownership>(i)), own[i]);
            }
            std::printf("\n  flag bytes  ");
            for (const auto& [v, n] : flag_values)
                std::printf(" %02x:%zu", static_cast<unsigned>(v), n);
            std::printf("\n");
        }
        std::printf("  water        %zu bodies, %zu triangles, map %dx%d", w.water.bodies.size(), w.water.triangle_count(), w.water.width, w.water.height);
        for (std::size_t i = 0; i < w.water.bodies.size() && i < 4; ++i)
            std::printf(" [level %g, %zu tris]", static_cast<double>(w.water.bodies[i].level), w.water.bodies[i].tris.size());
        std::printf("\n");
        if (!w.pool_water.empty())
        {
            // How the pools' surfaces stand against the ground under them, highest corner.
            std::map<int, std::size_t> above;
            for (const PoolWater& p : w.pool_water)
                if (w.terrain.contains(p.col, p.row))
                    ++above[p.surface - static_cast<int>(std::ceil(w.terrain.at(p.col, p.row).max_height()))];
            std::printf("  pools        %zu squares of water; surface minus ground:", w.pool_water.size());
            for (const auto& [d, n] : above)
                std::printf(" %d x%zu", d, n);
            std::printf("\n");
        }

        std::array<std::size_t, 4> kinds { };
        std::size_t with_underground = 0;
        std::map<std::pair<int, int>, std::size_t> per_tile;
        for (const auto& p : w.paths)
        {
            ++kinds[static_cast<std::size_t>(p.kind)];
            ++per_tile[{ p.col, p.row }];
            if (p.elevation && p.elevation->underground)
                ++with_underground;
        }
        std::size_t stacked = 0;
        for (const auto& [_, n] : per_tile)
            stacked += n > 1;
        std::printf(
            "  paths        %zu tiles (ground %zu, flying %zu, queue %zu, unknown %zu), %zu distinct positions, "
            "%zu positions with several, %zu queue lines, %zu underground\n",
            w.paths.size(), kinds[0], kinds[1], kinds[2], kinds[3], per_tile.size(), stacked, w.queue_lines.size(), with_underground);
        std::size_t free_standing = 0, hidden = 0, unset = 0, adjust = 0, named = 0;
        std::set<std::string> symbols;
        for (const auto& o : w.objects)
        {
            free_standing += o.free_standing();
            hidden += o.hidden;
            unset += !o.base_height;
            adjust += o.height_adjust.has_value();
            named += !o.symbol.empty();
            symbols.insert(o.symbol);
        }
        std::printf(
            "  scenery      %zu placed (free-standing %zu, hidden %zu, height unset %zu, with adjust %zu, "
            "symbol resolved %zu, %zu distinct symbols), skirt trees %zu, blocking blob %zu bytes\n",
            w.objects.size(), free_standing, hidden, unset, adjust, named, symbols.size(), w.skirt_trees.size(), w.blocking_scenery.size());
        if (!g.empty())
        {
            // Orientation check: paths and scenery sit on bought land, so the (col,row) reading should hit owned
            // tiles far more often than the transposed (row,col) reading.
            std::size_t direct = 0, swapped = 0, total = 0;
            int max_col = -1, max_row = -1;
            const auto owned = [&](int c, int r) { return g.contains(c, r) && g.at(c, r).ownership() == Ownership::Owned; };
            for (const auto& p : w.paths)
            {
                ++total;
                direct += owned(p.col, p.row);
                swapped += owned(p.row, p.col);
                max_col = std::max<int>(max_col, p.col);
                max_row = std::max<int>(max_row, p.row);
            }
            for (const auto& o : w.objects)
            {
                ++total;
                direct += owned(o.col, o.row);
                swapped += owned(o.row, o.col);
                max_col = std::max(max_col, o.col);
                max_row = std::max(max_row, o.row);
            }
            std::printf("  orientation  paths+scenery on owned tiles: (col,row) %zu, (row,col) %zu of %zu; max col %d, max row %d\n", direct, swapped, total, max_col, max_row);
        }
        if (w.weather)
        {
            const Weather& k = *w.weather;
            std::printf("  weather      %s (raw %d), forecast %s, climate %d, %g degC, wind %g\n", to_string(k.type), k.raw_type, to_string(k.forecast), k.climate, static_cast<double>(k.temperature), static_cast<double>(k.wind_speed));
        }
        if (w.sky_background)
            std::printf("  sky          background %d\n", *w.sky_background);
        if (w.time)
            std::printf("  time         day fraction %g, time %g\n", static_cast<double>(w.time->day_fraction), static_cast<double>(w.time->time));
        if (w.camera)
            std::printf("  camera       pos (%g, %g, %g) orientation (%g, %g, %g)\n", static_cast<double>(w.camera->position.x), static_cast<double>(w.camera->position.y), static_cast<double>(w.camera->position.z), static_cast<double>(w.camera->orientation.x), static_cast<double>(w.camera->orientation.y), static_cast<double>(w.camera->orientation.z));
        bool ok = true;
        for (const auto& i : w.issues)
        {
            std::printf("  ISSUE        %s x%zu (first object %llu)\n", to_string(i.kind), i.count, static_cast<unsigned long long>(i.first_object));
            ok = false;
        }
        if (png_path && !g.empty())
        {
            float lo = g.cells[0].corners[0], hi = lo;
            for (const auto& c : g.cells)
            {
                lo = std::min(lo, c.min_height());
                hi = std::max(hi, c.max_height());
            }
            const float span = hi > lo ? hi - lo : 1.0f;
            constexpr int kScale = 3;
            const int pw = g.width * kScale, ph = g.height * kScale;
            std::vector<unsigned char> px(static_cast<std::size_t>(pw) * static_cast<std::size_t>(ph));
            for (int y = 0; y < ph; ++y)
            {
                for (int x = 0; x < pw; ++x)
                {
                    const float v = (g.at(x / kScale, y / kScale).corners[0] - lo) / span;
                    px[static_cast<std::size_t>(y) * static_cast<std::size_t>(pw) + static_cast<std::size_t>(x)] = static_cast<unsigned char>(std::clamp(v, 0.0f, 1.0f) * 255.0f);
                }
            }
            if (write_gray_png(png_path, pw, ph, px))
                std::printf("  png          wrote %s (row 0 at the top)\n", png_path);
            else
                std::printf("  png          cannot write %s\n", png_path);
        }
        return ok;
    }

} // namespace
// ---- world report end ----------------------------------------------------------------------------------------

int main(int argc, char** argv)
{
    // `save-dump --effect FILE...`: each particle effect read as the game shows it, or why it could not be.
    if (std::any_of(argv + 1, argv + argc, [](const char* a) { return std::strcmp(a, "--effect") == 0; }))
    {
        int bad = 0;
        for (int i = 1; i < argc; ++i)
        {
            if (std::strncmp(argv[i], "--", 2) == 0)
                continue;
            std::ifstream in(argv[i], std::ios::binary);
            std::vector<char> chars((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            const auto effect = rct3::particles::read_effect(std::as_bytes(std::span(chars)));
            if (!effect)
            {
                std::printf("%s: %s\n", argv[i], rct3::particles::to_string(effect.error()));
                ++bad;
                continue;
            }
            std::string skins;
            for (const auto& p : effect->particles)
                skins += std::format(" {}", p.skin.empty() ? "-" : p.skin);
            // Ten seconds of it running round, for the most particles it keeps alive at once.
            rct3::particles::EffectRun run(*effect, rct3::random::stream(rct3::random::Seed { 1 }, rct3::random::RandomStream::Particles), true, 1u << 20);
            std::size_t peak = 0;
            for (int frame = 0; frame < 600; ++frame)
            {
                run.advance(1 / 60.0f);
                peak = std::max(peak, run.particles().count());
            }
            std::printf("%s: life %.2f, %zu roots, %zu emitters, %zu particles, peak %zu live:%s\n", argv[i], static_cast<double>(effect->lifetime), effect->roots.size(), effect->emitters.size(), effect->particles.size(), peak, skins.c_str());
        }
        return bad == 0 ? 0 : 1;
    }

    // BEGIN park: `save-dump --park [--root INSTALL_DIR] FILE...` prints the decoded park summary and checks.
    if (std::any_of(argv + 1, argv + argc, [](const char* a) { return std::strcmp(a, "--park") == 0; }))
    {
        const char* root = nullptr;
        int bad = 0;
        for (int i = 1; i < argc; ++i)
        {
            if (std::strcmp(argv[i], "--root") == 0 && i + 1 < argc)
                root = argv[i + 1];
        }
        for (int i = 1; i < argc; ++i)
        {
            if (std::strcmp(argv[i], "--park") == 0)
                continue;
            if (std::strcmp(argv[i], "--root") == 0)
            {
                ++i;
                continue;
            }
            std::ifstream in(argv[i], std::ios::binary);
            const std::vector<char> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            const auto bytes = std::as_bytes(std::span(raw));
            const auto file = rct3::save::read_save(bytes, { .verify_checksum = false });
            if (!file)
            {
                std::printf("%s: read error %s\n", argv[i], rct3::save::to_string(file.error()));
                ++bad;
                continue;
            }
            const rct3::save::ObjectIndex index(*file);
            const auto model = rct3::save::load_park(*file, index);
            if (!model)
            {
                std::printf("%s: park load error %s\n", argv[i], rct3::save::to_string(model.error()));
                ++bad;
                continue;
            }
            const auto& m = *model;
            const auto date = m.clock.date();
            std::printf("%s\n", argv[i]);
            std::printf("  park       rating %g guests %g flags %x open %d..%d\n", static_cast<double>(m.park.rating().value_or(-1.0f)), static_cast<double>(m.park.guests_in_park.latest().value_or(-1.0f)), m.park.flags.raw, m.park.opening_minutes, m.park.closing_minutes);
            std::printf("  clock      time %g year %lld month %d week %d\n", static_cast<double>(m.clock.time), static_cast<long long>(date.year), date.month, date.week);
            std::printf("  finance    cash %d loan %d/%d ledger periods %zu sum0 %lld\n", m.finance.cash, m.finance.loan, m.finance.max_loan, m.finance.ledger_periods_loaded, static_cast<long long>(m.finance.ledger[0].raw_sum()));
            std::printf(
                "  scenario   objectives %zu conditions %zu challenges %zu categories %zu messages %zu/%zu"
                " particles %zu\n",
                m.objectives.objectives.size(), m.objectives.conditions.size(), m.objectives.challenges.size(), m.research.categories.size(), m.messages.ordered.size(), m.messages.ring_size, m.particle_objects);
            std::printf("  content    requirements %zu without overlay %zu invented %zu\n", m.content.entries.size(), m.content.entries_without_overlay, m.content.invented_count());
            if (root)
            {
                const auto rep = rct3::save::check_content(m.content, root);
                std::printf(
                    "  install    resolved %zu (exact %zu, other case %zu, with unique %zu) missing %zu"
                    " (%zu distinct stems)\n",
                    rep.resolved, rep.exact_case, rep.resolved - rep.exact_case, rep.with_unique, rep.missing, rep.missing_stems.size());
            }
            for (const auto& v : rct3::save::validate(m))
            {
                std::printf("  VIOLATION  %s\n", v.c_str());
                ++bad;
            }
        }
        return bad ? 1 : 0;
    }
    // END park
    // `save-dump --rating FILE...`: our park rating model's score for each save, beside the rating it holds.
    if (std::any_of(argv + 1, argv + argc, [](const char* a) { return std::strcmp(a, "--rating") == 0; }))
    {
        using namespace rct3::save;
        double error_sum = 0;
        std::size_t compared = 0;
        for (int i = 1; i < argc; ++i)
        {
            if (std::strncmp(argv[i], "--", 2) == 0)
                continue;
            std::ifstream in(argv[i], std::ios::binary);
            const std::vector<char> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            const auto file = read_save(std::as_bytes(std::span(raw)), { .verify_checksum = false });
            if (!file)
                continue;
            const ObjectIndex index(*file);
            const auto park = load_park(*file, index);
            const auto people = load_people(*file, index);
            const auto ride_model = load_rides(*file, index);
            if (!park || !people || !ride_model || !park->park.rating())
                continue;
            auto input = rct3::simulation::rating_input(*people, *ride_model, park->clock.time);
            input.guests = static_cast<std::int32_t>(park->park.guests_in_park.latest().value_or(0.0f));
            input.harder = park->park.flags.has(ParkFlag::HarderRating);
            const auto p = rct3::simulation::rate_park(input);
            const float saved = *park->park.rating();
            error_sum += std::abs(static_cast<double>(p.total) - static_cast<double>(saved));
            ++compared;
            std::printf("%-44s saved %4.0f ours %4d  happy %3d rides %3d up %3d guests %3d litter %4d harder %4d  (%d guests, %zu rides)\n", std::string(argv[i]).substr(std::string(argv[i]).find_last_of('/') + 1).c_str(), static_cast<double>(saved), p.total, p.happiness, p.rides, p.reliability, p.guests, p.litter, p.harder, input.guests, input.rides.size());
        }
        if (compared > 0)
            std::printf("mean difference %.0f over %zu saves\n", error_sum / static_cast<double>(compared), compared);
        return 0;
    }
    // `save-dump --trains FILE...`: every saved train and car, grouped by the state number it holds, with what
    // the save shows about them in that state, to tell what each number means.
    if (std::any_of(argv + 1, argv + argc, [](const char* a) { return std::strcmp(a, "--trains") == 0; }))
    {
        using namespace rct3::save;
        struct Seen
        {
            std::size_t count = 0, still = 0, platform = 0, riders = 0, files = 0;
            double speed = 0, time = 0, accel = 0;
            std::map<std::string, std::size_t> pieces;
            std::map<int, std::size_t> others; ///< Car states for trains, train states for cars
            std::string last_file;
        };
        std::map<int, Seen> trains, cars;
        // The piece's shape without the ride's prefix, e.g. "Station" from "WoodenCoaster_Station".
        const auto shape = [](const std::string& symbol) {
            const auto at = symbol.find('_');
            std::string s = at == std::string::npos ? symbol : symbol.substr(at + 1);
            if (const auto colon = s.find(':'); colon != std::string::npos)
                s.resize(colon);
            return s;
        };
        const auto note = [](Seen& seen, const std::string& file, float speed, float time, float accel, bool platform, int riders, const std::string& piece, int other) {
            ++seen.count;
            seen.still += std::abs(speed) < 0.01f;
            seen.platform += platform;
            seen.riders += riders > 0;
            seen.speed += std::abs(speed);
            seen.time += time;
            seen.accel += accel;
            ++seen.pieces[piece];
            ++seen.others[other];
            if (seen.last_file != file)
            {
                ++seen.files;
                seen.last_file = file;
            }
        };
        for (int i = 1; i < argc; ++i)
        {
            if (std::strncmp(argv[i], "--", 2) == 0)
                continue;
            std::ifstream in(argv[i], std::ios::binary);
            const std::vector<char> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            const auto file = read_save(std::as_bytes(std::span(raw)), { .verify_checksum = false });
            if (!file)
                continue;
            const ObjectIndex index(*file);
            const auto model = load_rides(*file, index);
            if (!model)
                continue;
            for (const Ride& ride : model->rides)
            {
                std::map<ObjectId, const TrackPiece*> pieces;
                if (ride.track)
                    for (const TrackPiece& p : ride.track->pieces)
                        pieces[p.id] = &p;
                const auto piece = [&](ObjectId id) -> const TrackPiece* {
                    const auto it = pieces.find(id);
                    return it == pieces.end() ? nullptr : it->second;
                };
                for (const Train& t : ride.trains)
                {
                    int riders = 0;
                    for (const Car& c : t.cars)
                        riders += c.passengers;
                    const TrackPiece* front = piece(t.front_piece);
                    note(trains[static_cast<int>(t.state)], argv[i], t.speed, t.state_time, t.acceleration, front != nullptr && front->platform.has_value(), riders, front ? shape(front->symbol) : "-", t.cars.empty() ? -1 : static_cast<int>(t.cars.front().state));
                    for (const Car& c : t.cars)
                    {
                        const TrackPiece* at = piece(c.front_piece);
                        note(cars[static_cast<int>(c.state)], argv[i], c.speed, t.state_time, t.acceleration, at != nullptr && at->platform.has_value(), c.passengers, at ? shape(at->symbol) : "-", static_cast<int>(t.state));
                    }
                }
            }
        }
        const auto print = [](const char* what, const char* other, const std::map<int, Seen>& seen) {
            std::printf("%s states\n", what);
            for (const auto& [state, s] : seen)
            {
                const auto n = static_cast<double>(s.count);
                std::printf("  %3d  %5zu in %3zu files  still %3.0f%%  platform %3.0f%%  riders %3.0f%%  speed %5.1f  accel %6.2f  time %6.1f\n", state, s.count, s.files, 100 * static_cast<double>(s.still) / n, 100 * static_cast<double>(s.platform) / n, 100 * static_cast<double>(s.riders) / n, s.speed / n, s.accel / n, s.time / n);
                std::vector<std::pair<std::size_t, std::string>> top;
                for (const auto& [name, k] : s.pieces)
                    top.emplace_back(k, name);
                std::ranges::sort(top, std::greater { });
                std::printf("       pieces");
                for (std::size_t k = 0; k < top.size() && k < 4; ++k)
                    std::printf(" %s %.0f%%", top[k].second.c_str(), 100 * static_cast<double>(top[k].first) / n);
                std::printf("\n       %s", other);
                for (const auto& [o, k] : s.others)
                    std::printf(" %d:%zu", o, k);
                std::printf("\n");
            }
        };
        print("train", "front car states", trains);
        print("car", "train states", cars);
        return 0;
    }
    // `save-dump --paths FILE...`: the raised paths and queues of the saves, grouped by kind and stored slope,
    // with the pieces that draw them and how their heights stand against each other and the ground.
    if (std::any_of(argv + 1, argv + argc, [](const char* a) { return std::strcmp(a, "--paths") == 0; }))
    {
        using namespace rct3::save;
        struct Seen
        {
            std::size_t count = 0, underground = 0, no_base = 0;
            std::map<std::string, std::size_t> pieces;
            std::map<int, std::size_t> quantised_minus_4base, base_minus_ground, piece_minus_base, direction;
        };
        std::map<std::pair<std::string, int>, Seen> seen;
        for (int i = 1; i < argc; ++i)
        {
            if (std::strncmp(argv[i], "--", 2) == 0)
                continue;
            std::ifstream in(argv[i], std::ios::binary);
            const std::vector<char> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            const auto file = read_save(std::as_bytes(std::span(raw)), { .verify_checksum = false });
            if (!file)
                continue;
            const ObjectIndex index(*file);
            const auto world = load_world(*file, index);
            if (!world)
                continue;
            std::map<std::uint64_t, const PlacedObject*> objects;
            for (const PlacedObject& o : world->objects)
                objects[o.id] = &o;
            if (std::getenv("PATHS_ALL") != nullptr)
            {
                for (const QueueLine& q : world->queue_lines)
                    std::printf("line %s %llu %llu %d %llu %d\n", argv[i], static_cast<unsigned long long>(q.id), static_cast<unsigned long long>(q.head_ref), q.length, static_cast<unsigned long long>(q.ride_ref), q.station_index);
                if (const auto rides = load_rides(*file, index))
                {
                    for (const Ride& ride : rides->rides)
                    {
                        for (const Station& st : ride.stations)
                        {
                            if (st.entrance_spot)
                                std::printf("entrance %s %d %d %d %d %llu\n", argv[i], st.entrance_spot->col, st.entrance_spot->row, st.entrance_spot->height, st.entrance_spot->direction, static_cast<unsigned long long>(st.queue_line));
                        }
                    }
                }
            }
            for (const PathTile& p : world->paths)
            {
                if (std::getenv("PATHS_ALL") != nullptr && world->terrain.contains(p.col, p.row))
                {
                    const auto& c = world->terrain.at(p.col, p.row).corners;
                    const auto o = p.elevation ? objects.find(p.elevation->scenery_ref) : objects.end();
                    std::printf("tile %s %s%s %d %d %d %d %d %.2f %.2f %.2f %.2f %s %d\n", argv[i], to_string(p.kind), p.elevation && p.elevation->underground ? "-under" : "", p.elevation ? p.elevation->slope : -1, p.direction, p.col, p.row, p.elevation ? p.elevation->quantised_height : 0, c[0], c[1], c[2], c[3], o != objects.end() ? o->second->symbol.c_str() : "-", o != objects.end() ? o->second->rotation : -1);
                }
                if (std::getenv("PATHS_ALL") != nullptr && p.queue)
                {
                    const auto deck = p.elevation ? objects.find(p.elevation->scenery_ref) : objects.end();
                    if (deck != objects.end())
                        std::printf("qdeck %s|%d %d %d %d %d %s %d %d\n", argv[i], p.elevation->slope, p.direction, p.queue->start_direction, p.queue->end_direction, p.elevation->quantised_height, deck->second->symbol.c_str(), deck->second->rotation, deck->second->base_height.value_or(-999));
                }
                if (std::getenv("PATHS_ALL") != nullptr && p.queue)
                    std::printf(
                        "queue %s %llu %d %d %d %d %d %d %llu %s %s %s %d,%d,%d %d,%d,%d\n", argv[i], static_cast<unsigned long long>(p.id), p.col, p.row, p.elevation ? p.elevation->quantised_height : 0, p.elevation ? p.elevation->slope : -1, p.queue->start_direction, p.queue->end_direction, static_cast<unsigned long long>(p.queue->queue_line_ref), p.surface_name.c_str(), p.queue->fence_symbol.c_str(), p.queue->fence_overlay.c_str(), p.surface_colours ? (*p.surface_colours)[0] : -1,
                        p.surface_colours ? (*p.surface_colours)[1] : -1, p.surface_colours ? (*p.surface_colours)[2] : -1, p.queue->fence_colours[0], p.queue->fence_colours[1], p.queue->fence_colours[2]);
                if (!p.elevation)
                    continue;
                Seen& s = seen[{ to_string(p.kind), p.elevation->slope }];
                ++s.count;
                s.underground += p.elevation->underground;
                ++s.direction[p.direction];
                s.no_base += !p.elevation->base_height;
                const int quantised = p.elevation->quantised_height;
                const float ground = world->terrain.contains(p.col, p.row) ? world->terrain.at(p.col, p.row).min_height() : 0;
                ++s.quantised_minus_4base[quantised % 4];
                ++s.base_minus_ground[quantised - static_cast<int>(std::floor(ground * 4))];
                if (const auto o = objects.find(p.elevation->scenery_ref); o != objects.end())
                {
                    std::string symbol = o->second->symbol;
                    // Without the style's prefix: "Concrete_Slope" and "Tarmac_Slope" are the same piece.
                    if (const auto at = symbol.find('_'); at != std::string::npos)
                        symbol = symbol.substr(at + 1);
                    ++s.pieces[symbol];
                    if (std::getenv("PATHS_RAW") != nullptr && s.count <= 4)
                        std::printf(
                            "raw %s slope %d dir %d tile %d,%d corners %.2f %.2f %.2f %.2f quant %d | %s @%s base %d rot %d adjust %.2f absolute %d offset %d\n", to_string(p.kind), p.elevation->slope, p.direction, p.col, p.row, world->terrain.at(p.col, p.row).corners[0], world->terrain.at(p.col, p.row).corners[1], world->terrain.at(p.col, p.row).corners[2], world->terrain.at(p.col, p.row).corners[3], quantised, o->second->symbol.c_str(), o->second->overlay.c_str(), o->second->base_height.value_or(-999),
                            o->second->rotation, o->second->height_adjust.value_or(-9), o->second->force_absolute_height, o->second->height_offset);
                    if (o->second->base_height)
                        ++s.piece_minus_base[*o->second->base_height * 4 - quantised];
                }
            }
        }
        const auto top = [](const auto& counts, std::size_t n) {
            std::vector<std::pair<std::size_t, std::string>> sorted;
            for (const auto& [k, c] : counts)
            {
                std::ostringstream key;
                key << k;
                sorted.emplace_back(c, key.str());
            }
            std::ranges::sort(sorted, std::greater { });
            std::string out;
            for (std::size_t i = 0; i < sorted.size() && i < n; ++i)
                out += std::format(" {} x{}", sorted[i].second, sorted[i].first);
            return out;
        };
        for (const auto& [key, s] : seen)
        {
            std::printf("%s slope %d: %zu (underground %zu, no base %zu)\n", key.first.c_str(), key.second, s.count, s.underground, s.no_base);
            std::printf("  pieces            %s\n", top(s.pieces, 8).c_str());
            std::printf("  quantised %% 4     %s\n", top(s.quantised_minus_4base, 6).c_str());
            std::printf("  quantised - 4*grnd%s\n", top(s.base_minus_ground, 6).c_str());
            std::printf("  4*piece - quant   %s\n", top(s.piece_minus_base, 6).c_str());
            std::printf("  direction         %s\n", top(s.direction, 5).c_str());
        }
        return 0;
    }
    // --- people sweep: save-dump --people PATH... (begin) ---
    if (argc > 1 && std::any_of(argv + 1, argv + argc, [](const char* a) { return std::strcmp(a, "--people") == 0; }))
    {
        using namespace rct3::save;
        PeopleModel total;
        std::map<std::string, std::size_t> issue_counts;
        std::map<std::string, std::size_t> kinds;
        std::size_t files = 0, failed = 0, shown = 0;
        for (int i = 1; i < argc; ++i)
        {
            if (std::strncmp(argv[i], "--", 2) == 0)
                continue;
            std::ifstream in(argv[i], std::ios::binary);
            const std::vector<char> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            const auto file = read_save(std::as_bytes(std::span(raw)), { .verify_checksum = false });
            if (!file)
            {
                std::printf("%s: container error %s\n", argv[i], to_string(file.error()));
                ++failed;
                continue;
            }
            const ObjectIndex index(*file);
            auto model = load_people(*file, index);
            if (!model)
            {
                std::printf("%s: people load error %s\n", argv[i], to_string(model.error()));
                ++failed;
                continue;
            }
            ++files;
            const auto issues = validate(*model);
            for (const auto& is : issues)
                ++issue_counts[to_string(is.kind)];
            std::printf(
                "%-40s guests %5zu staff %3zu animals %5zu zoo %3zu groups %5zu holders %5zu carried %4zu shop %4zu"
                " litter %5zu issues %zu\n",
                std::string(argv[i]).substr(std::string(argv[i]).find_last_of('/') + 1).c_str(), model->guests.size(), model->staff.size(), model->animals.size(), model->zoo_animals.size(), model->groups.size(), model->holders.size(), model->carried_items.size(), model->shop_items.size(), model->litter.size(), issues.size());
            for (const auto& s : model->staff)
                ++kinds[to_string(s.kind)];
            for (const auto& a : model->animals)
                ++kinds[to_string(a.kind)];
            if (shown < 2 && !model->guests.empty())
            {
                const auto& g = model->guests.front();
                std::printf(
                    "  e.g. guest %llu %s at (%.1f %.1f %.1f) state %d/%d cash %d food %.2f happy %.2f group %llu\n", static_cast<unsigned long long>(g.person.id), to_string(g.person.type), static_cast<double>(g.person.position.x), static_cast<double>(g.person.position.y), static_cast<double>(g.person.position.z), static_cast<int>(g.person.state), g.person.sub_state, g.cash.current, static_cast<double>(g.needs.food), static_cast<double>(g.mood_now.happiness), static_cast<unsigned long long>(g.group.id));
                ++shown;
            }
            const auto add = [](auto& dst, auto& src) { std::move(src.begin(), src.end(), std::back_inserter(dst)); };
            add(total.guests, model->guests);
            add(total.staff, model->staff);
            add(total.animals, model->animals);
            add(total.zoo_animals, model->zoo_animals);
            add(total.groups, model->groups);
            add(total.holders, model->holders);
            add(total.carried_items, model->carried_items);
            add(total.shop_items, model->shop_items);
            add(total.litter, model->litter);
        }
        std::printf(
            "TOTAL files %zu failed %zu guests %zu staff %zu animals %zu zoo %zu groups %zu holders %zu carried %zu"
            " shop %zu litter %zu\n",
            files, failed, total.guests.size(), total.staff.size(), total.animals.size(), total.zoo_animals.size(), total.groups.size(), total.holders.size(), total.carried_items.size(), total.shop_items.size(), total.litter.size());
        for (const auto& [k, n] : kinds)
            std::printf("  kind %-16s %zu\n", k.c_str(), n);
        for (const auto& [k, n] : issue_counts)
            std::printf("  issue %-30s %zu\n", k.c_str(), n);
        return failed ? 1 : 0;
    }
    // --- people sweep (end) ---
    bool classes = false;
    bool objects = false;
    bool check = false;
    bool world = false;              // world report option
    const char* world_png = nullptr; // world report option
    const char* value_class = nullptr;
    const char* object_id = nullptr;
    const char* path = nullptr;
    bool rides = false; // --rides
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--classes") == 0)
            classes = true;
        else if (std::strcmp(argv[i], "--objects") == 0)
            objects = true;
        else if (std::strcmp(argv[i], "--check") == 0)
            check = true;
        else if (std::strcmp(argv[i], "--rides") == 0)
            rides = true;
        else if (std::strcmp(argv[i], "--values") == 0 && i + 1 < argc)
            value_class = argv[++i];
        else if (std::strcmp(argv[i], "--object") == 0 && i + 1 < argc)
            object_id = argv[++i];
        else if (std::strcmp(argv[i], "--world") == 0)
            world = true; // world report option
        else if (std::strcmp(argv[i], "--world-png") == 0 && i + 1 < argc)
            world_png = argv[++i]; // world report option
        else
            path = argv[i];
    }
    if (!path)
    {
        std::fprintf(stderr, "usage: save-dump [--classes] [--objects] [--values CLASS] [--object ID] [--check] [--world] [--world-png FILE] PATH\n");
        return 2;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        std::fprintf(stderr, "cannot open %s\n", path);
        return 2;
    }
    const std::vector<char> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto bytes = std::as_bytes(std::span(raw));

    const auto file = rct3::save::read_save(bytes, { .verify_checksum = false });
    if (!file)
    {
        std::printf("error: %s\n", rct3::save::to_string(file.error()));
        return 1;
    }

    if (world || world_png)
        return world_report(*file, path, world_png) ? 0 : 1; // world report option

    std::printf("size           %zu\n", raw.size());
    std::printf("header         %s\n", file->has_header ? "yes" : "no");
    if (file->has_header)
    {
        std::printf("version        %u\n", file->header.version());
        std::printf("header words  ");
        for (const auto w : file->header.words)
            std::printf(" %x", w);
        std::printf("\n");
    }
    std::printf("sections\n");
    for (const auto& s : file->sections)
    {
        std::printf("  %-13s offset %-9zu size %zu\n", rct3::save::to_string(s.kind), s.offset, s.size);
    }
    std::printf("classes        %zu\n", file->classes.size());
    std::printf("objects        %zu\n", file->objects.size());
    bool ok = true;
    if (file->has_checksum)
    {
        ok = file->stored_checksum == file->computed_checksum;
        std::printf("checksum       stored %08x computed %08x %s\n", file->stored_checksum, file->computed_checksum, ok ? "ok" : "MISMATCH");
    }

    std::vector<std::size_t> count(file->classes.size());
    std::vector<std::size_t> size(file->classes.size());
    for (const auto& o : file->objects)
    {
        ++count[o.class_index];
        size[o.class_index] += o.bytes.size();
    }
    if (classes)
    {
        std::printf("class table\n");
        std::printf("  %-4s %-34s %-6s %-8s %s\n", "idx", "name", "attrs", "objects", "bytes");
        for (std::size_t i = 0; i < file->classes.size(); ++i)
        {
            const auto& c = file->classes[i];
            std::printf("  %-4zu %-34.*s %-6zu %-8zu %zu\n", i, static_cast<int>(c.name.size()), c.name.data(), c.attributes.size(), count[i], size[i]);
        }
    }
    if (objects)
    {
        std::printf("objects\n");
        for (const auto& o : file->objects)
        {
            const auto& name = file->classes[o.class_index].name;
            std::printf("  id %-10llu class %-4u %-30.*s offset %-9zu size %zu\n", static_cast<unsigned long long>(o.id), o.class_index, static_cast<int>(name.size()), name.data(), o.offset, o.bytes.size());
        }
    }

    const auto print_object = [&](const rct3::save::Object& o) {
        const auto decoded = rct3::save::decode_object(*file, o);
        if (!decoded)
        {
            std::printf("object %llu: %s\n", static_cast<unsigned long long>(o.id), rct3::save::to_string(decoded.error()));
            return;
        }
        std::printf("object %llu class %.*s (%zu values, %zu opaque, %zu fallbacks)\n", static_cast<unsigned long long>(decoded->id), static_cast<int>(decoded->class_name.size()), decoded->class_name.data(), decoded->stats.values, decoded->stats.opaque, decoded->stats.fallbacks);
        print_value(decoded->fields, 2, nullptr);
    };
    if (value_class)
    {
        for (const auto& o : file->objects)
        {
            if (file->classes[o.class_index].name == value_class)
                print_object(o);
        }
    }
    if (object_id)
    {
        const rct3::save::ObjectIndex index(*file);
        if (const auto o = index.find(std::strtoull(object_id, nullptr, 10)))
            print_object(*o);
        else
            std::printf("no object with id %s\n", object_id);
    }
    if (check)
    {
        const rct3::save::ObjectIndex index(*file);
        rct3::save::DecodeStats total;
        RefCount refs;
        std::size_t failed = 0;
        for (const auto& o : file->objects)
        {
            const auto decoded = rct3::save::decode_object(*file, o);
            if (!decoded)
            {
                ++failed;
                continue;
            }
            total.values += decoded->stats.values;
            total.opaque += decoded->stats.opaque;
            total.fallbacks += decoded->stats.fallbacks;
            if (decoded->stats.fallbacks)
                std::printf("fallbacks in %.*s id %llu: %zu\n", static_cast<int>(decoded->class_name.size()), decoded->class_name.data(), static_cast<unsigned long long>(decoded->id), decoded->stats.fallbacks);
            count_refs(decoded->fields, index, refs);
        }
        std::printf("check          objects %zu failed %zu values %zu opaque %zu fallbacks %zu\n", file->objects.size(), failed, total.values, total.opaque, total.fallbacks);
        std::printf("references     total %zu null %zu resolved %zu unresolved %zu\n", refs.total, refs.null, refs.resolved, refs.total - refs.null - refs.resolved);
        if (failed || total.fallbacks)
            ok = false;
    }
    // --- rides: begin ---------------------------------------------------------------------------------
    // --rides: load the ride model and print one summary line plus validation counters.
    if (rides)
    {
        const rct3::save::ObjectIndex index(*file);
        const auto model = rct3::save::load_rides(*file, index);
        if (!model)
        {
            std::printf("rides          error: %s\n", rct3::save::to_string(model.error()));
            return 1;
        }
        std::size_t tracked = 0, flat = 0, special = 0, tracks = 0, circuits = 0, pieces = 0, stations = 0;
        std::size_t trains = 0, cars = 0, items = 0, extras = 0, segments = 0;
        for (const auto& r : model->rides)
        {
            tracked += r.kind == rct3::save::RideKind::Tracked;
            flat += r.kind == rct3::save::RideKind::Flat;
            special += r.kind == rct3::save::RideKind::Special;
            stations += r.stations.size();
            trains += r.trains.size();
            for (const auto& t : r.trains)
                cars += t.cars.size();
            if (r.track)
            {
                ++tracks;
                circuits += r.track->circuit;
                pieces += r.track->pieces.size();
                segments += r.track->segments.size();
            }
        }
        for (const auto& s : model->stalls)
        {
            items += s.items.size();
            extras += s.extras.size();
        }
        std::printf("rides          tracked %zu flat %zu special %zu stalls %zu\n", tracked, flat, special, model->stalls.size());
        std::printf("tracks         %zu circuit %zu open %zu segments %zu pieces %zu orphan_pieces %zu\n", tracks, circuits, tracks - circuits, segments, pieces, model->orphan_pieces);
        std::printf("trains         %zu cars %zu stations %zu shop_items %zu shop_extras %zu\n", trains, cars, stations, items, extras);
        const auto& g = model->diagnostics;
        std::printf(
            "invariants     track_back %zu unresolved_tracks %zu truncated %zu segment_pieces %zu piece_segment %zu "
            "platform_back %zu open_live_ends %zu train_back %zu train_index %zu car_back %zu car_index %zu "
            "car_count %zu unresolved_trains %zu unresolved_cars %zu dup_refs %zu queue_size %zu item_back %zu "
            "unresolved_items %zu\n",
            g.track_back_link_mismatch, g.unresolved_tracks, g.truncated_tracks, g.segment_piece_mismatch, g.piece_segment_mismatch, g.platform_back_link_mismatch, g.open_ends_with_live_link, g.train_back_link_mismatch, g.train_index_mismatch, g.car_back_link_mismatch, g.car_index_mismatch, g.car_count_mismatch, g.unresolved_trains, g.unresolved_cars, g.duplicate_refs, g.queue_size_mismatch, g.item_back_link_mismatch, g.unresolved_items);
        for (const auto& r : model->rides)
        {
            std::printf("  ride %-10llu kind %u state %d mode %d ticket %lld stations %zu trains %zu pieces %zu%s\n", static_cast<unsigned long long>(r.base.id), static_cast<unsigned>(r.kind), static_cast<int>(r.base.state), static_cast<int>(r.mode), static_cast<long long>(r.ticket_price), r.stations.size(), r.trains.size(), r.track ? r.track->pieces.size() : 0, r.track ? (r.track->circuit ? " circuit" : " open") : "");
        }
    }
    // --- rides: end -----------------------------------------------------------------------------------
    return ok ? 0 : 1;
}
