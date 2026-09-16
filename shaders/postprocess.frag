#version 330 core
in vec2 vUv;
uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform sampler2D uMotion;
uniform sampler2D uDepth;
uniform sampler2D uEncodedNormal;
uniform int uTemporalDebugView;
uniform bool uToneMapping;
uniform bool uBloomEnabled;
uniform bool uEncodeSrgb;
uniform float uExposure;
uniform float uBloomIntensity;
uniform bool uOutlineEnabled;
uniform bool uOutlineNormalAvailable;
uniform float uOutlineWidth;
uniform float uOutlineDepthThreshold;
uniform float uOutlineNormalThreshold;
uniform vec3 uOutlineColor;
uniform float uInverseWidth;
uniform float uInverseHeight;
uniform mat4 uInverseProjection;
out vec4 fragmentColor;

vec3 aces(vec3 color) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);
}

vec3 linearToSrgb(vec3 color) {
    color = max(color, vec3(0.0));
    bvec3 cutoff = lessThanEqual(color, vec3(0.0031308));
    return mix(1.055 * pow(color, vec3(1.0 / 2.4)) - 0.055, color * 12.92, cutoff);
}

float viewDepth(vec2 uv, float deviceDepth) {
    if (deviceDepth >= 0.999999) return 1.0e20;
    vec4 viewPosition = uInverseProjection
        * vec4(uv * 2.0 - 1.0, deviceDepth * 2.0 - 1.0, 1.0);
    return abs(viewPosition.z / max(abs(viewPosition.w), 1.0e-6));
}

float outlineEdge() {
    float centerDeviceDepth = texture(uDepth, vUv).r;
    // Draw silhouettes inward on opaque geometry. This avoids a halo over the
    // sky and deliberately leaves non-depth-writing transparent surfaces alone.
    if (centerDeviceDepth >= 0.999999) return 0.0;

    float centerDepth = viewDepth(vUv, centerDeviceDepth);
    vec3 centerNormal = vec3(0.0, 0.0, 1.0);
    if (uOutlineNormalAvailable) {
        centerNormal = normalize(texture(uEncodedNormal, vUv).xyz * 2.0 - 1.0);
    }
    vec2 texel = vec2(uInverseWidth, uInverseHeight) * max(uOutlineWidth, 0.5);
    vec2 offsets[8] = vec2[8](
        vec2(-1.0,  0.0), vec2(1.0,  0.0),
        vec2( 0.0, -1.0), vec2(0.0,  1.0),
        vec2(-1.0, -1.0), vec2(1.0, -1.0),
        vec2(-1.0,  1.0), vec2(1.0,  1.0)
    );
    float maximumDepthDelta = 0.0;
    float maximumNormalDelta = 0.0;
    for (int index = 0; index < 8; ++index) {
        vec2 sampleUv = clamp(vUv + offsets[index] * texel, vec2(0.0), vec2(1.0));
        float sampleDeviceDepth = texture(uDepth, sampleUv).r;
        if (sampleDeviceDepth >= 0.999999) {
            maximumDepthDelta = 1.0;
            continue;
        }
        float sampleDepth = viewDepth(sampleUv, sampleDeviceDepth);
        float relativeDelta = abs(sampleDepth - centerDepth)
            / max(min(sampleDepth, centerDepth), 0.25);
        maximumDepthDelta = max(maximumDepthDelta, relativeDelta);
        if (uOutlineNormalAvailable) {
            vec3 sampleNormal = normalize(
                texture(uEncodedNormal, sampleUv).xyz * 2.0 - 1.0
            );
            maximumNormalDelta = max(
                maximumNormalDelta,
                1.0 - clamp(dot(centerNormal, sampleNormal), -1.0, 1.0)
            );
        }
    }
    float depthEdge = smoothstep(
        uOutlineDepthThreshold,
        uOutlineDepthThreshold * 2.0,
        maximumDepthDelta
    );
    float normalEdge = uOutlineNormalAvailable
        ? smoothstep(
            uOutlineNormalThreshold,
            uOutlineNormalThreshold * 1.5,
            maximumNormalDelta
        )
        : 0.0;
    return max(depthEdge, normalEdge);
}

void main() {
    if (uTemporalDebugView == 1) {
        vec2 motion = texture(uMotion, vUv).rg;
        fragmentColor = vec4(clamp(vec3(0.5 + motion.x * 24.0, 0.5 + motion.y * 24.0, 0.5), 0.0, 1.0), 1.0);
        return;
    }
    if (uTemporalDebugView == 2) {
        float historyWeight = texture(uScene, vUv).a;
        fragmentColor = vec4(vec3(historyWeight), 1.0);
        return;
    }
    vec3 color = texture(uScene, vUv).rgb;
    if (uBloomEnabled) color += texture(uBloom, vUv).rgb * uBloomIntensity;
    color *= max(uExposure, 0.0);
    color = uToneMapping ? aces(color) : clamp(color, 0.0, 1.0);
    color = uEncodeSrgb ? linearToSrgb(color) : color;
    if (uOutlineEnabled) {
        color = mix(color, max(uOutlineColor, vec3(0.0)), outlineEdge());
    }
    fragmentColor = vec4(color, 1.0);
}
