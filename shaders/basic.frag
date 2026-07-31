#version 410 core

in vec3 fragWorldPos;
in vec3 fragNormal;

out vec4 FragColor;

// A simple diffuse (Lambert) lighting with several point lights (the torches).
// This is only a temporary shader to SEE the dungeon: Lorenzo will replace it with the real
// GGX + shadow mapping shader in M2.

const int MAX_LIGHTS = 32;

uniform int   numLights;
uniform vec3  lightPos[MAX_LIGHTS];
uniform vec3  lightColor[MAX_LIGHTS];
uniform float lightRadius[MAX_LIGHTS];
uniform float lightIntensity[MAX_LIGHTS];

uniform vec3 objectColor;   // base color of the surface (floor vs wall)

void main() {
    vec3 N = normalize(fragNormal);

    // a tiny ambient term so surfaces in the dark are not pure black
    vec3 result = objectColor * 0.08;

    for (int i = 0; i < numLights; i++) {
        // vector from the surface to the light
        vec3 toLight = lightPos[i] - fragWorldPos;
        float dist = length(toLight);
        vec3 L = toLight / max(dist, 0.0001);

        // Lambert diffuse term
        float diff = max(dot(N, L), 0.0);

        // distance attenuation: full at the light, fading to 0 at "radius"
        float att = clamp(1.0 - dist / lightRadius[i], 0.0, 1.0);
        att = att * att;

        result += objectColor * lightColor[i] * diff * lightIntensity[i] * att;
    }

    FragColor = vec4(result, 1.0);
}
