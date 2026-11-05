#include "imgui_tree_graph/TreeRenderer.hpp"

#include <algorithm>
#include <cmath>

namespace ImGuiTreeGraph {
namespace {

struct Rect {
    ImVec2 min;
    ImVec2 max;
};

bool Intersects(Rect a, Rect b) noexcept {
    return a.max.x >= b.min.x && a.min.x <= b.max.x &&
           a.max.y >= b.min.y && a.min.y <= b.max.y;
}

bool Contains(Rect r, ImVec2 p) noexcept {
    return p.x >= r.min.x && p.x <= r.max.x && p.y >= r.min.y && p.y <= r.max.y;
}

Rect Around(ImVec2 p, float x, float y) noexcept {
    return {{p.x - x, p.y - y}, {p.x + x, p.y + y}};
}

// Parent links and sibling indices are established by ComputeBuchheimLayout.
template<class Node>
Node* Next(Node* n, const BuchheimNode* root, bool skip = false) noexcept {
    if (!skip && !n->is_leaf()) return n->children.front().get();
    while (n != root) {
        auto* parent = n->parent;
        if (n->number < parent->children.size()) return parent->children[n->number].get();
        n = parent;
    }
    return nullptr;
}

ImU32 Fill(const BuchheimNode& n, const RendererConfig& config) {
    if (n.fill_color != 0) return n.fill_color;
    if (config.node_color) return config.node_color(n, config.node_color_user_data);
    switch (n.player_id) {
    case 0: return IM_COL32(52, 126, 190, 255);
    case 1: return IM_COL32(185, 71, 66, 255);
    default: return IM_COL32(95, 128, 112, 255);
    }
}

ImVec2 Screen(ImVec2 world, ImVec2 origin, const Camera& camera) noexcept {
    return {origin.x + camera.pan.x + world.x * camera.zoom,
            origin.y + camera.pan.y + world.y * camera.zoom};
}

ImVec2 Position(const BuchheimNode& n) noexcept { return {n.x, n.y}; }

Rect Expand(Rect r, float x, float y) noexcept {
    return {{r.min.x - x, r.min.y - y}, {r.max.x + x, r.max.y + y}};
}

Rect Subtree(const BuchheimNode& n, ImVec2 origin, const Camera& camera,
             const LayoutConfig& layout) noexcept {
    // Shapes/dimensions remain live. Include outlines, AA fringe, cluster marker,
    // and the collapsed marker even for very narrow/tiny node dimensions.
    return Expand({Screen(n.subtree_min, origin, camera), Screen(n.subtree_max, origin, camera)},
                  std::max(4.0f, (layout.node_width * 0.5f + 4) * camera.zoom) + 1,
                  std::max(4.0f, (layout.node_height * 0.5f + 4) * camera.zoom) + 1);
}

bool Cluster(const BuchheimNode& n, float scale, const LayoutConfig& layout,
             float pixels, float thickness = 0) noexcept {
    return pixels > 0 && n.subtree_count >= 4 &&
        (n.subtree_max.x - n.subtree_min.x + layout.node_width + 2 * thickness) * scale <= pixels &&
        (n.subtree_max.y - n.subtree_min.y + layout.node_height + 2 * thickness) * scale <= pixels;
}

void Text(ImDrawList* dl, ImVec2 center, std::string_view text, float size,
          ImU32 color, Rect clip) {
    if (text.empty()) return;
    ImFont* font = ImGui::GetFont();
    const ImVec2 total = font->CalcTextSizeA(size, 1.0e30f, 0.0f,
                                           text.data(), text.data() + text.size());
    float y = center.y - total.y * 0.5f;
    while (!text.empty()) {
        const auto end = text.find('\n');
        const auto line = text.substr(0, end);
        const ImVec2 extent = font->CalcTextSizeA(size, 1.0e30f, 0.0f,
                                                line.data(), line.data() + line.size());
        const Rect bounds{{center.x - extent.x * 0.5f, y},
                          {center.x + extent.x * 0.5f, y + size}};
        if (!line.empty() && Intersects(bounds, clip)) {
            const ImVec4 fine_clip(clip.min.x, clip.min.y, clip.max.x, clip.max.y);
            dl->AddText(font, size, bounds.min, color, line.data(),
                        line.data() + line.size(), 0.0f, &fine_clip);
        }
        y += size;
        if (end == std::string_view::npos) break;
        text.remove_prefix(end + 1);
    }
}

void Grid(ImDrawList* dl, Rect viewport, ImVec2 origin, const Camera& camera) {
    float spacing = 32.0f * camera.zoom;
    while (spacing < 16.0f) spacing *= 5.0f;
    while (spacing > 80.0f) spacing /= 5.0f;
    const ImVec2 zero = Screen({0, 0}, origin, camera);
    for (int level = 0; level < 2; ++level) {
        const float step = spacing * (level == 0 ? 1.0f : 5.0f);
        const ImU32 color = level == 0 ? IM_COL32(190, 200, 220, 18)
                                     : IM_COL32(190, 200, 220, 42);
        float offset = std::fmod(zero.x - viewport.min.x, step);
        if (offset < 0) offset += step;
        for (float x = viewport.min.x + offset; x <= viewport.max.x; x += step)
            dl->AddLine({x, viewport.min.y}, {x, viewport.max.y}, color);
        offset = std::fmod(zero.y - viewport.min.y, step);
        if (offset < 0) offset += step;
        for (float y = viewport.min.y + offset; y <= viewport.max.y; y += step)
            dl->AddLine({viewport.min.x, y}, {viewport.max.x, y}, color);
    }
    if (Intersects(Around(zero, 6, 6), viewport)) {
        dl->AddLine({zero.x - 6, zero.y}, {zero.x + 6, zero.y}, IM_COL32(235, 160, 110, 150));
        dl->AddLine({zero.x, zero.y - 6}, {zero.x, zero.y + 6}, IM_COL32(235, 160, 110, 150));
    }
}

struct Overview {
    Rect box;
    ImVec2 world_min;
    ImVec2 offset;
    float scale;

