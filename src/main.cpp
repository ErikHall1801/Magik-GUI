// GLAD must come first ! Or else we get conflicts 
#include <glad/glad.h>
#include <stdio.h>
#include <GLFW/glfw3.h>
#include <stb_image.h>
#include <iostream>
#include <vector>
#include <variant>
#include <format>
#include <cmath>
#include <memory>
#include <nfd.hpp>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "magik.h"
#include "magik_gui_interactive_elements.h"

// ### Enum classes ###
enum class e_magik_gui_split_order_types : unsigned int
{
    x_axis = 0,
    y_axis = 1
};



// ### Defines & Structs ###
typedef void (*action_caller)(void*, magik_gui_context_menu*);

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
        ImGui::BeginChild("Scrolling region", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::TextUnformatted(buffer.begin(), buffer.end());

        if (auto_scroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
            ImGui::SetScrollHereY(1.0f);
        }

        ImGui::EndChild();
    }
};

static console_data g_console_data_instance;
console_data* global_consol_data = &g_console_data_instance;

struct magik_gui_image
{
    GLuint buffer;
    int height;
    int width;
    int channels;
};

struct bsp_node
{
    e_magik_gui_split_order_types type = e_magik_gui_split_order_types::x_axis;
    float relative_children_divider_position = 0.5f;

    unsigned int id = 0; 

    bsp_node* parent = nullptr;
    std::unique_ptr<bsp_node> child_a = nullptr;
    std::unique_ptr<bsp_node> child_b = nullptr;

    float abs_position_x = 0.0f;
    float abs_position_y = 1.0f;
    float abs_size_x = 1.0f;
    float abs_size_y = 1.0f;
};

struct bsp_window
{
    const char* title = "Window";
    ImGuiWindowFlags flags;
    action_caller user_function;
    void* user_data = nullptr;

    unsigned int id;

    magik_gui_context_menu* context_menu = nullptr;
};

struct bsp_state
{
    std::unique_ptr<bsp_node> root = std::make_unique<bsp_node>();
    std::vector<bsp_window> windows;
};

struct bsp_graph_vis
{
    bool is_root = false;
    bool is_leaf = false;
    float relative_position_to_root_x = 0.0f;
    float relative_position_to_root_y = 0.0f;
};

struct bsp_graph_manager
{
    /*
    Rework feature list;
        These are for when the system works 
        - merge_nodes (The nodes may not have the same parent, which may involve reordering the graph, this covers deletion as well)
        - generate_graph_visualization() (Returns a simple vector with all the nodes and their relations to be rendered by imgui. It also highlights the current node the mouse is over (ImGui::TreeNode ?))
    */

    std::vector<bsp_state> states;
    std::vector<bsp_node*> sibling_edges;

    unsigned int active_state_id = 0;
    float minimum_relative_children_divider_fraction = 0.05f; // as in 0.05 - 0.95 instead of 0-1

    bool is_leaf(bsp_node* node) const
    {
        return (node->child_a == nullptr) && (node->child_b == nullptr);
    }

    bool is_root(bsp_node* node) const
    {
        return !node->parent;
    }

    bsp_node* find_node(bsp_node* node, const unsigned int target_id)
    {
        if(!node) return nullptr;

        if(is_leaf(node))
        {
            if(node->id == target_id)
            {
                return node;
            }
            else
            {
                return nullptr;
            }
        }

        if(node->child_a) { if(auto res = find_node(node->child_a.get(), target_id)) return res; }
        if(node->child_b) { if(auto res = find_node(node->child_b.get(), target_id)) return res; }

        return nullptr;
    }

    bsp_node* find_parent(bsp_node* node, const unsigned int child_a_panel_id, const unsigned int child_b_panel_id)
    {
        if(!node || is_leaf(node)) return nullptr;

        if((node->child_a->id == child_a_panel_id) && (node->child_b->id == child_b_panel_id)) return node;

        if(node->child_a) { if(auto res = find_node(node->child_a.get(), child_a_panel_id)) return res; }
        if(node->child_b) { if(auto res = find_node(node->child_b.get(), child_b_panel_id)) return res; }

        return nullptr;
    }

