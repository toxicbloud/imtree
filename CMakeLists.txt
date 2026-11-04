cmake_minimum_required(VERSION 3.24)
project(imgui_tree_graph VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
option(IMGUI_TREE_GRAPH_BUILD_DEMO "Build the GLFW/OpenGL3 demonstration" ON)
option(IMGUI_TREE_GRAPH_BUILD_TESTS "Build regression tests" ON)

# Consumers can provide their own compatible Dear ImGui target.
set(IMGUI_TREE_GRAPH_IMGUI_TARGET "" CACHE STRING "Existing Dear ImGui CMake target")
if(NOT IMGUI_TREE_GRAPH_IMGUI_TARGET OR IMGUI_TREE_GRAPH_BUILD_DEMO)
    set(CPM_DOWNLOAD_VERSION 0.40.8)
    if(NOT CPM_SOURCE_CACHE AND DEFINED ENV{CPM_SOURCE_CACHE})
        set(CPM_SOURCE_CACHE "$ENV{CPM_SOURCE_CACHE}")
    endif()
    if(CPM_SOURCE_CACHE)
        set(CPM_DOWNLOAD_LOCATION "${CPM_SOURCE_CACHE}/cmake/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
    else()
        set(CPM_DOWNLOAD_LOCATION "${CMAKE_BINARY_DIR}/cmake/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
    endif()
    set(CPM_DOWNLOAD_HASH 78ba32abdf798bc616bab7c73aac32a17bbd7b06ad9e26a6add69de8f3ae4791)
    if(EXISTS "${CPM_DOWNLOAD_LOCATION}")
        file(SHA256 "${CPM_DOWNLOAD_LOCATION}" CPM_CACHED_HASH)
        if(NOT CPM_CACHED_HASH STREQUAL CPM_DOWNLOAD_HASH)
            file(REMOVE "${CPM_DOWNLOAD_LOCATION}")
        endif()
    endif()
    if(NOT EXISTS "${CPM_DOWNLOAD_LOCATION}")
        file(DOWNLOAD
            "https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake"
            "${CPM_DOWNLOAD_LOCATION}"
            EXPECTED_HASH "SHA256=${CPM_DOWNLOAD_HASH}"
            TLS_VERIFY ON STATUS CPM_DOWNLOAD_STATUS)
        list(GET CPM_DOWNLOAD_STATUS 0 CPM_DOWNLOAD_RESULT)
        if(NOT CPM_DOWNLOAD_RESULT EQUAL 0)
            file(REMOVE "${CPM_DOWNLOAD_LOCATION}")
            message(FATAL_ERROR "CPM download failed: ${CPM_DOWNLOAD_STATUS}")
        endif()
    endif()
    include("${CPM_DOWNLOAD_LOCATION}")
endif()

if(NOT IMGUI_TREE_GRAPH_IMGUI_TARGET)
    CPMAddPackage(NAME imgui
        GITHUB_REPOSITORY ocornut/imgui
        GIT_TAG v1.91.9b
        DOWNLOAD_ONLY YES)
    add_library(imgui_tree_graph_imgui STATIC
        "${imgui_SOURCE_DIR}/imgui.cpp"
        "${imgui_SOURCE_DIR}/imgui_draw.cpp"
        "${imgui_SOURCE_DIR}/imgui_tables.cpp"
        "${imgui_SOURCE_DIR}/imgui_widgets.cpp")
    target_include_directories(imgui_tree_graph_imgui PUBLIC
        "$<BUILD_INTERFACE:${imgui_SOURCE_DIR}>")
    set_target_properties(imgui_tree_graph_imgui PROPERTIES POSITION_INDEPENDENT_CODE ON)
    set(IMGUI_TREE_GRAPH_IMGUI_TARGET imgui_tree_graph_imgui)
endif()
if(NOT TARGET "${IMGUI_TREE_GRAPH_IMGUI_TARGET}")
    message(FATAL_ERROR "IMGUI_TREE_GRAPH_IMGUI_TARGET must name an existing target")
endif()

add_library(imgui_tree_graph
    src/BuchheimLayout.cpp
    src/TreeRenderer.cpp)
add_library(ImGuiTreeGraph::imgui_tree_graph ALIAS imgui_tree_graph)
target_compile_features(imgui_tree_graph PUBLIC cxx_std_20)
set_target_properties(imgui_tree_graph PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(imgui_tree_graph PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_link_libraries(imgui_tree_graph PUBLIC
    "$<BUILD_INTERFACE:${IMGUI_TREE_GRAPH_IMGUI_TARGET}>")
if(MSVC)
    target_compile_options(imgui_tree_graph PRIVATE /W4 /permissive-)
else()
    target_compile_options(imgui_tree_graph PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(IMGUI_TREE_GRAPH_BUILD_DEMO)
    CPMAddPackage(NAME glfw GITHUB_REPOSITORY glfw/glfw GIT_TAG 3.4
        OPTIONS "GLFW_BUILD_DOCS OFF" "GLFW_BUILD_TESTS OFF"
                "GLFW_BUILD_EXAMPLES OFF" "GLFW_INSTALL OFF"
                "GLFW_BUILD_WAYLAND OFF")
    find_package(OpenGL REQUIRED)
    # Backends must use exactly the same ImGui source revision as the core.
    set(IMGUI_TREE_GRAPH_IMGUI_SOURCE_DIR "${imgui_SOURCE_DIR}" CACHE PATH
        "ImGui source directory (required for demo with an external ImGui target)")
    if(NOT EXISTS "${IMGUI_TREE_GRAPH_IMGUI_SOURCE_DIR}/backends/imgui_impl_glfw.cpp")
        message(FATAL_ERROR "Set IMGUI_TREE_GRAPH_IMGUI_SOURCE_DIR to your ImGui source directory")
    endif()
    add_executable(imgui_tree_graph_demo examples/demo.cpp
        "${IMGUI_TREE_GRAPH_IMGUI_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
        "${IMGUI_TREE_GRAPH_IMGUI_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp")
    target_include_directories(imgui_tree_graph_demo PRIVATE
        "${IMGUI_TREE_GRAPH_IMGUI_SOURCE_DIR}/backends")
    target_link_libraries(imgui_tree_graph_demo PRIVATE
        ImGuiTreeGraph::imgui_tree_graph glfw OpenGL::GL)
endif()

if(IMGUI_TREE_GRAPH_BUILD_TESTS)
    enable_testing()
    add_executable(imgui_tree_graph_layout_tests tests/layout_tests.cpp)
    target_link_libraries(imgui_tree_graph_layout_tests PRIVATE ImGuiTreeGraph::imgui_tree_graph)
    add_test(NAME layout COMMAND imgui_tree_graph_layout_tests)
    add_executable(imgui_tree_graph_renderer_tests tests/renderer_tests.cpp)
    target_link_libraries(imgui_tree_graph_renderer_tests PRIVATE ImGuiTreeGraph::imgui_tree_graph)
    add_test(NAME renderer COMMAND imgui_tree_graph_renderer_tests)
endif()

install(TARGETS imgui_tree_graph EXPORT imgui_tree_graphTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
# Installed users supply Dear ImGui headers/linkage; ImGui is never vendored into the install.
install(EXPORT imgui_tree_graphTargets NAMESPACE ImGuiTreeGraph::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/imgui_tree_graph")
