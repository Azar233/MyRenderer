#version 330 core

in vec3 vLinearColor;
in float vEdgeCoordinate;

uniform float uIntensity;
uniform float uEdgeSoftness;
uniform float uBloomContribution;

out vec4 fragmentColor;

void main() {
    float softness = clamp(uEdgeSoftness, 0.01, 1.0);
    float edge = 1.0 - smoothstep(1.0 - softness, 1.0, abs(vEdgeCoordinate));
    float hdrGain = 1.0 + uBloomContribution;
    fragmentColor = vec4(vLinearColor * uIntensity * hdrGain * edge, 0.0);
}
