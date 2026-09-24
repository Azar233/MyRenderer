#version 330 core

in vec3 vWorldPosition;
in vec3 vNormal;
in vec3 vVelocity;
in float vFoam;
in vec4 vCurrentClip;
in vec4 vPreviousClip;
in float vMotionValid;

uniform float uFoamStrength;

layout (location = 0) out vec4 gAlbedo;
layout (location = 1) out vec4 gEncodedNormal;
layout (location = 2) out vec2 gMetallicRoughness;
layout (location = 3) out vec4 gMotion;

void main() {
    float foam = smoothstep(0.38, 0.82, vFoam) * uFoamStrength;
    gAlbedo = vec4(0.015, 0.11, 0.16, foam);
    // The normal attachment has a free alpha channel. 0.5 identifies the water material
    // without consuming metallic/roughness precision or altering existing scene materials.
    gEncodedNormal = vec4(normalize(vNormal) * 0.5 + 0.5, 0.5);
    gMetallicRoughness = vec2(0.0, mix(0.07, 0.65, foam));
    gMotion = vec4(0.0);
    if (vMotionValid > 0.5 && vCurrentClip.w > 0.0 && vPreviousClip.w > 0.0) {
        vec2 currentUv = vCurrentClip.xy / vCurrentClip.w * 0.5 + 0.5;
        vec2 previousUv = vPreviousClip.xy / vPreviousClip.w * 0.5 + 0.5;
        gMotion = vec4(currentUv - previousUv, 1.0, 0.0);
    }
}
