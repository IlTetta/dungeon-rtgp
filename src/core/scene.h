#pragma once

// The data that describes the scene.

#include <vector>
#include <glm/glm.hpp>
#include "engine/mesh.h"
#include "material.h"

// Axis Aligned Bounding Box
struct AABB {
    glm::vec3 min;   // corner with the smallest x, y, z
    glm::vec3 max;   // corner with the largest x, y, z
};

// One thing to draw
struct RenderObject {
    // Index of the mesh in Scene::meshes
    int meshIndex;

    // position, rotation and scale in the world
    glm::mat4 modelMatrix;

    // Bounding box in world coordinates.
    AABB worldBounds;

    // the KIND of surface, for the game logic (see material.h)
    MaterialId material;

    // how it LOOKS: index into Scene::materials (texture, roughness, ...)
    int materialIndex = 0;
};

// A SPOT is a wall torch, a POINT is a brazier.
enum LightType {
    LIGHT_POINT,    // braziers: cubemap shadow, no direction
    LIGHT_SPOT  // wall torches: needs Light::direction
};

// A light of the dungeon. Every light is a fire: a torch or a brazier.
struct Light {
    glm::vec3 position;
    glm::vec3 color;
    float intensity;
    float radius;   // distance where the light contribution goes to about 0

    // Each frame the renderer gives a shadow only to the nearest
    // candidates, up to a budget per type (SPOT and POINT).
    bool castsShadow = false;

    LightType type = LIGHT_POINT;

    // where a SPOT points.
    glm::vec3 direction = glm::vec3(0.0f, 0.0f, 1.0f);

    // a wall or corridor torch: the HUD "Lighting" window changes intensity and radius only on these
    bool isTorch = false;

    // a real flame (torch or brazier): the particles spawn only from these lights
    bool isFire = false;
};

// How a surface looks: albedo texture, tiling, and the GGX parameters.
struct Material {
    GLuint albedo = 0;  // texture id (from loadTexture)
    float uvScale = 1.0f;   // how many times the texture repeats over the UVs
    float roughness = 0.8f; // 0 = mirror, 1 = very rough
    glm::vec3 F0 = glm::vec3(0.04f);    // reflectance at normal incidence (0.04 = dielectric)
};

// Scene contains Mesh objects, which cannot be copied, so a Scene cannot be copied either:
// we always pass it by reference (const Scene&).
struct Scene {
    std::vector<Mesh> meshes;   // geometry on the GPU
    std::vector<Material> materials;
    std::vector<RenderObject> objects;  // what we draw
    std::vector<Light> lights;  // torches and braziers
};
