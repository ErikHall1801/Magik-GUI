#ifndef MGUI_CONTAINERS_H
#define MGUI_CONTAINERS_H

/*
    mgui containers
    ===============

    Layout *inside* a panel. A container owns widgets and draws them top to bottom; containers are
    widgets themselves, so they nest:

        mgui::panel& settings = layout.add_panel(4, "Settings");

        auto& camera = settings.section("Camera");                      // collapsible header
        camera.drag("Position", store.declare<mgui::vec3>("camera.position", {2, 5, 9}), 0.05f);

        auto& buttons = camera.row({ 1, 1 });                           // horizontal split, weights 1:1
        buttons.button("Render").primary();
        buttons.button("Pause");

    `row` is the in-panel equivalent of the BSP split: it divides the available width by weights. Every
    child receives its share through frame::layout.width, so widgets never need to know where they are.

    Helpers (button, slider, drag, ...) are shorthands for add<W>(...). Any class derived from
    mgui::widget can be added with add<W>(constructor args...).
*/

#include <memory>
#include <numeric>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include "imgui.h"
#include "mgui_core.h"
#include "mgui_widgets.h"

namespace mgui
{
    class section;
    class row;

    class container : public widget_t<container>
    {
    public:
        explicit container(std::string label_ = {}) : widget_t(std::move(label_)) {}

        /**
        * Adds any widget. Returns a reference that stays valid for the container's lifetime.
        */
        template <typename W, typename... Args>
        W& add(Args&&... args)
        {
            static_assert(std::is_base_of_v<widget, W>, "mgui::container::add<W>: W must derive from mgui::widget");
            auto ptr = std::make_unique<W>(std::forward<Args>(args)...);
            W& ref = *ptr;
            children.push_back(std::move(ptr));
            return ref;
        }

        // ### Layout options for everything inside this container (see also .labels(mode)) ###
        container& label_ratio(float r) { ratio_override = r; return *this; }

        // ### Containers ###
        mgui::section& section(std::string title, bool default_open = true);
        mgui::row& row(std::vector<float> weights = {});

        // ### Shorthands ###
        mgui::text& text(std::string s) { return add<mgui::text>(std::move(s)); }
        mgui::text& text(std::function<std::string()> fn) { return add<mgui::text>(std::move(fn)); }
        mgui::readout& readout(std::string label_, std::function<std::string()> fn) { return add<mgui::readout>(std::move(label_), std::move(fn)); }
        mgui::separator& separator(std::string title = {}) { return add<mgui::separator>(std::move(title)); }
        mgui::spacing& spacing(float design_pixels = 6.0f) { return add<mgui::spacing>(design_pixels); }
        mgui::custom& custom(std::function<void(frame&)> fn) { return add<mgui::custom>(std::move(fn)); }

        mgui::button& button(std::string label_) { return add<mgui::button>(std::move(label_)); }
        mgui::checkbox& checkbox(std::string label_, value<bool>& v) { return add<mgui::checkbox>(std::move(label_), v); }

        template <typename T>
        mgui::slider<T>& slider(std::string label_, value<T>& v, std::type_identity_t<T> lo, std::type_identity_t<T> hi) { return add<mgui::slider<T>>(std::move(label_), v, lo, hi); }

        template <typename T>
        mgui::drag<T>& drag(std::string label_, value<T>& v, float speed = 0.01f) { return add<mgui::drag<T>>(std::move(label_), v, speed); }

        template <typename T>
        mgui::spinner<T>& spinner(std::string label_, value<T>& v, std::type_identity_t<T> step) { return add<mgui::spinner<T>>(std::move(label_), v, step); }

        mgui::dropdown& dropdown(std::string label_, value<int>& v, item_source items) { return add<mgui::dropdown>(std::move(label_), v, std::move(items)); }
        mgui::dropdown_text& dropdown(std::string label_, value<std::string>& v, item_source items) { return add<mgui::dropdown_text>(std::move(label_), v, std::move(items)); }
        mgui::list_box& list(std::string label_, value<int>& v, item_source items, int rows = 5) { return add<mgui::list_box>(std::move(label_), v, std::move(items), rows); }
        mgui::segmented& segmented(std::string label_, value<int>& v, item_source items) { return add<mgui::segmented>(std::move(label_), v, std::move(items)); }
        mgui::text_input& text_input(std::string label_, value<std::string>& v) { return add<mgui::text_input>(std::move(label_), v); }

        template <typename T>
        mgui::color_edit<T>& color(std::string label_, value<T>& v) { return add<mgui::color_edit<T>>(std::move(label_), v); }

        template <typename T>
        mgui::plot& plot(std::string label_, T&& data_fn, float height = 50.0f) { return add<mgui::plot>(std::move(label_), std::forward<T>(data_fn), height); }

        size_t size() const { return children.size(); }

    protected:
        frame child_frame(const frame& f) const
        {
            frame sub = f;   // label_override was already applied by widget::draw
            if (ratio_override) sub.layout.label_ratio = *ratio_override;
            return sub;
        }

