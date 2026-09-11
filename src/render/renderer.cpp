// renderer.cpp
// See renderer.h for the "why" of each part; here we just implement it.

#include "render/renderer.h"

#include <glfw/glfw3.h>   // glfwGetTime(), used for the torch flicker animation
#include <algorithm>      // std::sort, for the nearest-to-camera shadow light selection
#include <cmath>          // sin(), used for the torch flicker
#include <random>         // SSAO kernel/noise generation (initSSAO)
#include <string>         // std::to_string, used to build "lightPositions[i]" uniform names
#include <vector>

Renderer::Renderer(const char* vertexPath, const char* fragmentPath)
    : shader(vertexPath, fragmentPath),
      fovDegrees(60.0f),
      viewportWidth(1280), viewportHeight(720),
      sceneFBO(1280, 720, /*wantColor*/true, /*wantDepth*/true)
{
    glEnable(GL_DEPTH_TEST);
    projection = glm::mat4(1.0f);

    shadows.init();                             // shadow maps own their resources (render/shadow_maps.h)
    ssao.init(viewportWidth, viewportHeight);   // SSAO owns its own pipeline now (render/ssao_pass.h)
    fog.init();                                 // fog owns its shader + full-screen quad (fog_pass.h)

    // build the structural-instancing cube + instance buffer once (needs the GL context, which is
    // live by now). Whether it is actually used each frame depends on structuralInstancing.
    instancer.init();
}

void Renderer::setViewport(int width, int height) {
    viewportWidth = width;
    viewportHeight = height;

    glViewport(0, 0, width, height);

    // guard against a division by zero if the window is minimized (height becomes 0)
    float aspect = (height > 0) ? (float)width / (float)height : 1.0f;

    // far plane at 500: the dungeon can be ~130 units across on the diagonal, so a nearer far
    // plane (e.g. 100) would clip distant geometry.
    projection = glm::perspective(glm::radians(fovDegrees), aspect, 0.1f, 500.0f);

    // the G-buffer/SSAO/scene textures must be exactly screen-sized (unlike the shadow maps).
    if (width > 0 && height > 0) {
        ssao.resize(width, height);
        sceneFBO.resize(width, height);
    }
}

void Renderer::resetLightFades() {
    shadowWeight.clear();
    lightWeight.clear();
    // renderInternal() re-fills both to all-0 on the next frame, the moment it sees their size
    // no longer matches scene.lights.size() (0 never matches a non-empty scene).
}

void Renderer::render(const Scene& scene, Camera& camera, FrameMetrics& metrics) {
    // Normal (player) path: draw from the camera and cull against that same camera's
    // frustum, built here from this frame's view-projection.
    Frustum frustum = extractFrustum(projection * camera.getViewMatrix());
    renderInternal(scene, camera.getViewMatrix(), camera.Position, frustum, /*hideCeiling*/false, metrics);
}

void Renderer::renderSpectator(const Scene& scene, const glm::mat4& view, const glm::vec3& eye,
    const Frustum& cullFrustum, bool hideCeiling, FrameMetrics& metrics) {
    // Debug path: draw from `view`/`eye` (the far spectator camera) but cull against the
    // frozen `cullFrustum` we were handed (the player's). Same drawing core as render().
    renderInternal(scene, view, eye, cullFrustum, hideCeiling, metrics);
}

