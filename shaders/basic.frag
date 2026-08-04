// basic.frag
//
// Fragment shader for the basic forward rendering.
// Every fragment of an object just gets painted with one flat color, sent from the CPU.

#version 410 core

out vec4 FragColor;

uniform vec3 baseColor;

void main() {
    FragColor = vec4(baseColor, 1.0);
}
