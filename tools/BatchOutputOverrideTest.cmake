if(NOT DEFINED BATCH OR NOT EXISTS "${BATCH}")
    message(FATAL_ERROR "MyRendererBatch executable is missing")
endif()
if(NOT DEFINED JOB OR NOT EXISTS "${JOB}")
    message(FATAL_ERROR "OpenEXR Render Job fixture is missing")
endif()
if(NOT DEFINED MULTI_JOB OR NOT EXISTS "${MULTI_JOB}")
    message(FATAL_ERROR "Multi-frame Render Job fixture is missing")
endif()
if(NOT DEFINED OUTPUT_DIR)
    message(FATAL_ERROR "OUTPUT_DIR is required")
endif()

file(REMOVE_RECURSE "${OUTPUT_DIR}")
execute_process(
    COMMAND "${BATCH}" render-frame "${JOB}" 0 --output "${OUTPUT_DIR}/single"
    RESULT_VARIABLE render_result
    OUTPUT_VARIABLE render_stdout
    ERROR_VARIABLE render_stderr
)
if(NOT render_result EQUAL 0)
    message(FATAL_ERROR
        "Single-frame output override failed (${render_result})\n${render_stdout}\n${render_stderr}")
endif()

foreach(output
        single.png single.exr
        single-normal.png single-normal.exr
        single-depth.png single-depth.exr
        single-report.json)
    if(NOT EXISTS "${OUTPUT_DIR}/${output}")
        message(FATAL_ERROR "Output override did not create ${output}")
    endif()
endforeach()

execute_process(
    COMMAND "${BATCH}" render-sequence "${MULTI_JOB}" --output "${OUTPUT_DIR}/missing-frame-token"
    RESULT_VARIABLE invalid_result
    OUTPUT_VARIABLE invalid_stdout
    ERROR_VARIABLE invalid_stderr
)
if(invalid_result EQUAL 0)
    message(FATAL_ERROR
        "Sequence output override without a frame token unexpectedly succeeded\n"
        "${invalid_stdout}\n${invalid_stderr}")
endif()

message(STATUS "Batch OpenEXR output override acceptance passed")
