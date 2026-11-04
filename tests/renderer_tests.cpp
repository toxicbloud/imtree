#include "imgui_tree_graph/TreeRenderer.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace {
using namespace ImGuiTreeGraph;

std::size_t allocations = 0;
void* Allocate(std::size_t size, void*) {
    ++allocations;
    return std::malloc(size);
}
void Free(void* ptr, void*) { std::free(ptr); }

void Check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}

bool Near(float a, float b) { return std::abs(a - b) < 0.02f; }

struct FrameResult {
    ImVec2 origin;
    ImVec2 size;
    int vertices;
    bool changed;
};

FrameResult Frame(TreeRenderer& renderer, BuchheimNode* root,
                  const LayoutConfig& layout, const RendererConfig& config) {
    ImGui::NewFrame();
    ImGui::SetNextWindowPos({80, 60});
    ImGui::SetNextWindowSize({640, 480});
    ImGui::Begin("test", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size = ImGui::GetContentRegionAvail();
    const int before = ImGui::GetWindowDrawList()->VtxBuffer.Size;
    const bool changed = renderer.render("tree", root, layout, config);
    const int vertices = ImGui::GetWindowDrawList()->VtxBuffer.Size - before;
    ImGui::End();
    ImGui::Render();
    return {origin, size, vertices, changed};
}

} // namespace

