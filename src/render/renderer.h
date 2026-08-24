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

    // M2 (Lorenzo, M2_shadows_plan.md): live-tunable shading/shadow constants, sent to
    // ggx.frag as uniforms every frame (see renderInternal()). main.cpp exposes them in an
    // ImGui "Shadow tuning" panel so we can find good-looking values interactively; once we
    // like a set, we write it down and bake it in here as the new defaults (and can delete
    // the panel). Defaults below are exactly what used to be hardcoded `const float` in the
    // shader, so nothing changes until someone moves a slider.
    struct ShadingTuning {
        float ambient           = 0.12f;   // base fill light so unlit areas are not pitch black
        float spotBiasMax       = 0.05f;   // SPOT shadow depth bias at grazing angles
        float spotBiasMin       = 0.005f;  // SPOT shadow depth bias at normal incidence
        float spotNormalOffset  = 0.03f;   // SPOT: push along N before the light-space projection (S2)
        float pointBiasScale    = 0.05f;   // POINT shadow distance bias at grazing angles (x lightRadius)
        float pointBiasMinScale = 0.02f;   // POINT shadow distance bias at normal incidence (x lightRadius)
        float pointNormalOffset = 0.06f;   // POINT: push along N before the distance test (S2)
        float pointPCFRadius    = 0.04f;   // S4: POINT shadow softness (0 = hard single-tap)

        // S3 (M2_shadows_plan.md, problem #3): how forgiving the shadow-caster selection is
        // about swapping lights when the player moves. A light that already has a slot keeps
        // it as long as it stays within shadowHysteresisMargin of the cutoff distance (>1.0 -
        // stays in the running a bit past the "strict nearest-K" point, instead of popping out
        // the instant something else edges closer); shadowFadeSeconds is how long a shadow
        // takes to ramp fully in/out when a light does enter/leave its slot, instead of
        // snapping to "shadowed"/"unshadowed" in one frame.
        float shadowHysteresisMargin = 1.25f;
        float shadowFadeSeconds      = 0.35f;

        // E1 (M2_shadows_plan.md, problem #5): SSAO ("contact shadow" near where props/walls
        // meet the floor, independent of any light) - see shaders/ssao.frag for what each one
        // actually does. ssaoEnabled exists mainly so the tuning panel can A/B it instantly.
        bool  ssaoEnabled = true;
        float ssaoRadius   = 0.5f;    // view-space units: how far the sample hemisphere reaches
        float ssaoBias     = 0.025f;  // fights the SSAO equivalent of shadow acne
        float ssaoStrength = 1.5f;    // contrast of the final AO term
    } tuning;

    // Must be >= the number of lights we ever pass to the shader in one draw call, and must
    // match "#define MAX_LIGHTS 32" in shaders/ggx.frag. If Scene::lights ever grows past
    // this (more torches than we can shade at once), render() below simply ignores the
    // extra ones; proper light culling (picking only the closest lights per object) is a
    // job for a later milestone, not for basic forward rendering.
    static const int MAX_LIGHTS = 32;

    // M2 (locked decision, M2_shadows_plan.md §2: "K_SHADOW = 4"): how many lights can cast a
    // REAL shadow AT THE SAME TIME, and must match "#define MAX_SHADOW_LIGHTS 4" in
    // shaders/ggx.frag. Fixed budget across the WHOLE game, not per room: the torch/brazier
    // count is meant to be a tunable variable, so renderInternal() does not just take "the
    // first MAX_SHADOW_LIGHTS in Scene::lights" - every frame it picks the MAX_SHADOW_LIGHTS
    // castsShadow lights closest to the camera, so real shadows follow the player from room
    // to room. A light that doesn't make the cut this frame falls back to unshadowed (same
    // as castsShadow == false) - never a crash. A slot can hold either a SPOT (2D map) or a
    // POINT (cubemap) shadow, decided per-frame by that slot's light's Light::type.
    static const int MAX_SHADOW_LIGHTS = 4;

    // Resolution of a SPOT light's 2D shadow map (square). 1024 is the usual starting point
    // for an indoor scene at this scale; if PCF edges look too blocky up close, or perf needs
    // it to go down, this is the one number to tune.
    static const int SHADOW_MAP_SIZE = 1024;

    // Resolution of ONE FACE of a POINT light's cubemap shadow. Smaller than SHADOW_MAP_SIZE
    // on purpose: a cubemap is 6 of these per light (vs. 1 map for a SPOT), so the total texel
    // budget per point-shadow-caster is already 6x - keeping each face at 512 instead of 1024
    // keeps that budget roughly comparable to a SPOT light's.
    static const int POINT_SHADOW_SIZE = 512;

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

    // S3 (M2_shadows_plan.md, problem #3 "pop-in"): per-scene-light fade weight (0 = no
    // shadow effect, 1 = full), indexed the same way as Scene::lights - persists ACROSS
    // frames (unlike everything else in renderInternal, which is recomputed from scratch each
    // frame) so a light's shadow can ramp in/out instead of popping. Resized/reset to all-0
    // whenever its size no longer matches scene.lights.size() (the dungeon was regenerated -
    // there is no "old" light to continue a fade from, starting fresh is correct, not a bug).
    std::vector<float> shadowWeight;

    // Wall-clock time (glfwGetTime()) at the end of the previous renderInternal() call, so we
    // can compute a per-frame delta for the fade above without needing main.cpp to pass one
    // in. -1 marks "no previous frame yet" (first call: dt = 0, nothing to fade yet).
    float lastFrameTime = -1.0f;

    // --- M2: shadow mapping state ---
    // SPOT (cone) shadows: depth-only shader (shaders/shadowmap.vert/.frag), one FBO + one
    // 2D depth texture per potential shadow-casting slot.
    Shader shadowShader;
    GLuint shadowFBO[MAX_SHADOW_LIGHTS];
    GLuint shadowMapTex[MAX_SHADOW_LIGHTS];

    // POINT (cubemap) shadows (M2_shadows_plan.md, S1, single-pass "F2" version): a
    // vertex+geometry+fragment shader (shaders/pointshadow.vert/.geom/.frag) that renders ALL
    // 6 faces of a light's cubemap in ONE draw call per object (the geometry shader re-emits
    // each triangle 6 times, once per face, via gl_Layer - see pointshadow.geom), instead of
    // the earlier version's 6 separate draw-call passes. One FBO + one cubemap (GL_R32F: world
    // distance from the light, see pointshadow.frag) per potential shadow-casting slot, with
    // the WHOLE cubemap attached as a single LAYERED color target (glFramebufferTexture, not
    // glFramebufferTexture2D - the latter only attaches one face). Depth needs to be layered
    // too now (all 6 faces are rasterized in the same draw call, each must depth-test only
    // against its own face) - one shared depth CUBEMAP (not a renderbuffer, layered
    // attachment needs an actual texture), reused across slots since they still render one
    // at a time, sequentially.
    Shader pointShadowShader;
    GLuint shadowCubeFBO[MAX_SHADOW_LIGHTS];
    GLuint shadowCubeTex[MAX_SHADOW_LIGHTS];
    GLuint pointShadowDepthCubeTex;

    // Create the shadow FBOs + textures (both kinds) once, at startup.
    void initShadowMaps();

    // Build the light-space matrix (projection * view, from the light's point of view) for
    // one SPOT shadow-casting torch. Perspective, not orthographic (see
    // shaders/shadowmap.vert): its shadow map only needs to cover the ~100 degree cone it is
    // aimed into (Light::direction).
    glm::mat4 computeLightSpaceMatrix(const Light& light) const;

    // Render the whole scene, depth-only, into shadowFBO[slot] using shadowShader and the
    // given light-space matrix. Called once per SPOT shadow-casting torch, before the color
    // pass.
    void renderShadowPass(const Scene& scene, int slot, const glm::mat4& lightSpaceMatrix);

    // Build the 6 face matrices (90-degree-FOV perspective, aimed down +-X/+-Y/+-Z from the
    // light's position) for one POINT shadow-casting brazier's cubemap.
    void computePointShadowMatrices(const Light& light, glm::mat4 outFaces[6]) const;

    // Render the whole scene, depth-only, into shadowCubeTex[slot]'s 6 faces in ONE pass
    // (pointShadowShader's geometry shader fans each triangle out to all 6) using
    // pointShadowShader. Called once per POINT shadow-casting brazier, before the color pass.
    void renderPointShadowPass(const Scene& scene, int slot, const Light& light);

    // --- E1: SSAO state (M2_shadows_plan.md, problem #5) ---
    // Resolution the G-buffer/SSAO textures are allocated at; kept in sync with the real
    // viewport by setViewport() (see resizeSSAO()).
    int ssaoWidth = 0;
    int ssaoHeight = 0;

    Shader gBufferShader;   // shaders/gbuffer.vert/.frag - writes view-space pos/normal
    Shader ssaoShader;      // shaders/fullscreen.vert + ssao.frag - the raw, noisy AO term
    Shader ssaoBlurShader;  // shaders/fullscreen.vert + ssaoblur.frag - smooths it out

    GLuint gBufferFBO = 0, gPositionTex = 0, gNormalTex = 0, gDepthRBO = 0;
    GLuint ssaoFBO = 0, ssaoColorTex = 0;
    GLuint ssaoBlurFBO = 0, ssaoBlurColorTex = 0;
    GLuint ssaoNoiseTex = 0;   // small tiled texture of random per-pixel rotation vectors
    glm::vec3 ssaoKernel[32];  // hemisphere sample offsets, in the surface's own tangent space

    GLuint quadVAO = 0, quadVBO = 0;   // the 2-triangle full-screen quad the two passes above draw

    // Creates every SSAO GPU resource above (called once from the constructor) and computes
    // the (fixed, does not need recomputing per frame) hemisphere kernel.
    void initSSAO();

    // Re-creates the G-buffer/SSAO textures at a new size (the AO texture must be pixel-for-
    // pixel screen-sized, unlike the shadow maps which have their own fixed resolution).
    // Called from setViewport() whenever the size actually changed.
    void resizeSSAO(int width, int height);

    // The 3-pass SSAO pipeline itself: G-buffer -> raw AO -> blur. Called once per frame,
    // after the shadow passes and before the color pass (the color pass samples its result).
    // `view`/`cullFrustum` match whichever camera renderInternal is drawing from.
    void renderSSAO(const Scene& scene, const glm::mat4& view, const Frustum& cullFrustum);

    // NB: the renderer no longer owns any texture. Each object carries a materialIndex into
    // Scene::materials, and we just bind that material's albedo texture. Loading the textures
    // is done on the scene-building side (src/world/), so the renderer only READS the scene.
};
