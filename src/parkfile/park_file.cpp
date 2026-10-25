// SPDX-License-Identifier: GPL-3.0-or-later
#include "parkfile/park_file.hpp"

#include "parkfile/binary.hpp"
#include "simulation/components.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <format>
#include <span>
#include <string_view>
#include <tuple>
#include <utility>

namespace rct3::parkfile
{

    namespace
    {
        using simulation::SimWorld;
        using namespace detail;

        // ----- components -----

        // How each component is stored: its chunk tag and version, the smallest record it can have (to check
        // counts before allocating), and its fields in order.
        template<typename T> struct Stored;

        template<> struct Stored<simulation::Visitor>
        {
            static constexpr std::string_view kTag = "VSTR";
            static constexpr std::uint32_t kVersion = 2; ///< 2 adds `avatar`
            static constexpr std::size_t kMinRecord = 4 + 1 + 1 + 4 + 8;
            static void write(Writer& w, const simulation::Visitor& v)
            {
                w.number(std::to_underlying(v.age));
                w.number(std::uint8_t { v.female });
                w.text(v.name);
                w.number(v.arrived);
                w.number(std::uint8_t { v.avatar });
            }
            static std::expected<simulation::Visitor, std::string> read(Reader& r, std::uint32_t version)
            {
                simulation::Visitor v;
                const auto age = r.number<std::uint8_t>();
                if (age > std::to_underlying(simulation::AgeGroup::Adult))
                    return std::unexpected(std::format("unknown age group {}", age));
                v.age = static_cast<simulation::AgeGroup>(age);
                v.female = r.number<std::uint8_t>() != 0;
                v.name = r.text();
                v.arrived = r.number<std::uint64_t>();
                v.avatar = version >= 2 && r.number<std::uint8_t>() != 0;
                return v;
            }
        };

        template<> struct Stored<simulation::Pose>
        {
            static constexpr std::string_view kTag = "POSE";
            static constexpr std::uint32_t kVersion = 1;
            static constexpr std::size_t kMinRecord = 4 + 16;
            static void write(Writer& w, const simulation::Pose& l)
            {
                w.vec3(l.position);
                w.number(l.heading);
            }
            static std::expected<simulation::Pose, std::string> read(Reader& r)
            {
                const glm::vec3 position = r.vec3();
                return simulation::Pose { position, r.number<float>() };
            }
        };

        template<> struct Stored<simulation::Needs>
        {
            static constexpr std::string_view kTag = "NEED";
            static constexpr std::uint32_t kVersion = 1;
            static constexpr std::size_t kMinRecord = 4 + 12;
            static void write(Writer& w, const simulation::Needs& n)
            {
                w.number(n.hunger.value);
                w.number(n.thirst.value);
                w.number(n.bladder.value);
            }
            static std::expected<simulation::Needs, std::string> read(Reader& r)
            {
                simulation::Needs n;
                n.hunger.value = r.number<float>();
                n.thirst.value = r.number<float>();
                n.bladder.value = r.number<float>();
                return n;
            }
        };

        template<> struct Stored<simulation::Mood>
        {
            static constexpr std::string_view kTag = "MOOD";
            static constexpr std::uint32_t kVersion = 1;
            static constexpr std::size_t kMinRecord = 4 + 12;
            static void write(Writer& w, const simulation::Mood& m)
            {
                w.number(m.happiness.value);
                w.number(m.energy.value);
                w.number(m.nausea.value);
            }
            static std::expected<simulation::Mood, std::string> read(Reader& r)
            {
                simulation::Mood m;
                m.happiness.value = r.number<float>();
                m.energy.value = r.number<float>();
                m.nausea.value = r.number<float>();
                return m;
            }
        };

