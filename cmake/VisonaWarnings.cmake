# Warning flags for Visona's own non-JUCE code (core and tests). Link visona::warnings PRIVATE.

add_library(visona_warnings INTERFACE)
add_library(visona::warnings ALIAS visona_warnings)

if(MSVC)
    target_compile_options(visona_warnings INTERFACE
        /W4 /permissive-
        $<$<BOOL:${VISONA_WARNINGS_AS_ERRORS}>:/WX>)
else()
    target_compile_options(visona_warnings INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wconversion
        -Wsign-conversion
        -Wdouble-promotion
        -Wold-style-cast
        -Wnon-virtual-dtor
        -Woverloaded-virtual
        -Wcast-align
        -Wformat=2
        -Wimplicit-fallthrough
        $<$<BOOL:${VISONA_WARNINGS_AS_ERRORS}>:-Werror>)
endif()
