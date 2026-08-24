#version 330 core

in vec2 vUv;
out vec4 fragmentColor;

uniform sampler2D uImage;
uniform bool uHorizontal;
uniform float uSharpness;

void main() {
    vec2 texel = 1.0 / vec2(textureSize(uImage, 0));
    float spread = mix(2.4, 0.55, clamp(uSharpness, 0.0, 1.0));
    vec2 axis = (uHorizontal ? vec2(texel.x, 0.0) : vec2(0.0, texel.y)) * spread;
    vec3 result = texture(uImage, vUv).rgb * 0.2941176;
    result += texture(uImage, vUv + axis * 1.3333333).rgb * 0.3529412;
    result += texture(uImage, vUv - axis * 1.3333333).rgb * 0.3529412;
    fragmentColor = vec4(result, 1.0);
}