        template<> struct Stored<simulation::Wallet>
        {
            static constexpr std::string_view kTag = "WALT";
            static constexpr std::uint32_t kVersion = 2;
            static constexpr std::size_t kMinRecord = 4 + 8;
            static constexpr auto kSums = std::array { &simulation::Wallet::cash, &simulation::Wallet::bank, &simulation::Wallet::withdrawn, &simulation::Wallet::spent, &simulation::Wallet::spent_entry, &simulation::Wallet::spent_rides, &simulation::Wallet::spent_food, &simulation::Wallet::spent_drink, &simulation::Wallet::spent_souvenirs };
            static void write(Writer& w, const simulation::Wallet& m)
            {
                for (const auto sum : kSums)
                    w.number((m.*sum).value);
            }
            static std::expected<simulation::Wallet, std::string> read(Reader& r, std::uint32_t version)
            {
                simulation::Wallet m;
                for (const auto sum : version >= 2 ? std::span(kSums) : std::span(kSums).first(1))
                    (m.*sum).value = r.number<std::int64_t>();
                return m;
            }
        };

        template<> struct Stored<simulation::Departure>
        {
            static constexpr std::string_view kTag = "DPRT";
            static constexpr std::uint32_t kVersion = 1;
            static constexpr std::size_t kMinRecord = 4 + 1 + 8;
            static void write(Writer& w, const simulation::Departure& d)
            {
                w.number(std::to_underlying(d.wait_for));
                w.number(d.min_wait);
                w.number(d.max_wait);
            }
            static std::expected<simulation::Departure, std::string> read(Reader& r)
            {
                const auto wait_for = r.number<std::uint8_t>();
                if (wait_for > std::to_underlying(simulation::LoadWait::Any))
                    return std::unexpected(std::format("unknown load to wait for {}", wait_for));
                simulation::Departure d { .wait_for = static_cast<simulation::LoadWait>(wait_for) };
                d.min_wait = r.number<std::int32_t>();
                d.max_wait = r.number<std::int32_t>();
                return d;
            }
        };

        template<> struct Stored<simulation::Upkeep>
        {
            static constexpr std::string_view kTag = "UPKP";
            static constexpr std::uint32_t kVersion = 1;
            static constexpr std::size_t kMinRecord = 4 + 4 + 1 + 4 + 4 + 4 + 4 + 4 + 8 + 1 + 4;
            static void write(Writer& w, const simulation::Upkeep& u)
            {
                w.number(u.reliability.value);
                w.number(std::uint8_t { u.broken });
                w.number(std::to_underlying(u.kind));
                w.number(std::to_underlying(u.last));
                w.number(u.since_inspection.value);
                w.number(u.inspect_every.value);
                w.number(u.breakdowns);
                for (const std::uint8_t d : u.downtime)
                    w.number(d);
                w.number(u.samples);
                w.number(u.sample_clock.value);
            }
            static std::expected<simulation::Upkeep, std::string> read(Reader& r)
            {
                simulation::Upkeep u;
                u.reliability.value = r.number<float>();
                u.broken = r.number<std::uint8_t>() != 0;
                const auto kind = r.number<std::int32_t>();
                const auto last = r.number<std::int32_t>();
                constexpr auto kLast = std::to_underlying(simulation::Breakdown::ControlFailure);
                if (kind < 0 || kind > kLast || last < 0 || last > kLast)
                    return std::unexpected(std::format("unknown breakdown {} or {}", kind, last));
                u.kind = static_cast<simulation::Breakdown>(kind);
                u.last = static_cast<simulation::Breakdown>(last);
                u.since_inspection.value = r.number<float>();
                u.inspect_every.value = r.number<float>();
                u.breakdowns = r.number<std::uint32_t>();
                for (std::uint8_t& d : u.downtime)
                    d = r.number<std::uint8_t>();
                u.samples = r.number<std::uint8_t>();
                u.sample_clock.value = r.number<float>();
                return u;
            }
        };

        template<> struct Stored<simulation::Patrol>
        {
            static constexpr std::string_view kTag = "PTRL";
            static constexpr std::uint32_t kVersion = 1;
            static constexpr std::size_t kMinRecord = 4 + 4 + 4;
            static void write(Writer& w, const simulation::Patrol& p)
            {
                w.number(p.zone);
                w.number(static_cast<std::uint32_t>(p.tiles.size()));
                for (const glm::ivec2 t : p.tiles)
                {
                    w.number(std::int32_t { t.x });
                    w.number(std::int32_t { t.y });
                }
            }
            static std::expected<simulation::Patrol, std::string> read(Reader& r)
            {
                simulation::Patrol p;
                p.zone = r.number<std::uint32_t>();
                const auto count = r.number<std::uint32_t>();
                if (count > r.left() / 8)
                    return std::unexpected(std::format("{} tiles cannot fit in {} bytes", count, r.left()));
                p.tiles.reserve(count);
                for (std::uint32_t i = 0; i < count; ++i)
                {
                    const auto col = r.number<std::int32_t>();
                    p.tiles.emplace_back(col, r.number<std::int32_t>());
                }
                return p;
            }
        };

