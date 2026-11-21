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
// the messages with show(), as text ready to draw, and hears of new ones through take_alert(). Every message has
// an icon before its text, standing for what it is about; a click on an icon that leads somewhere is heard
// through take_clicked().
#pragma once

#include "core/types.hpp"
#include "save/model/rides.hpp"
#include "simulation/park/messages.hpp"
#include "ui/string_table.hpp"
#include "ui/widgets/button.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace rct3::ui::screens::park_view
{

    struct MessageDrawerStyle
    {
        std::string lip_left;  ///< The left half of the lip along the bottom
        std::string lip_right; ///< And its right half
        std::string back;      ///< Repeated up the drawer above the lip
        std::string older;     ///< The arrow to older messages; its family has a disabled member
        std::string newer;     ///< And to newer ones
        std::string locate;    ///< The camera: the icon of a message about something with no icon of its own
        std::string award;     ///< Of award and fine messages; it opens the park's awards
        std::string objective; ///< Of messages about the scenario's goals
        std::string inspector; ///< Of the park inspector's reports
        Color text_colour = hex("#000000");
        Color stripe = hex("#ffffff");        ///< Behind the newest message and every second one up from it
        Color stripe_alt = hex("#e5e5e5");    ///< Behind the others
        units::Seconds double_press { 0.4f }; ///< A second press this soon opens or closes (ours)
    };

    /// One message as the drawer shows it.
    struct DrawerMessage
    {
        u64 serial = 0; ///< Grows with each new message; see simulation::park::ParkMessage
        std::u16string text { };
        std::string icon { };   ///< Skin before the text
        std::string mood { };   ///< A thought's face between the icon and the text, for a message telling a thought
        bool clickable = false; ///< A click on the icon leads to what the message is about
    };

    namespace message_drawer
    {
        inline constexpr f32 kWidth = 480;
        inline constexpr f32 kLeft = -258;                     ///< From the middle of the screen to the drawer's left edge
        inline constexpr f32 kTop = 4;                         ///< From the top of the screen; the top never moves
        inline constexpr f32 kClosed = 22;                     ///< Height when closed, all of it behind the bar
        inline constexpr f32 kOpen = 360;                      ///< Height when fully open
        inline constexpr units::PixelPace kSlideSpeed { 200 }; ///< It slides by itself
        inline constexpr units::Seconds kHold { 8 };           ///< It stays down this long to show new messages
    } // namespace message_drawer

    /// The text of `message`: its string with the subject's name, or the award's, filled in. A name that is
    /// not known is left out along with the separator before it.
    [[nodiscard]] std::u16string message_text(const simulation::park::ParkMessage& message, const StringTable& strings);

    /// Names each message about one of `rides` or `stalls` as the attraction's window names it, so a ride that
    /// kept its default name reads as its kind and number. A message keeps the name it was posted with, unless
    /// that is the attraction's name as stored.
    void name_subjects(simulation::park::ParkMessages& messages, std::span<const save::model::Ride> rides, std::span<const save::model::Stall> stalls, const StringTable& strings);

    /// The drawer's messages, oldest first, each with the icon its kind has: an award's, a goal's, the inspector's,
    /// else the camera. Only an award's icon is clickable; the owner gives messages about an attraction theirs.
    /// A message whose text is a thought, such as the inspector's reports, also has that thought's face.
    [[nodiscard]] std::vector<DrawerMessage> drawer_messages(const simulation::park::ParkMessages& messages, const StringTable& strings, const MessageDrawerStyle& style);

    /// The drawer's back and lip over `rect`, the drawer's own rectangle, without its messages.
    void draw_drawer_frame(DrawContext& context, const MessageDrawerStyle& style, const Rect& rect);

    class MessageDrawer : public Widget
    {
    public:
        MessageDrawer(std::string name, MessageDrawerStyle style, const StringTable& strings);

        /// The messages to show, oldest first. Those with a serial above any shown before are new; the
        /// first call takes all it is given as already seen.
        void show(std::span<const DrawerMessage> messages);
        /// The serial of the message whose icon was clicked, once.
        [[nodiscard]] std::optional<u64> take_clicked()
        {
            return std::exchange(clicked_, std::nullopt);
        }
        /// True once after new messages arrived.
        [[nodiscard]] bool take_alert()
        {
            return std::exchange(alert_, false);
        }

        [[nodiscard]] const MessageDrawerStyle& style() const
        {
            return style_;
        }
        [[nodiscard]] f32 height() const
        {
            return height_;
        }
        /// How many of the newest messages are skipped, scrolled past by the arrows.
        [[nodiscard]] usize skipped() const
        {
            return skipped_;
        }
        [[nodiscard]] widgets::Button& older_button() const
        {
            return *older_;
        }
        [[nodiscard]] widgets::Button& newer_button() const
        {
            return *newer_;
        }

        [[nodiscard]] bool takes_pointer() const override
        {
            return true;
        }
        /// Pulled up and down anywhere but on an icon that can be clicked.
        [[nodiscard]] CursorKind cursor(Point p) const override
        {
            return icon_at(p) ? CursorKind::Hand : CursorKind::ResizeVertical;
        }
        void on_press(Point p) override;
        void on_drag(Point p) override;
        void on_release(Point p, bool inside) override;
        bool on_wheel(Point notches) override;
        void update(const UpdateContext& context) override;
        void draw(DrawContext& context) const override;

    private:
        enum class Slide : u8
        {
            Resting,
            Opening, ///< Down to show the new messages
            Holding,
            Closing, ///< Back up to where it rests
        };

        void set_height(f32 height);
        /// From the drawer's bottom, the top of the `count` newest rows after those skipped.
        [[nodiscard]] f32 rows_top(usize count) const;
        void step(std::ptrdiff_t older);
        /// The serial of the message whose clickable icon is at `p`, if any.
        [[nodiscard]] std::optional<u64> icon_at(Point p) const;
        void draw_inside(DrawContext& context, const Rect& r) const;

        MessageDrawerStyle style_;
        std::vector<DrawerMessage> messages_; ///< Newest first
        std::vector<f32> row_heights_;        ///< Of each message, newest first, as of the last update
        u64 seen_ = 0;
        bool shown_ = false;
        bool alert_ = false;
        std::optional<u64> clicked_;
        f32 height_ = message_drawer::kClosed;
        f32 rest_ = message_drawer::kClosed; ///< Where it goes back to by itself
        Slide slide_ = Slide::Resting;
        usize unseen_ = 0; ///< New messages to slide down for
        units::Seconds held_;
        usize skipped_ = 0;
        bool dragging_ = false;
        f32 drag_from_ = 0;                         ///< Pointer height minus drawer height at the press
        std::optional<units::Seconds> since_press_; ///< Since the first press of a pair
        widgets::Button* older_ = nullptr;
        widgets::Button* newer_ = nullptr;
    };

} // namespace rct3::ui::screens::park_view
