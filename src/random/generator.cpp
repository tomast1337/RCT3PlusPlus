// SPDX-License-Identifier: GPL-3.0-or-later
#include "random/generator.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <format>
#include <limits>

namespace rct3::random
{

    namespace
    {
        constexpr std::size_t kHexPerWord = 16;
    } // namespace

    std::uint64_t split_mix(std::uint64_t& state)
    {
        state += 0x9e3779b97f4a7c15ULL;
        std::uint64_t z = state;
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
    }

    RandomState seeded(Seed seed)
    {
        RandomState out;
        for (std::uint64_t& w : out.words)
            w = split_mix(seed.value);
        // SplitMix64 is a bijection on its counter, so four outputs in a row are never all zero; checked anyway.
        if (out.words == std::array<std::uint64_t, 4> { })
            out.words[0] = 1;
        return out;
    }

    RandomState stream(Seed seed, RandomStream stream)
    {
        std::uint64_t mixed = seed.value;
        const std::uint64_t a = split_mix(mixed);
        std::uint64_t id = static_cast<std::uint64_t>(stream);
        return seeded(Seed { a ^ split_mix(id) });
    }

    std::uint64_t next(RandomState& state)
    {
        auto& s = state.words;
        const std::uint64_t out = std::rotl(s[1] * 5, 7) * 9;
        const std::uint64_t t = s[1] << 17;
        s[2] ^= s[0];
        s[3] ^= s[1];
        s[1] ^= s[2];
        s[0] ^= s[3];
        s[2] ^= t;
        s[3] = std::rotl(s[3], 45);
        return out;
    }

    double unit(RandomState& state)
    {
        // The top 53 bits fill a double's mantissa exactly.
        return static_cast<double>(next(state) >> 11) * 0x1.0p-53;
    }

    std::uint64_t below(RandomState& state, std::uint64_t count)
    {
        if (count == 0)
            return 0;
        // Draws again past the last whole multiple of `count`, so no value is more likely than another.
        const std::uint64_t limit = std::numeric_limits<std::uint64_t>::max() - std::numeric_limits<std::uint64_t>::max() % count;
        std::uint64_t value = next(state);
        while (value >= limit)
            value = next(state);
        return value % count;
    }

    std::int64_t between(RandomState& state, std::int64_t low, std::int64_t high)
    {
        if (high <= low)
            return low;
        const auto span = static_cast<std::uint64_t>(high) - static_cast<std::uint64_t>(low);
        if (span == std::numeric_limits<std::uint64_t>::max())
            return static_cast<std::int64_t>(next(state));
        return static_cast<std::int64_t>(static_cast<std::uint64_t>(low) + below(state, span + 1));
    }

    double between(RandomState& state, double low, double high)
    {
        return low + (high - low) * unit(state);
    }

    bool chance(RandomState& state, double p)
    {
        if (!(p > 0))
            return false;
        return unit(state) < p;
    }

    std::string seed_text(Seed seed)
    {
        std::array<char, kHexPerWord> raw { };
        const auto end = std::to_chars(raw.data(), raw.data() + raw.size(), seed.value, 16).ptr;
        const auto length = static_cast<std::size_t>(end - raw.data());
        return std::format("0x{}{}", std::string(kHexPerWord - length, '0'), std::string_view(raw.data(), length));
    }

    std::expected<Seed, std::string> parse_seed(std::string_view text)
    {
        int base = 10;
        if (text.starts_with("0x") || text.starts_with("0X"))
        {
            text.remove_prefix(2);
            base = 16;
        }
        std::uint64_t seed = 0;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), seed, base);
        if (text.empty() || error != std::errc { } || end != text.data() + text.size())
            return std::unexpected("a seed is a whole number from 0 to 2^64 - 1, in decimal or 0x hex");
        return Seed { seed };
    }

    std::string to_text(const RandomState& state)
    {
        std::string out;
        out.reserve(kHexPerWord * state.words.size());
        for (const std::uint64_t w : state.words)
        {
            std::array<char, kHexPerWord> digits;
            digits.fill('0');
            std::array<char, kHexPerWord> raw { };
            const auto end = std::to_chars(raw.data(), raw.data() + raw.size(), w, 16).ptr;
            const auto length = static_cast<std::size_t>(end - raw.data());
            std::copy(raw.data(), end, digits.data() + (kHexPerWord - length));
            out.append(digits.data(), digits.size());
        }
        return out;
    }

    std::expected<RandomState, std::string> parse_state(std::string_view text)
    {
        RandomState out;
        if (text.size() != kHexPerWord * out.words.size())
            return std::unexpected(std::format("a random state is {} hex digits", kHexPerWord * out.words.size()));
        for (std::size_t i = 0; i < out.words.size(); ++i)
        {
            const char* first = text.data() + i * kHexPerWord;
            const char* last = first + kHexPerWord;
            for (const char* c = first; c != last; ++c)
            {
                const bool hex = (*c >= '0' && *c <= '9') || (*c >= 'a' && *c <= 'f') || (*c >= 'A' && *c <= 'F');
                if (!hex)
                    return std::unexpected("a random state has hex digits only");
            }
            std::from_chars(first, last, out.words[i], 16);
        }
        if (out.words == std::array<std::uint64_t, 4> { })
            return std::unexpected("a random state cannot be all zero");
        return out;
    }

} // namespace rct3::random
