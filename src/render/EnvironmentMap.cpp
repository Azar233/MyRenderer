#include "render/EnvironmentMap.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <glad/gl.h>
#include <glm/geometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <stb_image.h>

#include "render/Shader.h"

namespace {

constexpr float pi = 3.14159265359f;

struct EquirectangularHdr {
    int width{0};
    int height{0};
    std::vector<float> pixels;

    bool valid() const {
        return width > 0 && height > 0
            && pixels.size() == static_cast<std::size_t>(width * height * 3);
    }
};

glm::vec3 faceDirection(int face, float u, float v) {
    switch (face) {
    case 0: return glm::normalize(glm::vec3(1.0f, -v, -u));
    case 1: return glm::normalize(glm::vec3(-1.0f, -v, u));
    case 2: return glm::normalize(glm::vec3(u, 1.0f, v));
    case 3: return glm::normalize(glm::vec3(u, -1.0f, -v));
    case 4: return glm::normalize(glm::vec3(u, -v, 1.0f));
    default: return glm::normalize(glm::vec3(-u, -v, -1.0f));
    }
}

glm::vec3 proceduralStudioRadiance(const glm::vec3& direction) {
    const float horizon = std::clamp(direction.y * 0.5f + 0.5f, 0.0f, 1.0f);
    const glm::vec3 ground(0.018f, 0.014f, 0.012f);
    const glm::vec3 horizonColor(0.24f, 0.30f, 0.42f);
    const glm::vec3 zenith(0.025f, 0.055f, 0.14f);
    glm::vec3 color = direction.y < 0.0f
        ? ground * (0.65f + 0.35f * horizon)
        : horizonColor * (1.0f - horizon) + zenith * horizon;
    const glm::vec3 keyDirection = glm::normalize(glm::vec3(0.58f, 0.45f, 0.68f));
    const glm::vec3 rimDirection = glm::normalize(glm::vec3(-0.72f, 0.18f, 0.66f));
    const float key = std::pow(std::max(glm::dot(direction, keyDirection), 0.0f), 180.0f);
    const float rim = std::pow(std::max(glm::dot(direction, rimDirection), 0.0f), 260.0f);
    return color
        + glm::vec3(10.0f, 8.2f, 6.4f) * key
        + glm::vec3(3.2f, 5.4f, 8.0f) * rim;
}

glm::vec3 hdrPixel(const EquirectangularHdr& image, int x, int y) {
    x = (x % image.width + image.width) % image.width;
    y = std::clamp(y, 0, image.height - 1);
    const std::size_t offset = static_cast<std::size_t>((y * image.width + x) * 3);
    return glm::vec3(
        image.pixels[offset],
        image.pixels[offset + 1U],
        image.pixels[offset + 2U]
    );
}

glm::vec3 sampleEquirectangular(
    const EquirectangularHdr& image,
    const glm::vec3& direction
) {
    if (!image.valid()) return proceduralStudioRadiance(direction);
    const glm::vec3 unitDirection = glm::normalize(direction);
    const float u = std::atan2(unitDirection.z, unitDirection.x) / (2.0f * pi) + 0.5f;
    const float v = std::acos(std::clamp(unitDirection.y, -1.0f, 1.0f)) / pi;
    const float x = u * static_cast<float>(image.width) - 0.5f;
    const float y = v * static_cast<float>(image.height) - 0.5f;
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const float tx = x - std::floor(x);
    const float ty = y - std::floor(y);
    const glm::vec3 top = glm::mix(hdrPixel(image, x0, y0), hdrPixel(image, x0 + 1, y0), tx);
    const glm::vec3 bottom = glm::mix(
        hdrPixel(image, x0, y0 + 1),
        hdrPixel(image, x0 + 1, y0 + 1),
        tx
    );
    return glm::mix(top, bottom, ty);
}

float radicalInverseVdc(std::uint32_t bits) {
    bits = (bits << 16U) | (bits >> 16U);
    bits = ((bits & 0x55555555U) << 1U) | ((bits & 0xAAAAAAAAU) >> 1U);
    bits = ((bits & 0x33333333U) << 2U) | ((bits & 0xCCCCCCCCU) >> 2U);
    bits = ((bits & 0x0F0F0F0FU) << 4U) | ((bits & 0xF0F0F0F0U) >> 4U);
    bits = ((bits & 0x00FF00FFU) << 8U) | ((bits & 0xFF00FF00U) >> 8U);
    return static_cast<float>(bits) * 2.3283064365386963e-10f;
}

glm::vec2 hammersley(std::uint32_t index, std::uint32_t count) {
    return glm::vec2(static_cast<float>(index) / static_cast<float>(count), radicalInverseVdc(index));
}

void tangentBasis(const glm::vec3& normal, glm::vec3& tangent, glm::vec3& bitangent) {
    const glm::vec3 up = std::abs(normal.z) < 0.999f
        ? glm::vec3(0.0f, 0.0f, 1.0f)
        : glm::vec3(1.0f, 0.0f, 0.0f);
    tangent = glm::normalize(glm::cross(up, normal));
    bitangent = glm::cross(normal, tangent);
}

glm::vec3 cosineSampleHemisphere(const glm::vec2& xi, const glm::vec3& normal) {
    const float radius = std::sqrt(xi.x);
    const float angle = 2.0f * pi * xi.y;
    const glm::vec3 local(
        radius * std::cos(angle),
        radius * std::sin(angle),
        std::sqrt(std::max(1.0f - xi.x, 0.0f))
    );
    glm::vec3 tangent;
    glm::vec3 bitangent;
    tangentBasis(normal, tangent, bitangent);
    return glm::normalize(tangent * local.x + bitangent * local.y + normal * local.z);
}

glm::vec3 importanceSampleGgx(
    const glm::vec2& xi,
    const glm::vec3& normal,
    float roughness
) {
    const float alpha = roughness * roughness;
    const float phi = 2.0f * pi * xi.x;
    const float cosTheta = std::sqrt(
        (1.0f - xi.y) / std::max(1.0f + (alpha * alpha - 1.0f) * xi.y, 0.0001f)
    );
    const float sinTheta = std::sqrt(std::max(1.0f - cosTheta * cosTheta, 0.0f));
    const glm::vec3 local(std::cos(phi) * sinTheta, std::sin(phi) * sinTheta, cosTheta);
    glm::vec3 tangent;
    glm::vec3 bitangent;
    tangentBasis(normal, tangent, bitangent);
    return glm::normalize(tangent * local.x + bitangent * local.y + normal * local.z);
}

float geometrySchlickGgxIbl(float nDotV, float roughness) {
    const float k = roughness * roughness * 0.5f;
    return nDotV / std::max(nDotV * (1.0f - k) + k, 0.0001f);
}

glm::vec2 integrateBrdf(float nDotV, float roughness) {
    constexpr std::uint32_t sampleCount = 128U;
    const glm::vec3 normal(0.0f, 0.0f, 1.0f);
    const glm::vec3 view(std::sqrt(std::max(1.0f - nDotV * nDotV, 0.0f)), 0.0f, nDotV);
    float scale = 0.0f;
    float bias = 0.0f;
    for (std::uint32_t index = 0; index < sampleCount; ++index) {
        const glm::vec3 halfDirection = importanceSampleGgx(
            hammersley(index, sampleCount),
            normal,
            roughness
        );
        const glm::vec3 light = glm::normalize(2.0f * glm::dot(view, halfDirection) * halfDirection - view);
        const float nDotL = std::max(light.z, 0.0f);
        const float nDotH = std::max(halfDirection.z, 0.0f);
        const float vDotH = std::max(glm::dot(view, halfDirection), 0.0f);
        if (nDotL <= 0.0f) continue;
        const float geometry = geometrySchlickGgxIbl(nDotV, roughness)
            * geometrySchlickGgxIbl(nDotL, roughness);
        const float visibility = geometry * vDotH / std::max(nDotH * nDotV, 0.0001f);
        const float fresnel = std::pow(1.0f - vDotH, 5.0f);
        scale += (1.0f - fresnel) * visibility;
        bias += fresnel * visibility;
    }
    return glm::vec2(scale, bias) / static_cast<float>(sampleCount);
}

} // namespace

