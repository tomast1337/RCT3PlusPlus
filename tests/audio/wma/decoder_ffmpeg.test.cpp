// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

extern "C" {
#include <ffmpeg/libavutil/tx.h>
}

#include "core/types.hpp"

#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

TEST_CASE("the inverse MDCT matches its definition")
{
    for (const i32 n : { 16, 64, 256 })
    {
        std::vector<f32> in(static_cast<usize>(n));
        for (i32 k = 0; k < n; ++k)
            in[static_cast<usize>(k)] = std::sin(0.7f * static_cast<f32>(k)) + 0.25f * static_cast<f32>(k % 3);
        AVTXContext* tx = nullptr;
        av_tx_fn fn = nullptr;
        const f32 scale = 0.5f;
        REQUIRE(av_tx_init(&tx, &fn, AV_TX_FLOAT_MDCT, 1, n, &scale, AV_TX_FULL_IMDCT) == 0);
        std::vector<f32> out(static_cast<usize>(2 * n));
        fn(tx, out.data(), in.data(), sizeof(f32));
        av_tx_uninit(&tx);
        CHECK(tx == nullptr);
        for (i32 i = 0; i < 2 * n; ++i)
        {
            f64 sum = 0;
            for (i32 k = 0; k < n; ++k)
                sum += in[static_cast<usize>(k)] * std::cos(std::numbers::pi / n * (i + 0.5 + n / 2.0) * (k + 0.5));
            CHECK(out[static_cast<usize>(i)] == doctest::Approx(-scale * sum).epsilon(1e-4).scale(1e-3 * n));
        }
    }
}
