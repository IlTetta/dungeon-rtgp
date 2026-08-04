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
    projection = glm::perspective(glm::radians(fovDegrees), aspect, 0.1f, 100.0f);
}

glm::vec3 Renderer::albedoForMaterial(MaterialId material) const {
    switch (material) {
        case MAT_FLOOR: return glm::vec3(0.45f, 0.40f, 0.33f);   // dusty brown stone
        case MAT_WALL:  return glm::vec3(0.55f, 0.55f, 0.58f);   // cold grey stone
        case MAT_PROP:  return glm::vec3(0.60f, 0.42f, 0.20f);   // warm wood/metal tint
        default:        return glm::vec3(1.0f, 0.0f, 1.0f);      // magenta = "forgot a case"
    }
}

float Renderer::roughnessForMaterial(MaterialId material) const {
    // "a" in the GGX formulas: 0 = mirror-smooth, 1 = very rough. All our current
    // materials are rough, unpolished surfaces (stone, wood), so these are all on the
    // rough half of the range; a shinier PROP (metal torch holder) would get a lower
    // value once we split materials further.
    switch (material) {
        case MAT_FLOOR: return 0.85f;   // worn, uneven stone
        case MAT_WALL:  return 0.75f;   // rough-cut stone
        case MAT_PROP:  return 0.45f;   // wood/metal, a bit smoother
        default:         return 0.7f;
    }
}

glm::vec3 Renderer::f0ForMaterial(MaterialId material) const {
    // F0 = reflectance at 0 degrees incidence. For ordinary (non-metal) "dielectric"
    // materials this is nearly grey and close to 0.04, regardless of the material's
    // actual color (that is a well-known simplification used until we add a proper
    // metalness workflow with textures). None of our current materials are metals, so
    // every material gets the same placeholder value for now.
    (void)material;   // not used yet, kept as a parameter so callers do not need to change
                       // once different materials need different F0 (e.g. a metal prop)
    return glm::vec3(0.04f);
}

void Renderer::render(const Scene& scene, Camera& camera, FrameMetrics& metrics) {
    // reset the counters: they describe THIS frame only, Renderer is the single writer
    // for these two fields (see the comment in metrics.h)
    metrics.drawCalls = 0;
    metrics.trianglesDrawn = 0;

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

    // --- 3. one draw call per object in the scene ---
    for (const RenderObject& obj : scene.objects) {
        shader.setMat4("model", obj.modelMatrix);
        shader.setVec3("baseColor", albedoForMaterial(obj.material));
        shader.setFloat("roughness", roughnessForMaterial(obj.material));
        shader.setVec3("F0", f0ForMaterial(obj.material));

        // obj.meshIndex is an index into scene.meshes (see the comment in scene.h on why
        // we store an index here and not a pointer/reference to the Mesh directly)
        const Mesh& mesh = scene.meshes[obj.meshIndex];
        mesh.draw();

        metrics.drawCalls += 1;
        metrics.trianglesDrawn += (int)(mesh.indices.size() / 3);
    }
}

void Renderer::clean() {
    shader.clean();
}
