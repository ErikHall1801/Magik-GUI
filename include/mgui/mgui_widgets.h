#ifndef MGUI_WIDGETS_H
#define MGUI_WIDGETS_H

/*
    mgui widgets
    ============

    A widget is a view onto one or more values in the value_store. Every widget derives from
    `widget_t<Self>`, which gives it the chainable common options:

        .tooltip("...")                   hover help
        .visible_if([]{ return ...; })    hide the widget
        .enabled_if([]{ return ...; })    grey it out
        .labels(mgui::label_mode::top)    label position for this widget (left / top / none)

    Adding a new input type
    -----------------------
        // A widget bound to one value<T>. bound_widget gives it .on_change(), .commit(), .tooltip(), ...
        class knob : public mgui::bound_widget<knob, float>
        {
        public:
            knob(std::string label, mgui::value<float>& v) : bound_widget(std::move(label), v) {}

        protected:
            void draw_contents(mgui::frame& f) override
            {
                mgui::labelled_field field(f, label);           // label column + field width
                float tmp = val.get();
                const bool edited = ImGui::SliderFloat("##knob", &tmp, 0.0f, 1.0f);
                if (edited) val.edit(tmp);                      // show the new value ...
                commit_edit(edited, ImGui::IsItemDeactivated()); // ... and commit per the commit policy
            }
        };

        settings.add<knob>("Exposure", store.declare<float>("render.exposure", 0.5f));

    Use `labelled_field` for the label column, `f.layout.width` for the space you are allowed to use (never
    GetContentRegionAvail, it ignores rows) and `commit_edit` for continuous edits. Discrete widgets simply
    call val.set(x, change_source::user).
*/

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <functional>
#include <initializer_list>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include "imgui.h"
#include "mgui_core.h"
#include "mgui_style.h"

namespace mgui
{
    // ##############
    // ### Layout ###
    // ##############

    enum class label_mode
    {
        left,   // property grid: label column on the left, field on the right (default)
        top,    // label on its own line above the field
        none    // no label, the field takes the whole width
    };

    struct layout_state
    {
        float width = 100.0f;           // width the current widget may use, in pixels
        label_mode labels = label_mode::left;
        float label_ratio = 0.40f;      // label column share of the width in left mode
        float label_min = 60.0f;        // design pixels
        float label_max = 200.0f;       // design pixels
    };

    struct frame
    {
        value_store& store;
        layout_state layout;
    };

    /**
    * Draws a widget's label according to the current label mode and prepares the width of the field that
    * follows. Fields must use ImGui's "##id" form so the label is not drawn twice.
    */
    class labelled_field
    {
    public:
        labelled_field(frame& f, const std::string& label)
        {
            const layout_state& l = f.layout;
            const ImGuiStyle& style = ImGui::GetStyle();

            if (label.empty() || l.labels == label_mode::none)
            {
                width = l.width;
            }
            else if (l.labels == label_mode::top)
            {
                ImGui::TextUnformatted(label.c_str());
                width = l.width;
            }
            else
            {
                const float label_width = std::clamp(l.width * l.label_ratio, px(l.label_min), std::max(px(l.label_min), std::min(px(l.label_max), l.width * 0.6f)));
                const ImVec2 start = ImGui::GetCursorScreenPos();

                draw_clipped_label(label, start, label_width, ImGui::GetFrameHeight());

                ImGui::SameLine(0.0f, 0.0f);
                ImGui::SetCursorScreenPos(ImVec2(start.x + label_width + style.ItemInnerSpacing.x, ImGui::GetCursorScreenPos().y));
                width = std::max(1.0f, l.width - label_width - style.ItemInnerSpacing.x);
            }

            ImGui::SetNextItemWidth(width);
        }

        float width = 0.0f;

        /**
        * Draws `text` in a box of `w` x `h` (an ImGui item), with an ellipsis and a tooltip when it does
        * not fit. Used for the label column.
        */
        static void draw_clipped_label(const std::string& text, ImVec2 pos, float w, float h)
        {
            ImGui::Dummy(ImVec2(w, h));
            const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);

            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float text_h = ImGui::GetTextLineHeight();
            const ImVec2 text_pos(pos.x, pos.y + (h - text_h) * 0.5f);
            const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);

            const float full = ImGui::CalcTextSize(text.c_str()).x;
            if (full <= w)
            {
                dl->AddText(text_pos, col, text.c_str());
                return;
            }

            // Truncate with an ellipsis
            const char* ellipsis = "...";
            const float ellipsis_w = ImGui::CalcTextSize(ellipsis).x;
            size_t n = text.size();
            while (n > 0 && ImGui::CalcTextSize(text.c_str(), text.c_str() + n).x + ellipsis_w > w) n--;

