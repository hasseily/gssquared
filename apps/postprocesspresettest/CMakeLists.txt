add_executable(postprocesspresettest main.cpp
    ../../src/display/postprocess/PostProcessPreset.cpp)
target_include_directories(postprocesspresettest PRIVATE ../../src)
add_test(NAME postprocesspresettest COMMAND postprocesspresettest
    "${CMAKE_SOURCE_DIR}/assets/postprocess/presets")
