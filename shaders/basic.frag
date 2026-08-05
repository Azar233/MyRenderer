#version 330 core

in vec3 vWorldPosition;
in vec3 vWorldNormal;
in vec2 vTexCoord0;
in vec4 vWorldTangent;

uniform vec3 uBaseColor;
uniform sampler2D uBaseColorTexture;
uniform sampler2D uNormalTexture;
uniform bool uHasNormalTexture;
uniform bool uNormalMappingEnabled;
uniform vec3 uLightDirection;
uniform vec3 uCameraPosition;
uniform float uAmbientStrength;
uniform float uDiffuseStrength;
uniform float uSpecularStrength;
uniform float uShininess;

out vec4 fragmentColor;

vec3 linearToSrgb(vec3 color) {
    color = max(color, vec3(0.0));
    bvec3 cutoff = lessThanEqual(color, vec3(0.0031308));
    vec3 lower = color * 12.92;
    vec3 higher = 1.055 * pow(color, vec3(1.0 / 2.4)) - 0.055;
    return mix(higher, lower, cutoff);
}

void main() {
    vec3 normal = normalize(vWorldNormal);
    if (uNormalMappingEnabled && uHasNormalTexture && abs(vWorldTangent.w) > 0.5) {
        vec3 tangent = normalize(vWorldTangent.xyz - normal * dot(normal, vWorldTangent.xyz));
        vec3 bitangent = normalize(cross(normal, tangent)) * vWorldTangent.w;
        vec3 tangentNormal = texture(uNormalTexture, vTexCoord0).xyz * 2.0 - 1.0;
        normal = normalize(mat3(tangent, bitangent, normal) * tangentNormal);
    }
    vec3 lightDirection = normalize(-uLightDirection);
    vec3 viewDirection = normalize(uCameraPosition - vWorldPosition);
    vec3 halfDirection = normalize(lightDirection + viewDirection);

    float diffuseFactor = max(dot(normal, lightDirection), 0.0);
    float specularFactor = 0.0;
    if (diffuseFactor > 0.0) {
        specularFactor = pow(max(dot(normal, halfDirection), 0.0), max(uShininess, 1.0));
    }

    vec3 materialColor = uBaseColor * texture(uBaseColorTexture, vTexCoord0).rgb;
    vec3 ambient = uAmbientStrength * materialColor;
    vec3 diffuse = uDiffuseStrength * diffuseFactor * materialColor;
    vec3 specular = uSpecularStrength * specularFactor * vec3(1.0);
    vec3 linearColor = ambient + diffuse + specular;
    vec3 displayColor = linearToSrgb(linearColor);
    fragmentColor = vec4(displayColor, 1.0);
}
