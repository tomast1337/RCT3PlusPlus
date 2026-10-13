// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/slideshow.hpp"

#include "image/picture_file.hpp"

#include <algorithm>
#include <random>
#include <utility>

namespace rct3::ui
{

    namespace
    {

        constexpr float kEdge = 74; ///< Of the front screen's artwork, at the reference size
        constexpr float kReferenceWidth = 1024;
        constexpr float kReferenceHeight = 768;

    } // namespace

    std::vector<std::filesystem::path> slideshow_pictures(std::span<const std::filesystem::path> folders, std::uint32_t seed)
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

    Rect slideshow_rect(float width, float height)
    {
        if (width <= 0 || height <= 0)
            return { };
        const float left = kEdge * width / kReferenceWidth;
        const float bottom = kEdge * height / kReferenceHeight * height / width;
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

    void Slideshow::update(float elapsed)
    {
        if (pictures_.empty() || elapsed <= 0)
            return;
        time_ += elapsed;
        const float cycle = timing_.fade + timing_.hold;
        if (cycle <= 0)
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
        const float alpha = timing_.fade > 0 ? std::min(time_ / timing_.fade, 1.0f) : 1.0f;
        return { under_, over_, alpha };
    }

} // namespace rct3::ui
