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
      shadowShader("shaders/shadowmap.vert", "shaders/shadowmap.frag"),
      pointShadowShader("shaders/pointshadow.vert", "shaders/pointshadow.geom", "shaders/pointshadow.frag"),
      gBufferShader("shaders/gbuffer.vert", "shaders/gbuffer.frag"),
      ssaoShader("shaders/fullscreen.vert", "shaders/ssao.frag"),
      ssaoBlurShader("shaders/fullscreen.vert", "shaders/ssaoblur.frag")
{
    glEnable(GL_DEPTH_TEST);
    projection = glm::mat4(1.0f);

    initShadowMaps();
    initSSAO();
}

void Renderer::initShadowMaps() {
    // SPOT (cone) shadows: one FBO + one 2D depth texture per potential shadow-casting slot,
    // created once at startup and reused every frame.
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
        // white border: anything outside the light's frustum reads depth = 1.0 (farthest
        // possible), so it never looks "in shadow" just for falling outside the torch's cone.
        float borderColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);

        glBindFramebuffer(GL_FRAMEBUFFER, shadowFBO[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowMapTex[i], 0);
        glDrawBuffer(GL_NONE);   // depth only, no color attachment
        glReadBuffer(GL_NONE);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    // POINT (cubemap) shadows, single-pass: one shared depth cubemap (needs to be a texture,
    // not a renderbuffer, to support layered attachment), and one FBO + one 6-face color
    // cubemap per slot, storing world-space distance as a single float per texel rather than
    // raw depth.
    glGenTextures(1, &pointShadowDepthCubeTex);
    glBindTexture(GL_TEXTURE_CUBE_MAP, pointShadowDepthCubeTex);
    for (int face = 0; face < 6; ++face) {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_DEPTH_COMPONENT,
                     POINT_SHADOW_SIZE, POINT_SHADOW_SIZE, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    for (int i = 0; i < MAX_SHADOW_LIGHTS; ++i) {
        glGenTextures(1, &shadowCubeTex[i]);
        glBindTexture(GL_TEXTURE_CUBE_MAP, shadowCubeTex[i]);
        for (int face = 0; face < 6; ++face) {
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_R32F,
                         POINT_SHADOW_SIZE, POINT_SHADOW_SIZE, 0, GL_RED, GL_FLOAT, nullptr);
        }
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        // cubemaps clamp to edge, not border: there is no "outside" a cube.
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

        glGenFramebuffers(1, &shadowCubeFBO[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, shadowCubeFBO[i]);
        // glFramebufferTexture (not ...Texture2D) attaches all 6 faces at once as a layered
        // target - the geometry shader picks the face per emitted triangle via gl_Layer.
        glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, shadowCubeTex[i], 0);
        glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, pointShadowDepthCubeTex, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
}

glm::mat4 Renderer::computeLightSpaceMatrix(const Light& light) const {
    // Perspective, not orthographic: a torch's shadow only needs to cover the cone it is
    // aimed into. 100 degrees is a reasonable indoor cone for a wall-mounted torch; near/far
    // follow the light's own attenuation radius, so shadow-map precision and light
    // attenuation stay consistent with each other.
    float nearPlane = 0.05f;
    float farPlane = (light.radius > nearPlane) ? light.radius : (nearPlane + 1.0f);

    glm::mat4 lightProjection = glm::perspective(glm::radians(100.0f), 1.0f, nearPlane, farPlane);

    // "up" for lookAt() cannot be parallel to the look direction. Torches are aimed roughly
    // horizontally, so world-up is safe; a torch aimed straight up/down would need a fallback.
    glm::vec3 dir = glm::normalize(light.direction);
    glm::mat4 lightView = glm::lookAt(light.position, light.position + dir, glm::vec3(0.0f, 1.0f, 0.0f));

    return lightProjection * lightView;
}

void Renderer::renderShadowPass(const Scene& scene, int slot, const glm::mat4& lightSpaceMatrix) {
    glViewport(0, 0, SHADOW_MAP_SIZE, SHADOW_MAP_SIZE);
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFBO[slot]);
    glClear(GL_DEPTH_BUFFER_BIT);

    // Cull FRONT faces during the depth pass (draw only back faces): the recorded depth ends
    // up on the far side of the geometry instead of the near side, which pushes self-
    // shadowing acne behind the surface instead of on top of it. Safe here because every
    // current mesh (dungeon boxes, OBJ props) is closed/solid.
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);

    shadowShader.use();
    shadowShader.setMat4("lightSpaceMatrix", lightSpaceMatrix);

    // Still cull against this light's own frustum (not the player's): an object outside the
    // torch's cone cannot occlude anything inside it either, so skipping it is free
    // correctness. This is also the main perf win once several shadow-casters are active.
    Frustum lightFrustum = extractFrustum(lightSpaceMatrix);
    for (const RenderObject& obj : scene.objects) {
        if (!isAABBVisible(lightFrustum, obj.worldBounds))
            continue;
        shadowShader.setMat4("model", obj.modelMatrix);
        const Mesh& mesh = scene.meshes[obj.meshIndex];
        mesh.draw();
    }
    // not counted in FrameMetrics::drawCalls (that field describes the color pass) - the cost
    // shows up in frameTimeMs instead.

    glDisable(GL_CULL_FACE);   // the color pass does not cull
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::computePointShadowMatrices(const Light& light, glm::mat4 outFaces[6]) const {
    // 90-degree FOV perspective (exactly covers one cube face) aimed down each of
    // +-X/+-Y/+-Z from the light's position, in GL_TEXTURE_CUBE_MAP_POSITIVE_X.. face order.
    float nearPlane = 0.05f;
    float farPlane = (light.radius > nearPlane) ? light.radius : (nearPlane + 1.0f);
    glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, nearPlane, farPlane);

    const glm::vec3& p = light.position;
    outFaces[0] = proj * glm::lookAt(p, p + glm::vec3( 1, 0, 0), glm::vec3(0, -1, 0));
    outFaces[1] = proj * glm::lookAt(p, p + glm::vec3(-1, 0, 0), glm::vec3(0, -1, 0));
    outFaces[2] = proj * glm::lookAt(p, p + glm::vec3(0,  1, 0), glm::vec3(0, 0,  1));
    outFaces[3] = proj * glm::lookAt(p, p + glm::vec3(0, -1, 0), glm::vec3(0, 0, -1));
    outFaces[4] = proj * glm::lookAt(p, p + glm::vec3(0, 0,  1), glm::vec3(0, -1, 0));
    outFaces[5] = proj * glm::lookAt(p, p + glm::vec3(0, 0, -1), glm::vec3(0, -1, 0));
}

