#version 330 core

in float vViewDepth;
in vec3 vWorldNormal;

uniform int uPassMode;
uniform sampler2D uGlassBackfaceDepthTexture;
uniform int uGlassObjectId;

layout (location = 0) out vec4 surfaceData;
layout (location = 1) out uint surfaceObjectId;

void main() {
    if (uPassMode == 0) {
        surfaceData = vec4(max(vViewDepth, 0.0));
        surfaceObjectId = 0u;
        return;
    }

    vec2 framebufferSize = vec2(textureSize(uGlassBackfaceDepthTexture, 0));
    vec2 screenUv = gl_FragCoord.xy / max(framebufferSize, vec2(1.0));
    float exitDepth = texture(uGlassBackfaceDepthTexture, screenUv).r;
    float tolerance = max(exitDepth * 0.0003, 0.00075);
    if (exitDepth <= 0.0 || abs(vViewDepth - exitDepth) > tolerance) {
        discard;
    }

    surfaceData = vec4(normalize(vWorldNormal) * 0.5 + 0.5, 1.0);
    surfaceObjectId = uint(max(uGlassObjectId, 0));
}
