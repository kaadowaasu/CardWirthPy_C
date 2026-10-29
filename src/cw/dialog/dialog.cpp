// UIの実装は一つに統一し、
// 別経路の実装を設けない。
#include "cw/dialog/dialog.hpp"
#include "cw/dialog/completed_frame.hpp"

#include "cw/animatedimage.hpp"
#include "cw/cw.hpp"
#include "cw/frame.hpp"
#include "cw/thread.hpp"
#include "cw/util.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#endif

namespace cw::dialog {

std::vector<::cw::lib::String> g_py_all{
    "message", "cardinfo", "cardcontrol", "select", "scenarioselect", "scenarioinstall", "charainfo", "text",
    "create", "edit", "etc", "settings", "skin", "skininstall", "partyrecord", "transfer", "progress"};

namespace {

using Bitmap = ::cw::lib::Bitmap;
using DeviceContext = ::cw::lib::DeviceContext;
using Dialog = ::cw::lib::Dialog;
using Object = ::cw::lib::Object;
using Window = ::cw::lib::Window;
using Frame = ::cw::frame::Frame;
using Value = ::cw::lib::Value;
using String = ::cw::lib::String;
using Integer = ::cw::lib::Integer;

struct BitmapEntry {
    std::shared_ptr<Bitmap> bitmap{};
    Integer frame_id{};
};
using BitmapMap = std::map<const Bitmap*, BitmapEntry>;

std::mutex g_video_mutex;
bool g_installed{};
std::atomic_bool g_all_mode{};
std::atomic<std::uintptr_t> g_all_mode_host_window{};
std::mutex g_all_mode_viewport_mutex;
AllModeNativeViewport g_all_mode_engine_viewport{};
AllModeNativeViewport g_all_mode_debugger_viewport{};
std::vector<std::weak_ptr<Dialog>> g_dialogs;
std::shared_ptr<DialogBackend> g_dialog_backend;
std::shared_ptr<std::atomic_bool> g_video_timer_stop;
std::thread g_video_timer_thread;

struct VideoTimerShutdown {
    ~VideoTimerShutdown() {
        if (g_video_timer_stop)
            g_video_timer_stop->store(true, std::memory_order_release);
        if (g_video_timer_thread.joinable()) g_video_timer_thread.join();
    }
} g_video_timer_shutdown;

// wx はこれらを DC/Window インスタンスの動的属性として保持
// する。 SDL の互換オブジェクトには動的属性辞書がないため、オブジ
// ェクトの識別情報をキーにしたマップで、同じ生存期間の状態を保持する。
std::unordered_map<const Object*, std::weak_ptr<Window>> g_dc_windows;
std::unordered_map<const Object*, std::shared_ptr<Bitmap>> g_memorydc_bitmaps;
std::unordered_map<const Window*, BitmapMap> g_paint_bitmaps;
std::unordered_map<const Window*, std::weak_ptr<Window>> g_bitmap_windows;
std::unordered_map<const Window*, std::map<String, std::shared_ptr<Bitmap>>> g_window_bitmap_attrs;
std::unordered_map<const Bitmap*, BitmapMap> g_contained_bitmaps;

template<typename T>
std::shared_ptr<T> object_from_value(const Value& value)
{
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
    return object && *object ? std::dynamic_pointer_cast<T>(*object) : nullptr;
}

Value object_value(const std::shared_ptr<Object>& object)
{
    return object ? Value(object) : Value{};
}

Value bitmap_value(const std::shared_ptr<Bitmap>& bitmap)
{
    return bitmap ? object_value(std::static_pointer_cast<Object>(bitmap)) : Value{};
}

std::shared_ptr<Window> window_argument(const ::cw::lib::Arguments& args,
                                        const ::cw::lib::KeywordArguments& kwargs)
{
    if (!args.empty()) {
        if (const auto window = object_from_value<Window>(args.front())) return window;
    }
    const auto found = kwargs.find("window");
    return found == kwargs.end() ? nullptr : object_from_value<Window>(found->second);
}

void add_contained_bitmaps(const std::shared_ptr<Bitmap>& source, BitmapMap& destination)
{
    if (!source) return;
    const std::lock_guard lock(g_video_mutex);
    const auto found = g_contained_bitmaps.find(source.get());
    if (found != g_contained_bitmaps.end()) destination.insert(found->second.begin(), found->second.end());
}

bool bitmap_is_ok(const std::shared_ptr<Bitmap>& bitmap)
{
    return bitmap && bitmap->size.width > 0 && bitmap->size.height > 0;
}

bool refresh_bitmap_map(BitmapMap& bitmaps)
{
    bool refreshed = false;
    for (auto it = bitmaps.begin(); it != bitmaps.end();) {
        if (!it->second.bitmap || !::cw::util::is_animated_wxbmp(it->second.bitmap)) {
            it = bitmaps.erase(it);
            refreshed = true;
            continue;
        }
        const auto resolved = ::cw::util::resolve_animated_wxbmp(it->second.bitmap);
        if (resolved && resolved.get() != it->second.bitmap.get()) {
            it->second.bitmap = resolved;
            refreshed = true;
        }
        ++it;
    }
    return refreshed;
}

bool refresh_window_bitmaps(const std::shared_ptr<Window>& window)
{
    if (!window) return false;
    std::lock_guard lock(g_video_mutex);
    bool refreshed = false;
    if (const auto attributes = g_window_bitmap_attrs.find(window.get()); attributes != g_window_bitmap_attrs.end()) {
        for (auto it = attributes->second.begin(); it != attributes->second.end();) {
            if (!it->second || !::cw::util::is_animated_wxbmp(it->second)) {
                it = attributes->second.erase(it);
                refreshed = true;
                continue;
            }
            const auto resolved = ::cw::util::resolve_animated_wxbmp(it->second);
            if (resolved && resolved.get() != it->second.get()) {
                it->second = resolved;
                refreshed = true;
            }
            ++it;
        }
    }
    return refreshed;
}

void release_window_bitmaps(const std::shared_ptr<Window>& window)
{
    if (!window) return;
    std::set<const Bitmap*> released;
    std::set<const Object*> released_dcs;
    std::set<const Bitmap*> released_memory_targets;
    std::vector<std::shared_ptr<Bitmap>> bitmaps;
    {
        std::lock_guard lock(g_video_mutex);
        if (const auto painted = g_paint_bitmaps.find(window.get()); painted != g_paint_bitmaps.end()) {
            for (const auto& [key, entry] : painted->second) {
                (void)key;
                if (entry.bitmap && released.insert(entry.bitmap.get()).second) bitmaps.push_back(entry.bitmap);
            }
            g_paint_bitmaps.erase(painted);
        }
        if (const auto attributes = g_window_bitmap_attrs.find(window.get()); attributes != g_window_bitmap_attrs.end()) {
            for (const auto& [name, bitmap] : attributes->second) {
                (void)name;
                if (bitmap && released.insert(bitmap.get()).second) bitmaps.push_back(bitmap);
            }
            g_window_bitmap_attrs.erase(attributes);
        }
        for (auto it = g_dc_windows.begin(); it != g_dc_windows.end();) {
            const auto owner = it->second.lock();
            if (!owner || owner == window) {
                released_dcs.insert(it->first);
                it = g_dc_windows.erase(it);
            } else {
                ++it;
            }
        }
        // DC とそのメモリー上の描画先は、生存期間に合わせて登録する
        // 。 ダイアログより長く残ると、アドレスの再利用によって後のダ
        // イアログが古いアニメーション画像集合を引き継ぐおそれがある。
        for (auto it = g_memorydc_bitmaps.begin(); it != g_memorydc_bitmaps.end();) {
            if (released_dcs.contains(it->first)) {
                if (it->second) released_memory_targets.insert(it->second.get());
                it = g_memorydc_bitmaps.erase(it);
            } else {
                ++it;
            }
        }
        for (auto it = g_contained_bitmaps.begin(); it != g_contained_bitmaps.end();) {
            bool remove = released_memory_targets.contains(it->first);
            if (!remove) {
                for (const auto& [key, entry] : it->second) {
                    (void)key;
                    if (entry.bitmap && released.contains(entry.bitmap.get())) {
                        remove = true;
                        break;
                    }
                }
            }
            if (remove) it = g_contained_bitmaps.erase(it);
            else ++it;
        }
    }
    for (const auto& bitmap : bitmaps) ::cw::util::release_animated_wxbmp(bitmap);
}

class CommonDialogBackend : public DialogBackend {
public:
    void attach_dc(const std::shared_ptr<DeviceContext>&, const std::shared_ptr<Window>&) override {}
    Value select_bitmap(const std::shared_ptr<DeviceContext>&, const std::shared_ptr<Bitmap>& bitmap) override { return bitmap_value(bitmap); }
    Value draw_bitmap(const std::shared_ptr<DeviceContext>& dc, const std::shared_ptr<Bitmap>& bitmap,
                      const ::cw::lib::Arguments&, const ::cw::lib::KeywordArguments&) override {
        std::shared_ptr<Window> window;
        { const std::lock_guard lock(g_video_mutex); const auto found = g_dc_windows.find(dc.get()); if (found != g_dc_windows.end()) window = found->second.lock(); }
        if (window) refresh(window);
        return bitmap_value(bitmap);
    }
    Value set_bitmap(const std::shared_ptr<Window>& window, const String&, const std::shared_ptr<Bitmap>& bitmap) override {
        refresh(window); return bitmap_value(bitmap);
    }
    void refresh(const std::shared_ptr<Window>& window) override {
        if (!window || !::cw::g_cwpy || !::cw::g_cwpy->m_backend) return;
        ::cw::g_cwpy->m_backend->invalidate(*::cw::g_cwpy, window->bounds);
    }
    bool show(const std::shared_ptr<Dialog>& dialog, bool shown) override {
        if (!dialog) return false;
        dialog->visible = shown;
        if (::cw::g_cwpy && ::cw::g_cwpy->m_backend) ::cw::g_cwpy->m_backend->pump(*::cw::g_cwpy);
        return dialog->visible == shown;
    }
    Integer show_modal(const std::shared_ptr<Dialog>& dialog) override {
        if (!dialog) throw std::invalid_argument("show_modal: null dialog");
        // このバックエンドにはモーダルを閉じられるイベント源がない。 画面
        // なし・既定のバックエンドでは無期限の入れ子ループに入らず失敗とし
        // て終了し、実際の UI ホストが専用のバックエンドを供給する。
        dialog->visible = false;
        return dialog->id;
    }
    bool destroy(const std::shared_ptr<Dialog>& dialog) override {
        if (!dialog) return false;
        dialog->visible = false;
        if (::cw::g_cwpy && ::cw::g_cwpy->m_backend) ::cw::g_cwpy->m_backend->pump(*::cw::g_cwpy);
        return true;
    }
};

#ifdef _WIN32
constexpr UINT_PTR kDialogBackgroundSubclassId = 0x43574247; // 'CWBG'

int native_dialog_system_colour(NativeDialogBackground background) noexcept
{
    return background == NativeDialogBackground::window ? COLOR_WINDOW : COLOR_BTNFACE;
}

LRESULT CALLBACK dialog_background_subclass_proc(HWND hwnd, UINT message,
                                                 WPARAM wparam, LPARAM lparam,
                                                 UINT_PTR subclass_id,
                                                 DWORD_PTR reference)
{
    (void)reference;
    if (message == WM_SIZE && !is_all_mode() &&
        (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CHILD) == 0) {
        // 初回の配置と通常のサイズ変更は、復元通知として扱わない。
        const bool was_minimized = GetPropW(hwnd, L"CW.DialogIconized") != nullptr;
        const bool minimized = wparam == SIZE_MINIMIZED;
        if (minimized != was_minimized &&
            (minimized || wparam == SIZE_RESTORED || wparam == SIZE_MAXIMIZED)) {
            if (minimized) SetPropW(hwnd, L"CW.DialogIconized", reinterpret_cast<HANDLE>(1));
            else RemovePropW(hwnd, L"CW.DialogIconized");
            auto notification = std::make_shared<::cw::lib::Window>();
            notification->id = minimized ? 1 : 0;
            ::cw::frame::Frame_move_dlg_OnIconize(std::move(notification));
        }
    }
    if (message == WM_ERASEBKGND) {
        paint_native_dialog_background(
            reinterpret_cast<std::uintptr_t>(hwnd), static_cast<std::uintptr_t>(wparam),
            static_cast<NativeDialogBackground>(reference));
        return 1;
    }
    if (message == WM_CTLCOLORSTATIC || message == WM_CTLCOLORBTN) {
        // ネイティブの STATIC ラベルと BUTTON のグ
        // ループ枠は既定で透明であり、埋め込み子ダイアログではコ
        // ントロールの矩形から以前のホスト画像が透ける。 基底ダ
        // イアログのすべての子に、親と同じ不透明な色を設定する。
        const HDC dc = reinterpret_cast<HDC>(wparam);
        if (dc) {
            SetBkMode(dc, OPAQUE);
            const auto background = static_cast<NativeDialogBackground>(reference);
            const int system_colour = native_dialog_system_colour(background);
            SetBkColor(dc, GetSysColor(system_colour));
            SetDCBrushColor(dc, GetSysColor(system_colour));
            return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
        }
        return 0;
    }
    if (message == WM_NCDESTROY) {
        RemovePropW(hwnd, L"CW.DialogIconized");
        RemoveWindowSubclass(hwnd, dialog_background_subclass_proc, subclass_id);
    }
    return DefSubclassProc(hwnd, message, wparam, lparam);
}

std::wstring wide_text(const String& text) {
    if (text.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (count <= 0) return std::wstring(text.begin(), text.end());
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), count); return result;
}

#endif

} // namespace

