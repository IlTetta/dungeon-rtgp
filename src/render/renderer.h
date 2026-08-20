#pragma once

// Renderer class.
// A "basic forward renderer" with GGX (Cook-Torrance) shading. Its job, once per frame,
// is to:
//   1. (M2) render the depth maps of the shadow-casting torches into off-screen FBOs
//   2. clear the screen (color + depth buffer)
//   3. activate our shader program and tell it about the camera, the lights and the
//      shadow maps: these do not change between one object and the next, so we set them
//      only once
//   4. loop over every RenderObject in the Scene and, for each one, tell the shader where
//      it is (the "model" matrix) and which material to draw it with (its albedo texture +
//      uvScale / roughness / F0, see core/scene.h), then ask its Mesh to draw itself
//   5. fill in the "Renderer" fields of FrameMetrics (drawCalls, trianglesDrawn), so the
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
// diffuse term, with a textured albedo per material.
//
// M2: shadow mapping (PCF) for up to MAX_SHADOW_LIGHTS point lights (torches), following
// the depth-pass + PCF-with-adaptive-bias approach from lecture07a (Davide Gadia), with a
// perspective (instead of orthographic) light projection, since our lights are point lights
// and not a directional one - see the long comment at the top of shaders/ggx.frag for what
// is/isn't taken verbatim from the professor's code. Volumetric fog is still not included
// here; see render/framebuffer.h for that scaffolding.

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>   // glm::perspective, glm::lookAt

#include "engine/shader.h"
#include "engine/camera.h"
#include "core/scene.h"
#include "core/metrics.h"
#include "world/frustum_culling.h"   // Andrea's culling, used inside the render loop

class Renderer {
public:
    // frustum culling on/off. main.cpp flips this from the C key so we can compare ON vs OFF.
    // When on, render() skips every object whose AABB is outside the camera frustum.
    bool cullingEnabled = true;

    // Must be >= the number of lights we ever pass to the shader in one draw call, and must
    // match "#define MAX_LIGHTS 32" in shaders/ggx.frag. If Scene::lights ever grows past
    // this (more torches than we can shade at once), render() below simply ignores the
    // extra ones; proper light culling (picking only the closest lights per object) is a
    // job for a later milestone, not for basic forward rendering.
    static const int MAX_LIGHTS = 32;

    // M2: how many torches can cast a shadow AT THE SAME TIME, and must match
    // "#define MAX_SHADOW_LIGHTS 3" in shaders/ggx.vert and shaders/ggx.frag. This is a
    // fixed budget across the WHOLE game, not per room: Andrea's torch/brazier count is a
    // tunable variable, so render() does not just take "the first MAX_SHADOW_LIGHTS in
    // Scene::lights" - every frame it picks the MAX_SHADOW_LIGHTS castsShadow lights closest
    // to the camera (see renderInternal()), so real shadows follow the player from room to
    // room. Any light that doesn't make the cut this frame falls back to unshadowed (same
    // as a light with castsShadow == false) - never a crash.
    static const int MAX_SHADOW_LIGHTS = 3;

    // Resolution of each shadow map (square). 1024 is the usual starting point for an indoor
    // scene at this scale; if PCF edges look too blocky up close, or perf needs it to go
    // down, this is the one number to tune.
    static const int SHADOW_MAP_SIZE = 1024;

    // Loads and compiles the given vertex/fragment shader pair (paths relative to the
    // working directory; see the shaders copy step in CMakeLists.txt). main.cpp passes
    // "shaders/ggx.vert" / "shaders/ggx.frag" by default.
    Renderer(const char* vertexPath, const char* fragmentPath);

    // Call this once at startup, and again every time the window is resized: it updates the
    // OpenGL viewport and the projection matrix (they must always match, or the image comes
    // out stretched).
    void setViewport(int width, int height);

    // Draws one whole frame from "camera" (the first-person player camera). Culls against
    // that same camera's frustum. Also writes drawCalls / trianglesDrawn into "metrics".
    void render(const Scene& scene, Camera& camera, FrameMetrics& metrics);

    // DEBUG / M3 tooling (Andrea): draw the scene from an arbitrary "spectator" viewpoint
    // (explicit view matrix + eye position) while culling against a DIFFERENT, frozen frustum
    // (normally the player's). This is what lets a second, far camera SHOW the frustum culling
    // in action: geometry outside the player frustum disappears even though we look from above.
    //   - view / eye : where we DRAW from (the spectator camera),
    //   - cullFrustum : what we CULL against (the player's frustum, frozen),
    //   - hideCeiling : skip MAT_CEILING objects, so an overhead view can see inside the dungeon.
    // Both render() and this share the same drawing core (renderInternal), so both get shadows.
    void renderSpectator(const Scene& scene, const glm::mat4& view, const glm::vec3& eye,
                         const Frustum& cullFrustum, bool hideCeiling, FrameMetrics& metrics);

    // The current projection matrix (built by setViewport). main needs it to build the player's
    // view-projection for the frozen frustum and for the debug frustum wireframe.
    const glm::mat4& getProjection() const { return projection; }

    // Frees the GPU shader programs and the shadow-map FBOs/textures. Call once, at shutdown.
    void clean();

private:
    // The shared drawing core used by both render() and renderSpectator(): shadow passes,
    // clear, per-frame uniforms (view/eye/lights/shadows), then one draw call per VISIBLE
    // object, culling its AABB against `cullFrustum` (when cullingEnabled) and optionally
    // skipping ceilings. `eye` is both the camera position sent to the shader and the point
    // we pick the nearest shadow-casting lights around.
    void renderInternal(const Scene& scene, const glm::mat4& view, const glm::vec3& eye,
                        const Frustum& cullFrustum, bool hideCeiling, FrameMetrics& metrics);

    Shader shader;
    glm::mat4 projection;

    // kept so setViewport() can rebuild the projection if we ever change the field of view
    float fovDegrees;

    // current viewport size, so renderInternal() can restore it after the shadow passes
    // (which switch the viewport to SHADOW_MAP_SIZE x SHADOW_MAP_SIZE).
    int viewportWidth;
    int viewportHeight;

    // --- M2: shadow mapping state ---
    // depth-only shader used for the shadow pass (shaders/shadowmap.vert/.frag).
    Shader shadowShader;
    // one FBO + one depth texture per potential shadow-casting torch.
    GLuint shadowFBO[MAX_SHADOW_LIGHTS];
    GLuint shadowMapTex[MAX_SHADOW_LIGHTS];

    // Create the shadow FBOs + depth textures once, at startup.
    void initShadowMaps();

    // Build the light-space matrix (projection * view, from the light's point of view) for
    // one shadow-casting torch. Perspective, not orthographic (see shaders/shadowmap.vert):
    // a torch is a point light, its shadow map only needs to cover the cone it is aimed into
    // (Light::direction), not the whole room (that would need a full cubemap, out of scope).
    glm::mat4 computeLightSpaceMatrix(const Light& light) const;

    // Render the whole scene, depth-only, into shadowFBO[slot] using shadowShader and the
    // given light-space matrix. Called once per shadow-casting torch, before the color pass.
    void renderShadowPass(const Scene& scene, int slot, const glm::mat4& lightSpaceMatrix);

    // NB: the renderer no longer owns any texture. Each object carries a materialIndex into
    // Scene::materials, and we just bind that material's albedo texture. Loading the textures
    // is done on the scene-building side (src/world/), so the renderer only READS the scene.
};
