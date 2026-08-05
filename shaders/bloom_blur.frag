#version 330 core
in vec2 vUv;
uniform sampler2D uImage;
uniform bool uHorizontal;
out vec4 fragmentColor;
void main() {
    vec2 texel = 1.0 / vec2(textureSize(uImage, 0));
    vec2 direction = uHorizontal ? vec2(texel.x, 0.0) : vec2(0.0, texel.y);
    float weights[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
    vec3 result = texture(uImage, vUv).rgb * weights[0];
    for (int index = 1; index < 5; ++index) {
        result += texture(uImage, vUv + direction * index).rgb * weights[index];
        result += texture(uImage, vUv - direction * index).rgb * weights[index];
    }
    fragmentColor = vec4(result, 1.0);
}
