// ggx.vert
//
// M2: rispetto alla versione M1, aggiunge il calcolo delle coordinate di ogni vertice
// nello "spazio della luce" per ciascuna torcia che proietta ombre (fino a
// MAX_SHADOW_LIGHTS), ricalcando "posLightSpace" di 21_ggx_tex_shadow.vert (Davide
// Gadia, lecture07a): "for the correct rendering of the shadows, we need to calculate
// the vertex coordinates also in light coordinates (= using light as a camera)".
//
// Differenza dall'originale del prof: li' c'era UNA sola luce (direzionale) quindi UNA
// sola posLightSpace/lightSpaceMatrix. Noi ne teniamo fino a MAX_SHADOW_LIGHTS (array),
// una per ogni torcia scelta come shadow-caster (vedi Light::castsShadow in scene.h).
// Il resto (calcolo di posizione/normale/UV mondo) e' lo stesso della versione M1: qui
// lavoriamo in WORLD space, non VIEW space come lecture07a — scelta gia' fatta e
// documentata in M1, la logica di shadow test non dipende da quale spazio scegliamo,
// solo che sia coerente tra vertex e fragment shader.

#version 410 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
layout (location = 3) in vec3 aTangent;
layout (location = 4) in vec3 aBitangent;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

// M2: una lightSpaceMatrix per ogni torcia shadow-caster attiva in questo frame.
#define MAX_SHADOW_LIGHTS 3
uniform mat4 lightSpaceMatrices[MAX_SHADOW_LIGHTS];
uniform int numShadowLights;   // quante di lightSpaceMatrices[] sono davvero in uso (<= MAX_SHADOW_LIGHTS)

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoords;

// M2: posizione del vertice in "coordinate della luce", una per ogni shadow-caster.
out vec4 posLightSpace[MAX_SHADOW_LIGHTS];

void main() {
    vec4 worldPos = model * vec4(aPos, 1.0);
    FragPos = worldPos.xyz;

    // Vera normal matrix (world space): serve transpose(inverse(mat3(model))), non
    // mat3(model) da solo - la nota "valido finche' non introduciamo scale non uniformi"
    // che c'era qui in M1 non regge piu': world/dungeon_geometry.h scala ogni muro e ogni
    // pavimento in modo non uniforme (glm::scale(m, box.size) con box.size = (tileSize,
    // wallHeight, tileSize)), quindi mat3(model) da solo distorceva gia' le normali nel
    // dungeon vero. Il prof calcola l'equivalente lato CPU per ogni oggetto,
    // inverseTranspose(mat3(view * model)) (lecture07a.cpp) - qui e' per-vertice in
    // shader, senza aggiungere una uniform dedicata in renderer.cpp: costa una inverse()
    // in piu' a vertice, trascurabile alla scala di questa scena.
    mat3 normalMatrix = transpose(inverse(mat3(model)));
    Normal = normalize(normalMatrix * aNormal);
    TexCoords = aTexCoords;

    // M2: proiettiamo lo stesso worldPos nello spazio di ciascuna luce-ombra, cosi' il
    // fragment shader potra' fare il confronto di profondita' con la shadow map
    // corrispondente (esattamente "posLightSpace = lightSpaceMatrix * mPosition" del
    // prof, ripetuto per ogni torcia).
    for (int i = 0; i < MAX_SHADOW_LIGHTS; ++i) {
        if (i < numShadowLights) {
            posLightSpace[i] = lightSpaceMatrices[i] * worldPos;
        } else {
            posLightSpace[i] = vec4(0.0);   // slot inutilizzato in questo frame
        }
    }

    gl_Position = projection * view * worldPos;
}
