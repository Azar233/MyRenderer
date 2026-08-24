#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

#include <glm/geometric.hpp>
#include <glm/vec2.hpp>

#include "optics/PrismOptics.h"

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool finite(const glm::vec2& value) {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

float angularDifference(const glm::vec2& a, const glm::vec2& b) {
    const float cosine = glm::clamp(glm::dot(glm::normalize(a), glm::normalize(b)), -1.0f, 1.0f);
    return std::acos(cosine);
}

} // namespace

int main() {
    try {
        const std::array<glm::vec2, 3> triangle{
            glm::vec2(-0.7f, -0.48125f),
            glm::vec2(0.7f, -0.48125f),
            glm::vec2(0.0f, 0.74375f)
        };

        const PrismRay2D heroRay{
            glm::vec2(-2.4f, -0.15f),
            glm::normalize(glm::vec2(-0.39f, 0.12f) - glm::vec2(-2.4f, -0.15f))
        };
        const PrismOpticalPath heroPath = traceTriangularPrism(triangle, heroRay, 1.52f);
        require(heroPath.valid, "hero ray should cross both prism interfaces");
        require(!heroPath.totalInternalReflection, "hero ray should leave crown glass");
        require(heroPath.entryPoint.x < 0.0f, "hero ray should enter through the left face");
        require(finite(heroPath.exitDirection), "hero exit direction should remain finite");
        require(
            heroPath.totalTransmittance > 0.0f && heroPath.totalTransmittance <= 1.0f,
            "Fresnel transmission should remain normalized"
        );

        const PrismRay2D normalEntryRay{
            glm::vec2(0.3f, -2.0f),
            glm::vec2(0.0f, 1.0f)
        };
        const PrismOpticalPath normalEntry = traceTriangularPrism(triangle, normalEntryRay, 1.52f);
        require(normalEntry.valid, "normal-entry ray should cross the prism");
        require(
            angularDifference(normalEntry.incidentDirection, normalEntry.internalDirection) < 1.0e-4f,
            "normal incidence should not bend at the entry interface"
        );

        const PrismOpticalPath repeated = traceTriangularPrism(triangle, heroRay, 1.52f);
        require(repeated.valid, "repeated trace should remain valid");
        require(
            glm::length(repeated.exitPoint - heroPath.exitPoint) < 1.0e-6f
                && glm::length(repeated.exitDirection - heroPath.exitDirection) < 1.0e-6f,
            "identical IOR inputs should produce identical paths"
        );

        const PrismOpticalPath lowerIor = traceTriangularPrism(triangle, heroRay, 1.33f);
        const PrismOpticalPath higherIor = traceTriangularPrism(triangle, heroRay, 1.72f);
        require(lowerIor.valid && higherIor.valid, "comparison IOR paths should remain valid");
        require(
            angularDifference(lowerIor.exitDirection, higherIor.exitDirection) > 1.0e-3f,
            "different IOR values should separate outgoing directions"
        );

        bool foundTotalInternalReflection = false;
        for (int sample = -12; sample <= 12 && !foundTotalInternalReflection; ++sample) {
            const float targetY = static_cast<float>(sample) * 0.045f;
            const PrismRay2D candidate{
                glm::vec2(-2.0f, targetY - 0.25f),
                glm::normalize(glm::vec2(-0.45f, targetY) - glm::vec2(-2.0f, targetY - 0.25f))
            };
            const PrismOpticalPath path = traceTriangularPrism(triangle, candidate, 2.4f);
            if (path.valid) {
                require(finite(path.internalDirection), "high-IOR internal direction should remain finite");
                require(finite(path.exitDirection), "TIR reflection direction should remain finite");
                foundTotalInternalReflection |= path.totalInternalReflection;
            }
        }
        require(foundTotalInternalReflection, "high-IOR angle sweep should exercise total internal reflection");

        std::cout << "Prism optics tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Prism optics test failed: " << error.what() << '\n';
        return 1;
    }
}
