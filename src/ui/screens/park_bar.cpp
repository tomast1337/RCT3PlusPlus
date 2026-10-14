// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/park_bar.hpp"

#include "strings/unicode.hpp"
#include "ui/save_text.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace rct3::ui
{

    namespace
    {

        constexpr std::string_view kMonth = "Month"; ///< Followed by the month's number from 0
        constexpr std::string_view kYear = "Year";
        constexpr std::string_view kObjectiveHelp = "PID_TT_ObjectiveTabPopUp";
        constexpr std::array<std::string_view, 4> kSpeedHelp = { "Playing_PauseHelp", "Playing_PlayHelp", "Playing_FastForwardHelp", "Playing_SuperFastForwardHelp" };
        constexpr std::string_view kGuestsHelp = "Toolbar_TT_GuestsInPark";
        constexpr std::string_view kRatingHelp = "Toolbar_TT_ParkRating";
        constexpr std::array<std::string_view, 7> kWeatherHelp = { "WeatherType_Sunny_TT", "WeatherType_SunnyCloudy_TT", "WeatherType_Cloudy_TT", "WeatherType_LightRain_TT", "WeatherType_HeavyRain_TT", "WeatherType_Thunderstorm_TT", "WeatherType_Snowstorm_TT" };

        // Where things sit in the bar, left to right: each item's size and the room before it.
        constexpr float kStart = 5;  ///< Before the first item of each part
        constexpr float kWide = 5;   ///< Between buttons and groups
        constexpr float kNarrow = 2; ///< Between readouts
        constexpr float kSmallIcon = 16;
        constexpr std::array<float, 4> kSpeedWidths = { 8, 8, 13, 17 };
        constexpr float kSpeedHeight = 13;
        constexpr float kMoneyWidth = 97;
        constexpr float kGuestsWidth = 38;
        constexpr float kThumbWidth = 12;
        constexpr float kThumbHeight = 15;
        constexpr float kRatingWidth = 25;
        constexpr float kRatingHeight = 10;
        constexpr float kTemperatureWidth = 35;
        constexpr float kWeatherSize = 20;
        constexpr float kArrowWidth = 9;
        constexpr float kDateWidth = 210;
        constexpr float kDividerWidth = 38;
        constexpr float kRightStart = park_bar::kLeftWidth + kDividerWidth;

        /// Top of an item `height` tall centred above the frame's lower edge.
        float centred_top(float height)
        {
            return std::round((park_bar::kHeight - park_bar::kLowerEdge - height) / 2);
        }

        /// Places `w` at `x`, centred up and down, and returns where the next item may start.
        float put(Widget& w, float x, float width, float height)
        {
            w.placement = top_left(x, centred_top(height), width, height);
            return x + width;
        }

        std::u16string ascii(std::string_view s)
        {
            return strings::to_utf16(s);
        }

        std::size_t weather_index(save::WeatherType t)
        {
            const auto i = static_cast<std::int32_t>(t);
            return i >= 0 && i < 7 ? static_cast<std::size_t>(i) : 0;
        }

        Color mixed(Color a, Color b, float t)
        {
            const auto mix = [t](std::uint8_t x, std::uint8_t y) { return static_cast<std::uint8_t>(std::lround(x + (y - x) * t)); };
            return { mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b), mix(a.a, b.a) };
        }

    } // namespace

    std::u16string park_bar_date(const StringTable& strings, std::int32_t month, std::int64_t year, std::int32_t minutes, bool twelve_hour)
    {
        std::u16string out(strings.get(std::format("{}{}", kMonth, month)));
        out += u", ";
        out += strings.get(kYear);
        out += ascii(std::format(" {}  ", year));
        out += clock_text(minutes, twelve_hour);
        return out;
    }

    ParkBar::ParkBar(std::string name, ParkBarStyle style, const StringTable& strings)
        : Widget(std::move(name))
        , style_(std::move(style))
        , strings_(strings)
    {
        placement = { { 0.5f, 0, 0.5f, 0 }, { -park_bar::kWidth / 2, 0, park_bar::kWidth / 2, park_bar::kHeight } };

        const auto label = [&](std::string label_name, Align align) -> Label& {
            Label& l = add<Label>(std::move(label_name), std::u16string { });
            l.style = { style_.text_colour, align, Align::Centre, false };
            return l;
        };

        float x = kStart;
        objective_ = &add<Button>("objective", std::string { }, style_.objective_icon);
        objective_->icon_scale = 0.4f;
        objective_->tooltip = strings_.get(kObjectiveHelp);
        x = put(*objective_, x, kSmallIcon, kSmallIcon);
        for (std::size_t i = 0; i < speeds_.size(); ++i)
        {
            speeds_[i] = &add<Button>(std::format("speed{}", i), std::string { }, style_.speed_icons[i]);
            speeds_[i]->tooltip = strings_.get(kSpeedHelp[i]);
            x = put(*speeds_[i], x + kWide, kSpeedWidths[i], kSpeedHeight);
        }
        money_ = &label("money", Align::Centre);
        x = put(*money_, x + kNarrow, kMoneyWidth, park_bar::kHeight - park_bar::kLowerEdge);
        guests_ = &label("guests", Align::Centre);
        guests_->tooltip = strings_.get(kGuestsHelp);
        x = put(*guests_, x + kNarrow, kGuestsWidth, park_bar::kHeight - park_bar::kLowerEdge);
        x = put(add<Panel>("thumb_down", style_.thumb_down), x + kWide, kThumbWidth, kThumbHeight);
        rating_area_ = &add<Panel>("rating", std::string { });
        x = put(*rating_area_, x + kNarrow, kRatingWidth, kRatingHeight);
        put(add<Panel>("thumb_up", style_.thumb_up), x + kNarrow, kThumbWidth, kThumbHeight);

        x = kRightStart;
        temperature_ = &label("temperature", Align::Centre);
        x = put(*temperature_, x, kTemperatureWidth, park_bar::kHeight - park_bar::kLowerEdge);
        weather_ = &add<Panel>("weather", std::string { });
        x = put(*weather_, x, kWeatherSize, kWeatherSize);
        arrow_ = &add<Panel>("forecast_arrow", style_.forecast_arrow);
        x = put(*arrow_, x, kArrowWidth, kWeatherSize);
        forecast_ = &add<Panel>("forecast", std::string { });
        x = put(*forecast_, x, kWeatherSize, kWeatherSize);
        date_ = &label("date", Align::Centre);
        put(*date_, x, kDateWidth, park_bar::kHeight - park_bar::kLowerEdge);

        show_speed();
        show({ });
    }

    void ParkBar::toggle_pause()
    {
        speed_ = simulation::pause_toggled(speed_);
        show_speed();
    }

    void ParkBar::show(const ParkBarValues& values)
    {
        money_->text = values.money;
        guests_->text = values.guests;
        temperature_->text = values.temperature;
        date_->text = values.date;

        rating_ = values.rating;
        rating_area_->visible = rating_.has_value();
        rating_area_->tooltip = rating_ ? std::u16string(strings_.get(kRatingHelp)) + ascii(std::format(" {}", std::lround(*rating_ * 1000))) : std::u16string { };

        const bool known = values.weather != save::WeatherType::Unknown;
        weather_->visible = known;
        weather_->skin = style_.weather_icons[weather_index(values.weather)];
        weather_->tooltip = known ? std::u16string(strings_.get(kWeatherHelp[weather_index(values.weather)])) : std::u16string { };
        const bool coming = known && values.forecast && *values.forecast != values.weather && *values.forecast != save::WeatherType::Unknown;
        arrow_->visible = coming;
        forecast_->visible = coming;
        if (coming)
        {
            forecast_->skin = style_.weather_icons[weather_index(*values.forecast)];
            forecast_->tooltip = strings_.get(kWeatherHelp[weather_index(*values.forecast)]);
        }
    }

    void ParkBar::show_speed()
    {
        // The pause button shows picked while paused; otherwise the button of the speed playing does.
        const std::size_t picked = speed_.paused ? 0 : 1 + static_cast<std::size_t>(speed_.speed);
        for (std::size_t i = 0; i < speeds_.size(); ++i)
        {
            const std::string& selected = style_.picked_speed_icons[i];
            speeds_[i]->icon = i == picked && !selected.empty() ? selected : style_.speed_icons[i];
        }
    }

    void ParkBar::update(const UpdateContext&)
    {
        if (speeds_[0]->take_click())
            toggle_pause();
        for (std::size_t i = 1; i < speeds_.size(); ++i)
        {
            if (speeds_[i]->take_click())
                speed_ = simulation::playing(speed_, static_cast<simulation::Speed>(i - 1));
        }
        show_speed();
    }

    void ParkBar::draw(DrawContext& context) const
    {
        const Rect r = rect();
        const float divider_end = r.x0 + kRightStart;
        draw_skin(context.list, context.skins.find(style_.left_frame), { r.x0, r.y0, r.x0 + park_bar::kLeftWidth, r.y1 });
        draw_skin(context.list, context.skins.find(style_.divider), { r.x0 + park_bar::kLeftWidth, r.y0, divider_end, r.y1 });
        draw_skin(context.list, context.skins.find(style_.right_frame), { divider_end, r.y0, r.x1, r.y1 });

        if (!rating_)
            return;
        // The rating fills its bar from the left, its colour going from low to high with it.
        const float t = std::clamp(*rating_, 0.0f, 1.0f);
        const Rect bar = rating_area_->rect();
        context.list.fill(bar, style_.rating_back);
        context.list.fill({ bar.x0, bar.y0, bar.x0 + std::round(bar.width() * t), bar.y1 }, mixed(style_.rating_low, style_.rating_high, t));
    }

} // namespace rct3::ui
