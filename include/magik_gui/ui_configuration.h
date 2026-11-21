#ifndef MAGIK_GUI_UI_CONFIGURATION_H
#define MAGIK_GUI_UI_CONFIGURATION_H

/*
    Magik GUI - UI configuration
    ============================

    This is the file to edit when adding controls. The pattern is always the same:

        1. declare a value:      auto& spin = store.declare<float>("scene.kerr_spin", 0.999f);
        2. attach a command:     spin.on_change([&](const mgui::change<float>& c) { ... link.push(cmd); });
        3. show it somewhere:    spacetime.slider("Spin", spin, -1, 1);

    Actions receive c.old_value, c.new_value, c.source (init / user / program / load) and c.mods (keys held).
    At start-up every action runs once with c.source == init, which sends the full initial state to Magik.
    Several widgets may show the same value (e.g. the Settings panel and the viewport context menu); they
    stay in sync and the action runs once per change.

    Keys are also the names in magik_gui_settings.ini. Values persist unless .set_persist(false).
*/

#include <cmath>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "app.h"

namespace magik_gui
{
    // Panel ids, also the leaf ids of the BSP layout
    enum panel_id : unsigned int
    {
        panel_viewport   = 0,
        panel_scene      = 1,
        panel_console    = 2,
        panel_statistics = 3,
        panel_settings   = 4,
        panel_debug      = 5
    };

    namespace detail
    {
        constexpr double speed_of_light = 299792458.0;     // m/s
        constexpr double gravitational_constant = 6.67430e-11;

        inline double schwarzschild_radius(double mass_kg) { return 2.0 * gravitational_constant * mass_kg / (speed_of_light * speed_of_light); }

        inline std::string format(const char* fmt, double v)
        {
            char buf[64];
            std::snprintf(buf, sizeof(buf), fmt, v);
            return buf;
        }

        // AOV list stored as "name:channels,name:channels"
        using aov_map = std::map<std::string, int>;

        inline aov_map parse_aovs(const std::string& text)
        {
            aov_map out;
            std::stringstream stream(text);
            std::string entry;
            while (std::getline(stream, entry, ','))
            {
                const size_t colon = entry.rfind(':');
                if (colon == std::string::npos || colon == 0) continue;
                out[entry.substr(0, colon)] = std::clamp(std::atoi(entry.c_str() + colon + 1), 1, 4);
            }
            return out;
        }

        inline std::string format_aovs(const aov_map& aovs)
        {
            std::string out;
            for (const auto& [name, channels] : aovs) out += (out.empty() ? "" : ",") + name + ":" + std::to_string(channels);
            return out;
        }

        inline std::vector<std::string> aov_names(const std::string& text)
        {
            std::vector<std::string> out;
            for (const auto& [name, channels] : parse_aovs(text)) out.push_back(name + "  (" + std::to_string(channels) + " ch)");
            return out;
        }
    }

