// SPDX-License-Identifier: GPL-3.0-or-later
#include "parkfile/park_file.hpp"

#include "core/types.hpp"
#include "parkfile/binary.hpp"
#include "simulation/component_tags.hpp"

#include <array>
#include <format>
#include <span>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace rct3::parkfile
{

    namespace
    {
        using simulation::SimWorld;
        using namespace detail;

        /// What a world keeps beside its entities, in "WRLD".
        struct RunState
        {
            u64 tick = 0;
            random::Seed seed { };
            random::RandomState gameplay { };
            f32 guest_slowdown = 1;
            bool parties_huddle_often = false;
            simulation::NameCheats cheats { };
        };

        constexpr u32 kRunVersion = 2;
        constexpr u32 kEntitiesVersion = 2;
        constexpr u32 kLinksVersion = 1;
        constexpr u32 kComponentVersion = 1;

        std::string_view name(const std::array<char, 4>& tag)
        {
            return { tag.data(), tag.size() };
        }

        template<typename T> void write_component(Writer& w, const entt::registry& registry, std::string_view tag)
        {
            const auto* storage = registry.storage<T>();
            const usize at = begin_chunk(w, tag, kComponentVersion);
            w.number(layout_hash<T>());
            w.number(static_cast<u32>(storage != nullptr ? storage->size() : 0));
            if (storage != nullptr)
            {
                // In the storage's own order, which the park's loops go by.
                const entt::sparse_set& base = *storage;
                for (auto e = base.rbegin(); e != base.rend(); ++e)
                {
                    w.number(entt::to_integral(*e));
                    if constexpr (!std::is_empty_v<T>)
                        put(w, storage->get(*e));
                }
            }
            end_chunk(w, at);
        }

        template<typename T> std::expected<void, std::string> read_component(Reader& r, entt::registry& registry, std::string_view tag)
        {
            if (r.number<u64>() != layout_hash<T>())
                return std::unexpected(std::format("{}: written by a build whose park model differs from this one's", tag));
            const auto count = r.number<u32>();
            if (count > r.left() / 4)
                return std::unexpected(std::format("{}: {} records cannot fit in {} bytes", tag, count, r.left()));
            auto& storage = registry.storage<T>();
            for (u32 i = 0; i < count; ++i)
            {
                const auto e = static_cast<entt::entity>(r.number<u32>());
                if (!registry.valid(e) || storage.contains(e))
                    return std::unexpected(std::format("{}: record {} names entity {}, which is repeated or not in the park", tag, i, entt::to_integral(e)));
                if constexpr (std::is_empty_v<T>)
                    storage.emplace(e);
                else
                {
                    T value { };
                    get(r, value);
                    if (!r.ok())
                        return std::unexpected(std::format("{}: ends inside record {}", tag, i));
                    storage.emplace(e, std::move(value));
                }
            }
            return { };
        }

        std::expected<void, std::string> read_entities(Reader& r, entt::registry& registry)
        {
            const auto count = r.number<u32>();
            const auto in_use = r.number<u32>();
            if (count > r.left() / 4 || in_use > count)
                return std::unexpected(std::format("ENTS: {} entities, {} in use, cannot fit in {} bytes", count, in_use, r.left()));
            auto& storage = registry.storage<entt::entity>();
            for (u32 i = 0; i < count; ++i)
            {
                const auto e = static_cast<entt::entity>(r.number<u32>());
                if (e == entt::null || e == entt::tombstone || storage.contains(e) || storage.generate(e) != e)
                    return std::unexpected(std::format("ENTS: entity {} is invalid or repeated", entt::to_integral(e)));
            }
            // A new entity, once the destroyed ones are used up, is the lowest one never made, as in the run saved:
            // that one made every entity below the next it would make.
            // Those past the first `in_use` were destroyed, and are handed out again first.
            storage.free_list(in_use);
            return { };
        }

        /// Reads one chunk's payload, or skips it when the tag is not one we know.
        std::expected<void, std::string> read_chunk(std::string_view tag, u32 version, Reader& r, SimWorld& world, bool& have_entities)
        {
            const auto known = [&](u32 newest) -> std::expected<void, std::string> {
                if (version != newest)
                    return std::unexpected(std::format("{}: version {} is not one this build reads", tag, version));
                return { };
            };
            if (tag == "WRLD")
            {
                if (auto ok = known(kRunVersion); !ok)
                    return ok;
                RunState run;
                if (auto read = get_chunk(tag, r, run); !read)
                    return read;
                world.tick = run.tick;
                world.seed = run.seed;
                world.gameplay = run.gameplay;
                world.guest_slowdown = run.guest_slowdown;
                world.parties_huddle_often = run.parties_huddle_often;
                world.cheats = run.cheats;
                return { };
            }
            if (tag == "ENTS")
            {
                if (auto ok = known(kEntitiesVersion); !ok)
                    return ok;
                if (have_entities)
                    return std::unexpected("ENTS: more than one");
                have_entities = true;
                return read_entities(r, world.registry);
            }
            if (tag == "PLNK")
            {
                if (auto ok = known(kLinksVersion); !ok)
                    return ok;
                std::vector<simulation::PathLink> links;
                if (auto read = get_chunk(tag, r, links); !read)
                    return read;
                world.paths.clear_links();
                for (const simulation::PathLink& link : links)
                    world.paths.link(link);
                return { };
            }
            std::expected<void, std::string> result;
            bool matched = false;
            const auto component = [&]<typename T>(simulation::Tagged<T> t) {
                if (matched || tag != name(t.tag))
                    return;
                matched = true;
                if (!have_entities)
                    result = std::unexpected(std::format("{}: comes before ENTS", tag));
                else if (auto ok = known(kComponentVersion); !ok)
                    result = ok;
                else if (!world.registry.storage<T>().empty())
                    result = std::unexpected(std::format("{}: more than one", tag));
                else
                    result = read_component<T>(r, world.registry, tag);
            };
            std::apply([&](auto... t) { (component(t), ...); }, simulation::kComponents);
            const auto park_value = [&]<typename T>(simulation::Tagged<T> t) {
                if (matched || tag != name(t.tag))
                    return;
                matched = true;
                if (auto ok = known(kComponentVersion); !ok)
                    result = ok;
                else
                {
                    T value { };
                    if (auto read = get_chunk(tag, r, value); !read)
                        result = read;
                    else
                        world.registry.ctx().insert_or_assign(std::move(value));
                }
            };
            std::apply([&](auto... t) { (park_value(t), ...); }, simulation::kParkValues);
            return result;
        }
    } // namespace

    std::vector<std::byte> write_park_file(const SimWorld& world)
    {
        Writer w;
        const auto& ctx = world.registry.ctx();
        const u32 values = std::apply([&](auto... t) { return (u32 { ctx.contains<typename decltype(t)::Type>() } + ... + 0); }, simulation::kParkValues);
        write_header(w, static_cast<u32>(3 + std::tuple_size_v<decltype(simulation::kComponents)>) + values);

        put_chunk(w, "WRLD", kRunVersion, RunState { .tick = world.tick, .seed = world.seed, .gameplay = world.gameplay, .guest_slowdown = world.guest_slowdown, .parties_huddle_often = world.parties_huddle_often, .cheats = world.cheats });

        // Every entity in the storage's order, the destroyed ones after those in use, so a load hands out the
        // same entities next.
        const auto& entities = *world.registry.storage<entt::entity>();
        usize at = begin_chunk(w, "ENTS", kEntitiesVersion);
        w.number(static_cast<u32>(entities.size()));
        w.number(static_cast<u32>(entities.free_list()));
        const entt::sparse_set& base = entities;
        for (auto e = base.rbegin(); e != base.rend(); ++e)
            w.number(entt::to_integral(*e));
        end_chunk(w, at);

        put_chunk(w, "PLNK", kLinksVersion, world.paths.links());

        std::apply([&](auto... t) { (write_component<typename decltype(t)::Type>(w, world.registry, name(t.tag)), ...); }, simulation::kComponents);
        const auto park_value = [&]<typename T>(simulation::Tagged<T> t) {
            if (ctx.contains<T>())
                put_chunk(w, name(t.tag), kComponentVersion, ctx.get<T>());
        };
        std::apply([&](auto... t) { (park_value(t), ...); }, simulation::kParkValues);
        return w.take();
    }

    std::expected<void, std::string> read_park_file(std::span<const std::byte> bytes, SimWorld& world)
    {
        // Emptied in place, so what the park was given to run in (the ground under wrecks) stays.
        for (const auto [id, storage] : world.registry.storage())
            if (id != entt::type_hash<entt::entity>::value())
                storage.clear();
        world.registry.storage<entt::entity>().clear();
        std::apply([&](auto... t) { (world.registry.ctx().erase<typename decltype(t)::Type>(), ...); }, simulation::kParkValues);
        world.paths.clear_links();
        bool have_entities = false;
        return read_chunks(bytes, [&](std::string_view tag, u32 version, Reader& body) { return read_chunk(tag, version, body, world, have_entities); });
    }

    std::expected<SimWorld, std::string> read_park_file(std::span<const std::byte> bytes)
    {
        SimWorld world;
        if (auto read = read_park_file(bytes, world); !read)
            return std::unexpected(read.error());
        return world;
    }

    namespace detail
    {
        void write_header(Writer& w, u32 chunks)
        {
            w.bytes(kMagic.data(), kMagic.size());
            w.number(kParkFileVersion);
            w.number(chunks);
        }

        std::expected<void, std::string> read_chunks(std::span<const std::byte> bytes, const std::function<std::expected<void, std::string>(std::string_view tag, u32 version, Reader& body)>& chunk)
        {
            Reader r(bytes);
            std::array<char, 8> magic { };
            r.take(magic.data(), magic.size());
            if (!r.ok() || magic != kMagic)
                return std::unexpected("not a park file");
            const auto version = r.number<u32>();
            const auto chunks = r.number<u32>();
            if (!r.ok())
                return std::unexpected("the header is cut short");
            if (version == 0 || version > kParkFileVersion)
                return std::unexpected(std::format("file format version {} is newer than this build reads ({})", version, kParkFileVersion));
            if (chunks > r.left() / kChunkHeader)
                return std::unexpected(std::format("{} chunks cannot fit in {} bytes", chunks, r.left()));
            for (u32 i = 0; i < chunks; ++i)
            {
                std::array<char, 4> tag { };
                r.take(tag.data(), tag.size());
                const auto chunk_version = r.number<u32>();
                const auto length = r.number<u64>();
                const std::span<const std::byte> payload = r.slice(length <= r.left() ? static_cast<usize>(length) : r.left() + 1);
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
