// main.cpp
//
// Integrated application (M1 + M2):
//   - Andrea's side: procedural BSP dungeon -> 3D geometry (the Scene), FPS camera with
//     ad-hoc wall collisions, and the ImGui performance HUD.
//   - Lorenzo's side: the Renderer (GGX forward shading) that actually draws the Scene.
//   - The frustum culling now lives inside the Renderer (it iterates the objects anyway).
//
// Controls: WASD move, Shift sprint, mouse look, C toggle culling, ESC quit.

#ifdef _WIN32
    #define APIENTRY __stdcall
#endif

#include <glad/glad.h>
#include <glfw/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <string>
#include <iostream>

#include "engine/camera.h"
#include "core/scene.h"
#include "core/metrics.h"
#include "dungeon/dungeon_generator.h"
#include "world/dungeon_geometry.h"
#include "world/props.h"
#include "world/collision.h"
#include "render/renderer.h"

// Dear ImGui (performance HUD)
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

// window size
const unsigned int WIDTH = 1280;
const unsigned int HEIGHT = 720;

// radius of the player sphere used for wall collisions
const float PLAYER_RADIUS = 0.4f;

// frustum culling on/off (toggled with the C key), to compare performance ON vs OFF
bool cullingEnabled = true;

// globals used by the input callbacks (same simple approach as the lab code)
Camera camera(glm::vec3(0.0f, 1.6f, 0.0f), true);   // start position is set later, after we build the level
bool keys[1024] = { false };
float deltaTime = 0.0f;   // time between the current frame and the previous one
float lastFrame = 0.0f;
float lastX = WIDTH / 2.0f;
float lastY = HEIGHT / 2.0f;
bool firstMouse = true;

// called by GLFW when a key is pressed or released
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mode) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);
    // toggle frustum culling on/off
    if (key == GLFW_KEY_C && action == GLFW_PRESS)
        cullingEnabled = !cullingEnabled;
    if (key >= 0 && key < 1024) {
        if (action == GLFW_PRESS)   keys[key] = true;
        if (action == GLFW_RELEASE) keys[key] = false;
    }
}

// called by GLFW when the mouse moves
void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    if (firstMouse) {            // avoid a big jump on the very first mouse event
        lastX = (float)xpos;
        lastY = (float)ypos;
        firstMouse = false;
    }
    float xoffset = (float)xpos - lastX;
    float yoffset = lastY - (float)ypos;   // reversed: screen y grows downward
    lastX = (float)xpos;
    lastY = (float)ypos;
    camera.processMouse(xoffset, yoffset);
}

// check which movement keys are held and move the camera accordingly
void applyMovements() {
    // hold SHIFT to sprint: normal speed feels right in the rooms, sprint helps in the long corridors
    camera.MovementSpeed = (keys[GLFW_KEY_LEFT_SHIFT] || keys[GLFW_KEY_RIGHT_SHIFT]) ? 15.0f : 6.0f;

    if (keys[GLFW_KEY_W]) camera.processKeyboard(FORWARD, deltaTime);
    if (keys[GLFW_KEY_S]) camera.processKeyboard(BACKWARD, deltaTime);
    if (keys[GLFW_KEY_A]) camera.processKeyboard(LEFT, deltaTime);
    if (keys[GLFW_KEY_D]) camera.processKeyboard(RIGHT, deltaTime);
}

int main() {
    // --- window + OpenGL 4.1 core context ---
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);

    GLFWwindow* window = glfwCreateWindow(WIDTH, HEIGHT, "Dungeon RTGP", nullptr, nullptr);
    if (window == nullptr) {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSetKeyCallback(window, key_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);   // capture and hide the mouse

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cout << "Failed to initialize GLAD" << std::endl;
        return -1;
    }

    // --- build the dungeon and its 3D geometry (needs the OpenGL context, so we do it now) ---
    DungeonParams params;   // default tile size / wall height
    DungeonGenerator generator(60, 30, 12345);
    Dungeon dungeon = generator.generate();
    DungeonLayout layout = buildDungeonLayout(dungeon, params);
    Scene scene = buildScene(layout);
    addProps(scene, dungeon, params);   // torches, statues, columns (loaded from assets/models)

    // place the camera at eye height in the center of the first room
    if (!dungeon.rooms.empty()) {
        Rect r = dungeon.rooms[0];
        float cx = (r.x + r.w * 0.5f) * params.tileSize;
        float cz = (r.y + r.h * 0.5f) * params.tileSize;
        camera.Position = glm::vec3(cx, 1.6f, cz);
    }

    // --- renderer (GGX forward shading) ---
    Renderer renderer("shaders/ggx.vert", "shaders/ggx.frag");
    renderer.setViewport(WIDTH, HEIGHT);

    // --- init Dear ImGui (for the performance HUD) ---
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    // "true" lets ImGui chain to the keyboard/mouse callbacks we already installed above.
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 410");

    FrameMetrics metrics;   // most fields are filled by the renderer; fps/frameTime here in main

    // --- render loop ---
    while (!glfwWindowShouldClose(window)) {
        // time management
        float currentFrame = (float)glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        glfwPollEvents();
        applyMovements();
        // push the player out of any wall it tried to walk into
        camera.Position = resolveWallCollisions(camera.Position, PLAYER_RADIUS, scene);

        // start a new ImGui frame (before drawing anything)
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // draw the whole scene (this clears the screen, does the culling, and fills the
        // object/draw/light counters in "metrics")
        renderer.cullingEnabled = cullingEnabled;
        renderer.render(scene, camera, metrics);

        // the two metrics that main is responsible for
        metrics.fps = ImGui::GetIO().Framerate;
        metrics.frameTimeMs = deltaTime * 1000.0f;

        // --- HUD window ---
        ImGui::Begin("Performance");
        ImGui::Text("FPS: %.0f  (%.2f ms)", metrics.fps, metrics.frameTimeMs);
        ImGui::Separator();
        ImGui::Text("Objects drawn : %d / %d", metrics.drawCalls, metrics.objectsTotal);
        ImGui::Text("Objects culled: %d", metrics.objectsCulled);
        ImGui::Text("Triangles     : %d", metrics.trianglesDrawn);
        ImGui::Text("Lights        : %d", metrics.activeLights);
        ImGui::Separator();
        ImGui::Text("Frustum culling: %s", cullingEnabled ? "ON" : "OFF");
        ImGui::TextDisabled("C toggle culling - WASD move - Shift sprint - ESC quit");
        ImGui::End();

        // draw the HUD on top of the scene, then present the frame
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    // --- cleanup ---
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    renderer.clean();
    glfwTerminate();
    return 0;
}
