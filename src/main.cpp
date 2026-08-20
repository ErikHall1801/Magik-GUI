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

// ### Enum classes ###
enum class e_magik_gui_split_order_types : unsigned int
{
    x_axis = 0,
    y_axis = 1
};



// ### Defines & Structs ###
typedef void (*action_caller)(void*);

struct consol_data
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

struct magik_gui_image
{
    GLuint buffer;
    int height;
    int width;
    int channels;
};

struct single_node
{
    // The box_id is only meaningful if this is a leaf
    // node. I.e if children A and B are nullptr ! 

    e_magik_gui_split_order_types type = e_magik_gui_split_order_types::x_axis;
    float rel_anchor = 0.5f;
    unsigned int box_id = 0;

    std::unique_ptr<single_node> child_a = nullptr;
    std::unique_ptr<single_node> child_b = nullptr;

    ImVec2 abs_pos;
    ImVec2 abs_size;

    bool is_leaf() const
    {
        return child_a == nullptr && child_b == nullptr;
    }

    void split(float split_anchor, e_magik_gui_split_order_types split_type, unsigned int split_box_b_id)
    {
        // Note, this must have been a leaf node before so 
        // the rel_anchor and type did not have a meaning
        // yet.  

        if(!is_leaf()) return;

        rel_anchor = split_anchor;
        type = split_type;

        child_a = std::make_unique<single_node>();
        child_b = std::make_unique<single_node>();

        child_a->box_id = box_id;
        child_b->box_id = split_box_b_id;
    }
};

struct panel_info
{
    const char* title = "Window";
    ImGuiWindowFlags flags;
    action_caller user_function;
    void* user_data = nullptr;
    unsigned int id;
};

struct layout_split_state
{
    std::unique_ptr<single_node> root = std::make_unique<single_node>();
    std::vector<single_node*> splitters;
    std::vector<panel_info> panels;

    single_node* find_node(single_node* node, const unsigned int search_box_id)
    {
        if(!node) return nullptr;

        if(node->is_leaf())
        {
            if(node->box_id == search_box_id) return node;
            return nullptr;
        }

        if(node->child_a)
        {
            if(auto result = find_node(node->child_a.get(), search_box_id)) return result;
        }

        if(node->child_b)
        {
            if(auto result = find_node(node->child_b.get(), search_box_id)) return result;
        }

        return nullptr;
    }

    bool compute_abs_bounds(single_node* node, ImVec2& out_size, ImVec2& out_position, ImVec2 current_size, ImVec2 current_position, const unsigned int box_id)
    {
        if(!node) return false;

        node->abs_pos = current_position;
        node->abs_size = current_size;

        if(node->is_leaf())
        {
            if(node->box_id == box_id)
            {
                out_size = current_size;
                out_position = current_position;
                return true;
            }

            return false;
        }

        ImVec2 size_a = current_size;
        ImVec2 size_b = current_size;
        ImVec2 position_a = current_position;
        ImVec2 position_b = current_position;

        if(node->type == e_magik_gui_split_order_types::x_axis)
        {
            size_a.x = current_size.x * node->rel_anchor;
            size_b.x = current_size.x - size_a.x;
            position_b.x = current_position.x + size_a.x;
        }
        else
        {
            size_a.y = current_size.y * node->rel_anchor;
            size_b.y = current_size.y - size_a.y;
            position_b.y = current_position.y + size_a.y;
        }

        if(compute_abs_bounds(node->child_a.get(), out_size, out_position, size_a, position_a, box_id)) { return true; }

        if(compute_abs_bounds(node->child_b.get(), out_size, out_position, size_b, position_b, box_id)) { return true; }

        return false;
    }

    bool contains_box(const single_node* node, const unsigned int target_id)
    {
        if(!node) return false;
        
        if(node->is_leaf()) return node->box_id == target_id;

        return contains_box(node->child_a.get(), target_id) || contains_box(node->child_b.get(), target_id);
    }

