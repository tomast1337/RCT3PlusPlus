// SPDX-License-Identifier: GPL-3.0-or-later
#include "career/career_file.hpp"

#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstring>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <system_error>

namespace rct3::career
{

    namespace
    {

        namespace fs = std::filesystem;

        constexpr std::array<char, 4> kMagic = { 'R', 'C', '3', 'C' };
        constexpr std::uint16_t kVersion = 1;
        constexpr std::array<char, 4> kHead = { 'H', 'E', 'A', 'D' };
        constexpr std::array<char, 4> kScenario = { 'S', 'C', 'E', 'N' };
        constexpr std::array<char, 4> kEnd = { 'E', 'N', 'D', ' ' };
        constexpr std::array<std::string_view, 3> kPackIds = { "base", "soaked", "wild" };
        constexpr std::string_view kPrefix = "career_";
        constexpr int kMostFiles = 10000;

        /// CRC-32 as zlib and PNG compute it (the reflected polynomial).
        std::uint32_t crc32(std::uint32_t crc, std::span<const std::byte> bytes)
        {
            constexpr std::uint32_t kReflected = 0xEDB8'8320u;
            crc = ~crc;
            for (const std::byte b : bytes)
            {
                crc ^= std::to_integer<std::uint32_t>(b);
                for (int bit = 0; bit < 8; ++bit)
                    crc = (crc >> 1) ^ ((crc & 1u) != 0 ? kReflected : 0u);
            }
            return ~crc;
        }

        std::span<const std::byte> tag_bytes(const std::array<char, 4>& tag)
        {
            return std::as_bytes(std::span(tag));
        }

        class Writer
        {
        public:
            std::vector<std::byte> bytes;

            template<typename T> void put(T value)
            {
                const auto raw = std::bit_cast<std::array<std::byte, sizeof(T)>>(value);
                if constexpr (std::endian::native == std::endian::big)
                    bytes.insert(bytes.end(), raw.rbegin(), raw.rend());
                else
                    bytes.insert(bytes.end(), raw.begin(), raw.end());
            }
            void text(std::string_view s)
            {
                put(static_cast<std::uint32_t>(s.size()));
                const auto b = std::as_bytes(std::span(s));
                bytes.insert(bytes.end(), b.begin(), b.end());
            }
            void chunk(const std::array<char, 4>& tag, std::span<const std::byte> payload)
            {
                const auto t = tag_bytes(tag);
                bytes.insert(bytes.end(), t.begin(), t.end());
                put(static_cast<std::uint32_t>(payload.size()));
                bytes.insert(bytes.end(), payload.begin(), payload.end());
                put(crc32(crc32(0, t), payload));
            }
        };

        class Reader
        {
        public:
            explicit Reader(std::span<const std::byte> bytes)
                : bytes_(bytes)
            {
            }

            [[nodiscard]] std::size_t left() const
            {
                return bytes_.size() - at_;
            }

            template<typename T> std::optional<T> get()
            {
                if (left() < sizeof(T))
                    return std::nullopt;
                std::array<std::byte, sizeof(T)> raw { };
                std::memcpy(raw.data(), bytes_.data() + at_, sizeof(T));
                if constexpr (std::endian::native == std::endian::big)
                    std::ranges::reverse(raw);
                at_ += sizeof(T);
                return std::bit_cast<T>(raw);
            }
            std::optional<std::span<const std::byte>> take(std::size_t n)
            {
                if (left() < n)
                    return std::nullopt;
                const auto out = bytes_.subspan(at_, n);
                at_ += n;
                return out;
            }
            std::optional<std::string> text()
            {
                const auto n = get<std::uint32_t>();
                if (!n)
                    return std::nullopt;
                const auto b = take(*n);
                if (!b)
                    return std::nullopt;
                return std::string(reinterpret_cast<const char*>(b->data()), b->size());
            }

        private:
            std::span<const std::byte> bytes_;
            std::size_t at_ = 0;
        };

        std::vector<std::byte> scenario_payload(const ScenarioProgress& p)
        {
            Writer w;
            w.text(p.id);
            w.put(static_cast<std::uint8_t>(p.status));
            w.text(p.last_save);
            for (const std::int64_t t : p.reached)
                w.put(t);
            w.put(p.play_seconds);
            return w.bytes;
        }

