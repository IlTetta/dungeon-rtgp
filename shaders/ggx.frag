// ggx.frag
//
// Cook-Torrance BRDF: GGX distribution, Schlick-GGX geometry term, Schlick Fresnel, plus
// Lambert diffuse. Albedo comes from a per-object texture. Shadows: SPOT lights use a 2D
// depth map + PCF (shadowPCF, close to the classic single-light shadow-mapping recipe);
// POINT lights use a cubemap of world-space distances instead, since a cone can't cover an
// omnidirectional light. SSAO darkens the ambient term near contact points/corners.

#version 410 core

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoords;

out vec4 FragColor;

uniform sampler2D ssaoMap;
uniform vec2 screenSize;
uniform bool ssaoOn;

// material
uniform sampler2D albedoMap;   // base color (diffuse albedo), read from a texture
uniform float uvScale;         // texture tiling: how many times it repeats across the UVs
uniform float roughness;       // "a" in the formulas below, in [0,1]: 0 = mirror, 1 = very rough
uniform vec3 F0;               // Fresnel reflectance at normal incidence (0 degrees)

uniform vec3 viewPos;   // world-space camera position, needed to build V

#define MAX_LIGHTS 32

uniform int numLights;
uniform vec3 lightPositions[MAX_LIGHTS];
uniform vec3 lightColors[MAX_LIGHTS];
uniform float lightIntensities[MAX_LIGHTS];
uniform float lightRadii[MAX_LIGHTS];

// per-light shadow slot: -1 = no shadow (fully lit), otherwise which shadow map/cubemap and
// whether it's a SPOT (2D) or POINT (cubemap) one.
// Shadow casters are split by type: SPOT (torch) 2D maps and POINT (brazier) cubemaps have
// separate budgets (must match ShadowMaps::MAX_SPOT / MAX_POINT in shadow_maps.h). lightShadowSlot
// is the slot WITHIN the light's own type's array; lightShadowIsPoint says which array to read.
#define MAX_SPOT_SHADOWS 8
#define MAX_POINT_SHADOWS 2
uniform int lightShadowSlot[MAX_LIGHTS];
uniform bool lightShadowIsPoint[MAX_LIGHTS];
// fade weight (0..1) so a shadow ramps out over a fraction of a second instead of snapping the
// instant a light loses its slot.
uniform float lightShadowWeight[MAX_LIGHTS];

uniform mat4 lightSpaceMatrices[MAX_SPOT_SHADOWS];      // SPOT slots (proj * view of the light)
uniform sampler2D shadowMaps[MAX_SPOT_SHADOWS];        // SPOT: 2D depth maps
uniform samplerCube pointShadowMaps[MAX_POINT_SHADOWS]; // POINT: cubemaps storing world distance

const float PI = 3.14159265359;

// Tunable from the ImGui panel (Renderer::ShadingTuning) instead of hardcoded, so we can
// dial them in without recompiling.
uniform float ambient;
uniform float spotBiasMax;
uniform float spotBiasMin;
uniform float spotNormalOffset;
uniform float pointBiasScale;
uniform float pointBiasMinScale;
uniform float pointNormalOffset;
uniform float pointPCFRadius;

// 20 sample directions roughly evenly spread around a point, used to soften the POINT
// shadow test by averaging nearby directions instead of a single tap.
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
    float k = (a + 1.0) * (a + 1.0) / 8.0;   // k for direct/analytic lights
    float ggxV = geometrySchlickGGX(NdotV, k);
    float ggxL = geometrySchlickGGX(NdotL, k);
    return ggxV * ggxL;
}

// F: Schlick's approximation of the Fresnel reflectance
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// SPOT shadow: 3x3 PCF with an adaptive bias, and a normal-offset applied to the sample
// point before projecting (fights acne on sloped surfaces; the bias handles flat ones).
float shadowPCF(mat4 lightSpaceMatrix, sampler2D shadowMap, vec3 N, vec3 L) {
    vec4 fragPosLightSpace = lightSpaceMatrix * vec4(FragPos + N * spotNormalOffset, 1.0);

    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;   // manual perspective divide
    projCoords = projCoords * 0.5 + 0.5;   // [-1,1] -> [0,1]

    float currentDepth = projCoords.z;
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

    if (projCoords.z > 1.0)   // beyond the light's far plane: never in shadow
        shadow = 0.0;

    return shadow;
}

void main() {
    vec3 baseColor = texture(albedoMap, TexCoords * uvScale).rgb;

    vec3 N = normalize(Normal);
    vec3 V = normalize(viewPos - FragPos);
    float NdotV = max(dot(N, V), 0.0001);

    vec3 Lo = vec3(0.0);   // outgoing radiance accumulated from every light

    for (int i = 0; i < numLights; ++i) {
        vec3 toLight = lightPositions[i] - FragPos;
        float dist = length(toLight);
        vec3 L = toLight / max(dist, 0.0001);
        vec3 H = normalize(V + L);

        float NdotL = max(dot(N, L), 0.0);
        if (NdotL <= 0.0) continue;   // surface faces away from this light

        float D = distributionGGX(N, H, roughness);
        float G = geometrySmith(NdotV, NdotL, roughness);
        vec3  F = fresnelSchlick(max(dot(V, H), 0.0), F0);
        vec3 specular = (F * D * G) / max(4.0 * NdotV * NdotL, 0.0001);

        // energy conservation: what's reflected specularly can't also be diffuse
        vec3 kD = vec3(1.0) - F;
        vec3 diffuse = kD * baseColor / PI;

        float atten = clamp(1.0 - dist / lightRadii[i], 0.0, 1.0);
        vec3 radiance = lightColors[i] * lightIntensities[i] * atten;

        float shadow = 0.0;
        int slot = lightShadowSlot[i];   // index within this light's own type's shadow array
        if (slot >= 0) {
            if (lightShadowIsPoint[i] && slot < MAX_POINT_SHADOWS) {
                // distance-based cubemap test, PCF-softened over the 20 offsets above
                vec3 offsetFragPos = FragPos + N * pointNormalOffset;
                vec3 fragToLight = offsetFragPos - lightPositions[i];
                float currentDist = length(fragToLight);
                // Plain world-unit bias, NOT scaled by lightRadii[i]: a brazier reaches 20 world
                // units, and a scaled bias was bigger than the props next to it, so their contact
                // shadows disappeared.
                float bias = max(pointBiasScale * (1.0 - NdotL), pointBiasMinScale);
                float diskRadius = (1.0 + currentDist / max(lightRadii[i], 0.001)) * pointPCFRadius;

                for (int s = 0; s < 20; ++s) {
                    float closestDist = texture(pointShadowMaps[slot], fragToLight + pointPCFOffsets[s] * diskRadius).r;
                    shadow += (currentDist - bias > closestDist) ? 1.0 : 0.0;
                }
                shadow /= 20.0;
            } else if (!lightShadowIsPoint[i] && slot < MAX_SPOT_SHADOWS) {
                shadow = shadowPCF(lightSpaceMatrices[slot], shadowMaps[slot], N, L);
            }
            shadow *= lightShadowWeight[i];   // fade out instead of snapping
        }

        Lo += (1.0 - shadow) * (diffuse + specular) * radiance * NdotL;
    }

    // SSAO only darkens ambient/indirect light, not the direct contribution above (shadow
    // maps already handle direct occlusion for their own lights).
    float ao = ssaoOn ? texture(ssaoMap, gl_FragCoord.xy / screenSize).r : 1.0;
    vec3 ambientColor = ambient * baseColor * ao;
    FragColor = vec4(ambientColor + Lo, 1.0);
}
