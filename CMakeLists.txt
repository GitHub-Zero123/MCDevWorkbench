cmake_minimum_required(VERSION 3.24)

project(
    MCDevWorkbench
    VERSION 0.1.0
    DESCRIPTION "GUI workbench for Minecraft process, log, and debugging workflows"
    LANGUAGES CXX
)

if(WIN32)
    enable_language(RC)
endif()

option(MCDEVLINK_BUILD_TESTS "Build MCDevLink unit tests" OFF)
option(MCDEVLINK_BUILD_EXAMPLES "Build MCDevLink examples and manual MC probes" OFF)

if(ANDROID)
    set(MCDEVWORKBENCH_GUI_DEFAULT OFF)
else()
    set(MCDEVWORKBENCH_GUI_DEFAULT ON)
endif()
option(MCDEVWORKBENCH_BUILD_GUI "Build the MCDevWorkbench EUI desktop application" ${MCDEVWORKBENCH_GUI_DEFAULT})

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

if(MCDEVWORKBENCH_BUILD_GUI)
    if(ANDROID)
        message(
            FATAL_ERROR
            "MCDevWorkbench GUI currently targets the EUI-NEO desktop branch. "
            "Configure Android with -DMCDEVWORKBENCH_BUILD_GUI=OFF until the "
            "EUI Android branch is rebased and its app glue is integrated."
        )
    endif()

    set(EUI_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/EUI-NEO")
    if(NOT EXISTS "${EUI_SOURCE_DIR}/CMakeLists.txt")
        message(
            FATAL_ERROR
            "EUI-NEO submodule is missing. Run: "
            "git submodule update --init --recursive"
        )
    endif()

    # MCDevWorkbench owns the executable; bundled EUI apps, fixtures, install
    # rules, and optional modules are not part of this build.
    set(EUI_BUILD_APPS OFF)
    set(EUI_BUILD_USER_APPS OFF)
    set(EUI_BUILD_TEST_FIXTURES OFF)
    set(EUI_ENABLE_INSTALL OFF)
    set(EUI_ENABLE_MODULES OFF)
    add_subdirectory("${EUI_SOURCE_DIR}")

    add_executable(
        MCDevWorkbench
        src/App.cpp
        src/BackendController.cpp
        src/BackendController.hpp
        src/platform/WindowTheme.cpp
        src/platform/WindowTheme.hpp
    )
    target_compile_features(MCDevWorkbench PRIVATE cxx_std_20)
    target_link_libraries(MCDevWorkbench PRIVATE MCDevWorkbench::Backend)

    # EUI-NEO v0.5.6 defines these values only in its subdirectory scope, but
    # eui_neo_configure_app() resolves them in the caller's scope. Recreate the
    # integration inputs here so the framework main() and application resource
    # are actually added to this parent-owned executable.
    if(EUI_WINDOW_BACKEND STREQUAL "sdl2")
        set(EUI_APP_MAIN_SOURCE "${EUI_SOURCE_DIR}/core/app/sdl2_app_main.cpp")
    else()
        set(EUI_APP_MAIN_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/EuiGlfwAppMain.cpp")
    endif()
    set(APP_ICON_RESOURCE "")
    if(WIN32)
        set(MCDEVWORKBENCH_ICON_ICO "${CMAKE_CURRENT_SOURCE_DIR}/assets/MCDevWorkbench.ico")
        if(NOT EXISTS "${MCDEVWORKBENCH_ICON_ICO}")
            message(FATAL_ERROR "MCDevWorkbench icon is missing: ${MCDEVWORKBENCH_ICON_ICO}")
        endif()
        set(MCDEVWORKBENCH_ICON_RC "${CMAKE_CURRENT_BINARY_DIR}/mcdev_workbench_icon.rc")
        file(TO_NATIVE_PATH "${MCDEVWORKBENCH_ICON_ICO}" MCDEVWORKBENCH_ICON_ICO_NATIVE)
        string(REPLACE "\\" "\\\\" MCDEVWORKBENCH_ICON_ICO_ESCAPED "${MCDEVWORKBENCH_ICON_ICO_NATIVE}")
        file(WRITE "${MCDEVWORKBENCH_ICON_RC}"
            "1 ICON \"${MCDEVWORKBENCH_ICON_ICO_ESCAPED}\"\n"
            "GLFW_ICON ICON \"${MCDEVWORKBENCH_ICON_ICO_ESCAPED}\"\n")
        set(APP_ICON_RESOURCE "${MCDEVWORKBENCH_ICON_RC}")
    endif()

    # This app uses system fonts and code-drawn UI only. The window icon above
    # is compiled into the executable, so no runtime assets directory is needed.
    set(EUI_APP_ASSETS_DIR "")
    eui_neo_configure_app(MCDevWorkbench)

    if(MSVC)
        # EUI disables these checks for non-Debug app targets. Restore them for
        # this network-facing process; later options take precedence in MSVC.
        target_compile_options(MCDevWorkbench PRIVATE /GS /sdl /W4 /permissive-)
    else()
        target_compile_options(MCDevWorkbench PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    if(WIN32)
        target_compile_definitions(MCDevWorkbench PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
        target_link_libraries(MCDevWorkbench PRIVATE dwmapi)
    endif()
endif()
