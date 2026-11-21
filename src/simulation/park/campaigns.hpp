// SPDX-License-Identifier: GPL-3.0-or-later
//
// Marketing campaigns: the player pays for some weeks of advertising or of coupons handed out, and while a campaign
// runs it adds to the park's publicity, drawing parties on top of those the park draws by itself. Coupon campaigns'
// parties come holding coupons: free entry to the park, or a free ride on the campaign's attraction. Each week of a
// campaign is paid as it begins, whether the park can pay or not, and the player is told when one ends.
#pragma once

#include "core/types.hpp"
#include "save/model/park.hpp"
#include "simulation/park/state.hpp"
#include "units/units.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace rct3::simulation::park
{

    /// Weeks a campaign may run, at least and at most. Chosen by eye: a week to three months.
    inline constexpr i32 kFewestCampaignWeeks = 1;
    inline constexpr i32 kMostCampaignWeeks = 12;

    /// The message telling the player a campaign has ended: the game's own text, named by the campaign's kind.
    inline constexpr std::string_view kCampaignEndedKey = "Campaign_Finished";

    /// The string key naming a campaign of `kind`.
    [[nodiscard]] std::string_view campaign_name_key(save::model::CampaignKind kind);

    /// What a week of a campaign of `kind` costs.
    [[nodiscard]] units::Money campaign_week_cost(save::model::CampaignKind kind);

    /// Parties a running campaign of `kind` draws on top of the park's own, as a share of them.
    [[nodiscard]] f32 campaign_pull(save::model::CampaignKind kind);

    /// The publicity `campaigns` add to the park's own level.
    [[nodiscard]] f32 campaigns_publicity(std::span<const save::model::Campaign> campaigns);

    /// The park's own publicity, without what `campaigns` add: the newest level of `series` (kept with them
    /// added), or kPlainPublicity when the park kept none.
    [[nodiscard]] f32 own_publicity(const save::model::Series& series, std::span<const save::model::Campaign> campaigns);

    /// Whole weeks a campaign runs.
    [[nodiscard]] i32 campaign_weeks(const save::model::Campaign& campaign);

    /// Whole weeks of `campaign` still to begin at calendar time `time`, counting the one under way.
    [[nodiscard]] i32 campaign_weeks_left(const save::model::Campaign& campaign, f32 time);

    /// A campaign the player asks for.
    struct CampaignOrder
    {
        save::model::CampaignKind kind = save::model::CampaignKind::Advertising;
        i32 weeks = kFewestCampaignWeeks;
        std::optional<u64> attraction { }; ///< For attraction vouchers (AttractionCommon::id)
    };

    /// Whether `order` may start beside `campaigns`: none of its kind runs (one of each kind at a time), its weeks
    /// are within range, and attraction vouchers name an attraction.
    [[nodiscard]] bool may_start_campaign(std::span<const save::model::Campaign> campaigns, const CampaignOrder& order);

    /// Starts `order` at the park's time, paying its first week on the ledger's marketing line. Refused, with
    /// false, when it may not start (may_start_campaign), or when the park cannot pay all its weeks: then the
    /// player is told (cannot_pay).
    bool start_campaign(ParkState& park, save::model::Finance& finance, std::optional<i64>& kept, std::vector<save::model::Campaign>& campaigns, const CampaignOrder& order);

    /// Moves `campaigns` on from calendar time `before` to the park's: each week begun since is paid on the
    /// marketing line whether the park can pay or not, and each campaign past its end is dropped, the player
    /// told.
    void step_campaigns(ParkState& park, save::model::Finance& finance, std::optional<i64>& kept, std::vector<save::model::Campaign>& campaigns, f32 before);

} // namespace rct3::simulation::park
