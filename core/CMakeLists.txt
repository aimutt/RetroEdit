add_library(RetroCore STATIC
    platform/AppData.cpp
    platform/Beep.cpp
    platform/Print.cpp
    platform/Window.cpp
    render/GlyphCache.cpp
    render/RetroRenderer.cpp
    render/ScreenBuffer.cpp
    render/Theme.cpp
    editor/Dictionary.cpp
    editor/Dictionary.gen.cpp
    editor/FileDocument.cpp
    editor/Palette.cpp
    editor/FileSettings.cpp
    editor/TextBuffer.cpp
    editor/UndoHistory.cpp
    editor/WordCount.cpp
)

# core/ is the include root for shared headers; consumers include them as
# e.g. #include "render/ScreenBuffer.h", #include "app/Cursor.h".
target_include_directories(RetroCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(RetroCore PUBLIC
    SDL3::SDL3
    SDL3_ttf::SDL3_ttf
    $<$<PLATFORM_ID:Windows>:Winmm>     # PlaySound, used by platform/Beep.cpp
    $<$<PLATFORM_ID:Windows>:Gdi32>     # CreateDC/TextOut/etc., used by platform/Print.cpp
    $<$<PLATFORM_ID:Windows>:Winspool>  # EnumPrinters/OpenPrinter/etc., used by platform/Print.cpp
)

target_compile_options(RetroCore PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /utf-8>
)

# The generated dictionary translation unit emits more than 65536 COFF
# sections in /O2 Release; compile it with /bigobj.
set_source_files_properties(editor/Dictionary.gen.cpp
    PROPERTIES COMPILE_OPTIONS "$<$<CXX_COMPILER_ID:MSVC>:/bigobj>"
)
