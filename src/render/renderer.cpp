// renderer.cpp
// See renderer.h for the "why" of each part; here we just implement it.

#include "render/renderer.h"

#include <glfw/glfw3.h>   // glfwGetTime(), used for the torch flicker animation
#include <algorithm>      // std::sort, for the nearest-to-camera shadow light selection
#include <cmath>          // sin(), used for the torch flicker
#include <random>         // SSAO kernel/noise generation (initSSAO)
#include <string>         // std::to_string, used to build "lightPositions[i]" uniform names
#include <vector>

// Which materials cast shadows. Flat structural slabs (floors, ceilings) never cast a useful
// shadow and only self-shadow into grazing-angle acne; wall torches (MAT_DECOR) sit right at
// their own light, so their bracket would shadow the light that spawns them. None of these cast.
static bool isShadowCaster(MaterialId m) {
    return m != MAT_FLOOR && m != MAT_CEILING && m != MAT_DECOR;
}

Renderer::Renderer(const char* vertexPath, const char* fragmentPath)
    : shader(vertexPath, fragmentPath),
      fovDegrees(60.0f),
      viewportWidth(1280), viewportHeight(720),
      shadowShader("shaders/shadowmap.vert", "shaders/shadowmap.frag"),
      pointShadowShader("shaders/pointshadow.vert", "shaders/pointshadow.geom", "shaders/pointshadow.frag"),
      gBufferShader("shaders/gbuffer.vert", "shaders/gbuffer.frag"),
      ssaoShader("shaders/fullscreen.vert", "shaders/ssao.frag"),
      ssaoBlurShader("shaders/fullscreen.vert", "shaders/ssaoblur.frag"),
      sceneFBO(1280, 720, /*wantColor*/true, /*wantDepth*/true),
      fogShader("shaders/fullscreen.vert", "shaders/fog.frag")
{
    glEnable(GL_DEPTH_TEST);
    projection = glm::mat4(1.0f);

    initShadowMaps();
    initSSAO();

    // How many texture units the fragment shader can sample - this caps how big the shadow budget
    // can grow (MAX_SPOT_SHADOWS + MAX_POINT_SHADOWS + albedo + SSAO must all fit). GL 4.1 only
    // guarantees >= 16, but real GPUs usually report 32. Printed once so we know the real ceiling.
    GLint maxTexUnits = 0;
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &maxTexUnits);
    std::cout << "GL_MAX_TEXTURE_IMAGE_UNITS = " << maxTexUnits
              << "  (shadow color pass uses " << (MAX_SPOT_SHADOWS + MAX_POINT_SHADOWS + 2)
              << ": " << MAX_SPOT_SHADOWS << " spot + " << MAX_POINT_SHADOWS << " point + albedo + ssao)"
              << std::endl;
}

