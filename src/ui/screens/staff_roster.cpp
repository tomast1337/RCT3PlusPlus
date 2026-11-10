// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/staff_roster.hpp"

#include "simulation/staff_care.hpp"
#include "strings/unicode.hpp"
#include "ui/money_text.hpp"
#include "ui/screens/person_info.hpp"
#include "ui/widgets/menu.hpp"
#include "ui/widgets/panel.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace rct3::ui
{

    namespace
    {
        constexpr std::size_t kHires = 5;
        /// The hire buttons: the job each hires, its picture and help.
        struct Hire
        {
            save::StaffKind kind = save::StaffKind::Handyman;
            std::string_view icon;
            std::string_view help;
        };
        constexpr std::array<Hire, kHires> kHireButtons = { {
            { save::StaffKind::Handyman, "Staff_HireMaintenanceIcon", "Staff_Hire_Maintenance" },
            { save::StaffKind::Mechanic, "Staff_HireMechanicIcon", "Staff_Hire_Mechanic" },
            { save::StaffKind::SecurityGuard, "Staff_HireSecurityIcon", "Staff_Hire_Security" },
            { save::StaffKind::Entertainer, "Staff_HireEntertainerIcon", "Staff_Hire_Entertainer" },
            { save::StaffKind::AnimalKeeper, "Staff_HireAnimalKeeperIcon", "Staff_Hire_AnimalKeeper" },
        } };
        constexpr std::string_view kHireFrame = "Staff_HireStaff";

        // The info panel's words for the same tasks and costumes, kept here while that panel is reworked apart.
        constexpr std::array<RosterTask, 4> kHandymanTasks = { {
            { 0, "Staff_TaskButtonShovel", "StaffTasks_SweepPathsHelp" },
            { 1, "Staff_TaskButtonWater", "StaffTasks_WaterGardensHelp" },
            { 2, "Staff_TaskButtonBins", "StaffTasks_EmptyBinsHelp" },
            { 3, "Staff_TaskButtonRepair", "Rct3pp_MendPathItemsHelp", true }, // simulation::kLeaveBrokenTask
        } };
        constexpr std::array<RosterTask, 2> kMechanicTasks = { {
            { 0, "Staff_TaskButtonInspect", "StaffTasks_InspectRidesHelp" },
            { 1, "Staff_TaskButtonRepair", "StaffTasks_FixRidesHelp" },
        } };
        constexpr std::array<RosterTask, 8> kInspectorTasks = { {
            { 0, "Staff_TaskButtonEvalFlatRides", "StaffTasks_Evaluate_FlatRidesHelp" },
            { 1, "Staff_TaskButtonEvalCoasters", "StaffTasks_Evaluate_RollerCoastersHelp" },
            { 2, "Staff_TaskButtonEvalFoodDrinkStalls", "StaffTasks_Evaluate_FoodDrinkStallsHelp" },
            { 3, "Staff_TaskButtonEvalSouvenirStalls", "StaffTasks_Evaluate_SouvenirStallsHelp" },
            { 4, "Staff_TaskButtonEvalVistas", "StaffTasks_Evaluate_VistaHelp" },
            { 5, "Staff_TaskButtonEvalStaff", "StaffTasks_Evaluate_StaffHelp" },
            { 6, "Staff_TaskButtonEvalPools", "StaffTasks_Evaluate_PoolsHelp" },
            { 7, "Staff_TaskButtonEvalViewingGalleries", "StaffTasks_Evaluate_ViewingGalleriesHelp" },
        } };
        constexpr std::array<RosterTask, 2> kKeeperTasks = { {
            { 0, "Staff_TaskButtonShovel", "AnimalKeeper_ClearDungTT" },
            { 1, "Staff_TaskButtonRefilTroughs", "AnimalKeeper_FillTroughsTT" },
        } };
        constexpr std::array<std::string_view, 7> kCostumes = {
            "Entertainer_SubType_Panda", "Entertainer_SubType_Elephant", "Entertainer_SubType_PumpkinHead", "Entertainer_SubType_Alien", "Entertainer_SubType_Cowgirl", "Entertainer_SubType_Shark", "Entertainer_SubType_Tiger",
        };
        /// The picture of each job, in the order of save::StaffKind.
        constexpr std::array<std::string_view, 8> kPortraits = {
            "Staff_JobIsMaintenanceIcon", "Staff_JobIsMechanicIcon", "Staff_JobIsSecurityIcon", "Staff_JobIsEntertainerIcon", "Staff_JobIsVendorIcon", "Staff_JobIsAnimalKeeperIcon", "Staff_JobIsMaintenanceIcon", "Staff_JobIsParkInspectorIcon",
        };
        constexpr std::string_view kOk = "SMB_Label_OK";
        constexpr std::string_view kYes = "SMB_Label_Yes";
        constexpr std::string_view kNo = "SMB_Label_No";
        constexpr float kSecondClick = 0.4f; ///< Chosen by eye: seconds within which a second click on a picture counts
        constexpr float kBox = 9;            ///< The unfold box, as the picture of it is wide
        constexpr float kPortraitWidth = 32;
        constexpr float kPortraitHeight = 41;
        constexpr float kIcon = 18;
        constexpr float kTaskSize = 20;

        std::u16string text(const StringTable& strings, std::string_view key)
        {
            return std::u16string(strings.get(key));
        }

        /// How many clothes the uniform of `kind` has colours for; 0 for none.
        std::size_t uniform_pots(save::StaffKind kind)
        {
            using enum save::StaffKind;
            switch (kind)
            {
                case Handyman:
                case SecurityGuard:
                case Entertainer:
                    return 2;
                case Mechanic:
                case AnimalKeeper:
                    return 1;
                default:
                    return 0;
            }
        }

        Label& add_label(Widget& parent, std::string name, std::u16string text, Placement placement, Color colour, Align align = Align::Start)
        {
            Label& l = parent.add<Label>(std::move(name), std::move(text), font::FontRole::Small);
            l.placement = placement;
            l.style.colour = colour;
            l.style.horizontal = align;
            l.style.vertical = Align::Centre;
            return l;
        }

        Button& add_icon(Widget& parent, std::string name, std::string skin, std::string icon, Placement placement, std::u16string help)
        {
            Button& b = parent.add<Button>(std::move(name), std::move(skin), std::move(icon));
            b.placement = placement;
            b.tooltip = std::move(help);
            return b;
        }

        /// The topmost widget, where boxes and menus open.
        Widget& root_of(Widget& w)
        {
            Widget* at = &w;
            while (at->parent() != nullptr)
                at = at->parent();
            return *at;
        }
    } // namespace

    std::span<const RosterTask> roster_tasks(save::StaffKind kind)
    {
        using enum save::StaffKind;
        switch (kind)
        {
            case Handyman:
                return kHandymanTasks;
            case Mechanic:
                return kMechanicTasks;
            case ParkInspector:
                return kInspectorTasks;
            case AnimalKeeper:
                return kKeeperTasks;
            default:
                return { };
        }
    }

    StaffRoster::StaffRoster(std::string name, const StaffRosterStyle& style, const StringTable& strings, float width, float height)
        : Window(std::move(name), style.window, width, height)
        , style_(style)
        , strings_(&strings)
    {
        Widget& body = content();
        add_label(body, "Sort", text(strings, "StaffWindow_SortBy"), top_left(style.sort_x, 0, 50, style.sort_height), style.text_colour);
        sort_box_ = &body.add<Dropdown>("SortBy", style.dropdown);
        sort_box_->placement = { { 0, 0, 1, 0 }, { style.sort_x + 50, 0, 0, style.sort_height } };
        sort_box_->style.text_colour = style.accent_colour;
        for (const std::string_view key : kRosterSortNames)
            sort_box_->items.push_back(text(strings, key));
        sort_box_->select(0);

        auto& back = body.add<Panel>("ListBack", style.list_back);
        back.colour = style.plain;
        const float bottom = 2 * style.bottom_line + 6;
        back.placement = { { 0, 0, 1, 1 }, { 0, style.sort_height + 4, 0, -bottom } };
        list_ = &body.add<ScrollArea>("table", style.scroll);
        list_->placement = back.placement;

        // Hire: above Show:, as the game stacks them.
        const float hire_y = bottom - 2;
        add_label(body, "Staff_HireLabel", text(strings, "Staff_HireLabel"), bottom_left(0, hire_y - style.bottom_line, style.label_width, style.bottom_line), style.text_colour);
        for (std::size_t i = 0; i < kHires; ++i)
        {
            Button& b = add_icon(body, std::string(kHireButtons[i].icon), std::string(kHireFrame), std::string(kHireButtons[i].icon), bottom_left(style.label_width + static_cast<float>(i) * (style.hire_size + 2), hire_y - style.bottom_line, style.hire_size, style.bottom_line), text(strings, kHireButtons[i].help));
            hires_[i] = &b;
        }
        add_label(body, "Staff_Filter", text(strings, "Staff_Filter"), bottom_left(0, 0, style.label_width, style.bottom_line), style.text_colour);
        for (std::size_t i = 0; i < kRosterJobs.size(); ++i)
        {
            Button& b = add_icon(body, std::string(kRosterJobs[i].skin), std::string(kRosterJobs[i].skin), { }, bottom_left(style.label_width + static_cast<float>(i) * style.job_size, 0, style.job_size, style.bottom_line), text(strings, kRosterJobs[i].help));
            b.toggle = true;
            b.on = (jobs_ & (1u << i)) != 0;
            views_[i] = &b;
        }
        add_label(body, "Staff_Panel_All", text(strings, "Staff_Panel_All"), bottom_right(kBox + 4, 0, 30, style.bottom_line), style.text_colour, Align::End);
        expand_all_ = &add_icon(body, "ExpandAll", "Box_Plus", { }, bottom_right(0, style.bottom_line / 2, kBox, kBox), text(strings, "Staff_Panel_ExpandAll"));
        contract_all_ = &add_icon(body, "ContractAll", "Box_Minus", { }, bottom_right(0, style.bottom_line / 2 - kBox - 1, kBox, kBox), text(strings, "Staff_Panel_ContractAll"));
    }

    std::vector<std::pair<std::size_t, bool>> StaffRoster::rows() const
    {
        std::vector<std::pair<std::size_t, bool>> out;
        for (const Row& r : rows_)
            out.emplace_back(r.line.index, r.expanded);
        return out;
    }

    void StaffRoster::show(const ManagementFacts& facts, const StringTable& strings, const InfoUnits& units)
    {
        page_age_ = 0;
        strings_ = &strings;
        units_ = units;
        animal_houses_ = facts.animal_houses;
        std::vector<RosterLine> lines = roster_lines(facts, strings, jobs_, sort_);
        // Same rows in the same order: only those whose words changed are made again, and not one being typed in.
        const bool same = lines.size() == rows_.size() && std::ranges::equal(lines, rows_, [](const RosterLine& l, const Row& r) { return l.index == r.line.index; });
        if (same)
        {
            for (std::size_t i = 0; i < lines.size(); ++i)
            {
                Row& r = rows_[i];
                const bool typing = r.name != nullptr && r.name->has_keyboard_focus();
                if (lines[i] == r.line || typing)
                    continue;
                r.line = std::move(lines[i]);
                build_row(r);
            }
            return;
        }
        // The list goes, palettes and menus with it.
        if (palette_ != nullptr)
            (void)remove(*palette_);
        palette_ = nullptr;
        painting_.reset();
        for (Row& r : rows_)
            (void)list_->content().remove(*r.panel);
        rows_.clear();
        for (RosterLine& l : lines)
        {
            Row& r = rows_.emplace_back();
            r.expanded = std::ranges::contains(unfolded_, l.index);
            r.line = std::move(l);
            r.panel = &list_->content().add<Widget>(std::format("row{}", r.line.index));
            build_row(r);
        }
        stack_rows();
    }

    void StaffRoster::build_row(Row& r)
    {
        const StringTable& strings = *strings_;
        Widget& p = *r.panel;
        while (!p.children().empty())
            (void)p.remove(*p.children().back());
        r.tasks.clear();
        r.swatches.clear();
        r.choices = { };
        r.patrol = r.costume = r.discipline = r.train = r.wage_up = r.wage_down = nullptr;
        const RosterLine& l = r.line;
        const auto kind = static_cast<std::size_t>(l.kind);
        const bool inspector = l.kind == save::StaffKind::ParkInspector;
        const bool vendor = l.kind == save::StaffKind::Vendor;
        auto& band = p.add<Panel>("band", std::string { });
        band.colour = (&r - rows_.data()) % 2 == 0 ? style_.stripe : style_.plain;
        band.placement = fill();

        r.expand = &add_icon(p, "Expand", r.expanded ? "Box_Minus" : "Box_Plus", { }, top_left(1, 1, kBox, kBox), text(strings, "Staff_Panel_Expand"));
        r.portrait = &add_icon(p, "Portrait", { }, std::string(kind < kPortraits.size() ? kPortraits[kind] : kPortraits[0]), top_left(kBox, 2, kPortraitWidth, kPortraitHeight), text(strings, "StaffWindow_CentreViewHelp"));
        const float x = kBox + kPortraitWidth + 3;
        r.name = &p.add<EditBox>("Name", l.name);
        r.name->skin = style_.name_box;
        r.name->focused_skin = style_.name_box_focused;
        r.name->max_length = kLongestName;
        r.name->placement = top_left(x, 2, style_.name_width, 17);
        r.name->tooltip = text(strings, "StaffWindow_NameColumnHelp");
        add_label(p, "Doing", l.doing, top_left(x + 2, 21, style_.name_width + 40, 18), style_.text_colour).tooltip = text(strings, "StaffWindow_ActivityColumnHelp");

        // On the right: their patrol and their mood, under them the uniform's colours. The inspector has none.
        if (!inspector && !vendor)
        {
            r.patrol = &add_icon(p, "Patrol", "Staff_ButtonPatrol", { }, top_right(kIcon + 4, 2, kIcon, kIcon), text(strings, "StaffWindow_EditPatrolAreaHelp"));
            r.patrol->kept_down = l.painting_patrol; // Held down while their area is being painted
        }
        if (!inspector)
        {
            const std::string_view mood_help = l.mood == Mood::Happy ? "StaffWindow_Happiness_Happy" : l.mood == Mood::Sad ? "StaffWindow_Happiness_Sad" : "StaffWindow_Happiness_OK";
            auto& face = p.add<Panel>("Mood", mood_icon(l.mood));
            face.placement = top_right(2, 2, kIcon, kIcon);
            face.tooltip = text(strings, mood_help);
            const std::size_t pots = uniform_pots(l.kind);
            const float right = 2 + style_.swatch * static_cast<float>(pots);
            for (std::size_t pot = 0; pot < pots; ++pot)
            {
                Button& s = add_icon(p, std::format("Colour{}", pot), { }, std::format("{}{}", style_.palette.swatch_prefix, l.colours[pot] % style_.uniform_colours), top_right(right - style_.swatch * static_cast<float>(pot + 1) + 2, 23, style_.swatch, style_.swatch), text(strings, l.kind == save::StaffKind::Entertainer ? "Staff_Costume_Colours" : pots == 1 ? "Staff_Uniform_Colour" : "Staff_Uniform_Colours"));
                r.swatches.push_back(&s);
            }
            if (l.kind == save::StaffKind::Entertainer)
                r.costume = &add_icon(p, "Costume", { }, "Staff_CostumeColumnIcon", top_right(right + 2, 23, style_.swatch, style_.swatch), text(strings, "StaffWindow_CostumeColumnHelp"));
        }
        if (!r.expanded)
            return;

        // Unfolded: their tasks, laziness, warnings and training on one line, their wage and start on the next.
        float y = style_.row_height;
        float tx = x;
        for (const RosterTask& t : roster_tasks(l.kind))
        {
            Button& b = add_icon(p, std::string(t.skin), std::string(t.skin), { }, top_left(tx, y, kTaskSize, kTaskSize), text(strings, t.help));
            b.toggle = true;
            b.on = ((l.tasks & (1 << t.bit)) != 0) != t.inverted;
            r.tasks.emplace_back(t.bit, &b);
            tx += kTaskSize + 1;
        }
        if (inspector || vendor)
            return;
        add_label(p, "Laziness", strings::to_utf16(std::format("{}%", std::lround(l.laziness * 100))), top_right(2 * kIcon + 12, y, 50, kTaskSize), style_.text_colour, Align::End).tooltip = text(strings, "StaffWindow_LazinessColumnHelp");
        r.discipline = &add_icon(p, "Discipline", { }, "Staff_DisciplineColumnIcon", top_right(kIcon + 4, y, kIcon, kIcon), text(strings, "StaffWindow_DisciplineSackHelp"));
        r.train = &add_icon(p, "Train", { }, std::format("Staff_Training{}Icon", std::clamp(l.training, 0, simulation::kMostTraining)), top_right(2, y, kIcon, kIcon), text(strings, "StaffWindow_TrainHelp"));
        y += style_.line_height;
        add_label(p, "Wage", money_text(units_.currency, l.wage), top_left(x, y, 60, style_.line_height), style_.text_colour).tooltip = text(strings, "StaffWindow_WageColumnHelp");
        r.wage_up = &add_icon(p, "WageUp", "SpinnerUp", { }, top_left(x + 64, y + 2, 20, 8), text(strings, "StaffWindow_WageColumnHelp"));
        r.wage_down = &add_icon(p, "WageDown", "SpinnerDown", { }, top_left(x + 64, y + 10, 20, 8), text(strings, "StaffWindow_WageColumnHelp"));
        r.wage_up->enabled = l.wage_level < simulation::kMostWageLevel;
        r.wage_down->enabled = l.wage_level > 0;
        add_label(p, "Hired", l.hired_text, top_right(2, y, 120, style_.line_height), style_.text_colour, Align::End).tooltip = text(strings, "StaffWindow_StartDateColumnHelp");
    }

    void StaffRoster::stack_rows()
    {
        float y = 0;
        for (std::size_t i = 0; i < rows_.size(); ++i)
        {
            Row& r = rows_[i];
            const bool inspector = r.line.kind == save::StaffKind::ParkInspector || r.line.kind == save::StaffKind::Vendor;
            const float lines = !r.expanded ? 0 : inspector ? (roster_tasks(r.line.kind).empty() ? 0 : 1) : 2;
            const float height = style_.row_height + lines * style_.line_height + (lines > 0 ? 2 : 0);
            r.panel->placement = { { 0, 0, 1, 0 }, { 0, y, 0, y + height } };
            y += height;
        }
    }

    void StaffRoster::say(std::u16string message, bool ask)
    {
        const StringTable& strings = *strings_;
        std::vector<QuestionAnswer> answers;
        if (ask)
            answers = { { text(strings, kYes), style_.question.confirm_icon }, { text(strings, kNo), style_.question.cancel_icon } };
        else
            answers = { { text(strings, kOk), style_.question.confirm_icon } };
        question_ = &root_of(*this).add<QuestionBox>("StaffQuestion", style_.question, std::move(message), answers);
    }

    void StaffRoster::update(const UpdateContext& context)
    {
        Window::update(context);
        page_age_ += context.elapsed;
        since_portrait_ += context.elapsed;
        const StringTable& strings = *strings_;
        bool relist = false;
        if (sort_box_->take_change() && sort_box_->selected())
        {
            sort_ = static_cast<RosterSort>(*sort_box_->selected());
            relist = true;
        }
        for (std::size_t i = 0; i < views_.size(); ++i)
        {
            if (!views_[i]->take_click())
                continue;
            jobs_ ^= static_cast<RosterJobs>(1u << i);
            views_[i]->on = (jobs_ & (1u << i)) != 0;
            relist = true;
        }
        for (std::size_t i = 0; i < kHires; ++i)
        {
            if (!hires_[i]->take_click())
                continue;
            // A keeper looks after an enclosure's animals.
            if (kHireButtons[i].kind == save::StaffKind::AnimalKeeper && !animal_houses_)
                say(text(strings, "EnclosureNeeded_Message"), false);
            else
                asks_.hire = kHireButtons[i].kind;
        }
        const bool all = expand_all_->take_click();
        const bool none = contract_all_->take_click();
        if (all || none)
        {
            unfolded_.clear();
            for (Row& r : rows_)
            {
                r.expanded = all;
                if (all)
                    unfolded_.push_back(r.line.index);
                build_row(r);
            }
            stack_rows();
        }
        for (Row& r : rows_)
        {
            const std::size_t who = r.line.index;
            const auto change = [&](PersonControl control, int value = 0, int bit = 0) { asks_.change = { who, InfoChange { .id = person_control(control, bit), .value = value, .pot = 0, .text = { } } }; };
            if (r.expand->take_click())
            {
                r.expanded = !r.expanded;
                if (r.expanded)
                    unfolded_.push_back(who);
                else
                    std::erase(unfolded_, who);
                build_row(r);
                stack_rows();
                break; // Its widgets were made again
            }
            if (r.portrait->take_click())
            {
                asks_.open = who;
                if (last_portrait_ == who && since_portrait_ < kSecondClick)
                    asks_.centre = who;
                last_portrait_ = who;
                since_portrait_ = 0;
            }
            if (r.name->take_commit())
                asks_.change = { who, InfoChange { .id = person_control(PersonControl::Name), .value = 0, .pot = 0, .text = r.name->text } };
            if (r.patrol != nullptr && r.patrol->take_click())
            {
                asks_.open = who;
                change(PersonControl::PatrolPaint, 1);
            }
            for (const auto& [bit, b] : r.tasks)
                if (b->take_click())
                    change(PersonControl::Task, 0, bit);
            if (r.wage_up != nullptr && r.wage_up->take_click())
                change(PersonControl::Wage, r.line.wage_level + 1);
            if (r.wage_down != nullptr && r.wage_down->take_click())
                change(PersonControl::Wage, r.line.wage_level - 1);
            if (r.train != nullptr && r.train->take_click() && question_ == nullptr)
            {
                if (r.line.training_cost <= 0)
                    say(text(strings, "StaffWindow_NoMoreTraining"), false);
                else
                {
                    std::u16string ask = text(strings, "StaffWindow_TrainingText1");
                    const std::u16string amount = money_text(units_.currency, r.line.training_cost);
                    if (const std::size_t at = ask.find(u"%AMOUNT%"); at != std::u16string::npos)
                        ask.replace(at, 8, amount);
                    say(ask + u" " + text(strings, "StaffWindow_TrainingText2"), true);
                    training_ = who;
                }
            }
            // The warning and the sack fan out beside the discipline button.
            if (r.discipline != nullptr && r.discipline->take_click())
            {
                if (r.choices[0] != nullptr)
                {
                    for (Button*& c : r.choices)
                        (void)r.panel->remove(*std::exchange(c, nullptr));
                }
                else
                {
                    const float y = r.discipline->rect().y0 - r.panel->rect().y0;
                    r.choices[0] = &add_icon(*r.panel, "Warn", { }, "Staff_GiveWarningIcon", top_right(3 * kIcon + 8, y, kIcon, kIcon), text(strings, "StaffWindow_DisciplineHelp"));
                    r.choices[1] = &add_icon(*r.panel, "Sack", { }, "Staff_GiveSackIcon", top_right(2 * kIcon + 6, y, kIcon, kIcon), text(strings, "StaffWindow_SackHelp"));
                    r.choices[0]->enabled = r.line.warnings < kMostWarnings;
                }
            }
            if (r.choices[0] != nullptr && r.choices[0]->take_click())
                change(PersonControl::Warn);
            if (r.choices[1] != nullptr && r.choices[1]->take_click())
                change(PersonControl::Sack);
            for (std::size_t pot = 0; pot < r.swatches.size(); ++pot)
            {
                if (!r.swatches[pot]->take_click())
                    continue;
                const bool same = painting_ == std::pair { who, pot };
                if (palette_ != nullptr)
                    (void)remove(*palette_);
                palette_ = nullptr;
                painting_.reset();
                if (same)
                    continue;
                const Point size = ColourPalette::size(style_.palette, style_.uniform_colours, style_.palette_columns);
                palette_ = &add<ColourPalette>("palette", style_.palette, style_.uniform_colours, style_.palette_columns, r.line.colours[pot] % style_.uniform_colours);
                const Rect s = r.swatches[pot]->rect();
                palette_->placement = top_left(s.x1 - rect().x0 - size.x, s.y1 - rect().y0, size.x, size.y);
                painting_ = { who, pot };
            }
            if (r.costume != nullptr && r.costume->take_click())
            {
                std::vector<MenuItem> items;
                for (std::size_t c = 0; c < kCostumes.size(); ++c)
                    items.push_back({ .text = text(strings, kCostumes[c]), .checked = c == r.line.costume });
                costumes_ = &root_of(*this).add<Menu>("Costumes", style_.menu, std::move(items));
                costumes_->place_at({ r.costume->rect().x0, r.costume->rect().y1 });
                dressing_ = who;
            }
        }
        if (palette_ != nullptr && painting_)
        {
            if (const auto colour = palette_->take_pick())
            {
                asks_.paint = RosterAsks::Paint { .staff = painting_->first, .pot = painting_->second, .colour = *colour };
                (void)remove(*palette_);
                palette_ = nullptr;
                painting_.reset();
            }
        }
        if (costumes_ != nullptr)
        {
            if (const auto picked = costumes_->take_choice(); picked && dressing_)
                asks_.change = { *dressing_, InfoChange { .id = person_control(PersonControl::Costume), .value = static_cast<int>(*picked), .pot = 0, .text = { } } };
            if (costumes_->closing())
                costumes_ = nullptr;
        }
        if (question_ != nullptr)
        {
            if (const auto answer = question_->take_answer())
            {
                if (*answer == 0 && training_)
                    asks_.change = { *training_, InfoChange { .id = person_control(PersonControl::Train), .value = 0, .pot = 0, .text = { } } };
                training_.reset();
                question_ = nullptr;
            }
            else if (question_->closing())
            {
                training_.reset();
                question_ = nullptr;
            }
        }
        if (relist && strings_ != nullptr)
            page_age_ = 1e9f; // The owner shows the park again at once
    }

} // namespace rct3::ui
