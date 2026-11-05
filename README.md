# imtree

**Explore your search trees in Dear ImGui.**

imtree is a C++20 addon for [Dear ImGui](https://github.com/ocornut/imgui)
that brings your algorithm's decisions into your debugging tools:
states, actions, probabilities, and scores.
Automatic layout handles placement; you navigate the tree and inspect what matters.

![A complete decision tree with three actions, six outcomes, and their scores](docs/images/imgui-tree-graph-demo.png)

*One position, three actions, six outcomes: the entire tree fits on screen.
Edges show probabilities, and each node carries its metrics.*

## From results to decisions

- **Understand the search.** See visits, values, and probabilities from your
  MCTS, minimax, or planner alongside the decisions that produced them.
- **Keep your bearings.** Cursor-centered zoom, panning, auto-fit, and a
  minimap help you navigate.
- **Focus on a branch.** Collapse subtrees with a click without resetting
  the camera.
- **Connect your inspector.** Read the clicked or hovered node, mouse button,
  modifiers, and canvas position.
- **Make it yours.** Multiline labels, per-node colors or a color callback,
  shapes, and edge badges.

![A collapsed branch keeps the focus on the remaining decisions](docs/images/imgui-tree-graph-collapsed.png)

*The same tree with one branch collapsed. Selection is available to your
inspector independently of collapsing.*

## Try the demo

The demo includes the small tree above and a nine-level MCTS tree.
CMake fetches Dear ImGui and GLFW automatically.

**Linux / Debian / Ubuntu**

```sh
sudo apt-get install cmake g++ git libgl1-mesa-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
./build/imgui_tree_graph_demo
```

**Windows:** use the same CMake commands with Visual Studio 2022 and its C++
tools, then run `build/Release/imgui_tree_graph_demo.exe`.
**macOS:** install Xcode Command Line Tools and use the same CMake commands.

Requirements: CMake >= 3.24, Git, C++20, and OpenGL >= 3.2 for the demo.
The first download requires Internet access; use `CPM_SOURCE_CACHE` to
reuse dependencies.

The scene selector switches between the small tree and the larger MCTS tree.
To open a view directly:

```sh
./build/imgui_tree_graph_demo --scene overview --view fit
./build/imgui_tree_graph_demo --scene overview --view fit --collapse-first
./build/imgui_tree_graph_demo --scene large --view root
```

| Input | Action |
| --- | --- |
| Mouse wheel | Zoom around the cursor |
| Right or middle drag | Pan |
| Left-click a node | Select and collapse/expand |
| Click/drag in the minimap | Recenter the view |
| Fit entire tree | Fit the visible tree |
| Disable Click collapses | Select without collapsing |

## Your first tree

Build the model once, then render it in your ImGui loop:

```cpp
#include <imgui_tree_graph/TreeRenderer.hpp>

using namespace ImGuiTreeGraph;

// Initialization: keep the model and renderer alive between frames.
auto root = std::make_unique<BuchheimNode>("Position");
auto move = std::make_unique<BuchheimNode>("Advance");
move->shape = NodeShape::RoundedRectangle;
move->secondary_label = "N=42\nQ=0.7";
move->edge_from_parent.label = "p=0.8";
root->add_child(std::move(move));

LayoutConfig layout;
ComputeBuchheimLayout(root.get(), layout);
TreeRenderer renderer;

// Every frame:
ImGui::Begin("Search");
renderer.render("tree", root.get(), layout);
if (const auto* node = renderer.interaction().hovered) {
    ImGui::BeginTooltip();
    ImGui::TextUnformatted(node->secondary_label.c_str());
    ImGui::EndTooltip();
}
ImGui::End();
```

`BuchheimNode` owns its children and labels. Call `ComputeBuchheimLayout` again
after changing the structure or layout parameters.
Rendering a fresh tree and interactive collapsing initialize or update the
layout automatically.

## Integration

The library uses Dear ImGui; GLFW and OpenGL are only needed by the demo.
You can provide your application's existing ImGui target:

```cmake
set(IMGUI_TREE_GRAPH_BUILD_DEMO OFF CACHE BOOL "" FORCE)
set(IMGUI_TREE_GRAPH_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(IMGUI_TREE_GRAPH_IMGUI_TARGET my_imgui CACHE STRING "" FORCE)
add_subdirectory(external/imtree)
target_link_libraries(my_app PRIVATE ImGuiTreeGraph::imgui_tree_graph)
```

Without an external target, CMake fetches Dear ImGui **v1.91.9b**
(and GLFW **3.4** for the demo).

## API and customization

| API | Purpose |
| --- | --- |
| `BuchheimNode::add_child` | Transfer ownership of a child and establish its parent and sibling index |
| `LayoutConfig` | Node dimensions and level/subtree spacing |
| `RendererConfig` | Grid, minimap, detail thresholds, and zoom limits |
| `TreeRenderer::camera()` | Access pan and zoom |
| `TreeRenderer::fit_to_tree()` | Fit visible nodes |
| `TreeRenderer::interaction()` | Read hover, click, double-click, and modifiers |
| `RendererConfig::node_color` | Provide custom colors for the canvas and minimap |
| `TreeRenderer::stats()` | Count visited nodes, skipped subtrees, and clusters in each view |

A nonzero `fill_color` overrides the callback and the player 1/player 2/chance
palette. Set `collapse_on_click = false` to reserve clicks for selection.
Interaction pointers are borrowed and events reset each render: keep the model
alive and do not mutate it while drawing.

The headers are the API reference:
[`BuchheimLayout.hpp`](include/imgui_tree_graph/BuchheimLayout.hpp) ·
[`TreeRenderer.hpp`](include/imgui_tree_graph/TreeRenderer.hpp).
The [demo](examples/demo.cpp) shows how to use them with GLFW/OpenGL3.

## Performance and scope

Placement uses the Buchheim-Jünger-Schulz algorithm in **O(N)**.
Rendering skips offscreen primitives and simplifies shapes, text, and badges
as you zoom out. Once ImGui's buffers have warmed up, the renderer does not
allocate per frame, excluding allocations in your own callback.

Layout also prepares hierarchical bounds: the renderer can skip an offscreen
subtree without visiting all its descendants. When zoomed out, subtrees with
a very small projected footprint become clusters. This is purely visual and
does not change their `collapsed` state. The minimap uses its own detail level.

This does not guarantee constant-time rendering: the worst case remains O(N),
particularly when many subtrees intersect the view. The model still owns
individually allocated nodes; it does not yet provide a flat pool or a
zero-copy adapter to your application's tree.

### Tuning level of detail

`RendererConfig::cluster_pixels` and `minimap_cluster_pixels` independently set
the maximum projected subtree extent in pixels. Set either to zero to disable
clustering in that view; `subtree_culling = false` disables offscreen subtree
pruning.

A cluster represents its subtree's root. Interaction events expose that node
and `hovered_hidden_count` / `clicked_hidden_count`, the number of descendants
hidden by clustering. Zooming in reveals them without modifying the model.

Edge badges take priority: when enabled at the current zoom, the edge pass
preserves even very long labels and disables canvas clustering. Labels, colors,
and shapes can change without rebuilding this cache. After changing an edge's
thickness, call `RefreshTreeVisualBounds(root)` or recompute the layout.

`renderer.stats()` separates canvas, edge, hit-test, and minimap visits.
Counters reset each render, so you can verify skipped work without relying
on timing measurements.

> **ImGui backend:** with 16-bit indices, enable
> `ImGuiBackendFlags_RendererHasVtxOffset` (already supported by the demo's
> OpenGL3 backend), or compile ImGui and its consumers with 32-bit indices.
> The renderer refuses to draw if neither option is available.

<details>
<summary>Model and camera details</summary>

- Children must be non-null. `add_child` rejects null pointers.
  Editing an already laid-out tree, even through `add_child`, still requires
  explicit relayout.
- The model represents a tree, not a graph: unfold cycles and shared states
  into separate occurrences. Identifiers are metadata and need not be unique.
- Dimensions are uniform, in world units. Circles use the smaller dimension
  as their diameter; `level_height` separates level centers.
  The root is anchored at `(0,0)`, and bounds include visible shapes.
- Camera pan is measured in canvas-local pixels. Setting the minimum zoom too
  high may prevent fitting a large tree.
- The mutable `fit_to_tree` overload initializes a fresh tree's layout.
  The const overload fits existing coordinates without mutation; for a fresh
  tree, it uses O(depth) temporary storage.

</details>

<details>
<summary>Demo screenshots</summary>

Both images come from the compiled demo running at 1440 x 900.
The second uses `--collapse-first` to show collapsing and the inspector.
On Linux, with `xvfb`, `xauth`, `xdotool`, and ImageMagick installed, this command
captures the complete view (add `--collapse-first` for the other view):

```sh
xvfb-run -a -s '-screen 0 1440x900x24' sh -c '
  LIBGL_ALWAYS_SOFTWARE=1 ./build/imgui_tree_graph_demo --scene overview --view fit &
  demo=$!
  trap "kill $demo 2>/dev/null || true" EXIT
  sleep 8
  kill -0 "$demo" || exit 1
  xdotool mousemove --sync 1439 899
  sleep 1
  import -window root docs/images/imgui-tree-graph-demo.png
'
```

</details>

<details>
<summary>Tests and installation</summary>

```sh
ctest --test-dir build -C Release --output-on-failure
cmake --install build --prefix /your/prefix
```

Tests cover layout and rendering without an external framework. CI builds on
Linux, Windows, and macOS, checks installation, and starts the demo under
Xvfb/Mesa. A Linux job uses AddressSanitizer/UBSan.

Installation exports `ImGuiTreeGraph::imgui_tree_graph` in
`lib/cmake/imgui_tree_graph/imgui_tree_graphTargets.cmake`. Provide your own
Dear ImGui includes and linkage with an ABI-compatible configuration.
To build the demo with an external ImGui target, also set
`IMGUI_TREE_GRAPH_IMGUI_SOURCE_DIR`.

</details>
