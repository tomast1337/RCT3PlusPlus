// SPDX-License-Identifier: GPL-3.0-or-later
//
// A movable window: background, frame, a close button in the title bar and a content area for the
// widgets a window class adds. Dragging the title bar moves it; it never leaves the desktop, so a window
// left near an edge comes back into view when the game window shrinks. It takes the pointer everywhere
// inside it, so clicks on empty parts do not reach the 3D view.
//
// A resizable window also follows its edges and corners: pressing within a thin band inside an edge drags
// that edge, and a corner drags both of its edges. Its size stays between min_size and the desktop's. The
// content is laid out again at the new size, so window classes anchor their widgets to the edges they
// should follow.
//
// Escape closes the window when the widget with the keyboard focus inside it does not use the key, or
// clicks its cancel button when it has one. Enter that nothing inside uses clicks its default button.
//
// Window classes derive from it, add their widgets to content() in the constructor and handle them in an
// update() override that calls Window::update() first.
#pragma once

#include "ui/font_set.hpp"
#include "ui/widget.hpp"
#include "ui/widgets/button.hpp"

#include <cstdint>
#include <string>

namespace rct3::ui
{

    /// The skins and measurements a window is drawn with. Lengths in logical units.
    struct WindowStyle
    {
        std::string frame;
        std::string frame_unfocused;
        std::string background;
        std::string title_button; ///< Skin family of the title bar buttons
        std::string close_icon;
        float title_height = 20;               ///< Band along the top that drags the window
        Rect background_inset { 6, 6, 6, 6 };  ///< Background drawn this far in from the frame's outer edges
        bool tile_background = false;          ///< Repeat the background skin at its own size instead of stretching it
        Rect icon_box { 6, 17, 52, 63 };       ///< From the top-left corner: the icon is centred in it at its own size
        Rect content_inset { 12, 28, 12, 12 }; ///< Content area in from the left, top, right and bottom edges
        float title_button_size = 14;
        Point title_button_margin { 10, 6 }; ///< From the top-right corner
        FontRole title_font = FontRole::Medium;
        Rect title_box { 12, 2, 40, 20 }; ///< Title text: left inset, top, right inset, bottom; tops and bottoms from the top edge
        Color title_colour { 0, 0, 0, 255 };
        float resize_band = 6;             ///< Inside each edge of a resizable window, the part that drags it
        float resize_corner = 16;          ///< Along each edge from a corner, the part that drags both edges
        Color grip_colour { 0, 0, 0, 96 }; ///< Marks in the bottom-right corner of a resizable window
    };

    class Window : public Widget
    {
    public:
        /// Centred on the desktop at `width` x `height`.
        Window(std::string name, const WindowStyle& style, float width, float height);

        std::u16string title;
        std::string icon; ///< Skin in the frame's top-left corner, if any
        bool resizable = false;
        Point min_size;                   ///< Smallest width and height a resize leaves; the size it was made at caps it
        Button* default_button = nullptr; ///< In this window: clicked by Enter that nothing inside uses
        Button* cancel_button = nullptr;  ///< In this window: clicked by Escape instead of closing it

        /// Whether an edge of the window is following the pointer.
        [[nodiscard]] bool resizing() const
        {
            return edges_ != 0;
        }

        /// Makes the window `height` tall, keeping its top edge where it is.
        void set_height(float height);

        [[nodiscard]] Widget& content()
        {
            return *content_;
        }
        [[nodiscard]] bool dragging() const
        {
            return dragging_;
        }

        [[nodiscard]] bool takes_pointer() const override
        {
            return true;
        }
        void on_press(Point p) override;
        void on_drag(Point p) override;
        void on_release(Point p, bool inside) override;
        bool on_key(const KeyPress& key) override;
        void update(const UpdateContext& context) override;
        void draw(DrawContext& context) const override;

    protected:
        [[nodiscard]] Button& close_button()
        {
            return *close_;
        }

    private:
        enum Edge : std::uint8_t
        {
            Left = 1,
            Top = 2,
            Right = 4,
            Bottom = 8,
        };

        /// The edges a press at `p` takes hold of, as Edge bits; 0 when none.
        [[nodiscard]] std::uint8_t edges_at(Point p) const;
        void resize_to(Point p);

        WindowStyle style_;
        Widget* content_ = nullptr;
        Button* close_ = nullptr;
        bool dragging_ = false;
        Point grab_;
        std::uint8_t edges_ = 0;
        Rect start_rect_; // Of a resize, so limits do not make the edge drift from the pointer
    };

} // namespace rct3::ui
