// particle.vert
// Vertex shader of the fire particles (src/world/particles.h).
//
// Every particle is a small quad that always faces the camera (a billboard). The base quad is
// 4 corners in [-0.5, 0.5] (location 0, per vertex). The data of each particle (center, color,
// size, alpha) comes as INSTANCED attributes (locations 1..3, one value per instance), so all
// the particles are drawn by one glDrawArraysInstanced call.
//
// For the benchmark there is also the naive path (one draw call per particle): then the same
// values come as uniforms. uInstanced chooses where to read them, so both paths use the same
// shader and differ only in the number of draw calls.

#version 410 core

layout (location = 0) in vec2 aCorner;  // per vertex: quad corner in [-0.5, 0.5]
layout (location = 1) in vec3 aCenter;  // per instance: world position
layout (location = 2) in vec3 aColor;   // per instance: color
layout (location = 3) in vec2 aSizeAlpha;   // per instance: x = size (world units), y = alpha

uniform mat4 view;
uniform mat4 projection;

// naive path: the same values as uniforms
uniform bool uInstanced;
uniform vec3 uCenter;
uniform vec3 uColor;
uniform vec2 uSizeAlpha;

out vec2 vCorner;   // for the round shape in the fragment shader
out vec3 vColor;
out float vAlpha;

void main() {
    vec3 center = uInstanced ? aCenter : uCenter;
    vec3 color = uInstanced ? aColor : uColor;
    vec2 sa = uInstanced ? aSizeAlpha : uSizeAlpha;

    // Billboard in VIEW space: we move the center to view space and then add the corner offset on
    // the view X and Y axes. In view space the camera looks down -Z, so the X/Y plane always faces
    // it: the quad faces the camera for any view.
    vec4 centerView = view * vec4(center, 1.0);
    centerView.xy += aCorner * sa.x;   // sa.x = size
    gl_Position = projection * centerView;

    vCorner = aCorner;
    vColor = color;
    vAlpha = sa.y;   // sa.y = alpha
}
