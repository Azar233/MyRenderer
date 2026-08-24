cmake_minimum_required(VERSION 3.20)

project(MyRenderer VERSION 0.1.0 LANGUAGES C CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# tinyobjloader v1.0.6 predates modern CMake policy declarations. This keeps
# CMake 4.x compatible without weakening this project's own minimum version.
set(CMAKE_POLICY_VERSION_MINIMUM 3.10)

include(FetchContent)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE
)

set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.3
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    glad
    GIT_REPOSITORY https://github.com/Dav1dde/glad.git
    GIT_TAG v2.0.8
    GIT_SHALLOW TRUE
)

set(TINYOBJLOADER_BUILD_TEST_LOADER OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    tinyobjloader
    GIT_REPOSITORY https://github.com/tinyobjloader/tinyobjloader.git
    GIT_TAG v1.0.6
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.7-docking
    GIT_SHALLOW TRUE
)

set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_NO_EXPORT ON CACHE BOOL "" FORCE)
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_COLLADA_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_GLTF_IMPORTER ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    assimp
    GIT_REPOSITORY https://github.com/assimp/assimp.git
    GIT_TAG v6.0.5
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(glfw glm glad tinyobjloader imgui assimp)

add_subdirectory(${glad_SOURCE_DIR}/cmake ${glad_BINARY_DIR}/cmake)
glad_add_library(glad_gl_core_33 REPRODUCIBLE API gl:core=3.3)

add_library(imgui_backend STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui_backend PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_compile_definitions(imgui_backend PRIVATE
    GLFW_INCLUDE_NONE
    IMGUI_IMPL_OPENGL_LOADER_GLAD2
)
target_link_libraries(imgui_backend PUBLIC glfw glad_gl_core_33)

add_executable(MyRenderer
    src/main.cpp
    src/app/AppIcon.cpp
    src/app/Application.cpp
    src/app/FileDialog.cpp
    src/io/AssimpImporter.cpp
    src/io/GltfMaterialExtensions.cpp
    src/io/ObjLoader.cpp
    src/optics/PrismOptics.cpp
    src/render/Camera.cpp
    src/render/DebugGrid.cpp
    src/render/EnvironmentMap.cpp
    src/render/GpuModel.cpp
    src/render/Mesh.cpp
    src/render/OpenGlDebug.cpp
    src/render/PostProcessor.cpp
    src/render/RenderTarget.cpp
    src/render/Renderer.cpp
    src/render/SceneDrawList.cpp
    src/render/Shader.cpp
    src/render/ShadowMap.cpp
    src/render/Texture2D.cpp
)

if(WIN32)
    enable_language(RC)
    set(MYRENDERER_ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/myrenderer.ico")
    configure_file(
        resources/MyRenderer.rc.in
        "${CMAKE_CURRENT_BINARY_DIR}/MyRenderer.rc"
        @ONLY
    )
    target_sources(MyRenderer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/MyRenderer.rc")
    target_link_libraries(MyRenderer PRIVATE comdlg32)
endif()

target_include_directories(MyRenderer PRIVATE
    src
    ${assimp_SOURCE_DIR}/contrib/stb
    ${assimp_SOURCE_DIR}/contrib/rapidjson/include
    ${assimp_SOURCE_DIR}/contrib/zlib
    ${assimp_BINARY_DIR}/contrib/zlib
)
target_compile_definitions(MyRenderer PRIVATE
    GLFW_INCLUDE_NONE
    MYRENDERER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(MyRenderer PRIVATE
    glad_gl_core_33
    glfw
    glm::glm
    imgui_backend
    assimp::assimp
    tinyobjloader
    zlibstatic
)

if(MSVC)
    target_compile_options(MyRenderer PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(MyRenderer PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_custom_command(TARGET MyRenderer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders
            $<TARGET_FILE_DIR:MyRenderer>/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/assets
            $<TARGET_FILE_DIR:MyRenderer>/assets
    COMMENT "Copying shaders and model assets"
)

if(BUILD_TESTING)
    add_executable(MyRendererAssetTests
        tests/AssetImportTests.cpp
        src/io/AssimpImporter.cpp
        src/io/GltfMaterialExtensions.cpp
        src/io/ObjLoader.cpp
    )
    target_include_directories(MyRendererAssetTests PRIVATE
        src
        ${assimp_SOURCE_DIR}/contrib/rapidjson/include
    )
    target_compile_definitions(MyRendererAssetTests PRIVATE
        MYRENDERER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_link_libraries(MyRendererAssetTests PRIVATE
        glm::glm
        assimp::assimp
        tinyobjloader
    )
    if(MSVC)
        target_compile_options(MyRendererAssetTests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(MyRendererAssetTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME asset-import COMMAND MyRendererAssetTests)

    add_executable(MyRendererSceneTests
        tests/SceneDrawListTests.cpp
        src/render/SceneDrawList.cpp
    )
    target_include_directories(MyRendererSceneTests PRIVATE src)
    target_link_libraries(MyRendererSceneTests PRIVATE glm::glm)
    if(MSVC)
        target_compile_options(MyRendererSceneTests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(MyRendererSceneTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME scene-draw-list COMMAND MyRendererSceneTests)

    add_executable(MyRendererPrismOpticsTests
        tests/PrismOpticsTests.cpp
        src/optics/PrismOptics.cpp
    )
    target_include_directories(MyRendererPrismOpticsTests PRIVATE src)
    target_link_libraries(MyRendererPrismOpticsTests PRIVATE glm::glm)
    if(MSVC)
        target_compile_options(MyRendererPrismOpticsTests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(MyRendererPrismOpticsTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME prism-optics COMMAND MyRendererPrismOpticsTests)
endif()

add_custom_target(gpu-smoke
    COMMAND ${CMAKE_COMMAND} -E env
            MYRENDERER_SMOKE_TEST=1
            MYRENDERER_MSAA=4
            $<TARGET_FILE:MyRenderer>
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/models/material_regression.obj
    COMMAND ${CMAKE_COMMAND} -E env
            MYRENDERER_SMOKE_TEST=1
            MYRENDERER_MSAA=4
            MYRENDERER_SCENE_DEMO=1
            $<TARGET_FILE:MyRenderer>
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/models/alpha_material_test.gltf
    COMMAND ${CMAKE_COMMAND} -E env
            MYRENDERER_SMOKE_TEST=1
            MYRENDERER_MSAA=4
            $<TARGET_FILE:MyRenderer>
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/models/glass_material_test.gltf
    COMMAND ${CMAKE_COMMAND} -E env
            MYRENDERER_SMOKE_TEST=1
            MYRENDERER_MSAA=4
            MYRENDERER_PRISM_DEMO=1
            MYRENDERER_PRISM_SAMPLES=21
            $<TARGET_FILE:MyRenderer>
    COMMAND ${CMAKE_COMMAND} -E env
            MYRENDERER_SMOKE_TEST=1
            MYRENDERER_MSAA=1
            MYRENDERER_RECOVERY_TEST=${CMAKE_CURRENT_SOURCE_DIR}/assets/models/does_not_exist.obj
            $<TARGET_FILE:MyRenderer>
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/models/cube.obj
    DEPENDS MyRenderer
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Running MyRenderer with a real OpenGL context"
)
