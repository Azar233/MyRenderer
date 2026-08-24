#version 330 core

layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec3 aNormal;

uniform mat4 uModel;

out vec3 vWorldPosition;
out vec3 vWorldNormal;

void main() {
    vWorldPosition = (uModel * vec4(aPosition, 1.0)).xyz;
    vWorldNormal = normalize(transpose(inverse(mat3(uModel))) * aNormal);
    gl_Position = vec4(vWorldPosition, 1.0);
}
