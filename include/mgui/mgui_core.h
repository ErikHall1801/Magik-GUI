#ifndef MGUI_CORE_H
#define MGUI_CORE_H

/*
    mgui core
    =========

    The persistent part of the GUI. Everything the user can change lives in a `value_store` as a typed
    `value<T>` under a string key, e.g. "camera.position". Widgets are only *views* onto those values.

        * A value remembers its last committed state. When it changes, every action registered on it is
          called with a `change<T>` that carries the old value, the new value, where the change came from
          (user, program, init, load) and the keyboard modifiers held at that moment.
        * Several widgets may bind to the same key (e.g. a slider in the settings panel and one in the
          viewport context menu). They stay in sync automatically and share the same actions.
        * Widgets decide *when* a user edit is committed through a `commit_policy`
          (live, throttled, on_release). Until then the value is "pending": the widget shows the new
          number, but actions are not called. This matters for Magik, where every camera / metric command
          clears the render buffer.
        * Values flagged `persist` (the default) are saved to / loaded from a small text file.
*/

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeindex>
#include <unordered_map>
#include <vector>

#include "imgui.h"

namespace mgui
{
    using vec2 = std::array<float, 2>;
    using vec3 = std::array<float, 3>;
    using vec4 = std::array<float, 4>;
    using ivec2 = std::array<int, 2>;

    class value_store;



    // ###########################
    // ### Scale (DPI & zoom) ###
    // ###########################

    /**
    * Global UI scale. `dpi` comes from the monitor, `zoom` is the user preference (View > UI scale,
    * Ctrl +/-/0). Every hard coded size in mgui is written in unscaled "design pixels" and passed
    * through px() so the whole GUI scales together.
    */
    struct scale_state
    {
        float dpi = 1.0f;
        float zoom = 1.0f;

        float factor() const { return dpi * zoom; }
    };

    inline scale_state& scale() { static scale_state s; return s; }

    inline float px(float design_pixels) { return design_pixels * scale().factor(); }
    inline ImVec2 px(float x, float y) { return ImVec2(px(x), px(y)); }



    // #################
    // ### Modifiers ###
    // #################

    struct modifiers
    {
        bool ctrl = false;
        bool shift = false;
        bool alt = false;
        bool super = false;

        static modifiers current()
        {
            if (!ImGui::GetCurrentContext()) return {};
            const ImGuiIO& io = ImGui::GetIO();
            return modifiers{ io.KeyCtrl, io.KeyShift, io.KeyAlt, io.KeySuper };
        }

        bool none() const { return !ctrl && !shift && !alt && !super; }
    };



    // ###############
    // ### Changes ###
    // ###############

    enum class change_source
    {
        init,      // value_store::sync_all() at start-up, actions see old == new
        user,      // a widget committed an edit
        program,   // code called value<T>::set()
        load       // settings file was loaded at runtime
    };

    inline const char* to_string(change_source s)
    {
        switch (s)
        {
            case change_source::init:    return "init";
            case change_source::user:    return "user";
            case change_source::program: return "program";
            case change_source::load:    return "load";
        }
        return "?";
    }

    /**
    * What an action receives. References are valid for the duration of the call only.
    */
    template <typename T>
    struct change
    {
        const T& old_value;
        const T& new_value;
        change_source source;
        modifiers mods;
        std::string_view key;
        value_store& store;

        bool is_init() const { return source == change_source::init; }
    };



    // #####################
    // ### Serialization ###
    // #####################

    namespace serialization
    {
        inline std::string to_text(bool v) { return v ? "true" : "false"; }
        inline std::string to_text(int v) { return std::to_string(v); }
        inline std::string to_text(const std::string& v) { return v; }

        inline std::string to_text(double v)
        {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.17g", v);
            return buf;
        }

        inline std::string to_text(float v)
        {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.9g", v);
            return buf;
        }

        template <typename T, size_t N>
        std::string to_text(const std::array<T, N>& v)
        {
            std::string out;
            for (size_t i = 0; i < N; i++)
            {
                if (i) out += ' ';
                out += to_text(v[i]);
            }
            return out;
        }

        inline bool from_text(std::string_view s, bool& out)
        {
            if (s == "true" || s == "1") { out = true; return true; }
            if (s == "false" || s == "0") { out = false; return true; }
            return false;
        }

        inline bool from_text(std::string_view s, int& out)
        {
            std::string tmp(s);
            char* end = nullptr;
            long v = std::strtol(tmp.c_str(), &end, 10);
            if (end == tmp.c_str()) return false;
            out = static_cast<int>(v);
            return true;
        }

        inline bool from_text(std::string_view s, double& out)
        {
            std::string tmp(s);
            char* end = nullptr;
            double v = std::strtod(tmp.c_str(), &end);
            if (end == tmp.c_str()) return false;
            out = v;
            return true;
        }

        inline bool from_text(std::string_view s, float& out)
        {
            double d;
            if (!from_text(s, d)) return false;
            out = static_cast<float>(d);
            return true;
        }

