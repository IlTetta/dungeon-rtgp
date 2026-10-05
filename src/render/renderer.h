#pragma once

// Renderer class.
// A forward renderer with GGX (Cook-Torrance) shading. Once per frame (renderInternal) it:
//   1. picks the shadow-casting lights (nearest ones, separate budgets for SPOT and POINT) and
//      renders their depth maps (ShadowMaps)
//   2. runs the SSAO pipeline (SsaoPass)
//   3. clears the off-screen scene FBO and sets the per-frame uniforms: camera, the selected
//      lights (with flicker and fade), the shadow maps, SSAO
//   4. draws the structural slabs (instanced, if enabled) and then every other visible object,
//      each with its model matrix and material, culling against the frustum
//   5. runs the fog pass (FogPass), which composites the scene onto the screen
//   6. fills the Renderer fields of FrameMetrics, which the HUD and the benchmark read
//
// "Forward" means we compute the contribution of every light against an object directly
// in the fragment shader, in the same pass that draws that object (as opposed to
// "deferred" rendering, which splits this into two passes). It is the simplest approach
// and is enough for the number of lights (torches and braziers) we shade at once.
//
// Shading model: shaders/ggx.frag implements the Cook-Torrance "FDG" BRDF (Fresnel x
// microfacet Distribution x Geometry term) with the GGX/Trowbridge-Reitz distribution,
// Schlick's Fresnel approximation and Smith's (Schlick-GGX) geometry term, plus a Lambert
// diffuse term, with a textured albedo per material.
//
// Shadows: SPOT lights (wall torches) use a 2D depth map + PCF, close to the classic
// shadow-mapping setup of the lab (perspective instead of orthographic, since a torch is a local
// light, not a directional one). POINT lights (braziers) use a cubemap of world-space distances
// instead, since a single cone cannot cover an omnidirectional light.
//
// Volumetric fog: after the color pass, a full-screen ray march (shaders/fog.frag) reads back its
// color+depth and composites an atmospheric haze on top, see FogPass (render/fog_pass.h).

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>   // glm::perspective, glm::lookAt

#include "engine/shader.h"
#include "engine/camera.h"
#include "core/scene.h"
#include "core/metrics.h"
#include "world/frustum_culling.h"   // Andrea's culling, used inside the render loop
#include "render/framebuffer.h"      // off-screen color+depth target the fog pass reads from
#include "render/structural_instancer.h"   // instanced floor/wall/ceiling draw (the instancing A/B)
#include "render/ssao_pass.h"        // the SSAO pipeline, extracted into its own class
#include "render/shadow_maps.h"      // the shadow-map resources + depth passes, own class
#include "render/fog_pass.h"         // the volumetric fog composite, own class

class Renderer {
public:
    // frustum culling on/off. main.cpp flips this from the C key so we can compare ON vs OFF.
    // When on, render() skips every object whose AABB is outside the camera frustum.
    bool cullingEnabled = true;

    // Structural instancing on/off (the second measurable optimization). When on, the color pass
    // draws the floor/wall/ceiling slabs with 3 instanced calls instead of one per slab; the
    // per-object loop then draws only the props. OFF by DEFAULT on purpose: the plain per-object
    // path is the honest baseline, and keeping structural instancing off during the culling
    // experiment stops it from masking culling's draw-call reduction (with it on, the structural
    // draw count is a constant 3 regardless of culling). Flip it for the instancing A/B.
    bool structuralInstancing = false;

    // For the demo video (not a performance option): when false, the color pass shades with 0
    // direct lights, so only the ambient*albedo*AO term remains and SSAO can be shown alone.
    // Default true = normal lighting.
    bool directLightsEnabled = true;

    // Spectator camera (V) only. From up there every torch is far away and the view goes through
    // a long stretch of fog, so the picture was almost black. spectatorLight adds a soft light
    // from the spectator camera itself, and spectatorFog = false skips the fog in that view.
    // The player view is never affected.
    float spectatorLight = 0.8f;
    bool spectatorFog = false;

