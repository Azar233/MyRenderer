#version 330 core
in vec2 vTexCoord0;
uniform sampler2D uBaseColorTexture;
uniform vec4 uBaseColor;
uniform int uAlphaMode;
uniform float uAlphaCutoff;
uniform int uPickIndex;
layout(location = 0) out uint objectIndex;
void main() {
    float alpha = texture(uBaseColorTexture, vTexCoord0).a * uBaseColor.a;
    if (uAlphaMode == 1 && alpha < uAlphaCutoff) discard;
    if (alpha <= 0.001) discard;
    objectIndex = uint(uPickIndex);
}
