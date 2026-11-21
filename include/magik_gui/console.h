#ifndef MAGIK_GUI_CONSOLE_H
#define MAGIK_GUI_CONSOLE_H

#include <cstdarg>
#include <cstdio>
#include <deque>
#include <string>

#include "imgui.h"
#include "mgui/mgui.h"

namespace magik_gui
{
    enum class log_level { info, warning, error, debug };

    /**
    * Application log shown in the Console panel. Identical consecutive messages are collapsed into one
    * line with a repeat counter, so an error raised every frame does not flood the panel.
    */
    class console
    {
    public:
        void log(log_level level, const char* fmt, ...) IM_FMTARGS(3)
        {
            char buf[1024];
            va_list args;
            va_start(args, fmt);
            std::vsnprintf(buf, sizeof(buf), fmt, args);
            va_end(args);
            add(level, buf);
        }

        void add(log_level level, std::string text)
        {
            while (!text.empty() && (text.back() == '\n' || text.back() == ' ')) text.pop_back();

            if (!lines.empty() && lines.back().level == level && lines.back().text == text)
            {
                lines.back().repeats++;
                return;
            }

            lines.push_back({ level, std::move(text), 1, ImGui::GetCurrentContext() ? ImGui::GetTime() : 0.0 });
            if (lines.size() > max_lines) lines.pop_front();
            scroll_to_bottom = true;
        }

        void clear() { lines.clear(); }

        /**
        * Draws the log into the remaining space of the current window.
        */
        void draw(bool auto_scroll)
        {
            const mgui::theme& t = mgui::colors();

            ImGui::BeginChild("##console_lines", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
            for (const line& l : lines)
            {
                ImVec4 col = ImGui::GetStyleColorVec4(ImGuiCol_Text);
                const char* tag = "";
                switch (l.level)
                {
                    case log_level::info:    break;
                    case log_level::warning: col = t.warning; tag = "[ Warning ] "; break;
                    case log_level::error:   col = t.error;   tag = "[ Error ] ";   break;
                    case log_level::debug:   col = t.text_dim; break;
                }

                ImGui::PushStyleColor(ImGuiCol_Text, col);
                if (l.repeats > 1) ImGui::Text("%s%s  (x%d)", tag, l.text.c_str(), l.repeats);
                else               ImGui::Text("%s%s", tag, l.text.c_str());
                ImGui::PopStyleColor();
            }

            if (auto_scroll && scroll_to_bottom) ImGui::SetScrollHereY(1.0f);
            scroll_to_bottom = false;
            ImGui::EndChild();
        }

        size_t max_lines = 2000;

    private:
        struct line
        {
            log_level level;
            std::string text;
            int repeats;
            double time;
        };

        std::deque<line> lines;
        bool scroll_to_bottom = false;
    };
}

#endif
