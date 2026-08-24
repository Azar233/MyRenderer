if(NOT DEFINED RENDERER OR NOT DEFINED SOURCE_DIR OR NOT DEFINED OUTPUT_DIR)
    message(FATAL_ERROR "Glass2cBenchmark requires RENDERER, SOURCE_DIR, and OUTPUT_DIR")
endif()

file(MAKE_DIRECTORY "${OUTPUT_DIR}")
foreach(samples IN ITEMS 1 4)
    foreach(two_interface IN ITEMS 0 1)
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E env
                MYRENDERER_MSAA=${samples}
                MYRENDERER_TWO_INTERFACE_REFRACTION=${two_interface}
                MYRENDERER_BENCHMARK_WARMUP=30
                MYRENDERER_BENCHMARK_FRAMES=90
                MYRENDERER_RENDER_WIDTH=1920
                MYRENDERER_RENDER_HEIGHT=1080
                MYRENDERER_BENCHMARK_OUTPUT=${OUTPUT_DIR}/glass2c_msaa${samples}_two_interface${two_interface}.json
                "${RENDERER}"
                "${SOURCE_DIR}/assets/models/glass_volume_sphere.gltf"
            WORKING_DIRECTORY "${SOURCE_DIR}"
            RESULT_VARIABLE benchmark_result
        )
        if(NOT benchmark_result EQUAL 0)
            message(FATAL_ERROR "Glass-2C benchmark failed for MSAA ${samples}, two-interface ${two_interface}")
        endif()
    endforeach()
endforeach()
