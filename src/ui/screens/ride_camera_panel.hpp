// SPDX-License-Identifier: GPL-3.0-or-later
//
// The panel shown while the view rides along in a car, the only one on screen then: in the bottom-right
// corner, a line naming the ride, the train and the car, and under it buttons to pin the panel, to leave, and
// to step to the previous or next camera of the train and to the previous or next train. A step there is
// nowhere to go to is greyed out.
//
// Once the pointer has been still a while the panel slides down out of sight, unless it is pinned or the
// pointer is over it, and it slides back up as soon as the pointer moves. The owner tells it how far the
// pointer moved each frame, and takes what the player asked for with take_action().
#pragma once

#include "ui/string_table.hpp"
#include "ui/widgets/button.hpp"
#include "ui/widgets/label.hpp"
#include "ui/widgets/panel.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace rct3::ui
{

    struct RideCameraPanelStyle
    {
        std::string back;   ///< Behind the whole panel
        std::string button; ///< Each button's face
        std::string pin;    ///< To pin it
        std::string unpin;  ///< Pinned, to let it hide again
        std::string leave;
        /// Previous camera, next camera, previous train, next train; and the same with nowhere to go.
        std::array<std::string, 4> steps;
        std::array<std::string, 4> no_steps;
        Color text_colour = hex("#ffffff");
    };

    enum class RideCameraAction : std::uint8_t
    {
        Leave,
        PreviousCamera,
        NextCamera,
        PreviousTrain,
        NextTrain,
    };

    namespace ride_camera_panel
    {
        inline constexpr float kStillSeconds = 3;    ///< The pointer still this long, it hides
        inline constexpr float kSlideSeconds = 0.3f; ///< To slide fully in or out
        inline constexpr float kWakeDistance = 2;    ///< Units the pointer moves in a frame to bring it back
    } // namespace ride_camera_panel

    class RideCameraPanel : public Widget
    {
    public:
        RideCameraPanel(std::string name, RideCameraPanelStyle style, const StringTable& strings);

        /// What its line says, and whether there are other cameras on the train and other trains to step to.
        void show(std::u16string info, bool cameras, bool trains);
        /// The pointer moved `distance` units since the last update.
        void pointer_moved(float distance);
        /// What a button asked for since the last call.
        [[nodiscard]] std::optional<RideCameraAction> take_action()
        {
            return std::exchange(asked_, std::nullopt);
        }
        /// 1 fully in sight, 0 out of it.
        [[nodiscard]] float shown() const
        {
            return shown_;
        }
        [[nodiscard]] bool pinned() const
        {
            return pin_->on;
        }
        [[nodiscard]] const Label& info() const
        {
            return *info_;
        }
        [[nodiscard]] Button& button(RideCameraAction action) const
        {
            return *buttons_.at(static_cast<std::size_t>(action));
        }
        [[nodiscard]] Button& pin() const
        {
            return *pin_;
        }

        [[nodiscard]] bool takes_pointer() const override
        {
            return true;
        }
        void update(const UpdateContext& context) override;

    private:
        RideCameraPanelStyle style_;
        const StringTable& strings_;
        Label* info_ = nullptr;
        Button* pin_ = nullptr;
        std::array<Button*, 5> buttons_ { }; ///< By RideCameraAction
        std::optional<RideCameraAction> asked_;
        float shown_ = 1;
        float still_ = 0; ///< Seconds the pointer has been still
    };

} // namespace rct3::ui
