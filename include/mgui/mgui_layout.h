#ifndef MGUI_LAYOUT_H
#define MGUI_LAYOUT_H

/*
    mgui BSP panel layout
    =====================

    The same binary space partition the Magik GUI always used, cleaned up:

        mgui::bsp_layout layout;                                   // root leaf has id 0
        layout.split(0, mgui::split_axis::x, 0.8f, 1);             // 0 | 1   (0 keeps the left 80 %)
        layout.split(0, mgui::split_axis::y, 0.75f, 2);            // 0 / 2   (0 keeps the top 75 %)

        mgui::panel& viewport = layout.add_panel(0, "Render viewport");

    split(target, axis, ratio, new_id) turns leaf `target` into two leaves: child A keeps `target`'s id and
    `ratio` of the space, child B gets `new_id`. Dragging a divider moves only that divider; nested
    dividers on the same axis keep their absolute screen position (the old "countermove" behaviour) and
    every panel keeps at least `min_panel_size` design pixels.

    Panels are separated by a small gap that holds the divider handle, so dragging a divider never
    clicks into a panel (scrollbars sit right at the panel edge).

    The divider ratios can be saved as a string (save_ratios / load_ratios) and restored to the values
    they had when mark_default() was called (reset).
*/

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "imgui.h"
#include "mgui_containers.h"
#include "mgui_core.h"

namespace mgui
{
    /**
    * x: children side by side, the divider is vertical. y: children stacked, the divider is horizontal.
    */
    enum class split_axis { x, y };

    struct bsp_node
    {
        split_axis axis = split_axis::x;
        float ratio = 0.5f;

        unsigned int id = 0;

        bsp_node* parent = nullptr;
        std::unique_ptr<bsp_node> a;
        std::unique_ptr<bsp_node> b;

        // Computed every frame, screen pixels
        ImVec2 pos{ 0.0f, 0.0f };
        ImVec2 size{ 0.0f, 0.0f };

        bool leaf() const { return !a && !b; }
        float extent(split_axis ax) const { return ax == split_axis::x ? size.x : size.y; }
    };

    class bsp_layout
    {
    public:
        static constexpr unsigned int no_id = std::numeric_limits<unsigned int>::max();

        bsp_layout() : root(std::make_unique<bsp_node>()) {}

        // ### Configuration ###

        /**
        * Splits leaf `target_id`. Returns false if it is not a leaf or new_id already exists.
        */
        bool split(unsigned int target_id, split_axis axis, float ratio, unsigned int new_id)
        {
            bsp_node* target = find_leaf(root.get(), target_id);
            if (!target || find_leaf(root.get(), new_id)) return false;

            target->axis = axis;
            target->ratio = std::clamp(ratio, 0.01f, 0.99f);

            target->a = std::make_unique<bsp_node>();
            target->b = std::make_unique<bsp_node>();
            target->a->parent = target;
            target->b->parent = target;
            target->a->id = target->id;
            target->b->id = new_id;
            target->id = no_id;
            return true;
        }

        /**
        * Creates the panel shown in leaf `id`. The layout owns it.
        */
        panel& add_panel(unsigned int id, std::string title, ImGuiWindowFlags flags = panel::default_flags)
        {
            auto p = std::make_unique<panel>(std::move(title), flags);
            panel& ref = *p;
            panels[id] = std::move(p);
            return ref;
        }

        panel* find_panel(unsigned int id)
        {
            auto it = panels.find(id);
            return it == panels.end() ? nullptr : it->second.get();
        }

        /**
        * Remembers the current ratios as the ones reset() goes back to.
        */
        void mark_default() { default_ratios = save_ratios(); }
        void reset() { load_ratios(default_ratios); }

        // ### Persistence ###

        std::string save_ratios() const
        {
            std::vector<float> r;
            collect_ratios(root.get(), r);
            std::string out;
            char buf[32];
            for (size_t i = 0; i < r.size(); i++)
            {
                std::snprintf(buf, sizeof(buf), "%s%.4f", i ? " " : "", r[i]);
                out += buf;
            }
            return out;
        }

        /**
        * Applies ratios saved by save_ratios(). Ignored (returns false) if the tree has a different shape.
        */
        bool load_ratios(const std::string& text)
        {
            std::vector<float> r;
            std::istringstream stream(text);
            float v;
            while (stream >> v) r.push_back(v);

            std::vector<bsp_node*> nodes;
            collect_internal(root.get(), nodes);
            if (r.size() != nodes.size()) return false;

            for (size_t i = 0; i < nodes.size(); i++) nodes[i]->ratio = std::clamp(r[i], 0.01f, 0.99f);
            return true;
        }

        // ### Per frame ###

