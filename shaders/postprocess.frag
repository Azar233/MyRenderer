#version 330 core
in vec2 vUv;
uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform bool uToneMapping;
uniform bool uBloomEnabled;
uniform float uExposure;
uniform float uBloomIntensity;
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

void main() {
    vec3 color = texture(uScene, vUv).rgb;
    if (uBloomEnabled) color += texture(uBloom, vUv).rgb * uBloomIntensity;
    color *= max(uExposure, 0.0);
    color = uToneMapping ? aces(color) : clamp(color, 0.0, 1.0);
    fragmentColor = vec4(linearToSrgb(color), 1.0);
}
