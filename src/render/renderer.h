#pragma once

// Renderer class.
// A "basic forward renderer" with GGX (Cook-Torrance) shading. Its job, once per frame,
// is to:
//   1. clear the screen (color + depth buffer)
//   2. activate our shader program and tell it about the camera and the lights: these
//      do not change between one object and the next, so we set them only once
//   3. loop over every RenderObject in the Scene and, for each one, tell the shader
//      where it is (the "model" matrix) and what it is made of (base color, roughness,
//      F0 - picked per MaterialId, see material.h), then ask its Mesh to draw itself
//   4. fill in the "Renderer" fields of FrameMetrics (drawCalls, trianglesDrawn), so the
//      HUD (Andrea's side) can show and log them
//
// "Forward" means we compute the contribution of every light against an object directly
// in the fragment shader, in the same pass that draws that object (as opposed to
// "deferred" rendering, which splits this into two passes). It is the simplest approach
// and is enough for the number of point lights (torches) we expect on screen at once.
//
// Shading model: shaders/ggx.frag implements the Cook-Torrance "FDG" BRDF (Fresnel x
// microfacet Distribution x Geometry term) with the GGX/Trowbridge-Reitz distribution,
// Schlick's Fresnel approximation and Smith's (Schlick-GGX) geometry term, plus a Lambert
// diffuse term. shaders/basic.vert/.frag (the flat, unlit pair from the very first
// pipeline test) are kept in the repo as a quick fallback/sanity check, but are not used
// by default anymore.
//
// Not yet included here: shadow mapping and volumetric fog. Both will need rendering
// into an off-screen framebuffer first; see render/framebuffer.h for that scaffolding.

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>   // glm::perspective

#include "engine/shader.h"
#include "engine/camera.h"
#include "core/scene.h"
#include "core/metrics.h"
#include "world/frustum_culling.h"   // Andrea's culling, now used inside the render loop

class Renderer {
public:
    // frustum culling on/off. main.cpp flips this from the C key so we can compare ON vs OFF.
    // When on, render() skips every object whose AABB is outside the camera frustum.
    bool cullingEnabled = true;

    // Must be >= the number of lights we ever pass to the shader in one draw call, and
    // must match "#define MAX_LIGHTS 8" in shaders/ggx.frag. If Scene::lights ever grows
    // past this (more torches than we can shade at once), render() below simply ignores
    // the extra ones for now; proper light culling (picking only the closest lights per
    // object) is a job for a later milestone, not for basic forward rendering.
    static const int MAX_LIGHTS = 32;

    // Loads and compiles the given vertex/fragment shader pair (paths are relative to
    // the working directory the program is run from; see the comment next to the
    // shaders copy step in CMakeLists.txt). main.cpp passes "shaders/ggx.vert" /
    // "shaders/ggx.frag" by default.
    Renderer(const char* vertexPath, const char* fragmentPath);

    // Call this once at startup, and again every time the window is resized: it updates
    // both the OpenGL viewport and the projection matrix (they must always match, or the
    // image comes out stretched).
    void setViewport(int width, int height);

    // Draws one whole frame: every RenderObject in "scene", from the point of view of
    // "camera". Also writes drawCalls / trianglesDrawn into "metrics".
    // "scene" is taken by const reference: the renderer only READS it, it never creates
    // or removes meshes/objects (that is Andrea's side, in src/world/).
    void render(const Scene& scene, Camera& camera, FrameMetrics& metrics);

    // DEBUG / M3 tooling (Andrea): draw the scene from an arbitrary "spectator" viewpoint
    // (explicit view matrix + eye position) while culling against a DIFFERENT, frozen frustum
    // (normally the player's). This is what lets a second, far camera SHOW the frustum culling
    // in action: geometry outside the player frustum disappears even though we look from above.
    //   - view / eye : where we DRAW from (the spectator camera),
    //   - cullFrustum : what we CULL against (the player's frustum, frozen),
    //   - hideCeiling : skip MAT_CEILING objects, so an overhead view can see inside the dungeon.
    // The normal render() above is unchanged (Lorenzo's call site keeps working); both share the
    // same drawing core (renderInternal).
    void renderSpectator(const Scene& scene, const glm::mat4& view, const glm::vec3& eye,
                         const Frustum& cullFrustum, bool hideCeiling, FrameMetrics& metrics);

    // The current projection matrix (built by setViewport). main needs it to build the player's
    // view-projection for the frozen frustum and for the debug frustum wireframe.
    const glm::mat4& getProjection() const { return projection; }

    // Frees the GPU shader program. Call once, when the application closes.
    void clean();

private:
    // The shared drawing core used by both render() and renderSpectator(): clear, set the
    // per-frame uniforms (view/eye/lights), then one draw call per VISIBLE object, culling each
    // object's AABB against `cullFrustum` (when cullingEnabled) and optionally skipping ceilings.
    void renderInternal(const Scene& scene, const glm::mat4& view, const glm::vec3& eye,
                        const Frustum& cullFrustum, bool hideCeiling, FrameMetrics& metrics);

    Shader shader;
    glm::mat4 projection;

    // kept around so setViewport() can rebuild the projection matrix if we ever change
    // the field of view at runtime (we do not yet, but it costs nothing to keep it here
    // instead of hardcoding the same number twice)
    float fovDegrees;

    // NB: the renderer no longer owns any texture. Each object carries a materialIndex into
    // Scene::materials, and we just bind that material's albedo texture. Loading the textures
    // is done on the scene-building side (src/world/), so the renderer only READS the scene.
};