void Renderer::renderInternal(const Scene& scene, const glm::mat4& view, const glm::vec3& eye,
    const Frustum& cullFrustum, bool hideCeiling, FrameMetrics& metrics) {
    metrics.drawCalls = 0;
    metrics.trianglesDrawn = 0;
    metrics.objectsDrawn = 0;
    metrics.objectsTotal = (int)scene.objects.size();

    // Pick which lights cast a shadow this frame, split by TYPE (torches = SPOT, braziers =
    // POINT), each type ranked by distance to `eye` against its own budget (MAX_SPOT_SHADOWS /
    // MAX_POINT_SHADOWS). Splitting them lets a whole room's worth of shadows be active at once,
    // since a SPOT map is a fraction of a cubemap's cost. A light that doesn't make its type's cut
    // simply falls back to unshadowed.
    int spotShadowLightIndex[MAX_SPOT_SHADOWS];
    int pointShadowLightIndex[MAX_POINT_SHADOWS];
    glm::mat4 lightSpaceMatrices[MAX_SPOT_SHADOWS];   // SPOT slots only
    int numSpotShadows = 0;
    int numPointShadows = 0;

    // keep shadowWeight/lightWeight in sync with the scene - a dungeon regenerate can change how
    // many lights exist, in which case there's no "old" fade to continue and starting at 0 is
    // correct: gaining a slot is instant regardless, and a nonzero weight means "still fading,
    // protect this slot" - a fresh light has nothing to protect.
    if (shadowWeight.size() != scene.lights.size())
        shadowWeight.assign(scene.lights.size(), 0.0f);
    if (lightWeight.size() != scene.lights.size())
        lightWeight.assign(scene.lights.size(), 0.0f);

    float now = (float)glfwGetTime();
    float dt = (lastFrameTime < 0.0f) ? 0.0f : (now - lastFrameTime);   // first frame: no fade yet
    // Clamp dt so a stall (e.g. several new casters lighting up in one frame) can't complete a
    // whole fade in one huge step - keeps the fade spread over several frames even after a spike.
    dt = std::min(dt, 1.0f / 15.0f);
    lastFrameTime = now;

    // Split the shadow-casting lights by type, each list ranked by squared distance to the eye.
    // Distance only, NOT view direction - a light's position doesn't change when you turn your
    // head, so view-frustum ranking made lights reshuffle just from turning in place.
    std::vector<int> spotCandidates, pointCandidates;
    for (size_t i = 0; i < scene.lights.size() && tuning.shadowsEnabled; ++i) {
        if (!scene.lights[i].castsShadow) continue;
        if (scene.lights[i].type == LIGHT_POINT) pointCandidates.push_back((int)i);
        else                                     spotCandidates.push_back((int)i);
    }
    auto byDistToEye = [&](int a, int b) {
        glm::vec3 da = scene.lights[a].position - eye;
        glm::vec3 db = scene.lights[b].position - eye;
        return glm::dot(da, da) < glm::dot(db, db);   // squared distance: avoids sqrt() calls
        };
    std::sort(spotCandidates.begin(), spotCandidates.end(), byDistToEye);
    std::sort(pointCandidates.begin(), pointCandidates.end(), byDistToEye);

    // Pick the nearest N of each type, where N is the type's budget. The rest fade out (see below).
    auto pickCasters = [&](const std::vector<int>& cands, int budget) {
        return std::vector<int>(cands.begin(),
            cands.begin() + std::min((size_t)budget, cands.size()));
        };
    // Budget is the compile-time array size, but capped at RUNTIME by maxSpotShadows/maxPointShadows
    // (clamped to [0, compile-time max]). That runtime cap is what the benchmark's "scaling the number
    // of shadow-casting lights" experiment sweeps - fewer casters => fewer depth passes => less cost.
    int spotBudget  = std::max(0, std::min(MAX_SPOT_SHADOWS,  maxSpotShadows));
    int pointBudget = std::max(0, std::min(MAX_POINT_SHADOWS, maxPointShadows));
    std::vector<int> chosenSpot  = pickCasters(spotCandidates,  spotBudget);
    std::vector<int> chosenPoint = pickCasters(pointCandidates, pointBudget);

    for (int idx : chosenSpot) {
        spotShadowLightIndex[numSpotShadows] = idx;
        lightSpaceMatrices[numSpotShadows] = shadows.computeSpotMatrix(scene.lights[idx]);
        numSpotShadows++;
    }
    for (int idx : chosenPoint)
        pointShadowLightIndex[numPointShadows++] = idx;

    // Combined list, SPOTS first then POINTS: the shading selection below places shadow-casters
    // first, and the per-light shadow uniforms map shading position i < numSpotShadows -> SPOT slot
    // i, otherwise -> POINT slot (i - numSpotShadows).
    int shadowLightIndex[MAX_SHADOW_LIGHTS];
    int numShadowLights = 0;
    for (int j = 0; j < numSpotShadows;  ++j) shadowLightIndex[numShadowLights++] = spotShadowLightIndex[j];
    for (int k = 0; k < numPointShadows; ++k) shadowLightIndex[numShadowLights++] = pointShadowLightIndex[k];

    // Fade only applies to LOSING a slot (gaining one is instant - the torch was already burning).
    float fadeStep = (tuning.shadowFadeSeconds > 0.0f) ? (dt / tuning.shadowFadeSeconds) : 1.0f;
    auto updateFade = [&](const std::vector<int>& cands, const std::vector<int>& chosen) {
        for (int idx : cands) {
            bool active = std::find(chosen.begin(), chosen.end(), idx) != chosen.end();
            shadowWeight[idx] = active ? 1.0f : std::max(0.0f, shadowWeight[idx] - fadeStep);
        }
    };
    updateFade(spotCandidates,  chosenSpot);
    updateFade(pointCandidates, chosenPoint);

    // render each caster's depth map: SPOT = 1 pass (2D), POINT = 6 (one per cubemap face)
    metrics.shadowLights = numShadowLights;
    metrics.shadowPasses = 0;
    for (int slot = 0; slot < numSpotShadows; ++slot) {
        shadows.renderSpotPass(scene, slot, lightSpaceMatrices[slot]);
        metrics.shadowPasses += 1;
    }
    for (int slot = 0; slot < numPointShadows; ++slot) {
        shadows.renderPointPass(scene, slot, scene.lights[pointShadowLightIndex[slot]]);
        metrics.shadowPasses += 6;
    }

    // restore the real viewport (the shadow passes above switched it to a shadow-map size)
    glViewport(0, 0, viewportWidth, viewportHeight);

    // SSAO's own 3-pass pipeline runs from the same camera the color pass is about to draw
    // from, before it (the color pass samples its result). Always runs, even with
    // tuning.ssaoEnabled off, so the blurred texture is never stale - ssaoEnabled just gates
    // whether ggx.frag actually multiplies by it.
    ssao.render(scene, projection, view, cullFrustum, cullingEnabled,
                { tuning.ssaoRadius, tuning.ssaoBias, tuning.ssaoStrength });

    // --- 1. clear the scene FBO (not the screen directly - the fog pass composites this
    // onto the screen afterward, it needs the color AND depth back) ---
    sceneFBO.bind();
    glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // --- 2. things that are the same for the whole frame: camera + lights + shadows ---
    shader.use();
    shader.setMat4("view", view);
    shader.setMat4("projection", projection);
    shader.setVec3("viewPos", eye);   // ggx.frag needs this to build V

    shader.setFloat("ambient", tuning.ambient);
    shader.setFloat("spotBiasMax", tuning.spotBiasMax);
    shader.setFloat("spotBiasMin", tuning.spotBiasMin);
    shader.setFloat("spotNormalOffset", tuning.spotNormalOffset);
    shader.setFloat("pointBiasScale", tuning.pointBiasScale);
    shader.setFloat("pointBiasMinScale", tuning.pointBiasMinScale);
    shader.setFloat("pointNormalOffset", tuning.pointNormalOffset);
    shader.setFloat("pointPCFRadius", tuning.pointPCFRadius);

    // Which lights actually get a uniform slot and shade the scene at all - up to MAX_LIGHTS,
    // nearest to `eye` first (distance only - see the note above the shadow-caster sort on why
    // view direction is deliberately NOT a factor here), not just the first ones in
    // Scene::lights. A light that doesn't get a slot contributes nothing at all (unlike one
    // that just has no shadow), which is what made corridors/far rooms go dark with more
    // lights than MAX_LIGHTS. The shadow-casters chosen above are placed first in this list on
    // purpose - a shadow-casting light that wasn't shaded would be pointless - everything else
    // is filled with the nearest remaining lights.
    std::vector<int> shadeCandidates;
    shadeCandidates.reserve(scene.lights.size());
    for (size_t i = 0; i < scene.lights.size(); ++i) shadeCandidates.push_back((int)i);
    std::sort(shadeCandidates.begin(), shadeCandidates.end(), [&](int a, int b) {
        glm::vec3 da = scene.lights[a].position - eye;
        glm::vec3 db = scene.lights[b].position - eye;
        return glm::dot(da, da) < glm::dot(db, db);
    });

    // Everything not already forced in as a shadow-caster, still nearest-first (shadeCandidates
    // already is, so filtering keeps that order).
    std::vector<int> nonShadowCandidates;
    nonShadowCandidates.reserve(shadeCandidates.size());
    for (int idx : shadeCandidates) {
        bool isForced = false;
        for (int k = 0; k < numShadowLights; ++k)
            if (shadowLightIndex[k] == idx) { isForced = true; break; }
        if (!isForced) nonShadowCandidates.push_back(idx);
    }

    int lightBudget = MAX_LIGHTS - numShadowLights;

    // "D": nearest-budget candidates, with the same margin-hysteresis trick as the shadow-
    // caster selection, so a light near the cutoff doesn't flip in/out every frame.
    std::vector<int> lightD(nonShadowCandidates.begin(),
        nonShadowCandidates.begin() + std::min((size_t)lightBudget, nonShadowCandidates.size()));

    if ((int)nonShadowCandidates.size() > lightBudget && !lightD.empty()) {
        glm::vec3 dCutoff = scene.lights[lightD.back()].position - eye;
        float cutoffDist2 = glm::dot(dCutoff, dCutoff);
        float marginDist2 = cutoffDist2 * tuning.lightHysteresisMargin * tuning.lightHysteresisMargin;

        for (size_t i = lightBudget; i < nonShadowCandidates.size(); ++i) {
            int cand = nonShadowCandidates[i];
            if (lightWeight[cand] <= 0.0f) continue;
            glm::vec3 d = scene.lights[cand].position - eye;
            if (glm::dot(d, d) > marginDist2) continue;

            int victimPos = -1;
            float victimDist2 = -1.0f;
            for (size_t k = 0; k < lightD.size(); ++k) {
                if (lightWeight[lightD[k]] > 0.0f) continue;
                glm::vec3 dv = scene.lights[lightD[k]].position - eye;
                float dv2 = glm::dot(dv, dv);
                if (dv2 > victimDist2) { victimDist2 = dv2; victimPos = (int)k; }
            }
            if (victimPos >= 0) lightD[victimPos] = cand;
        }
    }

    // Gotcha: a light dropped from "D" the instant it's no longer near enough would still pop,
    // since a light only fades visibly while it HAS a uniform slot. So any light still mid-fade
    // (lightWeight > 0) that isn't in D keeps its slot until the fade actually finishes. This
    // relies on weight starting at 0 for a fresh light (see above) - anything that starts it
    // nonzero for lights with no real slot to protect lets stillFading balloon and crowd out D
    // entirely, which is exactly the bug that used to starve genuinely-nearby lights here.
    std::vector<int> stillFading;
    for (int idx : nonShadowCandidates) {
        if (lightWeight[idx] <= 0.0f) continue;
        if (std::find(lightD.begin(), lightD.end(), idx) != lightD.end()) continue;
        stillFading.push_back(idx);
    }
    if ((int)stillFading.size() > lightBudget)
        stillFading.resize(lightBudget);   // rare overflow: keep the nearest still-fading ones

    std::vector<int> chosenLights = stillFading;
    for (int idx : lightD) {
        if ((int)chosenLights.size() >= lightBudget) break;
        chosenLights.push_back(idx);
    }

    int shadeLightIndex[MAX_LIGHTS];
    int lightCount = 0;
    for (int slot = 0; slot < numShadowLights; ++slot)
        shadeLightIndex[lightCount++] = shadowLightIndex[slot];
    for (int idx : chosenLights)
        shadeLightIndex[lightCount++] = idx;

    // Demo aid: the "Direct lights" toggle forces the shaded light count to 0 so the surfaces (and
    // the fog) get NO direct contribution - only the ambient*albedo*AO term survives. That isolates
    // SSAO on camera (raise Ambient, then flip SSAO) and gives the "dark dungeon" shot. Does not
    // touch the shadow passes (harmless waste while off) nor the light selection above.
    int shadedLightCount = directLightsEnabled ? lightCount : 0;
    metrics.activeLights = shadedLightCount;

    // Same "instant on, faded off" shape as shadowWeight above - a torch already burning gets
    // its shading slot back immediately, losing one still fades out over lightFadeSeconds.
    float lightFadeStep = (tuning.lightFadeSeconds > 0.0f) ? (dt / tuning.lightFadeSeconds) : 1.0f;
    for (int idx : nonShadowCandidates) {
        bool active = std::find(chosenLights.begin(), chosenLights.end(), idx) != chosenLights.end();
        if (active)
            lightWeight[idx] = 1.0f;
        else
            lightWeight[idx] = std::max(0.0f, lightWeight[idx] - lightFadeStep);
    }
    // A forced shadow-caster is missing from nonShadowCandidates above, so without this its
    // lightWeight would go stale while its own brightness bypasses it (hasShadow -> lw = 1.0
    // below) - only surfacing the moment it loses its shadow slot and lw suddenly reads
    // whatever was frozen in there. Pin it to 1 the whole time so there's nothing stale left.
    for (int j = 0; j < numShadowLights; ++j)
        lightWeight[shadowLightIndex[j]] = 1.0f;

    // Torches flicker over time instead of being static: a sum of two sines at different
    // frequencies (less mechanical than one), phase-shifted per light so they don't pulse in
    // sync. A pure function of elapsed time, so it stays repeatable for a benchmark run; only
    // the intensity SENT to the shader is animated, scene.lights[i].intensity itself is never
    // modified. Reuses `now` from the fade timer above.
    //
    // The phase is seeded from the light's own scene.lights index (shadeLightIndex[i]), not
    // its slot position i: shadeLightIndex gets re-sorted by distance every frame, so two
    // already fully-lit lights can swap slots just from a tiny change in relative distance -
    // seeding by slot made that swap alone jump the flicker phase, an unfaded pop with nothing
    // to do with the actual pop-in system. Seeding by the light's own index keeps the phase
    // with the light regardless of which slot it lands in.
    float t = now;

    // Also kept per-light (not just sent to the shader): the fog pass reuses these exact
    // already-flickered, already-faded values below, instead of recomputing its own from a
    // second, independent light selection (see the note on FogPass::render in render/fog_pass.h).
    float shadeIntensity[MAX_LIGHTS];

    shader.setInt("numLights", shadedLightCount);
    for (int i = 0; i < lightCount; ++i) {
        const Light& light = scene.lights[shadeLightIndex[i]];
        std::string idx = "[" + std::to_string(i) + "]";

        // Toned down after playtesting felt it too jumpy: half the amplitude and roughly half
        // the frequency of the original pass, so the flame reads as a slow, gentle waver
        // instead of a nervous flicker (and halves how big a jump ANY discontinuity here -
        // e.g. a slot swap - could still produce, on top of it being rarer already).
        float seed = (float)shadeLightIndex[i] * 12.9898f;   // arbitrary per-light phase offset
        float flicker = 0.94f
                       + 0.05f * sin(t * 3.0f + seed)
                       + 0.02f * sin(t * 8.0f + seed * 2.3f);
        flicker = glm::clamp(flicker, 0.85f, 1.05f);   // never fully dark, never a huge spike

        // shadow-casters are placed first in shadeLightIndex above, SPOTS then POINTS. So shading
        // position i < numSpotShadows is SPOT slot i; the next numPointShadows are POINT slot
        // (i - numSpotShadows); the rest have no shadow. Forced (shadow-casting) lights are always
        // fully weighted; everyone else uses its ramped lightWeight, so a light fades in/out
        // instead of popping when it gains/loses its shading slot.
        bool hasShadow = (i < numShadowLights);
        bool sIsPoint  = hasShadow && (i >= numSpotShadows);
        int  sSlot     = !hasShadow ? -1 : (sIsPoint ? (i - numSpotShadows) : i);
        float lw = hasShadow ? 1.0f : lightWeight[shadeLightIndex[i]];
        shadeIntensity[i] = light.intensity * flicker * lw;

        shader.setVec3("lightPositions" + idx, light.position);
        shader.setVec3("lightColors" + idx, light.color);
        shader.setFloat("lightIntensities" + idx, shadeIntensity[i]);
        shader.setFloat("lightRadii" + idx, light.radius);

        shader.setInt("lightShadowSlot" + idx, sSlot);
        shader.setInt("lightShadowIsPoint" + idx, sIsPoint ? 1 : 0);
        shader.setFloat("lightShadowWeight" + idx, hasShadow ? shadowWeight[shadeLightIndex[i]] : 0.0f);
    }

    // Bind every shadow map to its own fixed texture unit (unused slots just aren't sampled).
    // SPOT 2D maps take units 0..MAX_SPOT_SHADOWS-1, POINT cubemaps the units right after.
    for (int slot = 0; slot < MAX_SPOT_SHADOWS; ++slot) {
        std::string idx = "[" + std::to_string(slot) + "]";
        if (slot < numSpotShadows)
            shader.setMat4("lightSpaceMatrices" + idx, lightSpaceMatrices[slot]);
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D, shadows.spotTexture(slot));
        shader.setInt("shadowMaps" + idx, slot);
    }
    for (int slot = 0; slot < MAX_POINT_SHADOWS; ++slot) {
        std::string idx = "[" + std::to_string(slot) + "]";
        int unit = MAX_SPOT_SHADOWS + slot;
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_CUBE_MAP, shadows.pointTexture(slot));
        shader.setInt("pointShadowMaps" + idx, unit);
    }

    // per-object albedo texture goes on the unit right after both shadow-map arrays
    const int ALBEDO_UNIT = MAX_SPOT_SHADOWS + MAX_POINT_SHADOWS;   // = 10
    shader.setInt("albedoMap", ALBEDO_UNIT);

    // blurred SSAO term, one unit further along
    const int SSAO_UNIT = ALBEDO_UNIT + 1;   // = 13
    glActiveTexture(GL_TEXTURE0 + SSAO_UNIT);
    glBindTexture(GL_TEXTURE_2D, ssao.blurredAOTexture());
    shader.setInt("ssaoMap", SSAO_UNIT);
    shader.setVec2("screenSize", glm::vec2((float)viewportWidth, (float)viewportHeight));
    shader.setInt("ssaoOn", tuning.ssaoEnabled ? 1 : 0);

    // --- 3. draw the scene, culled against cullFrustum ---
    // Structural slabs (floor/wall/ceiling) can go through the instancer: grouped by material into
    // up to 3 instanced draw calls instead of one per slab. When it's off, they fall through to the
    // per-object loop below like everything else. The per-object path stays for the props (varied
    // meshes) and is also the honest baseline of the instancing A/B.
    shader.setInt("useInstanceModel", 0);   // default: model comes from the uniform (per-object path)
    if (structuralInstancing)
        instancer.drawStructural(shader, scene, cullFrustum, cullingEnabled, hideCeiling,
                                 ALBEDO_UNIT, metrics);

    for (const RenderObject& obj : scene.objects) {
        // debug overhead view: drop the ceiling slabs, otherwise a top-down camera only sees
        // the closed roof and never the rooms below.
        if (hideCeiling && obj.material == MAT_CEILING)
            continue;

        if (structuralInstancing && isStructural(obj.material))
            continue;   // already drawn by the instancer above

        if (cullingEnabled && !isAABBVisible(cullFrustum, obj.worldBounds))
            continue;   // outside the view: skip it

        shader.setMat4("model", obj.modelMatrix);

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
        metrics.objectsDrawn += 1;
    }

    // "culled" = total minus what we actually drew. Uses objectsDrawn (which the instancer also
    // feeds), NOT drawCalls: with instancing on those differ (many slabs, few calls), so counting
    // by draw calls would wrongly report the instanced slabs as culled.
    metrics.objectsCulled = metrics.objectsTotal - metrics.objectsDrawn;

    // Fog reads back what the main pass just drew into sceneFBO, so it has to come after.
    // Hand it the exact list of lights (and their already-flickered, already-faded
    // intensities) the color pass just used, instead of letting it pick its own - see the
    // note on FogPass::render (render/fog_pass.h) for why a second, independent selection popped.
    std::vector<int> fogLightIndex(shadeLightIndex, shadeLightIndex + shadedLightCount);
    std::vector<float> fogLightIntensity(shadeIntensity, shadeIntensity + shadedLightCount);
    glm::mat4 invViewProj = glm::inverse(projection * view);
    fog.render(sceneFBO, scene, eye, invViewProj, fogLightIndex, fogLightIntensity,
               viewportWidth, viewportHeight,
               { tuning.fogEnabled, tuning.fogColor, tuning.fogDensity, tuning.fogScatter,
                 tuning.fogMaxDistance, tuning.fogSteps }, metrics);
}

void Renderer::clean() {
    shader.clean();
    shadows.clean();     // SPOT/POINT FBOs+textures + the two depth-only shaders
    ssao.clean();        // G-buffer/AO FBOs+textures, 3 shaders, noise, kernel, own quad
    fog.clean();         // fog shader + its own full-screen quad
    // sceneFBO cleans up its own GPU objects in its destructor (Framebuffer is RAII)
    instancer.clean();   // cube VAO/VBO/EBO + instance VBO of the structural instancer
}
