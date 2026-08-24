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
      // M2: fixed paths, same convention as ggx.vert/.frag - there is only ever one shadow
      // pipeline of each kind, no reason to make these configurable from main.cpp.
      shadowShader("shaders/shadowmap.vert", "shaders/shadowmap.frag"),
      pointShadowShader("shaders/pointshadow.vert", "shaders/pointshadow.geom", "shaders/pointshadow.frag"),
      // E1: G-buffer + the two full-screen SSAO passes (see renderer.h for what each does).
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
    // created once at startup and reused every frame (we just re-render into them). Mirrors
    // the FBO setup in lecture07a's SetupShadowMap(), repeated MAX_SHADOW_LIGHTS times.
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

    // POINT (cubemap) shadows, single-pass (M2_shadows_plan.md, S1 "F2"): one shared DEPTH
    // CUBEMAP (needs to be a texture, not a renderbuffer, to support layered attachment -
    // see renderer.h), and one FBO + one 6-face color cubemap per slot, storing world-space
    // distance as a single float per texel (see shaders/pointshadow.frag) rather than depth.
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
        // cubemaps must clamp to edge (not border): there is no "outside" a cube, every
        // direction hits some face.
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

        glGenFramebuffers(1, &shadowCubeFBO[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, shadowCubeFBO[i]);
        // glFramebufferTexture (NOT ...Texture2D) attaches all 6 faces at once as a LAYERED
        // target: the geometry shader picks the face per emitted triangle via gl_Layer,
        // instead of us re-binding one face at a time across 6 separate draw calls.
        glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, shadowCubeTex[i], 0);
        glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, pointShadowDepthCubeTex, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
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

    // M2_shadows_plan.md, problem #2/S2: cull FRONT faces during the depth pass (draw only
    // back faces), so the depth recorded for a light-facing surface is the far side of the
    // geometry it belongs to, not the near side - pushes self-shadowing acne behind the
    // surface instead of on top of it. Safe here because every current mesh (dungeon boxes,
    // OBJ props) is closed/solid, so back faces still fully cover what front faces would.
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);

    shadowShader.use();
    shadowShader.setMat4("lightSpaceMatrix", lightSpaceMatrix);

    // depth-only, but still cull against THIS light's own frustum (not the player's): an
    // object outside the torch's 100-degree cone cannot occlude anything inside it either,
    // so skipping it is free correctness, not an approximation. This was the main cost behind
    // the low framerate once several shadow-casters were active at once - see the perf note
    // on renderPointShadowPass() below, where it matters even more.
    Frustum lightFrustum = extractFrustum(lightSpaceMatrix);
    for (const RenderObject& obj : scene.objects) {
        if (!isAABBVisible(lightFrustum, obj.worldBounds))
            continue;
        shadowShader.setMat4("model", obj.modelMatrix);
        const Mesh& mesh = scene.meshes[obj.meshIndex];
        mesh.draw();
    }
    // NOTE: we do NOT count these draw calls in FrameMetrics::drawCalls - that field
    // describes the main color pass (what the HUD/benchmark cares about, see metrics.h);
    // the shadow pass cost shows up in frameTimeMs instead, same as in a real frame budget.

    glDisable(GL_CULL_FACE);   // the color pass does not cull (see test_scene.h's M1 note)
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::computePointShadowMatrices(const Light& light, glm::mat4 outFaces[6]) const {
    // Standard 6-face point-light setup: a 90-degree FOV perspective (exactly covers one
    // face of a cube) aimed down each of +-X/+-Y/+-Z from the light's own position, in the
    // GL_TEXTURE_CUBE_MAP_POSITIVE_X.. face order. near/far follow the light's radius, same
    // reasoning as computeLightSpaceMatrix() for SPOT lights.
    float nearPlane = 0.05f;
    float farPlane = (light.radius > nearPlane) ? light.radius : (nearPlane + 1.0f);
    glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, nearPlane, farPlane);

    const glm::vec3& p = light.position;
    // GL_TEXTURE_CUBE_MAP_POSITIVE_X, NEGATIVE_X, POSITIVE_Y, NEGATIVE_Y, POSITIVE_Z, NEGATIVE_Z
    outFaces[0] = proj * glm::lookAt(p, p + glm::vec3( 1, 0, 0), glm::vec3(0, -1, 0));
    outFaces[1] = proj * glm::lookAt(p, p + glm::vec3(-1, 0, 0), glm::vec3(0, -1, 0));
    outFaces[2] = proj * glm::lookAt(p, p + glm::vec3(0,  1, 0), glm::vec3(0, 0,  1));
    outFaces[3] = proj * glm::lookAt(p, p + glm::vec3(0, -1, 0), glm::vec3(0, 0, -1));
    outFaces[4] = proj * glm::lookAt(p, p + glm::vec3(0, 0,  1), glm::vec3(0, -1, 0));
    outFaces[5] = proj * glm::lookAt(p, p + glm::vec3(0, 0, -1), glm::vec3(0, -1, 0));
}

