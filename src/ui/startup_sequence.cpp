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
            steps.push_back({ "SChinese_WarningScreen", kStartupWarningSeconds, false });
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

    void StartupSequence::update(float seconds, bool skip)
    {
        if (finished())
            return;
        if (skip && steps_[step_].skippable)
        {
            step_ = steps_.size();
            return;
        }
        // A long frame may finish a step and run into the next one.
        float left = seconds;
        while (!finished())
        {
            const float until_fade_out = steps_[step_].seconds - elapsed_;
            const float until_next = until_fade_out + kStartupFadeSeconds;
            if (left < until_next)
            {
                elapsed_ += left;
                return;
            }
            left -= until_next;
            ++step_;
            elapsed_ = 0;
        }
    }

    std::optional<StartupFrame> StartupSequence::frame() const
    {
        if (finished())
            return std::nullopt;
        const float fade_in = std::min(elapsed_ / kStartupFadeSeconds, 1.0f);
        const float fade_out = 1.0f - std::max(elapsed_ - steps_[step_].seconds, 0.0f) / kStartupFadeSeconds;
        return StartupFrame { steps_[step_].image, std::clamp(std::min(fade_in, fade_out), 0.0f, 1.0f) };
    }

} // namespace rct3::ui