    // Live-tunable shading/shadow constants, sent to the shaders as uniforms every frame (see
    // renderInternal()). The HUD (hud/hud.h) exposes them, to dial them in without recompiling;
    // the defaults below are what looked right during testing.
    struct ShadingTuning {
        // Master real-time-shadow switch, same idea as ssaoEnabled/fogEnabled below: forces
        // numShadowLights to 0 for the frame (renderInternal never populates shadowCandidates
        // when this is off), so every light renders fully lit/unshadowed. Mainly a diagnostic
        // A/B toggle, e.g. to tell a shadow-bias artifact apart from an SSAO one by watching
        // whether a given visual survives with shadows fully out of the picture.
        bool  shadowsEnabled = true;

        float ambient           = 0.09f;   // base fill light so unlit areas are not pitch black
        float spotBiasMax       = 0.05f;   // SPOT shadow depth bias at grazing angles
        float spotBiasMin       = 0.005f;  // SPOT shadow depth bias at normal incidence
        float spotNormalOffset  = 0.03f;   // SPOT: push along N before the light-space projection
        // Plain world-unit distance bias, same idea as spotBiasMax/Min above, NOT scaled by
        // the light's radius (see ggx.frag: a scaled bias was bigger than the props next to a
        // brazier and erased their contact shadows).
        float pointBiasScale    = 0.05f;   // POINT shadow distance bias at grazing angles
        float pointBiasMinScale = 0.02f;   // POINT shadow distance bias at normal incidence
        float pointNormalOffset = 0.06f;   // POINT: push along N before the distance test
        float pointPCFRadius    = 0.04f;   // POINT shadow softness (0 = hard single-tap)

        // How long a shadow takes to fade out when its light drops out of the nearest-N casters
        // (gaining a slot is instant, the torch was already burning). The fade hides the pop.
        float shadowFadeSeconds = 0.5f;

        // Same idea for the SHADING light selection (which lights get a uniform slot at all,
        // not just which ones get a shadow): with more than MAX_LIGHTS torches nearby, a light
        // could otherwise drop out of the nearest-MAX_LIGHTS set and go instantly dark.
        float lightHysteresisMargin = 1.6f;
        float lightFadeSeconds      = 0.5f;

        // SSAO ("contact shadow" near where props/walls meet the floor, independent of any
        // light), see shaders/ssao.frag. ssaoEnabled is mostly there for quick A/B testing.
        bool  ssaoEnabled = true;
        float ssaoRadius   = 0.3f;    // view-space units: how far the sample hemisphere reaches
        float ssaoBias     = 0.035f;  // fights the SSAO equivalent of shadow acne
        float ssaoStrength = 1.2f;    // contrast of the final AO term

        // Volumetric fog: ray marched from the camera to whatever the main pass drew,
        // see shaders/fog.frag. fogSteps also lands in FrameMetrics::fogSteps for the
        // "fog steps vs fps" benchmark experiment.
        bool  fogEnabled     = true;
        glm::vec3 fogColor   = glm::vec3(0.5f, 0.55f, 0.6f);   // cool, slightly blue mist
        // exp(-density*rayLength), so this alone decides how much haze builds up over a
        // typical room-sized ray: too high buries the shadow/SSAO contrast in a uniform
        // "milky" look, too low makes the fog invisible in a small room. 0.02 reads as a
        // light haze at normal room distances.
        float fogDensity     = 0.02f;    // higher = thicker fog, opaque sooner
        // How much nearby torches light up the fog. In a big room, or with several torches
        // close together, the ray passes near enough lights for long enough that this adds up
        // fast: kept low so it reads as a local glow, not a wash over the whole room.
        float fogScatter     = 0.1f;    // how strongly nearby torches light up the fog
        float fogMaxDistance = 40.0f;    // world units: march no farther than this
        // Ray march sample count: quality/cost knob, and the one the "fog steps vs fps"
        // benchmark experiment dials up/down. 12 is the default because 24 measured a real
        // FPS drop (from 60-100 to 25-36) with MAX_FOG_LIGHTS lights sampled at every step;
        // the slider still goes up to 64 for the experiment itself.
        int   fogSteps       = 12;
    } tuning;

