#pragma once

// Shadow-map resources + depth passes, pulled out of the Renderer.
//
// Two light types, two techniques:
//   SPOT (wall torches) -> a 2D depth map, one depth pass, culled against the torch's own cone.
//   POINT (braziers)    -> a distance cubemap (world-space distance in R32F), one LAYERED pass:
//                          the geometry shader (pointshadow.geom) fans each triangle to all 6 faces
//                          at once via gl_Layer, so it is one draw call per object, not six.
//
// This class OWNS the FBOs/textures and the two depth-only shaders, and renders depth into them.
// It deliberately does NOT do the nearest-N caster SELECTION nor the color-pass BINDING of the
// maps: those stay in the Renderer (renderInternal), which owns the light-fade state and the
// texture-unit layout. Same behaviour as the old inline Renderer::render*ShadowPass() methods -
// only the code moved. Same owned-resource pattern as SsaoPass / Framebuffer / StructuralInstancer.

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>   // glm::perspective, glm::lookAt, glm::radians

#include <string>

#include "core/scene.h"
#include "engine/shader.h"
#include "world/frustum_culling.h"   // Frustum, extractFrustum, isAABBVisible

class ShadowMaps {
public:
    // Budgets split by TYPE (the two kinds cost very differently: a SPOT map is 1 pass, a POINT
    // cubemap is 6 faces) and the source of truth for them. The Renderer aliases these into its
    // public MAX_SPOT_SHADOWS / MAX_POINT_SHADOWS / MAX_SHADOW_LIGHTS (which renderInternal, the HUD
    // and the benchmark use). These MUST match "#define MAX_SPOT_SHADOWS / MAX_POINT_SHADOWS" in
    // shaders/ggx.frag. The color pass binds MAX_SPOT 2D maps + MAX_POINT cubemaps + albedo + SSAO
    // = 12 texture units, well under the GL 4.1 minimum of 16.
    static const int MAX_SPOT  = 8;    // SPOT (torch) 2D maps
    static const int MAX_POINT = 2;    // POINT (brazier) cubemaps

    // Resolution of a SPOT 2D map (square). 512 keeps them cheap and a touch soft. POINT faces are
    // the same size, but a cubemap is 6 of them, so the per-caster texel budget is already 6x.
    static const int SPOT_SIZE  = 512;
    static const int POINT_SIZE = 512;

    // Shaders need the GL context, live by the time this (a Renderer member) is constructed.
    ShadowMaps()
        : shadowShader("shaders/shadowmap.vert", "shaders/shadowmap.frag"),
          pointShadowShader("shaders/pointshadow.vert", "shaders/pointshadow.geom", "shaders/pointshadow.frag")
    {}

