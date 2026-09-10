add_executable(postprocesstest main.cpp)
target_link_libraries(postprocesstest PRIVATE gs2_postprocess)
add_test(NAME postprocesstest COMMAND postprocesstest "${CMAKE_BINARY_DIR}/resources")
set_tests_properties(postprocesstest PROPERTIES SKIP_RETURN_CODE 77)
