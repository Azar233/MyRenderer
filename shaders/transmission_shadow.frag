#version 330 core

out vec4 fragmentColor;

uniform float uTransmissionFactor;
uniform float uThicknessFactor;
uniform float uVolumeThicknessScale;
uniform vec3 uAttenuationColor;
uniform float uAttenuationDistance;
uniform bool uVolumeGlassOverrideEnabled;
uniform float uVolumeGlassTransmission;
uniform vec3 uVolumeGlassAttenuationColor;
uniform float uVolumeGlassAttenuationDistance;

void main() {
    float transmission = uVolumeGlassOverrideEnabled
        ? uVolumeGlassTransmission
        : uTransmissionFactor;
    vec3 attenuationColor = uVolumeGlassOverrideEnabled
        ? uVolumeGlassAttenuationColor
        : uAttenuationColor;
    float attenuationDistance = uVolumeGlassOverrideEnabled
        ? uVolumeGlassAttenuationDistance
        : uAttenuationDistance;
    vec3 attenuation = vec3(1.0);
    if (uThicknessFactor > 0.0 && attenuationDistance < 1.0e19) {
        // Both boundary surfaces are rasterized. Half the optical depth per
        // surface makes their multiplicative product approximate one volume.
        attenuation = pow(
            clamp(attenuationColor, vec3(0.0001), vec3(1.0)),
            vec3(0.5 * uThicknessFactor * uVolumeThicknessScale)
                / max(attenuationDistance, 0.0001)
        );
    }
    fragmentColor = vec4(mix(vec3(1.0), attenuation, clamp(transmission, 0.0, 1.0)), 1.0);
}