    void collect_sibling_edges(bsp_node* node)
    {
        if(!node || is_leaf(node)) return;

        sibling_edges.push_back(node);

        collect_sibling_edges(node->child_a.get());
        collect_sibling_edges(node->child_b.get());
    }

    void find_sibling_edges()
    {
        sibling_edges.clear();
        collect_sibling_edges(states[active_state_id].root.get());
    }

    void split_node(const unsigned int state_id, const unsigned int target_id, e_magik_gui_split_order_types split_type, float split_relative_children_divider_position, unsigned int child_b_id)
    {
        bsp_node* target_node = find_node(states[state_id].root.get(), target_id);

        if(!target_node || !is_leaf(target_node)) return;

        target_node->type = split_type;
        target_node->relative_children_divider_position = split_relative_children_divider_position;

        target_node->child_a = std::make_unique<bsp_node>();
        target_node->child_b = std::make_unique<bsp_node>();

        target_node->child_a->parent = target_node;
        target_node->child_a->id = target_node->id;
        target_node->child_b->parent = target_node;
        target_node->child_b->id = child_b_id;

        target_node->id = std::numeric_limits<unsigned int>::max();
    }

    void merge_nodes()
    {

    }

    void swap_nodes(const unsigned int id_a, const unsigned int id_b)
    {
        bsp_node* node_a = find_node(states[active_state_id].root.get(), id_a); 
        bsp_node* node_b = find_node(states[active_state_id].root.get(), id_b);

        if(!node_a)
        {
            global_consol_data->mlog("[ Error ] - Node id_a not found \n");
            return;
        }

        if(!node_b)
        {
            global_consol_data->mlog("[ Error ] - Node id_b not found \n");
            return;
        }

        if(node_a == node_b)
        {
            global_consol_data->mlog("[ Error ] - Node cannot be swapped with itself \n");
            return;
        }

        bsp_node* parent_a = node_a->parent;
        bsp_node* parent_b = node_b->parent;

        if(!parent_a || !parent_b)
        {
            global_consol_data->mlog("[ Error ] - Cannot swap node with root \n");
            return;
        }

        std::unique_ptr<bsp_node>& ptr_a = (parent_a->child_a.get() == node_a) ? parent_a->child_a : parent_a->child_b;
        std::unique_ptr<bsp_node>& ptr_b = (parent_b->child_a.get() == node_b) ? parent_b->child_a : parent_b->child_b;

        ptr_a.swap(ptr_b);

        node_a->parent = parent_b;
        node_b->parent = parent_a;
    }

    float get_min_divider_limit(bsp_node* node, const e_magik_gui_split_order_types type, float l0, float l1, float min_span)
    {
        if(!node) return l0;
        
        if(is_leaf(node)) return l0 + min_span;

        if(type == node->type)
        {
            float d_sub = l0 + node->relative_children_divider_position * (l1 - l0);
            return get_min_divider_limit(node->child_b.get(), type, d_sub, l1, min_span);
        }
        else
        {
            float limit_a = get_min_divider_limit(node->child_a.get(), type, l0, l1, min_span);
            float limit_b = get_min_divider_limit(node->child_b.get(), type, l0, l1, min_span);
            return std::max(limit_a, limit_b);
        }
    }

    float get_max_divider_limit(bsp_node* node, const e_magik_gui_split_order_types type, float l0, float l1, float min_span)
    {
        if(!node) return l1;
        
        if(is_leaf(node)) return l1 - min_span;

        if(type == node->type)
        {
            float d_sub = l1 - (1.0f - node->relative_children_divider_position) * (l1 - l0);
            return get_max_divider_limit(node->child_a.get(), type, l0, d_sub, min_span);
        }
        else
        {
            float limit_a = get_max_divider_limit(node->child_a.get(), type, l0, l1, min_span);
            float limit_b = get_max_divider_limit(node->child_b.get(), type, l0, l1, min_span);
            return std::min(limit_a, limit_b);
        }
    }

