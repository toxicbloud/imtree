#include "imgui_tree_graph/BuchheimLayout.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <random>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace ImGuiTreeGraph;

namespace
{

void Check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

bool Near(float left, float right)
{
    return std::abs(left - right) < 0.02f;
}

BuchheimNode* Child(BuchheimNode& node)
{
    node.children.push_back(std::make_unique<BuchheimNode>());
    return node.children.back().get();
}

std::vector<BuchheimNode*> Nodes(BuchheimNode& root, bool visible = true)
{
    std::vector<BuchheimNode*> nodes{&root};
    for (std::size_t i = 0; i < nodes.size(); ++i)
        if (!visible || !nodes[i]->collapsed)
            for (auto& child : nodes[i]->children)
                nodes.push_back(child.get());
    return nodes;
}

void Validate(BuchheimNode& root, const LayoutConfig& config = {})
{
    Check(root.parent == nullptr && Near(root.x, 0) && Near(root.y, 0), "root anchoring");
    std::map<float, std::vector<BuchheimNode*>> levels;
    for (BuchheimNode* node : Nodes(root))
    {
        levels[node->y].push_back(node);
        if (node->parent)
        {
            Check(Near(node->y - node->parent->y, config.level_height), "level spacing");
            Check(node->parent->children[node->number - 1].get() == node, "parent/index assignment");
        }
        if (!node->is_leaf())
        {
            Check(Near(node->x, (node->children.front()->x + node->children.back()->x) / 2),
                  "parent centering");
            for (std::size_t i = 1; i < node->children.size(); ++i)
                Check(node->children[i]->x - node->children[i - 1]->x + 0.02f
                          >= config.node_width + config.sibling_distance, "sibling spacing");
        }
    }
    for (auto& [height, nodes] : levels)
    {
        (void)height;
        std::sort(nodes.begin(), nodes.end(), [](const auto* a, const auto* b) { return a->x < b->x; });
        for (std::size_t i = 1; i < nodes.size(); ++i)
        {
            const float gap = nodes[i]->parent == nodes[i - 1]->parent
                            ? config.sibling_distance : config.subtree_distance;
            Check(nodes[i]->x - nodes[i - 1]->x + 0.02f >= config.node_width + gap,
                  "same-level overlap or subtree spacing");
        }
    }
}

void TestSimpleAndBounds()
{
    BuchheimNode root("root");
    Check(root.id == 0 && root.primary_label == "root" && root.secondary_label.empty(),
          "model identity and label defaults");
    Check(root.shape == NodeShape::Circle && root.fill_color == 0, "model visual defaults");
    Check(root.edge_from_parent.label.empty() && root.edge_from_parent.thickness == 1,
          "incoming edge defaults");
    auto* a = Child(root);
    auto* b = Child(root);
    auto* c = Child(root);
    root.shape = a->shape = b->shape = c->shape = NodeShape::RoundedRectangle;
    ComputeBuchheimLayout(&root);
    Validate(root);
    Check(Near(a->x, -124) && Near(b->x, 0) && Near(c->x, 124), "exact sibling positions");
    Check(a->left_sibling() == nullptr && b->left_sibling() == a
              && c->leftmost_sibling() == a, "sibling helpers");
    Check(root.leftmost_child() == a && root.rightmost_child() == c, "child helpers");
    ImVec2 minimum, maximum;
    ComputeTreeAABB(&root, minimum, maximum);
    Check(Near(minimum.x, -174) && Near(minimum.y, -30)
              && Near(maximum.x, 174) && Near(maximum.y, 140), "rectangle bounds");
    root.collapsed = true;
    root.shape = NodeShape::Circle;
    ComputeBuchheimLayout(&root);
    ComputeTreeAABB(&root, minimum, maximum);
    Check(Near(minimum.x, -30) && Near(minimum.y, -30)
              && Near(maximum.x, 30) && Near(maximum.y, 30), "collapsed circle bounds");
    Check(root.is_leaf() && !root.leftmost_child() && !root.rightmost_child(), "collapsed helpers");
    minimum = maximum = ImVec2(42, 42);
    ComputeBuchheimLayout(nullptr);
    ComputeTreeAABB(nullptr, minimum, maximum);
    Check(minimum.x == 0 && minimum.y == 0 && maximum.x == 0 && maximum.y == 0, "null bounds");
}

void TestOwnershipAndFreshBounds()
{
    BuchheimNode root;
    auto owned = std::make_unique<BuchheimNode>("first");
    auto* address = owned.get();
    auto& first = root.add_child(std::move(owned));
    auto& second = root.add_child(std::make_unique<BuchheimNode>("second"));
    auto& grandchild = first.add_child(std::make_unique<BuchheimNode>());
    Check(!owned && &first == address && first.parent == &root && first.number == 1,
          "add_child transfers ownership without moving nodes");
    Check(second.parent == &root && second.number == 2 && second.left_sibling() == &first,
          "add_child maintains sibling indices");
    Check(grandchild.parent == &first && grandchild.number == 1, "nested ownership links");
    try
    {
        root.add_child(nullptr);
        Check(false, "null add_child must throw");
    }
    catch (const std::invalid_argument&) {}
    Check(root.children.size() == 2, "null add_child leaves tree unchanged");
    // Legacy direct ownership edits must also be safe before layout, without
    // mutating links, coordinates, or layout scratch through a const pointer.
    auto* direct = Child(second);
    direct->x = 300;
    direct->y = 400;
    ImVec2 lo, hi;
    const BuchheimNode* const_root = &root;
    ComputeTreeAABB(const_root, lo, hi);
    Check(hi.x == 330 && hi.y == 430 && direct->parent == nullptr &&
              !root.has_layout() && root.ancestor == nullptr, "fresh const ownership bounds");
    second.collapsed = true;
    ComputeTreeAABB(const_root, lo, hi);
    Check(hi.x == 30 && hi.y == 30, "fresh const bounds skip collapsed descendants");
    second.collapsed = false;
    ComputeBuchheimLayout(&root);
    Validate(root);
    Check(root.has_layout() && direct->has_layout() && direct->parent == &second,
          "layout marks and repairs all ownership");
    root.children.erase(root.children.begin());
    ComputeBuchheimLayout(&root);
    Check(root.children.front()->number == 1, "legacy external edits remain supported");
}

void TestAsymmetricAndRepeatability()
{
    for (unsigned seed = 0; seed < 80; ++seed)
    {
        std::mt19937 random(seed);
            BuchheimNode root;
        auto all = Nodes(root);
        for (int i = 0; i < 400; ++i)
        {
            auto* parent = all[random() % all.size()];
            all.push_back(Child(*parent));
        }
        LayoutConfig config;
        config.node_width = 47;
        config.node_height = 31;
        config.sibling_distance = 13;
        config.subtree_distance = 29;
        config.level_height = 73;
        ComputeBuchheimLayout(&root, config);
        Validate(root, config);
        std::vector<std::pair<float, float>> positions;
        for (auto* node : all)
            positions.emplace_back(node->x, node->y);
        ComputeBuchheimLayout(&root, config);
        for (std::size_t i = 0; i < all.size(); ++i)
            Check(all[i]->x == positions[i].first && all[i]->y == positions[i].second,
                  "repeatability including modifier reset");
        for (std::size_t i = 1; i < all.size(); ++i)
            all[i]->collapsed = random() % 5 == 0;
        ComputeBuchheimLayout(&root, config);
        Validate(root, config);
        for (auto* node : all)
            node->collapsed = false;
        ComputeBuchheimLayout(&root, config);
        Validate(root, config);
        for (std::size_t i = 0; i < all.size(); ++i)
            Check(all[i]->x == positions[i].first && all[i]->y == positions[i].second,
                  "collapse/expand restores positions");
    }
}

void TestThreads()
{
    BuchheimNode root;
    auto* left = Child(root);
    Child(*left);
    auto* inner = Child(*left);
    for (int i = 0; i < 12; ++i)
        inner = Child(*inner);
    Child(root);
    auto* right = Child(root);
    auto* branch = Child(*right);
    for (int i = 0; i < 8; ++i)
        branch = Child(*branch);
    branch = Child(*right);
    Child(*branch);
    Child(*branch);
    ComputeBuchheimLayout(&root);
    Validate(root);
    bool has_thread = false;
    for (auto* node : Nodes(root))
        has_thread = has_thread || node->thread != nullptr;
    Check(has_thread, "asymmetric case exercises threads");
    ComputeBuchheimLayout(&root);
    Validate(root);
}

void TestDeepAndWide()
{
    BuchheimNode root;
    root.shape = NodeShape::RoundedRectangle;
    BuchheimNode* node = &root;
    constexpr int depth = 100000;
    for (int i = 0; i < depth; ++i)
        node = Child(*node);
    node->y = 1234;
    ImVec2 minimum, maximum;
    ComputeTreeAABB(static_cast<const BuchheimNode*>(&root), minimum, maximum);
    Check(maximum.y == 1264 && node->parent == nullptr, "stack-safe fresh const bounds");
    ComputeBuchheimLayout(&root);
    Check(node->x == 0 && node->y == static_cast<float>(depth) * 110, "stack-safe deep layout");
    ComputeTreeAABB(&root, minimum, maximum);
    Check(Near(minimum.x, -50) && maximum.y == node->y + 30, "stack-safe deep bounds");
    root.collapsed = true;
    ComputeBuchheimLayout(&root);
    Check(node->x == 0 && node->y == 0 && node->mod == 0, "hidden descendants reset");

    BuchheimNode wide;
    for (int i = 0; i < 20000; ++i)
        Child(wide);
    ComputeBuchheimLayout(&wide);
    Check(Near(wide.children.back()->x - wide.children.front()->x, 19999.0f * 124),
          "wide sibling indexing");
}

} // namespace

int main()
{
    try
    {
        TestSimpleAndBounds();
        TestOwnershipAndFreshBounds();
        TestAsymmetricAndRepeatability();
        TestThreads();
        TestDeepAndWide();
        std::cout << "All layout tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Layout test failure: " << error.what() << '\n';
        return 1;
    }
}
