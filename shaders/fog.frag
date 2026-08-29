// fog.frag
//
// Volumetric fog, ray marched: for each screen pixel, reconstruct the world-space point the
// main color pass drew there (from its depth buffer), then walk from the camera to that point
// in fixed steps, accumulating fog density and how much nearby torches light up each bit of
// fog along the way. Composited over the already-rendered scene color, so this is the last
// pass before the HUD.
//
// Kept deliberately simple: no shadow-map sampling inside the march (would give "god rays"
// but costs a lot more) - just density plus attenuated light color, integrated along the
// real ray per pixel rather than a flat screen tint.

#version 410 core

in vec2 TexCoords;
out vec4 FragColor;

uniform sampler2D sceneColor;
uniform sampler2D sceneDepth;

uniform mat4 invViewProj;
uniform vec3 viewPos;

#define MAX_FOG_LIGHTS 4
uniform int numFogLights;
uniform vec3 fogLightPositions[MAX_FOG_LIGHTS];
uniform vec3 fogLightColors[MAX_FOG_LIGHTS];
uniform float fogLightIntensities[MAX_FOG_LIGHTS];
uniform float fogLightRadii[MAX_FOG_LIGHTS];

uniform vec3 fogColor;
uniform float fogDensity;
uniform float fogScatter;
uniform float fogMaxDistance;
uniform int fogSteps;

// Undo the projection*view the main pass applied, using this pixel's depth, to get back the
// world-space position of whatever surface is there.
vec3 reconstructWorldPos(vec2 uv, float depth) {
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 worldPos = invViewProj * ndc;
    return worldPos.xyz / worldPos.w;
}

void main() {
    vec3 sceneCol = texture(sceneColor, TexCoords).rgb;
    float depth = texture(sceneDepth, TexCoords).r;

    vec3 surfacePos = reconstructWorldPos(TexCoords, depth);
    vec3 toSurface = surfacePos - viewPos;
    float rayLength = length(toSurface);
    vec3 rayDir = toSurface / max(rayLength, 0.0001);

    // march up to whichever is closer: the surface itself, or the fog's own max range
    float marchDistance = min(rayLength, fogMaxDistance);
    float stepSize = marchDistance / float(fogSteps);

    // a faint ambient tint so the fog is not pitch black in stretches with no torch nearby
    vec3 ambientFog = fogColor * 0.15;

    vec3 scatter = vec3(0.0);
    float transmittance = 1.0;

    for (int i = 0; i < fogSteps; ++i) {
        float t = (float(i) + 0.5) * stepSize;
        vec3 samplePos = viewPos + rayDir * t;

        float stepDensity = fogDensity * stepSize;
        float stepTransmittance = exp(-stepDensity);

        vec3 inscatter = ambientFog;
        for (int L = 0; L < numFogLights; ++L) {
            vec3 toLight = fogLightPositions[L] - samplePos;
            float dist = length(toLight);
            float atten = clamp(1.0 - dist / fogLightRadii[L], 0.0, 1.0);
            inscatter += fogLightColors[L] * fogLightIntensities[L] * atten * atten * fogScatter;
        }

        scatter += transmittance * (1.0 - stepTransmittance) * inscatter;
        transmittance *= stepTransmittance;

        if (transmittance < 0.01) break;   // fully opaque fog from here on: stop early
    }

    // Cheap Reinhard tonemap on the SCATTER term only (not the whole image): with several
    // bright torches nearby the accumulated in-scattered light can add up past 1.0 per
    // channel and clip to flat white. x/(x+1) caps it below 1.0 regardless of how much
    // energy piled up, while leaving sceneCol*transmittance (already a normal LDR color)
    // untouched, so this only kicks in where it is actually needed.
    scatter = scatter / (scatter + vec3(1.0));

    vec3 finalColor = sceneCol * transmittance + scatter;
    FragColor = vec4(finalColor, 1.0);
}