    ImVec2 project(ImVec2 p) const noexcept {
        return {offset.x + (p.x - world_min.x) * scale,
                offset.y + (p.y - world_min.y) * scale};
    }
};

Overview MakeOverview(Rect canvas, const BuchheimNode* root,
                      const LayoutConfig& layout, const RendererConfig& config) noexcept {
    const float margin = std::max(0.0f, config.minimap_margin);
    const float width = std::clamp(config.minimap_size.x, 1.0f,
                                  std::max(1.0f, canvas.max.x - canvas.min.x - 2 * margin));
    const float height = std::clamp(config.minimap_size.y, 1.0f,
                                   std::max(1.0f, canvas.max.y - canvas.min.y - 2 * margin));
    const bool left = config.minimap_corner == MinimapCorner::TopLeft ||
                      config.minimap_corner == MinimapCorner::BottomLeft;
    const bool top = config.minimap_corner == MinimapCorner::TopLeft ||
                     config.minimap_corner == MinimapCorner::TopRight;
    const ImVec2 pos{left ? canvas.min.x + margin : canvas.max.x - margin - width,
                     top ? canvas.min.y + margin : canvas.max.y - margin - height};
    // Centers were reduced once at layout time. Current dimensions may change
    // without relayout; a rectangle envelope conservatively covers all shapes.
    ImVec2 lo{}, hi{};
    if (root) {
        lo = {root->subtree_min.x - layout.node_width * 0.5f,
              root->subtree_min.y - layout.node_height * 0.5f};
        hi = {root->subtree_max.x + layout.node_width * 0.5f,
              root->subtree_max.y + layout.node_height * 0.5f};
    }
    lo.x -= 20; lo.y -= 20; hi.x += 20; hi.y += 20;
    const float scale = std::min(width / std::max(1.0f, hi.x - lo.x),
                                 height / std::max(1.0f, hi.y - lo.y));
    return {{pos, {pos.x + width, pos.y + height}}, lo,
            {pos.x + (width - (hi.x - lo.x) * scale) * 0.5f,
             pos.y + (height - (hi.y - lo.y) * scale) * 0.5f}, scale};
}

void DrawOverview(ImDrawList* dl, const BuchheimNode* root, const Overview& map,
                  Rect viewport, ImVec2 origin, const Camera& camera,
                  const LayoutConfig& layout, const RendererConfig& config, TraversalStats& stats) {
    dl->AddRectFilled(map.box.min, map.box.max, IM_COL32(16, 18, 23, 240), 4);
    dl->PushClipRect(map.box.min, map.box.max, true);
    const Rect clip{{std::max(map.box.min.x, viewport.min.x), std::max(map.box.min.y, viewport.min.y)},
                    {std::min(map.box.max.x, viewport.max.x), std::min(map.box.max.y, viewport.max.y)}};
    for (const auto* n = root; n;) {
        ++stats.minimap_nodes_visited;
        // Include this subtree's incoming edge, not just its node centers.
        Rect subtree{map.project(n->subtree_min), map.project(n->subtree_max)};
        if (n != root) {
            const auto parent = map.project(Position(*n->parent));
            subtree.min.x = std::min(subtree.min.x, parent.x);
            subtree.min.y = std::min(subtree.min.y, parent.y);
            subtree.max.x = std::max(subtree.max.x, parent.x);
            subtree.max.y = std::max(subtree.max.y, parent.y);
        }
        if (config.subtree_culling && !Intersects(Expand(subtree, 3, 3), clip)) {
            ++stats.minimap_subtrees_culled;
            n = Next(n, root, true);
            continue;
        }
        const bool cluster = Cluster(*n, map.scale, layout, config.minimap_cluster_pixels);
        if (cluster) ++stats.minimap_clusters;
        const ImVec2 p = map.project(Position(*n));
        if (n != root) {
            const ImVec2 a = map.project(Position(*n->parent));
            const Rect edge{{std::min(a.x, p.x) - 0.5f, std::min(a.y, p.y) - 0.5f},
                            {std::max(a.x, p.x) + 0.5f, std::max(a.y, p.y) + 0.5f}};
            if (Intersects(edge, clip)) dl->AddLine(a, p, IM_COL32(150, 155, 165, 140));
        }
        const float radius = cluster ? 3.0f : 2.0f;
        if (Intersects(Around(p, radius, radius), clip))
            dl->AddCircleFilled(p, radius, Fill(*n, config), 6);
        n = Next(n, root, cluster);
    }
    const ImVec2 a = map.project({(viewport.min.x - origin.x - camera.pan.x) / camera.zoom,
                                  (viewport.min.y - origin.y - camera.pan.y) / camera.zoom});
    const ImVec2 b = map.project({(viewport.max.x - origin.x - camera.pan.x) / camera.zoom,
                                  (viewport.max.y - origin.y - camera.pan.y) / camera.zoom});
    if (Intersects({a, b}, clip)) {
        const ImVec2 lo{std::max(a.x, clip.min.x), std::max(a.y, clip.min.y)};
        const ImVec2 hi{std::min(b.x, clip.max.x), std::min(b.y, clip.max.y)};
        dl->AddRectFilled(lo, hi, IM_COL32(80, 160, 255, 40));
        dl->AddRect(lo, hi, IM_COL32(110, 180, 255, 230));
    }
    dl->PopClipRect();
    dl->AddRect(map.box.min, map.box.max, IM_COL32(100, 110, 130, 220), 4);
}

} // namespace

Camera& TreeRenderer::camera() noexcept { return camera_; }
const Camera& TreeRenderer::camera() const noexcept { return camera_; }
const NodeInteraction& TreeRenderer::interaction() const noexcept { return interaction_; }
const TraversalStats& TreeRenderer::stats() const noexcept { return stats_; }

void TreeRenderer::fit_to_tree(BuchheimNode* root, ImVec2 size,
                              const LayoutConfig& layout) noexcept {
    if (root && !root->has_layout()) ComputeBuchheimLayout(root, layout);
    fit_to_tree(static_cast<const BuchheimNode*>(root), size, layout);
}

void TreeRenderer::fit_to_tree(const BuchheimNode* root, ImVec2 size,
                              const LayoutConfig& layout) noexcept {
    ImVec2 lo, hi;
    ComputeTreeAABB(root, lo, hi, layout);
    camera_.zoom = std::clamp(std::min(std::max(1.0f, size.x - 40) / std::max(1.0f, hi.x - lo.x),
                                       std::max(1.0f, size.y - 40) / std::max(1.0f, hi.y - lo.y)),
                              0.00001f, 4.0f);
    camera_.pan = {size.x * 0.5f - (lo.x + hi.x) * 0.5f * camera_.zoom,
                   size.y * 0.5f - (lo.y + hi.y) * 0.5f * camera_.zoom};
}

bool TreeRenderer::render(std::string_view id, BuchheimNode* root,
                          const LayoutConfig& layout, const RendererConfig& config) {
    interaction_ = {};
    stats_ = {};
    if (root && !root->has_layout()) ComputeBuchheimLayout(root, layout);
    // Large trees can exceed 64K vertices even when only their mini-map is visible.
    if (root && sizeof(ImDrawIdx) == 2 &&
        !(ImGui::GetIO().BackendFlags & ImGuiBackendFlags_RendererHasVtxOffset)) {
        ImGui::TextUnformatted("imgui_tree_graph: enable RendererHasVtxOffset or use 32-bit ImDrawIdx.");
        return false;
    }
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size = ImGui::GetContentRegionAvail();
    if (size.x <= 0 || size.y <= 0) return false;
    if (id.empty()) ImGui::PushID("");
    else ImGui::PushID(id.data(), id.data() + id.size());
    ImGui::InvisibleButton("canvas", size, ImGuiButtonFlags_MouseButtonLeft |
                            ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const Rect canvas{origin, {origin.x + size.x, origin.y + size.y}};
    const Rect viewport{{std::max(origin.x, dl->GetClipRectMin().x),
                         std::max(origin.y, dl->GetClipRectMin().y)},
                        {std::min(canvas.max.x, dl->GetClipRectMax().x),
                         std::min(canvas.max.y, dl->GetClipRectMax().y)}};
    if (viewport.min.x >= viewport.max.x || viewport.min.y >= viewport.max.y) {
        ImGui::PopID();
        return false;
    }
    auto& io = ImGui::GetIO();
    const float min_zoom = std::isfinite(config.min_zoom) ? std::max(0.00001f, config.min_zoom) : 0.00001f;
    const float max_zoom = std::isfinite(config.max_zoom) ? std::max(min_zoom, config.max_zoom) : std::max(min_zoom, 4.0f);
    camera_.zoom = std::isfinite(camera_.zoom) ? std::clamp(camera_.zoom, min_zoom, max_zoom) : 1.0f;
    Overview map = MakeOverview(canvas, root, layout, config);
    const bool over_map = config.show_minimap && Contains(map.box, io.MousePos);
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) minimap_dragging_ = false;
    if (hovered && over_map && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) minimap_dragging_ = true;
    if (minimap_dragging_ && active) {
        const ImVec2 world{map.world_min.x + (io.MousePos.x - map.offset.x) / map.scale,
                           map.world_min.y + (io.MousePos.y - map.offset.y) / map.scale};
        camera_.pan = {size.x * 0.5f - world.x * camera_.zoom,
                       size.y * 0.5f - world.y * camera_.zoom};
    } else if (active && !over_map &&
               (ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0) ||
                ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0))) {
        camera_.pan.x += io.MouseDelta.x;
        camera_.pan.y += io.MouseDelta.y;
    }
    if (hovered && !over_map && io.MouseWheel != 0) {
        const float previous = camera_.zoom;
        camera_.zoom = std::clamp(previous * std::pow(1.15f, io.MouseWheel), min_zoom, max_zoom);
        const ImVec2 local{io.MousePos.x - origin.x, io.MousePos.y - origin.y};
        camera_.pan = {local.x - (local.x - camera_.pan.x) * camera_.zoom / previous,
                       local.y - (local.y - camera_.pan.y) * camera_.zoom / previous};
    }
    bool changed = false;
    if (hovered && !over_map && !minimap_dragging_ && Contains(viewport, io.MousePos)) {
        interaction_.screen_position = io.MousePos;
        interaction_.world_position = {(io.MousePos.x - origin.x - camera_.pan.x) / camera_.zoom,
                                       (io.MousePos.y - origin.y - camera_.pan.y) / camera_.zoom};
        interaction_.ctrl = io.KeyCtrl;
        interaction_.shift = io.KeyShift;
        interaction_.alt = io.KeyAlt;
        interaction_.super = io.KeySuper;
        for (auto* n = root; n;) {
            ++stats_.hit_nodes_visited;
            if (config.subtree_culling &&
                !Contains(Subtree(*n, origin, camera_, layout), io.MousePos)) {
                ++stats_.hit_subtrees_culled;
                n = Next(n, root, true);
                continue;
            }
            const bool cluster = camera_.zoom < config.badge_zoom &&
                Cluster(*n, camera_.zoom, layout, config.cluster_pixels, n->subtree_edge_thickness);
            const ImVec2 p = Screen(Position(*n), origin, camera_);
            const float hw = layout.node_width * camera_.zoom * 0.5f;
            const float hh = layout.node_height * camera_.zoom * 0.5f;
            const float dx = io.MousePos.x - p.x, dy = io.MousePos.y - p.y;
            const float radius = std::min(hw, hh);
            const bool hit = cluster ? dx * dx + dy * dy <= 16 :
                (n->shape == NodeShape::Circle ? dx * dx + dy * dy <= radius * radius
                                               : Contains(Around(p, hw, hh), io.MousePos));
            if (hit) {
                interaction_.hovered = n;
                interaction_.hovered_hidden_count = cluster ? n->subtree_count - 1 : 0;
                for (int button = ImGuiMouseButton_Left; button <= ImGuiMouseButton_Middle; ++button) {
                    if (!ImGui::IsMouseClicked(button)) continue;
                    interaction_.clicked = n;
                    interaction_.clicked_hidden_count = interaction_.hovered_hidden_count;
                    interaction_.mouse_button = button;
                    interaction_.double_clicked = ImGui::IsMouseDoubleClicked(button);
                    break;
                }
                if (!cluster && config.collapse_on_click && !n->children.empty() &&
                    interaction_.mouse_button == ImGuiMouseButton_Left) {
                    n->collapsed = !n->collapsed;
                    ComputeBuchheimLayout(root, layout);
                    changed = true;
                    map = MakeOverview(canvas, root, layout, config);
                }
                break;
            }
            n = Next(n, root, cluster);
        }
    }
    dl->PushClipRect(viewport.min, viewport.max, true);
    dl->AddRectFilled(viewport.min, viewport.max, config.background);
    if (config.show_grid) Grid(dl, viewport, origin, camera_);
    const float zoom = camera_.zoom;
    // Edges first so node fills cover their endpoints.
    for (const auto* cursor = root; cursor;) {
        const auto* n = cursor;
        ++stats_.main_edges_visited;
        const bool cluster = zoom < config.badge_zoom &&
            Cluster(*n, zoom, layout, config.cluster_pixels, n->subtree_edge_thickness);
        cursor = Next(n, root, cluster);
        // Badges are independent visuals with mutable, potentially unbounded
        // labels. Never prune their pass while badges are enabled.
        if (config.subtree_culling && zoom < config.badge_zoom &&
            std::isfinite(n->subtree_edge_thickness)) {
            Rect bounds = Subtree(*n, origin, camera_, layout);
            if (n != root) {
                const auto p = Screen(Position(*n->parent), origin, camera_);
                bounds.min.x = std::min(bounds.min.x, p.x - layout.node_width * zoom);
                bounds.min.y = std::min(bounds.min.y, p.y - layout.node_height * zoom);
                bounds.max.x = std::max(bounds.max.x, p.x + layout.node_width * zoom);
                bounds.max.y = std::max(bounds.max.y, p.y + layout.node_height * zoom);
            }
            const float pad = std::max(0.5f, n->subtree_edge_thickness * zoom) + 1;
            if (!Intersects(Expand(bounds, pad, pad), viewport)) {
                ++stats_.edge_subtrees_culled;
                cursor = Next(n, root, true);
                continue;
            }
        }
        // Keep the representative's incoming edge when collapsing its internals.
        if (n == root) continue;
        ImVec2 a = Screen(Position(*n->parent), origin, camera_);
        ImVec2 b = Screen(Position(*n), origin, camera_);
        const float parent_radius = (n->parent->shape == NodeShape::Circle ?
            std::min(layout.node_width, layout.node_height) : layout.node_height) * zoom * 0.5f;
        const float radius = (n->shape == NodeShape::Circle ?
            std::min(layout.node_width, layout.node_height) : layout.node_height) * zoom * 0.5f;
        a.y += parent_radius;
        b.y -= radius;
        const float mid_y = (a.y + b.y) * 0.5f;
        const float thickness = std::max(0.5f, n->edge_from_parent.thickness * zoom);
        const Rect bounds{{std::min(a.x, b.x) - thickness, std::min(a.y, b.y) - thickness},
                          {std::max(a.x, b.x) + thickness, std::max(a.y, b.y) + thickness}};
        if (Intersects(bounds, viewport)) {
            if (zoom < config.detail_zoom) dl->AddLine(a, b, n->edge_from_parent.color, thickness);
            else dl->AddBezierCubic(a, {a.x, mid_y}, {b.x, mid_y}, b, n->edge_from_parent.color, thickness);
        }
        if (zoom >= config.badge_zoom && !n->edge_from_parent.label.empty()) {
            const auto& label = n->edge_from_parent.label;
            const float font_size = ImGui::GetFontSize() * zoom;
            const ImVec2 extent = ImGui::GetFont()->CalcTextSizeA(font_size, 1.0e30f, 0, label.data(), label.data() + label.size());
            const ImVec2 mid{(a.x + b.x) * 0.5f, mid_y};
            const Rect badge = Around(mid, extent.x * 0.5f + 4, extent.y * 0.5f + 2);
            if (Intersects(badge, viewport)) {
                dl->AddRectFilled(badge.min, badge.max, IM_COL32(20, 22, 28, 215), 3);
                Text(dl, mid, label, font_size, IM_COL32(235, 235, 240, 255), viewport);
            }
        }
    }
    for (const auto* cursor = root; cursor;) {
        const auto* n = cursor;
        ++stats_.main_nodes_visited;
        if (config.subtree_culling && !Intersects(Subtree(*n, origin, camera_, layout), viewport)) {
            ++stats_.main_subtrees_culled;
            cursor = Next(n, root, true);
            continue;
        }
        const bool cluster = zoom < config.badge_zoom &&
            Cluster(*n, zoom, layout, config.cluster_pixels, n->subtree_edge_thickness);
        cursor = Next(n, root, cluster);
        if (cluster) {
            ++stats_.main_clusters;
            const auto p = Screen(Position(*n), origin, camera_);
            if (Intersects(Around(p, 4, 4), viewport))
                dl->AddCircleFilled(p, 4, Fill(*n, config), 8);
            continue;
        }
        const ImVec2 p = Screen(Position(*n), origin, camera_);
        const float hw = layout.node_width * zoom * 0.5f;
        const float hh = layout.node_height * zoom * 0.5f;
        const float radius = std::min(hw, hh);
        const Rect bounds = n->shape == NodeShape::Circle ? Around(p, radius, radius) : Around(p, hw, hh);
        Rect stroke = bounds;
        stroke.min.x -= zoom; stroke.min.y -= zoom;
        stroke.max.x += zoom; stroke.max.y += zoom;
        if (!Intersects(stroke, viewport)) continue;
        const bool detail = zoom >= config.detail_zoom;
        if (n->shape == NodeShape::Circle) {
            dl->AddCircleFilled(p, radius, Fill(*n, config), detail ? 32 : 8);
            if (detail) dl->AddCircle(p, radius, n->border_color, 32, 2 * zoom);
        } else {
            dl->AddRectFilled(bounds.min, bounds.max, Fill(*n, config), detail ? 6 * zoom : 0);
            if (detail) dl->AddRect(bounds.min, bounds.max, n->border_color, 6 * zoom, 0, 2 * zoom);
        }
        if (zoom >= config.text_zoom) {
            const Rect clip{{std::max(bounds.min.x, viewport.min.x), std::max(bounds.min.y, viewport.min.y)},
                            {std::min(bounds.max.x, viewport.max.x), std::min(bounds.max.y, viewport.max.y)}};
            const float font_size = ImGui::GetFontSize() * zoom;
            const auto& primary = n->primary_label;
            const auto& secondary = n->secondary_label;
            const float first_height = primary.empty() ? 0 : ImGui::GetFont()->CalcTextSizeA(
                font_size, 1.0e30f, 0, primary.data(), primary.data() + primary.size()).y;
            const float second_height = secondary.empty() ? 0 : ImGui::GetFont()->CalcTextSizeA(
                font_size, 1.0e30f, 0, secondary.data(), secondary.data() + secondary.size()).y;
            Text(dl, {p.x, p.y - second_height * 0.5f}, primary, font_size, IM_COL32_WHITE, clip);
            Text(dl, {p.x, p.y + first_height * 0.5f}, secondary, font_size, IM_COL32(225, 225, 230, 230), clip);
        }
        if (n->collapsed && zoom >= config.badge_zoom) {
            const ImVec2 marker{p.x + hw * 0.55f, p.y - hh * 0.55f};
            if (Intersects(Around(marker, 3 * zoom, 3 * zoom), viewport))
                dl->AddCircleFilled(marker, 3 * zoom, IM_COL32(255, 215, 110, 255), 8);
        }
    }
    if (config.show_minimap && Intersects(map.box, viewport))
        DrawOverview(dl, root, map, viewport, origin, camera_, layout, config, stats_);
    dl->PopClipRect();
    ImGui::PopID();
    return changed;
}

} // namespace ImGuiTreeGraph
