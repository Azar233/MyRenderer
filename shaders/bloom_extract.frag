#version 330 core
in vec2 vUv;
uniform sampler2D uScene;
uniform float uThreshold;
out vec4 fragmentColor;
void main() {
    vec3 color = texture(uScene, vUv).rgb;
    float brightness = max(max(color.r, color.g), color.b);
    float weight = smoothstep(uThreshold, uThreshold + 0.5, brightness);
    fragmentColor = vec4(color * weight, 1.0);
}
