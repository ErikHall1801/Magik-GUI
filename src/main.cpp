// GLAD must come first ! Or else we get conflicts
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <stb_image.h>
#include <nfd.hpp>

#include <cstdio>
#include <filesystem>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "magik.h"

#include "magik_gui/app.h"
#include "magik_gui/ui_configuration.h"

/*
    Platform layer only: window, OpenGL, Dear ImGui backends and the Magik render manager.
    Everything you can see and click is declared in include/magik_gui/ui_configuration.h.
*/

static GLFWwindow* create_window(int width, int height)
{
    if (!glfwInit()) return nullptr;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);   // initial window size follows the monitor DPI

    GLFWwindow* window = glfwCreateWindow(width, height, "Magik GUI 0.2.0", nullptr, nullptr);
    if (!window)
    {
        std::printf("Failed to create glfw window !\n");
        glfwTerminate();
        return nullptr;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(0);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::printf("Failed to load OpenGL function pointers !\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return nullptr;
    }

    GLFWimage icon;
    int channels = 0;
    icon.pixels = stbi_load("assets/icon.png", &icon.width, &icon.height, &channels, 4);
    if (icon.pixels)
    {
        glfwSetWindowIcon(window, 1, &icon);
        stbi_image_free(icon.pixels);
    }

    return window;
}

static void setup_imgui(GLFWwindow* window)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    // The font is loaded once; mgui::style_manager scales it (Dear ImGui >= 1.92 rasterizes on demand)
    const char* font_path = "assets/fonts/GoogleSans-Regular.ttf";
    if (std::filesystem::exists(font_path)) io.Fonts->AddFontFromFileTTF(font_path, 18.0f);
    else                                    io.Fonts->AddFontDefault();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");
}

int main()
{
    GLFWwindow* window = create_window(2000, 1100);
    if (!window) return -1;

    if (NFD_Init() != NFD_OKAY)
    {
        std::fprintf(stderr, "Failed to initialize NFD\n");
        return -1;
    }

    setup_imgui(window);

    magik_gui::console log;
    magik_gui::install_error_callback(log);

    check_magik_errors(magik_gl_init((magik_gl_loader_proc)glfwGetProcAddress));

    uint32_t major = 0, minor = 0, revision = 0;
    const char* version = nullptr;
    check_magik_errors(magik_get_version(&major, &minor, &revision, &version));
    if (version) log.log(magik_gui::log_level::info, "%s", version);
    magik_get_system_Info();

    magik_manager_descriptor_t descriptor;
    descriptor.display_type = MAGIK_DISPLAY_SWAPCHAIN;
    descriptor.user_device_id = 0;
    descriptor.user_stream = nullptr;
    descriptor.cqs_n_reserved_chunk = 4096;
    descriptor.cqs_drop_overflow = true;

    magik_render_manager_t manager = nullptr;
    check_magik_errors(magik_render_manager_create(&manager, descriptor));

    {
        magik_gui::app app(manager, log);
        magik_gui::configure_ui(app);
        app.start();

        while (!glfwWindowShouldClose(window) && !app.quit_requested)
        {
            glfwPollEvents();
            if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) != 0)
            {
                ImGui_ImplGlfw_Sleep(10);
                continue;
            }

            // Automatic scaling: monitor DPI, re-read every frame so moving the window between monitors works
            float content_scale_x = 1.0f, content_scale_y = 1.0f;
            glfwGetWindowContentScale(window, &content_scale_x, &content_scale_y);
            int window_w = 0, window_h = 0, framebuffer_w = 0, framebuffer_h = 0;
            glfwGetWindowSize(window, &window_w, &window_h);
            glfwGetFramebufferSize(window, &framebuffer_w, &framebuffer_h);
            const float framebuffer_scale = window_w > 0 ? static_cast<float>(framebuffer_w) / static_cast<float>(window_w) : 1.0f;

            app.prepare_frame(content_scale_x, framebuffer_scale);

            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            app.draw();

            ImGui::Render();
            glViewport(0, 0, framebuffer_w, framebuffer_h);
            glClearColor(0.05f, 0.05f, 0.055f, 1.0f);   // shows in the gaps between panels
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            glfwSwapBuffers(window);

            app.end_frame();
        }

        app.shutdown();
        check_magik_errors(magik_render_manager_destroy(manager));
    }

    magik_set_error_callback(nullptr, nullptr);   // the console is about to go away

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    NFD_Quit();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
