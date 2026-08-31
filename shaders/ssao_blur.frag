#version 330 core

in vec2 vUv;
uniform sampler2D uOcclusion;
uniform sampler2D uDepth;
out float fragmentOcclusion;

void main() {
    vec2 texel = 1.0 / vec2(textureSize(uOcclusion, 0));
    float centerDepth = texture(uDepth, vUv).r;
    float weighted = 0.0;
    float weightSum = 0.0;
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            vec2 uv = vUv + vec2(x, y) * texel;
            float sampleDepth = texture(uDepth, uv).r;
            float spatial = exp(-float(x * x + y * y) * 0.22);
            float depthWeight = exp(-abs(sampleDepth - centerDepth) * 900.0);
            float weight = spatial * depthWeight;
            weighted += texture(uOcclusion, uv).r * weight;
            weightSum += weight;
        }
    }
    fragmentOcclusion = weighted / max(weightSum, 0.0001);
}
