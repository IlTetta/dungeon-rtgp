// renderer.cpp
// See renderer.h for the "why" of each part; here we just implement it.

#include "render/renderer.h"

#include <string>   // std::to_string, used to build "lightPositions[i]" uniform names
#include <vector>

Renderer::Renderer(const char* vertexPath, const char* fragmentPath)
    : shader(vertexPath, fragmentPath),
      fovDegrees(60.0f),
      viewportWidth(1280), viewportHeight(720),
      // M2: fixed paths, same convention as ggx.vert/.frag - no reason to make these
      // configurable from main.cpp, there is only ever one shadow pipeline.
      shadowShader("shaders/shadowmap.vert", "shaders/shadowmap.frag")
{
    glEnable(GL_DEPTH_TEST);
    projection = glm::mat4(1.0f);

    initShadowMaps();
}

void Renderer::initShadowMaps() {
    // One FBO + one depth texture per potential shadow-casting torch, created once at
    // startup and reused every frame (we just re-render into them, we do not recreate
    // them). This mirrors the FBO setup in lecture07a's SetupShadowMap(), repeated
    // MAX_SHADOW_LIGHTS times.
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
        // white border: any point OUTSIDE the light's frustum samples the border and
        // reads depth = 1.0 (the farthest possible), so it never looks "in shadow" just
        // because it fell outside the torch's cone - same choice as lecture07a.
        float borderColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);

        glBindFramebuffer(GL_FRAMEBUFFER, shadowFBO[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowMapTex[i], 0);
        // this FBO has no color attachment - we are only interested in depth, exactly
        // like the professor's shadow FBO.
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

glm::mat4 Renderer::computeLightSpaceMatrix(const Light& light) const {
    // Perspective, not orthographic (see renderer.h / shaders/shadowmap.vert comments):
    // a torch's shadow only needs to cover the cone it is aimed into.
    // FOV: wide enough that a torch mounted on a wall still covers most of a typical
    // room without needing to be aimed with pixel-perfect precision - 100 degrees is a
    // reasonable starting point for an indoor cone light; near/far follow the light's
    // own radius (its "range" for normal lighting attenuation too, see ggx.frag), so
    // shadow-map depth precision and light attenuation stay consistent with each other.
    float nearPlane = 0.05f;
    float farPlane = (light.radius > nearPlane) ? light.radius : (nearPlane + 1.0f);

    glm::mat4 lightProjection = glm::perspective(glm::radians(100.0f), 1.0f, nearPlane, farPlane);

    // "up" for lookAt() cannot be parallel to the look direction, or the matrix
    // degenerates. Torches are normally aimed roughly horizontally (into the room), so
    // world-up (0,1,0) is safe; if a torch were ever aimed straight up/down this would
    // need a fallback, which is not a case we expect for wall-mounted torches.
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

    for (const RenderObject& obj : scene.objects) {
        shadowShader.setMat4("model", obj.modelMatrix);
        const Mesh& mesh = scene.meshes[obj.meshIndex];
        mesh.draw();
    }
    // NOTE: we do NOT count these draw calls in FrameMetrics::drawCalls - that field
    // describes the main color pass (what the HUD/benchmark cares about, see
    // metrics.h); the shadow pass cost shows up in frameTimeMs instead, same as it would
    // in a real frame budget.

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::setViewport(int width, int height) {
    viewportWidth = width;
    viewportHeight = height;

    glViewport(0, 0, width, height);
    float aspect = (height > 0) ? (float)width / (float)height : 1.0f;
    projection = glm::perspective(glm::radians(fovDegrees), aspect, 0.1f, 100.0f);
}

glm::vec3 Renderer::albedoForMaterial(MaterialId material) const {
    switch (material) {
        case MAT_FLOOR: return glm::vec3(0.45f, 0.40f, 0.33f);
        case MAT_WALL:  return glm::vec3(0.55f, 0.55f, 0.58f);
        case MAT_PROP:  return glm::vec3(0.60f, 0.42f, 0.20f);
        default:        return glm::vec3(1.0f, 0.0f, 1.0f);
    }
}

float Renderer::roughnessForMaterial(MaterialId material) const {
    switch (material) {
        case MAT_FLOOR: return 0.85f;
        case MAT_WALL:  return 0.75f;
        case MAT_PROP:  return 0.45f;
        default:         return 0.7f;
    }
}

glm::vec3 Renderer::f0ForMaterial(MaterialId material) const {
    (void)material;
    return glm::vec3(0.04f);
}

void Renderer::render(const Scene& scene, Camera& camera, FrameMetrics& metrics) {
    metrics.drawCalls = 0;
    metrics.trianglesDrawn = 0;
    metrics.objectsTotal = (int)scene.objects.size();

    // --- M2, step 0: pick which torches cast a shadow this frame, and render their
    // depth maps BEFORE the main color pass (the main pass needs to sample them). ---
    // We take the first MAX_SHADOW_LIGHTS lights with castsShadow == true, in the order
    // Andrea's scene puts them in Scene::lights; if a room ever had more than that, the
    // extra ones would just render unshadowed (same as any light with castsShadow ==
    // false), not crash or get skipped from lighting entirely.
    int shadowLightIndex[MAX_SHADOW_LIGHTS];   // -> index into scene.lights
    glm::mat4 lightSpaceMatrices[MAX_SHADOW_LIGHTS];
    int numShadowLights = 0;

    for (size_t i = 0; i < scene.lights.size() && numShadowLights < MAX_SHADOW_LIGHTS; ++i) {
        if (scene.lights[i].castsShadow) {
            shadowLightIndex[numShadowLights] = (int)i;
            lightSpaceMatrices[numShadowLights] = computeLightSpaceMatrix(scene.lights[i]);
            numShadowLights++;
        }
    }

    for (int slot = 0; slot < numShadowLights; ++slot) {
        renderShadowPass(scene, slot, lightSpaceMatrices[slot]);
    }

    // restore the real viewport (the shadow passes above switched it to
    // SHADOW_MAP_SIZE x SHADOW_MAP_SIZE)
    glViewport(0, 0, viewportWidth, viewportHeight);

    // --- 1. clear the screen ---
    glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // --- 2. things that are the same for the whole frame: camera + lights + shadows ---
    shader.use();
    shader.setMat4("view", camera.getViewMatrix());
    shader.setMat4("projection", projection);
    shader.setVec3("viewPos", camera.Position);

    int lightCount = (int)scene.lights.size();
    if (lightCount > MAX_LIGHTS) lightCount = MAX_LIGHTS;
    metrics.activeLights = lightCount;

    shader.setInt("numLights", lightCount);
    for (int i = 0; i < lightCount; ++i) {
        const Light& light = scene.lights[i];
        std::string idx = "[" + std::to_string(i) + "]";
        shader.setVec3("lightPositions" + idx, light.position);
        shader.setVec3("lightColors" + idx, light.color);
        shader.setFloat("lightIntensities" + idx, light.intensity);
        shader.setFloat("lightRadii" + idx, light.radius);

        // M2: tell the shader which shadow-map slot (if any) this light uses. Default
        // -1 = "no shadow", overwritten below for the lights we actually picked.
        shader.setInt("lightShadowSlot" + idx, -1);
    }
    for (int slot = 0; slot < numShadowLights; ++slot) {
        int lightIdx = shadowLightIndex[slot];
        if (lightIdx < lightCount) {   // guard: could only be false if MAX_LIGHTS < MAX_SHADOW_LIGHTS, never true here
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
        // bind every slot's texture regardless (unused ones just will not be sampled,
        // since ggx.frag only calls shadowPCF() when lightShadowSlot[i] >= 0), but the
        // sampler uniform still needs a valid, distinct texture unit either way.
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D, shadowMapTex[slot]);
        shader.setInt("shadowMaps" + idx, slot);
    }

    // --- 3. frustum culling: build the 6 planes from this frame's view-projection ---
    // (Andrea's code, in world/frustum_culling.h). When culling is on, we skip every object
    // whose AABB is completely outside these planes: it cannot be seen, so drawing it would
    // just waste a draw call. This is the measurable optimization of the project.
    Frustum frustum = extractFrustum(projection * camera.getViewMatrix());

    // --- 4. one draw call per VISIBLE object in the scene ---
    for (const RenderObject& obj : scene.objects) {
        if (cullingEnabled && !isAABBVisible(frustum, obj.worldBounds))
            continue;   // outside the view: skip it

        shader.setMat4("model", obj.modelMatrix);
        shader.setVec3("baseColor", albedoForMaterial(obj.material));
        shader.setFloat("roughness", roughnessForMaterial(obj.material));
        shader.setVec3("F0", f0ForMaterial(obj.material));

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
