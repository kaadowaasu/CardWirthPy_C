#include "cw/lib/system_clipboard.hpp"

#include <SDL3/SDL.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

namespace cw::lib {
namespace {

struct ClipboardReadRequest { std::string value; };
void SDLCALL read_clipboard_on_main(void* userdata) {
    auto& request=*static_cast<ClipboardReadRequest*>(userdata);
    char* text=SDL_GetClipboardText();
    if(text) request.value=text;
    SDL_free(text);
}
struct ClipboardWriteRequest { const std::string* value{}; };
void SDLCALL write_clipboard_on_main(void* userdata) {
    const auto& request=*static_cast<ClipboardWriteRequest*>(userdata);
    if(request.value)(void)SDL_SetClipboardText(request.value->c_str());
}
#if defined(__EMSCRIPTEN__)
void write_clipboard_in_browser(const std::string& text) {
    // SDL の Emscripten 描画ドライバーにはクリップボ
    // ードのコールバックがない。 ネイティブダイアログと同じ共通境
    // 界を維持しつつ、ブラウザーのユーザー操作時の経路を直接使う。
    MAIN_THREAD_EM_ASM({
        const value=UTF8ToString($0,$1);
        let copied=false;
        const active=document.activeElement;
        try {
            const area=document.createElement("textarea");
            area.value=value;
            area.setAttribute("readonly","");
            area.style.position="fixed";
            area.style.left="-9999px";
            area.style.top="0";
            area.style.opacity="0";
            document.body.appendChild(area);
            area.focus();
            area.select();
            copied=document.execCommand("copy");
            area.remove();
        } catch (_) {}
        if (active && typeof active.focus === "function") active.focus();
        if (!copied && navigator.clipboard && typeof navigator.clipboard.writeText === "function") {
            try {
                const pending=navigator.clipboard.writeText(value);
                if (pending && typeof pending.catch === "function") pending.catch(() => {});
            } catch (_) {}
        }
    },text.data(),text.size());
}
#endif

}  // namespace

std::string system_clipboard_text() {
    ClipboardReadRequest request;
    if(SDL_IsMainThread()) read_clipboard_on_main(&request);
    else if(!SDL_RunOnMainThread(read_clipboard_on_main,&request,true)) return {};
    return request.value;
}

void set_system_clipboard_text(const std::string& text) {
    // 貼り付け用に SDL のプロセス内クリップボード状態も保持する。 Web の
    // Emscripten 描画ドライバーにはブラウザーへの書き込みコールバックがな
    // いため、 同じ値を DOM の接続経路からシステムのクリップボードへ反映する。
    ClipboardWriteRequest request{&text};
    if(SDL_IsMainThread()) write_clipboard_on_main(&request);
    else (void)SDL_RunOnMainThread(write_clipboard_on_main,&request,true);
#if defined(__EMSCRIPTEN__)
    write_clipboard_in_browser(text);
#endif
}

}  // namespace cw::lib
