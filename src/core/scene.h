#pragma once

// SHARED FILE: da cambiare insieme all'inizio di ogni milestone
//
// IMPORTANT: in the .cpp that includes this file, you must include <glad/glad.h>
// BEFORE this header. The reason is that mesh.h (below) uses OpenGL types like GLuint
// and OpenGL functions, but it does not include glad by itself: it expects glad to be
// already included. If you include scene.h before glad you will get a lot of
// "GLuint was not declared" errors.

#include <vector>
#include <glm/glm.hpp>      // gives us glm::vec3 and glm::mat4
#include <utils/mesh.h>     // the Mesh class from the lab framework (owns VAO/VBO/EBO)
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