        inline bool from_text(std::string_view s, std::string& out) { out = std::string(s); return true; }

        template <typename T, size_t N>
        bool from_text(std::string_view s, std::array<T, N>& out)
        {
            std::istringstream stream{ std::string(s) };
            std::array<T, N> tmp{};
            for (size_t i = 0; i < N; i++)
            {
                if (!(stream >> tmp[i])) return false;
            }
            out = tmp;
            return true;
        }
    }



    // ##############
    // ### Values ###
    // ##############

    class value_base
    {
    public:
        explicit value_base(std::string key_) : key(std::move(key_)) {}
        virtual ~value_base() = default;

        virtual std::type_index type() const = 0;
        virtual std::string to_text() const = 0;
        virtual bool from_text(std::string_view text) = 0;   // sets current AND committed, no actions
        virtual void commit(change_source source) = 0;       // dispatch if pending
        virtual void sync(change_source source) = 0;         // dispatch unconditionally (old == new)
        virtual void reset(change_source source) = 0;        // back to default value
        virtual bool pending() const = 0;
        virtual void accept_silently() = 0;                  // committed = current, no actions

        std::string key;
        bool persist = true;
    };

    template <typename T>
    class value final : public value_base
    {
    public:
        using action_fn = std::function<void(const change<T>&)>;

        value(value_store& store_, std::string key_, T default_value_)
            : value_base(std::move(key_)), store(store_), current(default_value_), committed(default_value_), default_value(std::move(default_value_)) {}

        /**
        * Registers an action. It is called with (old, new) whenever the committed value changes.
        * Several actions may be attached to one value; they run in registration order.
        */
        value& on_change(action_fn fn) { actions.push_back(std::move(fn)); return *this; }

        /**
        * Optional sanitizer applied to every edit and set, e.g. clamping a velocity below c.
        */
        value& sanitize_with(std::function<T(const T&)> fn) { sanitizer = std::move(fn); current = sanitizer(current); committed = current; return *this; }

        value& set_persist(bool p) { persist = p; return *this; }

        // What widgets display. May differ from committed() while an edit is pending.
        const T& get() const { return current; }

        // What the actions last saw.
        const T& last_committed() const { return committed; }

        const T& default_val() const { return default_value; }

        /**
        * Widget-side edit: updates the displayed value but does not call actions. Call commit() later.
        */
        void edit(const T& v) { current = sanitizer ? sanitizer(v) : v; }

        /**
        * Program-side change: updates and immediately commits.
        */
        void set(const T& v, change_source source = change_source::program)
        {
            edit(v);
            commit(source);
        }

        void commit(change_source source) override
        {
            if (dispatching)
            {
                // An action changed its own value. Remember and dispatch once the current loop finishes.
                redispatch_source = source;
                redispatch = true;
                return;
            }

            for (int guard = 0; guard < 8; guard++)
            {
                if (current == committed) return;
                dispatch(source, /*force=*/false);
                if (!redispatch) return;
                redispatch = false;
                source = redispatch_source;
            }
        }

        void sync(change_source source) override { dispatch(source, /*force=*/true); }

        void reset(change_source source) override { edit(default_value); commit(source); }

        bool pending() const override { return !(current == committed); }

        void accept_silently() override { committed = current; }

        std::type_index type() const override { return std::type_index(typeid(T)); }

        std::string to_text() const override { return serialization::to_text(committed); }

        bool from_text(std::string_view text) override
        {
            T tmp = current;
            if (!serialization::from_text(text, tmp)) return false;
            edit(tmp);
            return true;
        }

    private:
        void dispatch(change_source source, bool force)
        {
            if (!force && current == committed) return;

            const T old_value = committed;
            committed = current;
            const T new_value = committed;

            dispatching = true;
            const change<T> c{ old_value, new_value, source, modifiers::current(), key, store };
            for (auto& fn : actions) fn(c);
            dispatching = false;
        }

        value_store& store;

        T current;
        T committed;
        T default_value;

        std::vector<action_fn> actions;
        std::function<T(const T&)> sanitizer;

        bool dispatching = false;
        bool redispatch = false;
        change_source redispatch_source = change_source::program;
    };



    // ###################
    // ### Value store ###
    // ###################

    class value_store
    {
    public:
        value_store() = default;
        value_store(const value_store&) = delete;
        value_store& operator=(const value_store&) = delete;

        /**
        * Creates the value if it does not exist yet, otherwise returns the existing one (the default is
        * then ignored). Declaring an existing key with a different type is a programming error and throws.
        */
        template <typename T>
        value<T>& declare(const std::string& key, T default_value)
        {
            auto it = values.find(key);
            if (it != values.end())
            {
                if (it->second->type() != std::type_index(typeid(T)))
                {
                    throw std::logic_error("mgui::value_store: key '" + key + "' declared with two different types");
                }
                return static_cast<value<T>&>(*it->second);
            }

            auto v = std::make_unique<value<T>>(*this, key, std::move(default_value));
            value<T>& ref = *v;
            order.push_back(v.get());
            values.emplace(key, std::move(v));
            return ref;
        }

