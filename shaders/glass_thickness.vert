#version 330 core

layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec3 aNormal;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

out float vViewDepth;
out vec3 vWorldNormal;

void main() {
    vec4 worldPosition = uModel * vec4(aPosition, 1.0);
    mat3 normalMatrix = transpose(inverse(mat3(uModel)));
    vec4 viewPosition = uView * worldPosition;
    vViewDepth = -viewPosition.z;
    vWorldNormal = normalize(normalMatrix * aNormal);
    gl_Position = uProjection * viewPosition;
}
