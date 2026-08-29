// particle.frag
// Fragment shader for the spark/ember particles. Draws a soft ROUND dot on the quad (no texture:
// the shape is computed from the quad corner), and outputs it for ADDITIVE blending so overlapping
// sparks build up a warm glow.

#version 410 core

in vec2  vCorner;   // quad corner in [-0.5, 0.5]
in vec3  vColor;
in float vAlpha;

out vec4 FragColor;

void main() {
    // distance from the quad center: 0 at the center, 1 at the edge midpoints. This turns the
    // square quad into a soft circle.
    float r = length(vCorner) * 2.0;
    float falloff = 1.0 - smoothstep(0.0, 1.0, r);   // 1 in the middle, fading to 0 at the rim
    float a = vAlpha * falloff;
    if (a < 0.01) discard;                            // skip the fully transparent rim

    // Additive blend (SRC_ALPHA, ONE) is set on the CPU side, so it adds vColor * a to the frame:
    // we output the plain color and let the alpha drive how much it adds.
    FragColor = vec4(vColor, a);
}
