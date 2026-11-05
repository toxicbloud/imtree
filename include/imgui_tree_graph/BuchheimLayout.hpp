#pragma once

#include <imgui.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ImGuiTreeGraph
{

enum class NodeShape : std::uint8_t
{
    Circle,
    RoundedRectangle,
    Square = RoundedRectangle
};

struct EdgeVisualData
{
    std::string label;
    ImU32 color = IM_COL32(160, 160, 160, 255);
    float thickness = 1.0f;
};

struct LayoutConfig
{
    // All values must be finite. Node dimensions must be positive and gaps
    // nonnegative. Use level_height >= node_height to avoid vertical overlap.
    float node_width = 100.0f;
    float node_height = 60.0f;
    float sibling_distance = 24.0f;
    float subtree_distance = 40.0f;
    float level_height = 110.0f;
};

struct BuchheimNode
{
    std::uint32_t id = 0;
    std::string primary_label;
    std::string secondary_label;
    EdgeVisualData edge_from_parent;
    NodeShape shape = NodeShape::Circle;
    std::int8_t player_id = -1;
    ImU32 fill_color = 0; // Nonzero overrides the renderer's color callback and palette.
    ImU32 border_color = IM_COL32(180, 185, 195, 255);
    bool collapsed = false;

    float x = 0.0f;
    float y = 0.0f;
    BuchheimNode* parent = nullptr;
    // Child pointers must be non-null. Layout repairs parent and sibling indices.
    std::vector<std::unique_ptr<BuchheimNode>> children;

    float prelim = 0.0f;
    float mod = 0.0f;
    float shift = 0.0f;
    float change = 0.0f;
    BuchheimNode* thread = nullptr;
    BuchheimNode* ancestor = nullptr;
    std::size_t number = 1;

    // Layout-owned, visible-subtree center bounds/count (collapsed descendants
    // excluded). Do not edit; coordinates/topology/collapse changes need relayout.
    ImVec2 subtree_min{};
    ImVec2 subtree_max{};
    std::size_t subtree_count = 1;
    // Conservative maximum absolute edge thickness for the visible subtree.
    float subtree_edge_thickness = 1.0f;

    BuchheimNode();
    explicit BuchheimNode(std::string primary_label);
    ~BuchheimNode();
    BuchheimNode(const BuchheimNode&) = delete;
    BuchheimNode& operator=(const BuchheimNode&) = delete;

    // Transfers ownership; rejects null. Existing pointers remain stable. As with
    // direct children edits, recompute layout after changing an already laid-out tree.
    BuchheimNode& add_child(std::unique_ptr<BuchheimNode> child);
    bool has_layout() const noexcept;
    bool is_leaf() const noexcept;
    BuchheimNode* left_sibling() const noexcept;
    BuchheimNode* leftmost_sibling() const noexcept;
    BuchheimNode* leftmost_child() const noexcept;
    BuchheimNode* rightmost_child() const noexcept;

private:
    BuchheimNode* default_ancestor_ = nullptr;
    bool layout_initialized_ = false;
    friend void ComputeBuchheimLayout(BuchheimNode*, const LayoutConfig&) noexcept;
};

// Coordinates denote node centers; the root is anchored at (0, 0).
void ComputeBuchheimLayout(BuchheimNode* root, const LayoutConfig& config = {}) noexcept;
// Call with the rendering root after editing edge thickness on a laid-out tree;
// O(N), allocation-free, no coordinate changes. Layout calls this automatically.
// Labels, colors, shapes, and renderer node dimensions may change freely without
// refreshing this cache. Font changes need no refresh either. Relayout is still
// required for coordinate/tree edits and changes to layout spacing parameters.
void RefreshTreeVisualBounds(BuchheimNode* root) noexcept;
// Fits current coordinates without mutation. Fresh trees use O(depth) scratch;
// laid-out trees use parent links with no allocations. Excludes collapsed children.
void ComputeTreeAABB(const BuchheimNode* root, ImVec2& minimum, ImVec2& maximum,
                     const LayoutConfig& config = {}) noexcept;

} // namespace ImGuiTreeGraph
