#version 330 core

in vec2 vUv;
uniform sampler2D uNormal;
uniform sampler2D uDepth;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat4 uInverseProjection;
uniform vec3 uKernel[16];
uniform float uRadius;
uniform float uBias;
uniform float uStrength;
out float fragmentOcclusion;

vec3 reconstructViewPosition(vec2 uv, float depth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 view = uInverseProjection * clip;
    return view.xyz / max(view.w, 0.00001);
}

float hash(vec2 value) {
    return fract(sin(dot(value, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
    float centerDepth = texture(uDepth, vUv).r;
    if (centerDepth >= 0.999999) {
        fragmentOcclusion = 1.0;
        return;
    }
    vec3 viewPosition = reconstructViewPosition(vUv, centerDepth);
    vec3 worldNormal = normalize(texture(uNormal, vUv).rgb * 2.0 - 1.0);
    vec3 normal = normalize(mat3(uView) * worldNormal);
    float angle = hash(gl_FragCoord.xy) * 6.28318530718;
    vec3 randomVector = vec3(cos(angle), sin(angle), 0.0);
    vec3 tangent = normalize(randomVector - normal * dot(randomVector, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 tangentBasis = mat3(tangent, bitangent, normal);

    float occluded = 0.0;
    for (int index = 0; index < 16; ++index) {
        vec3 samplePosition = viewPosition + tangentBasis * uKernel[index] * uRadius;
        vec4 sampleClip = uProjection * vec4(samplePosition, 1.0);
        vec2 sampleUv = sampleClip.xy / max(sampleClip.w, 0.00001) * 0.5 + 0.5;
        if (any(lessThan(sampleUv, vec2(0.0))) || any(greaterThan(sampleUv, vec2(1.0)))) {
            continue;
        }
        float sampleDepth = texture(uDepth, sampleUv).r;
        if (sampleDepth >= 0.999999) continue;
        float sampleViewZ = reconstructViewPosition(sampleUv, sampleDepth).z;
        float rangeWeight = smoothstep(
            0.0, 1.0, uRadius / max(abs(viewPosition.z - sampleViewZ), 0.0001)
        );
        occluded += sampleViewZ >= samplePosition.z + uBias ? rangeWeight : 0.0;
    }
    fragmentOcclusion = pow(clamp(1.0 - occluded / 16.0, 0.0, 1.0), uStrength);
}
