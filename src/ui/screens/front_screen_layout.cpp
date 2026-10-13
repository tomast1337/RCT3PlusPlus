// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/front_screen_layout.hpp"

#include <algorithm>
#include <cmath>

namespace rct3::ui::front_screen
{

    Frame frame_for(const Rect& screen)
    {
        Frame f;
        f.screen = screen;
        f.sx = screen.width() / kDesignWidth;
        f.sy = screen.height() / kDesignHeight;
        f.round = std::min(f.sx, f.sy);
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
        const Point button = on_ring(menu_angle(index), kMenuRadius);
        const Point corner { button.x + kLabelBoxOffset.x, button.y + kLabelBoxOffset.y };
        const auto inside = [&](const Rect& part) { return frame.stretched({ corner.x + part.x0, corner.y + part.y0, corner.x + part.x1, corner.y + part.y1 }); };
        const float reach = kLabelEndStep * static_cast<float>(index * index);
        const Rect end { kLabelBand.x1, kLabelBand.y0, kLabelBand.x1 + reach, kLabelBand.y1 };
        return { inside(kLabelPatch), inside(kLabelBand), inside(end), inside(kLabelText) };
    }

    Rect badge_rect(const Frame& frame, std::size_t index)
    {
        // Turning by a positive angle with y down goes clockwise on the screen.
        const float length = std::hypot(kBadgeFirstDirection.x, kBadgeFirstDirection.y);
        const float x = kBadgeFirstDirection.x / length;
        const float y = kBadgeFirstDirection.y / length;
        const float turn = kBadgeStep * static_cast<float>(index + 1);
        const Point corner { kRingCentre.x + kBadgeRadius * (x * std::cos(turn) - y * std::sin(turn)), kRingCentre.y + kBadgeRadius * (x * std::sin(turn) + y * std::cos(turn)) };
        return frame.stretched({ corner.x, corner.y - kBadgeSize, corner.x + kBadgeSize, corner.y });
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
