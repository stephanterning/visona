# Adapt only the platform header used by the pinned LSP measurement subset.
# Keep fetched sources pristine and put the generated header ahead of upstream.
function(visona_lsp_msvc_headers output_dir)
    set(source "${lsp_common_SOURCE_DIR}/include/lsp-plug.in/common/types.h")
    file(READ "${source}" contents)

    # Fail on pin drift instead of silently producing an incomplete adaptation.
    foreach(needle IN ITEMS "#include <unistd.h>"
            "#if (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)"
            "#if defined(__WORDSIZE) && (__WORDSIZE == 128)")
        string(FIND "${contents}" "${needle}" position)
        if(position EQUAL -1)
            message(FATAL_ERROR "Review the LSP MSVC adaptation: missing ${needle}")
        endif()
    endforeach()

    # MSVC has no POSIX unistd.h or ssize_t. ptrdiff_t is signed and pointer-sized.
    string(REPLACE "#include <unistd.h>" "typedef ptrdiff_t ssize_t;"
        contents "${contents}")
    # The supported Windows target is little-endian x64. Do not depend on
    # undefined GCC preprocessor macros comparing equal to zero in MSVC.
    string(REPLACE "#if (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)"
        "#if defined(_WIN32)\n    #define ARCH_LE\n#elif (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)"
        contents "${contents}")
    string(REPLACE "#if defined(__WORDSIZE) && (__WORDSIZE == 128)"
        "#if defined(_WIN64)\n        #define ARCH_64BIT\n    #elif defined(_WIN32)\n        #define ARCH_32BIT\n    #elif defined(__WORDSIZE) && (__WORDSIZE == 128)"
        contents "${contents}")

    set(header "${output_dir}/lsp-plug.in/common/types.h")
    file(MAKE_DIRECTORY "${output_dir}/lsp-plug.in/common")
    # configure_file avoids changing the header timestamp on every configure.
    file(WRITE "${header}.in" "${contents}")
    configure_file("${header}.in" "${header}" COPYONLY)
endfunction()
