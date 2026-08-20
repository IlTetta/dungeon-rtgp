// shadowmap.vert
//
// Vertex shader per la passata di depth-only che genera una shadow map.
// Ricalca 19_shadowmap.vert (Davide Gadia, lecture07a): l'unica cosa che serve e'
// trasformare ogni vertice nello spazio della luce (lightSpaceMatrix = proiezione *
// vista della luce), esattamente come per una camera normale ma "seduti" sulla torcia.
//
// Differenza rispetto all'originale: il prof usa una proiezione ORTOGRAFICA (va bene per
// una luce direzionale, i cui raggi sono paralleli). Noi la costruiamo lato CPU con
// glm::perspective invece di glm::ortho (Renderer::renderShadowPass, in renderer.cpp),
// perche' una torcia e' una point light e i suoi raggi divergono da un punto - ma questo
// shader non lo sa nemmeno: gli arriva gia' una lightSpaceMatrix pronta, qualunque sia il
// tipo di proiezione con cui e' stata costruita.

#version 410 core

layout (location = 0) in vec3 aPos;
// Le altre location (Normal, TexCoords, Tangent, Bitangent) non servono per una depth-only
// pass: non calcoliamo colore, quindi non le dichiariamo nemmeno (esattamente come fa il
// prof in 19_shadowmap.vert).

uniform mat4 model;
uniform mat4 lightSpaceMatrix;

void main() {
    gl_Position = lightSpaceMatrix * model * vec4(aPos, 1.0);
}
