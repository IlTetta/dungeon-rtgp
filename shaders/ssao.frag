// ssao.frag
//
// Hemisphere-kernel screen-space ambient occlusion: for every pixel, scatter samples in a
// small hemisphere oriented along its normal, re-project each one to screen space, and check
// whether the G-buffer's actual surface there is closer to the camera than the sample: if
// so something occludes that direction. Averaged over all samples this becomes a soft
// "how enclosed is this point" factor. Runs in view space, so re-projection is just
// `projection * viewSpacePos`.

#version 410 core

in vec2 TexCoords;
out float FragAO;

uniform sampler2D gPosition;
uniform sampler2D gNormal;
uniform sampler2D texNoise;

#define KERNEL_SIZE 32
uniform vec3 samples[KERNEL_SIZE];

uniform mat4 projection;
uniform vec2 noiseScale;   // screenSize / noise texture size, tiles the 4x4 noise texture
uniform float radius;      // how far the sample hemisphere reaches, in view-space units
uniform float bias;        // minimum depth difference before something counts as occluding
uniform float strength;    // contrast of the final AO term

void main() {
    vec3 fragPos = texture(gPosition, TexCoords).xyz;
    vec3 normal = normalize(texture(gNormal, TexCoords).xyz);
    vec3 randomVec = normalize(texture(texNoise, TexCoords * noiseScale).xyz);

    // orient the kernel along the normal, randomly rotated per pixel so the fixed sample
    // pattern doesn't show up as banding (the blur pass cleans up the resulting noise)
    vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN = mat3(tangent, bitangent, normal);

    float occlusion = 0.0;
    for (int i = 0; i < KERNEL_SIZE; ++i) {
        vec3 samplePos = fragPos + (TBN * samples[i]) * radius;

        vec4 offset = projection * vec4(samplePos, 1.0);
        offset.xyz /= offset.w;
        offset.xyz = offset.xyz * 0.5 + 0.5;

        float sampleDepth = texture(gPosition, offset.xy).z;

        // don't count distant surfaces along the same screen direction as occluders
        float rangeCheck = smoothstep(0.0, 1.0, radius / abs(fragPos.z - sampleDepth));
        occlusion += (sampleDepth >= samplePos.z + bias ? 1.0 : 0.0) * rangeCheck;
    }

    occlusion = 1.0 - (occlusion / float(KERNEL_SIZE));
    FragAO = pow(clamp(occlusion, 0.0, 1.0), strength);
}
