#include "imgui_tree_graph/TreeRenderer.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <limits>
#include <vector>

namespace { std::size_t cpp_allocations = 0; }

void* operator new(std::size_t size) {
    ++cpp_allocations;
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

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
                  const LayoutConfig& layout, const RendererConfig& config, float bottom_clip = 0) {
    ImGui::NewFrame();
    ImGui::SetNextWindowPos({80, 60});
    ImGui::SetNextWindowSize({640, 480});
    ImGui::Begin("test", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size = ImGui::GetContentRegionAvail();
    const int before = ImGui::GetWindowDrawList()->VtxBuffer.Size;
    if (bottom_clip > 0)
        ImGui::GetWindowDrawList()->PushClipRect(origin,
            {origin.x + size.x, origin.y + size.y - bottom_clip}, true);
    const bool changed = renderer.render("tree", root, layout, config);
    if (bottom_clip > 0) ImGui::GetWindowDrawList()->PopClipRect();
    const int vertices = ImGui::GetWindowDrawList()->VtxBuffer.Size - before;
    ImGui::End();
    ImGui::Render();
    return {origin, size, vertices, changed};
}

struct ColorProbe {
    int calls[3]{};
};

ImU32 CustomColor(const BuchheimNode& node, void* data) {
    auto& probe = *static_cast<ColorProbe*>(data);
    Check(node.id < 3, "callback receives original node");
    ++probe.calls[node.id];
    return IM_COL32(7, 19, 233, 255);
}

int ColorVertices(ImU32 color) {
    int count = 0;
    const auto* draw = ImGui::GetDrawData();
    for (int i = 0; i < draw->CmdListsCount; ++i)
        for (const auto& vertex : draw->CmdLists[i]->VtxBuffer)
            if (vertex.col == color) ++count;
    return count;
}

std::uint64_t GeometryHash() {
    std::uint64_t hash = 1469598103934665603ULL;
    const auto* draw = ImGui::GetDrawData();
    for (int i = 0; i < draw->CmdListsCount; ++i)
        for (const auto& vertex : draw->CmdLists[i]->VtxBuffer) {
            const auto* bytes = reinterpret_cast<const unsigned char*>(&vertex);
            for (std::size_t j = 0; j < sizeof(vertex); ++j)
                hash = (hash ^ bytes[j]) * 1099511628211ULL;
        }
    return hash;
}

void TestAcceleration() {
    LayoutConfig layout;
    RendererConfig config;
    config.show_grid = false;
    config.show_minimap = false;
    auto root = std::make_unique<BuchheimNode>();
    std::vector<BuchheimNode*> nodes{root.get()};
    for (std::size_t i = 0; i < 32767; ++i)
        for (int j = 0; j < 2; ++j)
            nodes.push_back(&nodes[i]->add_child(std::make_unique<BuchheimNode>()));
    ComputeBuchheimLayout(root.get(), layout);
    Check(root->subtree_count == 65535, "large cache must count every visible node");
    TreeRenderer renderer;
    auto& io = ImGui::GetIO();
    renderer.camera() = {{-100000000, -100000000}, 0.1f};
    auto frame = Frame(renderer, root.get(), layout, config);
    io.AddMousePosEvent(frame.origin.x + 200, frame.origin.y + 100);
    frame = Frame(renderer, root.get(), layout, config);
    const auto skipped = renderer.stats();
    Check(skipped.main_nodes_visited == 1 && skipped.main_edges_visited == 1 &&
          skipped.hit_nodes_visited == 1 && skipped.main_subtrees_culled == 1 &&
          skipped.edge_subtrees_culled == 1 && !renderer.interaction().hovered,
          "offscreen 65k subtree must skip nodes, edges and hits at its root");
    Frame(renderer, root.get(), layout, config);
    Check(renderer.stats().main_nodes_visited == skipped.main_nodes_visited &&
          renderer.stats().main_edges_visited == skipped.main_edges_visited &&
          renderer.stats().hit_nodes_visited == skipped.hit_nodes_visited,
          "traversal statistics are deterministic across identical frames");
    const auto hash = GeometryHash();
    config.subtree_culling = false;
    config.cluster_pixels = 0;
    Frame(renderer, root.get(), layout, config);
    Check(GeometryHash() == hash && renderer.stats().main_nodes_visited == 65535 &&
          renderer.stats().main_edges_visited == 65535 && renderer.stats().hit_nodes_visited == 65535,
          "disabled pruning/LOD preserves offscreen geometry with full traversal");
    config.subtree_culling = true;
    config.cluster_pixels = 3;
    renderer.fit_to_tree(root.get(), frame.size, layout);
    Frame(renderer, root.get(), layout, config);
    Check(renderer.stats().main_clusters > 0 && renderer.stats().main_nodes_visited < 10000 &&
          renderer.stats().main_edges_visited < 10000, "automatic fit-space LOD skips dense subtrees");
    const auto fit_stats = renderer.stats();
    config.cluster_pixels = 0;
    Frame(renderer, root.get(), layout, config);
    Check(renderer.stats().main_clusters == 0 && renderer.stats().main_nodes_visited == 65535,
          "main clustering disabled independently");
    config.show_minimap = true;
    renderer.camera() = {{-100000000, -100000000}, 1};
    Frame(renderer, root.get(), layout, config);
    Check(renderer.stats().minimap_clusters > 0 && renderer.stats().minimap_nodes_visited < 10000 &&
          renderer.stats().main_clusters == 0, "minimap clusters independently at readable main zoom");
    std::printf("65535-node traversal: offscreen nodes/edges/hits=%zu/%zu/%zu; "
                "fit nodes/edges=%zu/%zu clusters=%zu; minimap nodes=%zu clusters=%zu\n",
                skipped.main_nodes_visited, skipped.main_edges_visited, skipped.hit_nodes_visited,
                fit_stats.main_nodes_visited, fit_stats.main_edges_visited, fit_stats.main_clusters,
                renderer.stats().minimap_nodes_visited, renderer.stats().minimap_clusters);
    config.minimap_cluster_pixels = 0;
    Frame(renderer, root.get(), layout, config, 130);
    const auto clipped_hash = GeometryHash();
    Check(renderer.stats().minimap_nodes_visited == 1 &&
          renderer.stats().minimap_subtrees_culled == 1,
          "partially clipped minimap prunes an absent 65k subtree at the root");
    config.subtree_culling = false;
    Frame(renderer, root.get(), layout, config, 130);
    Check(GeometryHash() == clipped_hash && renderer.stats().minimap_nodes_visited == 65535,
          "minimap subtree pruning preserves disabled-mode clipped geometry");
    config.subtree_culling = true;
    Frame(renderer, root.get(), layout, config);
    Check(renderer.stats().minimap_clusters == 0 && renderer.stats().minimap_nodes_visited == 65535,
          "unclustered minimap retains the full 16-bit vertex-offset path");
    config.cluster_pixels = 3;
    config.minimap_cluster_pixels = 2;
    renderer.camera() = {{260, 100}, 0.00001f};
    // Force the entire projected tree into one representative for exact hit tests.
    config.cluster_pixels = 100;
    config.show_minimap = false;
    io.AddMousePosEvent(frame.origin.x + 260, frame.origin.y + 100);
    Frame(renderer, root.get(), layout, config);
    Check(renderer.stats().main_nodes_visited == 1 && renderer.stats().main_edges_visited == 1 &&
          renderer.stats().main_clusters == 1 && renderer.stats().hit_nodes_visited == 1 &&
          renderer.interaction().hovered == root.get() &&
          renderer.interaction().hovered_hidden_count == 65534,
          "cluster hit exposes representative and hidden count");
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    Check(!Frame(renderer, root.get(), layout, config).changed && !root->collapsed &&
          renderer.interaction().clicked == root.get() &&
          renderer.interaction().clicked_hidden_count == 65534,
          "automatic cluster clicks must not mutate explicit collapse");
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    config.show_minimap = true;
    for (int i = 0; i < 4; ++i) Frame(renderer, root.get(), layout, config);
    const auto cpp_before = cpp_allocations, imgui_before = allocations;
    for (int i = 0; i < 3; ++i) Frame(renderer, root.get(), layout, config);
    Check(cpp_before == cpp_allocations && imgui_before == allocations,
          "clustered main/hit/minimap steady state must not allocate");
    root->collapsed = true;
    ComputeBuchheimLayout(root.get(), layout);
    Frame(renderer, root.get(), layout, config);
    Check(root->subtree_count == 1 && renderer.stats().main_clusters == 0 &&
          renderer.stats().minimap_nodes_visited == 1, "relayout refreshes collapsed caches");
    root->collapsed = false;
    ComputeBuchheimLayout(root.get(), layout);
    Frame(renderer, root.get(), layout, config);
    Check(root->subtree_count == 65535 && renderer.stats().main_clusters == 1,
          "expansion restores caches and automatic clustering");
    Frame(renderer, nullptr, layout, config);
    Check(renderer.stats().main_nodes_visited == 0 && renderer.stats().main_clusters == 0,
          "null render resets statistics");
    io.AddMousePosEvent(-100, -100);
}

void TestConservativeVisuals() {
    auto root = std::make_unique<BuchheimNode>();
    auto& child = root->add_child(std::make_unique<BuchheimNode>());
    LayoutConfig layout;
    layout.level_height = 2000;
    ComputeBuchheimLayout(root.get(), layout);
    RendererConfig config;
    config.show_minimap = config.show_grid = false;
    config.cluster_pixels = 0;
    TreeRenderer renderer;
    // Both endpoints lie outside, but the incoming curve crosses the viewport.
    renderer.camera() = {{260, -800}, 1};
    const auto empty = Frame(renderer, nullptr, layout, config);
    const auto crossing = Frame(renderer, root.get(), layout, config);
    Check(crossing.vertices > empty.vertices, "crossing incoming edge cannot be pruned with node bounds");
    auto compare = [&] {
        config.subtree_culling = false;
        const auto plain = Frame(renderer, root.get(), layout, config);
        const auto hash = GeometryHash();
        config.subtree_culling = true;
        const auto pruned = Frame(renderer, root.get(), layout, config);
        Check(plain.vertices == pruned.vertices && hash == GeometryHash(),
              "culling must preserve exact conservative visual geometry");
        return pruned.vertices;
    };
    compare();
    layout.level_height = 4000;
    ComputeBuchheimLayout(root.get(), layout);
    renderer.camera() = {{260, -400}, 0.25f};
    Check(compare() > empty.vertices && renderer.stats().main_nodes_visited == 2,
          "low-zoom pruned edge pass retains crossing incoming line");
    config.detail_zoom = 0.1f;
    Check(compare() > empty.vertices, "crossing Bezier control hull remains conservative");
    config.detail_zoom = 0.3f;
    layout.level_height = 2000;
    ComputeBuchheimLayout(root.get(), layout);
    renderer.camera() = {{260, -800}, 1};
    renderer.camera().pan.x = -1000;
    Check(compare() == empty.vertices, "offscreen narrow incoming edge submits nothing");
    child.edge_from_parent.label.assign(500, 'W');
    Check(compare() > empty.vertices, "live long badge can be visible with edge and endpoints offscreen");
    child.edge_from_parent.label.assign(800, 'M');
    Check(compare() > empty.vertices, "long badge edits require no relayout or cache refresh");
    child.edge_from_parent.label.assign(60, 'W');
    renderer.camera().pan.x = -300;
    Check(compare() == empty.vertices, "small-font badge stays offscreen");
    auto& io = ImGui::GetIO();
    io.FontDefault = io.Fonts->Fonts[1];
    Check(compare() > empty.vertices, "font changes immediately reveal uncached badge extents");
    io.FontDefault = io.Fonts->Fonts[0];
    child.edge_from_parent.label.clear();
    renderer.camera() = {{-400, -200}, 0.25f};
    child.edge_from_parent.thickness = 4000;
    RefreshTreeVisualBounds(root.get());
    Check(root->subtree_edge_thickness == 4000 && compare() > empty.vertices,
          "visual-only thickness refresh preserves thick offscreen crossing strokes");
    child.edge_from_parent.thickness = 1;
    RefreshTreeVisualBounds(root.get());
    Check(root->subtree_edge_thickness == 1, "visual refresh shrinks obsolete stroke bound");
    root->shape = NodeShape::RoundedRectangle;
    layout.node_width = 4000;
    renderer.camera().pan.y = 100;
    Check(compare() > empty.vertices, "live shapes and dimensions remain conservative without relayout");
}

void TestLocalStrokeBounds() {
    BuchheimNode root;
    auto& thick = root.add_child(std::make_unique<BuchheimNode>());
    auto& dense = root.add_child(std::make_unique<BuchheimNode>());
    for (int i = 0; i < 3; ++i) dense.add_child(std::make_unique<BuchheimNode>());
    auto* tail = &root.add_child(std::make_unique<BuchheimNode>());
    for (int i = 0; i < 512; ++i)
        tail = &tail->add_child(std::make_unique<BuchheimNode>());
    LayoutConfig layout;
    layout.level_height = 10000;
    thick.edge_from_parent.thickness = 1.0e9f;
    // Transparent edges emit no geometry even for the nonfinite-bound case.
    thick.edge_from_parent.color = 0;
    ComputeBuchheimLayout(&root, layout);
    RendererConfig config;
    config.show_grid = config.show_minimap = false;
    config.cluster_pixels = 30;
    TreeRenderer renderer;
    renderer.camera() = {{260, 100}, 0.001f};
    auto frame = Frame(renderer, &root, layout, config);
    auto& io = ImGui::GetIO();
    io.AddMousePosEvent(frame.origin.x + 260 + dense.x * 0.001f,
                        frame.origin.y + 100 + dense.y * 0.001f);
    auto verify = [&] {
        Frame(renderer, &root, layout, config);
        Check(dense.subtree_edge_thickness == 1 && renderer.stats().main_clusters == 1 &&
              renderer.stats().main_edges_visited < 100 &&
              renderer.stats().edge_subtrees_culled > 0 &&
              renderer.interaction().hovered == &dense &&
              renderer.interaction().hovered_hidden_count == 3,
              "unrelated wide/nonfinite edge cannot block local clustering, hit LOD, or pruning");
    };
    verify();
    thick.edge_from_parent.thickness = std::numeric_limits<float>::infinity();
    RefreshTreeVisualBounds(&root);
    Check(std::isinf(root.subtree_edge_thickness), "nonfinite bound propagates only through ancestors");
    verify();
    io.AddMousePosEvent(-100, -100);
}

void TestNewAPI() {
    auto& io = ImGui::GetIO();
    LayoutConfig layout;
    RendererConfig config;
    config.show_grid = false;
    config.show_minimap = false;
    TreeRenderer renderer;
    auto root = std::make_unique<BuchheimNode>("root");
    root->children.push_back(std::make_unique<BuchheimNode>("leaf"));
    root->children.push_back(std::make_unique<BuchheimNode>("leaf"));
    renderer.camera().pan = {260, 100};
    Frame(renderer, root.get(), layout, config);
    const auto frame = Frame(renderer, root.get(), layout, config);
    Check(root->has_layout() && root->children[1]->parent == root.get() &&
          root->children[1]->number == 2 && root->children[1]->y == layout.level_height,
          "render must lazily layout legacy fresh trees");

    auto fresh = std::make_unique<BuchheimNode>();
    auto* tip = fresh.get();
    for (int i = 0; i < 100000; ++i)
        tip = &tip->add_child(std::make_unique<BuchheimNode>());
    renderer.fit_to_tree(fresh.get(), frame.size, layout);
    Check(fresh->has_layout() && tip->y == 100000.0f * layout.level_height,
          "mutable fit must lazily layout deep fresh trees");
    Check(fresh->subtree_count == 100001 && fresh->subtree_min.y == 0 &&
          fresh->subtree_max.y == tip->y, "deep-tree caches are reduced without recursion");
    Frame(renderer, fresh.get(), layout, config);
    for (int i = 0; i < 4; ++i) Frame(renderer, fresh.get(), layout, config);
    const auto deep_cpp_before = cpp_allocations, deep_imgui_before = allocations;
    Frame(renderer, fresh.get(), layout, config);
    Check(deep_cpp_before == cpp_allocations && deep_imgui_before == allocations,
          "deep-tree traversal must remain allocation-free after warmup");
    renderer.fit_to_tree(nullptr, frame.size);
    Check(std::isfinite(renderer.camera().pan.x), "null fit overload must be unambiguous");
    auto const_fresh = std::make_unique<BuchheimNode>();
    const_fresh->children.push_back(std::make_unique<BuchheimNode>());
    const_fresh->children.front()->x = 400;
    renderer.fit_to_tree(static_cast<const BuchheimNode*>(const_fresh.get()), frame.size, layout);
    Check(!const_fresh->has_layout() && const_fresh->children.front()->parent == nullptr &&
          const_fresh->children.front()->x == 400 && renderer.camera().zoom > 0,
          "const fresh fit must not mutate topology or coordinates");

    renderer.camera() = {{260, 100}, 1};
    config.collapse_on_click = false;
    io.AddMousePosEvent(frame.origin.x + 260, frame.origin.y + 100);
    Frame(renderer, root.get(), layout, config);
    Check(renderer.interaction().hovered == root.get() && !renderer.interaction().clicked,
          "hover events include nonclicked nodes");
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    Check(!Frame(renderer, root.get(), layout, config).changed && !root->collapsed,
          "collapse can be disabled independently of clicks");
    const auto event = renderer.interaction();
    Check(event.clicked == root.get() && event.mouse_button == ImGuiMouseButton_Left &&
          event.ctrl && Near(event.world_position.x, 0) && Near(event.world_position.y, 0) &&
          Near(event.screen_position.x, frame.origin.x + 260), "rich click event");
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    Frame(renderer, root.get(), layout, config);
    Check(!renderer.interaction().clicked, "click events reset on the next render");
    auto* leaf = root->children.front().get();
    io.AddMousePosEvent(frame.origin.x + 260 + leaf->x, frame.origin.y + 100 + leaf->y);
    Frame(renderer, root.get(), layout, config);
    io.AddMouseButtonEvent(ImGuiMouseButton_Right, true);
    Frame(renderer, root.get(), layout, config);
    Check(renderer.interaction().clicked == leaf &&
          renderer.interaction().mouse_button == ImGuiMouseButton_Right,
          "leaf and right-button clicks are reported");
    io.AddMouseButtonEvent(ImGuiMouseButton_Right, false);
    Frame(renderer, root.get(), layout, config);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    Frame(renderer, root.get(), layout, config);
    Check(renderer.interaction().clicked == leaf && !leaf->collapsed,
          "left-clicking a leaf reports selection without changing layout");
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    Frame(renderer, root.get(), layout, config);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    Frame(renderer, root.get(), layout, config);
    Check(renderer.interaction().clicked == leaf && renderer.interaction().double_clicked,
          "double clicks are exposed to custom inspectors");
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    Frame(renderer, root.get(), layout, config);
    io.AddMousePosEvent(frame.origin.x + frame.size.x - 20, frame.origin.y + frame.size.y - 20);
    Frame(renderer, root.get(), layout, config);
    Check(!renderer.interaction().hovered, "empty canvas clears hovered nodes");
    Frame(renderer, nullptr, layout, config);
    Check(!renderer.interaction().hovered && !renderer.interaction().clicked,
          "null render clears interaction");

    root->id = 0;
    root->children[0]->id = 1;
    root->children[1]->id = 2;
    root->player_id = 0;
    root->children[0]->player_id = 1;
    root->children[1]->player_id = -1;
    config.show_minimap = true;
    Frame(renderer, root.get(), layout, config);
    Check(ColorVertices(IM_COL32(52, 126, 190, 255)) > 0 &&
          ColorVertices(IM_COL32(185, 71, 66, 255)) > 0 &&
          ColorVertices(IM_COL32(95, 128, 112, 255)) > 0,
          "default player palette remains unchanged");
    ColorProbe probe;
    config.node_color = CustomColor;
    config.node_color_user_data = &probe;
    Frame(renderer, root.get(), layout, config);
    Check(probe.calls[0] == 2 && probe.calls[1] == 2 && probe.calls[2] == 2 &&
          ColorVertices(IM_COL32(7, 19, 233, 255)) > 0,
          "custom colors override every player palette on canvas and minimap");
    probe = {};
    root->fill_color = IM_COL32(251, 3, 91, 255);
    Frame(renderer, root.get(), layout, config);
    Check(probe.calls[0] == 0 && probe.calls[1] == 2 && probe.calls[2] == 2 &&
          ColorVertices(root->fill_color) > 0, "explicit fill overrides callback in both views");
    for (int i = 0; i < 4; ++i) Frame(renderer, root.get(), layout, config);
    const auto before = allocations;
    const auto cpp_before = cpp_allocations;
    Frame(renderer, root.get(), layout, config);
    Check(before == allocations && cpp_before == cpp_allocations,
          "custom interaction and color rendering does not allocate");
    const ImVec2 map_center{frame.origin.x + frame.size.x - config.minimap_margin - 100,
                            frame.origin.y + frame.size.y - config.minimap_margin - 75};
    io.AddMousePosEvent(map_center.x, map_center.y);
    Frame(renderer, root.get(), layout, config);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    Frame(renderer, root.get(), layout, config);
    Check(!renderer.interaction().hovered && !renderer.interaction().clicked,
          "minimap interactions must not leak node events");
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    io.AddMousePosEvent(-100, -100);
    Frame(renderer, nullptr, layout, config);
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
    io.Fonts->AddFontDefault();
    ImFontConfig large_font;
    large_font.SizePixels = 26;
    io.Fonts->AddFontDefault(&large_font);
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    TestNewAPI();
    TestAcceleration();
    TestConservativeVisuals();
    TestLocalStrokeBounds();

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
    config.minimap_cluster_pixels = 0;
    ComputeBuchheimLayout(wide.get(), default_layout);
    renderer.camera().pan = {-10000000, -10000000};
    const auto large_map = Frame(renderer, wide.get(), default_layout, config);
    Check(large_map.vertices > 65535, "large minimap must handle vertex-offset draw commands");
    for (int i = 0; i < 4; ++i) Frame(renderer, wide.get(), default_layout, config);
    const auto warmed_allocations = allocations;
    const auto warmed_cpp_allocations = cpp_allocations;
    Frame(renderer, wide.get(), default_layout, config);
    Frame(renderer, wide.get(), default_layout, config);
    Check(allocations == warmed_allocations && cpp_allocations == warmed_cpp_allocations,
          "steady-state rendering must not allocate");
    io.BackendFlags &= ~ImGuiBackendFlags_RendererHasVtxOffset;
    Frame(renderer, root.get(), layout, config);
    ImGui::DestroyContext();
    std::puts("Renderer tests passed");
}