void set_all_mode(bool arg_enabled, std::uintptr_t arg_host_window) noexcept
{
    if (!arg_enabled) {
        g_all_mode_host_window.store(0, std::memory_order_release);
        const std::lock_guard lock(g_all_mode_viewport_mutex);
        g_all_mode_engine_viewport = {};
        g_all_mode_debugger_viewport = {};
    } else if (arg_host_window != 0)
        g_all_mode_host_window.store(arg_host_window, std::memory_order_release);
    g_all_mode.store(arg_enabled, std::memory_order_release);
}

bool is_all_mode() noexcept
{
    return g_all_mode.load(std::memory_order_acquire);
}

std::uintptr_t all_mode_host_window() noexcept
{
    return g_all_mode_host_window.load(std::memory_order_acquire);
}

void set_all_mode_native_viewports(const AllModeNativeViewport arg_engine,
                                   const AllModeNativeViewport arg_debugger) noexcept
{
    const std::lock_guard lock(g_all_mode_viewport_mutex);
    g_all_mode_engine_viewport = arg_engine;
    g_all_mode_debugger_viewport = arg_debugger;
}

std::optional<AllModeNativeViewport> all_mode_native_viewport(
    const bool arg_debugger) noexcept
{
    const std::lock_guard lock(g_all_mode_viewport_mutex);
    const auto viewport = arg_debugger
        ? g_all_mode_debugger_viewport : g_all_mode_engine_viewport;
    return viewport.valid() ? std::optional{viewport} : std::nullopt;
}

