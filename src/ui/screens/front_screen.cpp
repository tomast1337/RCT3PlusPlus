// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/front_screen.hpp"

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
        using A = FrontAction;
        using M = FrontMenu;

        constexpr std::array<FrontEntry, 3> kMain = { {
            { "FrontScreen_Play_TT", "FrontScreen_Play_Help", "FrontScreen_Icon_Play", A::OpenMenu, M::Play },
            { "FrontScreen_Tools_TT", "FrontScreen_Tools_Help", "FrontScreen_Icon_Designers", A::OpenMenu, M::Tools },
            { "FrontScreen_Options_TT", "FrontScreen_Options_Help", "FrontScreen_Icon_Options", A::Options },
        } };
        constexpr std::array<FrontEntry, 4> kPlay = { {
            { "FrontScreen_Tutorial_TT", "FrontScreen_Tutorial_Help", "FrontScreen_Icon_Tutorial", A::OpenMenu, M::Tutorials },
            { "FrontScreen_Career_TT", "FrontScreen_Profile_Help", "FrontScreen_Icon_Career", A::OpenMenu, M::Campaigns },
            { "FrontScreen_Sandbox_TT", "FrontScreen_Sandbox_Help", "FrontScreen_Icon_Sandbox", A::OpenMenu, M::Sandbox },
            { "FrontScreen_LoadScenario_TT", "FrontScreen_LoadScenario_Help", "FrontScreen_Icon_StartSandbox", A::OpenMenu, M::CustomScenario },
        } };
        constexpr std::array<FrontEntry, 4> kTools = { {
            { "FrontScreen_PeopleDesigner_TT", "FrontScreen_PeopleDesigner_Help", "FrontScreen_Icon_PersonDesigner", A::PeopleDesigner },
            { "FrontScreen_ScenarioEditor_TT", "FrontScreen_ScenarioEditor_Help", "FrontScreen_Icon_ScenarioEditor", A::ScenarioEditor },
            { "FrontScreen_CoasterDesigner_TT", "FrontScreen_CoasterDesigner_Help", "FrontScreen_Icon_CoasterEditor", A::CoasterDesigner },
            { "FrontScreen_BuildingDesigner_TT", "FrontScreen_BuildingDesigner_Help", "FrontScreen_Icon_BuildingDesigner", A::BuildingDesigner },
        } };
        constexpr std::array<FrontEntry, 2> kSandbox = { {
            { "FrontScreen_New_TT", "FrontScreen_New_Help", "FrontScreen_Icon_NewScenario", A::NewSandbox },
            { "FrontScreen_Load_TT", "FrontScreen_Load_Help", "FrontScreen_Icon_Load", A::LoadSandbox },
        } };
        constexpr std::array<FrontEntry, 2> kCustomScenario = { {
            { "FrontScreen_New_TT", "FrontScreen_NewCustomScenario_Help", "FrontScreen_Icon_NewScenario", A::NewCustomScenario },
            { "FrontScreen_Load_TT", "FrontScreen_LoadCustomScenario_Help", "FrontScreen_Icon_Load", A::LoadCustomScenario },
        } };
        constexpr std::array<FrontEntry, 3> kCampaigns = { {
            { "RCTName_short", "", "FrontScreen_Icon_RCT3", A::Campaign, M::Main, 0 },
            { "AOP_Name_0", "", "FrontScreen_Icon_AOP0", A::Campaign, M::Main, 1 },
            { "AOP_Name_1", "", "FrontScreen_Icon_AOP1", A::Campaign, M::Main, 2 },
        } };
        constexpr std::array<FrontEntry, 3> kTutorials = { {
            { "RCTName_short", "", "FrontScreen_Icon_RCT3", A::Tutorial, M::Main, 0 },
            { "AOP_Name_0", "", "FrontScreen_Icon_AOP0", A::Tutorial, M::Main, 1 },
            { "AOP_Name_1", "", "FrontScreen_Icon_AOP1", A::Tutorial, M::Main, 2 },
        } };

        const std::array<FrontMenuInfo, kFrontMenuCount> kMenus = { {
            { M::Main, kMain, "FrontScreen_MainMenu" },
            { M::Main, kPlay, "FrontScreen_PlayMenu" },
            { M::Main, kTools, "FrontScreen_ToolMenu" },
            { M::Play, kSandbox, "FrontScreen_SandboxMenu" },
            { M::Play, kCustomScenario, "FrontScreen_MainMenu" }, // As in the game
            { M::Play, kCampaigns, "FrontScreen_SelectCampaign" },
            { M::Play, kTutorials, "FrontScreen_SelectTutorial" },
        } };

        // The artwork's skins.
        constexpr std::string_view kBandTopSkin = "FrontScreen_TopLeft";
        constexpr std::string_view kBandBottomSkin = "FrontScreen_BottomLeft";
        constexpr std::string_view kStripRightSkin = "FrontScreen_BottomRight";
        constexpr std::string_view kLogoSkin = "FrontScreen_RCT3Logo";
        constexpr std::array<std::string_view, 2> kChildrenSkins = { "FrontScreen_FrontEndLogoUT", "FrontScreen_FrontEndLogo" }; ///< The newer picture first
        constexpr std::array<std::string_view, 5> kArcSkins = { "FrontScreen_OverCircle_P1", "FrontScreen_OverCircle_P2", "FrontScreen_OverCircle_P3", "FrontScreen_OverCircle_P4", "FrontScreen_OverCircle_P5" };
        constexpr std::string_view kButtonSkin = "FrontScreen_IconCircle_New";
        constexpr std::string_view kPatchSkin = "FrontScreen_IconCircle_Back";
        constexpr std::string_view kBandEndSkin = "FrontScreen_Triangle";
        constexpr Color kBandColour { 0xF7, 0xF2, 0xF2, 255 };
        constexpr std::string_view kSmallButtonSkin = "MenuButtonSmall";
        constexpr std::string_view kBackIcon = "File_CareerMap";
        constexpr std::string_view kQuitIcon = "File_Quit";
        constexpr std::string_view kBadgeSkin = "FrontScreen_AOPBadge_";
        // Languages whose logo is drawn in their own script.
        constexpr std::array<std::string_view, 2> kOwnLogoLanguages = { "Chinese", "SChinese" };

        constexpr std::string_view kBackTitle = "FrontScreen_Back_TT";
        constexpr std::string_view kBackHelp = "FrontScreen_Back_Help";
        constexpr std::string_view kQuitTitle = "FrontScreen_Exit_TT";
        constexpr std::string_view kQuitHelp = "FrontScreen_Exit_Help";
        constexpr std::string_view kExpansionName = "AOP_Name_";
        constexpr std::string_view kExpansionInstalled = "AOP_InstalledMessage";

        constexpr float kSwellFactor = 1.46f; ///< How big a picture grows under the pointer
        constexpr float kDisabledAlpha = 0.6f;
        constexpr float kDisabledBoxAlpha = 0.5f; // A disabled button's box and name
        constexpr Color kLabelColour { 0, 0, 0, 255 };
        constexpr Color kHelpColour { 0, 0, 0, 255 };

        class FrontBadge : public Widget
        {
        public:
            FrontBadge(std::string name, std::string skin)
                : Widget(std::move(name))
                , skin_(std::move(skin))
            {
            }
            [[nodiscard]] bool takes_pointer() const override
            {
                return true; // For its tooltip
            }
            void draw(DrawContext& context) const override
            {
                draw_skin(context.list, context.skins.find(skin_), rect());
            }

        private:
            std::string skin_;
        };

        // A skin at its own size, scaled with the design frame, from a design corner.
        void draw_at_size(DrawContext& context, const fs::Frame& frame, std::string_view name, Point corner)
        {
            const SkinRef* skin = context.skins.find(name);
            if (skin == nullptr)
                return;
            const auto [w, h] = skin->size();
            draw_skin(context.list, skin, frame.stretched({ corner.x, corner.y, corner.x + w, corner.y + h }));
        }

        Color faded(Color c, float alpha)
        {
            c.a = static_cast<std::uint8_t>(std::lround(static_cast<float>(c.a) * std::clamp(alpha, 0.0f, 1.0f)));
            return c;
        }

        std::size_t index(FrontMenu menu)
        {
            return static_cast<std::size_t>(menu);
        }

        Placement placed(const Rect& r, const Rect& parent)
        {
            return top_left(r.x0 - parent.x0, r.y0 - parent.y0, r.width(), r.height());
        }

    } // namespace

    const FrontMenuInfo& front_menu(FrontMenu menu)
    {
        return kMenus.at(index(menu));
    }

    void FrontButton::draw(DrawContext& context) const
    {
        const SkinState s = state();
        const Color tint = s == SkinState::Disabled ? faded(kWhite, kDisabledAlpha) : kWhite;
        draw_skin(context.list, context.skins.find(skin), rect(), s == SkinState::Pressed ? kPressedTint : tint);
        if (!icon.empty())
            draw_skin(context.list, context.skins.find(icon), face(), tint);
    }

    void draw_front_bands(DrawContext& context, const fs::Frame& frame)
    {
        draw_skin(context.list, context.skins.find(kBandTopSkin), frame.stretched(fs::kBandTop));
        draw_skin(context.list, context.skins.find(kBandBottomSkin), frame.stretched(fs::kBandBottom));
        draw_skin(context.list, context.skins.find(kStripRightSkin), frame.stretched(fs::kStripRight));
    }

    void draw_front_arc(DrawContext& context, const fs::Frame& frame, std::string_view logo)
    {
        for (std::size_t i = 0; i < kArcSkins.size(); ++i)
            draw_at_size(context, frame, kArcSkins[i], fs::kArcPieces.at(i));
        draw_at_size(context, frame, context.skins.find(logo) != nullptr ? logo : kLogoSkin, fs::kLogo);
    }

    void draw_front_help(DrawContext& context, const fs::Frame& frame, std::u16string_view title, std::u16string_view text)
    {
        if (const Font* f = context.fonts.get(FontRole::ScaledLarge))
            draw_text(context.list, *f, title, frame.stretched(fs::kHelpTitle), { kHelpColour, Align::Centre, Align::Centre, false });
        if (const Font* f = context.fonts.get(FontRole::ScaledSmall))
            draw_text(context.list, *f, text, frame.stretched(fs::kHelpText), { kHelpColour, Align::Centre, Align::Centre, false });
    }

    std::string front_logo(std::string_view language)
    {
        std::string logo(kLogoSkin);
        if (std::ranges::find(kOwnLogoLanguages, language) != kOwnLogoLanguages.end())
            logo += "_" + std::string(language);
        return logo;
    }

    std::array<bool, kExpansionCount> named_expansions(const StringTable& strings)
    {
        std::array<bool, kExpansionCount> named { };
        for (int n = 0; n < kExpansionCount; ++n)
            named.at(static_cast<std::size_t>(n)) = !strings.get(std::string(kExpansionName) + std::to_string(n)).empty();
        return named;
    }

    FrontScreen::FrontScreen(std::string name, const StringTable& strings, std::array<bool, kExpansionCount> expansions, std::string_view language)
        : Widget(std::move(name))
        , strings_(strings)
        , logo_(front_logo(language))
    {
        for (std::size_t m = 0; m < kFrontMenuCount; ++m)
        {
            const auto& entries = kMenus[m].entries;
            for (std::size_t i = 0; i < entries.size(); ++i)
            {
                const FrontEntry& e = entries[i];
                auto& b = add<FrontButton>("menu" + std::to_string(m) + "_" + std::to_string(i), std::string(kButtonSkin), std::string(e.icon));
                b.enabled = e.pack == 0 || expansions.at(static_cast<std::size_t>(e.pack - 1));
                // Only the main menu's buttons fly in at first; the others wait out of sight.
                const bool main = m == index(FrontMenu::Main);
                b.visible = main;
                buttons_[m].push_back({ &b, &e, main ? fs::kStart : fs::kParked, !main });
            }
        }
        back_ = &add<FrontButton>("back", std::string(kSmallButtonSkin), std::string(kBackIcon));
        quit_ = &add<FrontButton>("quit", std::string(kSmallButtonSkin), std::string(kQuitIcon));
        back_->visible = false;
        for (std::size_t p = 0; p < expansions.size(); ++p)
        {
            if (!expansions[p])
                continue;
            const std::string n = std::to_string(p);
            auto& badge = add<FrontBadge>("badge" + n, std::string(kBadgeSkin) + n);
            badge.tooltip = std::u16string(strings.get(kExpansionInstalled)) + u" " + std::u16string(strings.get(std::string(kExpansionName) + n));
            badges_.push_back({ &badge, std::string(kBadgeSkin) + n });
        }
    }

    void FrontScreen::open(FrontMenu menu)
    {
        if (menu == current_ || index(menu) >= kFrontMenuCount)
            return;
        current_ = menu;
        clear_labels();
    }

    void FrontScreen::back()
    {
        if (current_ != FrontMenu::Main)
            open(front_menu(current_).parent);
    }

    std::optional<FrontRequest> FrontScreen::take_request()
    {
        if (requests_.empty())
            return std::nullopt;
        const FrontRequest r = requests_.front();
        requests_.erase(requests_.begin());
        return r;
    }

    FrontButton* FrontScreen::button(FrontMenu menu, std::size_t index_in_menu) const
    {
        const auto& list = buttons_.at(index(menu));
        return index_in_menu < list.size() ? list[index_in_menu].button : nullptr;
    }

    std::u16string_view FrontScreen::label(std::size_t i) const
    {
        return i < labels_.size() ? std::u16string_view(labels_[i]) : std::u16string_view { };
    }

    float FrontScreen::label_alpha(std::size_t i) const
    {
        return i < label_alpha_.size() ? label_alpha_[i] : 0;
    }

    bool FrontScreen::on_key(const KeyPress& key)
    {
        if (key.key == Key::Escape && current_ != FrontMenu::Main)
        {
            back();
            return true;
        }
        return false;
    }

    fs::Polar FrontScreen::goal(FrontMenu menu, std::size_t i) const
    {
        const fs::Polar home { fs::menu_angle(i), fs::kMenuRadius };
        if (menu == current_)
            return home;
        if (current_ != FrontMenu::Main && menu == front_menu(current_).parent)
            return { home.angle, home.radius * fs::kParentRadiusFactor };
        return fs::kParked;
    }

    void FrontScreen::settle()
    {
        for (std::size_t m = 0; m < kFrontMenuCount; ++m)
        {
            for (std::size_t i = 0; i < buttons_[m].size(); ++i)
            {
                MenuButton& mb = buttons_[m][i];
                mb.at = goal(static_cast<FrontMenu>(m), i);
                mb.arrived = true;
            }
        }
        // The labels' text comes with the next update.
        for (std::size_t i = 0; i < buttons_[index(current_)].size() && i < label_alpha_.size(); ++i)
            label_alpha_[i] = 1;
    }

    void FrontScreen::clear_labels()
    {
        for (auto& l : labels_)
            l.clear();
        label_alpha_.fill(0);
    }

    void FrontScreen::update(const UpdateContext& context)
    {
        const fs::Frame frame = fs::frame_for(rect());
        const float size = fs::kMenuButtonSize * frame.round;

        // Clicks first, so a menu opened now starts moving this frame.
        for (const MenuButton& mb : buttons_[index(current_)])
        {
            if (!mb.button->take_click())
                continue;
            if (mb.entry->action == FrontAction::OpenMenu)
                open(mb.entry->menu);
            else
                requests_.push_back({ mb.entry->action, mb.entry->pack });
            break;
        }
        for (auto& list : buttons_)
        {
            for (const MenuButton& mb : list)
                (void)mb.button->take_click(); // Buttons on their way out do nothing
        }
        if (back_->take_click())
            back();
        if (quit_->take_click())
            requests_.push_back({ FrontAction::Quit, 0 });

        for (std::size_t m = 0; m < kFrontMenuCount; ++m)
        {
            const auto menu = static_cast<FrontMenu>(m);
            for (std::size_t i = 0; i < buttons_[m].size(); ++i)
            {
                MenuButton& mb = buttons_[m][i];
                mb.arrived = fs::ease(mb.at, goal(menu, i), context.elapsed);
                const Rect r = fs::button_rect(frame, fs::on_ring(mb.at.angle, mb.at.radius), fs::kMenuButtonSize);
                mb.button->placement = placed(r, rect());
                mb.button->swell = size * (kSwellFactor - 1) / 2;
                const bool open = menu == current_ || (current_ != FrontMenu::Main && menu == front_menu(current_).parent);
                mb.button->visible = open || !mb.arrived;
                if (menu == current_ && mb.arrived && i < labels_.size())
                {
                    if (labels_[i].empty())
                        labels_[i] = strings_.get(mb.entry->label_key);
                    label_alpha_[i] = std::min(1.0f, label_alpha_[i] + context.elapsed * fs::kLabelFadeRate);
                }
            }
        }

        const std::array<FrontButton*, 2> small = { back_, quit_ };
        for (std::size_t i = 0; i < small.size(); ++i)
        {
            const float angle = fs::kSmallAngles.at(i);
            small[i]->placement = placed(fs::button_rect(frame, fs::on_ring(angle, fs::kSmallRadius), fs::kSmallButtonSize), rect());
        }
        back_->visible = current_ != FrontMenu::Main;
        for (std::size_t i = 0; i < badges_.size(); ++i)
            badges_[i].widget->placement = placed(fs::badge_rect(frame, i), rect());

        // The help lines follow the pointer; without a button under it, the menu's name.
        help_title_ = strings_.get(front_menu(current_).title_key);
        help_text_.clear();
        const auto show = [&](std::string_view title, std::string_view help) {
            help_title_ = strings_.get(title);
            help_text_ = help.empty() ? std::u16string { } : std::u16string(strings_.get(help));
        };
        for (const MenuButton& mb : buttons_[index(current_)])
        {
            if (mb.button->visible && mb.button->hovered())
                show(mb.entry->label_key, mb.entry->help_key);
        }
        if (back_->visible && back_->hovered())
            show(kBackTitle, kBackHelp);
        if (quit_->hovered())
            show(kQuitTitle, kQuitHelp);
    }

    void FrontScreen::draw(DrawContext& context) const
    {
        const fs::Frame frame = fs::frame_for(rect());
        draw_front_bands(context, frame);

        // The boxes beside the buttons that have arrived: a light band from a white ring behind the button,
        // under its name and on under the arc, then the name. The arc comes after and covers the band's end.
        const auto& current = buttons_[index(current_)];
        const Font* label_font = context.fonts.get(FontRole::ScaledLarge);
        for (std::size_t i = 0; i < current.size() && i < labels_.size(); ++i)
        {
            if (label_alpha_[i] <= 0)
                continue;
            const float alpha = current[i].button->enabled ? label_alpha_[i] : label_alpha_[i] * kDisabledBoxAlpha;
            const fs::LabelBox box = fs::label_box(frame, i);
            context.list.fill(box.band, faded(kBandColour, alpha));
            draw_skin(context.list, context.skins.find(kBandEndSkin), box.end, faded(kWhite, alpha));
            draw_skin(context.list, context.skins.find(kPatchSkin), box.patch, faded(kWhite, alpha));
            if (label_font != nullptr)
                draw_text(context.list, *label_font, labels_[i], box.text, { faded(kLabelColour, alpha), Align::Centre, Align::Centre, true });
        }
        draw_front_arc(context, frame, logo_);
        if (current_ == FrontMenu::Main)
        {
            for (const std::string_view name : kChildrenSkins)
            {
                if (const SkinRef* skin = context.skins.find(name))
                {
                    draw_skin(context.list, skin, frame.stretched(fs::kChildren));
                    break;
                }
            }
        }
        draw_front_help(context, frame, help_title_, help_text_);
    }

} // namespace rct3::ui
