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

GLuint load_image(int& width, int& height, int& channels)
{
    unsigned char* pixels = stbi_load(
        "assets/example_render.png",
        &width,
        &height,
        &channels,
        4
    );

    GLuint texture;

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    stbi_image_free(pixels);

    return texture;
}

std::string handle_key_press()
{
    if (ImGui::IsKeyPressed(ImGuiKey_A))
    {
        return "A";
    }

    if (ImGui::IsKeyPressed(ImGuiKey_W))
    {
        return "W";
    }

    if (ImGui::IsKeyPressed(ImGuiKey_S))
    {
        return "S";
    }

    if (ImGui::IsKeyPressed(ImGuiKey_D))
    {
        return "D";
    }

    return "";
}

enum class MaterialType
{
    Dielectric,
    Conductor
};

enum class DielectricType
{
    Glass,
    Plastic
};

enum class ConductorType
{
    Copper,
    Gold,
    Iron
};


struct Dielectric
{
    DielectricType type;
    float specular_ior;
    ImVec4 rgb_color;
    bool transmissive;
};

struct Conductor
{
    ConductorType type;
    float ior;
};

struct UniversalMaterial
{
    MaterialType type;
    std::variant<Dielectric, Conductor> properties;
    std::string material_name;
};

const char* dielectric_types[] = {
    "Glass",
    "Plastic"
};

const char* conductor_types[] = {
    "Copper",
    "Gold",
    "Iron"
};

const char* material_types[] = {
    "Dielectric",
    "Conductor"
};

