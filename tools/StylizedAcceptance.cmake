cmake_minimum_required(VERSION 3.20)

foreach(required_variable RENDERER COMPARATOR SOURCE_DIR OUTPUT_DIR)
    if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
        message(FATAL_ERROR "${required_variable} is required")
    endif()
endforeach()

file(MAKE_DIRECTORY "${OUTPUT_DIR}")
function(capture_stylized name stylized render_path outline taa width height scene_name)
    set(output "${OUTPUT_DIR}/${name}.png")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            "MYRENDERER_SMOKE_TEST=1"
            "MYRENDERER_RENDER_WIDTH=${width}"
            "MYRENDERER_RENDER_HEIGHT=${height}"
            "MYRENDERER_SCREENSHOT=${output}"
            "MYRENDERER_STYLIZED=${stylized}"
            "MYRENDERER_STYLIZED_BANDS=3"
            "MYRENDERER_STYLIZED_OUTLINE=${outline}"
            "MYRENDERER_STYLIZED_OUTLINE_WIDTH=1.5"
            "MYRENDERER_RENDER_PATH=${render_path}"
            "MYRENDERER_TAA=${taa}"
            "MYRENDERER_HIDE_SELECTION_OUTLINE=1"
            "${RENDERER}"
            "${SOURCE_DIR}/assets/scenes/${scene_name}"
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE capture_result
    )
    if(NOT capture_result EQUAL 0 OR NOT EXISTS "${output}")
        message(FATAL_ERROR "Failed to capture ${name}")
    endif()
    file(SIZE "${output}" output_size)
    if(output_size LESS 1024)
        message(FATAL_ERROR "Stylized capture is unexpectedly small: ${output}")
    endif()
endfunction()

function(require_different first second description)
    execute_process(
        COMMAND "${COMPARATOR}" "${first}" "${second}" 0 0
        RESULT_VARIABLE comparison_result
    )
    if(comparison_result EQUAL 0)
        message(FATAL_ERROR "${description} produced identical images")
    elseif(NOT comparison_result EQUAL 1)
        message(FATAL_ERROR "${description} comparison failed with exit code ${comparison_result}")
    endif()
endfunction()

set(gallery 14_polyhaven_material_gallery.myscene)
capture_stylized(sr_p2a_pbr_forward 0 0 0 0 640 360 ${gallery})
capture_stylized(sr_p2b_toon_forward 1 0 1 0 640 360 ${gallery})
capture_stylized(sr_p2b_toon_deferred 1 1 1 0 640 360 ${gallery})
capture_stylized(sr_p2b_toon_deferred_no_outline 1 1 0 0 640 360 ${gallery})
capture_stylized(sr_p2b_toon_deferred_taa 1 1 1 1 640 360 ${gallery})
capture_stylized(sr_p2b_toon_deferred_no_outline_taa 1 1 0 1 640 360 ${gallery})
capture_stylized(sr_p2b_toon_deferred_960x540 1 1 1 0 960 540 ${gallery})
capture_stylized(sr_p2b_toon_deferred_no_outline_960x540 1 1 0 0 960 540 ${gallery})
capture_stylized(
    sr_p2b_transmission_boundary 1 1 1 0 640 360
    12_reference_pathtracer_volume.myscene
)

# The mode switch must be visually meaningful, while Forward and Deferred
# should remain close enough to represent the same stylized lighting model.
require_different(
    "${OUTPUT_DIR}/sr_p2a_pbr_forward.png"
    "${OUTPUT_DIR}/sr_p2b_toon_forward.png"
    "PBR/Stylized mode switch"
)
require_different(
    "${OUTPUT_DIR}/sr_p2b_toon_deferred_no_outline.png"
    "${OUTPUT_DIR}/sr_p2b_toon_deferred.png"
    "640x360 outline"
)
require_different(
    "${OUTPUT_DIR}/sr_p2b_toon_deferred_no_outline_960x540.png"
    "${OUTPUT_DIR}/sr_p2b_toon_deferred_960x540.png"
    "960x540 outline"
)
require_different(
    "${OUTPUT_DIR}/sr_p2b_toon_deferred_no_outline_taa.png"
    "${OUTPUT_DIR}/sr_p2b_toon_deferred_taa.png"
    "TAA outline"
)

execute_process(
    COMMAND "${COMPARATOR}"
        "${OUTPUT_DIR}/sr_p2b_toon_forward.png"
        "${OUTPUT_DIR}/sr_p2b_toon_deferred.png"
        0.02 0.15
    RESULT_VARIABLE path_comparison_result
)
if(NOT path_comparison_result EQUAL 0)
    message(FATAL_ERROR "Forward and Deferred stylized output diverged")
endif()

message(STATUS "SR-P2A/B stylized lighting and outline acceptance passed: ${OUTPUT_DIR}")