    void countermove_children_in_branch(bsp_node* node, const e_magik_gui_split_order_types type, const float l0_old, const float l0_new, const float l1_old, const float l1_new)
    {
        if(!node || is_leaf(node)) return;

        if(type == node->type)
        {
            float old_size = l1_old - l0_old;
            float new_size = l1_new - l0_new;

            if (new_size < 0.0001f) new_size = 0.0001f;

            float d0 = l0_old + (node->relative_children_divider_position * old_size);
            float new_relative = (d0 - l0_new) / new_size;

            node->relative_children_divider_position = new_relative;

            float clamped_d0 = l0_new + (new_relative * new_size);

            countermove_children_in_branch(node->child_a.get(), type, l0_old, l0_new, d0, clamped_d0);
            countermove_children_in_branch(node->child_b.get(), type, d0, clamped_d0, l1_old, l1_new);

            return;
        }

        countermove_children_in_branch(node->child_a.get(), type, l0_old, l0_new, l1_old, l1_new);
        countermove_children_in_branch(node->child_b.get(), type, l0_old, l0_new, l1_old, l1_new);
    }

    void resize_node(bsp_node* node, const float new_relative_children_divider_position, const float application_size_x, const float application_size_y)
    {
        if(!node)
        {
            global_consol_data->mlog("[ Warning ] - Target node for resizing not found");
            return;
        }

        float clamped_relative_divider = new_relative_children_divider_position;
        float adjusted_relative_minimum = minimum_relative_children_divider_fraction;
        float min_absolute_size = minimum_relative_children_divider_fraction*std::max(application_size_x, application_size_y);

        switch(node->type)
        {
            case e_magik_gui_split_order_types::x_axis:
            {
                adjusted_relative_minimum = min_absolute_size/application_size_x;
                break;
            }

            case e_magik_gui_split_order_types::y_axis:
            {
                adjusted_relative_minimum = min_absolute_size/application_size_y;
                break;
            }
        }

        if(new_relative_children_divider_position > (1.0f - adjusted_relative_minimum))
        {
            clamped_relative_divider = 1.0f - adjusted_relative_minimum;
        }

        if(new_relative_children_divider_position < adjusted_relative_minimum)
        {
            clamped_relative_divider = adjusted_relative_minimum;
        }

        float old_relative_divider = node->relative_children_divider_position;
        float new_relative_divider = clamped_relative_divider;
        float old_div = node->relative_children_divider_position;

        float min_div = get_min_divider_limit(node->child_a.get(), node->type, 0.0f, old_div, adjusted_relative_minimum);
        float max_div = get_max_divider_limit(node->child_b.get(), node->type, old_div, 1.0f, adjusted_relative_minimum);

        if(min_div > max_div) { min_div = max_div = (min_div + max_div) * 0.5f; }

        float new_div = clamped_relative_divider;

        if(new_div < min_div) new_div = min_div;
        if(new_div > max_div) new_div = max_div;

        if (std::abs(new_div - old_div) < 0.0001f) return;

        node->relative_children_divider_position = new_div;

        countermove_children_in_branch(node->child_a.get(), node->type, 0.0f, 0.0f, old_div, new_div);
        countermove_children_in_branch(node->child_b.get(), node->type, old_div, new_div, 1.0f, 1.0f);

        return;
    }

    void copy_graph(bsp_node* reference_node, bsp_node* copy_node)
    {
        if(!reference_node || !copy_node) return;

        copy_node->type = reference_node->type;
        copy_node->relative_children_divider_position = reference_node->relative_children_divider_position;
        copy_node->id = reference_node->id;

        if(reference_node->child_a.get())
        {
            std::unique_ptr<bsp_node> copy_child_a = std::make_unique<bsp_node>();
            copy_node->child_a = std::move(copy_child_a);
            copy_node->child_a->parent = copy_node;

            copy_graph(reference_node->child_a.get(), copy_node->child_a.get());
        }

        if(reference_node->child_b.get())
        {
            std::unique_ptr<bsp_node> copy_child_b = std::make_unique<bsp_node>();
            copy_node->child_b = std::move(copy_child_b);
            copy_node->child_b->parent = copy_node;

            copy_graph(reference_node->child_b.get(), copy_node->child_b.get());
        }
    }