        using Components = std::tuple<simulation::Visitor, simulation::Pose, simulation::Needs, simulation::Mood, simulation::Wallet, simulation::Departure, simulation::Upkeep, simulation::Patrol>;

        std::uint32_t id(entt::entity e)
        {
            return entt::to_integral(e);
        }

        // ----- chunks -----

        template<typename T> void write_component(Writer& w, const entt::registry& registry)
        {
            std::vector<std::pair<std::uint32_t, const T*>> records;
            for (const auto [e, value] : registry.view<T>().each())
                records.emplace_back(id(e), &value);
            std::ranges::sort(records, { }, &std::pair<std::uint32_t, const T*>::first);
            const std::size_t at = begin_chunk(w, Stored<T>::kTag, Stored<T>::kVersion);
            w.number(static_cast<std::uint32_t>(records.size()));
            for (const auto& [e, value] : records)
            {
                w.number(e);
                Stored<T>::write(w, *value);
            }
            end_chunk(w, at);
        }

        template<typename T> std::expected<void, std::string> read_component(Reader& r, entt::registry& registry, std::uint32_t version)
        {
            const auto count = r.number<std::uint32_t>();
            if (count > r.left() / Stored<T>::kMinRecord)
                return std::unexpected(std::format("{}: {} records cannot fit in {} bytes", Stored<T>::kTag, count, r.left()));
            std::uint32_t last = 0;
            for (std::uint32_t i = 0; i < count; ++i)
            {
                const auto e = r.number<std::uint32_t>();
                const auto entity = static_cast<entt::entity>(e);
                if ((i > 0 && e <= last) || !registry.valid(entity))
                    return std::unexpected(std::format("{}: record {} names entity {}, which is out of order or not in the park", Stored<T>::kTag, i, e));
                last = e;
                auto value = [&] {
                    if constexpr (requires { Stored<T>::read(r, version); })
                        return Stored<T>::read(r, version);
                    else
                        return Stored<T>::read(r);
                }();
                if (!value)
                    return std::unexpected(std::format("{}: {}", Stored<T>::kTag, value.error()));
                if (!r.ok())
                    return std::unexpected(std::format("{}: ends inside record {}", Stored<T>::kTag, i));
                registry.emplace<T>(entity, std::move(*value));
            }
            return { };
        }

        std::expected<void, std::string> read_entities(Reader& r, entt::registry& registry)
        {
            const auto count = r.number<std::uint32_t>();
            if (count > r.left() / 4)
                return std::unexpected(std::format("ENTS: {} entities cannot fit in {} bytes", count, r.left()));
            for (std::uint32_t i = 0; i < count; ++i)
            {
                const auto e = static_cast<entt::entity>(r.number<std::uint32_t>());
                if (e == entt::null || registry.valid(e) || registry.create(e) != e)
                    return std::unexpected(std::format("ENTS: entity {} is invalid or repeated", id(e)));
            }
            return { };
        }

