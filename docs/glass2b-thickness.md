# Glass-2B Geometric Thickness

Glass-2B replaces the uniform-only volume path proxy with a screen-space
front/back surface estimate for closed transmissive meshes. The implementation
remains compatible with OpenGL 3.3 and preserves the glTF material fallback.

## Pass and resource flow

```text
Opaque HDR (+ optional spectral beam)
  -> Glass front/back thickness
       -> R32F minimum view depth (entry)
       -> R32F maximum view depth (exit)
  -> Forward transparent / refractive scene
       -> geometric path or glTF thickness fallback
       -> Beer-Lambert + RGB dispersion
  -> Bloom + tone map
```

The depth-only geometry shader draws every transmissive submesh twice. `GL_MIN`
selects the nearest surface and `GL_MAX` selects the farthest surface. This does
not depend on imported winding or `doubleSided`; the glass fragment shader also
rejects fragments behind the recorded entry surface so a reversed mesh does not
accidentally shade its exit surface as the visible boundary.

At a valid pixel:

```text
viewRayDistance = (exitViewDepth - entryViewDepth)
                / abs(viewSpaceCameraRay.z)

normalThickness = viewRayDistance
                * abs(dot(cameraRay, geometricNormal))
                * volumeThicknessScale

volumePathLength = normalThickness
                 / max(abs(dot(refractedRay, interfaceNormal)), 0.15)
```

The same path length drives Beer-Lambert attenuation and the R/G/B dispersion
samples. When the depth span is missing or invalid, the shader follows
`KHR_materials_volume` and uses:

```text
fallbackThickness = thicknessFactor * thicknessTexture.g
                  * volumeThicknessScale
```

Khronos defines `thicknessTexture` as linear data stored in the G channel and
multiplied by `thicknessFactor`:
<https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_materials_volume>

## Controls and diagnostics

- `Geometric glass thickness`: enables geometric depth-span path length; the
  front-depth texture still selects the visible entry surface for robust volume
  rendering.
- `Thickness`: shows the resulting refracted path length.
- `Front/back thickness data`: green/yellow means a positive front/back span;
  magenta means the material thickness/texture fallback is required.
- `MYRENDERER_GEOMETRIC_THICKNESS=0|1` and `MYRENDERER_GLASS_DEBUG=8` expose the
  same controls to deterministic screenshots.

## Known boundaries

- A single min/max pair cannot distinguish overlapping transmissive objects;
  their entry and exit depths may be combined.
- Concave or nested volumes can overestimate thickness because the farthest
  surface is selected.
- The exit interface uses a locally parallel-surface normal approximation.
  Exact curved-surface exit refraction needs an exit-normal layer, per-object
  depth peeling, ray queries, or hardware ray tracing.
- The pass adds two R32F render targets and two depth-only draws per
  transmissive submesh. GPU timing and memory should be reported alongside
  future Glass-4 portfolio captures.

## Validation snapshot

On the RTX 4060 Laptop reference machine at 1920×1080 and 4x MSAA:

- all 3 CTest targets pass, including the new linear Thickness Texture import
  assertions;
- the existing 10-scene Prism-5 visual matrix remains 10/10 within its
  cross-driver thresholds; the largest changed-pixel fraction is `1.1454%`;
- `glass_material_test.gltf` reports valid positive entry/exit spans across
  both closed glass meshes;
- the fixed-camera Thickness debug comparison between uniform fallback and
  geometric mode has normalized RGBA MAE `0.0121168` and `6.68002%` changed
  pixels, proving the geometric branch is observably active;
- the Prism benchmark reports 16 draw calls (14 before Glass-2B) and
  `229,866,284` estimated render bytes. The two 1080p R32F layers account for
  the deterministic `16,588,800` byte increase over the Prism-5 baseline;
- geometric mode measured GPU frame P50/P95 `1.432576 / 2.362368 ms`. This
  single run is recorded as a smoke benchmark, not as proof of a speedup over
  the older baseline because ordinary driver variance is larger than the
  observed timing difference.
