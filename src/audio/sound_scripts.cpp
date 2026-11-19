// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/sound_scripts.hpp"

#include "core/types.hpp"

#include <algorithm>

namespace rct3::audio
{

    f32 Glide::at(units::Seconds now) const
    {
        if (now >= until || until <= start)
            return to;
        if (now <= start)
            return from;
        return from + (to - from) * ((now - start).value / (until - start).value);
    }

    std::optional<ovl::SoundCue> next_cue(std::span<const ovl::SoundStep> script, ScriptPlay& play, units::Seconds now)
    {
        while (play.next_step < script.size() && units::Seconds { script[play.next_step].at } <= now)
        {
            const ovl::SoundStep& step = script[play.next_step++];
            const units::Seconds at { step.at };
            switch (step.cue)
            {
                case ovl::SoundCue::Start:
                case ovl::SoundCue::Stop:
                    return step.cue;
                case ovl::SoundCue::Volume:
                    play.volume = { .from = play.volume.at(at), .to = step.value, .start = at, .until = units::Seconds { step.until } };
                    break;
                case ovl::SoundCue::Pitch:
                    play.pitch = { .from = play.pitch.at(at), .to = step.value, .start = at, .until = units::Seconds { step.until } };
                    break;
            }
        }
        return std::nullopt;
    }

    void restart_script(ScriptPlay& play, units::Seconds was)
    {
        const f32 volume = play.volume.at(was);
        const f32 pitch = play.pitch.at(was);
        play = { .next_step = 0, .volume = { .from = volume, .to = volume }, .pitch = { .from = pitch, .to = pitch } };
    }

    Screamers pick_screamers(ScreamingRiders& riders, f32 roll)
    {
        const usize all = riders.men + riders.women;
        const bool men = static_cast<f32>(riders.men) > std::clamp(roll, 0.0f, 1.0f) * static_cast<f32>(all) || riders.women == 0;
        usize& of = men ? riders.men : riders.women;
        if (of <= 1)
            return men ? Screamers::Man : Screamers::Woman;
        of = 1;
        return men ? Screamers::Men : Screamers::Women;
    }

} // namespace rct3::audio
