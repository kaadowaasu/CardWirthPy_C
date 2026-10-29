#pragma once

// UIの実装は一つに統一し、
// 別経路の実装を設けない。

// cw/dialog/__init__.py の C++20 移植。
#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"

namespace cw::dialog {
extern std::vector<::cw::lib::String> g_py_all;

class DialogBackend {
public:
    virtual ~DialogBackend() = default;
    virtual void attach_dc(const std::shared_ptr<::cw::lib::DeviceContext>& arg_dc,
                           const std::shared_ptr<::cw::lib::Window>& arg_window) = 0;
    virtual ::cw::lib::Value select_bitmap(const std::shared_ptr<::cw::lib::DeviceContext>& arg_dc,
                                            const std::shared_ptr<::cw::lib::Bitmap>& arg_bitmap) = 0;
    virtual ::cw::lib::Value draw_bitmap(const std::shared_ptr<::cw::lib::DeviceContext>& arg_dc,
                                          const std::shared_ptr<::cw::lib::Bitmap>& arg_bitmap,
                                          const ::cw::lib::Arguments& arg_args,
                                          const ::cw::lib::KeywordArguments& arg_kwargs) = 0;
    virtual ::cw::lib::Value set_bitmap(const std::shared_ptr<::cw::lib::Window>& arg_window,
                                         const ::cw::lib::String& arg_attr,
                                         const std::shared_ptr<::cw::lib::Bitmap>& arg_bitmap) = 0;
    virtual void refresh(const std::shared_ptr<::cw::lib::Window>& arg_window) = 0;
    virtual bool show(const std::shared_ptr<::cw::lib::Dialog>& arg_dialog, bool arg_shown) = 0;
    virtual ::cw::lib::Integer show_modal(const std::shared_ptr<::cw::lib::Dialog>& arg_dialog) = 0;
    virtual bool destroy(const std::shared_ptr<::cw::lib::Dialog>& arg_dialog) = 0;
};

std::shared_ptr<DialogBackend> get_dialog_backend();
std::shared_ptr<DialogBackend> create_default_dialog_backend();

// 共通ホストモード。基底ダイアログ層と配置処理が参照するほか、
// 個別の共通表示でもALL用の配置を選択する。
void set_all_mode(bool arg_enabled, std::uintptr_t arg_host_window = 0) noexcept;
[[nodiscard]] bool is_all_mode() noexcept;
[[nodiscard]] std::uintptr_t all_mode_host_window() noexcept;
struct AllModeNativeViewport {
    int x{};
    int y{};
    int width{};
    int height{};
    [[nodiscard]] bool valid() const noexcept { return width > 0 && height > 0; }
};
void set_all_mode_native_viewports(AllModeNativeViewport arg_engine,
                                   AllModeNativeViewport arg_debugger = {}) noexcept;
[[nodiscard]] std::optional<AllModeNativeViewport>
all_mode_native_viewport(bool arg_debugger = false) noexcept;
[[nodiscard]] std::optional<std::pair<int, int>> normalize_all_mode_window(
    std::uintptr_t arg_window, std::uintptr_t arg_parent = 0) noexcept;
void pump_all_mode_events() noexcept;

// wx.Dialog を置き換えるネイティブウィンドウは、
// 不透明で中立色のクライアント面から始める。単体版 Engine
// と ALL が同じ基底ダイアログの初期化を使い、個
// 々のダイアログに ALL 専用の描画分岐を設けない。
enum class NativeDialogBackground {
    button_face,
    window,
};
void initialize_native_dialog_background(
    std::uintptr_t arg_window,
    NativeDialogBackground arg_background = NativeDialogBackground::button_face) noexcept;
void paint_native_dialog_background(std::uintptr_t arg_window,
                                    std::uintptr_t arg_device_context,
                                    NativeDialogBackground arg_background = NativeDialogBackground::button_face) noexcept;

void _install_modal_video_updater();
std::shared_ptr<::cw::lib::Bitmap> _install_modal_video_updater_resolve_bitmap(std::shared_ptr<::cw::lib::Bitmap> arg_bitmap);
void _install_modal_video_updater_patch_dc_init(::cw::lib::Value arg_cls);
void _install_modal_video_updater_patch_dc_init___init__(std::shared_ptr<::cw::lib::DeviceContext> arg_self, const ::cw::lib::Arguments& arg_args, const ::cw::lib::KeywordArguments& arg_kwargs);
::cw::lib::Value _install_modal_video_updater_get_dc_window(std::shared_ptr<::cw::lib::DeviceContext> arg_dc);
void _install_modal_video_updater_register_drawn_bitmap(std::shared_ptr<::cw::lib::DeviceContext> arg_dc, std::shared_ptr<::cw::lib::Bitmap> arg_bitmap);
void _install_modal_video_updater_patch_memorydc_selectobject();
void _install_modal_video_updater_patch_memorydc_selectobject___init__(std::shared_ptr<::cw::lib::DeviceContext> arg_self, const ::cw::lib::Arguments& arg_args, const ::cw::lib::KeywordArguments& arg_kwargs);
::cw::lib::Value _install_modal_video_updater_patch_memorydc_selectobject_SelectObject(std::shared_ptr<::cw::lib::DeviceContext> arg_self, std::shared_ptr<::cw::lib::Bitmap> arg_bitmap);
void _install_modal_video_updater_patch_drawbitmap(::cw::lib::Value arg_cls);
::cw::lib::Value _install_modal_video_updater_patch_drawbitmap_DrawBitmap(std::shared_ptr<::cw::lib::DeviceContext> arg_self, std::shared_ptr<::cw::lib::Bitmap> arg_bitmap, const ::cw::lib::Arguments& arg_args, const ::cw::lib::KeywordArguments& arg_kwargs);
void _install_modal_video_updater_patch_bitmap_setter(::cw::lib::Value arg_cls, ::cw::lib::String arg_name, ::cw::lib::String arg_attr);
::cw::lib::Value _install_modal_video_updater_patch_bitmap_setter_setter(std::shared_ptr<::cw::lib::Window> arg_self, std::shared_ptr<::cw::lib::Bitmap> arg_bitmap, const ::cw::lib::Arguments& arg_args, const ::cw::lib::KeywordArguments& arg_kwargs);
bool _install_modal_video_updater_refresh_animated_bitmaps(std::shared_ptr<::cw::lib::Window> arg_window);
bool _install_modal_video_updater_refresh_painted_animated_bitmaps(std::shared_ptr<::cw::lib::Window> arg_window);
void _install_modal_video_updater_release_animated_bitmaps(std::shared_ptr<::cw::lib::Window> arg_window);
void _install_modal_video_updater_update_dialog_video(std::shared_ptr<::cw::lib::Dialog> arg_window);
void _install_modal_video_updater_request_main_video_update();
void _install_modal_video_updater_request_main_video_update_update();
void _install_modal_video_updater_update_all_dialog_videos();
void _install_modal_video_updater_start_dialog_video_timer(std::shared_ptr<::cw::lib::Dialog> arg_window);
void _install_modal_video_updater_start_dialog_video_timer_on_timer(std::shared_ptr<::cw::lib::Window> arg_event);
void _install_modal_video_updater_stop_dialog_video_timer(std::shared_ptr<::cw::lib::Dialog> arg_window, bool arg_release);
// Engine の実行環境と UI ディスパッチャーの破棄前に、セッションが
// 所有する動画スケジューラーを停止する。起動途中でも繰り返し安全に呼べる。
void shutdown_dialog_video_updater();
bool _install_modal_video_updater_Show(std::shared_ptr<::cw::lib::Dialog> arg_self, const ::cw::lib::Arguments& arg_args, const ::cw::lib::KeywordArguments& arg_kwargs);
::cw::lib::Integer _install_modal_video_updater_ShowModal(std::shared_ptr<::cw::lib::Dialog> arg_self);
bool _install_modal_video_updater_Destroy(std::shared_ptr<::cw::lib::Dialog> arg_self);
void main();
}

