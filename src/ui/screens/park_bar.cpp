// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/park_bar.hpp"

#include "strings/unicode.hpp"
#include "ui/save_text.hpp"
#include "ui/temperature_text.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace rct3::ui
{

    namespace
    {

        constexpr std::string_view kMonth = "Month"; ///< Followed by the month's number from 0
        constexpr std::string_view kYear = "Year";
        constexpr std::string_view kUnlimitedCash = "Cash_Unlimited";
        constexpr std::string_view kObjectiveHelp = "PID_TT_ObjectiveTabPopUp";
        constexpr std::array<std::string_view, 4> kSpeedHelp = { "Playing_PauseHelp", "Playing_PlayHelp", "Playing_FastForwardHelp", "Playing_SuperFastForwardHelp" };
        constexpr std::string_view kGuestsHelp = "Toolbar_TT_GuestsInPark";
        constexpr std::string_view kDayModeHelp = "Toolbar_TT_DayNight";
        constexpr std::string_view kRatingHelp = "Toolbar_TT_ParkRating";
        // Our own labels: the parts are those of our rating model.
        constexpr std::string_view kRatingHappiness = "Rct3pp_RatingHappiness";
        constexpr std::string_view kRatingRides = "Rct3pp_RatingRides";
        constexpr std::string_view kRatingReliability = "Rct3pp_RatingReliability";
        constexpr std::string_view kRatingGuests = "Rct3pp_RatingGuests";
        constexpr std::string_view kRatingLitter = "Rct3pp_RatingLitter";
        constexpr std::string_view kRatingHarder = "Rct3pp_RatingHarder";
        constexpr std::string_view kRatingTotal = "Rct3pp_RatingScore";
        constexpr std::array<std::string_view, 7> kWeatherHelp = { "WeatherType_Sunny_TT", "WeatherType_SunnyCloudy_TT", "WeatherType_Cloudy_TT", "WeatherType_LightRain_TT", "WeatherType_HeavyRain_TT", "WeatherType_Thunderstorm_TT", "WeatherType_Snowstorm_TT" };

        // Where things sit in the bar, left to right: each item's size and the room before it.
        constexpr float kStart = 5;  ///< Before the first item of each part
        constexpr float kWide = 5;   ///< Between buttons and groups
        constexpr float kNarrow = 2; ///< Between readouts
        constexpr float kSmallIcon = 16;
        constexpr std::array<float, 4> kSpeedWidths = { 8, 8, 13, 17 };
        constexpr float kSpeedHeight = 13;
        constexpr float kMoneyWidth = 100;
        constexpr float kNarrowMoneyWidth = 90; ///< While the day mode button shows
        constexpr float kArcRadius = 24;        ///< From the day mode button's middle to each mode's
        constexpr float kArcSpread = 0.8f;      ///< Radians between neighbouring modes in the arc
        constexpr float kDoubleClick = 0.4f;    ///< Seconds within which a second click on the money counts
        constexpr float kGuestsWidth = 40;
        constexpr float kThumbWidth = 12;
        constexpr float kThumbHeight = 15;
        constexpr float kRatingWidth = 25;
        constexpr float kRatingHeight = 10;
        constexpr float kTemperatureWidth = 35;
        constexpr float kWeatherSize = 20;
        constexpr float kArrowWidth = 9;
        constexpr float kDateWidth = 210;
        constexpr float kRightStart = park_bar::kLeftWidth + park_bar::kDividerWidth;

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

    std::u16string park_rating_help(const StringTable& strings, float rating, const std::optional<simulation::RatingParts>& parts)
    {
        std::u16string out(strings.get(kRatingHelp));
        out += ascii(std::format(" {}", std::lround(rating * simulation::kMaxRating)));
        if (!parts)
            return out;
        // One line a part, with its sign; parts that only ever take away are left out while they take nothing.
        const auto line = [&](std::string_view label, std::int32_t value, bool always) {
            if (!always && value == 0)
                return;
            out += u'\n';
            out += strings.get(label);
            out += ascii(std::format(" {:+}", value));
        };
        line(kRatingHappiness, parts->happiness, true);
        line(kRatingRides, parts->rides, true);
        line(kRatingReliability, parts->reliability, true);
        line(kRatingGuests, parts->guests, true);
        line(kRatingLitter, parts->litter, false);
        line(kRatingHarder, parts->harder, false);
        out += u'\n';
        out += strings.get(kRatingTotal);
        out += ascii(std::format(" {}", parts->total));
        return out;
    }

    ParkBarValues park_bar_values(const simulation::ParkState& state, float day_time, const StringTable& strings, const ParkBarUnits& units)
    {
        ParkBarValues v;
        v.speed = state.speed;
        v.money = state.cash ? money_text(units.currency, *state.cash) : std::u16string(strings.get(kUnlimitedCash));
        v.guests = ascii(std::to_string(state.guests));
        v.rating = state.rating;
        if (state.rating)
            v.rating_help = park_rating_help(strings, *state.rating, state.rating_parts);
        if (state.temperature)
            v.temperature = temperature_text(*state.temperature, units.fahrenheit);
        v.weather = state.weather;
        v.forecast = state.forecast;
        const auto minutes = static_cast<std::int32_t>(std::lround(day_time * save::kMinutesPerDay));
        v.date = park_bar_date(strings, state.date.month, state.date.year + 1, minutes, units.twelve_hour);
        return v;
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

        objective_ = &add<Button>("objective", std::string { }, style_.objective_icon);
        objective_->icon_scale = 0.4f;
        objective_->tooltip = strings_.get(kObjectiveHelp);
        day_mode_ = &add<Button>("day_mode", std::string { });
        day_mode_->tooltip = strings_.get(kDayModeHelp);
        day_mode_->visible = false;
        // The modes open in an arc below the button, from Day on the left round to Night on the right.
        constexpr std::array<std::string_view, 3> kChoiceNames = { "park_hours", "day", "night" };
        constexpr std::array<float, 3> kChoiceAngles = { 0, -kArcSpread, kArcSpread };
        for (std::size_t i = 0; i < day_choices_.size(); ++i)
        {
            day_choices_[i] = &day_mode_->add<Button>(std::string(kChoiceNames[i]), std::string { }, style_.day_mode_icons[i]);
            day_choices_[i]->visible = false;
            const float middle = kSmallIcon / 2;
            const float x = std::round(middle + kArcRadius * std::sin(kChoiceAngles[i]));
            const float y = std::round(middle + kArcRadius * std::cos(kChoiceAngles[i]));
            day_choices_[i]->placement = top_left(x - middle, y - middle, kSmallIcon, kSmallIcon);
        }
        for (std::size_t i = 0; i < speeds_.size(); ++i)
        {
            speeds_[i] = &add<Button>(style_.speed_icons[i], std::string { }, style_.speed_icons[i]);
            speeds_[i]->tooltip = strings_.get(kSpeedHelp[i]);
        }
        money_ = &add<Button>("money", std::string { });
        money_->text_colour = style_.text_colour;
        guests_ = &label("guests", Align::Centre);
        guests_->tooltip = strings_.get(kGuestsHelp);
        thumb_down_ = &add<Panel>("thumb_down", style_.thumb_down);
        rating_area_ = &add<Panel>("rating", std::string { });
        thumb_up_ = &add<Panel>("thumb_up", style_.thumb_up);
        place_left();

        float x = kRightStart;
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
        // shortcut: no fireworks, escaped animals or custom family buttons after the date, at the right end, as the
        // park has none of them yet; add them when it does.

        show_speed();
        show({ });
    }

    void ParkBar::place_left()
    {
        const bool day = day_mode_->visible;
        float x = put(*objective_, kStart, kSmallIcon, kSmallIcon);
        // Hidden, the day mode button takes no room.
        x = put(*day_mode_, day ? x + kWide : x, day ? kSmallIcon : 0, kSmallIcon);
        for (std::size_t i = 0; i < speeds_.size(); ++i)
            x = put(*speeds_[i], x + kWide, kSpeedWidths[i], kSpeedHeight);
        x = put(*money_, x + kNarrow, day ? kNarrowMoneyWidth : kMoneyWidth, park_bar::kHeight - park_bar::kLowerEdge);
        x = put(*guests_, x + kNarrow, kGuestsWidth, park_bar::kHeight - park_bar::kLowerEdge);
        x = put(*thumb_down_, x + kWide, kThumbWidth, kThumbHeight);
        x = put(*rating_area_, x + kNarrow, kRatingWidth, kRatingHeight);
        put(*thumb_up_, x + kNarrow, kThumbWidth, kThumbHeight);
    }

    void ParkBar::show(const ParkBarValues& values)
    {
        const bool day = values.day_mode.has_value();
        if (day != day_mode_->visible)
        {
            day_mode_->visible = day;
            for (Button* b : day_choices_)
                b->visible = false;
            place_left();
        }
        if (day)
            day_mode_->icon = style_.day_mode_icons[static_cast<std::size_t>(*values.day_mode)];
        speed_ = values.speed;
        show_speed();
        money_->text = values.money;
        guests_->text = values.guests;
        temperature_->text = values.temperature;
        date_->text = values.date;

        rating_ = values.rating;
        rating_area_->visible = rating_.has_value();
        rating_area_->tooltip = rating_ ? values.rating_help : std::u16string { };

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

    bool ParkBar::covers(Point p) const
    {
        const Rect r = rect();
        const float dx = p.x - (r.x0 + park_bar::kLeftWidth + park_bar::kDividerWidth / 2);
        const float dy = p.y - r.y1;
        return dx * dx + dy * dy > park_bar::kNotchRadius * park_bar::kNotchRadius;
    }

    void ParkBar::update(const UpdateContext& context)
    {
        if (day_mode_->take_click())
        {
            const bool open = !day_choices_[0]->visible;
            for (Button* b : day_choices_)
                b->visible = open;
        }
        for (std::size_t i = 0; i < day_choices_.size(); ++i)
        {
            if (!day_choices_[i]->take_click())
                continue;
            asked_day_ = static_cast<simulation::DayMode>(i);
            for (Button* b : day_choices_)
                b->visible = false;
        }
        since_money_click_ += context.elapsed;
        if (money_->take_click())
        {
            asked_finances_ = asked_finances_ || since_money_click_ <= kDoubleClick;
            // A third click starts a new pair.
            since_money_click_ = since_money_click_ <= kDoubleClick ? kDoubleClick + 1 : 0;
        }

        const simulation::GameSpeed from = asked_.value_or(speed_);
        if (speeds_[0]->take_click())
            asked_ = simulation::pause_toggled(from);
        for (std::size_t i = 1; i < speeds_.size(); ++i)
        {
            if (speeds_[i]->take_click())
                asked_ = simulation::playing(from, static_cast<simulation::Speed>(i - 1));
        }
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
