#pragma once

#include "imgui_tree_graph/BuchheimLayout.hpp"

#include <string_view>

namespace ImGuiTreeGraph {

enum class MinimapCorner : std::uint8_t { TopLeft, TopRight, BottomLeft, BottomRight };

struct Camera {
    ImVec2 pan{0.0f, 0.0f};
    float zoom{1.0f};
};

// Borrowed pointers remain valid only while the caller owns the nodes. Reset on
// every render, including empty/clipped canvases. Minimap input is not node input.
struct NodeInteraction {
    const BuchheimNode* hovered{nullptr};
    const BuchheimNode* clicked{nullptr};
    int mouse_button{-1}; // ImGuiMouseButton; -1 when no node was clicked.
    bool double_clicked{false};
    ImVec2 screen_position{};
    ImVec2 world_position{};
    bool ctrl{false}, shift{false}, alt{false}, super{false};
};

struct RendererConfig {
    bool show_grid{true};
    bool show_minimap{true};
    MinimapCorner minimap_corner{MinimapCorner::BottomRight};
    ImVec2 minimap_size{200.0f, 150.0f};
    float minimap_margin{12.0f};
    float min_zoom{0.00001f};
    float max_zoom{4.0f};
    float text_zoom{0.65f};
    float badge_zoom{0.45f};
    float detail_zoom{0.3f};
    ImU32 background{IM_COL32(23, 25, 30, 255)};
    bool collapse_on_click{true};
    // Called for visible main-canvas and minimap fills only when fill_color == 0.
    // Keep callbacks allocation-free and do not mutate the tree during rendering.
    ImU32 (*node_color)(const BuchheimNode&, void* user_data){nullptr};
    void* node_color_user_data{nullptr};
};

// Keep one renderer per canvas. Compute the layout after externally changing the tree.
class TreeRenderer {
public:
    [[nodiscard]] Camera& camera() noexcept;
    [[nodiscard]] const Camera& camera() const noexcept;
    [[nodiscard]] const NodeInteraction& interaction() const noexcept;
    // Mutable overload lazily lays out fresh trees. Const overload only fits their
    // current coordinates (never mutates), including an unlaid-out tree.
    void fit_to_tree(BuchheimNode* root, ImVec2 canvas_size,
                     const LayoutConfig& layout = {}) noexcept;
    void fit_to_tree(const BuchheimNode* root, ImVec2 canvas_size,
                     const LayoutConfig& layout = {}) noexcept;
    void fit_to_tree(std::nullptr_t, ImVec2 canvas_size,
                     const LayoutConfig& layout = {}) noexcept {
        fit_to_tree(static_cast<const BuchheimNode*>(nullptr), canvas_size, layout);
    }
    // Returns true if a node was collapsed/expanded. Layout is updated before drawing.
    bool render(std::string_view id, BuchheimNode* root,
                const LayoutConfig& layout = {}, const RendererConfig& config = {});

private:
    Camera camera_{};
    bool minimap_dragging_{false};
    NodeInteraction interaction_{};
};

} // namespace ImGuiTreeGraph
