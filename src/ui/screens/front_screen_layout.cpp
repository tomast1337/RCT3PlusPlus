// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/front_screen_layout.hpp"

#include <algorithm>
#include <cmath>

namespace rct3::ui::front_screen
{

    Frame frame_for(const Rect& screen, bool uniform)
    {
        Frame f;
        f.screen = screen;
        f.origin = { screen.x0, screen.y0 };
        f.sx = screen.width() / kDesignWidth;
        f.sy = screen.height() / kDesignHeight;
        f.round = std::min(f.sx, f.sy);
        if (uniform)
        {
            f.sx = f.round;
            f.sy = f.round;
            f.origin.y = screen.y1 - kDesignHeight * f.round;
        }
        return f;
    }

    Point on_ring(float angle, float radius)
    {
        // Anticlockwise on the screen, where y grows downwards.
        return { kRingCentre.x + radius * std::cos(angle), kRingCentre.y - radius * std::sin(angle) };
    }

    float menu_angle(std::size_t index)
    {
        return kMenuFirstAngle + kMenuAngleStep * static_cast<float>(index);
    }

    Rect button_rect(const Frame& frame, Point centre, float size)
    {
        const Point c = frame.at(centre);
        const float half = size * frame.round / 2;
        return { c.x - half, c.y - half, c.x + half, c.y + half };
    }

    LabelBox label_box(const Frame& frame, std::size_t index)
    {
        // The ring round the button is drawn at the round scale, so it stays a circle that hugs the button;
        // so is the box's height and where the band starts under the ring. Only the band's far end, and the
        // text's, stretch across the screen with the artwork.
        const Point design = on_ring(menu_angle(index), kMenuRadius);
        const Point button = frame.at(design);
        const Point corner { button.x + kLabelBoxOffset.x * frame.round, button.y + kLabelBoxOffset.y * frame.round };
        const float far = frame.at({ design.x + kLabelBoxOffset.x, 0 }).x; // The box's left edge, stretched
        const auto round_x = [&](float x) { return corner.x + x * frame.round; };
        const auto stretched_x = [&](float x) { return far + x * frame.sx; };
        const auto y = [&](float v) { return corner.y + v * frame.round; };
        const float reach = kLabelEndStep * static_cast<float>(index * index);
        const Rect patch { round_x(kLabelPatch.x0), y(kLabelPatch.y0), round_x(kLabelPatch.x1), y(kLabelPatch.y1) };
        const Rect band { round_x(kLabelBand.x0), y(kLabelBand.y0), stretched_x(kLabelBand.x1), y(kLabelBand.y1) };
        const Rect end { band.x1, band.y0, stretched_x(kLabelBand.x1 + reach), band.y1 };
        const Rect text { round_x(kLabelText.x0), y(kLabelText.y0), std::max(round_x(kLabelText.x0), stretched_x(kLabelText.x1)), y(kLabelText.y1) };
        return { patch, band, end, text };
    }

    Rect badge_rect(const Frame& frame, std::size_t slot)
    {
        // Turning by a positive angle with y down goes clockwise on the screen.
        const float length = std::hypot(kBadgeFirstDirection.x, kBadgeFirstDirection.y);
        const float x = kBadgeFirstDirection.x / length;
        const float y = kBadgeFirstDirection.y / length;
        const float turn = kBadgeStep * static_cast<float>(slot + 1);
        const Point corner { kRingCentre.x + kBadgeRadius * (x * std::cos(turn) - y * std::sin(turn)), kRingCentre.y + kBadgeRadius * (x * std::sin(turn) + y * std::cos(turn)) };
        const Point at = frame.at(corner);
        const float x0 = std::floor(at.x);
        const float y1 = std::floor(at.y);
        return { x0, y1 - kBadgeSize, x0 + kBadgeSize, y1 };
    }

    bool ease(Polar& current, const Polar& goal, float elapsed)
    {
        const float k = std::max(0.0f, elapsed) * kEaseRate;
        const auto toward = [k](float from, float to) { return (from + to * k) / (1 + k); };
        if (std::abs(current.angle - goal.angle) > kAngleClose)
        {
            current.angle = toward(current.angle, goal.angle);
            return false;
        }
        current.angle = goal.angle;
        if (std::abs(current.radius - goal.radius) > kRadiusClose)
        {
            current.radius = toward(current.radius, goal.radius);
            return false;
        }
        current.radius = goal.radius;
        return true;
    }

} // namespace rct3::ui::front_screen
