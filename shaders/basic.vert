#version 330 core

layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord0;
layout (location = 3) in vec4 aTangent;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat4 uLightViewProjection;

out vec3 vWorldPosition;
out vec3 vWorldNormal;
out vec2 vTexCoord0;
out vec4 vWorldTangent;
out vec4 vShadowPosition;

void main() {
    vec4 worldPosition = uModel * vec4(aPosition, 1.0);
    mat3 normalMatrix = transpose(inverse(mat3(uModel)));
    vWorldPosition = worldPosition.xyz;
    vWorldNormal = normalize(normalMatrix * aNormal);
    vTexCoord0 = aTexCoord0;
    vec3 worldTangent = mat3(uModel) * aTangent.xyz;
    vWorldTangent = vec4(normalize(worldTangent), aTangent.w);
    vShadowPosition = uLightViewProjection * worldPosition;
    gl_Position = uProjection * uView * worldPosition;
}
