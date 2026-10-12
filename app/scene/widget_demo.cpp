// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/widget_demo.hpp"

#include "ui/game_style.hpp"
#include "ui/scale.hpp"
#include "ui/skin_set.hpp"
#include "ui/widgets/button.hpp"
#include "ui/widgets/panel.hpp"
#include "ui/widgets/tickbox.hpp"
#include "ui/widgets/window.hpp"

#include <string>

namespace rct3::scene
{

    namespace
    {

        namespace skins = ui::game_skins;

        constexpr std::string_view kToggleFamily = "Staff_TaskButtonBins";
        constexpr float kWidth = 320;
        constexpr float kHeight = 270;
        constexpr float kCascade = 28; ///< How far each new window opens from the last

        class DemoWindow : public ui::Window
        {
        public:
            DemoWindow(std::string name, float cascade)
                : ui::Window(std::move(name), skins::window_style(), kWidth, kHeight)
            {
                placement = ui::centred(kWidth, kHeight, cascade, cascade);
                ui::Widget& body = content();

                // The list area stretches with the window; the row below it stays on the bottom edge.
                auto& list = body.add<ui::Panel>("list", std::string(skins::kListBox));
                list.placement = { { 0, 0, 1, 1 }, { 0, 0, 0, -44 } };

                for (int i = 0; i < 3; ++i)
                {
                    auto& tick = body.add<ui::Tickbox>("option" + std::to_string(i), std::string(skins::kTickbox), std::string(skins::kTick));
                    tick.placement = ui::bottom_left(static_cast<float>(i) * 20, 22, 12, 12);
                    tick.ticked = i == 0;
                }
                auto& toggle = body.add<ui::Button>("toggle", std::string(kToggleFamily));
                toggle.toggle = true;
                toggle.placement = ui::bottom_left(70, 18, 20, 20);

                cancel_ = &body.add<ui::Button>("cancel", std::string(skins::kButton), std::string(skins::kCancelIcon));
                cancel_->placement = ui::bottom_right(0, 4, 30, 30);
                confirm_ = &body.add<ui::Button>("confirm", std::string(skins::kButton), std::string(skins::kConfirmIcon));
                confirm_->placement = ui::bottom_right(36, 4, 30, 30);
            }

            void update() override
            {
                ui::Window::update();
                if (cancel_->take_click())
                    close();
                if (confirm_->take_click() && parent() != nullptr)
                {
                    const auto n = static_cast<float>(parent()->children().size());
                    parent()->add<DemoWindow>("window" + std::to_string(parent()->children().size()), n * kCascade);
                }
            }

        private:
            ui::Button* confirm_ = nullptr;
            ui::Button* cancel_ = nullptr;
        };

        float scale_for(const render::Device& device)
        {
            return ui::auto_scale(static_cast<float>(device.extent().height));
        }

    } // namespace

    std::expected<WidgetDemo, std::string> create_widget_demo(render::Device& device, const std::filesystem::path& path)
    {
        auto set = ui::load_skin_set(path);
        if (!set)
            return std::unexpected(set.error());

        WidgetDemo demo;
        auto renderer = ui::create_ui_renderer(device);
        if (!renderer)
            return std::unexpected(renderer.error());
        demo.renderer = *renderer;

        auto textures = ui::upload_textures(device, set->images);
        if (!textures)
        {
            destroy_widget_demo(device, demo);
            return std::unexpected(textures.error());
        }
        demo.textures = std::move(*textures);
        demo.skins = ui::skin_library(set->skins, demo.textures);
        demo.desktop.root().add<DemoWindow>("window0", 0.0f);
        return demo;
    }

    bool update_widget_demo(render::Device& device, WidgetDemo& demo, const ui::PointerState& pointer_pixels)
    {
        const float scale = scale_for(device);
        const render::Extent extent = device.extent();
        demo.desktop.layout({ 0, 0, static_cast<float>(extent.width) / scale, static_cast<float>(extent.height) / scale });

        // Something to show again once every window is closed.
        if (demo.desktop.root().children().empty())
            demo.desktop.root().add<DemoWindow>("window0", 0.0f);

        ui::PointerState pointer = pointer_pixels;
        pointer.position = { pointer.position.x / scale, pointer.position.y / scale };
        return demo.desktop.update(pointer);
    }

    void draw_widget_demo(render::Device& device, WidgetDemo& demo)
    {
        demo.list.clear();
        demo.list.set_scale(scale_for(device));
        ui::DrawContext context { demo.list, demo.skins };
        demo.desktop.draw(context);
        ui::render_draw_list(device, demo.renderer, demo.list);
    }

    void destroy_widget_demo(render::Device& device, WidgetDemo& demo)
    {
        ui::destroy_textures(device, demo.textures);
        ui::destroy_ui_renderer(device, demo.renderer);
        demo = { };
    }

} // namespace rct3::scene
