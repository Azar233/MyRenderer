#version 330 core

layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord0;
layout (location = 3) in vec4 aTangent;
layout (location = 4) in mat4 aInstanceModel;
layout (location = 8) in uvec4 aJointIndices;
layout (location = 9) in vec4 aJointWeights;

uniform mat4 uModel;
uniform mat4 uNodeTransform;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat4 uLightViewProjection;
uniform bool uInstanced;
uniform bool uSkinningEnabled;
uniform mat4 uJointMatrices[64];

out vec3 vWorldPosition;
out vec3 vWorldNormal;
out vec2 vTexCoord0;
out vec4 vWorldTangent;
out vec4 vShadowPosition;
out vec3 vSkinJointColor;
out float vSkinDominantWeight;

vec3 jointColor(uint jointIndex) {
    float joint = float(jointIndex) + 1.0;
    return fract(sin(joint * vec3(12.9898, 78.233, 37.719)) * 43758.5453);
}

void main() {
    mat4 model = (uInstanced ? aInstanceModel : uModel) * uNodeTransform;
    mat4 skin = mat4(1.0);
    if (uSkinningEnabled) {
        skin = uJointMatrices[aJointIndices.x] * aJointWeights.x
            + uJointMatrices[aJointIndices.y] * aJointWeights.y
            + uJointMatrices[aJointIndices.z] * aJointWeights.z
            + uJointMatrices[aJointIndices.w] * aJointWeights.w;
    }
    vec4 localPosition = skin * vec4(aPosition, 1.0);
    vec3 localNormal = normalize(mat3(skin) * aNormal);
    vec3 localTangent = normalize(mat3(skin) * aTangent.xyz);
    vec4 worldPosition = model * localPosition;
    mat3 normalMatrix = transpose(inverse(mat3(model)));
    vWorldPosition = worldPosition.xyz;
    vWorldNormal = normalize(normalMatrix * localNormal);
    vTexCoord0 = aTexCoord0;
    vec3 worldTangent = mat3(model) * localTangent;
    vWorldTangent = vec4(normalize(worldTangent), aTangent.w);
    vShadowPosition = uLightViewProjection * worldPosition;
    vSkinJointColor = jointColor(aJointIndices.x) * aJointWeights.x
        + jointColor(aJointIndices.y) * aJointWeights.y
        + jointColor(aJointIndices.z) * aJointWeights.z
        + jointColor(aJointIndices.w) * aJointWeights.w;
    vSkinDominantWeight = max(max(aJointWeights.x, aJointWeights.y), max(aJointWeights.z, aJointWeights.w));
    gl_Position = uProjection * uView * worldPosition;
}
