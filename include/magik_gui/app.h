#ifndef MAGIK_GUI_APP_H
#define MAGIK_GUI_APP_H

/*
    Magik GUI application state.

    Owns the value store, style, BSP layout, Magik link and the viewport texture. Platform code (GLFW,
    ImGui backends) stays in main.cpp; all controls are declared in ui_configuration.h.

    Per frame, in this order:
        app.prepare_frame(content_scale, fb_scale);   // before ImGui::NewFrame()
        ImGui::NewFrame();
        app.draw();
        ... render ...
        app.end_frame();                              // dispatches the command buffer

    Values owned by the app (ui_configuration.h attaches widgets / commands to them):
        camera.resolution        ivec2   resolution requested from Magik, driven by the viewport size
        render.resolution_scale  float   fraction of the viewport size that is rendered
        camera.lock_resolution   bool    use camera.fixed_resolution instead of the viewport size
        camera.fixed_resolution  ivec2
        render.aov               string  AOV shown in the viewport
        ui.zoom                  float   user UI scale on top of the monitor DPI
        ui.layout                string  divider ratios of the BSP layout
*/

#include <cstddef>   // magik.h uses size_t without including stddef.h
#include <glad/glad.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include "imgui.h"
#include "magik.h"
#include "mgui/mgui.h"
#include "console.h"
#include "magik_link.h"

namespace magik_gui
{
    struct viewport_image
    {
        GLuint texture = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t channels = 0;
    };

    class app
    {
    public:
        app(magik_render_manager_t manager, console& log_)
            : log(log_), link(manager, log_)
        {
            check_magik_errors(magik_configure_framebuffer_collection(&framebuffer, MAGIK_FRAMEBUFFER_COLLECTION_CONFIG_OPENGL_INTEROP));

            resolution       = &store.declare<mgui::ivec2>("camera.resolution", { 640, 360 });
            resolution_scale = &store.declare<float>("render.resolution_scale", 1.0f);
            lock_resolution  = &store.declare<bool>("camera.lock_resolution", false);
            fixed_resolution = &store.declare<mgui::ivec2>("camera.fixed_resolution", { 1920, 1080 });
            displayed_aov    = &store.declare<std::string>("render.aov", std::string("test"));
            zoom             = &store.declare<float>("ui.zoom", 1.0f);
            layout_ratios    = &store.declare<std::string>("ui.layout", std::string());

            resolution->set_persist(false);
            resolution->sanitize_with([](const mgui::ivec2& r) { return mgui::ivec2{ std::max(2, r[0]), std::max(2, r[1]) }; });
            fixed_resolution->sanitize_with([](const mgui::ivec2& r) { return mgui::ivec2{ std::clamp(r[0], 2, 16384), std::clamp(r[1], 2, 16384) }; });
            resolution_scale->sanitize_with([](const float& s) { return std::clamp(s, 0.05f, 1.0f); });
            zoom->sanitize_with([this](const float& z) { return std::clamp(z, style.min_zoom, style.max_zoom); });
            zoom->on_change([this](const mgui::change<float>& c) { style.set_zoom(c.new_value); });
        }

        ~app()
        {
            if (image.texture) glDeleteTextures(1, &image.texture);
            if (framebuffer) check_magik_errors(magik_destroy_framebuffer_collection(framebuffer));
        }

        app(const app&) = delete;
        app& operator=(const app&) = delete;

        /**
        * Call after ImGui::CreateContext() and configure_ui(). Loads the settings file and sends the
        * complete initial state to Magik (every action runs once with change_source::init).
        */
        void start()
        {
            style.init();
            mgui::active_style_manager() = &style;

            layout.mark_default();
            if (store.load(settings_path, /*dispatch=*/false) >= 0) log.log(log_level::info, "Loaded settings from %s", settings_path.c_str());
            layout.load_ratios(layout_ratios->get());

            store.sync_all();
        }

        void prepare_frame(float content_scale, float framebuffer_scale)
        {
            style.update(content_scale, framebuffer_scale);
            fetch_framebuffer();
        }

