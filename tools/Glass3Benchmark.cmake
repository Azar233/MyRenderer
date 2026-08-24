if(NOT DEFINED RENDERER OR NOT DEFINED SOURCE_DIR OR NOT DEFINED OUTPUT_DIR)
    message(FATAL_ERROR "Glass3Benchmark requires RENDERER, SOURCE_DIR, and OUTPUT_DIR")
endif()

file(MAKE_DIRECTORY "${OUTPUT_DIR}")
foreach(mode IN ITEMS off projector lightspace)
    if(mode STREQUAL "off")
        set(extra_env MYRENDERER_CAUSTICS=0)
    elseif(mode STREQUAL "projector")
        set(extra_env MYRENDERER_CAUSTICS=1 MYRENDERER_CAUSTICS_MODE=0)
    else()
        set(extra_env MYRENDERER_CAUSTICS=1 MYRENDERER_CAUSTICS_MODE=1)
    endif()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            MYRENDERER_GLASS3_DEMO=1
            MYRENDERER_MSAA=4
            MYRENDERER_BENCHMARK_WARMUP=30
            MYRENDERER_BENCHMARK_FRAMES=90
            MYRENDERER_RENDER_WIDTH=1920
            MYRENDERER_RENDER_HEIGHT=1080
            MYRENDERER_BENCHMARK_OUTPUT=${OUTPUT_DIR}/glass3_${mode}.json
            ${extra_env}
            "${RENDERER}"
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE benchmark_result
    )
    if(NOT benchmark_result EQUAL 0)
        message(FATAL_ERROR "Glass-3 benchmark failed for ${mode}")
    endif()
endforeach()
