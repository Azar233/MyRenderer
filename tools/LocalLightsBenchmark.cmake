if(NOT DEFINED RENDERER OR NOT DEFINED SOURCE_DIR OR NOT DEFINED OUTPUT_DIR)
    message(FATAL_ERROR "LocalLightsBenchmark requires RENDERER, SOURCE_DIR, and OUTPUT_DIR")
endif()

file(MAKE_DIRECTORY "${OUTPUT_DIR}")

function(run_local_light_benchmark name render_path tier)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            MYRENDERER_BENCHMARK_WARMUP=30
            MYRENDERER_BENCHMARK_FRAMES=90
            MYRENDERER_RENDER_WIDTH=1920
            MYRENDERER_RENDER_HEIGHT=1080
            MYRENDERER_BENCHMARK_OUTPUT=${OUTPUT_DIR}/${name}.json
            MYRENDERER_MSAA=4
            MYRENDERER_RENDER_PATH=${render_path}
            MYRENDERER_GBUFFER_DEBUG=0
            MYRENDERER_LIGHT_STRESS=1
            MYRENDERER_LOCAL_LIGHT_TIER=${tier}
            "${RENDERER}"
            "${SOURCE_DIR}/assets/models/cube.obj"
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE benchmark_result
    )
    if(NOT benchmark_result EQUAL 0)
        message(FATAL_ERROR "Local-light benchmark failed for ${name}")
    endif()
endfunction()

run_local_light_benchmark(gp_p1b_forward_lights8 0 0)
run_local_light_benchmark(gp_p1b_deferred_lights8 1 0)
run_local_light_benchmark(gp_p1b_forward_lights32 0 1)
run_local_light_benchmark(gp_p1b_deferred_lights32 1 1)
run_local_light_benchmark(gp_p1b_forward_lights64 0 2)
run_local_light_benchmark(gp_p1b_deferred_lights64 1 2)
