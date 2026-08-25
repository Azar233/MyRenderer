#version 330 core

layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord0;
layout (location = 3) in vec4 aTangent;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

out vec3 vWorldNormal;
out vec2 vTexCoord0;
out vec4 vWorldTangent;

void main() {
    vec4 worldPosition = uModel * vec4(aPosition, 1.0);
    mat3 normalMatrix = transpose(inverse(mat3(uModel)));
    vWorldNormal = normalize(normalMatrix * aNormal);
    vTexCoord0 = aTexCoord0;
    vWorldTangent = vec4(normalize(mat3(uModel) * aTangent.xyz), aTangent.w);
    gl_Position = uProjection * uView * worldPosition;
}
