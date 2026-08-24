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

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoords;

void main() {
    vec4 worldPos = model * vec4(aPos, 1.0);
    FragPos = worldPos.xyz;

    // Not mat3(model): walls/floors are scaled non-uniformly, so normals need the real
    // inverse-transpose or they end up not perpendicular to the surface.
    mat3 normalMatrix = transpose(inverse(mat3(model)));
    Normal = normalize(normalMatrix * aNormal);

    TexCoords = aTexCoords;

    gl_Position = projection * view * worldPos;
}
