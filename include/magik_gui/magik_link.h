#ifndef MAGIK_GUI_MAGIK_LINK_H
#define MAGIK_GUI_MAGIK_LINK_H

/*
    Thin wrapper around Magik's command queue system.

        link.push(magik_command_set_kerr_dimensionless_spin_parameter_t{ .dimensionless_spin_parameter = 0.9f });
        link.add_aov("test", 3);

    * every push is checked; failures go to the console with the command's name
    * end_frame() dispatches the command buffer once per GUI frame and polls the worker's async error
    * push counts are kept for the Statistics panel
    * with log_commands enabled every pushed command is echoed to the console (handy when wiring up new
      controls)

    To support a new Magik command: add it to command_name() below, then push it from an action in
    ui_configuration.h.
*/

#include <algorithm>
#include <cstddef>   // magik.h uses size_t without including stddef.h
#include <cstring>
#include <string>
#include <string_view>

#include "magik.h"
#include "console.h"

namespace magik_gui
{
    inline const char* command_name(e_magik_cqs_command_types type)
    {
        switch (type)
        {
            case MAGIK_COMMAND_SET_STATE_RENDER:                                              return "set_state_render";
            case MAGIK_COMMAND_SET_STATE_PAUSE:                                               return "set_state_pause";
            case MAGIK_COMMAND_CLEAR_RENDER_BUFFER:                                           return "clear_render_buffer";
            case MAGIK_COMMAND_ADD_AOV:                                                       return "add_aov";
            case MAGIK_COMMAND_REMOVE_AOV:                                                    return "remove_aov";
            case MAGIK_COMMAND_PRINTF:                                                        return "printf";
            case MAGIK_COMMAND_SET_METRIC_MINKOWSKI:                                          return "set_metric_minkowski";
            case MAGIK_COMMAND_SET_METRIC_SCHWARZSCHILD:                                      return "set_metric_schwarzschild";
            case MAGIK_COMMAND_SET_METRIC_KERR:                                               return "set_metric_kerr";
            case MAGIK_COMMAND_SET_ABSOLUTE_ERROR_TOLERANCE:                                  return "set_absolute_error_tolerance";
            case MAGIK_COMMAND_SET_RELATIVE_ERROR_TOLERANCE:                                  return "set_relative_error_tolerance";
            case MAGIK_COMMAND_SET_MAX_SUBSTEPS:                                              return "set_max_substeps";
            case MAGIK_COMMAND_SET_OBSERVER_LOCAL_PROPER_VELOCITY:                            return "set_observer_local_proper_velocity";
            case MAGIK_COMMAND_SET_CELESTIAL_SPHERE_RADIUS:                                   return "set_celestial_sphere_radius";
            case MAGIK_COMMAND_SET_SCHWARZSCHILD_EVENT_HORIZON_SCALAR:                        return "set_schwarzschild_event_horizon_scalar";
            case MAGIK_COMMAND_SET_SCHWARZSCHILD_MASS_PARAMETER:                              return "set_schwarzschild_mass_parameter";
            case MAGIK_COMMAND_SET_KERR_EVENT_HORIZON_SCALAR:                                 return "set_kerr_event_horizon_scalar";
            case MAGIK_COMMAND_SET_KERR_MASS_PARAMETER:                                       return "set_kerr_mass_parameter";
            case MAGIK_COMMAND_SET_KERR_DIMENSIONLESS_SPIN_PARAMETER:                         return "set_kerr_dimensionless_spin_parameter";
            case MAGIK_COMMAND_SET_ACTIVE_CAMERA:                                             return "set_active_camera";
            case MAGIK_COMMAND_SET_CAMERA_POSITION:                                           return "set_camera_position";
            case MAGIK_COMMAND_SET_CAMERA_ORIENTATION:                                        return "set_camera_orientation";
            case MAGIK_COMMAND_SET_CAMERA_RESOLUTION:                                         return "set_camera_resolution";
            case MAGIK_COMMAND_SET_N_INTRINSIC_SPECTRAL_IRRADIANCE_BIN:                       return "set_n_intrinsic_spectral_irradiance_bin";
            case MAGIK_COMMAND_SET_SAMPLE_DOMAIN_MAXIMUM_WAVELENGTH_NM:                       return "set_sample_domain_maximum_wavelength_nm";
            case MAGIK_COMMAND_SET_SAMPLE_DOMAIN_MINIMUM_WAVELENGTH_NM:                       return "set_sample_domain_minimum_wavelength_nm";
            case MAGIK_COMMAND_SET_REDISTRIBUTION_STANDARD_DEVIATION_NM:                      return "set_redistribution_standard_deviation_nm";
            case MAGIK_COMMAND_SET_COLOR_MATCHING_FUNCTION:                                   return "set_color_matching_function";
            case MAGIK_COMMAND_SET_SCENE_COLORSPACE:                                          return "set_scene_colorspace";
            case MAGIK_COMMAND_SET_VIEW_TRANSFORM:                                            return "set_view_transform";
            case MAGIK_COMMAND_SET_DISPLAY_COLORSPACE:                                        return "set_display_colorspace";
            case MAGIK_COMMAND_SET_OUTPUT_FORMAT:                                             return "set_output_format";
            case MAGIK_COMMAND_SET_WHITE_BALANCE_WHITE_POINT:                                 return "set_white_balance_white_point";
            case MAGIK_COMMAND_SET_WHITE_BALANCE_TINT:                                        return "set_white_balance_tint";
            case MAGIK_COMMAND_SET_VIEW_TRANSFORM_REINHARD_EXTENDED_WHITE_LUMINANCE:          return "set_view_transform_reinhard_extended_white_luminance";
            case MAGIK_COMMAND_SET_VIEW_TRANSFORM_KHRONOS_PBR_NEUTRAL_START_COMPRESSION:      return "set_view_transform_khronos_pbr_neutral_start_compression";
            case MAGIK_COMMAND_SET_VIEW_TRANSFORM_KHRONOS_PBR_NEUTRAL_DESATURATION:           return "set_view_transform_khronos_pbr_neutral_desaturation";
            case MAGIK_COMMAND_SET_VIEW_TRANSFORM_GT7_MODE:                                   return "set_view_transform_gt7_mode";
            case MAGIK_COMMAND_SET_VIEW_TRANSFORM_GT7_SDR_PAPER_WHITE_BALANCE:                return "set_view_transform_gt7_sdr_paper_white_balance";
            case MAGIK_COMMAND_SET_VIEW_TRANSFORM_GT7_REFERENCE_LUMINANCE:                    return "set_view_transform_gt7_reference_luminance";
            case MAGIK_COMMAND_SET_VIEW_TRANSFORM_GT7_JZAZBZ_EXPONENT_SCALE_FACTOR:           return "set_view_transform_gt7_jzazbz_exponent_scale_factor";
            case MAGIK_COMMAND_SET_N_GEODESIC_STEP_LIMIT:                                     return "set_n_geodesic_step_limit";
            case MAGIK_COMMAND_SET_N_REFLECTED_LIMIT:                                         return "set_n_reflected_limit";
            default:                                                                          return "unknown command";
        }
    }

