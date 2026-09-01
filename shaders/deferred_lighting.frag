#version 330 core

in vec2 vUv;

uniform sampler2D uGAlbedo;
uniform sampler2D uGNormal;
uniform sampler2D uGMaterial;
uniform sampler2D uGDepth;
uniform samplerCube uIrradianceMap;
uniform samplerCube uPrefilteredEnvironmentMap;
uniform sampler2D uBrdfLut;
uniform sampler2D uShadowMap;
uniform sampler2D uTransmissionShadowMap;
uniform sampler2D uCausticsMap;
uniform sampler2D uSsao;
uniform mat4 uInverseViewProjection;
uniform mat4 uLightViewProjection;
uniform vec3 uCameraPosition;
uniform vec3 uLightDirection;
const int MAX_LOCAL_LIGHTS = 64;
uniform int uLocalLightCount;
uniform vec4 uLocalLightPositionRadius[MAX_LOCAL_LIGHTS];
uniform vec4 uLocalLightColorIntensity[MAX_LOCAL_LIGHTS];
uniform vec4 uLocalLightDirectionOuter[MAX_LOCAL_LIGHTS];
uniform float uAmbientStrength;
uniform float uDiffuseStrength;
uniform float uSpecularStrength;
uniform float uShininess;
uniform float uEnvironmentIntensity;
uniform float uEnvironmentMaxMip;
uniform bool uPbrEnabled;
uniform bool uIblEnabled;
uniform bool uShadowsEnabled;
uniform bool uColoredTransmissionShadowsEnabled;
uniform bool uCausticsEnabled;
uniform bool uSsaoEnabled;
uniform bool uSkinningDebugActive;
uniform int uGBufferDebugView;

out vec4 fragmentColor;

const float PI = 3.14159265359;

vec3 reconstructWorldPosition(float depth) {
    vec4 clip = vec4(vUv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 world = uInverseViewProjection * clip;
    return world.xyz / max(world.w, 0.00001);
}

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

vec3 localLightRadiance(int index, vec3 worldPosition, out vec3 lightDirection) {
    vec4 positionRadius = uLocalLightPositionRadius[index];
    vec3 toLight = positionRadius.xyz - worldPosition;
    float distanceToLight = length(toLight);
    lightDirection = toLight / max(distanceToLight, 0.0001);
    float radius = max(abs(positionRadius.w), 0.001);
    float distanceRatio = distanceToLight / radius;
    float smoothRange = clamp(1.0 - pow(distanceRatio, 4.0), 0.0, 1.0);
    float attenuation = smoothRange * smoothRange
        / max(1.0 + distanceToLight * distanceToLight, 0.0001);

    if (positionRadius.w < 0.0) {
        vec4 directionOuter = uLocalLightDirectionOuter[index];
        vec3 fromLight = normalize(worldPosition - positionRadius.xyz);
        float coneCosine = dot(fromLight, normalize(directionOuter.xyz));
        float innerCosine = min(directionOuter.w + 0.10, 0.999);
        attenuation *= smoothstep(directionOuter.w, innerCosine, coneCosine);
    }
    vec4 colorIntensity = uLocalLightColorIntensity[index];
    return colorIntensity.rgb * colorIntensity.w * attenuation;
}

vec3 projectedCoordinates(vec3 worldPosition) {
    vec4 shadowPosition = uLightViewProjection * vec4(worldPosition, 1.0);
    return shadowPosition.xyz / max(shadowPosition.w, 0.0001) * 0.5 + 0.5;
}

vec3 shadowVisibility(vec3 worldPosition, vec3 normal, vec3 lightDirection) {
    if (!uShadowsEnabled) return vec3(1.0);
    vec3 projected = projectedCoordinates(worldPosition);
    if (projected.z > 1.0 || projected.z < 0.0
        || any(lessThan(projected.xy, vec2(0.0)))
        || any(greaterThan(projected.xy, vec2(1.0)))) return vec3(1.0);
    float bias = max(0.0015 * (1.0 - dot(normal, lightDirection)), 0.00035);
    vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0));
    float visible = 0.0;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float sampleDepth = texture(uShadowMap, projected.xy + vec2(x, y) * texel).r;
            visible += projected.z - bias <= sampleDepth ? 1.0 : 0.0;
        }
    }
    vec3 transmission = uColoredTransmissionShadowsEnabled
        ? texture(uTransmissionShadowMap, projected.xy).rgb
        : vec3(1.0);
    return transmission * (visible / 9.0);
}

vec3 causticRadiance(vec3 worldPosition) {
    if (!uCausticsEnabled) return vec3(0.0);
    vec3 projected = projectedCoordinates(worldPosition);
    if (projected.z > 1.0 || projected.z < 0.0
        || any(lessThan(projected.xy, vec2(0.0)))
        || any(greaterThan(projected.xy, vec2(1.0)))) return vec3(0.0);
    return texture(uCausticsMap, projected.xy).rgb;
}

