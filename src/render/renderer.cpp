// renderer.cpp
// See renderer.h for the "why" of each part; here we just implement it.

#include "render/renderer.h"

#include <string>   // std::to_string, used to build "lightPositions[i]" uniform names

Renderer::Renderer(const char* vertexPath, const char* fragmentPath)
    : shader(vertexPath, fragmentPath), fovDegrees(60.0f)
{
    glEnable(GL_DEPTH_TEST);
    projection = glm::mat4(1.0f);
}

void Renderer::setViewport(int width, int height) {
    glViewport(0, 0, width, height);

    // guard against a division by zero if the window is minimized (height becomes 0)
    float aspect = (height > 0) ? (float)width / (float)height : 1.0f;

    // perspective(fov, aspect, near, far): near/far are the distances of the clipping
    // planes. 0.1 is close enough for a first-person view without visible near-clipping,
    // 100.0 is generous for a dungeon room (we are not building an open world here).
    // far plane at 500: the dungeon can be ~130 units across on the diagonal, so a nearer far
    // plane (e.g. 100) would clip distant geometry.
    projection = glm::perspective(glm::radians(fovDegrees), aspect, 0.1f, 500.0f);
}

void Renderer::render(const Scene& scene, Camera& camera, FrameMetrics& metrics) {
    // reset the counters: they describe THIS frame only. Since the culling now happens here,
    // the Renderer is the single writer for all of these object/draw counters.
    metrics.drawCalls = 0;
    metrics.trianglesDrawn = 0;
    metrics.objectsTotal = (int)scene.objects.size();

    // --- 1. clear the screen ---
    // we clear both the color buffer (the picture from last frame) and the depth buffer
    // (the per-pixel "closest distance so far", also from last frame); forgetting the
    // depth buffer would make every new frame's depth test compare against stale values.
    glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // --- 2. things that are the same for the whole frame: camera + lights ---
    shader.use();
    shader.setMat4("view", camera.getViewMatrix());
    shader.setMat4("projection", projection);
    shader.setVec3("viewPos", camera.Position);   // ggx.frag needs this to build V

    // we can only send MAX_LIGHTS lights to the shader in one go; if the scene ever has
    // more torches than that, we simply take the first MAX_LIGHTS and drop the rest for
    // now (picking the closest ones instead of the first ones is a job for a later
    // milestone, once we have culling in place).
    int lightCount = (int)scene.lights.size();
    if (lightCount > MAX_LIGHTS) lightCount = MAX_LIGHTS;
    metrics.activeLights = lightCount;

    shader.setInt("numLights", lightCount);
    for (int i = 0; i < lightCount; ++i) {
        const Light& light = scene.lights[i];
        // build "lightPositions[0]", "lightPositions[1]", ... one string per light
        std::string idx = "[" + std::to_string(i) + "]";
        shader.setVec3("lightPositions" + idx, light.position);
        shader.setVec3("lightColors" + idx, light.color);
        shader.setFloat("lightIntensities" + idx, light.intensity);
        shader.setFloat("lightRadii" + idx, light.radius);
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
        // look up this object's material and bind its albedo texture to unit 0
        const Material& mat = scene.materials[obj.materialIndex];
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, mat.albedo);
        shader.setInt("albedoMap", 0);
        shader.setFloat("uvScale", mat.uvScale);
        shader.setFloat("roughness", mat.roughness);
        shader.setVec3("F0", mat.F0);

        // obj.meshIndex is an index into scene.meshes (see the comment in scene.h on why
        // we store an index here and not a pointer/reference to the Mesh directly)
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
}