    inline const char* result_name(e_magik_result_types r)
    {
        switch (r)
        {
            case MAGIK_SUCCESS:                                                               return "success";
            case MAGIK_ERROR_NOT_RUNNING:                                                     return "not running";
            case MAGIK_ERROR_INVALID_POINTER:                                                 return "invalid pointer";
            case MAGIK_UNKNOWN_ENUM_TYPE:                                                     return "unknown enum type";
            case MAGIK_INVALID_CUDA_DEVICE:                                                   return "invalid cuda device";
            case MAGIK_ERROR_HOST_OUT_OF_MEMORY:                                              return "host out of memory";
            case MAGIK_ERROR_HOST_MEMORY_ALLOCATION_FAILED:                                   return "host memory allocation failed";
            case MAGIK_ERROR_HOST_TRACE_ALLOCATED_STEPS_AND_CAPACITY_MISMATCH:                return "host trace allocated steps and capacity mismatch";
            case MAGIK_ERROR_DEVICE_OUT_OF_MEMORY:                                            return "device out of memory";
            case MAGIK_ERROR_DEVICE_ALLOCATION_FAILED:                                        return "device allocation failed";
            case MAGIK_ERROR_INVALID_ID:                                                      return "invalid id";
            case MAGIK_ERROR_PROVIDED_ID_NOT_FOUND:                                           return "provided id not found";
            case MAGIK_ERROR_COMMAND_BUFFER_OVERFLOW:                                         return "command buffer overflow";
            case MAGIK_ERROR_INVALID_COMMAND:                                                 return "invalid command";
            case MAGIK_ERROR_PACKED_COMMAND_NOT_ALLIGNED:                                     return "packed command not alligned";
            case MAGIK_ERROR_COMMAND_DROPPED:                                                 return "command dropped";
            case MAGIK_ERROR_COMMAND_BUFFER_ALLOCATION_FAILED:                                return "command buffer allocation failed";
            case MAGIK_ERROR_COMMAND_SIZE_NOT_A_MULTIPLE_OF_4:                                return "command size not a multiple of 4";
            case MAGIK_ERROR_OUT_OF_BOUNDS_COMMAND_BUFFER_READ:                               return "out of bounds command buffer read";
            case MAGIK_ERROR_NEGATIVE_WAVELENGTH:                                             return "negative wavelength";
            case MAGIK_ERROR_NEGATIVE_MONTE_CARLO_WEIGHT:                                     return "negative monte carlo weight";
            case MAGIK_ERROR_REFLECTANCE_GREATER_THAN_ONE:                                    return "reflectance greater than one";
            case MAGIK_ERROR_REFLECTANCE_LESS_THAN_ZERO:                                      return "reflectance less than zero";
            case MAGIK_ERROR_POSITION_INSIDE_CONDUCTOR:                                       return "position inside conductor";
            case MAGIK_ERROR_NEGATIVE_VOLUME_COEFFICIENT:                                     return "negative volume coefficient";
            case MAGIK_ERROR_VELOCITY_GREATER_THAN_SPEED_OF_LIGHT:                            return "velocity greater than speed of light";
            case MAGIK_ERROR_POSITION_INSIDE_EVENT_HORIZON:                                   return "position inside event horizon";
            case MAGIK_ERROR_NEGATIVE_DOPPLER_FACTOR:                                         return "negative doppler factor";
            case MAGIK_ERROR_NEGATIVE_TIME_COORDINATE:                                        return "negative time coordinate";
            case MAGIK_ERROR_POSITIVE_NORM_TIMELIKE_FOUR_VECTOR:                              return "positive norm timelike four vector";
            case MAGIK_ERROR_INCORRECT_PHOTON_NORM_FOUR_VECTOR:                               return "incorrect photon norm four vector";
            case MAGIK_ERROR_INCORRECT_PARTICLE_NORM_FOUR_VECTOR:                             return "incorrect particle norm four vector";
            case MAGIK_ERROR_UNKNOWN_METRIC:                                                  return "unknown metric";
            case MAGIK_ERROR_LENSLETS_OUT_OF_ORDER:                                           return "lenslets out of order";
            case MAGIK_ERROR_NO_FRONT_LENSLET_ID:                                             return "no front lenslet id";
            case MAGIK_ERROR_LENSLET_CYLINDER_LARGER_THAN_SURFACE_RADIUS:                     return "lenslet cylinder larger than surface radius";
            case MAGIK_ERROR_NO_REFERENCE_TO_SPECIFIED_SPECTRAL_DATA:                         return "no reference to specified spectral data";
            case MAGIK_ERROR_AOV_INCORRECT_EXTRACT_CALL:                                      return "aov incorrect extract call";
            case MAGIK_ERROR_AOV_ALLOCATED_BEFORE_INITIALIZATION:                             return "aov allocated before initialization";
            case MAGIK_ERROR_AOV_SWAPCHAIN_NOT_INITALIZED:                                    return "aov swapchain not initalized";
            case MAGIK_ERROR_GL_LOADER_FAILED:                                                return "gl loader failed";
            case MAGIK_ERROR_GL_FUNCTIONS_NOT_LOADED:                                         return "gl functions not loaded";
            case MAGIK_ERROR_GL_BUFFER_SIZE_MISMATCH:                                         return "gl buffer size mismatch";
            case MAGIK_ERROR_UNKNOWN_COLOR_MATCHING_FUNCTION:                                 return "unknown color matching function";
            case MAGIK_ERROR_UNKNOWN:                                                         return "unknown";
            default:                                                                          return nullptr;
        }
    }

