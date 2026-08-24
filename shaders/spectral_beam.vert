#version 330 core

layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec3 aLinearColor;
layout (location = 2) in float aEdgeCoordinate;

uniform mat4 uView;
uniform mat4 uProjection;

out vec3 vLinearColor;
out float vEdgeCoordinate;

void main() {
    vLinearColor = aLinearColor;
    vEdgeCoordinate = aEdgeCoordinate;
    gl_Position = uProjection * uView * vec4(aPosition, 1.0);
}
