#version 330 core

layout (location = 0) in vec3 aPosition;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

out float vViewDepth;

void main() {
    vec4 worldPosition = uModel * vec4(aPosition, 1.0);
    vec4 viewPosition = uView * worldPosition;
    vViewDepth = -viewPosition.z;
    gl_Position = uProjection * viewPosition;
}