    inline std::string describe(e_magik_result_types r)
    {
        const char* name = result_name(r);
        return name ? std::string(name) + " (" + std::to_string(static_cast<unsigned>(r)) + ")"
                    : "error " + std::to_string(static_cast<unsigned>(r));
    }

    /**
    * Route check_magik_errors() into the console.
    */
    inline void install_error_callback(console& log)
    {
        magik_set_error_callback(
            [](e_magik_result_types result, const char* func, const char* file, int line, void* user)
            {
                const char* slash = std::strrchr(file, '/');
                const char* bslash = std::strrchr(file, '\\');
                const char* name = std::max(slash, bslash) ? std::max(slash, bslash) + 1 : file;
                static_cast<console*>(user)->log(log_level::error, "Magik: %s in '%s' (%s:%d)", describe(result).c_str(), func, name, line);
            },
            &log);
    }

    class magik_link
    {
    public:
        magik_link(magik_render_manager_t manager_, console& log_) : manager(manager_), log(log_) {}

        /**
        * Pushes any magik_command_*_t. Returns false (and logs) on failure.
        */
        template <typename Cmd>
        bool push(const Cmd& cmd)
        {
            const e_magik_cqs_command_types type = cmd.embedded_type;
            const e_magik_result_types r = magik_cqs_push_command(manager, &cmd);

            if (r != MAGIK_SUCCESS)
            {
                log.log(log_level::error, "push %s failed: %s", command_name(type), describe(r).c_str());
                return false;
            }

            pushed_this_frame++;
            pushed_total++;
            if (log_commands) log.log(log_level::debug, "-> %s", command_name(type));
            return true;
        }

