#ifndef MAGIK_GUI_INTERACTIVE_ELEMENTS_H
#define MAGIK_GUI_INTERACTIVE_ELEMENTS_H

#include <vector>
#include <variant>
#include <memory>
#include <unordered_map>
#include "imgui.h"
#include <iostream>

typedef void (*interactive_element_action_caller)(void*);

// Forward declaration for use in magik_gui_dropdown_menu and magik_gui_context_menu
struct magik_gui_staging_area;

static void magik_gui_begin_labeled(const char* label)
{
    ImGui::PushID(label);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(label);
    ImGui::PopTextWrapPos();
}

static void magik_gui_end_labeled()
{
    ImGui::PopID();
}

static float magik_gui_get_available_width(float relative_width = 1.0f)
{
    return ImGui::GetContentRegionAvail().x*relative_width;
}

enum class e_magik_gui_interactive_element_types
{
    Button,
    Checkbox,
    Slider,
    Colorpicker,
    Dropdown,
    List,
    DropdownMenu
};

struct magik_gui_button
{
    const char* label;
    int counter;

    ImVec2 size;
};

struct magik_gui_slider
{
    const char* label;
    float value;
    float min_value = 0.0f;
    float max_value = 1.0f;

    float relative_width;

    ImGuiSliderFlags flags = false;
    const char* format = "%.2f";

};

struct magik_gui_colorpicker
{
    const char* label;
    ImVec4 color;

    float relative_width = 1.0f;

    ImGuiColorEditFlags flags = ImGuiColorEditFlags_PickerHueWheel;

};

struct magik_gui_checkbox
{
    const char* label;
    bool value;
};

enum class e_magik_gui_list_type
{
    Dropdown,
    List
};

template <typename T>
struct magik_gui_base_list
{
    const char* label;
    std::vector<T> elements;
    
    const char* (*get_element_name)(const T&);
    
    e_magik_gui_list_type type;

    int selected_element_index = -1;

    float relative_width = 1.0f;
};

struct magik_gui_list
{
    std::unique_ptr<void, void(*)(void*)> data;
    void (*show_fn)(void*);

    float height = 0.0f;

    template <typename T>
    static magik_gui_list create(const char* label, std::vector<T> elements, const char* (*get_name)(const T&), e_magik_gui_list_type type, void (*show_fn)(void*))
    {
        auto* list = new magik_gui_base_list<T>{ label, std::move(elements), get_name, type };

        return magik_gui_list{
            std::unique_ptr<void, void(*)(void*)>(list, [](void* ptr) { delete (magik_gui_base_list<T>*)ptr; }),
            show_fn
        };
    }

    void show() { show_fn(data.get()); }
};

struct magik_gui_dropdown_menu
{
    const char* label;
    
    interactive_element_action_caller action;
    std::unique_ptr<magik_gui_staging_area> staging_area = std::make_unique<magik_gui_staging_area>();

    bool open = false;
};

using magik_gui_element_data = std::variant<
        magik_gui_button,
        magik_gui_checkbox,
        magik_gui_slider,
        magik_gui_colorpicker,
        magik_gui_dropdown_menu,
        magik_gui_list
    >;

struct magik_gui_interactive_element
{
    ImVec2 position;
    
    interactive_element_action_caller action;
    magik_gui_element_data data;

    void show()
    {
        if (!action) return;

        void* data_ptr = std::visit(
            [](auto& element) -> void* { return &element; },
            data
        );

        action(data_ptr);
    }
};

void magik_gui_show_generic_list(void* user_data)
{
    ((magik_gui_list*)user_data)->show();
}

struct magik_gui_staging_area
{
    // TODO: Call this position something more descriptive to dinstinguish from element position
    ImVec2 position;
    float height = 0.0f;

    const unsigned int id = -1;
    const unsigned int parent_window_id = -1;

    float vertical_offset_from_top = 30.0f;
    float vertical_offset_between_elements = 5.0f;

    bool auto_size_width = false;

    std::vector<magik_gui_interactive_element> interactive_elements;


    magik_gui_button& add_button(const char* label, interactive_element_action_caller action, ImVec2 size)
    {
        interactive_elements.push_back({magik_gui_interactive_element{
                position,
                action,
                magik_gui_button{label, 0, size}
            }
        });

        return std::get<magik_gui_button>(interactive_elements.back().data);
    }

    magik_gui_slider& add_slider(const char* label, interactive_element_action_caller action, float value, float min_value, float max_value, float width)
    {
        interactive_elements.push_back(magik_gui_interactive_element{
            position,
            action,
            magik_gui_slider{label, value, min_value, max_value, width}
        });

        return std::get<magik_gui_slider>(interactive_elements.back().data);
    }

