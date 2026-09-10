# The independent reference uses macOS OpenGL 4.1, while the product uses Metal.
add_executable(postprocessreferencetest main.cpp
    ../../src/display/postprocess/PostProcessPreset.cpp
)
target_include_directories(postprocessreferencetest PRIVATE ../../src)
target_link_libraries(postprocessreferencetest PRIVATE gs2_postprocess "-framework OpenGL")
add_dependencies(postprocessreferencetest assemble_resources)
add_test(NAME postprocess_reference
    COMMAND postprocessreferencetest
        "${CMAKE_BINARY_DIR}/resources"
        "${CMAKE_BINARY_DIR}/postprocess-reference-artifacts"
)
set_tests_properties(postprocess_reference PROPERTIES SKIP_RETURN_CODE 77)
