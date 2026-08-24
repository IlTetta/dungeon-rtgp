// pointshadow.frag
//
// Fragment shader for a POINT light's cubemap shadow (M2, problem #6/S1), one face at a time
// - which face is decided upstream (pointshadow.geom picks it via gl_Layer, single-pass; see
// that file's header for why there is no per-face vertex/geometry work left to do here).
// Unlike shadowmap.frag (which writes nothing - the SPOT/2D shadow map only needs the GPU's
// own depth buffer), this DOES write a color: the world-space distance from the light to this
// fragment. We store distance instead of raw depth because a cubemap face's depth buffer
// alone cannot be compared across faces in a way that is simple to reason about (each face
// has its own near/far mapping); a plain world distance, read back the same way regardless of
// which face it came from, is the standard technique for point-light shadows.
//
// The color attachment this writes into is a single-channel float texture (GL_R32F, see
// Renderer::initShadowMaps): one distance value per texel, nothing else.

#version 410 core

in vec3 FragPos;

uniform vec3 lightPos;

out float FragDistance;

void main() {
    FragDistance = length(FragPos - lightPos);
}
