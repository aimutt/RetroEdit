cmake_minimum_required(VERSION 3.24)
project(RetroEdit VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Disable SDL build targets we don't need to keep compile times short
set(SDL_SHARED    ON  CACHE BOOL "Build SDL as a shared library" FORCE)
set(SDL_STATIC    OFF CACHE BOOL "Build SDL as a static library" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "Build SDL_test library"    FORCE)
set(SDL_TESTS     OFF CACHE BOOL "Build SDL tests"              FORCE)
set(SDL_EXAMPLES  OFF CACHE BOOL "Build SDL examples"           FORCE)
add_subdirectory(SDL)

# SDL_ttf — vendored prebuilt VC binaries (SDL3_ttf-devel-3.2.2-VC).
# The shipped Config.cmake ends with check_required_components(), a macro that
# CMake normally injects when packages are generated via
# configure_package_config_file. The vendored config is missing that injection,
# so define a no-op stub before find_package picks it up.
if(NOT COMMAND check_required_components)
    macro(check_required_components _name)
    endmacro()
endif()
find_package(SDL3_ttf CONFIG REQUIRED PATHS "${CMAKE_CURRENT_SOURCE_DIR}/SDL_ttf/cmake" NO_DEFAULT_PATH)

add_executable(RetroEdit
    src/main.cpp
    src/app/Application.cpp
    src/platform/Window.cpp
    src/render/ScreenBuffer.cpp
    src/render/RetroRenderer.cpp
    src/render/GlyphCache.cpp
    src/ui/RetroUi.cpp
    src/editor/TextBuffer.cpp
    src/editor/FileDocument.cpp
    src/editor/UndoHistory.cpp
)

target_compile_options(RetroEdit PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /utf-8>
)

target_include_directories(RetroEdit PRIVATE src)
target_link_libraries(RetroEdit PRIVATE SDL3::SDL3 SDL3_ttf::SDL3_ttf)

# Copy SDL3.dll and SDL3_ttf.dll next to the executable after each build
add_custom_command(TARGET RetroEdit POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:SDL3::SDL3>
        $<TARGET_FILE_DIR:RetroEdit>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:SDL3_ttf::SDL3_ttf>
        $<TARGET_FILE_DIR:RetroEdit>
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:RetroEdit>/assets
    COMMENT "Copying SDL3 DLLs and assets"
)