std::optional<std::pair<int, int>> normalize_all_mode_window(
    std::uintptr_t arg_window, std::uintptr_t arg_parent) noexcept
{
#ifdef _WIN32
    if (!is_all_mode()) return std::nullopt;
    const HWND window = reinterpret_cast<HWND>(arg_window);
    if (!window || !IsWindow(window)) return std::nullopt;
    HWND parent = reinterpret_cast<HWND>(all_mode_host_window());
    if (!parent || !IsWindow(parent)) parent = reinterpret_cast<HWND>(arg_parent);
    if (!parent || !IsWindow(parent) || parent == window) return std::nullopt;

    // ウィンドウのプロパティは HWND とともに消えるため、同じ
    // ハンドルを再利用する後のダイアログへ古い推奨寸法が残らない。
    constexpr wchar_t width_property[] = L"CardWirth.All.ClientWidth";
    constexpr wchar_t height_property[] = L"CardWirth.All.ClientHeight";
    const auto stored_width = reinterpret_cast<std::intptr_t>(GetPropW(window, width_property));
    const auto stored_height = reinterpret_cast<std::intptr_t>(GetPropW(window, height_property));
    int width = stored_width > 0 ? static_cast<int>(stored_width - 1) : 0;
    int height = stored_height > 0 ? static_cast<int>(stored_height - 1) : 0;
    const bool first_normalization = width <= 0 || height <= 0;
    if (first_normalization) {
        RECT client{};
        if (!GetClientRect(window, &client)) return std::nullopt;
        width = std::max<LONG>(1, client.right - client.left);
        height = std::max<LONG>(1, client.bottom - client.top);
        SetPropW(window, width_property,
                 reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(width) + 1));
        SetPropW(window, height_property,
                 reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(height) + 1));
    }

    bool changed = false;
    if (GetParent(window) != parent) {
        SetParent(window, parent);
        changed = true;
    }
    const LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
    const LONG_PTR normalized_style =
        (style & ~(WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME |
                   WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_BORDER | WS_DLGFRAME |
                   WS_CLIPSIBLINGS)) |
        WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
    if (style != normalized_style) {
        SetWindowLongPtrW(window, GWL_STYLE, normalized_style);
        changed = true;
    }
    const LONG_PTR ex_style = GetWindowLongPtrW(window, GWL_EXSTYLE);
    const LONG_PTR normalized_ex_style =
        ex_style & ~(WS_EX_APPWINDOW | WS_EX_TOOLWINDOW | WS_EX_DLGMODALFRAME |
                     WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE);
    if (ex_style != normalized_ex_style) {
        SetWindowLongPtrW(window, GWL_EXSTYLE, normalized_ex_style);
        changed = true;
    }
    if (changed || first_normalization) {
        SetWindowPos(window, HWND_TOP, 0, 0, width, height,
                     SWP_NOMOVE | SWP_FRAMECHANGED | SWP_NOACTIVATE);
    }
    return std::pair{width, height};
