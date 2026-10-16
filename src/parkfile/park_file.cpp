// SPDX-License-Identifier: GPL-3.0-or-later
#include "parkfile/park_file.hpp"

#include "simulation/components.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <format>
#include <string_view>
#include <tuple>
#include <utility>

namespace rct3::parkfile
{

    namespace
    {
        using simulation::SimWorld;

        constexpr std::array<char, 8> kMagic { 'R', '3', 'P', 'P', 'P', 'A', 'R', 'K' };
        constexpr std::size_t kChunkHeader = 16;

        // ----- writing -----

        class Writer
        {
        public:
            void bytes(const void* data, std::size_t size)
            {
                const auto* p = static_cast<const std::byte*>(data);
                out_.insert(out_.end(), p, p + size);
            }
            template<typename T> void number(T value)
            {
                if constexpr (std::is_floating_point_v<T>)
                    number(std::bit_cast<std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>>(value));
                else
                {
                    if constexpr (std::endian::native == std::endian::big)
                        value = std::byteswap(value);
                    bytes(&value, sizeof value);
                }
            }
            void text(std::string_view s)
            {
                number(static_cast<std::uint32_t>(s.size()));
                bytes(s.data(), s.size());
            }
            void vec3(glm::vec3 v)
            {
                number(v.x);
                number(v.y);
                number(v.z);
            }
            std::size_t size() const
            {
                return out_.size();
            }
            /// Overwrites the eight bytes at `at` with `value`.
            void patch(std::size_t at, std::uint64_t value)
            {
                if constexpr (std::endian::native == std::endian::big)
                    value = std::byteswap(value);
                std::memcpy(out_.data() + at, &value, sizeof value);
            }
            std::vector<std::byte> take()
            {
                return std::move(out_);
            }

        private:
            std::vector<std::byte> out_;
        };

        // ----- reading -----

        /// Reads within a span; past its end, reads give zero and the reader is no longer ok.
        class Reader
        {
        public:
            explicit Reader(std::span<const std::byte> in)
                : in_(in)
            {
            }

            bool take(void* to, std::size_t size)
            {
                if (size > in_.size() - at_)
                {
                    ok_ = false;
                    at_ = in_.size();
                    std::memset(to, 0, size);
                    return false;
                }
                std::memcpy(to, in_.data() + at_, size);
                at_ += size;
                return true;
            }
            template<typename T> T number()
            {
                if constexpr (std::is_floating_point_v<T>)
                    return std::bit_cast<T>(number<std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>>());
                else
                {
                    T value { };
                    take(&value, sizeof value);
                    if constexpr (std::endian::native == std::endian::big)
                        value = std::byteswap(value);
                    return value;
                }
            }
            std::string text()
            {
                const auto size = number<std::uint32_t>();
                if (size > left())
                {
                    ok_ = false;
                    at_ = in_.size();
                    return { };
                }
                std::string s(size, '\0');
                take(s.data(), size);
                return s;
            }
            glm::vec3 vec3()
            {
                const float x = number<float>();
                const float y = number<float>();
                const float z = number<float>();
                return { x, y, z };
            }
            std::span<const std::byte> slice(std::size_t size)
            {
                if (size > left())
                {
                    ok_ = false;
                    at_ = in_.size();
                    return { };
                }
                const auto s = in_.subspan(at_, size);
                at_ += size;
                return s;
            }
            std::size_t left() const
            {
                return in_.size() - at_;
            }
            bool ok() const
            {
                return ok_;
            }

        private:
            std::span<const std::byte> in_;
            std::size_t at_ = 0;
            bool ok_ = true;
        };

        // ----- components -----

        // How each component is stored: its chunk tag and version, the smallest record it can have (to check
        // counts before allocating), and its fields in order.
        template<typename T> struct Stored;

