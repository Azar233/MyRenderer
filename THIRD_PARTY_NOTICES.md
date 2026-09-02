# Third-party notices

MyRenderer fetches the following pinned dependencies while configuring. Their
canonical license files are copied into the `licenses` directory of release
packages; those licenses apply to the dependency code and take precedence over
this summary.

| Component | Pinned version | License | Upstream |
| --- | --- | --- | --- |
| GLFW | 3.4 | zlib/libpng | <https://github.com/glfw/glfw> |
| GLM | 1.0.3 | MIT or Happy Bunny (Modified MIT) | <https://github.com/g-truc/glm> |
| glad | 2.0.8 | Apache-2.0 / CC0 (component-dependent) | <https://github.com/Dav1dde/glad> |
| tinyobjloader | 1.0.6 | MIT | <https://github.com/tinyobjloader/tinyobjloader> |
| Dear ImGui | 1.92.7-docking | MIT | <https://github.com/ocornut/imgui> |
| Assimp | 6.0.5 | BSD-3-Clause; bundled components retain their own notices | <https://github.com/assimp/assimp> |

Assimp brings the stb image loader, RapidJSON and zlib into this build. Their
license notices remain in the fetched Assimp source tree and Assimp's canonical
license file is shipped with packaged binaries.

Asset provenance and redistribution scope are recorded separately in
`ASSET_LICENSES.md`. The project's MIT license does not relicense third-party
assets.
