// ggx.frag
//
// Fragment shader implementing the Cook-Torrance "FDG" specular BRDF with the GGX
// (Trowbridge-Reitz) normal distribution, Schlick-GGX/Smith geometry term, and Schlick's
// Fresnel approximation, plus a Lambert diffuse term. The albedo comes from a per-object
// texture (the material system in src/world/ / src/core/scene.h).
//
// M2 shadow mapping (Lorenzo): shadowPCF() below is transcribed from "Shadow_PCF_Final" in
// 22_ggx_tex_shadow.frag (Davide Gadia, lecture07a) - same adaptive bias (0.05*(1-N.L),
// clamped to 0.005), same 3x3 kernel, same "beyond the light far plane => no shadow" fix.
// Difference from the professor's single directional light: here the shadow map is not one
// but an array (up to MAX_SHADOW_LIGHTS), one per torch/brazier with Light::castsShadow ==
// true, selected per light through lightShadowSlot[i] (see Renderer::renderInternal).
//
// M2 shadow REWORK (see M2_shadows_plan.md): two problems this fixes vs. the first version.
//   - #6 "point light without 360 degree shadow": a brazier is not aimed anywhere, a single
//     cone shadow map cannot cover it. Lights with Light::type == LIGHT_POINT now get a real
//     cubemap shadow (shadowPoint() below) instead of the cone test; LIGHT_SPOT keeps the
//     cone (shadowPCF()). lightShadowIsPoint[i] tells the shader which one to use for light i.
//   - #2 "shadow on nothing" (self-shadowing acne near the light): the light-space projection
//     used to be computed per-VERTEX in ggx.vert and interpolated; it is now computed here,
//     per-FRAGMENT, from FragPos offset a little along the real per-fragment normal
//     (NORMAL_OFFSET) before projecting - the standard "normal offset bias" companion to the
//     depth bias below. The depth pass itself (Renderer::renderShadowPass /
//     renderPointShadowPass) also now culls FRONT faces (draws only back faces), which pushes
//     the recorded depth behind the light-facing surface instead of on top of it.

#version 410 core

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoords;

out vec4 FragColor;

// M2 (Lorenzo, problem #5/E1): the blurred SSAO result (see ssao.frag/ssaoblur.frag),
// sampled by screen position - one extra full-screen texture, independent of any light.
// ssaoOn is the tuning-panel A/B toggle: the CPU still renders the SSAO pass either way (see
// Renderer::renderInternal), this just decides whether the result is actually used.
uniform sampler2D ssaoMap;
uniform vec2 screenSize;
uniform bool ssaoOn;

// material
uniform sampler2D albedoMap;   // base color (diffuse albedo), read from a texture
uniform float uvScale;         // texture tiling: how many times it repeats across the UVs
uniform float roughness;       // "a" in the formulas below, in [0,1]: 0 = mirror, 1 = very rough
uniform vec3 F0;               // Fresnel reflectance at normal incidence (0 degrees)

uniform vec3 viewPos;   // world-space camera position, needed to build V

// mirrors Scene::lights (src/core/scene.h) as plain arrays; MAX_LIGHTS must match the
// constant with the same name in renderer.h.
#define MAX_LIGHTS 32

uniform int numLights;
uniform vec3 lightPositions[MAX_LIGHTS];
uniform vec3 lightColors[MAX_LIGHTS];
uniform float lightIntensities[MAX_LIGHTS];
uniform float lightRadii[MAX_LIGHTS];

// M2: for each light, the index (0..MAX_SHADOW_LIGHTS-1) of its shadow map, or -1 if this
// light casts no shadow this frame (then it stays "fully lit", like in M1). Set in
// renderer.cpp, together with which KIND of shadow map that slot holds this frame.
#define MAX_SHADOW_LIGHTS 4
uniform int lightShadowSlot[MAX_LIGHTS];
uniform bool lightShadowIsPoint[MAX_LIGHTS];   // true = read pointShadowMaps[slot] (cubemap)
                                                // false = read shadowMaps[slot] (2D cone)
// S3 (M2_shadows_plan.md, problem #3 "pop-in"): how much of this light's shadow to actually
// apply, 0..1 - ramped in renderer.cpp instead of snapping to a full shadow the instant a
// light is (re)assigned a slot. 0 = fully lit (as if unshadowed), 1 = the real shadow test.
uniform float lightShadowWeight[MAX_LIGHTS];

uniform mat4 lightSpaceMatrices[MAX_SHADOW_LIGHTS];   // SPOT slots only (proj * view of the light)
uniform sampler2D shadowMaps[MAX_SHADOW_LIGHTS];       // SPOT: 2D depth maps
uniform samplerCube pointShadowMaps[MAX_SHADOW_LIGHTS]; // POINT: cubemaps storing world distance

const float PI = 3.14159265359;

