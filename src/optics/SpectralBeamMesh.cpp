#include "optics/SpectralBeamMesh.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <glm/geometric.hpp>

namespace {

constexpr float epsilon = 1.0e-6f;

glm::vec3 toWorld(const glm::vec2& point) {
    return glm::vec3(point, 0.0f);
}

glm::vec3 ribbonRight(
    const glm::vec3& from,
    const glm::vec3& to,
    const glm::vec3& cameraPosition
) {
    const glm::vec3 segment = to - from;
    if (glm::dot(segment, segment) <= epsilon) {
        return glm::vec3(0.0f, 1.0f, 0.0f);
    }
    const glm::vec3 direction = glm::normalize(segment);
    glm::vec3 viewDirection = cameraPosition - 0.5f * (from + to);
    if (glm::dot(viewDirection, viewDirection) <= epsilon) {
        viewDirection = glm::vec3(0.0f, 0.0f, 1.0f);
    } else {
        viewDirection = glm::normalize(viewDirection);
    }
    glm::vec3 right = glm::cross(direction, viewDirection);
    if (glm::dot(right, right) <= epsilon) {
        const glm::vec3 fallback = std::abs(direction.y) < 0.95f
            ? glm::vec3(0.0f, 1.0f, 0.0f)
            : glm::vec3(1.0f, 0.0f, 0.0f);
        right = glm::cross(direction, fallback);
    }
    return glm::normalize(right);
}

void appendTriangle(
    std::vector<SpectralBeamVertex>& vertices,
    const SpectralBeamVertex& a,
    const SpectralBeamVertex& b,
    const SpectralBeamVertex& c
) {
    vertices.push_back(a);
    vertices.push_back(b);
    vertices.push_back(c);
}

void appendRibbonSegment(
    std::vector<SpectralBeamVertex>& vertices,
    const glm::vec3& from,
    const glm::vec3& to,
    const glm::vec3& color,
    const glm::vec3& cameraPosition,
    float fromHalfWidth,
    float toHalfWidth
) {
    if (glm::length(to - from) <= epsilon) {
        return;
    }
    const glm::vec3 right = ribbonRight(from, to, cameraPosition);
    const SpectralBeamVertex fromLeft{from - right * fromHalfWidth, color, -1.0f};
    const SpectralBeamVertex fromRight{from + right * fromHalfWidth, color, 1.0f};
    const SpectralBeamVertex toLeft{to - right * toHalfWidth, color, -1.0f};
    const SpectralBeamVertex toRight{to + right * toHalfWidth, color, 1.0f};
    appendTriangle(vertices, fromLeft, toLeft, toRight);
    appendTriangle(vertices, fromLeft, toRight, fromRight);
}

glm::vec3 weightedColor(const PrismSpectralSample& sample, float sampleCount) {
    return sample.linearRgb * std::max(sample.normalizedEnergy * sampleCount, 0.0f);
}

glm::vec3 sevenBandColor(const PrismSpectralSample& sample, float sampleCount) {
    const float maximumChannel = std::max({
        sample.linearRgb.r,
        sample.linearRgb.g,
        sample.linearRgb.b,
        epsilon
    });
    return sample.linearRgb / maximumChannel
        * std::max(sample.normalizedEnergy * sampleCount, 0.0f);
}

std::vector<const PrismSpectralSample*> validSamples(const SpectralBeamData& spectrum) {
    std::vector<const PrismSpectralSample*> result;
    result.reserve(spectrum.samples.size());
    for (const PrismSpectralSample& sample : spectrum.samples) {
        if (sample.path.valid && sample.normalizedEnergy > 0.0f) {
            result.push_back(&sample);
        }
    }
    return result;
}

void appendContinuousFan(
    std::vector<SpectralBeamVertex>& vertices,
    const std::vector<const PrismSpectralSample*>& samples,
    bool internal,
    float outputLength,
    float startFraction,
    float endFraction,
    float startPadding,
    float endPadding,
    const glm::vec3& cameraPosition
) {
    if (samples.size() < 2U) {
        return;
    }
    const float sampleCount = static_cast<float>(samples.size());
    std::vector<glm::vec3> starts;
    std::vector<glm::vec3> ends;
    std::vector<glm::vec3> colors;
    starts.reserve(samples.size());
    ends.reserve(samples.size());
    colors.reserve(samples.size());
    for (const PrismSpectralSample* sample : samples) {
        const glm::vec3 entry = toWorld(sample->path.entryPoint);
        const glm::vec3 exit = toWorld(sample->path.exitPoint);
        starts.push_back(internal
            ? entry + (exit - entry) * startFraction
            : exit + toWorld(sample->path.exitDirection) * (outputLength * startFraction));
        ends.push_back(internal
            ? entry + (exit - entry) * endFraction
            : exit + toWorld(sample->path.exitDirection) * (outputLength * endFraction));
        colors.push_back(weightedColor(*sample, sampleCount));
    }

    std::vector<glm::vec3> startBoundaries(samples.size() + 1U);
    std::vector<glm::vec3> endBoundaries(samples.size() + 1U);
    std::vector<glm::vec3> boundaryColors(samples.size() + 1U);
    startBoundaries.front() = starts.front();
    endBoundaries.front() = ends.front();
    boundaryColors.front() = colors.front();
    for (std::size_t index = 1U; index < samples.size(); ++index) {
        startBoundaries[index] = 0.5f * (starts[index - 1U] + starts[index]);
        endBoundaries[index] = 0.5f * (ends[index - 1U] + ends[index]);
        boundaryColors[index] = 0.5f * (colors[index - 1U] + colors[index]);
    }
    startBoundaries.back() = starts.back();
    endBoundaries.back() = ends.back();
    boundaryColors.back() = colors.back();

    auto padOuterBoundaries = [&](
        std::vector<glm::vec3>& boundaries,
        const std::vector<glm::vec3>& centers,
        float padding,
        const std::vector<glm::vec3>& otherCenters
    ) {
        if (padding <= 0.0f) {
            return;
        }
        glm::vec3 bandDirection = centers.back() - centers.front();
        if (glm::dot(bandDirection, bandDirection) <= epsilon) {
            bandDirection = ribbonRight(
                centers.front(),
                otherCenters.front(),
                cameraPosition
            );
        } else {
            bandDirection = glm::normalize(bandDirection);
        }
        boundaries.front() -= bandDirection * padding;
        boundaries.back() += bandDirection * padding;
    };
    padOuterBoundaries(startBoundaries, starts, startPadding, ends);
    padOuterBoundaries(endBoundaries, ends, endPadding, starts);

    const float denominator = static_cast<float>(samples.size());
    for (std::size_t index = 0U; index < samples.size(); ++index) {
        const float edge0 = -1.0f + 2.0f * static_cast<float>(index) / denominator;
        const float edge1 = -1.0f + 2.0f * static_cast<float>(index + 1U) / denominator;
        const SpectralBeamVertex start0{
            startBoundaries[index], boundaryColors[index], edge0
        };
        const SpectralBeamVertex end0{
            endBoundaries[index], boundaryColors[index], edge0
        };
        const SpectralBeamVertex start1{
            startBoundaries[index + 1U], boundaryColors[index + 1U], edge1
        };
        const SpectralBeamVertex end1{
            endBoundaries[index + 1U], boundaryColors[index + 1U], edge1
        };
        appendTriangle(vertices, start0, end0, end1);
        appendTriangle(vertices, start0, end1, start1);
    }
}

void beginBatch(SpectralBeamMeshData& mesh, SpectralBeamGroup group) {
    mesh.batches.push_back(SpectralBeamBatch{group, mesh.vertices.size(), 0U});
}

void endBatch(SpectralBeamMeshData& mesh) {
    SpectralBeamBatch& batch = mesh.batches.back();
    batch.vertexCount = mesh.vertices.size() - batch.firstVertex;
    if (batch.vertexCount == 0U) {
        mesh.batches.pop_back();
    }
}

} // namespace

