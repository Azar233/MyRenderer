if(NOT DEFINED RENDERER OR NOT DEFINED SOURCE_DIR OR NOT DEFINED OUTPUT_DIR)
    message(FATAL_ERROR "Glass4Benchmark requires RENDERER, SOURCE_DIR, and OUTPUT_DIR")
endif()

file(MAKE_DIRECTORY "${OUTPUT_DIR}")

function(run_glass4_benchmark name model)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            MYRENDERER_MSAA=4
            MYRENDERER_BENCHMARK_WARMUP=30
            MYRENDERER_BENCHMARK_FRAMES=90
            MYRENDERER_RENDER_WIDTH=1920
            MYRENDERER_RENDER_HEIGHT=1080
            MYRENDERER_BENCHMARK_OUTPUT=${OUTPUT_DIR}/${name}.json
            ${ARGN}
            "${RENDERER}"
            ${model}
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE benchmark_result
    )
    if(NOT benchmark_result EQUAL 0)
        message(FATAL_ERROR "Glass-4 benchmark failed for ${name}")
    endif()
endfunction()

run_glass4_benchmark(
    glass4_volume_validation
    "${SOURCE_DIR}/assets/models/glass_volume_sphere.gltf"
    MYRENDERER_GLASS3_DEMO=0
    MYRENDERER_GLASS_PRESET=1
    MYRENDERER_DISPERSION_ENABLED=0
    MYRENDERER_CAUSTICS=0
)
run_glass4_benchmark(
    glass4_volume_approximate
    "${SOURCE_DIR}/assets/models/glass_volume_sphere.gltf"
    MYRENDERER_GLASS3_DEMO=0
    MYRENDERER_GLASS_PRESET=1
    MYRENDERER_DISPERSION_ENABLED=0
    MYRENDERER_CAUSTICS=0
    MYRENDERER_TWO_INTERFACE_REFRACTION=0
)
run_glass4_benchmark(
    glass4_caustics_off
    ""
    MYRENDERER_GLASS3_DEMO=1
    MYRENDERER_GLASS_PRESET=3
    MYRENDERER_DISPERSION_ENABLED=1
    MYRENDERER_CAUSTICS=0
)
run_glass4_benchmark(
    glass4_caustics_on
    ""
    MYRENDERER_GLASS3_DEMO=1
    MYRENDERER_GLASS_PRESET=3
    MYRENDERER_DISPERSION_ENABLED=1
    MYRENDERER_CAUSTICS=1
    MYRENDERER_CAUSTICS_MODE=1
)
