#version 330 core

in vec3 vWorldPosition;
in vec3 vWorldNormal;
in vec2 vTexCoord0;
in vec4 vWorldTangent;
in vec4 vShadowPosition;

uniform vec4 uBaseColor;
uniform sampler2D uBaseColorTexture;
uniform sampler2D uNormalTexture;
uniform sampler2D uMetallicRoughnessTexture;
uniform samplerCube uIrradianceMap;
uniform samplerCube uPrefilteredEnvironmentMap;
uniform sampler2D uBrdfLut;
uniform sampler2D uShadowMap;
uniform sampler2D uCausticsMap;
uniform sampler2D uTransmissionShadowMap;
uniform sampler2D uOpaqueColorTexture;
uniform sampler2D uSceneDepthTexture;
uniform sampler2D uThicknessTexture;
uniform sampler2D uGlassFrontfaceDepthTexture;
uniform sampler2D uGlassBackfaceDepthTexture;
uniform sampler2D uGlassExitNormalTexture;
uniform usampler2D uGlassObjectIdTexture;
uniform mat4 uView;
uniform mat4 uProjection;
uniform bool uHasNormalTexture;
uniform bool uHasMetallicRoughnessTexture;
uniform bool uHasThicknessTexture;
uniform bool uHasGlassBackfaceData;
uniform bool uGeometricThicknessEnabled;
uniform bool uTwoInterfaceRefractionEnabled;
uniform bool uVolumeGlassOverrideEnabled;
uniform bool uNormalMappingEnabled;
uniform bool uPbrEnabled;
uniform bool uIblEnabled;
uniform bool uShadowsEnabled;
uniform bool uColoredTransmissionShadowsEnabled;
uniform bool uCausticsEnabled;
uniform bool uTransmissionEnabled;
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
uniform float uTransmissionFactor;
uniform float uIndexOfRefraction;
uniform float uIndexOfRefractionOverride;
uniform float uThicknessFactor;
uniform vec3 uAttenuationColor;
uniform float uAttenuationDistance;
uniform float uRefractionScale;
uniform float uVolumeThicknessScale;
uniform float uVolumeGlassTransmission;
uniform float uVolumeGlassRoughness;
uniform vec3 uVolumeGlassAttenuationColor;
uniform float uVolumeGlassAttenuationDistance;
uniform float uDispersionStrength;
uniform float uMaterialDispersion;
uniform float uOpaqueColorMaxMip;
uniform int uRefractionSteps;
uniform int uGlassDebugView;
uniform int uAlphaMode;
uniform float uAlphaCutoff;
uniform bool uDoubleSided;
uniform int uGlassObjectId;

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

vec3 lightProjectionCoordinates() {
    vec3 projected = vShadowPosition.xyz / max(vShadowPosition.w, 0.0001);
    return projected * 0.5 + 0.5;
}

vec3 shadowVisibility(vec3 normal, vec3 lightDirection) {
    if (!uShadowsEnabled) return vec3(1.0);
    vec3 projected = lightProjectionCoordinates();
    if (projected.z > 1.0 || projected.z < 0.0
        || any(lessThan(projected.xy, vec2(0.0)))
        || any(greaterThan(projected.xy, vec2(1.0)))) return vec3(1.0);
    float bias = max(0.0015 * (1.0 - dot(normal, lightDirection)), 0.00035);
    vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0));
    float visible = 0.0;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float depth = texture(uShadowMap, projected.xy + vec2(x, y) * texel).r;
            visible += projected.z - bias <= depth ? 1.0 : 0.0;
        }
    }
    vec3 transmission = uColoredTransmissionShadowsEnabled
        ? texture(uTransmissionShadowMap, projected.xy).rgb
        : vec3(1.0);
    return transmission * (visible / 9.0);
}