EnvironmentMap::EnvironmentMap(
    const std::filesystem::path& vertexShaderPath,
    const std::filesystem::path& fragmentShaderPath
) : shader_(std::make_unique<Shader>(vertexShaderPath, fragmentShaderPath)) {
    EquirectangularHdr source;
    const std::filesystem::path hdriPath = vertexShaderPath.parent_path().parent_path()
        / "assets" / "environments" / "delta_2_2k.hdr";
    int components = 0;
    float* loadedPixels = stbi_loadf(
        hdriPath.string().c_str(),
        &source.width,
        &source.height,
        &components,
        3
    );
    if (loadedPixels != nullptr) {
        source.pixels.assign(
            loadedPixels,
            loadedPixels + static_cast<std::ptrdiff_t>(source.width * source.height * 3)
        );
        stbi_image_free(loadedPixels);
        const bool finiteRadiance = std::all_of(
            source.pixels.begin(),
            source.pixels.end(),
            [](float value) { return std::isfinite(value) && value >= 0.0f; }
        );
        const float peakRadiance = source.pixels.empty()
            ? 0.0f
            : *std::max_element(source.pixels.begin(), source.pixels.end());
        if (!finiteRadiance || peakRadiance <= 0.0f) {
            source.pixels.clear();
            source.width = 0;
            source.height = 0;
        }
    } else {
        source.width = 0;
        source.height = 0;
    }

    const auto radiance = [&source](const glm::vec3& direction) {
        return sampleEquirectangular(source, direction);
    };
    const int size = radianceFaceSize_;
    maximumMipLevel_ = static_cast<int>(std::log2(prefilteredFaceSize_));
    glGenTextures(1, &texture_);
    glBindTexture(GL_TEXTURE_CUBE_MAP, texture_);
    std::vector<float> pixels(static_cast<std::size_t>(size * size * 3));
    for (int face = 0; face < 6; ++face) {
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                const float u = (2.0f * (static_cast<float>(x) + 0.5f) / size) - 1.0f;
                const float v = (2.0f * (static_cast<float>(y) + 0.5f) / size) - 1.0f;
                const glm::vec3 color = radiance(faceDirection(face, u, v));
                const std::size_t offset = static_cast<std::size_t>((y * size + x) * 3);
                pixels[offset] = color.r;
                pixels[offset + 1U] = color.g;
                pixels[offset + 2U] = color.b;
            }
        }
        glTexImage2D(
            GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
            0,
            GL_RGB16F,
            size,
            size,
            0,
            GL_RGB,
            GL_FLOAT,
            pixels.data()
        );
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

    constexpr int irradianceSize = 16;
    constexpr std::uint32_t irradianceSamples = 128U;
    glGenTextures(1, &irradianceTexture_);
    glBindTexture(GL_TEXTURE_CUBE_MAP, irradianceTexture_);
    std::vector<float> irradiancePixels(
        static_cast<std::size_t>(irradianceSize * irradianceSize * 3)
    );
    for (int face = 0; face < 6; ++face) {
        for (int y = 0; y < irradianceSize; ++y) {
            for (int x = 0; x < irradianceSize; ++x) {
                const float u = 2.0f * (static_cast<float>(x) + 0.5f) / irradianceSize - 1.0f;
                const float v = 2.0f * (static_cast<float>(y) + 0.5f) / irradianceSize - 1.0f;
                const glm::vec3 normal = faceDirection(face, u, v);
                glm::vec3 sum(0.0f);
                for (std::uint32_t sample = 0; sample < irradianceSamples; ++sample) {
                    sum += radiance(cosineSampleHemisphere(
                        hammersley(sample, irradianceSamples),
                        normal
                    ));
                }
                const glm::vec3 color = sum / static_cast<float>(irradianceSamples);
                const std::size_t offset = static_cast<std::size_t>((y * irradianceSize + x) * 3);
                irradiancePixels[offset] = color.r;
                irradiancePixels[offset + 1U] = color.g;
                irradiancePixels[offset + 2U] = color.b;
            }
        }
        glTexImage2D(
            GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
            0,
            GL_RGB16F,
            irradianceSize,
            irradianceSize,
            0,
            GL_RGB,
            GL_FLOAT,
            irradiancePixels.data()
        );
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    constexpr std::uint32_t prefilterSamples = 96U;
    glGenTextures(1, &prefilteredTexture_);
    glBindTexture(GL_TEXTURE_CUBE_MAP, prefilteredTexture_);
    for (int mip = 0; mip <= maximumMipLevel_; ++mip) {
        const int mipSize = std::max(prefilteredFaceSize_ >> mip, 1);
        const float roughness = maximumMipLevel_ > 0
            ? static_cast<float>(mip) / static_cast<float>(maximumMipLevel_)
            : 0.0f;
        std::vector<float> mipPixels(static_cast<std::size_t>(mipSize * mipSize * 3));
        for (int face = 0; face < 6; ++face) {
            for (int y = 0; y < mipSize; ++y) {
                for (int x = 0; x < mipSize; ++x) {
                    const float u = 2.0f * (static_cast<float>(x) + 0.5f) / mipSize - 1.0f;
                    const float v = 2.0f * (static_cast<float>(y) + 0.5f) / mipSize - 1.0f;
                    const glm::vec3 normal = faceDirection(face, u, v);
                    glm::vec3 sum(0.0f);
                    float weight = 0.0f;
                    for (std::uint32_t sample = 0; sample < prefilterSamples; ++sample) {
                        const glm::vec3 halfDirection = importanceSampleGgx(
                            hammersley(sample, prefilterSamples),
                            normal,
                            std::max(roughness, 0.001f)
                        );
                        const glm::vec3 light = glm::normalize(
                            2.0f * glm::dot(normal, halfDirection) * halfDirection - normal
                        );
                        const float nDotL = std::max(glm::dot(normal, light), 0.0f);
                        if (nDotL > 0.0f) {
                            sum += radiance(light) * nDotL;
                            weight += nDotL;
                        }
                    }
                    const glm::vec3 color = sum / std::max(weight, 0.0001f);
                    const std::size_t offset = static_cast<std::size_t>((y * mipSize + x) * 3);
                    mipPixels[offset] = color.r;
                    mipPixels[offset + 1U] = color.g;
                    mipPixels[offset + 2U] = color.b;
                }
            }
            glTexImage2D(
                GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
                mip,
                GL_RGB16F,
                mipSize,
                mipSize,
                0,
                GL_RGB,
                GL_FLOAT,
                mipPixels.data()
            );
        }
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, maximumMipLevel_);

    constexpr int brdfSize = 64;
    std::vector<float> brdfPixels(static_cast<std::size_t>(brdfSize * brdfSize * 2));
    for (int y = 0; y < brdfSize; ++y) {
        const float roughness = (static_cast<float>(y) + 0.5f) / brdfSize;
        for (int x = 0; x < brdfSize; ++x) {
            const float nDotV = (static_cast<float>(x) + 0.5f) / brdfSize;
            const glm::vec2 integrated = integrateBrdf(nDotV, roughness);
            const std::size_t offset = static_cast<std::size_t>((y * brdfSize + x) * 2);
            brdfPixels[offset] = integrated.x;
            brdfPixels[offset + 1U] = integrated.y;
        }
    }
    glGenTextures(1, &brdfLutTexture_);
    glBindTexture(GL_TEXTURE_2D, brdfLutTexture_);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RG16F,
        brdfSize,
        brdfSize,
        0,
        GL_RG,
        GL_FLOAT,
        brdfPixels.data()
    );
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    glGenVertexArrays(1, &vertexArray_);
}