// M2 (Lorenzo): live-tunable shading/shadow constants - these used to be `const float` here,
// now they are uniforms so main.cpp's ImGui "Shadow tuning" panel can drive them live (see
// Renderer::ShadingTuning in renderer.h). Defaults below match the values that were
// hardcoded before, so nothing changes until someone moves a slider.
uniform float ambient;            // base fill light so unlit areas are not pitch black
uniform float spotBiasMax;        // SPOT shadow depth bias at grazing angles
uniform float spotBiasMin;        // SPOT shadow depth bias at normal incidence
uniform float spotNormalOffset;   // SPOT: how far to push the sample point along N (S2)
uniform float pointBiasScale;     // POINT shadow distance bias at grazing angles (x lightRadius)
uniform float pointBiasMinScale;  // POINT shadow distance bias at normal incidence (x lightRadius)
uniform float pointNormalOffset;  // POINT: how far to push the sample point along N (S2)
uniform float pointPCFRadius;     // S4: POINT shadow softness (0 = hard single-tap)

// S4 (M2_shadows_plan.md, problems #4/#8): 20 sample directions spread roughly evenly around
// a point (the corners/edge-midpoints/face-midpoints of a cube), used to soften the POINT
// shadow test below by averaging several nearby directions instead of just one - the cubemap
// equivalent of shadowPCF()'s 3x3 kernel. Same technique/table as the well-known LearnOpenGL
// point-shadow PCF (not from the professor's material - his course has not covered point
// light shadows at all yet, see the M2_shadows_plan.md note on this).
const vec3 pointPCFOffsets[20] = vec3[](
    vec3( 1,  1,  1), vec3( 1, -1,  1), vec3(-1, -1,  1), vec3(-1,  1,  1),
    vec3( 1,  1, -1), vec3( 1, -1, -1), vec3(-1, -1, -1), vec3(-1,  1, -1),
    vec3( 1,  1,  0), vec3( 1, -1,  0), vec3(-1, -1,  0), vec3(-1,  1,  0),
    vec3( 1,  0,  1), vec3(-1,  0,  1), vec3( 1,  0, -1), vec3(-1,  0, -1),
    vec3( 0,  1,  1), vec3( 0, -1,  1), vec3( 0,  1, -1), vec3( 0, -1, -1)
);

// D: GGX / Trowbridge-Reitz normal distribution function
float distributionGGX(vec3 N, vec3 H, float a) {
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    return a2 / max(denom, 0.0000001);
}

// G1: Schlick-GGX single-term geometry (masking OR shadowing, for one direction at a time)
float geometrySchlickGGX(float NdotX, float k) {
    return NdotX / (NdotX * (1.0 - k) + k);
}

// G2 = G1(n,v) * G1(n,l): Smith's method, combining masking (view) and shadowing (light)
float geometrySmith(float NdotV, float NdotL, float a) {
    // k for direct (analytic) lights, as opposed to the slightly different k used for
    // image-based lighting; we only have direct point lights (torches) for now.
    float k = (a + 1.0) * (a + 1.0) / 8.0;
    float ggxV = geometrySchlickGGX(NdotV, k);
    float ggxL = geometrySchlickGGX(NdotL, k);
    return ggxV * ggxL;
}

// F: Schlick's approximation of the Fresnel reflectance
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// M2 SPOT shadow test - PCF, transcribed from Shadow_PCF_Final in 22_ggx_tex_shadow.frag,
// with the normal-offset applied before the projection (see file header, problem #2/S2).
float shadowPCF(mat4 lightSpaceMatrix, sampler2D shadowMap, vec3 N, vec3 L) {
    vec4 fragPosLightSpace = lightSpaceMatrix * vec4(FragPos + N * spotNormalOffset, 1.0);

    // manual perspective divide (not automatic here, unlike gl_Position)
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;   // from [-1,1] to [0,1]

    float currentDepth = projCoords.z;

    // adaptive bias: more bias when the surface is grazing to the light (fights shadow acne) -
    // same formula shape as the professor's (there hardcoded [0.005, 0.05]), now tunable.
    // Complements spotNormalOffset above rather than replacing it: the normal offset fixes
    // acne on sloped surfaces, this depth bias fixes the remaining flat-surface precision error.
    float bias = max(spotBiasMax * (1.0 - dot(N, L)), spotBiasMin);

    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(shadowMap, 0);
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float pcfDepth = texture(shadowMap, projCoords.xy + vec2(x, y) * texelSize).r;
            shadow += (currentDepth - bias > pcfDepth) ? 1.0 : 0.0;
        }
    }
    shadow /= 9.0;

    // points beyond the light's far plane are never in shadow (same as the professor's)
    if (projCoords.z > 1.0)
        shadow = 0.0;

    return shadow;
}

