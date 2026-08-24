// ssaoblur.frag
//
// ssao.frag's per-pixel random rotation leaves visible noise - a simple 4x4 box blur cleans
// it up into the soft, even darkening SSAO is supposed to look like.

#version 410 core

in vec2 TexCoords;
out float FragColor;

uniform sampler2D ssaoInput;

void main() {
    vec2 texelSize = 1.0 / vec2(textureSize(ssaoInput, 0));
    float result = 0.0;
    for (int x = -2; x < 2; ++x) {
        for (int y = -2; y < 2; ++y) {
            vec2 offset = vec2(float(x), float(y)) * texelSize;
            result += texture(ssaoInput, TexCoords + offset).r;
        }
    }
    FragColor = result / 16.0;
}
