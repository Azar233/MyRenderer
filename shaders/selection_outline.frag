#version 330 core
uniform sampler2D uMask;
uniform sampler2D uSilhouette;
uniform sampler2D uSceneDepth;
out vec4 fragColor;
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    ivec2 size = textureSize(uMask, 0);
    if (texelFetch(uSilhouette, p, 0).r > 0.5) discard;
    float sceneDepth = texelFetch(uSceneDepth, p, 0).r;
    float edge = 0.0;
    for (int y = -3; y <= 3; ++y) {
        for (int x = -3; x <= 3; ++x) {
            if (x*x + y*y > 9) continue;
            ivec2 q = clamp(p + ivec2(x, y), ivec2(0), size - 1);
            vec2 silhouette = texelFetch(uSilhouette, q, 0).rg;
            if (silhouette.r > 0.5 && silhouette.g <= sceneDepth + 0.00001)
                edge = max(edge, texelFetch(uMask, q, 0).r);
        }
    }
    if (edge < 0.5) discard;
    fragColor = vec4(1.0, 0.58, 0.08, 1.0);
}