void Renderer::initShadowMaps() {
    // SPOT (cone) shadows: one FBO + one 2D depth texture per potential shadow-casting slot,
    // created once at startup and reused every frame.
    for (int i = 0; i < MAX_SPOT_SHADOWS; ++i) {
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

    for (int i = 0; i < MAX_POINT_SHADOWS; ++i) {
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
    // aimed into. 120 degrees is a wide indoor cone for a wall-mounted torch - wide enough that a
    // prop edging up to the SIDE of a torch keeps its shadow (a narrower cone dropped it while the
    // omnidirectional lighting still lit it). near/far follow the light's own attenuation radius, so
    // shadow-map precision and light attenuation stay consistent with each other.
    float nearPlane = 0.05f;
    float farPlane = (light.radius > nearPlane) ? light.radius : (nearPlane + 1.0f);

    glm::mat4 lightProjection = glm::perspective(glm::radians(120.0f), 1.0f, nearPlane, farPlane);

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

    // Standard shadow mapping: render FRONT faces (cull back), so the recorded depth is the
    // near side of each caster - keeps contact shadows tight under floor-standing props (front-
    // face culling stored the far side, which leaked light under them / looked inverted on the
    // grazing floor). Self-shadow acne is handled by the slope-scaled bias + normal offset in
    // ggx.frag, and the worst offenders (flat floors/ceilings) are excluded from casting anyway.
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    shadowShader.use();
    shadowShader.setMat4("lightSpaceMatrix", lightSpaceMatrix);

    // Still cull against this light's own frustum (not the player's): an object outside the
    // torch's cone cannot occlude anything inside it either, so skipping it is free
    // correctness. This is also the main perf win once several shadow-casters are active.
    Frustum lightFrustum = extractFrustum(lightSpaceMatrix);
    for (const RenderObject& obj : scene.objects) {
        if (!isShadowCaster(obj.material))   // floors/ceilings (self-shadow acne) and torches
            continue;                        // (would self-shadow their own light) don't cast
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
    // This cubemap stores world-space DISTANCE (GL_R32F). Without an explicit glClearColor, an
    // unrendered texel (any direction that hits nothing before the far plane - e.g. toward
    // open floor/ceiling, which never cast, see isShadowCaster) inherits whatever the last
    // glClearColor call set elsewhere, which reads back as "occluder right here" and shadows
    // that whole direction for no reason. Clear to well past the far plane instead, so "nothing
    // here" correctly means "no occluder".
    float farClear = light.radius * 2.0f;
    glClearColor(farClear, farClear, farClear, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);   // clears all 6 layers at once

    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);   // front faces (see renderShadowPass): keeps contact shadows tight

    pointShadowShader.use();
    pointShadowShader.setVec3("lightPos", light.position);
    for (int face = 0; face < 6; ++face)
        pointShadowShader.setMat4("lightSpaceMatrices[" + std::to_string(face) + "]", faces[face]);

    // one draw call per object (not 6) - the geometry shader fans each triangle out to every
    // face that needs it
    for (const RenderObject& obj : scene.objects) {
        if (!isShadowCaster(obj.material))   // floors/ceilings/torches don't cast (see above)
            continue;
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

    // the G-buffer/SSAO/scene textures must be exactly screen-sized (unlike the shadow maps).
    if (width > 0 && height > 0) {
        resizeSSAO(width, height);
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

    // Nearest-`budget` from a (sorted) candidate list, with the same margin-hysteresis as before:
    // an already-active caster (shadowWeight > 0) keeps its slot while within shadowHysteresisMargin
    // of the cutoff, evicting the farthest non-retained slot, so ranking churn near the boundary
    // reads as a graceful hold instead of a flicker.
    auto pickCasters = [&](const std::vector<int>& cands, int budget) {
        std::vector<int> chosen(cands.begin(),
            cands.begin() + std::min((size_t)budget, cands.size()));
        if ((int)cands.size() > budget && !chosen.empty()) {
            glm::vec3 dCut = scene.lights[chosen.back()].position - eye;
            float cutoff2 = glm::dot(dCut, dCut);
            float margin2 = cutoff2 * tuning.shadowHysteresisMargin * tuning.shadowHysteresisMargin;
            for (size_t i = budget; i < cands.size(); ++i) {
                int cand = cands[i];
                if (shadowWeight[cand] <= 0.0f) continue;   // was not active: nothing to protect
                glm::vec3 d = scene.lights[cand].position - eye;
                if (glm::dot(d, d) > margin2) continue;      // too far even with the margin
                int victim = -1; float victim2 = -1.0f;      // evict the farthest non-retained member
                for (size_t k = 0; k < chosen.size(); ++k) {
                    if (shadowWeight[chosen[k]] > 0.0f) continue;
                    glm::vec3 dv = scene.lights[chosen[k]].position - eye;
                    float dv2 = glm::dot(dv, dv);
                    if (dv2 > victim2) { victim2 = dv2; victim = (int)k; }
                }
                if (victim >= 0) chosen[victim] = cand;
            }
        }
        return chosen;
    };
    std::vector<int> chosenSpot  = pickCasters(spotCandidates,  MAX_SPOT_SHADOWS);
    std::vector<int> chosenPoint = pickCasters(pointCandidates, MAX_POINT_SHADOWS);

    for (int idx : chosenSpot) {
        spotShadowLightIndex[numSpotShadows] = idx;
        lightSpaceMatrices[numSpotShadows] = computeLightSpaceMatrix(scene.lights[idx]);
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
        renderShadowPass(scene, slot, lightSpaceMatrices[slot]);
        metrics.shadowPasses += 1;
    }
    for (int slot = 0; slot < numPointShadows; ++slot) {
        renderPointShadowPass(scene, slot, scene.lights[pointShadowLightIndex[slot]]);
        metrics.shadowPasses += 6;
    }

    // restore the real viewport (the shadow passes above switched it to a shadow-map size)
    glViewport(0, 0, viewportWidth, viewportHeight);

    // SSAO's own 3-pass pipeline runs from the same camera the color pass is about to draw
    // from, before it (the color pass samples its result). Always runs, even with
    // tuning.ssaoEnabled off, so the blurred texture is never stale - ssaoEnabled just gates
    // whether ggx.frag actually multiplies by it.
    renderSSAO(scene, view, cullFrustum);

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
    metrics.activeLights = lightCount;

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
    // second, independent light selection (see the "gotcha" note on renderFog's declaration).
    float shadeIntensity[MAX_LIGHTS];

    shader.setInt("numLights", lightCount);
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
        glBindTexture(GL_TEXTURE_2D, shadowMapTex[slot]);
        shader.setInt("shadowMaps" + idx, slot);
    }
    for (int slot = 0; slot < MAX_POINT_SHADOWS; ++slot) {
        std::string idx = "[" + std::to_string(slot) + "]";
        int unit = MAX_SPOT_SHADOWS + slot;
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_CUBE_MAP, shadowCubeTex[slot]);
        shader.setInt("pointShadowMaps" + idx, unit);
    }

    // per-object albedo texture goes on the unit right after both shadow-map arrays
    const int ALBEDO_UNIT = MAX_SPOT_SHADOWS + MAX_POINT_SHADOWS;   // = 10
    shader.setInt("albedoMap", ALBEDO_UNIT);

    // blurred SSAO term, one unit further along
    const int SSAO_UNIT = ALBEDO_UNIT + 1;   // = 13
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

    // Fog reads back what the main pass just drew into sceneFBO, so it has to come after.
    // Hand it the exact list of lights (and their already-flickered, already-faded
    // intensities) the color pass just used, instead of letting it pick its own - see the
    // "gotcha" on renderFog's declaration for why a second, independent selection popped.
    std::vector<int> fogLightIndex(shadeLightIndex, shadeLightIndex + lightCount);
    std::vector<float> fogLightIntensity(shadeIntensity, shadeIntensity + lightCount);
    glm::mat4 invViewProj = glm::inverse(projection * view);
    renderFog(scene, eye, invViewProj, fogLightIndex, fogLightIntensity, metrics);
}

void Renderer::renderFog(const Scene& scene, const glm::vec3& eye, const glm::mat4& invViewProj,
                         const std::vector<int>& shadeLightIndex, const std::vector<float>& shadeLightIntensity,
                         FrameMetrics& metrics) {
    Framebuffer::unbind(viewportWidth, viewportHeight);   // draw the composited result to the screen

    fogShader.use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneFBO.colorTexture());
    fogShader.setInt("sceneColor", 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, sceneFBO.depthTexture());
    fogShader.setInt("sceneDepth", 1);

    fogShader.setMat4("invViewProj", invViewProj);
    fogShader.setVec3("viewPos", eye);
    fogShader.setVec3("fogColor", tuning.fogColor);
    fogShader.setFloat("fogDensity", tuning.fogEnabled ? tuning.fogDensity : 0.0f);
    fogShader.setFloat("fogScatter", tuning.fogScatter);
    fogShader.setFloat("fogMaxDistance", tuning.fogMaxDistance);
    int steps = tuning.fogEnabled ? tuning.fogSteps : 1;   // density 0 makes 1 step a no-op, cheaply
    fogShader.setInt("fogSteps", steps);
    metrics.fogSteps = steps;

    // Fog only marches with a handful of lights, not the full shaded set - narrow
    // shadeLightIndex down to its MAX_FOG_LIGHTS nearest members. This sorts WITHIN an
    // already-stable list rather than re-picking from scene.lights, so which lights make the
    // cut only changes when the main pass's own set changes (already anti-popped) - nothing
    // here can pop on its own anymore.
    const int MAX_FOG_LIGHTS = 4;   // must match #define MAX_FOG_LIGHTS in shaders/fog.frag
    std::vector<int> order(shadeLightIndex.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = (int)i;
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        glm::vec3 da = scene.lights[shadeLightIndex[a]].position - eye;
        glm::vec3 db = scene.lights[shadeLightIndex[b]].position - eye;
        return glm::dot(da, da) < glm::dot(db, db);
    });
    int numFogLights = std::min((int)order.size(), MAX_FOG_LIGHTS);
    fogShader.setInt("numFogLights", numFogLights);
    for (int i = 0; i < numFogLights; ++i) {
        int slot = order[i];
        const Light& light = scene.lights[shadeLightIndex[slot]];
        std::string idx = "[" + std::to_string(i) + "]";

        fogShader.setVec3("fogLightPositions" + idx, light.position);
        fogShader.setVec3("fogLightColors" + idx, light.color);
        fogShader.setFloat("fogLightIntensities" + idx, shadeLightIntensity[slot]);
        fogShader.setFloat("fogLightRadii" + idx, light.radius);
    }

    glDisable(GL_DEPTH_TEST);   // full-screen quad, no depth test needed
    glBindVertexArray(quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);

    // The fog composited COLOR onto the screen, but the scene DEPTH is still only in sceneFBO. Copy
    // it onto the default framebuffer so anything drawn to the screen after render() returns - the
    // fire particles, the debug frustum wireframe - depth-tests against the real scene again (before
    // the fog pass existed, render() left the scene depth on the screen and they relied on that).
    sceneFBO.blitDepthToScreen(viewportWidth, viewportHeight);
}

void Renderer::clean() {
    shader.clean();
    shadowShader.clean();
    pointShadowShader.clean();
    for (int i = 0; i < MAX_SPOT_SHADOWS; ++i) {
        glDeleteFramebuffers(1, &shadowFBO[i]);
        glDeleteTextures(1, &shadowMapTex[i]);
    }
    for (int i = 0; i < MAX_POINT_SHADOWS; ++i) {
        glDeleteFramebuffers(1, &shadowCubeFBO[i]);
        glDeleteTextures(1, &shadowCubeTex[i]);
    }
    glDeleteTextures(1, &pointShadowDepthCubeTex);

    gBufferShader.clean();
    ssaoShader.clean();
    ssaoBlurShader.clean();
    fogShader.clean();
    // sceneFBO cleans up its own GPU objects in its destructor (Framebuffer is RAII)
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
