// basic.frag
//
// Minimal fragment shader, used only by DebugDraw: every fragment gets one flat color from the
// CPU.

#version 410 core

out vec4 FragColor;

uniform vec3 baseColor;

void main() {
    FragColor = vec4(baseColor, 1.0);
}
