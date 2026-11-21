#ifndef MGUI_STYLE_H
#define MGUI_STYLE_H

/*
    mgui style & automatic scaling
    ==============================

    `style_manager` owns the theme and the scale. Call `update()` once per frame *before*
    ImGui::NewFrame() with the window's content scale and framebuffer scale; whenever either one or the
    user zoom (set_zoom) changes, the whole ImGui style (paddings, roundings, fonts) is rebuilt from the
    unscaled base style. Nothing else in the GUI needs to care: widget code uses mgui::px() for its own
    sizes, so moving the window to a monitor with a different DPI rescales everything automatically.

    Fonts: Dear ImGui >= 1.92 rasterizes glyphs on demand, so changing the scale keeps text crisp without
    rebuilding the atlas.
*/

#include <algorithm>
#include <cmath>

#include "imgui.h"
#include "mgui_core.h"

namespace mgui
{
    inline ImVec4 rgb(float r, float g, float b, float a = 1.0f) { return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a); }

    struct theme
    {
        // Greys taken from the original Magik GUI so the look stays familiar
        ImVec4 window_bg      = rgb(24, 24, 24);
        ImVec4 title_bg       = rgb(36, 36, 36);
        ImVec4 title_bg_active= rgb(48, 48, 48);
        ImVec4 child_bg       = rgb(0, 0, 0, 0.0f);
        ImVec4 popup_bg       = rgb(30, 30, 32, 0.98f);
        ImVec4 frame_bg       = rgb(40, 40, 44);
        ImVec4 frame_hovered  = rgb(52, 52, 58);
        ImVec4 frame_active   = rgb(60, 60, 70);
        ImVec4 border         = rgb(58, 58, 64, 0.6f);
        ImVec4 text           = rgb(222, 222, 226);
        ImVec4 text_dim       = rgb(128, 128, 136);

        // Accent, the splitter colour of the original GUI, brightened for interactive states
        ImVec4 accent         = rgb(110, 110, 190);
        ImVec4 accent_hovered = rgb(130, 130, 210);
        ImVec4 accent_active  = rgb(150, 150, 230);
        ImVec4 accent_muted   = rgb(72, 72, 96);

        ImVec4 warning        = rgb(230, 180, 80);
        ImVec4 error          = rgb(235, 95, 95);
        ImVec4 success        = rgb(110, 200, 130);

        float base_font_size  = 18.0f;
    };

    inline void apply_theme(const theme& t, ImGuiStyle& s)
    {
        ImGui::StyleColorsDark(&s);

        s.WindowPadding     = ImVec2(10.0f, 8.0f);
        s.FramePadding      = ImVec2(8.0f, 4.0f);
        s.ItemSpacing       = ImVec2(8.0f, 6.0f);
        s.ItemInnerSpacing  = ImVec2(6.0f, 4.0f);
        s.IndentSpacing     = 14.0f;
        s.ScrollbarSize     = 12.0f;
        s.GrabMinSize       = 10.0f;

        s.WindowRounding    = 0.0f;   // panels tile the screen, keep their corners square
        s.ChildRounding     = 4.0f;
        s.FrameRounding     = 4.0f;
        s.PopupRounding     = 6.0f;
        s.ScrollbarRounding = 6.0f;
        s.GrabRounding      = 3.0f;
        s.TabRounding       = 4.0f;

        s.WindowBorderSize  = 1.0f;
        s.FrameBorderSize   = 0.0f;
        s.PopupBorderSize   = 1.0f;
        s.WindowTitleAlign  = ImVec2(0.0f, 0.5f);

        ImVec4* c = s.Colors;
        c[ImGuiCol_Text]                 = t.text;
        c[ImGuiCol_TextDisabled]         = t.text_dim;
        c[ImGuiCol_WindowBg]             = t.window_bg;
        c[ImGuiCol_ChildBg]              = t.child_bg;
        c[ImGuiCol_PopupBg]              = t.popup_bg;
        c[ImGuiCol_Border]               = t.border;
        c[ImGuiCol_FrameBg]              = t.frame_bg;
        c[ImGuiCol_FrameBgHovered]       = t.frame_hovered;
        c[ImGuiCol_FrameBgActive]        = t.frame_active;
        c[ImGuiCol_TitleBg]              = t.title_bg;
        c[ImGuiCol_TitleBgActive]        = t.title_bg_active;
        c[ImGuiCol_TitleBgCollapsed]     = t.title_bg;
        c[ImGuiCol_MenuBarBg]            = t.title_bg;
        c[ImGuiCol_ScrollbarBg]          = rgb(0, 0, 0, 0.0f);
        c[ImGuiCol_ScrollbarGrab]        = t.frame_hovered;
        c[ImGuiCol_ScrollbarGrabHovered] = t.frame_active;
        c[ImGuiCol_ScrollbarGrabActive]  = t.accent_muted;
        c[ImGuiCol_CheckMark]            = t.accent_active;
        c[ImGuiCol_SliderGrab]           = t.accent;
        c[ImGuiCol_SliderGrabActive]     = t.accent_active;
        c[ImGuiCol_Button]               = t.frame_bg;
        c[ImGuiCol_ButtonHovered]        = t.frame_hovered;
        c[ImGuiCol_ButtonActive]         = t.accent_muted;
        c[ImGuiCol_Header]               = t.accent_muted;
        c[ImGuiCol_HeaderHovered]        = ImVec4(t.accent.x, t.accent.y, t.accent.z, 0.55f);
        c[ImGuiCol_HeaderActive]         = t.accent;
        c[ImGuiCol_Separator]            = t.border;
        c[ImGuiCol_SeparatorHovered]     = t.accent;
        c[ImGuiCol_SeparatorActive]      = t.accent_active;
        c[ImGuiCol_PlotLines]            = t.accent_hovered;
        c[ImGuiCol_PlotHistogram]        = t.accent;
        c[ImGuiCol_TextSelectedBg]       = ImVec4(t.accent.x, t.accent.y, t.accent.z, 0.35f);
        c[ImGuiCol_NavCursor]            = t.accent_active;
    }

    class style_manager
    {
    public:
        explicit style_manager(theme t = {}) : palette(t) {}

        /**
        * Call once after ImGui::CreateContext().
        */
        void init()
        {
            apply_theme(palette, base_style);
            base_style.FontSizeBase = palette.base_font_size;
            dirty = true;
            apply();
        }

        /**
        * content_scale:     glfwGetWindowContentScale (monitor DPI / 96 on Windows & Linux)
        * framebuffer_scale: framebuffer pixels per window coordinate (2 on a macOS retina screen, else 1)
        *
        * On platforms where window coordinates are already scaled (macOS) the style must not be scaled
        * again, hence dpi = content_scale / framebuffer_scale.
        */
        void update(float content_scale, float framebuffer_scale)
        {
            if (content_scale <= 0.0f) content_scale = 1.0f;
            if (framebuffer_scale <= 0.0f) framebuffer_scale = 1.0f;

            float dpi = std::max(0.5f, content_scale / framebuffer_scale);
            if (std::fabs(dpi - scale().dpi) > 0.001f)
            {
                scale().dpi = dpi;
                dirty = true;
            }

            apply();
        }

        void set_zoom(float z)
        {
            z = std::clamp(z, min_zoom, max_zoom);
            if (std::fabs(z - scale().zoom) < 0.001f) return;
            scale().zoom = z;
            dirty = true;
        }

        float zoom() const { return scale().zoom; }

        const theme& colors() const { return palette; }

        float min_zoom = 0.5f;
        float max_zoom = 3.0f;

    private:
        void apply()
        {
            if (!dirty || !ImGui::GetCurrentContext()) return;
            dirty = false;

            ImGuiStyle& style = ImGui::GetStyle();
            style = base_style;
            style.ScaleAllSizes(scale().factor());
            style.FontSizeBase = palette.base_font_size;
            style.FontScaleMain = scale().zoom;
            style.FontScaleDpi = scale().dpi;
        }

        theme palette;
        ImGuiStyle base_style;
        bool dirty = true;
    };

    inline style_manager*& active_style_manager() { static style_manager* m = nullptr; return m; }

    inline const theme& colors()
    {
        static theme fallback;
        return active_style_manager() ? active_style_manager()->colors() : fallback;
    }
}

#endif
