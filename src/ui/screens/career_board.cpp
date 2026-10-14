// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/career_board.hpp"

#include "ui/skin_library.hpp"
#include "ui/text_layout.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace rct3::ui
{

    namespace
    {

        namespace fs = front_screen;
        namespace cm = career_board;
        using career::ScenarioStatus;

        constexpr std::string_view kQuestionName = "career_board_question";
        constexpr std::string_view kNumber = "CareerMap_ScenarioDD";
        constexpr std::u16string_view kNumberField = u"%NUMBER%";
        constexpr std::string_view kNeedLevels = "CareerMap_MustCompleteNLevels";
        constexpr std::string_view kNeedEntrepreneur = "CareerMap_MustCompleteEntrepreneur";
        constexpr std::string_view kNeedTycoon = "CareerMap_MustCompleteTycoon";
        constexpr std::string_view kNewName = "CareerMap_NewGame_TT";
        constexpr std::string_view kResumeName = "CareerMap_ResumeGame_TT";
        constexpr std::string_view kLoadName = "CareerMap_LoadGame_TT";
        constexpr std::string_view kMenuName = "CareerMap_Quit_TT";
        constexpr std::string_view kQuitName = "FrontScreen_Exit_TT";
        constexpr std::string_view kYes = "SMB_Label_Yes";
        constexpr std::string_view kNo = "SMB_Label_No";
        constexpr std::size_t kYesAnswer = 0;

        struct Confirm
        {
            std::string_view title;
            std::string_view text;
        };
        constexpr std::array<Confirm, 3> kConfirms = { {
            { "CareerMap_Title_ConfirmNew", "CareerMap_Message_ConfirmNew" },
            { "CareerMap_Title_ConfirmResume", "CareerMap_Message_ConfirmResume" },
            { "CareerMap_Title_ConfirmExit", "CareerMap_Message_ConfirmExit" },
        } };

        /// A scenario's round button, with its mark in the corner. It grows whole under the pointer, and
        /// looks the same when locked.
        class ScenarioButton : public FrontButton
        {
        public:
            using FrontButton::FrontButton;
            std::string mark;

            void draw(DrawContext& context) const override
            {
                const Rect r = face();
                draw_skin(context.list, context.skins.find(skin), r, state() == SkinState::Pressed ? kPressedTint : kWhite);
                draw_icon(context, kWhite);
                const float x = r.x0 + cm::kMarkAt.x * r.width();
                const float y = r.y0 + cm::kMarkAt.y * r.height();
                draw_skin(context.list, context.skins.find(mark), { x, y, x + cm::kMarkSize * r.width(), y + cm::kMarkSize * r.height() });
            }
        };

        Placement placed(const Rect& r, const Rect& parent)
        {
            return top_left(r.x0 - parent.x0, r.y0 - parent.y0, r.width(), r.height());
        }

        std::string mark_of(const CareerBoardStyle& style, ScenarioStatus status)
        {
            switch (status)
            {
                case ScenarioStatus::Locked:
                    return style.locked;
                case ScenarioStatus::Open:
                    return style.unlocked;
                case ScenarioStatus::Apprentice:
                    return style.hats[0];
                case ScenarioStatus::Entrepreneur:
                    return style.hats[1];
                case ScenarioStatus::Tycoon:
                    return style.hats[2];
            }
            return { };
        }

        float text_height(const UpdateContext& context, FontRole role, std::u16string_view text, float width)
        {
            const Font* font = context.fonts != nullptr ? context.fonts->get(role) : nullptr;
            if (font == nullptr || text.empty())
                return 0;
            return static_cast<float>(break_lines(*font, text, width).size()) * font->line_height();
        }

    } // namespace

    float career_board::scenario_angle(std::size_t index)
    {
        return kScenarioFirstAngle + kScenarioAngleStep * static_cast<float>(index);
    }

    float career_board::pop(float time)
    {
        const float t = std::clamp(time / kPopTime, 0.0f, 1.0f);
        return 1 + (kPopFactor - 1) * (1 - std::sqrt(t));
    }

    CareerBoard::CareerBoard(std::string name, CareerBoardStyle style, const StringTable& strings, std::vector<CareerBoardScenario> scenarios, std::size_t picked)
        : Widget(std::move(name))
        , style_(std::move(style))
        , strings_(strings)
        , scenarios_(std::move(scenarios))
    {
        for (std::size_t i = 0; i < scenarios_.size(); ++i)
        {
            auto& b = add<ScenarioButton>("scenario" + std::to_string(i), style_.scenario_button, scenarios_[i].info.icon);
            b.mark = mark_of(style_, scenarios_[i].status);
            b.icon_own_size = true;
            b.enabled = career::is_open(scenarios_[i].status);
            buttons_.push_back(&b);
        }
        new_ = &add<FrontButton>("new", style_.small_button, style_.new_icon);
        resume_ = &add<FrontButton>("resume", style_.small_button, style_.resume_icon);
        load_ = &add<FrontButton>("load", style_.small_button, style_.load_icon);
        menu_ = &add<FrontButton>("menu", style_.small_button, style_.menu_icon);
        quit_ = &add<FrontButton>("quit", style_.small_button, style_.quit_icon);
        // Nowhere until the first layout, so nothing is under the pointer before then.
        for (FrontButton* w : { new_, resume_, load_, menu_, quit_ })
            w->placement = top_left(0, 0, 0, 0);
        for (FrontButton* b : buttons_)
            b->placement = top_left(0, 0, 0, 0);
        picked_ = picked < scenarios_.size() && career::is_open(scenarios_[picked].status) ? picked : 0;
        set_can_resume(picked_, picked_ < scenarios_.size() && scenarios_[picked_].can_resume);
    }

    std::u16string CareerBoard::text(std::string_view key) const
    {
        return std::u16string(strings_.get(key));
    }

    std::u16string CareerBoard::numbered(std::string_view key, std::size_t number) const
    {
        std::u16string s = text(key);
        const std::u16string_view field = kNumberField;
        if (const auto at = s.find(field); at != std::u16string::npos)
        {
            const std::string n = std::to_string(number);
            s.replace(at, field.size(), std::u16string(n.begin(), n.end()));
        }
        return s;
    }

    void CareerBoard::pick(std::size_t index)
    {
        if (index >= scenarios_.size() || !career::is_open(scenarios_[index].status) || index == picked_)
            return;
        picked_ = index;
        since_pick_ = 0;
    }

    void CareerBoard::set_can_resume(std::size_t index, bool can)
    {
        if (index < scenarios_.size())
            scenarios_[index].can_resume = can;
    }

    std::optional<std::size_t> CareerBoard::take_picked()
    {
        return std::exchange(picked_out_, std::nullopt);
    }

    std::optional<std::size_t> CareerBoard::take_new()
    {
        return std::exchange(new_out_, std::nullopt);
    }

    std::optional<std::size_t> CareerBoard::take_resume()
    {
        return std::exchange(resume_out_, std::nullopt);
    }

    bool CareerBoard::take_load()
    {
        return std::exchange(load_out_, false);
    }

    bool CareerBoard::take_menu()
    {
        return std::exchange(menu_out_, false);
    }

    bool CareerBoard::take_quit()
    {
        return std::exchange(quit_out_, false);
    }

    void CareerBoard::ask(Asking what)
    {
        if (question_ != nullptr || parent() == nullptr)
            return;
        asking_ = what;
        const Confirm& c = kConfirms.at(static_cast<std::size_t>(what));
        const std::vector<QuestionAnswer> answers { { text(kYes), style_.question.confirm_icon }, { text(kNo), style_.question.cancel_icon } };
        question_ = &parent()->add<QuestionBox>(std::string(kQuestionName), style_.question, text(c.text), answers);
        question_->title = text(c.title);
    }

    bool CareerBoard::on_key(const KeyPress& key)
    {
        if (key.key != Key::Escape)
            return false;
        ask(Asking::Menu);
        return true;
    }

    void CareerBoard::hover_text()
    {
        const auto show = [&](std::u16string title, std::u16string help = { }) {
            help_title_ = std::move(title);
            help_text_ = std::move(help);
        };
        const std::u16string picked_title = picked_ < scenarios_.size() ? scenarios_[picked_].info.title : std::u16string { };
        show(numbered(kNumber, picked_ + 1), picked_title);
        for (std::size_t i = 0; i < buttons_.size(); ++i)
        {
            if (!buttons_[i]->hovered())
                continue;
            const career::Lock& lock = scenarios_[i].lock;
            switch (lock.kind)
            {
                case career::Lock::Kind::Open:
                    show(numbered(kNumber, i + 1), scenarios_[i].info.title);
                    break;
                case career::Lock::Kind::Levels:
                    show(numbered(kNumber, i + 1), numbered(kNeedLevels, lock.levels));
                    break;
                case career::Lock::Kind::Entrepreneur:
                    show(numbered(kNumber, i + 1), text(kNeedEntrepreneur));
                    break;
                case career::Lock::Kind::Tycoon:
                    show(numbered(kNumber, i + 1), text(kNeedTycoon));
                    break;
            }
        }
        const std::array<std::pair<const FrontButton*, std::string_view>, 5> named = { {
            { new_, kNewName },
            { resume_, kResumeName },
            { load_, kLoadName },
            { menu_, kMenuName },
            { quit_, kQuitName },
        } };
        for (const auto& [button, key] : named)
        {
            if (button->visible && button->hovered())
                show(text(key), picked_title);
        }
    }

    void CareerBoard::layout(const UpdateContext& context)
    {
        const fs::Frame frame = fs::frame_for(rect());
        for (std::size_t i = 0; i < buttons_.size(); ++i)
        {
            const bool picked = i == picked_;
            const float size = cm::kScenarioSize * (picked ? cm::kPickedFactor : 1);
            auto& b = static_cast<ScenarioButton&>(*buttons_[i]);
            b.placement = placed(fs::button_rect(frame, fs::on_ring(cm::scenario_angle(i), cm::kScenarioRadius), size), rect());
            const float swell = picked ? cm::kPickedSwell : fs::kSwellFactor;
            b.swell = size * frame.round * (swell - 1) / 2;
            b.swell_time = (swell - 1) / fs::kSwellRate;
            b.icon_scale = (picked ? cm::kPickedIconScale : cm::kIconScale) * frame.round;
        }

        // The panel holds the name, then the description, then the buttons; a long description pushes the
        // panel's bottom down.
        const float wrap = (cm::kTextRight - cm::kTextLeft) * frame.sx;
        const std::u16string_view title_text = picked_ < scenarios_.size() ? std::u16string_view(scenarios_[picked_].info.title) : std::u16string_view { };
        const std::u16string_view description = picked_ < scenarios_.size() ? std::u16string_view(scenarios_[picked_].info.description) : std::u16string_view { };
        const float title_height = text_height(context, FontRole::ScaledLarge, title_text, wrap) + cm::kTitleGap * frame.sy;
        const float button = cm::kButtonSize * frame.round;
        const float room = (cm::kPanelBottom - cm::kPanelTop) * frame.sy - title_height;
        const float extra = std::max(0.0f, text_height(context, FontRole::ScaledSmall, description, wrap) + 2 * button - room);
        const Point top_left = frame.at({ cm::kPanelLeft, cm::kPanelTop });
        const Point bottom_right = frame.at({ cm::kPanelRight, cm::kPanelBottom });
        panel_ = { top_left.x, top_left.y, bottom_right.x, bottom_right.y + extra };
        const float text_left = frame.at({ cm::kTextLeft, 0 }).x;
        const float text_right = frame.at({ cm::kTextRight, 0 }).x;
        title_box_ = { text_left, panel_.y0, text_right, panel_.y0 + title_height };
        description_box_ = { text_left, title_box_.y1, text_right, panel_.y1 };

        const std::array<FrontButton*, 3> under = { new_, resume_, load_ };
        const float button_bottom = panel_.y1 - cm::kButtonUp * button;
        for (std::size_t i = 0; i < under.size(); ++i)
        {
            const float x = panel_.x1 - cm::kButtonSteps.at(i) * cm::kButtonStep * button;
            under[i]->placement = placed({ x, button_bottom - button, x + button, button_bottom }, rect());
        }
        const std::array<FrontButton*, 2> small = { menu_, quit_ };
        for (std::size_t i = 0; i < small.size(); ++i)
            small[i]->placement = placed(fs::button_rect(frame, fs::on_ring(cm::kSmallAngles.at(i), fs::kSmallRadius), fs::kSmallButtonSize), rect());
        for (FrontButton* b : { new_, resume_, load_, menu_, quit_ })
        {
            b->swell = button * (fs::kSwellFactor - 1) / 2;
            b->swell_time = (fs::kSwellFactor - 1) / fs::kSwellRate;
        }
    }

    void CareerBoard::update(const UpdateContext& context)
    {
        since_pick_ += context.elapsed;
        if (question_ != nullptr)
        {
            if (const auto answer = question_->take_answer())
            {
                question_ = nullptr; // It closes with the answer
                if (*answer == kYesAnswer)
                {
                    switch (asking_)
                    {
                        case Asking::New:
                            new_out_ = picked_;
                            break;
                        case Asking::Resume:
                            resume_out_ = picked_;
                            break;
                        case Asking::Menu:
                            menu_out_ = true;
                            break;
                    }
                }
            }
        }
        for (std::size_t i = 0; i < buttons_.size(); ++i)
        {
            if (buttons_[i]->take_click() && i != picked_)
            {
                pick(i);
                picked_out_ = picked_;
            }
        }
        if (new_->take_click())
            ask(Asking::New);
        if (resume_->take_click())
            ask(Asking::Resume);
        if (load_->take_click())
            load_out_ = true;
        if (menu_->take_click())
            ask(Asking::Menu);
        if (quit_->take_click())
            quit_out_ = true;

        resume_->visible = picked_ < scenarios_.size() && scenarios_[picked_].can_resume;
        layout(context);
        hover_text();
    }

    void CareerBoard::draw(DrawContext& context) const
    {
        const fs::Frame frame = fs::frame_for(rect());
        draw_front_bands(context, frame);

        // The large picture in the bottom-left corner, the front piece over it settling from a little big.
        if (picked_ < scenarios_.size())
        {
            const CareerScenarioInfo& info = scenarios_[picked_].info;
            const SkinRef* picture = context.skins.find(info.picture);
            const SkinRef* front = context.skins.find(info.picture_front);
            if (picture == nullptr)
                picture = context.skins.find(style_.no_picture);
            const float scale = front != nullptr ? cm::kPictureScale : 1;
            const float bottom = frame.at({ 0, fs::kDesignHeight - cm::kPictureBottom }).y;
            const auto rect_of = [&](const SkinRef& skin, float k) {
                const auto [w, h] = skin.size();
                const float width = w * frame.sx * scale * k;
                const float height = h * frame.sy * scale * k;
                const float base_width = w * frame.sx * scale;
                const float x = rect().x0 + (base_width - width) / 2;
                return Rect { x, bottom - height, x + width, bottom };
            };
            if (picture != nullptr)
                draw_skin(context.list, picture, rect_of(*picture, 1));
            if (front != nullptr)
                draw_skin(context.list, front, rect_of(*front, cm::pop(since_pick_)));
        }

        draw_skin(context.list, context.skins.find(style_.panel), panel_);
        if (picked_ < scenarios_.size())
        {
            const CareerScenarioInfo& info = scenarios_[picked_].info;
            if (const Font* f = context.fonts.get(FontRole::ScaledLarge))
                draw_text(context.list, *f, info.title, title_box_, { style_.text_colour, Align::Start, Align::Centre, true });
            if (const Font* f = context.fonts.get(FontRole::ScaledSmall))
                draw_text(context.list, *f, info.description, description_box_, { style_.text_colour, Align::Start, Align::Start, true });
        }
        draw_front_help(context, frame, help_title_, help_text_);
    }

} // namespace rct3::ui