int main()
{
	// Init GLFW lib
	if (!glfwInit()) { return -1; }

	// Tells glfw that we use version 3.3 of openGL
	// So that if someone does not have this version the window creation
	// will fail. 
	// The "CORE_PROFILE" means we will not use backwards compatible features. 
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	// Create window
	int height = 1000;
	int width = 1400;
	GLFWwindow* window = glfwCreateWindow(width, height, "Test application", nullptr, nullptr);

	if (!window)
	{
		printf("Failed to create window ! \n");
		glfwTerminate();
		return -1;
	}

	// Make context current ?
	// Means the OpenGL rendering context is tied to the window
	// Once the context is current, we can issue commands like
	// clearing the buffer, drawing geometry etc. Since OpenGL 
	// is a state machine
	glfwMakeContextCurrent(window);

	// Load OpenGL function pointers
	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
	{
		printf("Failed to load OpenGL function pointers ! \n");
		glfwTerminate();
		return -1;
	}


    // Setup Dear ImGui context
    float main_scale = ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor()); // Valid on GLFW 3.3+ only
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // When adding fonts, the first font added will be used as  the default font
    // Load ImGui's default font first to use this as default
    ImFont* default_font = io.Fonts->AddFontDefault();

    ImFont* italic_font = io.Fonts->AddFontFromFileTTF(
        "assets/fonts/PlaywriteDELAGuides-Regular.ttf",
        30.0f // Font size
    );

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();

    // Setup scaling
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale);        // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)
    style.FontScaleDpi = main_scale;        // Set initial font scale. (in docking branch: using io.ConfigDpiScaleFonts=true automatically overrides this for every window depending on the current monitor)

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);

    // Our state
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

	ImGui_ImplOpenGL3_Init("#version 330");

    int image_width;
    int image_height;
    int channels;

    GLuint texture = load_image(image_width, image_height, channels);

    static std::vector<std::string> logs;

    static std::vector<UniversalMaterial> materials;
    static int selected_material = -1;

    static char material_name[128] = "";
    static float ior = 1.0f;
    static ImVec4 rgb_color = ImVec4{1.0f, 1.0f, 1.0f, 1.0f};
    static bool transmissive = false;

    static int selected_dielectric_type = 0;
    static int selected_conductor_type = 0;

	// Render loop
	while (!glfwWindowShouldClose(window))
	{
        // Poll and handle events (inputs, window resize, etc.)
        // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
        // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
        // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
        // Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
        glfwPollEvents();
        if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) != 0)
        {
            ImGui_ImplGlfw_Sleep(10);
            continue;
        }

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

         if (ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            glfwSetWindowShouldClose(window, true);
        }

        ImGuiViewport* viewport = ImGui::GetMainViewport();

        float settings_window_width = viewport->Size.x*0.3f;
        float console_window_height = viewport->Size.y*0.3f;
        
        float display_window_width = viewport->Size.x - settings_window_width;
        float display_window_height = viewport->Size.y - console_window_height;

        // Setting up settings window

        ImGui::SetNextWindowPos(
            ImVec2(viewport->Pos.x + viewport->Size.x - settings_window_width,
                   viewport->Pos.y)
        );

        ImGui::SetNextWindowSize(
            ImVec2(settings_window_width, viewport->Size.y)
        );

        ImGui::Begin(
            "Settings",
            nullptr,
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse
        );

        // Displays material information in the settings panel
        if (selected_material >= 0 && selected_material < materials.size())
        {
            UniversalMaterial& material = materials[selected_material];

            ImGui::Text("%s", material.material_name.c_str());

            // Dielectric and conductors have different properties, so only display the appropriate properties for the selected material
            switch (material.type)
            {
                case MaterialType::Dielectric:
                {
                    Dielectric& dielectric = std::get<Dielectric>(material.properties);

                    ImGui::SliderFloat("IOR", &dielectric.specular_ior, 1.0f, 2.0f);

                    if (ImGui::ColorEdit3("Dielectric Color", &dielectric.rgb_color.x, ImGuiColorEditFlags_PickerHueWheel))
                    {
                        logs.push_back(std::format("Dielectric color changed to ({} {} {})", dielectric.rgb_color.x, dielectric.rgb_color.y, dielectric.rgb_color.z));
                    }

                    ImGui::Checkbox("Transmissive", &dielectric.transmissive);

                    break;
                }

                case MaterialType::Conductor:
                {
                    Conductor& conductor = std::get<Conductor>(material.properties);

                    ImGui::SliderFloat("IOR", &conductor.ior, 1.0f, 2.0f);

                    break;
                }
            }
        }
        else
        {
            ImGui::Text("No material selected");
        }

        ImGui::End();

        // Setting up console log window

        ImGui::SetNextWindowPos(
            ImVec2(viewport->Pos.x, viewport->Pos.y + viewport->Size.y - console_window_height)
        );

        ImGui::SetNextWindowSize(
            ImVec2(viewport->Size.x - settings_window_width, console_window_height)
        );

        ImGui::Begin(
            "Console log",
            nullptr,
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse
        );

        std::string key_pressed = handle_key_press();

        if (!key_pressed.empty())
        {
            logs.push_back(key_pressed);
        }

        for (const std::string& message: logs)
        {
            ImGui::TextUnformatted(message.c_str());
        }

        //ImGui::PushFont(italic_font);
        //ImGui::Text("This is in italic");
        //ImGui::PopFont();

        ImGui::End();

        // Setting up display window

        ImGui::SetNextWindowPos(
            ImVec2(viewport->Pos.x, viewport->Pos.y)
        );

        ImGui::SetNextWindowSize(
            ImVec2(display_window_width, display_window_height)
        );

        ImGui::Begin(
            "Display",
            nullptr,
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse
        );

        ImVec2 available_display_space = ImGui::GetContentRegionAvail();

        float scale = std::min(
            available_display_space.x / image_width,
            available_display_space.y / image_height
        );

        scale = std::min(scale, 1.0f);

        ImVec2 image_size(
            image_width * scale,
            image_height * scale
        );

        float offset_x = (available_display_space.x - image_size.x)*0.5;
        float offset_y = (available_display_space.y - image_size.y)*0.5;

        // Draws the image centered in the display window 
        ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + offset_x, ImGui::GetCursorPosY() + offset_y));
        ImGui::Image((ImTextureID)(intptr_t)texture, image_size);

        // Open popup on right click in the display window
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            ImGui::OpenPopup("DisplayContextMenu");
        }

        // The popup opens the material list
        if (ImGui::BeginPopup("DisplayContextMenu"))
        {
            if (ImGui::Button("Add material"))
            {
                ImGui::OpenPopup("AddMaterialPopup");
            }

            // Adding a material requires specifying material properties
            if (ImGui::BeginPopup("AddMaterialPopup"))
            {
                UniversalMaterial material;
                
                ImGui::InputText("Material Name", material_name, IM_ARRAYSIZE(material_name));

                static int selected_material_type = 0;

                // Dropdown for choosing dielectric/conductor
                ImGui::Combo("Material type", &selected_material_type, material_types, IM_ARRAYSIZE(material_types));

                // Choose material properties
                // The parameters that can be tweaked depends on the material
                switch (selected_material_type)
                {
                    case 0:
                    {
                        ImGui::Combo("Dielectric type", &selected_dielectric_type, dielectric_types, IM_ARRAYSIZE(dielectric_types));

                        ImGui::SliderFloat("Specular IOR", &ior, 1.0f, 2.0f);
                        ImGui::ColorEdit3("Dielectric Color", &rgb_color.x, ImGuiColorEditFlags_PickerHueWheel);
                        ImGui::Checkbox("Transmissive", &transmissive);

                        break;
                    }

                    case 1:
                    {
                        ImGui::Combo("Conductor type", &selected_conductor_type, conductor_types, IM_ARRAYSIZE(conductor_types));
                        ImGui::SliderFloat("IOR", &ior, 1.0f, 2.0f);
           
                        break;
                    }
                }

                ImGui::Separator();

                // Finishes the creation of the material and stores the new material in the material list
                if (ImGui::Button("Create"))
                {
                    UniversalMaterial material;

                    material.material_name = material_name;

                    switch (selected_material_type)
                    {
                        case 0:
                        {
                            material.type = MaterialType::Dielectric;

                            material.properties = Dielectric{
                                static_cast<DielectricType>(selected_dielectric_type),
                                ior,
                                rgb_color,
                                transmissive
                            };

                            break;
                        }

                        case 1:
                        {
                            material.type = MaterialType::Conductor;

                            material.properties = Conductor{
                                static_cast<ConductorType>(selected_conductor_type),
                                ior
                            };

                            break;
                        }
                    }

                    materials.push_back(material);
                    selected_material = materials.size() - 1;

                    material_name[0] = '\0';
                    ior = 1.0f;
                    rgb_color = ImVec4{1.0f, 1.0f, 1.0f, 1.0f};
                    transmissive = false;

                    selected_dielectric_type = 0;
                    selected_conductor_type = 0;

                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            
            // Shows the list of materials
            if (ImGui::BeginListBox("Materials"))
            {
                for (int i = 0; i < materials.size(); i++)
                {
                    bool selected = selected_material == i;

                    if (ImGui::Selectable(materials[i].material_name.c_str(), selected))
                    {
                        selected_material = i;
                    }

                    if (selected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }

                ImGui::EndListBox();
            }

            ImGui::EndPopup();
        }

        ImGui::End();

        // Rendering
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
	}

	return 0;
}