    // Create the FBOs + textures for both kinds, once, at startup.
    void init() {
        // SPOT (cone) shadows: one FBO + one 2D depth texture per potential shadow-casting slot,
        // created once at startup and reused every frame.
        for (int i = 0; i < MAX_SPOT; ++i) {
            glGenFramebuffers(1, &shadowFBO[i]);

            glGenTextures(1, &shadowMapTex[i]);
            glBindTexture(GL_TEXTURE_2D, shadowMapTex[i]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT,
                         SPOT_SIZE, SPOT_SIZE, 0,
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
                         POINT_SIZE, POINT_SIZE, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        }
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

        for (int i = 0; i < MAX_POINT; ++i) {
            glGenTextures(1, &shadowCubeTex[i]);
            glBindTexture(GL_TEXTURE_CUBE_MAP, shadowCubeTex[i]);
            for (int face = 0; face < 6; ++face) {
                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_R32F,
                             POINT_SIZE, POINT_SIZE, 0, GL_RED, GL_FLOAT, nullptr);
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

    // Build the light-space matrix (projection * view from the torch's point of view) for one SPOT
    // caster. Perspective (not orthographic): a torch's shadow only needs to cover the cone it is
    // aimed into. The Renderer both renders the SPOT pass with this AND sends it to the color pass.
    glm::mat4 computeSpotMatrix(const Light& light) const {
        // 120 degrees is a wide indoor cone for a wall-mounted torch - wide enough that a prop
        // edging up to the SIDE of a torch keeps its shadow (a narrower cone dropped it while the
        // omnidirectional lighting still lit it). near/far follow the light's own attenuation
        // radius, so shadow-map precision and light attenuation stay consistent with each other.
        float nearPlane = 0.05f;
        float farPlane = (light.radius > nearPlane) ? light.radius : (nearPlane + 1.0f);

        glm::mat4 lightProjection = glm::perspective(glm::radians(120.0f), 1.0f, nearPlane, farPlane);

        // "up" for lookAt() cannot be parallel to the look direction. Torches are aimed roughly
        // horizontally, so world-up is safe; a torch aimed straight up/down would need a fallback.
        glm::vec3 dir = glm::normalize(light.direction);
        glm::mat4 lightView = glm::lookAt(light.position, light.position + dir, glm::vec3(0.0f, 1.0f, 0.0f));

        return lightProjection * lightView;
    }

    // Render the scene depth-only into the SPOT slot's 2D map, using the given light-space matrix.
    void renderSpotPass(const Scene& scene, int slot, const glm::mat4& lightSpaceMatrix) {
        glViewport(0, 0, SPOT_SIZE, SPOT_SIZE);
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

    // Render the scene depth-only into the POINT slot's cubemap - all 6 faces in ONE pass (the
    // geometry shader fans each triangle to every face). Stores world-space distance per texel.
    void renderPointPass(const Scene& scene, int slot, const Light& light) {
        glm::mat4 faces[6];
        computePointMatrices(light, faces);

        glViewport(0, 0, POINT_SIZE, POINT_SIZE);
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
        glCullFace(GL_BACK);   // front faces (see renderSpotPass): keeps contact shadows tight

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

    // The maps the color pass binds: SPOT 2D depth map / POINT distance cubemap for a given slot.
    GLuint spotTexture(int slot) const { return shadowMapTex[slot]; }
    GLuint pointTexture(int slot) const { return shadowCubeTex[slot]; }

    void clean() {
        shadowShader.clean();
        pointShadowShader.clean();
        for (int i = 0; i < MAX_SPOT; ++i) {
            glDeleteFramebuffers(1, &shadowFBO[i]);
            glDeleteTextures(1, &shadowMapTex[i]);
        }
        for (int i = 0; i < MAX_POINT; ++i) {
            glDeleteFramebuffers(1, &shadowCubeFBO[i]);
            glDeleteTextures(1, &shadowCubeTex[i]);
        }
        glDeleteTextures(1, &pointShadowDepthCubeTex);
    }

private:
    // Which materials cast shadows. Flat structural slabs (floors, ceilings) never cast a useful
    // shadow and only self-shadow into grazing-angle acne; wall torches (MAT_DECOR) sit right at
    // their own light, so their bracket would shadow the light that spawns them. None of these cast.
    static bool isShadowCaster(MaterialId m) {
        return m != MAT_FLOOR && m != MAT_CEILING && m != MAT_DECOR;
    }

    // The POINT pass renders all 6 faces in one draw, so it can't cull per face like SPOT does -
    // cheapest correct stand-in: skip an object whose AABB doesn't overlap the light's falloff
    // sphere (past that distance it contributes ~0 light anyway).
    static bool aabbIntersectsSphere(const AABB& box, const glm::vec3& center, float radius) {
        glm::vec3 closest = glm::clamp(center, box.min, box.max);
        glm::vec3 d = closest - center;
        return glm::dot(d, d) <= radius * radius;
    }

    // 90-degree-FOV perspective aimed down each of +-X/+-Y/+-Z from the light, in
    // GL_TEXTURE_CUBE_MAP_POSITIVE_X.. face order.
    void computePointMatrices(const Light& light, glm::mat4 outFaces[6]) const {
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

    Shader shadowShader;        // shaders/shadowmap.vert/.frag - SPOT depth
    Shader pointShadowShader;   // shaders/pointshadow.vert/.geom/.frag - POINT layered distance

    GLuint shadowFBO[MAX_SPOT];        // SPOT: one depth FBO per slot
    GLuint shadowMapTex[MAX_SPOT];     // SPOT: one 2D depth map per slot
    GLuint shadowCubeFBO[MAX_POINT];   // POINT: one FBO per slot
    GLuint shadowCubeTex[MAX_POINT];   // POINT: one distance cubemap (R32F) per slot
    GLuint pointShadowDepthCubeTex = 0;   // POINT: one shared depth cubemap, reused across slots
};
