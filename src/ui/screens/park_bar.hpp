// SPDX-License-Identifier: GPL-3.0-or-later
//
// The bar along the top of the park view, as the game shows it: a strip of fixed size centred at the top of
// the screen. Its left part holds the objectives button and the game speed buttons, then the park's money,
// its guests and its rating between a thumb down and a thumb up; a divider leads to the right part, with
// the temperature, the weather and its forecast, and the date and time.
//
// Everything the bar shows is given by the owner with show(), as text ready to draw, so the bar never needs
// the park itself; park_bar_values() writes it from the park's state. A press on a speed button is a
// request: the owner takes the speed asked for with take_speed() and puts it in the park's state. A notch in
// the middle of the bar lets the pointer through to the message drawer behind it.
#pragma once

#include "save/model/world.hpp"
#include "simulation/park_rating.hpp"
#include "simulation/park_state.hpp"
#include "ui/money_text.hpp"
#include "ui/string_table.hpp"
#include "ui/widgets/button.hpp"
#include "ui/widgets/label.hpp"
#include "ui/widgets/panel.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace rct3::ui
{

    struct ParkBarStyle
    {
        std::string left_frame;  ///< Behind the left part
        std::string divider;     ///< Between the parts
        std::string right_frame; ///< Behind the right part
        std::string objective_icon;
        /// Pause, play, fast and fastest, and the same picked; the picked ones may be missing.
        std::array<std::string, 4> speed_icons;
        std::array<std::string, 4> picked_speed_icons;
        std::string thumb_down;
        std::string thumb_up;
        /// One for each kind of weather, in the order of save::WeatherType.
        std::array<std::string, 7> weather_icons;
        std::string forecast_arrow;
        Color text_colour = hex("#ffffff");
        Color rating_back = hex("#00000080");
        Color rating_low = hex("#d8402c");  ///< The rating bar's colour at no rating
        Color rating_high = hex("#4cc23a"); ///< And at the highest
    };

    /// What the bar shows. Empty text leaves its place empty.
    struct ParkBarValues
    {
        simulation::GameSpeed speed;
        std::u16string money;
        std::u16string guests;
        std::optional<float> rating; ///< 0 to 1; no bar without one
        std::u16string rating_help;  ///< The rating's tooltip: its number, and its parts once a week has been rated
        std::u16string temperature;
        save::WeatherType weather = save::WeatherType::Unknown;
        /// The weather on its way, shown after an arrow; nothing when it stays the same.
        std::optional<save::WeatherType> forecast;
        std::u16string date;
    };

    namespace park_bar
    {
        inline constexpr float kWidth = 680;
        inline constexpr float kHeight = 28;
        inline constexpr float kLeftWidth = 304; ///< Of the left part; the divider starts there
        inline constexpr float kDividerWidth = 38;
        inline constexpr float kLowerEdge = 3; ///< The frame's border along the bottom; items centre above it
        /// The notch in the middle of the bar that lets the pointer through to the message drawer behind it:
        /// a half disc of this radius up from the bar's bottom edge, centred on the divider.
        inline constexpr float kNotchRadius = 18;
    } // namespace park_bar

    /// "<Month>, Year <n>", two spaces, then the time: the bar's date. `month` counts from 0, `year` from 1.
    [[nodiscard]] std::u16string park_bar_date(const StringTable& strings, std::int32_t month, std::int64_t year, std::int32_t minutes, bool twelve_hour);

    /// How the player asked to see amounts, temperatures and times.
    struct ParkBarUnits
    {
        Currency currency;
        bool fahrenheit = false;
        bool twelve_hour = true;
    };

    /// The bar's values for the park's state at `day_time` (a fraction of the day).
    /// The rating's tooltip: the rating out of the most there is, then a line for each of its parts when known.
    [[nodiscard]] std::u16string park_rating_help(const StringTable& strings, float rating, const std::optional<simulation::RatingParts>& parts);

    [[nodiscard]] ParkBarValues park_bar_values(const simulation::ParkState& state, float day_time, const StringTable& strings, const ParkBarUnits& units);

    class ParkBar : public Widget
    {
    public:
        ParkBar(std::string name, ParkBarStyle style, const StringTable& strings);

        void show(const ParkBarValues& values);
        /// The speed a button asked for since the last call, worked out from the speed last shown.
        [[nodiscard]] std::optional<simulation::GameSpeed> take_speed()
        {
            return std::exchange(asked_, std::nullopt);
        }

        [[nodiscard]] Button& objective_button() const
        {
            return *objective_;
        }
        /// Pause, play, fast and fastest.
        [[nodiscard]] Button& speed_button(std::size_t i) const
        {
            return *speeds_.at(i);
        }
        [[nodiscard]] const Label& date() const
        {
            return *date_;
        }
        [[nodiscard]] const Panel& weather() const
        {
            return *weather_;
        }
        [[nodiscard]] const Panel& forecast() const
        {
            return *forecast_;
        }

        /// The bar stops the pointer, so pressing on it never reaches the park.
        [[nodiscard]] bool takes_pointer() const override
        {
            return true;
        }
        /// Everywhere but the notch.
        [[nodiscard]] bool covers(Point p) const override;
        void update(const UpdateContext& context) override;
        void draw(DrawContext& context) const override;

    private:
        void show_speed();

        ParkBarStyle style_;
        const StringTable& strings_;
        simulation::GameSpeed speed_;
        std::optional<simulation::GameSpeed> asked_;
        std::optional<float> rating_;
        Button* objective_ = nullptr;
        std::array<Button*, 4> speeds_ { };
        Label* money_ = nullptr;
        Label* guests_ = nullptr;
        Panel* rating_area_ = nullptr;
        Label* temperature_ = nullptr;
        Panel* weather_ = nullptr;
        Panel* arrow_ = nullptr;
        Panel* forecast_ = nullptr;
        Label* date_ = nullptr;
    };

} // namespace rct3::ui
