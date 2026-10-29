# SDL_mixer は管理対象外の固定版依存。OS 向けの補
# 正をリポジトリに保持し、再取得後、コンパイル前に適用する。
find_program(CARDWIRTH_PATCH_GIT git REQUIRED)
set(sdl "${CMAKE_CURRENT_LIST_DIR}/../3rdparty/SDL_mixer")
set(patches
    "${CMAKE_CURRENT_LIST_DIR}/../platform/patches/sdl-mixer-cancel-fade-in.patch")

# SDL の管理対象外のチェックアウトは共有され
# る。検査・適用を直列化し、設定処理と依存取得
# が異なるパッチ群を部分適用しないようにする。
file(LOCK "${sdl}/.cardwirth-patches.lock" GUARD PROCESS TIMEOUT 60)
foreach(patch IN LISTS patches)
    # 管理対象外の SDL はパッチと改行形式が異なる場
    # 合がある。適用済みの判定では空白の違いを無視する。
    execute_process(COMMAND "${CARDWIRTH_PATCH_GIT}" -C "${sdl}" apply --reverse --check --ignore-space-change "${patch}"
        RESULT_VARIABLE already_applied OUTPUT_QUIET ERROR_QUIET)
    if(NOT already_applied EQUAL 0)
        execute_process(COMMAND "${CARDWIRTH_PATCH_GIT}" -C "${sdl}" apply --check "${patch}"
            RESULT_VARIABLE can_apply ERROR_VARIABLE detail)
        if(NOT can_apply EQUAL 0)
            message(FATAL_ERROR "SDL patch does not match the dependency: ${patch}; no files changed: ${detail}")
        endif()
        execute_process(COMMAND "${CARDWIRTH_PATCH_GIT}" -C "${sdl}" apply "${patch}"
            RESULT_VARIABLE applied ERROR_VARIABLE detail)
        if(NOT applied EQUAL 0)
            message(FATAL_ERROR "SDL patch failed: ${patch}: ${detail}")
        endif()
    endif()
endforeach()
file(LOCK "${sdl}/.cardwirth-patches.lock" RELEASE)
