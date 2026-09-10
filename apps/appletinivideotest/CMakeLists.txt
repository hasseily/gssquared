add_executable(appletinivideotest main.cpp)

target_link_libraries(appletinivideotest PRIVATE gs2_displaypp)

add_test(NAME appletinivideotest COMMAND appletinivideotest)