    void add_state()
    {
        states.push_back(bsp_state());
    }

    void add_state(bsp_state state)
    {
        states.push_back(std::move(state));
    }

    bsp_state copy_state(const unsigned int state_id)
    {
        if(state_id >= states.size())
        {
            global_consol_data->mlog("[ Warning ] - State id larger than number of states, returned empty state \n");
            return bsp_state();
        }

        bsp_state local_copy_state;

        copy_graph(states[state_id].root.get(), local_copy_state.root.get());

        for(size_t i = 0; i < states[state_id].windows.size(); i++)
        {
            local_copy_state.windows.push_back(states[state_id].windows[i]);
        }

        return local_copy_state;
    }

    void remove_state(const unsigned int state_id)
    {
        if(state_id >= states.size())
        {
            global_consol_data->mlog("[ Error ] - State id larger than number of states \n");
            return;
        }

        if((state_id == active_state_id) && states.size() > 1 && active_state_id > 0)
        {
            active_state_id--;
        }

        states.erase(states.begin() + state_id);
    }

    void set_active_state(const unsigned int state_id)
    {
        if(state_id >= states.size())
        {
            global_consol_data->mlog("[ Error ] - State id larger than number of states \n");
            return;
        }

        if(state_id == active_state_id)
        {
            global_consol_data->mlog("[ Warning ] - State is already active \n");
            return;
        }

        active_state_id = state_id;
        find_sibling_edges();
    }

    void set_minimum_relative_children_divider_fraction(const float val)
    {
        minimum_relative_children_divider_fraction = val;
    }

    void compute_absolute_bounds(bsp_node* node, const float current_size_x, const float current_size_y, const float current_position_x, const float current_position_y)
    {
        if(!node) return;

        node->abs_position_x = current_position_x;
        node->abs_position_y = current_position_y;
        node->abs_size_x = current_size_x;
        node->abs_size_y = current_size_y;

        if(is_leaf(node)) return;

        float position_a_x = current_position_x;
        float position_a_y = current_position_y;
        float size_a_x = current_size_x;
        float size_a_y = current_size_y;
        float position_b_x = current_position_x;
        float position_b_y = current_position_y;
        float size_b_x = current_size_x;
        float size_b_y = current_size_y;

        switch(node->type)
        {
            case e_magik_gui_split_order_types::x_axis:
            {
                size_a_x = current_size_x * node->relative_children_divider_position;
                size_b_x = current_size_x - size_a_x;
                position_b_x = current_position_x + size_a_x;
                break;
            }

            case e_magik_gui_split_order_types::y_axis:
            {
                size_a_y = current_size_y * node->relative_children_divider_position;
                size_b_y = current_size_y - size_a_y;
                position_b_y = current_position_y + size_a_y;
                break;
            }
        }

        if(node->child_a) compute_absolute_bounds(node->child_a.get(), size_a_x, size_a_y, position_a_x, position_a_y);
        if(node->child_b) compute_absolute_bounds(node->child_b.get(), size_b_x, size_b_y, position_b_x, position_b_y);
    }

    void add_window_to_state(const unsigned int state_id, bsp_window window, magik_gui_context_menu* context_menu)
    {
        if(state_id >= states.size()) 
        {
            global_consol_data->mlog("[ Warning ] - State id larger than number of states \n");
            return;
        }

        for(auto iter : states[state_id].windows)
        {
            if(iter.id == window.id)
            {
                global_consol_data->mlog("[ Error ] - id already bound \n");
                return;
            }
        }

        bsp_node* target = find_node(states[state_id].root.get(), window.id);

        if(!target)
        {
            global_consol_data->mlog("[ Warning ] - Window target node does not exist \n");
        }

        window.context_menu = context_menu;

        states[state_id].windows.push_back(window);
    }

    void remove_window_from_state(const unsigned int state_id, const unsigned int id)
    {
        if(state_id >= states.size()) 
        {
            global_consol_data->mlog("[ Warning ] - State id larger than number of states \n");
            return;
        }

        bool found = false;
        for(auto iter : states[state_id].windows)
        {
            if(iter.id == id)
            {
                found = true;
                break;
            }
        }

        if(!found)
        {
            global_consol_data->mlog("[ Error ] - id not found \n");
            return;
        }

        states[state_id].windows.erase(states[state_id].windows.begin() + id);
    }