vec3 causticRadiance() {
    if (!uCausticsEnabled) return vec3(0.0);
    vec3 projected = lightProjectionCoordinates();
    if (projected.z > 1.0 || projected.z < 0.0
        || any(lessThan(projected.xy, vec2(0.0)))
        || any(greaterThan(projected.xy, vec2(1.0)))) return vec3(0.0);
    return texture(uCausticsMap, projected.xy).rgb;
}

float estimateSurfaceNormalThickness(vec3 geometricNormal, vec3 viewDirection) {
    if (uThicknessFactor <= 0.0) return 0.0;
    float thicknessTextureValue = uHasThicknessTexture
        ? texture(uThicknessTexture, vTexCoord0).g
        : 1.0;
    float bakedThickness = uThicknessFactor
        * thicknessTextureValue
        * uVolumeThicknessScale;
    if (!uGeometricThicknessEnabled || !uHasGlassBackfaceData) {
        return bakedThickness;
    }

    vec2 framebufferSize = vec2(textureSize(uGlassFrontfaceDepthTexture, 0));
    vec2 screenUv = gl_FragCoord.xy / max(framebufferSize, vec2(1.0));
    float frontfaceViewDepth = texture(uGlassFrontfaceDepthTexture, screenUv).r;
    float backfaceViewDepth = texture(uGlassBackfaceDepthTexture, screenUv).r;
    uint pairedObjectId = texture(uGlassObjectIdTexture, screenUv).r;
    float viewDepthSpan = backfaceViewDepth - frontfaceViewDepth;
    if (frontfaceViewDepth >= 1.0e19 || backfaceViewDepth <= 0.0
        || viewDepthSpan <= 0.0001 || pairedObjectId != uint(max(uGlassObjectId, 0))) {
        return bakedThickness;
    }

    vec3 cameraRay = -viewDirection;
    float cameraForwardProjection = abs((uView * vec4(cameraRay, 0.0)).z);
    float viewRayThickness = viewDepthSpan / max(cameraForwardProjection, 0.05);
    float normalProjection = abs(dot(cameraRay, normalize(geometricNormal)));
    return max(viewRayThickness * normalProjection * uVolumeThicknessScale, 0.0);
}

bool sampleExitSurface(vec2 uv, out vec3 exitNormal) {
    vec4 encodedNormal = texture(uGlassExitNormalTexture, uv);
    uint pairedObjectId = texture(uGlassObjectIdTexture, uv).r;
    if (encodedNormal.a < 0.5 || pairedObjectId != uint(max(uGlassObjectId, 0))) {
        exitNormal = vec3(0.0);
        return false;
    }
    exitNormal = normalize(encodedNormal.xyz * 2.0 - 1.0);
    return true;
}

