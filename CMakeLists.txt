cmake_minimum_required(VERSION 3.15)

option(CATFEEDER_ESP_IDF "Build the ESP-IDF firmware through PlatformIO" OFF)
if(CATFEEDER_ESP_IDF)
    include($ENV{IDF_PATH}/tools/cmake/project.cmake)
    project(CatFeeder)
    return()
endif()

project(CatFeeder LANGUAGES CXX)

add_library(catfeeder src/Controller.cpp src/Settings.cpp)
target_include_directories(catfeeder PUBLIC include)
target_compile_features(catfeeder PUBLIC cxx_std_11)
set_target_properties(catfeeder PROPERTIES CXX_EXTENSIONS OFF)

add_executable(catfeeder_sim simulation/main.cpp)
target_include_directories(catfeeder_sim PRIVATE simulation)
target_link_libraries(catfeeder_sim PRIVATE catfeeder)

add_executable(catfeeder_tests tests/controller_tests.cpp)
target_include_directories(catfeeder_tests PRIVATE simulation)
target_link_libraries(catfeeder_tests PRIVATE catfeeder)

foreach(target catfeeder catfeeder_sim catfeeder_tests)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /EHsc)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endforeach()

enable_testing()
add_test(NAME controller COMMAND catfeeder_tests)