// A single draw call's geometry-shader output now covers all 6 faces at once, so we can no
// longer cull per-face like the old 6-pass version did (a triangle might only matter to one
// face, but the CPU has to decide per-OBJECT before the single draw call even starts). The
// honest trade-off of going single-pass: fewer draw calls and one CPU-side scene traversal
// instead of 6, but coarser culling and 6x geometry-shader amplification on the GPU. This is
// the cheapest correct stand-in for "per-face" culling: skip an object if its AABB cannot
// possibly be seen from ANY face, i.e. it does not overlap a sphere of the light's own
// falloff radius around its position (objects past that distance contribute ~0 light anyway).
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
    glCullFace(GL_FRONT);   // same acne fix as the SPOT pass, see renderShadowPass()

    pointShadowShader.use();
    pointShadowShader.setVec3("lightPos", light.position);
    for (int face = 0; face < 6; ++face)
        pointShadowShader.setMat4("lightSpaceMatrices[" + std::to_string(face) + "]", faces[face]);

    // ONE draw call per object (not 6): the geometry shader fans each triangle out to every
    // face that needs it.
    for (const RenderObject& obj : scene.objects) {
        if (!aabbIntersectsSphere(obj.worldBounds, light.position, light.radius))
            continue;
        pointShadowShader.setMat4("model", obj.modelMatrix);
        const Mesh& mesh = scene.meshes[obj.meshIndex];
        mesh.draw();
    }
    // NOTE: same as renderShadowPass() - these draw calls are not counted in
    // FrameMetrics::drawCalls, but DO count in FrameMetrics::shadowPasses (see
    // renderInternal()): still reported as 6 "face-equivalents" per point-shadow-caster, for
    // an apples-to-apples GPU cost comparison against the earlier 6-pass version in the
    // "#lights scaling" benchmark experiment, even though it is now 1 real draw call.

    glDisable(GL_CULL_FACE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::initSSAO() {
    // The full-screen quad both SSAO passes draw: 2 triangles in NDC space, position + UV
    // interleaved. Not a Mesh (engine/mesh.h) on purpose - that class carries 5 vertex
    // attributes meant for real 3D geometry, this is 2 floats + 2 floats for a fixed shape
    // that never changes, a dedicated tiny VAO/VBO is simpler than forcing it through Mesh.
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

    // Hemisphere sample kernel (LearnOpenGL's SSAO technique): 32 vectors in tangent space,
    // z >= 0 (a hemisphere, not a full sphere - samples only ever point "outward" from the
    // surface), scattered more densely near the origin (lerp(0.1, 1.0, t*t)) so nearby
    // occluders matter more than distant ones. Computed once here, not per frame - it is the
    // same 32 vectors every time, only their ORIENTATION (via the noise texture, per pixel)
    // changes at runtime.
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

    // 4x4 tile of random rotation vectors (z = 0, we only need a rotation AROUND the normal,
    // not a 3D direction) - texture-repeated across the screen in ssao.frag so every pixel's
    // kernel is rotated a little differently, turning what would otherwise be banding
    // artifacts into noise (which the blur pass then removes).
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
    if (width == ssaoWidth && height == ssaoHeight) return;   // nothing to do

    // free whatever we already had (resizeSSAO can run again later, e.g. a window resize)
    if (gBufferFBO) {
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

    // --- G-buffer: view-space position + normal, floating point (they are NOT colors, a
    // normal or a position component is routinely outside [0,1] or negative) ---
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

    // --- raw (noisy) AO term: single channel is enough, it is just a 0..1 factor ---
    glGenFramebuffers(1, &ssaoFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, ssaoFBO);
    glGenTextures(1, &ssaoColorTex);
    glBindTexture(GL_TEXTURE_2D, ssaoColorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, width, height, 0, GL_RED, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ssaoColorTex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // --- blurred AO term: what ggx.frag actually samples ---
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
    // --- pass 1: G-buffer (view-space position + normal of the closest surface) ---
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

    // --- pass 2: raw AO, from the G-buffer, onto the full-screen quad ---
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

    // --- pass 3: blur, to remove the per-pixel noise the random rotation above introduces ---
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

    // perspective(fov, aspect, near, far). far plane at 500: the dungeon can be ~130 units
    // across on the diagonal, so a nearer far plane (e.g. 100) would clip distant geometry.
    projection = glm::perspective(glm::radians(fovDegrees), aspect, 0.1f, 500.0f);

    // E1: the G-buffer/SSAO textures must be exactly screen-sized (unlike the shadow maps,
    // which have their own fixed resolution) - keep them in sync whenever this changes.
    if (width > 0 && height > 0)
        resizeSSAO(width, height);
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
    int shadowLightIndex[MAX_SHADOW_LIGHTS];    // -> index into scene.lights
    bool shadowIsPoint[MAX_SHADOW_LIGHTS];       // Light::type of that slot's light, this frame
    glm::mat4 lightSpaceMatrices[MAX_SHADOW_LIGHTS];   // SPOT slots only
    int numShadowLights = 0;

    // S3 (M2_shadows_plan.md, problem #3 "pop-in"): keep shadowWeight in sync with the scene -
    // a dungeon regenerate can change how many lights exist, in which case there is no "old"
    // fade to continue, starting every light at 0 is correct.
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

    // S3: the strict nearest-K would be `shadowCandidates[0..MAX_SHADOW_LIGHTS)`, but that
    // flickers a light on/off every frame right at the boundary as the player moves. Instead:
    // start from the strict nearest-K ("chosen"), then let an ALREADY-ACTIVE light
    // (shadowWeight > 0) keep its slot even if something else has technically edged closer,
    // as long as it is still within shadowHysteresisMargin of the cutoff - evicting the
    // farthest slot in `chosen` that isn't itself a retained light, so the slot count never
    // grows past MAX_SHADOW_LIGHTS.
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

    // S3: ramp shadowWeight toward 1 for every light that has a slot this frame, toward 0 for
    // every other castsShadow light (including ones that just lost their slot above, or were
    // evicted as a hysteresis victim) - this turns "gained/lost a slot" into a fade over
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

    // M2 (metrics.h, shadowLights/shadowPasses): a SPOT slot costs 1 depth pass, a POINT slot
    // costs 6 (one per cubemap face) - this is the real per-frame GPU cost the "#lights
    // scaling" benchmark experiment needs.
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

    // E1: SSAO's own 3-pass pipeline (G-buffer -> raw AO -> blur), from the SAME camera the
    // color pass below is about to draw from - has to run before it, the color pass samples
    // its result. Always runs (even with tuning.ssaoEnabled off) so ssaoBlurColorTex is never
    // stale garbage from an earlier frame; ssaoEnabled itself only gates whether ggx.frag
    // actually multiplies by it (see the ssaoOn uniform below) - simpler and more correct
    // than trying to skip the pass and hand the shader a substitute "flat 1.0" texture.
    renderSSAO(scene, view, cullFrustum);

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

    // M2: live-tunable shading/shadow constants (see ShadingTuning in renderer.h) - sent every
    // frame so main.cpp's ImGui panel can drive them without a rebuild.
    shader.setFloat("ambient", tuning.ambient);
    shader.setFloat("spotBiasMax", tuning.spotBiasMax);
    shader.setFloat("spotBiasMin", tuning.spotBiasMin);
    shader.setFloat("spotNormalOffset", tuning.spotNormalOffset);
    shader.setFloat("pointBiasScale", tuning.pointBiasScale);
    shader.setFloat("pointBiasMinScale", tuning.pointBiasMinScale);
    shader.setFloat("pointNormalOffset", tuning.pointNormalOffset);
    shader.setFloat("pointPCFRadius", tuning.pointPCFRadius);

    // M2 (M2_shadows_plan.md, problem #7/L1): which lights actually get a uniform slot and
    // shade the scene at all - up to MAX_LIGHTS, nearest to `eye` first, NOT just "the first
    // MAX_LIGHTS in Scene::lights". With more lights in the dungeon than MAX_LIGHTS, a light
    // that doesn't even get a slot contributes literally nothing (unlike one that just has no
    // shadow, which is still lit) - that is what made corridors/far rooms go dark. The
    // MAX_SHADOW_LIGHTS shadow-casters chosen above are placed FIRST in this list on purpose
    // (a shadow-casting light that were not shaded would be pointless), everything else after
    // them is filled with the nearest remaining lights.
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

    // M2 "dynamic point lights" (work_division.md §4): torches flicker over time instead of
    // being static point lights. The flicker is a sum of two sines at different frequencies
    // (one slow, one fast, for a less mechanical look than a single sine) phase-shifted per
    // light index (the "+ seed"), so torches do not pulse in sync. It is a PURE function of
    // elapsed time: same glfwGetTime() => same result, so it stays compatible with a
    // repeatable benchmark path (Andrea, M3) - no accumulated state / rand. Only the
    // intensity we SEND to the shader is animated: scene.lights[i].intensity (Andrea's real
    // data) is never modified. Reuses `now` from the S3 fade timer above - same instant, no
    // reason to call glfwGetTime() twice.
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

        // Because the shadow-casters were placed FIRST in shadeLightIndex above, the i-th
        // shaded light IS shadow slot i whenever i < numShadowLights - no separate lookup
        // needed, and no light can ever have a shadow without also being shaded.
        bool hasShadow = (i < numShadowLights);
        shader.setInt("lightShadowSlot" + idx, hasShadow ? i : -1);
        shader.setInt("lightShadowIsPoint" + idx, (hasShadow && shadowIsPoint[i]) ? 1 : 0);
        // S3: how much of this light's shadow to actually apply this frame (see the fade
        // loop above) - 0 right when a light is first promoted into a slot, ramping to 1 over
        // shadowFadeSeconds, so a shadow appears gradually instead of snapping on.
        shader.setFloat("lightShadowWeight" + idx, hasShadow ? shadowWeight[shadeLightIndex[i]] : 0.0f);
    }

    for (int slot = 0; slot < MAX_SHADOW_LIGHTS; ++slot) {
        std::string idx = "[" + std::to_string(slot) + "]";
        if (slot < numShadowLights && !shadowIsPoint[slot])
            shader.setMat4("lightSpaceMatrices" + idx, lightSpaceMatrices[slot]);

        // Both kinds of shadow map live on their own fixed texture units regardless of
        // whether this slot is in use this frame (unused ones just are not sampled, since
        // ggx.frag only reads shadowMaps[]/pointShadowMaps[] when lightShadowSlot[i] >= 0) -
        // the sampler uniform still needs a valid, distinct unit either way. SPOT maps take
        // units 0..MAX_SHADOW_LIGHTS-1, POINT cubemaps the next MAX_SHADOW_LIGHTS after that.
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D, shadowMapTex[slot]);
        shader.setInt("shadowMaps" + idx, slot);

        int cubeUnit = MAX_SHADOW_LIGHTS + slot;
        glActiveTexture(GL_TEXTURE0 + cubeUnit);
        glBindTexture(GL_TEXTURE_CUBE_MAP, shadowCubeTex[slot]);
        shader.setInt("pointShadowMaps" + idx, cubeUnit);
    }

    // the per-object albedo texture goes on the unit right after both shadow-map arrays, so
    // it never clashes with them (all are sampled in the same draw).
    const int ALBEDO_UNIT = 2 * MAX_SHADOW_LIGHTS;   // = 8
    shader.setInt("albedoMap", ALBEDO_UNIT);

    // E1: the finished (blurred) SSAO term, one unit further along.
    const int SSAO_UNIT = ALBEDO_UNIT + 1;   // = 9
    glActiveTexture(GL_TEXTURE0 + SSAO_UNIT);
    glBindTexture(GL_TEXTURE_2D, ssaoBlurColorTex);
    shader.setInt("ssaoMap", SSAO_UNIT);
    shader.setVec2("screenSize", glm::vec2((float)viewportWidth, (float)viewportHeight));
    shader.setInt("ssaoOn", tuning.ssaoEnabled ? 1 : 0);

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
