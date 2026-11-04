#include "imgui_tree_graph/BuchheimLayout.hpp"

#include <algorithm>
#include <utility>

namespace ImGuiTreeGraph
{

BuchheimNode::BuchheimNode() = default;

BuchheimNode::BuchheimNode(std::string primary_label)
    : primary_label(std::move(primary_label))
{
}

BuchheimNode::~BuchheimNode()
{
    // Release before deleting so even an unlaid-out, very deep tree is stack-safe.
    while (!children.empty())
    {
        BuchheimNode* node = children.back().release();
        children.pop_back();
        if (!node)
            continue;
        node->parent = nullptr;
        while (node)
        {
            if (!node->children.empty())
            {
                BuchheimNode* child = node->children.back().release();
                node->children.pop_back();
                if (child)
                {
                    child->parent = node;
                    node = child;
                }
            }
            else
            {
                BuchheimNode* next = node->parent;
                delete node;
                node = next;
            }
        }
    }
}

bool BuchheimNode::is_leaf() const noexcept
{
    return collapsed || children.empty();
}

BuchheimNode* BuchheimNode::left_sibling() const noexcept
{
    return parent && number > 1 ? parent->children[number - 2].get() : nullptr;
}

BuchheimNode* BuchheimNode::leftmost_sibling() const noexcept
{
    return parent && number > 1 ? parent->children.front().get() : nullptr;
}

BuchheimNode* BuchheimNode::leftmost_child() const noexcept
{
    return is_leaf() ? nullptr : children.front().get();
}

BuchheimNode* BuchheimNode::rightmost_child() const noexcept
{
    return is_leaf() ? nullptr : children.back().get();
}

namespace
{

BuchheimNode* NextLeft(BuchheimNode* node) noexcept
{
    return node->is_leaf() ? node->thread : node->leftmost_child();
}

BuchheimNode* NextRight(BuchheimNode* node) noexcept
{
    return node->is_leaf() ? node->thread : node->rightmost_child();
}

void MoveSubtree(BuchheimNode* left, BuchheimNode* right, float shift) noexcept
{
    const float portion = shift / static_cast<float>(right->number - left->number);
    right->change -= portion;
    right->shift += shift;
    left->change += portion;
    right->prelim += shift;
    right->mod += shift;
}

void ExecuteShifts(BuchheimNode* node) noexcept
{
    float shift = 0.0f;
    float change = 0.0f;
    for (auto it = node->children.rbegin(); it != node->children.rend(); ++it)
    {
        BuchheimNode* child = it->get();
        child->prelim += shift;
        child->mod += shift;
        change += child->change;
        shift += child->shift + change;
    }
}

BuchheimNode* Apportion(BuchheimNode* node, BuchheimNode* default_ancestor,
                       const LayoutConfig& config) noexcept
{
    BuchheimNode* left = node->left_sibling();
    if (!left)
        return default_ancestor;

    BuchheimNode* inside_right = node;
    BuchheimNode* outside_right = node;
    BuchheimNode* inside_left = left;
    BuchheimNode* outside_left = node->leftmost_sibling();
    float inside_right_mod = inside_right->mod;
    float outside_right_mod = outside_right->mod;
    float inside_left_mod = inside_left->mod;
    float outside_left_mod = outside_left->mod;

    while (NextRight(inside_left) && NextLeft(inside_right))
    {
        inside_left = NextRight(inside_left);
        inside_right = NextLeft(inside_right);
        outside_left = NextLeft(outside_left);
        outside_right = NextRight(outside_right);
        outside_right->ancestor = node;
        const float shift = inside_left->prelim + inside_left_mod
                          - inside_right->prelim - inside_right_mod
                          + config.node_width + config.subtree_distance;
        if (shift > 0.0f)
        {
            BuchheimNode* ancestor = inside_left->ancestor;
            if (ancestor->parent != node->parent)
                ancestor = default_ancestor;
            MoveSubtree(ancestor, node, shift);
            inside_right_mod += shift;
            outside_right_mod += shift;
        }
        inside_left_mod += inside_left->mod;
        inside_right_mod += inside_right->mod;
        outside_left_mod += outside_left->mod;
        outside_right_mod += outside_right->mod;
    }
    if (NextRight(inside_left) && !NextRight(outside_right))
    {
        outside_right->thread = NextRight(inside_left);
        outside_right->mod += inside_left_mod - outside_right_mod;
    }
    if (NextLeft(inside_right) && !NextLeft(outside_left))
    {
        outside_left->thread = NextLeft(inside_right);
        outside_left->mod += inside_right_mod - outside_left_mod;
        default_ancestor = node;
    }
    return default_ancestor;
}

} // namespace

void ComputeBuchheimLayout(BuchheimNode* root, const LayoutConfig& config) noexcept
{
    if (!root)
        return;

    root->parent = nullptr;
    root->number = 1;
    BuchheimNode* node = root;
    // Reset all nodes, including hidden descendants, and repair the topology.
    while (node)
    {
        node->x = node->y = node->prelim = node->mod = node->shift = node->change = 0.0f;
        node->thread = nullptr;
        node->ancestor = node;
        node->default_ancestor_ = node->children.empty() ? nullptr : node->children.front().get();
        for (std::size_t i = 0; i < node->children.size(); ++i)
        {
            node->children[i]->parent = node;
            node->children[i]->number = i + 1;
        }
        if (!node->children.empty())
        {
            node = node->children.front().get();
            continue;
        }
        while (node != root && node->number == node->parent->children.size())
            node = node->parent;
        node = node == root ? nullptr : node->parent->children[node->number].get();
    }

    node = root;
    while (!node->is_leaf())
        node = node->leftmost_child();
    while (node)
    {
        BuchheimNode* left = node->left_sibling();
        if (node->is_leaf())
            node->prelim = left ? left->prelim + config.node_width + config.sibling_distance : 0.0f;
        else
        {
            ExecuteShifts(node);
            const float midpoint = (node->leftmost_child()->prelim + node->rightmost_child()->prelim) * 0.5f;
            node->prelim = left ? left->prelim + config.node_width + config.sibling_distance : midpoint;
            if (left)
                node->mod = node->prelim - midpoint;
        }
        BuchheimNode* parent = node->parent;
        if (!parent)
            break;
        parent->default_ancestor_ = Apportion(node, parent->default_ancestor_, config);
        if (node->number < parent->children.size())
        {
            node = parent->children[node->number].get();
            while (!node->is_leaf())
                node = node->leftmost_child();
        }
        else
            node = parent;
    }

    const float root_prelim = root->prelim;
    float modifier = 0.0f;
    std::size_t depth = 0;
    node = root;
    while (node)
    {
        node->x = node->prelim + modifier - root_prelim;
        node->y = static_cast<float>(depth) * config.level_height;
        if (!node->is_leaf())
        {
            modifier += node->mod;
            ++depth;
            node = node->leftmost_child();
            continue;
        }
        while (node != root && node->number == node->parent->children.size())
        {
            node = node->parent;
            modifier -= node->mod;
            --depth;
        }
        node = node == root ? nullptr : node->parent->children[node->number].get();
    }
}

void ComputeTreeAABB(const BuchheimNode* root, ImVec2& minimum, ImVec2& maximum,
                     const LayoutConfig& config) noexcept
{
    minimum = maximum = ImVec2(0.0f, 0.0f);
    if (!root)
        return;

    const BuchheimNode* node = root;
    bool first = true;
    while (node)
    {
        const float radius = std::min(config.node_width, config.node_height) * 0.5f;
        const float half_width = node->shape == NodeShape::Circle ? radius : config.node_width * 0.5f;
        const float half_height = node->shape == NodeShape::Circle ? radius : config.node_height * 0.5f;
        if (first)
        {
            minimum = ImVec2(node->x - half_width, node->y - half_height);
            maximum = ImVec2(node->x + half_width, node->y + half_height);
            first = false;
        }
        else
        {
            minimum.x = std::min(minimum.x, node->x - half_width);
            minimum.y = std::min(minimum.y, node->y - half_height);
            maximum.x = std::max(maximum.x, node->x + half_width);
            maximum.y = std::max(maximum.y, node->y + half_height);
        }
        if (!node->is_leaf())
        {
            node = node->leftmost_child();
            continue;
        }
        while (node != root && node->number == node->parent->children.size())
            node = node->parent;
        node = node == root ? nullptr : node->parent->children[node->number].get();
    }
}

} // namespace ImGuiTreeGraph
