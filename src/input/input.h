#pragma once

// FPS input: keyboard and mouse.
//
// GLFW calls our code through callbacks that must be plain functions, so
// they reach the app state through a global object. We keep all of it in one struct, g_input.
// main owns the camera and the toggles; g_input only keeps pointers to them, set once by
// initInput().

#include <glad/glad.h>
#include <glfw/glfw3.h>

#include "engine/camera.h"

struct InputState {
    // things owned by main (set by initInput)
    Camera* camera = nullptr;
    bool* cullingEnabled = nullptr;      // C: frustum culling on/off
    bool* debugCamEnabled = nullptr;     // V: top-down spectator view of the culling
    bool* firstMouse = nullptr;          // true after a teleport, to avoid a jump of the view
    bool* requestClearFocus = nullptr;   // asks the loop to take the focus away from the HUD

    // state used only by the input code
    bool keys[1024] = { false };
    float lastX = 0.0f, lastY = 0.0f;
    bool uiMode = false;   // F1: cursor free to use the HUD, camera look paused
};

inline InputState g_input;

// Key callback: ESC quits, C / V / F1 toggle things, and we remember which keys are held.
inline void key_callback(GLFWwindow* window, int key, int scancode, int action, int mode) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);
    if (key == GLFW_KEY_C && action == GLFW_PRESS)
        *g_input.cullingEnabled = !*g_input.cullingEnabled;
    if (key == GLFW_KEY_V && action == GLFW_PRESS)
        *g_input.debugCamEnabled = !*g_input.debugCamEnabled;

    // F1 frees the cursor to use the HUD, or captures it again for the first-person look.
    // We use F1 and not TAB because TAB is the ImGui key to move to the next field: it would
    // put the focus in a HUD text box and then WASD would be typed there.
    if (key == GLFW_KEY_F1 && action == GLFW_PRESS) {
        g_input.uiMode = !g_input.uiMode;
        glfwSetInputMode(window, GLFW_CURSOR, g_input.uiMode ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
        *g_input.firstMouse = true;
        // back to first person: the loop removes the focus from any HUD widget
        if (!g_input.uiMode)
            *g_input.requestClearFocus = true;
    }

    // held keys, read every frame by applyMovements()
    if (key >= 0 && key < 1024) {
        if (action == GLFW_PRESS)
            g_input.keys[key] = true;
        if (action == GLFW_RELEASE)
            g_input.keys[key] = false;
    }
}

// Mouse callback: turns the camera, unless the cursor is being used for the HUD.
inline void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    if (g_input.uiMode)
        return;

    // first event (or after a teleport): there is no previous position yet, so we just store
    // this one, otherwise the offset would be huge and the view would jump
    if (*g_input.firstMouse) {
        g_input.lastX = (float)xpos;
        g_input.lastY = (float)ypos;
        *g_input.firstMouse = false;
    }
    float xoffset = (float)xpos - g_input.lastX;
    float yoffset = g_input.lastY - (float)ypos;   // reversed: screen y grows downward
    g_input.lastX = (float)xpos;
    g_input.lastY = (float)ypos;
    g_input.camera->processMouse(xoffset, yoffset);
}

// Move the camera with the held WASD keys. Called every frame, except during a benchmark replay.
inline void applyMovements(float deltaTime) {
    Camera& camera = *g_input.camera;
    // Shift = sprint: 6 feels right inside the rooms, 15 helps in the long corridors
    camera.MovementSpeed = (g_input.keys[GLFW_KEY_LEFT_SHIFT] || g_input.keys[GLFW_KEY_RIGHT_SHIFT]) ? 15.0f : 6.0f;

    if (g_input.keys[GLFW_KEY_W])
        camera.processKeyboard(FORWARD, deltaTime);
    if (g_input.keys[GLFW_KEY_S])
        camera.processKeyboard(BACKWARD, deltaTime);
    if (g_input.keys[GLFW_KEY_A])
        camera.processKeyboard(LEFT, deltaTime);
    if (g_input.keys[GLFW_KEY_D])
        camera.processKeyboard(RIGHT, deltaTime);
}

// Install the callbacks, capture the mouse and store the pointers to the state main owns.
// Must be called BEFORE ImGui is initialized: ImGui then chains its own callbacks onto ours.
inline void initInput(GLFWwindow* window, Camera& camera,
                      bool& cullingEnabled, bool& debugCamEnabled,
                      bool& firstMouse, bool& requestClearFocus) {
    g_input.camera = &camera;
    g_input.cullingEnabled = &cullingEnabled;
    g_input.debugCamEnabled = &debugCamEnabled;
    g_input.firstMouse = &firstMouse;
    g_input.requestClearFocus = &requestClearFocus;

    glfwSetKeyCallback(window, key_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);   // capture and hide the mouse
}
