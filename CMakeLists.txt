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
# Our core/ and engine/ classes are header-only, so they do NOT go here: they are pulled in
# by #include from main.cpp. What must be compiled is:
#   - main.cpp                (our program)
#   - external/glad/glad.c    (the GLAD implementation: it defines the OpenGL functions)
add_executable(dungeon
    src/main.cpp
    external/glad/glad.c

    # LV - Aggiunti renderer e framebuffer 

    # M1: the renderer is split into a .h (interface, see src/render/renderer.h) and a
    # .cpp (implementation) because it is bigger and more central than the header-only
    # engine/ classes, so it gets its own translation unit like main.cpp does.
    src/render/renderer.cpp

    # Off-screen rendering scaffolding (used later by shadow mapping / fog); see
    # src/render/framebuffer.h for what it is for.
    src/render/framebuffer.cpp

    # --- add here LATER, when we create them ---
    # src/world/world.cpp

    # --- add here LATER the Dear ImGui source files (needed for the HUD in M2) ---
    # external/imgui/imgui.cpp
    # external/imgui/imgui_draw.cpp
    # external/imgui/imgui_tables.cpp
    # external/imgui/imgui_widgets.cpp
    # external/imgui/imgui_impl_glfw.cpp
    # external/imgui/imgui_impl_opengl3.cpp
)

# The prebuilt GLFW from the lab was compiled with the STATIC RELEASE C runtime (it pulls in
# LIBCMT). We build with the same runtime (/MT) in every configuration, otherwise the linker
# warns about conflicting runtime libraries (LNK4098). Needs CMake policy CMP0091, which is
# NEW by default since CMake 3.15.
set_property(TARGET dungeon PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded")

# --- where to look for header files ---
#   src      -> lets us write  #include "core/scene.h" , #include "engine/mesh.h"
#   external -> lets us write  #include <glad/glad.h> , <glfw/glfw3.h> , <glm/glm.hpp>
target_include_directories(dungeon PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/external
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

#LV - Aggiunte shaders
# main.cpp loads them with a plain relative path ("shaders/basic.vert"), which is
# resolved against the program's WORKING DIRECTORY at run time (not against the source
# tree). That working directory is normally the executable's own folder (e.g.
# build/Debug/), so we copy shaders/ there after every build. This way "Ctrl+F5" in
# Visual Studio, or running dungeon.exe by hand from build/Debug/, both just work; you
# do not need to configure a custom working directory by hand.
add_custom_command(TARGET dungeon POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/shaders
        $<TARGET_FILE_DIR:dungeon>/shaders
)