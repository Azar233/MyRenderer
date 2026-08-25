if(NOT DEFINED RENDERER OR NOT DEFINED SOURCE_DIR OR NOT DEFINED OUTPUT_DIR)
    message(FATAL_ERROR "DeferredBenchmark requires RENDERER, SOURCE_DIR, and OUTPUT_DIR")
endif()

file(MAKE_DIRECTORY "${OUTPUT_DIR}")

function(run_deferred_benchmark name render_path msaa)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            MYRENDERER_BENCHMARK_WARMUP=30
            MYRENDERER_BENCHMARK_FRAMES=90
            MYRENDERER_RENDER_WIDTH=1920
            MYRENDERER_RENDER_HEIGHT=1080
            MYRENDERER_BENCHMARK_OUTPUT=${OUTPUT_DIR}/${name}.json
            MYRENDERER_MSAA=${msaa}
            MYRENDERER_RENDER_PATH=${render_path}
            MYRENDERER_GBUFFER_DEBUG=0
            MYRENDERER_PBR=1
            MYRENDERER_IBL=1
            MYRENDERER_SHADOWS=1
            MYRENDERER_BLOOM=1
            MYRENDERER_GRID=0
            MYRENDERER_AXES=0
            MYRENDERER_GROUND=1
            "${RENDERER}"
            "${SOURCE_DIR}/assets/models/pbr_material_test.gltf"
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE benchmark_result
    )
    if(NOT benchmark_result EQUAL 0)
        message(FATAL_ERROR "Deferred benchmark failed for ${name}")
    endif()
endfunction()

run_deferred_benchmark(gp_p1_forward_msaa1 0 1)
run_deferred_benchmark(gp_p1_forward_msaa4 0 4)
run_deferred_benchmark(gp_p1_deferred_msaa1 1 1)
run_deferred_benchmark(gp_p1_deferred_msaa4 1 4)
