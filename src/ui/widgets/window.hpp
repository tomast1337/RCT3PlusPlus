// SPDX-License-Identifier: GPL-3.0-or-later
//
// A movable window: background, frame, a close button in the title bar and a content area for the
// widgets a window class adds. Dragging the title bar moves it; it never leaves the desktop, so a window
// left near an edge comes back into view when the game window shrinks. It takes the pointer everywhere
// inside it, so clicks on empty parts do not reach the 3D view.
//
// Window classes derive from it, add their widgets to content() in the constructor and handle them in an
// update() override that calls Window::update() first.
#pragma once

#include "ui/widget.hpp"
#include "ui/widgets/button.hpp"

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
        float background_inset = 6;            ///< Background drawn this far in from the frame's outer edge
        Rect content_inset { 12, 28, 12, 12 }; ///< Content area in from the left, top, right and bottom edges
        float title_button_size = 14;
        Point title_button_margin { 10, 6 }; ///< From the top-right corner
    };

    class Window : public Widget
    {
    public:
        /// Centred on the desktop at `width` x `height`.
        Window(std::string name, const WindowStyle& style, float width, float height);

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
        void update() override;
        void draw(DrawContext& context) const override;

    private:
        WindowStyle style_;
        Widget* content_ = nullptr;
        Button* close_ = nullptr;
        bool dragging_ = false;
        Point grab_;
    };

} // namespace rct3::ui