        // ### Commands carrying a string ###

        bool add_aov(std::string_view name, uint32_t channels)
        {
            magik_command_add_aov_t cmd;
            cmd.channels = channels;
            cmd.length_of_name = copy_text(cmd.name, sizeof(cmd.name), name);
            return push(cmd);
        }

        bool remove_aov(std::string_view name)
        {
            magik_command_remove_aov_t cmd;
            cmd.length_of_name = copy_text(cmd.name, sizeof(cmd.name), name);
            return push(cmd);
        }

        bool print(std::string_view text)
        {
            magik_command_printf_t cmd;
            cmd.length_of_text = copy_text(cmd.text, sizeof(cmd.text), text);
            return push(cmd);
        }

        /**
        * Once per GUI frame, after all pushes.
        */
        void end_frame()
        {
            bool dispatched = false;
            const e_magik_result_types r = magik_cqs_dispatch_command_buffer(manager, &dispatched);
            if (r != MAGIK_SUCCESS) log.log(log_level::error, "dispatch failed: %s", describe(r).c_str());

            if (dispatched) { dispatches++; pending_commands = 0; }
            else            { pending_commands += pushed_this_frame; }
            pushed_this_frame = 0;

            const e_magik_result_types async = magik_render_manager_fetch_error(manager);
            if (async != last_async_error)
            {
                if (async != MAGIK_SUCCESS) log.log(log_level::error, "Magik worker: %s", describe(async).c_str());
                last_async_error = async;
            }
        }

        magik_render_manager_t handle() const { return manager; }

        bool log_commands = false;

        // Statistics
        uint64_t pushed_total = 0;
        uint64_t dispatches = 0;
        uint64_t pending_commands = 0;      // pushed but not yet handed to the worker
        e_magik_result_types last_async_error = MAGIK_SUCCESS;

    private:
        static uint32_t copy_text(char* dst, size_t capacity, std::string_view src)
        {
            const size_t n = std::min(src.size(), capacity - 1);
            std::memcpy(dst, src.data(), n);
            dst[n] = '\0';
            return static_cast<uint32_t>(n);
        }

        magik_render_manager_t manager;
        console& log;
        uint64_t pushed_this_frame = 0;
    };
}

#endif
