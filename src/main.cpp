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

int main() {
    // --- 1. initialize GLFW and ask for an OpenGL 4.1 core context ---
    // If the machine does not support 4.1 core, the window creation will fail.
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);   // required on macOS, harmless on Windows

    // --- 2. create the window (width, height, title) ---
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Dungeon RTGP", nullptr, nullptr);
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

    // tell OpenGL the size of the area we draw into (the whole window here)
    glViewport(0, 0, 1280, 720);

    // --- 4. the render loop: runs once per frame until we close the window ---
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();   // process input / window events (needed, otherwise the window freezes)

        // close the window when ESC is pressed
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, true);

        // fill the screen with a dark color
        glClearColor(0.1f, 0.1f, 0.15f, 1.0f);   // R, G, B, A
        glClear(GL_COLOR_BUFFER_BIT);            // actually apply the clear color

        glfwSwapBuffers(window);   // show the frame we just drew
    }

    // --- 5. cleanup before exiting ---
    glfwTerminate();
    return 0;
}
