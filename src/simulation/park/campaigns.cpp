// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/park/campaigns.hpp"

#include "core/types.hpp"
#include "simulation/park/finance.hpp"
#include "simulation/park/messages.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>

namespace rct3::simulation::park
{

    namespace
    {
        using save::model::CampaignKind;

        constexpr f32 kWeek = save::model::GameClock::kUnitsPerWeek;

        /// Weeks of `campaign` begun by calendar time `time`: the first as it starts, none before.
        i32 weeks_begun(const save::model::Campaign& campaign, f32 time)
        {
            if (time < campaign.start)
                return 0;
            return std::min(campaign_weeks(campaign), static_cast<i32>(std::floor((time - campaign.start) / kWeek)) + 1);
        }
    } // namespace

    std::string_view campaign_name_key(CampaignKind kind)
    {
        switch (kind)
        {
            case CampaignKind::ParkVouchers:
                return "Campaign_Park_Vouchers";
            case CampaignKind::Advertising:
                return "Campaign_Park_Advertise";
            case CampaignKind::AttractionVouchers:
                return "Campaign_Attraction_Vouchers";
        }
        return { };
    }

    units::Money campaign_week_cost(CampaignKind kind)
    {
        // Taken from OpenRCT2 (GPL-3.0), src/openrct2/management/Marketing.cpp, AdvertisingCampaignPricePerWeek:
        // 50.00 for free park entry and free rides, 350.00 for advertising the park.
        constexpr units::Money kCoupons { 5000 };
        constexpr units::Money kAdvertising { 35000 };
        return kind == CampaignKind::Advertising ? kAdvertising : kCoupons;
    }

    f32 campaign_pull(CampaignKind kind)
    {
        // Chosen by eye: advertising doubles the parties coming, free entry draws half as many again and a free
        // ride on one attraction fewer.
        constexpr std::array<f32, save::model::kCampaignKinds> kPull = { 0.5f, 1.0f, 0.3f };
        return kPull[std::to_underlying(kind)];
    }

    f32 campaigns_publicity(std::span<const save::model::Campaign> campaigns)
    {
        f32 points = 0;
        for (const save::model::Campaign& c : campaigns)
            points += campaign_pull(c.kind) * kPlainPublicity;
        return points;
    }

    f32 own_publicity(const save::model::Series& series, std::span<const save::model::Campaign> campaigns)
    {
        const std::optional<f32> kept = series.latest();
        if (!kept || *kept <= 0)
            return kPlainPublicity;
        return std::max(0.0f, *kept - campaigns_publicity(campaigns));
    }

    i32 campaign_weeks(const save::model::Campaign& campaign)
    {
        return std::max(0, static_cast<i32>(std::lround((campaign.end - campaign.start) / kWeek)));
    }

    i32 campaign_weeks_left(const save::model::Campaign& campaign, f32 time)
    {
        return std::max(0, static_cast<i32>(std::ceil((campaign.end - std::max(time, campaign.start)) / kWeek)));
    }

    bool may_start_campaign(std::span<const save::model::Campaign> campaigns, const CampaignOrder& order)
    {
        if (std::ranges::contains(campaigns, order.kind, &save::model::Campaign::kind))
            return false;
        if (order.weeks < kFewestCampaignWeeks || order.weeks > kMostCampaignWeeks)
            return false;
        return order.kind != CampaignKind::AttractionVouchers || order.attraction.has_value();
    }

    bool start_campaign(ParkState& park, save::model::Finance& finance, std::optional<i64>& kept, std::vector<save::model::Campaign>& campaigns, const CampaignOrder& order)
    {
        if (!may_start_campaign(campaigns, order))
            return false;
        const units::Money week = campaign_week_cost(order.kind);
        if (park.cash && *park.cash < units::Money { week.value * order.weeks })
        {
            cannot_pay(park);
            return false;
        }
        const save::model::Campaign& c = campaigns.emplace_back(
            save::model::Campaign {
                .kind = order.kind,
                .attraction = order.kind == CampaignKind::AttractionVouchers ? order.attraction : std::nullopt,
                .start = park.time,
                .end = park.time + static_cast<f32>(order.weeks) * kWeek,
            });
        book(park, finance, kept, units::Money { week.value * weeks_begun(c, park.time) }, LedgerLine::Marketing, Charge::Always);
        return true;
    }

    void step_campaigns(ParkState& park, save::model::Finance& finance, std::optional<i64>& kept, std::vector<save::model::Campaign>& campaigns, f32 before)
    {
        for (const save::model::Campaign& c : campaigns)
            if (const i32 weeks = weeks_begun(c, park.time) - weeks_begun(c, before); weeks > 0)
                book(park, finance, kept, units::Money { campaign_week_cost(c.kind).value * weeks }, LedgerLine::Marketing, Charge::Always);
        std::erase_if(campaigns, [&](const save::model::Campaign& c) {
            if (park.time < c.end)
                return false;
            post(park.messages, { .text_key = std::string(kCampaignEndedKey), .field_key = std::string(campaign_name_key(c.kind)) });
            return true;
        });
    }

} // namespace rct3::simulation::park
