#version 330 core

in vec2 vUv;
out vec4 fragmentColor;

uniform float uStrength;
uniform float uScale;
uniform vec3 uDirection;
uniform float uSharpness;
uniform float uAnimationPhase;

void main() {
    float angle = atan(uDirection.z, uDirection.x);
    mat2 rotation = mat2(cos(angle), -sin(angle), sin(angle), cos(angle));
    vec2 centered = rotation * (
        vUv - vec2(0.5) - vec2(uDirection.x, uDirection.z) * 0.045
    );
    centered.x *= 1.55;
    centered /= max(uScale, 0.05);
    float phase = uAnimationPhase * 6.2831853;
    float ripple = 0.018 * sin(centered.x * 34.0 + phase)
        + 0.012 * sin(centered.y * 27.0 - phase * 0.71);
    float radius = length(centered);
    float edgeWidth = mix(0.055, 0.008, clamp(uSharpness, 0.0, 1.0));
    vec3 spectral = vec3(0.0);
    spectral.r = exp(-pow((radius - (0.105 + ripple + 0.014)) / edgeWidth, 2.0));
    spectral.g = exp(-pow((radius - (0.105 + ripple)) / edgeWidth, 2.0));
    spectral.b = exp(-pow((radius - (0.105 + ripple - 0.014)) / edgeWidth, 2.0));
    float envelope = smoothstep(0.48, 0.10, radius);
    fragmentColor = vec4(spectral * envelope * uStrength * 0.38, 1.0);
}
