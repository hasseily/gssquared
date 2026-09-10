add_executable(postprocesstest main.cpp
    ../../src/display/postprocess/PostProcessPreset.cpp
)
target_include_directories(postprocesstest PRIVATE ../../src)
target_link_libraries(postprocesstest PRIVATE gs2_postprocess)
if(UNIX AND NOT APPLE AND NOT EMSCRIPTEN)
    target_link_libraries(postprocesstest PRIVATE OpenGL::GL)
endif()
add_dependencies(postprocesstest assemble_resources)
add_test(NAME postprocess_gpu COMMAND postprocesstest)
set_tests_properties(postprocess_gpu PROPERTIES SKIP_RETURN_CODE 77)