SpectralBeamMeshData buildSpectralBeamMesh(
    const SpectralBeamData& spectrum,
    const glm::vec3& cameraPosition,
    float outputLength,
    float beamWidth
) {
    SpectralBeamMeshData mesh;
    const std::vector<const PrismSpectralSample*> samples = validSamples(spectrum);
    if (samples.empty()) {
        return mesh;
    }

    const float safeWidth = std::max(beamWidth, 0.001f);
    const float halfWidth = 0.5f * safeWidth;
    const PrismSpectralSample* centerSample = *std::min_element(
        samples.begin(),
        samples.end(),
        [](const PrismSpectralSample* left, const PrismSpectralSample* right) {
            return std::abs(left->wavelengthNanometers - 550.0f)
                < std::abs(right->wavelengthNanometers - 550.0f);
        }
    );

    beginBatch(mesh, SpectralBeamGroup::Incident);
    appendRibbonSegment(
        mesh.vertices,
        toWorld(spectrum.incidentRay.origin),
        toWorld(centerSample->path.entryPoint),
        glm::vec3(1.0f),
        cameraPosition,
        halfWidth,
        halfWidth * 0.65f
    );
    endBatch(mesh);

    beginBatch(mesh, SpectralBeamGroup::Internal);
    const glm::vec3 internalEntry = toWorld(centerSample->path.entryPoint);
    const glm::vec3 internalExit = toWorld(centerSample->path.exitPoint);
    const glm::vec3 internalMidpoint = 0.5f * (internalEntry + internalExit);
    const glm::vec3 internalColor = glm::vec3(0.82f, 0.92f, 1.0f)
        * std::max(centerSample->path.entryTransmittance, 0.1f);
    appendRibbonSegment(
        mesh.vertices,
        internalEntry,
        internalMidpoint,
        internalColor,
        cameraPosition,
        0.0f,
        halfWidth * 0.48f
    );
    appendRibbonSegment(
        mesh.vertices,
        internalMidpoint,
        internalExit,
        internalColor,
        cameraPosition,
        halfWidth * 0.48f,
        0.0f
    );
    endBatch(mesh);

    beginBatch(mesh, SpectralBeamGroup::Exit);
    if (spectrum.mode == PrismSpectrumMode::Continuous && samples.size() >= 2U) {
        appendContinuousFan(
            mesh.vertices, samples, false, outputLength,
            0.0f, 1.0f, 0.0f, halfWidth * 0.55f, cameraPosition
        );
    } else {
        const float sampleCount = static_cast<float>(samples.size());
        for (const PrismSpectralSample* sample : samples) {
            const glm::vec3 exit = toWorld(sample->path.exitPoint);
            appendRibbonSegment(
                mesh.vertices,
                exit,
                toWorld(sample->path.exitPoint + sample->path.exitDirection * outputLength),
                sevenBandColor(*sample, sampleCount),
                cameraPosition,
                0.0f,
                halfWidth * 0.28f
            );
        }
    }
    endBatch(mesh);
    return mesh;
}