        template<> struct Stored<simulation::Visitor>
        {
            static constexpr std::string_view kTag = "VSTR";
            static constexpr std::uint32_t kVersion = 1;
            static constexpr std::size_t kMinRecord = 4 + 1 + 1 + 4 + 8;
            static void write(Writer& w, const simulation::Visitor& v)
            {
                w.number(std::to_underlying(v.age));
                w.number(std::uint8_t { v.female });
                w.text(v.name);
                w.number(v.arrived);
            }
            static std::expected<simulation::Visitor, std::string> read(Reader& r)
            {
                simulation::Visitor v;
                const auto age = r.number<std::uint8_t>();
                if (age > std::to_underlying(simulation::AgeGroup::Adult))
                    return std::unexpected(std::format("unknown age group {}", age));
                v.age = static_cast<simulation::AgeGroup>(age);
                v.female = r.number<std::uint8_t>() != 0;
                v.name = r.text();
                v.arrived = r.number<std::uint64_t>();
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
                w.number(n.hunger);
                w.number(n.thirst);
                w.number(n.bladder);
            }
            static std::expected<simulation::Needs, std::string> read(Reader& r)
            {
                simulation::Needs n;
                n.hunger = r.number<float>();
                n.thirst = r.number<float>();
                n.bladder = r.number<float>();
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
                w.number(m.happiness);
                w.number(m.energy);
                w.number(m.nausea);
            }
            static std::expected<simulation::Mood, std::string> read(Reader& r)
            {
                simulation::Mood m;
                m.happiness = r.number<float>();
                m.energy = r.number<float>();
                m.nausea = r.number<float>();
                return m;
            }
        };

        template<> struct Stored<simulation::Wallet>
        {
            static constexpr std::string_view kTag = "WALT";
            static constexpr std::uint32_t kVersion = 1;
            static constexpr std::size_t kMinRecord = 4 + 8;
            static void write(Writer& w, const simulation::Wallet& m)
            {
                w.number(m.cash);
            }
            static std::expected<simulation::Wallet, std::string> read(Reader& r)
            {
                return simulation::Wallet { r.number<std::int64_t>() };
            }
        };

        using Components = std::tuple<simulation::Visitor, simulation::Pose, simulation::Needs, simulation::Mood, simulation::Wallet>;

        std::uint32_t id(entt::entity e)
        {
            return entt::to_integral(e);
        }

        // ----- chunks -----

        /// Starts a chunk; returns where its length goes, filled in by end_chunk.
        std::size_t begin_chunk(Writer& w, std::string_view tag, std::uint32_t version)
        {
            w.bytes(tag.data(), 4);
            w.number(version);
            const std::size_t at = w.size();
            w.number(std::uint64_t { 0 });
            return at;
        }

        void end_chunk(Writer& w, std::size_t length_at)
        {
            w.patch(length_at, w.size() - length_at - 8);
        }

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

        template<typename T> std::expected<void, std::string> read_component(Reader& r, entt::registry& registry)
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
                auto value = Stored<T>::read(r);
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
                world.seed = r.number<std::uint64_t>();
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
                    result = read_component<T>(r, world.registry);
            };
            std::apply([&](auto... c) { (component(std::type_identity<decltype(c)> { }), ...); }, Components { });
            return result;
        }
    } // namespace

    std::vector<std::byte> write_park_file(const SimWorld& world)
    {
        Writer w;
        w.bytes(kMagic.data(), kMagic.size());
        w.number(kParkFileVersion);
        w.number(static_cast<std::uint32_t>(2 + std::tuple_size_v<Components>));

        std::size_t at = begin_chunk(w, "WRLD", 1);
        w.number(world.tick);
        w.number(world.seed);
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

        SimWorld world;
        bool have_entities = false;
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
            if (auto read = read_chunk(name, chunk_version, body, world, have_entities); !read)
                return std::unexpected(read.error());
            if (!body.ok())
                return std::unexpected(std::format("{}: ends early", name));
        }
        if (r.left() != 0)
            return std::unexpected(std::format("{} bytes after the last chunk", r.left()));
        return world;
    }

} // namespace rct3::parkfile
