if(NOT DEFINED RENDERER OR NOT DEFINED SOURCE_DIR OR NOT DEFINED OUTPUT_DIR)
    message(FATAL_ERROR "InstanceStressBenchmark requires RENDERER, SOURCE_DIR, and OUTPUT_DIR")
endif()

file(MAKE_DIRECTORY "${OUTPUT_DIR}")

function(run_instance_benchmark name optimization culling lod)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            MYRENDERER_BENCHMARK_WARMUP=30
            MYRENDERER_BENCHMARK_FRAMES=90
            MYRENDERER_RENDER_WIDTH=1920
            MYRENDERER_RENDER_HEIGHT=1080
            MYRENDERER_BENCHMARK_OUTPUT=${OUTPUT_DIR}/${name}.json
            MYRENDERER_MSAA=4
            MYRENDERER_RENDER_PATH=0
            MYRENDERER_INSTANCE_STRESS=1
            MYRENDERER_INSTANCE_OPTIMIZATION=${optimization}
            MYRENDERER_FRUSTUM_CULLING=${culling}
            MYRENDERER_LOD=${lod}
            "${RENDERER}"
            "${SOURCE_DIR}/assets/models/sphere.obj"
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE benchmark_result
    )
    if(NOT benchmark_result EQUAL 0)
        message(FATAL_ERROR "Instance stress benchmark failed for ${name}")
    endif()
endfunction()

run_instance_benchmark(gp_p1c_baseline 0 0 0)
run_instance_benchmark(gp_p1c_instancing 1 0 0)
run_instance_benchmark(gp_p1c_instancing_culling 1 1 0)
run_instance_benchmark(gp_p1c_instancing_culling_lod 1 1 1)
