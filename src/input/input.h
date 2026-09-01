#pragma once

// FPS input handling (keyboard + mouse), pulled out of main.cpp.
//
// GLFW delivers input through C-style callbacks that must be plain functions, so - exactly like
// the lab code - they reach the app state through one file-scope object. We gather that state in
// an InputState struct (one instance, g_input) instead of a handful of loose globals in main, and
// give main a single initInput() front door. main still OWNS the things the callbacks touch (the
// camera, the culling / debug-cam toggles, ...); InputState just keeps pointers to them, wired
// once by initInput(). (Routing the callbacks to an object with NO globals - via the window user
// pointer - would be a further step; we deliberately keep the simple globals style here.)

#include <glad/glad.h>
#include <glfw/glfw3.h>

#include "engine/camera.h"

struct InputState {
    // things main owns; the callbacks reach them through these pointers (set by initInput)
    Camera* camera            = nullptr;
    bool*   cullingEnabled    = nullptr;   // C: frustum culling on/off
    bool*   debugCamEnabled   = nullptr;   // V: top-down spectator view of the culling
    bool*   firstMouse        = nullptr;   // reset after a camera teleport, to avoid a look jump
    bool*   requestClearFocus = nullptr;   // set on return to first-person; the loop clears HUD focus

    // input-internal transient state (main never touches these)
    bool  keys[1024] = { false };
    float lastX = 0.0f, lastY = 0.0f;
    bool  uiMode = false;                  // F1: cursor free for the HUD, camera look paused
};

// the single input state the GLFW callbacks talk to
inline InputState g_input;

// GLFW key callback: ESC quit, C culling, V debug cam, F1 cursor/HUD toggle, plus the held-keys map.
inline void key_callback(GLFWwindow* window, int key, int scancode, int action, int mode) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);
    // toggle frustum culling on/off
    if (key == GLFW_KEY_C && action == GLFW_PRESS)
        *g_input.cullingEnabled = !*g_input.cullingEnabled;
    // toggle the debug spectator camera (far, top-down view of the culling)
    if (key == GLFW_KEY_V && action == GLFW_PRESS)
        *g_input.debugCamEnabled = !*g_input.debugCamEnabled;
    // F1: toggle the mouse cursor free (to use the HUD) / captured (first-person look).
    // We use F1 rather than TAB on purpose: TAB is ImGui's own "focus next field" key, so
    // toggling with TAB would also move the keyboard focus into a HUD text box (and then WASD
    // would be typed into it instead of moving the camera). F1 never touches the HUD widgets.
    if (key == GLFW_KEY_F1 && action == GLFW_PRESS) {
        g_input.uiMode = !g_input.uiMode;
        glfwSetInputMode(window, GLFW_CURSOR, g_input.uiMode ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
        *g_input.firstMouse = true;   // avoid a camera jump when going back to first-person
        // going back to first-person: ask the loop to drop the focus from any HUD widget that
        // still holds it, so movement keys reach the game and not a text field.
        if (!g_input.uiMode) *g_input.requestClearFocus = true;
    }
    if (key >= 0 && key < 1024) {
        if (action == GLFW_PRESS)   g_input.keys[key] = true;
        if (action == GLFW_RELEASE) g_input.keys[key] = false;
    }
}

// GLFW mouse-move callback: turn the camera, unless the HUD currently has the cursor (uiMode).
inline void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    if (g_input.uiMode) return;          // in UI mode the mouse controls the HUD, not the camera
    if (*g_input.firstMouse) {           // avoid a big jump on the very first mouse event
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

// Move the camera from the currently-held WASD keys (Shift = sprint). deltaTime keeps the movement
// speed frame-rate independent. Called every non-replay frame from the render loop.
inline void applyMovements(float deltaTime) {
    Camera& camera = *g_input.camera;
    // hold SHIFT to sprint: normal speed feels right in the rooms, sprint helps in the long corridors
    camera.MovementSpeed = (g_input.keys[GLFW_KEY_LEFT_SHIFT] || g_input.keys[GLFW_KEY_RIGHT_SHIFT]) ? 15.0f : 6.0f;

    if (g_input.keys[GLFW_KEY_W]) camera.processKeyboard(FORWARD, deltaTime);
    if (g_input.keys[GLFW_KEY_S]) camera.processKeyboard(BACKWARD, deltaTime);
    if (g_input.keys[GLFW_KEY_A]) camera.processKeyboard(LEFT, deltaTime);
    if (g_input.keys[GLFW_KEY_D]) camera.processKeyboard(RIGHT, deltaTime);
}

// Wire the GLFW callbacks + capture the mouse, and tell InputState which app-owned state to drive.
// Call once, after the window/context exist and BEFORE ImGui is initialised (ImGui chains onto
// the callbacks we install here), passing the objects main owns.
inline void initInput(GLFWwindow* window, Camera& camera,
                      bool& cullingEnabled, bool& debugCamEnabled,
                      bool& firstMouse, bool& requestClearFocus) {
    g_input.camera            = &camera;
    g_input.cullingEnabled    = &cullingEnabled;
    g_input.debugCamEnabled   = &debugCamEnabled;
    g_input.firstMouse        = &firstMouse;
    g_input.requestClearFocus = &requestClearFocus;

    glfwSetKeyCallback(window, key_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);   // capture and hide the mouse
}
