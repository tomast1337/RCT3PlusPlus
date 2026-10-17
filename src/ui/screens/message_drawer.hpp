// SPDX-License-Identifier: GPL-3.0-or-later
//
// The park's messages, in a drawer that hangs from the middle of the bar along the top of the park view.
// Closed, it hides behind the bar; the round notch in the middle of the bar lets the pointer through to it.
// The newest message sits at the bottom, just above the drawer's lip, and older ones stack upwards on
// alternating stripes.
//
// When messages arrive the drawer slides down until all of them show, waits a few seconds and slides back
// up to where it was. Dragging it up or down sets where it rests; a double press opens it fully or closes
// it. Two arrows on the lip step through older and newer messages when they do not all fit. The owner gives
// the messages with show(), as text ready to draw, and hears of new ones through take_alert().
#pragma once

#include "simulation/park_messages.hpp"
#include "ui/string_table.hpp"
#include "ui/widgets/button.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace rct3::ui
{

    struct MessageDrawerStyle
    {
        std::string lip_left;  ///< The left half of the lip along the bottom
        std::string lip_right; ///< And its right half
        std::string back;      ///< Repeated up the drawer above the lip
        std::string older;     ///< The arrow to older messages; its family has a disabled member
        std::string newer;     ///< And to newer ones
        Color text_colour = hex("#000000");
        Color stripe = hex("#ffffff");     ///< Behind the newest message and every second one up from it
        Color stripe_alt = hex("#e5e5e5"); ///< Behind the others
        float double_press = 0.4f;         ///< Seconds within which a second press opens or closes (ours)
    };

    /// One message as the drawer shows it.
    struct DrawerMessage
    {
        std::uint64_t serial = 0; ///< Grows with each new message; see simulation::ParkMessage
        std::u16string text;
    };

    namespace message_drawer
    {
        inline constexpr float kWidth = 480;
        inline constexpr float kLeft = -258;      ///< From the middle of the screen to the drawer's left edge
        inline constexpr float kTop = 4;          ///< From the top of the screen; the top never moves
        inline constexpr float kClosed = 22;      ///< Height when closed, all of it behind the bar
        inline constexpr float kOpen = 360;       ///< Height when fully open
        inline constexpr float kSlideSpeed = 200; ///< Units a second it slides by itself
        inline constexpr float kHoldSeconds = 8;  ///< It stays down this long to show new messages
    } // namespace message_drawer

    /// The text of `message`: its string with the subject's name, or the award's, filled in. A name that is
    /// not known is left out along with the separator before it.
    [[nodiscard]] std::u16string message_text(const simulation::ParkMessage& message, const StringTable& strings);

    /// The drawer's messages, oldest first.
    [[nodiscard]] std::vector<DrawerMessage> drawer_messages(const simulation::ParkMessages& messages, const StringTable& strings);

    /// The drawer's back and lip over `rect`, the drawer's own rectangle, without its messages.
    void draw_drawer_frame(DrawContext& context, const MessageDrawerStyle& style, const Rect& rect);

    class MessageDrawer : public Widget
    {
    public:
        MessageDrawer(std::string name, MessageDrawerStyle style, const StringTable& strings);

        /// The messages to show, oldest first. Those with a serial above any shown before are new; the
        /// first call takes all it is given as already seen.
        void show(std::span<const DrawerMessage> messages);
        /// True once after new messages arrived.
        [[nodiscard]] bool take_alert()
        {
            return std::exchange(alert_, false);
        }

        [[nodiscard]] float height() const
        {
            return height_;
        }
        /// How many of the newest messages are skipped, scrolled past by the arrows.
        [[nodiscard]] std::size_t skipped() const
        {
            return skipped_;
        }
        [[nodiscard]] Button& older_button() const
        {
            return *older_;
        }
        [[nodiscard]] Button& newer_button() const
        {
            return *newer_;
        }

        [[nodiscard]] bool takes_pointer() const override
        {
            return true;
        }
        /// Pulled up and down anywhere.
        [[nodiscard]] CursorKind cursor(Point) const override
        {
            return CursorKind::ResizeVertical;
        }
        void on_press(Point p) override;
        void on_drag(Point p) override;
        void on_release(Point p, bool inside) override;
        bool on_wheel(Point notches) override;
        void update(const UpdateContext& context) override;
        void draw(DrawContext& context) const override;

    private:
        enum class Slide : std::uint8_t
        {
            Resting,
            Opening, ///< Down to show the new messages
            Holding,
            Closing, ///< Back up to where it rests
        };

        void set_height(float height);
        /// From the drawer's bottom, the top of the `count` newest rows after those skipped.
        [[nodiscard]] float rows_top(std::size_t count) const;
        void step(std::ptrdiff_t older);
        void draw_inside(DrawContext& context, const Rect& r) const;

        MessageDrawerStyle style_;
        std::vector<DrawerMessage> messages_; ///< Newest first
        std::vector<float> row_heights_;      ///< Of each message, newest first, as of the last update
        std::uint64_t seen_ = 0;
        bool shown_ = false;
        bool alert_ = false;
        float height_ = message_drawer::kClosed;
        float rest_ = message_drawer::kClosed; ///< Where it goes back to by itself
        Slide slide_ = Slide::Resting;
        std::size_t unseen_ = 0; ///< New messages to slide down for
        float held_ = 0;
        std::size_t skipped_ = 0;
        bool dragging_ = false;
        float drag_from_ = 0; ///< Pointer height minus drawer height at the press
        float since_press_ = 1e9f;
        Button* older_ = nullptr;
        Button* newer_ = nullptr;
    };

} // namespace rct3::ui
