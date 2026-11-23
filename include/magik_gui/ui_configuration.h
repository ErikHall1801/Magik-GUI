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
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "app.h"
#include "camera_controller.h"

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
        constexpr float min_abs_mass = 1.0f;               // kg, smallest |M| sent to Magik (0 is degenerate)

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

        /**
        * Sends `Cmd` with `field` set to the value whenever the value changes (including once at start-up).
        * For the many commands that carry a single number or enum:
        *     detail::send(link, abs_tolerance, &magik_command_set_absolute_error_tolerance_t::absolute_error_tolerance);
        */
        template <typename Cmd, typename F, typename T>
        void send(magik_link& link, mgui::value<T>& v, F Cmd::* field)
        {
            v.on_change([&link, field](const mgui::change<T>& c)
            {
                Cmd cmd;
                cmd.*field = static_cast<F>(c.new_value);
                link.push(cmd);
            });
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
        auto& cam_pivot     = store.declare<vec3>("camera.pivot", { 0.0f, 0.0f, 0.0f });         // orbit centre, GUI only
        // Spacetime
        auto& metric        = store.declare<int>("scene.metric", 2);                             // 0 Minkowski, 1 Schwarzschild, 2 Kerr
        auto& schw_mass     = store.declare<float>("scene.schwarzschild_mass", 6.7329546075E26f);   // |M|
        auto& schw_negative = store.declare<bool>("scene.schwarzschild_negative", false);           // sign of M
        auto& kerr_mass     = store.declare<float>("scene.kerr_mass", 6.7329546075E26f);
        auto& kerr_spin     = store.declare<float>("scene.kerr_spin", 0.999f);
        auto& schw_horizon  = store.declare<float>("scene.schwarzschild_horizon_scalar", 1.01f);
        auto& kerr_horizon  = store.declare<float>("scene.kerr_horizon_scalar", 1.01f);
        auto& sky_radius    = store.declare<float>("scene.celestial_sphere_radius", 1000.0f);    // m
        // Integrator
        auto& abs_tolerance = store.declare<float>("integrator.absolute_tolerance", 1e-3f);
        auto& rel_tolerance = store.declare<float>("integrator.relative_tolerance", 1e-10f);
        auto& max_substeps  = store.declare<int>("integrator.max_substeps", 56);
        // Render limits
        auto& step_limit    = store.declare<int>("render.geodesic_step_limit", 512);
        auto& bounce_limit  = store.declare<int>("render.reflection_limit", 10);
        // Color (defaults = Magik's color_settings_t)
        auto& n_bins        = store.declare<int>("color.spectral_bins", 24);
        auto& wl_min        = store.declare<float>("color.min_wavelength_nm", 360.0f);
        auto& wl_max        = store.declare<float>("color.max_wavelength_nm", 830.0f);
        auto& redist_sd     = store.declare<float>("color.redistribution_sd_nm", 10.0f);
        auto& cmf           = store.declare<int>("color.cmf", MAGIK_CMF_CIE_1931);
        auto& scene_cs      = store.declare<int>("color.scene_colorspace", MAGIK_SCENE_COLORSPACE_LINEAR_REC709);
        auto& display_cs    = store.declare<int>("color.display_colorspace", MAGIK_DISPLAY_COLORSPACE_REC709);
        auto& output_fmt    = store.declare<int>("color.output_format", MAGIK_OUTPUT_FORMAT_FLOAT32);
        auto& white_point   = store.declare<float>("color.white_point_k", 5600.0f);
        auto& tint          = store.declare<float>("color.tint", 0.0f);
        auto& view_tf       = store.declare<int>("color.view_transform", MAGIK_VIEW_TRANSFORM_GRAN_TURISMO_7);
        auto& reinhard_wl   = store.declare<float>("color.reinhard_white_luminance", 4.0f);
        auto& pbr_start     = store.declare<float>("color.pbr_neutral_start_compression", 0.8f);
        auto& pbr_desat     = store.declare<float>("color.pbr_neutral_desaturation", 0.15f);
        auto& gt7_mode      = store.declare<int>("color.gt7_mode", MAGIK_GRAN_TURISMO_7_MODE_JZAZBZ);
        auto& gt7_paper     = store.declare<float>("color.gt7_sdr_paper_white", 250.0f);
        auto& gt7_ref       = store.declare<float>("color.gt7_reference_luminance", 100.0f);
        auto& gt7_jz_scale  = store.declare<float>("color.gt7_jzazbz_exponent_scale", 1.7f);
        auto& agx_look      = store.declare<int>("color.agx_look", MAGIK_AGX_LOOK_PUNCHY);
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
        // Magik supports negative Schwarzschild mass (it uses |M| and the sign separately), but M == 0 makes its
        // length scale G|M|/c^2 zero and every position is divided by it. The GUI mirrors Magik: a magnitude that
        // never reaches 0 plus a sign, combined into one signed value when the command is sent.
        schw_mass.sanitize_with([](const float& m) { return std::max(std::fabs(m), detail::min_abs_mass); });
        active_camera.sanitize_with([](const int& id) { return std::max(id, 0); });
        aov_new_ch.sanitize_with([](const int& n) { return std::clamp(n, 1, 4); });

        // Magik silently ignores values outside these limits, which would leave the GUI showing a value the
        // renderer does not use, so the GUI enforces the same limits.
        auto at_least = [](float lo) { return [lo](const float& x) { return std::max(x, lo); }; };
        auto int_range = [](int lo, int hi) { return [lo, hi](const int& x) { return std::clamp(x, lo, hi); }; };
        schw_horizon.sanitize_with(at_least(1.0001f));       // must be > 1
        kerr_horizon.sanitize_with(at_least(1.0001f));
        sky_radius.sanitize_with(at_least(1.0e-3f));         // > 0
        abs_tolerance.sanitize_with(at_least(1.0e-30f));     // > 0
        rel_tolerance.sanitize_with(at_least(1.0e-30f));
        max_substeps.sanitize_with(int_range(1, 100000));    // > 0
        step_limit.sanitize_with(int_range(1, 10000000));
        bounce_limit.sanitize_with(int_range(1, 100000));
        n_bins.sanitize_with(int_range(1, 64));              // 1 .. 64
        wl_min.sanitize_with(at_least(1.0f));                // > 0
        wl_max.sanitize_with(at_least(1.0f));
        redist_sd.sanitize_with(at_least(0.01f));            // > 0
        white_point.sanitize_with(at_least(500.0f));         // blackbody temperature
        gt7_paper.sanitize_with(at_least(0.01f));            // > 0
        gt7_ref.sanitize_with(at_least(0.01f));
        gt7_jz_scale.sanitize_with(at_least(1.0001f));       // > 1



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
            magik_command_set_observer_local_proper_velocity_t cmd;
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

        auto push_schwarzschild_mass = [&]
        {
            magik_command_set_schwarzschild_mass_parameter_t cmd;
            cmd.mass_parameter = schw_negative.get() ? -schw_mass.get() : schw_mass.get();
            link.push(cmd);
        };
        schw_mass.on_change([=](const change<float>&) { push_schwarzschild_mass(); });
        schw_negative.on_change([=](const change<bool>& c) { if (!c.is_init()) push_schwarzschild_mass(); });   // init already sent by schw_mass

        kerr_mass.on_change([&](const change<float>& c)
        {
            magik_command_set_kerr_mass_parameter_t cmd;
            cmd.mass_parameter = c.new_value;
            link.push(cmd);
        });

        kerr_spin.on_change([&](const change<float>& c)
        {
            magik_command_set_kerr_dimensionless_spin_parameter_t cmd;
            cmd.dimensionless_spin_parameter = c.new_value;
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

        // Relativity / integrator
        detail::send(link, schw_horizon,  &magik_command_set_schwarzschild_event_horizon_scalar_t::scalar);
        detail::send(link, kerr_horizon,  &magik_command_set_kerr_event_horizon_scalar_t::scalar);
        detail::send(link, sky_radius,    &magik_command_set_celestial_sphere_radius_t::celestial_sphere_radius);
        detail::send(link, abs_tolerance, &magik_command_set_absolute_error_tolerance_t::absolute_error_tolerance);
        detail::send(link, rel_tolerance, &magik_command_set_relative_error_tolerance_t::relative_error_tolerance);
        detail::send(link, max_substeps,  &magik_command_set_max_substeps_t::max_substeps);
        // Render limits
        detail::send(link, step_limit,    &magik_command_set_n_geodesic_step_limit_t::n_geodesic_step_limit);
        detail::send(link, bounce_limit,  &magik_command_set_n_reflected_limit_t::n_reflected_limit);
        // Color
        detail::send(link, n_bins,        &magik_command_set_n_intrinsic_spectral_irradiance_bin_t::n_isi_bin);
        detail::send(link, wl_min,        &magik_command_set_sample_domain_minimum_wavelength_nm_t::minimum_wavelength_nm);
        detail::send(link, wl_max,        &magik_command_set_sample_domain_maximum_wavelength_nm_t::maximum_wavelength_nm);
        detail::send(link, redist_sd,     &magik_command_set_redistribution_standard_deviation_nm_t::standard_deviation_nm);
        detail::send(link, cmf,           &magik_command_set_color_matching_function_t::cmf_type);
        detail::send(link, scene_cs,      &magik_command_set_scene_colorspace_t::scene_colorspace);
        detail::send(link, display_cs,    &magik_command_set_display_colorspace_t::display_colorspace);
        detail::send(link, output_fmt,    &magik_command_set_output_format_t::output_format);
        detail::send(link, white_point,   &magik_command_set_white_balance_white_point_t::white_point_kelvin);
        detail::send(link, tint,          &magik_command_set_white_balance_tint_t::tint);
        detail::send(link, view_tf,       &magik_command_set_view_transform_t::view_transform);
        detail::send(link, reinhard_wl,   &magik_command_set_view_transform_reinhard_extended_white_luminance_t::white_luminance);
        detail::send(link, pbr_start,     &magik_command_set_view_transform_khronos_pbr_neutral_start_compression_t::start_compression);
        detail::send(link, pbr_desat,     &magik_command_set_view_transform_khronos_pbr_neutral_desaturation_t::desaturation);
        detail::send(link, gt7_mode,      &magik_command_set_view_transform_gt7_mode_t::mode);
        detail::send(link, gt7_paper,     &magik_command_set_view_transform_gt7_sdr_paper_white_balance_t::sdr_paper_white_balance);
        detail::send(link, gt7_ref,       &magik_command_set_view_transform_gt7_reference_luminance_t::reference_luminance);
        detail::send(link, gt7_jz_scale,  &magik_command_set_view_transform_gt7_jzazbz_exponent_scale_factor_t::jzazbz_exponent_scale_factor);
        detail::send(link, agx_look,      &magik_command_set_view_transform_agx_look_t::look);

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

            render.spinner("Geodesic steps", step_limit, 64)
                .range(1, 10000000)
                .commit(commit_policy::on_release())
                .tooltip("Maximum geodesic steps over a whole path (not the bounce limit)");
            render.spinner("Reflections", bounce_limit, 1)
                .range(1, 100000)
                .commit(commit_policy::on_release())
                .tooltip("Maximum number of reflections");
        }

        // --- Camera ---
        // Orbit controller for the viewport (Blender style). It only edits the camera values above.
        auto orbit = std::make_shared<orbit_controller>(cam_position, cam_rotation, cam_pivot);

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
                .tooltip("Observer's local proper velocity in m/s. May exceed c locally, bounded by c globally.");

            camera.readout("|v| / c", [&]
            {
                const vec3 v = cam_velocity.get();
                const double speed = std::sqrt(double(v[0]) * v[0] + double(v[1]) * v[1] + double(v[2]) * v[2]);
                return detail::format("%.4f", speed / detail::speed_of_light);
            });

            camera.drag("Pivot", cam_pivot, 0.02f)
                .format("%.2f")
                .tooltip("Point the viewport camera orbits around. Shift + middle mouse drag moves it with the camera.");

            auto& buttons = camera.row({ 1.0f, 1.0f, 1.0f });
            buttons.button("Look at pivot").on_click([=](const button_event&) { orbit->look_at_pivot(); });
            buttons.button("Reset camera").on_click([&](const button_event&)
            {
                cam_position.reset(change_source::user);
                cam_rotation.reset(change_source::user);
                cam_velocity.reset(change_source::user);
                cam_pivot.reset(change_source::user);
            });
            buttons.button("Stop").on_click([&](const button_event&) { cam_velocity.set({ 0.0f, 0.0f, 0.0f }, change_source::user); }).tooltip("Set the velocity to zero");

            camera.text("Viewport: middle mouse orbit, wheel zoom, Shift pan, Ctrl dolly (Alt + left mouse = middle mouse)")
                .colored(text::tone::dim)
                .wrapped();
        }

        // --- Spacetime ---
        auto& spacetime = settings.section("Spacetime");
        {
            spacetime.dropdown("Metric", metric, { "Minkowski", "Schwarzschild", "Kerr" });

            spacetime.drag("Mass", schw_mass)
                .range(detail::min_abs_mass, 1.0e32f)
                .logarithmic()
                .format("%.4g kg")
                .commit(commit_policy::on_release())
                .tooltip("|M|. Ctrl + click to type a value.")
                .visible_if([&] { return metric.get() == 1; });
            spacetime.checkbox("Negative mass", schw_negative)
                .visible_if([&] { return metric.get() == 1; });
            spacetime.text("Negative mass: repulsive, no event horizon")
                .colored(text::tone::warning)
                .wrapped()
                .visible_if([&] { return metric.get() == 1 && schw_negative.get(); });
            spacetime.readout("r_s", [&] { return detail::format("%.4g m", detail::schwarzschild_radius(schw_mass.get())); })
                .tooltip("Schwarzschild radius 2GM / c^2")
                .visible_if([&] { return metric.get() == 1; });
            spacetime.slider("Horizon scalar", schw_horizon, 1.0001f, 1.5f)
                .format("%.4f")
                .commit(commit_policy::on_release())
                .tooltip("Enlarges the event horizon slightly so geodesics terminate sooner. Must be > 1.")
                .visible_if([&] { return metric.get() == 1; });

            spacetime.drag("Mass", kerr_mass)
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
            spacetime.slider("Horizon scalar", kerr_horizon, 1.0001f, 1.5f)
                .format("%.4f")
                .commit(commit_policy::on_release())
                .tooltip("Enlarges the event horizon slightly so geodesics do not orbit for hundreds of steps. Must be > 1.")
                .visible_if([&] { return metric.get() == 2; });

            spacetime.drag("Celestial sphere", sky_radius)
                .range(1.0e-3f, 1.0e9f)
                .logarithmic()
                .format("%.4g m")
                .commit(commit_policy::on_release())
                .tooltip("Radius of the celestial sphere centred on the observer");
        }

        // --- Integrator ---
        auto& integrator = settings.section("Integrator", false);
        {
            integrator.drag("Absolute tolerance", abs_tolerance)
                .range(1.0e-12f, 1.0f)
                .logarithmic()
                .format("%.2e")
                .commit(commit_policy::on_release());
            integrator.drag("Relative tolerance", rel_tolerance)
                .range(1.0e-16f, 1.0f)
                .logarithmic()
                .format("%.2e")
                .commit(commit_policy::on_release());
            integrator.spinner("Max substeps", max_substeps, 1)
                .range(1, 100000)
                .commit(commit_policy::on_release())
                .tooltip("Maximum RKF45 substeps per iteration");
        }

        // --- Color ---
        auto& color = settings.section("Color", false);
        {
            color.spinner("Spectral bins", n_bins, 1)
                .range(1, 64)
                .commit(commit_policy::on_release())
                .tooltip("Bins of the intrinsic spectral irradiance buffer. Diminishing returns past ~36.");
            color.drag("Min wavelength", wl_min, 1.0f)
                .range(1.0f, 5000.0f)
                .format("%.0f nm")
                .commit(commit_policy::on_release());
            color.drag("Max wavelength", wl_max, 1.0f)
                .range(1.0f, 5000.0f)
                .format("%.0f nm")
                .commit(commit_policy::on_release());
            color.text("Min wavelength must be below max wavelength")
                .colored(text::tone::warning)
                .wrapped()
                .visible_if([&] { return wl_min.get() >= wl_max.get(); });
            color.drag("Redistribution SD", redist_sd, 0.1f)
                .range(0.01f, 500.0f)
                .format("%.1f nm")
                .commit(commit_policy::on_release())
                .tooltip("Standard deviation of the gaussian that spreads each sample over adjacent bins");
            color.dropdown("Matching function", cmf, { "CIE 1931" });
            color.dropdown("Scene colorspace", scene_cs, { "CIE XYZ", "CIE RGB", "Linear Rec.709", "Linear Rec.2020", "ACEScg", "Adobe RGB (1998)" });
            color.dropdown("Display colorspace", display_cs, { "sRGB", "Rec.709", "Rec.2100 PQ", "Rec.2100 HLG", "DCI-P3 (theatrical white)", "Display P3", "Adobe RGB" });
            color.dropdown("Output format", output_fmt, { "UNorm 8", "UNorm 10", "UNorm 12", "UNorm 16", "Float 16", "Float 32" });

            color.separator("White balance");
            color.drag("White point", white_point, 10.0f)
                .range(500.0f, 40000.0f)
                .format("%.0f K")
                .commit(commit_policy::on_release());
            color.slider("Tint", tint, -0.05f, 0.05f)
                .format("%.4f")
                .commit(commit_policy::on_release())
                .tooltip("Offset from the blackbody locus in CIE uv (Duv)");

            color.separator("View transform");
            color.dropdown("View transform", view_tf, { "None", "Reinhard", "Reinhard extended", "Gran Turismo 7", "AgX", "Khronos PBR Neutral" });
            color.drag("White luminance", reinhard_wl, 0.01f)
                .range(0.0f, 1000.0f)
                .format("%.2f")
                .commit(commit_policy::on_release())
                .visible_if([&] { return view_tf.get() == MAGIK_VIEW_TRANSFORM_REINHARD_EXTENDED; });
            color.slider("Start compression", pbr_start, 0.0f, 1.0f)
                .format("%.3f")
                .commit(commit_policy::on_release())
                .visible_if([&] { return view_tf.get() == MAGIK_VIEW_TRANSFORM_KHRONOS_PBR_NEUTRAL; });
            color.slider("Desaturation", pbr_desat, 0.0f, 1.0f)
                .format("%.3f")
                .commit(commit_policy::on_release())
                .visible_if([&] { return view_tf.get() == MAGIK_VIEW_TRANSFORM_KHRONOS_PBR_NEUTRAL; });
            color.dropdown("GT7 mode", gt7_mode, { "ICtCp", "Jzazbz" })
                .visible_if([&] { return view_tf.get() == MAGIK_VIEW_TRANSFORM_GRAN_TURISMO_7; });
            color.drag("SDR paper white", gt7_paper, 1.0f)
                .range(0.01f, 10000.0f)
                .format("%.0f nits")
                .commit(commit_policy::on_release())
                .visible_if([&] { return view_tf.get() == MAGIK_VIEW_TRANSFORM_GRAN_TURISMO_7; });
            color.drag("Reference luminance", gt7_ref, 1.0f)
                .range(0.01f, 10000.0f)
                .format("%.0f nits")
                .commit(commit_policy::on_release())
                .visible_if([&] { return view_tf.get() == MAGIK_VIEW_TRANSFORM_GRAN_TURISMO_7; });
            color.drag("Jzazbz exponent scale", gt7_jz_scale, 0.01f)
                .range(1.0001f, 10.0f)
                .format("%.3f")
                .commit(commit_policy::on_release())
                .tooltip("Must be > 1")
                .visible_if([&] { return view_tf.get() == MAGIK_VIEW_TRANSFORM_GRAN_TURISMO_7 && gt7_mode.get() == MAGIK_GRAN_TURISMO_7_MODE_JZAZBZ; });
            color.dropdown("AgX look", agx_look, { "None", "Golden", "Punchy" })
                .visible_if([&] { return view_tf.get() == MAGIK_VIEW_TRANSFORM_AGX; });
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

        viewport.custom([&a, orbit](frame&)
        {
            const float width = ImGui::GetContentRegionAvail().x;
            a.draw_viewport();
            orbit->update(width);
        });

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
            menu.dropdown("Metric", metric, { "Minkowski", "Schwarzschild", "Kerr" });
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
