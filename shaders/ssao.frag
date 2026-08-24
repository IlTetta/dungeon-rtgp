// ssao.frag
//
// M2 (Lorenzo, problem #5/E1 "F2": M2_shadows_plan.md, SSAO - "contact shadow" near where
// props/walls meet the floor, independent of any light). Standard hemisphere-kernel SSAO
// (Crytek/LearnOpenGL technique - not from the professor's material, his course does not
// cover screen-space techniques): for every pixel, scatter samples() in a small hemisphere
// oriented along its normal, re-project each one back to screen space, and check whether the
// G-buffer's actual surface there is CLOSER to the camera than the sample - if so, something
// occludes that direction, darken the pixel a bit. Averaged over all samples this becomes a
// soft, distance-limited "how enclosed is this point" factor.
//
// Runs entirely in VIEW space (gPosition/gNormal from gbuffer.frag are already view-space):
// makes the re-projection a plain `projection * viewSpacePos` with no extra matrix needed.

#version 410 core

in vec2 TexCoords;
out float FragAO;

uniform sampler2D gPosition;   // view-space position (gbuffer.frag)
uniform sampler2D gNormal;     // view-space normal (gbuffer.frag)
uniform sampler2D texNoise;    // small tiled texture of random rotation vectors

#define KERNEL_SIZE 32
uniform vec3 samples[KERNEL_SIZE];   // hemisphere kernel, see Renderer::initSSAO

uniform mat4 projection;   // the SAME projection Renderer uses for the main color pass
uniform vec2 noiseScale;   // screenSize / noise texture size - tiles the 4x4 noise texture
uniform float radius;      // how far the sample hemisphere reaches, in view-space units
uniform float bias;        // minimum depth difference before something counts as "occluding"
uniform float strength;    // contrast of the final AO term (pow exponent)

void main() {
    vec3 fragPos = texture(gPosition, TexCoords).xyz;
    vec3 normal = normalize(texture(gNormal, TexCoords).xyz);
    vec3 randomVec = normalize(texture(texNoise, TexCoords * noiseScale).xyz);

    // Gram-Schmidt: build an orientation (TBN) that points the kernel's hemisphere along the
    // normal, randomly rotated per pixel (via randomVec) so the fixed 32-sample pattern does
    // not show up as a banding artifact - the blur pass afterwards cleans up the resulting
    // per-pixel noise.
    vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN = mat3(tangent, bitangent, normal);

    float occlusion = 0.0;
    for (int i = 0; i < KERNEL_SIZE; ++i) {
        vec3 samplePos = fragPos + (TBN * samples[i]) * radius;

        vec4 offset = projection * vec4(samplePos, 1.0);
        offset.xyz /= offset.w;
        offset.xyz = offset.xyz * 0.5 + 0.5;   // NDC [-1,1] -> texture [0,1]

        float sampleDepth = texture(gPosition, offset.xy).z;

        // a surface far outside the kernel radius should not count as an occluder just
        // because it happens to be along the same screen-space direction (avoids halos
        // around objects silhouetted against something distant)
        float rangeCheck = smoothstep(0.0, 1.0, radius / abs(fragPos.z - sampleDepth));
        occlusion += (sampleDepth >= samplePos.z + bias ? 1.0 : 0.0) * rangeCheck;
    }

    occlusion = 1.0 - (occlusion / float(KERNEL_SIZE));
    FragAO = pow(clamp(occlusion, 0.0, 1.0), strength);
}