        /// Reads one chunk's payload, or skips it when the tag is not one we know.
        std::expected<void, std::string> read_chunk(std::string_view tag, std::uint32_t version, Reader& r, SimWorld& world, bool& have_entities)
        {
            const auto known = [&](std::uint32_t newest) -> std::expected<void, std::string> {
                if (version == 0 || version > newest)
                    return std::unexpected(std::format("{}: version {} is not one this build reads", tag, version));
                return { };
            };
            if (tag == "WRLD")
            {
                if (auto ok = known(1); !ok)
                    return ok;
                world.tick = r.number<std::uint64_t>();
                world.seed = random::Seed { r.number<std::uint64_t>() };
                return { };
            }
            if (tag == "ENTS")
            {
                if (auto ok = known(1); !ok)
                    return ok;
                if (have_entities)
                    return std::unexpected("ENTS: more than one");
                have_entities = true;
                return read_entities(r, world.registry);
            }
            std::expected<void, std::string> result;
            bool matched = false;
            const auto component = [&]<typename T>(std::type_identity<T>) {
                if (matched || tag != Stored<T>::kTag)
                    return;
                matched = true;
                if (!have_entities)
                    result = std::unexpected(std::format("{}: comes before ENTS", tag));
                else if (auto ok = known(Stored<T>::kVersion); !ok)
                    result = ok;
                else if (!world.registry.storage<T>().empty())
                    result = std::unexpected(std::format("{}: more than one", tag));
                else
                    result = read_component<T>(r, world.registry, version);
            };
            std::apply([&](auto... c) { (component(std::type_identity<decltype(c)> { }), ...); }, Components { });
            return result;
        }
    } // namespace

    std::vector<std::byte> write_park_file(const SimWorld& world)
    {
        Writer w;
        write_header(w, static_cast<std::uint32_t>(2 + std::tuple_size_v<Components>));

        std::size_t at = begin_chunk(w, "WRLD", 1);
        w.number(world.tick);
        w.number(world.seed.value);
        end_chunk(w, at);

        std::vector<std::uint32_t> entities;
        for (const auto e : world.registry.view<entt::entity>())
            entities.push_back(id(e));
        std::ranges::sort(entities);
        at = begin_chunk(w, "ENTS", 1);
        w.number(static_cast<std::uint32_t>(entities.size()));
        for (const std::uint32_t e : entities)
            w.number(e);
        end_chunk(w, at);

        std::apply([&](auto... c) { (write_component<decltype(c)>(w, world.registry), ...); }, Components { });
        return w.take();
    }

    std::expected<SimWorld, std::string> read_park_file(std::span<const std::byte> bytes)
    {
        SimWorld world;
        bool have_entities = false;
        if (auto read = read_chunks(bytes, [&](std::string_view tag, std::uint32_t version, Reader& body) { return read_chunk(tag, version, body, world, have_entities); }); !read)
            return std::unexpected(read.error());
        return world;
    }

    namespace detail
    {
        void write_header(Writer& w, std::uint32_t chunks)
        {
            w.bytes(kMagic.data(), kMagic.size());
            w.number(kParkFileVersion);
            w.number(chunks);
        }

        std::expected<void, std::string> read_chunks(std::span<const std::byte> bytes, const std::function<std::expected<void, std::string>(std::string_view tag, std::uint32_t version, Reader& body)>& chunk)
        {
            Reader r(bytes);
            std::array<char, 8> magic { };
            r.take(magic.data(), magic.size());
            if (!r.ok() || magic != kMagic)
                return std::unexpected("not a park file");
            const auto version = r.number<std::uint32_t>();
            const auto chunks = r.number<std::uint32_t>();
            if (!r.ok())
                return std::unexpected("the header is cut short");
            if (version == 0 || version > kParkFileVersion)
                return std::unexpected(std::format("file format version {} is newer than this build reads ({})", version, kParkFileVersion));
            if (chunks > r.left() / kChunkHeader)
                return std::unexpected(std::format("{} chunks cannot fit in {} bytes", chunks, r.left()));
            for (std::uint32_t i = 0; i < chunks; ++i)
            {
                std::array<char, 4> tag { };
                r.take(tag.data(), tag.size());
                const auto chunk_version = r.number<std::uint32_t>();
                const auto length = r.number<std::uint64_t>();
                const std::span<const std::byte> payload = r.slice(length <= r.left() ? static_cast<std::size_t>(length) : r.left() + 1);
                if (!r.ok())
                    return std::unexpected(std::format("chunk {} is cut short", i));
                Reader body(payload);
                const std::string_view name(tag.data(), tag.size());
                if (auto read = chunk(name, chunk_version, body); !read)
                    return read;
                if (!body.ok())
                    return std::unexpected(std::format("{}: ends early", name));
            }
            if (r.left() != 0)
                return std::unexpected(std::format("{} bytes after the last chunk", r.left()));
            return { };
        }
    } // namespace detail

} // namespace rct3::parkfile