        template <typename T>
        value<T>* find(const std::string& key)
        {
            auto it = values.find(key);
            if (it == values.end() || it->second->type() != std::type_index(typeid(T))) return nullptr;
            return static_cast<value<T>*>(it->second.get());
        }

        template <typename T>
        value<T>& at(const std::string& key)
        {
            value<T>* v = find<T>(key);
            if (!v) throw std::out_of_range("mgui::value_store: unknown key or wrong type '" + key + "'");
            return *v;
        }

        /**
        * Read the displayed value of another key, e.g. from inside an action.
        */
        template <typename T>
        const T& get(const std::string& key) { return at<T>(key).get(); }

        template <typename T>
        void set(const std::string& key, const T& v, change_source source = change_source::program) { at<T>(key).set(v, source); }

        bool contains(const std::string& key) const { return values.count(key) != 0; }

        /**
        * Calls every action once with old == new. Use once after the UI is configured so the renderer
        * receives the GUI's initial state.
        */
        void sync_all(change_source source = change_source::init)
        {
            for (value_base* v : order) v->sync(source);
        }

        /**
        * Resets every persistent value to its default. Runtime-only values (persist == false) are left alone.
        */
        void reset_all(change_source source = change_source::program)
        {
            for (value_base* v : order) if (v->persist) v->reset(source);
        }

        /**
        * Text format, one "key = value" per line. Lines starting with '#' are comments.
        */
        bool save(const std::string& path) const
        {
            std::ofstream file(path);
            if (!file) return false;

            file << "# Magik GUI settings\n";
            for (const value_base* v : order)
            {
                if (!v->persist) continue;
                file << v->key << " = " << v->to_text() << "\n";
            }
            return static_cast<bool>(file);
        }

        /**
        * Loads values from a file. With dispatch == false the values are applied silently (use before
        * sync_all at start-up). With dispatch == true actions run for every value that changed.
        * Returns the number of values applied, or -1 if the file could not be opened.
        */
        int load(const std::string& path, bool dispatch, change_source source = change_source::load)
        {
            std::ifstream file(path);
            if (!file) return -1;

            int applied = 0;
            std::string line;
            while (std::getline(file, line))
            {
                if (line.empty() || line[0] == '#') continue;

                const size_t eq = line.find(" = ");
                if (eq == std::string::npos) continue;

                const std::string key = line.substr(0, eq);
                const std::string text = line.substr(eq + 3);

                auto it = values.find(key);
                if (it == values.end() || !it->second->persist) continue;
                if (!it->second->from_text(text)) continue;

                if (dispatch) it->second->commit(source);
                else          it->second->accept_silently();
                applied++;
            }
            return applied;
        }

        const std::vector<value_base*>& all() const { return order; }

    private:
        std::unordered_map<std::string, std::unique_ptr<value_base>> values;
        std::vector<value_base*> order;
    };



    // #####################
    // ### Commit policy ###
    // #####################

    /**
    * When a continuous widget (slider, drag, spinner, text field) hands a user edit to its value.
    *
    *   live()          every frame the widget changes the value
    *   throttle(ms)    at most once per interval while dragging, plus once on release so the final value
    *                   always arrives
    *   on_release()    once, when the user lets go / presses enter. old_value is the value before the drag.
    *
    * Discrete widgets (checkbox, dropdown, button) always commit immediately.
    */
    struct commit_policy
    {
        enum class kind { live, throttled, on_release };

        kind mode = kind::live;
        double interval_seconds = 0.0;

        static commit_policy live() { return { kind::live, 0.0 }; }
        static commit_policy on_release() { return { kind::on_release, 0.0 }; }
        static commit_policy throttle(double milliseconds) { return { kind::throttled, milliseconds / 1000.0 }; }
    };

    struct commit_tracker
    {
        double last_commit_time = -1.0e9;
    };

    /**
    * Shared helper for continuous widgets. `edited`: the widget changed the value this frame.
    * `released`: the widget was deactivated this frame (mouse up, enter, focus lost).
    */
    template <typename T>
    void apply_commit_policy(value<T>& v, const commit_policy& policy, commit_tracker& tracker, bool edited, bool released)
    {
        const double now = ImGui::GetTime();

        switch (policy.mode)
        {
            case commit_policy::kind::live:
            {
                if (edited || released) v.commit(change_source::user);
                break;
            }

            case commit_policy::kind::throttled:
            {
                if (released)
                {
                    v.commit(change_source::user);
                    tracker.last_commit_time = now;
                }
                else if (edited && (now - tracker.last_commit_time) >= policy.interval_seconds)
                {
                    v.commit(change_source::user);
                    tracker.last_commit_time = now;
                }
                break;
            }

            case commit_policy::kind::on_release:
            {
                if (released) v.commit(change_source::user);
                break;
            }
        }
    }
}

#endif
