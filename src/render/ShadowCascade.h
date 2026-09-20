#pragma once

#include <array>
#include <cstddef>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

// Cascaded shadow map fitting (P1-A slice 3). One orthographic box cannot cover a coastal scene:
// the near geometry needs centimetre detail and the far geometry needs hundreds of metres, and a
// single map has to trade one for the other. Cascades split the camera frustum along view depth
// and fit a light-space box to each slice.
//
// Everything here is pure math on matrices and vectors so it can be unit tested without an OpenGL
// context, which is where the split scheme, the bounds fit and the texel snapping are verified.
namespace shadow {

// Upper bound on the cascade count. The shaders declare matching fixed-size uniform arrays, so this
// is a contract between the two sides rather than a preference.
constexpr std::size_t maximumCascadeCount = 4U;

// One fitted cascade.
struct CascadeFit {
    // World -> light clip space for this slice.
    glm::mat4 lightViewProjection{1.0f};
    // Distance from the camera at which this cascade stops being used.
    float splitDistance{0.0f};
    // World units covered by one shadow texel, used for the slope-scaled depth bias.
    float texelWorldSize{0.0f};
    // The slice's centre depth in the shared light view, which the orthographic depth range is built
    // around so every cascade gets the same depth-precision budget.
    float centreDepth{0.0f};
};

// Practical split scheme: a blend of logarithmic (equal screen-space error) and uniform (equal
// world-space) distribution. `lambda` is 1 for pure logarithmic and 0 for pure uniform. A blend is
// the practical choice because pure logarithmic starves the near cascade of range while pure
// uniform wastes resolution on the far one.
//
// Returns `count` far distances in ascending order. `count` is clamped to
// [1, maximumCascadeCount], and a degenerate range (`nearPlane <= 0`, `farPlane <= nearPlane`)
// yields every split at `farPlane` rather than a decreasing sequence.
std::array<float, maximumCascadeCount> splitDistances(
    float nearPlane,
    float farPlane,
    std::size_t count,
    float lambda
);

// Everything needed to describe the camera to the fitter. Passing this instead of a view-projection
// matrix keeps the fitter free of projection-convention guesswork: it never has to work out which
// depth range the projection writes, where the camera is, or what its field of view is, so a change
// to the projection cannot silently bend the cascades around a frustum that is not being rendered.
struct CameraFrustum {
    glm::vec3 origin{0.0f};
    // Unit basis of the camera's view space in world space.
    glm::vec3 right{1.0f, 0.0f, 0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    glm::vec3 forward{0.0f, 0.0f, -1.0f};
    float nearPlane{0.1f};
    float farPlane{100.0f};
    float fieldOfViewDegrees{45.0f};
    float aspectRatio{16.0f / 9.0f};
};

// Eight world-space corners of the camera sub-frustum between `nearPlane` and `splitDistance`,
// ordered as the near plane's bottom-left, bottom-right, top-right, top-left, then the far plane's
// in the same order. `splitDistance` is clamped to the frustum's depth range.
std::array<glm::vec3, 8U> subFrustumCorners(
    const CameraFrustum& camera,
    float splitDistance
);

// Builds the one light view every cascade shares.
//
// Sharing the view is the decision that keeps cascades compatible with the rest of the renderer:
// transmission shadows and caustics consume the same matrix, and a per-cascade view would leave them
// with no self-consistent one to use. The view is anchored to a world-space pivot rather than to any
// cascade's own centre, so it changes only when the sun or the pivot moves -- never when the camera
// moves, which is also what makes the texel snapping below effective.
//
// `lightDirection` points from the scene towards the light, which is `-RendererSettings::lightDirection`
// (that field stores the direction the light travels). `depthRange` is the half-extent used along the
// light's own axis; it has to cover the scene's casters and receivers around the pivot.
glm::mat4 buildLightView(
    const glm::vec3& pivot,
    const glm::vec3& directionToLight,
    float depthRange
);

// Fits one orthographic box around a slice, expressed in the shared light view space, and returns the
// world -> light clip matrix for that cascade.
//
// `lightViewRange` must be the same `depthRange` the view was built with. It sizes the orthographic
// depth band so the band covers everything the light view can see, not merely the slice: a caster
// outside the slice still has to be able to shadow inside it, and a band tightened to the slice clips
// exactly the geometry that produces the shadows. That failure does not raise an error -- geometry
// outside the band simply stops casting and receiving, which reads as a straight-edged seam across the
// ground.
//
// The box is squared, rounded up to whole texels and snapped to the texel grid in light space, so the
// shadow map's texel grid is pinned to the light instead of sliding with the camera. `resolution` is
// the shadow map's texel count on a side.
CascadeFit fitCascade(
    const std::array<glm::vec3, 8U>& corners,
    const glm::mat4& lightView,
    float lightViewRange,
    float splitDistance,
    int resolution
);
} // namespace shadow