        /**
        * Lays out and draws every panel inside the given rectangle (usually the main viewport's work area),
        * then handles the divider handles. Returns true while a divider is being dragged.
        */
        bool draw(ImVec2 position, ImVec2 size, value_store& store)
        {
            compute_bounds(root.get(), position, size);
            fit_ratios(root.get());   // keep minimum sizes valid when the window shrinks
            compute_bounds(root.get(), position, size);

            draw_leaves(root.get(), store);
            return handle_dividers();
        }

        // Sizes in design pixels (scaled by mgui::px)
        float min_panel_size = 60.0f;
        float gap = 4.0f;

    private:
        // ### Tree helpers ###

        static bsp_node* find_leaf(bsp_node* node, unsigned int id)
        {
            if (!node) return nullptr;
            if (node->leaf()) return node->id == id ? node : nullptr;
            if (bsp_node* r = find_leaf(node->a.get(), id)) return r;
            return find_leaf(node->b.get(), id);
        }

        static void collect_ratios(const bsp_node* node, std::vector<float>& out)
        {
            if (!node || node->leaf()) return;
            out.push_back(node->ratio);
            collect_ratios(node->a.get(), out);
            collect_ratios(node->b.get(), out);
        }

        static void collect_internal(bsp_node* node, std::vector<bsp_node*>& out)
        {
            if (!node || node->leaf()) return;
            out.push_back(node);
            collect_internal(node->a.get(), out);
            collect_internal(node->b.get(), out);
        }

        static void compute_bounds(bsp_node* node, ImVec2 pos, ImVec2 size)
        {
            if (!node) return;
            node->pos = pos;
            node->size = size;
            if (node->leaf()) return;

            ImVec2 pos_b = pos, size_a = size, size_b = size;
            if (node->axis == split_axis::x)
            {
                size_a.x = std::floor(size.x * node->ratio);
                size_b.x = size.x - size_a.x;
                pos_b.x = pos.x + size_a.x;
            }
            else
            {
                size_a.y = std::floor(size.y * node->ratio);
                size_b.y = size.y - size_a.y;
                pos_b.y = pos.y + size_a.y;
            }

            compute_bounds(node->a.get(), pos, size_a);
            compute_bounds(node->b.get(), pos_b, size_b);
        }

        // ### Divider limits (ported from bsp_graph_manager) ###
        // All coordinates are relative to the node being resized: 0 = its start, 1 = its end on the axis.

        static float min_limit(const bsp_node* node, split_axis axis, float l0, float l1, float min_span)
        {
            if (!node) return l0;
            if (node->leaf()) return l0 + min_span;

            if (node->axis == axis)
            {
                const float d = l0 + node->ratio * (l1 - l0);
                return min_limit(node->b.get(), axis, d, l1, min_span);
            }
            return std::max(min_limit(node->a.get(), axis, l0, l1, min_span), min_limit(node->b.get(), axis, l0, l1, min_span));
        }

        static float max_limit(const bsp_node* node, split_axis axis, float l0, float l1, float min_span)
        {
            if (!node) return l1;
            if (node->leaf()) return l1 - min_span;

            if (node->axis == axis)
            {
                const float d = l1 - (1.0f - node->ratio) * (l1 - l0);
                return max_limit(node->a.get(), axis, l0, d, min_span);
            }
            return std::min(max_limit(node->a.get(), axis, l0, l1, min_span), max_limit(node->b.get(), axis, l0, l1, min_span));
        }

        /**
        * After the range [l0_old, l1_old] of a subtree became [l0_new, l1_new], re-express the same-axis
        * dividers inside it so they stay where they were on screen.
        */
        static void countermove(bsp_node* node, split_axis axis, float l0_old, float l0_new, float l1_old, float l1_new)
        {
            if (!node || node->leaf()) return;

            if (node->axis == axis)
            {
                const float old_size = l1_old - l0_old;
                const float new_size = std::max(1.0e-4f, l1_new - l0_new);

                const float d = l0_old + node->ratio * old_size;
                node->ratio = std::clamp((d - l0_new) / new_size, 0.0f, 1.0f);
                const float d_new = l0_new + node->ratio * new_size;

                countermove(node->a.get(), axis, l0_old, l0_new, d, d_new);
                countermove(node->b.get(), axis, d, d_new, l1_old, l1_new);
                return;
            }

            countermove(node->a.get(), axis, l0_old, l0_new, l1_old, l1_new);
            countermove(node->b.get(), axis, l0_old, l0_new, l1_old, l1_new);
        }

        void move_divider(bsp_node* node, float wanted_ratio)
        {
            const float extent = node->extent(node->axis);
            if (extent <= 1.0f) return;

            const float min_span = std::min(0.45f, px(min_panel_size) / extent);
            const float old_ratio = node->ratio;

            float lo = min_limit(node->a.get(), node->axis, 0.0f, old_ratio, min_span);
            float hi = max_limit(node->b.get(), node->axis, old_ratio, 1.0f, min_span);
            if (lo > hi) lo = hi = (lo + hi) * 0.5f;

            const float new_ratio = std::clamp(wanted_ratio, lo, hi);
            if (std::fabs(new_ratio - old_ratio) < 1.0e-5f) return;

            node->ratio = new_ratio;
            countermove(node->a.get(), node->axis, 0.0f, 0.0f, old_ratio, new_ratio);
            countermove(node->b.get(), node->axis, old_ratio, new_ratio, 1.0f, 1.0f);
        }

