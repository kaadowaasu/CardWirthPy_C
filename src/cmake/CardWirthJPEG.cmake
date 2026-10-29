# Py の通常の SDL_image の JPEG 復号は libjpeg-turbo 2.0.2
# を使う。共通復号と pygame 互換の JPEG 保存に同じ固定版の静的コーデ
# ックを使う。SDL3_image 独自の STB JPEG は画素の丸めが異なる。
include_guard(GLOBAL)
include(FetchContent)
function(cardwirth_configure_jpeg)
    # 元の版は CMP0077 導入前なので、オプションの上書きをこのアダ
    # プター内に限定する。取得したソースや親プロジェクトの方針は変えない。
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    set(ENABLE_SHARED OFF)
    set(ENABLE_STATIC ON)
    set(WITH_TURBOJPEG OFF)
    set(WITH_JAVA OFF)
    set(WITH_JPEG7 OFF)
    set(WITH_JPEG8 OFF)
    set(WITH_CRT_DLL OFF)
    # NASM/NEON の機械語オブジェクトは WebAssembly にリンク
    # できない。同じコーデック API と設定を、共通の C の本体で実行する。
    if(EMSCRIPTEN)
        set(WITH_SIMD OFF)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64|i[3-6]86|x86)$")
        # 元の 2.0.2 はアセンブラーなしでは設定に失敗する。ホスト
        # に NASM があれば SIMD を有効にし、なければホスト
        # DLL を要求せず、同じ共通コーデックを使えるようにする。
        include(CheckLanguage)
        check_language(ASM_NASM)
        if(NOT CMAKE_ASM_NASM_COMPILER)
            set(WITH_SIMD OFF)
            message(STATUS "JPEG: NASM unavailable; using the shared portable codec core")
        endif()
    endif()
    FetchContent_Declare(cardwirth_jpeg
        URL "https://codeload.github.com/libjpeg-turbo/libjpeg-turbo/zip/refs/tags/2.0.2"
        URL_HASH "SHA256=48f89f201e5baf130c722ebce2bc30775494d8f44fb2394d3c7896b59ec45f7e"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(cardwirth_jpeg)
    get_target_property(cardwirth_jpeg_type jpeg-static TYPE)
    if(NOT cardwirth_jpeg_type STREQUAL "STATIC_LIBRARY")
        message(FATAL_ERROR "The shared image codec requires static libjpeg-turbo")
    endif()
    target_include_directories(jpeg-static PUBLIC
        "${cardwirth_jpeg_SOURCE_DIR}" "${cardwirth_jpeg_BINARY_DIR}")
    add_library(JPEG::JPEG ALIAS jpeg-static)
    if(MSVC)
        target_compile_options(jpeg-static PRIVATE /utf-8)
    endif()
    file(READ "${cardwirth_jpeg_SOURCE_DIR}/LICENSE.md" cardwirth_jpeg_notice)
    set(cardwirth_jpeg_license "${CMAKE_CURRENT_BINARY_DIR}/generated/licenses/IJG-JPEG.txt")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated/licenses")
    file(WRITE "${cardwirth_jpeg_license}"
        "This software is based in part on the work of the Independent JPEG Group.\n\n${cardwirth_jpeg_notice}")
    set(CARDWIRTH_JPEG_SOURCE_DIR "${cardwirth_jpeg_SOURCE_DIR}" PARENT_SCOPE)
    set(CARDWIRTH_JPEG_LICENSE_FILE "${cardwirth_jpeg_license}" PARENT_SCOPE)
endfunction()
