add_executable(renderresourcetest main.cpp
    ../../src/ui/AssetAtlas.cpp ../../src/util/TextRenderer.cpp)
target_include_directories(renderresourcetest PRIVATE ../../src)
target_link_libraries(renderresourcetest PRIVATE ${GS2_SDL3} ${GS2_SDL3_IMAGE} ${GS2_SDL3_TTF})
add_test(NAME renderresourcetest COMMAND renderresourcetest "${CMAKE_SOURCE_DIR}/assets")
