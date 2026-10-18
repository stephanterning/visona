# A small, portable LSP target. Upstream meter and generic DSP sources are unmodified.
# See third_party/lsp/README.md for provenance and the intentionally limited API.
include(FetchContent)
FetchContent_Declare(lsp_units
    URL https://codeload.github.com/lsp-plugins/lsp-dsp-units/tar.gz/8dd2a6905ca7ccefd2c3624560235680cb8761e2
    URL_HASH SHA256=ac57a427db6de18010c483085ff05a7dd444191d24c0f3909eac0578f5f0690d)
FetchContent_Declare(lsp_dsp
    URL https://codeload.github.com/lsp-plugins/lsp-dsp-lib/tar.gz/refs/tags/1.1.1
    URL_HASH SHA256=5343d59e9625083dff3f19bfdb3e1615a77657d958b24ff77d782857d2469f57)
FetchContent_Declare(lsp_common
    URL https://codeload.github.com/lsp-plugins/lsp-common-lib/tar.gz/refs/tags/1.0.49
    URL_HASH SHA256=6c6a582ee0dcca12f62ed960bf7492ce60b42c6c6847ed14fac797b05b99b636)
FetchContent_MakeAvailable(lsp_units lsp_dsp lsp_common)

add_library(visona_lsp_stereo STATIC
    "${PROJECT_SOURCE_DIR}/third_party/lsp/StereoDsp.cpp"
    "${lsp_units_SOURCE_DIR}/src/main/meters/Correlometer.cpp"
    "${lsp_units_SOURCE_DIR}/src/main/meters/Panometer.cpp"
    "${lsp_units_SOURCE_DIR}/src/main/iface/IStateDumper.cpp")
set_target_properties(visona_lsp_stereo PROPERTIES POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
target_include_directories(visona_lsp_stereo SYSTEM PUBLIC
    "${lsp_units_SOURCE_DIR}/include" "${lsp_dsp_SOURCE_DIR}/include"
    "${lsp_common_SOURCE_DIR}/include")
target_compile_definitions(visona_lsp_stereo PUBLIC
    LSP_DSP_UNITS_BUILTIN LSP_DSP_LIB_BUILTIN LSP_COMMON_LIB_BUILTIN)

if(MSVC)
    include("${CMAKE_CURRENT_LIST_DIR}/LspMsvc.cmake")
    set(lsp_msvc_include "${CMAKE_CURRENT_BINARY_DIR}/lsp-msvc/include")
    visona_lsp_msvc_headers("${lsp_msvc_include}")
    target_include_directories(visona_lsp_stereo SYSTEM BEFORE PUBLIC "${lsp_msvc_include}")
endif()
