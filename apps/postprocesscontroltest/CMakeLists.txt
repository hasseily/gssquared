add_executable(postprocesscontroltest main.cpp
    ../../src/ui/NumericSlider.cpp ../../src/ui/TextInput.cpp
    ../../src/ui/Tile.cpp ../../src/util/TextRenderer.cpp
    ../../src/display/postprocess/PostProcessPreset.cpp)
target_include_directories(postprocesscontroltest PRIVATE ../../src)
target_link_libraries(postprocesscontroltest PRIVATE ${GS2_SDL3} ${GS2_SDL3_TTF})
add_test(NAME postprocesscontroltest COMMAND postprocesscontroltest "${CMAKE_SOURCE_DIR}/assets")
set_tests_properties(postprocesscontroltest PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy")
