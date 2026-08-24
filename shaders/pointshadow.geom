// pointshadow.geom
//
// Geometry shader for the SINGLE-PASS point-light cubemap shadow (M2, item #6/S1 "F2":
// M2_shadows_plan.md). Standard technique for rendering a cubemap shadow in one draw call
// instead of 6 (see e.g. LearnOpenGL's "point shadows" chapter - not from the professor's
// material, his course has not covered omnidirectional shadows).
//
// For every triangle the vertex shader hands us (still in plain world space, unprojected),
// we re-emit it 6 times, once per cube face: each copy is projected with that face's own
// light-space matrix and tagged with gl_Layer so the rasterizer writes it into the matching
// face of the bound cubemap (Renderer::initShadowMaps attaches the WHOLE cubemap, layered,
// once - not face by face like the old 6-pass version).

#version 410 core

layout (triangles) in;
layout (triangle_strip, max_vertices = 18) out;   // 3 vertices x 6 faces

// one projection*view per cube face, in GL_TEXTURE_CUBE_MAP_POSITIVE_X.. order (see
// Renderer::computePointShadowMatrices)
uniform mat4 lightSpaceMatrices[6];

in vec3 vWorldPos[];   // one per input vertex (3), from pointshadow.vert

out vec3 FragPos;      // to pointshadow.frag: world position of this emitted vertex

void main() {
    for (int face = 0; face < 6; ++face) {
        gl_Layer = face;   // route this copy of the triangle to cube face `face`
        for (int i = 0; i < 3; ++i) {
            FragPos = vWorldPos[i];
            gl_Position = lightSpaceMatrices[face] * vec4(vWorldPos[i], 1.0);
            EmitVertex();
        }
        EndPrimitive();
    }
}
