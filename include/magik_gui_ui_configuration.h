#ifndef MAGIK_GUI_UI_CONFIGURATION_H
#define MAGIK_GUI_UI_CONFIGURATION_H

#include "magik.h"
#include "magik_interactive_elements.h"

void configure_render_context_menu(magik_gui_context_menu& render_context_menu, magik_render_manager_t* manager)
{
    render_context_menu.staging_area->add_slider(
        "Rotation X",
        magik_gui_show_slider,
        0.0f,
        -10.0f,
        10.0f,
        manager
    );

    render_context_menu.staging_area->add_slider(
        "Rotation Y",
        magik_gui_show_slider,
        0.0f,
        -10.0f,
        10.0f,
        manager
    );

    render_context_menu.staging_area->add_slider(
        "Rotation Z",
        magik_gui_show_slider,
        0.0f,
        -10.0f,
        10.0f,
        manager
    );

    render_context_menu.staging_area->add_slider(
        "Position X",
        magik_gui_show_slider,
        2.0f,
        -100.0f,
        100.0f,
        manager
    );

    render_context_menu.staging_area->add_slider(
        "Position Y",
        magik_gui_show_slider,
        2.0f,
        -100.0f,
        100.0f,
        manager
    );

    render_context_menu.staging_area->add_slider(
        "Position Z",
        magik_gui_show_slider,
        2.0f,
        -100.0f,
        100.0f,
        manager
    );
}

void configure_staging_areas_menu(magik_gui_staging_area_manager& staging_area_manager, magik_render_manager_t* manager)
{
    std::vector<float> staging_area_heights{200.0f};

    staging_area_manager.add_staging_areas(4, 1, staging_area_heights);

    staging_area_manager.get_staging_area(4, 0)->add_colorpicker(
        "Dielectric colorpicker",
        magik_gui_show_colorpicker,
        ImVec4(1.0f, 1.0f, 1.0f, 1.0f),
        manager
    );
}

#endif