    magik_gui_dropdown_menu& add_dropdown_menu(const char* label, interactive_element_action_caller action)
    {
        interactive_elements.push_back(magik_gui_interactive_element{
            position,
            action,
            magik_gui_dropdown_menu{label}
        });
        
        return std::get<magik_gui_dropdown_menu>(interactive_elements.back().data);
    }

    template <typename T>
    magik_gui_list& add_list(const char* label, void (*list_show_fn)(void*), std::vector<T> elements, const char* (*get_element_name)(const T&), e_magik_gui_list_type type)
    {
        interactive_elements.push_back(magik_gui_interactive_element{
            position,
            magik_gui_show_generic_list,
            magik_gui_list::create<T>(label, std::move(elements), get_element_name, type, list_show_fn)
        });
        return std::get<magik_gui_list>(interactive_elements.back().data);
    }

    magik_gui_checkbox& add_checkbox(const char* label, interactive_element_action_caller action, bool value)
    {
        interactive_elements.push_back(magik_gui_interactive_element{
            position,
            action,
            magik_gui_checkbox{label, value}
        });

        return std::get<magik_gui_checkbox>(interactive_elements.back().data);
    }

    magik_gui_colorpicker& add_colorpicker(const char* label, interactive_element_action_caller action, ImVec4 color)
    {
        interactive_elements.push_back(magik_gui_interactive_element{
            position,
            action,
            magik_gui_colorpicker{label, color}
        });

        return std::get<magik_gui_colorpicker>(interactive_elements.back().data);
    }

    void show()
    {
        ImGui::SetCursorPos(position);
        ImGui::PushID(id);

        ImGuiChildFlags child_flags = ImGuiChildFlags_None;
        if (height <= 0.0f) child_flags |= ImGuiChildFlags_AutoResizeY;
        if (auto_size_width) child_flags |= ImGuiChildFlags_AutoResizeX;

        if (ImGui::BeginChild("staging_area_child", ImVec2(0.0f, height), child_flags))
        {
            ImGui::Dummy(ImVec2(0.0f, vertical_offset_from_top));

            for (magik_gui_interactive_element& element : interactive_elements)
            {
                element.show();
                ImGui::Dummy(ImVec2(0.0f, vertical_offset_between_elements));
            }
        }
        ImGui::EndChild();

        ImGui::PopID();
    }
};

struct magik_gui_staging_area_manager
{
    std::unordered_map<unsigned int, std::vector<magik_gui_staging_area>> staging_areas;

    void add_staging_areas(const unsigned int window_id, int n_staging_areas, std::vector<float> area_heights)
    {
        std::vector<magik_gui_staging_area>& areas = staging_areas[window_id];

        float cumulative_height = 0.0f;

        for (int i = 0; i < n_staging_areas; ++i)
        {
            unsigned int id = areas.size();

            areas.push_back(magik_gui_staging_area{
                ImVec2{0.0f, cumulative_height},
                area_heights[i],
                id,
                window_id
            });

            cumulative_height += area_heights[i];
        }
    }

    magik_gui_staging_area* get_staging_area(const unsigned int window_id, const unsigned int staging_area_id)
    {
        return &staging_areas.at(window_id).at(staging_area_id);
    }

    void show(const unsigned int window_id)
    {
        auto it = staging_areas.find(window_id);
        if (it == staging_areas.end()) return;

        for (magik_gui_staging_area& staging_area: it->second)
        {
            staging_area.show();
        }
    }
};

struct magik_gui_context_menu
{
    const char* label;
    
    interactive_element_action_caller action;

    float relative_width = 1.0f;
    float popup_width = 250.0f;

    std::unique_ptr<magik_gui_staging_area> staging_area = 
        std::make_unique<magik_gui_staging_area>(magik_gui_staging_area{ .auto_size_width = true });
    
    ImVec2 position;
};

template <typename T>
void magik_gui_show_list_dropdown(void* user_data)
{
    magik_gui_base_list<T>* list = (magik_gui_base_list<T>*)user_data;

    magik_gui_begin_labeled(list->label);

    ImGui::SetNextItemWidth(magik_gui_get_available_width(list->relative_width));

    ImGui::Combo(
        "##list",
        &list->selected_element_index,
        [](void* data, int index) -> const char*
        {
            magik_gui_base_list<T>* list = (magik_gui_base_list<T>*)data;

            if (index < 0 || index >= static_cast<int>(list->elements.size())) return "";

            return list->get_element_name(list->elements[index]);
        },
        list,
        static_cast<int>(list->elements.size())
    );
    magik_gui_end_labeled();
}

