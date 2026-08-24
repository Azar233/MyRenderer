if(NOT DEFINED RENDERER OR NOT DEFINED SOURCE_DIR OR NOT DEFINED OUTPUT_DIR)
    message(FATAL_ERROR "Prism5Benchmark requires RENDERER, SOURCE_DIR, and OUTPUT_DIR")
endif()

file(MAKE_DIRECTORY "${OUTPUT_DIR}")
foreach(samples IN ITEMS 7 15 21 31)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            MYRENDERER_PRISM_DEMO=1
            MYRENDERER_MSAA=4
            MYRENDERER_PRISM_SAMPLES=${samples}
            MYRENDERER_BENCHMARK_WARMUP=60
            MYRENDERER_BENCHMARK_FRAMES=180
            MYRENDERER_RENDER_WIDTH=1920
            MYRENDERER_RENDER_HEIGHT=1080
            MYRENDERER_BENCHMARK_OUTPUT=${OUTPUT_DIR}/prism5_samples_${samples}.json
            "${RENDERER}"
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE benchmark_result
    )
    if(NOT benchmark_result EQUAL 0)
        message(FATAL_ERROR "Prism-5 ${samples}-sample benchmark failed")
    endif()
endforeach()
