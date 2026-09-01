#version 330 core

layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec3 aNormal;
layout (location = 8) in uvec4 aJointIndices;
layout (location = 9) in vec4 aJointWeights;

uniform mat4 uModel;
uniform bool uSkinningEnabled;
uniform mat4 uJointMatrices[64];

out vec3 vWorldPosition;
out vec3 vWorldNormal;

void main() {
    mat4 skin = uSkinningEnabled
        ? uJointMatrices[aJointIndices.x] * aJointWeights.x
            + uJointMatrices[aJointIndices.y] * aJointWeights.y
            + uJointMatrices[aJointIndices.z] * aJointWeights.z
            + uJointMatrices[aJointIndices.w] * aJointWeights.w
        : mat4(1.0);
    vWorldPosition = (uModel * skin * vec4(aPosition, 1.0)).xyz;
    vWorldNormal = normalize(transpose(inverse(mat3(uModel))) * mat3(skin) * aNormal);
    gl_Position = vec4(vWorldPosition, 1.0);
}
