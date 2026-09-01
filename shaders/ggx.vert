// ggx.vert
//
// Vertex shader for GGX (Cook-Torrance) forward shading. Outputs world-space position and
// normal (lighting is done in world space) plus texture coordinates. The light-space
// projection for shadows is done per-fragment now, in ggx.frag, not here.

#version 410 core

// Attribute locations match Mesh::setupMesh() (src/engine/mesh.h).
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
layout (location = 3) in vec3 aTangent;     // not used yet (normal mapping)
layout (location = 4) in vec3 aBitangent;   // not used yet

// Per-INSTANCE model matrix (a mat4 spans locations 5..8), used only when the structural geometry
// is drawn instanced (StructuralInstancer). Ignored on the normal per-object path.
layout (location = 5) in mat4 aInstanceModel;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

// 0 = take the model matrix from the `model` uniform (per-object path: props, and structural
// geometry when instancing is off); 1 = take it from the per-instance aInstanceModel attribute
// (structural instancing). Lets one shader serve both paths so the pixels stay identical.
uniform int useInstanceModel;

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoords;

void main() {
    mat4 M = (useInstanceModel == 1) ? aInstanceModel : model;

    vec4 worldPos = M * vec4(aPos, 1.0);
    FragPos = worldPos.xyz;

    // Not mat3(M): walls/floors are scaled non-uniformly, so normals need the real
    // inverse-transpose or they end up not perpendicular to the surface.
    mat3 normalMatrix = transpose(inverse(mat3(M)));
    Normal = normalize(normalMatrix * aNormal);

    TexCoords = aTexCoords;

    gl_Position = projection * view * worldPos;
}
