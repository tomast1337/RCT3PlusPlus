// SPDX-License-Identifier: GPL-3.0-or-later
//
// Amounts of money that float up from where they were spent or taken, as the original game shows what
// building costs: red for money going out, green for money coming in. Each rises for a moment, its letters
// bobbing one after another in a wave, and fades out. The owner pins each to its point on the screen every
// frame and drops it once it is older than kMoneyFloatLife. Covers its parent and lets the pointer through.
//
#pragma once

#include "ui/widget.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace rct3::ui
{

    /// Seconds a float shows.
    inline constexpr float kMoneyFloatLife = 2.5f;

    struct MoneyFloat
    {
        Point at; ///< Where it rose from: the middle of the text's bottom edge when it began
        std::u16string text;
        bool gain = false; ///< Money in; otherwise money out
        float age = 0;     ///< Seconds since it began
    };

    /// How far a float has risen at `age`, in interface units.
    [[nodiscard]] float money_float_rise(float age) noexcept;

    /// How opaque a float is at `age`, from 1 down to 0 as it ends.
    [[nodiscard]] float money_float_alpha(float age) noexcept;

    /// How far letter `letter` of a float stands above the rest of its line at `age`, for text
    /// `line_height` high: a wave running along the letters.
    [[nodiscard]] float money_float_bob(std::size_t letter, float age, float line_height) noexcept;

    class MoneyFloaters : public Widget
    {
    public:
        explicit MoneyFloaters(std::string name);

        std::vector<MoneyFloat> floats;
        Color gain = hex("#5fd35a");
        Color spend = hex("#ff3a24");
        Color edge = hex("#000000"); ///< Round each letter, so it reads over any ground

        void draw(DrawContext& context) const override;
    };

} // namespace rct3::ui