#else
    (void)arg_window;
    (void)arg_parent;
    return std::nullopt;
#endif
}

void pump_all_mode_events() noexcept
{
#ifdef _WIN32
    if (!is_all_mode()) return;
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
#endif
}

void paint_native_dialog_background(std::uintptr_t arg_window,
                                    std::uintptr_t arg_device_context,
                                    NativeDialogBackground arg_background) noexcept
{
#ifdef _WIN32
    const HWND window = reinterpret_cast<HWND>(arg_window);
    HDC dc = reinterpret_cast<HDC>(arg_device_context);
    if (!window || !IsWindow(window) || !dc) return;
    RECT client{};
    if (!GetClientRect(window, &client)) return;
    FillRect(dc, &client, GetSysColorBrush(native_dialog_system_colour(arg_background)));
#else
    (void)arg_window;
    (void)arg_device_context;
    (void)arg_background;
#endif
}

void initialize_native_dialog_background(std::uintptr_t arg_window,
                                         NativeDialogBackground arg_background) noexcept
{
#ifdef _WIN32
    const HWND window = reinterpret_cast<HWND>(arg_window);
    if (!window || !IsWindow(window)) return;
    // 親の背景再描画から子の矩形を除外する。透明なコントロールへ背景
    // 色を届かせるため WS_CLIPCHILDREN を外すと、
    // ネイティブの編集欄・コンボ・ボタンが再描画する前にその下が毎回
    // 消え、ちらつく。 子の不透明な色は、下の WM_CTLCOLORSTATIC/WM_CTLCOLORBTN
    // で供給する。
    const LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
    if (!(style & WS_CLIPCHILDREN))
        SetWindowLongPtrW(window, GWL_STYLE, style | WS_CLIPCHILDREN);
    // 移植したダイアログの一部は、モデルの Dialog バックエンドでは
    // なく専用のネイティブ WNDCLASS を持つ。 この共通境界で背景
    // 消去処理を一度だけ取り付け、すべてに同じ不透明な初期描画面を与える。
    SetWindowSubclass(window, dialog_background_subclass_proc,
                      kDialogBackgroundSubclassId, static_cast<DWORD_PTR>(arg_background));
    const HDC dc = GetDC(window);
    if (dc) {
        paint_native_dialog_background(arg_window, reinterpret_cast<std::uintptr_t>(dc), arg_background);
        ReleaseDC(window, dc);
    }
    // 子コントロールは透明な背景を使う場合がある。 親
    // に不透明な背景色を設定した後、一度だけ子を再描画
    // する。その後の親の消去では子の矩形を除外する。
    RedrawWindow(window, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
#else
    (void)arg_window;
    (void)arg_background;
#endif
}

std::shared_ptr<DialogBackend> get_dialog_backend() { std::lock_guard lock(g_video_mutex); if (!g_dialog_backend) {
    // 互換用ダイアログのバックエンドはプラットフォームに依存させない。 完成したダ
    // イアログ UI は DialogPresentationProvider が
    // 公開し、ホストは受け渡すだけで、ネイティブのウィジェット構成に置き換えない。
    g_dialog_backend = std::make_shared<CommonDialogBackend>();
} return g_dialog_backend; }
std::shared_ptr<DialogBackend> create_default_dialog_backend() {
    return std::make_shared<CommonDialogBackend>();
}

void _install_modal_video_updater()
{
    std::lock_guard lock(g_video_mutex);
    if (g_installed) return;
    g_installed = true;
    if (!g_dialog_backend) g_dialog_backend = create_default_dialog_backend();
}

std::shared_ptr<Bitmap> _install_modal_video_updater_resolve_bitmap(std::shared_ptr<Bitmap> arg_bitmap)
{
    return ::cw::util::resolve_animated_wxbmp(std::move(arg_bitmap));
}

void _install_modal_video_updater_patch_dc_init(Value arg_cls)
{
    (void)arg_cls;
    _install_modal_video_updater();
}

void _install_modal_video_updater_patch_dc_init___init__(std::shared_ptr<DeviceContext> arg_self,
                                                         const ::cw::lib::Arguments& arg_args,
                                                         const ::cw::lib::KeywordArguments& arg_kwargs)
{
    if (!arg_self) return;
    const auto window = window_argument(arg_args, arg_kwargs);
    if (!window) return;
    std::lock_guard lock(g_video_mutex);
    g_dc_windows[arg_self.get()] = window;
    if (g_dialog_backend) g_dialog_backend->attach_dc(arg_self, window);
}

Value _install_modal_video_updater_get_dc_window(std::shared_ptr<DeviceContext> arg_dc)
{
    if (!arg_dc) return {};
    std::lock_guard lock(g_video_mutex);
    const auto found = g_dc_windows.find(arg_dc.get());
    if (found == g_dc_windows.end()) return {};
    const auto window = found->second.lock();
    return window ? object_value(std::static_pointer_cast<Object>(window)) : Value{};
}

void _install_modal_video_updater_register_drawn_bitmap(std::shared_ptr<DeviceContext> arg_dc,
                                                        std::shared_ptr<Bitmap> arg_bitmap)
{
    if (!arg_dc || !arg_bitmap) return;
    const bool animated = ::cw::util::is_animated_wxbmp(arg_bitmap);
    BitmapMap contained;
    add_contained_bitmaps(arg_bitmap, contained);
    if (!animated && contained.empty()) return;

    std::shared_ptr<Bitmap> memory_target;
    {
        std::lock_guard lock(g_video_mutex);
        const auto found = g_memorydc_bitmaps.find(arg_dc.get());
        if (found != g_memorydc_bitmaps.end()) memory_target = found->second;
        if (memory_target) {
            auto& target = g_contained_bitmaps[memory_target.get()];
            if (animated) target[arg_bitmap.get()] = {arg_bitmap, 0};
            target.insert(contained.begin(), contained.end());
            return;
        }
    }

    const auto window_value = _install_modal_video_updater_get_dc_window(std::move(arg_dc));
    const auto window = object_from_value<Window>(window_value);
    if (!window) return;
    std::lock_guard lock(g_video_mutex);
    g_bitmap_windows[window.get()] = window;
    auto& painted = g_paint_bitmaps[window.get()];
    if (animated) painted[arg_bitmap.get()] = {arg_bitmap, 0};
    painted.insert(contained.begin(), contained.end());
}

void _install_modal_video_updater_patch_memorydc_selectobject()
{
    _install_modal_video_updater();
}

// wx.MemoryDC は DC なので、選択と DrawBitmap のフックで同じ DeviceContext
// の識別情報を使う。 これにより合成したビットマップの依存関係を伝播できる。
void _install_modal_video_updater_patch_memorydc_selectobject___init__(std::shared_ptr<DeviceContext> arg_self,
                                                                       const ::cw::lib::Arguments& arg_args,
                                                                       const ::cw::lib::KeywordArguments& arg_kwargs)
{
    (void)arg_kwargs;
    if (!arg_self || arg_args.empty()) return;
    const auto bitmap = object_from_value<Bitmap>(arg_args.front());
    if (!bitmap_is_ok(bitmap)) return;
    std::lock_guard lock(g_video_mutex);
    g_memorydc_bitmaps[arg_self.get()] = bitmap;
}

Value _install_modal_video_updater_patch_memorydc_selectobject_SelectObject(std::shared_ptr<DeviceContext> arg_self,
                                                                             std::shared_ptr<Bitmap> arg_bitmap)
{
    if (!arg_self) return {};
    {
        std::lock_guard lock(g_video_mutex);
        if (bitmap_is_ok(arg_bitmap)) g_memorydc_bitmaps[arg_self.get()] = arg_bitmap;
        else g_memorydc_bitmaps.erase(arg_self.get());
    }
    // get_dialog_backend() も g_video_mutex
    // を取得するため、バックエンドの取得と任意の描画コールバックは
    // 管理用ロックの外で行う。 再帰不可のミューテックスを再取得する
    // と、アニメーション画像を使うモーダルが停止するおそれがある。
    return get_dialog_backend()->select_bitmap(arg_self, arg_bitmap);
}

void _install_modal_video_updater_patch_drawbitmap(Value arg_cls)
{
    (void)arg_cls;
    _install_modal_video_updater();
}

Value _install_modal_video_updater_patch_drawbitmap_DrawBitmap(std::shared_ptr<DeviceContext> arg_self,
                                                              std::shared_ptr<Bitmap> arg_bitmap,
                                                              const ::cw::lib::Arguments& arg_args,
                                                              const ::cw::lib::KeywordArguments& arg_kwargs)
{
    (void)arg_args;
    _install_modal_video_updater_register_drawn_bitmap(arg_self, arg_bitmap);
    const auto resolved = _install_modal_video_updater_resolve_bitmap(std::move(arg_bitmap));
    return get_dialog_backend()->draw_bitmap(arg_self, resolved, arg_args, arg_kwargs);
}

void _install_modal_video_updater_patch_bitmap_setter(Value arg_cls, String arg_name, String arg_attr)
{
    (void)arg_cls;
    (void)arg_name;
    _install_modal_video_updater();
    if (arg_attr.empty()) throw std::invalid_argument("patch_bitmap_setter: empty bitmap attribute");
}

Value _install_modal_video_updater_patch_bitmap_setter_setter(std::shared_ptr<Window> arg_self,
                                                             std::shared_ptr<Bitmap> arg_bitmap,
                                                             const ::cw::lib::Arguments& arg_args,
                                                             const ::cw::lib::KeywordArguments& arg_kwargs)
{
    (void)arg_args;
    if (!arg_self) return bitmap_value(arg_bitmap);
    String attr = "_cw_animated_bitmap";
    if (const auto found = arg_kwargs.find("attr"); found != arg_kwargs.end()) attr = found->second.as_string();
    {
        std::lock_guard lock(g_video_mutex);
        g_bitmap_windows[arg_self.get()] = arg_self;
        auto& attrs = g_window_bitmap_attrs[arg_self.get()];
        if (arg_bitmap && ::cw::util::is_animated_wxbmp(arg_bitmap)) attrs[attr] = arg_bitmap;
        else attrs.erase(attr);
        if (attrs.empty()) g_window_bitmap_attrs.erase(arg_self.get());
    }
    const auto resolved = _install_modal_video_updater_resolve_bitmap(std::move(arg_bitmap));
    return get_dialog_backend()->set_bitmap(arg_self, attr, resolved);
}

bool _install_modal_video_updater_refresh_animated_bitmaps(std::shared_ptr<Window> arg_window)
{
    return refresh_window_bitmaps(arg_window);
}

bool _install_modal_video_updater_refresh_painted_animated_bitmaps(std::shared_ptr<Window> arg_window)
{
    if (!arg_window) return false;
    std::lock_guard lock(g_video_mutex);
    const auto found = g_paint_bitmaps.find(arg_window.get());
    return found != g_paint_bitmaps.end() && refresh_bitmap_map(found->second);
}

void _install_modal_video_updater_release_animated_bitmaps(std::shared_ptr<Window> arg_window)
{
    release_window_bitmaps(std::move(arg_window));
}

void _install_modal_video_updater_update_dialog_video(std::shared_ptr<Dialog> arg_window)
{
    if (!arg_window || !arg_window->visible) return;
    // Python はすべての子ウィンドウを走査する。互換ブリッジはアニメー
    // ション画像を含むウィンドウだけを記録するため、共通の親子関係をたどる。
    std::vector<std::shared_ptr<Window>> windows{arg_window};
    {
        std::lock_guard lock(g_video_mutex);
        for (auto it = g_bitmap_windows.begin(); it != g_bitmap_windows.end();) {
            const auto window = it->second.lock();
            if (!window) { g_paint_bitmaps.erase(it->first); g_window_bitmap_attrs.erase(it->first); it = g_bitmap_windows.erase(it); continue; }
            if (window != arg_window) {
                std::set<const Window*> visited;
                for (auto parent = window->parent_window.lock(); parent && visited.insert(parent.get()).second; parent = parent->parent_window.lock())
                    if (parent == arg_window) { windows.push_back(window); break; }
            }
            ++it;
        }
    }
    for (const auto& window : windows) {
        const bool attributes = _install_modal_video_updater_refresh_animated_bitmaps(window);
        const bool painted = _install_modal_video_updater_refresh_painted_animated_bitmaps(window);
        if (attributes || painted) get_dialog_backend()->refresh(window);
    }
}

void _install_modal_video_updater_request_main_video_update()
{
    if (!::cw::g_cwpy) return;
    // Python は force_exec_func(update, True)
    // を送る。C++ では意図的に名前の異なる表示要求を使い、 UI タイマ
    // ーからシミュレーションやダイアログの生存期間処理へ再入しないようにする。
    ::cw::g_cwpy->request_presentation_update();
}

void _install_modal_video_updater_request_main_video_update_update()
{
    if (::cw::g_cwpy) ::cw::g_cwpy->request_presentation_update();
}

void _install_modal_video_updater_update_all_dialog_videos()
{
    // 実際にアニメーションするリソースが有効な場合だけ、モーダルの
    // 背後で Engine の更新を続ける。 静的なダイアログのた
    // めに、毎秒 30 回のウィンドウ全体の表示更新を強制しない。
    if (::cw::animatedimage::has_active())
        _install_modal_video_updater_request_main_video_update();
    std::vector<std::shared_ptr<Dialog>> live;
    {
        std::lock_guard lock(g_video_mutex);
        for (auto it = g_dialogs.begin(); it != g_dialogs.end();) {
            if (const auto dialog = it->lock()) {
                live.push_back(dialog);
                ++it;
            } else {
                it = g_dialogs.erase(it);
            }
        }
    }
    for (const auto& dialog : live) try { _install_modal_video_updater_update_dialog_video(dialog); } catch (...) { /* 一つのネイティブダイアログを破棄しても、共通タイマーは停止させない。 */ }
}

void dispatch_dialog_video_update(const std::weak_ptr<Frame>& weak_frame)
{
    // タイマーの所有権は UI の所有権と分ける。移植用ホストでは実際の
    // 画像更新を Frame の UI ディスパッチャーへ渡し、 タイマ
    // ースレッドから Window/Backend の状態を操作しない。
    const auto frame = weak_frame.lock();
    if (!frame) return;
    try {
        frame->exec_func([](const ::cw::lib::Arguments&, const ::cw::lib::KeywordArguments&) {
            _install_modal_video_updater_update_all_dialog_videos();
            return Value{};
        });
    } catch (...) {
        // 終了時はタイマースレッドの完了待ちより前に UI ディスパッチャーが閉じる場合
        // がある。 そのフレームを破棄しても安全であり、次のタイマー処理で再試行できる。
    }
}

void _install_modal_video_updater_start_dialog_video_timer(std::shared_ptr<Dialog> arg_window)
{
    if (!arg_window) return;
    std::lock_guard lock(g_video_mutex);
    for (const auto& weak : g_dialogs) if (weak.lock() == arg_window) return;
    g_dialogs.push_back(std::move(arg_window));
    // タイマーの所有方法は全 OS で共通にする。ワーカーは更新を予約するだけとし
    // 、 表示状態の変更はすべて Frame の UI ディスパッチャーで行う。
    if (!g_video_timer_thread.joinable()) {
        std::weak_ptr<Frame> weak_frame;
        if (::cw::g_cwpy)
            weak_frame = object_from_value<Frame>(::cw::g_cwpy->m_frame);
        g_video_timer_stop = std::make_shared<std::atomic_bool>(false);
        const auto stop = g_video_timer_stop;
        g_video_timer_thread = std::thread([stop, weak_frame] {
        while (!stop->load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(33));
            if (!stop->load(std::memory_order_acquire)) dispatch_dialog_video_update(weak_frame);
        }
        });
    }
}

