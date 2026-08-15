// ggx.frag
//
// Fragment shader implementing the Cook-Torrance "FDG" specular BRDF with the GGX
// (Trowbridge-Reitz) normal distribution, Schlick-GGX/Smith geometry term, and Schlick's
// Fresnel approximation, plus a Lambert diffuse term.


#version 410 core

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoords;

out vec4 FragColor;

// material
uniform sampler2D albedoMap;   // base color (diffuse albedo), read from a texture
uniform float uvScale;         // texture tiling: how many times it repeats across the UVs
uniform float roughness;       // "a" in the formulas above, in [0,1]: 0 = mirror, 1 = very rough
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

        Lo += (diffuse + specular) * radiance * NdotL;
    }

    vec3 ambient = AMBIENT * baseColor;
    FragColor = vec4(ambient + Lo, 1.0);
}
