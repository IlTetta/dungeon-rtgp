// ggx.vert
//
// Vertex shader for GGX (Cook-Torrance) forward shading.
// It outputs, per fragment: the WORLD-space position and normal (the GGX BRDF in ggx.frag
// needs the view/light directions there, and our lights/camera live in world space), the
// texture coordinates for the albedo lookup, and - new in M2 - the vertex position in the
// "light space" of every shadow-casting torch, so ggx.frag can do the depth comparison
// against each shadow map.
//
// The shadow part follows "posLightSpace" from 21_ggx_tex_shadow.vert (Davide Gadia,
// lecture07a): "for the correct rendering of the shadows we need to calculate the vertex
// coordinates also in light coordinates (= using the light as a camera)". Difference from
// the professor's code: there it was a SINGLE directional light (one lightSpaceMatrix);
// here we keep up to MAX_SHADOW_LIGHTS of them (an array), one per torch picked as a shadow
// caster this frame (see Light::castsShadow in scene.h).

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

// M2: one light-space matrix per torch that casts a shadow this frame.
#define MAX_SHADOW_LIGHTS 3
uniform mat4 lightSpaceMatrices[MAX_SHADOW_LIGHTS];
uniform int  numShadowLights;   // how many of lightSpaceMatrices[] are actually in use

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoords;

// M2: the vertex position in each shadow caster's light space.
out vec4 posLightSpace[MAX_SHADOW_LIGHTS];

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

    // M2: project the same world position into each shadow light's space, so the fragment
    // shader can compare depth against the matching shadow map ("posLightSpace =
    // lightSpaceMatrix * mPosition" in the professor's code, repeated per torch).
    for (int i = 0; i < MAX_SHADOW_LIGHTS; ++i) {
        if (i < numShadowLights)
            posLightSpace[i] = lightSpaceMatrices[i] * worldPos;
        else
            posLightSpace[i] = vec4(0.0);   // slot unused this frame
    }

    gl_Position = projection * view * worldPos;
}
