// GLAD must come first ! Or else we get conflicts 
#include <glad/glad.h>
#include <stdio.h>
#include <GLFW/glfw3.h>
#include <stb_image.h>
#include <iostream>
#include <vector>
#include <variant>
#include <format>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

static bool magik_gui_setup_glfw(GLFWwindow* &window, const int i_height, const int i_width)
{
    if(!glfwInit())
    {
        return false;
    }

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    window = glfwCreateWindow(i_width, i_height, "Magik GUI 0.1.0", nullptr, nullptr);

    if(!window)
    {
        printf("Failed to create glfw window ! \n");
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(window);

	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
	{
		printf("Failed to load OpenGL function pointers ! \n");
		glfwTerminate();
		return false;
	}

    int icon_height;
    int icon_width;
    int icon_channels;
    unsigned char* _icon = stbi_load("assets/icon.png", &icon_width, &icon_height, &icon_channels, 4); // Must be freed !
    GLFWimage icon;
    icon.height = icon_height;
    icon.width = icon_width;
    icon.pixels = _icon;
    glfwSetWindowIcon(window, 1, &icon);

    return true;
}

static void magik_gui_setup_imgui(GLFWwindow* window, ImFont* &gui_font, float& gui_scale)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    gui_font = io.Fonts->AddFontFromFileTTF("assets/fonts/GoogleSans-Regular.ttf", 18.0f);
    gui_scale = ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor());

    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(gui_scale);
    style.FontScaleDpi = gui_scale;

    ImGui_ImplGlfw_InitForOpenGL(window, true);

    ImGui_ImplOpenGL3_Init("#version 330");
}

struct magik_gui_image
{
    GLuint buffer;
    int height;
    int width;
    int channels;
};

struct console_data
{
    ImGuiTextBuffer buffer;
    bool auto_scroll = true;

    void clear()
    {
        buffer.clear();
    }

    void mlog(const char* fmt, ...) IM_FMTARGS(2)
    {
        va_list args;
        va_start(args, fmt);
        buffer.appendfv(fmt, args);
        va_end(args);
    }

    void mprint()
    {
        ImGui::BeginChild("ScrollingRegion", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::TextUnformatted(buffer.begin(), buffer.end());

        if (auto_scroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
            ImGui::SetScrollHereY(1.0f);
        }

        ImGui::EndChild();
    }
};

struct magik_gui_global_data
{
    ImFont* gui_font = nullptr;
    float gui_size = 0.0f;

    magik_gui_image picture_asset;

    console_data c_log;
};

struct magik_gui_sliderfloat
{
    const char* label;
    float value;
    float min = 0.0f;
    float max = 1.0f;

    void show()
    {
        ImGui::SliderFloat(label, &value, min, max);
    }
};

struct magik_gui_colorpicker
{
    const char* label;
    ImVec4 color;
    ImGuiColorEditFlags flags = ImGuiColorEditFlags_PickerHueWheel;

    void show()
    {
        ImGui::ColorEdit3(label, &color.x, flags);
    }
};

struct magik_gui_checkbox
{
    const char* label;
    bool value;

    void show()
    {
        ImGui::Checkbox(label, &value);
    }
};

struct magik_gui_dropdown
{
    const char* label;
    const char* const* elements;
    int element_count;
    int selected_element_index = 0;

    void show()
    {
        ImGui::Combo(label, &selected_element_index, elements, element_count);
    }
};

struct magik_gui_list
{
    const char* label;
    const char* const* elements;
    int element_count;
    int selected_element_index = -1;

