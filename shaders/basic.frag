#version 330 core

in vec3 vWorldPosition;
in vec3 vWorldNormal;

uniform vec3 uBaseColor;
uniform vec3 uLightDirection;
uniform vec3 uCameraPosition;
uniform float uAmbientStrength;
uniform float uDiffuseStrength;
uniform float uSpecularStrength;
uniform float uShininess;

out vec4 fragmentColor;

void main() {
    vec3 normal = normalize(vWorldNormal);
    vec3 lightDirection = normalize(-uLightDirection);
    vec3 viewDirection = normalize(uCameraPosition - vWorldPosition);
    vec3 halfDirection = normalize(lightDirection + viewDirection);

    float diffuseFactor = max(dot(normal, lightDirection), 0.0);
    float specularFactor = 0.0;
    if (diffuseFactor > 0.0) {
        specularFactor = pow(max(dot(normal, halfDirection), 0.0), max(uShininess, 1.0));
    }

    vec3 ambient = uAmbientStrength * uBaseColor;
    vec3 diffuse = uDiffuseStrength * diffuseFactor * uBaseColor;
    vec3 specular = uSpecularStrength * specularFactor * vec3(1.0);
    vec3 linearColor = ambient + diffuse + specular;
    vec3 displayColor = pow(clamp(linearColor, 0.0, 1.0), vec3(1.0 / 2.2));
    fragmentColor = vec4(displayColor, 1.0);
}
