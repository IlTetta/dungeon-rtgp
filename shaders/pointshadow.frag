// pointshadow.frag
//
// Writes the world-space distance from the light to this fragment (not raw depth - a plain
// distance can be compared the same way regardless of which cube face it came from).

#version 410 core

in vec3 FragPos;

uniform vec3 lightPos;

out float FragDistance;

void main() {
    FragDistance = length(FragPos - lightPos);
}
