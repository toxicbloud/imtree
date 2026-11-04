#include "imgui_tree_graph/TreeRenderer.hpp"

#include <GLFW/glfw3.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <charconv>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>

namespace {

std::unique_ptr<ImGuiTreeGraph::BuchheimNode> MakeTree(int depth, int max_depth, uint32_t& next_id) {
    using namespace ImGuiTreeGraph;
    auto node = std::make_unique<BuchheimNode>();
    node->id = next_id++;
    node->shape = depth % 2 == 0 ? NodeShape::Circle : NodeShape::RoundedRectangle;
    node->player_id = depth % 5 == 4 ? -1 : static_cast<int8_t>((depth / 2) % 2);
    node->primary_label = (depth % 2 == 0 ? "State " : "Action ") + std::to_string(node->id);
    node->secondary_label = "N=" + std::to_string(1000 / (depth + 1)) + "\nQ=0.62 P=0.33";
    node->edge_from_parent.label = depth % 2 == 0 ? "p=0.65" : "a=" + std::to_string(node->id % 3);
    if (depth < max_depth) {
        const int count = depth % 2 == 0 ? 3 : 2;
        for (int i = 0; i < count; ++i) node->children.push_back(MakeTree(depth + 1, max_depth, next_id));
    }
    return node;
}

void ErrorCallback(int code, const char* text) { std::fprintf(stderr, "GLFW %d: %s\n", code, text); }

} // namespace

int main(int argc, char** argv) {
    int frame_limit = 0;
    if (argc == 3 && std::string_view(argv[1]) == "--frames") {
        const std::string_view value(argv[2]);
        const auto result = std::from_chars(value.data(), value.data() + value.size(), frame_limit);
        if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || frame_limit <= 0) return 2;
    } else if (argc != 1) {
        std::fprintf(stderr, "Usage: %s [--frames positive-integer]\n", argv[0]);
        return 2;
    }
    glfwSetErrorCallback(ErrorCallback);
    if (!glfwInit()) return 1;
#ifdef __APPLE__
    const char* glsl = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#else
    const char* glsl = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif
    GLFWwindow* window = glfwCreateWindow(1440, 900, "imgui_tree_graph - MCTS", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(frame_limit == 0 ? 1 : 0);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();
    const bool glfw_ready = ImGui_ImplGlfw_InitForOpenGL(window, true);
    const bool gl_ready = glfw_ready && ImGui_ImplOpenGL3_Init(glsl);
    if (!gl_ready) {
        if (glfw_ready) ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    uint32_t count = 0;
    auto tree = MakeTree(0, 8, count);
    ImGuiTreeGraph::LayoutConfig layout;
    layout.node_width = 120;
    layout.node_height = 100;
    layout.level_height = 155;
    ImGuiTreeGraph::ComputeBuchheimLayout(tree.get(), layout);
    ImGuiTreeGraph::TreeRenderer renderer;
    ImGuiTreeGraph::RendererConfig config;
    bool first_frame = true;
    int frames = 0;
    while (!glfwWindowShouldClose(window) && (frame_limit == 0 || frames < frame_limit)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::Begin("Search tree", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                            ImGuiWindowFlags_NoSavedSettings);
        ImGui::Text("MCTS: %u nodes | Right/middle drag: pan | Wheel: zoom | Left click: collapse", count);
        if (ImGui::Button("Fit entire tree")) renderer.fit_to_tree(tree.get(), ImGui::GetContentRegionAvail(), layout);
        ImGui::SameLine();
        if (ImGui::Button("Root (1:1)")) {
            renderer.camera().zoom = 1;
            renderer.camera().pan = {ImGui::GetContentRegionAvail().x * 0.5f, 90};
        }
        ImGui::SameLine(); ImGui::Checkbox("Grid", &config.show_grid);
        ImGui::SameLine(); ImGui::Checkbox("Mini-map", &config.show_minimap);
        ImGui::SameLine(); ImGui::Text("Zoom %.2f", renderer.camera().zoom);
        if (first_frame) {
            renderer.camera().pan = {ImGui::GetContentRegionAvail().x * 0.5f, 90};
            first_frame = false;
        }
        renderer.render("mcts", tree.get(), layout, config);
        ImGui::End();
        ImGui::Render();
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        glViewport(0, 0, width, height);
        glClearColor(0.08f, 0.09f, 0.11f, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
        ++frames;
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
