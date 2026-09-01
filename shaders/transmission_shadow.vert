#version 330 core

layout (location = 0) in vec3 aPosition;
layout (location = 8) in uvec4 aJointIndices;
layout (location = 9) in vec4 aJointWeights;

uniform mat4 uModel;
uniform mat4 uLightViewProjection;
uniform bool uSkinningEnabled;
uniform mat4 uJointMatrices[64];

void main() {
    mat4 skin = uSkinningEnabled
        ? uJointMatrices[aJointIndices.x] * aJointWeights.x
            + uJointMatrices[aJointIndices.y] * aJointWeights.y
            + uJointMatrices[aJointIndices.z] * aJointWeights.z
            + uJointMatrices[aJointIndices.w] * aJointWeights.w
        : mat4(1.0);
    gl_Position = uLightViewProjection * uModel * skin * vec4(aPosition, 1.0);
}
