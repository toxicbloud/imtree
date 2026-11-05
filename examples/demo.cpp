#include "imgui_tree_graph/TreeRenderer.hpp"

#include <GLFW/glfw3.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>

namespace {

using namespace ImGuiTreeGraph;

std::unique_ptr<BuchheimNode> MakeOverview() {
    uint32_t next_id = 0;
    auto make_node = [&](const char* title, const char* metrics, ImU32 fill) {
        auto node = std::make_unique<BuchheimNode>();
        node->id = next_id++;
        node->shape = NodeShape::RoundedRectangle;
        node->primary_label = title;
        node->secondary_label = metrics;
        node->fill_color = fill;
        node->border_color = IM_COL32(147, 168, 189, 255);
        node->edge_from_parent.color = IM_COL32(120, 146, 174, 255);
        node->edge_from_parent.thickness = 1.5f;
        return node;
    };
    const ImU32 state = IM_COL32(44, 75, 110, 255);
    const ImU32 action = IM_COL32(42, 91, 105, 255);
    const ImU32 good = IM_COL32(48, 96, 76, 255);
    const ImU32 risk = IM_COL32(113, 75, 56, 255);
    auto root = make_node("Position", "N=1200\nQ=0.68", state);
    root->shape = NodeShape::Circle;
    auto advance = make_node("Advance", "N=720  Q=0.81\nP=0.50", action);
    advance->edge_from_parent.label = "P=0.50";
    auto defend = make_node("Defend", "N=300  Q=0.55\nP=0.30", action);
    defend->edge_from_parent.label = "P=0.30";
    auto explore = make_node("Explore", "N=180  Q=0.38\nP=0.20", action);
    explore->edge_from_parent.label = "P=0.20";
    auto add_outcome = [&](BuchheimNode& parent, const char* title, const char* metrics,
                           const char* probability, ImU32 fill) {
        auto outcome = make_node(title, metrics, fill);
        outcome->edge_from_parent.label = probability;
        parent.add_child(std::move(outcome));
    };
    add_outcome(*advance, "Gain ground", "N=540\nQ=0.92", "p=0.75", good);
    add_outcome(*advance, "Meet resistance", "N=180\nQ=0.48", "p=0.25", risk);
    add_outcome(*defend, "Hold position", "N=240\nQ=0.63", "p=0.80", good);
    add_outcome(*defend, "Lose tempo", "N=60\nQ=0.23", "p=0.20", risk);
    add_outcome(*explore, "Find a route", "N=72\nQ=0.70", "p=0.40", good);
    add_outcome(*explore, "Dead end", "N=108\nQ=0.17", "p=0.60", risk);
    root->add_child(std::move(advance));
    root->add_child(std::move(defend));
    root->add_child(std::move(explore));
    return root;
}

BuchheimNode* FindNode(BuchheimNode* root, uint32_t id) {
    if (root->id == id) return root;
    for (auto& child : root->children)
        if (auto* found = FindNode(child.get(), id)) return found;
    return nullptr;
}

unsigned VisibleCount(const BuchheimNode& node) {
    unsigned count = 1;
    if (!node.collapsed)
        for (const auto& child : node.children) count += VisibleCount(*child);
    return count;
}

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
        for (int i = 0; i < count; ++i) node->add_child(MakeTree(depth + 1, max_depth, next_id));
    }
    return node;
}

void ErrorCallback(int code, const char* text) { std::fprintf(stderr, "GLFW %d: %s\n", code, text); }

} // namespace