    void show()
    {
        if (ImGui::BeginListBox(label))
        {
            for (int i = 0; i < element_count; i++)
            {
                bool selected = selected_element_index == i;

                if (ImGui::Selectable(elements[i], selected))
                {
                    selected_element_index = i;
                }

                if (selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }

            ImGui::EndListBox();
        }
    }
};

template<typename... magik_gui_element>
void magik_gui_show_interactive_elements(magik_gui_element&... elements)
{
    (elements.show(), ...);
}

static magik_gui_global_data g_data_instance;
magik_gui_global_data* global_data = &g_data_instance;

static magik_gui_image load_magik_gui_image(const char* filename)
{
    magik_gui_image result;

    unsigned char* pixels = stbi_load(filename, &result.width, &result.height, &result.channels, 4);

    glGenTextures(1, &result.buffer);
    glBindTexture(GL_TEXTURE_2D, result.buffer);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, result.width, result.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    stbi_image_free(pixels);

    return result;
}

static void magik_gui_setup_global_data(ImFont* gui_font, float gui_size)
{
    global_data->gui_font = gui_font;
    global_data->gui_size = gui_size;

    global_data->picture_asset = load_magik_gui_image("assets/example_render.png");

    global_data->c_log = console_data();
}

// Free function

static void style_begin()
{
    ImGui::PushFont(global_data->gui_font);

    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(36.0f/255.0f, 36.0f/255.0f, 36.0f/255.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(48.0f/255.0f, 48.0f/255.0f, 48.0f/255.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(24.0f/255.0f, 24.0f/255.0f, 24.0f/255.0f, 1.0f));
}

static void style_end()
{
    ImGui::PopFont();

    ImGui::PopStyleColor();
    ImGui::PopStyleColor();
    ImGui::PopStyleColor();
}

static void magik_gui_new_frame(GLFWwindow* window)
{
    glfwPollEvents();
    if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) != 0)
    {
        ImGui_ImplGlfw_Sleep(10);
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

typedef void (*action_caller)(void*);

static void magik_gui_window(const char* title, ImGuiWindowFlags flags, const ImVec2 window_pos, const ImVec2 window_size, action_caller user_function, void* user_data)
{
    style_begin();
    ImGui::SetNextWindowPos(window_pos);
    ImGui::SetNextWindowSize(window_size);
    ImGui::Begin(title, nullptr, flags);

    if(user_function) user_function(user_data);

    style_end();
    ImGui::End();
}

// Placeholder interactive elements
magik_gui_sliderfloat slider = magik_gui_sliderfloat{
    "Test_slider",
    1.0f,
    0.0f,
    1.0f
};

magik_gui_colorpicker color = magik_gui_colorpicker{
    "Test_colorpicker",
    ImVec4{1.0f, 1.0f, 1.0f, 1.0f}
};

magik_gui_sliderfloat slider2 = magik_gui_sliderfloat{
    "Test_slider2",
    0.0f,
    0.0f,
    1.0f
};

magik_gui_colorpicker color2 = magik_gui_colorpicker{
    "Test_colorpicker2",
    ImVec4{0.0f, 0.0f, 0.0f, 1.0f}
};

const char* material_types[] = {
    "Dielectric",
    "Conductor"
};

magik_gui_dropdown material_type_dropdown = magik_gui_dropdown{
    "Material Type",
    material_types,
    2
};

magik_gui_list material_type_list = magik_gui_list{
    "Material type",
    material_types,
    2
};

static void context_menu(const char* context_menu_name)
{
    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        ImGui::OpenPopup(context_menu_name);
    }

    if (ImGui::BeginPopup(context_menu_name))
    {
        ImGui::TextUnformatted(context_menu_name);
        ImGui::Separator();

        magik_gui_show_interactive_elements(slider, color, material_type_dropdown, material_type_list);

        ImGui::EndPopup();
    }
}

// Console
static void console_function(void* user_data)
{
    global_data->c_log.mprint();
}

// Display window
static void display_function(void* user_data)
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));

    ImGui::BeginChild("BgOverride", ImGui::GetContentRegionAvail(), false);

    ImVec2 available_display_space = ImGui::GetContentRegionAvail();

    float scale = std::min(
        available_display_space.x / global_data->picture_asset.width,
        available_display_space.y / global_data->picture_asset.height
    );

    scale = std::min(scale, 1.0f);

    ImVec2 image_size(
        global_data->picture_asset.width * scale,
        global_data->picture_asset.height * scale
    );

    float offset_x = (available_display_space.x - image_size.x)*0.5;
    float offset_y = (available_display_space.y - image_size.y)*0.5;

    ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + offset_x, ImGui::GetCursorPosY() + offset_y));
    ImGui::Image((ImTextureID)(intptr_t)global_data->picture_asset.buffer, image_size);

    context_menu("DisplayContextMenu");

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

int main()
{
    const int initial_height = 1000;
    const int initial_width = 1400;
    GLFWwindow* window;
    if(!magik_gui_setup_glfw(window, initial_height, initial_width)) return -1;

    ImFont* gui_font;
    float gui_scale;
    magik_gui_setup_imgui(window, gui_font, gui_scale);

    magik_gui_setup_global_data(gui_font, gui_scale);

    global_data->c_log.mlog("Startup at %f seconds \n", ImGui::GetTime());

	while (!glfwWindowShouldClose(window))
	{
        if(ImGui::IsMouseDragging(ImGuiMouseButton_Left)) global_data->c_log.mlog("Mouse position is %f, %f \n", ImGui::GetMousePos().x, ImGui::GetMousePos().y);

        magik_gui_new_frame(window);

        ImGuiViewport* viewport = ImGui::GetMainViewport();

        float settings_window_width = viewport->Size.x*0.3f;
        float console_window_height = viewport->Size.y*0.3f;
        
        float display_window_width = viewport->Size.x - settings_window_width;
        float display_window_height = viewport->Size.y - console_window_height;

        magik_gui_window("Settings", ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse, ImVec2(viewport->Pos.x + viewport->Size.x - settings_window_width, viewport->Pos.y), ImVec2(settings_window_width, viewport->Size.y), nullptr, nullptr);

        magik_gui_window("Console", ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse, ImVec2(viewport->Pos.x, viewport->Pos.y + viewport->Size.y - console_window_height), ImVec2(viewport->Size.x - settings_window_width, console_window_height), console_function, nullptr);

        magik_gui_window("Image", ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse, ImVec2(viewport->Pos.x, viewport->Pos.y), ImVec2(display_window_width, display_window_height), display_function, nullptr);

        // Rendering
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
	}

	return 0;
}