    void count_node(bsp_node* node, int& total)
    {
        if(!node) return;

        total++;

        count_node(node->child_a.get(), total);
        count_node(node->child_b.get(), total);
    }

    int get_n_node(const unsigned int state_id)
    {
        if(state_id >= states.size()) 
        {
            global_consol_data->mlog("[ Warning ] - State id larger than number of states \n");
            return 0;
        }

        int total = 0;
        count_node(states[state_id].root.get(), total);
        return total;
    }

    void add_node_to_visualization(bsp_node* node, std::vector<bsp_graph_vis>& vis, float offset_x, float offset_y, float depth)
    {
        if(!node) return;

        bsp_graph_vis tmp;
        tmp.is_root = is_root(node);
        tmp.is_leaf = is_leaf(node);
        tmp.relative_position_to_root_x = offset_x;
        tmp.relative_position_to_root_y = offset_y;

        vis.push_back(tmp);

        add_node_to_visualization(node->child_a.get(), vis, (offset_x + 1.0f)*(1.0f+depth), offset_y + 1.0f, depth + 1.0f);
        add_node_to_visualization(node->child_b.get(), vis, (offset_x - 1.0f)*(1.0f+depth), offset_y + 1.0f, depth + 1.0f);
    }

    void generate_graph_visualization(const unsigned int state_id, std::vector<bsp_graph_vis>& vis)
    {
        if(state_id >= states.size()) 
        {
            global_consol_data->mlog("[ Warning ] - State id larger than number of states \n");
            return;
        }

        add_node_to_visualization(states[state_id].root.get(), vis, 0.0f, 0.0f, 0.0f);
    }
};

struct magik_gui_global_data
{
    ImFont* gui_font = nullptr;
    float gui_size = 0.0f;

