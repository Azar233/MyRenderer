#version 330 core

layout (triangles) in;
layout (triangle_strip, max_vertices = 4) out;

in vec3 vWorldPosition[];
in vec3 vWorldNormal[];
out vec2 gLocal;
out float gValid;
out float gEnergy;

uniform mat4 uLightViewProjection;
uniform vec3 uLightDirection;
uniform float uReceiverPlaneY;
uniform float uIndexOfRefraction;
uniform float uIndexOfRefractionOverride;
uniform float uMaterialDispersion;
uniform float uDispersionStrength;
uniform int uCausticChannel;
uniform float uCausticScale;
uniform vec3 uCausticDirection;

void emitSplatVertex(vec4 center, vec2 local, float radius, float energy) {
    gl_Position = center + vec4(local * radius * center.w, 0.0, 0.0);
    gLocal = local;
    gValid = 1.0;
    gEnergy = energy;
    EmitVertex();
}

void main() {
    vec3 worldPosition = (vWorldPosition[0] + vWorldPosition[1] + vWorldPosition[2]) / 3.0;
    vec3 worldNormal = normalize(vWorldNormal[0] + vWorldNormal[1] + vWorldNormal[2]);
    vec3 incident = normalize(uLightDirection);
    float incidence = max(dot(-incident, worldNormal), 0.0);
    float dispersion = uDispersionStrength > 0.0
        ? uDispersionStrength : max(uMaterialDispersion, 0.0);
    float centralIor = uIndexOfRefractionOverride > 0.0
        ? uIndexOfRefractionOverride : uIndexOfRefraction;
    // The splat projection magnifies the standard RGB IOR spread so the
    // channel separation remains legible at 1024x1024.
    float halfSpread = (max(centralIor, 1.0) - 1.0) * 0.10 * dispersion;
    float channelOffset = uCausticChannel == 0 ? -halfSpread
        : (uCausticChannel == 2 ? halfSpread : 0.0);
    float ior = max(centralIor + channelOffset, 1.0);
    vec3 refracted = refract(incident, worldNormal, 1.0 / ior);
    bool valid = incidence > 0.015 && length(refracted) > 0.01 && refracted.y < -0.001;
    float refractedT = valid ? (uReceiverPlaneY - worldPosition.y) / refracted.y : -1.0;
    float straightT = abs(incident.y) > 0.001
        ? (uReceiverPlaneY - worldPosition.y) / incident.y : -1.0;
    if (!valid || refractedT <= 0.0 || straightT <= 0.0) return;
    vec3 refractedHit = worldPosition + refracted * refractedT;
    vec3 straightHit = worldPosition + incident * straightT;
    vec3 receiver = straightHit
        + (refractedHit - straightHit) * max(uCausticScale, 0.0)
        + vec3(uCausticDirection.x, 0.0, uCausticDirection.z);
    vec4 center = uLightViewProjection * vec4(receiver, 1.0);
    float focusing = 1.0 / max(abs(refracted.y), 0.22);
    float energy = clamp(incidence * focusing * 0.026, 0.0, 0.12);
    float radius = 0.0105;
    emitSplatVertex(center, vec2(-1.0, -1.0), radius, energy);
    emitSplatVertex(center, vec2( 1.0, -1.0), radius, energy);
    emitSplatVertex(center, vec2(-1.0,  1.0), radius, energy);
    emitSplatVertex(center, vec2( 1.0,  1.0), radius, energy);
    EndPrimitive();
}
