#version 330 core

in vec2 vUv;

uniform sampler2D uCurrentColor;
uniform sampler2D uCurrentDepth;
uniform sampler2D uHistoryColor;
uniform sampler2D uHistoryDepth;
uniform sampler2D uObjectMotion;
uniform bool uObjectMotionAvailable;
uniform mat4 uInverseCurrentViewProjection;
uniform mat4 uPreviousViewProjection;
uniform bool uHistoryValid;
uniform float uHistoryWeight;

layout(location = 0) out vec4 resolvedColor;
layout(location = 1) out float resolvedDepth;
layout(location = 2) out vec2 resolvedMotion;

void main() {
    float currentDepth = texture(uCurrentDepth, vUv).r;
    vec4 clip = vec4(vUv * 2.0 - 1.0, currentDepth * 2.0 - 1.0, 1.0);
    vec4 world = uInverseCurrentViewProjection * clip;
    world /= max(abs(world.w), 0.00001);
    vec4 previousClip = uPreviousViewProjection * vec4(world.xyz, 1.0);
    vec3 previousNdc = previousClip.xyz / max(abs(previousClip.w), 0.00001);
    vec2 previousUv = previousNdc.xy * 0.5 + 0.5;
    vec4 objectMotion = uObjectMotionAvailable
        ? texture(uObjectMotion, vUv)
        : vec4(0.0);
    if (objectMotion.z > 0.5) previousUv = vUv - objectMotion.xy;
    resolvedMotion = vUv - previousUv;
    resolvedDepth = currentDepth;

    vec3 current = texture(uCurrentColor, vUv).rgb;
    bool inside = previousClip.w > 0.0
        && all(greaterThanEqual(previousUv, vec2(0.0)))
        && all(lessThanEqual(previousUv, vec2(1.0)));
    if (!uHistoryValid || !inside) {
        resolvedColor = vec4(current, 0.0);
        return;
    }
    float previousDepth = texture(uHistoryDepth, previousUv).r;
    float expectedDepth = previousNdc.z * 0.5 + 0.5;
    float depthThreshold = max(0.0015, 0.015 * (1.0 - currentDepth));
    if (abs(previousDepth - expectedDepth) > depthThreshold) {
        resolvedColor = vec4(current, 0.0);
        return;
    }

    vec3 neighborhoodMin = current;
    vec3 neighborhoodMax = current;
    vec2 texel = 1.0 / vec2(textureSize(uCurrentColor, 0));
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec3 sampleColor = texture(uCurrentColor, vUv + vec2(x, y) * texel).rgb;
            neighborhoodMin = min(neighborhoodMin, sampleColor);
            neighborhoodMax = max(neighborhoodMax, sampleColor);
        }
    }

    vec3 history = clamp(
        texture(uHistoryColor, previousUv).rgb,
        neighborhoodMin,
        neighborhoodMax
    );
    resolvedColor = vec4(mix(current, history, uHistoryWeight), uHistoryWeight);
}
