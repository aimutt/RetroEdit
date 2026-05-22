add_executable(RetroDocWriter
    src/main.cpp
    src/app/Application.cpp
    src/editor/FormattedTextBuffer.cpp
    src/editor/RichFileDocument.cpp
    src/editor/RichUndoHistory.cpp
    src/editor/RtfReader.cpp
    src/editor/RtfWriter.cpp
    src/render/WysiwygRenderer.cpp
    src/ui/RetroUi.cpp
)

target_include_directories(RetroDocWriter PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(RetroDocWriter PRIVATE RetroCore)

target_compile_options(RetroDocWriter PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /utf-8>
)

# Copy SDL3.dll, SDL3_ttf.dll, and assets/ next to the EXE after each build.
add_custom_command(TARGET RetroDocWriter POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:SDL3::SDL3>
        $<TARGET_FILE_DIR:RetroDocWriter>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:SDL3_ttf::SDL3_ttf>
        $<TARGET_FILE_DIR:RetroDocWriter>
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:RetroDocWriter>/assets
    COMMENT "Copying SDL3 DLLs and assets for RetroDocWriter"
)