// M2 POINT shadow test (problem #6/S1): the cubemap stores, per texel, the world-space
// distance from the light to the closest occluder in that direction (written by
// pointshadow.frag - see Renderer::renderPointShadowPass). We compare that against the real
// distance from the light to this fragment: farther => something was in the way => in shadow.
// F1 scope is correctness, not softness (PCF/soft cubemap sampling is S4, F2) - this is a
// single-tap test with an adaptive bias, the cubemap equivalent of "Shadow_Bias" rather than
// "Shadow_PCF_Final". Inlined in main() below (needs lightPositions[i]/lightRadii[i], simpler
// to read there than threading five parameters through a helper).

void main() {
    // read the base color from the texture (tiled by uvScale)
    vec3 baseColor = texture(albedoMap, TexCoords * uvScale).rgb;

    vec3 N = normalize(Normal);
    vec3 V = normalize(viewPos - FragPos);
    float NdotV = max(dot(N, V), 0.0001);   // avoid a divide by 0 below

    vec3 Lo = vec3(0.0);   // outgoing radiance accumulated from every light

    for (int i = 0; i < numLights; ++i) {
        vec3 toLight = lightPositions[i] - FragPos;
        float dist = length(toLight);
        vec3 L = toLight / max(dist, 0.0001);
        vec3 H = normalize(V + L);   // half-vector between view and light directions

        float NdotL = max(dot(N, L), 0.0);
        if (NdotL <= 0.0) continue;   // surface faces away from this light: no contribution

        // --- Cook-Torrance specular term: F * D * G2 / (4 * NdotV * NdotL) ---
        float D = distributionGGX(N, H, roughness);
        float G = geometrySmith(NdotV, NdotL, roughness);
        vec3  F = fresnelSchlick(max(dot(V, H), 0.0), F0);

        vec3 specular = (F * D * G) / max(4.0 * NdotV * NdotL, 0.0001);

        // Energy conservation: whatever fraction of light is reflected specularly (F)
        // cannot also be reflected diffusely, so the diffuse term is scaled by (1 - F).
        vec3 kD = vec3(1.0) - F;
        vec3 diffuse = kD * baseColor / PI;

        // same simple linear falloff we used before switching to GGX: full intensity at
        // the light, fading to 0 at "radius" (a placeholder until we need something more
        // physical, e.g. for the fog/optimization experiments)
        float atten = clamp(1.0 - dist / lightRadii[i], 0.0, 1.0);
        vec3 radiance = lightColors[i] * lightIntensities[i] * atten;

        // M2: if this light is a shadow caster, weight its contribution by (1 - shadow),
        // exactly like "finalColor = (1.0-shadow)*(lambert+specular)*NdotL" in the professor's
        // code. Lights with no shadow map (lightShadowSlot[i] == -1) stay fully lit.
        float shadow = 0.0;
        int slot = lightShadowSlot[i];
        if (slot >= 0 && slot < MAX_SHADOW_LIGHTS) {
            if (lightShadowIsPoint[i]) {
                // POINT (S4): compare distance-to-light against the cubemap's recorded
                // distance, averaged over pointPCFOffsets nearby directions instead of a
                // single tap - softens the shadow edge the same way shadowPCF()'s 3x3 kernel
                // does for SPOT. Both the sample point and each tap direction share the same
                // normal-offset (see file header) to fight acne.
                vec3 offsetFragPos = FragPos + N * pointNormalOffset;
                vec3 fragToLight = offsetFragPos - lightPositions[i];
                float currentDist = length(fragToLight);
                float bias = max(pointBiasScale * lightRadii[i] * (1.0 - NdotL), pointBiasMinScale * lightRadii[i]);
                float diskRadius = (1.0 + currentDist / max(lightRadii[i], 0.001)) * pointPCFRadius;

                for (int s = 0; s < 20; ++s) {
                    float closestDist = texture(pointShadowMaps[slot], fragToLight + pointPCFOffsets[s] * diskRadius).r;
                    shadow += (currentDist - bias > closestDist) ? 1.0 : 0.0;
                }
                shadow /= 20.0;
            } else {
                shadow = shadowPCF(lightSpaceMatrices[slot], shadowMaps[slot], N, L);
            }
            // S3: fade the shadow contribution in/out instead of snapping (see file header).
            shadow *= lightShadowWeight[i];
        }

        Lo += (1.0 - shadow) * (diffuse + specular) * radiance * NdotL;
    }

    // M2 (E1): SSAO only darkens the ambient/indirect term, not the direct light from torches
    // - that is the standard scope of screen-space AO (it approximates missing indirect
    // bounce light near contact points, not direct occlusion, which the shadow maps already
    // handle for their own lights).
    float ao = ssaoOn ? texture(ssaoMap, gl_FragCoord.xy / screenSize).r : 1.0;
    vec3 ambientColor = ambient * baseColor * ao;
    FragColor = vec4(ambientColor + Lo, 1.0);
}
