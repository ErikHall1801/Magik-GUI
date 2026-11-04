#ifndef MAGIK_GUI_UI_CONFIGURATION_H
#define MAGIK_GUI_UI_CONFIGURATION_H

#include "magik.h"
#include "magik_interactive_elements.h"

void configure_render_context_menu(magik_gui_context_menu& render_context_menu, magik_render_manager_t* manager)
{
    render_context_menu.staging_area->add_slider(
        "Rotation X",
        0.0f,
        -10.0f,
        10.0f,
        manager
    );

    render_context_menu.staging_area->add_slider(
        "Rotation Y",
        0.0f,
        -10.0f,
        10.0f,
        manager
    );

    render_context_menu.staging_area->add_slider(
        "Rotation Z",
        0.0f,
        -10.0f,
        10.0f,
        manager
    );

    render_context_menu.staging_area->add_slider(
        "Position X",
        2.0f,
        -100.0f,
        100.0f,
        manager
    );

    render_context_menu.staging_area->add_slider(
        "Position Y",
        5.0f,
        -100.0f,
        100.0f,
        manager
    );

    render_context_menu.staging_area->add_slider(
        "Position Z",
        9.0f,
        -100.0f,
        100.0f,
        manager,
        { .display_label = "Just a test name" }
    );
}

void configure_staging_areas_menu(magik_gui_staging_area_manager& staging_area_manager, magik_render_manager_t* manager)
{
    std::vector<float> staging_area_heights{200.0f};

    staging_area_manager.add_staging_areas(4, 1, staging_area_heights);

    staging_area_manager.get_staging_area(4, 0)->add_colorpicker(
        "Dielectric colorpicker",
        ImVec4(1.0f, 1.0f, 1.0f, 1.0f),
        manager,
        { .relative_width = 0.5f, .display_label = "Test_display_name" }
    );

    struct Material
    {
        const char* name;
        float ior;

        static const char* get_name(const Material& m){ return m.name; }
    };

    Material dielectric{"dielectric", 1.2f};
    Material conductor{"conductor", 2.5f};

    std::vector<Material> materials{dielectric, conductor};

    staging_area_manager.get_staging_area(4, 0)->add_list<Material>(
        "IORs",
        materials,
        Material::get_name,
        e_magik_gui_list_type::Dropdown,
        manager,
        { .relative_width = 0.6f, .display_label = "Test materials" }
    );

    staging_area_manager.get_staging_area(4, 0)->add_list<Material>(
        "IORs list",
        materials,
        Material::get_name,
        e_magik_gui_list_type::List,
        manager
    );

    magik_gui_dropdown_menu& dropdown = staging_area_manager.get_staging_area(4, 0)->add_dropdown_menu(
        "Dropdown inside settings",
        manager,
        { .display_label = "Dropdown menu test" }
    );

    dropdown.staging_area->add_checkbox(
        "Checkbox inside dropdown",
        false,
        manager
    );

    dropdown.staging_area->add_button(
        "Button inside dropdown menu",
        manager,
        { .size = ImVec2{100.0f, 100.0f} }
    );
}

#endif