std::size_t EnvironmentMap::estimatedBytes() const {
    const auto cubemapTexels = [](int baseSize, int maximumMipLevel) {
        std::size_t pixels = 0U;
        int size = baseSize;
        for (int level = 0; level <= maximumMipLevel; ++level) {
            pixels += static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 6U;
            size = std::max(size / 2, 1);
        }
        return pixels;
    };
    const int radianceMaximumMipLevel = static_cast<int>(std::log2(radianceFaceSize_));
    const std::size_t radiance = cubemapTexels(
        radianceFaceSize_,
        radianceMaximumMipLevel
    ) * 6U;
    const std::size_t prefiltered = cubemapTexels(
        prefilteredFaceSize_,
        maximumMipLevel_
    ) * 6U;
    const std::size_t irradiance = 16U * 16U * 6U * 6U;
    const std::size_t brdf = 64U * 64U * 4U;
    return radiance + prefiltered + irradiance + brdf;
}

EnvironmentMap::~EnvironmentMap() {
    if (vertexArray_ != 0U) glDeleteVertexArrays(1, &vertexArray_);
    if (brdfLutTexture_ != 0U) glDeleteTextures(1, &brdfLutTexture_);
    if (prefilteredTexture_ != 0U) glDeleteTextures(1, &prefilteredTexture_);
    if (irradianceTexture_ != 0U) glDeleteTextures(1, &irradianceTexture_);
    if (texture_ != 0U) glDeleteTextures(1, &texture_);
}

void EnvironmentMap::bind(unsigned int unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_CUBE_MAP, texture_);
}

void EnvironmentMap::bindIrradiance(unsigned int unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_CUBE_MAP, irradianceTexture_);
}

void EnvironmentMap::bindPrefiltered(unsigned int unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_CUBE_MAP, prefilteredTexture_);
}

void EnvironmentMap::bindBrdfLut(unsigned int unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, brdfLutTexture_);
}

void EnvironmentMap::draw(
    const glm::mat4& inverseViewProjection,
    const glm::vec3& cameraPosition,
    float intensity
) const {
    shader_->use();
    shader_->setMat4("uInverseViewProjection", inverseViewProjection);
    shader_->setVec3("uCameraPosition", cameraPosition);
    shader_->setFloat("uEnvironmentIntensity", intensity);
    shader_->setInt("uEnvironmentMap", 0);
    bind(0U);
    glBindVertexArray(vertexArray_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}
