if(NOT DEFINED RENDERER OR NOT DEFINED SOURCE_DIR OR NOT DEFINED OUTPUT_DIR)
    message(FATAL_ERROR "SkinningBenchmark requires RENDERER, SOURCE_DIR, and OUTPUT_DIR")
endif()

file(MAKE_DIRECTORY "${OUTPUT_DIR}")

function(run_skinning_benchmark name animation time debug_view)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            MYRENDERER_BENCHMARK_WARMUP=30
            MYRENDERER_BENCHMARK_FRAMES=90
            MYRENDERER_BENCHMARK_OUTPUT=${OUTPUT_DIR}/${name}.json
            MYRENDERER_ANIMATION_DEMO=1
            MYRENDERER_ANIMATION=${animation}
            MYRENDERER_ANIMATION_TIME=${time}
            MYRENDERER_SKIN_DEBUG=${debug_view}
            MYRENDERER_RENDER_WIDTH=1920
            MYRENDERER_RENDER_HEIGHT=1080
            MYRENDERER_MSAA=4
            MYRENDERER_RENDER_PATH=1
            MYRENDERER_TAA=0
            MYRENDERER_BLOOM=0
            "${RENDERER}"
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE benchmark_result
    )
    if(NOT benchmark_result EQUAL 0)
        message(FATAL_ERROR "Skinning benchmark failed for ${name}")
    endif()
endfunction()

run_skinning_benchmark(gp_p1e_bind_pose 0 0.0 0)
run_skinning_benchmark(gp_p1e_animated_pose 1 1.0 0)
run_skinning_benchmark(gp_p1e_joint_debug 1 1.0 1)