            std::string shown = text.substr(0, n) + ellipsis;
            const ImVec4 clip(pos.x, pos.y, pos.x + w, pos.y + h);
            dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), text_pos, col, shown.c_str(), nullptr, 0.0f, &clip);

            if (hovered) ImGui::SetTooltip("%s", text.c_str());
        }
    };



    // ############
    // ### Base ###
    // ############

    class widget
    {
    public:
        explicit widget(std::string label_ = {}) : label(std::move(label_)) {}
        virtual ~widget() = default;

        widget(const widget&) = delete;
        widget& operator=(const widget&) = delete;

        void draw(frame& f)
        {
            if (visible_fn && !visible_fn()) return;

            const bool disabled = enabled_fn && !enabled_fn();
            if (disabled) ImGui::BeginDisabled(true);

            frame local = f;
            if (label_override) local.layout.labels = *label_override;

            ImGui::PushID(this);
            ImGui::BeginGroup();
            draw_contents(local);
            ImGui::EndGroup();
            ImGui::PopID();

            if (!tooltip_text.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
            {
                ImGui::SetTooltip("%s", tooltip_text.c_str());
            }

            if (disabled) ImGui::EndDisabled();
        }

        const std::string& get_label() const { return label; }

    protected:
        virtual void draw_contents(frame& f) = 0;

        std::string label;
        std::string tooltip_text;
        std::function<bool()> visible_fn;
        std::function<bool()> enabled_fn;
        std::optional<label_mode> label_override;   // for this widget and, for containers, everything inside
    };

    /**
    * CRTP helper so the common options return the concrete widget type and can be chained with its own
    * options, e.g. slider.tooltip("...").commit(commit_policy::on_release()).
    */
    template <typename Self>
    class widget_t : public widget
    {
    public:
        using widget::widget;

        Self& tooltip(std::string text) { tooltip_text = std::move(text); return self(); }
        Self& visible_if(std::function<bool()> fn) { visible_fn = std::move(fn); return self(); }
        Self& enabled_if(std::function<bool()> fn) { enabled_fn = std::move(fn); return self(); }

        /**
        * Where the label goes: label_mode::left (label column), top (own line) or none.
        */
        Self& labels(label_mode m) { label_override = m; return self(); }

    protected:
        Self& self() { return static_cast<Self&>(*this); }
    };

    /**
    * Base for widgets bound to one value<T>. Adds .on_change() (forwards to the value) and .commit().
    */
    template <typename Self, typename T>
    class bound_widget : public widget_t<Self>
    {
    public:
        bound_widget(std::string label_, value<T>& v) : widget_t<Self>(std::move(label_)), val(v) {}

        /**
        * Convenience: registers an action on the bound value. If several widgets show the same key,
        * register the action once (on the value, or on one widget), otherwise it runs once per widget.
        */
        Self& on_change(typename value<T>::action_fn fn) { val.on_change(std::move(fn)); return this->self(); }

        Self& commit(commit_policy p) { policy_fn = [p] { return p; }; return this->self(); }

        /**
        * Policy decided at runtime, e.g. from a "live updates" checkbox.
        */
        Self& commit(std::function<commit_policy()> fn) { policy_fn = std::move(fn); return this->self(); }

        value<T>& bound() { return val; }

    protected:
        commit_policy policy() const { return policy_fn ? policy_fn() : commit_policy::live(); }

        void commit_edit(bool edited, bool released) { apply_commit_policy(val, policy(), tracker, edited, released); }

        value<T>& val;
        std::function<commit_policy()> policy_fn;
        commit_tracker tracker;
    };



    // ##############
    // ### Traits ###
    // ##############

    template <typename T> struct scalar_traits;
    template <> struct scalar_traits<float>  { static constexpr ImGuiDataType type = ImGuiDataType_Float;  static constexpr const char* format = "%.3f"; static constexpr bool is_integer = false; };
    template <> struct scalar_traits<double> { static constexpr ImGuiDataType type = ImGuiDataType_Double; static constexpr const char* format = "%.6g"; static constexpr bool is_integer = false; };
    template <> struct scalar_traits<int>    { static constexpr ImGuiDataType type = ImGuiDataType_S32;    static constexpr const char* format = "%d";   static constexpr bool is_integer = true;  };

    constexpr float rad_to_deg = 57.29577951308232f;
    constexpr float deg_to_rad = 0.017453292519943295f;



    // ##################
    // ### Text items ###
    // ##################

    class text : public widget_t<text>
    {
    public:
        enum class tone { normal, dim, warning, error, success };

        explicit text(std::string content_) : widget_t(""), content(std::move(content_)) {}
        explicit text(std::function<std::string()> fn_) : widget_t(""), fn(std::move(fn_)) {}

        text& wrapped(bool w = true) { wrap = w; return *this; }
        text& colored(tone t) { colour = t; return *this; }

    protected:
        void draw_contents(frame& f) override
        {
            const std::string s = fn ? fn() : content;

            bool pushed = true;
            switch (colour)
            {
                case tone::dim:     ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)); break;
                case tone::warning: ImGui::PushStyleColor(ImGuiCol_Text, colors().warning); break;
                case tone::error:   ImGui::PushStyleColor(ImGuiCol_Text, colors().error); break;
                case tone::success: ImGui::PushStyleColor(ImGuiCol_Text, colors().success); break;
                default: pushed = false; break;
            }

            if (wrap)
            {
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + f.layout.width);
                ImGui::TextUnformatted(s.c_str());
                ImGui::PopTextWrapPos();
            }
            else
            {
                ImGui::TextUnformatted(s.c_str());
            }

            if (pushed) ImGui::PopStyleColor();
        }

        std::string content;
        std::function<std::string()> fn;
        bool wrap = false;
        tone colour = tone::normal;
    };

    /**
    * Read-only "label: value" line in the property grid, e.g. a derived quantity.
    */
    class readout : public widget_t<readout>
    {
    public:
        readout(std::string label_, std::function<std::string()> fn_) : widget_t(std::move(label_)), fn(std::move(fn_)) {}

    protected:
        void draw_contents(frame& f) override
        {
            labelled_field field(f, label);
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", fn().c_str());
        }

        std::function<std::string()> fn;
    };

    class separator : public widget_t<separator>
    {
    public:
        explicit separator(std::string title_ = {}) : widget_t(""), title(std::move(title_)) {}

    protected:
        void draw_contents(frame& f) override
        {
            if (title.empty())
            {
                const ImVec2 p = ImGui::GetCursorScreenPos();
                const float y = p.y + px(3.0f);
                ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x, y), ImVec2(p.x + f.layout.width, y), ImGui::GetColorU32(ImGuiCol_Separator));
                ImGui::Dummy(ImVec2(f.layout.width, px(6.0f)));
            }
            else
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                ImGui::TextUnformatted(title.c_str());
                ImGui::PopStyleColor();

                const ImVec2 mn = ImGui::GetItemRectMin();
                const ImVec2 mx = ImGui::GetItemRectMax();
                const float x0 = mx.x + ImGui::GetStyle().ItemInnerSpacing.x;
                const float x1 = mn.x + f.layout.width;
                const float y = (mn.y + mx.y) * 0.5f;
                if (x1 > x0) ImGui::GetWindowDrawList()->AddLine(ImVec2(x0, y), ImVec2(x1, y), ImGui::GetColorU32(ImGuiCol_Separator));
            }
        }

        std::string title;
    };

    class spacing : public widget_t<spacing>
    {
    public:
        explicit spacing(float design_pixels_ = 6.0f) : widget_t(""), amount(design_pixels_) {}

    protected:
        void draw_contents(frame&) override { ImGui::Dummy(ImVec2(0.0f, px(amount))); }

        float amount;
    };



    // ##############
    // ### Button ###
    // ##############

    /**
    * Every phase of a button press, delivered to the matching callback.
    */
    enum class button_phase
    {
        pressed,      // mouse went down on the button
        held,         // every frame while held (on_hold without repeat) or every repeat tick (with repeat)
        long_press,   // once, after the button has been held for `long_press_seconds`
        released,     // mouse went up (inside or outside)
        clicked       // released inside the button: the classic click
    };

    struct button_event
    {
        button_phase phase;
        float held_seconds = 0.0f;  // time since pressed
        int repeat_index = 0;       // 0 for the first repeat tick, then 1, 2, ...
        modifiers mods;
    };

    /**
    * Auto-repeat while held. The interval shrinks by `acceleration` after every tick, down to
    * `min_interval`. acceleration = 1 keeps it constant.
    */
    struct repeat_config
    {
        float delay = 0.35f;
        float interval = 0.08f;
        float acceleration = 1.0f;
        float min_interval = 0.02f;
    };

    struct repeat_state
    {
        double pressed_at = -1.0;
        double next_tick = 0.0;
        int ticks = 0;

        /**
        * Call right after the ImGui item. Returns how many repeat ticks happened this frame (0 or 1).
        */
        int update(const repeat_config& cfg)
        {
            const double now = ImGui::GetTime();

            if (ImGui::IsItemActivated())
            {
                pressed_at = now;
                next_tick = now + cfg.delay;
                ticks = 0;
            }

            if (!ImGui::IsItemActive() || pressed_at < 0.0) return 0;
            if (now < next_tick) return 0;

            const double interval = std::max<double>(cfg.min_interval, cfg.interval * std::pow(cfg.acceleration, static_cast<float>(ticks)));
            next_tick = now + interval;
            ticks++;
            return 1;
        }

        float held_seconds() const { return pressed_at < 0.0 ? 0.0f : static_cast<float>(ImGui::GetTime() - pressed_at); }
    };

    class button : public widget_t<button>
    {
    public:
        using callback = std::function<void(const button_event&)>;

        enum class behavior
        {
            push,       // a normal button
            toggle,     // flips a bound bool on every click and shows its state
            momentary   // bound bool is true while the button is held down, false otherwise
        };

        explicit button(std::string label_) : widget_t(std::move(label_)) {}

        // Callbacks for each phase. They may all be combined.
        button& on_click(callback fn) { cb_click = std::move(fn); return *this; }
        button& on_press(callback fn) { cb_press = std::move(fn); return *this; }
        button& on_release(callback fn) { cb_release = std::move(fn); return *this; }

        /**
        * Called every frame while the button is held, or, with a repeat_config, after the initial delay at
        * the repeat interval (like a held keyboard key).
        */
        button& on_hold(callback fn) { cb_hold = std::move(fn); repeat.reset(); return *this; }
        button& on_hold(callback fn, repeat_config cfg) { cb_hold = std::move(fn); repeat = cfg; return *this; }

        /**
        * Fires once when held for `seconds`. With suppress_click the click after a long press is swallowed,
        * so one button can do two different things.
        */
        button& on_long_press(callback fn, float seconds = 0.6f, bool suppress_click = true)
        {
            cb_long = std::move(fn);
            long_press_seconds = seconds;
            long_press_suppresses_click = suppress_click;
            return *this;
        }

        /**
        * Toggle / momentary mode bound to a bool value. The value's actions run like any other change.
        */
        button& bind(value<bool>& v, behavior b)
        {
            state = &v;
            mode = b;
            return *this;
        }

        button& compact(bool c = true) { fill_width = !c; return *this; }
        button& height(float design_pixels) { fixed_height = design_pixels; return *this; }

        /**
        * Accent colouring for primary actions.
        */
        button& primary(bool p = true) { is_primary = p; return *this; }

    protected:
        void draw_contents(frame& f) override
        {
            const bool on = state && state->get();
            const bool highlight = is_primary || (mode != behavior::push && on);

            if (highlight)
            {
                const theme& t = colors();
                ImGui::PushStyleColor(ImGuiCol_Button, t.accent_muted);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, t.accent);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, t.accent_active);
            }

            const ImVec2 size(fill_width ? f.layout.width : 0.0f, fixed_height > 0.0f ? px(fixed_height) : 0.0f);
            const std::string id = label + "##btn";
            const bool clicked = ImGui::Button(id.c_str(), size);

            if (highlight) ImGui::PopStyleColor(3);

            const bool activated = ImGui::IsItemActivated();
            const bool active = ImGui::IsItemActive();
            const bool deactivated = ImGui::IsItemDeactivated();

            const int repeat_ticks = rep.update(repeat ? *repeat : repeat_config{});
            const float held = rep.held_seconds();
            const modifiers mods = modifiers::current();

            if (activated)
            {
                long_fired = false;
                if (cb_press) cb_press({ button_phase::pressed, 0.0f, 0, mods });
                if (state && mode == behavior::momentary) state->set(true, change_source::user);
            }

            if (active)
            {
                if (cb_hold)
                {
                    if (!repeat) cb_hold({ button_phase::held, held, 0, mods });
                    else if (repeat_ticks > 0) cb_hold({ button_phase::held, held, rep.ticks - 1, mods });
                }

                if (cb_long && !long_fired && held >= long_press_seconds)
                {
                    long_fired = true;
                    cb_long({ button_phase::long_press, held, 0, mods });
                }
            }

            if (deactivated)
            {
                if (cb_release) cb_release({ button_phase::released, held, 0, mods });
                if (state && mode == behavior::momentary) state->set(false, change_source::user);
            }

            if (clicked && !(long_fired && long_press_suppresses_click))
            {
                if (state && mode == behavior::toggle) state->set(!state->get(), change_source::user);
                if (cb_click) cb_click({ button_phase::clicked, held, 0, mods });
            }

            if (deactivated) rep.pressed_at = -1.0;
        }

        callback cb_click, cb_press, cb_release, cb_hold, cb_long;
        std::optional<repeat_config> repeat;
        repeat_state rep;

        float long_press_seconds = 0.6f;
        bool long_press_suppresses_click = true;
        bool long_fired = false;

        value<bool>* state = nullptr;
        behavior mode = behavior::push;

        bool fill_width = true;
        float fixed_height = 0.0f;
        bool is_primary = false;
    };



    // ################
    // ### Checkbox ###
    // ################

    class checkbox : public bound_widget<checkbox, bool>
    {
    public:
        using bound_widget::bound_widget;

        /**
        * Draw the label to the right of the box (classic ImGui style) instead of in the label column.
        */
        checkbox& text_right(bool r = true) { right = r; return *this; }

    protected:
        void draw_contents(frame& f) override
        {
            bool tmp = val.get();
            bool clicked;
            if (right)
            {
                clicked = ImGui::Checkbox((label + "##cb").c_str(), &tmp);
            }
            else
            {
                labelled_field field(f, label);
                clicked = ImGui::Checkbox("##cb", &tmp);
            }
            if (clicked) val.set(tmp, change_source::user);
        }

        bool right = false;
    };



    // ##############
    // ### Slider ###
    // ##############

    template <typename T>
    class slider : public bound_widget<slider<T>, T>
    {
        using base = bound_widget<slider<T>, T>;

    public:
        slider(std::string label_, value<T>& v, T min_, T max_) : base(std::move(label_), v), min_value(min_), max_value(max_) {}

        slider& format(std::string fmt_) { fmt = std::move(fmt_); return *this; }
        slider& logarithmic(bool l = true) { set_flag(ImGuiSliderFlags_Logarithmic, l); return *this; }
        slider& clamp(bool c = true) { set_flag(ImGuiSliderFlags_AlwaysClamp, c); return *this; }

        /**
        * The value is stored in radians, shown and edited in degrees. min / max are in radians.
        */
        slider& angle(bool a = true) { as_angle = a; if (fmt.empty()) fmt = "%.1f deg"; return *this; }

    protected:
        void draw_contents(frame& f) override
        {
            labelled_field field(f, this->label);

            T tmp = this->val.get();
            T lo = min_value, hi = max_value;

            if constexpr (!scalar_traits<T>::is_integer)
            {
                if (as_angle) { tmp *= rad_to_deg; lo *= rad_to_deg; hi *= rad_to_deg; }
            }

            const char* f_str = fmt.empty() ? scalar_traits<T>::format : fmt.c_str();
            const bool edited = ImGui::SliderScalar("##slider", scalar_traits<T>::type, &tmp, &lo, &hi, f_str, flags);

            if constexpr (!scalar_traits<T>::is_integer)
            {
                if (as_angle) tmp *= deg_to_rad;
            }

            if (edited) this->val.edit(tmp);
            this->commit_edit(edited, ImGui::IsItemDeactivated());
        }

        void set_flag(ImGuiSliderFlags flag, bool on) { flags = on ? (flags | flag) : (flags & ~flag); }

        T min_value, max_value;
        std::string fmt;
        ImGuiSliderFlags flags = ImGuiSliderFlags_None;
        bool as_angle = false;
    };



    // ############
    // ### Drag ###
    // ############

    /**
    * Drag field for one scalar (float, double, int) or an array of them (vec2 / vec3 / vec4 / ivec2).
    * Ctrl + click or double click to type a value.
    */
    template <typename T>
    class drag : public bound_widget<drag<T>, T>
    {
        using base = bound_widget<drag<T>, T>;

        static constexpr bool is_vector = !std::is_arithmetic_v<T>;

        template <typename U> struct element { using type = U; static constexpr int count = 1; };
        template <typename E, size_t N> struct element<std::array<E, N>> { using type = E; static constexpr int count = static_cast<int>(N); };

        using elem_t = typename element<T>::type;
        static constexpr int components = element<T>::count;

    public:
        drag(std::string label_, value<T>& v, float speed_ = 0.01f) : base(std::move(label_), v), speed(speed_) {}

        drag& range(elem_t lo, elem_t hi) { min_value = lo; max_value = hi; has_range = true; return *this; }
        drag& speed_per_pixel(float s) { speed = s; return *this; }
        drag& format(std::string fmt_) { fmt = std::move(fmt_); return *this; }
        /**
        * Logarithmic response, for values spanning many decades (masses, ...). Needs a range(). ImGui divides
        * the drag speed by the range in this mode, so the speed is derived from the range instead: one full
        * sweep of the range takes `sweep_pixels` of mouse movement.
        */
        drag& logarithmic(bool l = true, float sweep_pixels = 2000.0f)
        {
            flags = l ? (flags | ImGuiSliderFlags_Logarithmic) : (flags & ~ImGuiSliderFlags_Logarithmic);
            log_sweep_pixels = sweep_pixels;
            return *this;
        }

        /**
        * Stored in radians, shown and edited in degrees (float based types only).
        */
        drag& angle(bool a = true) { as_angle = a; if (fmt.empty()) fmt = "%.1f deg"; return *this; }

    protected:
        void draw_contents(frame& f) override
        {
            labelled_field field(f, this->label);

            T tmp = this->val.get();
            elem_t* data = nullptr;
            if constexpr (is_vector) data = tmp.data();
            else data = &tmp;

            float spd = speed;
            elem_t lo = min_value, hi = max_value;
            if ((flags & ImGuiSliderFlags_Logarithmic) && has_range)
            {
                spd = static_cast<float>((static_cast<double>(hi) - static_cast<double>(lo)) / std::max(1.0, static_cast<double>(px(log_sweep_pixels))));
            }

            if constexpr (!std::is_integral_v<elem_t>)
            {
                if (as_angle)
                {
                    for (int i = 0; i < components; i++) data[i] *= rad_to_deg;
                    lo *= rad_to_deg; hi *= rad_to_deg; spd *= rad_to_deg;
                }
            }

            const char* f_str = fmt.empty() ? scalar_traits<elem_t>::format : fmt.c_str();
            const bool edited = ImGui::DragScalarN("##drag", scalar_traits<elem_t>::type, data, components, spd,
                                                   has_range ? &lo : nullptr, has_range ? &hi : nullptr, f_str,
                                                   flags | (has_range ? ImGuiSliderFlags_AlwaysClamp : 0));

            if constexpr (!std::is_integral_v<elem_t>)
            {
                if (as_angle) for (int i = 0; i < components; i++) data[i] *= deg_to_rad;
            }

            if (edited) this->val.edit(tmp);
            this->commit_edit(edited, ImGui::IsItemDeactivated());
        }

        float speed;
        float log_sweep_pixels = 2000.0f;
        elem_t min_value{}, max_value{};
        bool has_range = false;
        std::string fmt;
        ImGuiSliderFlags flags = ImGuiSliderFlags_None;
        bool as_angle = false;
    };



    // ###############
    // ### Spinner ###
    // ###############

    /**
    * Numeric spin box: [ - ][ value ][ + ].
    *   - the buttons auto-repeat (and accelerate) while held
    *   - Shift = 10x step, Ctrl = 0.1x step (floating point only)
    *   - the field can be typed into; it commits on enter or when focus leaves
    *   - mouse wheel over the field steps the value
    */
    template <typename T>
    class spinner : public bound_widget<spinner<T>, T>
    {
        using base = bound_widget<spinner<T>, T>;

    public:
        spinner(std::string label_, value<T>& v, T step_) : base(std::move(label_), v), step(step_) {}

        spinner& range(T lo, T hi) { min_value = lo; max_value = hi; has_range = true; return *this; }
        spinner& format(std::string fmt_) { fmt = std::move(fmt_); return *this; }
        spinner& wrap_around(bool w = true) { wraps = w; return *this; }
        spinner& repeat(repeat_config cfg) { rep_cfg = cfg; return *this; }

    protected:
        T clamped(T v) const
        {
            if (!has_range) return v;
            if (wraps)
            {
                if (v > max_value) return min_value;
                if (v < min_value) return max_value;
                return v;
            }
            return std::clamp(v, min_value, max_value);
        }

        T step_amount() const
        {
            const modifiers m = modifiers::current();
            double s = static_cast<double>(step);
            if (m.shift) s *= 10.0;
            if constexpr (!scalar_traits<T>::is_integer) { if (m.ctrl) s *= 0.1; }
            return static_cast<T>(s);
        }

        void draw_contents(frame& f) override
        {
            labelled_field field(f, this->label);

            const ImGuiStyle& style = ImGui::GetStyle();
            const float h = ImGui::GetFrameHeight();
            const float input_w = std::max(px(20.0f), field.width - 2.0f * h - 2.0f * style.ItemInnerSpacing.x);

            T tmp = this->val.get();
            bool edited = false;
            bool released = false;

            // [ - ]
            ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
            ImGui::Button("-##dec", ImVec2(h, h));
            int ticks = ImGui::IsItemActivated() ? 1 : 0;
            ticks += dec_rep.update(rep_cfg);
            if (ticks) { tmp = clamped(static_cast<T>(tmp - step_amount() * ticks)); edited = true; }
            released |= ImGui::IsItemDeactivated();
            ImGui::PopItemFlag();

            // [ value ]
            ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
            ImGui::SetNextItemWidth(input_w);
            const char* f_str = fmt.empty() ? scalar_traits<T>::format : fmt.c_str();
            T typed = tmp;
            if (ImGui::InputScalar("##field", scalar_traits<T>::type, &typed, nullptr, nullptr, f_str))
            {
                typed_pending = clamped(typed);
            }
            if (ImGui::IsItemDeactivated() && typed_pending)
            {
                tmp = *typed_pending;
                typed_pending.reset();
                edited = true;
                released = true;
            }
            if (ImGui::IsItemHovered() && !ImGui::IsItemActive())
            {
                ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);   // step the value instead of scrolling the panel
                const float wheel = ImGui::GetIO().MouseWheel;
                if (wheel != 0.0f)
                {
                    tmp = clamped(static_cast<T>(tmp + step_amount() * (wheel > 0.0f ? 1 : -1)));
                    edited = true;
                    released = true;
                }
            }

            // [ + ]
            ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
            ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
            ImGui::Button("+##inc", ImVec2(h, h));
            ticks = ImGui::IsItemActivated() ? 1 : 0;
            ticks += inc_rep.update(rep_cfg);
            if (ticks) { tmp = clamped(static_cast<T>(tmp + step_amount() * ticks)); edited = true; }
            released |= ImGui::IsItemDeactivated();
            ImGui::PopItemFlag();

            if (edited) this->val.edit(tmp);
            this->commit_edit(edited, released);
        }

        T step;
        T min_value{}, max_value{};
        bool has_range = false;
        bool wraps = false;
        std::string fmt;
        repeat_config rep_cfg{ 0.35f, 0.10f, 0.85f, 0.016f };
        repeat_state dec_rep, inc_rep;
        std::optional<T> typed_pending;
    };



    // ##################
    // ### Selections ###
    // ##################

    /**
    * Items of a dropdown / list / segmented control. Either fixed, or a provider called every frame
    * (for lists that change at runtime, e.g. AOV names).
    */
    struct item_source
    {
        item_source() = default;
        item_source(std::initializer_list<std::string> items) : fixed(items) {}
        item_source(std::vector<std::string> items) : fixed(std::move(items)) {}
        template <typename F, typename = std::enable_if_t<std::is_invocable_r_v<std::vector<std::string>, F&>>>
        item_source(F fn) : provider(std::move(fn)) {}

        std::vector<std::string> fixed;
        std::function<std::vector<std::string>()> provider;

        std::vector<std::string> get() const { return provider ? provider() : fixed; }
    };

    /**
    * Combo box bound to an int index.
    */
    class dropdown : public bound_widget<dropdown, int>
    {
    public:
        dropdown(std::string label_, value<int>& v, item_source items_) : bound_widget(std::move(label_), v), items(std::move(items_)) {}

        /**
        * Adds a filter text field to the popup once the list has at least `threshold` entries.
        */
        dropdown& searchable(int threshold = 10) { search_threshold = threshold; return *this; }

    protected:
        void draw_contents(frame& f) override
        {
            labelled_field field(f, label);
            const std::vector<std::string> list = items.get();
            const int current = val.get();
            const char* preview = (current >= 0 && current < static_cast<int>(list.size())) ? list[current].c_str() : "-";

            if (ImGui::BeginCombo("##combo", preview, ImGuiComboFlags_HeightLarge))
            {
                const bool show_filter = search_threshold > 0 && static_cast<int>(list.size()) >= search_threshold;
                if (show_filter)
                {
                    if (ImGui::IsWindowAppearing()) { filter[0] = '\0'; ImGui::SetKeyboardFocusHere(); }
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    ImGui::InputTextWithHint("##filter", "Filter...", filter, sizeof(filter));
                }

                for (int i = 0; i < static_cast<int>(list.size()); i++)
                {
                    if (show_filter && filter[0] && !contains_case_insensitive(list[i], filter)) continue;

                    const bool selected = (i == current);
                    if (ImGui::Selectable(list[i].c_str(), selected)) val.set(i, change_source::user);
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }

        static bool contains_case_insensitive(const std::string& hay, const char* needle)
        {
            auto lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
            std::string h, n(needle);
            for (char c : hay) h += lower(c);
            for (char& c : n) c = lower(c);
            return h.find(n) != std::string::npos;
        }

        item_source items;
        int search_threshold = 0;
        char filter[128] = {};
    };

    /**
    * Combo box bound to the selected text itself (useful when the item list is dynamic, so an index
    * would point to a different item after the list changes).
    */
    class dropdown_text : public bound_widget<dropdown_text, std::string>
    {
    public:
        dropdown_text(std::string label_, value<std::string>& v, item_source items_) : bound_widget(std::move(label_), v), items(std::move(items_)) {}

    protected:
        void draw_contents(frame& f) override
        {
            labelled_field field(f, label);
            const std::vector<std::string> list = items.get();
            const std::string& current = val.get();

            if (ImGui::BeginCombo("##combo", current.empty() ? "-" : current.c_str()))
            {
                for (const std::string& item : list)
                {
                    const bool selected = (item == current);
                    if (ImGui::Selectable(item.c_str(), selected)) val.set(item, change_source::user);
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }

        item_source items;
    };

    /**
    * Always visible list box bound to an int index.
    */
    class list_box : public bound_widget<list_box, int>
    {
    public:
        list_box(std::string label_, value<int>& v, item_source items_, int visible_rows_ = 5) : bound_widget(std::move(label_), v), items(std::move(items_)), rows(visible_rows_) {}

    protected:
        void draw_contents(frame& f) override
        {
            labelled_field field(f, label);
            const std::vector<std::string> list = items.get();
            const int shown = std::max(1, std::min(rows, static_cast<int>(list.size())));
            const float height = ImGui::GetTextLineHeightWithSpacing() * static_cast<float>(shown) + ImGui::GetStyle().FramePadding.y * 2.0f;

            if (ImGui::BeginListBox("##list", ImVec2(field.width, height)))
            {
                for (int i = 0; i < static_cast<int>(list.size()); i++)
                {
                    const bool selected = (i == val.get());
                    if (ImGui::Selectable(list[i].c_str(), selected)) val.set(i, change_source::user);
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndListBox();
            }
        }

        item_source items;
        int rows;
    };

    /**
    * Row of equally sized toggle buttons, one of which is selected. Good for small enums
    * (metric type, quality presets...).
    */
    class segmented : public bound_widget<segmented, int>
    {
    public:
        segmented(std::string label_, value<int>& v, item_source items_) : bound_widget(std::move(label_), v), items(std::move(items_)) {}

    protected:
        void draw_contents(frame& f) override
        {
            labelled_field field(f, label);
            const std::vector<std::string> list = items.get();
            if (list.empty()) { ImGui::NewLine(); return; }

            const float spacing = px(2.0f);
            const float w = (field.width - spacing * static_cast<float>(list.size() - 1)) / static_cast<float>(list.size());
            const theme& t = colors();

            for (int i = 0; i < static_cast<int>(list.size()); i++)
            {
                if (i) ImGui::SameLine(0.0f, spacing);

                const bool selected = (i == val.get());
                if (selected)
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, t.accent_muted);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, t.accent);
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, t.accent_active);
                }

                ImGui::PushID(i);
                if (ImGui::Button(list[i].c_str(), ImVec2(w, 0.0f)) && !selected) val.set(i, change_source::user);
                ImGui::PopID();

                if (selected) ImGui::PopStyleColor(3);
            }
        }

        item_source items;
    };



    // #############
    // ### Color ###
    // #############

    template <typename T>   // vec3 (RGB) or vec4 (RGBA)
    class color_edit : public bound_widget<color_edit<T>, T>
    {
        using base = bound_widget<color_edit<T>, T>;
        static_assert(std::is_same_v<T, vec3> || std::is_same_v<T, vec4>, "color_edit needs mgui::vec3 or mgui::vec4");

    public:
        color_edit(std::string label_, value<T>& v) : base(std::move(label_), v) {}

        color_edit& flags(ImGuiColorEditFlags fl) { color_flags = fl; return *this; }
        color_edit& hdr(bool h = true) { color_flags = h ? (color_flags | ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float) : (color_flags & ~(ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float)); return *this; }

    protected:
        void draw_contents(frame& f) override
        {
            labelled_field field(f, this->label);
            T tmp = this->val.get();

            bool edited;
            if constexpr (std::is_same_v<T, vec3>) edited = ImGui::ColorEdit3("##color", tmp.data(), color_flags);
            else                                   edited = ImGui::ColorEdit4("##color", tmp.data(), color_flags);

            if (edited) this->val.edit(tmp);

            // The picker popup is a separate window: "released" when neither the fields nor the popup are active.
            // ColorEdit opens its popup as "picker" inside the "##color" ID scope.
            ImGui::PushID("##color");
            const bool picker_open = ImGui::IsPopupOpen("picker");
            ImGui::PopID();
            const bool active_now = ImGui::IsItemActive() || picker_open;
            const bool released = was_active && !active_now;
            was_active = active_now;

            this->commit_edit(edited, released || ImGui::IsItemDeactivatedAfterEdit());
        }

        ImGuiColorEditFlags color_flags = ImGuiColorEditFlags_PickerHueWheel;
        bool was_active = false;
    };



    // ############
    // ### Text ###
    // ############

    namespace detail
    {
        inline int string_resize_callback(ImGuiInputTextCallbackData* data)
        {
            if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
            {
                auto* str = static_cast<std::string*>(data->UserData);
                str->resize(static_cast<size_t>(data->BufTextLen));
                data->Buf = str->data();
            }
            return 0;
        }

        inline bool input_string(const char* id, std::string& s, const char* hint, ImGuiInputTextFlags flags = 0)
        {
            flags |= ImGuiInputTextFlags_CallbackResize;
            if (s.capacity() == 0) s.reserve(16);
            return hint ? ImGui::InputTextWithHint(id, hint, s.data(), s.capacity() + 1, flags, string_resize_callback, &s)
                        : ImGui::InputText(id, s.data(), s.capacity() + 1, flags, string_resize_callback, &s);
        }
    }

    /**
    * Single line text field bound to a std::string. Commits on enter or focus loss by default.
    */
    class text_input : public bound_widget<text_input, std::string>
    {
    public:
        text_input(std::string label_, value<std::string>& v) : bound_widget(std::move(label_), v) { commit(commit_policy::on_release()); }

        text_input& hint(std::string h) { hint_text = std::move(h); return *this; }

    protected:
        void draw_contents(frame& f) override
        {
            labelled_field field(f, label);
            if (!editing) buffer = val.get();

            const bool edited = detail::input_string("##text", buffer, hint_text.empty() ? nullptr : hint_text.c_str());
            editing = ImGui::IsItemActive();

            if (edited) val.edit(buffer);
            commit_edit(edited, ImGui::IsItemDeactivated());
        }

        std::string buffer;
        std::string hint_text;
        bool editing = false;
    };



    // ###############
    // ### Display ###
    // ###############

    /**
    * Line plot of a history buffer, e.g. frame times.
    */
    class plot : public widget_t<plot>
    {
    public:
        plot(std::string label_, std::function<const std::vector<float>&()> data_, float height_ = 50.0f)
            : widget_t(std::move(label_)), data(std::move(data_)), height(height_) {}

        plot& range(float lo, float hi) { min_value = lo; max_value = hi; return *this; }
        plot& overlay(std::function<std::string()> fn) { overlay_fn = std::move(fn); return *this; }
        plot& histogram(bool h = true) { as_histogram = h; return *this; }

    protected:
        void draw_contents(frame& f) override
        {
            labelled_field field(f, label);
            const std::vector<float>& d = data();
            const std::string ov = overlay_fn ? overlay_fn() : std::string();
            const ImVec2 size(field.width, px(height));
            const int n = static_cast<int>(d.size());

            if (as_histogram) ImGui::PlotHistogram("##plot", d.data(), n, 0, ov.c_str(), min_value, max_value, size);
            else              ImGui::PlotLines("##plot", d.data(), n, 0, ov.c_str(), min_value, max_value, size);
        }

        std::function<const std::vector<float>&()> data;
        float height;
        float min_value = FLT_MAX, max_value = FLT_MAX;
        std::function<std::string()> overlay_fn;
        bool as_histogram = false;
    };

    /**
    * Escape hatch: arbitrary ImGui code as a widget. `f.layout.width` is the width available.
    */
    class custom : public widget_t<custom>
    {
    public:
        explicit custom(std::function<void(frame&)> fn_, std::string label_ = {}) : widget_t(std::move(label_)), fn(std::move(fn_)) {}

    protected:
        void draw_contents(frame& f) override { if (fn) fn(f); }

        std::function<void(frame&)> fn;
    };
}

#endif
