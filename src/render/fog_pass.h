#pragma once

// Volumetric fog pass.
//
// A single full-screen ray march (shaders/fog.frag): it reads back the scene color + depth that the
// main color pass drew into an off-screen FBO, and composites an atmospheric haze on top, straight
// onto the screen. Owns its shader and its own full-screen quad.
//
// The Renderer calls render() after the color pass, handing it the SAME lights the color pass just
// used (same indices, same already-flickered-and-faded intensities), so the fog picks its
// MAX_FOG_LIGHTS-nearest subset of THAT list instead of re-sorting scene.lights on its own: a torch
// entering or leaving the fog's set then only changes through the already faded weights, it never
// pops on its own.

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include "core/scene.h"
#include "core/metrics.h"
#include "engine/shader.h"
#include "render/framebuffer.h"   // the off-screen scene color+depth this pass reads from

class FogPass {
public:
    // Fog only marches with a handful of lights. MUST match "#define MAX_FOG_LIGHTS" in fog.frag.
    static const int MAX_FOG_LIGHTS = 4;

    // The fog knobs, from the renderer's ShadingTuning (kept there so the HUD edits one place).
    struct Params {
        bool      enabled;
        glm::vec3 color;
        float     density;
        float     scatter;
        float     maxDistance;
        int       steps;
    };

    FogPass() : fogShader("shaders/fullscreen.vert", "shaders/fog.frag") {}

    // Build the full-screen quad. Call once, after the GL context exists.
    void init() {
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
    }

    // Composite the fog onto the screen from `sceneFBO`'s color+depth. `shadeLightIndex` /
    // `shadeLightIntensity` are exactly what the color pass used. Writes metrics.fogSteps and, at the
    // end, blits the scene depth to the screen so later overlays (particles, wireframe) depth-test
    // against the real scene again.
    void render(Framebuffer& sceneFBO, const Scene& scene, const glm::vec3& eye,
                const glm::mat4& invViewProj,
                const std::vector<int>& shadeLightIndex, const std::vector<float>& shadeLightIntensity,
                int viewportWidth, int viewportHeight, const Params& p, FrameMetrics& metrics) {
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
        fogShader.setVec3("fogColor", p.color);
        fogShader.setFloat("fogDensity", p.enabled ? p.density : 0.0f);
        fogShader.setFloat("fogScatter", p.scatter);
        fogShader.setFloat("fogMaxDistance", p.maxDistance);
        int steps = p.enabled ? p.steps : 1;   // density 0 makes 1 step a no-op, cheaply
        fogShader.setInt("fogSteps", steps);
        metrics.fogSteps = steps;

        // Fog only marches with a handful of lights, not the full shaded set: narrow
        // shadeLightIndex down to its MAX_FOG_LIGHTS nearest members. This sorts WITHIN an
        // already stable list rather than re-picking from scene.lights, so which lights make the
        // cut only changes when the main pass's own set changes (already faded): nothing here can
        // pop on its own.
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
        // it onto the default framebuffer, so what is drawn to the screen after render() returns (the
        // fire particles, the debug frustum wireframe) depth-tests against the real scene.
        sceneFBO.blitDepthToScreen(viewportWidth, viewportHeight);
    }

    void clean() {
        fogShader.clean();
        glDeleteVertexArrays(1, &quadVAO);
        glDeleteBuffers(1, &quadVBO);
    }

private:
    Shader fogShader;   // shaders/fullscreen.vert + fog.frag
    GLuint quadVAO = 0, quadVBO = 0;   // this pass's own full-screen quad
};
