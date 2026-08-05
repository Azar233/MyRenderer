#version 330 core

in vec2 vUv;
uniform samplerCube uEnvironmentMap;
uniform mat4 uInverseViewProjection;
uniform vec3 uCameraPosition;
uniform float uEnvironmentIntensity;
out vec4 fragmentColor;

void main() {
    vec4 world = uInverseViewProjection * vec4(vUv * 2.0 - 1.0, 1.0, 1.0);
    vec3 direction = normalize(world.xyz / world.w - uCameraPosition);
    fragmentColor = vec4(textureLod(uEnvironmentMap, direction, 0.0).rgb * uEnvironmentIntensity, 1.0);
}
