// ggx.vert
//
// Vertex shader for GGX (Cook-Torrance) forward shading.
// Compared to basic.vert, this one also needs the WORLD-space position and normal of
// each fragment, because the GGX BRDF (computed per-fragment, in ggx.frag) needs the
// view direction V and the light direction L at that point, and our lights/camera live
// in world space.

#version 410 coreGet-Item shaders\ggx.vert | Select-Object Name, Length

// Attribute locations must match the ones set up on the CPU side in Mesh::setupMesh()
// (see src/engine/mesh.h): 0 = Position, 1 = Normal, 2 = TexCoords.
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
// locations 3/4 (Tangent, Bitangent) are not used yet: they will matter once we add
// normal mapping (a later milestone), not for GGX with per-object flat normals.

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoords;

void main() {
    vec4 worldPos = model * vec4(aPos, 1.0);
    FragPos = worldPos.xyz;

    // Normals need the "normal matrix" (transpose of the inverse of the upper-left 3x3
    // of "model"), not "model" directly, otherwise a non-uniform scale would leave them
    // not perpendicular to the surface anymore. It costs an inverse per vertex, which is
    // fine for now; if it becomes a bottleneck we can precompute it on the CPU instead.
    mat3 normalMatrix = transpose(inverse(mat3(model)));
    Normal = normalize(normalMatrix * aNormal);

    TexCoords = aTexCoords;

    gl_Position = projection * view * worldPos;
}