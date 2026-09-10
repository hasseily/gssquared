add_executable(capturetest main.cpp
    ../../src/ui/ScreenshotWriter.cpp ../../src/ui/Clipboard.cpp ../../src/util/Event.cpp ../../src/util/EventQueue.cpp)
target_include_directories(capturetest PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(capturetest PRIVATE SDL3::SDL3 SDL3_image::SDL3_image-shared)
add_test(NAME capturetest COMMAND capturetest)
