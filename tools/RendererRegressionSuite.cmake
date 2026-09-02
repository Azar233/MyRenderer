if(NOT DEFINED RENDERER OR NOT DEFINED COMPARATOR OR NOT DEFINED SOURCE_DIR OR NOT DEFINED BINARY_DIR)
    message(FATAL_ERROR "RendererRegressionSuite requires RENDERER, COMPARATOR, SOURCE_DIR, and BINARY_DIR")
endif()

function(run_visual_suite script output_name)
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            -DRENDERER=${RENDERER}
            -DCOMPARATOR=${COMPARATOR}
            -DSOURCE_DIR=${SOURCE_DIR}
            -DOUTPUT_DIR=${BINARY_DIR}/${output_name}
            -P ${SOURCE_DIR}/tools/${script}.cmake
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE result
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Renderer regression suite failed in ${script}")
    endif()
endfunction()

run_visual_suite(Prism5VisualRegression prism5-visual-current)
run_visual_suite(Glass2cVisualRegression glass2c-visual-current)
run_visual_suite(Glass3VisualRegression glass3-visual-current)
run_visual_suite(Glass4VisualRegression glass4-visual-current)
run_visual_suite(DeferredVisualRegression deferred-visual-current)
run_visual_suite(LocalLightsVisualRegression local-lights-visual-current)
run_visual_suite(InstanceStressVisualRegression instance-stress-visual-current)
run_visual_suite(ScreenSpaceVisualRegression screen-space-visual-current)
run_visual_suite(SkinningVisualRegression skinning-visual-current)
run_visual_suite(FoundationVisualRegression foundation-visual-current)
