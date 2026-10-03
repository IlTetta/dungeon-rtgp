// particle.frag
// Fragment shader of the fire particles. It turns the square quad into a soft round dot
// computed from the corner position, for additive blending.

#version 410 core

in vec2 vCorner;   // quad corner in [-0.5, 0.5]
in vec3 vColor;
in float vAlpha;

out vec4 FragColor;

void main() {
    // distance from the quad center: 0 at the center, 1 at the middle of the edges
    float r = length(vCorner) * 2.0;
    float falloff = 1.0 - smoothstep(0.0, 1.0, r);   // 1 at the center, 0 at the border
    float a = vAlpha * falloff;
    if (a < 0.01)
        discard;   // the transparent corners would add nothing

    // The blending (SRC_ALPHA, ONE) is set on the CPU: the result is frame + vColor * a, so
    // overlapping particles add up into a brighter glow.
    FragColor = vec4(vColor, a);
}