        void draw()
        {
            record_frame_time();
            if (on_frame) on_frame();

            const ImGuiViewport* vp = ImGui::GetMainViewport();
            layout.draw(vp->WorkPos, vp->WorkSize, store);
        }

        void end_frame() { link.end_frame(); }

        void shutdown() { save_settings(); }

        // ### Settings ###

        bool save_settings()
        {
            layout_ratios->set(layout.save_ratios());
            const bool ok = store.save(settings_path);
            if (ok) log.log(log_level::info, "Saved settings to %s", settings_path.c_str());
            else    log.log(log_level::error, "Could not write %s", settings_path.c_str());
            return ok;
        }

        bool load_settings()
        {
            const int n = store.load(settings_path, /*dispatch=*/true);
            if (n < 0) { log.log(log_level::warning, "No settings file %s", settings_path.c_str()); return false; }
            layout.load_ratios(layout_ratios->get());
            log.log(log_level::info, "Loaded %d settings from %s", n, settings_path.c_str());
            return true;
        }

        // ### Viewport ###

        /**
        * Body of the viewport panel: requests the render resolution from the available size and shows the
        * current frame, letter-boxed.
        */
        void draw_viewport()
        {
            const ImVec2 avail = ImGui::GetContentRegionAvail();
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(origin, ImVec2(origin.x + avail.x, origin.y + avail.y), IM_COL32(0, 0, 0, 255));

            request_resolution(avail);

            if (image.texture && image.width > 0 && image.height > 0)
            {
                const float w = static_cast<float>(image.width), h = static_cast<float>(image.height);
                const float s = std::min(avail.x / w, avail.y / h);
                const ImVec2 size(w * s, h * s);
                ImGui::SetCursorScreenPos(ImVec2(origin.x + (avail.x - size.x) * 0.5f, origin.y + (avail.y - size.y) * 0.5f));
                ImGui::Image((ImTextureID)(intptr_t)image.texture, size);
            }
            else
            {
                const char* msg = "Waiting for Magik ...";
                const ImVec2 ts = ImGui::CalcTextSize(msg);
                dl->AddText(ImVec2(origin.x + (avail.x - ts.x) * 0.5f, origin.y + (avail.y - ts.y) * 0.5f), ImGui::GetColorU32(ImGuiCol_TextDisabled), msg);
            }

            // Keep the window's content size equal to the region so it never scrolls
            ImGui::SetCursorScreenPos(origin);
            ImGui::Dummy(avail);
        }

        // ### Statistics ###

        const std::vector<float>& frame_times_ms() const { return frame_ms; }
        float magik_frames_per_second() const { return magik_fps; }
        const viewport_image& current_image() const { return image; }

        // ### Members ###

        console& log;
        mgui::value_store store;
        mgui::style_manager style;
        mgui::bsp_layout layout;
        magik_link link;

        std::string settings_path = "magik_gui_settings.ini";
        bool quit_requested = false;

        /**
        * Called every frame after ImGui::NewFrame(), before the panels. ui_configuration.h uses it for the
        * menu bar and keyboard shortcuts.
        */
        std::function<void()> on_frame;

        mgui::value<mgui::ivec2>* resolution = nullptr;
        mgui::value<float>* resolution_scale = nullptr;
        mgui::value<bool>* lock_resolution = nullptr;
        mgui::value<mgui::ivec2>* fixed_resolution = nullptr;
        mgui::value<std::string>* displayed_aov = nullptr;
        mgui::value<float>* zoom = nullptr;
        mgui::value<std::string>* layout_ratios = nullptr;

