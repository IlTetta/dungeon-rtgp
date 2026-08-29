// particle.vert
// Vertex shader for the instanced spark/ember particles (src/world/particles.h).
//
// Each particle is a tiny camera-facing quad ("billboard"). The base quad is 4 corners in
// [-0.5, 0.5] (location 0, per-vertex). The per-particle data (center/color/size/alpha) comes in
// as INSTANCED attributes (locations 1-3, one value per instance via glVertexAttribDivisor), so
// thousands of particles are drawn in a single glDrawArraysInstanced call.
//
// For the benchmark A/B there is also a NAIVE path (one draw call per particle): in that case the
// instance attributes are not used and the same values arrive as uniforms instead. uInstanced picks
// which source to read, so the two paths share one shader and only differ in the number of draw
// calls (exactly the cost instancing removes).

#version 410 core

layout (location = 0) in vec2 aCorner;      // quad corner, per-vertex, in [-0.5, 0.5]
layout (location = 1) in vec3 aCenter;      // per-instance: world position of the particle
layout (location = 2) in vec3 aColor;       // per-instance: color
layout (location = 3) in vec2 aSizeAlpha;   // per-instance: x = size (world units), y = alpha

uniform mat4 view;
uniform mat4 projection;

// naive path: the same per-particle values, but as uniforms (see uInstanced)
uniform bool uInstanced;
uniform vec3 uCenter;
uniform vec3 uColor;
uniform vec2 uSizeAlpha;

out vec2  vCorner;   // pass the corner to the fragment shader for the round falloff
out vec3  vColor;
out float vAlpha;

void main() {
    // pick per-instance data (instanced draw) or per-draw uniforms (naive draw)
    vec3 center = uInstanced ? aCenter    : uCenter;
    vec3 color  = uInstanced ? aColor     : uColor;
    vec2 sa     = uInstanced ? aSizeAlpha : uSizeAlpha;

    // Billboard in VIEW space: transform the center to view space, then offset by the quad corner
    // on the view X/Y axes. In view space the camera looks down -Z, so an X/Y offset always lies in
    // the plane facing the camera -> the quad faces the camera automatically, for any view matrix,
    // with no "right/up" uniforms needed.
    vec4 centerView = view * vec4(center, 1.0);
    centerView.xy += aCorner * sa.x;            // sa.x = size
    gl_Position = projection * centerView;

    vCorner = aCorner;
    vColor  = color;
    vAlpha  = sa.y;                              // sa.y = alpha
}
