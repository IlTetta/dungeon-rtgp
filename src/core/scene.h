#pragma once

// SHARED FILE: da cambiare insieme all'inizio di ogni milestone

#include <vector>
#include <glm/glm.hpp>      // gives us glm::vec3 and glm::mat4
#include "engine/mesh.h"    // our Mesh class (owns VAO/VBO/EBO); it includes glad by itself
#include "material.h"

// Axis Aligned Bounding Box.
// "Axis aligned" means the box edges are parallel to the world X/Y/Z axes, so we do NOT
// need to store a rotation: two opposite corners (the smallest and the largest point)
// are enough to describe it completely.
struct AABB {
    glm::vec3 min;   // corner with the smallest x, y, z
    glm::vec3 max;   // corner with the largest  x, y, z
};

// One thing we want to draw in the scene (one wall, one floor tile, one prop, ...).
struct RenderObject {
    // We do NOT store a pointer to the Mesh, we store its position (index) inside the
    // Scene::meshes vector. A vector can move its elements in memory when it grows, so a
    // pointer could become invalid; an index instead stays valid. Also Mesh is a
    // "move only" class (it cannot be copied), and an index is a plain number that we
    // can copy around without problems.
    int meshIndex;

    // Where the object is placed in the world (position, rotation, scale all together).
    glm::mat4 modelMatrix;

    // The bounding box of this object, already expressed in world coordinates.
    // We keep it here so that in M2 the frustum culling can test it very quickly,
    // and so collisions can use it too, without recomputing it every frame.
    AABB worldBounds;

    // The "kind" of surface (floor / wall / ceiling / prop). Used by gameplay logic, e.g.
    // collisions (walls and props block the player, floors and ceilings do not).
    MaterialId material;

    // Which visual material to draw this object with: an index into Scene::materials.
    // This is separate from "material" above: many different objects (a floor, a barrel, a
    // statue...) can each have their own texture, while the collision "kind" stays coarse.
    int materialIndex = 0;
};

// A light is either a SPOT (aimed into a cone - a wall torch) or a POINT (omnidirectional -
// a brazier); each needs a different shadow technique (2D map vs. cubemap). Every light
// placed today is a floor brazier, so LIGHT_POINT is the default.
enum LightType {
    LIGHT_POINT,   // omnidirectional (braziers) - cubemap shadow, no "direction"
    LIGHT_SPOT     // aimed cone (wall torches) - needs Light::direction
};

// A point light. In our project these are the torches / braziers in the dungeon.
struct Light {
    glm::vec3 position;
    glm::vec3 color;
    float intensity;
    float radius;    // distance after which the light contribution is ~0 (attenuation)

    // does this light cast a shadow? Off by default. The renderer picks the
    // MAX_SHADOW_LIGHTS lights with this set to true that are closest to the camera each
    // frame, not just the first ones in the list.
    bool castsShadow = false;

    LightType type = LIGHT_POINT;

    // which way this light is "aimed" - only meaningful for type == LIGHT_SPOT. Must not be
    // vertical (glm::lookAt degenerates if parallel to world-up); default points forward.
    glm::vec3 direction = glm::vec3(0.0f, 0.0f, 1.0f);

    // Scene-side tag (Andrea): true for wall / corridor torches, so the ImGui "Lighting" window can
    // tune only the torch lights at runtime (applyTorchLightTuning). Room lights leave it false.
    bool isTorch = false;
};

// A visual material: HOW a surface looks. For now: an albedo (base color) texture, a texture
// tiling factor, and the GGX roughness / F0 parameters. Normal and roughness MAPS will be
// added here later (Step 2), and every object that uses this material gets them for free.
struct Material {
    GLuint albedo = 0;                 // base color texture id (from loadTexture)
    float uvScale = 1.0f;              // how many times the texture repeats across the UVs
    float roughness = 0.8f;            // GGX roughness (0 = mirror-smooth, 1 = very rough)
    glm::vec3 F0 = glm::vec3(0.04f);   // Fresnel reflectance at 0 degrees (0.04 = dielectric)
};

// The shared scene description.
// Andrea FILLS this (from the BSP dungeon generation), Lorenzo READS it (to draw).
//
// Note: because Scene contains a vector of Mesh, and Mesh cannot be copied, the whole
// Scene cannot be copied either. So we always pass it around by reference
// (for example "const Scene& scene"), never by value.
struct Scene {
    std::vector<Mesh> meshes;            // the real geometry uploaded to the GPU
    std::vector<Material> materials;     // the materials (textures) objects can be drawn with
    std::vector<RenderObject> objects;   // the list of instances to draw
    std::vector<Light> lights;           // the torches / braziers
};
