// SPDX-License-Identifier: GPL-3.0-or-later
#include "career/prf_import.hpp"

#include "save/save_file.hpp"
#include "save/value.hpp"

#include <cstdio>
#include <limits>
#include <string_view>

namespace rct3::career
{

    namespace
    {

        // Class and attribute names, as the format names them.
        constexpr std::string_view kClass = "ManagedPlayerProfile";
        constexpr int kExpansions = 2;

        std::int32_t integer(const save::Value* v, std::int32_t fallback)
        {
            if (v == nullptr || v->kind() != save::ValueKind::Integer)
                return fallback;
            const std::int64_t n = v->as_integer();
            if (n < std::numeric_limits<std::int32_t>::min() || n > std::numeric_limits<std::int32_t>::max())
                return fallback;
            return static_cast<std::int32_t>(n);
        }

        std::string text(const save::Value* v)
        {
            return v != nullptr && v->kind() == save::ValueKind::String ? std::string(v->text()) : std::string { };
        }

        ScenarioStatus status_of(std::int32_t status)
        {
            switch (status)
            {
                case 1:
                case 2:
                    return ScenarioStatus::Open;
                case 3:
                    return ScenarioStatus::Apprentice;
                case 4:
                    return ScenarioStatus::Entrepreneur;
                case 5:
                    return ScenarioStatus::Tycoon;
                default:
                    return ScenarioStatus::Locked;
            }
        }

    } // namespace

    const char* to_string(PrfError e) noexcept
    {
        switch (e)
        {
            case PrfError::NotASave:
                return "not a career file";
            case PrfError::NoCareer:
                return "no career in the file";
            case PrfError::UnknownPack:
                return "a career of an unknown game";
        }
        return "unknown";
    }

    std::expected<PrfCareer, PrfError> read_prf(std::span<const std::byte> bytes)
    {
        const auto file = save::read_save(bytes, { .verify_checksum = false });
        if (!file)
            return std::unexpected(PrfError::NotASave);
        for (const save::Object& o : file->objects)
        {
            if (o.class_index >= file->classes.size() || file->classes[o.class_index].name != kClass)
                continue;
            const auto decoded = save::decode_object(*file, o);
            if (!decoded)
                continue;
            PrfCareer prf;
            prf.name = text(decoded->find("PlayerName"));
            const std::int32_t pack = integer(decoded->find("AOPIndex"), -1);
            if (pack < -1 || pack >= kExpansions)
                return std::unexpected(PrfError::UnknownPack);
            prf.pack = pack + 1;
            prf.current = integer(decoded->find("CurrentScenario"), 0);
            if (const save::Value* list = decoded->find("ScenarioData"); list != nullptr && list->kind() == save::ValueKind::List)
            {
                for (const save::Value& item : list->items())
                    prf.scenarios.push_back({ integer(item.field("Status"), 0), text(item.field("LastSaveName")) });
            }
            return prf;
        }
        return std::unexpected(PrfError::NoCareer);
    }

    std::string prf_scenario_folder(std::size_t index)
    {
        std::array<char, 16> name { };
        std::snprintf(name.data(), name.size(), "Scenario%02zu", index + 1);
        return name.data();
    }

    Career career_from_prf(const PrfCareer& prf, std::span<const std::string> ids)
    {
        Career career;
        career.name = prf.name;
        career.pack = prf.pack;
        for (std::size_t i = 0; i < ids.size(); ++i)
        {
            ScenarioProgress p { .id = ids[i] };
            if (i < prf.scenarios.size())
            {
                p.status = status_of(prf.scenarios[i].status);
                if (!prf.scenarios[i].last_save.empty())
                    p.last_save = prf_scenario_folder(i) + "/" + prf.scenarios[i].last_save;
            }
            career.scenarios.push_back(std::move(p));
        }
        if (prf.current >= 0 && static_cast<std::size_t>(prf.current) < ids.size())
            career.current = static_cast<std::size_t>(prf.current);
        open_earned(career);
        return career;
    }

} // namespace rct3::career
