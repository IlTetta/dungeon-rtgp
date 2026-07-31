// main.cpp
//
// TEMPORARY integration / smoke test: it generates the dungeon, turns it into 3D geometry, and
// lets you walk through it in first person with a very simple diffuse lighting from the torches.
// This is only to SEE that the geometry, the scale and the lights are correct. Later, Lorenzo's
// Renderer (in src/render/) will replace all the drawing code here with the real GGX + shadows.
//
// Controls: WASD to move, mouse to look, ESC to quit.

#ifdef _WIN32
    #define APIENTRY __stdcall
#endif

#include <glad/glad.h>
#include <glfw/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>   // glm::inverseTranspose

#include <string>
#include <iostream>

#include "engine/shader.h"
#include "engine/camera.h"
#include "core/scene.h"
#include "dungeon/dungeon_generator.h"
#include "world/dungeon_geometry.h"

// window size
const unsigned int WIDTH = 1280;
const unsigned int HEIGHT = 720;

// --- globals used by the input callbacks (same simple approach as the lab code) ---
Camera camera(glm::vec3(0.0f, 1.6f, 0.0f), true);   // start position is fixed later, after we build the level
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

    glViewport(0, 0, WIDTH, HEIGHT);
    glEnable(GL_DEPTH_TEST);   // so nearer surfaces correctly hide farther ones

    // --- build the dungeon and its 3D geometry (needs the OpenGL context, so we do it now) ---
    DungeonParams params;   // default tile size / wall height
    DungeonGenerator generator(60, 30, 12345);
    Dungeon dungeon = generator.generate();
    DungeonLayout layout = buildDungeonLayout(dungeon, params);
    Scene scene = buildScene(layout);

    // place the camera at eye height in the center of the first room
    if (!dungeon.rooms.empty()) {
        Rect r = dungeon.rooms[0];
        float cx = (r.x + r.w * 0.5f) * params.tileSize;
        float cz = (r.y + r.h * 0.5f) * params.tileSize;
        camera.Position = glm::vec3(cx, 1.6f, cz);
    }

    // --- shader and constant matrices ---
    Shader shader("shaders/basic.vert", "shaders/basic.frag");
    glm::mat4 projection = glm::perspective(glm::radians(60.0f), (float)WIDTH / (float)HEIGHT, 0.1f, 500.0f);

    // base colors for the two materials
    glm::vec3 floorColor(0.35f, 0.35f, 0.40f);
    glm::vec3 wallColor(0.55f, 0.48f, 0.40f);

    // how many lights we actually send (the shader array has a fixed maximum)
    int numLights = (int)scene.lights.size();
    if (numLights > 32) numLights = 32;

    // --- render loop ---
    while (!glfwWindowShouldClose(window)) {
        // time management
        float currentFrame = (float)glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        glfwPollEvents();
        applyMovements();
        glm::mat4 view = camera.getViewMatrix();

        glClearColor(0.02f, 0.02f, 0.03f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        shader.use();
        // uniforms that are the same for every object this frame
        shader.setMat4("projectionMatrix", projection);
        shader.setMat4("viewMatrix", view);
        shader.setInt("numLights", numLights);
        for (int i = 0; i < numLights; i++) {
            std::string idx = std::to_string(i);
            shader.setVec3("lightPos[" + idx + "]", scene.lights[i].position);
            shader.setVec3("lightColor[" + idx + "]", scene.lights[i].color);
            shader.setFloat("lightRadius[" + idx + "]", scene.lights[i].radius);
            shader.setFloat("lightIntensity[" + idx + "]", scene.lights[i].intensity);
        }

        // draw every object in the scene
        for (const RenderObject& obj : scene.objects) {
            shader.setMat4("modelMatrix", obj.modelMatrix);
            // normal matrix = transpose(inverse(mat3(model))), so normals stay correct under scale
            glm::mat3 normalMatrix = glm::inverseTranspose(glm::mat3(obj.modelMatrix));
            shader.setMat3("normalMatrix", normalMatrix);
            shader.setVec3("objectColor", obj.material == MAT_FLOOR ? floorColor : wallColor);

            scene.meshes[obj.meshIndex].draw();
        }

        glfwSwapBuffers(window);
    }

    // --- cleanup ---
    shader.clean();
    glfwTerminate();
    return 0;
}
