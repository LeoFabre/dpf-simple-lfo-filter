cmake_minimum_required(VERSION 3.15)
project(SimpleLFOFilter VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(dpf)

dpf_add_plugin(SimpleLFOFilter
    TARGETS vst3
    UI_TYPE opengl
    FILES_DSP
        plugins/SimpleLFOFilter/SimpleLFOFilterPlugin.cpp
    FILES_UI
        plugins/SimpleLFOFilter/SimpleLFOFilterUI.cpp
        plugins/SimpleLFOFilter/VSlider.cpp)

target_include_directories(SimpleLFOFilter
    PUBLIC plugins/SimpleLFOFilter)