        std::optional<ScenarioProgress> read_scenario(std::span<const std::byte> payload)
        {
            Reader r(payload);
            ScenarioProgress p;
            auto id = r.text();
            const auto status = r.get<std::uint8_t>();
            auto last_save = r.text();
            if (!id || !status || *status > static_cast<std::uint8_t>(ScenarioStatus::Tycoon) || !last_save)
                return std::nullopt;
            p.id = std::move(*id);
            p.status = static_cast<ScenarioStatus>(*status);
            p.last_save = std::move(*last_save);
            for (std::int64_t& t : p.reached)
            {
                const auto v = r.get<std::int64_t>();
                if (!v)
                    return std::nullopt;
                t = *v;
            }
            const auto played = r.get<std::uint64_t>();
            if (!played)
                return std::nullopt;
            p.play_seconds = *played;
            return p;
        }

        std::string file_name(int number)
        {
            std::array<char, 8> digits { };
            std::snprintf(digits.data(), digits.size(), "%04d", number);
            return std::string(kPrefix) + digits.data() + std::string(kCareerExtension);
        }

        std::optional<std::vector<std::byte>> read_bytes(const fs::path& path)
        {
            std::ifstream in(path, std::ios::binary);
            if (!in)
                return std::nullopt;
            const std::vector<char> chars { std::istreambuf_iterator<char>(in), { } };
            std::vector<std::byte> bytes(chars.size());
            std::memcpy(bytes.data(), chars.data(), chars.size());
            return bytes;
        }

    } // namespace

    const char* to_string(CareerFileError e) noexcept
    {
        switch (e)
        {
            case CareerFileError::NotACareer:
                return "not a career file";
            case CareerFileError::NewerVersion:
                return "written by a newer version";
            case CareerFileError::Truncated:
                return "cut off";
            case CareerFileError::BadChecksum:
                return "damaged (checksum)";
            case CareerFileError::Malformed:
                return "damaged (contents)";
        }
        return "unknown";
    }

    std::string_view pack_id(int pack)
    {
        return pack >= 0 && static_cast<std::size_t>(pack) < kPackIds.size() ? kPackIds[static_cast<std::size_t>(pack)] : std::string_view { };
    }

    std::optional<int> pack_from_id(std::string_view id)
    {
        const auto it = std::ranges::find(kPackIds, id);
        if (it == kPackIds.end())
            return std::nullopt;
        return static_cast<int>(it - kPackIds.begin());
    }

    std::vector<std::byte> write_career(const CareerData& data)
    {
        const Career& c = data.career;
        Writer w;
        const auto magic = tag_bytes(kMagic);
        w.bytes.insert(w.bytes.end(), magic.begin(), magic.end());
        w.put(kVersion);
        w.put(std::uint16_t { 0 });

        Writer head;
        head.text(c.name);
        head.text(pack_id(c.pack));
        head.text(c.current < c.scenarios.size() ? std::string_view(c.scenarios[c.current].id) : std::string_view { });
        head.put(c.created);
        w.chunk(kHead, head.bytes);
        for (const auto* list : { &c.scenarios, &c.unlisted })
        {
            for (const ScenarioProgress& p : *list)
                w.chunk(kScenario, scenario_payload(p));
        }
        for (const CareerChunk& chunk : data.unknown)
            w.chunk(chunk.tag, chunk.payload);
        w.chunk(kEnd, { });
        return std::move(w.bytes);
    }

