#include "app/all/all_dialog_layer.hpp"
#include "cw/dialog/dialog.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <map>
#include <string_view>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace cardwirth::app::all {

class AllDialogLayer::Impl final {
public:
    std::uintptr_t host{};
    Rect viewport{};
    Rect debugger_viewport{};
    float engine_scale{1.0F};
#ifdef _WIN32
    std::vector<HWND> dialogs{};
    std::map<HWND, std::pair<int, int>> preferred_sizes{};
    std::map<HWND, RECT> last_host_bounds{};

    static bool is_debugger(HWND window) noexcept {
        if (!window || !IsWindow(window)) return false;
        wchar_t class_name[128]{};
        GetClassNameW(window, class_name, static_cast<int>(std::size(class_name)));
        return std::wstring_view(class_name) == L"CardWirthDebuggerWindow";
    }

    static BOOL CALLBACK discover(HWND window, LPARAM parameter) {
        auto* self = reinterpret_cast<Impl*>(parameter);
        if (!self || !window || reinterpret_cast<std::uintptr_t>(window) == self->host)
            return TRUE;
        DWORD process_id = 0;
        GetWindowThreadProcessId(window, &process_id);
        if (process_id != GetCurrentProcessId()) return TRUE;
        if (!IsWindowVisible(window)) return TRUE;
        const HWND host_window = reinterpret_cast<HWND>(self->host);
        wchar_t class_name[128]{};
        GetClassNameW(window, class_name, static_cast<int>(std::size(class_name)));
        // 外部ウィンドウで表示するのは、Windowsのファイル選択などOS標準のダイアロ
        // グのみ。 CardWirthのダイアログは固有のウィンドウクラス名で識別する。
        const std::wstring_view window_class(class_name);
        if (window_class == L"#32770" || window_class == L"IME" ||
            window_class == L"MSCTFIME UI" ||
            window_class == L"CardWirthSDLFrame")
            return TRUE;
        const bool cardwirth_dialog = window_class.starts_with(L"CardWirth");
        // EnumChildWindowsはすべての子孫を列挙するため、
        // ALLへ移すのはダイアログ本体だけとする。 CardWirthYadoCanvas
        // などの描画部品まで個別に親を変更すると、 ボタンの
        // 配置は正しくても画像だけがずれたり欠けたりする。
        const LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
        const LONG_PTR ex_style = GetWindowLongPtrW(window, GWL_EXSTYLE);
        const bool child_window = (style & WS_CHILD) != 0;
        // ALLで処理する前に、別のEngineウィンドウの子に
        // なっているダイアログもある。 通常の入れ子の描画領域は
        // 除外するが、ダイアログ固有の非クライアントスタイルを
        // 持つウィンドウは残し、共通処理で親子関係を整える。
        const bool framed_dialog = (style & (WS_CAPTION | WS_SYSMENU | WS_THICKFRAME)) != 0 ||
            (ex_style & WS_EX_DLGMODALFRAME) != 0;
        if (child_window && GetParent(window) != host_window && !framed_dialog) return TRUE;
        const bool owned_modal = GetWindow(window, GW_OWNER) == host_window &&
            (ex_style & WS_EX_DLGMODALFRAME) != 0;
        if (!cardwirth_dialog && !owned_modal)
            return TRUE;
        if (std::find(self->dialogs.begin(), self->dialogs.end(), window) ==
            self->dialogs.end()) {
            self->dialogs.push_back(window);
            if (const auto size = ::cw::dialog::normalize_all_mode_window(
                    reinterpret_cast<std::uintptr_t>(window), self->host))
                self->preferred_sizes[window] = *size;
        }
        return TRUE;
    }

