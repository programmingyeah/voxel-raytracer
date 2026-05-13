cmake_minimum_required(VERSION 3.10)
project(voxel_tracer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Vulkan
find_package(Vulkan REQUIRED)

# GLFW
find_package(PkgConfig REQUIRED)
pkg_search_module(GLFW REQUIRED glfw3)

# GLM
find_path(GLM_INCLUDE_DIR glm/glm.hpp REQUIRED)

# Shader compiler
if(DEFINED Vulkan_GLSLC_EXECUTABLE AND NOT Vulkan_GLSLC_EXECUTABLE STREQUAL "")
    set(GLSLC_EXECUTABLE "${Vulkan_GLSLC_EXECUTABLE}")
else()
    find_program(GLSLC_EXECUTABLE NAMES glslc REQUIRED)
endif()

set(SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/assets/shaders)
set(SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
set(COMPUTE_SHADER_SOURCE ${SHADER_SOURCE_DIR}/shader.comp)
set(COMPUTE_SHADER_OUTPUT ${SHADER_OUTPUT_DIR}/comp.spv)

add_custom_command(
    OUTPUT ${COMPUTE_SHADER_OUTPUT}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${COMPUTE_SHADER_SOURCE} -o ${COMPUTE_SHADER_OUTPUT}
    DEPENDS ${COMPUTE_SHADER_SOURCE}
    COMMENT "Compiling compute shader"
    VERBATIM
)

add_custom_target(compile_shaders DEPENDS ${COMPUTE_SHADER_OUTPUT})

# Source files
file(GLOB_RECURSE RENDER_SOURCES CONFIGURE_DEPENDS
    src/render/*.cpp
    src/world/*.cpp
)

add_executable(voxel_tracer
    src/main.cpp
    ${RENDER_SOURCES}
)

add_dependencies(voxel_tracer compile_shaders)

# Include directories
target_include_directories(voxel_tracer PRIVATE
    ${Vulkan_INCLUDE_DIRS}
    ${GLFW_INCLUDE_DIRS}
    ${GLM_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/render
    ${CMAKE_CURRENT_SOURCE_DIR}/src/world
)

# Link libraries
target_link_libraries(voxel_tracer
    ${Vulkan_LIBRARIES}
    ${GLFW_LIBRARIES}
    dl
)

# Pass runtime paths
target_compile_definitions(voxel_tracer PRIVATE
    SHADER_DIR="${SHADER_OUTPUT_DIR}"
)