    magik_gui_image picture_asset;
    console_data c_log;
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

static void replace_magik_gui_image(magik_gui_image& image, const char* filename)
{
    if (image.buffer != 0)
    {
        glDeleteTextures(1, &image.buffer);
    }

    image = load_magik_gui_image(filename);
}

static void magik_gui_setup_global_data(ImFont* gui_font, float gui_size)
{
    global_data->gui_font = gui_font;
    global_data->gui_size = gui_size;

    global_data->picture_asset;
    global_data->c_log = console_data();
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

static void magik_gui_window(const char* title, ImGuiWindowFlags flags, const ImVec2 window_pos, const ImVec2 window_size, unsigned int window_id, action_caller user_function, void* user_data, magik_gui_staging_area_manager* staging_area_manager, magik_gui_context_menu* context_menu)
{
    style_begin();
    ImGui::SetNextWindowPos(window_pos);
    ImGui::SetNextWindowSize(window_size);
    ImGui::Begin(title, nullptr, flags);

    if(staging_area_manager) staging_area_manager->show(window_id);
    
    if(user_function) user_function(user_data, context_menu);


    style_end();
    ImGui::End();
}

static void magik_gui_titlebar(GLFWwindow* window, bsp_graph_manager& graph_manager)
{
    if (ImGui::BeginMainMenuBar()) 
    {
	    if (ImGui::BeginMenu("Help"))
	    {
		    if(ImGui::MenuItem("Exit"))
		    {
			    glfwSetWindowShouldClose(window, GLFW_TRUE);
		    }

            if(ImGui::MenuItem("Reset layout"))
            {
                graph_manager.add_state(std::move(graph_manager.copy_state(1)));
                graph_manager.remove_state(0);
            }
        
		    ImGui::EndMenu();
	    }
        
	    ImGui::EndMainMenuBar();
    }
}

static void magik_gui_draw_panels(bsp_graph_manager& graph_manager, magik_gui_staging_area_manager* staging_area_manager)
{
    ImVec2 root_size = ImGui::GetMainViewport()->WorkSize;
    ImVec2 root_position = ImGui::GetMainViewport()->WorkPos;

    graph_manager.compute_absolute_bounds(graph_manager.states[graph_manager.active_state_id].root.get(), root_size.x, root_size.y, root_position.x, root_position.y);

    for(auto iter : graph_manager.states[graph_manager.active_state_id].windows)
    {
        bsp_node* window_node = graph_manager.find_node(graph_manager.states[graph_manager.active_state_id].root.get(), iter.id);
        magik_gui_window(iter.title, iter.flags, ImVec2(window_node->abs_position_x, window_node->abs_position_y), ImVec2(window_node->abs_size_x, window_node->abs_size_y), iter.id, iter.user_function, iter.user_data, staging_area_manager, iter.context_menu);
    }
}

static void magik_gui_panel_sliders(bsp_graph_manager& graph_manager)
{
    float box_height = 5.0f;
    float box_width = 100.0f;
    static int active_splitter_index = -1;

    if(!ImGui::IsMouseDown(ImGuiMouseButton_Left)) 
    {
        active_splitter_index = -1;
    }

    graph_manager.find_sibling_edges();
    for(size_t i = 0; i < graph_manager.sibling_edges.size(); i++)
    {
        auto* iter = graph_manager.sibling_edges[i];
        ImVec2 box_max, box_min;
        ImVec2 centroid;

        switch(iter->type)
        {
            case e_magik_gui_split_order_types::x_axis:
            {
                centroid.x = iter->abs_position_x + iter->abs_size_x * iter->relative_children_divider_position;
                centroid.y = (iter->abs_position_y + iter->abs_position_y + iter->abs_size_y) * 0.5f;
                box_max.x = centroid.x + box_height;
                box_max.y = centroid.y + box_width;
                box_min.x = centroid.x - box_height;
                box_min.y = centroid.y - box_width;

                if((iter->abs_size_y / std::fabsf(box_max.y - box_min.y)) < 1.0f)
                {
                    box_max.y = centroid.y + (iter->abs_size_y/4.0f);
                    box_min.y = centroid.y - (iter->abs_size_y/4.0f);
                }

                break;
            }
            case e_magik_gui_split_order_types::y_axis:
            {
                centroid.x = (iter->abs_position_x + iter->abs_position_x + iter->abs_size_x) * 0.5f;
                centroid.y = iter->abs_position_y + iter->abs_size_y * iter->relative_children_divider_position;
                box_max.x = centroid.x + box_width;
                box_max.y = centroid.y + box_height;
                box_min.x = centroid.x - box_width;
                box_min.y = centroid.y - box_height;

                if((iter->abs_size_x / std::fabsf(box_max.x - box_min.x)) < 1.0f)
                {
                    box_max.x = centroid.x + (iter->abs_size_x/4.0f);
                    box_min.x = centroid.x - (iter->abs_size_x/4.0f);
                }

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
                    new_anchor = (ImGui::GetMousePos().x - iter->abs_position_x) / iter->abs_size_x;
                    break;
                }
                case e_magik_gui_split_order_types::y_axis:
                {
                    new_anchor = (ImGui::GetMousePos().y - iter->abs_position_y) / iter->abs_size_y;
                    break;
                }
            }

            ImVec2 root_size = ImGui::GetMainViewport()->WorkSize;
            graph_manager.resize_node(iter, new_anchor, root_size.x, root_size.y);
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

/*
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

        //magik_gui_show_interactive_elements(slider, colorpicker, material_type_dropdown, material_type_list);

        if (ImGui::Button("Load image"))
        {
            nfdu8char_t* outPath = nullptr;

            nfdu8filteritem_t filters[] = {
                { "Images", "png,jpg,jpeg" }
            };

            nfdopendialogu8args_t args = {0};

            args.filterList = filters;
            args.filterCount = 1;

            nfdresult_t result = NFD_OpenDialogU8_With(&outPath, &args);

            const char* log_message;

            if (result == NFD_OKAY)
            {
                replace_magik_gui_image(global_data->picture_asset, outPath);
                
                global_data->c_log.mlog("Loaded %s\n", outPath);
                
                NFD_FreePathU8(outPath);
            }
            else if (result == NFD_CANCEL)
            {
                global_data->c_log.mlog("Image loading cancelled\n");
            }
            else
            {
                global_data->c_log.mlog("Error loading %s: %s\n", outPath, NFD_GetError());
            }

            
        }

        ImGui::EndPopup();
    }
}
*/

// ### Panel functions ###
static void console_function(void* user_data, magik_gui_context_menu* context_menu)
{
    global_consol_data->mprint();

    if (context_menu && context_menu->action) context_menu->action(context_menu);
}

static void display_function(void* user_data, magik_gui_context_menu* context_menu)
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

    if (context_menu && context_menu->action) context_menu->action(context_menu);

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

static void stats_function(void* user_data, magik_gui_context_menu* context_menu)
{
    // Print out the leaf node sizes 
    ImGuiTextBuffer buffer;
    // append_binary_tree(global_data->layout_state.root.get(), buffer);
    ImGui::BeginChild("ScrollingRegion", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::TextUnformatted(buffer.begin(), buffer.end());

    if(ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) 
    {
        ImGui::SetScrollHereY(1.0f);
    }

    if (context_menu && context_menu->action) context_menu->action(context_menu);

    ImGui::EndChild();
}

int main()
{
    const int initial_height = 1100;
    const int initial_width = 2000;
    GLFWwindow* window;
    if(!magik_gui_setup_glfw(window, initial_height, initial_width)) return -1;

    if(NFD_Init() != NFD_OKAY)
    {
        std::cerr << "Failed to initialize NFD\n";
        return -1;
    }

    ImFont* gui_font;
    float gui_scale;
    magik_gui_setup_imgui(window, gui_font, gui_scale);

    magik_gui_setup_global_data(gui_font, gui_scale);


    magik_gui_context_menu render_context_menu{
        "Test_context_menu",
        magik_gui_show_context_menu,
        0.2f
    };

    render_context_menu.staging_area->add_colorpicker(
        "Test_colorpicker",
        magik_gui_show_colorpicker,
        ImVec4(0.0f, 0.0f, 0.0f, 1.0f)
    );

    magik_gui_context_menu stats_context_menu{
        "Test_settings_context_menu",
        magik_gui_show_context_menu,
        0.5f
    };

    stats_context_menu.staging_area->add_checkbox(
        "Test_checkbox",
        magik_gui_show_checkbox,
        false
    );

    bsp_graph_manager graph_manager;
    graph_manager.add_state();
    graph_manager.set_active_state(0);
    graph_manager.split_node(graph_manager.active_state_id, 0, e_magik_gui_split_order_types::x_axis, 0.8f, 1);
    graph_manager.split_node(graph_manager.active_state_id, 0, e_magik_gui_split_order_types::y_axis, 0.75f, 2);
    graph_manager.split_node(graph_manager.active_state_id, 1, e_magik_gui_split_order_types::y_axis, 0.75f, 3);
    graph_manager.split_node(graph_manager.active_state_id, 1, e_magik_gui_split_order_types::y_axis, 0.2f, 4);
    //graph_manager.split_node(graph_manager.active_state_id, 1, e_magik_gui_split_order_types::y_axis, 0.2f, 5);

    graph_manager.add_window_to_state(graph_manager.active_state_id, bsp_window{"Render viewport", ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse, display_function, nullptr, 0}, &render_context_menu);
    graph_manager.add_window_to_state(graph_manager.active_state_id, bsp_window{"Scene graph", ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse, nullptr, nullptr, 1}, nullptr);
    graph_manager.add_window_to_state(graph_manager.active_state_id, bsp_window{"Console", ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse, console_function, nullptr, 2}, nullptr);
    graph_manager.add_window_to_state(graph_manager.active_state_id, bsp_window{"Statistics", ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse, stats_function, nullptr, 3}, &stats_context_menu);
    graph_manager.add_window_to_state(graph_manager.active_state_id, bsp_window{"Settings", ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse, nullptr, nullptr, 4}, nullptr);
    //graph_manager.add_window_to_state(graph_manager.active_state_id, bsp_window{"Test", ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse, console_function, nullptr, 5});

    graph_manager.add_state(std::move(graph_manager.copy_state(graph_manager.active_state_id)));

    magik_gui_staging_area_manager staging_area_manager;

    std::vector<float> settings_staging_area_heights{500.0f};

    staging_area_manager.add_staging_areas(4, 1, settings_staging_area_heights);

    staging_area_manager.get_staging_area(4, 0)->add_colorpicker(
        "Test_colorpicker_settings_window",
        magik_gui_show_colorpicker,
        ImVec4(1.0f, 1.0f, 1.0f, 1.0f)
    );

    staging_area_manager.get_staging_area(4, 0)->add_list<const char*>(
        "Test_settings_list",
        magik_gui_show_list<const char*>,
        std::vector<const char*>{"Material1", "Matieral", "Material3", "Material4", "Material6", "Matierla7"},
        +[](const char* const& element) -> const char* {
            return element;
        },
        e_magik_gui_list_type::List
    );

    staging_area_manager.get_staging_area(4, 0)->add_button(
        "Test_settings_button",
        magik_gui_show_button,
        ImVec2(100.0f, 100.0f)
    );

    staging_area_manager.get_staging_area(4, 0)->add_slider(
        "Test_settings_slider",
        magik_gui_show_slider,
        0,
        0,
        1,
        0.9f
    );

    std::vector<float> staging_area_heights{500.0f, 200.0f, 200.0f};

    staging_area_manager.add_staging_areas(3, 3, staging_area_heights);
    
    magik_gui_dropdown_menu& stats_dropdown = staging_area_manager.get_staging_area(3, 1)->add_dropdown_menu(
        "Test_dropdown_menu",
        magik_gui_show_dropdown_menu
    );

    staging_area_manager.add_staging_areas(1, 3, staging_area_heights);

    staging_area_manager.get_staging_area(1, 0)->add_checkbox(
        "Test_checkbox_render_window",
        magik_gui_show_checkbox,
        false
    );

    staging_area_manager.get_staging_area(1, 2)->add_colorpicker(
        "Test_colorpicker_scene_graph",
        magik_gui_show_colorpicker,
        ImVec4(1.0f, 1.0f, 1.0f, 1.0f)
    );

    stats_dropdown.staging_area->add_button(
        "Test_button_in_dropdown",
        magik_gui_show_button,
        ImVec2(100.0f, 100.0f)
    );

    struct material
    {
        const char* name;
        float ior;

        static const char* get_name(const material& m) { return m.name; }
    };

    material gold{"Gold", 2.5};
    material glass{"Glass", 1.5};

    std::vector<material> material_list{gold, glass};

    stats_dropdown.staging_area->add_list<material>(
        "Test_material_list",
        magik_gui_show_list<material>,
        material_list,
        material::get_name,
        e_magik_gui_list_type::Dropdown
    );

    staging_area_manager.get_staging_area(3, 0)->add_colorpicker(
        "Test_colorpicker",
        magik_gui_show_colorpicker,
        ImVec4(0.0f, 0.0f, 0.0f, 1.0f)
    );

    staging_area_manager.get_staging_area(3, 2)->add_slider(
        "Test_slider2",
        magik_gui_show_slider,
        0.0f,
        -1.0f,
        1.0f,
        1.0f
    );

    staging_area_manager.get_staging_area(3, 0)->add_list<material>(
        "Test_list_selectable",
        magik_gui_show_list<material>,
        material_list,
        material::get_name,
        e_magik_gui_list_type::List
    );



    uint32_t MAJOR = 0, MINOR = 0, REVISION = 0;
    const char* AS_CHAR;
    magik_get_version(&MAJOR, &MINOR, &REVISION, &AS_CHAR);
    global_consol_data->mlog(AS_CHAR);

	while (!glfwWindowShouldClose(window))
	{
        magik_gui_new_frame(window);

        magik_gui_titlebar(window, graph_manager);

        magik_gui_draw_panels(graph_manager, &staging_area_manager);

        magik_gui_panel_sliders(graph_manager);

        magik_gui_render(window);
	}

	return 0;
}