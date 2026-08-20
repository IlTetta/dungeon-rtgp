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
// M2: shadow mapping (PCF) for up to MAX_SHADOW_LIGHTS point lights (torches), following
// the depth-pass + PCF-with-adaptive-bias approach from lecture07a (Davide Gadia), with
// a perspective (instead of orthographic) light projection, since our lights are point
// lights and not a directional one - see the long comment at the top of shaders/ggx.frag
// for exactly what is/isn't taken verbatim from the professor's code. Volumetric fog is
// still not included here; see render/framebuffer.h for that scaffolding.

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

    // Must be >= the number of lights we ever pass to the shader in one draw call, and
    // must match "#define MAX_LIGHTS 8" in shaders/ggx.frag.
    static const int MAX_LIGHTS = 8;

    // M2: how many torches can cast a shadow AT THE SAME TIME, and must match
    // "#define MAX_SHADOW_LIGHTS 3" in shaders/ggx.vert and shaders/ggx.frag. This is a
    // budget across the WHOLE game, not per room: Andrea's wall-torch/brazier count is a
    // tunable variable and can be small or large, so render() does not just take "the
    // first MAX_SHADOW_LIGHTS in Scene::lights" - every frame it picks the
    // MAX_SHADOW_LIGHTS castsShadow lights closest to the camera (see the comment in
    // render()), so real shadows follow the player from room to room. Any light that
    // doesn't make the cut this frame falls back to unshadowed, same as any light with
    // castsShadow == false - never a crash.
    static const int MAX_SHADOW_LIGHTS = 3;

    // Resolution of each shadow map (square). 1024 is the usual starting point for an
    // indoor scene at this scale; if PCF edges look too blocky up close, or perf needs
    // it to go down, this is the one number to tune.
    static const int SHADOW_MAP_SIZE = 1024;

    Renderer(const char* vertexPath, const char* fragmentPath);

    void setViewport(int width, int height);

    void render(const Scene& scene, Camera& camera, FrameMetrics& metrics);

    void clean();

private:
    Shader shader;
    glm::mat4 projection;
    float fovDegrees;

    int viewportWidth;
    int viewportHeight;

    // --- M2: shadow mapping state ---
    // depth-only shader used for the shadow pass (shaders/shadowmap.vert/.frag).
    Shader shadowShader;
    // one FBO + one depth texture per potential shadow-casting torch.
    GLuint shadowFBO[MAX_SHADOW_LIGHTS];
    GLuint shadowMapTex[MAX_SHADOW_LIGHTS];

    void initShadowMaps();

    // Builds the light-space matrix (projection * view, from the light's point of view)
    // for one shadow-casting torch. Perspective, not orthographic (see the comment in
    // renderer.h above and in shaders/shadowmap.vert): a torch is a point light, its
    // shadow map only needs to cover the cone it is aimed into (Light::direction), not
    // the whole room in every direction (that would need a full cubemap, out of scope
    // for M2 - see the discussion that led to this design).
    glm::mat4 computeLightSpaceMatrix(const Light& light) const;

    // Renders the whole scene, depth-only, into shadowFBO[slot] using shadowShader and
    // the given light-space matrix. Called once per shadow-casting torch, before the
    // main color pass.
    void renderShadowPass(const Scene& scene, int slot, const glm::mat4& lightSpaceMatrix);

    // --- per-material look, until we have real textures ---
    glm::vec3 albedoForMaterial(MaterialId material) const;
    float roughnessForMaterial(MaterialId material) const;
    glm::vec3 f0ForMaterial(MaterialId material) const;
};
