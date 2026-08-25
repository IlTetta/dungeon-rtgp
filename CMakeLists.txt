# CMake build script for the Dungeon RTGP project.
#
# IMPORTANT: the prebuilt libraries in libs/win were built with Visual Studio 2022 (vc143),
# 64 bit. So this project must be built with the MSVC compiler, 64 bit (x64). With MinGW it
# would not link. In VS Code, pick the kit "Visual Studio Community 2022 - amd64".

cmake_minimum_required(VERSION 3.16)
project(dungeon_rtgp LANGUAGES C CXX)

# We use C++17.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- the files to compile ---
# Our core/, engine/, world/ and dungeon/ classes are header-only, so they do NOT go here:
# they are pulled in by #include. What must be compiled:
add_executable(dungeon
    src/main.cpp
    external/glad/glad.c
    external/stb/stb_image_impl.cpp   # compiles the stb_image library implementation once

    # the renderer and the off-screen framebuffer are split into .h + .cpp (they are bigger
    # and more central than the header-only classes), so they get their own translation units
    src/render/renderer.cpp
    src/render/framebuffer.cpp

    # Dear ImGui (performance HUD): 4 core files + 2 backends (GLFW input, OpenGL3 rendering).
    # ImGui is not a separate library: it is compiled together with our code.
    external/imgui/imgui.cpp
    external/imgui/imgui_draw.cpp
    external/imgui/imgui_tables.cpp
    external/imgui/imgui_widgets.cpp
    external/imgui/imgui_impl_glfw.cpp
    external/imgui/imgui_impl_opengl3.cpp
)

# The prebuilt GLFW from the lab was compiled with the STATIC RELEASE C runtime (it pulls in
# LIBCMT). We build with the same runtime (/MT) in every configuration, otherwise the linker
# warns about conflicting runtime libraries (LNK4098). Needs CMake policy CMP0091, which is
# NEW by default since CMake 3.15.
set_property(TARGET dungeon PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded")

# --- where to look for header files ---
#   src            -> #include "core/scene.h" , "engine/mesh.h" , "render/renderer.h" , ...
#   external       -> #include <glad/glad.h> , <glfw/glfw3.h> , <glm/glm.hpp>
#   external/imgui -> #include "imgui.h" (used by us and by the ImGui sources)
target_include_directories(dungeon PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/external
    ${CMAKE_SOURCE_DIR}/external/imgui
)

# --- where to look for the prebuilt .lib files ---
target_link_directories(dungeon PRIVATE
    ${CMAKE_SOURCE_DIR}/libs/win
)

# --- libraries to link against ---
#   glfw3    -> window creation and input (static library)
#   opengl32 -> the OpenGL library on Windows
#   gdi32    -> Windows graphics interface, required by the static GLFW
target_link_libraries(dungeon PRIVATE
    glfw3
    opengl32
    gdi32
)

# The program loads the shaders with relative paths ("shaders/ggx.vert", ...). To make sure
# they are found no matter the working directory, after each build we COPY the shaders folder
# next to the executable. Visual Studio runs the program from the executable's folder, so the
# relative paths then resolve correctly.
add_custom_command(TARGET dungeon POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/shaders"
        "$<TARGET_FILE_DIR:dungeon>/shaders")

# Same for the 3D model assets (loaded at run time with paths like "assets/models/bunny_lp.obj").
add_custom_command(TARGET dungeon POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:dungeon>/assets")

# Also set the VS debugger working directory to the project root (used when VS generates a normal
# solution). Harmless when it is not applied, because of the copy above.
set_target_properties(dungeon PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# --- test tool: BSP dungeon generator ---
# A separate console executable that generates a dungeon and prints it as ASCII. It is pure
# standard C++ (no OpenGL / no libraries), so we can run and debug the generation logic on its
# own, without the 3D window. In Visual Studio pick "bsp_test.exe" as the startup item to run it.
add_executable(bsp_test
    src/dungeon/dungeon_test.cpp
)
# needs "src" (our headers) and "external" (glm, and glad which mesh.h includes). It does NOT
# link OpenGL: the test only calls the pure layout code, never the GPU functions.
target_include_directories(bsp_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/external
)
