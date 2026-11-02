// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/thought_text.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <utility>

namespace rct3::ui
{

    namespace
    {
        constexpr std::string_view kKeyPrefix = "Thought_";
        constexpr std::u16string_view kNameMark = u"%NAME%";
        /// Texts tried for one thought at most; no thought has nearly as many.
        constexpr int kMostTexts = 32;

        struct Thought
        {
            std::string_view name; ///< What it is about: its keys are kKeyPrefix, this, then the text's number
            Mood mood = Mood::Neutral;
        };

        using enum Mood;

        /// Each thought by its number, with how it feels by what its texts say: glad, put out, or neither.
        constexpr std::array<Thought, 256> kThoughts { { { "None", Neutral },
                                                         { "ImLost", Sad },
                                                         { "ImTired", Sad },
                                                         { "ThisPathHasLitter", Sad },
                                                         { "ImHungry", Sad },
                                                         { "ImThirsty", Sad },
                                                         { "ReallyTidy", Happy },
                                                         { "ThisParkIsTooCrowded", Sad },
                                                         { "GreatScenery", Happy },
                                                         { "ILikeItHere", Happy },
                                                         { "ImNotHungry", Neutral },
                                                         { "ImNotThirsty", Neutral },
                                                         { "INeedToilet", Sad },
                                                         { "IFeelIll", Sad },
                                                         { "ThisPathIsVandalised", Sad },
                                                         { "ThisPathHasSickOnIt", Sad },
                                                         { "GreatJumpingFountains", Happy },
                                                         { "IWantToGetOffRide", Sad },
                                                         { "IWantToGetOutOfRide", Sad },
                                                         { "IWantToGoHome", Sad },
                                                         { "IFeelSick", Sad },
                                                         { "IFeelVerySick", Sad },
                                                         { "IveAlreadyGotThisObject", Neutral },
                                                         { "IHaventFinishedYet", Neutral },
                                                         { "IveSpentAllMyMoney", Sad },
                                                         { "ICantAffordThis", Sad },
                                                         { "ThisIsGoodValue", Happy },
                                                         { "ImNotPayingThisMuch", Sad },
                                                         { "BadMusic", Sad },
                                                         { "NiceMusic", Happy },
                                                         { "NeutralMusic", Neutral },
                                                         { "IveBeenQueueingAges", Sad },
                                                         { "IWontGoOnRideWhileRaining", Sad },
                                                         { "IPreferMoreIntenseRides", Neutral },
                                                         { "IPreferLessIntenseRides", Neutral },
                                                         { "IWishIHadACamera", Neutral },
                                                         { "GreatFireworks", Happy },
                                                         { "BoringFireworks", Sad },
                                                         { "GreatEntertainer", Happy },
                                                         { "BoringEntertainer", Sad },
                                                         { "IveBeenAttackedBySeagull", Sad },
                                                         { "IPreferLessNauseaRides", Neutral },
                                                         { "IEnjoyedThisRide", Happy },
                                                         { "ICantFindRide", Sad },
                                                         { "ImNotPayingThisMuchForToilet", Sad },
                                                         { "ThisRideIsntSafe", Sad },
                                                         { "ICantFindWayOut", Sad },
                                                         { "Staff_Bored", Sad },
                                                         { "Staff_TooBusy", Sad },
                                                         { "Staff_Tired", Sad },
                                                         { "Staff_WantToQuit", Sad },
                                                         { "Staff_Miserable", Sad },
                                                         { "Staff_Underpaid", Sad },
                                                         { "Staff_BadFacilities", Sad },
                                                         { "Staff_ExcellentFacilities", Happy },
                                                         { "Staff_TooRepetative", Sad },
                                                         { "Staff_Happy", Happy },
                                                         { "Staff_Motivated", Happy },
                                                         { "Staff_Varied", Happy },
                                                         { "StingyToppings", Sad },
                                                         { "MassesOfToppings", Happy },
                                                         { "VomitWitness", Sad },
                                                         { "Disaster", Sad },
                                                         { "Barred", Sad },
                                                         { "KidTantrum", Sad },
                                                         { "ParentTantrum", Sad },
                                                         { "ConversationHappy", Happy },
                                                         { "ConversationNeutral", Neutral },
                                                         { "ConversationSad", Sad },
                                                         { "ConversationRomance", Happy },
                                                         { "Tracked", Neutral },
                                                         { "PincerHappy", Happy },
                                                         { "PincerNeutral", Neutral },
                                                         { "PincerSad", Sad },
                                                         { "Observation", Happy },
                                                         { "HandHolding", Happy },
                                                         { "Running", Neutral },
                                                         { "Queuing", Happy },
                                                         { "Miser", Neutral },
                                                         { "ShiftyMusic", Happy },
                                                         { "Event", Sad },
                                                         { "SharkEvent", Neutral },
                                                         { "TruckEvent", Sad },
                                                         { "DoomEvent", Neutral },
                                                         { "EnterRide", Neutral },
                                                         { "PhotoSpotGood", Happy },
                                                         { "PhotoSpotConsistent", Happy },
                                                         { "PhotoSpotMixed", Sad },
                                                         { "Photographer", Neutral },
                                                         { "Photographee", Neutral },
                                                         { "SkySunset", Happy },
                                                         { "SkySunrise", Happy },
                                                         { "Sky", Happy },
                                                         { "WeatherRain", Sad },
                                                         { "WeatherThunder", Sad },
                                                         { "WeatherCold", Sad },
                                                         { "WeatherHot", Sad },
                                                         { "Themed1Good", Happy },
                                                         { "Themed2Good", Happy },
                                                         { "Themed3Good", Happy },
                                                         { "Themed4Good", Happy },
                                                         { "Themed1Bad", Sad },
                                                         { "Themed2Bad", Sad },
                                                         { "Themed3Bad", Sad },
                                                         { "Themed4Bad", Sad },
                                                         { "MoreRides", Neutral },
                                                         { "VIPNoMoreRides", Sad },
                                                         { "VIPClintNoLitter", Happy },
                                                         { "VIPClintMuchLitter", Sad },
                                                         { "VIPClintAutograph", Neutral },
                                                         { "VIPClintMoreIntensity", Neutral },
                                                         { "VIPClintLessIntensity", Neutral },
                                                         { "VIPJayNoLitter", Happy },
                                                         { "VIPJayMuchLitter", Sad },
                                                         { "VIPJayAutograph", Neutral },
                                                         { "VIPJayMoreIntensity", Neutral },
                                                         { "VIPJayLessIntensity", Neutral },
                                                         { "VIPJoeNoLitter", Happy },
                                                         { "VIPJoeMuchLitter", Sad },
                                                         { "VIPJoeAutograph", Neutral },
                                                         { "VIPJoeMoreIntensity", Neutral },
                                                         { "VIPJoeLessIntensity", Neutral },
                                                         { "VIPKariNoLitter", Happy },
                                                         { "VIPKariMuchLitter", Sad },
                                                         { "VIPKariAutograph", Neutral },
                                                         { "VIPKariMoreIntensity", Neutral },
                                                         { "VIPKariLessIntensity", Neutral },
                                                         { "ParanoidWatching", Neutral },
                                                         { "ParanoidThing", Neutral },
                                                         { "ParanoidBother", Sad },
                                                         { "ParanoidTickle", Happy },
                                                         { "GivenUpOnQueue", Sad },
                                                         { "PI_General_NoMaintenanceManNear", Sad },
                                                         { "PI_Attraction_Evaluated", Neutral },
                                                         { "PI_Ride_NoExit", Sad },
                                                         { "PI_Ride_NoEntrance", Sad },
                                                         { "PI_Ride_NoPathToEntrance", Sad },
                                                         { "PI_Ride_NoPathToExit", Sad },
                                                         { "PI_Ride_InappropriateMusic", Sad },
                                                         { "PI_Ride_NoRideAssessment", Sad },
                                                         { "PI_Ride_LongQueueTime", Sad },
                                                         { "PI_Ride_LongRideTime", Sad },
                                                         { "PI_TrackedRide_Boring", Sad },
                                                         { "PI_Stall_TooExpensive", Sad },
                                                         { "PI_Stall_TooCheap", Sad },
                                                         { "PI_Stall_NoPathToGetTo", Sad },
                                                         { "PI_Stall_TooManyForDemand", Sad },
                                                         { "PI_Stall_NotEnoughForDemand", Sad },
                                                         { "PI_Vista_InappropriateStyle", Sad },
                                                         { "PI_Staff_Lazy", Sad },
                                                         { "PI_Staff_Unhappy", Sad },
                                                         { "PI_Staff_NoPathToGetTo", Sad },
                                                         { "PI_Ride_TooIntense", Sad },
                                                         { "PI_Ride_WayTooIntense", Sad },
                                                         { "PI_Ride_Crashed", Sad },
                                                         { "PI_Ride_Crashed_Injuries", Sad },
                                                         { "PI_Guests_Hungry", Sad },
                                                         { "PI_Guests_Thirsty", Sad },
                                                         { "PI_Guests_Tired", Sad },
                                                         { "PI_Park_Litter", Sad },
                                                         { "PI_Park_Breakdowns", Sad },
                                                         { "PI_Park_Vandalism", Sad },
                                                         { "ImNotPayingThisMuchForItem", Sad },
                                                         { "BeenInjured", Sad },
                                                         { "RideClosed", Sad },
                                                         { "RideBroken", Sad },
                                                         { "Themed5Good", Happy },
                                                         { "Themed6Good", Happy },
                                                         { "Themed5Bad", Sad },
                                                         { "Themed6Bad", Sad },
                                                         { "RideIsntVeryExciting", Sad },
                                                         { "RideIsFarAway", Sad },
                                                         { "PI_Guests_Toilet", Sad },
                                                         { "AttractionTooBusy", Sad },
                                                         { "VIPWatermanNoLitter", Happy },
                                                         { "VIPWatermanMuchLitter", Sad },
                                                         { "VIPWatermanAutograph", Neutral },
                                                         { "VIPWatermanMoreIntensity", Neutral },
                                                         { "VIPWatermanLessIntensity", Neutral },
                                                         { "PoolGood", Happy },
                                                         { "PoolBad", Sad },
                                                         { "PoolOvercleaned", Sad },
                                                         { "NoPools", Sad },
                                                         { "HeadingForPool", Happy },
                                                         { "JacuzziGood", Happy },
                                                         { "LasersGood", Happy },
                                                         { "LasersBad", Sad },
                                                         { "WaterShowGood", Happy },
                                                         { "WaterShowBad", Sad },
                                                         { "JumpingFountainsGood", Happy },
                                                         { "JumpingFountainsBad", Sad },
                                                         { "WetBad", Sad },
                                                         { "PI_StallTooBusy", Sad },
                                                         { "PI_PoolDirty", Sad },
                                                         { "PI_PoolVeryDirty", Sad },
                                                         { "PI_DangerousFlume", Sad },
                                                         { "PI_DangerousDivingBoard", Sad },
                                                         { "PI_NoMechanicsCanReachRide", Sad },
                                                         { "TooColdForPools", Sad },
                                                         { "PI_MechanicBadZones", Sad },
                                                         { "DontNeedUmbrella", Neutral },
                                                         { "QueueTooLong", Sad },
                                                         { "AnimalLooksUnhealthy", Sad },
                                                         { "RobotArmIAmSuperior", Neutral },
                                                         { "RobotArmRideInferior", Neutral },
                                                         { "RobotArmHumanInferior", Neutral },
                                                         { "RobotArmParkInspector", Neutral },
                                                         { "RobotArmVIP", Neutral },
                                                         { "RobotArmJoinMe", Neutral },
                                                         { "RobotArmMalfunction", Neutral },
                                                         { "VIPClintMuchDung", Sad },
                                                         { "VIPSeeAnimals", Happy },
                                                         { "SeeAnimal", Happy },
                                                         { "AnimalLooksHungry", Sad },
                                                         { "AnimalLooksWellFed", Happy },
                                                         { "AnimalEating", Neutral },
                                                         { "AnimalCuddly", Happy },
                                                         { "AnimalCute", Happy },
                                                         { "AnimalUgly", Sad },
                                                         { "AnimalLooksFunny", Neutral },
                                                         { "AnimalIsBoring", Sad },
                                                         { "AnimalLooksTired", Sad },
                                                         { "AnimalLooksHealthy", Happy },
                                                         { "AnimalLooksBored", Sad },
                                                         { "AnimalHavingFun", Happy },
                                                         { "AnimalLooksDirty", Sad },
                                                         { "AnimalLooksClean", Happy },
                                                         { "AnimalPreference", Happy },
                                                         { "AnimalLooksLike", Neutral },
                                                         { "PI_AnimalKeepersBadZones", Sad },
                                                         { "PI_NoKeepersCanReachShelter", Sad },
                                                         { "PI_CantEvaluateKeeper", Sad },
                                                         { "PI_NoGalleryOnEnclosure", Sad },
                                                         { "PI_AnimalEscaped", Sad },
                                                         { "PI_AnimalNeedsHouse", Sad },
                                                         { "PI_AnimalCannotReachHouse", Sad },
                                                         { "PI_AnimalHungryFoodTroughsEmpty", Sad },
                                                         { "PI_HouseFullyOccupied", Sad },
                                                         { "PI_TooMuchDung", Sad },
                                                         { "PI_EnclosureTooSmall", Sad },
                                                         { "PI_EnclosureOvercrowded", Sad },
                                                         { "PI_AnimalAntiSocial", Sad },
                                                         { "PI_AnimalLonely", Sad },
                                                         { "PI_AnimalKeeperCantReachHut", Sad },
                                                         { "Themed7Good", Happy },
                                                         { "Themed8Good", Happy },
                                                         { "Themed7Bad", Sad },
                                                         { "Themed8Bad", Sad },
                                                         { "AnimalAntiSocial", Sad },
                                                         { "AnimalLonely", Sad },
                                                         { "AnimalKilled", Sad },
                                                         { "VIPClintAnimalKilled", Sad },
                                                         { "PI_PersonTrappedInEnclosure", Sad },
                                                         { "PI_FenceUnsuitable", Sad },
                                                         { "AnimalPregnant", Happy },
                                                         { "PandaPregnant", Happy } } };

    } // namespace