vec3 sampleTransmittedRadiance(
    vec3 normal,
    vec3 viewDirection,
    float ior,
    float roughness,
    float surfaceNormalThickness,
    out bool totalInternalReflection,
    out bool screenSpaceHit,
    out vec2 refractedUv,
    out float volumePathLength,
    out bool exitSurfaceHit,
    out vec3 sampledExitNormal
) {
    screenSpaceHit = false;
    refractedUv = vec2(-1.0);
    volumePathLength = 0.0;
    exitSurfaceHit = false;
    sampledExitNormal = vec3(0.0);
    vec3 incident = -viewDirection;
    vec3 geometricNormal = normalize(vWorldNormal);
    bool entering = dot(incident, geometricNormal) < 0.0;
    vec3 refractionNormal = dot(incident, normal) < 0.0 ? normal : -normal;
    float eta = entering ? 1.0 / ior : ior;
    vec3 refractedDirection = refract(incident, refractionNormal, eta);
    totalInternalReflection = dot(refractedDirection, refractedDirection) < 0.000001;
    if (totalInternalReflection) {
        vec3 reflectedDirection = reflect(incident, refractionNormal);
        return textureLod(
            uPrefilteredEnvironmentMap,
            reflectedDirection,
            roughness * uEnvironmentMaxMip
        ).rgb * uEnvironmentIntensity;
    }

    if (surfaceNormalThickness > 0.0) {
        float normalDistance = max(abs(dot(refractedDirection, refractionNormal)), 0.15);
        volumePathLength = surfaceNormalThickness / normalDistance;
    }

    vec3 radianceOrigin = vWorldPosition;
    vec3 radianceDirection = normalize(refractedDirection);
    if (uTwoInterfaceRefractionEnabled && uGeometricThicknessEnabled
        && uHasGlassBackfaceData && surfaceNormalThickness > 0.0) {
        int exitStepCount = clamp(uRefractionSteps, 4, 32);
        vec2 entryUv = gl_FragCoord.xy
            / max(vec2(textureSize(uGlassFrontfaceDepthTexture, 0)), vec2(1.0));
        float cameraDepthSpan = max(
            texture(uGlassBackfaceDepthTexture, entryUv).r
                - texture(uGlassFrontfaceDepthTexture, entryUv).r,
            0.0
        );
        float cameraForwardProjection = abs(
            (uView * vec4(normalize(-viewDirection), 0.0)).z
        );
        float cameraRayPath = cameraDepthSpan / max(cameraForwardProjection, 0.05);
        float exitTraceDistance = max(
            max(volumePathLength * 1.6, cameraRayPath * 1.35),
            uRefractionScale
        );
        float previousProgress = 0.0;
        float previousDepthDifference = -cameraDepthSpan;
        for (int stepIndex = 1; stepIndex <= 32; ++stepIndex) {
            if (stepIndex > exitStepCount) break;
            float rayProgress = float(stepIndex) / float(exitStepCount);
            vec3 samplePosition = vWorldPosition
                + refractedDirection * exitTraceDistance * rayProgress;
            vec4 sampleViewPosition = uView * vec4(samplePosition, 1.0);
            vec4 sampleClip = uProjection * sampleViewPosition;
            if (sampleClip.w <= 0.0) break;
            vec2 sampleUv = sampleClip.xy / sampleClip.w * 0.5 + 0.5;
            if (any(lessThan(sampleUv, vec2(0.0)))
                || any(greaterThan(sampleUv, vec2(1.0)))) break;

            float exitViewDepth = texture(uGlassBackfaceDepthTexture, sampleUv).r;
            float rayViewDepth = -sampleViewPosition.z;
            float depthTolerance = max(exitViewDepth * 0.0015, 0.0025);
            float depthDifference = rayViewDepth - exitViewDepth;
            vec3 exitNormal = vec3(0.0);
            if (exitViewDepth > 0.0
                && depthDifference >= -depthTolerance
                && sampleExitSurface(sampleUv, exitNormal)) {
                float crossingWeight = clamp(
                    -previousDepthDifference
                        / max(depthDifference - previousDepthDifference, 0.00001),
                    0.0,
                    1.0
                );
                float crossingProgress = mix(
                    previousProgress,
                    rayProgress,
                    crossingWeight
                );
                vec3 crossingPosition = vWorldPosition
                    + refractedDirection * exitTraceDistance * crossingProgress;
                vec4 crossingClip = uProjection * uView * vec4(crossingPosition, 1.0);
                vec2 crossingUv = crossingClip.xy / crossingClip.w * 0.5 + 0.5;
                vec3 refinedNormal = vec3(0.0);
                if (sampleExitSurface(crossingUv, refinedNormal)) {
                    exitNormal = refinedNormal;
                }
                exitSurfaceHit = true;
                sampledExitNormal = exitNormal;
                radianceOrigin = crossingPosition;
                volumePathLength = length(crossingPosition - vWorldPosition);
                vec3 exitInterfaceNormal = dot(refractedDirection, exitNormal) > 0.0
                    ? -exitNormal
                    : exitNormal;
                vec3 exitedDirection = refract(
                    normalize(refractedDirection),
                    exitInterfaceNormal,
                    ior
                );
                if (dot(exitedDirection, exitedDirection) > 0.000001) {
                    radianceDirection = normalize(exitedDirection);
                } else {
                    totalInternalReflection = true;
                    radianceDirection = normalize(reflect(
                        refractedDirection,
                        exitInterfaceNormal
                    ));
                }
                break;
            }
            if (exitViewDepth > 0.0) {
                previousProgress = rayProgress;
                previousDepthDifference = depthDifference;
            }
        }
    }

    if (!exitSurfaceHit && uGeometricThicknessEnabled && uHasGlassBackfaceData
        && surfaceNormalThickness > 0.0) {
        vec3 approximateExitInterfaceNormal = refractionNormal;
        vec3 exitedDirection = refract(
            normalize(refractedDirection),
            approximateExitInterfaceNormal,
            ior
        );
        if (dot(exitedDirection, exitedDirection) > 0.000001) {
            radianceDirection = normalize(exitedDirection);
            radianceOrigin = vWorldPosition + refractedDirection * volumePathLength;
        }
    }

    int stepCount = clamp(uRefractionSteps, 4, 32);
    vec2 lastValidUv = vec2(-1.0);
    for (int stepIndex = 1; stepIndex <= 32; ++stepIndex) {
        if (stepIndex > stepCount) break;
        float rayProgress = float(stepIndex) / float(stepCount);
        float traceDistance = max(uRefractionScale * 4.0, 1.5);
        vec3 samplePosition = radianceOrigin
            + radianceDirection * traceDistance * rayProgress;
        vec4 refractedClip = uProjection * uView * vec4(samplePosition, 1.0);
        if (refractedClip.w <= 0.0) break;

        vec2 candidateUv = refractedClip.xy / refractedClip.w * 0.5 + 0.5;
        bool insideScreen = all(greaterThanEqual(candidateUv, vec2(0.0)))
            && all(lessThanEqual(candidateUv, vec2(1.0)));
        if (!insideScreen) break;

        float opaqueDepth = texture(uSceneDepthTexture, candidateUv).r;
        float rayDepth = refractedClip.z / refractedClip.w * 0.5 + 0.5;
        bool isBehindGlass = opaqueDepth > gl_FragCoord.z + 0.0001;
        if (isBehindGlass) {
            lastValidUv = candidateUv;
            bool crossedOpaqueSurface = opaqueDepth < 0.99999
                && rayDepth >= opaqueDepth - 0.0015;
            if (crossedOpaqueSurface) {
                screenSpaceHit = true;
                refractedUv = candidateUv;
                return textureLod(
                    uOpaqueColorTexture,
                    candidateUv,
                    roughness * uOpaqueColorMaxMip
                ).rgb;
            }
        }
    }

    if (lastValidUv.x >= 0.0) {
        screenSpaceHit = true;
        refractedUv = lastValidUv;
        return textureLod(
            uOpaqueColorTexture,
            lastValidUv,
            roughness * uOpaqueColorMaxMip
        ).rgb;
    }

    return textureLod(
        uPrefilteredEnvironmentMap,
        radianceDirection,
        roughness * uEnvironmentMaxMip
    ).rgb * uEnvironmentIntensity;
}

