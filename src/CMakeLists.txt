cmake_minimum_required(VERSION 3.24)

project(CardWirthPy_C VERSION 0.1.0 LANGUAGES C CXX)

if(APPLE)
    # AppKit/UIKit の保存・文書プロバイダーのアダプターは Objective-C++
    # 。ゲーム・エディターの変換ソースは通常の共通 C++ のままにする。
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 専用の単体ホストは未変更の製品 Engine の依存をコンパイルす
# るが、実行可能なアプリとして梱包しない。通常ビルドでは素材検証・コ
# ピー・導入をすべて保持し、このオプションを製品構成として扱わない。
option(CARDWIRTH_TEST_ONLY "Skip Linux application packaging for a dedicated unit host" OFF)

# 非公開の暗号化 UserData は明示的な有効化が必要
# 。通常ビルドでは常に OFF とし、要求がない限り暗号コ
# ード、鍵処理、識別情報、暗号依存をバイナリーに含めない。
option(CARDWIRTH_ENGINE_PRIVATE_ENCRYPTION
    "Build Engine with direct encrypted UserData support and maintenance tool" OFF)

# 単一構成の暗号化 Release で、全体・Release
# の両フラグが消されたキャッシュを黙って再利用し
# ない。最適化なしでは描画・アニメーションを含む
# Engine 全体が遅くなり、暗号化と無関係な描
# 画でも数倍遅く見え得る。複数構成の生成器はビルド
# 時に Release フラグを選ぶため、この検査
# は Ninja 形式の単一構成だけに適用する。
if(CARDWIRTH_ENGINE_PRIVATE_ENCRYPTION AND
   NOT CMAKE_CONFIGURATION_TYPES AND
   CMAKE_BUILD_TYPE STREQUAL "Release")
    foreach(_cardwirth_language IN ITEMS C CXX)
        string(STRIP
            "${CMAKE_${_cardwirth_language}_FLAGS} ${CMAKE_${_cardwirth_language}_FLAGS_RELEASE}"
            _cardwirth_release_flags)
        if(_cardwirth_release_flags STREQUAL "")
            message(FATAL_ERROR
                "Encrypted Release ${_cardwirth_language} flags are empty; reconfigure with the toolchain's Release optimization flags")
        endif()
    endforeach()
    unset(_cardwirth_language)
    unset(_cardwirth_release_flags)
endif()

# 通常ビルドは全対応ツールチェーンで出力を抑える。診断時は -DCARDWIRTH_SUPPRESS_WARNINGS=OFF
# で解除できるが、コンパイ
# ラーのエラーは抑制しない。
option(CARDWIRTH_SUPPRESS_WARNINGS
    "Suppress build warnings for all CardWirth targets and platforms" ON)
if(CARDWIRTH_SUPPRESS_WARNINGS)
    # コンパイラー警告に加え、CMake の警告・状態
    # 出力も抑える。致命的・通常のエラーは表示する。
    set(CMAKE_MESSAGE_LOG_LEVEL ERROR)
    set(CMAKE_SUPPRESS_DEVELOPER_WARNINGS ON CACHE BOOL "" FORCE)
    set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "" FORCE)
    if(MSVC)
        add_compile_options(/w)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        add_compile_options(-w)
        # Emscripten と Apple/GNU のドライバーはリンク
        # 時にも警告を出す。コンパイルオプションはその呼び出しには届かない。
        add_link_options(-w)
    endif()
endif()

if(MSVC)
    # Windows はアプリ起動前に、暗黙リンクした CRT DLL
    # を lib/ から見つけられない。静的 CRT で実行ファイルの
    # 隣を整理し、任意のバックエンドだけを lib/ から動的に読む。
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# 単体版 Engine・Editor、または統合版を作る。既存
# IDE プロジェクト用に cardwirth_sample
# のターゲット名を保ち、下の出力名で選んだ製品を明確にする。
set(CARDWIRTH_APP_VARIANT "Engine" CACHE STRING "CardWirth application variant (Engine, OneWindow, Editor, Explorer, or All)")
set_property(CACHE CARDWIRTH_APP_VARIANT PROPERTY STRINGS Engine OneWindow Editor Explorer All)
if(NOT CARDWIRTH_APP_VARIANT STREQUAL "Engine" AND
   NOT CARDWIRTH_APP_VARIANT STREQUAL "OneWindow" AND
   NOT CARDWIRTH_APP_VARIANT STREQUAL "Editor" AND
   NOT CARDWIRTH_APP_VARIANT STREQUAL "Explorer" AND
   NOT CARDWIRTH_APP_VARIANT STREQUAL "All")
    message(FATAL_ERROR "CARDWIRTH_APP_VARIANT must be Engine, OneWindow, Editor, Explorer, or All")
endif()
if(CARDWIRTH_ENGINE_PRIVATE_ENCRYPTION AND
   NOT CARDWIRTH_APP_VARIANT STREQUAL "Engine" AND
   NOT CARDWIRTH_APP_VARIANT STREQUAL "OneWindow")
    message(FATAL_ERROR
        "CARDWIRTH_ENGINE_PRIVATE_ENCRYPTION is supported only by Engine or OneWindow")
endif()
set(CARDWIRTH_STANDALONE_ENGINE_HOST OFF)
if(CARDWIRTH_APP_VARIANT STREQUAL "Engine" AND
   NOT ANDROID AND NOT IOS AND NOT EMSCRIPTEN)
    # デスクトップ Engine は Windows・Linux・macOS
    # で同じ単体版の入口を使う。Android/iOS は、ネイティブの外
    # 側ウィンドウの寿命が同じ動作を管理できるまで、ホスト内の仮入口を使う。
    set(CARDWIRTH_STANDALONE_ENGINE_HOST ON)
endif()

option(CARDWIRTH_WEB_PTHREADS
    "Enable the required pthread-backed Web runtime" ON)
option(CARDWIRTH_WEB_SKIP_FINAL_OPTIMIZATION
    "Keep Release compilation but skip expensive final WebAssembly optimization passes" OFF)
option(CARDWIRTH_WEB_IO_SERVER
    "Run C++ on an application pthread and use browser-main JavaScript as the asynchronous I/O server" ON)
option(CARDWIRTH_WEB_ASYNCIFY_ADVISE
    "Print Binaryen Asyncify instrumentation decisions during the final Web link" OFF)
option(CARDWIRTH_WEB_ASYNCIFY_USE_ONLY_LIST
    "Use the generated variant-specific Asyncify instrumentation allowlist" ON)
if(EMSCRIPTEN AND NOT CARDWIRTH_APP_VARIANT STREQUAL "All" AND
   NOT CARDWIRTH_APP_VARIANT STREQUAL "Engine" AND
   NOT CARDWIRTH_APP_VARIANT STREQUAL "Editor")
    message(FATAL_ERROR
        "The Web frontend supports CARDWIRTH_APP_VARIANT=Engine, Editor or All")
endif()
if(EMSCRIPTEN AND NOT CARDWIRTH_WEB_PTHREADS)
    message(FATAL_ERROR
        "Web W0 is no longer supported; CARDWIRTH_WEB_PTHREADS must be ON")
endif()
include(cmake/web_exceptions.cmake)
if(EMSCRIPTEN)
    # 共有メモリーの WebAssembly にリンクする全オブジェクトは、同梱 SDL
    # 依存も含め atomics/bulk-memory 用にコンパイルする。
    add_compile_options(-pthread)
    add_link_options(-pthread -sCASE_INSENSITIVE_FS=1)
endif()

set(CARDWIRTH_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
if(NOT WIN32)
    # 旧 SDL サンプルは test_resource を使えるが、CardWirth
    # 製品は AppPaths から実行時のルートを読む。素材ツリーは任意とし
    # 、ローカル素材のない新しいチェックアウトでも各製品を設定できるようにする。
    set(CARDWIRTH_RESOURCE_DIR "${CARDWIRTH_ROOT}/test_resource")
    if(NOT EXISTS "${CARDWIRTH_RESOURCE_DIR}")
        message(STATUS "Optional test_resource directory not found; sample media will be unavailable")
    endif()
endif()

# Android は SDLActivity が読み込むため、SDL 自
# 体を共有ライブラリーにする。ほかの環境は静的リンクで自己完結させる。
if(ANDROID)
    set(SDL_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
else()
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
endif()
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
# Classic CardWirth のフィールドは CP932 を使う。SDL の共通 iconv
# の代替は Unicode・Latin 系しか知らないため、macOS はシステムの
# 変換表を使う。共通デコーダーへ届く前に CP932 の全バイトが置換されるのを防ぐ。
if(APPLE)
    set(SDL_SYSTEM_ICONV ON CACHE BOOL "" FORCE)
endif()
if(EMSCRIPTEN)
    # SDL はアプリと同じスレッド方式でビルドする。add_subdirectory()
    # より前に設定し、SDL の機能確認にも W1 パッケージと同
    # じ Emscripten の pthread フラグを渡す。
    set(SDL_PTHREADS ON CACHE BOOL "" FORCE)
endif()
include("${CMAKE_CURRENT_SOURCE_DIR}/tools/apply_sdl_patches.cmake")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/platform/patches/sdl-ime.patch"
    "${CMAKE_CURRENT_SOURCE_DIR}/platform/patches/sdl-cocoa-hide-window.patch")
add_subdirectory(3rdparty/SDL EXCLUDE_FROM_ALL)

# 拡張ライブラリーはアプリ・Android の共通 main ラ
# イブラリーへ静的リンクし、この製品で使う形式だけを有効にする。
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

set(SDLIMAGE_VENDORED ON CACHE BOOL "" FORCE)
set(SDLIMAGE_DEPS_SHARED OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_INSTALL OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_TESTS OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_BACKEND_STB ON CACHE BOOL "" FORCE)
set(SDLIMAGE_BACKEND_WIC OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_BACKEND_IMAGEIO OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_ANI ON CACHE BOOL "" FORCE)
set(SDLIMAGE_AVIF OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_BMP ON CACHE BOOL "" FORCE)
set(SDLIMAGE_GIF ON CACHE BOOL "" FORCE)
set(SDLIMAGE_JPG ON CACHE BOOL "" FORCE)
set(SDLIMAGE_JXL OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_LBM OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_PCX OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_PNG ON CACHE BOOL "" FORCE)
# APNG のアニメーション復号は SDL_image の libpng
# 経路を使う。同梱の libpng/zlib を静的に有効にし、システムの
# DLL・共有ライブラリーに依存せず全 OS で同じデコーダーを使う。
set(SDLIMAGE_PNG_LIBPNG ON CACHE BOOL "" FORCE)
set(SDLIMAGE_PNG_SHARED OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_PNM OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_QOI OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_SVG OFF CACHE BOOL "" FORCE)
# JPDC の pygame 互換の代替保存は、ファイル名に関係なく
# TGA を書く。共通デコーダーもその保存画像を開けるようにする。
set(SDLIMAGE_TGA ON CACHE BOOL "" FORCE)
set(SDLIMAGE_TIF OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_WEBP ON CACHE BOOL "" FORCE)
set(SDLIMAGE_WEBP_SHARED OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_XCF OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_XPM OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_XV OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_BMP_SAVE OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_ANI_SAVE ON CACHE BOOL "" FORCE)
set(SDLIMAGE_GIF_SAVE ON CACHE BOOL "" FORCE)
# Editor の画像出力は全 OS で同じ SDL コーデックを使う。PNG
# 保存を有効に保つ。SDL 内蔵の書き込み処理は libpng を必要としない。
set(SDLIMAGE_PNG_SAVE ON CACHE BOOL "" FORCE)
set(SDLIMAGE_WEBP_SAVE ON CACHE BOOL "" FORCE)
if(IOS)
    # iOS では CMake が MACOSX_BUNDLE を全体に有効にする。SDL_image
    # 同梱の libwebp のホスト専用 webpmux は通常のビルドツ
    # ールのままにし、iOS でも RUNTIME の導入規則を有効に保つ。
    set(CARDWIRTH_IOS_SAVED_MACOSX_BUNDLE "${CMAKE_MACOSX_BUNDLE}")
    set(CMAKE_MACOSX_BUNDLE OFF)
endif()
add_subdirectory(3rdparty/SDL_image EXCLUDE_FROM_ALL)
if(IOS)
    set(CMAKE_MACOSX_BUNDLE "${CARDWIRTH_IOS_SAVED_MACOSX_BUNDLE}")
    unset(CARDWIRTH_IOS_SAVED_MACOSX_BUNDLE)
endif()

set(SDLMIXER_VENDORED ON CACHE BOOL "" FORCE)
set(SDLMIXER_INSTALL OFF CACHE BOOL "" FORCE)
set(SDLMIXER_TESTS OFF CACHE BOOL "" FORCE)
set(SDLMIXER_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDLMIXER_AIFF OFF CACHE BOOL "" FORCE)
set(SDLMIXER_WAVE ON CACHE BOOL "" FORCE)
set(SDLMIXER_VOC OFF CACHE BOOL "" FORCE)
set(SDLMIXER_AU OFF CACHE BOOL "" FORCE)
set(SDLMIXER_FLAC ON CACHE BOOL "" FORCE)
set(SDLMIXER_FLAC_LIBFLAC OFF CACHE BOOL "" FORCE)
set(SDLMIXER_FLAC_DRFLAC ON CACHE BOOL "" FORCE)
set(SDLMIXER_GME OFF CACHE BOOL "" FORCE)
set(SDLMIXER_MOD OFF CACHE BOOL "" FORCE)
set(SDLMIXER_MP3 ON CACHE BOOL "" FORCE)
set(SDLMIXER_MP3_DRMP3 ON CACHE BOOL "" FORCE)
set(SDLMIXER_MP3_MPG123 OFF CACHE BOOL "" FORCE)
set(SDLMIXER_MIDI ON CACHE BOOL "" FORCE)
set(SDLMIXER_MIDI_FLUIDSYNTH OFF CACHE BOOL "" FORCE)
set(SDLMIXER_MIDI_TIMIDITY ON CACHE BOOL "" FORCE)
set(SDLMIXER_OPUS OFF CACHE BOOL "" FORCE)
set(SDLMIXER_VORBIS_STB ON CACHE BOOL "" FORCE)
set(SDLMIXER_VORBIS_VORBISFILE OFF CACHE BOOL "" FORCE)
set(SDLMIXER_VORBIS_TREMOR OFF CACHE BOOL "" FORCE)
set(SDLMIXER_WAVPACK OFF CACHE BOOL "" FORCE)
# 依存の再取得後も、ローカルのフェード取り消し API を保つ。
include("${CMAKE_CURRENT_SOURCE_DIR}/tools/apply_sdl_mixer_patches.cmake")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/platform/patches/sdl-mixer-cancel-fade-in.patch")
add_subdirectory(3rdparty/SDL_mixer EXCLUDE_FROM_ALL)

set(SDLTTF_VENDORED ON CACHE BOOL "" FORCE)
set(SDLTTF_INSTALL OFF CACHE BOOL "" FORCE)
set(SDLTTF_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDLTTF_HARFBUZZ OFF CACHE BOOL "" FORCE)
set(SDLTTF_PLUTOSVG OFF CACHE BOOL "" FORCE)
add_subdirectory(3rdparty/SDL_ttf EXCLUDE_FROM_ALL)

file(GLOB_RECURSE CARDWIRTH_CW_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/cw/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/cw/*.hpp"
)
list(REMOVE_ITEM CARDWIRTH_CW_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/cw/lib/simd_image.cpp")
# 画素の基本処理は ALL も含め Editor と Engine で共有する。
add_library(cardwirth_simd_image STATIC cw/lib/simd_image.cpp)
target_include_directories(cardwirth_simd_image PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(cardwirth_simd_image PUBLIC cxx_std_20)

# Engine、Explorer、統合 ALL はルート付きの保存処理
# と実行時パスの境界を共有する。この一覧は CARDWIRTH_CW_SOURCES
# の外に置き、単体版 Editor の独立性を保つ。
set(CARDWIRTH_PLATFORM_SOURCES
    platform/storage/storage.cpp
    platform/storage/storage.hpp
    platform/storage/native_storage.hpp
    platform/storage/engine_encryption.hpp
    platform/storage/storage_mount.cpp
    platform/storage/storage_mount.hpp
    platform/storage/app_paths.cpp
    platform/storage/app_paths.hpp
    platform/storage/app_runtime_context.cpp
    platform/storage/app_runtime_context.hpp
    platform/app_paths.hpp
    platform/app_runtime_context.hpp
)
if(NOT CARDWIRTH_ENGINE_PRIVATE_ENCRYPTION)
    list(APPEND CARDWIRTH_PLATFORM_SOURCES
        platform/storage/engine_encryption_stub.cpp)
endif()

set(CARDWIRTH_ENTRY_SOURCE app/main.cpp)
if(CARDWIRTH_APP_VARIANT STREQUAL "Editor")
    set(CARDWIRTH_ENTRY_SOURCE app/cwxeditor_main.cpp)
elseif(CARDWIRTH_STANDALONE_ENGINE_HOST)
    # 単体版は自身のネイティブ Frame を持つ。共通 Presentation
    # モデルは変えず、呼び出し側が選ぶ外側のホストだけを ALL と変える。
    set(CARDWIRTH_ENTRY_SOURCE app/cardwirth_main.cpp)
elseif(CARDWIRTH_APP_VARIANT STREQUAL "Engine")
    # すべての Engine ターゲットが同じ SDL ホス
    # トと共通表示の境界を使う。OS・製品の違いはホストの引
    # 数だけとし、第二のアプリ側 UI 実装は持たせない。
    set(CARDWIRTH_ENTRY_SOURCE app/multi_app_main.cpp)
elseif(NOT CARDWIRTH_APP_VARIANT STREQUAL "Engine")
    set(CARDWIRTH_ENTRY_SOURCE app/multi_app_main.cpp)
endif()

set(CARDWIRTH_SHELL_SOURCES
        app/app_shell.cpp
        app/app_shell.hpp
        app/process.cpp
        app/process.hpp
        app/explorer/archive.cpp
        app/explorer/archive.hpp
        app/explorer/component.cpp
        app/explorer/component.hpp
        app/explorer/explorer.cpp
        app/explorer/explorer.hpp
        app/explorer/filesystem.cpp
        app/explorer/filesystem.hpp
        app/explorer/modal.cpp
        app/explorer/modal.hpp
        app/explorer/preview.cpp
        app/explorer/preview.hpp
        app/explorer/platform_transfer.cpp
        app/explorer/platform_transfer.hpp
        app/explorer/text_document.cpp
        app/explorer/text_document.hpp
        app/ui/backend.cpp
        app/ui/backend.hpp
        app/editor_launch.cpp
        app/editor_launch.hpp
        app/ui/widget.hpp
)

if(CARDWIRTH_APP_VARIANT STREQUAL "Editor")
    # CWXEditor は自身の OS コンポーネントと入口を持つ。単体
    # 版をすべての Engine・SDL シェルのソースから独立させる。
    set(CARDWIRTH_SOURCES
        ${CARDWIRTH_ENTRY_SOURCE}
    )
elseif(CARDWIRTH_APP_VARIANT STREQUAL "Explorer")
    # スマートフォン向け Explorer をゲーム・エディター
    # から独立させる。ビルドを短縮し、OS 固有の Engine
    # ソースが Android アプリへ混入するのを防ぐ。
    set(CARDWIRTH_SOURCES
        ${CARDWIRTH_ENTRY_SOURCE}
        app/platform_font.cpp
        app/platform_font.hpp
        ${CARDWIRTH_SHELL_SOURCES}
        ${CARDWIRTH_PLATFORM_SOURCES}
        cw/lib/archive.cpp
        cw/lib/archive.hpp
        cw/lib/encoding.cpp
        cw/lib/encoding.hpp
        # Explorer のテキスト編集は共通のクリップボード境界を使う。
        cw/lib/system_clipboard.cpp
        cw/lib/system_clipboard.hpp
    )
else()
    set(CARDWIRTH_SOURCES
        ${CARDWIRTH_ENTRY_SOURCE}
        app/debug_control_server.cpp
        app/debug_control_server.hpp
        app/engine_runtime.cpp
        app/engine_runtime.hpp
        ${CARDWIRTH_PLATFORM_SOURCES}
        ${CARDWIRTH_CW_SOURCES}
    )
    if(NOT CARDWIRTH_APP_VARIANT STREQUAL "All")
        # Engine は cwxeditor_lib を持たない。ALL は
        # 下の cwxeditor_lib からこの一つの共通実装を取得する。
        list(APPEND CARDWIRTH_SOURCES
            app/platform_font.cpp
            app/platform_font.hpp)
    endif()
    if(NOT CARDWIRTH_STANDALONE_ENGINE_HOST)
        list(APPEND CARDWIRTH_SOURCES ${CARDWIRTH_SHELL_SOURCES})
    endif()
    if(CARDWIRTH_STANDALONE_ENGINE_HOST)
        # Web を含む単体版 Engine は、Explorer の UI
        # を取り込まず、共通のメモリー上の MIDI プレーヤーをリンクする。
        list(APPEND CARDWIRTH_SOURCES
            app/explorer/midi_player.cpp
            app/explorer/midi_player.hpp
        )
    endif()
    if(CARDWIRTH_STANDALONE_ENGINE_HOST AND NOT EMSCRIPTEN)
        if(ANDROID OR IOS OR EMSCRIPTEN)
            list(APPEND CARDWIRTH_SOURCES
                app/explorer/platform_transfer.cpp
                app/explorer/platform_transfer.hpp)
        endif()
        # 単体版 Unix の入口も、SDL ホストと同じ UserData の配置準
        # 備を使う。Apple は bundle の Resources に書かず、Linux
        # の外部プロファイルには管理された Data の互換配置を用意する。
        list(APPEND CARDWIRTH_SOURCES
            app/app_shell.cpp
            app/app_shell.hpp)
    endif()
    if(CARDWIRTH_APP_VARIANT STREQUAL "All" OR
       CARDWIRTH_APP_VARIANT STREQUAL "OneWindow" OR
       (CARDWIRTH_APP_VARIANT STREQUAL "Engine" AND
        NOT CARDWIRTH_STANDALONE_ENGINE_HOST))
        # Engine は ALL と同じ SDL ホ
        # ストの単一製品設定。Engine を直ちに起
        # 動し、ALL の製品選択画面は表示しない。
        list(APPEND CARDWIRTH_SOURCES
            app/all/all_display_mode.cpp
            app/all/all_display_mode.hpp
            app/all/all_dialog_layer.cpp
            app/all/all_dialog_layer.hpp
            app/all/all_layout.cpp
            app/all/all_layout.hpp
        )
    endif()
endif()

# Web 以外のネイティブ ALL は、全環境で一つの共通パッケージ導入処理を使う。
if(CARDWIRTH_APP_VARIANT STREQUAL "All" AND NOT EMSCRIPTEN)
    list(APPEND CARDWIRTH_SOURCES
        app/additional_data.cpp
        app/additional_data.hpp)
endif()

# MIDI は共通 Editor/ALL の音声バックエンドに含む。Explorer
# と SDL Engine ホストも同じプレーヤーを直接必要とするが、cwxeditor_lib
# をリンクしないため、それぞれ一度だけ組み込む。
if(NOT CARDWIRTH_APP_VARIANT STREQUAL "Editor" AND
   NOT CARDWIRTH_APP_VARIANT STREQUAL "All" AND
   NOT CARDWIRTH_STANDALONE_ENGINE_HOST)
    list(APPEND CARDWIRTH_SOURCES
        app/explorer/midi_player.cpp
        app/explorer/midi_player.hpp)
endif()

# Engine は cwxeditor_lib に依存しないため、同じ OS 専用の文書アダ
# プターをここでコンパイルする。ALL は下の cwxeditor_lib から取得する。
if(APPLE AND (CARDWIRTH_APP_VARIANT STREQUAL "Engine" OR
              CARDWIRTH_APP_VARIANT STREQUAL "OneWindow"))
    list(APPEND CARDWIRTH_SOURCES
        app/explorer/platform_transfer_apple.hpp
        app/explorer/platform_transfer_apple.mm)
    set_source_files_properties(app/explorer/platform_transfer_apple.mm
        PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
endif()

if(APPLE AND (CARDWIRTH_APP_VARIANT STREQUAL "Engine" OR
              CARDWIRTH_APP_VARIANT STREQUAL "OneWindow" OR
              CARDWIRTH_APP_VARIANT STREQUAL "All" OR
              CARDWIRTH_APP_VARIANT STREQUAL "Editor" OR
              CARDWIRTH_APP_VARIANT STREQUAL "Explorer"))
    list(APPEND CARDWIRTH_SOURCES
        app/platform_apple.hpp
        app/platform_apple.mm)
    # ALL と Editor は共通 Editor ライブラリーから文
    # 書プロバイダーを取得する。アダプターがリンク時・実行時に呼ぶため
    # 、ネイティブの書き込み可能ルートの提供処理は製品のソースに残す。
    if(CARDWIRTH_APP_VARIANT STREQUAL "Explorer")
        list(APPEND CARDWIRTH_SOURCES
            app/explorer/platform_transfer_apple.hpp
            app/explorer/platform_transfer_apple.mm)
        set_source_files_properties(app/explorer/platform_transfer_apple.mm
            PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    endif()
    set_source_files_properties(app/platform_apple.mm
        PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
endif()

include(CTest)
include(cmake/EditorTestBundle.cmake)

# 独立した追加検査も一つの実行ファイルを共有するが、CTest はプロセスを分離する。
set(CARDWIRTH_ADDITIONAL_BUNDLE_CASES
    cwxeditor_recovered_xml_values_tests
    editor_expression_for_repair_tests
    editor_save_generation_repair_tests
    editor_unlink_history_repair_tests
    editor_resource_history_repair_tests
    editor_indexed_handcard_repair_tests
    editor_custom_label_repair_tests
    cwxeditor_recovered_talk_dialog_apply_tests
    editor_card_apply_repair_tests
    editor_coupon_conversion_repair_tests
    editor_initial_card_command_tests
    editor_cardpane_life_mark_geometry_tests
    editor_cast_aptitude_source_scale_tests
    editor_settings_message_contract_tests
    editor_scene_contract_tests
    editor_party_directory_command_tests
    editor_variable_state_command_tests
    editor_card_creation_pending_tests
    editor_saveas_close_transaction_tests
    editor_dialog_retirement_ownership_tests
    editor_shortcut_shell_ownership_tests
    editor_file_sync_backup_tests
    editor_variable_state_contract_tests
    editor_shared_use_history_tests
    editor_scenario_creation_contract_tests
    editor_settings_skin_lifecycle_tests
    editor_scene_payload_contract_tests
    editor_party_launch_history_tests
    editor_clipboard_format_contract_tests
    editor_handcard_transfer_contract_tests
    editor_image_transfer_contract_tests
    editor_reference_navigation_contract_tests
    editor_model_owner_dirty_tests
    editor_flag_history_contract_tests
    editor_template_refresh_contract_tests
    editor_history_autoapply_tests
    editor_supplemental_value_contract_tests
    editor_flag_default_projection_tests
    editor_chooser_api_residual_tests
    editor_message_template_recovered_tests
    editor_scene_selection_recovered_tests
    editor_scene_route_residual_tests
    editor_scene_commands_recovered_tests
    editor_image_api_residual_tests
    editor_customtable_recovered_tests
    editor_navigation_drag_recovered_tests
    editor_shared_render_api_tests
    editor_scene_hidden_selection_recovered_tests
    editor_live_preview_recovered_tests
    cwxeditor_ui_contract_tests
    editor_gui_recovery_root_tests
    editor_gui_card_recovery_batch01_tests
    editor_gui_card_recovery_batch02_tests
    editor_gui_card_recovery_batch03_tests
    editor_scene_deletion_snapshot_tests
    editor_resource_deletion_snapshot_tests
    editor_gui_card_recovery_batch04_tests
    editor_gui_card_recovery_batch05_tests
    editor_gui_card_recovery_batch06_tests
    editor_shared_gui_recovery_batch05_tests
    editor_save_sound_regression_tests
    editor_event_lifecycle_api_e76_e77_tests
    editor_gui_search_refresh_api_e81_e82_tests
    editor_gui_search_candidate_api_e52_e53_e92_tests
    editor_event_navigation_api_e85_e86_e97_tests
    editor_text_tabs_regression_tests
    editor_start_reference_navigation_tests
    editor_event_scene_commands_api_e30_tests
    editor_instance_receiver_startup_tests
    editor_event_tree_drag_api_e43_tests
    editor_apir058_battle_settings_delta_tests
    editor_apir058_bgm_exception_tests
)
if(BUILD_TESTING AND NOT ANDROID AND NOT IOS)
    add_executable(cardwirth_keycode_level_tests tests/keycode_level_tests.cpp)
    target_compile_features(cardwirth_keycode_level_tests PRIVATE cxx_std_20)
    target_include_directories(cardwirth_keycode_level_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    if(MSVC)
        target_compile_options(cardwirth_keycode_level_tests PRIVATE /W4 /EHsc /utf-8)
    endif()
    add_test(NAME keycode_level_contract COMMAND cardwirth_keycode_level_tests)

    add_executable(cardwirth_novel_mode_tests
        tests/novel_mode_tests.cpp
        cw/novel.cpp
        platform/storage/storage.cpp
        platform/storage/storage_mount.cpp)
    target_compile_features(cardwirth_novel_mode_tests PRIVATE cxx_std_20)
    target_include_directories(cardwirth_novel_mode_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}")
    if(MSVC)
        target_compile_options(cardwirth_novel_mode_tests PRIVATE /W4 /EHsc /utf-8)
    else()
        target_compile_options(cardwirth_novel_mode_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME novel_mode_contract COMMAND cardwirth_novel_mode_tests)

    add_executable(cardwirth_simd_image_tests
        tests/simd_image_tests.cpp
        cw/lib/simd_image.cpp)
    target_compile_features(cardwirth_simd_image_tests PRIVATE cxx_std_20)
    target_include_directories(cardwirth_simd_image_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}")
    if(MSVC)
        target_compile_options(cardwirth_simd_image_tests PRIVATE /W4 /EHsc /utf-8)
    else()
        target_compile_options(cardwirth_simd_image_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME simd_image_exactness COMMAND cardwirth_simd_image_tests)
endif()
if(BUILD_TESTING AND NOT ANDROID AND NOT IOS)
    add_executable(cardwirth_next_binary_reader_tests
        tests/next_binary_reader_tests.cpp)
    target_compile_features(cardwirth_next_binary_reader_tests PRIVATE cxx_std_20)
    target_include_directories(cardwirth_next_binary_reader_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}")
    if(MSVC)
        target_compile_options(cardwirth_next_binary_reader_tests PRIVATE
            /W4 /EHsc /utf-8)
    else()
        target_compile_options(cardwirth_next_binary_reader_tests PRIVATE
            -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME next_binary_reader COMMAND cardwirth_next_binary_reader_tests)
endif()
if(BUILD_TESTING AND NOT ANDROID AND NOT IOS)
    add_executable(cardwirth_explorer_tests
        tests/explorer_tests.cpp
        app/explorer/archive.cpp
        app/explorer/archive.hpp
        app/explorer/explorer.cpp
        app/explorer/explorer.hpp
        app/explorer/filesystem.cpp
        app/explorer/filesystem.hpp
        app/explorer/modal.cpp
        app/explorer/modal.hpp
        app/explorer/midi_player.cpp
        app/explorer/midi_player.hpp
        app/explorer/preview.cpp
        app/explorer/preview.hpp
        app/explorer/text_document.cpp
        app/explorer/text_document.hpp
        # 文脈に基づく Explorer アダプターも検
        # 査に含める。NativeStorage を検
        # 査ターゲットに残し、単体アプリでコンパイルす
        # るだけでなく論理ルートの生成経路を実行する。
        ${CARDWIRTH_PLATFORM_SOURCES}
        cw/lib/archive.cpp
        cw/lib/archive.hpp
        cw/lib/encoding.cpp
        cw/lib/encoding.hpp
        cw/lib/regex_grapheme.cpp
        cw/lib/regex_grapheme.hpp
    )
    target_compile_features(cardwirth_explorer_tests PRIVATE cxx_std_20)
    target_include_directories(cardwirth_explorer_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(cardwirth_explorer_tests PRIVATE
        SDL3::SDL3 SDL3_image::SDL3_image SDL3_mixer::SDL3_mixer)
    if(MSVC)
        target_compile_options(cardwirth_explorer_tests PRIVATE /W4 /utf-8)
    else()
        target_compile_options(cardwirth_explorer_tests PRIVATE -Wall -Wextra -Wpedantic)
        target_link_libraries(cardwirth_explorer_tests PRIVATE ${CMAKE_DL_LIBS})
    endif()
    add_test(NAME explorer_api COMMAND cardwirth_explorer_tests)
endif()

if(BUILD_TESTING AND NOT ANDROID AND NOT IOS)
    add_executable(cardwirth_storage_tests
        tests/storage_contract.cpp
        ${CARDWIRTH_PLATFORM_SOURCES}
    )
    target_compile_features(cardwirth_storage_tests PRIVATE cxx_std_20)
    target_include_directories(cardwirth_storage_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    if(MSVC)
        target_compile_options(cardwirth_storage_tests PRIVATE /W4 /EHsc /utf-8)
    else()
        target_compile_options(cardwirth_storage_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME storage_contract COMMAND cardwirth_storage_tests)
endif()
if(BUILD_TESTING AND NOT ANDROID AND NOT IOS AND NOT EMSCRIPTEN)
    add_executable(cardwirth_managed_userdata_path_tests
        tests/managed_userdata_path_tests.cpp)
    target_compile_features(cardwirth_managed_userdata_path_tests PRIVATE cxx_std_20)
    target_include_directories(cardwirth_managed_userdata_path_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}")
    if(MSVC)
        target_compile_options(cardwirth_managed_userdata_path_tests PRIVATE
            /W4 /EHsc /utf-8)
    else()
        target_compile_options(cardwirth_managed_userdata_path_tests PRIVATE
            -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME managed_userdata_paths
             COMMAND cardwirth_managed_userdata_path_tests)

    add_executable(cardwirth_user_data_layout_tests
        tests/user_data_layout_tests.cpp
        app/app_shell.cpp)
    target_compile_features(cardwirth_user_data_layout_tests PRIVATE cxx_std_20)
    target_include_directories(cardwirth_user_data_layout_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}")
    if(MSVC)
        target_compile_options(cardwirth_user_data_layout_tests PRIVATE
            /W4 /EHsc /utf-8)
    else()
        target_compile_options(cardwirth_user_data_layout_tests PRIVATE
            -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME user_data_layout COMMAND cardwirth_user_data_layout_tests)
    if((CMAKE_SYSTEM_NAME STREQUAL "Linux" OR APPLE) AND
       NOT ANDROID AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS" AND NOT EMSCRIPTEN)
        find_package(Threads REQUIRED)
        find_package(Threads REQUIRED)
        add_executable(cardwirth_process_launch_tests
            tests/process_launch_tests.cpp app/process.cpp)
        target_compile_features(cardwirth_process_launch_tests PRIVATE cxx_std_20)
        target_include_directories(cardwirth_process_launch_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
        target_link_libraries(cardwirth_process_launch_tests PRIVATE Threads::Threads)
        add_test(NAME process_launch COMMAND cardwirth_process_launch_tests)
    endif()
    add_executable(cardwirth_app_visibility_tests tests/app_visibility_tests.cpp)
    add_executable(cardwirth_platform_audio_lifecycle_tests
        tests/platform_audio_lifecycle_tests.cpp cwxeditor_src/lib/audio.cpp)
    foreach(audio_test IN ITEMS cardwirth_app_visibility_tests cardwirth_platform_audio_lifecycle_tests)
        target_compile_features(${audio_test} PRIVATE cxx_std_20)
        target_include_directories(${audio_test} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
        if(MSVC)
            target_compile_options(${audio_test} PRIVATE /W4 /EHsc /utf-8 /UNDEBUG)
        else()
            target_compile_options(${audio_test} PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
        endif()
    endforeach()
    add_test(NAME app_visibility COMMAND cardwirth_app_visibility_tests)
    add_test(NAME platform_audio_lifecycle COMMAND cardwirth_platform_audio_lifecycle_tests)
    find_program(CARDWIRTH_AUDIO_TEST_NODE NAMES node node.exe)
    if(CARDWIRTH_AUDIO_TEST_NODE)
    endif()
endif()
if(BUILD_TESTING)
    find_program(CARDWIRTH_WEB_TEST_NODE NAMES node node.exe)
    if(CARDWIRTH_WEB_TEST_NODE)
        add_test(NAME web_document_cancel COMMAND "${CARDWIRTH_WEB_TEST_NODE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/web_document_cancel_tests.cjs")
        add_test(NAME web_viewport_rotation COMMAND "${CARDWIRTH_WEB_TEST_NODE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/web_viewport_rotation_tests.cjs")
        add_test(NAME web_storage_sync COMMAND "${CARDWIRTH_WEB_TEST_NODE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/web_storage_sync_tests.cjs")
        add_test(NAME web_upload_staging COMMAND "${CARDWIRTH_WEB_TEST_NODE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/web_upload_staging_tests.cjs")
        add_test(NAME web_metadata_verification COMMAND "${CARDWIRTH_WEB_TEST_NODE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/web_metadata_verification_tests.cjs")
        add_test(NAME web_high_address COMMAND "${CARDWIRTH_WEB_TEST_NODE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/web_high_address_tests.cjs")
    endif()
endif()
if(ANDROID AND CARDWIRTH_APP_VARIANT STREQUAL "All")
    # Editor ライブラリーは単体版 Android でも使う。OS 専用の SAF
    # 連携をそこで一度だけ持ち、ALL の Explorer も同じ実装をリンクする。
    list(REMOVE_ITEM CARDWIRTH_SOURCES app/explorer/platform_transfer.cpp)
endif()
if(BUILD_TESTING AND NOT CARDWIRTH_ENGINE_PRIVATE_ENCRYPTION AND
   (CARDWIRTH_APP_VARIANT STREQUAL "Engine" OR
    CARDWIRTH_APP_VARIANT STREQUAL "All"))
    add_executable(cardwirth_engine_encryption_contract_tests
        tests/engine_encryption_contract_tests.cpp
        platform/storage/storage.cpp
        platform/storage/engine_encryption_stub.cpp)
    target_include_directories(cardwirth_engine_encryption_contract_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_features(cardwirth_engine_encryption_contract_tests PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(cardwirth_engine_encryption_contract_tests PRIVATE
            /W4 /EHsc /utf-8)
    endif()
    add_test(NAME engine_encryption_off_contract
        COMMAND cardwirth_engine_encryption_contract_tests)
endif()
if(ANDROID)
    add_library(main SHARED ${CARDWIRTH_SOURCES})
elseif(APPLE)
    add_executable(cardwirth_sample MACOSX_BUNDLE ${CARDWIRTH_SOURCES})
elseif(WIN32)
    add_executable(cardwirth_sample WIN32 ${CARDWIRTH_SOURCES})
else()
    add_executable(cardwirth_sample ${CARDWIRTH_SOURCES})
endif()

if(ANDROID)
    set(CARDWIRTH_TARGET main)
else()
    set(CARDWIRTH_TARGET cardwirth_sample)
endif()
set(CARDWIRTH_TARGETS ${CARDWIRTH_TARGET})
include(cmake/exception_origin.cmake)
cardwirth_enable_exception_origin(${CARDWIRTH_TARGET})
if(TARGET cardwirth_explorer_tests)
    cardwirth_enable_exception_origin(cardwirth_explorer_tests)
endif()
target_link_libraries(${CARDWIRTH_TARGET} PRIVATE cardwirth_simd_image)

# 差分ビルドでも、製品を作るたびに Home のビルド日時を更新する
# 。メインの翻訳単位に古い __DATE__ が残ることを避ける。
set(CARDWIRTH_BUILD_TIMESTAMP_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/cardwirth_build_timestamp.hpp")
add_custom_target(cardwirth_build_timestamp
    COMMAND "${CMAKE_COMMAND}" "-DOUTPUT=${CARDWIRTH_BUILD_TIMESTAMP_HEADER}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/write_build_timestamp.cmake"
    BYPRODUCTS "${CARDWIRTH_BUILD_TIMESTAMP_HEADER}"
    VERBATIM)
add_dependencies(${CARDWIRTH_TARGET} cardwirth_build_timestamp)
target_include_directories(${CARDWIRTH_TARGET} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")

# 元の Engine の XML 動作に対応する、固定版の静的 Expat 依存を一つ使う。
if(NOT CARDWIRTH_APP_VARIANT STREQUAL "Editor" AND
   NOT CARDWIRTH_APP_VARIANT STREQUAL "Explorer")
    # cw/lib/imagecodec.cpp は Engine を含む製品だけがコンパイルする
    # 。それらは同じ固定 JPEG コーデックを使い、ほかの製品には未使用のコーデックを加えない。
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/CardWirthJPEG.cmake")
    cardwirth_configure_jpeg()
    function(cardwirth_configure_engine_xml)
        include(FetchContent)
        set(EXPAT_BUILD_TOOLS OFF)
        set(EXPAT_BUILD_EXAMPLES OFF)
        set(EXPAT_BUILD_TESTS OFF)
        set(EXPAT_BUILD_DOCS OFF)
        set(EXPAT_BUILD_PKGCONFIG OFF)
        set(EXPAT_ENABLE_INSTALL OFF)
        set(EXPAT_SHARED_LIBS OFF)
        set(EXPAT_DTD ON)
        set(EXPAT_GE ON)
        set(EXPAT_CHAR_TYPE char)
        set(EXPAT_MSVC_STATIC_CRT ON)
        FetchContent_Declare(cardwirth_expat
            URL "https://github.com/libexpat/libexpat/releases/download/R_2_8_5/expat-2.8.5.tar.gz"
            URL_HASH "SHA256=920dde485e15eda0cce8d2310b41d492c534e5e3d89ad407a0b4176dd2ff88fe"
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        FetchContent_MakeAvailable(cardwirth_expat)
        if(MSVC)
            # 元の C ソースは BOM なしの UTF-8 コメントを含む。コンパイラ
            # ーのソース文字コードを、ホストの ANSI コードページから独立させる。
            target_compile_options(expat PRIVATE /utf-8)
        endif()
        foreach(cardwirth_xml_target IN LISTS CARDWIRTH_TARGETS)
            target_link_libraries(${cardwirth_xml_target} PRIVATE expat::expat)
            add_custom_command(TARGET ${cardwirth_xml_target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory
                    "$<TARGET_FILE_DIR:${cardwirth_xml_target}>/licenses"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${cardwirth_expat_SOURCE_DIR}/COPYING"
                    "$<TARGET_FILE_DIR:${cardwirth_xml_target}>/licenses/Expat.txt")
        endforeach()
        install(FILES "${cardwirth_expat_SOURCE_DIR}/COPYING"
            DESTINATION bin/licenses RENAME Expat.txt)
        # 外部 API の検査は、新しい製品オブジェクトとこの静的成果物そのもの
        # を再利用する。OS ごとのファイル名や CRT 接尾辞を推測しない。
        file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/cardwirth_expat_link_$<CONFIG>.cmake"
            CONTENT "set(CARDWIRTH_EXPAT_LIBRARY \"$<TARGET_FILE:expat>\")\n")
    endfunction()
    cardwirth_configure_engine_xml()
endif()



if(CARDWIRTH_ENGINE_PRIVATE_ENCRYPTION)
    set(CARDWIRTH_PRIVATE_ROOT "${CARDWIRTH_ROOT}/private")
    if(NOT EXISTS "${CARDWIRTH_PRIVATE_ROOT}/CMakeLists.txt")
        message(FATAL_ERROR
            "Encrypted UserData was requested, but the private source overlay is unavailable")
    endif()
    # 非公開オーバーレイが有効時の実装と、独立した作成・検証・
    # 復旧ツールを提供する。無効時のビルド依存には含めない。
    set(CARDWIRTH_PUBLIC_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}" CACHE PATH "" FORCE)
    add_subdirectory("${CARDWIRTH_PRIVATE_ROOT}"
                     "${CMAKE_CURRENT_BINARY_DIR}/private-encryption" EXCLUDE_FROM_ALL)
    target_compile_definitions(${CARDWIRTH_TARGET} PRIVATE
        CARDWIRTH_ENGINE_PRIVATE_ENCRYPTION=1)
    target_link_libraries(${CARDWIRTH_TARGET} PRIVATE
        cardwirth_engine_private_encryption)
    if(NOT ANDROID AND NOT IOS AND NOT EMSCRIPTEN)
        add_dependencies(${CARDWIRTH_TARGET} cardwirth_encryption_tool)
    endif()
    add_custom_command(TARGET ${CARDWIRTH_TARGET} POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
            -DCARDWIRTH_EXECUTABLE=$<TARGET_FILE:${CARDWIRTH_TARGET}>
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/append_encryption_capability_marker.cmake"
        VERBATIM)
endif()

# 単体版 Engine は CardWirthNext 1.60 の変換に
# 共通 CWX バイナリーモデルを使う。Editor の GUI・アプリ
# は取り込まず、この二ライブラリーには共通モデルとサービスだけを含める。
function(cardwirth_setup_cwx_regex)
    if(TARGET pcre2-32-static)
        return()
    endif()
    # 全 CWX の利用側・環境で一つの Unicode 正規表現エンジンを使う。D アダプ
    # ターが Phobos の反復・文字の規則を提供し、製品モード別の高速経路は作らない。
    include(FetchContent)
    set(PCRE2_BUILD_PCRE2_8 OFF CACHE BOOL "" FORCE)
    set(PCRE2_BUILD_PCRE2_16 OFF CACHE BOOL "" FORCE)
    set(PCRE2_BUILD_PCRE2_32 ON CACHE BOOL "" FORCE)
    set(PCRE2_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(PCRE2_BUILD_PCRE2GREP OFF CACHE BOOL "" FORCE)
    set(PCRE2_SUPPORT_JIT OFF CACHE BOOL "" FORCE)
    set(PCRE2_SUPPORT_UNICODE ON CACHE BOOL "" FORCE)
    set(PCRE2_STATIC_PIC ON CACHE BOOL "" FORCE)
    # 元の汎用ライブラリーの設定は、この依存の範囲だけに限定する。
    set(BUILD_STATIC_LIBS ON)
    set(BUILD_SHARED_LIBS OFF)
    FetchContent_Declare(cardwirth_pcre2
        URL "https://github.com/PCRE2Project/pcre2/releases/download/pcre2-10.49/pcre2-10.49.tar.gz"
        URL_HASH SHA256=929f0b20e62879252a15886b06c89f1edef61a363cbd5826fb041080a5e557ae)
    FetchContent_MakeAvailable(cardwirth_pcre2)
    set_property(GLOBAL PROPERTY CARDWIRTH_PCRE2_LICENSE "${cardwirth_pcre2_SOURCE_DIR}/LICENCE.md")
    if(MSVC)
        target_compile_options(pcre2-32-static PRIVATE /utf-8)
    endif()
    file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/cardwirth_pcre2_link_$<CONFIG>.cmake"
        CONTENT "set(CARDWIRTH_PCRE2_LIBRARY \"$<TARGET_FILE:pcre2-32-static>\")\n")
    install(FILES "${cardwirth_pcre2_SOURCE_DIR}/LICENCE.md"
        DESTINATION licenses RENAME PCRE2-LICENCE)
    # 元実装との差: Android の製品ターゲットは cardwirth_sample ではなく main
    # なので、CARDWIRTH_TARGET 経由で指定する(固定名だと Android の構成が失敗する)。
    add_custom_command(TARGET ${CARDWIRTH_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${CARDWIRTH_TARGET}>/licenses"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${cardwirth_pcre2_SOURCE_DIR}/LICENCE.md"
            "$<TARGET_FILE_DIR:${CARDWIRTH_TARGET}>/licenses/PCRE2-LICENCE")
endfunction()

# Editor は既定で単体版とし、すべての ALL ホストへ
# リンクする。部品・配置・入力・描画の実装は全 OS で一つに
# し、OS・ALL の違いはホスト・サービスの引数だけで表す。
set(CARDWIRTH_EDITOR_PORT_ENABLED OFF)
if(CARDWIRTH_APP_VARIANT STREQUAL "Editor" OR
   CARDWIRTH_APP_VARIANT STREQUAL "All")
    set(CARDWIRTH_EDITOR_PORT_ENABLED ON)
endif()

if(CARDWIRTH_EDITOR_PORT_ENABLED OR
   CARDWIRTH_APP_VARIANT STREQUAL "Engine" OR
   CARDWIRTH_APP_VARIANT STREQUAL "OneWindow")
    # Editor と変換モデルは同じ real80 実装を使う
    # 。ヘッダー、演算状態、リンク依存もその共通経路にそろえる。
    add_subdirectory(3rdparty/softfloat)
endif()

if(CARDWIRTH_APP_VARIANT STREQUAL "Engine" OR
   CARDWIRTH_APP_VARIANT STREQUAL "OneWindow")
    file(GLOB_RECURSE CARDWIRTH_NEXT_CWX_CORE_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/cwxeditor_src/core/cwx/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/cwxeditor_src/core/cwx/*.hpp")
    file(GLOB CARDWIRTH_NEXT_CWX_LIB_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/cwxeditor_src/lib/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/cwxeditor_src/lib/*.hpp")
    # 変換器は共通のモデル・保存ヘルパーだけを使う。受動的な UI 入
    # 力は Editor の表示側に属し、モデルのイベント ABI と
    # 独立して変更され得るため、Engine の連携には組み込まない。
    list(FILTER CARDWIRTH_NEXT_CWX_LIB_SOURCES EXCLUDE REGEX
        "/ui_passive_input\\.cpp$")
    cardwirth_setup_cwx_regex()
    add_library(cardwirth_next_cwx_lib STATIC ${CARDWIRTH_NEXT_CWX_LIB_SOURCES})
    target_link_libraries(cardwirth_next_cwx_lib PUBLIC pcre2-32-static)
    target_link_libraries(cardwirth_next_cwx_lib PRIVATE cardwirth_softfloat3e)
    add_library(cardwirth_next_cwx_core STATIC ${CARDWIRTH_NEXT_CWX_CORE_SOURCES})
    foreach(next_library IN ITEMS cardwirth_next_cwx_lib cardwirth_next_cwx_core)
        target_compile_features(${next_library} PUBLIC cxx_std_20)
        target_include_directories(${next_library} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
        if(MSVC)
            target_compile_options(${next_library} PRIVATE /W4 /EHsc /utf-8 /bigobj /MP /wd4100 /wd4458)
        else()
            target_compile_options(${next_library} PRIVATE -Wall -Wextra -Wpedantic)
        endif()
    endforeach()
    # 文字の輪郭抽出は SDL_ttf と同じ同梱フォントエンジンを
    # 使う。文字の塗り・線・配置は共通 CWX ライブラリーに残す。
    target_link_libraries(cardwirth_next_cwx_lib PUBLIC
        SDL3::SDL3 SDL3_image::SDL3_image SDL3_mixer::SDL3_mixer SDL3_ttf::SDL3_ttf Freetype::Freetype)
    target_link_libraries(cardwirth_next_cwx_core PUBLIC cardwirth_next_cwx_lib)
    target_link_libraries(${CARDWIRTH_TARGET} PRIVATE cardwirth_next_cwx_core)
endif()

# CWXEditor は D と同じ二段階のリソース検索を使
# う。実行ファイルの隣の resource/ が組み込み既定
# 値を上書きし、同梱素材はすべて実行ファイルからも取得できる
# 。executable_resource() が UTF-8
# の RCDATA 文字列 ID で引くため、名前は相対
# ・スラッシュ区切りにする。ALL も外部 resource/
# なしで同じ Editor のアイコンを必要とするため
# 、開発用の一部ローダーのソースツリー検索に頼らない。
if(WIN32 AND (CARDWIRTH_APP_VARIANT STREQUAL "Editor" OR
             CARDWIRTH_APP_VARIANT STREQUAL "All"))
    enable_language(RC)

    set(CWXEDITOR_RESOURCE_ROOT
        "${CMAKE_CURRENT_SOURCE_DIR}/cwxeditor_src/resource")
    if(NOT IS_DIRECTORY "${CWXEDITOR_RESOURCE_ROOT}")
        message(FATAL_ERROR
            "CWXEditor resource directory was not found: ${CWXEDITOR_RESOURCE_ROOT}")
    endif()

    file(GLOB_RECURSE CWXEDITOR_EMBEDDED_RESOURCES CONFIGURE_DEPENDS
        "${CWXEDITOR_RESOURCE_ROOT}/*")
    list(SORT CWXEDITOR_EMBEDDED_RESOURCES)

    set(CWXEDITOR_RESOURCE_SCRIPT
        "${CMAKE_CURRENT_BINARY_DIR}/generated/cwxeditor_resources.rc")
    get_filename_component(CWXEDITOR_RESOURCE_SCRIPT_DIR
        "${CWXEDITOR_RESOURCE_SCRIPT}" DIRECTORY)
    file(MAKE_DIRECTORY "${CWXEDITOR_RESOURCE_SCRIPT_DIR}")
    file(WRITE "${CWXEDITOR_RESOURCE_SCRIPT}"
        "#pragma code_page(65001)\n\n"
        "// Generated by CMake. External resource/ files take precedence at runtime.\n")

    foreach(cwxeditor_resource IN LISTS CWXEDITOR_EMBEDDED_RESOURCES)
        file(RELATIVE_PATH cwxeditor_resource_name
            "${CWXEDITOR_RESOURCE_ROOT}" "${cwxeditor_resource}")
        file(TO_CMAKE_PATH "${cwxeditor_resource_name}" cwxeditor_resource_name)
        file(TO_CMAKE_PATH "${cwxeditor_resource}" cwxeditor_resource_path)
        string(REPLACE "\"" "\"\"" cwxeditor_resource_name
            "${cwxeditor_resource_name}")
        string(REPLACE "\"" "\"\"" cwxeditor_resource_path
            "${cwxeditor_resource_path}")
        file(APPEND "${CWXEDITOR_RESOURCE_SCRIPT}"
            "\"resource/${cwxeditor_resource_name}\" RCDATA \"${cwxeditor_resource_path}\"\n")
    endforeach()

    # RC ファイルはビルドルールではなく設定時に書く。GENERATED
    # にすると、Ninja が --clean-first
    # で削除し、空の cmd.exe /c 再生成規則
    # を出して RC の入力が欠落するため、指定しない。
    set_source_files_properties("${CWXEDITOR_RESOURCE_SCRIPT}" PROPERTIES
        OBJECT_DEPENDS "${CWXEDITOR_EMBEDDED_RESOURCES}")
    target_sources(${CARDWIRTH_TARGET} PRIVATE "${CWXEDITOR_RESOURCE_SCRIPT}")
    list(LENGTH CWXEDITOR_EMBEDDED_RESOURCES CWXEDITOR_RESOURCE_COUNT)
    message(STATUS
        "Embedding ${CWXEDITOR_RESOURCE_COUNT} CWXEditor resources as RCDATA")
endif()

# Windows の実行ファイルアイコンは、Editor のアプリ内 RCDATA 画像
# と別に、指定された二つの PNG から作る。全 Windows 製品構成で共有する。
if(WIN32)
    enable_language(RC)
    set(CARDWIRTH_ICON_ROOT "${CARDWIRTH_ROOT}/icon")
    set(CARDWIRTH_ICON_SMALL "${CARDWIRTH_ICON_ROOT}/icon_32×32_pyC.png")
    set(CARDWIRTH_ICON_LARGE "${CARDWIRTH_ICON_ROOT}/icon_128×128_pyC.png")
    foreach(icon_source IN ITEMS "${CARDWIRTH_ICON_SMALL}" "${CARDWIRTH_ICON_LARGE}")
        if(NOT EXISTS "${icon_source}")
            message(FATAL_ERROR "Application icon is missing: ${icon_source}")
        endif()
    endforeach()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CARDWIRTH_ICON_SMALL}" "${CARDWIRTH_ICON_LARGE}")
    find_program(CARDWIRTH_POWERSHELL NAMES pwsh powershell REQUIRED)
    set(CARDWIRTH_WINDOWS_ICON "${CMAKE_CURRENT_BINARY_DIR}/generated/cardwirth_app.ico")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
    execute_process(COMMAND "${CARDWIRTH_POWERSHELL}" -NoProfile
        -ExecutionPolicy Bypass -File
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/create_windows_icon.ps1"
        -Small "${CARDWIRTH_ICON_SMALL}" -Large "${CARDWIRTH_ICON_LARGE}"
        -Output "${CARDWIRTH_WINDOWS_ICON}"
        RESULT_VARIABLE icon_result ERROR_VARIABLE icon_error)
    if(NOT icon_result EQUAL 0)
        message(FATAL_ERROR "Could not create Windows application icon: ${icon_error}")
    endif()
    file(TO_CMAKE_PATH "${CARDWIRTH_WINDOWS_ICON}" CARDWIRTH_WINDOWS_ICON_RC_PATH)
    set(CARDWIRTH_WINDOWS_ICON_RC
        "${CMAKE_CURRENT_BINARY_DIR}/generated/cardwirth_app_icon.rc")
    file(WRITE "${CARDWIRTH_WINDOWS_ICON_RC}"
        "1 ICON \"${CARDWIRTH_WINDOWS_ICON_RC_PATH}\"\n")
    set_source_files_properties("${CARDWIRTH_WINDOWS_ICON_RC}" PROPERTIES
        OBJECT_DEPENDS "${CARDWIRTH_WINDOWS_ICON}")
    target_sources(${CARDWIRTH_TARGET} PRIVATE "${CARDWIRTH_WINDOWS_ICON_RC}")
endif()

if(CARDWIRTH_EDITOR_PORT_ENABLED)
    file(GLOB_RECURSE CWXEDITOR_CORE_PORT_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/cwxeditor_src/core/cwx/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/cwxeditor_src/core/cwx/*.hpp"
    )
    file(GLOB_RECURSE CWXEDITOR_GUI_PORT_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/cwxeditor_src/gui/cwx/editor/gui/dwt/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/cwxeditor_src/gui/cwx/editor/gui/dwt/*.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/cwxeditor_src/gui/cwx/editor/gui/sound.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/cwxeditor_src/gui/cwx/editor/gui/sound.hpp"
    )
    # flagdirtree.d と datawindow.d は、現在の複合
    # flagspane/areatable の表示処理に統合されている。一対一
    # の移植一覧用の外側の型は残すが、廃止した空モジュールを製品に組み込まない。
    list(FILTER CWXEDITOR_GUI_PORT_SOURCES EXCLUDE REGEX
        "/(flagdirtree|datawindow)\\.(cpp|hpp)$")
    foreach(retired_gui_module IN ITEMS flagdirtree datawindow)
        foreach(gui_candidate IN LISTS CWXEDITOR_GUI_PORT_SOURCES)
            if(gui_candidate MATCHES "/${retired_gui_module}\\.(cpp|hpp)$")
                message(FATAL_ERROR
                    "Retired CWXEditor GUI state owner re-entered the product: ${gui_candidate}")
            endif()
        endforeach()
    endforeach()
    file(GLOB CWXEDITOR_APP_PORT_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/cwxeditor_src/source/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/cwxeditor_src/source/*.hpp"
    )
    file(GLOB CWXEDITOR_LIB_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/cwxeditor_src/lib/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/cwxeditor_src/lib/*.hpp"
    )
    # GUI モデルは全ホストと Editor/ALL で同じにす
    # る。OS の選択はサービス・アダプターが担当し、dwt 内の
    # 前処理分岐・製品モード照会で第二の配置・状態経路を作らない。
    foreach(editor_gui_source IN LISTS CWXEDITOR_GUI_PORT_SOURCES)
        if(NOT editor_gui_source MATCHES "\\.(cpp|hpp)$")
            continue()
        endif()
        file(READ "${editor_gui_source}" editor_gui_text)
        if(editor_gui_text MATCHES
           "#[ \t]*(if|ifdef|ifndef|elif)[^\r\n]*(WIN32|_WIN32|__APPLE__|__linux__|ANDROID|__ANDROID__|EMSCRIPTEN|__EMSCRIPTEN__)")
            message(FATAL_ERROR
                "CWXEditor GUI contains an OS-specific implementation branch: ${editor_gui_source}")
        endif()
        if(editor_gui_text MATCHES
           "CARDWIRTH_APP_VARIANT|CARDWIRTH_EMBED_EDITOR|is_all_mode[ \t]*\\(")
            message(FATAL_ERROR
                "CWXEditor GUI contains an Editor/ALL-specific implementation branch: ${editor_gui_source}")
        endif()
    endforeach()

    # SDL はイベント・画像のアダプター。選択、表示領域、意味上
    # の変更コールバックは共通 ui_* の状態機械が管理する。
    set(CWXEDITOR_SDL_ADAPTER
        "${CMAKE_CURRENT_SOURCE_DIR}/cwxeditor_src/lib/sdl_ui.cpp")
    file(READ "${CWXEDITOR_SDL_ADAPTER}" cwxeditor_sdl_adapter_text)
    foreach(forbidden_adapter_state IN ITEMS
            "(\\.|->)(selection|selected_index|top_index)[ \t\r\n]*="
            "on_selection_changed[ \t]*\\("
            "on_cell_activate[ \t]*\\("
            "on_activate[ \t]*\\(")
        if(cwxeditor_sdl_adapter_text MATCHES "${forbidden_adapter_state}")
            message(FATAL_ERROR
                "SDL adapter owns common UI semantic state (${forbidden_adapter_state})")
        endif()
    endforeach()
    # 製品の表示処理と共通の操作処理は一つずつ持つ。
    # GLOB で追加ファイルを全 OS に含めるが
    # 、廃止した Windows 表示処理の復活や、
    # 構成ごとの追加による二重コンパイルを許さない。
    function(cwxeditor_assert_source_count source expected)
        set(matches 0)
        get_filename_component(source_absolute "${source}" ABSOLUTE
            BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        foreach(candidate IN LISTS CWXEDITOR_LIB_SOURCES)
            get_filename_component(candidate_absolute "${candidate}" ABSOLUTE
                BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
            if(candidate_absolute STREQUAL "${source_absolute}")
                math(EXPR matches "${matches} + 1")
            endif()
        endforeach()
        if(NOT matches EQUAL expected)
            message(FATAL_ERROR
                "CWXEditor source graph expected ${expected} copy/copies of ${source}, found ${matches}")
        endif()
    endfunction()

    foreach(retired_presenter IN ITEMS
            windows_ui.cpp windows_ui.hpp windows_ui_test.hpp)
        cwxeditor_assert_source_count(
            "${CMAKE_CURRENT_SOURCE_DIR}/cwxeditor_src/lib/${retired_presenter}" 0)
    endforeach()
    foreach(common_source IN ITEMS
            sdl_ui.cpp
            ui_accessibility.cpp
            ui_actions.cpp
            ui_activation.cpp
            ui_child_order.cpp
            ui_chrome_input.cpp
            ui_chrome_presentation.cpp
            ui_combo_edit.cpp
            ui_container_presentation.cpp
            ui_decoration_presentation.cpp
            ui_drag_drop.cpp
            ui_edit_focus.cpp
            ui_edit_input.cpp
            ui_external_drop.cpp
            ui_frame_presentation.cpp
            ui_frame_replay.cpp
            ui_frame_updates.cpp
            ui_image_presentation.cpp
            ui_inline_edit.cpp
            ui_input_publication.cpp
            ui_item_presentation.cpp
            ui_layout.cpp
            ui_metrics.cpp
            ui_modal_operation.cpp
            ui_mouse_input.cpp
            ui_number_edit.cpp
            ui_number_repeat.cpp
            ui_passive_input.cpp
            ui_pointer_input.cpp
            ui_pointer_timing.cpp
            ui_popup_presentation.cpp
            ui_scroll.cpp
            ui_slider.cpp
            ui_strip_presentation.cpp
            ui_surface_lifetime.cpp
            ui_text_layout.cpp
            ui_tooltip_timing.cpp
            ui_touch_input.cpp
            ui_value_dialogs.cpp
            ui_window_attributes.cpp
            ui_window_lifecycle.cpp
            ui_window_maximize.cpp
            ui_window_stack.cpp)
        cwxeditor_assert_source_count(
            "${CMAKE_CURRENT_SOURCE_DIR}/cwxeditor_src/lib/${common_source}" 1)
    endforeach()
    # SDL 表示側は独自・システムのフォントファミリーを解
    # 決する。実装を一つの Editor/ALL 共通ライ
    # ブラリーに置き、単体製品と検査で同じヘルパーを使う。
    list(APPEND CWXEDITOR_LIB_SOURCES
        app/platform_font.cpp
        app/platform_font.hpp)
    cwxeditor_assert_source_count(
        "${CMAKE_CURRENT_SOURCE_DIR}/app/platform_font.cpp" 1)
    # TinySoundFont は Web を含む全環境の SDL 音声
    # アダプターの MIDI 合成器で、Engine・Editor・Explorer
    # が共有する。Editor/ALL の共通ライブラリーに一つだ
    # け置き、製品と検査がシェル経由で二重リンクしないようにする。
    list(APPEND CWXEDITOR_LIB_SOURCES
        app/explorer/midi_player.cpp
        app/explorer/midi_player.hpp)
    function(cwxeditor_assert_product_source_count source expected)
        set(matches 0)
        get_filename_component(source_absolute "${source}" ABSOLUTE
            BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        foreach(candidate IN LISTS CARDWIRTH_SOURCES)
            get_filename_component(candidate_absolute "${candidate}" ABSOLUTE
                BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
            if(candidate_absolute STREQUAL "${source_absolute}")
                math(EXPR matches "${matches} + 1")
            endif()
        endforeach()
        if(NOT matches EQUAL expected)
            message(FATAL_ERROR
                "CWXEditor product source graph expected ${expected} copy/copies of ${source_absolute}, found ${matches}")
        endif()
    endfunction()
    cwxeditor_assert_product_source_count(
        "${CMAKE_CURRENT_SOURCE_DIR}/app/platform_font.cpp" 0)
    if(ANDROID OR
       ((EMSCRIPTEN OR APPLE) AND CARDWIRTH_APP_VARIANT STREQUAL "Editor"))
        # 単体版 Editor に Explorer UI や Engine をリンクせ
        # ず、プロバイダー・ブラウザーの取り込み準備と出力公開を再利用する。Apple
        # も Objective-C++ アダプターの周囲にこの共通処理を必要とする。
        list(APPEND CWXEDITOR_LIB_SOURCES
            app/explorer/platform_transfer.cpp
            app/explorer/platform_transfer.hpp)
    endif()
    if(APPLE)
        # SDL は Cocoa のネイティブ選択画面を持つが、iOS 側は仮実
        # 装。単体版・ALL の両 Editor で、Explorer の UIDocumentPicker
        # の準備・確定アダプターを再利用する。
        list(APPEND CWXEDITOR_LIB_SOURCES
            cwxeditor_src/lib/apple_accessibility.hpp
            cwxeditor_src/lib/apple_accessibility.mm
            app/explorer/platform_transfer_apple.hpp
            app/explorer/platform_transfer_apple.mm)
        set_source_files_properties(
            cwxeditor_src/lib/apple_accessibility.mm
            app/explorer/platform_transfer_apple.mm
            PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    endif()
    if(CARDWIRTH_APP_VARIANT STREQUAL "Editor")
        # 単体版 Editor のソース構成にはアーカイブコーデッ
        # クと共通 Storage がないため、互換ライブラリーに
        # 含める。ALL は共通ターゲットですでにコンパイルする。
        list(APPEND CWXEDITOR_LIB_SOURCES
            cw/lib/archive.cpp
            cw/lib/archive.hpp
            cw/lib/encoding.cpp
            cw/lib/encoding.hpp
            platform/storage/storage.cpp
            platform/storage/storage.hpp
            platform/storage/storage_mount.cpp
            platform/storage/storage_mount.hpp
            platform/storage/app_paths.cpp
            platform/storage/app_paths.hpp
            platform/storage/app_runtime_context.cpp
            platform/storage/app_runtime_context.hpp
        )
    endif()

    # 構成別の追加で第二の実装を黙ってリンクしない
    # 。上の名前付き検査に加え、最終の共通ライブラ
    # リー内の全 C++/Objective-C++
    # を検査し、新しい状態機械も対象にする。
    function(cwxeditor_assert_unique_implementation_sources)
        set(seen_sources)
        foreach(candidate IN LISTS CWXEDITOR_LIB_SOURCES)
            if(NOT candidate MATCHES "\\.(cpp|mm)$")
                continue()
            endif()
            get_filename_component(candidate_absolute "${candidate}" ABSOLUTE
                BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
            list(FIND seen_sources "${candidate_absolute}" duplicate_index)
            if(NOT duplicate_index EQUAL -1)
                message(FATAL_ERROR
                    "CWXEditor common source graph registers ${candidate_absolute} more than once")
            endif()
            list(APPEND seen_sources "${candidate_absolute}")
        endforeach()
    endfunction()
    cwxeditor_assert_unique_implementation_sources()

    # 単体版 Editor と ALL の共通表示コードの所
    # 有者は cwxeditor_lib だけ。後の ALL/Release
    # 用追加で同じ実装を CARDWIRTH_SOURCES
    # にも含めると、リンク順で二つの状態機械の一方
    # が選ばれてしまう。新しい共通ソースも手動一覧の
    # 更新なしで対象となるよう、汎用的に確認する。
    function(cwxeditor_assert_product_excludes_common_implementations)
        foreach(common_candidate IN LISTS CWXEDITOR_LIB_SOURCES)
            if(NOT common_candidate MATCHES "\\.(cpp|mm)$")
                continue()
            endif()
            get_filename_component(common_absolute "${common_candidate}" ABSOLUTE
                BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
            foreach(product_candidate IN LISTS CARDWIRTH_SOURCES)
                if(NOT product_candidate MATCHES "\\.(cpp|mm)$")
                    continue()
                endif()
                get_filename_component(product_absolute "${product_candidate}" ABSOLUTE
                    BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
                if(product_absolute STREQUAL "${common_absolute}")
                    message(FATAL_ERROR
                        "CWXEditor common implementation ${common_absolute} is also registered by the product target")
                endif()
            endforeach()
        endforeach()
    endfunction()
    cwxeditor_assert_product_excludes_common_implementations()

    cardwirth_setup_cwx_regex()
    add_library(cwxeditor_lib STATIC ${CWXEDITOR_LIB_SOURCES})
    target_link_libraries(cwxeditor_lib PUBLIC pcre2-32-static cardwirth_simd_image)
    target_link_libraries(cwxeditor_lib PRIVATE cardwirth_softfloat3e)
    add_library(cwxeditor_core STATIC
        cwxeditor_src/core/editor_core.cpp
        cwxeditor_src/core/editor_core.hpp
        ${CWXEDITOR_CORE_PORT_SOURCES}
    )
    add_library(cwxeditor_gui STATIC
        cwxeditor_src/gui/editor_gui.cpp
        cwxeditor_src/gui/editor_gui.hpp
        ${CWXEDITOR_GUI_PORT_SOURCES}
    )
    add_library(cwxeditor_app STATIC ${CWXEDITOR_APP_PORT_SOURCES})

    option(CWXEDITOR_ENABLE_RELEASE_DIAGNOSTICS
        "Write CWXEditor action and performance logs in Release builds" ON)

    foreach(cwxeditor_library IN ITEMS cwxeditor_lib cwxeditor_core cwxeditor_gui cwxeditor_app)
        target_compile_features(${cwxeditor_library} PUBLIC cxx_std_20)
        target_include_directories(${cwxeditor_library} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
        if(MSVC)
            target_compile_options(${cwxeditor_library} PRIVATE /W4 /EHsc /utf-8 /bigobj /MP /wd4100 /wd4458)
        else()
            target_compile_options(${cwxeditor_library} PRIVATE -Wall -Wextra -Wpedantic)
            if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
                # 生成した 274 要素の列挙表は有効な C++20 だが、Clang/libc++
                # の控えめな畳み込み式の深さ制限を超える。
                target_compile_options(${cwxeditor_library} PRIVATE -fbracket-depth=512)
            endif()
        endif()
    endforeach()
    if(ANDROID)
        # NDK の libc++ は Android/ELF の stop_token/jthread
        # を実験機能の設定下に置く。共通 Editor の TaskGroup は
        # 、ALL も含め、協調的なワーカー終了にこれらの C++20 機能を使う。
        target_compile_definitions(cwxeditor_lib PRIVATE
            _LIBCPP_ENABLE_EXPERIMENTAL=1)
    endif()
    # ファイル診断は Windows の単体版 Editor の Release だけで有効にする。
    if(WIN32 AND CARDWIRTH_APP_VARIANT STREQUAL "Editor")
        foreach(editor_log_target cwxeditor_core cwxeditor_lib cwxeditor_gui)
            target_compile_definitions(${editor_log_target} PUBLIC
                $<$<CONFIG:Release>:CWXEDITOR_FILE_LOGGING=1>)
        endforeach()
        if(CWXEDITOR_ENABLE_RELEASE_DIAGNOSTICS)
            target_compile_definitions(cwxeditor_lib PRIVATE
                $<$<CONFIG:Release>:CWXEDITOR_LOG_IN_RELEASE=1>)
        endif()
    endif()

    target_link_libraries(cwxeditor_gui PUBLIC cwxeditor_core cwxeditor_lib)
    if(WIN32)
        # 共通の要求フォントファミリーの選択と、符号付きの高さの計測に Windows のフォント API を使う。
        target_link_libraries(cwxeditor_lib PRIVATE imm32 Gdi32 dwrite ole32)
    endif()
    # ネイティブ字形パスの共通処理は、単体EngineのCWXサービスで
    # も使用する。全製品でフォントエンジンとテキスト処理方針を統一する。
    target_link_libraries(cwxeditor_lib PUBLIC
        SDL3::SDL3 SDL3_image::SDL3_image SDL3_mixer::SDL3_mixer SDL3_ttf::SDL3_ttf Freetype::Freetype)
    if(APPLE)
        target_link_libraries(cwxeditor_lib PUBLIC
            "-framework CoreText" "-framework CoreFoundation"
            "-framework Foundation" "-framework UniformTypeIdentifiers")
        if(IOS)
            target_link_libraries(cwxeditor_lib PUBLIC "-framework UIKit")
        else()
            target_link_libraries(cwxeditor_lib PUBLIC "-framework AppKit")
        endif()
    endif()
    target_link_libraries(cwxeditor_app PUBLIC cwxeditor_gui)
    target_link_libraries(${CARDWIRTH_TARGET} PRIVATE cwxeditor_app)

    if(BUILD_TESTING AND NOT ANDROID)
        option(CWXEDITOR_STRICT_GUI_TESTS
            "Enable strict CWXEditor GUI contract gates" ON)
        # 統合版ALLでは、これらの実装をcwxeditor_libで
        # はなく製品実行ファイルへ直接組み込む。Editorのテスト実
        # 行ファイルから実行ファイルのターゲットはリンクできないため、
        # 同じ実行時処理をテスト専用ライブラリとして提供する。単体Editor
        # では上記のcwxeditor_libに含まれる。
        set(CWXEDITOR_TEST_RUNTIME_TARGET)
        if(CARDWIRTH_APP_VARIANT STREQUAL "All")
            add_library(cwxeditor_test_runtime STATIC
                ${CARDWIRTH_PLATFORM_SOURCES}
                cw/lib/archive.cpp
                cw/lib/archive.hpp
                cw/lib/encoding.cpp
                cw/lib/encoding.hpp)
            target_compile_features(cwxeditor_test_runtime PUBLIC cxx_std_20)
            target_include_directories(cwxeditor_test_runtime PUBLIC
                "${CMAKE_CURRENT_SOURCE_DIR}")
            target_link_libraries(cwxeditor_test_runtime PUBLIC SDL3::SDL3)
            if(MSVC)
                target_compile_options(cwxeditor_test_runtime PRIVATE
                    /W4 /EHsc /utf-8 /bigobj)
            else()
                target_compile_options(cwxeditor_test_runtime PRIVATE
                    -Wall -Wextra -Wpedantic)
            endif()
            set(CWXEDITOR_TEST_RUNTIME_TARGET cwxeditor_test_runtime)
        endif()

        add_executable(cwxeditor_tests tests/cwxeditor_tests.cpp)
        add_executable(cwxeditor_parity_tests tests/cwxeditor_parity_tests.cpp)
        add_executable(cwxeditor_extended_parity_tests tests/cwxeditor_extended_parity_tests.cpp)
        add_executable(cwxeditor_core_d_unittest_parity_tests tests/cwxeditor_core_d_unittest_parity_tests.cpp)
        add_executable(cwxeditor_contract_tests tests/cwxeditor_contract_tests.cpp)
        add_executable(cwxeditor_shutdown_tests tests/cwxeditor_shutdown_tests.cpp)
        # コアのテスト実行ファイルはアプリケーションをリンクしないため
        # 、アーカイブ処理に必要な例外発生元の記録処理を別途提供する。
        cardwirth_enable_exception_origin(cwxeditor_core_d_unittest_parity_tests)
        target_sources(cwxeditor_core_d_unittest_parity_tests PRIVATE
            tests/editor_core_alignment_regression_tests.cpp
            tests/editor_core_io_cwl_regression_tests.cpp
            tests/editor_core_resource_binary_regression_tests.cpp
            tests/editor_core_model_alignment_regression_tests.cpp
            tests/editor_core_grade3_fixed_regression_tests.cpp
            tests/editor_core_grade4_regression_tests.cpp
            tests/editor_core_stage4_complete_regression_tests.cpp
            tests/editor_core_grade5_io_regression_tests.cpp
            tests/editor_core_stage5_io_resources_complete_regression_tests.cpp
            tests/editor_core_grade5_misc_regression_tests.cpp
            tests/editor_core_grade5_model_regression_tests.cpp
            tests/editor_core_area_lifetime_regression_tests.cpp
            tests/editor_core_background_view_regression_tests.cpp
            tests/editor_core_event_tree_view_regression_tests.cpp
            tests/editor_core_background_lifetime_regression_tests.cpp
            tests/editor_core_grade5_expression_regression_tests.cpp
            tests/editor_core_stage3_fixed_cab_regression_tests.cpp
            tests/editor_core_stage3_fixed_module_regression_tests.cpp
            tests/editor_core_stage4_area_card_event_regression_tests.cpp
            tests/editor_core_stage5_expression_flag_regression_tests.cpp
            tests/editor_core_stage5_script_utils_complete_regression_tests.cpp
            tests/editor_core_stage3_binary_utility_regression_tests.cpp
            tests/editor_core_summary_private_api_access.cpp
            tests/editor_core_stage5_summary_usecounter_regression_tests.cpp)
        target_compile_features(cwxeditor_tests PRIVATE cxx_std_20)
        target_compile_features(cwxeditor_parity_tests PRIVATE cxx_std_20)
        target_compile_features(cwxeditor_extended_parity_tests PRIVATE cxx_std_20)
        target_compile_features(cwxeditor_core_d_unittest_parity_tests PRIVATE cxx_std_20)
        target_compile_features(cwxeditor_contract_tests PRIVATE cxx_std_20)
        target_compile_features(cwxeditor_shutdown_tests PRIVATE cxx_std_20)
        target_include_directories(cwxeditor_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
        target_include_directories(cwxeditor_parity_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
        target_include_directories(cwxeditor_extended_parity_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
        target_include_directories(cwxeditor_core_d_unittest_parity_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
        target_include_directories(cwxeditor_contract_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
        target_include_directories(cwxeditor_shutdown_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
        target_compile_definitions(cwxeditor_tests PRIVATE
            CWXEDITOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
        target_compile_definitions(cwxeditor_contract_tests PRIVATE
            CWXEDITOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
        # 契約テストの診断用コンパイル分岐を、Windows Editor
        # のReleaseビルドでライブラリ内部に有効化する設定と揃える。
        if(WIN32 AND CARDWIRTH_APP_VARIANT STREQUAL "Editor" AND
           CWXEDITOR_ENABLE_RELEASE_DIAGNOSTICS)
            target_compile_definitions(cwxeditor_contract_tests PRIVATE
                $<$<CONFIG:Release>:CWXEDITOR_LOG_IN_RELEASE=1>)
        endif()
        target_link_libraries(cwxeditor_tests PRIVATE
            cwxeditor_app cwxeditor_gui cwxeditor_core cwxeditor_lib
            ${CWXEDITOR_TEST_RUNTIME_TARGET})
        target_link_libraries(cwxeditor_parity_tests PRIVATE
            cwxeditor_app cwxeditor_gui cwxeditor_core cwxeditor_lib
            ${CWXEDITOR_TEST_RUNTIME_TARGET})
        target_link_libraries(cwxeditor_extended_parity_tests PRIVATE
            cwxeditor_app cwxeditor_gui cwxeditor_core cwxeditor_lib
            ${CWXEDITOR_TEST_RUNTIME_TARGET})
        target_link_libraries(cwxeditor_core_d_unittest_parity_tests PRIVATE
            cwxeditor_core cwxeditor_lib ${CWXEDITOR_TEST_RUNTIME_TARGET})
        target_link_libraries(cwxeditor_contract_tests PRIVATE
            cwxeditor_app cwxeditor_gui cwxeditor_core cwxeditor_lib
            ${CWXEDITOR_TEST_RUNTIME_TARGET})
        target_link_libraries(cwxeditor_shutdown_tests PRIVATE
            cwxeditor_app cwxeditor_gui cwxeditor_core cwxeditor_lib
            ${CWXEDITOR_TEST_RUNTIME_TARGET})
        foreach(cwxeditor_test_host IN ITEMS
                cwxeditor_tests cwxeditor_parity_tests
                cwxeditor_extended_parity_tests cwxeditor_core_d_unittest_parity_tests
                cwxeditor_contract_tests cwxeditor_shutdown_tests)
            cardwirth_enable_exception_origin(${cwxeditor_test_host})
        endforeach()
        # 大きな既存フィクスチャの翻訳単位は、メモリの少ないビルド環境
        # で容量を使い切る。GCC内部の回収処理だけを調整し、テスト
        # の意味、アサーション、最適化、他のターゲットは変更しない。
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            target_compile_options(cwxeditor_tests PRIVATE
                "--param=ggc-min-expand=5" "--param=ggc-min-heapsize=4096")
        endif()
        if(MSVC)
            target_compile_options(cwxeditor_tests PRIVATE /W4 /EHsc /utf-8 /bigobj)
            target_compile_options(cwxeditor_parity_tests PRIVATE /W4 /EHsc /utf-8 /bigobj)
            target_compile_options(cwxeditor_extended_parity_tests PRIVATE /W4 /EHsc /utf-8 /bigobj)
            target_compile_options(cwxeditor_core_d_unittest_parity_tests PRIVATE /W4 /EHsc /utf-8 /bigobj)
            target_compile_options(cwxeditor_contract_tests PRIVATE /W4 /EHsc /utf-8 /bigobj)
            # 複数ウィンドウの契約テストは、大きなフィクスチャを同じスレッドに複数
            # 保持する。既定の1 MiBスタックでは結果を報告する前に終了する。
            target_link_options(cwxeditor_contract_tests PRIVATE /STACK:8388608)
            target_compile_options(cwxeditor_shutdown_tests PRIVATE /W4 /EHsc /utf-8 /bigobj)
        endif()
        if(WIN32)
            target_link_libraries(cwxeditor_tests PRIVATE
                User32 Gdi32 Shell32 Ole32 Comctl32 Comdlg32 Winmm Shlwapi Ws2_32
                Dbghelp Setupapi WindowsCodecs Uuid Normaliz)
            target_link_libraries(cwxeditor_parity_tests PRIVATE
                User32 Gdi32 Shell32 Ole32 Comctl32 Comdlg32 Winmm Shlwapi Ws2_32
                Dbghelp Setupapi WindowsCodecs Uuid Normaliz)
            target_link_libraries(cwxeditor_extended_parity_tests PRIVATE
                User32 Gdi32 Shell32 Ole32 Comctl32 Comdlg32 Winmm Shlwapi Ws2_32
                Dbghelp Setupapi WindowsCodecs Uuid Normaliz)
            target_link_libraries(cwxeditor_core_d_unittest_parity_tests PRIVATE
                User32 Gdi32 Shell32 Ole32 Comctl32 Comdlg32 Winmm Shlwapi Ws2_32
                Dbghelp Setupapi WindowsCodecs Uuid Normaliz)
            target_link_libraries(cwxeditor_contract_tests PRIVATE
                User32 Gdi32 Shell32 Ole32 Comctl32 Comdlg32 Winmm Shlwapi Ws2_32
                Dbghelp Setupapi WindowsCodecs Uuid Normaliz)
            target_link_libraries(cwxeditor_shutdown_tests PRIVATE
                User32 Gdi32 Shell32 Ole32 Comctl32 Comdlg32 Winmm Shlwapi Ws2_32
                Dbghelp Setupapi WindowsCodecs Uuid Normaliz)
        endif()
        # Engineの実行時処理をリンクせず、Editor専用テストでALL
        # の画面対応と実際のEditor起動ライフサイクルを検証する。
        target_sources(cwxeditor_tests PRIVATE app/all/all_layout.cpp app/editor_launch.cpp)
        target_compile_definitions(cwxeditor_tests PRIVATE CARDWIRTH_EMBED_EDITOR=1)
        add_test(NAME cwxeditor_api COMMAND cwxeditor_tests)
        add_test(NAME cwxeditor_d_cpp_parity COMMAND cwxeditor_parity_tests)
        add_test(NAME cwxeditor_d_cpp_extended_parity COMMAND cwxeditor_extended_parity_tests)
        add_test(NAME cwxeditor_core_d_unittest_parity COMMAND cwxeditor_core_d_unittest_parity_tests)
        add_test(NAME cwxeditor_contracts COMMAND cwxeditor_contract_tests)
        add_test(NAME cwxeditor_event_dialog_warning_ownership COMMAND cwxeditor_contract_tests --event-dialog-warning-ownership)
        set_tests_properties(cwxeditor_event_dialog_warning_ownership PROPERTIES TIMEOUT 180 LABELS "editor;gui;targeted")
        add_test(NAME cwxeditor_mainwindow_api45_repair COMMAND cwxeditor_contract_tests --mainwindow-api45-repair)
        set_tests_properties(cwxeditor_mainwindow_api45_repair PROPERTIES TIMEOUT 180 LABELS "editor;gui;targeted")
        add_test(NAME cwxeditor_event_readonly_total_capability COMMAND cwxeditor_contract_tests --event-readonly-total-capability)
        set_tests_properties(cwxeditor_event_readonly_total_capability PROPERTIES TIMEOUT 120 LABELS "editor;gui;targeted")
        add_test(NAME cwxeditor_shutdown COMMAND cwxeditor_shutdown_tests)

        # Release構成も含め、プラットフォームに依存しないタスクの所有権と配送の契約を検証する。
        add_executable(cwxeditor_task_group_tests tests/editor_task_group_tests.cpp)
        target_compile_features(cwxeditor_task_group_tests PRIVATE cxx_std_20)
        target_include_directories(cwxeditor_task_group_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
        target_link_libraries(cwxeditor_task_group_tests PRIVATE cwxeditor_lib)
        cardwirth_enable_exception_origin(cwxeditor_task_group_tests)
        if(MSVC)
            target_compile_options(cwxeditor_task_group_tests PRIVATE /W4 /EHsc /utf-8)
        endif()
        add_test(NAME cwxeditor_task_group COMMAND cwxeditor_task_group_tests)
        set_tests_properties(cwxeditor_task_group PROPERTIES TIMEOUT 15 LABELS "editor;task;contract")

        add_executable(cwxeditor_file_picker_tests tests/editor_file_picker_tests.cpp)
        target_compile_features(cwxeditor_file_picker_tests PRIVATE cxx_std_20)
        target_include_directories(cwxeditor_file_picker_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
        target_link_libraries(cwxeditor_file_picker_tests PRIVATE
            cwxeditor_lib ${CWXEDITOR_TEST_RUNTIME_TARGET})
        cardwirth_enable_exception_origin(cwxeditor_file_picker_tests)
        if(MSVC)
            target_compile_options(cwxeditor_file_picker_tests PRIVATE /W4 /EHsc /utf-8)
        endif()
        add_test(NAME cwxeditor_file_picker COMMAND cwxeditor_file_picker_tests)
        set_tests_properties(cwxeditor_file_picker PROPERTIES TIMEOUT 15 LABELS "editor;storage;contract")
        # このアダプターのヒント方針はJavaだけで実装されている。Android
        # クラスや選択画面、端末を使わず、ホストのJDKで検証する。
        if(NOT CMAKE_CROSSCOMPILING)
            find_package(Java QUIET COMPONENTS Development Runtime)
            if(Java_FOUND)
                set(CWXEDITOR_DOCUMENT_HINTS_CLASSES "${CMAKE_CURRENT_BINARY_DIR}/document-hints-classes")
                set(CWXEDITOR_DOCUMENT_HINTS_SOURCE
                    "${CMAKE_CURRENT_SOURCE_DIR}/platform/android/app/src/main/java/jp/cardwirth/sdlsample/DocumentMimeHints.java")
                set(CWXEDITOR_DOCUMENT_HINTS_TEST
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/android/DocumentMimeHintsTests.java")
                set(CWXEDITOR_DOCUMENT_HINTS_CLASS
                    "${CWXEDITOR_DOCUMENT_HINTS_CLASSES}/jp/cardwirth/sdlsample/DocumentMimeHintsTests.class")
                set(CWXEDITOR_DOCUMENT_LEASES_SOURCE
                    "${CMAKE_CURRENT_SOURCE_DIR}/platform/android/app/src/main/java/jp/cardwirth/sdlsample/DocumentImportLeases.java")
                set(CWXEDITOR_DOCUMENT_LEASES_TEST
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/android/DocumentImportLeasesTests.java")
                set(CWXEDITOR_DOCUMENT_LEASES_CLASS
                    "${CWXEDITOR_DOCUMENT_HINTS_CLASSES}/jp/cardwirth/sdlsample/DocumentImportLeasesTests.class")
                add_custom_command(OUTPUT "${CWXEDITOR_DOCUMENT_HINTS_CLASS}" "${CWXEDITOR_DOCUMENT_LEASES_CLASS}"
                    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CWXEDITOR_DOCUMENT_HINTS_CLASSES}"
                    COMMAND "${Java_JAVAC_EXECUTABLE}" -encoding UTF-8 -d "${CWXEDITOR_DOCUMENT_HINTS_CLASSES}"
                        "${CWXEDITOR_DOCUMENT_HINTS_SOURCE}" "${CWXEDITOR_DOCUMENT_HINTS_TEST}"
                        "${CWXEDITOR_DOCUMENT_LEASES_SOURCE}" "${CWXEDITOR_DOCUMENT_LEASES_TEST}"
                    DEPENDS "${CWXEDITOR_DOCUMENT_HINTS_SOURCE}" "${CWXEDITOR_DOCUMENT_HINTS_TEST}"
                        "${CWXEDITOR_DOCUMENT_LEASES_SOURCE}" "${CWXEDITOR_DOCUMENT_LEASES_TEST}"
                    VERBATIM)
                add_custom_target(cwxeditor_document_hints_tests ALL DEPENDS
                    "${CWXEDITOR_DOCUMENT_HINTS_CLASS}" "${CWXEDITOR_DOCUMENT_LEASES_CLASS}")
                add_test(NAME cwxeditor_document_hints COMMAND "${Java_JAVA_EXECUTABLE}"
                    -cp "${CWXEDITOR_DOCUMENT_HINTS_CLASSES}" jp.cardwirth.sdlsample.DocumentMimeHintsTests)
                set_tests_properties(cwxeditor_document_hints PROPERTIES TIMEOUT 15 LABELS "editor;storage;contract;android")
                add_test(NAME cwxeditor_document_import_leases COMMAND "${Java_JAVA_EXECUTABLE}"
                    -cp "${CWXEDITOR_DOCUMENT_HINTS_CLASSES}" jp.cardwirth.sdlsample.DocumentImportLeasesTests)
                set_tests_properties(cwxeditor_document_import_leases PROPERTIES TIMEOUT 15 LABELS "editor;storage;contract;android")
            else()
                message(STATUS "Host JDK unavailable: Android document hint contract test is not registered")
            endif()
        endif()

        add_library(cwxeditor_test_support STATIC
            tests/cwxeditor_test_support/test_harness.cpp
            tests/cwxeditor_test_support/editor_fixture.cpp
            tests/cwxeditor_test_support/ui_contract.cpp)
        target_compile_features(cwxeditor_test_support PUBLIC cxx_std_20)
        target_include_directories(cwxeditor_test_support PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}")
        target_link_libraries(cwxeditor_test_support PUBLIC
            cwxeditor_app cwxeditor_gui cwxeditor_core cwxeditor_lib
            ${CWXEDITOR_TEST_RUNTIME_TARGET})
        cardwirth_enable_exception_origin(cwxeditor_test_support)
        if(MSVC)
            target_compile_options(cwxeditor_test_support PRIVATE
                /W4 /EHsc /utf-8 /bigobj)
            target_compile_options(cwxeditor_test_support INTERFACE /utf-8 /bigobj)
        endif()

        # このエクスポーターの回帰テストはGUIのコンテントアダプターを使用
        # するが、リンク対象は必要最小限にする。下記のコア専用D互換テスト
        # にはcwxeditor_guiやアプリケーションをリンクしない。
        add_executable(editor_core_stage5_script_gui_export_tests
            tests/editor_core_stage5_script_gui_export_tests.cpp)
        cardwirth_enable_exception_origin(editor_core_stage5_script_gui_export_tests)
        target_compile_features(editor_core_stage5_script_gui_export_tests PRIVATE cxx_std_20)
        target_include_directories(editor_core_stage5_script_gui_export_tests PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}")
        target_link_libraries(editor_core_stage5_script_gui_export_tests PRIVATE
            cwxeditor_gui cwxeditor_core cwxeditor_lib
            ${CWXEDITOR_TEST_RUNTIME_TARGET})
        if(MSVC)
            target_compile_options(editor_core_stage5_script_gui_export_tests PRIVATE
                /W4 /EHsc /utf-8 /bigobj)
        endif()
        if(WIN32)
            target_link_libraries(editor_core_stage5_script_gui_export_tests PRIVATE
                User32 Gdi32 Shell32 Ole32 Comctl32 Comdlg32 Winmm Shlwapi Ws2_32
                Dbghelp Setupapi WindowsCodecs Uuid Normaliz)
        endif()
        add_test(NAME editor_core_stage5_script_gui_export_tests
                 COMMAND editor_core_stage5_script_gui_export_tests)
        set_tests_properties(editor_core_stage5_script_gui_export_tests
            PROPERTIES TIMEOUT 120 LABELS "editor;gui;script;regression")

        add_executable(cwxeditor_ui_contract_dump
            tests/cwxeditor_ui_contract_dump.cpp)
        cardwirth_add_bundled_editor_test(cwxeditor_ui_contract_tests
            tests/cwxeditor_ui_contract_tests.cpp)
        add_executable(cwxeditor_workflow_tests
            tests/cwxeditor_workflow_tests.cpp)
        cardwirth_add_bundled_editor_test(cwxeditor_recovered_xml_values_tests tests/cwxeditor_recovered_xml_values_tests.cpp)
        add_test(NAME cwxeditor_recovered_xml_values_tests COMMAND ${cwxeditor_recovered_xml_values_tests_TEST_COMMAND})
        cardwirth_add_bundled_editor_test(editor_expression_for_repair_tests tests/editor_expression_for_repair_tests.cpp)
        add_test(NAME editor_expression_for_repair_tests COMMAND ${editor_expression_for_repair_tests_TEST_COMMAND})
        cardwirth_add_bundled_editor_test(editor_save_generation_repair_tests tests/editor_save_generation_repair_tests.cpp)
        add_test(NAME editor_save_generation_repair_tests COMMAND ${editor_save_generation_repair_tests_TEST_COMMAND})
        add_executable(editor_catalog_encoding_repair_tests tests/editor_catalog_encoding_repair_tests.cpp)
        add_test(NAME editor_catalog_encoding_repair_tests COMMAND editor_catalog_encoding_repair_tests)
        cardwirth_add_bundled_editor_test(editor_unlink_history_repair_tests tests/editor_unlink_history_repair_tests.cpp)
        add_test(NAME editor_unlink_history_repair_tests COMMAND ${editor_unlink_history_repair_tests_TEST_COMMAND})
        add_executable(editor_resource_clone_contract_tests tests/editor_resource_clone_contract_tests.cpp)
        add_test(NAME editor_resource_clone_contract_tests COMMAND editor_resource_clone_contract_tests)
        set_tests_properties(editor_resource_clone_contract_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        cardwirth_add_bundled_editor_test(editor_resource_history_repair_tests tests/editor_resource_history_repair_tests.cpp)
        add_test(NAME editor_resource_history_repair_tests COMMAND ${editor_resource_history_repair_tests_TEST_COMMAND})
        set_tests_properties(editor_resource_history_repair_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        cardwirth_add_bundled_editor_test(editor_indexed_handcard_repair_tests tests/editor_indexed_handcard_repair_tests.cpp)
        add_test(NAME editor_indexed_handcard_repair_tests COMMAND ${editor_indexed_handcard_repair_tests_TEST_COMMAND})
        add_executable(editor_script_argument_repair_tests tests/editor_script_argument_repair_tests.cpp)
        add_test(NAME editor_script_argument_repair_tests COMMAND editor_script_argument_repair_tests)
        add_executable(editor_event_command_repair_tests tests/editor_event_command_repair_tests.cpp)
        add_test(NAME editor_event_command_repair_tests COMMAND editor_event_command_repair_tests)
        cardwirth_add_bundled_editor_test(editor_custom_label_repair_tests tests/editor_custom_label_repair_tests.cpp)
        add_test(NAME editor_custom_label_repair_tests COMMAND ${editor_custom_label_repair_tests_TEST_COMMAND})
        add_executable(editor_cast_value_repair_tests tests/editor_cast_value_repair_tests.cpp)
        cardwirth_enable_exception_origin(editor_cast_value_repair_tests)
        add_test(NAME editor_cast_value_repair_tests COMMAND editor_cast_value_repair_tests)
        cardwirth_add_bundled_editor_test(cwxeditor_recovered_talk_dialog_apply_tests tests/cwxeditor_recovered_talk_dialog_apply_tests.cpp)
        add_test(NAME cwxeditor_recovered_talk_dialog_apply_tests COMMAND ${cwxeditor_recovered_talk_dialog_apply_tests_TEST_COMMAND})
        cardwirth_add_bundled_editor_test(editor_card_apply_repair_tests tests/editor_card_apply_repair_tests.cpp)
        add_test(NAME editor_card_apply_repair_tests COMMAND ${editor_card_apply_repair_tests_TEST_COMMAND})
        add_executable(cwxeditor_archive_repair_tests tests/cwxeditor_archive_repair_tests.cpp)
        add_test(NAME cwxeditor_archive_repair_tests COMMAND cwxeditor_archive_repair_tests)
        add_executable(editor_reference_binding_repair_tests tests/editor_reference_binding_repair_tests.cpp)
        add_test(NAME editor_reference_binding_repair_tests COMMAND editor_reference_binding_repair_tests)
        set_tests_properties(editor_reference_binding_repair_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        cardwirth_add_bundled_editor_test(editor_coupon_conversion_repair_tests tests/editor_coupon_conversion_repair_tests.cpp)
        add_test(NAME editor_coupon_conversion_repair_tests COMMAND ${editor_coupon_conversion_repair_tests_TEST_COMMAND})
        cardwirth_add_bundled_editor_test(editor_initial_card_command_tests tests/editor_initial_card_command_tests.cpp)
        add_test(NAME editor_initial_card_command_tests COMMAND ${editor_initial_card_command_tests_TEST_COMMAND})
        cardwirth_add_bundled_editor_test(editor_cardpane_life_mark_geometry_tests WITH_ARGS tests/editor_cardpane_life_mark_geometry_tests.cpp)
        add_test(NAME editor_cardpane_life_mark_geometry_tests COMMAND ${editor_cardpane_life_mark_geometry_tests_TEST_COMMAND})
        cardwirth_add_bundled_editor_test(editor_cast_aptitude_source_scale_tests tests/editor_cast_aptitude_source_scale_tests.cpp)
        add_test(NAME editor_cast_aptitude_source_scale_tests COMMAND ${editor_cast_aptitude_source_scale_tests_TEST_COMMAND})
        cardwirth_add_bundled_editor_test(editor_settings_message_contract_tests tests/editor_settings_message_contract_tests.cpp)
        add_test(NAME editor_settings_message_contract_tests COMMAND ${editor_settings_message_contract_tests_TEST_COMMAND})
        cardwirth_add_bundled_editor_test(editor_scene_contract_tests tests/editor_scene_contract_tests.cpp)
        add_test(NAME editor_scene_contract_tests COMMAND ${editor_scene_contract_tests_TEST_COMMAND})
        cardwirth_add_bundled_editor_test(editor_party_directory_command_tests tests/editor_party_directory_command_tests.cpp)
        if(WIN32)
            cardwirth_attach_editor_test_resource(editor_party_directory_command_tests
                tests/editor_party_directory_command_tests_version.rc)
        endif()
        add_test(NAME editor_party_directory_command_tests COMMAND ${editor_party_directory_command_tests_TEST_COMMAND})
        cardwirth_add_bundled_editor_test(editor_variable_state_command_tests tests/editor_variable_state_command_tests.cpp)
        add_test(NAME editor_variable_state_command_tests COMMAND ${editor_variable_state_command_tests_TEST_COMMAND})
        add_executable(editor_ignore_directory_contract_tests tests/editor_ignore_directory_contract_tests.cpp)
        add_test(NAME editor_ignore_directory_contract_tests COMMAND editor_ignore_directory_contract_tests)
        add_executable(editor_name_warning_command_tests tests/editor_name_warning_command_tests.cpp)
        add_test(NAME editor_name_warning_command_tests COMMAND editor_name_warning_command_tests)
        cardwirth_add_bundled_editor_test(editor_card_creation_pending_tests tests/editor_card_creation_pending_tests.cpp)
        add_test(NAME editor_card_creation_pending_tests COMMAND ${editor_card_creation_pending_tests_TEST_COMMAND})
        add_executable(editor_stateful_callback_contract_tests tests/editor_stateful_callback_contract_tests.cpp)
        add_test(NAME editor_stateful_callback_contract_tests COMMAND editor_stateful_callback_contract_tests)
        cardwirth_add_bundled_editor_test(editor_saveas_close_transaction_tests tests/editor_saveas_close_transaction_tests.cpp)
        add_test(NAME editor_saveas_close_transaction_tests COMMAND ${editor_saveas_close_transaction_tests_TEST_COMMAND})
        cardwirth_add_bundled_editor_test(editor_dialog_retirement_ownership_tests tests/editor_dialog_retirement_ownership_tests.cpp)
        add_test(NAME editor_dialog_retirement_ownership_tests COMMAND ${editor_dialog_retirement_ownership_tests_TEST_COMMAND})
        cardwirth_add_bundled_editor_test(editor_shortcut_shell_ownership_tests tests/editor_shortcut_shell_ownership_tests.cpp)
        add_test(NAME editor_shortcut_shell_ownership_tests COMMAND ${editor_shortcut_shell_ownership_tests_TEST_COMMAND})
        add_executable(editor_text_font_contract_tests tests/editor_text_font_contract_tests.cpp)
        add_test(NAME editor_text_font_contract_tests COMMAND editor_text_font_contract_tests)
        cardwirth_add_bundled_editor_test(editor_file_sync_backup_tests tests/editor_file_sync_backup_tests.cpp)
        add_test(NAME editor_file_sync_backup_tests COMMAND ${editor_file_sync_backup_tests_TEST_COMMAND})
        cardwirth_add_bundled_editor_test(editor_variable_state_contract_tests tests/editor_variable_state_contract_tests.cpp)
        add_test(NAME editor_variable_state_contract_tests COMMAND ${editor_variable_state_contract_tests_TEST_COMMAND})
        add_executable(editor_expression_script_contract_tests tests/editor_expression_script_contract_tests.cpp)
        add_test(NAME editor_expression_script_contract_tests COMMAND editor_expression_script_contract_tests)
        add_executable(editor_event_tree_command_tests tests/editor_event_tree_command_tests.cpp)
        add_test(NAME editor_event_tree_command_tests COMMAND editor_event_tree_command_tests)
        add_executable(editor_coupon_editing_contract_tests tests/editor_coupon_editing_contract_tests.cpp)
        add_test(NAME editor_coupon_editing_contract_tests COMMAND editor_coupon_editing_contract_tests)
        cardwirth_add_bundled_editor_test(editor_shared_use_history_tests tests/editor_shared_use_history_tests.cpp)
        add_test(NAME editor_shared_use_history_tests COMMAND ${editor_shared_use_history_tests_TEST_COMMAND})
        add_executable(editor_event_legacy_paste_tests tests/editor_event_legacy_paste_tests.cpp)
        if(MSVC)
            # cwxeditor_contract_testsと同様、複数の表示済みウィ
            # ンドウのフィクスチャが共存するため、既定の1 MiBスタックでは溢れる。
            target_link_options(editor_event_legacy_paste_tests PRIVATE /STACK:8388608)
        endif()
        add_test(NAME editor_event_legacy_paste_tests COMMAND editor_event_legacy_paste_tests)
        cardwirth_add_bundled_editor_test(editor_scenario_creation_contract_tests tests/editor_scenario_creation_contract_tests.cpp)
        add_test(NAME editor_scenario_creation_contract_tests COMMAND ${editor_scenario_creation_contract_tests_TEST_COMMAND})
        cardwirth_add_bundled_editor_test(editor_settings_skin_lifecycle_tests tests/editor_settings_skin_lifecycle_tests.cpp)
        add_test(NAME editor_settings_skin_lifecycle_tests COMMAND ${editor_settings_skin_lifecycle_tests_TEST_COMMAND})
        cardwirth_add_bundled_editor_test(editor_scene_payload_contract_tests tests/editor_scene_payload_contract_tests.cpp)
        add_test(NAME editor_scene_payload_contract_tests COMMAND ${editor_scene_payload_contract_tests_TEST_COMMAND})
        cardwirth_add_bundled_editor_test(editor_party_launch_history_tests tests/editor_party_launch_history_tests.cpp)
        add_test(NAME editor_party_launch_history_tests COMMAND ${editor_party_launch_history_tests_TEST_COMMAND})
        cardwirth_add_bundled_editor_test(editor_clipboard_format_contract_tests tests/editor_clipboard_format_contract_tests.cpp)
        add_test(NAME editor_clipboard_format_contract_tests COMMAND ${editor_clipboard_format_contract_tests_TEST_COMMAND})
        cardwirth_add_bundled_editor_test(editor_handcard_transfer_contract_tests WITH_ARGS tests/editor_handcard_transfer_contract_tests.cpp)
        add_test(NAME editor_handcard_transfer_contract_tests COMMAND ${editor_handcard_transfer_contract_tests_TEST_COMMAND})
        cardwirth_add_bundled_editor_test(editor_image_transfer_contract_tests tests/editor_image_transfer_contract_tests.cpp)
        add_test(NAME editor_image_transfer_contract_tests COMMAND ${editor_image_transfer_contract_tests_TEST_COMMAND})
        add_executable(editor_search_replacement_contract_tests tests/editor_search_replacement_contract_tests.cpp)
        add_test(NAME editor_search_replacement_contract_tests COMMAND editor_search_replacement_contract_tests)
        cardwirth_add_bundled_editor_test(editor_reference_navigation_contract_tests WITH_ARGS tests/editor_reference_navigation_contract_tests.cpp)
        add_test(NAME editor_reference_navigation_contract_tests COMMAND ${editor_reference_navigation_contract_tests_TEST_COMMAND})
        cardwirth_add_bundled_editor_test(editor_model_owner_dirty_tests tests/editor_model_owner_dirty_tests.cpp)
        add_test(NAME editor_model_owner_dirty_tests COMMAND ${editor_model_owner_dirty_tests_TEST_COMMAND})
        cardwirth_add_bundled_editor_test(editor_flag_history_contract_tests tests/editor_flag_history_contract_tests.cpp)
        add_test(NAME editor_flag_history_contract_tests COMMAND ${editor_flag_history_contract_tests_TEST_COMMAND})
        add_executable(editor_initial_model_contract_tests tests/editor_initial_model_contract_tests.cpp)
        add_test(NAME editor_initial_model_contract_tests COMMAND editor_initial_model_contract_tests)
        cardwirth_add_bundled_editor_test(editor_template_refresh_contract_tests tests/editor_template_refresh_contract_tests.cpp)
        add_test(NAME editor_template_refresh_contract_tests COMMAND ${editor_template_refresh_contract_tests_TEST_COMMAND})
        add_executable(editor_remaining_value_contract_tests tests/editor_remaining_value_contract_tests.cpp)
        add_test(NAME editor_remaining_value_contract_tests COMMAND editor_remaining_value_contract_tests)
        add_executable(editor_shortcut_binding_contract_tests tests/editor_shortcut_binding_contract_tests.cpp)
        add_test(NAME editor_shortcut_binding_contract_tests COMMAND editor_shortcut_binding_contract_tests)
        add_executable(editor_inline_transaction_tests tests/editor_inline_transaction_tests.cpp)
        add_test(NAME editor_inline_transaction_tests COMMAND editor_inline_transaction_tests)
        add_executable(editor_expression_boundary_contract_tests tests/editor_expression_boundary_contract_tests.cpp)
        add_test(NAME editor_expression_boundary_contract_tests COMMAND editor_expression_boundary_contract_tests)
        add_executable(editor_initial_event_contract_tests tests/editor_initial_event_contract_tests.cpp)
        add_test(NAME editor_initial_event_contract_tests COMMAND editor_initial_event_contract_tests)
        add_executable(editor_state_notification_contract_tests tests/editor_state_notification_contract_tests.cpp)
        add_test(NAME editor_state_notification_contract_tests COMMAND editor_state_notification_contract_tests)
        cardwirth_add_bundled_editor_test(editor_history_autoapply_tests tests/editor_history_autoapply_tests.cpp)
        add_test(NAME editor_history_autoapply_tests COMMAND ${editor_history_autoapply_tests_TEST_COMMAND})
        cardwirth_add_bundled_editor_test(editor_supplemental_value_contract_tests tests/editor_supplemental_value_contract_tests.cpp)
        add_test(NAME editor_supplemental_value_contract_tests COMMAND ${editor_supplemental_value_contract_tests_TEST_COMMAND})
        add_executable(editor_initial_storage_contract_tests tests/editor_initial_storage_contract_tests.cpp)
        add_test(NAME editor_initial_storage_contract_tests COMMAND editor_initial_storage_contract_tests)
        cardwirth_add_bundled_editor_test(editor_flag_default_projection_tests tests/editor_flag_default_projection_tests.cpp)
        add_test(NAME editor_flag_default_projection_tests COMMAND ${editor_flag_default_projection_tests_TEST_COMMAND})
        cardwirth_add_bundled_editor_test(editor_search_remaining_backend_tests tests/editor_search_remaining_backend_tests.cpp)
        add_test(NAME editor_search_remaining_backend_tests COMMAND ${editor_search_remaining_backend_tests_TEST_COMMAND})
        add_executable(editor_search_public_helper_contract_tests tests/editor_search_public_helper_contract_tests.cpp)
        add_test(NAME editor_search_public_helper_contract_tests COMMAND editor_search_public_helper_contract_tests)
        add_executable(editor_script_partial_api_contract_tests tests/editor_script_partial_api_contract_tests.cpp)
        add_test(NAME editor_script_partial_api_contract_tests COMMAND editor_script_partial_api_contract_tests)
        add_executable(editor_shared_api_recovered_tests tests/editor_shared_api_recovered_tests.cpp)
        add_test(NAME editor_shared_api_recovered_tests COMMAND editor_shared_api_recovered_tests)
        set_tests_properties(editor_shared_api_recovered_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        cardwirth_add_bundled_editor_test(editor_chooser_api_residual_tests tests/editor_chooser_api_residual_tests.cpp)
        add_test(NAME editor_chooser_api_residual_tests COMMAND ${editor_chooser_api_residual_tests_TEST_COMMAND})
        set_tests_properties(editor_chooser_api_residual_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_model_contracts_recovered_tests tests/editor_model_contracts_recovered_tests.cpp)
        add_test(NAME editor_model_contracts_recovered_tests COMMAND editor_model_contracts_recovered_tests)
        set_tests_properties(editor_model_contracts_recovered_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_event_display_settings_recovery_tests tests/editor_event_display_settings_recovery_tests.cpp)
        add_test(NAME editor_event_display_settings_recovery_tests COMMAND editor_event_display_settings_recovery_tests)
        set_tests_properties(editor_event_display_settings_recovery_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        cardwirth_add_bundled_editor_test(editor_message_template_recovered_tests tests/editor_message_template_recovered_tests.cpp)
        add_test(NAME editor_message_template_recovered_tests COMMAND ${editor_message_template_recovered_tests_TEST_COMMAND})
        set_tests_properties(editor_message_template_recovered_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_tools_timebar_recovery_tests tests/editor_tools_timebar_recovery_tests.cpp)
        add_test(NAME editor_tools_timebar_recovery_tests COMMAND editor_tools_timebar_recovery_tests)
        set_tests_properties(editor_tools_timebar_recovery_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_motion_coupon_command_tests tests/editor_motion_coupon_command_tests.cpp)
        add_test(NAME editor_motion_coupon_command_tests COMMAND editor_motion_coupon_command_tests)
        set_tests_properties(editor_motion_coupon_command_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_recovered_event_boundaries_tests tests/editor_recovered_event_boundaries_tests.cpp)
        add_test(NAME editor_recovered_event_boundaries_tests COMMAND editor_recovered_event_boundaries_tests)
        set_tests_properties(editor_recovered_event_boundaries_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        cardwirth_add_bundled_editor_test(editor_scene_selection_recovered_tests tests/editor_scene_selection_recovered_tests.cpp)
        add_test(NAME editor_scene_selection_recovered_tests COMMAND ${editor_scene_selection_recovered_tests_TEST_COMMAND})
        set_tests_properties(editor_scene_selection_recovered_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        cardwirth_add_bundled_editor_test(editor_scene_route_residual_tests tests/editor_scene_route_residual_tests.cpp)
        add_test(NAME editor_scene_route_residual_tests COMMAND ${editor_scene_route_residual_tests_TEST_COMMAND})
        set_tests_properties(editor_scene_route_residual_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_template_ignition_route_tests tests/editor_template_ignition_route_tests.cpp)
        add_test(NAME editor_template_ignition_route_tests COMMAND editor_template_ignition_route_tests)
        set_tests_properties(editor_template_ignition_route_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_audio_retirement_recovered_tests tests/editor_audio_retirement_recovered_tests.cpp)
        add_test(NAME editor_audio_retirement_recovered_tests COMMAND editor_audio_retirement_recovered_tests)
        set_tests_properties(editor_audio_retirement_recovered_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        cardwirth_add_bundled_editor_test(editor_scene_commands_recovered_tests tests/editor_scene_commands_recovered_tests.cpp)
        add_test(NAME editor_scene_commands_recovered_tests COMMAND ${editor_scene_commands_recovered_tests_TEST_COMMAND})
        set_tests_properties(editor_scene_commands_recovered_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        cardwirth_add_bundled_editor_test(editor_image_api_residual_tests tests/editor_image_api_residual_tests.cpp)
        add_test(NAME editor_image_api_residual_tests COMMAND ${editor_image_api_residual_tests_TEST_COMMAND})
        set_tests_properties(editor_image_api_residual_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_supplemental_api_recovered_tests tests/editor_supplemental_api_recovered_tests.cpp)
        add_test(NAME editor_supplemental_api_recovered_tests COMMAND editor_supplemental_api_recovered_tests)
        set_tests_properties(editor_supplemental_api_recovered_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_model_observer_recovered_tests tests/editor_model_observer_recovered_tests.cpp)
        add_test(NAME editor_model_observer_recovered_tests COMMAND editor_model_observer_recovered_tests)
        set_tests_properties(editor_model_observer_recovered_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        cardwirth_add_bundled_editor_test(editor_customtable_recovered_tests tests/editor_customtable_recovered_tests.cpp)
        add_test(NAME editor_customtable_recovered_tests COMMAND ${editor_customtable_recovered_tests_TEST_COMMAND})
        set_tests_properties(editor_customtable_recovered_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_platform_layout_recovery_tests tests/editor_platform_layout_recovery_tests.cpp)
        add_test(NAME editor_platform_layout_recovery_tests COMMAND editor_platform_layout_recovery_tests)
        set_tests_properties(editor_platform_layout_recovery_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        cardwirth_add_bundled_editor_test(editor_navigation_drag_recovered_tests tests/editor_navigation_drag_recovered_tests.cpp)
        add_test(NAME editor_navigation_drag_recovered_tests COMMAND ${editor_navigation_drag_recovered_tests_TEST_COMMAND})
        set_tests_properties(editor_navigation_drag_recovered_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_clipboard_list_recovery_tests tests/editor_clipboard_list_recovery_tests.cpp)
        add_test(NAME editor_clipboard_list_recovery_tests COMMAND editor_clipboard_list_recovery_tests)
        set_tests_properties(editor_clipboard_list_recovery_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_template_rebase_recovered_tests tests/editor_template_rebase_recovered_tests.cpp)
        add_test(NAME editor_template_rebase_recovered_tests COMMAND editor_template_rebase_recovered_tests)
        set_tests_properties(editor_template_rebase_recovered_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_shared_api_completion_tests tests/editor_shared_api_completion_tests.cpp)
        add_test(NAME editor_shared_api_completion_tests COMMAND editor_shared_api_completion_tests)
        set_tests_properties(editor_shared_api_completion_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        cardwirth_add_bundled_editor_test(editor_shared_render_api_tests WITH_ARGS tests/editor_shared_render_api_tests.cpp)
        add_test(NAME editor_shared_render_api_tests COMMAND ${editor_shared_render_api_tests_TEST_COMMAND})
        set_tests_properties(editor_shared_render_api_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_expression_description_recovered_tests tests/editor_expression_description_recovered_tests.cpp)
        add_test(NAME editor_expression_description_recovered_tests COMMAND editor_expression_description_recovered_tests)
        set_tests_properties(editor_expression_description_recovered_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        cardwirth_add_bundled_editor_test(editor_scene_hidden_selection_recovered_tests tests/editor_scene_hidden_selection_recovered_tests.cpp)
        add_test(NAME editor_scene_hidden_selection_recovered_tests COMMAND ${editor_scene_hidden_selection_recovered_tests_TEST_COMMAND})
        set_tests_properties(editor_scene_hidden_selection_recovered_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_picker_dialog_contract_tests tests/editor_picker_dialog_contract_tests.cpp)
        add_test(NAME editor_picker_dialog_contract_tests COMMAND editor_picker_dialog_contract_tests)
        set_tests_properties(editor_picker_dialog_contract_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_name_checkbox_recovered_tests tests/editor_name_checkbox_recovered_tests.cpp)
        add_test(NAME editor_name_checkbox_recovered_tests COMMAND editor_name_checkbox_recovered_tests)
        set_tests_properties(editor_name_checkbox_recovered_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_text_xml_api_recovered_tests tests/editor_text_xml_api_recovered_tests.cpp)
        add_test(NAME editor_text_xml_api_recovered_tests COMMAND editor_text_xml_api_recovered_tests)
        set_tests_properties(editor_text_xml_api_recovered_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_final_state_contract_tests tests/editor_final_state_contract_tests.cpp)
        add_test(NAME editor_final_state_contract_tests COMMAND editor_final_state_contract_tests)
        set_tests_properties(editor_final_state_contract_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_load_language_contract_tests tests/editor_load_language_contract_tests.cpp)
        add_test(NAME editor_load_language_contract_tests COMMAND editor_load_language_contract_tests)
        set_tests_properties(editor_load_language_contract_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        cardwirth_add_bundled_editor_test(editor_live_preview_recovered_tests tests/editor_live_preview_recovered_tests.cpp)
        add_test(NAME editor_live_preview_recovered_tests COMMAND ${editor_live_preview_recovered_tests_TEST_COMMAND})
        set_tests_properties(editor_live_preview_recovered_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_helper_boundaries_recovered_tests tests/editor_helper_boundaries_recovered_tests.cpp)
        add_test(NAME editor_helper_boundaries_recovered_tests COMMAND editor_helper_boundaries_recovered_tests)
        set_tests_properties(editor_helper_boundaries_recovered_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_trace_entries_recovered_tests tests/editor_trace_entries_recovered_tests.cpp)
        add_test(NAME editor_trace_entries_recovered_tests COMMAND editor_trace_entries_recovered_tests)
        set_tests_properties(editor_trace_entries_recovered_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_trace_namewindow6_recovered_tests tests/editor_trace_namewindow6_recovered_tests.cpp)
        add_test(NAME editor_trace_namewindow6_recovered_tests COMMAND editor_trace_namewindow6_recovered_tests)
        set_tests_properties(editor_trace_namewindow6_recovered_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_trace_radarspinner_entries_tests tests/editor_trace_radarspinner_entries_tests.cpp)
        add_test(NAME editor_trace_radarspinner_entries_tests COMMAND editor_trace_radarspinner_entries_tests)
        set_tests_properties(editor_trace_radarspinner_entries_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_trace_undo_entries_tests tests/editor_trace_undo_entries_tests.cpp)
        add_test(NAME editor_trace_undo_entries_tests COMMAND editor_trace_undo_entries_tests)
        set_tests_properties(editor_trace_undo_entries_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_trace_textdialog2_recovered_tests tests/editor_trace_textdialog2_recovered_tests.cpp)
        add_test(NAME editor_trace_textdialog2_recovered_tests COMMAND editor_trace_textdialog2_recovered_tests)
        set_tests_properties(editor_trace_textdialog2_recovered_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_trace_contentinitializer_recovered_tests tests/editor_trace_contentinitializer_recovered_tests.cpp)
        add_test(NAME editor_trace_contentinitializer_recovered_tests COMMAND editor_trace_contentinitializer_recovered_tests)
        set_tests_properties(editor_trace_contentinitializer_recovered_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_trace_abilityview_recovered_tests tests/editor_trace_abilityview_recovered_tests.cpp)
        add_test(NAME editor_trace_abilityview_recovered_tests COMMAND editor_trace_abilityview_recovered_tests)
        set_tests_properties(editor_trace_abilityview_recovered_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_trace_roundview_entries_tests tests/editor_trace_roundview_entries_tests.cpp)
        add_test(NAME editor_trace_roundview_entries_tests COMMAND editor_trace_roundview_entries_tests)
        set_tests_properties(editor_trace_roundview_entries_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_trace_autokeycode_xml_entries_tests tests/editor_trace_autokeycode_xml_entries_tests.cpp)
        add_test(NAME editor_trace_autokeycode_xml_entries_tests COMMAND editor_trace_autokeycode_xml_entries_tests)
        set_tests_properties(editor_trace_autokeycode_xml_entries_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_trace_textdialog_caret_recovered_tests tests/editor_trace_textdialog_caret_recovered_tests.cpp)
        add_test(NAME editor_trace_textdialog_caret_recovered_tests COMMAND editor_trace_textdialog_caret_recovered_tests)
        set_tests_properties(editor_trace_textdialog_caret_recovered_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_incsearch_unicode_gate_tests tests/editor_incsearch_unicode_gate_tests.cpp)
        add_test(NAME editor_incsearch_unicode_gate_tests COMMAND editor_incsearch_unicode_gate_tests)
        set_tests_properties(editor_incsearch_unicode_gate_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_trace_history_recorded_tests tests/editor_trace_history_recorded_tests.cpp)
        add_test(NAME editor_trace_history_recorded_tests COMMAND editor_trace_history_recorded_tests)
        set_tests_properties(editor_trace_history_recorded_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_trace_dprops_recorded_tests tests/editor_trace_dprops_recorded_tests.cpp)
        add_test(NAME editor_trace_dprops_recorded_tests COMMAND editor_trace_dprops_recorded_tests)
        set_tests_properties(editor_trace_dprops_recorded_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_trace_slot_reuse_tests
            tests/editor_trace_slot_reuse_tests.cpp
            cwxeditor_src/core/cwx/perf.cpp)
        target_compile_features(editor_trace_slot_reuse_tests PRIVATE cxx_std_20)
        target_include_directories(editor_trace_slot_reuse_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
        add_test(NAME editor_trace_slot_reuse_tests COMMAND editor_trace_slot_reuse_tests)
        set_tests_properties(editor_trace_slot_reuse_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_trace_chooser_recorded_tests tests/editor_trace_chooser_recorded_tests.cpp)
        add_test(NAME editor_trace_chooser_recorded_tests COMMAND editor_trace_chooser_recorded_tests)
        set_tests_properties(editor_trace_chooser_recorded_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_trace_dskin_recorded_tests tests/editor_trace_dskin_recorded_tests.cpp)
        add_test(NAME editor_trace_dskin_recorded_tests COMMAND editor_trace_dskin_recorded_tests)
        set_tests_properties(editor_trace_dskin_recorded_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_trace_flag_recorded_tests tests/editor_trace_flag_recorded_tests.cpp)
        add_test(NAME editor_trace_flag_recorded_tests COMMAND editor_trace_flag_recorded_tests)
        set_tests_properties(editor_trace_flag_recorded_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_flag_text_behavior_tests tests/editor_flag_text_behavior_tests.cpp)
        add_test(NAME editor_flag_text_behavior_tests COMMAND editor_flag_text_behavior_tests)
        set_tests_properties(editor_flag_text_behavior_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_trace_coupon_recorded_tests tests/editor_trace_coupon_recorded_tests.cpp)
        add_test(NAME editor_trace_coupon_recorded_tests COMMAND editor_trace_coupon_recorded_tests)
        set_tests_properties(editor_trace_coupon_recorded_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_trace_motion_recorded_tests tests/editor_trace_motion_recorded_tests.cpp)
        add_test(NAME editor_trace_motion_recorded_tests COMMAND editor_trace_motion_recorded_tests)
        set_tests_properties(editor_trace_motion_recorded_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_repair_coupon_absent_index_tests tests/editor_repair_coupon_absent_index_tests.cpp)
        add_test(NAME editor_repair_coupon_absent_index_tests COMMAND editor_repair_coupon_absent_index_tests)
        set_tests_properties(editor_repair_coupon_absent_index_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_repair_motion_callback_ownership_tests tests/editor_repair_motion_callback_ownership_tests.cpp)
        add_test(NAME editor_repair_motion_callback_ownership_tests COMMAND editor_repair_motion_callback_ownership_tests)
        set_tests_properties(editor_repair_motion_callback_ownership_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_trace_path_recorded_tests tests/editor_trace_path_recorded_tests.cpp)
        add_test(NAME editor_trace_path_recorded_tests COMMAND editor_trace_path_recorded_tests)
        set_tests_properties(editor_trace_path_recorded_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_trace_menu_recorded_tests tests/editor_trace_menu_recorded_tests.cpp)
        add_test(NAME editor_trace_menu_recorded_tests COMMAND editor_trace_menu_recorded_tests)
        set_tests_properties(editor_trace_menu_recorded_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_trace_race_recorded_tests tests/editor_trace_race_recorded_tests.cpp)
        add_test(NAME editor_trace_race_recorded_tests COMMAND editor_trace_race_recorded_tests)
        set_tests_properties(editor_trace_race_recorded_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_race_attribute_order_tests tests/editor_race_attribute_order_tests.cpp)
        add_test(NAME editor_race_attribute_order_tests COMMAND editor_race_attribute_order_tests)
        set_tests_properties(editor_race_attribute_order_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_trace_warning_recorded_tests tests/editor_trace_warning_recorded_tests.cpp)
        add_test(NAME editor_trace_warning_recorded_tests COMMAND editor_trace_warning_recorded_tests)
        set_tests_properties(editor_trace_warning_recorded_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_trace_filesync_recorded_tests tests/editor_trace_filesync_recorded_tests.cpp)
        add_test(NAME editor_trace_filesync_recorded_tests COMMAND editor_trace_filesync_recorded_tests)
        set_tests_properties(editor_trace_filesync_recorded_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_trace_props_recorded_tests tests/editor_trace_props_recorded_tests.cpp)
        add_test(NAME editor_trace_props_recorded_tests COMMAND editor_trace_props_recorded_tests)
        set_tests_properties(editor_trace_props_recorded_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_trace_textholder_recorded_tests tests/editor_trace_textholder_recorded_tests.cpp)
        add_test(NAME editor_trace_textholder_recorded_tests COMMAND editor_trace_textholder_recorded_tests)
        set_tests_properties(editor_trace_textholder_recorded_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_trace_msgutils_recorded_tests tests/editor_trace_msgutils_recorded_tests.cpp)
        add_test(NAME editor_trace_msgutils_recorded_tests COMMAND editor_trace_msgutils_recorded_tests)
        set_tests_properties(editor_trace_msgutils_recorded_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_trace_small_model_helpers_tests tests/editor_trace_small_model_helpers_tests.cpp)
        add_test(NAME editor_trace_small_model_helpers_tests COMMAND editor_trace_small_model_helpers_tests)
        set_tests_properties(editor_trace_small_model_helpers_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_trace_adapter_callbacks_recorded_tests tests/editor_trace_adapter_callbacks_recorded_tests.cpp)
        add_test(NAME editor_trace_adapter_callbacks_recorded_tests COMMAND editor_trace_adapter_callbacks_recorded_tests)
        set_tests_properties(editor_trace_adapter_callbacks_recorded_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_warning_target_priority_tests tests/editor_warning_target_priority_tests.cpp)
        add_test(NAME editor_warning_target_priority_tests COMMAND editor_warning_target_priority_tests)
        set_tests_properties(editor_warning_target_priority_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_trace_card_image_callbacks_tests tests/editor_trace_card_image_callbacks_tests.cpp)
        add_test(NAME editor_trace_card_image_callbacks_tests COMMAND editor_trace_card_image_callbacks_tests)
        set_tests_properties(editor_trace_card_image_callbacks_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_trace_flag_event_tests tests/editor_trace_flag_event_tests.cpp)
        add_test(NAME editor_trace_flag_event_tests COMMAND editor_trace_flag_event_tests)
        set_tests_properties(editor_trace_flag_event_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_trace_content_early_tests tests/editor_trace_content_early_tests.cpp)
        add_test(NAME editor_trace_content_early_tests COMMAND editor_trace_content_early_tests)
        set_tests_properties(editor_trace_content_early_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_trace_background_area_tests tests/editor_trace_background_area_tests.cpp)
        add_test(NAME editor_trace_background_area_tests COMMAND editor_trace_background_area_tests)
        set_tests_properties(editor_trace_background_area_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_trace_content_middle_tests tests/editor_trace_content_middle_tests.cpp)
        add_test(NAME editor_trace_content_middle_tests COMMAND editor_trace_content_middle_tests)
        set_tests_properties(editor_trace_content_middle_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_trace_card_families_tests tests/editor_trace_card_families_tests.cpp)
        add_test(NAME editor_trace_card_families_tests COMMAND editor_trace_card_families_tests)
        set_tests_properties(editor_trace_card_families_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_trace_menu_enemy_tests tests/editor_trace_menu_enemy_tests.cpp)
        add_test(NAME editor_trace_menu_enemy_tests COMMAND editor_trace_menu_enemy_tests)
        set_tests_properties(editor_trace_menu_enemy_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_trace_content_remaining_tests tests/editor_trace_content_remaining_tests.cpp)
        add_test(NAME editor_trace_content_remaining_tests COMMAND editor_trace_content_remaining_tests)
        set_tests_properties(editor_trace_content_remaining_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_trace_motion_flag_callbacks_tests tests/editor_trace_motion_flag_callbacks_tests.cpp)
        add_test(NAME editor_trace_motion_flag_callbacks_tests COMMAND editor_trace_motion_flag_callbacks_tests)
        set_tests_properties(editor_trace_motion_flag_callbacks_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_coupon_warning_collection_tests tests/editor_coupon_warning_collection_tests.cpp)
        add_test(NAME editor_coupon_warning_collection_tests COMMAND editor_coupon_warning_collection_tests)
        set_tests_properties(editor_coupon_warning_collection_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_trace_cast_status_callbacks_tests tests/editor_trace_cast_status_callbacks_tests.cpp)
        add_test(NAME editor_trace_cast_status_callbacks_tests COMMAND editor_trace_cast_status_callbacks_tests)
        set_tests_properties(editor_trace_cast_status_callbacks_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_trace_coupon_callbacks_tests tests/editor_trace_coupon_callbacks_tests.cpp)
        add_test(NAME editor_trace_coupon_callbacks_tests COMMAND editor_trace_coupon_callbacks_tests)
        set_tests_properties(editor_trace_coupon_callbacks_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_trace_content_gate_callbacks_tests tests/editor_trace_content_gate_callbacks_tests.cpp)
        add_test(NAME editor_trace_content_gate_callbacks_tests COMMAND editor_trace_content_gate_callbacks_tests)
        set_tests_properties(editor_trace_content_gate_callbacks_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_trace_menu_expansion_callbacks_tests tests/editor_trace_menu_expansion_callbacks_tests.cpp)
        add_test(NAME editor_trace_menu_expansion_callbacks_tests COMMAND editor_trace_menu_expansion_callbacks_tests)
        set_tests_properties(editor_trace_menu_expansion_callbacks_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_trace_typed_outer_gate_tests tests/editor_trace_typed_outer_gate_tests.cpp)
        add_test(NAME editor_trace_typed_outer_gate_tests COMMAND editor_trace_typed_outer_gate_tests)
        set_tests_properties(editor_trace_typed_outer_gate_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_trace_menu_outer_gate_tests tests/editor_trace_menu_outer_gate_tests.cpp)
        add_test(NAME editor_trace_menu_outer_gate_tests COMMAND editor_trace_menu_outer_gate_tests)
        set_tests_properties(editor_trace_menu_outer_gate_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_trace_event_outer_gate_tests tests/editor_trace_event_outer_gate_tests.cpp)
        add_test(NAME editor_trace_event_outer_gate_tests COMMAND editor_trace_event_outer_gate_tests)
        set_tests_properties(editor_trace_event_outer_gate_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_trace_content_outer_callbacks_tests tests/editor_trace_content_outer_callbacks_tests.cpp)
        add_test(NAME editor_trace_content_outer_callbacks_tests COMMAND editor_trace_content_outer_callbacks_tests)
        set_tests_properties(editor_trace_content_outer_callbacks_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_effect_card_warning_tests tests/editor_effect_card_warning_tests.cpp)
        add_test(NAME editor_effect_card_warning_tests COMMAND editor_effect_card_warning_tests)
        set_tests_properties(editor_effect_card_warning_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_trace_event_owner_scope_tests tests/editor_trace_event_owner_scope_tests.cpp)
        add_test(NAME editor_trace_event_owner_scope_tests COMMAND editor_trace_event_owner_scope_tests)
        set_tests_properties(editor_trace_event_owner_scope_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_trace_content_optional_gate_tests tests/editor_trace_content_optional_gate_tests.cpp)
        add_test(NAME editor_trace_content_optional_gate_tests COMMAND editor_trace_content_optional_gate_tests)
        set_tests_properties(editor_trace_content_optional_gate_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_trace_cast_selection_tests tests/editor_trace_cast_selection_tests.cpp)
        add_test(NAME editor_trace_cast_selection_tests COMMAND editor_trace_cast_selection_tests)
        set_tests_properties(editor_trace_cast_selection_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_trace_content_message_branches_tests tests/editor_trace_content_message_branches_tests.cpp)
        add_test(NAME editor_trace_content_message_branches_tests COMMAND editor_trace_content_message_branches_tests)
        set_tests_properties(editor_trace_content_message_branches_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_trace_content_compatibility_branches_tests tests/editor_trace_content_compatibility_branches_tests.cpp)
        add_test(NAME editor_trace_content_compatibility_branches_tests COMMAND editor_trace_content_compatibility_branches_tests)
        set_tests_properties(editor_trace_content_compatibility_branches_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_trace_step_traversal_tests tests/editor_trace_step_traversal_tests.cpp)
        add_test(NAME editor_trace_step_traversal_tests COMMAND editor_trace_step_traversal_tests)
        set_tests_properties(editor_trace_step_traversal_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_step_warning_order_tests tests/editor_step_warning_order_tests.cpp)
        add_test(NAME editor_step_warning_order_tests COMMAND editor_step_warning_order_tests --d-oracle)
        set_tests_properties(editor_step_warning_order_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_trace_text_helper_early_tests tests/editor_trace_text_helper_early_tests.cpp)
        add_test(NAME editor_trace_text_helper_early_tests COMMAND editor_trace_text_helper_early_tests)
        set_tests_properties(editor_trace_text_helper_early_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_trace_text_helper_content_tests tests/editor_trace_text_helper_content_tests.cpp)
        add_test(NAME editor_trace_text_helper_content_tests COMMAND editor_trace_text_helper_content_tests)
        set_tests_properties(editor_trace_text_helper_content_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_enemy_text_warning_contract_tests tests/editor_enemy_text_warning_contract_tests.cpp)
        add_test(NAME editor_enemy_text_warning_contract_tests COMMAND editor_enemy_text_warning_contract_tests)
        set_tests_properties(editor_enemy_text_warning_contract_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_content_producer_contract_tests tests/editor_content_producer_contract_tests.cpp)
        add_test(NAME editor_content_producer_contract_tests COMMAND editor_content_producer_contract_tests --d-oracle)
        set_tests_properties(editor_content_producer_contract_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_content_producer_direct_api_tests tests/editor_content_producer_direct_api_tests.cpp)
        add_test(NAME editor_content_producer_direct_api_tests COMMAND editor_content_producer_direct_api_tests --d-oracle)
        set_tests_properties(editor_content_producer_direct_api_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_content_producer_lifecycle_tests tests/editor_content_producer_lifecycle_tests.cpp)
        add_test(NAME editor_content_producer_lifecycle_tests COMMAND editor_content_producer_lifecycle_tests)
        set_tests_properties(editor_content_producer_lifecycle_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        if(MSVC)
            foreach(content_producer_test IN ITEMS
                    editor_content_producer_contract_tests
                    editor_content_producer_direct_api_tests
                    editor_content_producer_lifecycle_tests)
                target_compile_options(${content_producer_test} PRIVATE /utf-8)
            endforeach()
        endif()
        add_executable(editor_content_name_binding_history_tests tests/editor_content_name_binding_history_tests.cpp)
        add_test(NAME editor_content_name_binding_history_tests COMMAND editor_content_name_binding_history_tests --d-oracle)
        set_tests_properties(editor_content_name_binding_history_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_trace_content_producer_getters_tests tests/editor_trace_content_producer_getters_tests.cpp)
        add_test(NAME editor_trace_content_producer_getters_tests COMMAND editor_trace_content_producer_getters_tests)
        set_tests_properties(editor_trace_content_producer_getters_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_executable(editor_trace_step_value_pair_tests tests/editor_trace_step_value_pair_tests.cpp)
        add_test(NAME editor_trace_step_value_pair_tests COMMAND editor_trace_step_value_pair_tests)
        set_tests_properties(editor_trace_step_value_pair_tests PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        add_custom_target(editor_repair_regressions DEPENDS
            editor_trace_step_value_pair_tests
            editor_content_producer_contract_tests
            editor_content_producer_direct_api_tests
            editor_content_producer_lifecycle_tests
            editor_content_name_binding_history_tests
            editor_trace_content_producer_getters_tests
            editor_trace_text_helper_early_tests
            editor_trace_text_helper_content_tests
            editor_enemy_text_warning_contract_tests
            editor_trace_content_compatibility_branches_tests
            editor_trace_step_traversal_tests
            editor_step_warning_order_tests
            editor_trace_content_message_branches_tests
            editor_trace_cast_selection_tests
            editor_trace_event_owner_scope_tests
            editor_trace_content_optional_gate_tests
            editor_trace_event_outer_gate_tests
            editor_trace_content_outer_callbacks_tests
            editor_effect_card_warning_tests
            editor_trace_typed_outer_gate_tests
            editor_trace_menu_outer_gate_tests
            editor_trace_content_gate_callbacks_tests
            editor_trace_menu_expansion_callbacks_tests
            editor_trace_cast_status_callbacks_tests
            editor_trace_coupon_callbacks_tests
            editor_trace_card_families_tests
            editor_trace_menu_enemy_tests
            editor_trace_content_remaining_tests
            editor_trace_motion_flag_callbacks_tests
            editor_coupon_warning_collection_tests
            editor_trace_background_area_tests
            editor_trace_content_middle_tests
            editor_trace_adapter_callbacks_recorded_tests
            editor_warning_target_priority_tests
            editor_trace_card_image_callbacks_tests
            editor_trace_flag_event_tests
            editor_trace_content_early_tests
            editor_trace_msgutils_recorded_tests
            editor_trace_small_model_helpers_tests
            editor_trace_warning_recorded_tests
            editor_trace_filesync_recorded_tests
            editor_trace_props_recorded_tests
            editor_trace_textholder_recorded_tests
            editor_trace_path_recorded_tests
            editor_trace_menu_recorded_tests
            editor_trace_race_recorded_tests
            editor_race_attribute_order_tests
            editor_trace_coupon_recorded_tests
            editor_trace_motion_recorded_tests
            editor_repair_coupon_absent_index_tests
            editor_repair_motion_callback_ownership_tests
            editor_flag_text_behavior_tests
            editor_trace_chooser_recorded_tests
            editor_trace_dskin_recorded_tests
            editor_trace_flag_recorded_tests
            editor_incsearch_unicode_gate_tests
            editor_trace_history_recorded_tests
            editor_trace_dprops_recorded_tests
            editor_trace_slot_reuse_tests
            editor_trace_autokeycode_xml_entries_tests
            editor_trace_textdialog_caret_recovered_tests
            editor_trace_abilityview_recovered_tests
            editor_trace_roundview_entries_tests
            editor_trace_contentinitializer_recovered_tests
            editor_trace_radarspinner_entries_tests
            editor_trace_undo_entries_tests
            editor_trace_textdialog2_recovered_tests
            editor_trace_namewindow6_recovered_tests
            editor_text_xml_api_recovered_tests
            editor_final_state_contract_tests
            editor_load_language_contract_tests
            editor_live_preview_recovered_tests
            editor_helper_boundaries_recovered_tests
            editor_trace_entries_recovered_tests
            editor_scene_hidden_selection_recovered_tests
            editor_picker_dialog_contract_tests
            editor_name_checkbox_recovered_tests
            editor_expression_description_recovered_tests
            editor_template_ignition_route_tests
            editor_audio_retirement_recovered_tests
            editor_scene_commands_recovered_tests
            editor_image_api_residual_tests
            editor_supplemental_api_recovered_tests
            editor_model_observer_recovered_tests
            editor_customtable_recovered_tests
            editor_platform_layout_recovery_tests
            editor_navigation_drag_recovered_tests
            editor_clipboard_list_recovery_tests
            editor_template_rebase_recovered_tests
            editor_shared_api_completion_tests
            editor_shared_render_api_tests
            editor_shared_api_recovered_tests
            editor_chooser_api_residual_tests
            editor_model_contracts_recovered_tests
            editor_event_display_settings_recovery_tests
            editor_message_template_recovered_tests
            editor_tools_timebar_recovery_tests
            editor_motion_coupon_command_tests
            editor_recovered_event_boundaries_tests
            editor_scene_selection_recovered_tests
            editor_scene_route_residual_tests
            editor_initial_storage_contract_tests
            editor_flag_default_projection_tests
            editor_search_remaining_backend_tests
            editor_search_public_helper_contract_tests
            editor_script_partial_api_contract_tests
            editor_supplemental_value_contract_tests
            editor_history_autoapply_tests
            editor_state_notification_contract_tests
            editor_initial_event_contract_tests
            editor_remaining_value_contract_tests
            editor_shortcut_binding_contract_tests
            editor_inline_transaction_tests
            editor_expression_boundary_contract_tests
            editor_template_refresh_contract_tests
            editor_initial_model_contract_tests
            editor_model_owner_dirty_tests
            editor_flag_history_contract_tests
            editor_search_replacement_contract_tests
            editor_reference_navigation_contract_tests
            editor_scenario_creation_contract_tests
            editor_settings_skin_lifecycle_tests
            editor_scene_payload_contract_tests
            editor_party_launch_history_tests
            editor_clipboard_format_contract_tests
            editor_handcard_transfer_contract_tests
            editor_image_transfer_contract_tests
            editor_event_legacy_paste_tests
            editor_shared_use_history_tests
            editor_event_tree_command_tests
            editor_coupon_editing_contract_tests

            editor_file_sync_backup_tests
            editor_variable_state_contract_tests
            editor_expression_script_contract_tests

            editor_dialog_retirement_ownership_tests
            editor_shortcut_shell_ownership_tests
            editor_text_font_contract_tests

            editor_saveas_close_transaction_tests
            cwxeditor_recovered_xml_values_tests
            editor_expression_for_repair_tests
            editor_save_generation_repair_tests
            editor_catalog_encoding_repair_tests
            editor_unlink_history_repair_tests
            editor_indexed_handcard_repair_tests
            editor_script_argument_repair_tests
            editor_event_command_repair_tests
            editor_custom_label_repair_tests
            editor_cast_value_repair_tests
            cwxeditor_recovered_talk_dialog_apply_tests
            editor_card_apply_repair_tests
            cwxeditor_archive_repair_tests
            editor_coupon_conversion_repair_tests
            editor_initial_card_command_tests
            editor_cardpane_life_mark_geometry_tests
            editor_cast_aptitude_source_scale_tests
            editor_settings_message_contract_tests
            editor_scene_contract_tests
            editor_party_directory_command_tests
            editor_variable_state_command_tests
            editor_name_warning_command_tests
            editor_ignore_directory_contract_tests
            editor_card_creation_pending_tests
            editor_stateful_callback_contract_tests
            editor_resource_history_repair_tests
            editor_resource_clone_contract_tests
            editor_reference_binding_repair_tests
        )
        set_tests_properties(
            editor_initial_storage_contract_tests
            editor_flag_default_projection_tests
            editor_search_remaining_backend_tests
            editor_search_public_helper_contract_tests
            editor_script_partial_api_contract_tests
            editor_supplemental_value_contract_tests
            editor_history_autoapply_tests
            editor_state_notification_contract_tests
            editor_initial_event_contract_tests
            editor_remaining_value_contract_tests
            editor_shortcut_binding_contract_tests
            editor_inline_transaction_tests
            editor_expression_boundary_contract_tests
            editor_template_refresh_contract_tests
            editor_initial_model_contract_tests
            editor_model_owner_dirty_tests
            editor_flag_history_contract_tests
            editor_search_replacement_contract_tests
            editor_reference_navigation_contract_tests
            editor_scenario_creation_contract_tests
            editor_settings_skin_lifecycle_tests
            editor_scene_payload_contract_tests
            editor_party_launch_history_tests
            editor_clipboard_format_contract_tests
            editor_handcard_transfer_contract_tests
            editor_image_transfer_contract_tests
            editor_event_legacy_paste_tests
            editor_shared_use_history_tests
            editor_event_tree_command_tests
            editor_coupon_editing_contract_tests
            editor_file_sync_backup_tests
            editor_variable_state_contract_tests
            editor_expression_script_contract_tests
            editor_dialog_retirement_ownership_tests
            editor_shortcut_shell_ownership_tests
            editor_text_font_contract_tests
            editor_saveas_close_transaction_tests
            cwxeditor_recovered_xml_values_tests
            editor_expression_for_repair_tests
            editor_save_generation_repair_tests
            editor_catalog_encoding_repair_tests
            editor_unlink_history_repair_tests
            editor_indexed_handcard_repair_tests
            editor_script_argument_repair_tests
            editor_event_command_repair_tests
            editor_custom_label_repair_tests
            editor_cast_value_repair_tests
            cwxeditor_recovered_talk_dialog_apply_tests
            editor_card_apply_repair_tests
            cwxeditor_archive_repair_tests
            editor_coupon_conversion_repair_tests
            editor_initial_card_command_tests
            editor_cardpane_life_mark_geometry_tests
            editor_cast_aptitude_source_scale_tests
            editor_settings_message_contract_tests
            editor_scene_contract_tests
            editor_party_directory_command_tests
            editor_variable_state_command_tests
            editor_name_warning_command_tests
            editor_ignore_directory_contract_tests
            editor_card_creation_pending_tests
            editor_stateful_callback_contract_tests
            editor_resource_history_repair_tests
            editor_resource_clone_contract_tests
            editor_reference_binding_repair_tests
            PROPERTIES TIMEOUT 120 LABELS "editor;repair")
        foreach(cwxeditor_gui_test IN ITEMS
                editor_trace_step_value_pair_tests
                editor_content_producer_contract_tests
                editor_content_producer_direct_api_tests
                editor_content_producer_lifecycle_tests
                editor_content_name_binding_history_tests
                editor_trace_content_producer_getters_tests
                editor_trace_text_helper_early_tests
                editor_trace_text_helper_content_tests
                editor_enemy_text_warning_contract_tests
                editor_trace_content_compatibility_branches_tests
                editor_trace_step_traversal_tests
                editor_step_warning_order_tests
                editor_trace_content_message_branches_tests
                editor_trace_cast_selection_tests
                editor_trace_event_owner_scope_tests
                editor_trace_content_optional_gate_tests
                editor_trace_event_outer_gate_tests
                editor_trace_content_outer_callbacks_tests
                editor_effect_card_warning_tests
                editor_trace_typed_outer_gate_tests
                editor_trace_menu_outer_gate_tests
                editor_trace_content_gate_callbacks_tests
                editor_trace_menu_expansion_callbacks_tests
                editor_trace_cast_status_callbacks_tests
                editor_trace_coupon_callbacks_tests
                editor_trace_card_families_tests
                editor_trace_menu_enemy_tests
                editor_trace_content_remaining_tests
                editor_trace_motion_flag_callbacks_tests
                editor_coupon_warning_collection_tests
                editor_trace_background_area_tests
                editor_trace_content_middle_tests
                editor_trace_adapter_callbacks_recorded_tests
                editor_warning_target_priority_tests
                editor_trace_card_image_callbacks_tests
                editor_trace_flag_event_tests
                editor_trace_content_early_tests
                editor_trace_msgutils_recorded_tests
                editor_trace_small_model_helpers_tests
                editor_trace_warning_recorded_tests
                editor_trace_filesync_recorded_tests
                editor_trace_props_recorded_tests
                editor_trace_textholder_recorded_tests
                editor_trace_path_recorded_tests
                editor_trace_menu_recorded_tests
                editor_trace_race_recorded_tests
                editor_race_attribute_order_tests
                editor_trace_coupon_recorded_tests
                editor_trace_motion_recorded_tests
                editor_repair_coupon_absent_index_tests
                editor_repair_motion_callback_ownership_tests
                editor_flag_text_behavior_tests
                editor_trace_chooser_recorded_tests
                editor_trace_dskin_recorded_tests
                editor_trace_flag_recorded_tests
                editor_incsearch_unicode_gate_tests
                editor_trace_history_recorded_tests
                editor_trace_dprops_recorded_tests
                editor_trace_autokeycode_xml_entries_tests
                editor_trace_textdialog_caret_recovered_tests
                editor_trace_abilityview_recovered_tests
                editor_trace_roundview_entries_tests
                editor_trace_contentinitializer_recovered_tests
                editor_trace_radarspinner_entries_tests
                editor_trace_undo_entries_tests
                editor_trace_textdialog2_recovered_tests
                editor_trace_namewindow6_recovered_tests
                editor_text_xml_api_recovered_tests
                editor_final_state_contract_tests
                editor_load_language_contract_tests
                editor_live_preview_recovered_tests
                editor_helper_boundaries_recovered_tests
                editor_trace_entries_recovered_tests
                editor_scene_hidden_selection_recovered_tests
                editor_picker_dialog_contract_tests
                editor_name_checkbox_recovered_tests
                editor_expression_description_recovered_tests
                editor_template_ignition_route_tests
                editor_audio_retirement_recovered_tests
                editor_scene_commands_recovered_tests
                editor_image_api_residual_tests
                editor_supplemental_api_recovered_tests
                editor_model_observer_recovered_tests
                editor_customtable_recovered_tests
                editor_platform_layout_recovery_tests
                editor_navigation_drag_recovered_tests
                editor_clipboard_list_recovery_tests
                editor_template_rebase_recovered_tests
                editor_shared_api_completion_tests
                editor_shared_render_api_tests
                editor_shared_api_recovered_tests
                editor_chooser_api_residual_tests
                editor_model_contracts_recovered_tests
                editor_event_display_settings_recovery_tests
                editor_message_template_recovered_tests
                editor_tools_timebar_recovery_tests
                editor_motion_coupon_command_tests
                editor_recovered_event_boundaries_tests
                editor_scene_selection_recovered_tests
                editor_scene_route_residual_tests
                editor_initial_storage_contract_tests
                editor_flag_default_projection_tests
                editor_search_remaining_backend_tests
                editor_search_public_helper_contract_tests
                editor_script_partial_api_contract_tests
                editor_supplemental_value_contract_tests
                editor_history_autoapply_tests
                editor_state_notification_contract_tests
                editor_initial_event_contract_tests
                editor_remaining_value_contract_tests
                editor_shortcut_binding_contract_tests
                editor_inline_transaction_tests
                editor_expression_boundary_contract_tests
                editor_template_refresh_contract_tests
                editor_initial_model_contract_tests
                editor_model_owner_dirty_tests
                editor_flag_history_contract_tests
                editor_search_replacement_contract_tests
                editor_reference_navigation_contract_tests
                editor_scenario_creation_contract_tests
                editor_settings_skin_lifecycle_tests
                editor_scene_payload_contract_tests
                editor_party_launch_history_tests
                editor_clipboard_format_contract_tests
                editor_handcard_transfer_contract_tests
                editor_image_transfer_contract_tests
                editor_event_legacy_paste_tests
                editor_shared_use_history_tests
                editor_event_tree_command_tests
                editor_coupon_editing_contract_tests
                editor_file_sync_backup_tests
                editor_variable_state_contract_tests
                editor_expression_script_contract_tests
                editor_dialog_retirement_ownership_tests
                editor_shortcut_shell_ownership_tests
                editor_text_font_contract_tests
                editor_saveas_close_transaction_tests
                editor_resource_history_repair_tests
                editor_resource_clone_contract_tests
                editor_reference_binding_repair_tests
                editor_stateful_callback_contract_tests
                editor_card_creation_pending_tests
                editor_ignore_directory_contract_tests
                editor_name_warning_command_tests
                editor_variable_state_command_tests
                editor_party_directory_command_tests
                editor_scene_contract_tests
                editor_settings_message_contract_tests
                editor_initial_card_command_tests
                editor_cardpane_life_mark_geometry_tests
                editor_cast_aptitude_source_scale_tests
                editor_coupon_conversion_repair_tests
                cwxeditor_archive_repair_tests
                editor_unlink_history_repair_tests
                editor_indexed_handcard_repair_tests
                editor_script_argument_repair_tests
                editor_event_command_repair_tests
                editor_custom_label_repair_tests
                editor_cast_value_repair_tests
                cwxeditor_recovered_talk_dialog_apply_tests
                editor_card_apply_repair_tests
                cwxeditor_recovered_xml_values_tests
                editor_expression_for_repair_tests
                editor_save_generation_repair_tests
                editor_catalog_encoding_repair_tests
                cwxeditor_ui_contract_dump
                cwxeditor_ui_contract_tests
                cwxeditor_workflow_tests)
            target_compile_features(${cwxeditor_gui_test} PRIVATE cxx_std_20)
            if(cwxeditor_gui_test STREQUAL "editor_effect_card_warning_tests" OR
               cwxeditor_gui_test STREQUAL "editor_trace_event_owner_scope_tests")
                # GUIフィクスチャを使わず、コアモデルの回帰を検証する。
                target_link_libraries(${cwxeditor_gui_test} PRIVATE
                    cwxeditor_core cwxeditor_lib ${CWXEDITOR_TEST_RUNTIME_TARGET})
                cardwirth_enable_exception_origin(${cwxeditor_gui_test})
            else()
                target_link_libraries(${cwxeditor_gui_test} PRIVATE cwxeditor_test_support)
            endif()
            if(MSVC)
                target_compile_options(${cwxeditor_gui_test} PRIVATE
                    /W4 /EHsc /utf-8 /bigobj)
            endif()
            if(WIN32)
                target_link_libraries(${cwxeditor_gui_test} PRIVATE
                    User32 Gdi32 Shell32 Ole32 Comctl32 Comdlg32 Winmm Shlwapi Ws2_32
                    Dbghelp Setupapi WindowsCodecs Uuid Normaliz)
            endif()
        endforeach()

        add_test(NAME cwxeditor_ui_contract_sanity
                 COMMAND ${cwxeditor_ui_contract_tests_TEST_COMMAND})
        add_test(NAME cwxeditor_ui_contract_dump_smoke
                 COMMAND cwxeditor_ui_contract_dump --output
                    "${CMAKE_CURRENT_BINARY_DIR}/test-results/cwxeditor/ui-contract-actual.json")
        add_test(NAME cwxeditor_workflows
                 COMMAND cwxeditor_workflow_tests)
        set_tests_properties(cwxeditor_ui_contract_sanity PROPERTIES
            LABELS "editor;ui;contract" TIMEOUT 30)
        set_tests_properties(cwxeditor_ui_contract_dump_smoke PROPERTIES
            LABELS "editor;ui;contract" TIMEOUT 30)
        set_tests_properties(cwxeditor_workflows PROPERTIES
            LABELS "editor;workflow" TIMEOUT 30)

    endif()
endif()

if(CARDWIRTH_EDITOR_PORT_ENABLED)
    target_compile_definitions(${CARDWIRTH_TARGET} PRIVATE CARDWIRTH_EMBED_EDITOR=1)
endif()

if(CARDWIRTH_APP_VARIANT STREQUAL "Engine")
    set(CARDWIRTH_OUTPUT_NAME "CardWirthPy_C")
    set(CARDWIRTH_OUTPUT_NAME_DEBUG "CardWirthPy_C_debug")
    set(CARDWIRTH_DISPLAY_NAME "CardWirthPy_C")
elseif(CARDWIRTH_APP_VARIANT STREQUAL "OneWindow")
    set(CARDWIRTH_OUTPUT_NAME "CardWirthPy_C_1Window")
    set(CARDWIRTH_OUTPUT_NAME_DEBUG "CardWirthPy_C_1Window_debug")
    set(CARDWIRTH_DISPLAY_NAME "CardWirthPy_C")
elseif(CARDWIRTH_APP_VARIANT STREQUAL "Editor")
    set(CARDWIRTH_OUTPUT_NAME "XEditor_C")
    set(CARDWIRTH_OUTPUT_NAME_DEBUG "XEditor_C_debug")
    set(CARDWIRTH_DISPLAY_NAME "XEditor_C")
elseif(CARDWIRTH_APP_VARIANT STREQUAL "Explorer")
    set(CARDWIRTH_OUTPUT_NAME "DataExplorer_C")
    set(CARDWIRTH_OUTPUT_NAME_DEBUG "DataExplorer_C_debug")
    set(CARDWIRTH_DISPLAY_NAME "DataExplorer_C")
else()
    set(CARDWIRTH_OUTPUT_NAME "CardWirthPy_C_All")
    set(CARDWIRTH_OUTPUT_NAME_DEBUG "CardWirthPy_C_All_debug")
    set(CARDWIRTH_DISPLAY_NAME "CardWirthPy_C")
endif()
set_target_properties(${CARDWIRTH_TARGET} PROPERTIES
    OUTPUT_NAME "${CARDWIRTH_OUTPUT_NAME}"
    OUTPUT_NAME_DEBUG "${CARDWIRTH_OUTPUT_NAME_DEBUG}"
)

# 共有パッケージの配置。デスクトップUnixでは実行ファイル横のlib/からライブラリを解
# 決し、署名済みAppleバンドルでは標準のFrameworksディレクトリを使う。Android
# のGradleパッケージはlib/<ABI>にネイティブライブラリを格納する。
if(APPLE)
    if(IOS)
        set_target_properties(${CARDWIRTH_TARGET} PROPERTIES
            BUILD_RPATH "@executable_path/Frameworks"
            INSTALL_RPATH "@executable_path/Frameworks")
    else()
        set_target_properties(${CARDWIRTH_TARGET} PROPERTIES
            BUILD_RPATH "@executable_path/../Frameworks"
            INSTALL_RPATH "@executable_path/../Frameworks")
    endif()
elseif(UNIX AND NOT ANDROID AND NOT EMSCRIPTEN)
    set_target_properties(${CARDWIRTH_TARGET} PROPERTIES
        BUILD_RPATH "$ORIGIN/lib"
        INSTALL_RPATH "$ORIGIN/lib")
endif()
if(CARDWIRTH_APP_VARIANT STREQUAL "Editor")
    target_compile_definitions(${CARDWIRTH_TARGET} PRIVATE CARDWIRTH_EDITOR_ONLY=1)
    if(MSVC)
        target_compile_options(${CARDWIRTH_TARGET} PRIVATE /EHsc /utf-8)
    endif()
elseif(CARDWIRTH_APP_VARIANT STREQUAL "Explorer")
    target_compile_definitions(${CARDWIRTH_TARGET} PRIVATE CARDWIRTH_EXPLORER_ONLY=1)
elseif(CARDWIRTH_APP_VARIANT STREQUAL "Engine")
    if(CARDWIRTH_STANDALONE_ENGINE_HOST)
        target_compile_definitions(${CARDWIRTH_TARGET} PRIVATE
            CARDWIRTH_STANDALONE_ENGINE=1)
    else()
        # 移植版EngineはSDLのホスト通信を使い、単体Windows版
        # およびALLと同じ共通Presentationモデルを維持する。
        target_compile_definitions(${CARDWIRTH_TARGET} PRIVATE
            CARDWIRTH_ENGINE_ONLY=1
            CARDWIRTH_EMBED_ENGINE=1
            CARDWIRTH_ALL_HOST=1)
    endif()
elseif(CARDWIRTH_APP_VARIANT STREQUAL "OneWindow")
    # ALLの単一ウィンドウ構成をEngine専用で使用する。CARDWIRTH_ENGINE_ONLY
    # では直ちにEngineを起動し、ホーム、Data
    # Explorer、Editorへの切り替えを除く。
    target_compile_definitions(${CARDWIRTH_TARGET} PRIVATE
        CARDWIRTH_ENGINE_ONLY=1
        CARDWIRTH_EMBED_ENGINE=1
        CARDWIRTH_ALL_HOST=1)
elseif(CARDWIRTH_APP_VARIANT STREQUAL "All")
    target_compile_definitions(${CARDWIRTH_TARGET} PRIVATE
        CARDWIRTH_EMBED_ENGINE=1
        CARDWIRTH_ALL_HOST=1)
endif()

if(CARDWIRTH_APP_VARIANT STREQUAL "All" AND BUILD_TESTING AND NOT ANDROID AND NOT IOS)
    add_executable(all_layout_tests
        tests/all_layout_tests.cpp
        app/all/all_display_mode.cpp
        app/all/all_layout.cpp)
    target_compile_features(all_layout_tests PRIVATE cxx_std_20)
    target_include_directories(all_layout_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    if(MSVC)
        target_compile_options(all_layout_tests PRIVATE /W4 /EHsc /utf-8)
    else()
        target_compile_options(all_layout_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME all_layout COMMAND all_layout_tests)
endif()
foreach(cardwirth_target IN LISTS CARDWIRTH_TARGETS)
    if(CARDWIRTH_JPEG_SOURCE_DIR)
        target_link_libraries(${cardwirth_target} PRIVATE JPEG::JPEG)
    endif()
    if(CARDWIRTH_APP_VARIANT STREQUAL "Editor" OR
       CARDWIRTH_APP_VARIANT STREQUAL "All")
        target_link_libraries(${cardwirth_target} PRIVATE ${CMAKE_DL_LIBS})
    else()
        target_link_libraries(${cardwirth_target} PRIVATE
            SDL3::SDL3
            SDL3_image::SDL3_image
            SDL3_mixer::SDL3_mixer
            SDL3_ttf::SDL3_ttf
            Freetype::Freetype
            ${CMAKE_DL_LIBS}
        )
    endif()
endforeach()

# Win32アダプターは同じ実行ファイルに組み込む。MSVCの環境依存の既定ライ
# ブラリ一覧に頼らず、インポートライブラリを明示する。BASSはリンクしない。bassplayer
# が実行時にbass.dll、bassmidi.dll、bass_fx.dll
# を解決し、利用できなければSDL_mixerへ切り替える。
if(WIN32)
    foreach(cardwirth_target IN LISTS CARDWIRTH_TARGETS)
        # Win32 UIはすべてUTF-16を使用する。外部ライブ
        # ラリや共通コントロールの汎用宣言もWide側に揃える。独自
        # の文字列付きメッセージは明示的な...W定数で送信する。
        target_compile_definitions(${cardwirth_target} PRIVATE UNICODE _UNICODE)
        target_link_libraries(${cardwirth_target} PRIVATE
            User32 Gdi32 Shell32 Ole32 Comctl32 Comdlg32 Winmm Shlwapi Ws2_32 Dbghelp Setupapi
            WindowsCodecs Uuid Normaliz
        )
        if(CARDWIRTH_APP_VARIANT STREQUAL "All")
            target_link_libraries(${cardwirth_target} PRIVATE Winhttp)
        endif()
        # マニフェストをターゲットのソースに含めると、Visual StudioとNinja/MSVC
        # の双方でmt.exeへ正しく渡せる。/MANIFESTINPUTを生のリンカーオ
        # プションで渡すと、CMakeのNinja用マニフェスト処理の入力一覧が空になる。
        target_sources(${cardwirth_target} PRIVATE app/CardWirthC.manifest)
    endforeach()
endif()

foreach(cardwirth_target IN LISTS CARDWIRTH_TARGETS)
    target_compile_features(${cardwirth_target} PRIVATE cxx_std_20)
    target_include_directories(${cardwirth_target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
endforeach()
if(ANDROID)
    # NDKのlibc++ではC++20のstop_tokenとjthread
    # に実験的機能の有効化が必要となる。Androidの共通ファイル監視で
    # これらの標準型を使うため、ヘッダー内のAPIを明示的に有効化する。
    foreach(cardwirth_target IN LISTS CARDWIRTH_TARGETS)
        target_compile_definitions(${cardwirth_target} PRIVATE
            _LIBCPP_ENABLE_EXPERIMENTAL=1)
    endforeach()
    # SDLActivityの既定のJava連携はlibSDL3.so
    # 、libmain.soの順に読み込み、後者のSDL_main
    # を呼ぶ。Androidの各製品は別々にパッケージ化されるた
    # め、必要な実行時名を全ターゲットで共有しても衝突しない。
    foreach(cardwirth_target IN LISTS CARDWIRTH_TARGETS)
        set_target_properties(${cardwirth_target} PROPERTIES
            OUTPUT_NAME main
            OUTPUT_NAME_DEBUG main
            OUTPUT_NAME_RELEASE main
            OUTPUT_NAME_RELWITHDEBINFO main
            OUTPUT_NAME_MINSIZEREL main)
    endforeach()
endif()
# 最適化ビルドでも計測用の時間診断を利用可能にする。ただしデバッグ専
# 用入力サーバーとDebug CRTは無効のままにする。通常のローカ
# ルビルドで使うMinSizeRelでも有効にしないと、::cardwirth::debug::log
# の呼び出しがすべて除去される。Webのリリースビル
# ドではデスクトップ用の診断トレースを出力しない。
if(NOT EMSCRIPTEN)
    foreach(cardwirth_target IN LISTS CARDWIRTH_TARGETS)
        target_compile_definitions(${cardwirth_target} PRIVATE
            $<$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>,$<CONFIG:RelWithDebInfo>>:ENABLE_LOG_IN_RELEASE=1>
        )
    endforeach()
endif()

# Python sqlite3互換層はSQLite C APIが利用できる構成でのみ有効化する。
# ExplorerとEditorはEngine DBを使用しないため、検出もリンクも行わない。
if(CARDWIRTH_APP_VARIANT STREQUAL "Engine" OR
   CARDWIRTH_APP_VARIANT STREQUAL "OneWindow" OR
   CARDWIRTH_APP_VARIANT STREQUAL "All")
find_package(SQLite3 QUIET)
if(TARGET SQLite::SQLite3)
    foreach(cardwirth_target IN LISTS CARDWIRTH_TARGETS)
        target_link_libraries(${cardwirth_target} PRIVATE SQLite::SQLite3)
        target_compile_definitions(${cardwirth_target} PRIVATE CW_ENABLE_SQLITE_C_API=1)
    endforeach()
elseif(EMSCRIPTEN)
    # Webではpthreadを使うHosted Engine
    # 全体を動作させる。Emscriptenが保守するSQLite
    # 移植版を使い、Python互換のシナリオ・宿データベ
    # ースをWebAssembly内でも利用可能にする。
    foreach(cardwirth_target IN LISTS CARDWIRTH_TARGETS)
        target_compile_options(${cardwirth_target} PRIVATE -sUSE_SQLITE3=1)
        target_link_options(${cardwirth_target} PRIVATE -sUSE_SQLITE3=1)
        target_compile_definitions(${cardwirth_target} PRIVATE CW_ENABLE_SQLITE_C_API=1)
    endforeach()
    message(STATUS "SQLite backend: Emscripten sqlite3 port")
elseif(APPLE)
    # sqlite3は両Appleプラットフォームの公開SDKライブラリ。iOS
    # のクロスコンパイルではFindSQLite3がインポートターゲットを公
    # 開しない場合があるため、その場合はSDKライブラリを明示的に解決する。
    find_library(CARDWIRTH_APPLE_SQLITE3 sqlite3 REQUIRED)
    foreach(cardwirth_target IN LISTS CARDWIRTH_TARGETS)
        target_link_libraries(${cardwirth_target} PRIVATE "${CARDWIRTH_APPLE_SQLITE3}")
        target_compile_definitions(${cardwirth_target} PRIVATE CW_ENABLE_SQLITE_C_API=1)
    endforeach()
    message(STATUS "SQLite backend: Apple SDK sqlite3")
elseif(WIN32 AND MSVC)
    # Windows 10/11は対応するSQLite C ABIをwinsqlite3.dll
    # で提供する。SDKヘッダーは通常のum直下ではなくum/winsqlite
    # にあり、cw/lib/sqlite.cppが専用機能マクロで選択する。
    get_filename_component(CARDWIRTH_WINSDK_ROOT
        "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots;KitsRoot10]"
        ABSOLUTE)
    # CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION
    # はVisual Studioジェネレーターでのみ設定される。Releaseビル
    # ドではNinjaとMSVCも使うため、明示されたVS SDKを優先し、未指定
    # なら公開WinSQLiteヘッダーを含む最新のインストール済みSDKを使う。
    set(CARDWIRTH_WINSDK_INCLUDE_VERSIONS "")
    if(CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION)
        list(APPEND CARDWIRTH_WINSDK_INCLUDE_VERSIONS
            "${CARDWIRTH_WINSDK_ROOT}/Include/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}")
    endif()
    file(GLOB CARDWIRTH_WINSDK_INSTALLED_INCLUDE_VERSIONS LIST_DIRECTORIES TRUE
        "${CARDWIRTH_WINSDK_ROOT}/Include/*")
    list(SORT CARDWIRTH_WINSDK_INSTALLED_INCLUDE_VERSIONS
        COMPARE NATURAL ORDER DESCENDING)
    list(APPEND CARDWIRTH_WINSDK_INCLUDE_VERSIONS
        ${CARDWIRTH_WINSDK_INSTALLED_INCLUDE_VERSIONS})
    list(REMOVE_DUPLICATES CARDWIRTH_WINSDK_INCLUDE_VERSIONS)
    set(CARDWIRTH_WINSQLITE_INCLUDE "")
    foreach(CARDWIRTH_WINSDK_INCLUDE_VERSION IN LISTS CARDWIRTH_WINSDK_INCLUDE_VERSIONS)
        if(EXISTS "${CARDWIRTH_WINSDK_INCLUDE_VERSION}/um/winsqlite/winsqlite3.h")
            set(CARDWIRTH_WINSQLITE_INCLUDE
                "${CARDWIRTH_WINSDK_INCLUDE_VERSION}/um/winsqlite")
            break()
        endif()
    endforeach()
    if(EXISTS "${CARDWIRTH_WINSQLITE_INCLUDE}/winsqlite3.h")
        foreach(cardwirth_target IN LISTS CARDWIRTH_TARGETS)
            target_include_directories(${cardwirth_target} PRIVATE
                "${CARDWIRTH_WINSQLITE_INCLUDE}")
            target_link_libraries(${cardwirth_target} PRIVATE winsqlite3)
            target_compile_definitions(${cardwirth_target} PRIVATE CW_ENABLE_WINSQLITE3_API=1)
        endforeach()
        message(STATUS "SQLite backend: Windows SDK winsqlite3")
    else()
        message(FATAL_ERROR
            "SQLite3 was not found and Windows SDK winsqlite3.h is unavailable. "
            "Install the Windows SDK Desktop C++ components or provide SQLite3_ROOT.")
    endif()
elseif(ANDROID)
    # Android NDKは非公開のプラットフォーム用libsqlite3
    # ABIを公開しない。パブリックドメインの単一ソース版をAPKへ組み込
    # み、シナリオと宿のデータベースにデスクトップ・Webと同じC API実
    # 装を使う。再現可能なビルドのため、リリースとダイジェストを固定する。
    include(FetchContent)
    FetchContent_Declare(cardwirth_sqlite3_source
        URL "https://www.sqlite.org/2022/sqlite-amalgamation-3390000.zip"
        URL_HASH "SHA512=cbaf4adb3e404d9aa403b34f133c5beca5f641ae1e23f84dbb021da1fb9efdc7c56b5922eb533ae5cb6d26410ac60cb3f026085591bc83ebc1c225aed0cf37ca"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_GetProperties(cardwirth_sqlite3_source)
    if(NOT cardwirth_sqlite3_source_POPULATED)
        if(POLICY CMP0169)
            cmake_policy(PUSH)
            cmake_policy(SET CMP0169 OLD)
        endif()
        FetchContent_Populate(cardwirth_sqlite3_source)
        if(POLICY CMP0169)
            cmake_policy(POP)
        endif()
    endif()
    add_library(cardwirth_sqlite3 STATIC
        "${cardwirth_sqlite3_source_SOURCE_DIR}/sqlite3.c")
    target_include_directories(cardwirth_sqlite3 PUBLIC
        "${cardwirth_sqlite3_source_SOURCE_DIR}")
    target_compile_definitions(cardwirth_sqlite3 PRIVATE
        SQLITE_OMIT_LOAD_EXTENSION=1
        SQLITE_THREADSAFE=1
        SQLITE_ENABLE_FTS4=1
        SQLITE_ENABLE_FTS5=1
        SQLITE_ENABLE_RTREE=1)
    foreach(cardwirth_target IN LISTS CARDWIRTH_TARGETS)
        target_link_libraries(${cardwirth_target} PRIVATE cardwirth_sqlite3)
        target_compile_definitions(${cardwirth_target} PRIVATE CW_ENABLE_SQLITE_C_API=1)
    endforeach()
    message(STATUS "SQLite backend: bundled Android amalgamation")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # EngineとALLはシナリオ・宿データベースなしでは動作しない。ビルドが
    # 成功しても最初のデータベース操作が未対応となるLinux製品を作らない。
    message(FATAL_ERROR
        "Linux Engine/All requires SQLite3. Install the SQLite3 development package "
        "or provide SQLite3_ROOT; a database-free Engine build is not supported.")
else()
    if(NOT CARDWIRTH_SUPPRESS_WARNINGS)
        message(WARNING
            "SQLite3 was not found; scenario/yado database operations will report BackendUnavailable")
    endif()
endif()
endif()

if(MSVC)
    foreach(cardwirth_target IN LISTS CARDWIRTH_TARGETS)
        target_compile_options(${cardwirth_target} PRIVATE /EHsc /utf-8 /bigobj /MP)
        if(NOT CARDWIRTH_SUPPRESS_WARNINGS)
            target_compile_options(${cardwirth_target} PRIVATE /W4)
        endif()
    endforeach()
else()
    foreach(cardwirth_target IN LISTS CARDWIRTH_TARGETS)
        if(NOT CARDWIRTH_SUPPRESS_WARNINGS)
            target_compile_options(${cardwirth_target} PRIVATE -Wall -Wextra -Wpedantic)
        endif()
    endforeach()
endif()

if(APPLE)
    string(TOLOWER "${CARDWIRTH_APP_VARIANT}" CARDWIRTH_APPLE_VARIANT_ID)
    set(CARDWIRTH_IOS_BUNDLE_ID "" CACHE STRING
        "Bundle identifier for iOS")
    set(CARDWIRTH_IOS_DEVELOPMENT_TEAM "" CACHE STRING
        "Apple Development Team ID for iOS")
    if(IOS AND NOT CARDWIRTH_IOS_BUNDLE_ID STREQUAL "")
        set(CARDWIRTH_APPLE_BUNDLE_ID
            "${CARDWIRTH_IOS_BUNDLE_ID}")
    else()
        set(CARDWIRTH_APPLE_BUNDLE_ID
            "jp.cardwirth.cardwirthc.${CARDWIRTH_APPLE_VARIANT_ID}")
    endif()
    if(IOS)
        set(CARDWIRTH_APPLE_PLATFORM_PLIST
"    <key>LSRequiresIPhoneOS</key>
    <true/>
    <key>UILaunchScreen</key>
    <dict/>
    <key>UIRequiresFullScreen</key>
    <true/>
    <key>UIApplicationSupportsIndirectInputEvents</key>
    <true/>
    <key>UISupportedInterfaceOrientations</key>
    <array>
        <string>UIInterfaceOrientationLandscapeLeft</string>
        <string>UIInterfaceOrientationLandscapeRight</string>
    </array>
    <key>UISupportedInterfaceOrientations~ipad</key>
    <array>
        <string>UIInterfaceOrientationLandscapeLeft</string>
        <string>UIInterfaceOrientationLandscapeRight</string>
    </array>")
    else()
        set(CARDWIRTH_APPLE_PLATFORM_PLIST
"    <key>NSHighResolutionCapable</key>
    <true/>
    <key>NSSupportsAutomaticGraphicsSwitching</key>
    <true/>")
    endif()
    set(CARDWIRTH_APPLE_BUNDLE_NAME "${CARDWIRTH_DISPLAY_NAME}")
    set(CARDWIRTH_BUILD_NUMBER "1" CACHE STRING
        "Apple CFBundleVersion (must increase for each App Store/TestFlight upload)")
    if(NOT CARDWIRTH_BUILD_NUMBER MATCHES "^[1-9][0-9]*$")
        message(FATAL_ERROR
            "CARDWIRTH_BUILD_NUMBER must be a positive integer: ${CARDWIRTH_BUILD_NUMBER}")
    endif()
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
    set(CARDWIRTH_APPLE_INFO_PLIST
        "${CMAKE_CURRENT_BINARY_DIR}/generated/CardWirthC-Apple-Info.plist")
    configure_file(platform/apple/Info.plist.in "${CARDWIRTH_APPLE_INFO_PLIST}" @ONLY)

    set(CARDWIRTH_APPLE_PRIVACY_MANIFEST
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/apple/PrivacyInfo.xcprivacy")
    if(NOT EXISTS "${CARDWIRTH_APPLE_PRIVACY_MANIFEST}")
        message(FATAL_ERROR "Apple privacy manifest was not found: ${CARDWIRTH_APPLE_PRIVACY_MANIFEST}")
    endif()
    # 利用者のアイコンディレクトリからmacOS/iOS用の各サイズを生成
    # する。既存の登録済みカタログは仮素材のため、バンドルには含めない。
    set(CARDWIRTH_ICON_ROOT "${CARDWIRTH_ROOT}/icon")
    set(CARDWIRTH_ICON_SMALL "${CARDWIRTH_ICON_ROOT}/icon_32×32_pyC.png")
    set(CARDWIRTH_ICON_LARGE "${CARDWIRTH_ICON_ROOT}/icon_128×128_pyC.png")
    foreach(icon_source IN ITEMS "${CARDWIRTH_ICON_SMALL}" "${CARDWIRTH_ICON_LARGE}")
        if(NOT EXISTS "${icon_source}")
            message(FATAL_ERROR "Application icon is missing: ${icon_source}")
        endif()
    endforeach()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CARDWIRTH_ICON_SMALL}" "${CARDWIRTH_ICON_LARGE}")
    find_program(CARDWIRTH_SIPS NAMES sips REQUIRED)
    set(CARDWIRTH_APPLE_ASSET_CATALOG
        "${CMAKE_CURRENT_BINARY_DIR}/generated/Assets.xcassets")
    set(CARDWIRTH_APPLE_ICON_SET
        "${CARDWIRTH_APPLE_ASSET_CATALOG}/AppIcon.appiconset")
    file(MAKE_DIRECTORY "${CARDWIRTH_APPLE_ICON_SET}")
    configure_file(platform/apple/Assets.xcassets/AppIcon.appiconset/Contents.json
        "${CARDWIRTH_APPLE_ICON_SET}/Contents.json" COPYONLY)
    if(IOS)
        # マーケティング用を含むiOSアプリアイコンにはアルファを含めな
        # い。JPEGを経由して背景を不透明にしてからPNGを出力する。
        foreach(icon_size IN ITEMS 32 128)
            set(icon_source "${CARDWIRTH_ICON_SMALL}")
            if(icon_size EQUAL 128)
                set(icon_source "${CARDWIRTH_ICON_LARGE}")
            endif()
            execute_process(COMMAND "${CARDWIRTH_SIPS}" -s format jpeg
                "${icon_source}" --out
                "${CMAKE_CURRENT_BINARY_DIR}/generated/source-${icon_size}-opaque.jpg"
                RESULT_VARIABLE icon_result ERROR_VARIABLE icon_error
                OUTPUT_QUIET)
            if(NOT icon_result EQUAL 0)
                message(FATAL_ERROR "Could not make iOS icon opaque: ${icon_error}")
            endif()
        endforeach()
    endif()
    foreach(icon_size IN ITEMS 16 20 29 32 40 58 60 64 76 80 87 120 128
                               152 167 180 256 512 1024)
        if(icon_size LESS_EQUAL 32)
            set(icon_source "${CARDWIRTH_ICON_SMALL}")
            if(IOS)
                set(icon_source "${CMAKE_CURRENT_BINARY_DIR}/generated/source-32-opaque.jpg")
            endif()
        else()
            set(icon_source "${CARDWIRTH_ICON_LARGE}")
            if(IOS)
                set(icon_source "${CMAKE_CURRENT_BINARY_DIR}/generated/source-128-opaque.jpg")
            endif()
        endif()
        set(icon_output "${CARDWIRTH_APPLE_ICON_SET}/AppIcon-${icon_size}.png")
        execute_process(COMMAND "${CARDWIRTH_SIPS}" -s format png
            -z ${icon_size} ${icon_size} "${icon_source}" --out "${icon_output}"
            RESULT_VARIABLE icon_result ERROR_VARIABLE icon_error
            OUTPUT_QUIET)
        if(NOT icon_result EQUAL 0)
            message(FATAL_ERROR "Could not generate Apple AppIcon-${icon_size}: ${icon_error}")
        endif()
    endforeach()
    set_source_files_properties("${CARDWIRTH_APPLE_PRIVACY_MANIFEST}" PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources")
    set_source_files_properties("${CARDWIRTH_APPLE_ASSET_CATALOG}" PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources")

    foreach(cardwirth_target IN LISTS CARDWIRTH_TARGETS)
        target_sources(${cardwirth_target} PRIVATE
            "${CARDWIRTH_APPLE_PRIVACY_MANIFEST}"
            "${CARDWIRTH_APPLE_ASSET_CATALOG}")
        target_link_libraries(${cardwirth_target} PRIVATE
            "-framework CoreText" "-framework CoreFoundation"
            "-framework Foundation" "-framework UniformTypeIdentifiers")
        if(IOS)
            target_link_libraries(${cardwirth_target} PRIVATE
                "-framework UIKit" "-framework AVFoundation" "-framework AVKit")
            target_compile_definitions(${cardwirth_target} PRIVATE CARDWIRTH_IOS=1)
            set_target_properties(${cardwirth_target} PROPERTIES
                XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "15.0"
                XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
                XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER
                    "${CARDWIRTH_APPLE_BUNDLE_ID}")
            if(NOT CARDWIRTH_IOS_DEVELOPMENT_TEAM STREQUAL "")
                set_target_properties(${cardwirth_target} PROPERTIES
                    XCODE_ATTRIBUTE_DEVELOPMENT_TEAM
                        "${CARDWIRTH_IOS_DEVELOPMENT_TEAM}"
                    XCODE_ATTRIBUTE_CODE_SIGN_STYLE
                        "Automatic")
            endif()
        else()
            target_link_libraries(${cardwirth_target} PRIVATE "-framework AppKit")
            target_compile_definitions(${cardwirth_target} PRIVATE CARDWIRTH_MACOS=1)
            set_target_properties(${cardwirth_target} PROPERTIES
                XCODE_ATTRIBUTE_MACOSX_DEPLOYMENT_TARGET "12.0"
                XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS
                    "${CMAKE_CURRENT_SOURCE_DIR}/platform/apple/CardWirthC-macOS.entitlements")
        endif()
        set_target_properties(${cardwirth_target} PROPERTIES
            MACOSX_BUNDLE_GUI_IDENTIFIER "${CARDWIRTH_APPLE_BUNDLE_ID}"
            MACOSX_BUNDLE_BUNDLE_NAME "${CARDWIRTH_APPLE_BUNDLE_NAME}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
            MACOSX_BUNDLE_BUNDLE_VERSION "${CARDWIRTH_BUILD_NUMBER}"
            MACOSX_BUNDLE_INFO_PLIST "${CARDWIRTH_APPLE_INFO_PLIST}"
            XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon"
        )
    endforeach()

    if(CARDWIRTH_APP_VARIANT STREQUAL "Engine" OR
       CARDWIRTH_APP_VARIANT STREQUAL "OneWindow" OR
       CARDWIRTH_APP_VARIANT STREQUAL "All")
        set(CARDWIRTH_APPLE_DATA_DIR "${CARDWIRTH_ROOT}/test_env/Data" CACHE PATH
            "Runtime Data directory packaged into Apple Engine/ALL bundles")
        if(NOT IS_DIRECTORY "${CARDWIRTH_APPLE_DATA_DIR}")
            message(FATAL_ERROR
                "Apple runtime Data was not found: ${CARDWIRTH_APPLE_DATA_DIR}. "
                "Set -DCARDWIRTH_APPLE_DATA_DIR=/absolute/path/to/Data")
        endif()
        if(NOT IS_DIRECTORY "${CARDWIRTH_APPLE_DATA_DIR}/Skin")
            message(FATAL_ERROR
                "Apple runtime Data/Skin was not found: ${CARDWIRTH_APPLE_DATA_DIR}/Skin")
        endif()
        file(GLOB CARDWIRTH_APPLE_SKIN_XMLS
            LIST_DIRECTORIES FALSE "${CARDWIRTH_APPLE_DATA_DIR}/Skin/*/Skin.xml")
        if(NOT CARDWIRTH_APPLE_SKIN_XMLS)
            message(FATAL_ERROR
                "Apple runtime Data has no usable Skin/*/Skin.xml: ${CARDWIRTH_APPLE_DATA_DIR}")
        endif()
        file(GLOB_RECURSE CARDWIRTH_APPLE_RESOURCES CONFIGURE_DEPENDS
            LIST_DIRECTORIES FALSE "${CARDWIRTH_APPLE_DATA_DIR}/*")
        foreach(resource IN LISTS CARDWIRTH_APPLE_RESOURCES)
            file(RELATIVE_PATH resource_relative "${CARDWIRTH_APPLE_DATA_DIR}" "${resource}")
            get_filename_component(resource_subdir "${resource_relative}" DIRECTORY)
            if(resource_subdir STREQUAL "")
                set(resource_location "Resources/Data")
            else()
                set(resource_location "Resources/Data/${resource_subdir}")
            endif()
            set_source_files_properties("${resource}" PROPERTIES
                MACOSX_PACKAGE_LOCATION "${resource_location}")
        endforeach()
        foreach(cardwirth_target IN LISTS CARDWIRTH_TARGETS)
            target_sources(${cardwirth_target} PRIVATE ${CARDWIRTH_APPLE_RESOURCES})
        endforeach()
    endif()

    if(CARDWIRTH_APP_VARIANT STREQUAL "Editor" OR
       CARDWIRTH_APP_VARIANT STREQUAL "All")
        set(CWXEDITOR_APPLE_RESOURCE_DIR
            "${CMAKE_CURRENT_SOURCE_DIR}/cwxeditor_src/resource")
        file(GLOB_RECURSE CWXEDITOR_APPLE_RESOURCES CONFIGURE_DEPENDS
            LIST_DIRECTORIES FALSE "${CWXEDITOR_APPLE_RESOURCE_DIR}/*")
        foreach(resource IN LISTS CWXEDITOR_APPLE_RESOURCES)
            file(RELATIVE_PATH resource_relative
                "${CWXEDITOR_APPLE_RESOURCE_DIR}" "${resource}")
            get_filename_component(resource_subdir "${resource_relative}" DIRECTORY)
            if(resource_subdir STREQUAL "")
                set(resource_location "Resources/resource")
            else()
                set(resource_location "Resources/resource/${resource_subdir}")
            endif()
            set_source_files_properties("${resource}" PROPERTIES
                MACOSX_PACKAGE_LOCATION "${resource_location}")
        endforeach()
        target_sources(${CARDWIRTH_TARGET} PRIVATE ${CWXEDITOR_APPLE_RESOURCES})
    endif()
endif()

if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN AND NOT CARDWIRTH_TEST_ONLY)
    # Linuxではビルド先から直接起動でき、インストール後もAppPaths
    # とD由来UIの相対ルートを維持する必要がある。Editor画像は
    # ソース管理されるが、Engine Dataは別途供給される。Apple
    # バンドルと同様、EngineとALLで入力を検証する。
    install(TARGETS ${CARDWIRTH_TARGET} RUNTIME DESTINATION bin)

    if(CARDWIRTH_APP_VARIANT STREQUAL "Editor" OR
       CARDWIRTH_APP_VARIANT STREQUAL "All")
        set(CWXEDITOR_LINUX_RESOURCE_DIR
            "${CMAKE_CURRENT_SOURCE_DIR}/cwxeditor_src/resource" CACHE PATH
            "CWXEditor resource directory copied into Linux Editor/ALL packages")
        if(NOT IS_DIRECTORY "${CWXEDITOR_LINUX_RESOURCE_DIR}")
            message(FATAL_ERROR
                "Linux CWXEditor resources were not found: ${CWXEDITOR_LINUX_RESOURCE_DIR}")
        endif()
        add_custom_command(TARGET ${CARDWIRTH_TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${CWXEDITOR_LINUX_RESOURCE_DIR}"
                    "$<TARGET_FILE_DIR:${CARDWIRTH_TARGET}>/resource"
            COMMENT "Copying CWXEditor resources for Linux"
            VERBATIM)
        install(DIRECTORY "${CWXEDITOR_LINUX_RESOURCE_DIR}/" DESTINATION bin/resource)
    endif()

    if(CARDWIRTH_APP_VARIANT STREQUAL "Engine" OR
       CARDWIRTH_APP_VARIANT STREQUAL "OneWindow" OR
       CARDWIRTH_APP_VARIANT STREQUAL "Editor" OR
       CARDWIRTH_APP_VARIANT STREQUAL "All")
        # 最小構成のLinuxやWSLには日本語フォントがない
        # ことが多い。Web DataLoader同梱のOFL
        # フォントを再利用し、単体Engineを含むSDLの3
        # 製品でUTF-8ラベルを代替字形ではなく表示する。
        set(CWXEDITOR_JAPANESE_FONT
            "${CMAKE_CURRENT_SOURCE_DIR}/web/DataLoader/app/Data/Font/NotoSansCJKjp-Regular.otf")
        set(CWXEDITOR_JAPANESE_FONT_LICENSE
            "${CMAKE_CURRENT_SOURCE_DIR}/web/DataLoader/app/Data/Font/OFL.txt")
        if(NOT EXISTS "${CWXEDITOR_JAPANESE_FONT}" OR
           NOT EXISTS "${CWXEDITOR_JAPANESE_FONT_LICENSE}")
            message(FATAL_ERROR "Bundled Linux Japanese font or OFL license is missing")
        endif()
        add_custom_command(TARGET ${CARDWIRTH_TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CWXEDITOR_JAPANESE_FONT}"
                    "$<TARGET_FILE_DIR:${CARDWIRTH_TARGET}>/resource/NotoSansCJKjp-Regular.otf"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CWXEDITOR_JAPANESE_FONT_LICENSE}"
                    "$<TARGET_FILE_DIR:${CARDWIRTH_TARGET}>/resource/NotoSansCJKjp-OFL.txt"
            COMMENT "Copying bundled Japanese UI font for Linux ${CARDWIRTH_APP_VARIANT}"
            VERBATIM)
        install(FILES "${CWXEDITOR_JAPANESE_FONT}"
            DESTINATION bin/resource RENAME NotoSansCJKjp-Regular.otf)
        install(FILES "${CWXEDITOR_JAPANESE_FONT_LICENSE}"
            DESTINATION bin/resource RENAME NotoSansCJKjp-OFL.txt)
        # Webと同じフォント一式と配置を使う（web/font_manifest.jsonの/program/Font/*.ttf
        # ）。LinuxとWebの文字描画を揃え、WebのテキストテストをLinux
        # にも対応させる。bundled_font_root()参照。
        set(CARDWIRTH_BUNDLED_IPA_DIR
            "${CMAKE_CURRENT_SOURCE_DIR}/web/DataLoader/app/Data/Font/IPA")
        set(CARDWIRTH_BUNDLED_IPA_FILES gothic.ttf uigothic.ttf mincho.ttf pgothic.ttf pmincho.ttf)
        set(cardwirth_ipa_copy_commands)
        foreach(ipa_file IN LISTS CARDWIRTH_BUNDLED_IPA_FILES ITEMS ReadMe.txt)
            if(NOT EXISTS "${CARDWIRTH_BUNDLED_IPA_DIR}/${ipa_file}")
                message(FATAL_ERROR "Bundled Linux IPA font is missing: ${ipa_file}")
            endif()
            list(APPEND cardwirth_ipa_copy_commands
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${CARDWIRTH_BUNDLED_IPA_DIR}/${ipa_file}"
                        "$<TARGET_FILE_DIR:${CARDWIRTH_TARGET}>/resource/Font/${ipa_file}")
            install(FILES "${CARDWIRTH_BUNDLED_IPA_DIR}/${ipa_file}" DESTINATION bin/resource/Font)
        endforeach()
        add_custom_command(TARGET ${CARDWIRTH_TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                    "$<TARGET_FILE_DIR:${CARDWIRTH_TARGET}>/resource/Font"
            ${cardwirth_ipa_copy_commands}
            COMMENT "Copying bundled IPA fonts for Linux ${CARDWIRTH_APP_VARIANT}"
            VERBATIM)
    endif()

    if(CARDWIRTH_APP_VARIANT STREQUAL "Engine" OR
       CARDWIRTH_APP_VARIANT STREQUAL "OneWindow" OR
       CARDWIRTH_APP_VARIANT STREQUAL "All")
        set(CARDWIRTH_LINUX_DATA_DIR "${CARDWIRTH_ROOT}/test_env/Data" CACHE PATH
            "Runtime Data directory copied into Linux Engine/ALL packages")
        if(NOT IS_DIRECTORY "${CARDWIRTH_LINUX_DATA_DIR}")
            message(FATAL_ERROR
                "Linux runtime Data was not found: ${CARDWIRTH_LINUX_DATA_DIR}. "
                "Set -DCARDWIRTH_LINUX_DATA_DIR=/absolute/path/to/Data")
        endif()
        file(GLOB CARDWIRTH_LINUX_SKIN_XMLS
            LIST_DIRECTORIES FALSE "${CARDWIRTH_LINUX_DATA_DIR}/Skin/*/Skin.xml")
        if(NOT CARDWIRTH_LINUX_SKIN_XMLS)
            message(FATAL_ERROR
                "Linux runtime Data has no usable Skin/*/Skin.xml: ${CARDWIRTH_LINUX_DATA_DIR}")
        endif()
        add_custom_command(TARGET ${CARDWIRTH_TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${CARDWIRTH_LINUX_DATA_DIR}"
                    "$<TARGET_FILE_DIR:${CARDWIRTH_TARGET}>/Data"
            COMMENT "Copying runtime Data for Linux ${CARDWIRTH_APP_VARIANT}"
            VERBATIM)
        install(DIRECTORY "${CARDWIRTH_LINUX_DATA_DIR}/" DESTINATION bin/Data)
    endif()

    string(TOLOWER "${CARDWIRTH_APP_VARIANT}" CARDWIRTH_LINUX_VARIANT_ID)
    set(CARDWIRTH_LINUX_APP_ID
        "jp.cardwirth.cardwirthc.${CARDWIRTH_LINUX_VARIANT_ID}")
    set(CARDWIRTH_LINUX_EXEC "${CARDWIRTH_OUTPUT_NAME}")
    if(CARDWIRTH_APP_VARIANT STREQUAL "All")
        set(CARDWIRTH_LINUX_LAUNCHER
            "${CMAKE_CURRENT_BINARY_DIR}/CardWirthPy_C_All.sh")
        configure_file(platform/linux/CardWirthC_All.sh.in
                       "${CARDWIRTH_LINUX_LAUNCHER}" @ONLY)
        install(PROGRAMS "${CARDWIRTH_LINUX_LAUNCHER}" DESTINATION bin)
        set(CARDWIRTH_LINUX_EXEC "CardWirthPy_C_All.sh")
    elseif(CARDWIRTH_APP_VARIANT STREQUAL "Engine")
        set(CARDWIRTH_LINUX_LAUNCHER
            "${CMAKE_CURRENT_BINARY_DIR}/CardWirthPy_C.sh")
        configure_file(platform/linux/CardWirthPy_C.sh.in
                       "${CARDWIRTH_LINUX_LAUNCHER}" @ONLY)
        install(PROGRAMS "${CARDWIRTH_LINUX_LAUNCHER}" DESTINATION bin)
        set(CARDWIRTH_LINUX_EXEC "CardWirthPy_C.sh")
    elseif(CARDWIRTH_APP_VARIANT STREQUAL "Editor")
        set(CARDWIRTH_LINUX_LAUNCHER
            "${CMAKE_CURRENT_BINARY_DIR}/XEditor_C.sh")
        configure_file(platform/linux/XEditor_C.sh.in
                       "${CARDWIRTH_LINUX_LAUNCHER}" @ONLY)
        install(PROGRAMS "${CARDWIRTH_LINUX_LAUNCHER}" DESTINATION bin)
        set(CARDWIRTH_LINUX_EXEC "XEditor_C.sh")
    endif()
    set(CARDWIRTH_LINUX_DESKTOP_FILE
        "${CMAKE_CURRENT_BINARY_DIR}/${CARDWIRTH_LINUX_APP_ID}.desktop")
    set(CARDWIRTH_LINUX_ICON
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/linux/cardwirthc-placeholder-256.png")
    if(NOT EXISTS "${CARDWIRTH_LINUX_ICON}")
        message(FATAL_ERROR "Linux placeholder application icon is missing: ${CARDWIRTH_LINUX_ICON}")
    endif()
    configure_file(platform/linux/CardWirthC.desktop.in
                   "${CARDWIRTH_LINUX_DESKTOP_FILE}" @ONLY)
    install(FILES "${CARDWIRTH_LINUX_DESKTOP_FILE}"
            DESTINATION share/applications)
    install(FILES "${CARDWIRTH_LINUX_ICON}"
            DESTINATION share/icons/hicolor/256x256/apps
            RENAME "${CARDWIRTH_LINUX_APP_ID}.png")

    # Linuxはfontconfigやホストのフォントを使わず、Webと同
    # じ同梱フォントだけを使う。bundled_font_root()参照。
endif()

# 絶対パスは非Windowsサンプルの開発時の代替手段に限る。
if(NOT WIN32 AND NOT ANDROID AND NOT IOS AND NOT EMSCRIPTEN AND EXISTS "${CARDWIRTH_RESOURCE_DIR}")
    foreach(cardwirth_target IN LISTS CARDWIRTH_TARGETS)
        target_compile_definitions(${cardwirth_target} PRIVATE
            CARDWIRTH_RESOURCE_DIR="${CARDWIRTH_RESOURCE_DIR}"
        )
    endforeach()
endif()

if(NOT WIN32 AND NOT ANDROID AND NOT APPLE AND NOT EMSCRIPTEN AND EXISTS "${CARDWIRTH_RESOURCE_DIR}")
    foreach(cardwirth_target IN LISTS CARDWIRTH_TARGETS)
        add_custom_command(TARGET ${cardwirth_target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${CARDWIRTH_RESOURCE_DIR}"
                    "$<TARGET_FILE_DIR:${cardwirth_target}>/test_resource"
            COMMENT "Copying test resources"
        )
    endforeach()
endif()

if(EMSCRIPTEN)
    if(CARDWIRTH_WEB_IO_SERVER)
        set(CARDWIRTH_WEB_LAZY_IO_LIBRARY
            "${CMAKE_CURRENT_SOURCE_DIR}/web/lazy_io_server.js")
    else()
        set(CARDWIRTH_WEB_LAZY_IO_LIBRARY
            "${CMAKE_CURRENT_SOURCE_DIR}/web/lazy_io.js")
    endif()

    # 生成したASYNCIFY_ONLY一覧は、ASYNCIFY_ADVISE
    # で解析したソースとツールチェーンの入力にのみ有効。関連するソ
    # ース、CMake、JSを変更すると通常の限定ビルドは停止する
    # 。製品別の一覧を解析処理で再生成し、確認してから使用する。
    if(NOT CARDWIRTH_WEB_IO_SERVER)
    file(GLOB_RECURSE CARDWIRTH_ASYNCIFY_FINGERPRINT_INPUTS CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cxx"
        "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/*.hh"
        "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/*.m"
        "${CMAKE_CURRENT_SOURCE_DIR}/*.mm"
        "${CMAKE_CURRENT_SOURCE_DIR}/*.js"
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cmake"
        "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt")
    list(FILTER CARDWIRTH_ASYNCIFY_FINGERPRINT_INPUTS EXCLUDE REGEX
        "/(cwxeditor_src_d|resource|DataLoader|vendor)/")
    list(SORT CARDWIRTH_ASYNCIFY_FINGERPRINT_INPUTS)
    set(CARDWIRTH_ASYNCIFY_FINGERPRINT_MATERIAL
        "variant=${CARDWIRTH_APP_VARIANT}\nthreads=${CARDWIRTH_WEB_PTHREADS}\ncompiler=${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}\n")
    foreach(CARDWIRTH_ASYNCIFY_INPUT IN LISTS CARDWIRTH_ASYNCIFY_FINGERPRINT_INPUTS)
        file(SHA256 "${CARDWIRTH_ASYNCIFY_INPUT}" CARDWIRTH_ASYNCIFY_INPUT_HASH)
        file(RELATIVE_PATH CARDWIRTH_ASYNCIFY_INPUT_RELATIVE
            "${CMAKE_CURRENT_SOURCE_DIR}" "${CARDWIRTH_ASYNCIFY_INPUT}")
        string(APPEND CARDWIRTH_ASYNCIFY_FINGERPRINT_MATERIAL
            "${CARDWIRTH_ASYNCIFY_INPUT_RELATIVE}=${CARDWIRTH_ASYNCIFY_INPUT_HASH}\n")
    endforeach()
    string(SHA256 CARDWIRTH_ASYNCIFY_FINGERPRINT
        "${CARDWIRTH_ASYNCIFY_FINGERPRINT_MATERIAL}")
    set(CARDWIRTH_ASYNCIFY_FINGERPRINT_FILE
        "${CMAKE_CURRENT_BINARY_DIR}/cardwirth_asyncify_fingerprint.txt")
    file(WRITE "${CARDWIRTH_ASYNCIFY_FINGERPRINT_FILE}"
        "${CARDWIRTH_ASYNCIFY_FINGERPRINT}\n")
    endif()

    if(CARDWIRTH_APP_VARIANT STREQUAL "All" OR
       CARDWIRTH_APP_VARIANT STREQUAL "Editor")
        set(CWXEDITOR_WEB_RESOURCE_ARCHIVE
            "${CMAKE_CURRENT_BINARY_DIR}/cwxeditor_resource.zip")
        add_custom_command(
            OUTPUT "${CWXEDITOR_WEB_RESOURCE_ARCHIVE}"
            COMMAND ${CMAKE_COMMAND} -E tar cf "${CWXEDITOR_WEB_RESOURCE_ARCHIVE}"
                    --format=zip resource
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/cwxeditor_src"
            DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/cwxeditor_src/resource"
            COMMENT "Packing UTF-8 CWXEditor resources for the Web virtual filesystem"
            VERBATIM)
        add_custom_target(cwxeditor_web_resources
            DEPENDS "${CWXEDITOR_WEB_RESOURCE_ARCHIVE}")
        add_dependencies(${CARDWIRTH_TARGET} cwxeditor_web_resources)
    endif()
    if(CARDWIRTH_WEB_IO_SERVER)
        set(CARDWIRTH_WEB_IO_SERVER_DEFINITION 1)
    else()
        set(CARDWIRTH_WEB_IO_SERVER_DEFINITION 0)
    endif()
    target_compile_definitions(${CARDWIRTH_TARGET} PRIVATE
        CARDWIRTH_WEB=1
        CARDWIRTH_WEB_IO_SERVER=${CARDWIRTH_WEB_IO_SERVER_DEFINITION})
    if(TARGET cwxeditor_lib)
        target_compile_definitions(cwxeditor_lib PRIVATE
            CARDWIRTH_WEB_IO_SERVER=${CARDWIRTH_WEB_IO_SERVER_DEFINITION})
    endif()
    target_compile_options(${CARDWIRTH_TARGET} PRIVATE "SHELL:-sUSE_ZLIB=1")
    # ブラウザーのJavaScriptからWasmへの再入は、これらのアプリ
    # ケーションコールバックに限定する。広範なC/C++ APIを公開せず、
    # 製品別の公開範囲を明示する。EmscriptenやSDLが内部で追加す
    # る必須公開関数とは別に、この一覧がアプリケーション側の公開契約となる。
    set(CARDWIRTH_WEB_EXPORTED_FUNCTIONS
        _main
        _cardwirth_web_transfer_item
        _cardwirth_web_transfer_filter
        _cardwirth_web_transfer_complete
        _cardwirth_web_download_complete)
    if(CARDWIRTH_APP_VARIANT STREQUAL "All" OR
       CARDWIRTH_APP_VARIANT STREQUAL "Editor")
        list(APPEND CARDWIRTH_WEB_EXPORTED_FUNCTIONS
            _cwxeditor_web_storage_sync_complete)
    endif()
    list(JOIN CARDWIRTH_WEB_EXPORTED_FUNCTIONS ","
         CARDWIRTH_WEB_EXPORTED_FUNCTIONS_VALUE)
    if(NOT CARDWIRTH_WEB_IO_SERVER)
    # 同期Wasm呼び出しを中断できるインポートはこれらに限る。
    # Binaryenはこれらを起点に必要な呼び出し元を解析する
    # 。短いASYNCIFY_ONLY一覧で代用すると、巻き戻
    # しが必要な仮想呼び出しやSDLの間接呼び出し元が漏れる。
    set(CARDWIRTH_WEB_ASYNCIFY_IMPORTS
        fd_read
        fd_write
        fd_pread
        fd_pwrite
        fd_seek
        fd_close
        _mmap_js
        _munmap_js
        _msync_js
        fd_sync
        emscripten_sleep)
    list(JOIN CARDWIRTH_WEB_ASYNCIFY_IMPORTS ","
         CARDWIRTH_WEB_ASYNCIFY_IMPORTS_VALUE)
    set(CARDWIRTH_WEB_ASYNCIFY_ONLY_OPTION "")
    if(CARDWIRTH_WEB_ASYNCIFY_USE_ONLY_LIST AND
       (CARDWIRTH_APP_VARIANT STREQUAL "Engine" OR
        CARDWIRTH_APP_VARIANT STREQUAL "All"))
        string(TOLOWER "${CARDWIRTH_APP_VARIANT}" CARDWIRTH_ASYNCIFY_VARIANT_ID)
        set(CARDWIRTH_ASYNCIFY_ONLY_FILE
            "${CMAKE_CURRENT_SOURCE_DIR}/web/asyncify_only_${CARDWIRTH_ASYNCIFY_VARIANT_ID}.txt")
        set(CARDWIRTH_ASYNCIFY_STAMP_FILE
            "${CMAKE_CURRENT_SOURCE_DIR}/web/asyncify_only_${CARDWIRTH_ASYNCIFY_VARIANT_ID}.sha256")
        if(NOT EXISTS "${CARDWIRTH_ASYNCIFY_ONLY_FILE}" OR
           NOT EXISTS "${CARDWIRTH_ASYNCIFY_STAMP_FILE}")
            message(FATAL_ERROR
                "Missing generated Asyncify list for ${CARDWIRTH_APP_VARIANT}; run tools/run_asyncify_analysis_test_1.ps1 -UpdateLists")
        endif()
        file(READ "${CARDWIRTH_ASYNCIFY_STAMP_FILE}" CARDWIRTH_ASYNCIFY_EXPECTED_FINGERPRINT)
        string(STRIP "${CARDWIRTH_ASYNCIFY_EXPECTED_FINGERPRINT}"
            CARDWIRTH_ASYNCIFY_EXPECTED_FINGERPRINT)
        if(NOT CARDWIRTH_ASYNCIFY_EXPECTED_FINGERPRINT STREQUAL
               CARDWIRTH_ASYNCIFY_FINGERPRINT)
            message(FATAL_ERROR
                "Asyncify list for ${CARDWIRTH_APP_VARIANT} is stale; run the advice pipeline and review the generated difference")
        endif()
        set(CARDWIRTH_WEB_ASYNCIFY_ONLY_OPTION
            "SHELL:-sASYNCIFY_ONLY=@${CARDWIRTH_ASYNCIFY_ONLY_FILE}")
        set_property(TARGET ${CARDWIRTH_TARGET} APPEND PROPERTY LINK_DEPENDS
            "${CARDWIRTH_ASYNCIFY_ONLY_FILE}"
            "${CARDWIRTH_ASYNCIFY_STAMP_FILE}")
    endif()
    endif()
    if(CARDWIRTH_WEB_IO_SERVER)
        # キャンバスはアプリケーションのワーカーに保持する。OFFSCREENCANVASES_TO_PTHREAD
        # を空に強制するとGL呼び出しが個別にブラウザーへ転送
        # され、Editorの合成前にホストの消去が見える場
        # 合がある。ワーカーが描画を所有する経路を維持する。
        set(CARDWIRTH_WEB_ASYNC_MODEL_OPTIONS
            "SHELL:-sASYNCIFY=0"
            "SHELL:-sPROXY_TO_PTHREAD=1"
            "SHELL:-sOFFSCREENCANVAS_SUPPORT=1"
            "SHELL:-sOFFSCREEN_FRAMEBUFFER=1")
        message(STATUS
            "Web async model: C++ application pthread with browser-main I/O server")
    else()
        set(CARDWIRTH_WEB_ASYNC_MODEL_OPTIONS
            "SHELL:-sASYNCIFY=1"
            "SHELL:-sASYNCIFY_STACK_SIZE=1048576"
            "SHELL:-sASYNCIFY_IMPORTS=${CARDWIRTH_WEB_ASYNCIFY_IMPORTS_VALUE}"
            "${CARDWIRTH_WEB_ASYNCIFY_ONLY_OPTION}"
            "$<$<BOOL:${CARDWIRTH_WEB_ASYNCIFY_ADVISE}>:SHELL:-sASYNCIFY_ADVISE=1>")
        message(STATUS "Web async model: Asyncify")
    endif()
    # --pre-jsの内容はEmscriptenの最終リンクで埋め込まれる。明示的な入
    # 力として追跡し、ローダーだけの編集でもJavaScriptラッパーを再リンクする。
    set_property(TARGET ${CARDWIRTH_TARGET} APPEND PROPERTY LINK_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/web/data_loader.js"
        "${CARDWIRTH_WEB_LAZY_IO_LIBRARY}"
        "${CMAKE_CURRENT_SOURCE_DIR}/web/lazy_io_server.js"
        "${CMAKE_CURRENT_SOURCE_DIR}/web/web_transfer.js")
    target_link_options(${CARDWIRTH_TARGET} PRIVATE
        "SHELL:--pre-js ${CMAKE_CURRENT_SOURCE_DIR}/web/data_loader.js"
        "SHELL:--js-library ${CARDWIRTH_WEB_LAZY_IO_LIBRARY}"
        "SHELL:--js-library ${CMAKE_CURRENT_SOURCE_DIR}/web/web_transfer.js"
        # Webフォントは個別にキャッシュする素材として配布する。SDL_ttf
        # の開始前に起動処理が/programへ配置するため、数MBあるこ
        # れらのファイルをWASMの仮想ファイルシステムへ埋め込まない。
        "SHELL:-lidbfs.js"
        "SHELL:-sMODULARIZE=1"
        "SHELL:-sEXPORT_NAME=createCardWirthModule"
        "SHELL:-sENVIRONMENT=web"
        "SHELL:-sALLOW_MEMORY_GROWTH=1"
        "SHELL:-sINITIAL_MEMORY=67108864"
        # メモリ上限は3 GiB。SDKは符号なしのヒープ添字を出力する。独自のWeb
        # 入出力とAtomicsの境界でも2 GiB超のwasm32ポインターを正規化
        # する。web_high_address_testsは符号付き引数を検証する。
        "SHELL:-sMAXIMUM_MEMORY=3221225472"
        "SHELL:-sSTACK_SIZE=8388608"
        "SHELL:-sUSE_ZLIB=1"
        "SHELL:-sFORCE_FILESYSTEM=1"
        "SHELL:-sEXIT_RUNTIME=0"
        # cmake/web_exceptions.cmakeで、リンクする全C++ラ
        # イブラリを含めてコンパイル時とリンク時の例外捕捉・後始末を有効にする。既定
        # では既存のAsyncify契約を維持する。任意の入出力サーバー方式ではC++
        # をワーカーへ移し、ストレージ処理をブラウザーのメインスレッドへ転送する。
        ${CARDWIRTH_WEB_ASYNC_MODEL_OPTIONS}
        "SHELL:-sEXPORTED_FUNCTIONS=${CARDWIRTH_WEB_EXPORTED_FUNCTIONS_VALUE}"
    )
    if(CARDWIRTH_APP_VARIANT STREQUAL "All" OR
       CARDWIRTH_APP_VARIANT STREQUAL "Editor")
        target_link_options(${CARDWIRTH_TARGET} PRIVATE
            "SHELL:--embed-file ${CWXEDITOR_WEB_RESOURCE_ARCHIVE}@/program/cwxeditor_resource.zip")
    endif()
    if(CARDWIRTH_WEB_SKIP_FINAL_OPTIMIZATION)
        # すべてのC++翻訳単位はCMAKE_BUILD_TYPE=Release
        # のまま、Emscriptenの最終リンクだけO0にする。既
        # 定方式ではBinaryenが必要なAsyncify変換を行
        # う。入出力サーバー方式ではAsyncify変換を行わない。
        target_link_options(${CARDWIRTH_TARGET} PRIVATE "SHELL:-O0")
    endif()
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_link_options(${CARDWIRTH_TARGET} PRIVATE
            "SHELL:-sASSERTIONS=2"
            "SHELL:-sEXCEPTION_STACK_TRACES=1")
    else()
        # Releaseでも原因を特定できるブラウザー診断を残
        # す。アサーションレベル1は配布可能な規模で、原因の
        # 見えないワーカー終了を具体的な実行時エラーにする。
        target_link_options(${CARDWIRTH_TARGET} PRIVATE
            "SHELL:-sASSERTIONS=1")
    endif()
    target_compile_definitions(${CARDWIRTH_TARGET} PRIVATE CARDWIRTH_WEB_PTHREADS=1)
    target_compile_options(${CARDWIRTH_TARGET} PRIVATE -pthread)
    target_link_options(${CARDWIRTH_TARGET} PRIVATE
        -pthread
        # 非同期・同期の代理処理は、ブラウザーのメインスレッドのIndexedDB
        # 処理が終わるまで、要求元のC++ワーカーだけを待機させる。入出力サ
        # ーバー方式ではアプリケーションのpthreadもこの枠を1つ使う。
        "SHELL:-sPTHREAD_POOL_SIZE=4"
    )
    set_target_properties(${CARDWIRTH_TARGET} PROPERTIES SUFFIX ".js")
endif()

# SRELLのBSDライセンス表示を、インストール先だけでなく製品バイナリの横にも配
# 置する。ヘッダーのみの依存関係なので、プラットフォームの実行時ライブラリは不要。
foreach(cardwirth_license_target IN LISTS CARDWIRTH_TARGETS)
    if(TARGET "${cardwirth_license_target}")
        add_custom_command(TARGET "${cardwirth_license_target}" POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:${cardwirth_license_target}>/licenses"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/srell/license.txt"
                "$<TARGET_FILE_DIR:${cardwirth_license_target}>/licenses/SRELL.txt"
            VERBATIM)
    endif()
endforeach()
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/srell/license.txt"
    DESTINATION bin/licenses RENAME SRELL.txt)

if(TARGET cardwirth_softfloat3e)
    foreach(cardwirth_license_target IN LISTS CARDWIRTH_TARGETS)
        if(TARGET "${cardwirth_license_target}")
            add_custom_command(TARGET "${cardwirth_license_target}" POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory
                    "$<TARGET_FILE_DIR:${cardwirth_license_target}>/licenses"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/softfloat/COPYING.txt"
                    "$<TARGET_FILE_DIR:${cardwirth_license_target}>/licenses/SoftFloat-3e.txt"
                VERBATIM)
        endif()
    endforeach()
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/softfloat/COPYING.txt"
        DESTINATION bin/licenses RENAME SoftFloat-3e.txt)
endif()

# 静的JPEGコーデックにはIJG/libjpeg-turboの配布条
# 件が適用される。全製品で同じ固定版依存ライブラリの表示を使用する。
if(CARDWIRTH_JPEG_SOURCE_DIR)
    foreach(cardwirth_license_target IN LISTS CARDWIRTH_TARGETS)
        add_custom_command(TARGET ${cardwirth_license_target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:${cardwirth_license_target}>/licenses"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CARDWIRTH_JPEG_LICENSE_FILE}"
                "$<TARGET_FILE_DIR:${cardwirth_license_target}>/licenses/IJG-JPEG.txt"
            VERBATIM)
    endforeach()
    install(FILES "${CARDWIRTH_JPEG_LICENSE_FILE}"
        DESTINATION bin/licenses RENAME IJG-JPEG.txt)
endif()

# LHAは任意のホストDLLではなく、Py互換の対応アーカイブ形式に含まれる
# 。共通アーカイブ処理の利用先すべてに同じ固定版の静的デコーダーを適用する。
include(cmake/CardWirthLHA.cmake)
cardwirth_attach_directory_lha()
if(TARGET cardwirth_lha_backend)
    foreach(cardwirth_license_target IN LISTS CARDWIRTH_TARGETS)
        cardwirth_stage_lha_license(${cardwirth_license_target})
    endforeach()
    install(FILES "${CARDWIRTH_LHA_LICENSE_FILE}"
        DESTINATION bin/licenses RENAME libarchive-LHA.txt)
endif()

# 一部の製品・テストは上記ディレクトリ設定の後に独自の警
# 告レベルを追加する。対応OSで/W4や-Wallが警
# 告を再有効化しないよう、抑制設定を末尾にも追加する。
if(CARDWIRTH_SUPPRESS_WARNINGS)
    if(MSVC)
        # ディレクトリ単位の/wとは別に/W0を使う。CMake
        # は同じオプションを重複除去するため、独自の警告レベルを
        # 追加するターゲットで/wの後に/W4が残るのを防ぐ。
        set(CARDWIRTH_WARNING_SUPPRESS_OPTION /W0 /wd4244)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        set(CARDWIRTH_WARNING_SUPPRESS_OPTION -w)
    endif()
    if(CARDWIRTH_WARNING_SUPPRESS_OPTION)
        foreach(cardwirth_warning_target IN ITEMS
                ${CARDWIRTH_TARGETS}
                cwxeditor_lib cwxeditor_core cwxeditor_gui cwxeditor_app
                cardwirth_explorer_tests cardwirth_storage_tests
                cardwirth_engine_encryption_contract_tests
                cardwirth_managed_userdata_path_tests
                cwxeditor_test_runtime cwxeditor_tests
                cwxeditor_parity_tests cwxeditor_extended_parity_tests
                editor_core_stage5_script_gui_export_tests
                cwxeditor_core_d_unittest_parity_tests cwxeditor_contract_tests
                cwxeditor_shutdown_tests cwxeditor_task_group_tests
                cwxeditor_file_picker_tests cwxeditor_test_support
                cwxeditor_ui_contract_dump cwxeditor_ui_contract_tests
                cwxeditor_workflow_tests all_layout_tests)
            if(TARGET "${cardwirth_warning_target}")
                target_compile_options("${cardwirth_warning_target}" PRIVATE
                    ${CARDWIRTH_WARNING_SUPPRESS_OPTION})
            endif()
        endforeach()
    endif()
endif()

# 物理音声デバイスなしで製品のWindows BASSローダーを検証
# する。修飾なしのBASS公開名はWindows x64 ABI向け
# 。別のアーキテクチャでは各プラットフォームの実行時テストを使う。
if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    if(TARGET cwxeditor_test_support)
        add_library(editor_test_bass SHARED
            tests/editor_windows_bass_fake_dll.cpp)
        target_compile_features(editor_test_bass PRIVATE cxx_std_20)
        set_target_properties(editor_test_bass PROPERTIES
            OUTPUT_NAME bass
            PREFIX ""
            RUNTIME_OUTPUT_DIRECTORY
                "${CMAKE_BINARY_DIR}/test-runtime/editor-windows-bass")
        add_library(editor_test_bassmidi SHARED
            tests/editor_windows_bass_fake_midi_dll.cpp)
        target_compile_features(editor_test_bassmidi PRIVATE cxx_std_20)
        set_target_properties(editor_test_bassmidi PROPERTIES
            OUTPUT_NAME bassmidi
            PREFIX ""
            ARCHIVE_OUTPUT_DIRECTORY
                "${CMAKE_BINARY_DIR}/test-runtime/editor-windows-bass"
            RUNTIME_OUTPUT_DIRECTORY
                "${CMAKE_BINARY_DIR}/test-runtime/editor-windows-bass")
        add_library(editor_test_bassmidi_no_soundfont SHARED
            tests/editor_windows_bass_fake_midi_dll.cpp)
        target_compile_features(editor_test_bassmidi_no_soundfont PRIVATE cxx_std_20)
        target_compile_definitions(editor_test_bassmidi_no_soundfont PRIVATE
            FAKE_BASS_MIDI_NO_SOUNDFONT_EXPORTS=1)
        set_target_properties(editor_test_bassmidi_no_soundfont PROPERTIES
            OUTPUT_NAME bassmidi
            PREFIX ""
            ARCHIVE_OUTPUT_DIRECTORY
                "${CMAKE_BINARY_DIR}/test-runtime/editor-windows-bass-no-soundfont"
            RUNTIME_OUTPUT_DIRECTORY
                "${CMAKE_BINARY_DIR}/test-runtime/editor-windows-bass-no-soundfont")
        add_dependencies(editor_test_bassmidi_no_soundfont editor_test_bass)
        add_custom_command(TARGET editor_test_bassmidi_no_soundfont POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:editor_test_bass>"
                "$<TARGET_FILE_DIR:editor_test_bassmidi_no_soundfont>/bass.dll")
        if(MSVC)
            target_compile_options(editor_test_bass PRIVATE /W4 /EHsc /utf-8)
            target_compile_options(editor_test_bassmidi PRIVATE /W4 /EHsc /utf-8)
            target_compile_options(editor_test_bassmidi_no_soundfont PRIVATE /W4 /EHsc /utf-8)
        endif()

        add_executable(editor_windows_bass_adapter_tests
            tests/editor_windows_bass_adapter_tests.cpp)
        target_compile_features(editor_windows_bass_adapter_tests PRIVATE cxx_std_20)
        target_link_libraries(editor_windows_bass_adapter_tests PRIVATE
            cwxeditor_test_support
            User32 Gdi32 Shell32 Ole32 Comctl32 Comdlg32 Winmm Shlwapi Ws2_32
            Dbghelp Setupapi WindowsCodecs Uuid Normaliz)
        add_dependencies(editor_windows_bass_adapter_tests
            editor_test_bass editor_test_bassmidi editor_test_bassmidi_no_soundfont)
        add_test(NAME editor_windows_bass_adapter
            COMMAND editor_windows_bass_adapter_tests
                "$<TARGET_FILE_DIR:editor_test_bass>"
                "$<TARGET_FILE_DIR:editor_test_bassmidi_no_soundfont>")
        set_tests_properties(editor_windows_bass_adapter PROPERTIES
            TIMEOUT 30 LABELS "editor;audio;windows;adapter")
    endif()
endif()

# 単体GUIの復旧回帰テスト。
if(TARGET cwxeditor_test_support)
    foreach(recovery_test IN ITEMS editor_gui_recovery_root_tests editor_gui_card_recovery_batch01_tests editor_gui_event_recovery_batch1_tests editor_shared_data_loss_recovery_tests editor_gui_event_retirement_boundary_tests editor_gui_card_recovery_batch02_tests editor_gui_event_recovery_batch2_tests editor_namewindow_recovery_batch2_tests editor_reference_failure_preservation_tests editor_history_spelling_recovery_tests editor_png_alpha_recovery_tests editor_png_key_depth_recovery_tests editor_gui_card_recovery_batch03_tests editor_gui_event_recovery_batch3_tests editor_scene_deletion_snapshot_tests editor_resource_deletion_snapshot_tests editor_shared_gui_recovery_batch03_tests editor_shared_gui_recovery_batch04_tests editor_gui_card_recovery_batch04_tests editor_gui_event_recovery_batch4_tests editor_gui_event_recovery_batch5_tests editor_gui_event_recovery_batch6_tests editor_gui_card_recovery_batch05_tests editor_variable_warning_resolution_tests editor_gui_card_recovery_batch06_tests editor_gui_event_notifications_tests editor_shared_gui_recovery_batch05_tests editor_save_sound_regression_tests editor_expression_dialog_recovery_tests editor_gui_event_branch_labels_tests editor_gui_event_followup_tests editor_eventeditor_viewport_contract_tests editor_input_assistance_tests editor_event_lifecycle_api_e76_e77_tests editor_event_diagnostic_native_host_tests editor_gui_tab_navigation_tests editor_gui_search_refresh_api_e81_e82_tests editor_gui_search_scope_notifications_tests editor_scene_event_sync_repair_tests editor_event_host_controls_tests editor_gui_search_candidate_api_e52_e53_e92_tests editor_event_navigation_api_e85_e86_e97_tests editor_name_undo_regression_tests editor_text_tabs_regression_tests editor_event_owner_icons_tests editor_start_reference_navigation_tests editor_autokeycode_menu_regression_tests editor_autokeycode_table_actions_tests editor_gui_search_field_menus_api_e54_tests editor_event_scene_commands_api_e30_tests editor_card_dialog_navigation_host_test editor_result_id_search_api_e55_tests editor_content_localization_api_e95_tests editor_setvariant_chooser_api_e91_tests editor_instance_receiver_startup_tests editor_event_tree_drag_api_e43_tests)
        if(recovery_test IN_LIST CARDWIRTH_ADDITIONAL_BUNDLE_CASES)
            if(recovery_test STREQUAL "editor_instance_receiver_startup_tests" OR
               recovery_test STREQUAL "editor_gui_event_notifications_tests" OR
               recovery_test STREQUAL "editor_gui_card_recovery_batch01_tests" OR
               recovery_test STREQUAL "editor_gui_card_recovery_batch02_tests" OR
               recovery_test STREQUAL "editor_scene_event_sync_repair_tests")
                cardwirth_add_bundled_editor_test(${recovery_test} WITH_ARGS tests/${recovery_test}.cpp)
            else()
                cardwirth_add_bundled_editor_test(${recovery_test} tests/${recovery_test}.cpp)
            endif()
        else()
        add_executable(${recovery_test} tests/${recovery_test}.cpp)
            set(${recovery_test}_TEST_COMMAND ${recovery_test})
        endif()
        target_compile_features(${recovery_test} PRIVATE cxx_std_20)
        if(MSVC)
            target_compile_options(${recovery_test} PRIVATE /utf-8)
        endif()
        target_link_libraries(${recovery_test} PRIVATE cwxeditor_test_support)
        add_test(NAME ${recovery_test} COMMAND ${${recovery_test}_TEST_COMMAND})
        set_tests_properties(${recovery_test} PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")
        if(WIN32 AND recovery_test STREQUAL "editor_instance_receiver_startup_tests")
            add_test(NAME editor_api_d_a0e60d5f0bd0ef15fbcb
                COMMAND ${${recovery_test}_TEST_COMMAND} --api-d-a0e60d5f0bd0ef15fbcb)
            set_tests_properties(editor_api_d_a0e60d5f0bd0ef15fbcb
                PROPERTIES TIMEOUT 30 LABELS "editor;gui;api;windows")
        endif()
        if(WIN32)
            target_link_libraries(${recovery_test} PRIVATE User32 Gdi32 Shell32 Ole32 Comctl32 Comdlg32 Winmm Shlwapi Ws2_32 Dbghelp Setupapi WindowsCodecs Uuid Normaliz)
        endif()
    endforeach()
    add_test(NAME editor_event_dialog_clear_warning_ownership
        COMMAND ${editor_gui_event_notifications_tests_TEST_COMMAND} --clear-event-warning-ownership)
    set_tests_properties(editor_event_dialog_clear_warning_ownership PROPERTIES TIMEOUT 120 LABELS "editor;gui;targeted")
    add_test(NAME editor_event_dialog_local_warning_ownership
        COMMAND ${editor_gui_event_notifications_tests_TEST_COMMAND} --event-dialog-warning-ownership)
    set_tests_properties(editor_event_dialog_local_warning_ownership PROPERTIES TIMEOUT 120 LABELS "editor;gui;targeted")
endif()

if(TARGET cwxeditor_test_support)
    add_custom_target(editor_gui_recovery_regression_suite)
    foreach(recovery_target IN ITEMS
        editor_gui_recovery_root_tests
        editor_gui_card_recovery_batch01_tests
        editor_gui_event_recovery_batch1_tests
        editor_shared_data_loss_recovery_tests
        editor_gui_event_retirement_boundary_tests
        editor_gui_card_recovery_batch02_tests
        editor_gui_event_recovery_batch2_tests
        editor_namewindow_recovery_batch2_tests
        editor_reference_failure_preservation_tests
        editor_history_spelling_recovery_tests
        editor_png_alpha_recovery_tests
        editor_png_key_depth_recovery_tests
        editor_gui_card_recovery_batch03_tests
        editor_gui_event_recovery_batch3_tests
        editor_scene_deletion_snapshot_tests
        editor_resource_deletion_snapshot_tests
        editor_resource_history_repair_tests
        editor_scene_contract_tests
        editor_scene_payload_contract_tests
        editor_scene_selection_recovered_tests
        editor_scene_route_residual_tests
        editor_motion_coupon_command_tests
        editor_shared_gui_recovery_batch03_tests
        editor_gui_card_recovery_batch04_tests
        editor_gui_event_recovery_batch4_tests
        editor_gui_event_recovery_batch5_tests
        editor_settings_message_contract_tests
        editor_stateful_callback_contract_tests
        editor_search_public_helper_contract_tests
        editor_image_transfer_contract_tests
        editor_template_refresh_contract_tests
        editor_initial_storage_contract_tests
        editor_supplemental_value_contract_tests
        editor_text_font_contract_tests
        editor_gui_card_recovery_batch05_tests
        editor_gui_event_recovery_batch6_tests
        editor_shared_gui_recovery_batch04_tests
        editor_variable_warning_resolution_tests
        editor_gui_card_recovery_batch06_tests
        editor_gui_event_notifications_tests
        editor_shared_gui_recovery_batch05_tests
        editor_save_sound_regression_tests
        editor_expression_dialog_recovery_tests
        editor_gui_event_branch_labels_tests
        editor_gui_event_followup_tests
        editor_eventeditor_viewport_contract_tests
        editor_input_assistance_tests
        editor_event_lifecycle_api_e76_e77_tests
        editor_event_diagnostic_native_host_tests
        editor_gui_tab_navigation_tests editor_gui_search_refresh_api_e81_e82_tests editor_gui_search_scope_notifications_tests editor_scene_event_sync_repair_tests editor_event_host_controls_tests editor_gui_search_candidate_api_e52_e53_e92_tests editor_event_navigation_api_e85_e86_e97_tests editor_name_undo_regression_tests editor_text_tabs_regression_tests editor_event_owner_icons_tests editor_start_reference_navigation_tests editor_autokeycode_menu_regression_tests editor_autokeycode_table_actions_tests editor_gui_search_field_menus_api_e54_tests editor_event_scene_commands_api_e30_tests editor_card_dialog_navigation_host_test editor_result_id_search_api_e55_tests editor_content_localization_api_e95_tests editor_setvariant_chooser_api_e91_tests editor_instance_receiver_startup_tests editor_event_tree_drag_api_e43_tests)
        if(TARGET ${recovery_target})
            add_dependencies(editor_gui_recovery_regression_suite ${recovery_target})
        endif()
    endforeach()
    add_custom_target(editor_gui_recovery_batch18 DEPENDS editor_event_diagnostic_native_host_tests editor_gui_tab_navigation_tests)
endif()

if(TARGET editor_gui_search_refresh_api_e81_e82_tests)
    add_custom_target(editor_gui_recovery_batch20 DEPENDS editor_gui_search_refresh_api_e81_e82_tests editor_gui_search_scope_notifications_tests)
endif()

if(TARGET editor_scene_event_sync_repair_tests)
    add_custom_target(editor_gui_recovery_batch22 DEPENDS editor_scene_event_sync_repair_tests editor_event_host_controls_tests)
endif()

if(TARGET editor_gui_search_candidate_api_e52_e53_e92_tests)
    add_custom_target(editor_gui_recovery_batch24 DEPENDS editor_gui_search_candidate_api_e52_e53_e92_tests)
endif()

if(TARGET editor_event_navigation_api_e85_e86_e97_tests)
    add_custom_target(editor_gui_recovery_batch26 DEPENDS editor_event_navigation_api_e85_e86_e97_tests)
endif()

if(TARGET editor_name_undo_regression_tests)
    add_custom_target(editor_gui_recovery_batch28 DEPENDS editor_name_undo_regression_tests editor_text_tabs_regression_tests)
endif()

if(TARGET editor_event_owner_icons_tests)
    add_custom_target(editor_gui_recovery_batch30 DEPENDS editor_event_owner_icons_tests)
endif()

if(TARGET editor_start_reference_navigation_tests)
    add_custom_target(editor_gui_recovery_batch32 DEPENDS editor_start_reference_navigation_tests)
endif()

if(TARGET editor_autokeycode_menu_regression_tests)
    add_custom_target(editor_gui_recovery_batch34 DEPENDS editor_autokeycode_menu_regression_tests)
endif()

if(TARGET editor_autokeycode_table_actions_tests)
    add_custom_target(editor_gui_recovery_batch36 DEPENDS editor_autokeycode_table_actions_tests editor_autokeycode_menu_regression_tests editor_inline_transaction_tests editor_clipboard_list_recovery_tests)
endif()

if(TARGET editor_gui_search_field_menus_api_e54_tests)
    add_custom_target(editor_gui_recovery_batch38 DEPENDS editor_gui_search_field_menus_api_e54_tests editor_autokeycode_menu_regression_tests editor_gui_search_candidate_api_e52_e53_e92_tests editor_name_undo_regression_tests editor_gui_search_refresh_api_e81_e82_tests editor_gui_search_scope_notifications_tests)
endif()

if(TARGET editor_event_scene_commands_api_e30_tests)
    add_custom_target(editor_gui_recovery_batch40 DEPENDS editor_event_scene_commands_api_e30_tests editor_card_dialog_navigation_host_test editor_event_host_controls_tests editor_scene_event_sync_repair_tests editor_start_reference_navigation_tests editor_gui_tab_navigation_tests)
endif()

if(TARGET editor_result_id_search_api_e55_tests)
    add_custom_target(editor_gui_recovery_batch42 DEPENDS editor_result_id_search_api_e55_tests editor_content_localization_api_e95_tests editor_gui_event_branch_labels_tests editor_gui_event_followup_tests editor_start_reference_navigation_tests editor_gui_search_candidate_api_e52_e53_e92_tests editor_gui_search_scope_notifications_tests editor_gui_search_refresh_api_e81_e82_tests editor_name_undo_regression_tests editor_gui_search_field_menus_api_e54_tests)
endif()

if(TARGET editor_setvariant_chooser_api_e91_tests)
    add_custom_target(editor_gui_recovery_batch44 DEPENDS editor_setvariant_chooser_api_e91_tests editor_instance_receiver_startup_tests editor_event_tree_drag_api_e43_tests editor_expression_dialog_recovery_tests editor_variable_state_contract_tests editor_chooser_api_residual_tests editor_event_host_controls_tests editor_start_reference_navigation_tests editor_scene_event_sync_repair_tests editor_gui_tab_navigation_tests editor_gui_event_followup_tests editor_gui_event_recovery_batch5_tests editor_event_navigation_api_e85_e86_e97_tests editor_event_lifecycle_api_e76_e77_tests editor_result_id_search_api_e55_tests editor_content_localization_api_e95_tests)
    add_dependencies(editor_gui_recovery_regression_suite editor_variable_state_contract_tests editor_chooser_api_residual_tests)
endif()

if(TARGET cwxeditor_test_support)
    add_executable(editor_variant_creation_legacy_tests tests/apir_010_021_025_standalone_main.cpp)
    target_compile_features(editor_variant_creation_legacy_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_variant_creation_legacy_tests PRIVATE cwxeditor_test_support)
    # 引数で対象を指定しない場合は、ハーネスの3ケースすべてを実行する。
    add_test(NAME editor_variant_creation_legacy_tests COMMAND editor_variant_creation_legacy_tests)
    set_tests_properties(editor_variant_creation_legacy_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")
    add_dependencies(editor_gui_recovery_regression_suite editor_variant_creation_legacy_tests)
    add_custom_target(editor_gui_recovery_batch46 DEPENDS editor_variant_creation_legacy_tests editor_variable_state_contract_tests editor_variable_state_command_tests editor_gui_tab_navigation_tests editor_gui_recovery_root_tests editor_event_tree_drag_api_e43_tests)

    add_executable(editor_variable_history_apir_023_024_tests
        tests/editor_variable_history_candidate.cpp)
    target_compile_features(editor_variable_history_apir_023_024_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_variable_history_apir_023_024_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_variable_history_apir_023_024_tests
        COMMAND editor_variable_history_apir_023_024_tests APIR-023 APIR-024)
    set_tests_properties(editor_variable_history_apir_023_024_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_scene_apir_005_008_tests
        tests/editor_scene_apir_005_008_mainwindow_tests.cpp)
    target_compile_features(editor_scene_apir_005_008_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_scene_apir_005_008_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_scene_apir_005_008_tests COMMAND ${editor_scene_apir_005_008_tests_TEST_COMMAND})
    set_tests_properties(editor_scene_apir_005_008_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_pane_history_apir_002_013_tests
        tests/editor_pane_history_regression_tests.cpp)
    target_compile_features(editor_pane_history_apir_002_013_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_pane_history_apir_002_013_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_pane_history_apir_002_013_tests COMMAND ${editor_pane_history_apir_002_013_tests_TEST_COMMAND})
    set_tests_properties(editor_pane_history_apir_002_013_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    foreach(batch48_target IN ITEMS
            editor_variable_history_apir_023_024_tests
            editor_scene_apir_005_008_tests
            editor_pane_history_apir_002_013_tests)
        add_dependencies(editor_gui_recovery_regression_suite "${batch48_target}")
    endforeach()
    add_custom_target(editor_gui_recovery_batch48 DEPENDS
        editor_variant_creation_legacy_tests
        editor_variable_history_apir_023_024_tests
        editor_scene_apir_005_008_tests
        editor_pane_history_apir_002_013_tests)


endif()

# GUIの契約テスト。
if(TARGET cwxeditor_test_support)
    add_executable(editor_folder_history_apir_003_tests
        tests/editor_folder_history_selection_tests.cpp)
    target_compile_features(editor_folder_history_apir_003_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_folder_history_apir_003_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_folder_history_apir_003_tests COMMAND editor_folder_history_apir_003_tests)
    set_tests_properties(editor_folder_history_apir_003_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    add_executable(editor_comment_identity_apir_026_027_tests
        tests/editor_comment_identity_candidate.cpp)
    target_compile_features(editor_comment_identity_apir_026_027_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_comment_identity_apir_026_027_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_comment_identity_apir_026_027_tests COMMAND editor_comment_identity_apir_026_027_tests)
    set_tests_properties(editor_comment_identity_apir_026_027_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_scene_variable_apir_016_034_041_078_tests
        tests/apir_016_034_041_078_standalone_main.cpp)
    target_compile_features(editor_scene_variable_apir_016_034_041_078_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_scene_variable_apir_016_034_041_078_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_scene_variable_apir_016_034_041_078_tests COMMAND ${editor_scene_variable_apir_016_034_041_078_tests_TEST_COMMAND})
    set_tests_properties(editor_scene_variable_apir_016_034_041_078_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_directory_host_apir_049_050_068_tests
        tests/editor_directory_host_contracts_tests.cpp)
    target_compile_features(editor_directory_host_apir_049_050_068_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_directory_host_apir_049_050_068_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_directory_host_apir_049_050_068_tests COMMAND ${editor_directory_host_apir_049_050_068_tests_TEST_COMMAND})
    set_tests_properties(editor_directory_host_apir_049_050_068_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    add_executable(editor_import_selection_apir_003_tests
        tests/luna_batch50_import_selection_tests.cpp)
    target_compile_features(editor_import_selection_apir_003_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_import_selection_apir_003_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_import_selection_apir_003_tests COMMAND editor_import_selection_apir_003_tests)
    set_tests_properties(editor_import_selection_apir_003_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    foreach(nextbatch_target IN ITEMS
            editor_folder_history_apir_003_tests
            editor_comment_identity_apir_026_027_tests
            editor_scene_variable_apir_016_034_041_078_tests
            editor_directory_host_apir_049_050_068_tests
            editor_import_selection_apir_003_tests)
        add_dependencies(editor_gui_recovery_regression_suite "${nextbatch_target}")
    endforeach()

    add_custom_target(editor_gui_recovery_batch50 DEPENDS
        editor_folder_history_apir_003_tests
        editor_comment_identity_apir_026_027_tests
        editor_scene_variable_apir_016_034_041_078_tests
        editor_directory_host_apir_049_050_068_tests
        editor_import_selection_apir_003_tests
        editor_flag_history_contract_tests
        editor_shared_use_history_tests
        editor_history_autoapply_tests
        editor_flag_text_behavior_tests
        editor_variable_state_contract_tests
        editor_variable_state_command_tests
        editor_pane_history_apir_002_013_tests
        editor_ignore_directory_contract_tests
        editor_scene_contract_tests
        editor_scene_payload_contract_tests
        editor_scene_selection_recovered_tests
        editor_scene_hidden_selection_recovered_tests
        editor_scene_commands_recovered_tests
        editor_scene_route_residual_tests
        editor_scene_event_sync_repair_tests
        editor_scene_apir_005_008_tests)
endif()

# GUIの状態追従の回帰テスト。
if(TARGET cwxeditor_test_support)
    cardwirth_add_bundled_editor_test(editor_flag_following_apir_015_028_030_tests
        tests/editor_apir_015_028_030_candidate.cpp)
    target_compile_features(editor_flag_following_apir_015_028_030_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_flag_following_apir_015_028_030_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_flag_following_apir_015_028_030_tests COMMAND ${editor_flag_following_apir_015_028_030_tests_TEST_COMMAND})
    set_tests_properties(editor_flag_following_apir_015_028_030_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_flag_clipboard_apir_033_tests
        tests/editor_apir_033_candidate.cpp)
    target_compile_features(editor_flag_clipboard_apir_033_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_flag_clipboard_apir_033_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_flag_clipboard_apir_033_tests COMMAND ${editor_flag_clipboard_apir_033_tests_TEST_COMMAND})
    set_tests_properties(editor_flag_clipboard_apir_033_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_area_table_following_apir_042_047_tests
        tests/editor_apir_042_047_candidate.cpp)
    target_compile_features(editor_area_table_following_apir_042_047_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_area_table_following_apir_042_047_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_area_table_following_apir_042_047_tests COMMAND ${editor_area_table_following_apir_042_047_tests_TEST_COMMAND})
    set_tests_properties(editor_area_table_following_apir_042_047_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    foreach(batch52_target IN ITEMS
            editor_flag_following_apir_015_028_030_tests
            editor_flag_clipboard_apir_033_tests
            editor_area_table_following_apir_042_047_tests)
        add_dependencies(editor_gui_recovery_regression_suite "${batch52_target}")
    endforeach()

    add_custom_target(editor_gui_recovery_batch52 DEPENDS
        editor_flag_following_apir_015_028_030_tests
        editor_flag_clipboard_apir_033_tests
        editor_area_table_following_apir_042_047_tests
        editor_flag_history_contract_tests
        editor_flag_text_behavior_tests
        editor_variable_state_command_tests
        editor_gui_event_notifications_tests
        editor_pane_history_apir_002_013_tests
        editor_resource_clone_contract_tests
        editor_recovered_event_boundaries_tests
        editor_scene_contract_tests
        editor_scene_payload_contract_tests
        editor_scene_selection_recovered_tests
        editor_scene_hidden_selection_recovered_tests
        editor_scene_commands_recovered_tests
        editor_scene_route_residual_tests
        editor_scene_event_sync_repair_tests
        editor_scene_apir_005_008_tests
        editor_resource_deletion_snapshot_tests)
endif()

# 単体GUIの復旧回帰テスト。
if(TARGET cwxeditor_test_support)
    cardwirth_add_bundled_editor_test(editor_dialog_following_apir_017_018_082_083_084_085_tests
        tests/apir017_018_082_085_candidate.cpp)
    target_compile_features(editor_dialog_following_apir_017_018_082_083_084_085_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_dialog_following_apir_017_018_082_083_084_085_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_dialog_following_apir_017_018_082_083_084_085_tests
        COMMAND ${editor_dialog_following_apir_017_018_082_083_084_085_tests_TEST_COMMAND})
    set_tests_properties(editor_dialog_following_apir_017_018_082_083_084_085_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_mainwindow_toolbar_apir_020_tests
        tests/apir020_mainwindow_toolbar_candidate.cpp)
    target_compile_features(editor_mainwindow_toolbar_apir_020_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_mainwindow_toolbar_apir_020_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_mainwindow_toolbar_apir_020_tests
        COMMAND ${editor_mainwindow_toolbar_apir_020_tests_TEST_COMMAND})
    set_tests_properties(editor_mainwindow_toolbar_apir_020_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_search_apir032_040_host_tests
        tests/editor_search_apir032_040_host_tests.cpp)
    target_compile_features(editor_search_apir032_040_host_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_search_apir032_040_host_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_search_apir032_040_host_tests
        COMMAND ${editor_search_apir032_040_host_tests_TEST_COMMAND})
    set_tests_properties(editor_search_apir032_040_host_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_apir_086_variable_status_tests
        tests/editor_apir_086_variable_status_tests.cpp)
    target_compile_features(editor_apir_086_variable_status_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_apir_086_variable_status_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_apir_086_variable_status_tests
        COMMAND ${editor_apir_086_variable_status_tests_TEST_COMMAND})
    set_tests_properties(editor_apir_086_variable_status_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    foreach(batch54_target IN ITEMS
            editor_dialog_following_apir_017_018_082_083_084_085_tests
            editor_mainwindow_toolbar_apir_020_tests
            editor_search_apir032_040_host_tests
            editor_apir_086_variable_status_tests)
        add_dependencies(editor_gui_recovery_regression_suite "${batch54_target}")
    endforeach()

    add_custom_target(editor_gui_recovery_batch54 DEPENDS
        editor_dialog_following_apir_017_018_082_083_084_085_tests
        editor_mainwindow_toolbar_apir_020_tests
        editor_search_apir032_040_host_tests
        editor_apir_086_variable_status_tests
        editor_expression_dialog_recovery_tests
        editor_image_transfer_contract_tests
        editor_text_font_contract_tests
        editor_flag_history_contract_tests
        editor_flag_text_behavior_tests
        editor_variable_state_contract_tests
        editor_variable_state_command_tests
        editor_gui_event_notifications_tests
        editor_event_host_controls_tests
        editor_event_lifecycle_api_e76_e77_tests
        editor_recovered_event_boundaries_tests
        editor_search_public_helper_contract_tests
        editor_gui_search_candidate_api_e52_e53_e92_tests
        editor_gui_search_field_menus_api_e54_tests
        editor_gui_search_refresh_api_e81_e82_tests
        editor_gui_search_scope_notifications_tests)
endif()

# 表示中のAreaTable、クリップボード、プロパティの取り込み、ディレクトリの回帰テスト。
if(TARGET cwxeditor_test_support)
    cardwirth_add_bundled_editor_test(editor_live_area_table_style_apir_012_tests
        tests/apir_012_live_area_table_style_tests.cpp)
    target_compile_features(editor_live_area_table_style_apir_012_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_live_area_table_style_apir_012_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_live_area_table_style_apir_012_tests COMMAND ${editor_live_area_table_style_apir_012_tests_TEST_COMMAND})
    set_tests_properties(editor_live_area_table_style_apir_012_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_clipboard_public_formats_apir_014_tests
        tests/editor_clipboard_public_formats_tests.cpp)
    target_compile_features(editor_clipboard_public_formats_apir_014_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_clipboard_public_formats_apir_014_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_clipboard_public_formats_apir_014_tests COMMAND ${editor_clipboard_public_formats_apir_014_tests_TEST_COMMAND})
    set_tests_properties(editor_clipboard_public_formats_apir_014_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_gui_eventtreeview_grade45_repair_tests
        tests/editor_gui_eventtreeview_grade45_repair_tests.cpp)
    target_compile_features(editor_gui_eventtreeview_grade45_repair_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_gui_eventtreeview_grade45_repair_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_gui_eventtreeview_grade45_repair_tests COMMAND ${editor_gui_eventtreeview_grade45_repair_tests_TEST_COMMAND})
    set_tests_properties(editor_gui_eventtreeview_grade45_repair_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;targeted")

    cardwirth_add_bundled_editor_test(editor_gui_eventtreeview_phase2_tests
        tests/candidates/verified_20261023/eventtreeview_phase2/eventtreeview_phase2_candidate.cpp)
    target_compile_features(editor_gui_eventtreeview_phase2_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_gui_eventtreeview_phase2_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_gui_eventtreeview_phase2_tests COMMAND ${editor_gui_eventtreeview_phase2_tests_TEST_COMMAND})
    set_tests_properties(editor_gui_eventtreeview_phase2_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;targeted")

    cardwirth_add_bundled_editor_test(editor_gui_dutils17_shared_public_path_tests
        tests/candidates/dutils17_remaining_shared_public_path_candidate.cpp)
    target_compile_features(editor_gui_dutils17_shared_public_path_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_gui_dutils17_shared_public_path_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_gui_dutils17_shared_public_path_tests COMMAND ${editor_gui_dutils17_shared_public_path_tests_TEST_COMMAND})
    set_tests_properties(editor_gui_dutils17_shared_public_path_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;targeted")

    cardwirth_add_bundled_editor_test(editor_gui_materialselect_integrated_public_path_tests
        tests/editor_gui_materialselect_integrated_public_path_tests.cpp)
    target_compile_features(editor_gui_materialselect_integrated_public_path_tests PRIVATE cxx_std_20)
    target_compile_definitions(editor_gui_materialselect_integrated_public_path_tests PRIVATE
        CWXEDITOR_TEST_RESOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/cwxeditor_src/resource")
    target_link_libraries(editor_gui_materialselect_integrated_public_path_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_gui_materialselect_integrated_public_path_tests COMMAND ${editor_gui_materialselect_integrated_public_path_tests_TEST_COMMAND})
    set_tests_properties(editor_gui_materialselect_integrated_public_path_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;targeted")

    cardwirth_add_bundled_editor_test(editor_gui_clipboard_empty_text_tests
        tests/editor_gui_clipboard_empty_text_tests.cpp)
    target_compile_features(editor_gui_clipboard_empty_text_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_gui_clipboard_empty_text_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_gui_clipboard_empty_text_tests COMMAND ${editor_gui_clipboard_empty_text_tests_TEST_COMMAND})
    set_tests_properties(editor_gui_clipboard_empty_text_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;targeted")

    cardwirth_add_bundled_editor_test(editor_gui_editablelistview_phase2_public_path_tests
        tests/candidates/editablelistview_phase2_public_path_candidate.cpp)
    target_compile_features(editor_gui_editablelistview_phase2_public_path_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_gui_editablelistview_phase2_public_path_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_gui_editablelistview_phase2_public_path_tests COMMAND ${editor_gui_editablelistview_phase2_public_path_tests_TEST_COMMAND})
    set_tests_properties(editor_gui_editablelistview_phase2_public_path_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;targeted")

    cardwirth_add_bundled_editor_test(editor_gui_flagtable_phase2_public_path_tests
        tests/editor_gui_flagtable_phase2_public_path_tests.cpp)
    target_compile_features(editor_gui_flagtable_phase2_public_path_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_gui_flagtable_phase2_public_path_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_gui_flagtable_phase2_public_path_tests COMMAND ${editor_gui_flagtable_phase2_public_path_tests_TEST_COMMAND})
    set_tests_properties(editor_gui_flagtable_phase2_public_path_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;targeted")

    cardwirth_add_bundled_editor_test(editor_gui_customtoolbar_grade45_tests
        tests/editor_gui_customtoolbar_grade45_tests.cpp)
    target_compile_features(editor_gui_customtoolbar_grade45_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_gui_customtoolbar_grade45_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_gui_customtoolbar_grade45_tests COMMAND ${editor_gui_customtoolbar_grade45_tests_TEST_COMMAND})
    set_tests_properties(editor_gui_customtoolbar_grade45_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;targeted")

    cardwirth_add_bundled_editor_test(editor_grade3_dialog_state_tests
        tests/editor_grade3_dialog_state_tests.cpp)
    target_compile_features(editor_grade3_dialog_state_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_grade3_dialog_state_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_grade3_dialog_state_tests COMMAND ${editor_grade3_dialog_state_tests_TEST_COMMAND})
    set_tests_properties(editor_grade3_dialog_state_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;targeted")

    cardwirth_add_bundled_editor_test(editor_gui_flagtable_next_public_path_tests
        tests/editor_gui_flagtable_next_public_path_tests.cpp)
    target_compile_features(editor_gui_flagtable_next_public_path_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_gui_flagtable_next_public_path_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_gui_flagtable_next_public_path_tests COMMAND ${editor_gui_flagtable_next_public_path_tests_TEST_COMMAND})
    set_tests_properties(editor_gui_flagtable_next_public_path_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;targeted")

    cardwirth_add_bundled_editor_test(editor_gui_editablelistview_reveal_tests
        tests/editor_gui_editablelistview_reveal_candidate.cpp)
    target_compile_features(editor_gui_editablelistview_reveal_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_gui_editablelistview_reveal_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_gui_editablelistview_reveal_tests COMMAND ${editor_gui_editablelistview_reveal_tests_TEST_COMMAND})
    set_tests_properties(editor_gui_editablelistview_reveal_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;targeted")

    cardwirth_add_bundled_editor_test(editor_gui_areatable_grade45_repair_tests WITH_ARGS
        tests/editor_gui_areatable_grade45_repair_tests.cpp)
    target_compile_features(editor_gui_areatable_grade45_repair_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_gui_areatable_grade45_repair_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_gui_areatable_grade45_repair_tests COMMAND ${editor_gui_areatable_grade45_repair_tests_TEST_COMMAND})
    set_tests_properties(editor_gui_areatable_grade45_repair_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;targeted")

    cardwirth_add_bundled_editor_test(editor_gui_cardpane_grade45_repair_tests
        tests/editor_gui_cardpane_grade45_repair_tests.cpp)
    target_compile_features(editor_gui_cardpane_grade45_repair_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_gui_cardpane_grade45_repair_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_gui_cardpane_grade45_repair_tests COMMAND ${editor_gui_cardpane_grade45_repair_tests_TEST_COMMAND})
    set_tests_properties(editor_gui_cardpane_grade45_repair_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;targeted")

    cardwirth_add_bundled_editor_test(editor_gui_flagtable_grade45_repair_tests
        tests/editor_gui_flagtable_grade45_repair_tests.cpp)
    target_compile_features(editor_gui_flagtable_grade45_repair_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_gui_flagtable_grade45_repair_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_gui_flagtable_grade45_repair_tests COMMAND ${editor_gui_flagtable_grade45_repair_tests_TEST_COMMAND})
    set_tests_properties(editor_gui_flagtable_grade45_repair_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;targeted")

    cardwirth_add_bundled_editor_test(editor_gui_xmlcontent_grade45_repair_tests
        tests/editor_gui_xmlcontent_grade45_repair_tests.cpp)
    target_compile_features(editor_gui_xmlcontent_grade45_repair_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_gui_xmlcontent_grade45_repair_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_gui_xmlcontent_grade45_repair_tests COMMAND ${editor_gui_xmlcontent_grade45_repair_tests_TEST_COMMAND})
    set_tests_properties(editor_gui_xmlcontent_grade45_repair_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;targeted")

    cardwirth_add_bundled_editor_test(editor_gui_summarydialog_grade45_repair_tests
        tests/editor_gui_summarydialog_grade45_repair_tests.cpp)
    target_compile_features(editor_gui_summarydialog_grade45_repair_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_gui_summarydialog_grade45_repair_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_gui_summarydialog_grade45_repair_tests COMMAND ${editor_gui_summarydialog_grade45_repair_tests_TEST_COMMAND})
    set_tests_properties(editor_gui_summarydialog_grade45_repair_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;targeted")

    cardwirth_add_bundled_editor_test(editor_gui_smalldialogs_grade45_repair_tests
        tests/editor_gui_smalldialogs_grade45_repair_tests.cpp)
    target_compile_features(editor_gui_smalldialogs_grade45_repair_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_gui_smalldialogs_grade45_repair_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_gui_smalldialogs_grade45_repair_tests COMMAND ${editor_gui_smalldialogs_grade45_repair_tests_TEST_COMMAND})
    set_tests_properties(editor_gui_smalldialogs_grade45_repair_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;targeted")

    cardwirth_add_bundled_editor_test(editor_area_table_apir_035_host_tests
        tests/editor_area_table_apir_035_host_candidate.cpp)
    target_compile_features(editor_area_table_apir_035_host_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_area_table_apir_035_host_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_area_table_apir_035_host_tests COMMAND ${editor_area_table_apir_035_host_tests_TEST_COMMAND})
    set_tests_properties(editor_area_table_apir_035_host_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_import_area_properties_apir_048_tests
        tests/editor_import_area_properties_apir_048_tests.cpp)
    target_compile_features(editor_import_area_properties_apir_048_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_import_area_properties_apir_048_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_import_area_properties_apir_048_tests COMMAND ${editor_import_area_properties_apir_048_tests_TEST_COMMAND})
    set_tests_properties(editor_import_area_properties_apir_048_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_directory_edit_sort_apir_052_055_host_tests
        tests/editor_directory_edit_sort_apir_052_055_host_tests.cpp)
    target_compile_features(editor_directory_edit_sort_apir_052_055_host_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_directory_edit_sort_apir_052_055_host_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_directory_edit_sort_apir_052_055_host_tests COMMAND ${editor_directory_edit_sort_apir_052_055_host_tests_TEST_COMMAND})
    set_tests_properties(editor_directory_edit_sort_apir_052_055_host_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_directory_focus_archive_apir_073_074_host_tests
        tests/editor_directory_focus_archive_apir_073_074_host_tests.cpp)
    target_compile_features(editor_directory_focus_archive_apir_073_074_host_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_directory_focus_archive_apir_073_074_host_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_directory_focus_archive_apir_073_074_host_tests COMMAND ${editor_directory_focus_archive_apir_073_074_host_tests_TEST_COMMAND})
    set_tests_properties(editor_directory_focus_archive_apir_073_074_host_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    foreach(batch56_target IN ITEMS
            editor_live_area_table_style_apir_012_tests
            editor_clipboard_public_formats_apir_014_tests
            editor_area_table_apir_035_host_tests
            editor_import_area_properties_apir_048_tests
            editor_directory_edit_sort_apir_052_055_host_tests
            editor_directory_focus_archive_apir_073_074_host_tests)
        add_dependencies(editor_gui_recovery_regression_suite "${batch56_target}")
    endforeach()

    add_custom_target(editor_gui_recovery_batch56 DEPENDS
        editor_live_area_table_style_apir_012_tests
        editor_clipboard_public_formats_apir_014_tests
        editor_area_table_apir_035_host_tests
        editor_import_area_properties_apir_048_tests
        editor_directory_edit_sort_apir_052_055_host_tests
        editor_directory_focus_archive_apir_073_074_host_tests
        editor_area_table_following_apir_042_047_tests
        editor_flag_clipboard_apir_033_tests
        editor_flag_following_apir_015_028_030_tests
        editor_import_selection_apir_003_tests
        editor_directory_host_apir_049_050_068_tests
        editor_folder_history_apir_003_tests
        editor_pane_history_apir_002_013_tests
        editor_settings_message_contract_tests
        editor_gui_event_notifications_tests
        editor_flag_history_contract_tests
        editor_flag_text_behavior_tests
        editor_recovered_event_boundaries_tests)
endif()


# ColorPicker、AreaTable、シーンの複数選択、ディレクトリのフォーカス、フラグの回帰テスト。
if(TARGET cwxeditor_test_support)
    cardwirth_add_bundled_editor_test(editor_color_picker_alpha_apir_065_tests
        tests/editor_color_picker_alpha_candidate.cpp)
    target_compile_features(editor_color_picker_alpha_apir_065_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_color_picker_alpha_apir_065_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_color_picker_alpha_apir_065_tests COMMAND ${editor_color_picker_alpha_apir_065_tests_TEST_COMMAND})
    set_tests_properties(editor_color_picker_alpha_apir_065_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_area_table_apir_036_undo_selection_tests
        tests/editor_area_table_apir_036_undo_selection_candidate.cpp)
    target_compile_features(editor_area_table_apir_036_undo_selection_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_area_table_apir_036_undo_selection_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_area_table_apir_036_undo_selection_tests COMMAND ${editor_area_table_apir_036_undo_selection_tests_TEST_COMMAND})
    set_tests_properties(editor_area_table_apir_036_undo_selection_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_scene_multiselect_activation_apir_064_tests
        tests/APIR064_scene_multiselect_activation_candidate.cpp)
    target_compile_features(editor_scene_multiselect_activation_apir_064_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_scene_multiselect_activation_apir_064_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_scene_multiselect_activation_apir_064_tests COMMAND ${editor_scene_multiselect_activation_apir_064_tests_TEST_COMMAND})
    set_tests_properties(editor_scene_multiselect_activation_apir_064_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_flagdir_apir_077_079_080_081_tests
        tests/editor_flagdir_apir_077_079_080_081_candidate.cpp)
    target_compile_features(editor_flagdir_apir_077_079_080_081_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_flagdir_apir_077_079_080_081_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_flagdir_apir_077_079_080_081_tests COMMAND ${editor_flagdir_apir_077_079_080_081_tests_TEST_COMMAND})
    set_tests_properties(editor_flagdir_apir_077_079_080_081_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_apir_087_089_091_tests
        tests/editor_apir_087_089_091_candidate.cpp)
    target_compile_features(editor_apir_087_089_091_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_apir_087_089_091_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_apir_087_089_091_tests COMMAND ${editor_apir_087_089_091_tests_TEST_COMMAND})
    set_tests_properties(editor_apir_087_089_091_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    foreach(batch58_target IN ITEMS
            editor_color_picker_alpha_apir_065_tests
            editor_area_table_apir_036_undo_selection_tests
            editor_scene_multiselect_activation_apir_064_tests
            editor_flagdir_apir_077_079_080_081_tests
            editor_apir_087_089_091_tests)
        add_dependencies(editor_gui_recovery_regression_suite "${batch58_target}")
    endforeach()

    add_custom_target(editor_gui_recovery_batch58 DEPENDS
        editor_color_picker_alpha_apir_065_tests
        editor_area_table_apir_036_undo_selection_tests
        editor_scene_multiselect_activation_apir_064_tests
        editor_flagdir_apir_077_079_080_081_tests
        editor_apir_087_089_091_tests
        editor_area_table_following_apir_042_047_tests
        editor_flag_clipboard_apir_033_tests
        editor_flag_following_apir_015_028_030_tests
        editor_import_selection_apir_003_tests
        editor_directory_host_apir_049_050_068_tests
        editor_folder_history_apir_003_tests
        editor_pane_history_apir_002_013_tests
        editor_settings_message_contract_tests
        editor_gui_event_notifications_tests
        editor_flag_history_contract_tests
        editor_flag_text_behavior_tests
        editor_recovered_event_boundaries_tests
        editor_png_alpha_recovery_tests
        editor_image_transfer_contract_tests
        editor_scene_apir_005_008_tests
        editor_scene_contract_tests
        editor_scene_deletion_snapshot_tests
        editor_scene_payload_contract_tests
        editor_scene_selection_recovered_tests
        editor_scene_route_residual_tests
        editor_scene_hidden_selection_recovered_tests
        editor_scene_commands_recovered_tests
        editor_scene_event_sync_repair_tests)
endif()


# 追加のGUI回帰テストを登録する。
if(TARGET cwxeditor_test_support)
    cardwirth_add_bundled_editor_test(editor_area_view_apir_057_063_scoped_tests
        tests/candidates/editor_area_view_apir_057_063_scoped_candidate.cpp)
    target_compile_features(editor_area_view_apir_057_063_scoped_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_area_view_apir_057_063_scoped_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_area_view_apir_057_063_scoped_tests COMMAND ${editor_area_view_apir_057_063_scoped_tests_TEST_COMMAND})
    set_tests_properties(editor_area_view_apir_057_063_scoped_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_directory_material_paths_apir_069_070_075_tests
        tests/candidates/editor_directory_material_paths_apir_069_070_075_tests.cpp)
    target_compile_features(editor_directory_material_paths_apir_069_070_075_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_directory_material_paths_apir_069_070_075_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_directory_material_paths_apir_069_070_075_tests COMMAND ${editor_directory_material_paths_apir_069_070_075_tests_TEST_COMMAND})
    set_tests_properties(editor_directory_material_paths_apir_069_070_075_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_image_skeleton_apir_051_host_tests
        tests/candidates/image_skeleton_host_test.cpp)
    target_compile_features(editor_image_skeleton_apir_051_host_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_image_skeleton_apir_051_host_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_image_skeleton_apir_051_host_tests COMMAND ${editor_image_skeleton_apir_051_host_tests_TEST_COMMAND})
    set_tests_properties(editor_image_skeleton_apir_051_host_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_mainwindow_skeleton_binding_apir_051_host_tests
        tests/candidates/mainwindow_skeleton_binding_host_test.cpp)
    target_compile_features(editor_mainwindow_skeleton_binding_apir_051_host_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_mainwindow_skeleton_binding_apir_051_host_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_mainwindow_skeleton_binding_apir_051_host_tests COMMAND ${editor_mainwindow_skeleton_binding_apir_051_host_tests_TEST_COMMAND})
    set_tests_properties(editor_mainwindow_skeleton_binding_apir_051_host_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_directory_warnings_apir_067_host_tests
        tests/candidates/editor_directory_warnings_apir_067_host_tests.cpp)
    target_compile_features(editor_directory_warnings_apir_067_host_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_directory_warnings_apir_067_host_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_directory_warnings_apir_067_host_tests COMMAND ${editor_directory_warnings_apir_067_host_tests_TEST_COMMAND})
    set_tests_properties(editor_directory_warnings_apir_067_host_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_area_table_apir_096_unicode_sort_tests
        tests/candidates/editor_area_table_apir_096_unicode_sort_candidate.cpp)
    target_compile_features(editor_area_table_apir_096_unicode_sort_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_area_table_apir_096_unicode_sort_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_area_table_apir_096_unicode_sort_tests COMMAND ${editor_area_table_apir_096_unicode_sort_tests_TEST_COMMAND})
    set_tests_properties(editor_area_table_apir_096_unicode_sort_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_event_duplicate_apir066_apir076_tests
        tests/candidates/editor_event_duplicate_apir066_apir076_candidate.cpp)
    target_compile_features(editor_event_duplicate_apir066_apir076_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_event_duplicate_apir066_apir076_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_event_duplicate_apir066_apir076_tests COMMAND ${editor_event_duplicate_apir066_apir076_tests_TEST_COMMAND})
    set_tests_properties(editor_event_duplicate_apir066_apir076_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")


    # 複数の契約を5つのプログラムで検証する。
    cardwirth_add_bundled_editor_test(editor_area_view_apir_059_060_061_062_tests
        tests/candidates/editor_apir_059_060_061_062_candidate.cpp)
    target_compile_features(editor_area_view_apir_059_060_061_062_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_area_view_apir_059_060_061_062_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_area_view_apir_059_060_061_062_tests COMMAND ${editor_area_view_apir_059_060_061_062_tests_TEST_COMMAND})
    set_tests_properties(editor_area_view_apir_059_060_061_062_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_variant_hint_apir_088_tests
        tests/candidates/editor_apir_088_variant_value_hint_tests.cpp)
    target_compile_features(editor_variant_hint_apir_088_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_variant_hint_apir_088_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_variant_hint_apir_088_tests COMMAND ${editor_variant_hint_apir_088_tests_TEST_COMMAND})
    set_tests_properties(editor_variant_hint_apir_088_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_flag_table_apir_090_reveal_tests
        tests/candidates/editor_flag_table_apir_090_reveal_tests.cpp)
    target_compile_features(editor_flag_table_apir_090_reveal_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_flag_table_apir_090_reveal_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_flag_table_apir_090_reveal_tests COMMAND ${editor_flag_table_apir_090_reveal_tests_TEST_COMMAND})
    set_tests_properties(editor_flag_table_apir_090_reveal_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_material_reference_apir_071_072_092_tests
        tests/candidates/editor_material_reference_apir_071_072_092_tests.cpp)
    target_compile_features(editor_material_reference_apir_071_072_092_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_material_reference_apir_071_072_092_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_material_reference_apir_071_072_092_tests COMMAND ${editor_material_reference_apir_071_072_092_tests_TEST_COMMAND})
    set_tests_properties(editor_material_reference_apir_071_072_092_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_eventview_template_apir_095_tests
        tests/candidates/apir095_eventview_template_mnemonic_tests.cpp)
    target_compile_features(editor_eventview_template_apir_095_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_eventview_template_apir_095_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_eventview_template_apir_095_tests COMMAND ${editor_eventview_template_apir_095_tests_TEST_COMMAND})
    set_tests_properties(editor_eventview_template_apir_095_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    foreach(next10_target IN ITEMS
            editor_area_view_apir_057_063_scoped_tests
            editor_directory_material_paths_apir_069_070_075_tests
            editor_image_skeleton_apir_051_host_tests
            editor_mainwindow_skeleton_binding_apir_051_host_tests
            editor_directory_warnings_apir_067_host_tests
            editor_area_table_apir_096_unicode_sort_tests
            editor_event_duplicate_apir066_apir076_tests
            editor_area_view_apir_059_060_061_062_tests
            editor_variant_hint_apir_088_tests
            editor_flag_table_apir_090_reveal_tests
            editor_material_reference_apir_071_072_092_tests
            editor_eventview_template_apir_095_tests)
        add_dependencies(editor_gui_recovery_regression_suite "${next10_target}")
    endforeach()

    add_custom_target(editor_gui_recovery_next10 DEPENDS
        editor_area_view_apir_057_063_scoped_tests
        editor_directory_material_paths_apir_069_070_075_tests
        editor_image_skeleton_apir_051_host_tests
        editor_mainwindow_skeleton_binding_apir_051_host_tests
        editor_directory_warnings_apir_067_host_tests
        editor_area_table_apir_096_unicode_sort_tests
        editor_event_duplicate_apir066_apir076_tests
        editor_area_view_apir_059_060_061_062_tests
        editor_variant_hint_apir_088_tests
        editor_flag_table_apir_090_reveal_tests
        editor_material_reference_apir_071_072_092_tests
        editor_eventview_template_apir_095_tests
    )

    # カードとエリアの通知に関する回帰テストのターゲット。
    cardwirth_add_bundled_editor_test(editor_gui_card_interactions_008_009_012_016_017_tests
        tests/candidates/editor_gui_card_interactions_008_009_012_016_017_tests.cpp)
    target_compile_features(editor_gui_card_interactions_008_009_012_016_017_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_gui_card_interactions_008_009_012_016_017_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_gui_card_interactions_008_009_012_016_017_tests COMMAND ${editor_gui_card_interactions_008_009_012_016_017_tests_TEST_COMMAND})
    set_tests_properties(editor_gui_card_interactions_008_009_012_016_017_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")
    add_dependencies(editor_gui_recovery_regression_suite editor_gui_card_interactions_008_009_012_016_017_tests)

    cardwirth_add_bundled_editor_test(editor_area_notifications_001_003_007_008_009_tests
        tests/candidates/editor_area_notifications_001_003_007_008_009_tests.cpp)
    target_compile_features(editor_area_notifications_001_003_007_008_009_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_area_notifications_001_003_007_008_009_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_area_notifications_001_003_007_008_009_tests COMMAND ${editor_area_notifications_001_003_007_008_009_tests_TEST_COMMAND})
    set_tests_properties(editor_area_notifications_001_003_007_008_009_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")
    add_dependencies(editor_gui_recovery_regression_suite editor_area_notifications_001_003_007_008_009_tests)

    add_custom_target(editor_gui_recovery_card_area_next10
        DEPENDS editor_gui_card_interactions_008_009_012_016_017_tests
                editor_area_notifications_001_003_007_008_009_tests)
endif()

# GUI回帰テストの追加候補。適用先の基準版はマニフェストを参照。
if(TARGET cwxeditor_test_support)
    cardwirth_add_bundled_editor_test(editor_toolbar_wrap_persistence_apir_093_tests
        tests/candidates/following_ten/queue_toolbar_settings/apir093_mainwindow_wrap_candidate.cpp)
    target_compile_features(editor_toolbar_wrap_persistence_apir_093_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_toolbar_wrap_persistence_apir_093_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_toolbar_wrap_persistence_apir_093_tests COMMAND ${editor_toolbar_wrap_persistence_apir_093_tests_TEST_COMMAND})
    set_tests_properties(editor_toolbar_wrap_persistence_apir_093_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_area_clip_display_apir_004_006_tests
        tests/candidates/following_ten/review_area_clip_display/editor_area_clip_display_focused_tests.cpp)
    target_compile_features(editor_area_clip_display_apir_004_006_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_area_clip_display_apir_004_006_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_area_clip_display_apir_004_006_tests COMMAND ${editor_area_clip_display_apir_004_006_tests_TEST_COMMAND})
    set_tests_properties(editor_area_clip_display_apir_004_006_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_area_duplicate_routing_apir_002_tests
        tests/candidates/following_ten/review_area_duplicate/editor_area_api_002_duplicate_event_routing_tests.cpp)
    target_compile_features(editor_area_duplicate_routing_apir_002_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_area_duplicate_routing_apir_002_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_area_duplicate_routing_apir_002_tests COMMAND ${editor_area_duplicate_routing_apir_002_tests_TEST_COMMAND})
    set_tests_properties(editor_area_duplicate_routing_apir_002_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_bg_reference_counter_apir_005_tests
        tests/candidates/following_ten/queue_area_notifications/editor_area_api_005_bg_reference_local_variable_test.cpp)
    target_compile_features(editor_bg_reference_counter_apir_005_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_bg_reference_counter_apir_005_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_bg_reference_counter_apir_005_tests COMMAND ${editor_bg_reference_counter_apir_005_tests_TEST_COMMAND})
    set_tests_properties(editor_bg_reference_counter_apir_005_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_flag_step_special_preview_apir_022_tests
        tests/candidates/following_ten/queue_toolbar_settings/editor_flag_step_special_preview_candidate.cpp)
    target_compile_features(editor_flag_step_special_preview_apir_022_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_flag_step_special_preview_apir_022_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_flag_step_special_preview_apir_022_tests COMMAND ${editor_flag_step_special_preview_apir_022_tests_TEST_COMMAND})
    set_tests_properties(editor_flag_step_special_preview_apir_022_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_scene_insert_apir_038_tests
        tests/candidates/following_ten/root_scene_insert038/editor_scene_insert_apir_038_headless_tests.cpp)
    target_compile_features(editor_scene_insert_apir_038_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_scene_insert_apir_038_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_scene_insert_apir_038_tests COMMAND ${editor_scene_insert_apir_038_tests_TEST_COMMAND})
    set_tests_properties(editor_scene_insert_apir_038_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_card_preview_interaction_apir_010_tests
        tests/candidates/following_ten/review_card_interactions/card_interaction_010_preview_candidate.cpp)
    target_compile_features(editor_card_preview_interaction_apir_010_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_card_preview_interaction_apir_010_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_card_preview_interaction_apir_010_tests COMMAND ${editor_card_preview_interaction_apir_010_tests_TEST_COMMAND})
    set_tests_properties(editor_card_preview_interaction_apir_010_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    # 実際のプリセット画像をコピーするフィクスチャ。インストール済みリソース
    # の横でテストを起動しなくても、ソース側の素材を見つけられるようにする。
    foreach(resource_fixture IN ITEMS
            editor_gui_card_interactions_008_009_012_016_017_tests
            editor_card_preview_interaction_apir_010_tests)
        target_compile_definitions(${resource_fixture} PRIVATE
            CWXEDITOR_TEST_RESOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/cwxeditor_src/resource")
    endforeach()

    cardwirth_add_bundled_editor_test(editor_card_comment_title_repltext_apir_015_tests
        tests/candidates/following_ten/root_card_comment015/editor_card_comment_title_repltext_tests.cpp)
    target_compile_features(editor_card_comment_title_repltext_apir_015_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_card_comment_title_repltext_apir_015_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_card_comment_title_repltext_apir_015_tests COMMAND ${editor_card_comment_title_repltext_apir_015_tests_TEST_COMMAND})
    set_tests_properties(editor_card_comment_title_repltext_apir_015_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_sound_preview_exclusivity_apir_013_tests
        tests/candidates/following_ten/root_sound_preview013/editor_sound_preview_exclusivity_tests.cpp)
    target_compile_features(editor_sound_preview_exclusivity_apir_013_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_sound_preview_exclusivity_apir_013_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_sound_preview_exclusivity_apir_013_tests COMMAND ${editor_sound_preview_exclusivity_apir_013_tests_TEST_COMMAND})
    set_tests_properties(editor_sound_preview_exclusivity_apir_013_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    foreach(following_ten_target IN ITEMS
            editor_toolbar_wrap_persistence_apir_093_tests
            editor_area_clip_display_apir_004_006_tests
            editor_area_duplicate_routing_apir_002_tests
            editor_bg_reference_counter_apir_005_tests
            editor_flag_step_special_preview_apir_022_tests
            editor_scene_insert_apir_038_tests
            editor_card_preview_interaction_apir_010_tests
            editor_card_comment_title_repltext_apir_015_tests
            editor_sound_preview_exclusivity_apir_013_tests)
        add_dependencies(editor_gui_recovery_regression_suite "${following_ten_target}")
    endforeach()

    add_custom_target(editor_gui_recovery_following_ten
        DEPENDS
            editor_toolbar_wrap_persistence_apir_093_tests
            editor_area_clip_display_apir_004_006_tests
            editor_area_duplicate_routing_apir_002_tests
            editor_bg_reference_counter_apir_005_tests
            editor_flag_step_special_preview_apir_022_tests
            editor_scene_insert_apir_038_tests
            editor_card_preview_interaction_apir_010_tests
            editor_card_comment_title_repltext_apir_015_tests
            editor_sound_preview_exclusivity_apir_013_tests)
endif()

# 召喚獣カードの回帰テスト追加候補。
if(TARGET cwxeditor_test_support)
    cardwirth_add_bundled_editor_test(editor_effect_dialog_beast_skin_apir_014_tests
        tests/candidates/nextbatch/CARDS-014/editor_effect_dialog_beast_skin_tests.cpp)
    target_compile_features(editor_effect_dialog_beast_skin_apir_014_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_effect_dialog_beast_skin_apir_014_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_effect_dialog_beast_skin_apir_014_tests COMMAND ${editor_effect_dialog_beast_skin_apir_014_tests_TEST_COMMAND})
    set_tests_properties(editor_effect_dialog_beast_skin_apir_014_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_motion_beast_canvas_enter_apir_002_tests
        tests/candidates/nextbatch/CARDS-API-002/editor_motion_beast_canvas_enter_tests.cpp)
    target_compile_features(editor_motion_beast_canvas_enter_apir_002_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_motion_beast_canvas_enter_apir_002_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_motion_beast_canvas_enter_apir_002_tests COMMAND ${editor_motion_beast_canvas_enter_apir_002_tests_TEST_COMMAND})
    set_tests_properties(editor_motion_beast_canvas_enter_apir_002_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_cards_api_026_navigation_host_tests
        tests/candidates/nextbatch/CARDS-API-026/editor_cards_api_026_candidate.cpp)
    target_compile_features(editor_cards_api_026_navigation_host_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_cards_api_026_navigation_host_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_cards_api_026_navigation_host_tests COMMAND ${editor_cards_api_026_navigation_host_tests_TEST_COMMAND})
    set_tests_properties(editor_cards_api_026_navigation_host_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_cards_api_027_material_host_tests
        tests/candidates/nextbatch/CARDS-API-027/editor_cards_api_027_candidate.cpp)
    target_compile_features(editor_cards_api_027_material_host_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_cards_api_027_material_host_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_cards_api_027_material_host_tests COMMAND ${editor_cards_api_027_material_host_tests_TEST_COMMAND})
    set_tests_properties(editor_cards_api_027_material_host_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_cards_api_032_result_navigation_host_tests
        tests/candidates/nextbatch/CARDS-API-032/editor_cards_api_032_candidate.cpp)
    target_compile_features(editor_cards_api_032_result_navigation_host_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_cards_api_032_result_navigation_host_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_cards_api_032_result_navigation_host_tests COMMAND ${editor_cards_api_032_result_navigation_host_tests_TEST_COMMAND})
    set_tests_properties(editor_cards_api_032_result_navigation_host_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_cards_api034_source_warning_host_tests
        tests/candidates/nextbatch/CARDS-API-034/editor_cards_api034_source_warning_host_tests.cpp)
    target_compile_features(editor_cards_api034_source_warning_host_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_cards_api034_source_warning_host_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_cards_api034_source_warning_host_tests COMMAND ${editor_cards_api034_source_warning_host_tests_TEST_COMMAND})
    set_tests_properties(editor_cards_api034_source_warning_host_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_cards012_search_menu_host_tests
        tests/candidates/nextbatch/CARDS-012/editor_cards012_search_menu_host_tests.cpp)
    target_compile_features(editor_cards012_search_menu_host_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_cards012_search_menu_host_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_cards012_search_menu_host_tests COMMAND ${editor_cards012_search_menu_host_tests_TEST_COMMAND})
    set_tests_properties(editor_cards012_search_menu_host_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_cards_api005_scene_alpha_host_tests
        tests/candidates/nextbatch/CARDS-API-005/cards_api005_scene_alpha_host_tests.cpp)
    target_compile_features(editor_cards_api005_scene_alpha_host_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_cards_api005_scene_alpha_host_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_cards_api005_scene_alpha_host_tests COMMAND ${editor_cards_api005_scene_alpha_host_tests_TEST_COMMAND})
    set_tests_properties(editor_cards_api005_scene_alpha_host_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    # カードAPIの一部は実行ファイルを追加せず、既存のテストプログラムを拡張する。
    foreach(beast_next10_target IN ITEMS
            editor_effect_dialog_beast_skin_apir_014_tests
            editor_motion_beast_canvas_enter_apir_002_tests
            editor_cards_api_026_navigation_host_tests
            editor_cards_api_027_material_host_tests
            editor_cards_api_032_result_navigation_host_tests
            editor_cards_api034_source_warning_host_tests
            editor_cards012_search_menu_host_tests
            editor_cards_api005_scene_alpha_host_tests)
        add_dependencies(editor_gui_recovery_regression_suite "${beast_next10_target}")
    endforeach()

    add_custom_target(editor_gui_recovery_beast_card_next10
        DEPENDS
            editor_effect_dialog_beast_skin_apir_014_tests
            editor_motion_beast_canvas_enter_apir_002_tests
            editor_gui_card_recovery_batch05_tests
            editor_cards_api_026_navigation_host_tests
            editor_cards_api_027_material_host_tests
            editor_cards_api_032_result_navigation_host_tests
            editor_cards_api034_source_warning_host_tests
            editor_cards012_search_menu_host_tests
            cwxeditor_tests
            editor_cards_api005_scene_alpha_host_tests)
endif()

# メイン画面とイベントグラフの回帰テスト追加候補。
if(TARGET cwxeditor_test_support)
    cardwirth_add_bundled_editor_test(editor_main_window_ledger_001_008_tests
        tests/candidates/main_graph_next10/editor_main_window_ledger_001_008_candidate.cpp)
    target_compile_features(editor_main_window_ledger_001_008_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_main_window_ledger_001_008_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_main_window_ledger_001_008_tests COMMAND ${editor_main_window_ledger_001_008_tests_TEST_COMMAND})
    set_tests_properties(editor_main_window_ledger_001_008_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_scene_middle_overlap_cards_api006_tests
        tests/candidates/main_graph_next10/editor_scene_middle_overlap_api006_tests.cpp)
    target_compile_features(editor_scene_middle_overlap_cards_api006_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_scene_middle_overlap_cards_api006_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_scene_middle_overlap_cards_api006_tests COMMAND ${editor_scene_middle_overlap_cards_api006_tests_TEST_COMMAND})
    set_tests_properties(editor_scene_middle_overlap_cards_api006_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_event_graph_settings_e07_tests
        tests/candidates/main_graph_next10/editor_event_graph_settings_e07_tests.cpp)
    target_compile_features(editor_event_graph_settings_e07_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_event_graph_settings_e07_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_event_graph_settings_e07_tests COMMAND ${editor_event_graph_settings_e07_tests_TEST_COMMAND})
    set_tests_properties(editor_event_graph_settings_e07_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    foreach(main_graph_next10_target IN ITEMS
            editor_main_window_ledger_001_008_tests
            editor_scene_middle_overlap_cards_api006_tests
            editor_event_graph_settings_e07_tests
    )
        add_dependencies(editor_gui_recovery_regression_suite "${main_graph_next10_target}")
    endforeach()

    add_custom_target(editor_gui_recovery_main_graph_next10
        DEPENDS
            editor_main_window_ledger_001_008_tests
            editor_scene_middle_overlap_cards_api006_tests
            editor_event_graph_settings_e07_tests
    )
endif()

# 変数ダイアログのROOTと共通再開処理の回帰テスト追加候補。
if(TARGET cwxeditor_test_support)
    cardwirth_add_bundled_editor_test(editor_root_001_variable_natural_sort_tests
        tests/candidates/variable_dialog_next10/root_001_natural_sort_candidate.cpp)
    target_compile_features(editor_root_001_variable_natural_sort_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_root_001_variable_natural_sort_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_root_001_variable_natural_sort_tests COMMAND ${editor_root_001_variable_natural_sort_tests_TEST_COMMAND})
    set_tests_properties(editor_root_001_variable_natural_sort_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_root_009_property_warnings_tests
        tests/candidates/variable_dialog_next10/root_009_property_warnings_candidate.cpp)
    target_compile_features(editor_root_009_property_warnings_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_root_009_property_warnings_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_root_009_property_warnings_tests COMMAND ${editor_root_009_property_warnings_tests_TEST_COMMAND})
    set_tests_properties(editor_root_009_property_warnings_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_root_010_multi_property_dialogs_tests
        tests/candidates/variable_dialog_next10/root_010_multi_property_dialogs_candidate.cpp)
    target_compile_features(editor_root_010_multi_property_dialogs_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_root_010_multi_property_dialogs_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_root_010_multi_property_dialogs_tests COMMAND ${editor_root_010_multi_property_dialogs_tests_TEST_COMMAND})
    set_tests_properties(editor_root_010_multi_property_dialogs_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_root_011_clone_clipboard_tests
        tests/candidates/variable_dialog_next10/root_011_clone_clipboard_candidate.cpp)
    target_compile_features(editor_root_011_clone_clipboard_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_root_011_clone_clipboard_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_root_011_clone_clipboard_tests COMMAND ${editor_root_011_clone_clipboard_tests_TEST_COMMAND})
    set_tests_properties(editor_root_011_clone_clipboard_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_dialog_edges_root003_shared_resume_tests
        tests/candidates/variable_dialog_next10/dialog_edges_next_tests.cpp)
    target_compile_features(editor_dialog_edges_root003_shared_resume_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_dialog_edges_root003_shared_resume_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_dialog_edges_root003_shared_resume_tests COMMAND ${editor_dialog_edges_root003_shared_resume_tests_TEST_COMMAND})
    set_tests_properties(editor_dialog_edges_root003_shared_resume_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    foreach(variable_dialog_next10_target IN ITEMS
            editor_root_001_variable_natural_sort_tests
            editor_root_009_property_warnings_tests
            editor_root_010_multi_property_dialogs_tests
            editor_root_011_clone_clipboard_tests
            editor_dialog_edges_root003_shared_resume_tests
    )
        add_dependencies(editor_gui_recovery_regression_suite "${variable_dialog_next10_target}")
    endforeach()

    add_custom_target(editor_gui_recovery_variable_dialog_next10
        DEPENDS
            editor_root_001_variable_natural_sort_tests
            editor_root_009_property_warnings_tests
            editor_root_010_multi_property_dialogs_tests
            editor_root_011_clone_clipboard_tests
            editor_dialog_edges_root003_shared_resume_tests
    )
endif()


# イベント追従と共通再開処理の回帰テスト追加候補。
if(TARGET cwxeditor_test_support)
    cardwirth_add_bundled_editor_test(editor_event_graph_following_next10_tests
        tests/candidates/event_next10/editor_event_graph_following_candidate.cpp)
    target_compile_features(editor_event_graph_following_next10_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_event_graph_following_next10_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_event_graph_following_next10_tests COMMAND ${editor_event_graph_following_next10_tests_TEST_COMMAND})
    set_tests_properties(editor_event_graph_following_next10_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_incremental_search_following_next10_tests
        tests/candidates/event_next10/editor_incremental_search_following_candidate.cpp)
    target_compile_features(editor_incremental_search_following_next10_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_incremental_search_following_next10_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_incremental_search_following_next10_tests COMMAND ${editor_incremental_search_following_next10_tests_TEST_COMMAND})
    set_tests_properties(editor_incremental_search_following_next10_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_event_dialog_shared_next10_tests
        tests/candidates/event_next10/editor_event_dialog_shared_candidate.cpp)
    target_compile_features(editor_event_dialog_shared_next10_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_event_dialog_shared_next10_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_event_dialog_shared_next10_tests COMMAND ${editor_event_dialog_shared_next10_tests_TEST_COMMAND})
    set_tests_properties(editor_event_dialog_shared_next10_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_connected_resource_reveal_026_next10_tests
        tests/candidates/event_next10/editor_connected_resource_reveal_026_candidate.cpp)
    target_compile_features(editor_connected_resource_reveal_026_next10_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_connected_resource_reveal_026_next10_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_connected_resource_reveal_026_next10_tests COMMAND ${editor_connected_resource_reveal_026_next10_tests_TEST_COMMAND})
    set_tests_properties(editor_connected_resource_reveal_026_next10_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    foreach(event_next10_target IN ITEMS
            editor_event_graph_following_next10_tests
            editor_incremental_search_following_next10_tests
            editor_event_dialog_shared_next10_tests
            editor_connected_resource_reveal_026_next10_tests
    )
        add_dependencies(editor_gui_recovery_regression_suite "${event_next10_target}")
    endforeach()

    add_custom_target(editor_gui_recovery_event_next10
        DEPENDS
            editor_event_graph_following_next10_tests
            editor_incremental_search_following_next10_tests
            editor_event_dialog_shared_next10_tests
            editor_connected_resource_reveal_026_next10_tests
    )
endif()


# ダイアログ表示調整の回帰テスト追加候補。
if(TARGET cwxeditor_test_support)
    cardwirth_add_bundled_editor_test(editor_dialog_polish_script_diagnostics0607_tests
        tests/candidates/dialog_polish_next10/editor_dialog_polish0607_native_host_candidate.cpp)
    target_compile_features(editor_dialog_polish_script_diagnostics0607_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_dialog_polish_script_diagnostics0607_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_dialog_polish_script_diagnostics0607_tests COMMAND ${editor_dialog_polish_script_diagnostics0607_tests_TEST_COMMAND})
    set_tests_properties(editor_dialog_polish_script_diagnostics0607_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_dialog_polish_enhancement_background001_tests
        tests/candidates/dialog_polish_next10/editor_dialog_polish_enhance001_native_candidate.cpp)
    target_compile_features(editor_dialog_polish_enhancement_background001_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_dialog_polish_enhancement_background001_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_dialog_polish_enhancement_background001_tests COMMAND ${editor_dialog_polish_enhancement_background001_tests_TEST_COMMAND})
    set_tests_properties(editor_dialog_polish_enhancement_background001_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_dialog_polish_keycode_rename_columns1112_tests
        tests/candidates/dialog_polish_next10/editor_dialog_polish_keycode1112_native_candidate.cpp)
    target_compile_features(editor_dialog_polish_keycode_rename_columns1112_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_dialog_polish_keycode_rename_columns1112_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_dialog_polish_keycode_rename_columns1112_tests COMMAND ${editor_dialog_polish_keycode_rename_columns1112_tests_TEST_COMMAND})
    set_tests_properties(editor_dialog_polish_keycode_rename_columns1112_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_dialog_polish_skin_selections010_tests
        tests/candidates/dialog_polish_next10/editor_dialog_polish_skin_selections010_native_candidate.cpp)
    target_compile_features(editor_dialog_polish_skin_selections010_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_dialog_polish_skin_selections010_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_dialog_polish_skin_selections010_tests COMMAND ${editor_dialog_polish_skin_selections010_tests_TEST_COMMAND})
    set_tests_properties(editor_dialog_polish_skin_selections010_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_dialog_polish_talker_crop003_tests
        tests/candidates/dialog_polish_next10/editor_dialog_polish_talker_crop003_candidate.cpp)
    target_compile_features(editor_dialog_polish_talker_crop003_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_dialog_polish_talker_crop003_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_dialog_polish_talker_crop003_tests COMMAND ${editor_dialog_polish_talker_crop003_tests_TEST_COMMAND})
    set_tests_properties(editor_dialog_polish_talker_crop003_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_dialog_polish_timebar_origin013_tests
        tests/candidates/dialog_polish_next10/editor_dialog_polish_timebar013_native_candidate.cpp)
    target_compile_features(editor_dialog_polish_timebar_origin013_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_dialog_polish_timebar_origin013_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_dialog_polish_timebar_origin013_tests COMMAND ${editor_dialog_polish_timebar_origin013_tests_TEST_COMMAND})
    set_tests_properties(editor_dialog_polish_timebar_origin013_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_dialog_polish_tooltips094_tests
        tests/candidates/dialog_polish_next10/editor_dialog_polish_tooltips094_native_candidate.cpp)
    target_compile_features(editor_dialog_polish_tooltips094_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_dialog_polish_tooltips094_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_dialog_polish_tooltips094_tests COMMAND ${editor_dialog_polish_tooltips094_tests_TEST_COMMAND})
    set_tests_properties(editor_dialog_polish_tooltips094_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_dialog_polish_unknown_skin009_tests
        tests/candidates/dialog_polish_next10/editor_dialog_polish_unknown_skin009_native_candidate.cpp)
    target_compile_features(editor_dialog_polish_unknown_skin009_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_dialog_polish_unknown_skin009_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_dialog_polish_unknown_skin009_tests COMMAND ${editor_dialog_polish_unknown_skin009_tests_TEST_COMMAND})
    set_tests_properties(editor_dialog_polish_unknown_skin009_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    foreach(dialog_polish_next10_target IN ITEMS
            editor_dialog_polish_script_diagnostics0607_tests
            editor_dialog_polish_enhancement_background001_tests
            editor_dialog_polish_keycode_rename_columns1112_tests
            editor_dialog_polish_skin_selections010_tests
            editor_dialog_polish_talker_crop003_tests
            editor_dialog_polish_timebar_origin013_tests
            editor_dialog_polish_tooltips094_tests
            editor_dialog_polish_unknown_skin009_tests
    )
        add_dependencies(editor_gui_recovery_regression_suite "${dialog_polish_next10_target}")
    endforeach()

    add_custom_target(editor_gui_recovery_dialog_polish_next10
        DEPENDS
            editor_dialog_polish_script_diagnostics0607_tests
            editor_dialog_polish_enhancement_background001_tests
            editor_dialog_polish_keycode_rename_columns1112_tests
            editor_dialog_polish_skin_selections010_tests
            editor_dialog_polish_talker_crop003_tests
            editor_dialog_polish_timebar_origin013_tests
            editor_dialog_polish_tooltips094_tests
            editor_dialog_polish_unknown_skin009_tests
    )
endif()

# 設定だけの再適用と、途中中断されたBGM適用の回帰テスト。
if(TARGET cwxeditor_test_support AND TARGET editor_gui_recovery_regression_suite)
    foreach(apir058_target IN ITEMS
            editor_apir058_battle_settings_delta_tests
            editor_apir058_bgm_exception_tests)
        if(apir058_target IN_LIST CARDWIRTH_ADDITIONAL_BUNDLE_CASES)
            cardwirth_add_bundled_editor_test(${apir058_target} tests/${apir058_target}.cpp)
        else()
        add_executable(${apir058_target} tests/${apir058_target}.cpp)
            set(${apir058_target}_TEST_COMMAND ${apir058_target})
        endif()
        target_compile_features(${apir058_target} PRIVATE cxx_std_20)
        target_link_libraries(${apir058_target} PRIVATE cwxeditor_test_support)
        add_test(NAME ${apir058_target} COMMAND ${${apir058_target}_TEST_COMMAND})
        set_tests_properties(${apir058_target} PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")
        add_dependencies(editor_gui_recovery_regression_suite ${apir058_target})
    endforeach()
endif()

# 追加回帰テストの登録。cwxeditor_test_support
# と復旧テス
# ト群のターゲ
# ット定義後に
# 配置する。
if(TARGET cwxeditor_test_support)
    cardwirth_add_bundled_editor_test(editor_cards_api_086_image_cache_candidate_tests
        tests/candidates/additional_six_20261011/editor_cards_api_086_image_cache_candidate.cpp)
    target_compile_features(editor_cards_api_086_image_cache_candidate_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_cards_api_086_image_cache_candidate_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_cards_api_086_image_cache_candidate_tests
        COMMAND ${editor_cards_api_086_image_cache_candidate_tests_TEST_COMMAND})
    set_tests_properties(editor_cards_api_086_image_cache_candidate_tests
        PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_cards_api_091_image_fileview_candidate_tests
        tests/candidates/additional_six_20261011/editor_cards_api_091_image_fileview_candidate.cpp)
    target_compile_features(editor_cards_api_091_image_fileview_candidate_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_cards_api_091_image_fileview_candidate_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_cards_api_091_image_fileview_candidate_tests
        COMMAND ${editor_cards_api_091_image_fileview_candidate_tests_TEST_COMMAND})
    set_tests_properties(editor_cards_api_091_image_fileview_candidate_tests
        PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_rv4_s_022_unicode_history_candidate_tests
        tests/candidates/additional_six_20261011/rv4_s_022_unicode_history_candidate.cpp)
    target_compile_features(editor_rv4_s_022_unicode_history_candidate_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_rv4_s_022_unicode_history_candidate_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_rv4_s_022_unicode_history_candidate_tests
        COMMAND ${editor_rv4_s_022_unicode_history_candidate_tests_TEST_COMMAND})
    set_tests_properties(editor_rv4_s_022_unicode_history_candidate_tests
        PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    cardwirth_add_bundled_editor_test(editor_rv4_e_017_tilt_comment_focus_candidate_tests
        tests/candidates/additional_six_20261011/rv4_e_017_tilt_comment_focus_candidate.cpp)
    target_compile_features(editor_rv4_e_017_tilt_comment_focus_candidate_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_rv4_e_017_tilt_comment_focus_candidate_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_rv4_e_017_tilt_comment_focus_candidate_tests
        COMMAND ${editor_rv4_e_017_tilt_comment_focus_candidate_tests_TEST_COMMAND})
    set_tests_properties(editor_rv4_e_017_tilt_comment_focus_candidate_tests
        PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")

    if(TARGET editor_gui_recovery_regression_suite)
        add_dependencies(editor_gui_recovery_regression_suite
            editor_cards_api_086_image_cache_candidate_tests
            editor_cards_api_091_image_fileview_candidate_tests
            editor_rv4_s_022_unicode_history_candidate_tests
            editor_rv4_e_017_tilt_comment_focus_candidate_tests)
    endif()
endif()

# 設定スキンのライフサイクルとシーンイベント同期修復は、新規ターゲットではなくテストソースの差
# し替えで対応する。既存の登録とターゲット名を維持し、src/tests/editor_settings_skin_lifecycle_tests.cpp
# とsrc/tests/editor_scene_event_sync_repair_tests.cpp
# へ候補を適用する。

# 戦闘BGMのUndoでは、再生中のプレビュー
# 音量を変えずに音量コントロールを復元する。
# この候補はコンパイル・実行時検証が未完了。
if(TARGET cwxeditor_test_support)
    cardwirth_add_bundled_editor_test(editor_rv4_c_007_bgm_undo_live_preview_tests
        tests/candidates/additional_seven_20261011/rv4_c_007_bgm_undo_live_preview_tests.cpp)
    target_compile_features(editor_rv4_c_007_bgm_undo_live_preview_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_rv4_c_007_bgm_undo_live_preview_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_rv4_c_007_bgm_undo_live_preview_tests
        COMMAND ${editor_rv4_c_007_bgm_undo_live_preview_tests_TEST_COMMAND})
    set_tests_properties(editor_rv4_c_007_bgm_undo_live_preview_tests
        PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")
    if(TARGET editor_gui_recovery_regression_suite)
        add_dependencies(editor_gui_recovery_regression_suite
            editor_rv4_c_007_bgm_undo_live_preview_tests)
    endif()
endif()

# 個別の単体テストを3つ登録する。既存テス
# トの変更はソースの差し替えで行い、登録済
# みのCMakeターゲットは追加しない。
if(TARGET cwxeditor_test_support)
    cardwirth_add_bundled_editor_test(editor_rv4_e005_clipboard_capability_tests
        tests/candidates/additional_ten_20261011/editor_rv4_e005_clipboard_capability_tests.cpp)
    target_compile_features(editor_rv4_e005_clipboard_capability_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_rv4_e005_clipboard_capability_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_rv4_e005_clipboard_capability_tests
        COMMAND ${editor_rv4_e005_clipboard_capability_tests_TEST_COMMAND})
    set_tests_properties(editor_rv4_e005_clipboard_capability_tests
        PROPERTIES TIMEOUT 15 LABELS "editor;recovery;contract")

    cardwirth_add_bundled_editor_test(editor_rv4_e016_wallpaper_initial_directory_tests
        tests/candidates/additional_ten_20261011/editor_rv4_e016_wallpaper_initial_directory_tests.cpp)
    target_compile_features(editor_rv4_e016_wallpaper_initial_directory_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_rv4_e016_wallpaper_initial_directory_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_rv4_e016_wallpaper_initial_directory_tests
        COMMAND ${editor_rv4_e016_wallpaper_initial_directory_tests_TEST_COMMAND})
    set_tests_properties(editor_rv4_e016_wallpaper_initial_directory_tests
        PROPERTIES TIMEOUT 15 LABELS "editor;recovery;contract")

    cardwirth_add_bundled_editor_test(editor_rv4_e023_vanished_drop_file_tests
        tests/candidates/additional_ten_20261011/editor_rv4_e023_vanished_drop_file_tests.cpp)
    target_compile_features(editor_rv4_e023_vanished_drop_file_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_rv4_e023_vanished_drop_file_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_rv4_e023_vanished_drop_file_tests
        COMMAND ${editor_rv4_e023_vanished_drop_file_tests_TEST_COMMAND})
    set_tests_properties(editor_rv4_e023_vanished_drop_file_tests
        PROPERTIES TIMEOUT 15 LABELS "editor;recovery;contract")

    if(TARGET editor_gui_recovery_regression_suite)
        add_dependencies(editor_gui_recovery_regression_suite
            editor_rv4_e005_clipboard_capability_tests
            editor_rv4_e016_wallpaper_initial_directory_tests
            editor_rv4_e023_vanished_drop_file_tests)
    endif()
endif()

# 対象を絞った回帰テストの集約ターゲット。新規の単体テストと、アサ
# ーションを追加した既存テストをまとめてビルドする。同じ組み合わ
# せはctest -L next40_focusedで実行する。
if(TARGET cwxeditor_test_support)
    set(_next40_regression_targets
        editor_root_001_variable_natural_sort_tests
        editor_root_009_property_warnings_tests
        editor_root_010_multi_property_dialogs_tests
        editor_root_011_clone_clipboard_tests
        editor_dialog_edges_root003_shared_resume_tests
        editor_event_graph_following_next10_tests
        editor_incremental_search_following_next10_tests
        editor_event_dialog_shared_next10_tests
        editor_connected_resource_reveal_026_next10_tests
        editor_dialog_polish_script_diagnostics0607_tests
        editor_dialog_polish_enhancement_background001_tests
        editor_dialog_polish_keycode_rename_columns1112_tests
        editor_dialog_polish_skin_selections010_tests
        editor_dialog_polish_talker_crop003_tests
        editor_dialog_polish_timebar_origin013_tests
        editor_dialog_polish_tooltips094_tests
        editor_dialog_polish_unknown_skin009_tests
        editor_cards_api_086_image_cache_candidate_tests
        editor_cards_api_091_image_fileview_candidate_tests
        editor_rv4_s_022_unicode_history_candidate_tests
        editor_rv4_e_017_tilt_comment_focus_candidate_tests
        editor_rv4_c_007_bgm_undo_live_preview_tests
        editor_rv4_e005_clipboard_capability_tests
        editor_rv4_e016_wallpaper_initial_directory_tests
        editor_rv4_e023_vanished_drop_file_tests
        cwxeditor_tests
        editor_area_view_apir_059_060_061_062_tests
        editor_scene_event_sync_repair_tests
        editor_settings_skin_lifecycle_tests
    )
    set(_next40_regression_tests
        editor_root_001_variable_natural_sort_tests
        editor_root_009_property_warnings_tests
        editor_root_010_multi_property_dialogs_tests
        editor_root_011_clone_clipboard_tests
        editor_dialog_edges_root003_shared_resume_tests
        editor_event_graph_following_next10_tests
        editor_incremental_search_following_next10_tests
        editor_event_dialog_shared_next10_tests
        editor_connected_resource_reveal_026_next10_tests
        editor_dialog_polish_script_diagnostics0607_tests
        editor_dialog_polish_enhancement_background001_tests
        editor_dialog_polish_keycode_rename_columns1112_tests
        editor_dialog_polish_skin_selections010_tests
        editor_dialog_polish_talker_crop003_tests
        editor_dialog_polish_timebar_origin013_tests
        editor_dialog_polish_tooltips094_tests
        editor_dialog_polish_unknown_skin009_tests
        editor_cards_api_086_image_cache_candidate_tests
        editor_cards_api_091_image_fileview_candidate_tests
        editor_rv4_s_022_unicode_history_candidate_tests
        editor_rv4_e_017_tilt_comment_focus_candidate_tests
        editor_rv4_c_007_bgm_undo_live_preview_tests
        editor_rv4_e005_clipboard_capability_tests
        editor_rv4_e016_wallpaper_initial_directory_tests
        editor_rv4_e023_vanished_drop_file_tests
        cwxeditor_api
        editor_area_view_apir_059_060_061_062_tests
        editor_scene_event_sync_repair_tests
        editor_settings_skin_lifecycle_tests
    )
    set_property(TEST ${_next40_regression_tests} APPEND PROPERTY LABELS "next40_focused")
    add_custom_target(editor_gui_next40_focused_regression_suite
        DEPENDS ${_next40_regression_targets})
    unset(_next40_regression_targets)
    unset(_next40_regression_tests)
endif()

# ABIを維持する4つの既存ケースは、共通の一括テスト実行部を使う。
if(TARGET cwxeditor_test_support)
    cardwirth_add_bundled_editor_test(shared_resume_16_casefold_candidates_tests
        tests/candidates/leaf_four_20261011/shared_resume_16_casefold_candidates_tests.cpp)
    target_compile_features(shared_resume_16_casefold_candidates_tests PRIVATE cxx_std_20)
    target_link_libraries(shared_resume_16_casefold_candidates_tests PRIVATE cwxeditor_test_support)
    add_test(NAME shared_resume_16_casefold_candidates_tests COMMAND ${shared_resume_16_casefold_candidates_tests_TEST_COMMAND})
    set_tests_properties(shared_resume_16_casefold_candidates_tests PROPERTIES TIMEOUT 30 LABELS "editor;recovery;leaf_four")
    cardwirth_add_bundled_editor_test(shared_resume_27_preview_position_tests
        tests/candidates/leaf_four_20261011/shared_resume_27_preview_position_tests.cpp)
    target_compile_features(shared_resume_27_preview_position_tests PRIVATE cxx_std_20)
    target_link_libraries(shared_resume_27_preview_position_tests PRIVATE cwxeditor_test_support)
    add_test(NAME shared_resume_27_preview_position_tests COMMAND ${shared_resume_27_preview_position_tests_TEST_COMMAND})
    set_tests_properties(shared_resume_27_preview_position_tests PROPERTIES TIMEOUT 30 LABELS "editor;recovery;leaf_four")
    cardwirth_add_bundled_editor_test(editor_shared_resume_22_history_external_drop_tests
        tests/candidates/leaf_four_20261011/editor_shared_resume_22_history_external_drop_tests.cpp)
    target_compile_features(editor_shared_resume_22_history_external_drop_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_shared_resume_22_history_external_drop_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_shared_resume_22_history_external_drop_tests COMMAND ${editor_shared_resume_22_history_external_drop_tests_TEST_COMMAND})
    set_tests_properties(editor_shared_resume_22_history_external_drop_tests PROPERTIES TIMEOUT 30 LABELS "editor;recovery;leaf_four")
    cardwirth_add_bundled_editor_test(editor_shared_api_03_classic_engine_path_open_tests
        tests/candidates/leaf_four_20261011/editor_shared_api_03_classic_engine_path_open_tests.cpp)
    target_compile_features(editor_shared_api_03_classic_engine_path_open_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_shared_api_03_classic_engine_path_open_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_shared_api_03_classic_engine_path_open_tests COMMAND ${editor_shared_api_03_classic_engine_path_open_tests_TEST_COMMAND})
    set_tests_properties(editor_shared_api_03_classic_engine_path_open_tests PROPERTIES TIMEOUT 30 LABELS "editor;recovery;leaf_four")
endif()

if(TARGET cwxeditor_test_support)
    cardwirth_add_bundled_editor_test(root_002_hidden_data_creation_tests tests/candidates/directory_four_20261011/root_002_hidden_data_creation_tests.cpp)
    target_compile_features(root_002_hidden_data_creation_tests PRIVATE cxx_std_20)
    target_link_libraries(root_002_hidden_data_creation_tests PRIVATE cwxeditor_test_support)
    add_test(NAME root_002_hidden_data_creation_tests COMMAND ${root_002_hidden_data_creation_tests_TEST_COMMAND})
    set_tests_properties(root_002_hidden_data_creation_tests PROPERTIES TIMEOUT 120 LABELS "editor;recovery;directory_four")
    cardwirth_add_bundled_editor_test(editor_root006_trace_settings_tests tests/candidates/directory_four_20261011/editor_root006_trace_settings_tests.cpp)
    target_compile_features(editor_root006_trace_settings_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_root006_trace_settings_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_root006_trace_settings_tests COMMAND ${editor_root006_trace_settings_tests_TEST_COMMAND})
    set_tests_properties(editor_root006_trace_settings_tests PROPERTIES TIMEOUT 120 LABELS "editor;recovery;directory_four")
endif()

if(TARGET cwxeditor_test_support)
    cardwirth_add_bundled_editor_test(editor_policy_apir001_004_019_tests tests/candidates/policy_two_20261011/editor_policy_apir001_004_019_tests.cpp)
    target_compile_features(editor_policy_apir001_004_019_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_policy_apir001_004_019_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_policy_apir001_004_019_tests COMMAND ${editor_policy_apir001_004_019_tests_TEST_COMMAND})
    set_tests_properties(editor_policy_apir001_004_019_tests PROPERTIES TIMEOUT 120 LABELS "editor;recovery;policy_two")
endif()


# シーン、履歴、パレットの回帰テストは既存のGUI実行部を共有する。
if(TARGET cwxeditor_test_support)
    cardwirth_add_bundled_editor_test(editor_root008_content_pathid_change_tests tests/candidates/verified_20261011/editor_root008_content_pathid_change_tests.cpp)
    target_compile_features(editor_root008_content_pathid_change_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_root008_content_pathid_change_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_root008_content_pathid_change_tests COMMAND ${editor_root008_content_pathid_change_tests_TEST_COMMAND})
    set_tests_properties(editor_root008_content_pathid_change_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery;verified_20261011")
    cardwirth_add_bundled_editor_test(editor_root008_expression_local_reentry_tests tests/candidates/verified_20261011/editor_root008_expression_local_reentry_tests.cpp)
    target_compile_features(editor_root008_expression_local_reentry_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_root008_expression_local_reentry_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_root008_expression_local_reentry_tests COMMAND ${editor_root008_expression_local_reentry_tests_TEST_COMMAND})
    set_tests_properties(editor_root008_expression_local_reentry_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery;verified_20261011")
    cardwirth_add_bundled_editor_test(editor_root008_material_copy_behavior_tests tests/candidates/verified_20261011/editor_root008_material_copy_behavior_tests.cpp)
    target_compile_features(editor_root008_material_copy_behavior_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_root008_material_copy_behavior_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_root008_material_copy_behavior_tests COMMAND ${editor_root008_material_copy_behavior_tests_TEST_COMMAND})
    set_tests_properties(editor_root008_material_copy_behavior_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery;verified_20261011")
    cardwirth_add_bundled_editor_test(editor_shared_history_structural_replay_tests tests/candidates/verified_20261011/editor_shared_history_structural_replay_tests.cpp)
    target_compile_features(editor_shared_history_structural_replay_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_shared_history_structural_replay_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_shared_history_structural_replay_tests COMMAND ${editor_shared_history_structural_replay_tests_TEST_COMMAND})
    set_tests_properties(editor_shared_history_structural_replay_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery;verified_20261011")
    cardwirth_add_bundled_editor_test(editor_apir058_mainwindow_area_battle_history_tests tests/candidates/verified_20261011/editor_apir058_mainwindow_area_battle_history_tests.cpp)
    target_compile_features(editor_apir058_mainwindow_area_battle_history_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_apir058_mainwindow_area_battle_history_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_apir058_mainwindow_area_battle_history_tests COMMAND ${editor_apir058_mainwindow_area_battle_history_tests_TEST_COMMAND})
    set_tests_properties(editor_apir058_mainwindow_area_battle_history_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery;verified_20261011")
    cardwirth_add_bundled_editor_test(editor_area_table_apir_009_clipboard_tests tests/candidates/verified_20261011/editor_area_table_apir_009_clipboard_tests.cpp)
    target_compile_features(editor_area_table_apir_009_clipboard_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_area_table_apir_009_clipboard_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_area_table_apir_009_clipboard_tests COMMAND ${editor_area_table_apir_009_clipboard_tests_TEST_COMMAND})
    set_tests_properties(editor_area_table_apir_009_clipboard_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery;verified_20261011")
    cardwirth_add_bundled_editor_test(editor_apir037_folder_drag_tests tests/candidates/verified_20261011/editor_apir037_folder_drag_tests.cpp)
    target_compile_features(editor_apir037_folder_drag_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_apir037_folder_drag_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_apir037_folder_drag_tests COMMAND ${editor_apir037_folder_drag_tests_TEST_COMMAND})
    set_tests_properties(editor_apir037_folder_drag_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery;verified_20261011")
    cardwirth_add_bundled_editor_test(editor_apir039_cross_scene_drop_tests tests/candidates/verified_20261011/editor_apir039_cross_scene_drop_tests.cpp)
    target_compile_features(editor_apir039_cross_scene_drop_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_apir039_cross_scene_drop_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_apir039_cross_scene_drop_tests COMMAND ${editor_apir039_cross_scene_drop_tests_TEST_COMMAND})
    set_tests_properties(editor_apir039_cross_scene_drop_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery;verified_20261011")
    cardwirth_add_bundled_editor_test(editor_api_e41_floating_palette_tests tests/candidates/verified_20261011/editor_api_e41_floating_palette_tests.cpp)
    target_compile_features(editor_api_e41_floating_palette_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_api_e41_floating_palette_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_api_e41_floating_palette_tests COMMAND ${editor_api_e41_floating_palette_tests_TEST_COMMAND})
    set_tests_properties(editor_api_e41_floating_palette_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery;verified_20261011")
    cardwirth_add_bundled_editor_test(editor_api_e41_lifecycle_tests tests/candidates/verified_20261011/editor_api_e41_lifecycle_tests.cpp)
    target_compile_features(editor_api_e41_lifecycle_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_api_e41_lifecycle_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_api_e41_lifecycle_tests COMMAND ${editor_api_e41_lifecycle_tests_TEST_COMMAND})
    set_tests_properties(editor_api_e41_lifecycle_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery;verified_20261011")
    cardwirth_add_bundled_editor_test(editor_api_e41_detached_lifecycle_tests tests/candidates/verified_20261011/editor_api_e41_detached_lifecycle_tests.cpp)
    target_compile_features(editor_api_e41_detached_lifecycle_tests PRIVATE cxx_std_20)
    target_link_libraries(editor_api_e41_detached_lifecycle_tests PRIVATE cwxeditor_test_support)
    add_test(NAME editor_api_e41_detached_lifecycle_tests COMMAND ${editor_api_e41_detached_lifecycle_tests_TEST_COMMAND})
    set_tests_properties(editor_api_e41_detached_lifecycle_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery;verified_20261011")
endif()

# 個別の回帰ケースを、ODR違反のない専用翻訳単位、1つの結合オブジェクト、3つの独立したCTestプロセスで検証する。
if(TARGET cwxeditor_test_support)
    cardwirth_add_bundled_editor_test(editor_next4_focused_cases_tests
        tests/candidates/verified_20261011/editor_next4_focused_cases.cpp)
    target_compile_features(editor_next4_focused_cases_tests PRIVATE cxx_std_20)
    target_include_directories(editor_next4_focused_cases_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(editor_next4_focused_cases_tests PRIVATE cwxeditor_test_support)
    target_compile_definitions(editor_next4_focused_cases_tests PRIVATE
        CWXEDITOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        CARDWIRTH_EMBED_EDITOR=1)
    set(_next4_dispatch "${editor_next4_focused_cases_tests_TEST_COMMAND}")
    add_test(NAME editor_rv4_e008_gesture_placement_tests COMMAND ${_next4_dispatch})
    set_tests_properties(editor_rv4_e008_gesture_placement_tests PROPERTIES
        TIMEOUT 120
        LABELS "editor;gui;recovery"
        ENVIRONMENT "CWXEDITOR_TEST_FILTER=test_event_view_gesture_placement_and_selection_contract;CWXEDITOR_TEST_TRACE=1")
    add_test(NAME editor_rv4_e010_palette_order_tests COMMAND ${_next4_dispatch})
    set_tests_properties(editor_rv4_e010_palette_order_tests PROPERTIES
        TIMEOUT 120
        LABELS "editor;gui;recovery"
        ENVIRONMENT "CWXEDITOR_TEST_FILTER=test_event_view_d_layout_and_palette_placement;CWXEDITOR_TEST_TRACE=1")
    add_test(NAME editor_rv4_r022_material_selector_audio_lifetime_tests COMMAND ${_next4_dispatch})
    set_tests_properties(editor_rv4_r022_material_selector_audio_lifetime_tests PROPERTIES
        TIMEOUT 120
        LABELS "editor;gui;recovery"
        ENVIRONMENT "CWXEDITOR_TEST_FILTER=test_material_selector_audio_lifetime;CWXEDITOR_TEST_TRACE=1")
    add_custom_target(editor_gui_next4_focused_cases DEPENDS editor_next4_focused_cases_tests)
    if(TARGET editor_gui_recovery_regression_suite)
        add_dependencies(editor_gui_recovery_regression_suite editor_next4_focused_cases_tests)
    endif()
    unset(_next4_dispatch)
endif()

if(BUILD_TESTING AND TARGET cwxeditor_gui)
    cardwirth_add_bundled_editor_test(editor_search_e106_e125_e134_tests tests/candidates/verified_20261011/editor_search_e106_e125_e134_tests.cpp)
    target_link_libraries(editor_search_e106_e125_e134_tests PRIVATE cwxeditor_gui cwxeditor_core cwxeditor_test_support)
    add_test(NAME editor_search_e106_e125_e134_tests COMMAND ${editor_search_e106_e125_e134_tests_TEST_COMMAND})
    set_tests_properties(editor_search_e106_e125_e134_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")
    if(TARGET editor_gui_recovery_regression_suite)
        add_dependencies(editor_gui_recovery_regression_suite editor_search_e106_e125_e134_tests)
    endif()
endif()

if(BUILD_TESTING AND TARGET cwxeditor_gui)
    cardwirth_add_bundled_editor_test(editor_search_e123_e109_mainwindow_tests tests/candidates/verified_20261011/editor_search_e123_e109_mainwindow_tests.cpp)
    target_link_libraries(editor_search_e123_e109_mainwindow_tests PRIVATE cwxeditor_gui cwxeditor_core cwxeditor_test_support)
    add_test(NAME editor_search_e123_e109_mainwindow_tests COMMAND ${editor_search_e123_e109_mainwindow_tests_TEST_COMMAND})
    set_tests_properties(editor_search_e123_e109_mainwindow_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")
    if(TARGET editor_gui_recovery_regression_suite)
        add_dependencies(editor_gui_recovery_regression_suite editor_search_e123_e109_mainwindow_tests)
    endif()
endif()

if(BUILD_TESTING AND TARGET cwxeditor_gui)
    cardwirth_add_bundled_editor_test(editor_api_e110_reference_utf8_focused_tests tests/candidates/verified_20261011/editor_api_e110_reference_utf8_focused_tests.cpp)
    target_link_libraries(editor_api_e110_reference_utf8_focused_tests PRIVATE cwxeditor_gui cwxeditor_core cwxeditor_test_support)
    add_test(NAME editor_api_e110_reference_utf8_focused_tests COMMAND ${editor_api_e110_reference_utf8_focused_tests_TEST_COMMAND})
    set_tests_properties(editor_api_e110_reference_utf8_focused_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")
    if(TARGET editor_gui_recovery_regression_suite)
        add_dependencies(editor_gui_recovery_regression_suite editor_api_e110_reference_utf8_focused_tests)
    endif()
endif()

if(BUILD_TESTING AND TARGET cwxeditor_gui)
    cardwirth_add_bundled_editor_test(editor_api_e124_history_enter_tests tests/candidates/verified_20261011/editor_api_e124_history_enter_tests.cpp)
    target_link_libraries(editor_api_e124_history_enter_tests PRIVATE cwxeditor_gui cwxeditor_core cwxeditor_test_support)
    add_test(NAME editor_api_e124_history_enter_tests COMMAND ${editor_api_e124_history_enter_tests_TEST_COMMAND})
    set_tests_properties(editor_api_e124_history_enter_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery")
    if(TARGET editor_gui_recovery_regression_suite)
        add_dependencies(editor_gui_recovery_regression_suite editor_api_e124_history_enter_tests)
    endif()
endif()

# 1つの結合プロセスでSLI解析、レート変換、SDLループの寿命を検証する。
if(BUILD_TESTING AND TARGET cwxeditor_gui)
    cardwirth_add_bundled_editor_test(editor_sli_loop_region_tests
        tests/candidates/verified_20261011/editor_sli_loop_region_tests.cpp)
    target_compile_features(editor_sli_loop_region_tests PRIVATE cxx_std_20)
    target_include_directories(editor_sli_loop_region_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(editor_sli_loop_region_tests PRIVATE cwxeditor_gui cwxeditor_core cwxeditor_test_support)
    add_test(NAME editor_sli_loop_region_tests COMMAND ${editor_sli_loop_region_tests_TEST_COMMAND})
    set_tests_properties(editor_sli_loop_region_tests PROPERTIES
        TIMEOUT 120 LABELS "editor;gui;audio;recovery"
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    if(TARGET editor_gui_recovery_regression_suite)
        add_dependencies(editor_gui_recovery_regression_suite editor_sli_loop_region_tests)
    endif()
endif()


# 1つの結合ケースで表示中のイベント名描画とスクリプトコピーコマンドを検証する。
if(BUILD_TESTING AND TARGET cwxeditor_gui)
    cardwirth_add_bundled_editor_test(editor_event_name_script_copy_tests
        tests/candidates/verified_20261012/editor_event_name_script_copy_tests.cpp)
    target_compile_features(editor_event_name_script_copy_tests PRIVATE cxx_std_20)
    target_include_directories(editor_event_name_script_copy_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(editor_event_name_script_copy_tests PRIVATE cwxeditor_gui cwxeditor_core cwxeditor_test_support)
    add_test(NAME editor_event_name_script_copy_tests COMMAND ${editor_event_name_script_copy_tests_TEST_COMMAND})
    set_tests_properties(editor_event_name_script_copy_tests PROPERTIES
        TIMEOUT 120 LABELS "editor;gui;recovery"
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    if(TARGET editor_gui_recovery_regression_suite)
        add_dependencies(editor_gui_recovery_regression_suite editor_event_name_script_copy_tests)
    endif()
endif()

# 1つの結合ケースで関連する5つの検索・取得元契約を検証する。
if(BUILD_TESTING AND TARGET cwxeditor_gui)
    cardwirth_add_bundled_editor_test(editor_replace_search_contract_tests
        tests/candidates/verified_20261012/editor_replace_search_contract_tests.cpp)
    target_compile_features(editor_replace_search_contract_tests PRIVATE cxx_std_20)
    target_include_directories(editor_replace_search_contract_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(editor_replace_search_contract_tests PRIVATE cwxeditor_gui cwxeditor_core cwxeditor_test_support)
    add_test(NAME editor_replace_search_contract_tests COMMAND ${editor_replace_search_contract_tests_TEST_COMMAND})
    set_tests_properties(editor_replace_search_contract_tests PROPERTIES
        TIMEOUT 120 LABELS "editor;gui;recovery"
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    if(TARGET editor_gui_recovery_regression_suite)
        add_dependencies(editor_gui_recovery_regression_suite editor_replace_search_contract_tests)
    endif()
endif()

# 1つの結合ケースで関連する4つの警告更新契約を検証する。
if(BUILD_TESTING AND TARGET cwxeditor_gui)
    cardwirth_add_bundled_editor_test(editor_event_warning_refresh_tests
        tests/candidates/verified_20261012/editor_event_warning_refresh_tests.cpp)
    target_compile_features(editor_event_warning_refresh_tests PRIVATE cxx_std_20)
    target_include_directories(editor_event_warning_refresh_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(editor_event_warning_refresh_tests PRIVATE cwxeditor_gui cwxeditor_core cwxeditor_test_support)
    add_test(NAME editor_event_warning_refresh_tests COMMAND ${editor_event_warning_refresh_tests_TEST_COMMAND})
    set_tests_properties(editor_event_warning_refresh_tests PROPERTIES
        TIMEOUT 120 LABELS "editor;gui;recovery"
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    if(TARGET editor_gui_recovery_regression_suite)
        add_dependencies(editor_gui_recovery_regression_suite editor_event_warning_refresh_tests)
    endif()
endif()

# 1つの結合ケースで配置モードと名前変更に対応した履歴の契約を検証する。
if(BUILD_TESTING AND TARGET cwxeditor_gui)
    cardwirth_add_bundled_editor_test(editor_event_modes_history_tests
        tests/candidates/verified_20261012/editor_event_modes_history_tests.cpp)
    target_compile_features(editor_event_modes_history_tests PRIVATE cxx_std_20)
    target_include_directories(editor_event_modes_history_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(editor_event_modes_history_tests PRIVATE cwxeditor_gui cwxeditor_core cwxeditor_test_support)
    add_test(NAME editor_event_modes_history_tests COMMAND ${editor_event_modes_history_tests_TEST_COMMAND})
    set_tests_properties(editor_event_modes_history_tests PROPERTIES
        TIMEOUT 120 LABELS "editor;gui;recovery"
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    if(TARGET editor_gui_recovery_regression_suite)
        add_dependencies(editor_gui_recovery_regression_suite editor_event_modes_history_tests)
    endif()
endif()

if(BUILD_TESTING AND TARGET cwxeditor_gui)
    cardwirth_add_bundled_editor_test(editor_card_list_interaction_tests
        tests/candidates/verified_20261012/editor_card_list_interaction_tests.cpp)
    target_compile_features(editor_card_list_interaction_tests PRIVATE cxx_std_20)
    target_include_directories(editor_card_list_interaction_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(editor_card_list_interaction_tests PRIVATE cwxeditor_gui cwxeditor_core cwxeditor_test_support)
    add_test(NAME editor_card_list_interaction_tests COMMAND ${editor_card_list_interaction_tests_TEST_COMMAND})
    set_tests_properties(editor_card_list_interaction_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery"
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    if(TARGET editor_gui_recovery_regression_suite)
        add_dependencies(editor_gui_recovery_regression_suite editor_card_list_interaction_tests)
    endif()
endif()

if(BUILD_TESTING AND TARGET cwxeditor_gui)
    cardwirth_add_bundled_editor_test(editor_cardpane_followup_tests
        tests/candidates/verified_20261012/editor_cardpane_followup_tests.cpp)
    target_compile_features(editor_cardpane_followup_tests PRIVATE cxx_std_20)
    target_include_directories(editor_cardpane_followup_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(editor_cardpane_followup_tests PRIVATE cwxeditor_gui cwxeditor_core cwxeditor_test_support)
    add_test(NAME editor_cardpane_followup_tests COMMAND ${editor_cardpane_followup_tests_TEST_COMMAND})
    set_tests_properties(editor_cardpane_followup_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery"
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    if(TARGET editor_gui_recovery_regression_suite)
        add_dependencies(editor_gui_recovery_regression_suite editor_cardpane_followup_tests)
    endif()
endif()

if(BUILD_TESTING AND TARGET cwxeditor_gui)
    cardwirth_add_bundled_editor_test(editor_cardpane_routes_tests WITH_ARGS
        tests/candidates/verified_20261012/editor_cardpane_routes_tests.cpp)
    target_compile_features(editor_cardpane_routes_tests PRIVATE cxx_std_20)
    target_include_directories(editor_cardpane_routes_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(editor_cardpane_routes_tests PRIVATE cwxeditor_gui cwxeditor_core cwxeditor_test_support)
    add_test(NAME editor_cardpane_routes_tests COMMAND ${editor_cardpane_routes_tests_TEST_COMMAND})
    set_tests_properties(editor_cardpane_routes_tests PROPERTIES TIMEOUT 120 LABELS "editor;gui;recovery"
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    if(TARGET editor_gui_recovery_regression_suite)
        add_dependencies(editor_gui_recovery_regression_suite editor_cardpane_routes_tests)
    endif()
endif()

# 対象となるテストターゲットと集約ビルドターゲットをすべて定義してから確定する。
if(BUILD_TESTING AND TARGET cwxeditor_gui AND TARGET cwxeditor_test_support)
    # 境界サイズがゼロのFlexImageの描画動作。
    cardwirth_add_bundled_editor_test(editor_cards_api_085_fleximage_zero_bounds_tests
        tests/candidates/verified_20261012/editor_cards_api_085_fleximage_zero_bounds_tests.cpp)

    # リンクされた手札カード画像の解決。
    cardwirth_add_bundled_editor_test(editor_cards_api_069_linked_hand_image_tests
        tests/candidates/verified_20261012/editor_cards_api_069_linked_hand_image_tests.cpp)

    # タイトル設定、補助選択、画像状態の設定。
    cardwirth_add_bundled_editor_test(editor_cards_api_074_075_079_tests
        tests/candidates/verified_20261012/editor_cards_api_074_075_079_tests.cpp)

    # entry()にmain()がないため、cardwirth_add_bundled_editor_test
    # の名前変更したmainの配送契約に合わせ、アダプターで提供する。
    cardwirth_add_bundled_editor_test(editor_cards_api_067_068_tests
        tests/candidates/verified_20261012/editor_cards_api_067_068_tests.cpp
        tests/candidates/verified_20261012/editor_cards_api_067_068_bundle_entry.cpp)

    # 表示中の連結素材のファイル選択。
    cardwirth_add_bundled_editor_test(editor_cards_api_066_connected_material_host_tests
        tests/candidates/verified_20261012/editor_cards_api_066_connected_material_host_tests.cpp)

    # 表示中の名前・履歴とコメントのタイトル処理。
    cardwirth_add_bundled_editor_test(editor_cards_api_070_071_tests
        tests/candidates/verified_20261012/editor_cards_api_070_071_tests.cpp)

    # 選択設定ダイアログの順序とCtrl付きダブルクリックの対象群。
    cardwirth_add_bundled_editor_test(editor_cards_api_083_084_tests
        tests/candidates/verified_20261012/editor_cards_api_083_084_tests.cpp)

    set(_recovered_editor_api_tests
        editor_cards_api_085_fleximage_zero_bounds_tests
        editor_cards_api_069_linked_hand_image_tests
        editor_cards_api_074_075_079_tests
        editor_cards_api_067_068_tests
        editor_cards_api_066_connected_material_host_tests
        editor_cards_api_070_071_tests
        editor_cards_api_083_084_tests)

    foreach(api_target IN LISTS _recovered_editor_api_tests)
        target_compile_features(${api_target} PRIVATE cxx_std_20)
        target_include_directories(${api_target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
        target_link_libraries(${api_target} PRIVATE
            cwxeditor_gui cwxeditor_core cwxeditor_test_support)
        add_test(NAME ${api_target} COMMAND ${${api_target}_TEST_COMMAND})
        set_tests_properties(${api_target} PROPERTIES
            TIMEOUT 120
            LABELS "editor;gui;recovery"
            ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
        if(TARGET editor_gui_recovery_regression_suite)
            add_dependencies(editor_gui_recovery_regression_suite ${api_target})
        endif()
    endforeach()
    unset(_recovered_editor_api_tests)
endif()


if(BUILD_TESTING AND TARGET cwxeditor_gui AND TARGET cwxeditor_test_support AND
   TARGET editor_gui_recovery_regression_suite)
    cardwirth_add_bundled_editor_test(editor_cards_api_072_081_paint_boundary_tests
        tests/candidates/verified_20261012/editor_cards_api_072_081_paint_boundary_tests.cpp)
    cardwirth_add_bundled_editor_test(editor_cards_api_073_076_077_tests WITH_ARGS
        tests/candidates/verified_20261012/editor_cards_api_073_076_077_tests.cpp)
    cardwirth_add_bundled_editor_test(editor_cards_api_076_event_marker_history_extended_tests WITH_ARGS
        tests/candidates/verified_20261012/editor_cards_api_076_event_marker_history_extended_tests.cpp)
    cardwirth_add_bundled_editor_test(editor_cards_api_078_080_082_tests WITH_ARGS
        tests/candidates/verified_20261012/editor_cards_api_078_080_082_tests.cpp)
    cardwirth_add_bundled_editor_test(editor_areaviewutils_pileimage_updatepath_tests
        tests/candidates/verified_20261012/editor_areaviewutils_pileimage_updatepath_tests.cpp)

    set(_next403zip_targets
        editor_cards_api_072_081_paint_boundary_tests
        editor_cards_api_073_076_077_tests
        editor_cards_api_076_event_marker_history_extended_tests
        editor_cards_api_078_080_082_tests
        editor_areaviewutils_pileimage_updatepath_tests)
    foreach(_target IN LISTS _next403zip_targets)
        target_compile_features(${_target} PRIVATE cxx_std_20)
        target_include_directories(${_target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
        target_link_libraries(${_target} PRIVATE
            cwxeditor_gui cwxeditor_core cwxeditor_test_support)
        if(TARGET editor_gui_recovery_regression_suite)
            add_dependencies(editor_gui_recovery_regression_suite ${_target})
        endif()
    endforeach()

    # 引数を取らない翻訳単位内の独立した2ケースを1つの経路で検証する。
    add_test(NAME editor_cards_api_072_081_paint_boundary_tests
        COMMAND ${editor_cards_api_072_081_paint_boundary_tests_TEST_COMMAND})

    # コマンドライン用翻訳単位ではケース別のCTest選択を維持する。経路を増やしても検証対象の件数は増やさない。
    add_test(NAME editor_cards_api_073_tests
        COMMAND ${editor_cards_api_073_076_077_tests_TEST_COMMAND} --case CARDS-API-073)
    add_test(NAME editor_cards_api_076_tests
        COMMAND ${editor_cards_api_073_076_077_tests_TEST_COMMAND} --case CARDS-API-076)
    add_test(NAME editor_cards_api_077_tests
        COMMAND ${editor_cards_api_073_076_077_tests_TEST_COMMAND} --case CARDS-API-077)

    # 拡張ライフサイクルの経路は、同じケースの補助的な検証。
    add_test(NAME editor_cards_api_076_event_marker_history_extended_tests
        COMMAND ${editor_cards_api_076_event_marker_history_extended_tests_TEST_COMMAND}
                --case CARDS-API-076)

    # 表示中のプレビューのテスト群は、APIごとに独立して選択できるようにする。
    add_test(NAME editor_cards_api_078_tests
        COMMAND ${editor_cards_api_078_080_082_tests_TEST_COMMAND} --case CARDS-API-078)
    add_test(NAME editor_cards_api_080_tests
        COMMAND ${editor_cards_api_078_080_082_tests_TEST_COMMAND} --case CARDS-API-080)
    add_test(NAME editor_cards_api_082_tests
        COMMAND ${editor_cards_api_078_080_082_tests_TEST_COMMAND} --case CARDS-API-082)

    # areaviewutils::PileImage::updatePathの直接的な回帰テスト。コマンドライン引数は使わない。
    add_test(NAME editor_areaviewutils_pileimage_updatepath_tests
        COMMAND ${editor_areaviewutils_pileimage_updatepath_tests_TEST_COMMAND})

    set(_next403zip_ctests
        editor_cards_api_072_081_paint_boundary_tests
        editor_cards_api_073_tests
        editor_cards_api_076_tests
        editor_cards_api_077_tests
        editor_cards_api_076_event_marker_history_extended_tests
        editor_cards_api_078_tests
        editor_cards_api_080_tests
        editor_cards_api_082_tests
        editor_areaviewutils_pileimage_updatepath_tests)
    set_tests_properties(${_next403zip_ctests} PROPERTIES
        TIMEOUT 120
        LABELS "editor;gui;recovery"
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    unset(_next403zip_ctests)
    unset(_next403zip_targets)
endif()

# 個別の回帰テスト。検証対象の件数は実行経路の数とは別に数える。
if(BUILD_TESTING AND TARGET cwxeditor_gui AND TARGET cwxeditor_test_support AND TARGET editor_gui_recovery_regression_suite)
    cardwirth_add_bundled_editor_test(editor_cards_api_087_tests WITH_ARGS
        tests/candidates/verified_20261012/editor_cards_api_087_mounted_tests.cpp)
    target_compile_features(editor_cards_api_087_tests PRIVATE cxx_std_20)
    target_include_directories(editor_cards_api_087_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(editor_cards_api_087_tests PRIVATE cwxeditor_gui cwxeditor_core cwxeditor_test_support)
    add_dependencies(editor_gui_recovery_regression_suite editor_cards_api_087_tests)
    add_test(NAME editor_cards_api_087_tests
        COMMAND ${editor_cards_api_087_tests_TEST_COMMAND} 087)
    set_tests_properties(editor_cards_api_087_tests PROPERTIES
        TIMEOUT 120 LABELS "editor;gui;recovery"
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    cardwirth_add_bundled_editor_test(editor_cards_api_088_tests WITH_ARGS
        tests/candidates/verified_20261012/editor_cards_api_088_mounted_tests.cpp)
    target_compile_features(editor_cards_api_088_tests PRIVATE cxx_std_20)
    target_include_directories(editor_cards_api_088_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(editor_cards_api_088_tests PRIVATE cwxeditor_gui cwxeditor_core cwxeditor_test_support)
    add_dependencies(editor_gui_recovery_regression_suite editor_cards_api_088_tests)
    add_test(NAME editor_cards_api_088_tests
        COMMAND ${editor_cards_api_088_tests_TEST_COMMAND} 088)
    set_tests_properties(editor_cards_api_088_tests PROPERTIES
        TIMEOUT 120 LABELS "editor;gui;recovery"
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    cardwirth_add_bundled_editor_test(editor_cards_api_094_tests WITH_ARGS
        tests/candidates/verified_20261012/editor_cards_api_094_exchange_action_tests.cpp)
    target_compile_features(editor_cards_api_094_tests PRIVATE cxx_std_20)
    target_include_directories(editor_cards_api_094_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(editor_cards_api_094_tests PRIVATE cwxeditor_gui cwxeditor_core cwxeditor_test_support)
    add_dependencies(editor_gui_recovery_regression_suite editor_cards_api_094_tests)
    target_compile_definitions(editor_cards_api_094_tests PRIVATE "cards_api_094_095_098_run=cardwirth_cards_api_094_registered_run")
    add_test(NAME editor_cards_api_094_tests
        COMMAND ${editor_cards_api_094_tests_TEST_COMMAND} --case 094)
    set_tests_properties(editor_cards_api_094_tests PROPERTIES
        TIMEOUT 120 LABELS "editor;gui;recovery"
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    cardwirth_add_bundled_editor_test(editor_cards_api_095_skill_tests WITH_ARGS
        tests/candidates/verified_20261012/editor_cards_api_095_skill_close_tests.cpp)
    target_compile_features(editor_cards_api_095_skill_tests PRIVATE cxx_std_20)
    target_include_directories(editor_cards_api_095_skill_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(editor_cards_api_095_skill_tests PRIVATE cwxeditor_gui cwxeditor_core cwxeditor_test_support)
    add_dependencies(editor_gui_recovery_regression_suite editor_cards_api_095_skill_tests)
    target_compile_definitions(editor_cards_api_095_skill_tests PRIVATE "cards_api_094_095_098_run=cardwirth_cards_api_095_skill_registered_run")
    add_test(NAME editor_cards_api_095_skill_tests
        COMMAND ${editor_cards_api_095_skill_tests_TEST_COMMAND} --case 095)
    set_tests_properties(editor_cards_api_095_skill_tests PROPERTIES
        TIMEOUT 120 LABELS "editor;gui;recovery"
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    cardwirth_add_bundled_editor_test(editor_cards_api_095_dual_close_tests WITH_ARGS
        tests/candidates/verified_20261012/editor_cards_api_095_retained_dual_close_tests.cpp)
    target_compile_features(editor_cards_api_095_dual_close_tests PRIVATE cxx_std_20)
    target_include_directories(editor_cards_api_095_dual_close_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(editor_cards_api_095_dual_close_tests PRIVATE cwxeditor_gui cwxeditor_core cwxeditor_test_support)
    add_dependencies(editor_gui_recovery_regression_suite editor_cards_api_095_dual_close_tests)
    target_compile_definitions(editor_cards_api_095_dual_close_tests PRIVATE "cards_api_095_retained_dual_run=cardwirth_cards_api_095_dual_close_registered_run")
    add_test(NAME editor_cards_api_095_item_close_tests
        COMMAND ${editor_cards_api_095_dual_close_tests_TEST_COMMAND} --case 095-item)
    set_tests_properties(editor_cards_api_095_item_close_tests PROPERTIES
        TIMEOUT 120 LABELS "editor;gui;recovery"
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    add_test(NAME editor_cards_api_095_beast_close_tests
        COMMAND ${editor_cards_api_095_dual_close_tests_TEST_COMMAND} --case 095-beast)
    set_tests_properties(editor_cards_api_095_beast_close_tests PROPERTIES
        TIMEOUT 120 LABELS "editor;gui;recovery"
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    cardwirth_add_bundled_editor_test(editor_cards_api_098_tests WITH_ARGS
        tests/candidates/verified_20261012/editor_cards_api_098_background_mask_tests.cpp)
    target_compile_features(editor_cards_api_098_tests PRIVATE cxx_std_20)
    target_include_directories(editor_cards_api_098_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(editor_cards_api_098_tests PRIVATE cwxeditor_gui cwxeditor_core cwxeditor_test_support)
    add_dependencies(editor_gui_recovery_regression_suite editor_cards_api_098_tests)
    target_compile_definitions(editor_cards_api_098_tests PRIVATE "cards_api_094_095_098_run=cardwirth_cards_api_098_registered_run")
    add_test(NAME editor_cards_api_098_tests
        COMMAND ${editor_cards_api_098_tests_TEST_COMMAND} --case 098)
    set_tests_properties(editor_cards_api_098_tests PROPERTIES
        TIMEOUT 120 LABELS "editor;gui;recovery"
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
endif()


# GUI復旧ケース。実行経路の数を検証対象の件数として数えない。
if(BUILD_TESTING AND TARGET cwxeditor_gui AND TARGET cwxeditor_core AND TARGET cwxeditor_test_support AND TARGET editor_gui_recovery_regression_suite)
    cardwirth_add_bundled_editor_test(editor_cards_api_093_paths_tests WITH_ARGS
        tests/candidates/verified_20261012/recover_next_087_088_093/cards_api_093_d_directory_rename_candidate_rev14_portable_diagnostics.cpp)
    target_compile_features(editor_cards_api_093_paths_tests PRIVATE cxx_std_20)
    target_include_directories(editor_cards_api_093_paths_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(editor_cards_api_093_paths_tests PRIVATE cwxeditor_gui cwxeditor_core cwxeditor_test_support)
    add_dependencies(editor_gui_recovery_regression_suite editor_cards_api_093_paths_tests)
    add_test(NAME editor_cards_api_093_d_root_tests
        COMMAND ${editor_cards_api_093_paths_tests_TEST_COMMAND} root)
    set_tests_properties(editor_cards_api_093_d_root_tests PROPERTIES
        TIMEOUT 120 LABELS "editor;gui;recovery"
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    add_test(NAME editor_cards_api_093_d_child_tests
        COMMAND ${editor_cards_api_093_paths_tests_TEST_COMMAND} child)
    set_tests_properties(editor_cards_api_093_d_child_tests PROPERTIES
        TIMEOUT 120 LABELS "editor;gui;recovery"
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    add_test(NAME editor_cards_api_093_d_prefix_tests
        COMMAND ${editor_cards_api_093_paths_tests_TEST_COMMAND} prefix)
    set_tests_properties(editor_cards_api_093_d_prefix_tests PROPERTIES
        TIMEOUT 120 LABELS "editor;gui;recovery"
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    add_test(NAME editor_cards_api_093_deferred_browse_tests
        COMMAND ${editor_cards_api_093_paths_tests_TEST_COMMAND} browse)
    set_tests_properties(editor_cards_api_093_deferred_browse_tests PROPERTIES
        TIMEOUT 120 LABELS "editor;gui;recovery"
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    add_test(NAME editor_cards_api_093_deferred_rebind_tests
        COMMAND ${editor_cards_api_093_paths_tests_TEST_COMMAND} rebind)
    set_tests_properties(editor_cards_api_093_deferred_rebind_tests PROPERTIES
        TIMEOUT 120 LABELS "editor;gui;recovery"
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    add_test(NAME editor_cards_api_093_deferred_retire_tests
        COMMAND ${editor_cards_api_093_paths_tests_TEST_COMMAND} retire)
    set_tests_properties(editor_cards_api_093_deferred_retire_tests PROPERTIES
        TIMEOUT 120 LABELS "editor;gui;recovery"
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    cardwirth_add_bundled_editor_test(editor_cards_api_093_pending_draft_tests WITH_ARGS
        tests/candidates/verified_20261012/recover_next_087_088_093/cards_api_093_d_directory_rename_candidate_rev6_pending_draft.cpp)
    target_compile_features(editor_cards_api_093_pending_draft_tests PRIVATE cxx_std_20)
    target_include_directories(editor_cards_api_093_pending_draft_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(editor_cards_api_093_pending_draft_tests PRIVATE cwxeditor_gui cwxeditor_core cwxeditor_test_support)
    add_dependencies(editor_gui_recovery_regression_suite editor_cards_api_093_pending_draft_tests)
    add_test(NAME editor_cards_api_093_pending_draft_tests
        COMMAND ${editor_cards_api_093_pending_draft_tests_TEST_COMMAND} draft)
    set_tests_properties(editor_cards_api_093_pending_draft_tests PROPERTIES
        TIMEOUT 120 LABELS "editor;gui;recovery"
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    cardwirth_add_bundled_editor_test(editor_cards_api_093_rebind_close_tests WITH_ARGS
        tests/candidates/verified_20261012/recover_next_087_088_093/cards_api_093_lifecycle_rebind_close_supplement_rev3.cpp)
    target_compile_features(editor_cards_api_093_rebind_close_tests PRIVATE cxx_std_20)
    target_include_directories(editor_cards_api_093_rebind_close_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(editor_cards_api_093_rebind_close_tests PRIVATE cwxeditor_gui cwxeditor_core cwxeditor_test_support)
    add_dependencies(editor_gui_recovery_regression_suite editor_cards_api_093_rebind_close_tests)
    add_test(NAME editor_cards_api_093_rebind_close_tests
        COMMAND ${editor_cards_api_093_rebind_close_tests_TEST_COMMAND} rebind-close)
    set_tests_properties(editor_cards_api_093_rebind_close_tests PROPERTIES
        TIMEOUT 120 LABELS "editor;gui;recovery"
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    cardwirth_add_bundled_editor_test(editor_cards_api_093_retained_close_tests WITH_ARGS
        tests/candidates/verified_20261012/recover_next_087_088_093/cards_api_093_retained_mainwindow_close_supplement_rev3.cpp)
    target_compile_features(editor_cards_api_093_retained_close_tests PRIVATE cxx_std_20)
    target_include_directories(editor_cards_api_093_retained_close_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(editor_cards_api_093_retained_close_tests PRIVATE cwxeditor_gui cwxeditor_core cwxeditor_test_support)
    add_dependencies(editor_gui_recovery_regression_suite editor_cards_api_093_retained_close_tests)
    add_test(NAME editor_cards_api_093_retained_mainwindow_close_tests
        COMMAND ${editor_cards_api_093_retained_close_tests_TEST_COMMAND} retained-main-close)
    set_tests_properties(editor_cards_api_093_retained_mainwindow_close_tests PROPERTIES
        TIMEOUT 120 LABELS "editor;gui;recovery"
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    cardwirth_add_bundled_editor_test(editor_cards_api_089_092_mounted_tests WITH_ARGS
        tests/candidates/verified_20261012/recover_next_089_090_092/editor_cards_api_089_090_092_mounted_candidate_rev6_engine_root.cpp)
    target_compile_features(editor_cards_api_089_092_mounted_tests PRIVATE cxx_std_20)
    target_include_directories(editor_cards_api_089_092_mounted_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(editor_cards_api_089_092_mounted_tests PRIVATE cwxeditor_gui cwxeditor_core cwxeditor_test_support)
    add_dependencies(editor_gui_recovery_regression_suite editor_cards_api_089_092_mounted_tests)
    target_compile_definitions(editor_cards_api_089_092_mounted_tests PRIVATE "cards_api_089_090_092=cards_api_089_092_rev6")
    add_test(NAME editor_cards_api_089_mounted_tests
        COMMAND ${editor_cards_api_089_092_mounted_tests_TEST_COMMAND} 089)
    set_tests_properties(editor_cards_api_089_mounted_tests PROPERTIES
        TIMEOUT 120 LABELS "editor;gui;recovery"
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    add_test(NAME editor_cards_api_092_engine_directory_tests
        COMMAND ${editor_cards_api_089_092_mounted_tests_TEST_COMMAND} 092)
    set_tests_properties(editor_cards_api_092_engine_directory_tests PROPERTIES
        TIMEOUT 120 LABELS "editor;gui;recovery"
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    cardwirth_add_bundled_editor_test(editor_cards_api_090_mounted_tests WITH_ARGS
        tests/candidates/verified_20261012/recover_next_089_090_092/editor_cards_api_089_090_092_mounted_candidate_rev7_apply_boundary.cpp)
    target_compile_features(editor_cards_api_090_mounted_tests PRIVATE cxx_std_20)
    target_include_directories(editor_cards_api_090_mounted_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(editor_cards_api_090_mounted_tests PRIVATE cwxeditor_gui cwxeditor_core cwxeditor_test_support)
    add_dependencies(editor_gui_recovery_regression_suite editor_cards_api_090_mounted_tests)
    target_compile_definitions(editor_cards_api_090_mounted_tests PRIVATE "cards_api_089_090_092=cards_api_090_rev7")
    add_test(NAME editor_cards_api_090_all_gallery_tests
        COMMAND ${editor_cards_api_090_mounted_tests_TEST_COMMAND} 090)
    set_tests_properties(editor_cards_api_090_all_gallery_tests PROPERTIES
        TIMEOUT 120 LABELS "editor;gui;recovery"
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    cardwirth_add_bundled_editor_test(editor_cards_api_089_drop_directory_tests WITH_ARGS
        tests/candidates/verified_20261012/recover_next_089_090_092/editor_cards_api_089_drop_directory_transition_candidate_root_rev1.cpp)
    target_compile_features(editor_cards_api_089_drop_directory_tests PRIVATE cxx_std_20)
    target_include_directories(editor_cards_api_089_drop_directory_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(editor_cards_api_089_drop_directory_tests PRIVATE cwxeditor_gui cwxeditor_core cwxeditor_test_support)
    add_dependencies(editor_gui_recovery_regression_suite editor_cards_api_089_drop_directory_tests)
    add_test(NAME editor_cards_api_089_drop_none_tests
        COMMAND ${editor_cards_api_089_drop_directory_tests_TEST_COMMAND} none)
    set_tests_properties(editor_cards_api_089_drop_none_tests PROPERTIES
        TIMEOUT 120 LABELS "editor;gui;recovery"
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    add_test(NAME editor_cards_api_089_drop_root_tests
        COMMAND ${editor_cards_api_089_drop_directory_tests_TEST_COMMAND} root)
    set_tests_properties(editor_cards_api_089_drop_root_tests PROPERTIES
        TIMEOUT 120 LABELS "editor;gui;recovery"
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    cardwirth_add_bundled_editor_test(editor_cards_api_097_sound_glob_tests
        tests/candidates/verified_20261012/editor_cards_api_097_sound_directory_glob_tests.cpp)
    target_compile_features(editor_cards_api_097_sound_glob_tests PRIVATE cxx_std_20)
    target_include_directories(editor_cards_api_097_sound_glob_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(editor_cards_api_097_sound_glob_tests PRIVATE cwxeditor_gui cwxeditor_core cwxeditor_test_support)
    add_dependencies(editor_gui_recovery_regression_suite editor_cards_api_097_sound_glob_tests)
    add_test(NAME editor_cards_api_097_sound_glob_tests
        COMMAND ${editor_cards_api_097_sound_glob_tests_TEST_COMMAND})
    set_tests_properties(editor_cards_api_097_sound_glob_tests PROPERTIES
        TIMEOUT 120 LABELS "editor;gui;recovery"
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    cardwirth_add_bundled_editor_test(editor_cards_api_099_round_inline_before_add_many_tests
        tests/candidates/verified_20261012/editor_cards_api_099_round_inline_before_add_many_tests.cpp)
    target_compile_features(editor_cards_api_099_round_inline_before_add_many_tests PRIVATE cxx_std_20)
    target_include_directories(editor_cards_api_099_round_inline_before_add_many_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(editor_cards_api_099_round_inline_before_add_many_tests PRIVATE cwxeditor_gui cwxeditor_core cwxeditor_test_support)
    add_dependencies(editor_gui_recovery_regression_suite editor_cards_api_099_round_inline_before_add_many_tests)
    add_test(NAME editor_cards_api_099_round_inline_before_add_many_tests
        COMMAND ${editor_cards_api_099_round_inline_before_add_many_tests_TEST_COMMAND})
    set_tests_properties(editor_cards_api_099_round_inline_before_add_many_tests PROPERTIES
        TIMEOUT 120 LABELS "editor;gui;recovery"
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    cardwirth_add_bundled_editor_test(editor_cards_api_100_material_drop_tests
        tests/candidates/verified_20261012/editor_cards_api_100_material_drop_tests.cpp)
    target_compile_features(editor_cards_api_100_material_drop_tests PRIVATE cxx_std_20)
    target_include_directories(editor_cards_api_100_material_drop_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(editor_cards_api_100_material_drop_tests PRIVATE cwxeditor_gui cwxeditor_core cwxeditor_test_support)
    add_dependencies(editor_gui_recovery_regression_suite editor_cards_api_100_material_drop_tests)
    add_test(NAME editor_cards_api_100_material_drop_tests
        COMMAND ${editor_cards_api_100_material_drop_tests_TEST_COMMAND})
    set_tests_properties(editor_cards_api_100_material_drop_tests PROPERTIES
        TIMEOUT 120 LABELS "editor;gui;recovery"
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
endif()


# カードAPIの受け入れ確認後に有効にする復旧テストの条件付き登録。
if(BUILD_TESTING AND TARGET cwxeditor_gui AND TARGET cwxeditor_core AND TARGET cwxeditor_test_support AND TARGET editor_gui_recovery_regression_suite)
    cardwirth_add_bundled_editor_test(editor_cards_api_035_039_040_mounted_tests WITH_ARGS
        tests/candidates/verified_20261012/editor_cards_api_035_039_040_mounted_tests.cpp)
    target_compile_features(editor_cards_api_035_039_040_mounted_tests PRIVATE cxx_std_20)
    target_include_directories(editor_cards_api_035_039_040_mounted_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(editor_cards_api_035_039_040_mounted_tests PRIVATE cwxeditor_gui cwxeditor_core cwxeditor_test_support)
    add_dependencies(editor_gui_recovery_regression_suite editor_cards_api_035_039_040_mounted_tests)
    cardwirth_add_bundled_editor_test(editor_cards_api_038_bg_image_mask_tests WITH_ARGS
        tests/candidates/verified_20261012/editor_cards_api_038_bg_image_mask_tests.cpp)
    target_compile_features(editor_cards_api_038_bg_image_mask_tests PRIVATE cxx_std_20)
    target_include_directories(editor_cards_api_038_bg_image_mask_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(editor_cards_api_038_bg_image_mask_tests PRIVATE cwxeditor_gui cwxeditor_core cwxeditor_test_support)
    add_dependencies(editor_gui_recovery_regression_suite editor_cards_api_038_bg_image_mask_tests)
    cardwirth_add_bundled_editor_test(editor_cards_api_041_045_047_readonly_effect_dialog_tests WITH_ARGS
        tests/candidates/verified_20261012/editor_cards_api_041_045_047_readonly_effect_dialog_tests.cpp)
    target_compile_features(editor_cards_api_041_045_047_readonly_effect_dialog_tests PRIVATE cxx_std_20)
    target_include_directories(editor_cards_api_041_045_047_readonly_effect_dialog_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(editor_cards_api_041_045_047_readonly_effect_dialog_tests PRIVATE cwxeditor_gui cwxeditor_core cwxeditor_test_support)
    add_dependencies(editor_gui_recovery_regression_suite editor_cards_api_041_045_047_readonly_effect_dialog_tests)
    cardwirth_add_bundled_editor_test(editor_cards_api_042_skin_change_tests WITH_ARGS
        tests/candidates/verified_20261012/editor_cards_api_042_skin_change_tests.cpp)
    target_compile_features(editor_cards_api_042_skin_change_tests PRIVATE cxx_std_20)
    target_include_directories(editor_cards_api_042_skin_change_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(editor_cards_api_042_skin_change_tests PRIVATE cwxeditor_gui cwxeditor_core cwxeditor_test_support)
    add_dependencies(editor_gui_recovery_regression_suite editor_cards_api_042_skin_change_tests)
    cardwirth_add_bundled_editor_test(editor_cards_api_042_wrapper_skin_tests
        tests/candidates/verified_20261012/editor_cards_api_042_wrapper_skin_tests.cpp)
    target_compile_features(editor_cards_api_042_wrapper_skin_tests PRIVATE cxx_std_20)
    target_include_directories(editor_cards_api_042_wrapper_skin_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(editor_cards_api_042_wrapper_skin_tests PRIVATE cwxeditor_gui cwxeditor_core cwxeditor_test_support)
    add_dependencies(editor_gui_recovery_regression_suite editor_cards_api_042_wrapper_skin_tests)
    add_test(NAME editor_cards_api_035_tests
        COMMAND ${editor_cards_api_035_039_040_mounted_tests_TEST_COMMAND} --case CARDS-API-035)
    set_tests_properties(editor_cards_api_035_tests PROPERTIES
        TIMEOUT 120 LABELS "editor;gui;recovery"
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    add_test(NAME editor_cards_api_039_tests
        COMMAND ${editor_cards_api_035_039_040_mounted_tests_TEST_COMMAND} --case CARDS-API-039)
    set_tests_properties(editor_cards_api_039_tests PROPERTIES
        TIMEOUT 120 LABELS "editor;gui;recovery"
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    add_test(NAME editor_cards_api_040_tests
        COMMAND ${editor_cards_api_035_039_040_mounted_tests_TEST_COMMAND} --case CARDS-API-040)
    set_tests_properties(editor_cards_api_040_tests PROPERTIES
        TIMEOUT 120 LABELS "editor;gui;recovery"
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    add_test(NAME editor_cards_api_038_jpg_tests
        COMMAND ${editor_cards_api_038_bg_image_mask_tests_TEST_COMMAND} jpg)
    set_tests_properties(editor_cards_api_038_jpg_tests PROPERTIES
        TIMEOUT 120 LABELS "editor;gui;recovery"
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    add_test(NAME editor_cards_api_038_jpeg_tests
        COMMAND ${editor_cards_api_038_bg_image_mask_tests_TEST_COMMAND} jpeg)
    set_tests_properties(editor_cards_api_038_jpeg_tests PROPERTIES
        TIMEOUT 120 LABELS "editor;gui;recovery"
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    add_test(NAME editor_cards_api_038_bmp_tests
        COMMAND ${editor_cards_api_038_bg_image_mask_tests_TEST_COMMAND} bmp)
    set_tests_properties(editor_cards_api_038_bmp_tests PROPERTIES
        TIMEOUT 120 LABELS "editor;gui;recovery"
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    add_test(NAME editor_cards_api_038_jpg_maskoff_tests
        COMMAND ${editor_cards_api_038_bg_image_mask_tests_TEST_COMMAND} jpg-maskoff)
    set_tests_properties(editor_cards_api_038_jpg_maskoff_tests PROPERTIES
        TIMEOUT 120 LABELS "editor;gui;recovery"
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    add_test(NAME editor_cards_api_038_jpeg_maskoff_tests
        COMMAND ${editor_cards_api_038_bg_image_mask_tests_TEST_COMMAND} jpeg-maskoff)
    set_tests_properties(editor_cards_api_038_jpeg_maskoff_tests PROPERTIES
        TIMEOUT 120 LABELS "editor;gui;recovery"
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    add_test(NAME editor_cards_api_038_palette_maskoff_tests
        COMMAND ${editor_cards_api_038_bg_image_mask_tests_TEST_COMMAND} palette-maskoff)
    set_tests_properties(editor_cards_api_038_palette_maskoff_tests PROPERTIES
        TIMEOUT 120 LABELS "editor;gui;recovery"
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    add_test(NAME editor_cards_api_041_tests
        COMMAND ${editor_cards_api_041_045_047_readonly_effect_dialog_tests_TEST_COMMAND} --case CARDS-API-041)
    set_tests_properties(editor_cards_api_041_tests PROPERTIES
        TIMEOUT 120 LABELS "editor;gui;recovery"
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    add_test(NAME editor_cards_api_045_tests
        COMMAND ${editor_cards_api_041_045_047_readonly_effect_dialog_tests_TEST_COMMAND} --case CARDS-API-045)
    set_tests_properties(editor_cards_api_045_tests PROPERTIES
        TIMEOUT 120 LABELS "editor;gui;recovery"
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    add_test(NAME editor_cards_api_047_tests
        COMMAND ${editor_cards_api_041_045_047_readonly_effect_dialog_tests_TEST_COMMAND} --case CARDS-API-047)
    set_tests_properties(editor_cards_api_047_tests PROPERTIES
        TIMEOUT 120 LABELS "editor;gui;recovery"
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    add_test(NAME editor_cards_api_042_tests
        COMMAND ${editor_cards_api_042_skin_change_tests_TEST_COMMAND} 042)
    set_tests_properties(editor_cards_api_042_tests PROPERTIES
        TIMEOUT 120 LABELS "editor;gui;recovery"
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    add_test(NAME editor_cards_api_042_wrapper_skin_tests
        COMMAND ${editor_cards_api_042_wrapper_skin_tests_TEST_COMMAND} )
    set_tests_properties(editor_cards_api_042_wrapper_skin_tests PROPERTIES
        TIMEOUT 120 LABELS "editor;gui;recovery"
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
endif()

# 実行ファイルのアイコン取得処理は、すべてのホストのPartyHistoryで共有する。
if(BUILD_TESTING AND TARGET cwxeditor_gui)
    cardwirth_add_bundled_editor_test(editor_partyhistory_icon_async_tests
        tests/candidates/verified_20261024/partyhistory5_icon_async_tests.cpp)
    target_compile_features(editor_partyhistory_icon_async_tests PRIVATE cxx_std_20)
    target_include_directories(editor_partyhistory_icon_async_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(editor_partyhistory_icon_async_tests PRIVATE cwxeditor_gui cwxeditor_core cwxeditor_test_support)
    add_test(NAME editor_partyhistory_icon_async_tests COMMAND ${editor_partyhistory_icon_async_tests_TEST_COMMAND})
    set_tests_properties(editor_partyhistory_icon_async_tests PROPERTIES
        TIMEOUT 120 LABELS "editor;gui;recovery")
endif()

cardwirth_finalize_editor_test_bundle()
