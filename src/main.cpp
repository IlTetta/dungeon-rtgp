// main.cpp
//
// For now this program only opens a window and clears it to a color every frame.
// It is the very first thing to compile: if we see the window, it means the toolchain and
// the libraries (GLFW, GLAD) are set up correctly. Later we will add here the scene, the
// renderer, the camera and the HUD.

// On Windows, glad may end up including windows.h, which defines APIENTRY. We define it
// first to avoid a "APIENTRY redefinition" warning. (This trick comes from the lab code.)
#ifdef _WIN32
    #define APIENTRY __stdcall
#endif

#include <glad/glad.h>     // must be included BEFORE glfw
#include <glfw/glfw3.h>

#include <iostream>

// LV - Aggiunta scena e include

// --- window size ---
// kept as regular variables (not #defines) because the framebuffer-size callback below
// needs to update them when the user resizes the window.

#include <glm/glm.hpp>
#include "engine/camera.h"
#include "render/renderer.h"
#include "core/scene.h"
#include "core/metrics.h"
#include "render/test_scene.h"

int windowWidth = 1280;
int windowHeight = 720;

// --- camera + mouse look ---
// The camera and the "last known mouse position" have to be reachable both from main()
// and from the GLFW callbacks below, which GLFW itself calls and to which we cannot pass
// extra arguments. The usual (LearnOpenGL-style) fix is a small set of file-level
// variables just for this purpose; we keep everything else as local variables in main().
Camera camera(glm::vec3(0.0f, 1.6f, 4.0f), true);   // ~eye height, a few meters back from the room
float lastMouseX = windowWidth / 2.0f;
float lastMouseY = windowHeight / 2.0f;
bool firstMouseMove = true;   // avoids one big jump on the very first mouse event

// Called by GLFW whenever the window is resized. We must tell both OpenGL (glViewport)
// and our projection matrix about the new size, otherwise the image gets stretched or
// only draws into a corner of the window; Renderer::setViewport() does both at once.
void framebufferSizeCallback(GLFWwindow* window, int width, int height) {
    windowWidth = width;
    windowHeight = height;
    // the Renderer pointer is stashed in the window's "user pointer" (set in main())
    Renderer* renderer = (Renderer*)glfwGetWindowUserPointer(window);
    if (renderer != nullptr) {
        renderer->setViewport(width, height);
    }
}

// Called by GLFW whenever the mouse moves. Turns the raw mouse delta into a camera
// rotation via Camera::processMouse().
void mouseCallback(GLFWwindow* window, double xpos, double ypos) {
    if (firstMouseMove) {
        lastMouseX = (float)xpos;
        lastMouseY = (float)ypos;
        firstMouseMove = false;
    }

    float xoffset = (float)xpos - lastMouseX;
    // reversed: screen y grows downward, but we want moving the mouse UP to look up
    float yoffset = lastMouseY - (float)ypos;
    lastMouseX = (float)xpos;
    lastMouseY = (float)ypos;

    camera.processMouse(xoffset, yoffset);
}

// Reads the WASD keys and moves the camera accordingly. Called once per frame.
void processInput(GLFWwindow* window, float deltaTime) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        camera.processKeyboard(FORWARD, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        camera.processKeyboard(BACKWARD, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        camera.processKeyboard(LEFT, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        camera.processKeyboard(RIGHT, deltaTime);
}

int main() {
    // --- 1. initialize GLFW and ask for an OpenGL 4.1 core context ---
    // If the machine does not support 4.1 core, the window creation will fail.
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);   // required on macOS, harmless on Windows

    // --- 2. create the window (width, height, title) ---
    // LV - Modifica per la finestra 
    //GLFWwindow* window = glfwCreateWindow(1280, 720, "Dungeon RTGP", nullptr, nullptr);
    GLFWwindow* window = glfwCreateWindow(windowWidth, windowHeight, "Dungeon RTGP", nullptr, nullptr);
    if (window == nullptr) {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    // make this window's OpenGL context the "current" one for the following GL calls
    glfwMakeContextCurrent(window);

    // --- 3. load all the OpenGL functions using GLAD ---
    // glfwGetProcAddress tells GLAD where to find the driver functions.
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cout << "Failed to initialize GLAD" << std::endl;
        return -1;
    }

    //LV - Modifica 
    // tell OpenGL the size of the area we draw into (the whole window here)
    //glViewport(0, 0, 1280, 720);
    // --- 4. the render loop: runs once per frame until we close the window ---
    // while (!glfwWindowShouldClose(window)) {
    //     glfwPollEvents();   // process input / window events (needed, otherwise the window freezes)

    //     // close the window when ESC is pressed
    //     if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
    //         glfwSetWindowShouldClose(window, true);

    //     // fill the screen with a dark color
    //     glClearColor(0.1f, 0.1f, 0.15f, 1.0f);   // R, G, B, A
    //     glClear(GL_COLOR_BUFFER_BIT);            // actually apply the clear color

    //     glfwSwapBuffers(window);   // show the frame we just drew
    // }

    // // --- 5. cleanup before exiting ---
    // glfwTerminate();
    // return 0;

   // --- 4. set up the renderer, the test scene, and the input callbacks ---
    Renderer renderer("shaders/ggx.vert", "shaders/ggx.frag");
    renderer.setViewport(windowWidth, windowHeight);

    Scene scene;
    TestScene::build(scene);   // TEMPORARY: replace with Andrea's dungeon scene once ready

    FrameMetrics metrics;   // reset and refilled every frame

    glfwSetWindowUserPointer(window, &renderer);
    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
    glfwSetCursorPosCallback(window, mouseCallback);
    // hide and lock the cursor to the window, and let GLFW report unbounded mouse deltas:
    // this is what makes first-person mouse-look feel right (no cursor visible, no edge
    // of the screen stopping you from turning further)
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    float lastFrameTime = (float)glfwGetTime();

    // --- 5. the render loop: runs once per frame until we close the window ---
    while (!glfwWindowShouldClose(window)) {
        // deltaTime = how many seconds passed since the previous frame; used so that
        // movement speed does not depend on how fast the machine can render frames
        float currentFrameTime = (float)glfwGetTime();
        float deltaTime = currentFrameTime - lastFrameTime;
        lastFrameTime = currentFrameTime;

        // fps / frameTimeMs are the two FrameMetrics fields main.cpp itself is
        // responsible for filling (see the comment in core/metrics.h)
        metrics.frameTimeMs = deltaTime * 1000.0f;
        metrics.fps = (deltaTime > 0.0f) ? (1.0f / deltaTime) : 0.0f;

        glfwPollEvents();   // process input / window events (needed, otherwise the window freezes)
        processInput(window, deltaTime);

        renderer.render(scene, camera, metrics);

        glfwSwapBuffers(window);   // show the frame we just drew
    }

    // --- 6. cleanup before exiting ---
    renderer.clean();
    glfwTerminate();
    return 0;
}
