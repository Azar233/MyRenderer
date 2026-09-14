#version 330 core
in vec2 vTexCoord0;
uniform sampler2D uBaseColorTexture;
uniform bool uSelected;
uniform vec4 uBaseColor;
uniform int uAlphaMode;
uniform float uAlphaCutoff;
out vec2 mask;
void main() {
    float alpha = texture(uBaseColorTexture, vTexCoord0).a * uBaseColor.a;
    if (uAlphaMode == 1 && alpha < uAlphaCutoff) discard;
    if (alpha <= 0.001) discard;
    mask = vec2(uSelected ? 1.0 : 0.0, gl_FragCoord.z);
}