void main() {
    float depth = texture(uGDepth, vUv).r;
    vec3 albedo = texture(uGAlbedo, vUv).rgb;
    vec3 encodedNormal = texture(uGNormal, vUv).rgb;
    vec2 material = texture(uGMaterial, vUv).rg;

    if (uSkinningDebugActive) {
        if (depth >= 0.999999) discard;
        fragmentColor = vec4(albedo, 1.0);
        return;
    }

    if (uGBufferDebugView == 1) {
        fragmentColor = vec4(albedo, 1.0);
        return;
    }
    if (uGBufferDebugView == 2) {
        fragmentColor = vec4(encodedNormal, 1.0);
        return;
    }
    if (uGBufferDebugView == 3) {
        fragmentColor = vec4(material.r, material.g, 0.0, 1.0);
        return;
    }
    if (uGBufferDebugView == 4) {
        float linearized = pow(clamp(depth, 0.0, 1.0), 64.0);
        fragmentColor = vec4(vec3(1.0 - linearized), 1.0);
        return;
    }
    float ambientOcclusion = uSsaoEnabled ? texture(uSsao, vUv).r : 1.0;
    if (uGBufferDebugView == 5) {
        fragmentColor = vec4(vec3(ambientOcclusion), 1.0);
        return;
    }
    if (depth >= 0.999999) discard;
    vec3 normal = normalize(encodedNormal * 2.0 - 1.0);
    float metallic = material.r;
    float roughness = clamp(material.g, 0.04, 1.0);

    vec3 worldPosition = reconstructWorldPosition(depth);
    vec3 viewDirection = normalize(uCameraPosition - worldPosition);
    vec3 lightDirection = normalize(-uLightDirection);
    vec3 halfDirection = normalize(viewDirection + lightDirection);
    float nDotL = max(dot(normal, lightDirection), 0.0);
    float nDotV = max(dot(normal, viewDirection), 0.0);
    vec3 visibility = shadowVisibility(worldPosition, normal, lightDirection);
    vec3 caustics = causticRadiance(worldPosition);

    if (!uPbrEnabled) {
        float diffuse = nDotL * uDiffuseStrength;
        float specular = pow(max(dot(normal, halfDirection), 0.0), uShininess)
            * uSpecularStrength;
        vec3 localLighting = vec3(0.0);
        for (int index = 0; index < uLocalLightCount; ++index) {
            vec3 localDirection;
            vec3 radiance = localLightRadiance(index, worldPosition, localDirection);
            float localNDotL = max(dot(normal, localDirection), 0.0);
            vec3 localHalf = normalize(localDirection + viewDirection);
            float localSpecular = localNDotL > 0.0
                ? pow(max(dot(normal, localHalf), 0.0), uShininess)
                    * uSpecularStrength
                : 0.0;
            localLighting += radiance * (
                albedo * localNDotL * uDiffuseStrength + localSpecular
            );
        }
        fragmentColor = vec4(
            albedo * (uAmbientStrength * ambientOcclusion + visibility * diffuse)
                + visibility * vec3(specular) + caustics * albedo + localLighting,
            1.0
        );
        return;
    }

    vec3 f0 = mix(vec3(0.04), albedo, metallic);
    vec3 fresnel = fresnelSchlick(max(dot(halfDirection, viewDirection), 0.0), f0);
    float distribution = distributionGGX(normal, halfDirection, roughness);
    float geometry = geometrySchlickGGX(nDotV, roughness)
        * geometrySchlickGGX(nDotL, roughness);
    vec3 specular = distribution * geometry * fresnel
        / max(4.0 * nDotV * nDotL, 0.0001);
    vec3 diffuseWeight = (vec3(1.0) - fresnel) * (1.0 - metallic);
    vec3 direct = (diffuseWeight * albedo / PI + specular)
        * nDotL * uDiffuseStrength;
    vec3 localDirect = vec3(0.0);
    for (int index = 0; index < uLocalLightCount; ++index) {
        vec3 localDirection;
        vec3 radiance = localLightRadiance(index, worldPosition, localDirection);
        float localNDotL = max(dot(normal, localDirection), 0.0);
        if (localNDotL <= 0.0) continue;
        vec3 localHalf = normalize(viewDirection + localDirection);
        vec3 localFresnel = fresnelSchlick(
            max(dot(localHalf, viewDirection), 0.0),
            f0
        );
        float localDistribution = distributionGGX(normal, localHalf, roughness);
        float localGeometry = geometrySchlickGGX(nDotV, roughness)
            * geometrySchlickGGX(localNDotL, roughness);
        vec3 localSpecular = localDistribution * localGeometry * localFresnel
            / max(4.0 * nDotV * localNDotL, 0.0001);
        vec3 localDiffuseWeight = (vec3(1.0) - localFresnel) * (1.0 - metallic);
        localDirect += radiance * (
            localDiffuseWeight * albedo / PI + localSpecular
        ) * localNDotL * uDiffuseStrength;
    }

    vec3 ambient = albedo * uAmbientStrength;
    if (uIblEnabled) {
        vec3 iblFresnel = fresnelSchlick(nDotV, f0);
        vec3 irradiance = texture(uIrradianceMap, normal).rgb;
        vec3 diffuseIbl = irradiance * albedo;
        vec3 reflected = reflect(-viewDirection, normal);
        vec3 prefiltered = textureLod(
            uPrefilteredEnvironmentMap,
            reflected,
            roughness * uEnvironmentMaxMip
        ).rgb;
        vec2 brdf = texture(uBrdfLut, vec2(nDotV, roughness)).rg;
        vec3 specularIbl = prefiltered * (iblFresnel * brdf.x + brdf.y);
        ambient = ((vec3(1.0) - iblFresnel) * (1.0 - metallic) * diffuseIbl
            + specularIbl) * uEnvironmentIntensity;
    }
    fragmentColor = vec4(
        ambient * ambientOcclusion + visibility * direct + localDirect + caustics * albedo,
        1.0
    );
}
