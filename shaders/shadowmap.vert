// shadowmap.vert
//
// Vertex shader of the depth-only pass that builds a SPOT shadow map. Based on
// 19_shadowmap.vert (lecture07a): it only moves each vertex into light space
// (lightSpaceMatrix = projection * view of the light), like a normal camera placed on the torch.
//
// Difference from the lab version: there the projection is ORTHOGRAPHIC, right for a directional
// light with parallel rays. A torch is a local light whose rays spread from a point, so on the
// CPU we build a PERSPECTIVE projection (ShadowMaps::computeSpotMatrix). The shader does not care:
// it just gets the finished lightSpaceMatrix.

#version 410 core

layout (location = 0) in vec3 aPos;
// the other attributes (normal, uv, ...) are not needed in a depth-only pass, so they are not
// declared (like in the lab version)

uniform mat4 model;
uniform mat4 lightSpaceMatrix;

void main() {
    gl_Position = lightSpaceMatrix * model * vec4(aPos, 1.0);
}
