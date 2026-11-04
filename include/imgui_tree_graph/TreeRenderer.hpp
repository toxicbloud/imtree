#pragma once

#include "imgui_tree_graph/BuchheimLayout.hpp"

#include <string_view>

namespace ImGuiTreeGraph {

enum class MinimapCorner : std::uint8_t { TopLeft, TopRight, BottomLeft, BottomRight };

struct Camera {
    ImVec2 pan{0.0f, 0.0f};
    float zoom{1.0f};
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
};

// Keep one renderer per canvas. Compute the layout after externally changing the tree.
class TreeRenderer {
public:
    [[nodiscard]] Camera& camera() noexcept;
    [[nodiscard]] const Camera& camera() const noexcept;
    void fit_to_tree(const BuchheimNode* root, ImVec2 canvas_size,
                     const LayoutConfig& layout = {}) noexcept;
    // Returns true if a node was collapsed/expanded. Layout is updated before drawing.
    bool render(std::string_view id, BuchheimNode* root,
                const LayoutConfig& layout = {}, const RendererConfig& config = {});

private:
    Camera camera_{};
    bool minimap_dragging_{false};
};

} // namespace ImGuiTreeGraph
