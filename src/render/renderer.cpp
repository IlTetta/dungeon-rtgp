// renderer.cpp
// See renderer.h for the "why" of each part; here we just implement it.

#include "render/renderer.h"

#include <glfw/glfw3.h>   // glfwGetTime(), used for the torch flicker animation
#include <algorithm>      // std::sort, for the nearest-to-camera shadow light selection
#include <cmath>          // sin(), used for the torch flicker
#include <string>         // std::to_string, used to build "lightPositions[i]" uniform names
#include <vector>

Renderer::Renderer(const char* vertexPath, const char* fragmentPath)
    : shader(vertexPath, fragmentPath),
      fovDegrees(60.0f),
      viewportWidth(1280), viewportHeight(720),
      // M2: fixed paths, same convention as ggx.vert/.frag - there is only ever one shadow
      // pipeline, no reason to make these configurable from main.cpp.
      shadowShader("shaders/shadowmap.vert", "shaders/shadowmap.frag")
{
    glEnable(GL_DEPTH_TEST);
    projection = glm::mat4(1.0f);

    initShadowMaps();
}

void Renderer::initShadowMaps() {
    // One FBO + one depth texture per potential shadow-casting torch, created once at startup
    // and reused every frame (we just re-render into them). This mirrors the FBO setup in
    // lecture07a's SetupShadowMap(), repeated MAX_SHADOW_LIGHTS times.
    for (int i = 0; i < MAX_SHADOW_LIGHTS; ++i) {
        glGenFramebuffers(1, &shadowFBO[i]);

        glGenTextures(1, &shadowMapTex[i]);
        glBindTexture(GL_TEXTURE_2D, shadowMapTex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT,
                     SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, 0,
                     GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        // white border: any point OUTSIDE the light's frustum samples the border and reads
        // depth = 1.0 (the farthest possible), so it never looks "in shadow" just because it
        // fell outside the torch's cone - same choice as lecture07a.
        float borderColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);

        glBindFramebuffer(GL_FRAMEBUFFER, shadowFBO[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowMapTex[i], 0);
        // this FBO has no color attachment - we only want depth, like the professor's FBO.
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

glm::mat4 Renderer::computeLightSpaceMatrix(const Light& light) const {
    // Perspective, not orthographic (see renderer.h / shaders/shadowmap.vert): a torch's
    // shadow only needs to cover the cone it is aimed into.
    // FOV: wide enough that a wall-mounted torch still covers most of a typical room without
    // pixel-perfect aiming - 100 degrees is a reasonable indoor cone. near/far follow the
    // light's own radius (its attenuation "range" in ggx.frag too), so shadow-map depth
    // precision and light attenuation stay consistent with each other.
    float nearPlane = 0.05f;
    float farPlane = (light.radius > nearPlane) ? light.radius : (nearPlane + 1.0f);

    glm::mat4 lightProjection = glm::perspective(glm::radians(100.0f), 1.0f, nearPlane, farPlane);

    // "up" for lookAt() cannot be parallel to the look direction, or the matrix degenerates.
    // Torches are normally aimed roughly horizontally (into the room), so world-up (0,1,0) is
    // safe; a torch aimed straight up/down would need a fallback, which we do not expect.
    glm::vec3 dir = glm::normalize(light.direction);
    glm::mat4 lightView = glm::lookAt(light.position, light.position + dir, glm::vec3(0.0f, 1.0f, 0.0f));

    return lightProjection * lightView;
}

void Renderer::renderShadowPass(const Scene& scene, int slot, const glm::mat4& lightSpaceMatrix) {
    glViewport(0, 0, SHADOW_MAP_SIZE, SHADOW_MAP_SIZE);
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFBO[slot]);
    glClear(GL_DEPTH_BUFFER_BIT);

    shadowShader.use();
    shadowShader.setMat4("lightSpaceMatrix", lightSpaceMatrix);

    // depth-only: draw every object (occluders outside the view still cast shadows), no
    // culling and no materials, we only care about depth from the light's point of view.
    for (const RenderObject& obj : scene.objects) {
        shadowShader.setMat4("model", obj.modelMatrix);
        const Mesh& mesh = scene.meshes[obj.meshIndex];
        mesh.draw();
    }
    // NOTE: we do NOT count these draw calls in FrameMetrics::drawCalls - that field
    // describes the main color pass (what the HUD/benchmark cares about, see metrics.h);
    // the shadow pass cost shows up in frameTimeMs instead, same as in a real frame budget.

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::setViewport(int width, int height) {
    viewportWidth = width;
    viewportHeight = height;

    glViewport(0, 0, width, height);

    // guard against a division by zero if the window is minimized (height becomes 0)
    float aspect = (height > 0) ? (float)width / (float)height : 1.0f;

    // perspective(fov, aspect, near, far). far plane at 500: the dungeon can be ~130 units
    // across on the diagonal, so a nearer far plane (e.g. 100) would clip distant geometry.
    projection = glm::perspective(glm::radians(fovDegrees), aspect, 0.1f, 500.0f);
}

void Renderer::render(const Scene& scene, Camera& camera, FrameMetrics& metrics) {
    // Normal (player) path: we DRAW from the camera and CULL against that same camera's
    // frustum, built here from this frame's view-projection (Andrea's Gribb-Hartmann
    // extraction, in world/frustum_culling.h).
    Frustum frustum = extractFrustum(projection * camera.getViewMatrix());
    renderInternal(scene, camera.getViewMatrix(), camera.Position, frustum, /*hideCeiling*/false, metrics);
}

void Renderer::renderSpectator(const Scene& scene, const glm::mat4& view, const glm::vec3& eye,
                               const Frustum& cullFrustum, bool hideCeiling, FrameMetrics& metrics) {
    // Debug path: DRAW from `view`/`eye` (the far spectator camera) but CULL against the
    // frozen `cullFrustum` we were handed (the player's). Same drawing core as render().
    renderInternal(scene, view, eye, cullFrustum, hideCeiling, metrics);
}

void Renderer::renderInternal(const Scene& scene, const glm::mat4& view, const glm::vec3& eye,
                              const Frustum& cullFrustum, bool hideCeiling, FrameMetrics& metrics) {
    // reset the counters: they describe THIS frame only. Since the culling happens here, the
    // Renderer is the single writer for all of these object/draw counters.
    metrics.drawCalls = 0;
    metrics.trianglesDrawn = 0;
    metrics.objectsTotal = (int)scene.objects.size();

    // --- M2, step 0: pick which torches cast a shadow this frame, and render their depth
    // maps BEFORE the color pass (the color pass samples them). ---
    // The number of castsShadow torches is meant to vary a lot (Andrea's placement is a
    // tunable count), while MAX_SHADOW_LIGHTS is a fixed perf budget - so every frame we pick
    // the MAX_SHADOW_LIGHTS shadow-casting lights CLOSEST to `eye`, not "the first ones in
    // Scene::lights". This way real shadows follow the player from room to room. lecture07a
    // only handles a single light ("For more lights ... the shader must be modified to
    // consider each case"): nearest-to-camera selection is the standard way a real engine
    // extends single-shadow-map code to many dynamic lights. A light that doesn't make the
    // cut this frame simply falls back to unshadowed (same as castsShadow == false).
    int shadowLightIndex[MAX_SHADOW_LIGHTS];   // -> index into scene.lights
    glm::mat4 lightSpaceMatrices[MAX_SHADOW_LIGHTS];
    int numShadowLights = 0;

    std::vector<int> shadowCandidates;
    for (size_t i = 0; i < scene.lights.size(); ++i) {
        if (scene.lights[i].castsShadow) shadowCandidates.push_back((int)i);
    }
    std::sort(shadowCandidates.begin(), shadowCandidates.end(), [&](int a, int b) {
        glm::vec3 da = scene.lights[a].position - eye;
        glm::vec3 db = scene.lights[b].position - eye;
        return glm::dot(da, da) < glm::dot(db, db);   // squared distance: avoids sqrt() calls
    });
    for (int idx : shadowCandidates) {
        if (numShadowLights >= MAX_SHADOW_LIGHTS) break;
        shadowLightIndex[numShadowLights] = idx;
        lightSpaceMatrices[numShadowLights] = computeLightSpaceMatrix(scene.lights[idx]);
        numShadowLights++;
    }

    for (int slot = 0; slot < numShadowLights; ++slot) {
        renderShadowPass(scene, slot, lightSpaceMatrices[slot]);
    }

    // restore the real viewport (the shadow passes above switched it to SHADOW_MAP_SIZE^2)
    glViewport(0, 0, viewportWidth, viewportHeight);

    // --- 1. clear the screen ---
    // we clear both the color buffer (last frame's picture) and the depth buffer (last
    // frame's per-pixel closest distance); forgetting the depth buffer would make the depth
    // test compare against stale values.
    glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // --- 2. things that are the same for the whole frame: camera + lights + shadows ---
    shader.use();
    shader.setMat4("view", view);
    shader.setMat4("projection", projection);
    shader.setVec3("viewPos", eye);   // ggx.frag needs this to build V

    int lightCount = (int)scene.lights.size();
    if (lightCount > MAX_LIGHTS) lightCount = MAX_LIGHTS;
    metrics.activeLights = lightCount;

    // M2 "dynamic point lights" (work_division.md §4): torches flicker over time instead of
    // being static point lights. The flicker is a sum of two sines at different frequencies
    // (one slow, one fast, for a less mechanical look than a single sine) phase-shifted per
    // light index (the "+ seed"), so torches do not pulse in sync. It is a PURE function of
    // elapsed time: same glfwGetTime() => same result, so it stays compatible with a
    // repeatable benchmark path (Andrea, M3) - no accumulated state / rand. Only the
    // intensity we SEND to the shader is animated: scene.lights[i].intensity (Andrea's real
    // data) is never modified.
    float t = (float)glfwGetTime();

    shader.setInt("numLights", lightCount);
    for (int i = 0; i < lightCount; ++i) {
        const Light& light = scene.lights[i];
        std::string idx = "[" + std::to_string(i) + "]";

        float seed = (float)i * 12.9898f;   // arbitrary per-light phase offset
        float flicker = 0.85f
                       + 0.10f * sin(t * 6.0f  + seed)
                       + 0.05f * sin(t * 17.0f + seed * 2.3f);
        flicker = glm::clamp(flicker, 0.6f, 1.15f);   // never fully dark, never a huge spike

        shader.setVec3("lightPositions" + idx, light.position);
        shader.setVec3("lightColors" + idx, light.color);
        shader.setFloat("lightIntensities" + idx, light.intensity * flicker);
        shader.setFloat("lightRadii" + idx, light.radius);

        // M2: which shadow-map slot (if any) this light uses. Default -1 = "no shadow",
        // overwritten below for the lights we actually picked.
        shader.setInt("lightShadowSlot" + idx, -1);
    }
    for (int slot = 0; slot < numShadowLights; ++slot) {
        int lightIdx = shadowLightIndex[slot];
        if (lightIdx < lightCount) {   // guard: only false if MAX_LIGHTS < MAX_SHADOW_LIGHTS
            std::string idx = "[" + std::to_string(lightIdx) + "]";
            shader.setInt("lightShadowSlot" + idx, slot);
        }
    }

    shader.setInt("numShadowLights", numShadowLights);
    for (int slot = 0; slot < MAX_SHADOW_LIGHTS; ++slot) {
        std::string idx = "[" + std::to_string(slot) + "]";
        if (slot < numShadowLights) {
            shader.setMat4("lightSpaceMatrices" + idx, lightSpaceMatrices[slot]);
        }
        // shadow maps live on texture units 0..MAX_SHADOW_LIGHTS-1; the albedo texture uses
        // the unit right after them (see the object loop). Bind every slot regardless so the
        // sampler is valid (unused ones just are not sampled, since ggx.frag only calls
        // shadowPCF() when lightShadowSlot[i] >= 0).
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D, shadowMapTex[slot]);
        shader.setInt("shadowMaps" + idx, slot);
    }

    // the per-object albedo texture goes on the unit right after the shadow maps, so it never
    // clashes with them (both are sampled in the same draw).
    const int ALBEDO_UNIT = MAX_SHADOW_LIGHTS;   // = 3
    shader.setInt("albedoMap", ALBEDO_UNIT);

    // --- 3. frustum culling: we cull against the passed-in cullFrustum (the player's, even
    // when we draw from the spectator camera). ---

    // --- 4. one draw call per VISIBLE object in the scene ---
    for (const RenderObject& obj : scene.objects) {
        // debug overhead view: drop the ceiling slabs, otherwise a top-down camera only sees
        // the closed roof and never the rooms below.
        if (hideCeiling && obj.material == MAT_CEILING)
            continue;

        if (cullingEnabled && !isAABBVisible(cullFrustum, obj.worldBounds))
            continue;   // outside the view: skip it

        shader.setMat4("model", obj.modelMatrix);

        // look up this object's material and bind its albedo texture to the albedo unit
        const Material& mat = scene.materials[obj.materialIndex];
        glActiveTexture(GL_TEXTURE0 + ALBEDO_UNIT);
        glBindTexture(GL_TEXTURE_2D, mat.albedo);
        shader.setFloat("uvScale", mat.uvScale);
        shader.setFloat("roughness", mat.roughness);
        shader.setVec3("F0", mat.F0);

        const Mesh& mesh = scene.meshes[obj.meshIndex];
        mesh.draw();

        metrics.drawCalls += 1;
        metrics.trianglesDrawn += (int)(mesh.indices.size() / 3);
    }

    // whatever we did not draw was culled
    metrics.objectsCulled = metrics.objectsTotal - metrics.drawCalls;
}

void Renderer::clean() {
    shader.clean();
    shadowShader.clean();
    for (int i = 0; i < MAX_SHADOW_LIGHTS; ++i) {
        glDeleteFramebuffers(1, &shadowFBO[i]);
        glDeleteTextures(1, &shadowMapTex[i]);
    }
}