void shutdown_dialog_video_updater()
{
    std::thread stopped_timer;
    std::vector<std::shared_ptr<Dialog>> live;
    {
        std::lock_guard lock(g_video_mutex);
        for (const auto& weak : g_dialogs)
            if (const auto dialog = weak.lock()) live.push_back(dialog);
        g_dialogs.clear();
        if (g_video_timer_stop)
            g_video_timer_stop->store(true, std::memory_order_release);
        if (g_video_timer_thread.joinable())
            stopped_timer = std::move(g_video_timer_thread);
        g_video_timer_stop.reset();
    }
    if (stopped_timer.joinable()) stopped_timer.join();
    for (auto& dialog : live)
        _install_modal_video_updater_release_animated_bitmaps(std::move(dialog));
}

void _install_modal_video_updater_start_dialog_video_timer_on_timer(std::shared_ptr<Window> arg_event)
{
    (void)arg_event;
    _install_modal_video_updater_update_all_dialog_videos();
}

void _install_modal_video_updater_stop_dialog_video_timer(std::shared_ptr<Dialog> arg_window, bool arg_release)
{
    std::thread stopped_timer;
    {
        std::lock_guard lock(g_video_mutex);
        g_dialogs.erase(std::remove_if(g_dialogs.begin(), g_dialogs.end(), [&](const auto& weak) {
            const auto dialog = weak.lock();
            return !dialog || dialog == arg_window;
        }), g_dialogs.end());
        if (g_dialogs.empty() && g_video_timer_thread.joinable()) {
            if (g_video_timer_stop)
                g_video_timer_stop->store(true, std::memory_order_release);
            stopped_timer = std::move(g_video_timer_thread);
            g_video_timer_stop.reset();
        }
    }
    if (stopped_timer.joinable()) stopped_timer.join();
    if (arg_release) _install_modal_video_updater_release_animated_bitmaps(std::move(arg_window));
}

