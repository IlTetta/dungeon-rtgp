#version 410 core

// ============================================================================
// GGX / Cook-Torrance illumination model + shadow mapping (PCF), M2.
//
// La BRDF (F, D, G, diffuse) e' INVARIATA rispetto a M1 - fonte gia' documentata li':
// RTGP_09a_Illumination_Models_part2.pdf (Davide Gadia).
//
// La parte NUOVA e' l'ombra: la funzione shadowPCF() qui sotto e' la trascrizione di
// "Shadow_PCF_Final" da 22_ggx_tex_shadow.frag (Davide Gadia, lecture07a) - stesso bias
// adattivo (0.05*(1-N.L), clampato a 0.005), stesso kernel 3x3, stessa correzione per i
// punti oltre il far plane della luce (projCoords.z > 1.0 => niente ombra).
//
// Deviazioni rispetto all'originale, tutte necessarie per "torce multiple" invece che
// "una luce direzionale singola" (il prof stesso lo dice nel commento del suo file:
// "For more lights, of different kind, the shader must be modified to consider each
// case"):
//   1) il prof usa le SUBROUTINE per scegliere a runtime tra Shadow_Acne / Shadow_Bias /
//      Shadow_PCF_Final (utile per confrontare le tre tecniche a lezione). Qui andiamo
//      dritti alla versione PCF finale come funzione normale: le altre due sono comunque
//      lasciate commentate sotto per riferimento/debug, dato che sono la stessa identica
//      logica, solo senza bias o senza filtro.
//   2) la shadow map non e' una sola ma un array (fino a MAX_SHADOW_LIGHTS): ogni torcia
//      con Light::castsShadow=true ha la sua, selezionata per indice tramite
//      lightShadowSlot[i] (vedi Renderer::render in renderer.cpp).
// ============================================================================

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoords;

#define MAX_SHADOW_LIGHTS 3
in vec4 posLightSpace[MAX_SHADOW_LIGHTS];

out vec4 FragColor;

#define MAX_LIGHTS 8
uniform vec3  lightPositions[MAX_LIGHTS];
uniform vec3  lightColors[MAX_LIGHTS];
uniform float lightIntensities[MAX_LIGHTS];
uniform float lightRadii[MAX_LIGHTS];
uniform int   numLights;

// M2: per ogni luce, l'indice (0..MAX_SHADOW_LIGHTS-1) dello shadow map corrispondente,
// oppure -1 se quella luce non proietta ombra (resta illuminata "piena", come in M1).
uniform int lightShadowSlot[MAX_LIGHTS];
uniform sampler2D shadowMaps[MAX_SHADOW_LIGHTS];

uniform vec3 viewPos;

uniform vec3  baseColor;
uniform float roughness;
uniform vec3  F0;

const float PI = 3.14159265359;

vec3 fresnelSchlick(float cosTheta, vec3 f0) {
    float t = clamp(1.0 - cosTheta, 0.0, 1.0);
    return f0 + (vec3(1.0) - f0) * (t * t * t * t * t);
}

