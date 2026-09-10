add_executable(postprocessgoldentest main.cpp
    ../../src/display/postprocess/PostProcessPreset.cpp
)
target_include_directories(postprocessgoldentest PRIVATE ../../src)
target_link_libraries(postprocessgoldentest PRIVATE gs2_postprocess)
add_dependencies(postprocessgoldentest assemble_resources)
file(GLOB_RECURSE GS2_GOLDEN_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/goldens/*")
if(EMSCRIPTEN)
    target_link_options(postprocessgoldentest PRIVATE
        -pthread -sPTHREAD_POOL_SIZE=2
        -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=268435456 -sMAXIMUM_MEMORY=1gb
        -sSTACK_SIZE=4mb -sFORCE_FILESYSTEM=1
        -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2
        "SHELL:--preload-file \"${CMAKE_SOURCE_DIR}/assets/shaders/postprocess@/resources/shaders/postprocess\""
        "SHELL:--preload-file \"${CMAKE_SOURCE_DIR}/assets/postprocess@/resources/postprocess\""
        "SHELL:--preload-file \"${CMAKE_CURRENT_SOURCE_DIR}/goldens@/goldens\""
    )
    set_target_properties(postprocessgoldentest PROPERTIES SUFFIX ".html")
    set_property(TARGET postprocessgoldentest APPEND PROPERTY LINK_DEPENDS ${GS2_GOLDEN_FILES})
else()
    if(UNIX AND NOT APPLE)
        target_link_libraries(postprocessgoldentest PRIVATE OpenGL::GL)
    endif()
    add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/postprocess-goldens/.assembled.stamp"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/goldens" "${CMAKE_BINARY_DIR}/postprocess-goldens"
        COMMAND ${CMAKE_COMMAND} -E touch "${CMAKE_BINARY_DIR}/postprocess-goldens/.assembled.stamp"
        DEPENDS ${GS2_GOLDEN_FILES}
        VERBATIM
    )
    add_custom_target(postprocess_golden_assets
        DEPENDS "${CMAKE_BINARY_DIR}/postprocess-goldens/.assembled.stamp")
    add_dependencies(postprocessgoldentest postprocess_golden_assets)
    add_test(NAME postprocess_golden COMMAND postprocessgoldentest)
    set_tests_properties(postprocess_golden PROPERTIES SKIP_RETURN_CODE 77)
endif()
