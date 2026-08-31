if(NOT DEFINED RENDERER OR NOT DEFINED SOURCE_DIR OR NOT DEFINED OUTPUT_DIR)
    message(FATAL_ERROR "ScreenSpaceBenchmark requires RENDERER, SOURCE_DIR, and OUTPUT_DIR")
endif()

file(MAKE_DIRECTORY "${OUTPUT_DIR}")

function(run_screen_space_benchmark name ssao taa motion)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            MYRENDERER_BENCHMARK_WARMUP=30
            MYRENDERER_BENCHMARK_FRAMES=90
            MYRENDERER_RENDER_WIDTH=1920
            MYRENDERER_RENDER_HEIGHT=1080
            MYRENDERER_BENCHMARK_OUTPUT=${OUTPUT_DIR}/${name}.json
            MYRENDERER_MSAA=1
            MYRENDERER_RENDER_PATH=1
            MYRENDERER_GBUFFER_DEBUG=0
            MYRENDERER_SSAO=${ssao}
            MYRENDERER_TAA=${taa}
            MYRENDERER_TAA_MOTION_DEMO=${motion}
            MYRENDERER_PBR=1
            MYRENDERER_IBL=1
            MYRENDERER_SHADOWS=1
            MYRENDERER_BLOOM=0
            MYRENDERER_GRID=0
            MYRENDERER_AXES=0
            MYRENDERER_GROUND=1
            MYRENDERER_SCENE_DEMO=1
            "${RENDERER}"
            "${SOURCE_DIR}/assets/models/pbr_material_test.gltf"
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE benchmark_result
    )
    if(NOT benchmark_result EQUAL 0)
        message(FATAL_ERROR "Screen-space benchmark failed for ${name}")
    endif()
endfunction()

run_screen_space_benchmark(gp_p1d_baseline 0 0 0)
run_screen_space_benchmark(gp_p1d_ssao 1 0 0)
run_screen_space_benchmark(gp_p1d_taa_static 0 1 0)
run_screen_space_benchmark(gp_p1d_taa_motion 0 1 1)
run_screen_space_benchmark(gp_p1d_ssao_taa 1 1 1)
