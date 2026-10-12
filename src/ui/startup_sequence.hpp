// SPDX-License-Identifier: GPL-3.0-or-later
//
// The logo sequence shown when the game starts, before the front end. Each step fades a full-screen logo
// in over one second, keeps it up, then fades it out over one second; the next step starts when the fade
// out ends. A step's time counts from the start of its fade in. Skipping during a skippable step ends the
// whole sequence at once, without a fade.
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rct3::ui
{

    inline constexpr float kStartupFadeSeconds = 1;
    inline constexpr float kStartupLogoSeconds = 4;
    inline constexpr float kStartupWarningSeconds = 6;

    struct StartupStep
    {
        std::string image; ///< Texture name in the banner overlay
        float seconds = kStartupLogoSeconds;
        bool skippable = true;
    };

    struct StartupOptions
    {
        bool simplified_chinese = false; ///< Starts with a warning screen that cannot be skipped
        bool soaked = true;              ///< Show the first expansion's logo
        bool wild = true;                ///< Show the second expansion's logo
    };

    /// The steps for an English install: developer logo, game logo, then one logo per expansion.
    [[nodiscard]] std::vector<StartupStep> startup_steps(const StartupOptions& options);

    struct StartupFrame
    {
        std::string_view image;
        float opacity = 0; ///< 0 transparent .. 1 opaque
    };

    class StartupSequence
    {
    public:
        explicit StartupSequence(std::vector<StartupStep> steps);

        /// Advances by `seconds`. `skip` is true on frames where the player pressed something.
        void update(float seconds, bool skip);

        [[nodiscard]] bool finished() const
        {
            return step_ >= steps_.size();
        }
        /// What to draw now; nothing once finished.
        [[nodiscard]] std::optional<StartupFrame> frame() const;

    private:
        std::vector<StartupStep> steps_;
        std::size_t step_ = 0;
        float elapsed_ = 0; ///< Since the current step started fading in; past `seconds` it is fading out
    };

} // namespace rct3::ui
