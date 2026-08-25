// shadowmap.frag
//
// Fragment shader per la creazione della shadow map. Identico nella sostanza a
// 20_shadowmap.frag (Davide Gadia, lecture07a): non fa nulla. La passata di shadow
// scrive nel depth buffer dedicato del FBO l'informazione di profondita' di ogni
// fragment dal punto di vista della luce; non calcola alcuna informazione di colore
// (l'FBO che usiamo, vedi Renderer::initShadowMaps, non ha nemmeno un color attachment).
//
// N.B. va usato insieme a "shadowmap.vert" come vertex shader.

#version 410 core

void main() {}
