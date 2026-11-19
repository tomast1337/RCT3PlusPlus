// SPDX-License-Identifier: GPL-3.0-or-later
#include "random/generator.hpp"

#include "core/types.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>

namespace rct3::random
{

    namespace
    {
        constexpr usize kHexPerWord = 16;
    } // namespace

    u64 split_mix(u64& state)
    {
        state += 0x9e3779b97f4a7c15ULL;
        u64 z = state;
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
    }

    RandomState seeded(Seed seed)
    {
        RandomState out;
        for (u64& w : out.words)
            w = split_mix(seed.value);
        // SplitMix64 is a bijection on its counter, so four outputs in a row are never all zero; checked anyway.
        if (out.words == std::array<u64, 4> { })
            out.words[0] = 1;
        return out;
    }

    RandomState stream(Seed seed, RandomStream stream)
    {
        u64 mixed = seed.value;
        const u64 a = split_mix(mixed);
        u64 id = static_cast<u64>(stream);
        return seeded(Seed { a ^ split_mix(id) });
    }

    u64 next(RandomState& state)
    {
        auto& s = state.words;
        const u64 out = std::rotl(s[1] * 5, 7) * 9;
        const u64 t = s[1] << 17;
        s[2] ^= s[0];
        s[3] ^= s[1];
        s[1] ^= s[2];
        s[0] ^= s[3];
        s[2] ^= t;
        s[3] = std::rotl(s[3], 45);
        return out;
    }

    f64 unit(RandomState& state)
    {
        // The top 53 bits fill a double's mantissa exactly.
        return static_cast<f64>(next(state) >> 11) * 0x1.0p-53;
    }

    u64 below(RandomState& state, u64 count)
    {
        if (count == 0)
            return 0;
        // Draws again past the last whole multiple of `count`, so no value is more likely than another.
        const u64 limit = std::numeric_limits<u64>::max() - std::numeric_limits<u64>::max() % count;
        u64 value = next(state);
        while (value >= limit)
            value = next(state);
        return value % count;
    }

    i64 between(RandomState& state, i64 low, i64 high)
    {
        if (high <= low)
            return low;
        const auto span = static_cast<u64>(high) - static_cast<u64>(low);
        if (span == std::numeric_limits<u64>::max())
            return static_cast<i64>(next(state));
        return static_cast<i64>(static_cast<u64>(low) + below(state, span + 1));
    }

    f64 between(RandomState& state, f64 low, f64 high)
    {
        return low + (high - low) * unit(state);
    }

    bool chance(RandomState& state, f64 p)
    {
        if (!(p > 0))
            return false;
        return unit(state) < p;
    }

    std::string seed_text(Seed seed)
    {
        std::array<char, kHexPerWord> raw { };
        const auto end = std::to_chars(raw.data(), raw.data() + raw.size(), seed.value, 16).ptr;
        const auto length = static_cast<usize>(end - raw.data());
        return std::format("0x{}{}", std::string(kHexPerWord - length, '0'), std::string_view(raw.data(), length));
    }

    std::expected<Seed, std::string> parse_seed(std::string_view text)
    {
        i32 base = 10;
        if (text.starts_with("0x") || text.starts_with("0X"))
        {
            text.remove_prefix(2);
            base = 16;
        }
        u64 seed = 0;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), seed, base);
        if (text.empty() || error != std::errc { } || end != text.data() + text.size())
            return std::unexpected("a seed is a whole number from 0 to 2^64 - 1, in decimal or 0x hex");
        return Seed { seed };
    }

    std::string to_text(const RandomState& state)
    {
        std::string out;
        out.reserve(kHexPerWord * state.words.size());
        for (const u64 w : state.words)
        {
            std::array<char, kHexPerWord> digits;
            digits.fill('0');
            std::array<char, kHexPerWord> raw { };
            const auto end = std::to_chars(raw.data(), raw.data() + raw.size(), w, 16).ptr;
            const auto length = static_cast<usize>(end - raw.data());
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
        for (usize i = 0; i < out.words.size(); ++i)
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
        if (out.words == std::array<u64, 4> { })
            return std::unexpected("a random state cannot be all zero");
        return out;
    }

} // namespace rct3::random