    single_node* find_splitter(single_node* node, const unsigned int box_id_a, const unsigned int box_id_b)
    {
        if(!node || node->is_leaf()) return nullptr;

        bool a_in_left = contains_box(node->child_a.get(), box_id_a);
        bool b_in_right = contains_box(node->child_b.get(), box_id_b);
        
        bool b_in_left = contains_box(node->child_a.get(), box_id_b);
        bool a_in_right = contains_box(node->child_b.get(), box_id_a);

        if((a_in_left && b_in_right) || (b_in_left && a_in_right)) 
        {
            return node;
        }

        if(a_in_left && b_in_left) return find_splitter(node->child_a.get(), box_id_a, box_id_b);
        if(a_in_right && b_in_right) return find_splitter(node->child_b.get(), box_id_a, box_id_b);

        return nullptr;
    }

    bool is_splitter(single_node* node)
    {
        return node->child_a && node->child_b;
    }

    void collect_splitters(single_node* node, std::vector<single_node*>& v_splitters)
    {
        if(!node) return;

        if(is_splitter(node)) { v_splitters.push_back(node); }

        if(node->child_a) { collect_splitters(node->child_a.get(), v_splitters); }

        if(node->child_b) { collect_splitters(node->child_b.get(), v_splitters); }
    }

    void split_node(const e_magik_gui_split_order_types type, const float rel_anchor, const unsigned int search_index, const unsigned int new_box_b_id)
    {
        single_node* target_node = find_node(root.get(), search_index);

        if(!target_node) return;

        target_node->split(rel_anchor, type, new_box_b_id);

        splitters.clear();
        collect_splitters(root.get(), splitters);
    }

    void counter_move_child_anchors(single_node* node, e_magik_gui_split_order_types target_axis, float old_pos, float old_size, float new_pos, float new_size)
    {
        if(!node || node->is_leaf()) return;

        if (new_size < 0.0001f) new_size = 0.0001f; 

        if(node->type == target_axis)
        {
            float old_rel = node->rel_anchor;
            float old_seam_pos = old_pos + (old_rel * old_size);            
            float new_anchor = (old_seam_pos - new_pos) / new_size;
            
            if(new_anchor > 0.999f) new_anchor = 0.999f;
            if(new_anchor < 0.001f) new_anchor = 0.001f;

            node->rel_anchor = new_anchor;

            counter_move_child_anchors(node->child_a.get(), target_axis, old_pos, old_rel * old_size, new_pos, new_anchor * new_size);
            counter_move_child_anchors(node->child_b.get(), target_axis, old_seam_pos, (1.0f - old_rel) * old_size, new_pos + (new_anchor * new_size), (1.0f - new_anchor) * new_size);
        }
        else 
        {
            counter_move_child_anchors(node->child_a.get(), target_axis, old_pos, old_size, new_pos, new_size);
            counter_move_child_anchors(node->child_b.get(), target_axis, old_pos, old_size, new_pos, new_size);
        }
    }

    void find_max_anchor_in_children(float& max_found, single_node* node, e_magik_gui_split_order_types type)
    {
        if(!node || node->is_leaf()) return;

        if(node->type == type) { max_found = std::max(max_found, node->rel_anchor); }

        find_max_anchor_in_children(max_found, node->child_a.get(), type);
        find_max_anchor_in_children(max_found, node->child_b.get(), type);
    }

    void move_anchor(const unsigned int box_id_a, const unsigned int box_id_b, float new_rel_anchor)
    {
        single_node* target_node = find_splitter(root.get(), box_id_a, box_id_b);
        if(!target_node) return;

        float old_rel_anchor = target_node->rel_anchor;

        float max_child_anchor = 0.01f;
        find_max_anchor_in_children(max_child_anchor, target_node->child_a.get(), target_node->type);
        find_max_anchor_in_children(max_child_anchor, target_node->child_b.get(), target_node->type);

        float safe_floor = (max_child_anchor * old_rel_anchor) / 0.99f;

        float actual_new_anchor = new_rel_anchor;
        if(actual_new_anchor < safe_floor) actual_new_anchor = safe_floor;
        if(actual_new_anchor > 0.99f) actual_new_anchor = 0.99f;

        target_node->rel_anchor = actual_new_anchor;

        float factor0 = old_rel_anchor / actual_new_anchor;
        float factor1 = actual_new_anchor / old_rel_anchor;
        
        counter_move_child_anchors(target_node->child_a.get(), target_node->type, 0.0f, old_rel_anchor, 0.0f, actual_new_anchor);
        counter_move_child_anchors(target_node->child_b.get(), target_node->type, old_rel_anchor, 1.0f - old_rel_anchor, actual_new_anchor, 1.0f - actual_new_anchor);
    }