    // How many lights the color pass can shade at once; must match "#define MAX_LIGHTS 32" in
    // shaders/ggx.frag. When the scene has more lights, renderInternal() picks the MAX_LIGHTS
    // nearest to the camera each frame (with a fade, so a light leaving the set does not pop).
    // The selection is per frame, not per object.
    static const int MAX_LIGHTS = 32;

    // Shadow budgets, split by TYPE (a room has many more torches than braziers, and a SPOT map is
    // 1 pass vs a POINT cubemap's 6 faces). The nearest MAX_SPOT_SHADOWS / MAX_POINT_SHADOWS lights
    // to the camera cast a shadow each frame; one that drops out fades (shadowFadeSeconds) instead
    // of popping; a light that never makes the cut is just unshadowed.
    //
    // The map RESOURCES + resolutions now live in ShadowMaps (render/shadow_maps.h). These are the
    // public ALIASES of its budgets, kept here because renderInternal (texture-unit layout), the HUD
    // and the benchmark all read Renderer::MAX_*. The color pass binds MAX_SPOT_SHADOWS 2D maps +
    // MAX_POINT_SHADOWS cubemaps + albedo + SSAO = 12 units (< the GL 4.1 minimum of 16). MUST match
    // "#define MAX_SPOT_SHADOWS / MAX_POINT_SHADOWS" in shaders/ggx.frag.
    static const int MAX_SPOT_SHADOWS  = ShadowMaps::MAX_SPOT;
    static const int MAX_POINT_SHADOWS = ShadowMaps::MAX_POINT;
    static const int MAX_SHADOW_LIGHTS = MAX_SPOT_SHADOWS + MAX_POINT_SHADOWS;   // total casters

    // RUNTIME caps on how many lights actually cast a shadow this frame, clamped to the compile-time
    // budgets above. This is the "scaling the number of dynamic lights" knob of the proposal: the
    // benchmark sweeps it, and frame time responds through the number of shadow depth passes (the
    // dominant per-light cost). Default = full budget. Editable from the HUD "Shadow tuning" panel.
    int maxSpotShadows  = MAX_SPOT_SHADOWS;
    int maxPointShadows = MAX_POINT_SHADOWS;

    // Loads and compiles the given vertex/fragment shader pair (paths relative to the
    // working directory; see the shaders copy step in CMakeLists.txt). main.cpp passes
    // "shaders/ggx.vert" / "shaders/ggx.frag".
    Renderer(const char* vertexPath, const char* fragmentPath);

    // Call this once at startup, and again every time the window is resized: it updates the
    // OpenGL viewport and the projection matrix (they must always match, or the image comes
    // out stretched).
    void setViewport(int width, int height);

    // Call this right after rebuilding the Scene from scratch (dungeon "Regenerate"/"Random
    // seed"/etc.). shadowWeight/lightWeight only auto-reset when scene.lights.size() actually
    // changes, which a same-size regenerate would NOT trigger, leaving old per-light fade
    // state misapplied to a completely different set of lights (one could start already fully
    // lit, another already faded out, until they drift back over the next fade duration).
    // Clearing both here makes every light start its fade from a clean 0, same as a fresh
    // launch.
    void resetLightFades();

    // Draws one whole frame from "camera" (the first-person player camera). Culls against
    // that same camera's frustum. Also writes the Renderer counters into "metrics".
    void render(const Scene& scene, Camera& camera, FrameMetrics& metrics);

    // Debug tool (Andrea): draw the scene from an arbitrary "spectator" viewpoint
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