        /**
        * Pushes dividers back inside their limits, e.g. after the window got smaller.
        */
        void fit_ratios(bsp_node* node)
        {
            if (!node || node->leaf()) return;
            if (active != node) move_divider(node, node->ratio);
            fit_ratios(node->a.get());
            fit_ratios(node->b.get());
        }

        // ### Drawing ###

        void draw_leaves(bsp_node* node, value_store& store)
        {
            if (!node) return;
            if (!node->leaf())
            {
                draw_leaves(node->a.get(), store);
                draw_leaves(node->b.get(), store);
                return;
            }

            panel* p = find_panel(node->id);
            if (!p) return;

            // Shrink by half the gap on every edge that borders another panel
            const float g = px(gap) * 0.5f;
            const ImVec2 work_min = ImGui::GetMainViewport()->WorkPos;
            const ImVec2 work_max(work_min.x + ImGui::GetMainViewport()->WorkSize.x, work_min.y + ImGui::GetMainViewport()->WorkSize.y);

            ImVec2 mn = node->pos;
            ImVec2 mx(node->pos.x + node->size.x, node->pos.y + node->size.y);
            if (mn.x > work_min.x + 0.5f) mn.x += g;
            if (mn.y > work_min.y + 0.5f) mn.y += g;
            if (mx.x < work_max.x - 0.5f) mx.x -= g;
            if (mx.y < work_max.y - 0.5f) mx.y -= g;

            ImGui::SetNextWindowPos(mn);
            ImGui::SetNextWindowSize(ImVec2(std::max(1.0f, mx.x - mn.x), std::max(1.0f, mx.y - mn.y)));

            if (!p->padded()) ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            const std::string window_id = p->title() + "###panel" + std::to_string(node->id);
            const bool open = ImGui::Begin(window_id.c_str(), nullptr, p->window_flags());
            if (!p->padded()) ImGui::PopStyleVar();

            if (open) p->draw_window(store);
            ImGui::End();
        }

        /**
        * Hit box of a divider: the gap between the two children, the full length of the node.
        */
        void divider_rect(const bsp_node* n, ImVec2& mn, ImVec2& mx) const
        {
            const float half = std::max(px(gap) * 0.5f, px(2.0f)) + 1.0f;
            if (n->axis == split_axis::x)
            {
                const float x = n->pos.x + std::floor(n->size.x * n->ratio);
                mn = ImVec2(x - half, n->pos.y);
                mx = ImVec2(x + half, n->pos.y + n->size.y);
            }
            else
            {
                const float y = n->pos.y + std::floor(n->size.y * n->ratio);
                mn = ImVec2(n->pos.x, y - half);
                mx = ImVec2(n->pos.x + n->size.x, y + half);
            }
        }

        bool handle_dividers()
        {
            std::vector<bsp_node*> nodes;
            collect_internal(root.get(), nodes);

            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) active = nullptr;

            // Do not start a drag while ImGui owns the mouse (popup open, a widget being dragged, ...)
            const bool can_start = !ImGui::IsAnyItemActive() && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);

            const ImVec2 mouse = ImGui::GetMousePos();
            bsp_node* hovered = nullptr;
            for (bsp_node* n : nodes)
            {
                ImVec2 mn, mx;
                divider_rect(n, mn, mx);
                if (mouse.x >= mn.x && mouse.x < mx.x && mouse.y >= mn.y && mouse.y < mx.y) { hovered = n; break; }
            }

            if (!active && hovered && can_start && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) active = hovered;

            if (active)
            {
                const float wanted = active->axis == split_axis::x
                    ? (mouse.x - active->pos.x) / std::max(1.0f, active->size.x)
                    : (mouse.y - active->pos.y) / std::max(1.0f, active->size.y);
                move_divider(active, wanted);
            }

            bsp_node* highlight = active ? active : (can_start ? hovered : nullptr);
            if (highlight)
            {
                ImGui::SetMouseCursor(highlight->axis == split_axis::x ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);

                ImVec2 mn, mx;
                divider_rect(highlight, mn, mx);
                const ImVec4 c = active ? colors().accent : colors().accent_muted;
                ImGui::GetForegroundDrawList()->AddRectFilled(mn, mx, ImGui::ColorConvertFloat4ToU32(c), px(2.0f));
            }

            return active != nullptr;
        }

        std::unique_ptr<bsp_node> root;
        std::unordered_map<unsigned int, std::unique_ptr<panel>> panels;
        std::string default_ratios;
        bsp_node* active = nullptr;
    };
}

#endif