bool _install_modal_video_updater_Show(std::shared_ptr<Dialog> arg_self,
                                       const ::cw::lib::Arguments& arg_args,
                                       const ::cw::lib::KeywordArguments& arg_kwargs)
{
    if (!arg_self) return false;
    bool shown = true;
    if (!arg_args.empty()) shown = arg_args.front().as_bool();
    else if (const auto found = arg_kwargs.find("show"); found != arg_kwargs.end()) shown = found->second.as_bool();
    const bool result = get_dialog_backend()->show(arg_self, shown);
    if (shown) _install_modal_video_updater_start_dialog_video_timer(arg_self);
    else _install_modal_video_updater_stop_dialog_video_timer(arg_self, false);
    return result;
}

Integer _install_modal_video_updater_ShowModal(std::shared_ptr<Dialog> arg_self)
{
    if (!arg_self) throw std::invalid_argument("ShowModal: null dialog");
    arg_self->visible = true;
    _install_modal_video_updater_start_dialog_video_timer(arg_self);
    try {
        const auto result = get_dialog_backend()->show_modal(arg_self);
        _install_modal_video_updater_stop_dialog_video_timer(arg_self, true); arg_self->visible = false; return result;
    } catch (...) {
        _install_modal_video_updater_stop_dialog_video_timer(arg_self, true); arg_self->visible = false; throw;
    }
}

bool _install_modal_video_updater_Destroy(std::shared_ptr<Dialog> arg_self)
{
    if (!arg_self) return false;
    _install_modal_video_updater_stop_dialog_video_timer(arg_self, true);
    return get_dialog_backend()->destroy(std::move(arg_self));
}

void main()
{
    // Python のモジュール用エントリーポイントと同様、ここでは何もしない。 アプ
    // リケーションの起動とダイアログの構築は、個別のダイアログモジュールが担当する。
}

} // namespace cw::dialog