    std::expected<CareerData, CareerFileError> read_career(std::span<const std::byte> bytes)
    {
        Reader r(bytes);
        const auto magic = r.take(kMagic.size());
        if (!magic || !std::ranges::equal(*magic, tag_bytes(kMagic)))
            return std::unexpected(CareerFileError::NotACareer);
        const auto version = r.get<std::uint16_t>();
        const auto flags = r.get<std::uint16_t>();
        if (!version || !flags)
            return std::unexpected(CareerFileError::Truncated);
        if (*version > kVersion)
            return std::unexpected(CareerFileError::NewerVersion);

        CareerData data;
        Career& c = data.career;
        std::string current;
        bool head = false;
        while (true)
        {
            const auto tag = r.take(4);
            const auto length = r.get<std::uint32_t>();
            if (!tag || !length)
                return std::unexpected(CareerFileError::Truncated);
            const auto payload = r.take(*length);
            const auto crc = r.get<std::uint32_t>();
            if (!payload || !crc)
                return std::unexpected(CareerFileError::Truncated);
            if (crc32(crc32(0, *tag), *payload) != *crc)
                return std::unexpected(CareerFileError::BadChecksum);
            std::array<char, 4> name { };
            std::memcpy(name.data(), tag->data(), name.size());

            if (name == kEnd)
                break;
            if (name == kHead)
            {
                Reader h(*payload);
                auto career_name = h.text();
                const auto pack = h.text();
                auto picked = h.text();
                const auto created = h.get<std::int64_t>();
                const auto pack_number = pack ? pack_from_id(*pack) : std::nullopt;
                if (head || !career_name || !pack_number || !picked || !created)
                    return std::unexpected(CareerFileError::Malformed);
                c.name = std::move(*career_name);
                c.pack = *pack_number;
                current = std::move(*picked);
                c.created = *created;
                head = true;
            }
            else if (name == kScenario)
            {
                auto p = read_scenario(*payload);
                if (!head || !p)
                    return std::unexpected(CareerFileError::Malformed);
                c.scenarios.push_back(std::move(*p));
            }
            else
                data.unknown.push_back({ name, { payload->begin(), payload->end() } });
        }
        if (!head)
            return std::unexpected(CareerFileError::Malformed);
        const auto picked = std::ranges::find(c.scenarios, current, &ScenarioProgress::id);
        c.current = picked != c.scenarios.end() ? static_cast<std::size_t>(picked - c.scenarios.begin()) : 0;
        return data;
    }

    std::vector<CareerEntry> list_careers(const fs::path& folder, int pack, std::optional<std::vector<config::FileProblem>&> problems)
    {
        std::vector<fs::path> paths;
        std::error_code error;
        for (const auto& entry : fs::directory_iterator(folder, error))
        {
            if (entry.is_regular_file(error) && entry.path().extension() == kCareerExtension)
                paths.push_back(entry.path());
        }
        std::ranges::sort(paths);
        std::vector<CareerEntry> careers;
        for (const fs::path& path : paths)
        {
            const auto bytes = read_bytes(path);
            auto data = bytes ? read_career(*bytes) : std::unexpected(CareerFileError::Truncated);
            if (!data)
            {
                if (problems)
                    problems->push_back({ std::format("{}: {}", path.filename().string(), bytes ? to_string(data.error()) : "cannot read") });
                continue;
            }
            if (data->career.pack == pack)
                careers.push_back({ path, std::move(*data) });
        }
        return careers;
    }

    fs::path free_career_path(const fs::path& folder)
    {
        std::error_code error;
        for (int i = 0; i < kMostFiles; ++i)
        {
            const fs::path path = folder / file_name(i);
            if (!fs::exists(path, error) && !fs::exists(fs::path(path).replace_extension(), error))
                return path;
        }
        return folder / file_name(kMostFiles);
    }

    std::expected<void, std::string> save_career(const fs::path& path, const CareerData& data)
    {
        const std::vector<std::byte> bytes = write_career(data);
        return config::write_text_file(path, std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    }

    std::expected<void, std::string> change_career(const fs::path& path, const std::function<void(Career&)>& change)
    {
        const auto bytes = read_bytes(path);
        auto data = bytes ? read_career(*bytes) : std::unexpected(CareerFileError::Truncated);
        if (!data)
            return std::unexpected(std::format("{}: {}", path.filename().string(), bytes ? to_string(data.error()) : "cannot read"));
        change(data->career);
        return save_career(path, *data);
    }

    std::expected<void, std::string> delete_career(const fs::path& path)
    {
        std::error_code error;
        const fs::path games = fs::path(path).replace_extension();
        fs::remove_all(games, error);
        if (error)
            return std::unexpected(std::format("cannot delete {}: {}", games.string(), error.message()));
        fs::remove(path, error);
        if (error)
            return std::unexpected(std::format("cannot delete {}: {}", path.string(), error.message()));
        return { };
    }

} // namespace rct3::career