        void draw_contents(frame& f) override
        {
            frame sub = child_frame(f);
            for (auto& child : children) child->draw(sub);
        }

        std::vector<std::unique_ptr<widget>> children;
        std::optional<float> ratio_override;
    };



    // ###############
    // ### Section ###
    // ###############

    /**
    * Collapsible header. Its open state is remembered by ImGui (imgui.ini).
    */
    class section : public container
    {
    public:
        section(std::string title, bool default_open_) : container(std::move(title)), default_open(default_open_) {}

    protected:
        void draw_contents(frame& f) override
        {
            const ImGuiTreeNodeFlags flags = default_open ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None;
            if (!ImGui::CollapsingHeader(label.c_str(), flags)) return;

            const float indent = px(6.0f);
            frame sub = child_frame(f);
            sub.layout.width = std::max(1.0f, f.layout.width - indent);

            ImGui::Indent(indent);
            for (auto& child : children) child->draw(sub);
            ImGui::Unindent(indent);
            ImGui::Dummy(ImVec2(0.0f, px(2.0f)));
        }

        bool default_open;
    };



    // ###########
    // ### Row ###
    // ###########

    /**
    * Places its children side by side. Weights default to 1 for children without an explicit weight.
    * Inside a row labels go on top by default (a label column per cell would waste space); override
    * with .labels(label_mode::left / none).
    */
    class row : public container
    {
    public:
        explicit row(std::vector<float> weights_) : weights(std::move(weights_)) { label_override = label_mode::top; }

    protected:
        void draw_contents(frame& f) override
        {
            if (children.empty()) return;

            const size_t n = children.size();
            std::vector<float> w(n, 1.0f);
            for (size_t i = 0; i < n && i < weights.size(); i++) w[i] = std::max(0.0f, weights[i]);
            const float total = std::accumulate(w.begin(), w.end(), 0.0f);
            if (total <= 0.0f) return;

            const float gap = ImGui::GetStyle().ItemSpacing.x;
            const float usable = std::max(1.0f, f.layout.width - gap * static_cast<float>(n - 1));
            const ImVec2 start = ImGui::GetCursorScreenPos();

            frame sub = child_frame(f);
            float x = start.x;
            float max_y = start.y;

            for (size_t i = 0; i < n; i++)
            {
                const float cell = usable * (w[i] / total);
                ImGui::SetCursorScreenPos(ImVec2(x, start.y));
                sub.layout.width = std::max(1.0f, cell);
                children[i]->draw(sub);
                max_y = std::max(max_y, ImGui::GetItemRectMax().y);
                x += cell + gap;
            }

            // Leave the cursor below the tallest cell
            ImGui::SetCursorScreenPos(ImVec2(start.x, max_y));
            ImGui::Dummy(ImVec2(f.layout.width, 0.0f));
        }

        std::vector<float> weights;
    };

    inline section& container::section(std::string title, bool default_open) { return add<mgui::section>(std::move(title), default_open); }
    inline row& container::row(std::vector<float> weights) { return add<mgui::row>(std::move(weights)); }



    // #############
    // ### Panel ###
    // #############

    /**
    * Top level container shown in a BSP leaf. Optionally has a right-click context menu, which is just
    * another container drawn in a popup (bind it to the same values as the panel to keep both in sync).
    */
    class panel : public container
    {
    public:
        explicit panel(std::string title_, ImGuiWindowFlags flags_ = default_flags)
            : container(std::move(title_)), flags(flags_) {}

        static constexpr ImGuiWindowFlags default_flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;

        /**
        * Right-click menu of this panel. Created on first use. `width` is in design pixels.
        */
        container& context_menu(float width = 300.0f)
        {
            if (!menu) menu = std::make_unique<container>("context");
            menu_width = width;
            return *menu;
        }

        const std::string& title() const { return label; }
        ImGuiWindowFlags window_flags() const { return flags; }

        panel& padding(bool p) { has_padding = p; return *this; }
        bool padded() const { return has_padding; }

        /**
        * Called by the layout inside ImGui::Begin / End.
        */
        void draw_window(value_store& store)
        {
            frame f{ store, layout_state{} };
            f.layout.width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
            draw(f);

            if (!menu) return;

            const std::string popup_id = "##context_" + label;
            if (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            {
                ImGui::OpenPopup(popup_id.c_str());
            }

            ImGui::SetNextWindowSize(ImVec2(px(menu_width), 0.0f), ImGuiCond_Always);
            if (ImGui::BeginPopup(popup_id.c_str()))
            {
                frame mf{ store, layout_state{} };
                mf.layout.width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
                menu->draw(mf);
                ImGui::EndPopup();
            }
        }

    private:
        ImGuiWindowFlags flags;
        std::unique_ptr<container> menu;
        float menu_width = 300.0f;
        bool has_padding = true;
    };
}

#endif
