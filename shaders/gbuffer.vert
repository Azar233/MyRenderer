#version 330 core

layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord0;
layout (location = 3) in vec4 aTangent;
layout (location = 4) in mat4 aInstanceModel;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform bool uInstanced;

out vec3 vWorldNormal;
out vec2 vTexCoord0;
out vec4 vWorldTangent;

void main() {
    mat4 model = uInstanced ? aInstanceModel : uModel;
    vec4 worldPosition = model * vec4(aPosition, 1.0);
    mat3 normalMatrix = transpose(inverse(mat3(model)));
    vWorldNormal = normalize(normalMatrix * aNormal);
    vTexCoord0 = aTexCoord0;
    vWorldTangent = vec4(normalize(mat3(model) * aTangent.xyz), aTangent.w);
    gl_Position = uProjection * uView * worldPosition;
}