    private:
        void request_resolution(ImVec2 avail)
        {
            mgui::ivec2 wanted;
            if (lock_resolution->get())
            {
                wanted = fixed_resolution->get();
            }
            else
            {
                // Render in framebuffer pixels, not window coordinates (matters on macOS / Wayland)
                const ImVec2 fb_scale = ImGui::GetIO().DisplayFramebufferScale;
                const float s = resolution_scale->get();
                wanted = { static_cast<int>(std::lround(avail.x * fb_scale.x * s)), static_cast<int>(std::lround(avail.y * fb_scale.y * s)) };
            }
            wanted = { std::max(2, wanted[0]), std::max(2, wanted[1]) };

            if (wanted == resolution->get()) { resize_pending_since = -1.0; return; }

            // Every resolution change restarts the render, so only send sizes that have settled: the same
            // size two frames in a row with the mouse released (dividers / window not being dragged), or at
            // most every 0.25 s while dragging.
            const double now = ImGui::GetTime();
            const bool stable = (wanted == last_wanted);
            last_wanted = wanted;
            if (resize_pending_since < 0.0) resize_pending_since = now;

            if ((stable && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) || now - resize_pending_since > 0.25)
            {
                resolution->set(wanted, mgui::change_source::program);
                resize_pending_since = -1.0;
            }
        }

        void fetch_framebuffer()
        {
            if (!framebuffer) return;

            bool is_new = false;
            check_magik_errors(magik_framebuffer_collection_fetch(link.handle(), &is_new, framebuffer));
            if (!is_new) return;

            magik_framebuffer_container_config_opengl_interop_t container;
            check_magik_errors(magik_extract_config_opengl_interop(link.handle(), &container, framebuffer, displayed_aov->get().c_str()));
            if (container.gl_buffer_id == 0 || container.x_resolution == 0 || container.y_resolution == 0) return;

            upload(container);
            frames_received++;
        }

        void upload(const magik_framebuffer_container_config_opengl_interop_t& c)
        {
            const uint32_t channels = std::clamp<uint32_t>(c.channels ? c.channels : 3u, 1u, 4u);
            static const GLenum formats[4]          = { GL_RED,  GL_RG,    GL_RGB,    GL_RGBA };
            static const GLint internal_formats[4]  = { GL_R32F, GL_RG32F, GL_RGB32F, GL_RGBA32F };

            if (!image.texture || image.width != c.x_resolution || image.height != c.y_resolution || image.channels != channels)
            {
                if (image.texture) glDeleteTextures(1, &image.texture);
                image.width = c.x_resolution;
                image.height = c.y_resolution;
                image.channels = channels;

                glGenTextures(1, &image.texture);
                glBindTexture(GL_TEXTURE_2D, image.texture);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                if (channels == 1)
                {
                    // Show single channel AOVs as grey instead of red
                    const GLint swizzle[4] = { GL_RED, GL_RED, GL_RED, GL_ONE };
                    glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzle);
                }
                glTexImage2D(GL_TEXTURE_2D, 0, internal_formats[channels - 1], image.width, image.height, 0, formats[channels - 1], GL_FLOAT, nullptr);
            }

            glBindTexture(GL_TEXTURE_2D, image.texture);
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, c.gl_buffer_id);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, image.width, image.height, formats[channels - 1], GL_FLOAT, nullptr);
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
            glBindTexture(GL_TEXTURE_2D, 0);
        }

        void record_frame_time()
        {
            frame_ms.push_back(ImGui::GetIO().DeltaTime * 1000.0f);
            if (frame_ms.size() > 240) frame_ms.erase(frame_ms.begin());

            const double now = ImGui::GetTime();
            if (now - fps_window_start >= 1.0)
            {
                magik_fps = static_cast<float>(static_cast<double>(frames_received - frames_at_window_start) / (now - fps_window_start));
                frames_at_window_start = frames_received;
                fps_window_start = now;
            }
        }

        magik_framebuffer_collection_external_t framebuffer = nullptr;
        viewport_image image;
        double resize_pending_since = -1.0;
        mgui::ivec2 last_wanted{ 0, 0 };

        std::vector<float> frame_ms;
        uint64_t frames_received = 0;
        uint64_t frames_at_window_start = 0;
        double fps_window_start = 0.0;
        float magik_fps = 0.0f;
    };
}

#endif
