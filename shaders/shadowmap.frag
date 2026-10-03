// shadowmap.frag
//
// Fragment shader of the SPOT shadow map pass, like 20_shadowmap.frag (lecture07a): it does
// nothing. The depth of each fragment, seen from the light, is written by the depth test into
// the depth texture of the FBO; there is no color to compute (the FBO has no color attachment,
// see ShadowMaps::init). Used together with shadowmap.vert.

#version 410 core

void main() {}
