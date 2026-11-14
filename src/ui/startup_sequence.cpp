// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/startup_sequence.hpp"

#include <algorithm>
#include <utility>

namespace rct3::ui
{

    std::vector<StartupStep> startup_steps(const StartupOptions& options)
    {
        std::vector<StartupStep> steps;
        if (options.simplified_chinese)
            steps.push_back({ "SChinese_WarningScreen", kStartupWarning, false });
        steps.push_back({ "AtariLogo" }); // The publisher's, in early releases only
        steps.push_back({ "FrontierLogo" });
        steps.push_back({ "RCT3Logo" });
        if (options.soaked)
            steps.push_back({ "AOPLogo_0" });
        if (options.wild)
            steps.push_back({ "AOPLogo_1" });
        return steps;
    }

    StartupSequence::StartupSequence(std::vector<StartupStep> steps)
        : steps_(std::move(steps))
    {
    }

    void StartupSequence::update(units::Seconds elapsed, bool skip)
    {
        if (finished())
            return;
        if (skip && steps_[step_].skippable)
        {
            step_ = steps_.size();
            return;
        }
        // A long frame may finish a step and run into the next one.
        units::Seconds left = elapsed;
        while (!finished())
        {
            const units::Seconds until_fade_out = steps_[step_].shown - elapsed_;
            const units::Seconds until_next = until_fade_out + kStartupFade;
            if (left < until_next)
            {
                elapsed_ += left;
                return;
            }
            left -= until_next;
            ++step_;
            elapsed_ = { };
        }
    }

    std::optional<StartupFrame> StartupSequence::frame() const
    {
        if (finished())
            return std::nullopt;
        const float fade_in = std::min(elapsed_.value / kStartupFade.value, 1.0f);
        const float fade_out = 1.0f - std::max((elapsed_ - steps_[step_].shown).value, 0.0f) / kStartupFade.value;
        return StartupFrame { steps_[step_].image, std::clamp(std::min(fade_in, fade_out), 0.0f, 1.0f), elapsed_ };
    }

} // namespace rct3::ui
