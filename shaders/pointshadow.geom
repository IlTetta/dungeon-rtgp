// pointshadow.geom
//
// Renders a point light's cubemap shadow in one draw call instead of six: takes each
// triangle in world space and re-emits it 6 times, once per cube face, projected with that
// face's own matrix and tagged with gl_Layer so it lands on the right face of the bound,
// layered cubemap.

#version 410 core

layout (triangles) in;
layout (triangle_strip, max_vertices = 18) out;   // 3 vertices x 6 faces

// one projection*view per cube face, in GL_TEXTURE_CUBE_MAP_POSITIVE_X.. order
uniform mat4 lightSpaceMatrices[6];

in vec3 vWorldPos[];
out vec3 FragPos;

void main() {
    for (int face = 0; face < 6; ++face) {
        gl_Layer = face;
        for (int i = 0; i < 3; ++i) {
            FragPos = vWorldPos[i];
            gl_Position = lightSpaceMatrices[face] * vec4(vWorldPos[i], 1.0);
            EmitVertex();
        }
        EndPrimitive();
    }
}