template <typename T>
void magik_gui_show_list_selectable(void* user_data)
{
    magik_gui_base_list<T>* list = (magik_gui_base_list<T>*)user_data;

    float item_height = ImGui::GetTextLineHeightWithSpacing();
    float box_height = item_height * static_cast<float>(list->elements.size()) + ImGui::GetStyle().FramePadding.y * 2.0f;

    magik_gui_begin_labeled(list->label);

    ImGui::SetNextItemWidth(magik_gui_get_available_width(list->relative_width));

    if (ImGui::BeginListBox("##list", ImVec2(0.0f, box_height)))
    {
        for (int i = 0; i < static_cast<int>(list->elements.size()); i++)
        {
            bool selected = list->selected_element_index == i;

            if (ImGui::Selectable(list->get_element_name(list->elements[i]), selected))
            {
                list->selected_element_index = i;
            }

            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndListBox();
    }
    magik_gui_end_labeled();
}

template <typename T>
void magik_gui_show_list(void* user_data)
{
    magik_gui_base_list<T>* list = (magik_gui_base_list<T>*)user_data;

    switch (list->type)
    {
        case e_magik_gui_list_type::Dropdown:
        {
            magik_gui_show_list_dropdown<T>(user_data);
            break;
        }
        
        case e_magik_gui_list_type::List:
        {
            magik_gui_show_list_selectable<T>(user_data);
            break;
        }
    }
}

void magik_gui_show_button(void* user_data)
{
    magik_gui_button* button = (magik_gui_button*)user_data;

    magik_gui_begin_labeled(button->label);
    if (ImGui::Button("##button", button->size))
    {
        button->counter++;
    }
    magik_gui_end_labeled();

}

void magik_gui_show_slider(void* user_data)
{
    magik_gui_slider* slider = (magik_gui_slider*)user_data;

    magik_gui_begin_labeled(slider->label);

    ImGui::SetNextItemWidth(magik_gui_get_available_width(slider->relative_width));
    ImGui::SliderFloat("##slider", &slider->value, slider->min_value, slider->max_value, slider->format, slider->flags);
    
    magik_gui_end_labeled();

    //float avail_width = ImGui::GetContentRegionAvail().x;


    //ImGui::SliderFloat(slider->label, &slider->value, slider->min_value, slider->max_value, slider->format, slider->flags);
}

void magik_gui_show_colorpicker(void* user_data)
{
    magik_gui_colorpicker* colorpicker = (magik_gui_colorpicker*)user_data;

    magik_gui_begin_labeled(colorpicker->label);

    ImGui::SetNextItemWidth(magik_gui_get_available_width(colorpicker->relative_width));

    ImGui::ColorEdit3("##color", &colorpicker->color.x, colorpicker->flags);
    magik_gui_end_labeled();
}

void magik_gui_show_checkbox(void* user_data)
{
    magik_gui_checkbox* checkbox = (magik_gui_checkbox*)user_data;

    magik_gui_begin_labeled(checkbox->label);
    ImGui::Checkbox("##checkbox", &checkbox->value);
    magik_gui_end_labeled();
}

void magik_gui_show_context_menu(void* user_data)
{
    magik_gui_context_menu* context_menu = (magik_gui_context_menu*)user_data;

    context_menu->staging_area->vertical_offset_from_top = 0.0f;

    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        context_menu->position = ImGui::GetMousePos();
        ImGui::OpenPopup(context_menu->label);
    }

    float parent_width = ImGui::GetWindowContentRegionMax().x - ImGui::GetWindowContentRegionMin().x;

    float popup_width = context_menu->relative_width * parent_width;

    ImGui::SetNextWindowPos(context_menu->position);
    ImGui::SetNextWindowSize(ImVec2(popup_width, 0.0f), ImGuiCond_Always);

    if (ImGui::BeginPopup(context_menu->label))
    {
        context_menu->staging_area->auto_size_width = false;
        context_menu->staging_area->show();

        ImGui::EndPopup();
    }
}

void magik_gui_show_dropdown_menu(void* user_data)
{
    magik_gui_dropdown_menu* dropdown_menu = (magik_gui_dropdown_menu*)user_data;

    float avail_width = ImGui::GetContentRegionAvail().x;

    //ImGui::SetNextItemWidth(avail_width*dropdown_menu->relative_width);
    //ImGui::SetNextWindowSizeConstraints(
    //    ImVec2(avail_width*dropdown_menu->relative_width, 0.0f),
    //    ImVec2(avail_width*dropdown_menu->relative_width, FLT_MAX)
    //);

    if (ImGui::BeginCombo(dropdown_menu->label, dropdown_menu->label))
    {
        dropdown_menu->staging_area->show();

        ImGui::EndCombo();
    }
}

#endif