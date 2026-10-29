# Py の必須 LHA 機能は全環境で実行ファイルとともに提供する。
# 導入済みの任意 DLL に頼らず、固定版の静的 libarchive
# の LHA デコーダーを共通アーカイブ API から使う。
include_guard(GLOBAL)
include(FetchContent)
include("${CMAKE_CURRENT_LIST_DIR}/CardWirthZIPCodecs.cmake")

function(cardwirth_configure_lha)
    if(TARGET cardwirth_lha_backend)
        return()
    endif()
    set(BUILD_SHARED_LIBS OFF)
    # LHA と Py ZIP の BZIP2/LZMA は、この必須の静的バックエンドを共
    # 有する。ほかの任意コーデックは無効にし、既存の 7Z/CAB の実行時方針は変えない。
    cardwirth_configure_zip_codecs()
    set(ENABLE_BZip2 ON)
    set(ENABLE_LZMA ON)
    foreach(feature IN ITEMS MBEDTLS NETTLE OPENSSL LIBB2 LZ4 LZO ZSTD
            ZLIB LIBXML2 EXPAT WIN32_XMLLITE PCREPOSIX PCRE2POSIX LIBGCC
            CNG TAR TAR_SHARED CPIO CPIO_SHARED CAT CAT_SHARED UNZIP UNZIP_SHARED
            XATTR ACL ICONV TEST COVERAGE INSTALL WERROR)
        set(ENABLE_${feature} OFF)
    endforeach()
    set(MSVC_USE_STATIC_CRT ON)
    set(CMAKE_POLICY_DEFAULT_CMP0126 NEW)
    # LHA リーダーは正規表現 API を使わない。NONE で、GCC 以外のホス
    # トで任意の POSIX 正規表現検出が libgcc を要求することも避ける。
    set(POSIX_REGEX_LIB NONE)
    FetchContent_Declare(cardwirth_libarchive
        URL "https://libarchive.org/downloads/libarchive-3.8.5.tar.gz"
        URL_HASH "SHA256=8a60f3a7bfd59c54ce82ae805a93dba65defd04148c3333b7eaa2102f03b7ffd"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(cardwirth_libarchive)
    file(READ "${cardwirth_libarchive_BINARY_DIR}/config.h" archive_config)
    foreach(capability IN ITEMS HAVE_LIBBZ2 HAVE_LIBLZMA)
        if(NOT archive_config MATCHES "#define ${capability} 1")
            message(FATAL_ERROR "The shared archive backend lost ${capability}")
        endif()
    endforeach()
    get_target_property(archive_type archive_static TYPE)
    if(NOT archive_type STREQUAL "STATIC_LIBRARY")
        message(FATAL_ERROR "The shared LHA backend requires static libarchive")
    endif()
    set_target_properties(archive_static PROPERTIES POSITION_INDEPENDENT_CODE ON)
    # Py のデコーダーは七バイトに収まる完全な単一要素ブロックを受
    # け入れる。元のブロック開始検査は必要な 16 ビットがあっても
    # 全キャッシュを要求するため、この境界だけ既存の範囲付きビット
    # 検査を使う。ヘッダー・データの CRC とほかの検査は保つ。
    set(lha_original "${cardwirth_libarchive_SOURCE_DIR}/libarchive/archive_read_support_format_lha.c")
    file(READ "${lha_original}" lha_decoder)
    set(lha_block_guard "if (!lzh_br_read_ahead_0(strm, br, 16))")
    string(REGEX MATCHALL "if [(]!lzh_br_read_ahead_0[(]strm, br, 16[)][)]" lha_guards "${lha_decoder}")
    list(LENGTH lha_guards lha_guard_count)
    if(NOT lha_guard_count EQUAL 1)
        message(FATAL_ERROR "Pinned LHA decoder block-start boundary changed")
    endif()
    string(REPLACE "${lha_block_guard}"
        "/* CardWirth: Py permits a short complete block; require 16 bits, not a full cache. */\n\t\t\tif (!lzh_br_read_ahead(strm, br, 16))"
        lha_decoder "${lha_decoder}")
    # Py は共通のファイル名復号へ LHA の生バイトを渡す。libarchive
    # が Unicode パスを作る前に同じ処理をし、任意の iconv が共通 CP932
    # 境界、特に Android の JNI 変換を置き換えないようにする。
    set(lha_name_bridge [=[

/* CardWirth: raw LHA names use the same shared encoding API as other archives. */
extern const wchar_t *cardwirth_lha_name_wide(const char *, size_t, size_t *);
static int
cardwirth_lha_copy_name(struct archive_mstring *target, const char *raw, size_t size)
{
    size_t length = 0;
    const wchar_t *wide = cardwirth_lha_name_wide(raw, size, &length);
    if (wide == NULL)
        return -1;
    return archive_mstring_copy_wcs_len(target, wide, length);
}
static void
cardwirth_lha_copy_bytes(struct archive_string *target, const char *raw, size_t size)
{
    /* Preserve the declared byte span until the shared NUL/path validation;
     * archive_strncpy would silently turn an embedded NUL into a shorter name. */
    archive_string_empty(target);
    archive_array_append(target, raw, size);
}
]=])
    string(REPLACE "#include \"archive_endian.h\""
        "#include \"archive_endian.h\"\n${lha_name_bridge}" lha_decoder "${lha_decoder}")
    foreach(part IN ITEMS dirname filename)
        string(REPLACE "archive_strncpy(&lha->${part},"
            "cardwirth_lha_copy_bytes(&lha->${part}," lha_decoder "${lha_decoder}")
        if(part STREQUAL "dirname")
            set(converter sconv_dir)
        else()
            set(converter sconv_fname)
        endif()
        set(original_name_call "archive_mstring_copy_mbs_len_l(&conv_buffer, lha->${part}.s, lha->${part}.length, lha->${converter})")
        string(FIND "${lha_decoder}" "${original_name_call}" name_boundary)
        if(name_boundary LESS 0)
            message(FATAL_ERROR "Pinned LHA filename boundary changed")
        endif()
        string(REPLACE "${original_name_call}"
            "cardwirth_lha_copy_name(&conv_buffer, lha->${part}.s, lha->${part}.length)"
            lha_decoder "${lha_decoder}")
        string(REPLACE "archive_string_conversion_charset_name(lha->${converter})"
            "\"CardWirth shared filename encoding\"" lha_decoder "${lha_decoder}")
    endforeach()
    set(lha_patched "${cardwirth_libarchive_BINARY_DIR}/archive_read_support_format_lha.c")
    file(WRITE "${lha_patched}" "${lha_decoder}")
    # Py は有効な空の ZIP-LZMA 項目を読める。元の実装は出力
    # 窓を宣言サイズのゼロに制限し、liblzma が終端マーカーを消費
    # できなくなるため、使い切ったこの窓だけ一バイトの作業領域を与える。
    # 既存のサイズ・CRC 検査で宣言を超えた出力は引き続き拒否する。
    file(READ "${cardwirth_libarchive_SOURCE_DIR}/libarchive/archive_read_support_format_zip.c" zip_decoder)
    set(zip_lzma_output "zip->zipx_lzma_stream.avail_out =\n\t\t/* These lzma_alone streams lack end of stream marker, so let's\n\t\t * make sure the unpacker won't try to unpack more than it's\n\t\t * supposed to. */\n\t\t(size_t)zipmin((int64_t) zip->uncompressed_buffer_size,\n\t\t    zip->entry->uncompressed_size -\n\t\t    zip->entry_uncompressed_bytes_read);")
    string(FIND "${zip_decoder}" "${zip_lzma_output}" zip_lzma_boundary)
    if(zip_lzma_boundary LESS 0)
        message(FATAL_ERROR "Pinned ZIP-LZMA output boundary changed")
    endif()
    set(zip_lzma_replacement "/* CardWirth: allow valid zero-payload LZMA streams to consume their end marker. */\n\tif (zip->entry_uncompressed_bytes_read > zip->entry->uncompressed_size) {\n\t\tarchive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,\n\t\t    \"ZIP-LZMA output exceeds declared size\");\n\t\treturn (ARCHIVE_FATAL);\n\t}\n\tzip->zipx_lzma_stream.avail_out = zip->entry_uncompressed_bytes_read ==\n\t    zip->entry->uncompressed_size ? 1 :\n\t    (size_t)zipmin((int64_t)zip->uncompressed_buffer_size,\n\t        zip->entry->uncompressed_size - zip->entry_uncompressed_bytes_read);")
    string(REPLACE "${zip_lzma_output}" "${zip_lzma_replacement}" zip_decoder "${zip_decoder}")
    set(zip_patched "${cardwirth_libarchive_BINARY_DIR}/archive_read_support_format_zip.c")
    file(WRITE "${zip_patched}" "${zip_decoder}")
    get_target_property(lha_sources archive_static SOURCES)
    set(lha_rebound_sources)
    get_target_property(lha_source_directory archive_static SOURCE_DIR)
    foreach(source IN LISTS lha_sources)
        if(source MATCHES "(^|/)archive_read_support_format_lha[.]c$")
            list(APPEND lha_rebound_sources "${lha_patched}")
        elseif(source MATCHES "(^|/)archive_read_support_format_zip[.]c$")
            list(APPEND lha_rebound_sources "${zip_patched}")
        elseif(IS_ABSOLUTE "${source}" OR source MATCHES "^[$]<")
            list(APPEND lha_rebound_sources "${source}")
        else()
            list(APPEND lha_rebound_sources "${lha_source_directory}/${source}")
        endif()
    endforeach()
    set_property(TARGET archive_static PROPERTY SOURCES "${lha_rebound_sources}")
    if(MSVC)
        target_compile_options(archive_static PRIVATE /utf-8)
    endif()
    add_library(cardwirth_lha_backend INTERFACE)
    target_link_libraries(cardwirth_lha_backend INTERFACE archive_static)
    target_include_directories(cardwirth_lha_backend INTERFACE
        "${cardwirth_libarchive_SOURCE_DIR}/libarchive")
    target_compile_definitions(cardwirth_lha_backend INTERFACE
        CARDWIRTH_STATIC_LHA_BACKEND=1 LIBARCHIVE_STATIC)
    # COPYING は個別の通知文に従うよう指定する。ソースごとの
    # 通知も含め、静的に取り込む各オブジェクトの著作権表示を保つ。
    file(READ "${cardwirth_libarchive_SOURCE_DIR}/COPYING" lha_notices)
    string(APPEND lha_notices "${CARDWIRTH_ZIP_CODEC_NOTICES}")
    file(GLOB lha_notice_sources
        "${cardwirth_libarchive_SOURCE_DIR}/libarchive/*.c"
        "${cardwirth_libarchive_SOURCE_DIR}/libarchive/*.h")
    list(SORT lha_notice_sources)
    foreach(source IN LISTS lha_notice_sources)
        file(READ "${source}" prefix LIMIT 32768)
        string(FIND "${prefix}" "*/" notice_end)
        if(notice_end GREATER_EQUAL 0)
            math(EXPR notice_end "${notice_end}+2")
            string(SUBSTRING "${prefix}" 0 ${notice_end} notice)
            if(notice MATCHES "[Cc]opyright|[Pp]ublic [Dd]omain")
                get_filename_component(name "${source}" NAME)
                string(APPEND lha_notices "\n\n--- ${name} ---\n${notice}\n")
            endif()
        endif()
    endforeach()
    set(lha_notice_file "${cardwirth_libarchive_BINARY_DIR}/libarchive-LHA-notices.txt")
    file(WRITE "${lha_notice_file}" "${lha_notices}")
    set(CARDWIRTH_LHA_LICENSE_FILE "${lha_notice_file}"
        CACHE INTERNAL "Pinned LHA backend license" FORCE)
endfunction()

function(cardwirth_attach_lha target)
    cardwirth_configure_lha()
    target_link_libraries(${target} PRIVATE cardwirth_lha_backend)
endfunction()

# 単体検査を含む、共通アーカイブ実装のすべての利用側に接続する。構成
# や OS によって、Py の必須 LHA 機能を黙って失わせない。
function(cardwirth_attach_directory_lha)
    get_property(archive_consumers DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    foreach(consumer IN LISTS archive_consumers)
        get_target_property(consumer_sources ${consumer} SOURCES)
        foreach(source IN LISTS consumer_sources)
            string(REPLACE "\\" "/" source "${source}")
            if(source MATCHES "(^|/)cw/lib/archive[.]cpp$")
                cardwirth_attach_lha(${consumer})
                break()
            endif()
        endforeach()
    endforeach()
endfunction()

function(cardwirth_stage_lha_license target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:${target}>/licenses"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CARDWIRTH_LHA_LICENSE_FILE}"
            "$<TARGET_FILE_DIR:${target}>/licenses/libarchive-LHA.txt"
        VERBATIM)
endfunction()