    void place(HWND window) {
        const HWND host_window = reinterpret_cast<HWND>(host);
        if (!host_window || !IsWindow(host_window) || !IsWindow(window)) return;
        if (const auto size = ::cw::dialog::normalize_all_mode_window(
                reinterpret_cast<std::uintptr_t>(window), host))
            preferred_sizes.try_emplace(window, *size);
        // ダイアログの実装はモーダル表示中に所有者を一時的に無効化する。
        // 子ウィンドウへ移した後は、ALL側でモーダル入力を制御する。
        EnableWindow(host_window, TRUE);

        RECT client{};
        RECT bounds{};
        if (!GetClientRect(host_window, &client) || !GetWindowRect(window, &bounds)) return;
        const float scale_x = static_cast<float>(client.right - client.left) /
                              static_cast<float>(kAllPcDesignWidth);
        const float scale_y = static_cast<float>(client.bottom - client.top) /
                              static_cast<float>(kAllPcDesignHeight);
        const bool debugger = is_debugger(window);
        int width = static_cast<int>(std::max<LONG>(1, bounds.right - bounds.left));
        int height = static_cast<int>(std::max<LONG>(1, bounds.bottom - bounds.top));
        if (const auto preferred = preferred_sizes.find(window);
            preferred != preferred_sizes.end()) {
            width = preferred->second.first;
            height = preferred->second.second;
        }
        const int host_w = static_cast<int>(std::max<LONG>(1, client.right - client.left));
        const int host_h = static_cast<int>(std::max<LONG>(1, client.bottom - client.top));
        const auto placement = plan_engine_dialog_placement({
            {0.0F, 0.0F, static_cast<float>(host_w), static_cast<float>(host_h)},
            native_pixel_rect(viewport, scale_x, scale_y),
            native_pixel_rect(debugger_viewport, scale_x, scale_y),
            {static_cast<float>(width), static_cast<float>(height)}, debugger});
        if (!placement.rect.valid()) return;
        const int x = static_cast<int>(std::lround(placement.rect.x));
        const int y = static_cast<int>(std::lround(placement.rect.y));
        width = std::max(1, static_cast<int>(std::lround(placement.rect.width)));
        height = std::max(1, static_cast<int>(std::lround(placement.rect.height)));
        POINT host_origin{};
        ClientToScreen(host_window, &host_origin);
        const int current_x = bounds.left - host_origin.x;
        const int current_y = bounds.top - host_origin.y;
        const int current_w = bounds.right - bounds.left;
        const int current_h = bounds.bottom - bounds.top;
        if (current_x != x || current_y != y || current_w != width || current_h != height) {
            UINT flags = SWP_NOACTIVATE | SWP_NOOWNERZORDER;
            // サイズ変更前のクライアント領域の画像は引き伸ばさず、
            // コピーもしない。 古い画像を使うと右端や下端に1フレ
            // ーム分の未描画領域が残るため、 共通の不透明な背景
            // と子コントロールを新しいサイズでまとめて描き直す。
            if (current_w != width || current_h != height)
                flags |= SWP_NOCOPYBITS;
            SetWindowPos(window, HWND_TOP, x, y, width, height,
                         flags);
            RedrawWindow(window, nullptr, nullptr,
                         RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
        }
        last_host_bounds[window] = RECT{x, y, x + width, y + height};
    }
#endif
};

AllDialogLayer::AllDialogLayer() : impl_(std::make_unique<Impl>()) {}
AllDialogLayer::~AllDialogLayer() = default;

void AllDialogLayer::update(std::uintptr_t native_host_window, Rect engine_viewport,
                            Rect debugger_viewport, float engine_scale) {
    impl_->host = native_host_window;
    impl_->viewport = engine_viewport;
    impl_->debugger_viewport = debugger_viewport;
    impl_->engine_scale = std::isfinite(engine_scale) && engine_scale > 0.0F
        ? engine_scale : 1.0F;
#ifdef _WIN32
    if (const HWND host = reinterpret_cast<HWND>(native_host_window);
        host && IsWindow(host)) {
        RECT client{};
        if (GetClientRect(host, &client)) {
            const float scale_x = static_cast<float>(client.right - client.left) /
                                  static_cast<float>(kAllPcDesignWidth);
            const float scale_y = static_cast<float>(client.bottom - client.top) /
                                  static_cast<float>(kAllPcDesignHeight);
            const auto native_viewport = [scale_x, scale_y](const Rect rect) {
                const auto pixels = native_pixel_rect(rect, scale_x, scale_y);
                return ::cw::dialog::AllModeNativeViewport{
                    static_cast<int>(pixels.x), static_cast<int>(pixels.y),
                    static_cast<int>(pixels.width), static_cast<int>(pixels.height)};
            };
            ::cw::dialog::set_all_mode_native_viewports(
                native_viewport(engine_viewport),
                debugger_viewport.valid() ? native_viewport(debugger_viewport)
                                          : ::cw::dialog::AllModeNativeViewport{});
        }
    }
    std::vector<RECT> exposed_regions;
    std::erase_if(impl_->dialogs, [this, &exposed_regions](HWND window) {
        if (IsWindow(window) && IsWindowVisible(window)) return false;
        if (const auto bounds = impl_->last_host_bounds.find(window);
            bounds != impl_->last_host_bounds.end()) {
            // ネイティブ子ウィンドウを破棄したり親を変更したり
            // しても、元の領域が再描画されるとは限らない。 HWND
            // を手放す前に露出する矩形を記録し、ALLのEngine
            // 画面を復元する。 これにより、閉じたダイア
            // ログのコントロールが画面に残るのを防ぐ。
            exposed_regions.push_back(bounds->second);
        }
        impl_->preferred_sizes.erase(window);
        impl_->last_host_bounds.erase(window);
        return true;
    });
    if (!exposed_regions.empty()) {
        if (const HWND host = reinterpret_cast<HWND>(native_host_window);
            host && IsWindow(host)) {
            // 透過する子孫コントロールは、記録したダイア
            // ログ矩形の外へ描画される場合がある。 閉じ
            // る際はALL全体を描き直し、シナリオ選択
            // や設定画面の表示が部分的に残るのを防ぐ。
            RedrawWindow(host, nullptr, nullptr,
                         RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN |
                             RDW_UPDATENOW);
        }
    }
    if (!native_host_window || !engine_viewport.valid()) return;
    EnumWindows(&Impl::discover, reinterpret_cast<LPARAM>(impl_.get()));
    EnumChildWindows(reinterpret_cast<HWND>(native_host_window), &Impl::discover,
                     reinterpret_cast<LPARAM>(impl_.get()));
    const auto dialogs = impl_->dialogs;
    for (const auto window : dialogs) impl_->place(window);
#else
    (void)native_host_window;
    (void)engine_viewport;
#endif
}

void AllDialogLayer::clear() noexcept {
#ifdef _WIN32
    impl_->dialogs.clear();
    impl_->preferred_sizes.clear();
    impl_->last_host_bounds.clear();
#endif
    impl_->host = 0;
    impl_->viewport = {};
    impl_->debugger_viewport = {};
    impl_->engine_scale = 1.0F;
}

bool AllDialogLayer::native_engine_dialog_active() const noexcept {
#ifdef _WIN32
    return std::any_of(impl_->dialogs.begin(), impl_->dialogs.end(), [](HWND window) {
        // Release版のデバッガーはモードレスで、ALLとは配
        // 置と重なり順のみを共有する。 Engineの入力処理では
        // 、デバッガーの表示をモーダルダイアログとして扱わない。
        return IsWindow(window) && IsWindowVisible(window) &&
               !Impl::is_debugger(window);
    });
#else
    return false;
#endif
}

std::optional<Size> AllDialogLayer::native_engine_dialog_intrinsic_size() const noexcept {
#ifdef _WIN32
    std::optional<Size> result;
    for (const auto window : impl_->dialogs) {
        if (!IsWindow(window) || !IsWindowVisible(window) || Impl::is_debugger(window))
            continue;
        const auto preferred = impl_->preferred_sizes.find(window);
        if (preferred == impl_->preferred_sizes.end()) continue;
        if (!result) result = Size{};
        result->width = std::max(result->width,
            static_cast<float>(preferred->second.first));
        result->height = std::max(result->height,
            static_cast<float>(preferred->second.second));
    }
    if (result && result->valid()) return result;
#endif
    return std::nullopt;
}

}  // namespace cardwirth::app::all
