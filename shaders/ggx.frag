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
// but an array (up to MAX_SHADOW_LIGHTS), one per torch with Light::castsShadow == true,
// selected per light through lightShadowSlot[i] (see Renderer::render in renderer.cpp).

#version 410 core

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoords;

// M2: vertex position in each shadow caster's light space (from ggx.vert).
#define MAX_SHADOW_LIGHTS 3
in vec4 posLightSpace[MAX_SHADOW_LIGHTS];

out vec4 FragColor;

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
// light casts no shadow (then it stays "fully lit", like in M1). Set in renderer.cpp.
uniform int lightShadowSlot[MAX_LIGHTS];
uniform sampler2D shadowMaps[MAX_SHADOW_LIGHTS];

const float PI = 3.14159265359;

const float AMBIENT = 0.12;   // base fill light so unlit areas (corridors) are not pitch black

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

// M2 shadow test - PCF version, transcribed from Shadow_PCF_Final in 22_ggx_tex_shadow.frag.
// fragPosLightSpace and shadowMap are passed as parameters (not read from one global) because
// here we can have up to MAX_SHADOW_LIGHTS torches, each with its own map; N and L are the
// current light's, from the lighting loop in main().
float shadowPCF(vec4 fragPosLightSpace, sampler2D shadowMap, vec3 N, vec3 L) {
    // manual perspective divide (not automatic here, unlike gl_Position)
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;   // from [-1,1] to [0,1]

    float currentDepth = projCoords.z;

    // adaptive bias: more bias when the surface is grazing to the light (fights shadow acne),
    // range [0.005, 0.05] - same formula as the professor's.
    float bias = max(0.05 * (1.0 - dot(N, L)), 0.005);

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

/* --- reference/debug versions, same logic as shadowPCF() but without the filter ---
   (transcribed from Shadow_Acne / Shadow_Bias of the professor, kept here commented out:
   handy while debugging to see the raw acne before the bias, or the bias without the PCF)

float shadowAcne(vec4 fragPosLightSpace, sampler2D shadowMap) {
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;
    float closestDepth = texture(shadowMap, projCoords.xy).r;
    float currentDepth = projCoords.z;
    return currentDepth > closestDepth ? 1.0 : 0.0;
}

float shadowBias(vec4 fragPosLightSpace, sampler2D shadowMap, vec3 N, vec3 L) {
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;
    float closestDepth = texture(shadowMap, projCoords.xy).r;
    float currentDepth = projCoords.z;
    float bias = max(0.05 * (1.0 - dot(N, L)), 0.005);
    return currentDepth - bias > closestDepth ? 1.0 : 0.0;
}
*/

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
        if (slot >= 0 && slot < MAX_SHADOW_LIGHTS)
            shadow = shadowPCF(posLightSpace[slot], shadowMaps[slot], N, L);

        Lo += (1.0 - shadow) * (diffuse + specular) * radiance * NdotL;
    }

    vec3 ambient = AMBIENT * baseColor;
    FragColor = vec4(ambient + Lo, 1.0);
}