    // Frees the GPU resources of the renderer and of its passes. Call once, at shutdown.
    void clean();

private:
    // The shared drawing core used by both render() and renderSpectator(): shadow passes,
    // clear, per-frame uniforms (view/eye/lights/shadows), then one draw call per VISIBLE
    // object, culling its AABB against `cullFrustum` (when cullingEnabled) and optionally
    // skipping ceilings. `eye` is both the camera position sent to the shader and the point
    // we pick the nearest shadow-casting lights around.
    void renderInternal(const Scene& scene, const glm::mat4& view, const glm::vec3& eye,
                        const Frustum& cullFrustum, bool hideCeiling, bool spectator,
                        FrameMetrics& metrics);

    Shader shader;
    glm::mat4 projection;

    // Draws the structural slabs instanced when structuralInstancing is on (owns its own cube +
    // instance VBO, built once in the constructor). See render/structural_instancer.h.
    StructuralInstancer instancer;

    // kept so setViewport() can rebuild the projection if we ever change the field of view
    float fovDegrees;

    // current viewport size, so renderInternal() can restore it after the shadow passes
    // (which switch the viewport to a shadow-map resolution).
    int viewportWidth;
    int viewportHeight;

    // Per-scene-light fade weight (0 = no shadow effect, 1 = full), indexed the same way as
    // Scene::lights. It persists ACROSS frames so a light's shadow can fade out instead of
    // popping when it loses its slot (gaining one is instant, see renderInternal). Reset to
    // all-0 whenever its size no longer matches scene.lights.size(): a new light has nothing
    // to fade.
    std::vector<float> shadowWeight;

    // Same idea for the SHADING selection (which lights get a uniform slot / contribute light
    // at all): indexed like scene.lights, persists across frames. A light not forced in as a
    // shadow caster snaps to 1 the instant it holds a shading slot, ramps toward 0 once it
    // does not, and keeps its slot until this reaches 0 (see the "gotcha" note in
    // renderInternal), so a torch fades out instead of vanishing when the player walks into a
    // torch-dense area.
    std::vector<float> lightWeight;

    // Wall-clock time (glfwGetTime()) at the end of the previous renderInternal() call, so we
    // can compute a per-frame delta for the fade above without main.cpp passing one in. -1
    // marks "no previous frame yet".
    float lastFrameTime = -1.0f;

    // --- shadow mapping ---
    // The shadow-map resources (SPOT 2D maps + POINT distance cubemaps) and the two depth passes
    // live in their own class (render/shadow_maps.h). renderInternal does the nearest-N caster
    // SELECTION (it owns the fade state), then per slot calls shadows.computeSpotMatrix(),
    // shadows.renderSpotPass() / shadows.renderPointPass(), and binds shadows.spotTexture(slot) /
    // shadows.pointTexture(slot) in the color pass.
    ShadowMaps shadows;

    // --- SSAO ---
    // The whole screen-space ambient-occlusion pipeline (G-buffer -> raw AO -> blur) lives in its
    // own class (render/ssao_pass.h) and owns all its GPU resources. renderInternal() calls
    // ssao.render(...) before the color pass, then binds ssao.blurredAOTexture() into it; setViewport
    // forwards the resize.
    SsaoPass ssao;

    // --- fog ---
    // The main color pass draws into this off-screen FBO (color + depth) instead of straight to the
    // screen, so the fog pass can read both back afterward. sceneFBO stays here (the color pass owns
    // it); the ray-march composite itself lives in FogPass (render/fog_pass.h), which owns its shader
    // and quad. renderInternal calls fog.render(sceneFBO, ...) after the color pass.
    Framebuffer sceneFBO;
    FogPass fog;

    // NB: the renderer does not own any texture. Each object carries a materialIndex into
    // Scene::materials, and we just bind that material's albedo texture. Loading the textures
    // is done on the scene-building side (src/world/), so the renderer only READS the scene.
};
