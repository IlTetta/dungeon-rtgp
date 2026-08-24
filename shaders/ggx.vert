// ggx.vert
//
// Vertex shader for GGX (Cook-Torrance) forward shading.
// It outputs, per fragment: the WORLD-space position and normal (the GGX BRDF in ggx.frag
// needs the view/light directions there, and our lights/camera live in world space) and the
// texture coordinates for the albedo lookup.
//
// M2 shadow rework (Lorenzo, see M2_shadows_plan.md, problem #2/S2): the per-shadow-caster
// "light space" position used to live here, computed per-vertex and interpolated. It now
// lives in ggx.frag instead, computed per-FRAGMENT from FragPos/Normal with a normal-offset
// (push the sampled point slightly along its own normal before projecting into light space).
// Per-fragment is more correct (the interpolated per-vertex position vs. the true per-fragment
// position drift apart on a stretched face, and the offset needs the real per-fragment
// normal), and it lets one SPOT and one POINT/cubemap path share the same "give me a light
// space depth" pattern without duplicating the projection math per shadow-caster in two
// places. This vertex shader is otherwise the same as before.

#version 410 core

// Attribute locations must match Mesh::setupMesh() (see src/engine/mesh.h):
// 0 = Position, 1 = Normal, 2 = TexCoords, 3 = Tangent, 4 = Bitangent.
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
layout (location = 3) in vec3 aTangent;     // not used yet (normal mapping, a later step)
layout (location = 4) in vec3 aBitangent;   // not used yet

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoords;

void main() {
    vec4 worldPos = model * vec4(aPos, 1.0);
    FragPos = worldPos.xyz;

    // Real normal matrix: transpose(inverse(mat3(model))), not mat3(model) alone. The
    // dungeon geometry scales walls/floors NON-uniformly (glm::scale(m, box.size) with
    // box.size = (tileSize, wallHeight, tileSize)), so mat3(model) alone would leave the
    // normals not perpendicular to the surface. It costs one inverse() per vertex, which is
    // negligible at this scene's scale.
    mat3 normalMatrix = transpose(inverse(mat3(model)));
    Normal = normalize(normalMatrix * aNormal);

    TexCoords = aTexCoords;

    gl_Position = projection * view * worldPos;
}
