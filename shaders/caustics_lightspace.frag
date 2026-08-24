#version 330 core

in vec2 gLocal;
in float gValid;
in float gEnergy;
out vec4 fragmentColor;

uniform int uCausticChannel;
uniform float uCausticStrength;
uniform float uTransmissionFactor;
uniform float uThicknessFactor;
uniform float uVolumeThicknessScale;
uniform vec3 uAttenuationColor;
uniform float uAttenuationDistance;
uniform vec4 uBaseColor;
uniform bool uVolumeGlassOverrideEnabled;
uniform float uVolumeGlassTransmission;
uniform vec3 uVolumeGlassAttenuationColor;
uniform float uVolumeGlassAttenuationDistance;

void main() {
    float radiusSquared = dot(gLocal, gLocal);
    if (gValid < 0.98 || radiusSquared > 1.0 || uTransmissionFactor <= 0.0) discard;
    vec3 attenuation = vec3(1.0);
    vec3 attenuationColor = uVolumeGlassOverrideEnabled
        ? uVolumeGlassAttenuationColor
        : uAttenuationColor;
    float attenuationDistance = uVolumeGlassOverrideEnabled
        ? uVolumeGlassAttenuationDistance
        : uAttenuationDistance;
    if (uThicknessFactor > 0.0 && attenuationDistance < 1.0e19) {
        attenuation = pow(
            clamp(attenuationColor, vec3(0.0001), vec3(1.0)),
            vec3(uThicknessFactor * uVolumeThicknessScale)
                / max(attenuationDistance, 0.0001)
        );
    }
    vec3 channel = uCausticChannel == 0 ? vec3(1.0, 0.0, 0.0)
        : (uCausticChannel == 1 ? vec3(0.0, 1.0, 0.0) : vec3(0.0, 0.0, 1.0));
    float transmission = uVolumeGlassOverrideEnabled
        ? uVolumeGlassTransmission
        : uTransmissionFactor;
    vec3 energy = channel * attenuation * uBaseColor.rgb
        * transmission * uCausticStrength * gEnergy
        * exp(-radiusSquared * 3.5);
    fragmentColor = vec4(energy, 1.0);
}
