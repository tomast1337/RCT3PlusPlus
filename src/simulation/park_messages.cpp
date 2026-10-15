// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/park_messages.hpp"

#include <algorithm>
#include <string_view>
#include <utility>

namespace rct3::simulation
{

    namespace
    {

        /// Saved text keys name the text item of a string resource; the table is keyed without it.
        constexpr std::string_view kTextSuffix = ":txt";

        std::string table_key(std::string key)
        {
            if (key.ends_with(kTextSuffix))
                key.resize(key.size() - kTextSuffix.size());
            return key;
        }

    } // namespace

    ParkMessages park_messages(const save::MessageLog& log)
    {
        ParkMessages messages;
        for (const save::Message& m : log.ordered)
        {
            ParkMessage message;
            message.text_key = table_key(m.text_key);
            message.age = std::max(m.time, 0.0f);
            message.subject = m.object;
            message.subject_name = m.object_name;
            message.award = m.award;
            post(messages, std::move(message));
        }
        return messages;
    }

    void post(ParkMessages& messages, ParkMessage message)
    {
        message.serial = ++messages.last_serial;
        messages.list.push_back(std::move(message));
        while (messages.list.size() > kMessageCapacity)
            messages.list.pop_front();
    }

    void age(ParkMessages& messages, float game_seconds)
    {
        for (ParkMessage& m : messages.list)
            m.age += game_seconds;
        std::erase_if(messages.list, [](const ParkMessage& m) { return m.age > kMessageLifetime; });
    }

} // namespace rct3::simulation
