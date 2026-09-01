#pragma once

// SSAO (Screen-Space Ambient Occlusion) pass, pulled out of the Renderer.
//
// It is a self-contained 3-pass pipeline: (1) a G-buffer of view-space position + normal, (2) the
// raw (noisy) AO term sampled with a hemisphere kernel, (3) a blur to remove the per-pixel noise.
// It OWNS all its GPU resources (the two G-buffer targets, the AO + blur targets, the noise
// texture, the hemisphere kernel, its three shaders and its own full-screen quad), so the Renderer
// no longer carries ~15 SSAO-only members. Same pattern as Framebuffer / StructuralInstancer.
//
// The Renderer owns one SsaoPass and, each frame, calls render() after the shadow passes and
// before the color pass, then binds blurredAOTexture() in the color pass (ggx.frag samples it).
// This is behaviour-identical to the old Renderer::renderSSAO() - only the code moved.

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <random>
#include <string>

#include "core/scene.h"
#include "engine/shader.h"
#include "world/frustum_culling.h"   // isAABBVisible, Frustum

class SsaoPass {
public:
    // The three shading knobs ggx uses, kept in the renderer's ShadingTuning (so the HUD edits one
    // place) and handed in each frame.
    struct Params {
        float radius;
        float bias;
        float strength;
    };

    // Shaders are constructed here (they need the GL context, which is live by the time the Renderer
    // - and thus this member - is constructed). The GPU buffers/textures are created in init().
    SsaoPass()
        : gBufferShader("shaders/gbuffer.vert", "shaders/gbuffer.frag"),
          ssaoShader("shaders/fullscreen.vert", "shaders/ssao.frag"),
          ssaoBlurShader("shaders/fullscreen.vert", "shaders/ssaoblur.frag")
    {}

    // Build the full-screen quad, the hemisphere kernel and the noise texture, then size the
    // G-buffer/AO targets to the initial viewport. Call once, after the GL context exists.
    void init(int viewportWidth, int viewportHeight) {
        // Full-screen quad both later passes draw: 2 triangles in NDC, position + UV interleaved.
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

        resize(viewportWidth, viewportHeight);
    }

    // (Re)create the screen-sized G-buffer/AO textures. Called from init() and whenever the viewport
    // size changes (Renderer::setViewport). No-op if the size did not actually change.
    void resize(int width, int height) {
        if (width == width_ && height == height_) return;

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
        width_ = width;
        height_ = height;

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

    // Run the 3-pass pipeline from the given camera (same one the color pass is about to use).
    // `cullFrustum`/`cullingEnabled` match the color pass, so the G-buffer draws the same objects.
    // Leaves the blurred AO in blurredAOTexture() and restores the viewport to the screen size.
    void render(const Scene& scene, const glm::mat4& projection, const glm::mat4& view,
                const Frustum& cullFrustum, bool cullingEnabled, const Params& p) {
        // pass 1: G-buffer (view-space position + normal of the closest surface)
        glBindFramebuffer(GL_FRAMEBUFFER, gBufferFBO);
        glViewport(0, 0, width_, height_);
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
        ssaoShader.setVec2("noiseScale", glm::vec2((float)width_ / 4.0f, (float)height_ / 4.0f));
        ssaoShader.setFloat("radius", p.radius);
        ssaoShader.setFloat("bias", p.bias);
        ssaoShader.setFloat("strength", p.strength);
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
        glViewport(0, 0, width_, height_);
    }

    // The blurred AO texture the color pass samples (ggx.frag "ssaoMap").
    GLuint blurredAOTexture() const { return ssaoBlurColorTex; }

    void clean() {
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

private:
    int width_ = 0, height_ = 0;    // == the viewport size (SSAO textures are screen-sized)

    Shader gBufferShader;   // shaders/gbuffer.vert/.frag - writes view-space pos/normal
    Shader ssaoShader;      // shaders/fullscreen.vert + ssao.frag - the raw, noisy AO term
    Shader ssaoBlurShader;  // shaders/fullscreen.vert + ssaoblur.frag - smooths it out

    GLuint gBufferFBO = 0, gPositionTex = 0, gNormalTex = 0, gDepthRBO = 0;
    GLuint ssaoFBO = 0, ssaoColorTex = 0;
    GLuint ssaoBlurFBO = 0, ssaoBlurColorTex = 0;
    GLuint ssaoNoiseTex = 0;    // small tiled texture of random per-pixel rotation vectors
    glm::vec3 ssaoKernel[32];   // hemisphere sample offsets, in the surface's own tangent space

    GLuint quadVAO = 0, quadVBO = 0;   // this pass's own full-screen quad (2 triangles)
};