int main() {
    IMGUI_CHECKVERSION();
    ImGui::SetAllocatorFunctions(Allocate, Free);
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.DisplaySize = {1024, 768};
    io.DeltaTime = 1.0f / 60.0f;
    io.IniFilename = nullptr;
    io.ConfigInputTrickleEventQueue = false;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    LayoutConfig layout;
    RendererConfig config;
    config.show_grid = false;
    config.show_minimap = false;
    auto root = std::make_unique<BuchheimNode>();
    root->children.push_back(std::make_unique<BuchheimNode>());
    root->children.push_back(std::make_unique<BuchheimNode>());
    ComputeBuchheimLayout(root.get(), layout);
    TreeRenderer renderer;
    renderer.camera().pan = {260, 100};
    Frame(renderer, root.get(), layout, config);
    const auto frame = Frame(renderer, root.get(), layout, config);

    // Mouse-local coordinates, not global screen coordinates, must anchor zoom.
    const ImVec2 mouse{frame.origin.x + 320, frame.origin.y + 180};
    io.AddMousePosEvent(mouse.x, mouse.y);
    Frame(renderer, root.get(), layout, config);
    const Camera before_zoom = renderer.camera();
    const ImVec2 world_before{(mouse.x - frame.origin.x - before_zoom.pan.x) / before_zoom.zoom,
                              (mouse.y - frame.origin.y - before_zoom.pan.y) / before_zoom.zoom};
    io.AddMouseWheelEvent(0, 1);
    Frame(renderer, root.get(), layout, config);
    const Camera after_zoom = renderer.camera();
    Check(after_zoom.zoom > before_zoom.zoom, "wheel must zoom");
    Check(Near((mouse.x - frame.origin.x - after_zoom.pan.x) / after_zoom.zoom, world_before.x) &&
          Near((mouse.y - frame.origin.y - after_zoom.pan.y) / after_zoom.zoom, world_before.y),
          "zoom must remain anchored under cursor in an offset canvas");

    io.AddMouseButtonEvent(ImGuiMouseButton_Middle, true);
    Frame(renderer, root.get(), layout, config);
    const Camera before_pan = renderer.camera();
    io.AddMousePosEvent(mouse.x + 17, mouse.y + 11);
    Frame(renderer, root.get(), layout, config);
    Check(Near(renderer.camera().pan.x, before_pan.pan.x + 17) &&
          Near(renderer.camera().pan.y, before_pan.pan.y + 11), "middle drag must pan");
    io.AddMouseButtonEvent(ImGuiMouseButton_Middle, false);
    Frame(renderer, root.get(), layout, config);
    io.AddMouseButtonEvent(ImGuiMouseButton_Right, true);
    Frame(renderer, root.get(), layout, config);
    const Camera before_right_pan = renderer.camera();
    io.AddMousePosEvent(mouse.x + 24, mouse.y + 16);
    Frame(renderer, root.get(), layout, config);
    Check(Near(renderer.camera().pan.x, before_right_pan.pan.x + 7) &&
          Near(renderer.camera().pan.y, before_right_pan.pan.y + 5), "right drag must pan");
    io.AddMouseButtonEvent(ImGuiMouseButton_Right, false);
    Frame(renderer, root.get(), layout, config);

    const Camera before_click = renderer.camera();
    io.AddMousePosEvent(frame.origin.x + before_click.pan.x, frame.origin.y + before_click.pan.y);
    Frame(renderer, root.get(), layout, config);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    const auto collapsed = Frame(renderer, root.get(), layout, config);
    Check(collapsed.changed && root->collapsed, "node click must collapse immediately");
    Check(Near(renderer.camera().pan.x, before_click.pan.x) &&
          Near(renderer.camera().pan.y, before_click.pan.y) &&
          Near(renderer.camera().zoom, before_click.zoom), "collapse must preserve camera");
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    Frame(renderer, root.get(), layout, config);

    config.show_minimap = true;
    const ImVec2 map_center{frame.origin.x + frame.size.x - config.minimap_margin - config.minimap_size.x * 0.5f,
                            frame.origin.y + frame.size.y - config.minimap_margin - config.minimap_size.y * 0.5f};
    io.AddMousePosEvent(map_center.x, map_center.y);
    Frame(renderer, root.get(), layout, config);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    Frame(renderer, root.get(), layout, config);
    Check(root->collapsed, "minimap click must not expand an underlying node");
    Check(Near(renderer.camera().pan.x, frame.size.x * 0.5f) &&
          Near(renderer.camera().pan.y, frame.size.y * 0.5f), "minimap center must center root");
    const Camera map_camera = renderer.camera();
    io.AddMousePosEvent(map_center.x + 15, map_center.y);
    Frame(renderer, root.get(), layout, config);
    Check(renderer.camera().pan.x < map_camera.pan.x, "minimap drag must move camera");
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    Frame(renderer, root.get(), layout, config);

    // With no grid/minimap, an offscreen tree must submit exactly the empty-canvas geometry.
    config.show_minimap = false;
    renderer.camera().pan = {-100000, -100000};
    root->primary_label = "offscreen";
    root->children.front()->edge_from_parent.label = "also offscreen";
    const auto empty = Frame(renderer, nullptr, layout, config);
    const auto outside = Frame(renderer, root.get(), layout, config);
    Check(empty.vertices == outside.vertices, "offscreen nodes must not submit geometry");
    root->collapsed = false;
    ComputeBuchheimLayout(root.get(), layout);
    const auto edges_outside = Frame(renderer, root.get(), layout, config);
    Check(empty.vertices == edges_outside.vertices, "offscreen edges/badges must not submit geometry");
    renderer.camera().pan = {260, 100};
    const auto visible = Frame(renderer, root.get(), layout, config);
    Check(visible.vertices > empty.vertices, "onscreen tree must submit geometry");
    renderer.camera().zoom = 0;
    Frame(renderer, nullptr, layout, config);
    Check(renderer.camera().zoom > 0, "zero zoom must be repaired");
    for (const auto corner : {MinimapCorner::TopLeft, MinimapCorner::TopRight,
                              MinimapCorner::BottomLeft, MinimapCorner::BottomRight}) {
        config.show_minimap = true;
        config.minimap_corner = corner;
        Frame(renderer, nullptr, layout, config);
        Frame(renderer, root.get(), layout, config);
    }
    renderer.fit_to_tree(root.get(), {640, 480}, layout);
    Check(std::isfinite(renderer.camera().pan.x) && renderer.camera().zoom > 0,
          "fit must produce a finite camera");
    layout.node_width = 100000;
    root->shape = NodeShape::RoundedRectangle;
    ComputeBuchheimLayout(root.get(), layout);
    renderer.fit_to_tree(root.get(), {640, 480}, layout);
    Check(renderer.camera().zoom < 0.08f, "fit must support very wide search trees");
    const float fit_zoom = renderer.camera().zoom;
    Frame(renderer, root.get(), layout, config);
    Check(Near(renderer.camera().zoom, fit_zoom), "render must preserve a wide-tree fit");
    auto wide = std::make_unique<BuchheimNode>();
    for (int i = 0; i < 7000; ++i) wide->children.push_back(std::make_unique<BuchheimNode>());
    const LayoutConfig default_layout;
    ComputeBuchheimLayout(wide.get(), default_layout);
    renderer.camera().pan = {-10000000, -10000000};
    const auto large_map = Frame(renderer, wide.get(), default_layout, config);
    Check(large_map.vertices > 65535, "large minimap must handle vertex-offset draw commands");
    for (int i = 0; i < 4; ++i) Frame(renderer, wide.get(), default_layout, config);
    const auto warmed_allocations = allocations;
    Frame(renderer, wide.get(), default_layout, config);
    Frame(renderer, wide.get(), default_layout, config);
    Check(allocations == warmed_allocations, "steady-state rendering must not allocate");
    io.BackendFlags &= ~ImGuiBackendFlags_RendererHasVtxOffset;
    Frame(renderer, root.get(), layout, config);
    ImGui::DestroyContext();
    std::puts("Renderer tests passed");
}