int main(int argc, char** argv) {
    int frame_limit = 0;
    int scene = 0;
    bool root_view = false, collapse_first = false;
    bool seen_frames = false, seen_scene = false, seen_view = false;
    auto usage = [&]() {
        std::fprintf(stderr, "Usage: %s [--frames positive-integer] "
                     "[--scene overview|large] [--view fit|root] [--collapse-first]\n", argv[0]);
        return 2;
    };
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--collapse-first" && !collapse_first) {
            collapse_first = true;
            continue;
        }
        if (i + 1 == argc) return usage();
        const std::string_view value(argv[++i]);
        if (arg == "--frames" && !seen_frames) {
            seen_frames = true;
            const auto result = std::from_chars(value.data(), value.data() + value.size(), frame_limit);
            if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || frame_limit <= 0)
                return usage();
        } else if (arg == "--scene" && !seen_scene) {
            seen_scene = true;
            if (value != "overview" && value != "large") return usage();
            scene = value == "large" ? 1 : 0;
        } else if (arg == "--view" && !seen_view) {
            seen_view = true;
            if (value != "fit" && value != "root") return usage();
            root_view = value == "root";
        } else return usage();
    }
    if (collapse_first && scene != 0) return usage();
    glfwSetErrorCallback(ErrorCallback);
    if (!glfwInit()) return 1;
    const char* glsl = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    GLFWwindow* window = glfwCreateWindow(1440, 900, "imgui_tree_graph - Search explorer", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(frame_limit == 0 ? 1 : 0);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImFontConfig font_config;
    font_config.SizePixels = 18.0f;
    ImGui::GetIO().Fonts->AddFontDefault(&font_config);
    auto& style = ImGui::GetStyle();
    style.WindowPadding = {20, 16};
    style.FramePadding = {10, 6};
    style.ItemSpacing = {12, 8};
    style.FrameRounding = 4;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.075f, 0.09f, 0.115f, 1);
    style.Colors[ImGuiCol_Button] = ImVec4(0.17f, 0.25f, 0.33f, 1);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.17f, 0.23f, 1);
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
    std::unique_ptr<BuchheimNode> tree;
    ImGuiTreeGraph::LayoutConfig layout;
    ImGuiTreeGraph::TreeRenderer renderer;
    ImGuiTreeGraph::RendererConfig config;
    config.show_grid = false;
    config.minimap_corner = MinimapCorner::TopRight;
    config.minimap_size = {176, 112};
    BuchheimNode* selected = nullptr;
    ImU32 selected_border = 0;
    auto select = [&](BuchheimNode* node) {
        if (selected) selected->border_color = selected_border;
        selected = node;
        if (selected) {
            selected_border = selected->border_color;
            selected->border_color = IM_COL32(249, 205, 115, 255);
        }
    };
    auto load_scene = [&]() {
        selected = nullptr;
        count = 0;
        layout = {};
        if (scene == 0) {
            tree = MakeOverview();
            count = 10;
            layout.node_width = 164;
            layout.node_height = 104;
            layout.sibling_distance = 34;
            layout.subtree_distance = 44;
            layout.level_height = 180;
        } else {
            tree = MakeTree(0, 8, count);
            layout.node_width = 120;
            layout.node_height = 100;
            layout.level_height = 155;
        }
        if (collapse_first && scene == 0) {
            tree->children.front()->collapsed = true;
            select(tree->children.front().get());
        }
        ImGuiTreeGraph::ComputeBuchheimLayout(tree.get(), layout);
    };
    load_scene();
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
        ImGui::TextUnformatted("imgui_tree_graph / Search explorer");
        ImGui::SameLine();
        ImGui::TextDisabled("  Dear ImGui + Buchheim layout");
        ImGui::TextDisabled("MCTS decisions at a glance. Compare actions, inspect outcomes, follow the search.");
        ImGui::Spacing();
        ImGui::SetNextItemWidth(240);
        if (ImGui::Combo("##scene", &scene, "Overview / 10 nodes\0Large search / 9 levels\0")) {
            collapse_first = false;
            root_view = false;
            load_scene();
            first_frame = true;
        }
        ImGui::SameLine();
        bool fit_requested = ImGui::Button("Fit entire tree");
        ImGui::SameLine();
        bool root_requested = ImGui::Button("Root (1:1)");
        ImGui::SameLine(); ImGui::Checkbox("Grid", &config.show_grid);
        ImGui::SameLine(); ImGui::Checkbox("Mini-map", &config.show_minimap);
        ImGui::SameLine(); ImGui::Checkbox("Click collapses", &config.collapse_on_click);
        ImGui::TextDisabled("%u / %u nodes visible  |  %s  |  Pan: right/middle drag  |  Zoom: wheel",
                            VisibleCount(*tree), count,
                            scene == 0 ? "3 levels / illustrative search" : "9 levels / generated stress test");
        ImGui::Separator();
        // Reserve a stable inspector so selecting a node never moves the canvas.
        ImGui::BeginChild("tree canvas", ImVec2(0, -82), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        if (first_frame) {
            fit_requested = !root_view;
            root_requested = root_view;
            first_frame = false;
        }
        if (fit_requested) renderer.fit_to_tree(tree.get(), ImGui::GetContentRegionAvail(), layout);
        if (root_requested) {
            renderer.camera().zoom = 1;
            renderer.camera().pan = {ImGui::GetContentRegionAvail().x * 0.5f, 90};
        }
        renderer.render("mcts", tree.get(), layout, config);
        if (const auto* clicked = renderer.interaction().clicked)
            select(FindNode(tree.get(), clicked->id));
        if (const auto* hovered = renderer.interaction().hovered) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(hovered->primary_label.c_str());
            ImGui::TextUnformatted(hovered->secondary_label.c_str());
            if (!hovered->children.empty())
                ImGui::TextDisabled("Click to %s %zu children", hovered->collapsed ? "expand" : "collapse",
                                    hovered->children.size());
            ImGui::EndTooltip();
        }
        ImGui::EndChild();
        ImGui::Separator();
        if (selected) {
            ImGui::TextColored(ImVec4(0.98f, 0.81f, 0.45f, 1), "INSPECT / %s", selected->primary_label.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("#%u  |  %zu children  |  %s", selected->id, selected->children.size(),
                                selected->children.empty() ? "outcome" : selected->collapsed ? "collapsed" : "expanded");
            std::string metrics = selected->secondary_label;
            std::replace(metrics.begin(), metrics.end(), '\n', ' ');
            ImGui::TextUnformatted(metrics.c_str());
        } else {
            ImGui::TextUnformatted("INSPECT / Click a node to select it");
            ImGui::TextDisabled("N = visits   Q = mean value   P = action prior   p = outcome probability");
        }
        ImGui::SameLine();
        ImGui::TextDisabled("  |  Zoom %.2fx", renderer.camera().zoom);
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
