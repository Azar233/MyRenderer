#version 330 core

in vec3 vWorldNormal;
in vec2 vTexCoord0;
in vec4 vWorldTangent;
in vec3 vSkinJointColor;
in float vSkinDominantWeight;

uniform vec4 uBaseColor;
uniform sampler2D uBaseColorTexture;
uniform sampler2D uNormalTexture;
uniform sampler2D uMetallicRoughnessTexture;
uniform bool uHasNormalTexture;
uniform bool uHasMetallicRoughnessTexture;
uniform bool uNormalMappingEnabled;
uniform float uMetallicFactor;
uniform float uRoughnessFactor;
uniform int uAlphaMode;
uniform float uAlphaCutoff;
uniform int uSkinningDebugView;

layout (location = 0) out vec4 gAlbedo;
layout (location = 1) out vec4 gEncodedNormal;
layout (location = 2) out vec2 gMetallicRoughness;

void main() {
    vec4 baseColor = uBaseColor * texture(uBaseColorTexture, vTexCoord0);
    if (uAlphaMode == 1 && baseColor.a < uAlphaCutoff) discard;

    if (uSkinningDebugView != 0) {
        float weight = clamp(vSkinDominantWeight, 0.0, 1.0);
        if (uSkinningDebugView == 1) {
            baseColor.rgb = vSkinJointColor;
        } else {
            baseColor.rgb = vec3(1.0 - weight, weight, 0.15 + 0.35 * weight);
        }
    }

    vec3 normal = normalize(vWorldNormal);
    if (uNormalMappingEnabled && uHasNormalTexture) {
        vec3 tangent = normalize(vWorldTangent.xyz);
        tangent = normalize(tangent - normal * dot(normal, tangent));
        vec3 bitangent = normalize(cross(normal, tangent)) * vWorldTangent.w;
        vec3 tangentNormal = texture(uNormalTexture, vTexCoord0).xyz * 2.0 - 1.0;
        normal = normalize(mat3(tangent, bitangent, normal) * tangentNormal);
    }

    vec2 metallicRoughness = vec2(uMetallicFactor, uRoughnessFactor);
    if (uHasMetallicRoughnessTexture) {
        vec4 materialSample = texture(uMetallicRoughnessTexture, vTexCoord0);
        metallicRoughness *= vec2(materialSample.b, materialSample.g);
    }
    gAlbedo = baseColor;
    gEncodedNormal = vec4(normal * 0.5 + 0.5, 1.0);
    gMetallicRoughness = clamp(metallicRoughness, vec2(0.0, 0.04), vec2(1.0));
}
