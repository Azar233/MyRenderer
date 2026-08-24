#pragma once

#include <cstddef>
#include <vector>

#include <glm/vec3.hpp>

#include "optics/PrismOptics.h"

enum class SpectralBeamGroup {
    Incident,
    Internal,
    Exit
};

struct SpectralBeamVertex {
    glm::vec3 position{0.0f};
    glm::vec3 linearColor{1.0f};
    float edgeCoordinate{0.0f};
};

struct SpectralBeamBatch {
    SpectralBeamGroup group{SpectralBeamGroup::Incident};
    std::size_t firstVertex{0};
    std::size_t vertexCount{0};
};

struct SpectralBeamMeshData {
    std::vector<SpectralBeamVertex> vertices;
    std::vector<SpectralBeamBatch> batches;
};

SpectralBeamMeshData buildSpectralBeamMesh(
    const SpectralBeamData& spectrum,
    const glm::vec3& cameraPosition,
    float outputLength,
    float beamWidth,
    glm::vec3 incidentWhitePoint = glm::vec3(1.0f)
);
