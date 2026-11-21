// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/park/campaigns.hpp"

#include "core/types.hpp"
#include "simulation/park/finance.hpp"
#include "simulation/park/state.hpp"

#include <algorithm>
#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::simulation::park;
using save::model::CampaignKind;

namespace
{
    constexpr f32 kWeek = save::model::GameClock::kUnitsPerWeek;
} // namespace

TEST_CASE("a campaign pays its first week as it starts and each further week as it begins, then ends with a message")
{
    ParkState park;
    park.cash = units::Money { 1'000'000 };
    park.time = 1000;
    save::model::Finance money;
    std::optional<i64> kept;
    std::vector<save::model::Campaign> campaigns;
    const units::Money week = campaign_week_cost(CampaignKind::Advertising);
    REQUIRE(start_campaign(park, money, kept, campaigns, { .kind = CampaignKind::Advertising, .weeks = 3 }));
    REQUIRE(campaigns.size() == 1);
    CHECK(money.ledger.front().marketing == week.value);
    CHECK(campaign_weeks_left(campaigns[0], park.time) == 3);

    // Within the first week nothing more; two weeks begun in one step pay for both.
    f32 before = park.time;
    park.time += kWeek / 2;
    step_campaigns(park, money, kept, campaigns, before);
    CHECK(money.ledger.front().marketing == week.value);
    before = park.time;
    park.time += kWeek * 1.6f;
    step_campaigns(park, money, kept, campaigns, before);
    CHECK(money.ledger.front().marketing == week.value * 3);
    CHECK(park.cash == units::Money { 1'000'000 - week.value * 3 });
    CHECK(campaign_weeks_left(campaigns[0], park.time) == 1);

    // Past its end it is gone, no week more paid, and the player told.
    before = park.time;
    park.time += kWeek;
    step_campaigns(park, money, kept, campaigns, before);
    CHECK(campaigns.empty());
    CHECK(money.ledger.front().marketing == week.value * 3);
    REQUIRE_FALSE(park.messages.list.empty());
    CHECK(park.messages.list.back().text_key == kCampaignEndedKey);
    CHECK(park.messages.list.back().field_key == campaign_name_key(CampaignKind::Advertising));
}

TEST_CASE("weeks are paid even when the park cannot pay")
{
    ParkState park;
    park.cash = units::Money { 100'000 };
    save::model::Finance money;
    std::optional<i64> kept;
    std::vector<save::model::Campaign> campaigns;
    REQUIRE(start_campaign(park, money, kept, campaigns, { .kind = CampaignKind::ParkVouchers, .weeks = 2 }));
    park.cash = units::Money { 0 };
    const f32 before = park.time;
    park.time += kWeek;
    step_campaigns(park, money, kept, campaigns, before);
    CHECK(park.cash == units::Money { -campaign_week_cost(CampaignKind::ParkVouchers).value });
}

TEST_CASE("a campaign the park cannot pay for, or may not start, is refused")
{
    ParkState park;
    park.cash = units::Money { campaign_week_cost(CampaignKind::Advertising).value * 2 };
    save::model::Finance money;
    std::optional<i64> kept;
    std::vector<save::model::Campaign> campaigns;
    SUBCASE("too little cash for all its weeks")
    {
        CHECK_FALSE(start_campaign(park, money, kept, campaigns, { .kind = CampaignKind::Advertising, .weeks = 3 }));
        CHECK(campaigns.empty());
        CHECK(money.ledger.empty());
        CHECK(std::ranges::contains(park.messages.list, kCannotPayKey, &ParkMessage::text_key));
    }
    SUBCASE("one of each kind at a time")
    {
        REQUIRE(start_campaign(park, money, kept, campaigns, { .kind = CampaignKind::ParkVouchers, .weeks = 1 }));
        CHECK_FALSE(may_start_campaign(campaigns, { .kind = CampaignKind::ParkVouchers, .weeks = 1 }));
        CHECK(may_start_campaign(campaigns, { .kind = CampaignKind::Advertising, .weeks = 1 }));
    }
    SUBCASE("attraction vouchers need an attraction, and weeks are within range")
    {
        CHECK_FALSE(may_start_campaign(campaigns, { .kind = CampaignKind::AttractionVouchers, .weeks = 2 }));
        CHECK(may_start_campaign(campaigns, { .kind = CampaignKind::AttractionVouchers, .weeks = 2, .attraction = 7 }));
        CHECK_FALSE(may_start_campaign(campaigns, { .kind = CampaignKind::Advertising, .weeks = 0 }));
        CHECK_FALSE(may_start_campaign(campaigns, { .kind = CampaignKind::Advertising, .weeks = kMostCampaignWeeks + 1 }));
    }
}

TEST_CASE("running campaigns add to the park's publicity, which the graph keeps with them added")
{
    const std::vector<save::model::Campaign> campaigns = { { .kind = CampaignKind::Advertising }, { .kind = CampaignKind::ParkVouchers } };
    const f32 added = campaigns_publicity(campaigns);
    CHECK(added == doctest::Approx((campaign_pull(CampaignKind::Advertising) + campaign_pull(CampaignKind::ParkVouchers)) * kPlainPublicity));
    CHECK(own_publicity({ }, campaigns) == doctest::Approx(kPlainPublicity));
    CHECK(own_publicity({ .samples = { 200 + added } }, campaigns) == doctest::Approx(200));
}
