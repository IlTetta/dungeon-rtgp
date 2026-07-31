#version 410 core

// Vertex attributes: they match the layout set in mesh.h (setupMesh).
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
// locations 2,3,4 (uv, tangent, bitangent) exist but are not used by this basic shader.

uniform mat4 modelMatrix;
uniform mat4 viewMatrix;
uniform mat4 projectionMatrix;
uniform mat3 normalMatrix;   // = transpose(inverse(mat3(model))), to transform normals correctly

// we send position and normal (in WORLD space) to the fragment shader, where we do the lighting
out vec3 fragWorldPos;
out vec3 fragNormal;

void main() {
    // position of this vertex in world space
    vec4 worldPos = modelMatrix * vec4(aPos, 1.0);
    fragWorldPos = worldPos.xyz;

    // normal in world space (normalMatrix handles the non-uniform scale of our boxes)
    fragNormal = normalize(normalMatrix * aNormal);

    // final clip-space position: projection * view * world
    gl_Position = projectionMatrix * viewMatrix * worldPos;
}