    std::string thought_key(std::int32_t id)
    {
        if (id <= 0 || static_cast<std::size_t>(id) >= kThoughts.size())
            return { };
        return std::format("{}{}", kKeyPrefix, kThoughts[static_cast<std::size_t>(id)].name);
    }

    Mood thought_mood(std::int32_t id)
    {
        if (id <= 0 || static_cast<std::size_t>(id) >= kThoughts.size())
            return Mood::Neutral;
        return kThoughts[static_cast<std::size_t>(id)].mood;
    }

    std::optional<Mood> key_mood(std::string_view key)
    {
        if (!key.starts_with(kKeyPrefix))
            return std::nullopt;
        key.remove_prefix(kKeyPrefix.size());
        while (!key.empty() && key.back() >= '0' && key.back() <= '9')
            key.remove_suffix(1);
        const auto it = std::ranges::find(kThoughts, key, &Thought::name);
        if (it == kThoughts.begin() || it == kThoughts.end())
            return std::nullopt;
        return it->mood;
    }

    Mood happiness_mood(float happiness)
    {
        if (happiness > 2.0f / 3)
            return Mood::Happy;
        return happiness < 1.0f / 3 ? Mood::Sad : Mood::Neutral;
    }

    std::string mood_icon(Mood mood)
    {
        constexpr std::array<std::string_view, 3> kIcons { "Guest_Thought_Neutral", "Guest_Thought_Happy", "Guest_Thought_Sad" };
        return std::string(kIcons[std::to_underlying(mood)]);
    }

    std::string thought_text_key(const StringTable& strings, std::int32_t id, std::int64_t variant)
    {
        const std::string key = thought_key(id);
        if (key.empty())
            return { };
        int count = 0;
        while (count < kMostTexts && strings.contains(std::format("{}{}", key, count + 1)))
            ++count;
        if (count == 0)
            return { };
        return std::format("{}{}", key, (variant % count + count) % count + 1);
    }

    std::u16string thought_text(const StringTable& strings, std::int32_t id, std::int64_t variant, std::u16string_view name)
    {
        const std::string key = thought_text_key(strings, id, variant);
        if (key.empty())
            return { };
        std::u16string text(strings.get(key));
        for (auto at = text.find(kNameMark); at != std::u16string::npos; at = text.find(kNameMark, at + name.size()))
            text.replace(at, kNameMark.size(), name);
        return text;
    }

} // namespace rct3::ui
