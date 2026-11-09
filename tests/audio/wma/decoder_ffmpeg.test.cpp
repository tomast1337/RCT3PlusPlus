// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

extern "C" {
#include <ffmpeg/libavutil/tx.h>
}

#include <cmath>
#include <numbers>
#include <vector>

TEST_CASE("the inverse MDCT matches its definition")
{
    for (const int n : { 16, 64, 256 })
    {
        std::vector<float> in(static_cast<std::size_t>(n));
        for (int k = 0; k < n; ++k)
            in[static_cast<std::size_t>(k)] = std::sin(0.7f * static_cast<float>(k)) + 0.25f * static_cast<float>(k % 3);
        AVTXContext* tx = nullptr;
        av_tx_fn fn = nullptr;
        const float scale = 0.5f;
        REQUIRE(av_tx_init(&tx, &fn, AV_TX_FLOAT_MDCT, 1, n, &scale, AV_TX_FULL_IMDCT) == 0);
        std::vector<float> out(static_cast<std::size_t>(2 * n));
        fn(tx, out.data(), in.data(), sizeof(float));
        av_tx_uninit(&tx);
        CHECK(tx == nullptr);
        for (int i = 0; i < 2 * n; ++i)
        {
            double sum = 0;
            for (int k = 0; k < n; ++k)
                sum += in[static_cast<std::size_t>(k)] * std::cos(std::numbers::pi / n * (i + 0.5 + n / 2.0) * (k + 0.5));
            CHECK(out[static_cast<std::size_t>(i)] == doctest::Approx(-scale * sum).epsilon(1e-4).scale(1e-3 * n));
        }
    }
}
