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

    // Tells the renderer how to shade this object (floor / wall / prop).
    MaterialId material;
};

// A point light. In our project these are the torches on the dungeon walls.
struct Light {
    glm::vec3 position;
    glm::vec3 color;
    float intensity;
    float radius;    // distance after which the light contribution is ~0 (attenuation)

    // M2 (Lorenzo, shadow mapping): does this light cast a shadow? Off by default, so any
    // light nobody explicitly opts in (e.g. Andrea's real dungeon torches, for now) behaves
    // exactly as before - no shadow, always "fully lit" like in M1. The renderer only takes
    // the first Renderer::MAX_SHADOW_LIGHTS lights with this set to true (see renderer.cpp).
    bool castsShadow = false;

    // M2: which way this light is "aimed", only used (and only meaningful) when
    // castsShadow == true - the shadow map is built as a single ~100 degree cone around
    // this direction (see Renderer::computeLightSpaceMatrix), not a full cubemap. Needs to
    // be a normalized-ish, non-vertical direction (glm::lookAt degenerates if it is
    // parallel to world-up); default points forward so an unset light never produces NaNs
    // even if castsShadow ends up true by mistake.
    glm::vec3 direction = glm::vec3(0.0f, 0.0f, 1.0f);
};

// The shared scene description.
// Andrea FILLS this (from the BSP dungeon generation), Lorenzo READS it (to draw).
//
// Note: because Scene contains a vector of Mesh, and Mesh cannot be copied, the whole
// Scene cannot be copied either. So we always pass it around by reference
// (for example "const Scene& scene"), never by value.
struct Scene {
    std::vector<Mesh> meshes;            // the real geometry uploaded to the GPU
    std::vector<RenderObject> objects;   // the list of instances to draw
    std::vector<Light> lights;           // the torches
};
