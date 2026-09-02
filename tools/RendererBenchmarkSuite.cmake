if(NOT DEFINED RENDERER OR NOT DEFINED SOURCE_DIR OR NOT DEFINED BINARY_DIR)
    message(FATAL_ERROR "RendererBenchmarkSuite requires RENDERER, SOURCE_DIR, and BINARY_DIR")
endif()

function(run_benchmark_suite script output_name)
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            -DRENDERER=${RENDERER}
            -DSOURCE_DIR=${SOURCE_DIR}
            -DOUTPUT_DIR=${BINARY_DIR}/${output_name}
            -P ${SOURCE_DIR}/tools/${script}.cmake
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE result
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Renderer benchmark suite failed in ${script}")
    endif()
endfunction()

run_benchmark_suite(Prism5Benchmark prism5-benchmarks)
run_benchmark_suite(Glass2cBenchmark glass2c-benchmarks)
run_benchmark_suite(Glass3Benchmark glass3-benchmarks)
run_benchmark_suite(Glass4Benchmark glass4-benchmarks)
run_benchmark_suite(DeferredBenchmark deferred-benchmarks)
run_benchmark_suite(LocalLightsBenchmark local-lights-benchmarks)
run_benchmark_suite(InstanceStressBenchmark instance-stress-benchmarks)
run_benchmark_suite(ScreenSpaceBenchmark screen-space-benchmarks)
run_benchmark_suite(SkinningBenchmark skinning-benchmarks)