// One draw call's geometry-shader output now covers all 6 faces at once, so per-face culling
// (like the SPOT pass does) isn't possible here - the CPU has to decide per object before the
// single draw call starts. Cheapest correct stand-in: skip an object if its AABB doesn't
// overlap a sphere of the light's own falloff radius (past that distance it contributes ~0
// light anyway).
static bool aabbIntersectsSphere(const AABB& box, const glm::vec3& center, float radius) {
    glm::vec3 closest = glm::clamp(center, box.min, box.max);
    glm::vec3 d = closest - center;
    return glm::dot(d, d) <= radius * radius;
}

void Renderer::renderPointShadowPass(const Scene& scene, int slot, const Light& light) {
    glm::mat4 faces[6];
    computePointShadowMatrices(light, faces);

    glViewport(0, 0, POINT_SHADOW_SIZE, POINT_SHADOW_SIZE);
    glBindFramebuffer(GL_FRAMEBUFFER, shadowCubeFBO[slot]);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);   // clears all 6 layers at once

    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);   // same acne fix as the SPOT pass

    pointShadowShader.use();
    pointShadowShader.setVec3("lightPos", light.position);
    for (int face = 0; face < 6; ++face)
        pointShadowShader.setMat4("lightSpaceMatrices[" + std::to_string(face) + "]", faces[face]);

    // one draw call per object (not 6) - the geometry shader fans each triangle out to every
    // face that needs it
    for (const RenderObject& obj : scene.objects) {
        if (!aabbIntersectsSphere(obj.worldBounds, light.position, light.radius))
            continue;
        pointShadowShader.setMat4("model", obj.modelMatrix);
        const Mesh& mesh = scene.meshes[obj.meshIndex];
        mesh.draw();
    }
    // reported as 6 "face-equivalents" in FrameMetrics::shadowPasses for a consistent GPU
    // cost comparison, even though it's now 1 real draw call.

    glDisable(GL_CULL_FACE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::initSSAO() {
    // Full-screen quad both SSAO passes draw: 2 triangles in NDC, position + UV interleaved.
    // Not a Mesh - that class carries attributes meant for real 3D geometry, this is a fixed
    // shape that never changes.
    float quadVertices[] = {
        // pos         // uv
        -1.0f,  1.0f,  0.0f, 1.0f,
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
        -1.0f,  1.0f,  0.0f, 1.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f,
    };
    glGenVertexArrays(1, &quadVAO);
    glGenBuffers(1, &quadVBO);
    glBindVertexArray(quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glBindVertexArray(0);

    // Hemisphere sample kernel: 32 vectors in tangent space, z >= 0, scattered more densely
    // near the origin so nearby occluders matter more than distant ones. Fixed once here -
    // only their orientation (via the noise texture, per pixel) changes at runtime.
    std::uniform_real_distribution<float> randZeroOne(0.0f, 1.0f);
    std::default_random_engine gen;
    for (int i = 0; i < 32; ++i) {
        glm::vec3 sample(
            randZeroOne(gen) * 2.0f - 1.0f,
            randZeroOne(gen) * 2.0f - 1.0f,
            randZeroOne(gen));
        sample = glm::normalize(sample) * randZeroOne(gen);
        float scale = (float)i / 32.0f;
        scale = 0.1f + 0.9f * (scale * scale);
        ssaoKernel[i] = sample * scale;
    }

    // 4x4 tile of random rotation vectors (z = 0: a rotation around the normal, not a full 3D
    // direction), tiled across the screen so every pixel's kernel is rotated a bit
    // differently - turns banding into noise, which the blur pass then removes.
    glm::vec3 ssaoNoise[16];
    for (int i = 0; i < 16; ++i)
        ssaoNoise[i] = glm::vec3(randZeroOne(gen) * 2.0f - 1.0f, randZeroOne(gen) * 2.0f - 1.0f, 0.0f);

    glGenTextures(1, &ssaoNoiseTex);
    glBindTexture(GL_TEXTURE_2D, ssaoNoiseTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, 4, 4, 0, GL_RGB, GL_FLOAT, ssaoNoise);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);

    resizeSSAO(viewportWidth, viewportHeight);
}

void Renderer::resizeSSAO(int width, int height) {
    if (width == ssaoWidth && height == ssaoHeight) return;

    if (gBufferFBO) {   // free whatever we already had, if this runs again (e.g. window resize)
        glDeleteFramebuffers(1, &gBufferFBO);
        glDeleteTextures(1, &gPositionTex);
        glDeleteTextures(1, &gNormalTex);
        glDeleteRenderbuffers(1, &gDepthRBO);
        glDeleteFramebuffers(1, &ssaoFBO);
        glDeleteTextures(1, &ssaoColorTex);
        glDeleteFramebuffers(1, &ssaoBlurFBO);
        glDeleteTextures(1, &ssaoBlurColorTex);
    }
    ssaoWidth = width;
    ssaoHeight = height;

    // G-buffer: view-space position + normal, floating point (not colors - components are
    // routinely outside [0,1] or negative).
    glGenFramebuffers(1, &gBufferFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, gBufferFBO);

    glGenTextures(1, &gPositionTex);
    glBindTexture(GL_TEXTURE_2D, gPositionTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0, GL_RGB, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gPositionTex, 0);

    glGenTextures(1, &gNormalTex);
    glBindTexture(GL_TEXTURE_2D, gNormalTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0, GL_RGB, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, gNormalTex, 0);

    GLenum gBufs[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
    glDrawBuffers(2, gBufs);

    glGenRenderbuffers(1, &gDepthRBO);
    glBindRenderbuffer(GL_RENDERBUFFER, gDepthRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, gDepthRBO);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // raw (noisy) AO term: single channel is enough, it's just a 0..1 factor
    glGenFramebuffers(1, &ssaoFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, ssaoFBO);
    glGenTextures(1, &ssaoColorTex);
    glBindTexture(GL_TEXTURE_2D, ssaoColorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, width, height, 0, GL_RED, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ssaoColorTex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // blurred AO term: what ggx.frag actually samples
    glGenFramebuffers(1, &ssaoBlurFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, ssaoBlurFBO);
    glGenTextures(1, &ssaoBlurColorTex);
    glBindTexture(GL_TEXTURE_2D, ssaoBlurColorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, width, height, 0, GL_RED, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ssaoBlurColorTex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::renderSSAO(const Scene& scene, const glm::mat4& view, const Frustum& cullFrustum) {
    // pass 1: G-buffer (view-space position + normal of the closest surface)
    glBindFramebuffer(GL_FRAMEBUFFER, gBufferFBO);
    glViewport(0, 0, ssaoWidth, ssaoHeight);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    gBufferShader.use();
    gBufferShader.setMat4("view", view);
    gBufferShader.setMat4("projection", projection);
    for (const RenderObject& obj : scene.objects) {
        if (cullingEnabled && !isAABBVisible(cullFrustum, obj.worldBounds))
            continue;
        gBufferShader.setMat4("model", obj.modelMatrix);
        scene.meshes[obj.meshIndex].draw();
    }

    // pass 2: raw AO, from the G-buffer, onto the full-screen quad
    glBindFramebuffer(GL_FRAMEBUFFER, ssaoFBO);
    glClear(GL_COLOR_BUFFER_BIT);
    ssaoShader.use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gPositionTex);
    ssaoShader.setInt("gPosition", 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, gNormalTex);
    ssaoShader.setInt("gNormal", 1);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, ssaoNoiseTex);
    ssaoShader.setInt("texNoise", 2);
    for (int i = 0; i < 32; ++i)
        ssaoShader.setVec3("samples[" + std::to_string(i) + "]", ssaoKernel[i]);
    ssaoShader.setMat4("projection", projection);
    ssaoShader.setVec2("noiseScale", glm::vec2((float)ssaoWidth / 4.0f, (float)ssaoHeight / 4.0f));
    ssaoShader.setFloat("radius", tuning.ssaoRadius);
    ssaoShader.setFloat("bias", tuning.ssaoBias);
    ssaoShader.setFloat("strength", tuning.ssaoStrength);
    glBindVertexArray(quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // pass 3: blur, to remove the per-pixel noise the random rotation above introduces
    glBindFramebuffer(GL_FRAMEBUFFER, ssaoBlurFBO);
    glClear(GL_COLOR_BUFFER_BIT);
    ssaoBlurShader.use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ssaoColorTex);
    ssaoBlurShader.setInt("ssaoInput", 0);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, viewportWidth, viewportHeight);
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

    // the G-buffer/SSAO textures must be exactly screen-sized (unlike the shadow maps).
    if (width > 0 && height > 0)
        resizeSSAO(width, height);
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
    metrics.objectsTotal = (int)scene.objects.size();

    // Pick which torches cast a shadow this frame, and render their depth maps before the
    // color pass. The number of castsShadow torches can vary a lot while MAX_SHADOW_LIGHTS is
    // a fixed perf budget, so every frame we pick the ones closest to `eye`, not just the
    // first ones in Scene::lights - this way real shadows follow the player from room to
    // room. A light that doesn't make the cut simply falls back to unshadowed.
    int shadowLightIndex[MAX_SHADOW_LIGHTS];    // -> index into scene.lights
    bool shadowIsPoint[MAX_SHADOW_LIGHTS];       // Light::type of that slot's light, this frame
    glm::mat4 lightSpaceMatrices[MAX_SHADOW_LIGHTS];   // SPOT slots only
    int numShadowLights = 0;

    // keep shadowWeight in sync with the scene - a dungeon regenerate can change how many
    // lights exist, in which case there's no "old" fade to continue, starting at 0 is correct.
    if (shadowWeight.size() != scene.lights.size())
        shadowWeight.assign(scene.lights.size(), 0.0f);

    float now = (float)glfwGetTime();
    float dt = (lastFrameTime < 0.0f) ? 0.0f : (now - lastFrameTime);   // first frame: no fade yet
    lastFrameTime = now;

    std::vector<int> shadowCandidates;
    for (size_t i = 0; i < scene.lights.size(); ++i) {
        if (scene.lights[i].castsShadow) shadowCandidates.push_back((int)i);
    }
    std::sort(shadowCandidates.begin(), shadowCandidates.end(), [&](int a, int b) {
        glm::vec3 da = scene.lights[a].position - eye;
        glm::vec3 db = scene.lights[b].position - eye;
        return glm::dot(da, da) < glm::dot(db, db);   // squared distance: avoids sqrt() calls
    });

    // The strict nearest-K would flicker a light on/off every frame right at the boundary as
    // the player moves. Instead: start from the strict nearest-K ("chosen"), then let an
    // already-active light (shadowWeight > 0) keep its slot even if something else has
    // technically edged closer, as long as it's still within shadowHysteresisMargin of the
    // cutoff - evicting the farthest slot that isn't itself a retained light, so the slot
    // count never grows past MAX_SHADOW_LIGHTS.
    std::vector<int> chosen(shadowCandidates.begin(),
        shadowCandidates.begin() + std::min((size_t)MAX_SHADOW_LIGHTS, shadowCandidates.size()));

    if ((int)shadowCandidates.size() > MAX_SHADOW_LIGHTS) {
        glm::vec3 dCutoff = scene.lights[chosen.back()].position - eye;
        float cutoffDist2 = glm::dot(dCutoff, dCutoff);
        float marginDist2 = cutoffDist2 * tuning.shadowHysteresisMargin * tuning.shadowHysteresisMargin;

        for (size_t i = MAX_SHADOW_LIGHTS; i < shadowCandidates.size(); ++i) {
            int cand = shadowCandidates[i];
            if (shadowWeight[cand] <= 0.0f) continue;   // was not active: nothing to protect
            glm::vec3 d = scene.lights[cand].position - eye;
            if (glm::dot(d, d) > marginDist2) continue;   // too far even with the margin

            // evict the farthest member of `chosen` that is not itself a retained light
            int victimPos = -1;
            float victimDist2 = -1.0f;
            for (size_t k = 0; k < chosen.size(); ++k) {
                if (shadowWeight[chosen[k]] > 0.0f) continue;
                glm::vec3 dv = scene.lights[chosen[k]].position - eye;
                float dv2 = glm::dot(dv, dv);
                if (dv2 > victimDist2) { victimDist2 = dv2; victimPos = (int)k; }
            }
            if (victimPos >= 0) chosen[victimPos] = cand;
        }
    }

    for (int idx : chosen) {
        shadowLightIndex[numShadowLights] = idx;
        shadowIsPoint[numShadowLights] = (scene.lights[idx].type == LIGHT_POINT);
        if (!shadowIsPoint[numShadowLights])
            lightSpaceMatrices[numShadowLights] = computeLightSpaceMatrix(scene.lights[idx]);
        numShadowLights++;
    }

    // ramp shadowWeight toward 1 for every light with a slot this frame, toward 0 for every
    // other castsShadow light - turns "gained/lost a slot" into a fade over
    // shadowFadeSeconds instead of an instant pop.
    float fadeStep = (tuning.shadowFadeSeconds > 0.0f) ? (dt / tuning.shadowFadeSeconds) : 1.0f;
    for (int idx : shadowCandidates) {
        bool active = false;
        for (int k = 0; k < numShadowLights; ++k) if (shadowLightIndex[k] == idx) { active = true; break; }
        float target = active ? 1.0f : 0.0f;
        if (target > shadowWeight[idx])
            shadowWeight[idx] = std::min(target, shadowWeight[idx] + fadeStep);
        else
            shadowWeight[idx] = std::max(target, shadowWeight[idx] - fadeStep);
    }

    // a SPOT slot costs 1 depth pass, a POINT slot costs 6 (one per cubemap face)
    metrics.shadowLights = numShadowLights;
    metrics.shadowPasses = 0;
    for (int slot = 0; slot < numShadowLights; ++slot) {
        if (shadowIsPoint[slot]) {
            renderPointShadowPass(scene, slot, scene.lights[shadowLightIndex[slot]]);
            metrics.shadowPasses += 6;
        } else {
            renderShadowPass(scene, slot, lightSpaceMatrices[slot]);
            metrics.shadowPasses += 1;
        }
    }

    // restore the real viewport (the shadow passes above switched it to a shadow-map size)
    glViewport(0, 0, viewportWidth, viewportHeight);

    // SSAO's own 3-pass pipeline runs from the same camera the color pass is about to draw
    // from, before it (the color pass samples its result). Always runs, even with
    // tuning.ssaoEnabled off, so the blurred texture is never stale - ssaoEnabled just gates
    // whether ggx.frag actually multiplies by it.
    renderSSAO(scene, view, cullFrustum);

    // --- 1. clear the screen ---
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
    // nearest to `eye` first, not just the first ones in Scene::lights. A light that doesn't
    // get a slot contributes nothing at all (unlike one that just has no shadow), which is
    // what made corridors/far rooms go dark with more lights than MAX_LIGHTS. The shadow-
    // casters chosen above are placed first in this list on purpose - a shadow-casting light
    // that wasn't shaded would be pointless - everything else is filled with the nearest
    // remaining lights.
    std::vector<int> shadeCandidates;
    shadeCandidates.reserve(scene.lights.size());
    for (size_t i = 0; i < scene.lights.size(); ++i) shadeCandidates.push_back((int)i);
    std::sort(shadeCandidates.begin(), shadeCandidates.end(), [&](int a, int b) {
        glm::vec3 da = scene.lights[a].position - eye;
        glm::vec3 db = scene.lights[b].position - eye;
        return glm::dot(da, da) < glm::dot(db, db);
    });

    int shadeLightIndex[MAX_LIGHTS];
    int lightCount = 0;
    for (int slot = 0; slot < numShadowLights && lightCount < MAX_LIGHTS; ++slot)
        shadeLightIndex[lightCount++] = shadowLightIndex[slot];
    for (int idx : shadeCandidates) {
        if (lightCount >= MAX_LIGHTS) break;
        bool alreadyPicked = false;
        for (int k = 0; k < numShadowLights; ++k)
            if (shadowLightIndex[k] == idx) { alreadyPicked = true; break; }
        if (!alreadyPicked) shadeLightIndex[lightCount++] = idx;
    }
    metrics.activeLights = lightCount;

    // Torches flicker over time instead of being static: a sum of two sines at different
    // frequencies (less mechanical than one), phase-shifted per light index so they don't
    // pulse in sync. A pure function of elapsed time, so it stays repeatable for a benchmark
    // run; only the intensity SENT to the shader is animated, scene.lights[i].intensity
    // itself is never modified. Reuses `now` from the fade timer above.
    float t = now;

    shader.setInt("numLights", lightCount);
    for (int i = 0; i < lightCount; ++i) {
        const Light& light = scene.lights[shadeLightIndex[i]];
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

        // shadow-casters are placed first in shadeLightIndex above, so the i-th shaded light
        // IS shadow slot i whenever i < numShadowLights - no lookup needed.
        bool hasShadow = (i < numShadowLights);
        shader.setInt("lightShadowSlot" + idx, hasShadow ? i : -1);
        shader.setInt("lightShadowIsPoint" + idx, (hasShadow && shadowIsPoint[i]) ? 1 : 0);
        shader.setFloat("lightShadowWeight" + idx, hasShadow ? shadowWeight[shadeLightIndex[i]] : 0.0f);
    }

    for (int slot = 0; slot < MAX_SHADOW_LIGHTS; ++slot) {
        std::string idx = "[" + std::to_string(slot) + "]";
        if (slot < numShadowLights && !shadowIsPoint[slot])
            shader.setMat4("lightSpaceMatrices" + idx, lightSpaceMatrices[slot]);

        // both kinds of shadow map live on their own fixed texture units regardless of
        // whether this slot is used this frame (unused ones just aren't sampled). SPOT maps
        // take units 0..MAX_SHADOW_LIGHTS-1, POINT cubemaps the next MAX_SHADOW_LIGHTS after.
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D, shadowMapTex[slot]);
        shader.setInt("shadowMaps" + idx, slot);

        int cubeUnit = MAX_SHADOW_LIGHTS + slot;
        glActiveTexture(GL_TEXTURE0 + cubeUnit);
        glBindTexture(GL_TEXTURE_CUBE_MAP, shadowCubeTex[slot]);
        shader.setInt("pointShadowMaps" + idx, cubeUnit);
    }

    // per-object albedo texture goes on the unit right after both shadow-map arrays
    const int ALBEDO_UNIT = 2 * MAX_SHADOW_LIGHTS;   // = 8
    shader.setInt("albedoMap", ALBEDO_UNIT);

    // blurred SSAO term, one unit further along
    const int SSAO_UNIT = ALBEDO_UNIT + 1;   // = 9
    glActiveTexture(GL_TEXTURE0 + SSAO_UNIT);
    glBindTexture(GL_TEXTURE_2D, ssaoBlurColorTex);
    shader.setInt("ssaoMap", SSAO_UNIT);
    shader.setVec2("screenSize", glm::vec2((float)viewportWidth, (float)viewportHeight));
    shader.setInt("ssaoOn", tuning.ssaoEnabled ? 1 : 0);

    // --- 3. one draw call per VISIBLE object in the scene, culled against cullFrustum ---
    for (const RenderObject& obj : scene.objects) {
        // debug overhead view: drop the ceiling slabs, otherwise a top-down camera only sees
        // the closed roof and never the rooms below.
        if (hideCeiling && obj.material == MAT_CEILING)
            continue;

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
    }

    metrics.objectsCulled = metrics.objectsTotal - metrics.drawCalls;
}

void Renderer::clean() {
    shader.clean();
    shadowShader.clean();
    pointShadowShader.clean();
    for (int i = 0; i < MAX_SHADOW_LIGHTS; ++i) {
        glDeleteFramebuffers(1, &shadowFBO[i]);
        glDeleteTextures(1, &shadowMapTex[i]);
        glDeleteFramebuffers(1, &shadowCubeFBO[i]);
        glDeleteTextures(1, &shadowCubeTex[i]);
    }
    glDeleteTextures(1, &pointShadowDepthCubeTex);

    gBufferShader.clean();
    ssaoShader.clean();
    ssaoBlurShader.clean();
    glDeleteFramebuffers(1, &gBufferFBO);
    glDeleteTextures(1, &gPositionTex);
    glDeleteTextures(1, &gNormalTex);
    glDeleteRenderbuffers(1, &gDepthRBO);
    glDeleteFramebuffers(1, &ssaoFBO);
    glDeleteTextures(1, &ssaoColorTex);
    glDeleteFramebuffers(1, &ssaoBlurFBO);
    glDeleteTextures(1, &ssaoBlurColorTex);
    glDeleteTextures(1, &ssaoNoiseTex);
    glDeleteVertexArrays(1, &quadVAO);
    glDeleteBuffers(1, &quadVBO);
}
