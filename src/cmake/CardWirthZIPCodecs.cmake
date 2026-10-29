# Py の ZipFile はホストのアーカイブ DLL なしで BZIP2・LZMA
# に対応する。全ホストで同じ共通アーカイブ API の下に両処理を同梱する。
include_guard(GLOBAL)
include(FetchContent)

function(cardwirth_configure_zip_codecs)
    if(TARGET cardwirth_bzip2 AND TARGET liblzma)
        return()
    endif()
    set(BUILD_SHARED_LIBS OFF)
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
    set(CMAKE_POLICY_DEFAULT_CMP0126 NEW)
    if(MSVC)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
    FetchContent_Declare(cardwirth_bzip2_source
        URL "https://sourceware.org/pub/bzip2/bzip2-1.0.8.tar.gz"
        URL_HASH "SHA256=ab5a03176ee106d3f0fa90e381da478ddae405918153cca248e682cd0c4a2269"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(cardwirth_bzip2_source)
    # この安定版は Makefile を持つため、OS 専用のビルドコマ
    # ンドや任意 DLL ではなく、ライブラリーのソースを一度宣言する。
    set(bzip2_sources)
    foreach(source IN ITEMS blocksort huffman crctable randtable compress decompress bzlib)
        list(APPEND bzip2_sources "${cardwirth_bzip2_source_SOURCE_DIR}/${source}.c")
    endforeach()
    add_library(cardwirth_bzip2 STATIC ${bzip2_sources})
    target_include_directories(cardwirth_bzip2 PUBLIC "${cardwirth_bzip2_source_SOURCE_DIR}")
    set_target_properties(cardwirth_bzip2 PROPERTIES POSITION_INDEPENDENT_CODE ON)
    if(MSVC)
        target_compile_definitions(cardwirth_bzip2 PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()

    # 必要なのは liblzma の基本処理だけ。実行ファイル、gettext
    # 、テスト、第二の並列展開方式を製品の依存に追加しない。
    set(XZ_NLS OFF)
    set(XZ_THREADS no)
    set(XZ_TOOL_XZ OFF)
    set(XZ_TOOL_XZDEC OFF)
    set(XZ_TOOL_LZMADEC OFF)
    set(XZ_TOOL_LZMAINFO OFF)
    set(XZ_TOOL_SCRIPTS OFF)
    set(XZ_DOC OFF)
    set(XZ_DOXYGEN OFF)
    set(BUILD_TESTING OFF)
    FetchContent_Declare(cardwirth_liblzma
        URL "https://github.com/tukaani-project/xz/releases/download/v5.8.4/xz-5.8.4.tar.gz"
        URL_HASH "SHA256=0014c7886930454fe8bd4228665b51af55eeae560ea135c9c4cd33f55b2591d9"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(cardwirth_liblzma)
    # 元のソース内ヘッダーは PRIVATE とされているため、導入済みパッ
    # ケージと同様に、埋め込み静的ターゲットの利用側へ公開 API を渡す。
    target_include_directories(liblzma PUBLIC
        # ソースツリーのパスは埋め込み依存のビルド中だけ有効。
        # 元のインストール用の出力は導入先のヘッダーを使う。
        "$<BUILD_INTERFACE:${cardwirth_liblzma_SOURCE_DIR}/src/liblzma/api>")
    foreach(codec IN ITEMS cardwirth_bzip2 liblzma)
        get_target_property(codec_type ${codec} TYPE)
        if(NOT codec_type STREQUAL "STATIC_LIBRARY")
            message(FATAL_ERROR "The shared ZIP backend requires static ${codec}")
        endif()
        set_target_properties(${codec} PROPERTIES POSITION_INDEPENDENT_CODE ON)
        if(MSVC)
            set_target_properties(${codec} PROPERTIES
                MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
            target_compile_options(${codec} PRIVATE /utf-8)
        endif()
    endforeach()

    # libarchive のシステム用 Find は、ソース内ター
    # ゲットのファイルができる前の設定時にリンクを試す。この二パッケ
    # ージだけ固定版のターゲットへ向ける。ヘッダーと静的 ABI は
    # 上の版で固定し、実際の復号・リンク能力は単体検査で確認する。
    file(WRITE "${CMAKE_FIND_PACKAGE_REDIRECTS_DIR}/bzip2-config.cmake"
        "set(BZIP2_FOUND TRUE)\nset(BZip2_FOUND TRUE)\nset(BZIP2_INCLUDE_DIR \"${cardwirth_bzip2_source_SOURCE_DIR}\")\nset(BZIP2_INCLUDE_DIRS \"${cardwirth_bzip2_source_SOURCE_DIR}\")\nset(BZIP2_LIBRARIES cardwirth_bzip2)\nset(USE_BZIP2_DLL FALSE)\nset(USE_BZIP2_STATIC TRUE)\n")
    file(WRITE "${CMAKE_FIND_PACKAGE_REDIRECTS_DIR}/liblzma-config.cmake"
        "set(LIBLZMA_FOUND TRUE)\nset(LibLZMA_FOUND TRUE)\nset(LIBLZMA_INCLUDE_DIR \"${cardwirth_liblzma_SOURCE_DIR}/src/liblzma/api\")\nset(LIBLZMA_INCLUDE_DIRS \"${cardwirth_liblzma_SOURCE_DIR}/src/liblzma/api\")\nset(LIBLZMA_LIBRARIES liblzma)\nset(WITHOUT_LZMA_API_STATIC FALSE)\nset(LZMA_API_STATIC TRUE)\nset(HAVE_LZMA_STREAM_ENCODER_MT FALSE)\n")
    file(READ "${cardwirth_bzip2_source_SOURCE_DIR}/LICENSE" bzip2_notice)
    file(READ "${cardwirth_liblzma_SOURCE_DIR}/COPYING" lzma_notice)
    file(READ "${cardwirth_liblzma_SOURCE_DIR}/COPYING.0BSD" lzma_license)
    set(CARDWIRTH_ZIP_CODEC_NOTICES
        "\n\n--- bzip2 1.0.8 ---\n${bzip2_notice}\n\n--- liblzma 5.8.4 ---\n${lzma_notice}\n${lzma_license}"
        PARENT_SCOPE)
endfunction()
