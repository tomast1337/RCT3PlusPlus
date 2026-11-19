// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/slideshow.hpp"

#include "core/types.hpp"
#include "image/picture_file.hpp"
#include "ui/screens/front/layout.hpp"

#include <algorithm>
#include <random>
#include <utility>

namespace rct3::ui
{

    namespace
    {

        using screens::front::front_screen::kDesignHeight;
        using screens::front::front_screen::kDesignWidth;

        constexpr f32 kEdge = 80; ///< Of the front screen's artwork, at the design size

    } // namespace

    std::vector<std::filesystem::path> slideshow_pictures(std::span<const std::filesystem::path> folders, u32 seed)
    {
        std::vector<std::filesystem::path> out;
        std::mt19937 random(seed);
        for (const auto& folder : folders)
        {
            std::ranges::move(image::list_pictures(folder), std::back_inserter(out));
            std::ranges::shuffle(out, random);
        }
        return out;
    }

    Rect slideshow_rect(f32 width, f32 height)
    {
        if (width <= 0 || height <= 0)
            return { };
        const f32 left = kEdge * width / kDesignWidth;
        const f32 bottom = kEdge * height / kDesignHeight * height / width;
        return { left, 0, width, height - bottom };
    }

    Slideshow::Slideshow(std::vector<std::filesystem::path> pictures, SlideshowTiming timing)
        : timing_(timing)
    {
        set_pictures(std::move(pictures));
    }

    void Slideshow::set_pictures(std::vector<std::filesystem::path> pictures)
    {
        pictures_ = std::move(pictures);
        under_ = 0;
        over_ = pictures_.size() > 1 ? 1 : 0;
    }

    void Slideshow::update(units::Seconds elapsed)
    {
        if (pictures_.empty() || elapsed <= units::Seconds { })
            return;
        time_ += elapsed;
        const units::Seconds cycle = timing_.fade + timing_.hold;
        if (cycle <= units::Seconds { })
            return;
        while (time_ >= cycle)
        {
            time_ -= cycle;
            under_ = over_;
            over_ = (over_ + 1) % pictures_.size();
        }
    }

    Slideshow::Shown Slideshow::shown() const
    {
        if (pictures_.empty())
            return { };
        const f32 fade = timing_.fade.value;
        const f32 alpha = fade > 0 ? std::min(time_.value / fade, 1.0f) : 1.0f;
        return { under_, over_, alpha };
    }

} // namespace rct3::ui