    void add_panel(const char* title, ImGuiWindowFlags flags, action_caller user_function, void* user_data, unsigned int id)
    {
        panel_info inst;
        inst.title = title;
        inst.flags = flags;
        inst.user_function = user_function;
        inst.user_data = user_data;
        inst.id = id;

        panels.push_back(inst);
    }
};

struct magik_gui_global_data
{
    ImFont* gui_font = nullptr;
    float gui_size = 0.0f;

    magik_gui_image picture_asset;

    consol_data c_log;

    layout_split_state layout_state;
};



// ### Globals ###
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

    global_data->c_log = consol_data();
}



// ### Templates ###
template<typename T> T clamp(T val)
{
    if(val > static_cast<T>(1.0))
    {
        return static_cast<T>(1.0);
    }

    if(val < static_cast<T>(0.0))
    {
        return static_cast<T>(0.0);
    }

    return val;
}



// ### GLFW ###
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



// ### Dear ImGui Style ###
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



// ### Dear ImGui general ###
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

static void magik_gui_titlebar(GLFWwindow* window)
{
    if (ImGui::BeginMainMenuBar()) 
    {
	    if (ImGui::BeginMenu("Help"))
	    {
		    if(ImGui::MenuItem("Exit"))
		    {
			    glfwSetWindowShouldClose(window, GLFW_TRUE);
		    }
        
		    ImGui::EndMenu();
	    }
        
	    ImGui::EndMainMenuBar();
    }
}

static void magik_gui_draw_panels()
{
    ImVec2 position, size, root_size, root_position;

    root_size = ImGui::GetMainViewport()->WorkSize;
    root_position = ImGui::GetMainViewport()->WorkPos;

    for(auto iter : global_data->layout_state.panels)
    {
        global_data->layout_state.compute_abs_bounds(global_data->layout_state.root.get(), size, position, root_size, root_position, iter.id);
        magik_gui_window(iter.title, iter.flags, position, size, iter.user_function, iter.user_data);
    }
}

