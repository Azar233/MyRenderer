#version 330 core

layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec3 aNormal;
layout (location = 8) in uvec4 aJointIndices;
layout (location = 9) in vec4 aJointWeights;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform bool uSkinningEnabled;
uniform mat4 uJointMatrices[64];

out float vViewDepth;
out vec3 vWorldNormal;

void main() {
    mat4 skin = uSkinningEnabled
        ? uJointMatrices[aJointIndices.x] * aJointWeights.x
            + uJointMatrices[aJointIndices.y] * aJointWeights.y
            + uJointMatrices[aJointIndices.z] * aJointWeights.z
            + uJointMatrices[aJointIndices.w] * aJointWeights.w
        : mat4(1.0);
    vec4 worldPosition = uModel * skin * vec4(aPosition, 1.0);
    mat3 normalMatrix = transpose(inverse(mat3(uModel)));
    vec4 viewPosition = uView * worldPosition;
    vViewDepth = -viewPosition.z;
    vWorldNormal = normalize(normalMatrix * mat3(skin) * aNormal);
    gl_Position = uProjection * viewPosition;
}