void main() {
    vec3 normal = normalize(vWorldNormal);
    if (uDoubleSided && !gl_FrontFacing) {
        normal = -normal;
    }
    if (uNormalMappingEnabled && uHasNormalTexture && abs(vWorldTangent.w) > 0.5) {
        vec3 tangent = normalize(vWorldTangent.xyz - normal * dot(normal, vWorldTangent.xyz));
        vec3 bitangent = normalize(cross(normal, tangent)) * vWorldTangent.w;
        vec3 tangentNormal = texture(uNormalTexture, vTexCoord0).xyz * 2.0 - 1.0;
        normal = normalize(mat3(tangent, bitangent, normal) * tangentNormal);
    }

    vec4 baseColorSample = uBaseColor * texture(uBaseColorTexture, vTexCoord0);
    if (uAlphaMode == 1 && baseColorSample.a < uAlphaCutoff) {
        discard;
    }
    float outputAlpha = uAlphaMode == 2 ? baseColorSample.a : 1.0;
    vec3 albedo = baseColorSample.rgb;
    vec2 materialSample = uHasMetallicRoughnessTexture
        ? texture(uMetallicRoughnessTexture, vTexCoord0).gb
        : vec2(1.0);
    float roughness = clamp(uRoughnessFactor * materialSample.x, 0.04, 1.0);
    if (uVolumeGlassOverrideEnabled && uTransmissionFactor > 0.0) {
        roughness = clamp(uVolumeGlassRoughness, 0.04, 1.0);
    }
    float metallic = clamp(uMetallicFactor * materialSample.y, 0.0, 1.0);
    vec3 lightDirection = normalize(-uLightDirection);
    vec3 viewDirection = normalize(uCameraPosition - vWorldPosition);
    vec3 halfDirection = normalize(lightDirection + viewDirection);
    float nDotL = max(dot(normal, lightDirection), 0.0);
    float nDotV = max(dot(normal, viewDirection), 0.001);
    vec3 visibility = shadowVisibility(normal, lightDirection);
    vec3 caustics = causticRadiance();

    if (uGlassDebugView == 11) {
        fragmentColor = vec4(caustics, 1.0);
        return;
    }
    if (uGlassDebugView == 12) {
        fragmentColor = vec4(visibility, 1.0);
        return;
    }

    if (!uPbrEnabled) {
        float specular = nDotL > 0.0
            ? pow(max(dot(normal, halfDirection), 0.0), max(uShininess, 1.0))
            : 0.0;
        fragmentColor = vec4(
            uAmbientStrength * albedo
            + visibility * (uDiffuseStrength * nDotL * albedo + uSpecularStrength * specular)
            + caustics * albedo,
            outputAlpha
        );
        return;
    }

    float ior = max(
        uIndexOfRefractionOverride > 0.0
            ? uIndexOfRefractionOverride
            : uIndexOfRefraction,
        1.0
    );
    float dielectricF0 = pow((ior - 1.0) / (ior + 1.0), 2.0);
    vec3 f0 = mix(vec3(dielectricF0), albedo, metallic);
    float distribution = distributionGGX(normal, halfDirection, roughness);
    float geometry = geometrySchlickGGX(nDotV, roughness)
        * geometrySchlickGGX(max(dot(normal, lightDirection), 0.0), roughness);
    vec3 fresnel = fresnelSchlick(max(dot(halfDirection, viewDirection), 0.0), f0);
    vec3 specular = distribution * geometry * fresnel
        / max(4.0 * nDotV * max(nDotL, 0.001), 0.001);
    vec3 diffuseWeight = (vec3(1.0) - fresnel) * (1.0 - metallic);
    vec3 directDiffuse = diffuseWeight * albedo / PI * nDotL * uDiffuseStrength;
    vec3 directSpecular = specular * nDotL * uDiffuseStrength;

    vec3 ambientDiffuse = uAmbientStrength * albedo;
    vec3 ambientSpecular = vec3(0.0);
    if (uIblEnabled) {
        vec3 reflection = reflect(-viewDirection, normal);
        vec3 diffuseEnvironment = texture(uIrradianceMap, normal).rgb;
        vec3 specularEnvironment = textureLod(
            uPrefilteredEnvironmentMap, reflection, roughness * uEnvironmentMaxMip
        ).rgb;
        vec3 environmentFresnel = fresnelSchlick(nDotV, f0);
        vec2 environmentBrdf = texture(uBrdfLut, vec2(nDotV, roughness)).rg;
        ambientDiffuse = (vec3(1.0) - environmentFresnel)
            * (1.0 - metallic)
            * albedo
            * diffuseEnvironment
            * uEnvironmentIntensity;
        ambientSpecular = specularEnvironment
            * (environmentFresnel * environmentBrdf.x + environmentBrdf.y)
            * uEnvironmentIntensity;
    }

    vec3 diffuseLighting = ambientDiffuse + visibility * directDiffuse + caustics * albedo;
    vec3 specularLighting = ambientSpecular + visibility * directSpecular;
    float transmission = uTransmissionEnabled
        ? clamp(
            (uVolumeGlassOverrideEnabled && uTransmissionFactor > 0.0
                ? uVolumeGlassTransmission
                : uTransmissionFactor) * (1.0 - metallic),
            0.0,
            1.0
        )
        : 0.0;
    if (transmission > 0.0) {
        if (uHasGlassBackfaceData) {
            vec2 framebufferSize = vec2(textureSize(uGlassFrontfaceDepthTexture, 0));
            vec2 screenUv = gl_FragCoord.xy / max(framebufferSize, vec2(1.0));
            float nearestViewDepth = texture(uGlassFrontfaceDepthTexture, screenUv).r;
            uint pairedObjectId = texture(uGlassObjectIdTexture, screenUv).r;
            float fragmentViewDepth = -(uView * vec4(vWorldPosition, 1.0)).z;
            float depthTolerance = max(nearestViewDepth * 0.0002, 0.0005);
            if (pairedObjectId == uint(max(uGlassObjectId, 0))
                && nearestViewDepth < 1.0e19
                && fragmentViewDepth > nearestViewDepth + depthTolerance) {
                discard;
            }
        }
        float surfaceNormalThickness = estimateSurfaceNormalThickness(
            normalize(vWorldNormal),
            viewDirection
        );
        bool totalInternalReflection = false;
        bool screenSpaceHit = false;
        vec2 refractedUv = vec2(-1.0);
        float volumePathLength = 0.0;
        bool exitSurfaceHit = false;
        vec3 exitSurfaceNormal = vec3(0.0);
        vec3 transmittedRadiance = sampleTransmittedRadiance(
            normal,
            viewDirection,
            ior,
            roughness,
            surfaceNormalThickness,
            totalInternalReflection,
            screenSpaceHit,
            refractedUv,
            volumePathLength,
            exitSurfaceHit,
            exitSurfaceNormal
        );
        vec3 rgbPathLengths = vec3(volumePathLength);
        float dispersion = uDispersionStrength > 0.0
            ? uDispersionStrength
            : max(uMaterialDispersion, 0.0);
        if (dispersion > 0.0001 && uThicknessFactor > 0.0) {
            float halfSpread = (ior - 1.0) * 0.025 * dispersion;
            vec3 channelIors = max(
                vec3(ior - halfSpread, ior, ior + halfSpread),
                vec3(1.0)
            );
            bool redTir = false;
            bool redHit = false;
            vec2 redUv = vec2(-1.0);
            float redPathLength = 0.0;
            bool redExitHit = false;
            vec3 redExitNormal = vec3(0.0);
            vec3 redRadiance = sampleTransmittedRadiance(
                normal,
                viewDirection,
                channelIors.r,
                roughness,
                surfaceNormalThickness,
                redTir,
                redHit,
                redUv,
                redPathLength,
                redExitHit,
                redExitNormal
            );
            bool blueTir = false;
            bool blueHit = false;
            vec2 blueUv = vec2(-1.0);
            float bluePathLength = 0.0;
            bool blueExitHit = false;
            vec3 blueExitNormal = vec3(0.0);
            vec3 blueRadiance = sampleTransmittedRadiance(
                normal,
                viewDirection,
                channelIors.b,
                roughness,
                surfaceNormalThickness,
                blueTir,
                blueHit,
                blueUv,
                bluePathLength,
                blueExitHit,
                blueExitNormal
            );
            transmittedRadiance = vec3(
                redRadiance.r,
                transmittedRadiance.g,
                blueRadiance.b
            );
            rgbPathLengths = vec3(redPathLength, volumePathLength, bluePathLength);
        }
        vec3 volumeTransmittance = vec3(1.0);
        vec3 attenuationColor = uVolumeGlassOverrideEnabled
            ? uVolumeGlassAttenuationColor
            : uAttenuationColor;
        float attenuationDistance = uVolumeGlassOverrideEnabled
            ? uVolumeGlassAttenuationDistance
            : uAttenuationDistance;
        if (uThicknessFactor > 0.0 && attenuationDistance < 1.0e19) {
            vec3 safeAttenuationColor = clamp(
                attenuationColor,
                vec3(0.0001),
                vec3(1.0)
            );
            volumeTransmittance = pow(
                safeAttenuationColor,
                rgbPathLengths / max(attenuationDistance, 0.0001)
            );
        }
        vec3 viewFresnel = totalInternalReflection
            ? vec3(1.0)
            : fresnelSchlick(nDotV, f0);
        vec3 transmittedLighting = transmittedRadiance
            * volumeTransmittance
            * albedo
            * (vec3(1.0) - viewFresnel);
        if (uGlassDebugView == 1) {
            fragmentColor = vec4(specularLighting, 1.0);
            return;
        }
        if (uGlassDebugView == 2) {
            fragmentColor = vec4(transmittedRadiance * volumeTransmittance * albedo, 1.0);
            return;
        }
        if (uGlassDebugView == 3) {
            float normalizedIor = clamp((ior - 1.0) / 1.5, 0.0, 1.0);
            vec3 iorColor = vec3(
                normalizedIor,
                1.0 - abs(normalizedIor * 2.0 - 1.0),
                1.0 - normalizedIor
            );
            fragmentColor = vec4(iorColor, 1.0);
            return;
        }
        if (uGlassDebugView == 4) {
            fragmentColor = vec4(
                screenSpaceHit ? vec3(refractedUv, 1.0) : vec3(1.0, 0.0, 1.0),
                1.0
            );
            return;
        }
        if (uGlassDebugView == 5) {
            fragmentColor = vec4(vec3(1.0 - exp(-volumePathLength)), 1.0);
            return;
        }
        if (uGlassDebugView == 6) {
            fragmentColor = vec4(volumeTransmittance, 1.0);
            return;
        }
        if (uGlassDebugView == 7) {
            fragmentColor = vec4(transmittedRadiance, 1.0);
            return;
        }
        if (uGlassDebugView == 8) {
            vec2 framebufferSize = vec2(textureSize(uGlassFrontfaceDepthTexture, 0));
            vec2 screenUv = gl_FragCoord.xy / max(framebufferSize, vec2(1.0));
            float frontfaceViewDepth = texture(uGlassFrontfaceDepthTexture, screenUv).r;
            float backfaceViewDepth = texture(uGlassBackfaceDepthTexture, screenUv).r;
            float depthSpan = max(backfaceViewDepth - frontfaceViewDepth, 0.0);
            fragmentColor = depthSpan > 0.0001
                ? vec4(1.0 - exp(-depthSpan), 1.0, 0.0, 1.0)
                : vec4(1.0, 0.0, 1.0, 1.0);
            return;
        }
        if (uGlassDebugView == 9) {
            fragmentColor = exitSurfaceHit
                ? vec4(exitSurfaceNormal * 0.5 + 0.5, 1.0)
                : vec4(1.0, 0.0, 1.0, 1.0);
            return;
        }
        if (uGlassDebugView == 10) {
            float id = float(max(uGlassObjectId, 0));
            vec3 idColor = fract(vec3(0.1031, 0.11369, 0.13787) * id);
            fragmentColor = vec4(idColor, 1.0);
            return;
        }
        fragmentColor = vec4(
            specularLighting + mix(diffuseLighting, transmittedLighting, transmission),
            1.0
        );
        return;
    }

    fragmentColor = vec4(diffuseLighting + specularLighting, outputAlpha);
}