static void magik_gui_panel_sliders()
{
    float box_height = 5.0f;
    float box_width = 100.0f;
    static int active_splitter_index = -1;

    if(!ImGui::IsMouseDown(ImGuiMouseButton_Left)) 
    {
        active_splitter_index = -1;
    }

    for(size_t i = 0; i < global_data->layout_state.splitters.size(); i++)
    {
        auto* iter = global_data->layout_state.splitters[i];
        ImVec2 box_max, box_min;
        ImVec2 centroid;

        switch(iter->type)
        {
            case e_magik_gui_split_order_types::x_axis:
            {
                centroid.x = iter->abs_pos.x + iter->abs_size.x * iter->rel_anchor;
                centroid.y = (iter->abs_pos.y + iter->abs_pos.y + iter->abs_size.y) * 0.5f;
                box_max.x = centroid.x + box_height;
                box_max.y = centroid.y + box_width;
                box_min.x = centroid.x - box_height;
                box_min.y = centroid.y - box_width;
                break;
            }
            case e_magik_gui_split_order_types::y_axis:
            {
                centroid.x = (iter->abs_pos.x + iter->abs_pos.x + iter->abs_size.x) * 0.5f;
                centroid.y = iter->abs_pos.y + iter->abs_size.y * iter->rel_anchor;
                box_max.x = centroid.x + box_width;
                box_max.y = centroid.y + box_height;
                box_min.x = centroid.x - box_width;
                box_min.y = centroid.y - box_height;
                break;
            }
        }

        bool is_hovered = ImGui::IsMouseHoveringRect(box_min, box_max, false);
        bool is_active = (active_splitter_index == static_cast<int>(i));

        if(is_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && active_splitter_index == -1) 
        {
            active_splitter_index = static_cast<int>(i);
            is_active = true;
        }

        if(is_active)
        {
            ImGui::SetMouseCursor(iter->type == e_magik_gui_split_order_types::x_axis ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
            float new_anchor = 0.0f;
            switch(iter->type)
            {
                case e_magik_gui_split_order_types::x_axis:
                {
                    new_anchor = (ImGui::GetMousePos().x - iter->abs_pos.x) / iter->abs_size.x;
                    break;
                }
                case e_magik_gui_split_order_types::y_axis:
                {
                    new_anchor = (ImGui::GetMousePos().y - iter->abs_pos.y) / iter->abs_size.y;
                    break;
                }
            }
            global_data->layout_state.move_anchor(iter->child_a->box_id, iter->child_b->box_id, clamp(new_anchor));
        }
        else if(is_hovered)
        {
            ImGui::SetMouseCursor(iter->type == e_magik_gui_split_order_types::x_axis ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
        }

        if(is_active || is_hovered)
        {
            ImDrawList* draw_list = ImGui::GetForegroundDrawList();
            draw_list->AddRectFilled(box_min, box_max, IM_COL32(72, 72, 96, 255), 4.0f);
        }
    } 
}

static void magik_gui_render(GLFWwindow* window)
{
    ImGui::Render();
    int display_w, display_h;
    glfwGetFramebufferSize(window, &display_w, &display_h);
    glViewport(0, 0, display_w, display_h);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window); 
}



// ### Panel functions ###
static void consol_function(void* user_data)
{
    global_data->c_log.mprint();
}

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

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

static void append_binary_tree(single_node* node, ImGuiTextBuffer& buffer)
{
    if(!node) return;

    buffer.appendf("Node position; [%f, %f] \n", node->abs_pos.x, node->abs_pos.y);
    buffer.appendf("Node size; [%f, %f] \n", node->abs_size.x, node->abs_size.y);
    buffer.appendf("\n");

    if(node->child_a) append_binary_tree(node->child_a.get(), buffer);
    if(node->child_b) append_binary_tree(node->child_b.get(), buffer);
}

static void stats_function(void* user_data)
{
    // Print out the leaf node sizes 
    ImGuiTextBuffer buffer;
    append_binary_tree(global_data->layout_state.root.get(), buffer);
    ImGui::BeginChild("ScrollingRegion", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::TextUnformatted(buffer.begin(), buffer.end());

    if(ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) 
    {
        ImGui::SetScrollHereY(1.0f);
    }

    ImGui::EndChild();
}

int main()
{
    const int initial_height = 1100;
    const int initial_width = 2000;
    GLFWwindow* window;
    if(!magik_gui_setup_glfw(window, initial_height, initial_width)) return -1;

    ImFont* gui_font;
    float gui_scale;
    magik_gui_setup_imgui(window, gui_font, gui_scale);

    magik_gui_setup_global_data(gui_font, gui_scale);

    global_data->c_log.mlog("Startup at %f seconds \n", ImGui::GetTime());

    global_data->layout_state.split_node(e_magik_gui_split_order_types::x_axis, 0.8f, 0, 1);
    global_data->layout_state.split_node(e_magik_gui_split_order_types::y_axis, 0.75f, 0, 2);
    global_data->layout_state.split_node(e_magik_gui_split_order_types::y_axis, 0.75f, 1, 3);
    global_data->layout_state.split_node(e_magik_gui_split_order_types::y_axis, 0.2f, 1, 4);

    global_data->layout_state.add_panel("Render viewport", ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse, display_function, nullptr, 0);
    global_data->layout_state.add_panel("Scene graph", ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse, nullptr, nullptr, 1);
    global_data->layout_state.add_panel("Consol log", ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse, consol_function, nullptr, 2);
    global_data->layout_state.add_panel("Statistics", ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse, stats_function, nullptr, 3);
    global_data->layout_state.add_panel("Settings", ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse, nullptr, nullptr, 4);

	while (!glfwWindowShouldClose(window))
	{
        magik_gui_new_frame(window);

        magik_gui_titlebar(window);

        magik_gui_draw_panels();

        magik_gui_panel_sliders();

        magik_gui_render(window);
	}

	return 0;
}