float distributionGGX(vec3 N, vec3 H, float alpha) {
    float alpha2 = alpha * alpha;
    float NdotH  = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = (NdotH2 * (alpha2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    return alpha2 / max(denom, 1e-6);
}

// Schlick-GGX method for geometry obstruction - stesso nome/stessa formula di G1() in
// 22_ggx_tex_shadow.frag.
float G1(float angle, float alpha) {
    float r = (alpha + 1.0);
    float k = (r * r) / 8.0;
    return angle / (angle * (1.0 - k) + k);
}

// ----------------------------------------------------------------------------
// Shadow test - versione PCF finale, trascritta da Shadow_PCF_Final in
// 22_ggx_tex_shadow.frag. "fragPosLightSpace" e "shadowMap" sono passati come parametri
// invece che presi da una singola variabile globale, perche' qui abbiamo fino a
// MAX_SHADOW_LIGHTS torce ognuna con la propria; N e L sono quelli della luce corrente
// nel ciclo di illuminazione qui sotto (main()).
// ----------------------------------------------------------------------------
float shadowPCF(vec4 fragPosLightSpace, sampler2D shadowMap, vec3 N, vec3 L) {
    // divisione prospettica manuale (qui non e' automatica come per gl_Position)
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    // da [-1,1] a [0,1]
    projCoords = projCoords * 0.5 + 0.5;

    float currentDepth = projCoords.z;

    // bias adattivo: stessa formula del prof, range [0.005, 0.05] in base all'angolo tra
    // normale e direzione della luce (piu' la superficie e' di taglio rispetto alla luce,
    // piu' bias serve per evitare l'acne)
    float bias = max(0.05 * (1.0 - dot(N, L)), 0.005);

    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(shadowMap, 0);
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float pcfDepth = texture(shadowMap, projCoords.xy + vec2(x, y) * texelSize).r;
            shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;
        }
    }
    shadow /= 9.0;

    // punti oltre il far plane della luce: mai in ombra (come nel prof)
    if (projCoords.z > 1.0)
        shadow = 0.0;

    return shadow;
}

/* --- versioni di riferimento/debug, stessa logica di shadowPCF() ma senza filtro ---
   (trascritte da Shadow_Acne/Shadow_Bias del prof, tenute qui commentate: utili se in
   fase di debug vuoi vedere l'acne "grezza" prima del bias, o il bias senza il PCF)

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
    vec3 N = normalize(Normal);
    vec3 V = normalize(viewPos - FragPos);
    float NdotV = max(dot(N, V), 0.0001);

    float alpha = clamp(roughness, 0.03, 1.0);

    vec3 Lo = vec3(0.0);

    for (int i = 0; i < numLights && i < MAX_LIGHTS; ++i) {
        vec3  toLight = lightPositions[i] - FragPos;
        float dist    = length(toLight);
        vec3  L       = toLight / max(dist, 1e-6);
        vec3  H       = normalize(V + L);

        float NdotL = max(dot(N, L), 0.0);
        if (NdotL <= 0.0) continue;

        // attenuazione: stessa scelta gia' documentata in M1 (falloff quadratico con
        // finestra morbida a lightRadii[i]) - non e' cambiata per M2.
        float distSq = dist * dist;
        float falloff = 1.0 / max(distSq, 1e-4);
        float radius = max(lightRadii[i], 1e-4);
        float window = clamp(1.0 - (distSq * distSq) / (radius * radius * radius * radius), 0.0, 1.0);
        falloff *= window * window;

        vec3 radiance = lightColors[i] * lightIntensities[i] * falloff;

        vec3  F = fresnelSchlick(max(dot(H, V), 0.0), F0);
        float D = distributionGGX(N, H, alpha);
        float G = G1(NdotV, alpha) * G1(NdotL, alpha);

        vec3 specular = (F * D * G) / (4.0 * NdotV * NdotL);

        vec3 kd = vec3(1.0) - F;
        vec3 diffuse = kd * baseColor / PI;

        // M2: se questa luce e' uno shadow-caster, peso il suo contributo con (1-shadow),
        // esattamente come "finalColor = (1.0-shadow)*(lambert+specular)*NdotL" del prof.
        // Le luci senza shadow map (lightShadowSlot[i] == -1) restano "sempre in luce",
        // come nel pattern multi-luce di lecture05 (che infatti non ha ombre).
        float shadow = 0.0;
        int slot = lightShadowSlot[i];
        if (slot >= 0 && slot < MAX_SHADOW_LIGHTS) {
            shadow = shadowPCF(posLightSpace[slot], shadowMaps[slot], N, L);
        }

        Lo += (1.0 - shadow) * (diffuse + specular) * radiance * NdotL;
    }

    vec3 ambient = vec3(0.03, 0.03, 0.035) * baseColor;
    vec3 color = ambient + Lo;

    FragColor = vec4(color, 1.0);
}
