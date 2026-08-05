#version 330 core

in vec3 vWorldPosition;
in vec3 vWorldNormal;
in vec2 vTexCoord0;
in vec4 vWorldTangent;
in vec4 vShadowPosition;

uniform vec3 uBaseColor;
uniform sampler2D uBaseColorTexture;
uniform sampler2D uNormalTexture;
uniform sampler2D uMetallicRoughnessTexture;
uniform samplerCube uEnvironmentMap;
uniform sampler2D uShadowMap;
uniform bool uHasNormalTexture;
uniform bool uHasMetallicRoughnessTexture;
uniform bool uNormalMappingEnabled;
uniform bool uPbrEnabled;
uniform bool uIblEnabled;
uniform bool uShadowsEnabled;
uniform vec3 uLightDirection;
uniform vec3 uCameraPosition;
uniform float uAmbientStrength;
uniform float uDiffuseStrength;
uniform float uSpecularStrength;
uniform float uShininess;
uniform float uMetallicFactor;
uniform float uRoughnessFactor;
uniform float uEnvironmentIntensity;
uniform float uEnvironmentMaxMip;

out vec4 fragmentColor;

const float PI = 3.14159265359;

float distributionGGX(vec3 normal, vec3 halfDirection, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float nDotH = max(dot(normal, halfDirection), 0.0);
    float denominator = nDotH * nDotH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * denominator * denominator, 0.0001);
}

float geometrySchlickGGX(float nDotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return nDotV / max(nDotV * (1.0 - k) + k, 0.0001);
}

vec3 fresnelSchlick(float cosine, vec3 f0) {
    return f0 + (1.0 - f0) * pow(clamp(1.0 - cosine, 0.0, 1.0), 5.0);
}

float shadowVisibility(vec3 normal, vec3 lightDirection) {
    if (!uShadowsEnabled) return 1.0;
    vec3 projected = vShadowPosition.xyz / max(vShadowPosition.w, 0.0001);
    projected = projected * 0.5 + 0.5;
    if (projected.z > 1.0) return 1.0;
    float bias = max(0.0015 * (1.0 - dot(normal, lightDirection)), 0.00035);
    vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0));
    float visible = 0.0;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float depth = texture(uShadowMap, projected.xy + vec2(x, y) * texel).r;
            visible += projected.z - bias <= depth ? 1.0 : 0.0;
        }
    }
    return visible / 9.0;
}

void main() {
    vec3 normal = normalize(vWorldNormal);
    if (uNormalMappingEnabled && uHasNormalTexture && abs(vWorldTangent.w) > 0.5) {
        vec3 tangent = normalize(vWorldTangent.xyz - normal * dot(normal, vWorldTangent.xyz));
        vec3 bitangent = normalize(cross(normal, tangent)) * vWorldTangent.w;
        vec3 tangentNormal = texture(uNormalTexture, vTexCoord0).xyz * 2.0 - 1.0;
        normal = normalize(mat3(tangent, bitangent, normal) * tangentNormal);
    }

    vec3 albedo = uBaseColor * texture(uBaseColorTexture, vTexCoord0).rgb;
    vec2 materialSample = uHasMetallicRoughnessTexture
        ? texture(uMetallicRoughnessTexture, vTexCoord0).gb
        : vec2(1.0);
    float roughness = clamp(uRoughnessFactor * materialSample.x, 0.04, 1.0);
    float metallic = clamp(uMetallicFactor * materialSample.y, 0.0, 1.0);
    vec3 lightDirection = normalize(-uLightDirection);
    vec3 viewDirection = normalize(uCameraPosition - vWorldPosition);
    vec3 halfDirection = normalize(lightDirection + viewDirection);
    float nDotL = max(dot(normal, lightDirection), 0.0);
    float nDotV = max(dot(normal, viewDirection), 0.001);
    float visibility = shadowVisibility(normal, lightDirection);

    if (!uPbrEnabled) {
        float specular = nDotL > 0.0
            ? pow(max(dot(normal, halfDirection), 0.0), max(uShininess, 1.0))
            : 0.0;
        fragmentColor = vec4(
            uAmbientStrength * albedo
            + visibility * (uDiffuseStrength * nDotL * albedo + uSpecularStrength * specular),
            1.0
        );
        return;
    }

    vec3 f0 = mix(vec3(0.04), albedo, metallic);
    float distribution = distributionGGX(normal, halfDirection, roughness);
    float geometry = geometrySchlickGGX(nDotV, roughness)
        * geometrySchlickGGX(max(dot(normal, lightDirection), 0.0), roughness);
    vec3 fresnel = fresnelSchlick(max(dot(halfDirection, viewDirection), 0.0), f0);
    vec3 specular = distribution * geometry * fresnel
        / max(4.0 * nDotV * max(nDotL, 0.001), 0.001);
    vec3 diffuseWeight = (vec3(1.0) - fresnel) * (1.0 - metallic);
    vec3 direct = (diffuseWeight * albedo / PI + specular) * nDotL * uDiffuseStrength;

    vec3 ambient = uAmbientStrength * albedo;
    if (uIblEnabled) {
        vec3 reflection = reflect(-viewDirection, normal);
        vec3 diffuseEnvironment = textureLod(uEnvironmentMap, normal, uEnvironmentMaxMip).rgb;
        vec3 specularEnvironment = textureLod(
            uEnvironmentMap, reflection, roughness * uEnvironmentMaxMip
        ).rgb;
        vec3 environmentFresnel = fresnelSchlick(nDotV, f0);
        ambient = ((vec3(1.0) - environmentFresnel) * (1.0 - metallic) * albedo * diffuseEnvironment
            + specularEnvironment * environmentFresnel) * uEnvironmentIntensity;
    }
    fragmentColor = vec4(ambient + visibility * direct, 1.0);
}