    inline void configure_ui(app& a)
    {
        using namespace mgui;
        value_store& store = a.store;
        magik_link& link = a.link;
        console& log = a.log;



        // #################
        // ### Layout    ###
        // #################
        // Same split order as the original GUI: viewport left, console below it, panels stacked on the right.

        bsp_layout& layout = a.layout;
        layout.split(panel_viewport, split_axis::x, 0.78f, panel_scene);
        layout.split(panel_viewport, split_axis::y, 0.75f, panel_console);
        layout.split(panel_scene, split_axis::y, 0.75f, panel_statistics);
        layout.split(panel_scene, split_axis::y, 0.08f, panel_settings);
        layout.split(panel_settings, split_axis::y, 0.72f, panel_debug);

        panel& viewport   = layout.add_panel(panel_viewport, "Render viewport", panel::default_flags | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        panel& scene      = layout.add_panel(panel_scene, "Scene graph");
        panel& console_p  = layout.add_panel(panel_console, "Console", panel::default_flags | ImGuiWindowFlags_NoScrollbar);
        panel& statistics = layout.add_panel(panel_statistics, "Statistics");
        panel& settings   = layout.add_panel(panel_settings, "Settings");
        panel& debug      = layout.add_panel(panel_debug, "Debug");
        viewport.padding(false);



        // #################
        // ### Values    ###
        // #################

        // Render state
        auto& rendering     = store.declare<bool>("render.enabled", true);
        // Camera
        auto& active_camera = store.declare<int>("camera.active", 0);
        auto& cam_position  = store.declare<vec3>("camera.position", { 2.0f, 5.0f, 9.0f });
        auto& cam_rotation  = store.declare<vec3>("camera.orientation", { 0.0f, 0.0f, 0.0f });   // radians
        auto& cam_velocity  = store.declare<vec3>("camera.velocity", { 0.0f, 0.0f, 0.0f });      // m/s
        // Spacetime
        auto& metric        = store.declare<int>("scene.metric", 2);                             // 0 Minkowski, 1 Schwarzschild, 2 Kerr
        auto& schw_mass     = store.declare<float>("scene.schwarzschild_mass", 6.7329546075E26f);
        auto& kerr_mass     = store.declare<float>("scene.kerr_mass", 6.7329546075E26f);
        auto& kerr_spin     = store.declare<float>("scene.kerr_spin", 0.999f);
        // AOVs
        auto& aovs          = store.declare<std::string>("aov.list", std::string("test:3"));
        auto& aov_selected  = store.declare<int>("aov.selected", 0).set_persist(false);
        auto& aov_new_name  = store.declare<std::string>("aov.new_name", std::string("")).set_persist(false);
        auto& aov_new_ch    = store.declare<int>("aov.new_channels", 3).set_persist(false);
        // Debug
        auto& printf_text   = store.declare<std::string>("debug.printf_text", std::string("Hello World"));
        auto& log_commands  = store.declare<bool>("debug.log_commands", false);
        auto& auto_scroll   = store.declare<bool>("console.auto_scroll", true);

        kerr_mass.sanitize_with([](const float& m) { return std::max(m, 1.0f); });     // Magik ignores M <= 0
        schw_mass.sanitize_with([](const float& m) { return std::max(m, 0.0f); });
        active_camera.sanitize_with([](const int& id) { return std::max(id, 0); });
        aov_new_ch.sanitize_with([](const int& n) { return std::clamp(n, 1, 4); });



        // ##################################
        // ### Actions: values -> Magik   ###
        // ##################################

        rendering.on_change([&](const change<bool>& c)
        {
            if (c.new_value) link.push(magik_command_set_state_render_t{});
            else             link.push(magik_command_set_state_pause_t{});
        });

        active_camera.on_change([&](const change<int>& c)
        {
            magik_command_set_active_camera_t cmd;
            cmd.id = static_cast<uint32_t>(c.new_value);
            link.push(cmd);
        });

        cam_position.on_change([&](const change<vec3>& c)
        {
            magik_command_set_camera_position_t cmd;
            cmd.id = static_cast<uint32_t>(active_camera.get());
            cmd.position_x = c.new_value[0];
            cmd.position_y = c.new_value[1];
            cmd.position_z = c.new_value[2];
            link.push(cmd);
        });

        cam_rotation.on_change([&](const change<vec3>& c)
        {
            magik_command_set_camera_orientation_t cmd;
            cmd.id = static_cast<uint32_t>(active_camera.get());
            cmd.rotation_x = c.new_value[0];
            cmd.rotation_y = c.new_value[1];
            cmd.rotation_z = c.new_value[2];
            link.push(cmd);
        });

        cam_velocity.on_change([&](const change<vec3>& c)
        {
            magik_command_set_camera_three_velocity_t cmd;
            cmd.velocity_x = c.new_value[0];
            cmd.velocity_y = c.new_value[1];
            cmd.velocity_z = c.new_value[2];
            link.push(cmd);
        });

        a.resolution->on_change([&](const change<ivec2>& c)
        {
            magik_command_set_camera_resolution_t cmd;
            cmd.x_resolution = static_cast<uint32_t>(c.new_value[0]);
            cmd.y_resolution = static_cast<uint32_t>(c.new_value[1]);
            link.push(cmd);
        });

        metric.on_change([&](const change<int>& c)
        {
            switch (c.new_value)
            {
                case 0:  link.push(magik_command_set_metric_minkowski_t{}); break;
                case 1:  link.push(magik_command_set_metric_schwarzschild_t{}); break;
                default: link.push(magik_command_set_metric_kerr_t{}); break;
            }
            if (c.source == change_source::user) log.log(log_level::info, "Metric: %s", c.new_value == 0 ? "Minkowski" : c.new_value == 1 ? "Schwarzschild" : "Kerr");
        });

        schw_mass.on_change([&](const change<float>& c)
        {
            magik_command_set_schwarzschild_mass_t cmd;
            cmd.mass_parameter = c.new_value;
            link.push(cmd);
        });

        kerr_mass.on_change([&](const change<float>& c)
        {
            magik_command_set_kerr_mass_t cmd;
            cmd.mass_parameter = c.new_value;
            link.push(cmd);
        });

        kerr_spin.on_change([&](const change<float>& c)
        {
            magik_command_set_kerr_spin_t cmd;
            cmd.spin_parameter = c.new_value;
            link.push(cmd);
        });

        // The AOV list action diffs old and new: only the AOVs that changed are added / removed.
        aovs.on_change([&](const change<std::string>& c)
        {
            const detail::aov_map before = c.is_init() ? detail::aov_map{} : detail::parse_aovs(c.old_value);
            const detail::aov_map after = detail::parse_aovs(c.new_value);

            for (const auto& [name, channels] : before)
            {
                auto it = after.find(name);
                if (it == after.end() || it->second != channels) link.remove_aov(name);
            }
            for (const auto& [name, channels] : after)
            {
                auto it = before.find(name);
                if (it == before.end() || it->second != channels) link.add_aov(name, static_cast<uint32_t>(channels));
            }

            // Keep the viewport on an AOV that exists
            if (!after.empty() && !after.count(a.displayed_aov->get())) a.displayed_aov->set(after.begin()->first);
        });

        log_commands.on_change([&](const change<bool>& c) { link.log_commands = c.new_value; });



        // ####################
        // ### Settings     ###
        // ####################

        // --- Render ---
        auto& render = settings.section("Render");
        {
            auto& buttons = render.row({ 1.0f, 1.0f });
            buttons.button("Render").bind(rendering, button::behavior::toggle).tooltip("Toggle rendering / pause (F5)");
            buttons.button("Clear buffer").on_click([&](const button_event&) { link.push(magik_command_clear_render_buffer_t{}); });

            render.slider("Resolution scale", *a.resolution_scale, 0.05f, 1.0f)
                .format("%.2f x")
                .commit(commit_policy::on_release())
                .tooltip("Render at a fraction of the viewport size. Applied on release.");

            render.checkbox("Lock resolution", *a.lock_resolution);
            render.drag("Fixed resolution", *a.fixed_resolution, 1.0f)
                .range(2, 16384)
                .format("%d px")
                .commit(commit_policy::on_release())
                .visible_if([&] { return a.lock_resolution->get(); });

            render.readout("Resolution", [&]
            {
                const ivec2 r = a.resolution->get();
                return std::to_string(r[0]) + " x " + std::to_string(r[1]);
            });

            render.dropdown("Display AOV", *a.displayed_aov, [&]
            {
                std::vector<std::string> names;
                for (const auto& [name, channels] : detail::parse_aovs(aovs.get())) names.push_back(name);
                return names;
            });
        }

        // --- Camera ---
        auto& camera = settings.section("Camera");
        {
            camera.spinner("Active camera", active_camera, 1).range(0, 63);

            camera.drag("Position", cam_position, 0.02f)
                .format("%.2f")
                .commit(commit_policy::throttle(33))
                .tooltip("Metres. Drag, or Ctrl + click to type. Shift: faster, Alt: slower.");

            camera.drag("Rotation", cam_rotation, 0.005f)
                .angle()
                .commit(commit_policy::throttle(33))
                .tooltip("Rotation around x, y, z, in degrees (sent to Magik in radians)");

            camera.drag("Velocity", cam_velocity, 1.0e5f)
                .format("%.3g")
                .commit(commit_policy::throttle(33))
                .tooltip("Local three velocity in m/s");

            camera.readout("|v| / c", [&]
            {
                const vec3 v = cam_velocity.get();
                const double speed = std::sqrt(double(v[0]) * v[0] + double(v[1]) * v[1] + double(v[2]) * v[2]);
                return detail::format("%.4f", speed / detail::speed_of_light);
            });

            auto& buttons = camera.row({ 1.0f, 1.0f });
            buttons.button("Reset camera").on_click([&](const button_event&)
            {
                cam_position.reset(change_source::user);
                cam_rotation.reset(change_source::user);
                cam_velocity.reset(change_source::user);
            });
            buttons.button("Stop").on_click([&](const button_event&) { cam_velocity.set({ 0.0f, 0.0f, 0.0f }, change_source::user); }).tooltip("Set the velocity to zero");
        }

        // --- Spacetime ---
        auto& spacetime = settings.section("Spacetime");
        {
            spacetime.segmented("Metric", metric, { "Minkowski", "Schwarzschild", "Kerr" }).labels(label_mode::top);

            spacetime.drag("Mass", schw_mass, 1.0e24f)
                .range(0.0f, 1.0e32f)
                .logarithmic()
                .format("%.4g kg")
                .commit(commit_policy::on_release())
                .visible_if([&] { return metric.get() == 1; });
            spacetime.readout("r_s", [&] { return detail::format("%.4g m", detail::schwarzschild_radius(schw_mass.get())); })
                .tooltip("Schwarzschild radius 2GM / c^2")
                .visible_if([&] { return metric.get() == 1; });

            spacetime.drag("Mass", kerr_mass, 1.0e24f)
                .range(1.0f, 1.0e32f)
                .logarithmic()
                .format("%.4g kg")
                .commit(commit_policy::on_release())
                .visible_if([&] { return metric.get() == 2; });
            spacetime.slider("Spin", kerr_spin, -1.0f, 1.0f)
                .format("%.4f")
                .commit(commit_policy::throttle(50))
                .tooltip("Dimensionless spin a. Ctrl + click to type values outside [-1, 1].")
                .visible_if([&] { return metric.get() == 2; });
            spacetime.text("|a| > 1: no event horizon (naked singularity)")
                .colored(text::tone::warning)
                .wrapped()
                .visible_if([&] { return metric.get() == 2 && std::fabs(kerr_spin.get()) > 1.0f; });
            spacetime.readout("r_s", [&] { return detail::format("%.4g m", detail::schwarzschild_radius(kerr_mass.get())); })
                .tooltip("2GM / c^2 of the Kerr mass")
                .visible_if([&] { return metric.get() == 2; });
        }

        // --- AOVs ---
        auto& aov = settings.section("AOVs", false);
        {
            aov.list("AOVs", aov_selected, [&] { return detail::aov_names(aovs.get()); }, 4).labels(label_mode::none);

            auto& add_row = aov.row({ 3.0f, 2.0f, 1.2f }).labels(label_mode::none);
            add_row.text_input("Name", aov_new_name).hint("new AOV name");
            add_row.spinner("Channels", aov_new_ch, 1).range(1, 4);
            add_row.button("Add").on_click([&](const button_event&)
            {
                const std::string name = aov_new_name.get();
                if (name.empty() || name.find_first_of(":,") != std::string::npos)
                {
                    log.log(log_level::warning, "AOV names must not be empty or contain ':' or ','");
                    return;
                }
                detail::aov_map m = detail::parse_aovs(aovs.get());
                m[name] = aov_new_ch.get();
                aovs.set(detail::format_aovs(m), change_source::user);
                aov_new_name.set(std::string(), change_source::program);
            });

            aov.button("Remove selected").on_click([&](const button_event&)
            {
                detail::aov_map m = detail::parse_aovs(aovs.get());
                auto it = m.begin();
                std::advance(it, std::clamp(aov_selected.get(), 0, std::max(0, static_cast<int>(m.size()) - 1)));
                if (it == m.end()) return;
                log.log(log_level::info, "Removed AOV '%s'", it->first.c_str());
                m.erase(it);
                aovs.set(detail::format_aovs(m), change_source::user);
            });
            aov.text("Magik currently only renders while an AOV named \"test\" exists.").colored(text::tone::dim).wrapped();
        }

        // --- Interface ---
        auto& ui = settings.section("Interface", false);
        {
            ui.slider("UI scale", *a.zoom, 0.5f, 3.0f).format("%.2f x").commit(commit_policy::on_release()).tooltip("Ctrl + / Ctrl - / Ctrl 0");
            ui.readout("Monitor DPI scale", [] { return detail::format("%.2f", mgui::scale().dpi); });
            ui.button("Reset layout").on_click([&](const button_event&) { a.layout.reset(); });
        }



        // ####################
        // ### Viewport     ###
        // ####################

        viewport.custom([&](frame&) { a.draw_viewport(); });

        // Right-click menu: the same values as the Settings panel, so both stay in sync
        container& menu = viewport.context_menu(320.0f);
        {
            auto& buttons = menu.row({ 1.0f, 1.0f });
            buttons.button("Render").bind(rendering, button::behavior::toggle);
            buttons.button("Clear").on_click([&](const button_event&) { link.push(magik_command_clear_render_buffer_t{}); });
            menu.separator("Camera");
            menu.drag("Position", cam_position, 0.02f).format("%.2f").commit(commit_policy::throttle(33));
            menu.drag("Rotation", cam_rotation, 0.005f).angle().commit(commit_policy::throttle(33));
            menu.drag("Velocity", cam_velocity, 1.0e5f).format("%.3g").commit(commit_policy::throttle(33));
            menu.separator("Spacetime");
            menu.segmented("Metric", metric, { "Mink.", "Schw.", "Kerr" });
            menu.slider("Kerr spin", kerr_spin, -1.0f, 1.0f).format("%.4f").commit(commit_policy::throttle(50)).visible_if([&] { return metric.get() == 2; });
        }



        // ####################
        // ### Other panels ###
        // ####################

        scene.text("No scene graph yet.")
            .colored(text::tone::dim)
            .tooltip("Magik has no object or material commands yet (the 6000 / 7000 ranges are empty).");

        {
            auto& bar = console_p.row({ 1.0f, 1.0f, 8.0f }).labels(label_mode::none);
            bar.button("Clear").on_click([&](const button_event&) { log.clear(); });
            bar.checkbox("Auto-scroll", auto_scroll).text_right();
            bar.spacing(0.0f);
            console_p.custom([&](frame&) { log.draw(auto_scroll.get()); });
        }

        statistics.readout("GUI", [] { return detail::format("%.0f fps", ImGui::GetIO().Framerate); });
        statistics.plot("Frame time", [&]() -> const std::vector<float>& { return a.frame_times_ms(); }, 36.0f)
            .range(0.0f, 50.0f)
            .overlay([] { return detail::format("%.1f ms", 1000.0 / std::max(1.0f, ImGui::GetIO().Framerate)); });
        statistics.readout("Magik frames", [&] { return detail::format("%.1f / s", a.magik_frames_per_second()); });
        statistics.readout("Image", [&]
        {
            const viewport_image& img = a.current_image();
            return std::to_string(img.width) + " x " + std::to_string(img.height) + ", " + std::to_string(img.channels) + " ch";
        });
        statistics.readout("Commands", [&] { return std::to_string(link.pushed_total) + " pushed, " + std::to_string(link.pending_commands) + " waiting"; });
        statistics.readout("Dispatches", [&] { return std::to_string(link.dispatches); });
        statistics.readout("Worker", [&] { return link.last_async_error == MAGIK_SUCCESS ? std::string("ok") : describe(link.last_async_error); });
        statistics.text("Renderer statistics: magik_fetch_renderer_statistics() is declared in magik.h but not implemented in the DLL yet.")
            .colored(text::tone::dim)
            .wrapped();

        // --- Debug ---
        {
            auto& print = debug.row({ 3.0f, 1.0f }).labels(label_mode::none);
            print.text_input("Text", printf_text);
            print.button("printf").on_click([&](const button_event&) { link.print(printf_text.get()); }).tooltip("Magik prints the text on its worker thread (stdout)");

            debug.checkbox("Log commands", log_commands).tooltip("Echo every pushed command to the console");

            // Button behaviours, as examples
            debug.separator("Button behaviours");
            debug.button("Hold: move camera +x")
                .on_hold([&](const button_event& e)
                {
                    vec3 p = cam_position.get();
                    p[0] += e.mods.shift ? 1.0f : 0.1f;
                    cam_position.set(p, change_source::user);
                }, repeat_config{ 0.3f, 0.12f, 0.85f, 0.03f })
                .tooltip("Repeats while held and speeds up. Shift: bigger steps.");

            debug.button("Hold to reset all settings")
                .on_click([&](const button_event&) { log.log(log_level::info, "Keep the button pressed for one second to reset all settings"); })
                .on_long_press([&](const button_event&)
                {
                    store.reset_all(change_source::user);
                    a.layout.reset();
                    log.log(log_level::warning, "All settings reset to defaults");
                }, 1.0f);
        }



        // ###########################
        // ### Menu bar, shortcuts ###
        // ###########################

        a.on_frame = [&]
        {
            const ImGuiIO& io = ImGui::GetIO();
            if (!io.WantTextInput)
            {
                if (io.KeyCtrl && (ImGui::IsKeyPressed(ImGuiKey_Equal, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd, false)))      a.zoom->set(a.zoom->get() + 0.1f, change_source::user);
                if (io.KeyCtrl && (ImGui::IsKeyPressed(ImGuiKey_Minus, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract, false))) a.zoom->set(a.zoom->get() - 0.1f, change_source::user);
                if (io.KeyCtrl && (ImGui::IsKeyPressed(ImGuiKey_0, false) || ImGui::IsKeyPressed(ImGuiKey_Keypad0, false)))            a.zoom->set(1.0f, change_source::user);
                if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) a.save_settings();
                if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) rendering.set(!rendering.get(), change_source::user);
            }

            if (!ImGui::BeginMainMenuBar()) return;

            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Save settings", "Ctrl+S")) a.save_settings();
                if (ImGui::MenuItem("Load settings")) a.load_settings();
                ImGui::Separator();
                if (ImGui::MenuItem("Exit")) a.quit_requested = true;
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Render"))
            {
                if (ImGui::MenuItem(rendering.get() ? "Pause" : "Render", "F5")) rendering.set(!rendering.get(), change_source::user);
                if (ImGui::MenuItem("Clear buffer")) link.push(magik_command_clear_render_buffer_t{});
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("View"))
            {
                if (ImGui::MenuItem("Reset layout")) a.layout.reset();
                ImGui::Separator();
                if (ImGui::MenuItem("Zoom in", "Ctrl +"))  a.zoom->set(a.zoom->get() + 0.1f, change_source::user);
                if (ImGui::MenuItem("Zoom out", "Ctrl -")) a.zoom->set(a.zoom->get() - 0.1f, change_source::user);
                if (ImGui::MenuItem("Reset zoom", "Ctrl 0")) a.zoom->set(1.0f, change_source::user);
                ImGui::EndMenu();
            }

            ImGui::EndMainMenuBar();
        };
    }
}

#endif
