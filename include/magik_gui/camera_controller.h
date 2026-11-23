#ifndef MAGIK_GUI_CAMERA_CONTROLLER_H
#define MAGIK_GUI_CAMERA_CONTROLLER_H

/*
    Blender style orbit camera for the viewport (modelled after the Illyana lens sim controller).

    The camera orbits a pivot point. World up is +y, the camera looks along its local -z (Magik's convention).

        Middle mouse drag            orbit around the pivot
        Mouse wheel                  dolly towards / away from the pivot
        Ctrl  + middle mouse drag    move along the view direction (fine zoom)
        Shift + middle mouse drag    pan: moves camera and pivot together
        Alt + left mouse             same as middle mouse (for touchpads)

    The controller only writes the camera.position / camera.orientation values; their actions send the
    Magik commands, so the Settings panel, context menu and settings file all stay in sync.
*/

#include <algorithm>
#include <array>
#include <cmath>

#include "imgui.h"
#include "mgui/mgui.h"

namespace magik_gui
{
    namespace camera_math
    {
        using mgui::vec3;
        using mat3 = std::array<std::array<float, 3>, 3>;   // [row][column]

        inline vec3 add(vec3 a, vec3 b) { return { a[0] + b[0], a[1] + b[1], a[2] + b[2] }; }
        inline vec3 sub(vec3 a, vec3 b) { return { a[0] - b[0], a[1] - b[1], a[2] - b[2] }; }
        inline vec3 mul(vec3 a, float s) { return { a[0] * s, a[1] * s, a[2] * s }; }
        inline float length(vec3 a) { return std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]); }
        inline vec3 normalize(vec3 a) { const float l = length(a); return l > 0.0f ? mul(a, 1.0f / l) : vec3{ 0.0f, 0.0f, 0.0f }; }
        inline vec3 cross(vec3 a, vec3 b) { return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] }; }
        inline vec3 mul(const mat3& m, vec3 v)
        {
            return { m[0][0] * v[0] + m[0][1] * v[1] + m[0][2] * v[2],
                     m[1][0] * v[0] + m[1][1] * v[1] + m[1][2] * v[2],
                     m[2][0] * v[0] + m[2][1] * v[1] + m[2][2] * v[2] };
        }

        /**
        * Magik's orientation: matrix_three_type::rotate(z, y, x) = Rx(x) * Ry(y) * Rz(z), angles in radians.
        */
        inline mat3 magik_orientation(vec3 r)
        {
            const float ca = std::cos(r[0]), sa = std::sin(r[0]);
            const float cb = std::cos(r[1]), sb = std::sin(r[1]);
            const float cc = std::cos(r[2]), sc = std::sin(r[2]);
            return { { { cb * cc,                -cb * sc,                 sb       },
                       { ca * sc + sa * sb * cc,  ca * cc - sa * sb * sc, -sa * cb  },
                       { sa * sc - ca * sb * cc,  sa * cc + ca * sb * sc,  ca * cb  } } };
        }

        /**
        * Inverse of magik_orientation: the (x, y, z) angles with Rx(x) * Ry(y) * Rz(z) == m.
        */
        inline vec3 magik_euler(const mat3& m)
        {
            const float sb = std::clamp(m[0][2], -1.0f, 1.0f);
            const float b = std::asin(sb);
            if (std::fabs(sb) < 0.99999f) return { std::atan2(-m[1][2], m[2][2]), b, std::atan2(-m[0][1], m[0][0]) };
            return { std::atan2(m[2][1], m[1][1]), b, 0.0f };   // gimbal lock, put everything into x
        }

        /**
        * Orientation that looks from `eye` at `target` with +y up (no roll). Columns: right, up, back.
        */
        inline mat3 look_at(vec3 eye, vec3 target)
        {
            const vec3 back = normalize(sub(eye, target));
            vec3 right = cross(vec3{ 0.0f, 1.0f, 0.0f }, back);
            if (length(right) < 1.0e-6f) right = { 1.0f, 0.0f, 0.0f };   // looking straight up / down
            right = normalize(right);
            const vec3 up = cross(back, right);
            return { { { right[0], up[0], back[0] },
                       { right[1], up[1], back[1] },
                       { right[2], up[2], back[2] } } };
        }
    }

    class orbit_controller
    {
    public:
        orbit_controller(mgui::value<mgui::vec3>& position_, mgui::value<mgui::vec3>& orientation_, mgui::value<mgui::vec3>& pivot_)
            : position(position_), orientation(orientation_), pivot(pivot_) {}

        // Tuning, same feel as the Illyana controller
        float orbit_speed_x = 14.0f;    // radians per viewport width
        float orbit_speed_y = 7.0f;
        float wheel_step = 0.25f;       // fraction of the distance per wheel notch
        float min_distance = 0.05f;     // metres

        /**
        * Turns the camera to look at the pivot without moving it.
        */
        void look_at_pivot()
        {
            orientation.set(camera_math::magik_euler(camera_math::look_at(position.get(), pivot.get())), mgui::change_source::user);
        }

        /**
        * Call from the viewport panel every frame. `viewport_width` is the width of the image area in pixels.
        */
        void update(float viewport_width)
        {
            using namespace camera_math;
            const ImGuiIO& io = ImGui::GetIO();

            const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
            const bool alt_left = io.KeyAlt && ImGui::IsMouseDown(ImGuiMouseButton_Left);
            const bool button_down = ImGui::IsMouseDown(ImGuiMouseButton_Middle) || alt_left;

            // Grab on press inside the viewport, keep the grab until release even outside of it
            if (!dragging && hovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) || (io.KeyAlt && ImGui::IsMouseClicked(ImGuiMouseButton_Left)))) dragging = true;
            if (!button_down) dragging = false;

            const float wheel = hovered ? io.MouseWheel : 0.0f;
            const ImVec2 delta = dragging ? io.MouseDelta : ImVec2(0.0f, 0.0f);
            if (!dragging && wheel == 0.0f) return;
            if (dragging) ImGui::SetMouseCursor(io.KeyShift ? ImGuiMouseCursor_ResizeAll : ImGuiMouseCursor_Hand);
            if (delta.x == 0.0f && delta.y == 0.0f && wheel == 0.0f) return;

            const float w = std::max(1.0f, viewport_width);
            vec3 eye = position.get();
            vec3 center = pivot.get();
            vec3 offset = sub(eye, center);
            const float r = std::max(length(offset), min_distance);

            if (dragging && io.KeyCtrl)
            {
                // Fine zoom: move along the view direction, keep the pivot
                const vec3 forward = mul(normalize(offset), -1.0f);
                const float step = -delta.y / w * r;
                if (r - step > min_distance) eye = add(eye, mul(forward, step));
            }
            else if (dragging && io.KeyShift)
            {
                // Pan: camera and pivot move together in the camera plane
                const mat3 m = look_at(eye, center);
                const vec3 right{ m[0][0], m[1][0], m[2][0] };
                const vec3 up{ m[0][1], m[1][1], m[2][1] };
                const vec3 move = add(mul(right, delta.x / w * r), mul(up, delta.y / w * r));
                eye = add(eye, move);
                center = add(center, move);
            }
            else
            {
                // Orbit (+ wheel dolly) in spherical coordinates around the pivot, y up
                float theta = std::acos(std::clamp(offset[1] / r, -1.0f, 1.0f));
                float phi = std::atan2(offset[0], offset[2]);

                theta -= delta.y / w * orbit_speed_y;
                phi -= delta.x / w * -orbit_speed_x;
                theta = std::clamp(theta, 0.001f, 3.14059265f);   // stay off the poles, no flip over the top

                const float new_r = std::max(min_distance, r * (1.0f - wheel * wheel_step));
                eye = add(center, vec3{ new_r * std::sin(theta) * std::sin(phi), new_r * std::cos(theta), new_r * std::sin(theta) * std::cos(phi) });
            }

            if (center != pivot.get()) pivot.set(center, mgui::change_source::user);
            position.set(eye, mgui::change_source::user);
            orientation.set(magik_euler(look_at(eye, center)), mgui::change_source::user);
        }

    private:
        mgui::value<mgui::vec3>& position;
        mgui::value<mgui::vec3>& orientation;
        mgui::value<mgui::vec3>& pivot;
        bool dragging = false;
    };
}

#endif
