# Emscripten は明示的に有効にしないと C++ の catch・後処理コードをコン
# パイル時に除く。リンク時だけの DISABLE_EXCEPTION_CATCHING=0
# では戻せない。両 Web モードで、アプリ・ライブラリーのターゲットを作る前に適用する。
if(EMSCRIPTEN)
    add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-fexceptions>")
    add_link_options(-fexceptions)
endif()
