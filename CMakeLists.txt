cmake_minimum_required(VERSION 3.24)

project(
    MCDevWorkbench
    VERSION 0.1.0
    DESCRIPTION "GUI workbench for Minecraft process, log, and debugging workflows"
    LANGUAGES CXX
)

option(MCDEVLINK_BUILD_TESTS "Build MCDevLink unit tests" OFF)
option(MCDEVLINK_BUILD_EXAMPLES "Build MCDevLink examples and manual MC probes" OFF)

set(MCDEVLINK_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/MCDevLink")
if(NOT EXISTS "${MCDEVLINK_SOURCE_DIR}/CMakeLists.txt")
    message(
        FATAL_ERROR
        "MCDevLink submodule is missing. Run: "
        "git submodule update --init --recursive"
    )
endif()

if(MCDEVLINK_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory("${MCDEVLINK_SOURCE_DIR}")

# Frontend targets should link this project-owned target instead of depending on
# the third-party target name throughout the application.
add_library(MCDevWorkbenchBackend INTERFACE)
add_library(MCDevWorkbench::Backend ALIAS MCDevWorkbenchBackend)
target_compile_features(MCDevWorkbenchBackend INTERFACE cxx_std_20)
target_link_libraries(MCDevWorkbenchBackend INTERFACE MCDevLink::MCDevLink)

