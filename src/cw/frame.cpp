#include "cw/lib/exception_origin.hpp"
#include "cw/lib/runtime_access.hpp"
// cw/frame.py の移植。wx/pygame の呼び出し
// は下の互換状態の背後へ分離する。 ゲーム状態と処理順は、SDL
// ・ネイティブの呼び出し側と画面なしテストからも利用可能にする。
#include "cw/frame.hpp"

#if !defined(_WIN32)
#include "cw/sdl_frame_adapter.hpp"
#endif
#include "cw/cw.hpp"
#include "cw/setting.hpp"
#include "cw/thread.hpp"
#include "cw/novel.hpp"
#include "cw/eventrelay.hpp"
#include "cw/event.hpp"
#include "cw/eventhandler.hpp"
#include "cw/dialog/cardcontrol.hpp"
#include "cw/dialog/dialog.hpp"
#include "cw/dialog/edit.hpp"
#include "cw/dialog/message.hpp"
#include "cw/dialog/settings.hpp"
#include "cw/dialog/select.hpp"
#include "cw/dialog/skin.hpp"
#include "cw/debug/debugger.hpp"
#include "cw/scenariodb.hpp"
#include "cw/header.hpp"
#include "cw/lib/simd_image.hpp"
#include "cw/data.hpp"
#include "cw/battle.hpp"
#include "cw/advlog.hpp"
#include "cw/character.hpp"
#include "cw/sprite/card.hpp"
#include "cw/sprite/message.hpp"
#include "cw/sprite/statusbar.hpp"
#include "cw/dialog/skininstall.hpp"
#include "cw/dialog/scenarioinstall.hpp"
#include "cw/dialog/scenarioselect.hpp"
#include "cw/dialog/presentation.hpp"
#include "platform/storage/storage_mount.hpp"
#include <cassert>
#include "cw/dialog/cardinfo.hpp"
#include "cw/dialog/charainfo.hpp"
#include "cw/dialog/partyrecord.hpp"
#include "cw/dialog/create.hpp"
#include "cw/dialog/etc.hpp"
#include "cw/dialog/text.hpp"
#include "cw/util.hpp"
#include "cw/update.hpp"
#include "cw/bassplayer.hpp"
#include "cw/lib/encoding.hpp"
#include "cw/lib/python_percent_string.hpp"
#include "cw/lib/imagecodec.hpp"
#include "cw/lib/ui_surface.hpp"
#include "cw/image.hpp"
#include "cw/imageretouch.hpp"
#include "app/debug_control_server.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <chrono>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <string_view>
#include <system_error>
#include <thread>
#include <tuple>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace {
using Frame = ::cw::frame::Frame;
using Value = ::cw::lib::Value;
using Integer = ::cw::lib::Integer;
using Real = ::cw::lib::Real;
using Decimal = ::cw::lib::Decimal;

// Python の wx ハンドラーと pygame スレッドは、これらのモデル操
// 作を直列化する。 ネイティブのモーダルループではゲームスレッドが動き続けるため、
// そこで状態を取得・準備し、UI は所有権付きスナップショットを使う。 画面なし
// の契約検証では同時に使うゲーム処理がないため、同じコールバックを直接実行する。
void query_game_state(const std::shared_ptr<::cw::thread::CWPy>& runtime,
                      ::cw::lib::Callback query)
{
    bool has_game_thread;
    {
        std::scoped_lock lock(runtime->m_game_exec_mutex);
        has_game_thread = runtime->m_game_thread_id != std::thread::id{};
    }
    if (has_game_thread) (void)runtime->sync_exec_11(std::move(query), {}, {});
    else (void)query({}, {});
}
using String = ::cw::lib::String;

bool is_synthetic_nonactive_pointer(const std::shared_ptr<::cw::lib::Window>& event)
{
    return ::cardwirth::debug::nonactive_automation_enabled() && event &&
           event->id == static_cast<Integer>(
               ::cardwirth::debug::nonactive_pointer_event_id);
}

std::shared_ptr<Frame> current_frame();

template<typename T>
std::shared_ptr<T> as_object(const Value& value) {
    const auto* p = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
    return p ? std::dynamic_pointer_cast<T>(*p) : nullptr;
}

std::shared_ptr<::cw::lib::Window> window_for(const std::shared_ptr<::cw::lib::Window>& parent,
                                              const String& title) {
    auto result = std::make_shared<::cw::lib::Window>();
    result->title = title;
    result->visible = false;
    if (parent) result->bounds = parent->bounds;
    return result;
}

String value_string(const Value& value) {
    if (const auto* p = std::get_if<String>(&value.storage())) return *p;
    if (const auto* p = std::get_if<Decimal>(&value.storage())) return p->to_string(true);
    if (const auto* p = std::get_if<Integer>(&value.storage())) return std::to_string(*p);
    if (const auto* p = std::get_if<Real>(&value.storage())) {
        std::ostringstream out;
        out << std::setprecision(std::numeric_limits<Real>::max_digits10) << *p;
        return out.str();
    }
    if (const auto* p = std::get_if<bool>(&value.storage())) return *p ? "TRUE" : "FALSE";
    return {};
}

std::optional<Integer> value_int(const Value& value) {
    if (const auto* p = std::get_if<Integer>(&value.storage())) return *p;
    if (const auto* p = std::get_if<bool>(&value.storage())) return *p ? 1 : 0;
    if (const auto* p = std::get_if<Decimal>(&value.storage())) {
        try {
            // Decimal::to_integer(Down) は Python の Decimal
            // と同じゼロ方向への切り捨て変換である。 桁あふれ・不正値をネイティブのキャストへ渡さない。
            return p->to_integer(::cw::lib::DecimalRounding::Down);
        } catch (const ::cw::lib::DecimalError&) {
            return std::nullopt;
        }
    }
    if (const auto* p = std::get_if<Real>(&value.storage())) {
        if (!std::isfinite(*p)) return std::nullopt;
        // double は INT64_MAX を 2 の 63 乗として表すため、ネイティ
        // ブ変換前にその値を含まない上限で判定する。 範囲外の変換による未定義動作を避ける。
        constexpr Real min_integer = static_cast<Real>(std::numeric_limits<Integer>::lowest());
        constexpr Real max_integer_exclusive = -min_integer;
        if (*p < min_integer || *p >= max_integer_exclusive) return std::nullopt;
        return static_cast<Integer>(*p);
    }
    if (const auto* p = std::get_if<String>(&value.storage())) {
        if (p->empty()) return std::nullopt;
        try {
            std::size_t used = 0;
            const auto result = std::stoll(*p, &used, 10);
            if (used != p->size()) return std::nullopt;
            return static_cast<Integer>(result);
        } catch (...) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

bool value_truthy(const Value& value) {
    if (value.is_none()) return false;
    if (const auto* p = std::get_if<Real>(&value.storage())) {
        // ネイティブの UI・イベント値では、有限でない数値を有効なコールバック結果として扱わ
        // ない。 Decimal は Value::as_bool を通して正確に判定する。
        return std::isfinite(*p) && *p != 0.0;
    }
    return value.as_bool();
}

Integer scaled_offset(Integer value, Real scale) {
    if (!std::isfinite(scale) || scale <= 0.0) return value;
    const Real scaled = static_cast<Real>(value) * scale;
    if (!std::isfinite(scaled)) return value;
    constexpr Real min_integer = static_cast<Real>(std::numeric_limits<Integer>::lowest());
    constexpr Real max_integer_exclusive = -min_integer;
    if (scaled < min_integer || scaled >= max_integer_exclusive) return value;
    return static_cast<Integer>(scaled);
}

std::shared_ptr<::cw::lib::Window> message_parent(
    const ::cw::lib::KeywordArguments& arguments, const Value& panel)
{
    if (const auto it = arguments.find("parentdialog"); it != arguments.end())
        if (const auto parent = as_object<::cw::lib::Window>(it->second)) return parent;
    return as_object<::cw::lib::Window>(panel);
}

void after_message(const std::shared_ptr<::cw::lib::Window>& parent)
{
    if (const auto owner = std::dynamic_pointer_cast<::cw::lib::Dialog>(parent);
        owner && owner->after_message_callback) owner->after_message_callback();
}

std::optional<Integer> show_defined_message_dialog(
    const std::shared_ptr<::cw::lib::Window>& parent, const String& text,
    Integer mode)
{
    const auto title = ::cw::g_cwpy && ::cw::g_cwpy->m_setting &&
            ::cw::g_cwpy->m_setting->m_msgs
        ? ::cw::g_cwpy->m_setting->m_msgs->get("message", "メッセージ")
        : String("メッセージ");
    std::shared_ptr<::cw::dialog::message::Message> dialog;
    if (mode == 1) {
        auto yesno = std::make_shared<::cw::dialog::message::YesNoMessage>();
        yesno->initialize(parent, title, text);
        dialog = std::move(yesno);
    } else {
        dialog = std::make_shared<::cw::dialog::message::Message>();
        dialog->initialize(parent, title, text, 2, std::nullopt);
    }

    const auto owner = std::dynamic_pointer_cast<::cw::lib::Dialog>(parent);
    const bool shown = ::cw::dialog::show_common_modal_presentation_with_owner(dialog, owner);
    if (!shown) return std::nullopt;
    return dialog->m_return_code;
}

std::optional<bool> show_defined_yesno_dialog(
    const std::shared_ptr<::cw::lib::Window>& parent, const String& text)
{
    const auto result = show_defined_message_dialog(parent, text, 1);
    return result ? std::optional<bool>(*result == ::cw::dialog::message::kWxIdOk) : std::nullopt;
}

String frame_message(const String& key, const String& fallback)
{
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_msgs)
        return ::cw::g_cwpy->m_setting->m_msgs->get(key, fallback);
    return fallback;
}

bool ydata_is_changed()
{
    return ::cw::g_cwpy && ::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata &&
           (*::cw::g_cwpy->m_ydata)->is_changed();
}

bool skin_version_supported(const String& version)
{
    bool supported = false;
    std::apply([&](const auto&... value) {
        ((supported = supported || version == value), ...);
    }, ::cw::g_SUPPORTED_SKIN);
    return supported;
}

String ascii_lower(String value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::shared_ptr<Frame> current_frame() {
    if (!::cw::g_cwpy) return nullptr;
    return as_object<Frame>(::cw::g_cwpy->m_frame);
}

#ifdef _WIN32
void schedule_modal_pointer_release_poll()
{
    SetTimer(nullptr, 0, 16, [](HWND, UINT, UINT_PTR timer, DWORD) {
        KillTimer(nullptr, timer);
        if (!::cw::frame::Frame_poll_modal_pointer_release())
            schedule_modal_pointer_release_poll();
    });
}
#endif

void post_native_input(Integer type, Integer key = 0, Integer button = 0,
                       bool touch = false)
{
    if (!::cw::g_cwpy) return;
    auto event = std::make_shared<::cw::eventhandler::InputEvent>();
    event->type = type;
    event->key = key;
    event->button = button;
    event->touch = touch;
    event->pos = {
        value_int(std::get<0>(::cw::g_cwpy->m_wxmousepos)).value_or(-1),
        value_int(std::get<1>(::cw::g_cwpy->m_wxmousepos)).value_or(-1)};
    event->timestamp = static_cast<Real>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    ::cw::thread::post_pygameevent(std::move(event));
}

void update_native_mouse_position(const std::shared_ptr<::cw::lib::Window>& event)
{
    if (!::cw::g_cwpy || !event) return;
    ::cw::g_cwpy->m_wxmousepos = {Value(event->bounds.x), Value(event->bounds.y)};
}

Integer normalize_win32_key(Integer key)
{
#if defined(_WIN32)
    if (key >= VK_F1 && key <= VK_F12)
        return ::cw::eventhandler::KEY_F1 + (key - VK_F1);
    switch (key) {
    case VK_LEFT: return ::cw::eventhandler::KEY_LEFT;
    case VK_RIGHT: return ::cw::eventhandler::KEY_RIGHT;
    case VK_UP: return ::cw::eventhandler::KEY_UP;
    case VK_DOWN: return ::cw::eventhandler::KEY_DOWN;
    case VK_PRIOR: return ::cw::eventhandler::KEY_PAGEUP;
    case VK_NEXT: return ::cw::eventhandler::KEY_PAGEDOWN;
    case VK_HOME: return ::cw::eventhandler::KEY_HOME;
    case VK_END: return ::cw::eventhandler::KEY_END;
    case VK_RETURN: return ::cw::eventhandler::KEY_RETURN;
    case VK_ESCAPE: return ::cw::eventhandler::KEY_ESCAPE;
    case VK_BACK: return ::cw::eventhandler::KEY_BACKSPACE;
    default: break;
    }
#endif
    return key;
}

void append_unique(std::vector<String>& values, const String& value) {
    if (std::find(values.begin(), values.end(), value) == values.end()) values.push_back(value);
}

// Python の cw.wins() は int() でゼロ方向へ切り捨てるため、ウィンドウの表示領域寸法に llround は使わない。
Integer truncate_scaled_integer(Integer value, Real scale) {
    if (!std::isfinite(scale) || scale <= 0.0) return value;
    const Real scaled = static_cast<Real>(value) * scale;
    if (!std::isfinite(scaled)) return value;
    return static_cast<Integer>(scaled);
}

#ifdef _WIN32
DWORD main_frame_window_style() {
    // wx.DEFAULT_FRAME_STYLE から WS_MAXIMIZEBOX と WS_THICKFRAME を除いた構成。
    return WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
}

std::pair<int, int> main_frame_window_size_for_client(int client_w, int client_h) {
    RECT rect{0, 0, client_w, client_h};
    AdjustWindowRectEx(&rect, main_frame_window_style(), FALSE, WS_EX_ACCEPTFILES);
    return {rect.right - rect.left, rect.bottom - rect.top};
}
#endif

// ネイティブ実装用の安全な共通既定値。このクラスは実装詳細であり、HeadlessFrameAdapter
// とは意図的に分ける。 Win32・ネイティブのア
// ダプターが誤って画面なしと表明しないようにする。
class FrameAdapterDefaults : public ::cw::frame::NativeFrameAdapter {
public:
    void attach_surface(Frame&, const std::shared_ptr<::cw::lib::Window>& panel) override {
        if (panel) panel->visible = true;
    }

    ::cw::lib::Rect display_geometry(const Frame&) const override {
        // ホストはこのアダプターを SDL のディスプレイ列挙に置き換えられ
        // る。 モデルの代替値は、ゲーム表示領域を収められる大きさにする。
        return {0, 0, 1920, 1080};
    }

    void set_icon(Frame&, const std::shared_ptr<::cw::lib::Window>&) override {}

    void bind(Frame&, EventSink) override {
        // ネイティブホストは自身のイベントループから Frame::dispatch_native_event を呼ぶ。
    }

    bool post_exec(Frame&) override {
        // 移植用ホストはこの処理を上書きし、実際の UI イベントループを
        // 起こす必要がある。 false を返す場合は sync_exec
        // を失敗させ、任意のワーカースレッドで UI 処理を実行しない。
        return false;
    }

    void adjust_position(Frame& frame, const std::shared_ptr<::cw::lib::Window>& window) override {
        if (!window) return;
        // 明示的な画面なしアダプターは、ディスプレイ情報の読み取り処理を供給する
        // 。 位置決めは全ホストで同じ規則を使い、ウィンドウサイズを維持する。
        const auto area=is_headless()?display_geometry(frame) : ::cw::util::window_work_area(*window);
        window->bounds=::cw::util::constrain_window_to_work_area(window->bounds,area);
    }

    bool dispatch_menu_accelerator(Frame&, Integer) override { return false; }
    bool compose_dialogs(Frame&, ::cw::lib::Bitmap&, Integer,
                         std::tuple<Integer, Integer, Integer>,
                         std::tuple<Integer, Integer, Integer>) override { return false; }
    void watch_left_button(Frame&, Integer) override {}
    std::optional<bool> modal_message(Frame&, const String&, const String&, bool, bool) override {
        return std::nullopt;
    }
};

class HeadlessFrameAdapter final : public FrameAdapterDefaults {
    std::optional<Integer> m_default_font_pixel_height_override{};
public:
    HeadlessFrameAdapter() = default;
    explicit HeadlessFrameAdapter(Integer arg_default_font_pixel_height)
        : m_default_font_pixel_height_override(arg_default_font_pixel_height) {}
    bool is_headless() const noexcept override { return true; }
    std::optional<Integer> default_font_pixel_height() const override {
        return m_default_font_pixel_height_override
            ? m_default_font_pixel_height_override
            : FrameAdapterDefaults::default_font_pixel_height();
    }
};

#ifdef _WIN32
constexpr UINT frame_exec_message = WM_APP + 0x4C2;
constexpr UINT frame_present_now_message = WM_APP + 0x4C3;
class Win32FrameAdapter final : public FrameAdapterDefaults {
    EventSink m_sink{};
    Frame* m_frame{};
    bool m_in_size_move{};
    bool m_move_pending{};
    std::atomic<Integer> m_client_width{0};
    std::atomic<Integer> m_client_height{0};

    bool is_headless() const noexcept override { return false; }
    bool supports_common_presentation() const noexcept override { return true; }
    static HWND hwnd(const Frame& frame) noexcept {
        return reinterpret_cast<HWND>(frame.m_native_window_handle);
    }

    static std::wstring wide(const String& text) {
        if (text.empty()) return {};
        const int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        std::wstring result(static_cast<std::size_t>(std::max(0, n)), L'\0');
        if (n > 0) MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), n);
        return result;
    }

    void refresh_client_size(HWND handle) {
        RECT client{};
        if (!handle || !GetClientRect(handle, &client)) {
            m_client_width.store(0, std::memory_order_release);
            m_client_height.store(0, std::memory_order_release);
            return;
        }
        m_client_width.store(static_cast<Integer>(client.right - client.left),
                             std::memory_order_release);
        m_client_height.store(static_cast<Integer>(client.bottom - client.top),
                              std::memory_order_release);
    }

    static void paint(HWND handle, Frame* frame) {
#if defined(CARDWIRTH_LOG_ENABLED)
        const auto paint_started = std::chrono::steady_clock::now();
#endif
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(handle, &paint);
        if (!dc) return;
        RECT client{};
        GetClientRect(handle, &client);
        // GDI が読み終えるまで、不変の表示スナップショットをロッ
        // クする。 ゲームスレッドが自身の RGBA 描画面を変更し
        // 続けても、WM_PAINT はその記憶領域と競合しない。
        std::unique_lock<std::mutex> present_lock;
        if (frame) present_lock = std::unique_lock<std::mutex>(frame->m_present_mutex);
        const auto width = frame ? frame->m_present_width : 0;
        const auto height = frame ? frame->m_present_height : 0;
        const bool dimensions_ok = width > 0 && height > 0 &&
            static_cast<std::uint64_t>(width) <= static_cast<std::uint64_t>(std::numeric_limits<int>::max()) &&
            static_cast<std::uint64_t>(height) <= static_cast<std::uint64_t>(std::numeric_limits<int>::max());
        const std::uint64_t byte_count = dimensions_ok
            ? static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * 4ULL : 0ULL;
        if (frame && dimensions_ok && byte_count == frame->m_present_bgra.size()) {
#if defined(CARDWIRTH_LOG_ENABLED)
            static std::atomic_bool first_surface_paint_logged{false};
            if (!first_surface_paint_logged.exchange(true))
                ::cardwirth::debug::log("Win32 first surface paint");
#endif
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = static_cast<LONG>(width);
            info.bmiHeader.biHeight = -static_cast<LONG>(height);
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            StretchDIBits(dc, 0, 0, client.right - client.left, client.bottom - client.top,
                          0, 0, static_cast<int>(width), static_cast<int>(height), frame->m_present_bgra.data(),
                          &info, DIB_RGB_COLORS, SRCCOPY);
        } else {
            FillRect(dc, &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        }
        EndPaint(handle, &paint);
#if defined(CARDWIRTH_LOG_ENABLED)
        ::cardwirth::debug::log_slow(
            "render.win32_paint", std::chrono::steady_clock::now() - paint_started,
            "surface=" + (frame && dimensions_ok ? std::to_string(width) + "x" + std::to_string(height) : "none") +
            " client=" + std::to_string(client.right - client.left) + "x" +
            std::to_string(client.bottom - client.top));
#endif
    }

    static LRESULT CALLBACK window_proc(HWND handle, UINT message, WPARAM wparam, LPARAM lparam) {
        auto* adapter = reinterpret_cast<Win32FrameAdapter*>(GetWindowLongPtrW(handle, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
            adapter = static_cast<Win32FrameAdapter*>(create->lpCreateParams);
            SetWindowLongPtrW(handle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(adapter));
        }
        const auto send = [&](const String& name, Integer id = 0,bool horizontal = false) {
            if (!adapter || !adapter->m_sink) return;
            auto event = std::make_shared<::cw::lib::Window>();
            event->id = id;event->wheel_horizontal=horizontal;
            // ネイティブの修飾キー情報はプラットフォーム境界で解釈する。
            // アクセラレーターの選択は共通のデバッガーモデルで行う。
            if (GetKeyState(VK_SHIFT) & 0x8000) event->modifiers |= ::cw::dialog::DialogPresentationModifierShift;
            if (GetKeyState(VK_CONTROL) & 0x8000) event->modifiers |= ::cw::dialog::DialogPresentationModifierControl;
            if (GetKeyState(VK_MENU) & 0x8000) event->modifiers |= ::cw::dialog::DialogPresentationModifierAlt;
            adapter->m_sink(name, event);
        };
        const auto send_mouse = [&](const String& name, LPARAM position,
                                    Integer event_id = 0) {
            if (!adapter || !adapter->m_sink) return;
            auto event = std::make_shared<::cw::lib::Window>();
            event->id = event_id;
            event->bounds.x = static_cast<short>(LOWORD(position));
            event->bounds.y = static_cast<short>(HIWORD(position));
            adapter->m_sink(name, event);
        };
        if (adapter && message == WM_SIZE) adapter->refresh_client_size(handle);
        switch (message) {
        case WM_PAINT: paint(handle, adapter ? adapter->m_frame : nullptr); return 0;
        case frame_exec_message:
            // 見出しのドラッグ中は DefWindowProc が
            // 入れ子のメッセージループへ入る。そこから任意の Python
            // 互換 UI コールバックを実行すると、 モーダル
            // 表示へ再入しゲームワーカーとデッドロックする可能性
            // がある。 一方、表示用の受信箱は不変フレームをこの
            // UI スレッドでコピーして再描画要求を出すだけな
            // ので、移動中も処理を続ける。 動画・アニメーション
            // ・通常の再描画を止めず、状態を変えるコールバックは
            // WM_EXITSIZEMOVE まで保留する。
            if (adapter && adapter->m_in_size_move) {
                if (adapter->m_frame) adapter->m_frame->drain_present_queue();
                return 0;
            }
            send("EXECFUNC");
            return 0;
        case frame_present_now_message:
            UpdateWindow(handle);
            if (adapter && adapter->m_frame) {
                adapter->m_frame->m_present_acked.store(static_cast<unsigned>(wparam),
                                                         std::memory_order_release);
                adapter->m_frame->m_present_wait_cv.notify_all();
            }
            return 0;
        case WM_MOUSEACTIVATE:
            // Win32 の Python は SDL 経由でマウス入力を受け取
            // り、アクティブ化のクリックを消費する。 ここでも Alt+Tab
            // から戻るクリックをゲーム・メニュー操作にしない。 ただしモードレス
            // のデバッガーから Engine へ戻る場合は同じアプリ内であり、
            // wx と同様にクリックをゲームへ渡す。 消費すると、最初のキャン
            // プ・メニューカードのクリックがフォーカス移動だけになってしまう。
            if (LOWORD(lparam) == HTCLIENT) {
                DWORD foreground_process = 0;
                if (const HWND foreground = GetForegroundWindow(); foreground)
                    (void)GetWindowThreadProcessId(foreground, &foreground_process);
                return foreground_process == GetCurrentProcessId()
                    ? MA_ACTIVATE : MA_ACTIVATEANDEAT;
            }
            break;
        case WM_ACTIVATE:
            if (LOWORD(wparam) == WA_INACTIVE) send("KILLFOCUS");
            else send("ACTIVATE");
            return 0;
        case WM_ACTIVATEAPP:
            send("ACTIVATE_APP", wparam ? 1 : 0);
            return 0;
        case WM_SETFOCUS: send("SETFOCUS"); return 0;
        case WM_KILLFOCUS: send("KILLFOCUS"); return 0;
        case WM_KEYDOWN: send("KEYDOWN", static_cast<Integer>(wparam)); return 0;
        case WM_KEYUP: send("KEYUP", static_cast<Integer>(wparam)); return 0;
        case WM_LBUTTONDOWN: send_mouse("LEFTDOWN", lparam); return 0;
        case WM_LBUTTONUP: send_mouse("LEFTUP", lparam); return 0;
        case WM_MBUTTONDOWN: send_mouse("MIDDLEDOWN", lparam); return 0;
        case WM_MBUTTONUP: send_mouse("MIDDLEUP", lparam); return 0;
        case WM_RBUTTONDOWN: send_mouse("RIGHTDOWN", lparam); return 0;
        case WM_RBUTTONUP: send_mouse("RIGHTUP", lparam); return 0;
        case WM_MOUSEMOVE: {
            if (adapter && adapter->m_sink) {
                auto event = std::make_shared<::cw::lib::Window>();
                event->bounds.x = static_cast<short>(LOWORD(lparam));
                event->bounds.y = static_cast<short>(HIWORD(lparam));
                adapter->m_sink("MOTION", event);
            }
            return 0;
        }
        case ::cardwirth::debug::nonactive_pointer_input_message: {
            const auto kind = static_cast<::cardwirth::debug::NonactivePointerInput>(wparam);
            const auto event_id = static_cast<Integer>(
                ::cardwirth::debug::nonactive_pointer_event_id);
            switch (kind) {
            case ::cardwirth::debug::NonactivePointerInput::motion:
                send_mouse("MOTION", lparam, event_id); break;
            case ::cardwirth::debug::NonactivePointerInput::left_down:
                send_mouse("LEFTDOWN", lparam, event_id); break;
            case ::cardwirth::debug::NonactivePointerInput::left_up:
                send_mouse("LEFTUP", lparam, event_id); break;
            case ::cardwirth::debug::NonactivePointerInput::middle_down:
                send_mouse("MIDDLEDOWN", lparam, event_id); break;
            case ::cardwirth::debug::NonactivePointerInput::middle_up:
                send_mouse("MIDDLEUP", lparam, event_id); break;
            case ::cardwirth::debug::NonactivePointerInput::right_down:
                send_mouse("RIGHTDOWN", lparam, event_id); break;
            case ::cardwirth::debug::NonactivePointerInput::right_up:
                send_mouse("RIGHTUP", lparam, event_id); break;
            }
            return 0;
        }
        case WM_MOUSEWHEEL: send("MOUSEWHEEL", GET_WHEEL_DELTA_WPARAM(wparam)); return 0;
        case WM_MOUSEHWHEEL: send("MOUSEWHEEL", GET_WHEEL_DELTA_WPARAM(wparam),true); return 0;
        case WM_ENTERSIZEMOVE:
            if (adapter) adapter->m_in_size_move = true;
            return 0;
        case WM_EXITSIZEMOVE:
            if (adapter) {
                adapter->m_in_size_move = false;
                if (adapter->m_move_pending) {
                    adapter->m_move_pending = false;
                    send("MOVE");
                }
                if (adapter->m_frame) (void)adapter->post_exec(*adapter->m_frame);
            }
            return 0;
        case WM_SIZE: if (wparam == SIZE_MINIMIZED || wparam == SIZE_RESTORED || wparam == SIZE_MAXIMIZED) send("ICONIZE", wparam == SIZE_MINIMIZED); break;
        case WM_MOVE:
            if (adapter && adapter->m_in_size_move) adapter->m_move_pending = true;
            else send("MOVE");
            break;
        case WM_DROPFILES: {
            if (adapter && adapter->m_frame) {
                const HDROP drop = reinterpret_cast<HDROP>(wparam);
                const UINT count = DragQueryFileW(drop, 0xFFFFFFFFU, nullptr, 0);
                std::vector<String> paths;
                for (UINT index = 0; index < count; ++index) {
                    const UINT length = DragQueryFileW(drop, index, nullptr, 0);
                    std::wstring path(static_cast<std::size_t>(length) + 1U, L'\0');
                    DragQueryFileW(drop, index, path.data(), length + 1);
                    path.resize(length);
                    const int n = WideCharToMultiByte(CP_UTF8, 0, path.data(), static_cast<int>(path.size()), nullptr, 0, nullptr, nullptr);
                    String utf8(static_cast<std::size_t>(std::max(0, n)), '\0');
                    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, path.data(), static_cast<int>(path.size()), utf8.data(), n, nullptr, nullptr);
                    paths.push_back(std::move(utf8));
                }
                DragFinish(drop);
                auto event = std::make_shared<::cw::lib::Window>();
                event->dropped_files = std::move(paths);
                adapter->m_frame->dispatch_native_event("DROPFILES", std::move(event));
            }
            return 0;
        }
        case WM_CLOSE:
#if defined(CARDWIRTH_LOG_ENABLED)
            ::cardwirth::debug::log("Win32 main window WM_CLOSE");
#endif
            send("CLOSE"); return 0;
        case WM_DESTROY:
#if defined(CARDWIRTH_LOG_ENABLED)
            ::cardwirth::debug::log("Win32 main window WM_DESTROY");
#endif
            send("DESTROY"); return 0;
        case WM_NCDESTROY:
#if defined(CARDWIRTH_LOG_ENABLED)
            ::cardwirth::debug::log("Win32 main window WM_NCDESTROY");
#endif
            break;
        default: break;
        }
        return DefWindowProcW(handle, message, wparam, lparam);
    }

public:
    std::optional<::cw::lib::Point> map_client_to_surface(
        const Frame& frame, ::cw::lib::Point point) const override {
        const auto client_width = m_client_width.load(std::memory_order_acquire);
        const auto client_height = m_client_height.load(std::memory_order_acquire);
        Integer surface_width = 0;
        Integer surface_height = 0;
        {
            std::scoped_lock lock(frame.m_present_mutex);
            surface_width = frame.m_present_width;
            surface_height = frame.m_present_height;
        }
        if (client_width <= 0 || client_height <= 0 ||
            surface_width <= 0 || surface_height <= 0) return std::nullopt;
        return ::cw::frame::map_client_point_to_surface(
            point, client_width, client_height, surface_width, surface_height);
    }

    void attach_surface(Frame& frame, const std::shared_ptr<::cw::lib::Window>& panel) override {
        FrameAdapterDefaults::attach_surface(frame, panel);
        WNDCLASSEXW type{};
        type.cbSize = sizeof(type);
        type.lpfnWndProc = window_proc;
        type.hInstance = GetModuleHandleW(nullptr);
        type.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        type.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        type.lpszClassName = L"CardWirthSDLFrame";
        if (!RegisterClassExW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                    "RegisterClassExW(CardWirthSDLFrame) failed");

        // FindWindow は別プロセスの同名ウィンドウを返す場合があるため使わない。 Frame ごとに自身のプロセ
        // スが所有する HWND を作り、WndProc のアダプターポインターの生存期間を Frame に結び付ける。
        const int client_w = panel ? std::max(1, static_cast<int>(panel->bounds.width)) : 640;
        const int client_h = panel ? std::max(1, static_cast<int>(panel->bounds.height)) : 480;
        const auto [window_w, window_h] = main_frame_window_size_for_client(client_w, client_h);
        HWND handle = CreateWindowExW(WS_EX_ACCEPTFILES, type.lpszClassName, L"CardWirthPy_C",
                                      main_frame_window_style(),
                                      CW_USEDEFAULT, CW_USEDEFAULT,
                                      window_w, window_h,
                                      nullptr, nullptr, type.hInstance, this);
        if (!handle)
            throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                    "CreateWindowExW(CardWirthPy_C) failed");
#if defined(CARDWIRTH_LOG_ENABLED)
        ::cardwirth::debug::log("Win32 main window created");
#endif
        frame.m_native_window_handle = reinterpret_cast<std::uintptr_t>(handle);
        if (panel) {
            panel->native_handle = frame.m_native_window_handle;
            RECT initial{};
            if (!GetWindowRect(handle, &initial))
                throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                        "GetWindowRect(initial CardWirthPy_C position) failed");
            // wx と同様、保存された各軸の位置を適用する前に初期位置を決める。
            // OS の値を公開し、保存位置の適用方針は Frame が管理する。
            panel->bounds.x = initial.left;
            panel->bounds.y = initial.top;
        }
        m_frame = &frame;
        refresh_client_size(handle);
        if (handle) {
            DragAcceptFiles(handle, TRUE);
            // wx の起動順に合わせ、CWPy が SDL を初期化して全画面を要求する前に、
            // 通常の所有ウィンドウの寸法を整える。 ここではポップアップ形式を有効にしない。
            if (panel) {
                const auto [outer_w, outer_h] = main_frame_window_size_for_client(client_w, client_h);
                SetWindowPos(handle, nullptr, static_cast<int>(panel->bounds.x), static_cast<int>(panel->bounds.y),
                             outer_w, outer_h,
                             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            }
            ::cardwirth::debug::prepare_nonactive_window(reinterpret_cast<std::uintptr_t>(handle));
            ShowWindow(handle, ::cardwirth::debug::nonactive_automation_enabled()
                                   ? SW_SHOWNOACTIVATE : SW_SHOW);
            UpdateWindow(handle);
#if defined(CARDWIRTH_LOG_ENABLED)
            ::cardwirth::debug::log(String("Win32 main window shown: valid=") +
                                    (IsWindow(handle) ? "true" : "false") +
                                    " visible=" + (IsWindowVisible(handle) ? "true" : "false"));
#endif
        }
    }

    void set_iconized(Frame& frame, bool iconized) override {
        if (const HWND handle = hwnd(frame); handle && IsWindow(handle))
            ShowWindow(handle, iconized ? SW_MINIMIZE : SW_RESTORE);
    }

    void detach_surface(Frame& frame) override {
        const HWND handle = hwnd(frame);
        if (handle && IsWindow(handle)) DestroyWindow(handle);
        m_client_width.store(0, std::memory_order_release);
        m_client_height.store(0, std::memory_order_release);
        frame.m_native_window_handle = 0;
    }

    ::cw::lib::Rect display_geometry(const Frame& frame) const override {
        const HWND handle = hwnd(frame);
        HMONITOR monitor = handle ? MonitorFromWindow(handle, MONITOR_DEFAULTTONEAREST)
                                  : MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        if (monitor && GetMonitorInfoW(monitor, &info)) {
            const RECT& rect = info.rcMonitor;
            return {rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top};
        }
        return FrameAdapterDefaults::display_geometry(frame);
    }

    void set_icon(Frame& frame, const std::shared_ptr<::cw::lib::Window>&) override {
        // Py と同様、アイコン変更では共通 Frame のタイトルを維持する。
        const HWND handle = hwnd(frame);
        if (!handle) return;
        wchar_t executable[MAX_PATH]{};
        const DWORD length = GetModuleFileNameW(nullptr, executable, MAX_PATH);
        if (!length || length >= MAX_PATH) return;
        const HICON icon = static_cast<HICON>(LoadImageW(nullptr, executable, IMAGE_ICON,
                                                         0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE));
        if (icon) {
            SendMessageW(handle, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
            SendMessageW(handle, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
        }
    }

    void set_title(Frame& frame, const String& title) override {
        if (const HWND handle = hwnd(frame)) SetWindowTextW(handle, wide(title).c_str());
    }

    void bind(Frame& frame, EventSink sink) override {
        m_sink = std::move(sink);
        m_frame = &frame;
        const HWND handle = hwnd(frame);
        if (handle) {
            refresh_client_size(handle);
            const bool nonactive = ::cardwirth::debug::nonactive_automation_enabled();
            ::cardwirth::debug::prepare_nonactive_window(reinterpret_cast<std::uintptr_t>(handle));
            ShowWindow(handle, nonactive ? SW_SHOWNOACTIVATE : SW_SHOWNORMAL);
            if (!nonactive) {
                SetForegroundWindow(handle);
                SetFocus(handle);
            }
        }
    }

    bool post_exec(Frame& frame) override {
        const HWND handle = hwnd(frame);
        return handle && IsWindow(handle) && PostMessageW(handle, frame_exec_message, 0, 0) != FALSE;
    }

    void present_now(Frame& frame) override {
        const HWND handle = hwnd(frame);
        if (!handle || !IsWindow(handle)) return;
        if (std::this_thread::get_id() == frame.m_ui_thread) {
            UpdateWindow(handle);
            return;
        }
        const unsigned seq = frame.m_present_seq.fetch_add(1, std::memory_order_acq_rel) + 1U;
        if (!PostMessageW(handle, frame_present_now_message, static_cast<WPARAM>(seq), 0))
            return;
        ::cw::lib::without_runtime_access([&] {
            std::unique_lock lock(frame.m_present_wait_mutex);
            frame.m_present_wait_cv.wait_for(lock, std::chrono::milliseconds(100), [&] {
                return frame.m_present_acked.load(std::memory_order_acquire) >= seq ||
                       ::cw::g_quit_app ||
                       (::cw::g_cwpy && !::cw::g_cwpy->m__running);
            });
        });
    }

    void present(Frame& frame, const std::optional<::cw::lib::Rect>& clip) override {
        const HWND handle = hwnd(frame);
        if (!handle || !IsWindow(handle)) return;

        // このメソッドはウィンドウ所有スレッドの Frame::drain_present_queue
        // だけから呼ぶ。 ゲーム・描画面の座標を現在の表示領域へここで
        // 変換し、ワーカーから Win32 状態に触れる必要をなくす。
        if (!clip) {
            InvalidateRect(handle, nullptr, FALSE);
        } else {
            RECT client{};
            GetClientRect(handle, &client);
            Integer surface_w = 0;
            Integer surface_h = 0;
            {
                std::scoped_lock lock(frame.m_present_mutex);
                surface_w = frame.m_present_width;
                surface_h = frame.m_present_height;
            }
            const LONG client_w = client.right - client.left;
            const LONG client_h = client.bottom - client.top;
            RECT dirty{};
            if (surface_w > 0 && surface_h > 0 && client_w > 0 && client_h > 0) {
                const auto map_x = [client_w, surface_w](Integer value) -> LONG {
                    return static_cast<LONG>((static_cast<long long>(value) * client_w) / surface_w);
                };
                const auto map_y = [client_h, surface_h](Integer value) -> LONG {
                    return static_cast<LONG>((static_cast<long long>(value) * client_h) / surface_h);
                };
                dirty = {map_x(clip->x), map_y(clip->y),
                         map_x(clip->x + clip->width), map_y(clip->y + clip->height)};
            } else {
                dirty = {static_cast<LONG>(clip->x), static_cast<LONG>(clip->y),
                         static_cast<LONG>(clip->x + clip->width),
                         static_cast<LONG>(clip->y + clip->height)};
            }
            if (dirty.right <= dirty.left) ++dirty.right;
            if (dirty.bottom <= dirty.top) ++dirty.bottom;
            InvalidateRect(handle, &dirty, FALSE);
        }
        // 通常のフレームは非同期のままにし、Win32 が WM_PAINT をまとめられるよ
        // うにする。 描画完了の通知を明示的に必要とする起動時のフレームは、Frame::drain_present_queue
        // が別途 present_now() を呼ぶ。
    }

    void adjust_position(Frame& frame, const std::shared_ptr<::cw::lib::Window>& window) override {
        const HWND handle = hwnd(frame);
        if (!handle || !window || window.get() != as_object<::cw::lib::Window>(frame.m_panel).get()) {
            FrameAdapterDefaults::adjust_position(frame, window);
            return;
        }
        RECT current{};
        if (!GetWindowRect(handle, &current)) return;
        ::cw::lib::Window requested;
        requested.bounds = {window->bounds.x, window->bounds.y,
                            current.right-current.left, current.bottom-current.top};
        // Py は要求位置へ移動してから、そのモニターを調べる。 まだ移動してい
        // ない HWND のモニターではなく、要求した矩形に対して問い合わせる。
        const auto placed=::cw::util::constrain_window_to_work_area(
            requested.bounds, ::cw::util::window_work_area(requested));
        const Integer x=placed.x,y=placed.y;
        SetWindowPos(handle, nullptr, static_cast<int>(x), static_cast<int>(y), 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        window->bounds.x = x;
        window->bounds.y = y;
    }

    void set_client_size(Frame& frame, const std::shared_ptr<::cw::lib::Window>& panel,
                         Integer client_w, Integer client_h) override {
        const HWND handle = hwnd(frame);
        if (!handle || !panel || panel.get() != as_object<::cw::lib::Window>(frame.m_panel).get()) return;
        const int cw = std::max(1, static_cast<int>(client_w));
        const int ch = std::max(1, static_cast<int>(client_h));
        m_client_width.store(cw, std::memory_order_release);
        m_client_height.store(ch, std::memory_order_release);
        RECT bounds{0, 0, cw, ch};
        if (!AdjustWindowRectEx(&bounds, static_cast<DWORD>(GetWindowLongPtrW(handle, GWL_STYLE)),
                                FALSE, static_cast<DWORD>(GetWindowLongPtrW(handle, GWL_EXSTYLE))))
            throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                    "AdjustWindowRectEx(client size) failed");
        SetWindowPos(handle, nullptr, 0, 0, bounds.right - bounds.left, bounds.bottom - bounds.top,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }

    ::cw::lib::Size startup_fullscreen_client_size(const Frame&, ::cw::lib::Size) const override {
        return {std::get<0>(::cw::g_SIZE_GAME), std::get<1>(::cw::g_SIZE_GAME)};
    }

    void set_fullscreen(Frame& frame, bool fullscreen) override {
        const HWND handle = hwnd(frame);
        if (!handle || !IsWindow(handle)) return;
        const auto state = GetWindowLongPtrW(handle, GWL_STYLE) & (WS_VISIBLE | WS_DISABLED);
        SetWindowLongPtrW(handle, GWL_STYLE, state | (fullscreen
            ? static_cast<LONG_PTR>(WS_POPUP) : static_cast<LONG_PTR>(main_frame_window_style())));
        const auto panel = as_object<::cw::lib::Window>(frame.m_panel);
        SetWindowPos(handle, nullptr, panel ? static_cast<int>(panel->bounds.x) : 0,
                     panel ? static_cast<int>(panel->bounds.y) : 0, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }

    // 互換アダプターのフック。処理の振り分けは共通 Frame/Debugger が管理する。
    bool dispatch_menu_accelerator(Frame&, Integer) override { return false; }

    ::cw::frame::ScreenshotWindows screenshot_windows(Frame& frame, bool capture_clients) override {
        auto result = ::cw::dialog::capture_presentation_windows(frame, capture_clients);
        const auto owner = hwnd(frame);
        if (!owner) return result;
        result.root = reinterpret_cast<std::uintptr_t>(owner);
        RECT owner_rect{};
        if (GetWindowRect(owner, &owner_rect)) result.frame_bounds = {
            static_cast<double>(owner_rect.left), static_cast<double>(owner_rect.top),
            static_cast<double>(owner_rect.right-owner_rect.left), static_cast<double>(owner_rect.bottom-owner_rect.top)};
        struct Capture { HWND root; bool pixels; ::cw::frame::ScreenshotWindows* out; } capture{owner,capture_clients,&result};
        EnumWindows([](HWND window, LPARAM data) -> BOOL {
            auto& c = *reinterpret_cast<Capture*>(data);
            if (window == c.root) return TRUE;
            auto parent = GetWindow(window,GW_OWNER);
            if (!parent) parent = GetParent(window);
            auto ancestor = parent;
            while (ancestor && ancestor != c.root) {
                auto next = GetWindow(ancestor,GW_OWNER);
                if (!next) next = GetParent(ancestor);
                if (next == ancestor) break;
                ancestor = next;
            }
            if (ancestor != c.root) return TRUE;
            const auto id = reinterpret_cast<std::uintptr_t>(window);
            // wx の cwpy_debug 属性がない旧ネイティブウィンド
            // ウでは、Engine の所有関係を対象判定に使う。 共通 Presentation
            // のウィンドウは、全ホストでモデルの識別、タイトル・
            // デバッグ情報、描画済みの表示領域の取得を提供する。
            if (std::any_of(c.out->windows.begin(),c.out->windows.end(),[&](const auto& w){return w.id==id;})) return TRUE;
            ::cw::frame::ScreenshotWindow item;
            item.id=id;item.parent=reinterpret_cast<std::uintptr_t>(parent);
            item.visible=IsWindowVisible(window);item.iconized=IsIconic(window);
            const auto focus=GetFocus();item.focused=focus==window || (focus && IsChild(window,focus));
            RECT bounds{},client{};
            if (GetWindowRect(window,&bounds)) item.bounds={static_cast<double>(bounds.left),static_cast<double>(bounds.top),
                static_cast<double>(bounds.right-bounds.left),static_cast<double>(bounds.bottom-bounds.top)};
            const int title_length=GetWindowTextLengthW(window);
            std::wstring title(static_cast<std::size_t>(title_length)+1,L'\0');
            GetWindowTextW(window,title.data(),title_length+1);
            const int bytes=WideCharToMultiByte(CP_UTF8,0,title.data(),title_length,nullptr,0,nullptr,nullptr);
            item.title.resize(static_cast<std::size_t>(std::max(0,bytes)));
            if(bytes)WideCharToMultiByte(CP_UTF8,0,title.data(),title_length,item.title.data(),bytes,nullptr,nullptr);
            if(c.pixels && item.visible && !item.iconized && GetClientRect(window,&client)) {
                const int w=client.right-client.left,h=client.bottom-client.top;
                if(w>0 && h>0) {
                    HDC source=GetDC(window),memory=source?CreateCompatibleDC(source):nullptr;
                    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=w;
                    info.bmiHeader.biHeight=-h;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
                    info.bmiHeader.biCompression=BI_RGB;void* pixels=nullptr;
                    HBITMAP dib=memory?CreateDIBSection(memory,&info,DIB_RGB_COLORS,&pixels,nullptr,0):nullptr;
                    HGDIOBJ old=dib?SelectObject(memory,dib):nullptr;
                    const bool copied=old && BitBlt(memory,0,0,w,h,source,0,0,SRCCOPY);
                    if(copied) {
                        auto surface=std::make_shared<::cw::lib::Surface>();surface->size={w,h};surface->has_pixel_alpha=false;
                        surface->rgba.resize(static_cast<std::size_t>(w)*h*4);
                        ::cw::lib::simd_image::swap_red_blue(static_cast<const std::uint8_t*>(pixels),surface->rgba.data(),static_cast<std::size_t>(w)*h);
                        ::cw::lib::simd_image::force_alpha(surface->rgba.data(),static_cast<std::size_t>(w)*h,255);
                        item.client=std::move(surface);
                    }
                    if(old)SelectObject(memory,old);if(dib)DeleteObject(dib);if(memory)DeleteDC(memory);if(source)ReleaseDC(window,source);
                }
            }
            c.out->windows.push_back(std::move(item));return TRUE;
        },reinterpret_cast<LPARAM>(&capture));
        return result;
    }

    bool compose_dialogs(Frame& frame, ::cw::lib::Bitmap& bitmap, Integer y,
        std::tuple<Integer,Integer,Integer> fore, std::tuple<Integer,Integer,Integer> back) override {
        frame._put_dlgscreenshots(std::shared_ptr<::cw::lib::Bitmap>(&bitmap, [](auto*){}),y,fore,back);
        return true;
    }

    void watch_left_button(Frame&, Integer delay_ms) override {
        const UINT delay = static_cast<UINT>(std::clamp<Integer>(delay_ms, 1, 60000));
        SetTimer(nullptr, 0, delay, [](HWND, UINT, UINT_PTR timer, DWORD) {
            KillTimer(nullptr, timer);
            ::cw::frame::MyApp_FilterEvent_watch_mousebutton();
        });
    }

    std::optional<bool> modal_message(Frame& frame, const String& title, const String& text,
                                       bool question, bool error) override {
        return ::cw::dialog::message::show_common_modal_message(
            frame.shared_from_this(),
            title, text, question, error);
    }
};
#endif

std::shared_ptr<::cw::frame::NativeFrameAdapter> make_native_adapter() {
#ifdef _WIN32
    return std::make_shared<Win32FrameAdapter>();
#else
    return ::cw::frame::make_sdl_native_adapter();
#endif
}

std::mutex g__killlist_mutex_value;
}

namespace cw::frame {

::cw::lib::Point map_client_point_to_surface(
    ::cw::lib::Point point,
    ::cw::lib::Integer client_width,
    ::cw::lib::Integer client_height,
    ::cw::lib::Integer surface_width,
    ::cw::lib::Integer surface_height)
{
    if (client_width <= 0 || client_height <= 0 ||
        surface_width <= 0 || surface_height <= 0) {
        return point;
    }
    const auto scale = [](Integer coordinate, Integer source, Integer target) {
        // Python の int()/cw._s_impl と同じ切り捨
        // て規則を使う。 ポインター座標は小さなネイティブ値だが、最終の整
        // 数変換前の乗算を安全にするため long double を使う。
        return static_cast<Integer>(
            static_cast<long double>(coordinate) *
            static_cast<long double>(target) /
            static_cast<long double>(source));
    };
    return {
        scale(point.x, client_width, surface_width),
        scale(point.y, client_height, surface_height)};
}

ScreenshotWindows NativeFrameAdapter::screenshot_windows(Frame& frame, bool capture_clients)
{
    return ::cw::dialog::capture_presentation_windows(frame, capture_clients);
}

std::shared_ptr<NativeFrameAdapter> make_headless_native_adapter()
{
    return std::make_shared<HeadlessFrameAdapter>();
}

std::shared_ptr<NativeFrameAdapter> make_headless_native_adapter(
    ::cw::lib::Integer default_font_pixel_height)
{
    return std::make_shared<HeadlessFrameAdapter>(default_font_pixel_height);
}

std::optional<::cw::lib::Integer> NativeFrameAdapter::default_font_pixel_height() const
{
    return ::cw::lib::ui::default_font_pixel_height();
}

void center_native_window(std::uintptr_t arg_window, std::uintptr_t arg_parent)
{
#ifdef _WIN32
    const HWND window = reinterpret_cast<HWND>(arg_window);
    if (!window || !IsWindow(window)) return;
    const bool nonactive = ::cardwirth::debug::nonactive_automation_enabled();
    ::cardwirth::debug::prepare_nonactive_window(arg_window);

    HWND parent = reinterpret_cast<HWND>(arg_parent);
    if (!parent || !IsWindow(parent)) {
        parent = GetActiveWindow();
        if (!parent) parent = GetForegroundWindow();
    }
    if (!parent || parent == window) return;

    // 透明なラベルや独自キャンバスを表示する前に、wx.Dialog
    // 相当の不透明な背景を用意する。 ALL の
    // 子ダイアログが、同じホスト領域を使っていた前
    // のダイアログのピクセルを引き継ぐことも防ぐ。
    ::cw::dialog::initialize_native_dialog_background(
        reinterpret_cast<std::uintptr_t>(window));

    const bool all_mode = ::cw::dialog::is_all_mode();
    if (all_mode) {
        if (const HWND host = reinterpret_cast<HWND>(::cw::dialog::all_mode_host_window());
            host && IsWindow(host) && host != window) {
            parent = host;
        }
        // ALL では共通境界で一度だけ wx.Dialog の意味
        // を変える。 ダイアログは移動可能なネイティブ枠を持たないホ
        // ストの固定子描画面とし、 cw/dialog や cw/debug
        // の個別実装には ALL 分岐を持ち込まない。
        (void)::cw::dialog::normalize_all_mode_window(
            reinterpret_cast<std::uintptr_t>(window),
            reinterpret_cast<std::uintptr_t>(parent));
    }

    RECT parent_rect{};
    RECT window_rect{};
    if (!(all_mode ? GetClientRect(parent, &parent_rect)
                   : GetWindowRect(parent, &parent_rect)) ||
        !GetWindowRect(window, &window_rect)) return;
    const int width = window_rect.right - window_rect.left;
    const int height = window_rect.bottom - window_rect.top;
    if (width <= 0 || height <= 0) return;

    wchar_t class_name[128]{};
    GetClassNameW(window, class_name, static_cast<int>(std::size(class_name)));
    const bool debugger = std::wstring_view(class_name) == L"CardWirthDebuggerWindow";
    int x = debugger && !all_mode
        ? parent_rect.right
        : parent_rect.left + ((parent_rect.right - parent_rect.left) - width) / 2;
    int y = debugger && !all_mode
        ? parent_rect.top
        : parent_rect.top + ((parent_rect.bottom - parent_rect.top) - height) / 2;
    if (all_mode) {
        // ALL は物理ホストを一つ持つが、Engine のダイアログは Engine
        // とデバッガーを合わせた領域ではなく Engine の子表示領域に
        // 属する。 ネイティブウィンドウが隠れている間に位置を決め、ホスト
        // 中央で一瞬表示されてから動くのではなく、最初から最終位置に置く。
        if (const auto viewport = ::cw::dialog::all_mode_native_viewport(debugger)) {
            x = viewport->x + (viewport->width - width) / 2;
            y = viewport->y + (viewport->height - height) / 2;
        }
    }
    if (debugger && !all_mode) {
        // Python と同様、モードレスのデバッガーをメインフレームの
        // 横へ置く。 この規則は共通配置ヘルパーにまとめ、cw/debug
        // 自体にプラットフォーム・ALL 分岐を持たせない。 小さい
        // 画面や右端付近など、右側に余地がない場合だけ範囲内へ収める。
        const HMONITOR monitor = MonitorFromWindow(parent, MONITOR_DEFAULTTONEAREST);
        MONITORINFO info{sizeof(MONITORINFO)};
        if (monitor && GetMonitorInfoW(monitor, &info)) {
            if (x + width > info.rcWork.right) x = parent_rect.left - width;
            const int work_width = info.rcWork.right - info.rcWork.left;
            const int work_height = info.rcWork.bottom - info.rcWork.top;
            x = width >= work_width ? info.rcWork.left
                : std::clamp(x, static_cast<int>(info.rcWork.left),
                             static_cast<int>(info.rcWork.right) - width);
            y = height >= work_height ? info.rcWork.top
                : std::clamp(y, static_cast<int>(info.rcWork.top),
                             static_cast<int>(info.rcWork.bottom) - height);
        }
    }
    SetWindowPos(window, nonactive ? nullptr : HWND_TOP, x, y, 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER |
                     (nonactive ? SWP_NOZORDER : 0));
#else
    (void)arg_window;
    (void)arg_parent;
#endif
}

// 所有する索引更新スレッドの完全な型が見える場所で構築する。
// ヘッダー内で既定化すると、構築失敗時の後始末にも不完全な型の削除が必要になる。
Frame::Frame() = default;

Frame::~Frame()
{
    // どちらの受信箱も取り出す前に終了状態を公開する。 同時に
    // 要求するワーカーは、フラグを見て拒否するか、下の取り出
    // し前に予約して完了待ちを解除されるかのどちらかとなる。
    std::deque<PendingCall> pending;
    {
        std::scoped_lock lock(m_exec_mutex);
        m_exec_shutdown.store(true, std::memory_order_release);
        pending.swap(m_exec_queue);
    }
    const auto stopped = std::make_exception_ptr(
        std::runtime_error("Frame UI dispatcher has stopped"));
    for (auto& call : pending) {
        if (call.result) {
            try { call.result->set_exception(stopped); } catch (...) {}
        }
    }

    std::deque<PendingPresent> pending_presents;
    {
        std::scoped_lock lock(m_present_queue_mutex);
        pending_presents.swap(m_present_queue);
    }
    const auto presentation_stopped = std::make_exception_ptr(
        std::runtime_error("Frame presentation dispatcher has stopped"));
    for (auto& request : pending_presents) {
        if (request.completion) {
            try { request.completion->set_exception(presentation_stopped); } catch (...) {}
        }
    }
    m_exec_cv.notify_all();
    if (m_game_thread.joinable()) {
        if (::cw::g_cwpy) ::cw::g_cwpy->m__running = false;
        if (m_game_thread.get_id() == std::this_thread::get_id()) m_game_thread.detach();
        else ::cw::lib::without_runtime_access([&] { m_game_thread.join(); });
    }
    // セッションの作業ディレクトリーやマウント済み Storage を解放する前に、ワーカ
    // ーの終了を待つ。 更新処理のデストラクターは、終了中のワーカーエラーを報告しない。
    m_scenariodb_updater.reset();
    if (m_native_adapter) m_native_adapter->detach_surface(*this);
}

void Frame::set_transient_dialog_model(std::shared_ptr<::cw::lib::Object> arg_model)
{
    std::scoped_lock lock(m_dialog_presentation_mutex);
    m_transient_dialog_model = std::move(arg_model);
}

void Frame::clear_transient_dialog_model(const ::cw::lib::Object* arg_model)
{
    std::scoped_lock lock(m_dialog_presentation_mutex);
    if (!arg_model || m_transient_dialog_model.get() == arg_model)
        m_transient_dialog_model.reset();
}

std::shared_ptr<::cw::lib::Object> Frame::dialog_presentation_model() const
{
    ::cw::lib::RuntimeAccess runtime_access;
    std::scoped_lock lock(m_dialog_presentation_mutex);
    return m_transient_dialog_model ? m_transient_dialog_model
                                    : m_active_dialog_model;
}

void Frame::initialize(std::shared_ptr<::cw::frame::MyApp> arg_app, ::cw::lib::String arg_skindirname)
{
    if (!m_native_adapter) m_native_adapter = make_native_adapter();
    if (const auto pixels = m_native_adapter->default_font_pixel_height();
        pixels && *pixels > 0) {
        // Py と同様、既定ファミリーの 12pt フォントのピクセル高か
        // ら dpi_level を求める。 共通経路でも整数の切り捨て
        // を維持し、対応する計測機能がないホストでは初期値 1 を保つ。
        ::cw::g_dpi_level = static_cast<::cw::lib::Integer>(
            static_cast<double>(*pixels) / 12.0 / (1.0 / 72.0) / 96.0);
    }
    m_ui_thread = std::this_thread::get_id();
    ::cw::setting::bind_default_ui_thread();
    {
        std::scoped_lock lock(m_exec_mutex);
        m_exec_shutdown.store(false, std::memory_order_release);
    }
    m_app = arg_app ? Value(std::static_pointer_cast<::cw::lib::Object>(arg_app)) : Value{};
    m__skindirname = std::move(arg_skindirname);
    m_is_iconized = false;
    m_app_visibility = {};
    m_host_covered.store(false, std::memory_order_release);
    m_audio_suspended = false;
    ::cw::util::set_sound_suspended(false);
    m_has_focus = !::cardwirth::debug::nonactive_automation_enabled();
    m_mouse_active = m_has_focus;
    m_window_position = {0, 0};
    m_panel = std::static_pointer_cast<::cw::lib::Object>(window_for(nullptr, "CardWirthPy_C"));
    auto panel = as_object<::cw::lib::Window>(m_panel);
    if (panel) {
        panel->bounds = {0, 0, std::get<0>(::cw::g_SIZE_GAME), std::get<1>(::cw::g_SIZE_GAME)};
        panel->visible = true;
    }
    m_style = Value(static_cast<Integer>(0));
    m__keybind = true;
    m__sync_running = false;
    m__retry_count = 0;
    m_initialThreadCount = 1;
    m__setting = std::make_shared<::cw::setting::Setting>();
    // PythonのSetting生成時と同じく、FPS・フレーム時間・ローカル設定を先に初期化する。
    // init_settingsは再読込みにも使い、これらの構築時の値を設定し直さない。
    m__setting->initialize();
    // 設定ファイルの既定値と読込みはinit_settingsに一元化する。不正なユーザーファイルはここでは意図的に無視し
    // 、Setting 自身が同じ既定値を適用して解析失敗を記録する。
    try {
        m__setting->init_settings(std::nullopt, true);
    } catch (const std::exception& error) {
#if defined(CARDWIRTH_LOG_ENABLED)
        ::cardwirth::debug::log(String("Setting initialization warning: ") + error.what());
#endif
        // init_skin がリソースを検証する前に、init_settings
        // は解析済みの値を設定している。 ここでそれらを消さず
        // 、Python と同様にスキンエラーを伝播させる。選択
        // スキンを既定の Setting へ黙って置き換えない。
    }
    apply_window_bounds();
    _bind_customevent();
}

void Frame::queue_present(std::shared_ptr<::cw::lib::Surface> arg_surface,
                          std::optional<::cw::lib::Rect> arg_clip,
                          bool arg_wait_for_paint)
{
    if (!arg_surface) return;

    const auto copy_surface = [](const std::shared_ptr<::cw::lib::Surface>& destination,
                                 const std::shared_ptr<::cw::lib::Surface>& source) {
        destination->size = source->size;
        destination->scr_scale = source->scr_scale;
        destination->bmpdepthis1 = source->bmpdepthis1;
        destination->has_colorkey = source->has_colorkey;
        destination->colorkey = source->colorkey;
        destination->depth1 = source->depth1;
        destination->logical_scale = source->logical_scale;
        destination->known_opaque = source->known_opaque;
        destination->surface_alpha = source->surface_alpha;
        destination->surface_alpha_enabled = source->surface_alpha_enabled;
        destination->has_pixel_alpha = source->has_pixel_alpha;
        destination->pixel_bits = source->pixel_bits;
        destination->rgba.resize(source->rgba.size());
        if (!source->rgba.empty())
            std::memcpy(destination->rgba.data(), source->rgba.data(), source->rgba.size());
    };

    std::shared_ptr<::cw::lib::Promise<void>> completion;
    ::cw::lib::Future<void> completed;
    if (arg_wait_for_paint) {
        completion = std::make_shared<::cw::lib::Promise<void>>();
        completed = completion->get_future();
    }

    bool published = false;
    {
        std::scoped_lock lock(m_present_queue_mutex);
        if (m_exec_shutdown.load(std::memory_order_acquire)) {
            if (completion) {
                try {
                    completion->set_exception(std::make_exception_ptr(
                        std::runtime_error("Frame presentation dispatcher has stopped")));
                } catch (...) {}
            }
            if (completed.valid()) completed.get();
            return;
        }
        // 通常のフレームは受信箱であり、無制限に描画履歴をためない。 起動フレーム
        // は wait_for_paint を設定するため、まとめて省略しない。
        if (!arg_wait_for_paint && !m_present_queue.empty() &&
            !m_present_queue.back().wait_for_paint) {
            // 未消費の受信箱のスナップショットを再利用し、新たに更新された行
            // だけをコピーする。 予約フレームを置き換える前に二倍・四倍の描
            // 画面全体を確保・複製すると、毎秒数百 MB の無駄が生じる。
            auto snapshot = m_present_queue.back().surface;
            const auto same_layout = snapshot && snapshot->size.width == arg_surface->size.width &&
                                     snapshot->size.height == arg_surface->size.height &&
                                     snapshot->rgba.size() == arg_surface->rgba.size();
            if (!same_layout || !arg_clip) {
                // 受信箱のロック中は保留パケットを利用側から見られ
                // ないため、 別の数 MB の vector を確
                // 保せず、プールのバッファーをその場で更新する。
                copy_surface(snapshot, arg_surface);
            } else {
                const auto left = std::clamp<Integer>(arg_clip->x, 0, arg_surface->size.width);
                const auto top = std::clamp<Integer>(arg_clip->y, 0, arg_surface->size.height);
                const auto right = std::clamp<Integer>(arg_clip->x + arg_clip->width, left, arg_surface->size.width);
                const auto bottom = std::clamp<Integer>(arg_clip->y + arg_clip->height, top, arg_surface->size.height);
                const auto stride = static_cast<std::size_t>(arg_surface->size.width) * 4U;
                const auto row_bytes = static_cast<std::size_t>(right - left) * 4U;
                for (Integer y = top; y < bottom; ++y) {
                    const auto offset = static_cast<std::size_t>(y) * stride +
                                        static_cast<std::size_t>(left) * 4U;
                    std::copy_n(arg_surface->rgba.data() + offset, row_bytes,
                                snapshot->rgba.data() + offset);
                }
            }
            PendingPresent request{std::move(snapshot), std::move(arg_clip), false, completion};
            // 合成処理は m_scr_draw を再利用するため、最新のス
            // ナップショットには以前の全更新が含まれる。 内容を置き換えて
            // も全更新矩形を維持し、先に予約された範囲のピクセルが m_present_bgra
            // へ届く前に失われないようにする。
            const auto previous_clip = m_present_queue.back().clip;
            if (!previous_clip || !request.clip) {
                request.clip.reset(); // 全体再描画の要求一つで、両方の更新範囲を含める。
            } else {
                const auto left = std::min(previous_clip->x, request.clip->x);
                const auto top = std::min(previous_clip->y, request.clip->y);
                const auto right = std::max(previous_clip->x + previous_clip->width,
                                            request.clip->x + request.clip->width);
                const auto bottom = std::max(previous_clip->y + previous_clip->height,
                                             request.clip->y + request.clip->height);
                request.clip = ::cw::lib::Rect{left, top, right - left, bottom - top};
            }
            m_present_queue.back() = std::move(request);
            ++m_present_replaced_count;
            published = true;
        } else {
            // m_scr_draw はゲームスレッドが所有して再利用する。 空の受
            // 信箱へ要求を入れる場合は、全体の不変なスナップショットを一つ作る。
            std::shared_ptr<::cw::lib::Surface> snapshot;
            for (const auto& candidate : m_present_pool) {
                if (candidate && candidate.use_count() == 1) {
                    snapshot = candidate;
                    break;
                }
            }
            if (!snapshot && m_present_pool.size() < 3U) {
                snapshot = std::make_shared<::cw::lib::Surface>();
                m_present_pool.push_back(snapshot);
            }
            if (!snapshot) {
                // Python は実体のグループを描く。ネイティブのスナップショ
                // ットも、プールの全バッファーが UI に保持されている時に最終
                // 更新を失わないようにする。 プール枠を増やす代わりに受信箱所
                // 有の代替を使い、後の通常フレームもこの一パケットへまとめる。
                snapshot = std::make_shared<::cw::lib::Surface>();
                ++m_present_fallback_count;
            }
            copy_surface(snapshot, arg_surface);
            PendingPresent request{std::move(snapshot), std::move(arg_clip),
                                   arg_wait_for_paint, completion};
            m_present_queue.push_back(std::move(request));
            published = true;
        }
    }

    if (!published) return;

    if (std::this_thread::get_id() == m_ui_thread) {
        drain_present_queue();
    } else {
        const bool posted = m_native_adapter && m_native_adapter->post_exec(*this);
        if (!posted && completion) {
            // 同期的な起動時表示は、起床要求の失敗後にいつ行われ
            // るか不明なホストの確認へ依存できない。 受信箱にま
            // だあれば取り除き、待機側へ確定したエラーを返す。
            bool removed = false;
            {
                std::scoped_lock lock(m_present_queue_mutex);
                const auto queued = std::find_if(m_present_queue.begin(), m_present_queue.end(),
                    [&](const PendingPresent& request) { return request.completion == completion; });
                if (queued != m_present_queue.end()) {
                    m_present_queue.erase(queued);
                    removed = true;
                }
            }
            if (removed) {
                try {
                    completion->set_exception(std::make_exception_ptr(
                        std::runtime_error("Frame presentation dispatcher wake failed")));
                } catch (...) {}
            }
        }
    }
    if (completed.valid()) {
        constexpr auto present_timeout = std::chrono::seconds(2);
        if (::cw::lib::without_runtime_access([&] { return completed.wait_for(present_timeout); }) != std::future_status::ready) {
            std::size_t queue_depth = 0;
            {
                std::scoped_lock lock(m_present_queue_mutex);
                const auto queued = std::find_if(m_present_queue.begin(), m_present_queue.end(),
                    [&](const PendingPresent& request) { return request.completion == completion; });
                if (queued != m_present_queue.end()) m_present_queue.erase(queued);
                queue_depth = m_present_queue.size();
            }
#if defined(CARDWIRTH_LOG_ENABLED)
            ::cardwirth::debug::log(
                "Frame presentation timeout: queue_depth=" + std::to_string(queue_depth) +
                " shutdown=" + (m_exec_shutdown.load(std::memory_order_acquire) ? "true" : "false") +
                " ui_thread=" + (m_ui_thread == std::thread::id{} ? "unset" : "set"));
#endif
            throw std::runtime_error("Frame presentation timed out waiting for the UI thread");
        }
        completed.get();
    }
}

void Frame::drain_present_queue()
{
    ::cw::lib::RuntimeAccess runtime_access;
    if (std::this_thread::get_id() != m_ui_thread) return;

    std::deque<PendingPresent> pending;
    {
        std::scoped_lock lock(m_present_queue_mutex);
        pending.swap(m_present_queue);
    }
    for (auto& request : pending) {
        try {
            if (const auto panel = as_object<::cw::lib::Window>(m_panel))
                panel->visible = true;
            const auto surface = request.surface;
            if (surface && surface->size.width > 0 && surface->size.height > 0) {
                const auto width = surface->size.width;
                const auto height = surface->size.height;
                const auto pixels = static_cast<std::uint64_t>(width) *
                                    static_cast<std::uint64_t>(height);
                if (pixels <= std::numeric_limits<std::size_t>::max() / 4U &&
                    surface->rgba.size() == static_cast<std::size_t>(pixels * 4U)) {
                    std::scoped_lock lock(m_present_mutex);
                    const bool resized = m_present_width != width ||
                                         m_present_height != height ||
                                         m_present_bgra.size() != surface->rgba.size();
                    m_present_surface = surface;
                    m_present_width = width;
                    m_present_height = height;
                    if (resized) m_present_bgra.resize(surface->rgba.size());

                    ::cw::lib::Rect dirty{0, 0, width, height};
                    if (!resized && request.clip) {
                        const auto left = std::max<Integer>(0, request.clip->x);
                        const auto top = std::max<Integer>(0, request.clip->y);
                        const auto right = std::min<Integer>(width,
                            request.clip->x + std::max<Integer>(0, request.clip->width));
                        const auto bottom = std::min<Integer>(height,
                            request.clip->y + std::max<Integer>(0, request.clip->height));
                        dirty = right > left && bottom > top
                            ? ::cw::lib::Rect{left, top, right - left, bottom - top}
                            : ::cw::lib::Rect{};
                    }
                    for (Integer y = dirty.y; y < dirty.y + dirty.height; ++y) {
                        const auto offset = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                                             static_cast<std::size_t>(dirty.x)) * 4U;
                        // ソフトウェアの合成処理は RGBA32 を使うが、Win32
                        // の上から下へ並ぶ DIB は BGRA のバイト列を読む。
                        ::cw::lib::simd_image::swap_red_blue(
                            surface->rgba.data() + offset, m_present_bgra.data() + offset,
                            static_cast<std::size_t>(dirty.width));
                    }
                }
            }
            if (m_native_adapter) {
                m_native_adapter->present(*this, request.clip);
                if (request.wait_for_paint) m_native_adapter->present_now(*this);
            }
            if (request.completion) request.completion->set_value();
        } catch (...) {
            if (request.completion) {
                try { request.completion->set_exception(::cw::lib::capture_exception()); } catch (...) {}
            }
        }
    }
}

void Frame::apply_window_bounds()
{
    auto panel = as_object<::cw::lib::Window>(m_panel);
    if (!panel) return;
    const auto current_x = panel->bounds.x;
    const auto current_y = panel->bounds.y;
    const Integer game_width = std::get<0>(::cw::g_SIZE_GAME);
    const Integer game_height = std::get<1>(::cw::g_SIZE_GAME);

    Real window_scale = 1.0;
    Real drawing_scale = 1.0;
    bool fullscreen = false;
    if (m__setting && m__setting->m_is_expanded) {
        fullscreen = m__setting->m_expandmode == "FullScreen";
        if (!fullscreen) {
            try {
                const Real parsed = std::stod(m__setting->m_expandmode);
                if (std::isfinite(parsed) && parsed > 0.0) window_scale = parsed;
            } catch (...) {
                window_scale = 1.0;
            }
        }
        if (std::isfinite(m__setting->m_expanddrawing) && m__setting->m_expanddrawing > 0.0)
            drawing_scale = m__setting->m_expanddrawing;
    }
    if (fullscreen) {
        const auto display = get_displaysize()->bounds;
        panel->bounds = display;
        if (!m_native_window_handle && m_native_adapter) {
            const auto client = m_native_adapter->startup_fullscreen_client_size(
                *this, {display.width, display.height});
            panel->bounds.width = client.width;
            panel->bounds.height = client.height;
        }
        // Py と同様、FullScreen 指定では初期のウィンドウ倍率を 1 のまま
        // にし、描画倍率には設定値を使う。 SDL のディスプレイ初期化後に init_fullscreenparams
        // でダイアログ・ウィンドウの寸法を調整する。
    } else {
        // パネルの範囲は外枠サイズではなく、wx の表示領域である cw.wins(SIZE_GAME) とする。
        panel->bounds = {0, 0,
                         truncate_scaled_integer(game_width, window_scale),
                         truncate_scaled_integer(game_height, window_scale)};
    }
    ::cw::g_UP_WIN = window_scale;
    ::cw::g_UP_WIN_M = window_scale;
    ::cw::g_UP_SCR = drawing_scale;

    if (m__setting) {
        // Py と同様、設定にない軸は現在位置を維持する。
        // 両軸が未指定なら、中央配置や範囲制限は行わない。
        auto x = current_x;
        auto y = current_y;
        if (!fullscreen || !m_native_window_handle) {
            const auto& saved = m__setting->m_window_position;
            if (std::get<0>(saved) || std::get<1>(saved)) {
                if (std::get<0>(saved)) x = *std::get<0>(saved);
                if (std::get<1>(saved)) y = *std::get<1>(saved);
            }
        }
        panel->bounds.x = x;
        panel->bounds.y = y;
        const auto& saved = m__setting->m_window_position;
        if (m_native_adapter && (std::get<0>(saved) || std::get<1>(saved)) &&
            (!fullscreen || !m_native_window_handle))
            m_native_adapter->adjust_position(*this, panel);
        m_window_position = {panel->bounds.x, panel->bounds.y};
    }
}

namespace {
std::optional<std::pair<::cw::lib::Rect, ::cw::lib::Size>> native_frame_geometry(const Frame& frame)
{
#ifdef _WIN32
    RECT outer{}, client{};
    const auto window = reinterpret_cast<HWND>(frame.m_native_window_handle);
    if (window && GetWindowRect(window, &outer) && GetClientRect(window, &client))
        return std::make_pair(::cw::lib::Rect{outer.left, outer.top,
                                            outer.right - outer.left, outer.bottom - outer.top},
                              ::cw::lib::Size{client.right - client.left, client.bottom - client.top});
    return std::nullopt;
#else
    return query_sdl_frame_geometry(frame);
#endif
}
}

bool Frame::restore_expanded_window_if_needed(::cw::lib::Rect work_area,
                                             ::cw::lib::Size best_size)
{
    if (!m__setting || !m__setting->m_is_expanded ||
        (best_size.width <= work_area.width && best_size.height <= work_area.height)) return false;
    const auto panel = as_object<::cw::lib::Window>(m_panel);
    if (!panel) return false;
    m__setting->m_is_expanded = false;
    ::cw::g_UP_WIN = ::cw::g_UP_WIN_M = ::cw::g_UP_SCR = 1.0;
    panel->bounds.width = std::get<0>(::cw::g_SIZE_GAME);
    panel->bounds.height = std::get<1>(::cw::g_SIZE_GAME);
    // Py と同様、現在位置を保ち、保存された軸だけを再適用してから範囲
    // 内に収める。 起動時の寸法調整に失敗しても、再び中央へ置かない。
    const auto& saved = m__setting->m_window_position;
    if (std::get<0>(saved)) panel->bounds.x = *std::get<0>(saved);
    if (std::get<1>(saved)) panel->bounds.y = *std::get<1>(saved);
    if (m_native_adapter) {
        m_native_adapter->set_client_size(*this, panel, panel->bounds.width, panel->bounds.height);
        if (std::get<0>(saved) || std::get<1>(saved))
            m_native_adapter->adjust_position(*this, panel);
    }
    m_window_position = {panel->bounds.x, panel->bounds.y};
    return true;
}

void Frame::set_fullscreen_mode(bool fullscreen)
{
    const auto panel = as_object<::cw::lib::Window>(m_panel);
    if (!panel || !m_native_adapter) return;
    // Py と同様、ここではネイティブの全画面・表示領域の形だけを変える
    // 。 set_expanded では保存設定より先に要求状態が変わる
    // 場合があり、起動時の範囲を再適用すると古い設定を使って、 CWPy::update_scale
    // が管理する倍率をリセットしてしまう。
    if (fullscreen) {
        panel->bounds = get_displaysize()->bounds;
    } else {
        panel->bounds.width = value_int(::cw::wins(Value(std::get<0>(::cw::g_SIZE_GAME)))).value();
        panel->bounds.height = value_int(::cw::wins(Value(std::get<1>(::cw::g_SIZE_GAME)))).value();
        panel->bounds.x = std::get<0>(m_window_position);
        panel->bounds.y = std::get<1>(m_window_position);
    }
    m_native_adapter->set_fullscreen(*this, fullscreen);
    m_native_adapter->set_client_size(*this, panel, panel->bounds.width, panel->bounds.height);
    if (!fullscreen) m_native_adapter->adjust_position(*this, panel);
}

void Frame::resize_window_to_display()
{
    const auto panel = as_object<::cw::lib::Window>(m_panel);
    if (!panel || !m_native_adapter) return;
    // ALL の外側ウィンドウはアプリ側が所有し、埋め込み Engine からサイズ変更しない。
    if (m_native_adapter->is_hosted()) return;
    const auto display = get_displaysize();
    if (!display) return;
    const auto geometry = native_frame_geometry(*this);
    if (!geometry && !m_native_adapter->is_headless())
        throw std::runtime_error("Frame display resize geometry is unavailable");
    // Py の SetSize は外寸を使う。 ネイティブアダプターは表示領域
    // の寸法を受け取るため、ゲーム寸法の規則ではなく実際の枠の寸法を差し引く。
    const auto decoration_width = geometry ? geometry->first.width - geometry->second.width : 0;
    const auto decoration_height = geometry ? geometry->first.height - geometry->second.height : 0;
    panel->bounds.width = std::max<::cw::lib::Integer>(1, display->bounds.width - decoration_width);
    panel->bounds.height = std::max<::cw::lib::Integer>(1, display->bounds.height - decoration_height);
    m_native_adapter->set_client_size(*this, panel, panel->bounds.width, panel->bounds.height);
}

void Frame::_start_wx(std::shared_ptr<Frame> arg_self)
{
    ::cw::lib::RuntimeAccess runtime_access;
    if (!arg_self || arg_self.get() != this)
        throw std::logic_error("Frame::_start_wx requires its owning shared_ptr");
    auto panel = as_object<::cw::lib::Window>(m_panel);
    if (m_native_adapter) m_native_adapter->attach_surface(*this, panel);
    else if (panel) panel->visible = true;
    // attach_surface は OS が選んだ初期の外枠位置を公開する。 wx の Frame.__init__
    // と同様、未指定の各軸は保持し、保存された軸だけを上書きする。
    if (panel && m_native_adapter && !m_native_adapter->is_hosted() && m__setting) {
        const auto& saved = m__setting->m_window_position;
        if (std::get<0>(saved) || std::get<1>(saved)) {
            if (std::get<0>(saved)) panel->bounds.x = *std::get<0>(saved);
            if (std::get<1>(saved)) panel->bounds.y = *std::get<1>(saved);
            m_native_adapter->adjust_position(*this, panel);
        }
        m_window_position = {panel->bounds.x, panel->bounds.y};
    }
    // Py と同様、全画面やゲームリソースの初期化前に、枠を含
    // む wx.GetBestSize 相当をモニターの作業領
    // 域と比較する。 ALL はアプリ所有のウィンドウへ埋め
    // 込むため、個別に寸法調整する最上位フレームを持たない。
    if (panel && m_native_adapter && !m_native_adapter->is_hosted() &&
        m__setting && m__setting->m_is_expanded) {
        const auto geometry = native_frame_geometry(*this);
        if (!geometry && !m_native_adapter->is_headless())
            throw std::runtime_error("Frame startup geometry is unavailable");
        const auto best = geometry ? ::cw::lib::Size{geometry->first.width, geometry->first.height}
                                   : ::cw::lib::Size{panel->bounds.width, panel->bounds.height};
        const auto work = m_native_adapter->is_headless() ? m_native_adapter->display_geometry(*this)
                                                        : ::cw::util::window_work_area(*panel);
        restore_expanded_window_if_needed(work, best);
    }
    if (panel && m_native_adapter &&
        !(m__setting && m__setting->m_is_expanded && m__setting->m_expandmode == "FullScreen"))
        m_native_adapter->set_client_size(*this, panel, panel->bounds.width, panel->bounds.height);
    if (m_native_adapter && panel && m__setting &&
        (std::get<0>(m__setting->m_window_position) || std::get<1>(m__setting->m_window_position)) &&
        !(m__setting->m_is_expanded && m__setting->m_expandmode == "FullScreen"))
        m_native_adapter->adjust_position(*this, panel);
    if (panel) m_window_position = {panel->bounds.x, panel->bounds.y};
    set_icon(panel);
    _bind();
    (void)apply_startup_skin_selection();
    if (m__setting && m__setting->m_auto_update_files)
        ::cw::update::update_files("Data", "Data", std::vector<String>{"../Scenario/"});
    if (!::cw::g_cwpy) ::cw::g_cwpy = std::make_shared<::cw::thread::CWPy>();
    if (::cw::g_cwpy) {
        // ゲームスレッドが m_frame を読む前に、最終の所有参照を公開する。 開
        // 始後に Value/shared_ptr を置き換えるとデータ競合になる。
        ::cw::g_cwpy->m_frame = std::static_pointer_cast<::cw::lib::Object>(arg_self);
        ::cw::g_cwpy->init(m__setting, arg_self);
        if (!m_game_thread.joinable()) {
            const auto game = ::cw::g_cwpy;
            // このメソッドが戻ると Frame::main は直ちに
            // SDL ホストループへ入る。 ワーカー生成前に意図し
            // た実行状態を公開し、UI ループが初期の false
            // を見て、 CWPy::_run() がスレッド識別
            // 情報を公開する前にプロセスを終了させないようにする。
            game->m__running.store(true, std::memory_order_release);
            m_game_thread = std::thread([game] {
                // Python の threading は run から漏れた例外を報告するがプロ
                // セスは終了しない。 std::thread は terminate を呼ぶため
                // 、run の finally 相当の後処理後にも、そのワーカー境界を維持する。
                try { game->run(); }
                catch (const std::exception& error) {
                    std::cerr << "CWPy worker exception: " << error.what() << '\n'
                              << ::cw::thread::current_exception_stacktrace() << '\n';
                } catch (...) {
                    std::cerr << "CWPy worker exception\n"
                              << ::cw::thread::current_exception_stacktrace() << '\n';
                }
            });
        }
    }
    if (!m_scenariodb_updater && m__setting) {
        // Py と同様、CWPy の後に開始する。Scenario と設定された追加ルートを
        // 更新し、 ScenarioSelect が索引を開く前にデータベースの整理を行う。
        auto updater = std::make_unique<::cw::scenariodb::ScenariodbUpdatingThread>();
        updater->initialize(m__setting, true, m__setting->get_scedir(std::nullopt),
                            m__setting->m_skintype);
        updater->start(true);
        m_scenariodb_updater = std::move(updater);
    }
}

bool Frame::apply_startup_skin_selection()
{
    const auto* name = std::get_if<String>(&m__skindirname.storage());
    if (!m__setting || !name || name->empty()) return false;
    m__setting->m_skindirname = *name;
    m__setting->write();
    m__setting->init_settings(std::nullopt, true);
    return true;
}

void Frame::set_title(::cw::lib::String arg_title)
{
    // Py と同様、Frame.SetTitle を UI の実行処理へ送る。
    // 共通アダプター境界で、共通モデルとネイティブの見出しをまとめて公開する。
    const auto owner = weak_from_this();
    exec_func([owner, title = std::move(arg_title)](const auto&, const auto&) -> Value {
        const auto frame = owner.lock();
        if (!frame || frame->m_exec_shutdown.load(std::memory_order_acquire)) return {};
        if (const auto panel = as_object<::cw::lib::Window>(frame->m_panel)) panel->title = title;
        if (frame->m_window_identity) frame->m_window_identity->title = title;
        if (frame->m_native_adapter) frame->m_native_adapter->set_title(*frame, title);
        return {};
    });
}

void Frame::set_icon(std::shared_ptr<::cw::lib::Window> arg_win)
{
    if (m_native_adapter) m_native_adapter->set_icon(*this, std::move(arg_win));
}

std::shared_ptr<::cw::lib::Window> Frame::get_displaysize()
{
    auto size = std::make_shared<::cw::lib::Window>();
    size->bounds = m_native_adapter ? m_native_adapter->display_geometry(*this)
                                    : ::cw::lib::Rect{0, 0, 1920, 1080};
    return size;
}

void Frame::set_native_adapter(std::shared_ptr<NativeFrameAdapter> arg_adapter)
{
    m_native_adapter = arg_adapter ? std::move(arg_adapter) : make_native_adapter();
}

void Frame::dispatch_native_event(::cw::lib::String arg_name, std::shared_ptr<::cw::lib::Window> arg_event)
{
    ::cw::lib::RuntimeAccess runtime_access;
    try {
        const auto it = m_native_event_handlers.find(arg_name);
        if (it != m_native_event_handlers.end() && it->second) {
            it->second(std::move(arg_event));
            // 新しいウィンドウの位置は、最初のフレームより前に Presentation が決める
            // 。 ここで既存のモードレス画面やユーザーが移動したウィンドウを再び中央へ置かない。
        }
    } catch (const std::exception& error) {
        const String details = String("Frame native event error (" + arg_name + "): ") + error.what() +
            "\r\n\r\nStack trace:\r\n" + ::cw::thread::current_exception_stacktrace();
        if (::cw::g_cwpy && ::cw::g_cwpy->m_backend)
            ::cw::g_cwpy->m_backend->notify(*::cw::g_cwpy, "内部エラー", details, true);
        else if (m_native_adapter)
            (void)m_native_adapter->modal_message(*this, "内部エラー", details, false, true);
        // モーダルの構築に失敗しても、セッションの終了を確定する。 そうしないと call_modaldlg()
        // が公開されなかったウィンドウを永久に待つ。 エラー画面が既に閉じ
        // た場合も、kill_dlg は同じセッションに重ねて適用できる。
        kill_dlg(std::nullopt, false, true);
    } catch (...) {
        const String details = String("Frame native event error (" + arg_name + "): unknown exception") +
            "\r\n\r\nStack trace:\r\n" + ::cw::thread::current_exception_stacktrace();
        if (::cw::g_cwpy && ::cw::g_cwpy->m_backend)
            ::cw::g_cwpy->m_backend->notify(*::cw::g_cwpy, "内部エラー", details, true);
        else if (m_native_adapter)
            (void)m_native_adapter->modal_message(*this, "内部エラー", details, false, true);
        kill_dlg(std::nullopt, false, true);
    }
}

void Frame::update_dialogparams()
{
    // wx はキャッシュしたコントロールを破棄してから置き換えを送る。
    // 共通コントロールは共有モデルなので、入力・アニメーション状態を
    // 終了してからキャッシュの所有権を解放し、親ウィンドウは閉じない。
    const auto retire = [](auto& slot) {
        if (slot && *slot) {
            (*slot)->m__quit = true;
            (*slot)->m__cancel_animation = true;
            if ((*slot)->m_ui_backend) (*slot)->m_ui_backend->close(**slot);
        }
        slot.reset();
    };
    retire(m__cardholder);
    retire(m__handview);
    retire(m__infoview);
    retire(m__replcardholder);
    const auto game = ::cw::g_cwpy;
    if (!game || m_exec_shutdown.load(std::memory_order_acquire)) return;
    const auto frame_owner = weak_from_this();
    const std::weak_ptr<::cw::thread::CWPy> game_owner = game;
    // Python と同様、ゲーム側の処理時に現在のリソースを取得してから、四つ
    // の UI 生成処理を送る。 古いリソースからコントロールを初期化しない。
    game->exec_func([frame_owner, game_owner](const auto&, const auto&) -> Value {
        const auto frame = frame_owner.lock();
        const auto game = game_owner.lock();
        if (!frame || !game || ::cw::g_cwpy != game ||
            frame->m_exec_shutdown.load(std::memory_order_acquire) || !game->m_rsrc)
            return {};
        const auto resource = game->m_rsrc;
        const auto resource_mutex = ::cw::thread::g_init_rsrc;
        for (int kind = 0; kind != 4; ++kind) {
            // 共通のゲーム・UI キューが同じ呼び出しスレッドを使う場合も、 post_func
            // で元の PendingEvent の境界を維持する。
            frame->post_func([frame_owner, game_owner, resource, resource_mutex, kind]
                (const auto&, const auto&) -> Value {
                std::unique_lock<::cw::lib::Mutex> lock;
                if (resource_mutex) lock = std::unique_lock<::cw::lib::Mutex>(*resource_mutex);
                const auto frame = frame_owner.lock();
                const auto game = game_owner.lock();
                if (!frame || !game || ::cw::g_cwpy != game || game->m_rsrc != resource ||
                    frame->m_exec_shutdown.load(std::memory_order_acquire)) return {};
                const auto parent = as_object<::cw::lib::Window>(frame->m_panel);
                if (kind == 0 && (!frame->m__cardholder || !*frame->m__cardholder)) {
                    auto model = std::make_shared<::cw::dialog::cardcontrol::SelectCard>();
                    model->initialize(parent, "CARDPOCKET"); frame->m__cardholder = std::move(model);
                } else if (kind == 1 && (!frame->m__handview || !*frame->m__handview)) {
                    auto model = std::make_shared<::cw::dialog::cardcontrol::HandView>();
                    model->initialize(parent); frame->m__handview = std::move(model);
                } else if (kind == 2 && (!frame->m__infoview || !*frame->m__infoview)) {
                    auto model = std::make_shared<::cw::dialog::cardcontrol::InfoView>();
                    model->initialize(parent); frame->m__infoview = std::move(model);
                } else if (kind == 3 && (!frame->m__replcardholder || !*frame->m__replcardholder)) {
                    auto model = std::make_shared<::cw::dialog::cardcontrol::ReplCardHolder>();
                    model->initialize(parent); frame->m__replcardholder = std::move(model);
                }
                return {};
            });
        }
        return {};
    });
}

void Frame::_bind()
{
    m__keybind = true;
    m_has_focus = !::cardwirth::debug::nonactive_automation_enabled();
    m_native_event_handlers.clear();
    m_native_event_handlers.emplace("EXECFUNC", [this](auto event) { OnEXECFUNC(std::move(event)); });
    m_native_event_handlers.emplace("SETFOCUS", [this](auto event) { OnSetFocus(std::move(event)); });
    m_native_event_handlers.emplace("KILLFOCUS", [this](auto event) { OnKillFocus(std::move(event)); });
    m_native_event_handlers.emplace("POINTERCANCEL", [this](auto event) { OnPointerCancel(std::move(event)); });
    m_native_event_handlers.emplace("KEYUP", [this](auto event) { OnKeyUp(std::move(event)); });
    m_native_event_handlers.emplace("KEYDOWN", [this](auto event) { OnKeyDown(std::move(event)); });
    m_native_event_handlers.emplace("MOTION", [this](auto event) { OnMotion(std::move(event)); });
    m_native_event_handlers.emplace("LEFTUP", [this](auto event) { OnLeftUp(std::move(event)); });
    m_native_event_handlers.emplace("LEFTDOWN", [this](auto event) { OnLeftDown(std::move(event)); });
    m_native_event_handlers.emplace("MIDDLEUP", [this](auto event) { OnMiddleUp(std::move(event)); });
    m_native_event_handlers.emplace("MIDDLEDOWN", [this](auto event) { OnMiddleDown(std::move(event)); });
    m_native_event_handlers.emplace("RIGHTUP", [this](auto event) { OnRightUp(std::move(event)); });
    m_native_event_handlers.emplace("RIGHTDOWN", [this](auto event) { OnRightDown(std::move(event)); });
    m_native_event_handlers.emplace("MOUSEWHEEL", [this](auto event) { OnMouseWheel(std::move(event)); });
    m_native_event_handlers.emplace("DROPFILES", [this](auto event) { OnDropFiles(std::move(event)); });
    m_native_event_handlers.emplace("DESTROY", [this](auto event) { OnDestroy(std::move(event)); });
    m_native_event_handlers.emplace("ICONIZE", [this](auto event) { OnIconize(std::move(event)); });
    m_native_event_handlers.emplace("MOVE", [this](auto event) { OnMove(std::move(event)); });
    m_native_event_handlers.emplace("CLOSE", [this](auto event) { OnCloseFromFrame(std::move(event)); });
    m_native_event_handlers.emplace("ACTIVATE", [this](auto) {
        m__keybind = true;
        m_has_focus = !::cardwirth::debug::nonactive_automation_enabled();
        update_keystate();
    });
    m_native_event_handlers.emplace("ACTIVATE_APP", [this](auto event) {
        OnActivateApp(std::move(event));
    });
    m_native_event_handlers.emplace("SETTINGS", [this](auto event) { OnSETTINGS(std::move(event)); });
    m_native_event_handlers.emplace("MENUCARDINFO", [this](auto event) { OnMENUCARDINFO(std::move(event)); });
    m_native_event_handlers.emplace("YADOSELECT", [this](auto event) { OnYADOSELECT(std::move(event)); });
    m_native_event_handlers.emplace("NOVELSAVESELECT", [this](auto event) { OnNOVELSAVESELECT(std::move(event)); });
    m_native_event_handlers.emplace("PARTYSELECT", [this](auto event) { OnPARTYSELECT(std::move(event)); });
    m_native_event_handlers.emplace("PLAYERSELECT", [this](auto event) { OnPLAYERSELECT(std::move(event)); });
    m_native_event_handlers.emplace("SCENARIOSELECT", [this](auto event) { OnSCENARIOSELECT(std::move(event)); });
    m_native_event_handlers.emplace("ALBUM", [this](auto event) { OnALBUM(std::move(event)); });
    m_native_event_handlers.emplace("PARTYRECORD", [this](auto event) { OnPARTYRECORD(std::move(event)); });
    m_native_event_handlers.emplace("BACKPACK", [this](auto event) { OnBACKPACK(std::move(event)); });
    m_native_event_handlers.emplace("STOREHOUSE", [this](auto event) { OnSTOREHOUSE(std::move(event)); });
    m_native_event_handlers.emplace("CARDPOCKET", [this](auto event) { OnCARDPOCKET(std::move(event)); });
    m_native_event_handlers.emplace("CARDPOCKETB", [this](auto event) { OnCARDPOCKETB(std::move(event)); });
    m_native_event_handlers.emplace("CARDPOCKET_REPLACE", [this](auto event) { OnCARDPOCKET_REPLACE(std::move(event)); });
    m_native_event_handlers.emplace("HANDVIEW", [this](auto event) { OnHANDVIEW(std::move(event)); });
    m_native_event_handlers.emplace("INFOVIEW", [this](auto event) { OnINFOVIEW(std::move(event)); });
    m_native_event_handlers.emplace("CHARAINFO", [this](auto event) { OnCHARAINFO(std::move(event)); });
    m_native_event_handlers.emplace("RETURNTITLE", [this](auto event) { OnRETURNTITLE(std::move(event)); });
    m_native_event_handlers.emplace("SAVE", [this](auto event) { OnSAVE(std::move(event)); });
    m_native_event_handlers.emplace("SAVED_MESSAGE", [this](auto event) { OnSAVED_MESSAGE(std::move(event)); });
    m_native_event_handlers.emplace("LOAD", [this](auto event) { OnLOAD(std::move(event)); });
    m_native_event_handlers.emplace("USECARD", [this](auto event) { OnUSECARD(std::move(event)); });
    m_native_event_handlers.emplace("RUNAWAY", [this](auto event) { OnRUNAWAY(std::move(event)); });
    m_native_event_handlers.emplace("ERROR", [this](auto event) { OnERROR(std::move(event)); });
    m_native_event_handlers.emplace("NOTICE", [this](auto event) { OnNOTICE(std::move(event)); });
    m_native_event_handlers.emplace("MESSAGE", [this](auto event) { OnMESSAGE(std::move(event)); });
    m_native_event_handlers.emplace("YESNO", [this](auto event) { OnYESNO(std::move(event)); });
    m_native_event_handlers.emplace("DATACOMP", [this](auto event) { OnDATACOMP(std::move(event)); });
    m_native_event_handlers.emplace("PARTYEDIT", [this](auto event) { OnPARTYEDIT(std::move(event)); });
    m_native_event_handlers.emplace("BATTLECOMMAND", [this](auto event) { OnBATTLECOMMAND(std::move(event)); });
    m_native_event_handlers.emplace("INSTRUCTIONS", [this](auto event) { OnINSTRUCTIONS(std::move(event)); });
    m_native_event_handlers.emplace("F9", [this](auto event) { OnF9(std::move(event)); });
    if (m_native_adapter) {
        m_native_adapter->bind(*this, [this](String name, std::shared_ptr<::cw::lib::Window> event) {
            dispatch_native_event(std::move(name), std::move(event));
        });
    }
}

void Frame::_bind_customevent()
{
    m__EVTTYPE_EXECFUNC = Value(static_cast<Integer>(1));
    m_dlgeventtypes.clear();
    const std::vector<String> names = {
        "CLOSE", "MENUCARDINFO", "YADOSELECT", "NOVELSAVESELECT", "PARTYSELECT", "PLAYERSELECT",
        "SCENARIOSELECT", "ALBUM", "PARTYRECORD", "BACKPACK", "STOREHOUSE",
        "CARDPOCKET", "CARDPOCKETB", "CARDPOCKET_REPLACE", "HANDVIEW", "INFOVIEW",
        "CHARAINFO", "RETURNTITLE", "SAVE", "SAVED_MESSAGE", "LOAD", "USECARD",
        "RUNAWAY", "ERROR", "NOTICE", "MESSAGE", "YESNO", "DATACOMP", "PARTYEDIT",
        "BATTLECOMMAND", "SETTINGS", "INSTRUCTIONS", "F9"};
    Integer id = 2;
    for (const auto& name : names) m_dlgeventtypes[name] = Value(id++);
}

void Frame::tick_clock(::cw::lib::Integer arg_framerate)
{
    if (arg_framerate <= 0) {
        arg_framerate = m__setting ? m__setting->m_fps : 60;
        if (arg_framerate <= 0) arg_framerate = 60;
    }
    const auto now = std::chrono::steady_clock::now();
    const auto target = 1.0 / static_cast<double>(arg_framerate);
    // Python の max(0, 1/fps - (time.time() - self._clock)) に合わ
    // せる。 start_wait() が m__clock を設定し、まだゼロなら最初のフレームとして待機しない。
    if (m__clock != 0.0) {
        const auto elapsed = std::chrono::duration<double>(now.time_since_epoch()).count() - m__clock;
        const auto remain = target - elapsed;
        if (remain > 0.0) ::cw::lib::without_runtime_access([&] { std::this_thread::sleep_for(std::chrono::duration<double>(remain)); });
    }
    m__clock = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void Frame::wait_frame(::cw::lib::Integer arg_count)
{
    for (Integer i = 0; i < std::max<Integer>(0, arg_count); ++i) tick_clock(0);
}

void Frame::start_wait()
{
    m__clock = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::shared_ptr<::cw::lib::Window> Frame::window_identity()
{
    m_window_identity->native_handle=m_native_window_handle;
    m_window_identity->visible=true;
    // 互換レジストリーはウィンドウの生アドレスをキーにする。 アドレスが再
    // 利用されても、新しい対象へ破棄済みウィンドウの関係を引き継がせない。
    auto state=m_window_identity_initialized
        ? ::cw::util::get_window_state(m_window_identity).value_or(::cw::util::WindowState{})
        : ::cw::util::WindowState{};
    m_window_identity_initialized=true;
    if(m_debugger && *m_debugger && !(*m_debugger)->m_window_identity_initialized) {
        ::cw::util::set_window_state((*m_debugger)->m_window_identity,{});
        (*m_debugger)->m_window_identity_initialized=true;
    }
    if(m_debugger && *m_debugger) {
        auto identity=(*m_debugger)->m_window_identity;
        identity->parent_window=m_window_identity;
        auto debug_state=::cw::util::get_window_state(identity).value_or(::cw::util::WindowState{});
        debug_state.parent=m_window_identity;debug_state.main_frame=false;
        ::cw::util::set_window_state(identity,std::move(debug_state));
    }
    state.main_frame=current_frame().get()==this;
    state.debugger=m_debugger && *m_debugger ? (*m_debugger)->m_window_identity : nullptr;
    ::cw::util::set_window_state(m_window_identity,std::move(state));
    if(auto panel=as_object<::cw::lib::Window>(m_panel)) {
        m_window_identity->bounds={std::get<0>(m_window_position),std::get<1>(m_window_position),panel->bounds.width,panel->bounds.height};
        panel->parent_window=m_window_identity;
        auto panel_state=::cw::util::get_window_state(panel).value_or(::cw::util::WindowState{});
        panel_state.parent=m_window_identity;
        ::cw::util::set_window_state(panel,std::move(panel_state));
    }
    return m_window_identity;
}

void Frame::show_debugger(bool arg_refreshtree)
{
    ::cw::lib::RuntimeAccess runtime_access;
    if (!::cw::g_cwpy || ::cw::g_cwpy->is_novel_mode() ||
        !::cw::g_cwpy->is_debugmode() || m_debugger) return;
    auto dlg = std::make_shared<::cw::debug::debugger::Debugger>();
    // initialize() より前にモデルを公開する。デバッガーの更新関数は Python
    // のゲームから UI へのコールバックに対応し、 予約した UI 状態を
    // 適用する前に、意図的に Frame::m_debugger を確認する。
    m_debugger = dlg;
    dlg->initialize(shared_from_this());
    (void)window_identity();
    if (arg_refreshtree) dlg->refresh_all();
    // Python と同様、デバッガーはモードレスであり Frame のモーダルではない。 m_active_dialog
    // に入れると can_screenshot() が操作を遮るダイアログとして扱ってしまう。
    if (::cw::g_cwpy && ::cw::g_cwpy->m_statusbar) {
        const auto show = ::cw::g_cwpy->m_statusbar->m_showbuttons;
        ::cw::g_cwpy->exec_func([show](const ::cw::lib::Arguments&, const ::cw::lib::KeywordArguments&) {
            if (::cw::g_cwpy && ::cw::g_cwpy->m_statusbar)
                ::cw::g_cwpy->m_statusbar->change(show, false);
            return Value{};
        });
    }
}

void Frame::close_debugger()
{
    ::cw::lib::RuntimeAccess runtime_access;
    if (!m_debugger) return;
    // Python/wx と同様、Close から Destroy を通してモードレスのデバッガ
    // ーを閉じてから、Frame.debugger を解放する。 shared_ptr を先にリ
    // セットすると、ネイティブの WndProc に無効な Debugger ポインターが残る。
    const auto debugger = *m_debugger;
    if (debugger) {
        debugger->OnClose({});
        debugger->OnDestroy({});
    }
    if (m_debugger && (!debugger || (*m_debugger).get() == debugger.get()))
        m_debugger.reset();
    (void)window_identity();
    if (m_active_dialog && m_active_dialog->title == "Debugger") m_active_dialog.reset();
    if (::cw::g_cwpy && ::cw::g_cwpy->m_event) ::cw::g_cwpy->m_event->m_waittime = 0;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_statusbar) {
        const auto show = ::cw::g_cwpy->m_statusbar->m_showbuttons;
        ::cw::g_cwpy->exec_func([show](const ::cw::lib::Arguments&, const ::cw::lib::KeywordArguments&) {
            if (::cw::g_cwpy && ::cw::g_cwpy->m_statusbar)
                ::cw::g_cwpy->m_statusbar->change(show, false);
            return Value{};
        });
    }
}

void Frame::exec_func(::cw::lib::Callback arg_func)
{
    if (!arg_func) return;
    // Py と同様、UI スレッドからの呼び出しも含め、常に AddPendingEvent 相当の予約を行う。
    enqueue_exec({std::move(arg_func), {}, {}, nullptr}, false);
}

void Frame::post_func(::cw::lib::Callback arg_func)
{
    if (!arg_func) return;
    enqueue_exec({std::move(arg_func), {}, {}, nullptr}, false);
}

bool Frame::post_input_update(::cw::lib::Callback arg_func)
{
    if (!arg_func) return false;
    return enqueue_exec_with_input_lease({std::move(arg_func), {}, {}, nullptr}, false, false);
}

void Frame::exec_func_2(::cw::lib::Callback arg_func, ::cw::lib::Value arg_arg1)
{
    if (!arg_func) return;
    enqueue_exec({std::move(arg_func), {std::move(arg_arg1)}, {}, nullptr}, false);
}

void Frame::exec_func_3(::cw::lib::Callback arg_func, ::cw::lib::Value arg_arg1, ::cw::lib::Value arg_arg2)
{
    if (!arg_func) return;
    enqueue_exec({std::move(arg_func), {std::move(arg_arg1), std::move(arg_arg2)}, {}, nullptr}, false);
}

void Frame::exec_func_4(::cw::lib::Callback arg_func, ::cw::lib::Value arg_arg1, ::cw::lib::Value arg_arg2, ::cw::lib::Value arg_arg3)
{
    if (!arg_func) return;
    enqueue_exec({std::move(arg_func), {std::move(arg_arg1), std::move(arg_arg2), std::move(arg_arg3)}, {}, nullptr}, false);
}

void Frame::exec_func_5(::cw::lib::Callback arg_func, ::cw::lib::Value arg_arg1, ::cw::lib::Value arg_arg2, ::cw::lib::Value arg_arg3, ::cw::lib::Value arg_arg4)
{
    if (!arg_func) return;
    enqueue_exec({std::move(arg_func), {std::move(arg_arg1), std::move(arg_arg2), std::move(arg_arg3), std::move(arg_arg4)}, {}, nullptr}, false);
}

void Frame::exec_func_6(::cw::lib::Callback arg_func, ::cw::lib::Value arg_arg1, ::cw::lib::Value arg_arg2, ::cw::lib::Value arg_arg3, ::cw::lib::Value arg_arg4, ::cw::lib::Value arg_arg5)
{
    if (!arg_func) return;
    enqueue_exec({std::move(arg_func), {std::move(arg_arg1), std::move(arg_arg2), std::move(arg_arg3), std::move(arg_arg4), std::move(arg_arg5)}, {}, nullptr}, false);
}

void Frame::exec_func_7(::cw::lib::Callback arg_func, ::cw::lib::Value arg_arg1, ::cw::lib::Value arg_arg2, ::cw::lib::Value arg_arg3, ::cw::lib::Value arg_arg4, ::cw::lib::Value arg_arg5, ::cw::lib::Value arg_arg6)
{
    if (!arg_func) return;
    enqueue_exec({std::move(arg_func), {std::move(arg_arg1), std::move(arg_arg2), std::move(arg_arg3), std::move(arg_arg4), std::move(arg_arg5), std::move(arg_arg6)}, {}, nullptr}, false);
}

void Frame::exec_func_8(::cw::lib::Callback arg_func, ::cw::lib::Value arg_arg1, ::cw::lib::Value arg_arg2, ::cw::lib::Value arg_arg3, ::cw::lib::Value arg_arg4, ::cw::lib::Value arg_arg5, ::cw::lib::Value arg_arg6, ::cw::lib::Value arg_arg7)
{
    if (!arg_func) return;
    enqueue_exec({std::move(arg_func), {std::move(arg_arg1), std::move(arg_arg2), std::move(arg_arg3), std::move(arg_arg4), std::move(arg_arg5), std::move(arg_arg6), std::move(arg_arg7)}, {}, nullptr}, false);
}

void Frame::exec_func_9(::cw::lib::Callback arg_func, ::cw::lib::Value arg_arg1, ::cw::lib::Value arg_arg2, ::cw::lib::Value arg_arg3, ::cw::lib::Value arg_arg4, ::cw::lib::Value arg_arg5, ::cw::lib::Value arg_arg6, ::cw::lib::Value arg_arg7, ::cw::lib::Value arg_arg8)
{
    if (!arg_func) return;
    enqueue_exec({std::move(arg_func), {std::move(arg_arg1), std::move(arg_arg2), std::move(arg_arg3), std::move(arg_arg4), std::move(arg_arg5), std::move(arg_arg6), std::move(arg_arg7), std::move(arg_arg8)}, {}, nullptr}, false);
}

void Frame::exec_func_10(::cw::lib::Callback arg_func, ::cw::lib::Value arg_arg1, ::cw::lib::Value arg_arg2, ::cw::lib::Value arg_arg3, ::cw::lib::Value arg_arg4, ::cw::lib::Value arg_arg5, ::cw::lib::Value arg_arg6, ::cw::lib::Value arg_arg7, ::cw::lib::Value arg_arg8, ::cw::lib::Value arg_arg9)
{
    if (!arg_func) return;
    enqueue_exec({std::move(arg_func), {std::move(arg_arg1), std::move(arg_arg2), std::move(arg_arg3), std::move(arg_arg4), std::move(arg_arg5), std::move(arg_arg6), std::move(arg_arg7), std::move(arg_arg8), std::move(arg_arg9)}, {}, nullptr}, false);
}

void Frame::exec_func_11(::cw::lib::Callback arg_func, const ::cw::lib::Arguments& arg_args, const ::cw::lib::KeywordArguments& arg_kwargs)
{
    if (!arg_func) return;
    enqueue_exec({std::move(arg_func), arg_args, arg_kwargs, nullptr}, false);
}

void Frame::post_modal_completion(::cw::lib::Callback arg_func)
{
    // 明示的な完了処理用の経路。無関係なコマンドは再入しない先入れ先出し順を維持する。
    if (arg_func) enqueue_exec({std::move(arg_func), {}, {}, nullptr, true}, false);
}

void Frame::enqueue_exec(PendingCall arg_call, const bool allow_inline)
{
    (void)enqueue_exec_with_input_lease(std::move(arg_call), allow_inline, true);
}

bool Frame::enqueue_exec_with_input_lease(PendingCall arg_call, const bool allow_inline, const bool retain_input)
{
    // 全ホストでキューと送信・起床規則を共通化する。通常のコマンドは
    // 入力受付権を保持する。 CallAfter 型の絞り込み更新
    // は次の物理入力を許し、連続する通知をまとめられるようにする。
    if (retain_input)
        arg_call.func = ::cw::dialog::retain_current_dialog_input(std::move(arg_call.func));
    bool dispatch_now = false;
    {
        std::scoped_lock lock(m_exec_mutex);
        if (m_exec_shutdown.load(std::memory_order_acquire)) return false;
        m_exec_queue.push_back(std::move(arg_call));
        dispatch_now = allow_inline && std::this_thread::get_id() == m_ui_thread && !m_dispatching;
    }
    if (dispatch_now) {
        OnEXECFUNC(nullptr);
    } else if ((!allow_inline || std::this_thread::get_id() != m_ui_thread) && m_native_adapter) {
        // ワーカーからはキューへ追加し、所有する UI ループを起こすだけとする
        // 。 コールバックが Win32 に触れる場合があるため、ここで OnEXECFUNC
        // を直接呼ばず、m_ui_thread で実行する。
        (void)m_native_adapter->post_exec(*this);
    }
    return true; // キューへの受付を表すだけであり、終了処理で取り消されない保証ではない。
}

::cw::lib::Value Frame::sync_exec(::cw::lib::Callback arg_func)
{
    return sync_exec_11(std::move(arg_func), {}, {} ).value_or(Value{});
}

::cw::lib::Value Frame::sync_exec_2(::cw::lib::Callback arg_func, ::cw::lib::Value arg_arg1)
{
    return sync_exec_11(std::move(arg_func), {std::move(arg_arg1)}, {}).value_or(Value{});
}

::cw::lib::Value Frame::sync_exec_3(::cw::lib::Callback arg_func, ::cw::lib::Value arg_arg1, ::cw::lib::Value arg_arg2)
{
    return sync_exec_11(std::move(arg_func), {std::move(arg_arg1), std::move(arg_arg2)}, {}).value_or(Value{});
}

::cw::lib::Value Frame::sync_exec_4(::cw::lib::Callback arg_func, ::cw::lib::Value arg_arg1, ::cw::lib::Value arg_arg2, ::cw::lib::Value arg_arg3)
{
    return sync_exec_11(std::move(arg_func), {std::move(arg_arg1), std::move(arg_arg2), std::move(arg_arg3)}, {}).value_or(Value{});
}

::cw::lib::Value Frame::sync_exec_5(::cw::lib::Callback arg_func, ::cw::lib::Value arg_arg1, ::cw::lib::Value arg_arg2, ::cw::lib::Value arg_arg3, ::cw::lib::Value arg_arg4)
{
    return sync_exec_11(std::move(arg_func), {std::move(arg_arg1), std::move(arg_arg2), std::move(arg_arg3), std::move(arg_arg4)}, {}).value_or(Value{});
}

::cw::lib::Value Frame::sync_exec_6(::cw::lib::Callback arg_func, ::cw::lib::Value arg_arg1, ::cw::lib::Value arg_arg2, ::cw::lib::Value arg_arg3, ::cw::lib::Value arg_arg4, ::cw::lib::Value arg_arg5)
{
    return sync_exec_11(std::move(arg_func), {std::move(arg_arg1), std::move(arg_arg2), std::move(arg_arg3), std::move(arg_arg4), std::move(arg_arg5)}, {}).value_or(Value{});
}

::cw::lib::Value Frame::sync_exec_7(::cw::lib::Callback arg_func, ::cw::lib::Value arg_arg1, ::cw::lib::Value arg_arg2, ::cw::lib::Value arg_arg3, ::cw::lib::Value arg_arg4, ::cw::lib::Value arg_arg5, ::cw::lib::Value arg_arg6)
{
    return sync_exec_11(std::move(arg_func), {std::move(arg_arg1), std::move(arg_arg2), std::move(arg_arg3), std::move(arg_arg4), std::move(arg_arg5), std::move(arg_arg6)}, {}).value_or(Value{});
}

::cw::lib::Value Frame::sync_exec_8(::cw::lib::Callback arg_func, ::cw::lib::Value arg_arg1, ::cw::lib::Value arg_arg2, ::cw::lib::Value arg_arg3, ::cw::lib::Value arg_arg4, ::cw::lib::Value arg_arg5, ::cw::lib::Value arg_arg6, ::cw::lib::Value arg_arg7)
{
    return sync_exec_11(std::move(arg_func), {std::move(arg_arg1), std::move(arg_arg2), std::move(arg_arg3), std::move(arg_arg4), std::move(arg_arg5), std::move(arg_arg6), std::move(arg_arg7)}, {}).value_or(Value{});
}

::cw::lib::Value Frame::sync_exec_9(::cw::lib::Callback arg_func, ::cw::lib::Value arg_arg1, ::cw::lib::Value arg_arg2, ::cw::lib::Value arg_arg3, ::cw::lib::Value arg_arg4, ::cw::lib::Value arg_arg5, ::cw::lib::Value arg_arg6, ::cw::lib::Value arg_arg7, ::cw::lib::Value arg_arg8)
{
    return sync_exec_11(std::move(arg_func), {std::move(arg_arg1), std::move(arg_arg2), std::move(arg_arg3), std::move(arg_arg4), std::move(arg_arg5), std::move(arg_arg6), std::move(arg_arg7), std::move(arg_arg8)}, {}).value_or(Value{});
}

::cw::lib::Value Frame::sync_exec_10(::cw::lib::Callback arg_func, ::cw::lib::Value arg_arg1, ::cw::lib::Value arg_arg2, ::cw::lib::Value arg_arg3, ::cw::lib::Value arg_arg4, ::cw::lib::Value arg_arg5, ::cw::lib::Value arg_arg6, ::cw::lib::Value arg_arg7, ::cw::lib::Value arg_arg8, ::cw::lib::Value arg_arg9)
{
    return sync_exec_11(std::move(arg_func), {std::move(arg_arg1), std::move(arg_arg2), std::move(arg_arg3), std::move(arg_arg4), std::move(arg_arg5), std::move(arg_arg6), std::move(arg_arg7), std::move(arg_arg8), std::move(arg_arg9)}, {}).value_or(Value{});
}

std::optional<::cw::lib::Value> Frame::sync_exec_11(::cw::lib::Callback arg_func, const ::cw::lib::Arguments& arg_args, const ::cw::lib::KeywordArguments& arg_kwargs)
{
    if (!arg_func) return Value{};
    if (std::this_thread::get_id() == m_ui_thread) {
        ::cw::lib::RuntimeAccess runtime_access;
#if defined(CARDWIRTH_LOG_ENABLED)
        std::size_t queue_size;
        bool dispatching;
        {
            std::scoped_lock lock(m_exec_mutex);
            queue_size = m_exec_queue.size();
            dispatching = m_dispatching;
        }
        ::cardwirth::debug::log("sync_exec_11 inline on UI thread queue=" +
            std::to_string(queue_size) + " dispatching=" +
            (dispatching ? "1" : "0"));
#endif
        return arg_func(arg_args, arg_kwargs);
    }

    auto completion = std::make_shared<::cw::lib::Promise<Value>>();
    auto future = completion->get_future();
    {
        std::scoped_lock lock(m_exec_mutex);
        if (m_exec_shutdown.load(std::memory_order_acquire))
            throw std::runtime_error("Frame UI dispatcher has stopped");
        m_exec_queue.push_back({std::move(arg_func), arg_args, arg_kwargs, completion});
#if defined(CARDWIRTH_LOG_ENABLED)
        ::cardwirth::debug::log("sync_exec_11 queued from worker queue=" +
            std::to_string(m_exec_queue.size()) + " dispatching=" +
            (m_dispatching ? "1" : "0"));
#endif
    }

    // wx.PostEvent に相当する。成功後にコールバックを実行するのは、UI スレッドの OnEXECFUNC だけとする。
    const bool posted = m_native_adapter && m_native_adapter->post_exec(*this);
    if (!posted) {
        bool cancelled = false;
        {
            std::scoped_lock lock(m_exec_mutex);
            const auto queued = std::find_if(m_exec_queue.begin(), m_exec_queue.end(),
                [&](const PendingCall& call) { return call.result == completion; });
            if (queued != m_exec_queue.end()) {
                m_exec_queue.erase(queued);
                cancelled = true;
            }
        }
        // 既に別のwake-upがqueueを取得済みなら、そのUI実行結果を待つ。
        // queueに残っている限りconsumerは起床していないため、shutdown
        // flagの反映を待たずに失敗させる。ここで待つと永久待ちになる。
        if (cancelled) throw std::runtime_error("Frame UI event loop is unavailable");
    }
    // set_value/set_exception、または終了処理のcancelのいずれかで必ず解除される。
#if defined(CARDWIRTH_LOG_ENABLED)
    ::cardwirth::debug::log("sync_exec_11 waiting on worker");
#endif
    // Py の sync_exec は UI コールバックの例外時に初
    // 期値 None を返す場合がある。 ネイティブでは promise
    // の完了で例外または終了エラーを待機中のワーカーへ戻し、失敗を
    // 空の成功結果と解釈させない。 UI 操作も同じ共有状態を必要と
    // する場合があるため、待機中は実行状態へのアクセス権を解放する。
    auto result = ::cw::lib::without_runtime_access([&] { return future.get(); });
#if defined(CARDWIRTH_LOG_ENABLED)
    ::cardwirth::debug::log("sync_exec_11 worker wait done");
#endif
    return result;
}

void Frame::assert_ui_thread() const
{
#ifndef NDEBUG
    // Python の wx Frame のコールバックは暗黙に UI 専用である。 C++ ではその規則を assert_ui_thread() で明示する。
    assert(m_ui_thread == std::thread::id{} ||
           m_ui_thread == std::this_thread::get_id());
#endif
}

void Frame::OnEXECFUNC(std::shared_ptr<::cw::lib::Window> arg_event)
{
    ::cw::lib::RuntimeAccess runtime_access;
    (void)arg_event;
    // 従来の exec_func() 互換経路はこの関数を直接呼んでいた。 ワーカーから来た場合はキ
    // ューに触らず UI ループを起こし、先行する同期呼び出しも含め UI スレッドだけで処理する。
    if (std::this_thread::get_id() != m_ui_thread) {
        {
            std::scoped_lock lock(m_exec_mutex);
            if (m_exec_shutdown.load(std::memory_order_acquire)) return;
        }
        if (m_native_adapter) m_native_adapter->post_exec(*this);
        return;
    }
    // Python/wxのイベント配送と同様、assert対象はUI側でqueueを消費する経路だけ。
    assert_ui_thread();
    // 表示要求は保留コールバックと同じ起床メッセージを使うが
    // 、先に取り出す。 これにより、UI がフレームをコピー
    // して描画した後にだけ、起動時の待機ワーカーを解放する。
    drain_present_queue();
    std::unique_lock lock(m_exec_mutex);
    if (m_dispatching) {
        // 処理中のコールバックが同期モーダルへ入り、そのワーカ
        // ーが上書き確認などのため sync_exec()
        // を使う場合がある。 通常の予約コマンドは再入させな
        // いが、結果待ちの判断要求と明示的に許可した完了経路
        // は処理できるようにする。 そうしないと、記録の完了
        // ・エラーの後処理でモーダル状態を解除できなくなる。
        for (;;) {
            const auto nested = std::find_if(
                m_exec_queue.begin(), m_exec_queue.end(),
                [](const PendingCall& call) { return call.result || call.modal_completion; });
            if (nested == m_exec_queue.end()) break;
            PendingCall call = std::move(*nested);
            m_exec_queue.erase(nested);
            lock.unlock();
            try {
                Value result{};
                if (call.func) result = call.func(call.args, call.kwargs);
                if (call.result) call.result->set_value(std::move(result));
            } catch (...) {
                if (call.result) {
                    try { call.result->set_exception(::cw::lib::capture_exception()); } catch (...) {}
                }
            }
            lock.lock();
        }
        return;
    }
    m_dispatching = true;
    while (!m_exec_queue.empty()) {
        PendingCall call = std::move(m_exec_queue.front());
        m_exec_queue.pop_front();
        lock.unlock();
        try {
            Value result{};
            if (call.func) result = call.func(call.args, call.kwargs);
            if (call.result) call.result->set_value(std::move(result));
        } catch (...) {
#if defined(CARDWIRTH_LOG_ENABLED)
            ::cardwirth::debug::log("OnEXECFUNC callback exception");
            ::cardwirth::debug::log_current_stacktrace("OnEXECFUNC callback exception stack");
#endif
            if (call.result) {
                try { call.result->set_exception(::cw::lib::capture_exception()); } catch (...) {}
            }
            else {
                lock.lock();
                m_dispatching = false;
                m_exec_cv.notify_all();
                throw;
            }
        }
        lock.lock();
    }
    m_dispatching = false;
    m_exec_cv.notify_all();
}

void Frame::OnSetFocus(std::shared_ptr<::cw::lib::Window> arg_event)
{
    ::cw::lib::RuntimeAccess runtime_access;
    (void)arg_event;
    m_has_focus = !::cardwirth::debug::nonactive_automation_enabled();
    m_mouse_active = m_has_focus;
    update_keystate();
    _update_mousepressed();
}

void Frame::update_keystate()
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_keyevent) return;
#ifdef _WIN32
    // Python: wx.GetKeyState(WXK_CONTROL) と is_keyin(K_LCTRL=306) で同期する。
    const bool os_control = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    if (os_control) {
        if (!::cw::g_cwpy->m_keyevent->is_keyin(306))
            ::cw::g_cwpy->m_keyevent->keydown(17);
    } else if (::cw::g_cwpy->m_keyevent->is_keyin(306)) {
        ::cw::g_cwpy->m_keyevent->keyup(17);
    }
#endif
}

void Frame::_update_mousepressed()
{
    if (!m_mouse_active) m_mouse_buttons = {false, false, false};
    if (::cw::g_cwpy) ::cw::g_cwpy->m_mousein = m_mouse_buttons;
}

bool Frame::native_pointer_context_active(
    const std::shared_ptr<::cw::lib::Window>& arg_event) const
{
    if (is_synthetic_nonactive_pointer(arg_event)) return true;
    if (::cardwirth::debug::nonactive_automation_enabled()) return false;
    if (m_has_focus) return true;
    if (!m_debugger) return false;
#ifdef _WIN32
    // Python と同様、Engine フレームまたはモードレスのデバ
    // ッガーがアクティブなら入力を受け付ける。 デバッガーが存在するだけ
    // でアプリがアクティブとはみなさず、別アプリが前面なら入力を防ぐ。
    if (!arg_event) return true; // 画面なしの契約検証用。
    DWORD foreground_process = 0;
    if (const HWND foreground = GetForegroundWindow(); foreground)
        (void)GetWindowThreadProcessId(foreground, &foreground_process);
    return foreground_process == GetCurrentProcessId();
#else
    // SDL はアクティブなアプリウィンドウにだけポインターイベントを送る。 Windows
    // 以外のデバッガーホストには、現在、独立したネイティブのフォーカス API がない。
    return true;
#endif
}

bool Frame::native_pointer_input_blocked() const
{
    return static_cast<bool>(m_active_dialog) || m_modal_pointer_suppressed ||
        (::cw::g_cwpy && ::cw::g_cwpy->is_modal_input_blocked());
}

bool Frame::prepare_native_pointer_button(
    const std::shared_ptr<::cw::lib::Window>& arg_event,
    int arg_button, bool arg_down)
{
    bool* pressed = arg_button == 1 ? &std::get<0>(m_mouse_buttons) :
                    arg_button == 2 ? &std::get<1>(m_mouse_buttons) :
                                      &std::get<2>(m_mouse_buttons);
    bool* cancelled = arg_button == 1 ? &std::get<0>(m_cancelled_pointer_buttons) :
                      arg_button == 2 ? &std::get<1>(m_cancelled_pointer_buttons) :
                                        &std::get<2>(m_cancelled_pointer_buttons);
    if (!arg_down) *pressed = false;
    if (m_modal_pointer_suppressed) {
        if (arg_down) *pressed = true;
        else (void)poll_modal_pointer_release();
        return false;
    }
    if (m_active_dialog ||
        (::cw::g_cwpy && ::cw::g_cwpy->is_modal_input_blocked())) {
        if (arg_down) *pressed = true;
        else _update_mousepressed();
        return false;
    }
    if (!arg_down) {
        _update_mousepressed();
        if (*cancelled) return false;
    }
    if (!native_pointer_context_active(arg_event)) return false;
    if (arg_down) *cancelled = false;
    m_mouse_active = true;
    update_native_mouse_position(arg_event);
    if (arg_down) {
        *pressed = true;
        _update_mousepressed();
    }
    return true;
}

void Frame::begin_modal_pointer_barrier(std::uint64_t arg_epoch)
{
    assert_ui_thread();
    m_modal_pointer_epoch = arg_epoch;
    m_modal_game_committed_epoch = 0;
    m_modal_pointer_suppressed = arg_epoch != 0;
    m_mouse_buttons = {false, false, false};
#ifdef _WIN32
    ReleaseCapture();
#endif
    if (!m_modal_pointer_suppressed) return;
    if (!poll_modal_pointer_release()) {
#ifdef _WIN32
        schedule_modal_pointer_release_poll();
#endif
    }
}

bool Frame::poll_modal_pointer_release()
{
    assert_ui_thread();
    if (!m_modal_pointer_suppressed) return true;
    bool released = true;
#ifdef _WIN32
    released = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) == 0 &&
               (GetAsyncKeyState(VK_MBUTTON) & 0x8000) == 0 &&
               (GetAsyncKeyState(VK_RBUTTON) & 0x8000) == 0;
#else
    released = !std::get<0>(m_mouse_buttons) && !std::get<1>(m_mouse_buttons) &&
               !std::get<2>(m_mouse_buttons);
#endif
    if (!released) return false;

    const auto epoch = m_modal_pointer_epoch;
    m_modal_pointer_suppressed = false;
    m_mouse_buttons = {false, false, false};
    if (::cw::g_cwpy) (void)::cw::g_cwpy->finish_modal_pointer_release(epoch);
    complete_modal_ui_restore(epoch);
    return true;
}

void Frame::complete_modal_ui_restore(std::uint64_t arg_epoch)
{
    assert_ui_thread();
    if (arg_epoch == 0 || m_modal_game_committed_epoch != arg_epoch ||
        m_modal_pointer_suppressed)
        return;
    const auto runtime = ::cw::g_cwpy;
    if (!runtime || !runtime->finish_modal_ui_restore(arg_epoch)) return;
    // 入れ子の確認画面は Engine の HWND ではなく、直近の Presentation
    // 所有者を復元する。 最後のモーダルセッションが共通の階層か
    // ら抜けてから、Engine を再び有効・アクティブにする。
    if (!runtime->is_showingdlg()) set_background_enabled(true);
    m_modal_game_committed_epoch = 0;
    // Python/wx の EndModal は、所有者の復元後、次の pygame
    // フレームより前に戻る。 C++ はステータスバーの再構築を次のゲー
    // ム処理へ送り、終了を始めたボタン自身を処理中に破棄しないようにする。
    runtime->exec_func([runtime, arg_epoch](const auto&, const auto&) {
        if (!runtime || !runtime->is_modal_generation_current(arg_epoch) ||
            runtime->is_modal_input_blocked())
            return Value{};
        if (runtime->m_statusbar && runtime->m_selectedheader &&
            *runtime->m_selectedheader)
            runtime->m_statusbar->change(true, false);
        return Value{};
    });
}

void Frame::OnKillFocus(std::shared_ptr<::cw::lib::Window> arg_event)
{
    ::cw::lib::RuntimeAccess runtime_access;
    (void)arg_event;
    m_has_focus = false;
    m_mouse_active = false;
    OnPointerCancel(arg_event);
    if (::cw::g_cwpy && ::cw::g_cwpy->m_keyevent) ::cw::g_cwpy->m_keyevent->clear();
}

void Frame::OnPointerCancel(std::shared_ptr<::cw::lib::Window> arg_event)
{
    ::cw::lib::RuntimeAccess runtime_access;
    (void)arg_event;
    m_cancelled_pointer_buttons = {true, true, true};
    m_mouse_buttons = {false, false, false};
    _update_mousepressed();
    if (const auto app = as_object<::cw::frame::MyApp>(m_app)) {
        app->m_flick_status = Value(g_FLICK_NONE);
        app->m_flick_window = Value{};
    }
    if (::cw::g_cwpy && ::cw::g_cwpy->m_keyevent)
        ::cw::g_cwpy->m_keyevent->cancel_pointer_sequence();
    post_native_input(::cw::eventhandler::EVENT_POINTERCANCEL);
    if (m_modal_pointer_suppressed) (void)poll_modal_pointer_release();
}

void Frame::OnKeyUp(std::shared_ptr<::cw::lib::Window> arg_event)
{
    ::cw::lib::RuntimeAccess runtime_access;
    if (!arg_event) return;
    if (::cw::g_cwpy && ::cw::g_cwpy->is_modal_input_blocked()) return;
    Integer keycode = normalize_win32_key(arg_event->id);
    if (keycode != 17) update_keystate();
    // Python: Ctrl+P の KEYUP は WXK_SNAPSHOT として relay へ渡す。
    if (keycode == static_cast<Integer>('P')) {
#ifdef _WIN32
        if (GetAsyncKeyState(VK_CONTROL) & 0x8000) keycode = 321;
#endif
    }
    if (::cw::g_cwpy && ::cw::g_cwpy->m_keyevent) ::cw::g_cwpy->m_keyevent->keyup(keycode);
}

void Frame::OnKeyDown(std::shared_ptr<::cw::lib::Window> arg_event)
{
    ::cw::lib::RuntimeAccess runtime_access;
    if (!arg_event) return;
    if (::cw::g_cwpy && ::cw::g_cwpy->is_modal_input_blocked()) return;
    const Integer keycode = normalize_win32_key(arg_event->id);
    // Python の移植版と同様、Alt+F4 はネイティブのウィンドウ管理側で処理する。
    if (keycode == 115 && !m_has_focus) return;
    if (keycode != 17) update_keystate();
    if (m_debugger && *m_debugger && (*m_debugger)->dispatch_menu_accelerator(keycode, arg_event->modifiers)) return;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_keyevent) ::cw::g_cwpy->m_keyevent->keydown(keycode);
}

void Frame::OnMotion(std::shared_ptr<::cw::lib::Window> arg_event)
{
    ::cw::lib::RuntimeAccess runtime_access;
    if (m_modal_pointer_suppressed) {
        (void)poll_modal_pointer_release();
        return;
    }
    if (native_pointer_input_blocked()) return;
    // Python と同様、Engine もデバッガーも非アクティブなら (-1,-1) とする。
    // mousemotion は pygame の移動イベントが管理するため、ここでは設定しない。
    if (!arg_event || !native_pointer_context_active(arg_event)) {
        m_mouse_active = false;
        if (::cw::g_cwpy) ::cw::g_cwpy->m_wxmousepos = {Value(-1), Value(-1)};
        return;
    }
    m_mouse_active = true;
    if (!::cw::g_cwpy) return;
    // WM_MOUSEMOVE は既に表示領域の座標を渡す
    // 。 ここでウィンドウ・全画面の原点を引くと選択バーの
    // 位置からずれるため、そのまま渡す。 ゲーム座標への変
    // 換は update_mousepos 自身が行う。
    update_native_mouse_position(arg_event);
    // ALL のホスト側が SDL のメインスレッドのイベント処理を所有
    // するため、単体ウィンドウと異なり、 移動イベントは Engine
    // の SDL バックエンドへも届くわけではない。 不足する pygame/SDL
    // の移動イベントをここで反映し、ホバー選択と続くクリックが新
    // しい位置を使うようにする。 単体のネイティブアダプターには自
    // 身の SDL 移動イベントが届くため、重複して予約しない。
    if (m_native_adapter && m_native_adapter->is_hosted())
        post_native_input(::cw::eventhandler::EVENT_NONE, 0, 0,
                          arg_event && arg_event->touch);
}

void Frame::OnLeftUp(std::shared_ptr<::cw::lib::Window> arg_event)
{
    ::cw::lib::RuntimeAccess runtime_access;
    if (!prepare_native_pointer_button(arg_event, 1, false)) return;
    // Py の Frame と同様、ボタンの押下・解除だけを転送する。 MyApp
    // の操作認識はダイアログに限定し、メインパネルのフリックはゲーム側で管理する。
    post_native_input(::cw::eventhandler::EVENT_MOUSEBUTTONUP, 0, 1,
                      arg_event && arg_event->touch);
}

void Frame::OnLeftDown(std::shared_ptr<::cw::lib::Window> arg_event)
{
    ::cw::lib::RuntimeAccess runtime_access;
    if (!prepare_native_pointer_button(arg_event, 1, true)) return;
    if (::cw::g_cwpy &&
        ::cw::g_cwpy->m_startup_animation_active.load(std::memory_order_acquire)) {
        ::cw::g_cwpy->m_cut_animation.store(true, std::memory_order_release);
    }
    // ゲームパネルに対してダイアログ用の操作認識を開始しない。 同じ押下・解除は
    // 下の post_native_input から共通のゲーム側認識へ届く。
    post_native_input(::cw::eventhandler::EVENT_MOUSEBUTTONDOWN, 0, 1,
                      arg_event && arg_event->touch);
}

void Frame::OnMiddleUp(std::shared_ptr<::cw::lib::Window> arg_event)
{
    ::cw::lib::RuntimeAccess runtime_access;
    if (!prepare_native_pointer_button(arg_event, 2, false)) return;
    post_native_input(::cw::eventhandler::EVENT_MOUSEBUTTONUP, 0, 2);
}

void Frame::OnMiddleDown(std::shared_ptr<::cw::lib::Window> arg_event)
{
    ::cw::lib::RuntimeAccess runtime_access;
    if (!prepare_native_pointer_button(arg_event, 2, true)) return;
    post_native_input(::cw::eventhandler::EVENT_MOUSEBUTTONDOWN, 0, 2);
}

void Frame::OnRightUp(std::shared_ptr<::cw::lib::Window> arg_event)
{
    ::cw::lib::RuntimeAccess runtime_access;
    if (!prepare_native_pointer_button(arg_event, 3, false)) return;
    post_native_input(::cw::eventhandler::EVENT_MOUSEBUTTONUP, 0, 3);
}

void Frame::OnRightDown(std::shared_ptr<::cw::lib::Window> arg_event)
{
    ::cw::lib::RuntimeAccess runtime_access;
    if (!prepare_native_pointer_button(arg_event, 3, true)) return;
    post_native_input(::cw::eventhandler::EVENT_MOUSEBUTTONDOWN, 0, 3);
}

void Frame::OnMouseWheel(std::shared_ptr<::cw::lib::Window> arg_event)
{
    ::cw::lib::RuntimeAccess runtime_access;
    if (native_pointer_input_blocked() ||
        !native_pointer_context_active(arg_event)) return;
    _update_mousepressed();
    // 下の型付き InputEvent だけを、ネイティブからゲームへのホイール
    // イベントとする。 ゲームハンドラーは InputEvent しか消費しない
    // ため、互換 Event を追加で送ると m_events に残ってしまう。
    post_native_input(::cw::eventhandler::EVENT_MOUSEBUTTONUP, 0,
                      ::cw::util::get_wheelrotation(arg_event) > 0 ? 4 : 5);
}

DropSkinUpdateRequest make_drop_skin_update_request(String arg_skindirname)
{
    return {std::move(arg_skindirname), true, true, std::nullopt, true, false};
}

void queue_drop_skin_update(
    std::shared_ptr<::cw::thread::CWPy> arg_runtime,
    DropSkinUpdateRequest arg_request,
    DropSkinUpdateApply arg_apply)
{
    if (!arg_runtime) return;
    arg_runtime->exec_func(
        [runtime = std::move(arg_runtime), request = std::move(arg_request),
         apply = std::move(arg_apply)](const auto&, const auto&) -> Value {
            if (::cw::g_cwpy != runtime) return {};
            if (apply) {
                apply(*runtime, request);
            } else {
                runtime->update_skin(request.skindirname, request.changearea,
                                     request.restartop, request.afterfunc,
                                     request.switch_skin, request.switch_yado);
            }
            return {};
        });
}

bool Frame::show_drop_dialog(
    const std::shared_ptr<::cw::lib::Object>& model,
    const String& title,
    const std::function<void()>& after_show)
{
    ::cw::lib::RuntimeAccess runtime_access;
    const auto game = ::cw::g_cwpy;
    if (!game) return false;
    // これらの直接の UI 呼び出しは call_modaldlg() を通らない。 Py
    // の add_showingdlg() と同様、共通モーダルホストの前に CWPy/Frame
    // の共通セッションを登録し、全終了経路でその同じトークンを閉じる。
    const auto session = game->add_showingdlg();
    begin_dialog_session(session);
    try {
        m_active_dialog = std::make_shared<::cw::lib::Dialog>();
        m_active_dialog->title = title;
        m_active_dialog->visible = true;
        const bool shown = ::cw::dialog::show_common_modal_presentation(model);
        if (shown && after_show) after_show();
        kill_dlg(std::nullopt, false, true, session);
        return shown;
    } catch (...) {
        const auto failure = ::cw::lib::capture_exception();
        try { kill_dlg(std::nullopt, false, true, session); }
        catch (...) { game->record_modal_close_failure(session, "Drop modal cleanup failed"); }
        ::cw::lib::rethrow_exception(failure);
    }
}

void Frame::OnDropFiles(std::shared_ptr<::cw::lib::Window> arg_event)
{
    ::cw::lib::RuntimeAccess runtime_access;
    if (!arg_event) return;
    const auto runtime = ::cw::g_cwpy;
    const auto owner = weak_from_this();
    if (!runtime || owner.expired()) return;
    const auto paths = arg_event->dropped_files;
    // Py と同様、ゲームキューを経由してから UI キューへ戻す。 ホストにド
    // ロップが届いた時点ではリソースが読み込み中の場合があるため、この順序を維持
    // する。 終了後の処理を避けるため、両キューをまたぐ所有者は弱参照とする。
    runtime->exec_func([runtime, owner, paths](const auto&, const auto&) -> Value {
        const auto frame = owner.lock();
        if (!frame || ::cw::g_cwpy != runtime) return {};
        frame->exec_func([runtime, owner, paths](const auto&, const auto&) -> Value {
            const auto frame = owner.lock();
            if (frame && ::cw::g_cwpy == runtime) frame->_on_drop_files(paths);
            return {};
        });
        return {};
    });
}

void Frame::_on_drop_files(std::vector<::cw::lib::String> arg_paths)
{
    ::cw::lib::RuntimeAccess runtime_access;
    assert_ui_thread();
    const auto runtime = ::cw::g_cwpy;
    if (m_active_dialog || (runtime && runtime->is_showingdlg())) return;
    auto parent = as_object<::cw::lib::Window>(m_panel);
    for (const auto& path : arg_paths) append_unique(m_dialog_history, path);
    for (const auto& path : arg_paths) {
        auto extension = std::filesystem::u8path(path).extension().generic_string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (extension != ".exe") continue;
        try {
            auto dialog = std::make_shared<::cw::dialog::skin::SkinConversionDialog>();
            dialog->initialize(parent, path, false, std::nullopt);
            (void)show_drop_dialog(dialog, "SkinConversion", [dialog] {
            if (dialog->m_select_skin && ::cw::g_cwpy) {
                const auto runtime = ::cw::g_cwpy;
                const auto skindirname = dialog->m_skindirname;
                queue_drop_skin_update(
                    runtime, make_drop_skin_update_request(skindirname));
            }
            });
            return;
        } catch (const std::exception& error) {
            _on_error(String(error.what()) + "\n\nStack trace:\n" +
                      ::cw::thread::current_exception_stacktrace(), parent, false);
            return;
        }
    }
    if (!::cw::dialog::skininstall::install_skin(arg_paths, parent, true).empty()) return;
    if (!::cw::g_cwpy || ::cw::g_cwpy->m_is_decompressing) {
        if (::cw::g_cwpy) ::cw::g_cwpy->play_sound("error", false, 100, 1, 0, 0, false);
        return;
    }
    const auto db = open_scenariodb();
    if (!db || !*db || !m__setting) return;
    auto [headers, rejected] = ::cw::dialog::scenarioinstall::to_scenarioheaders(
        arg_paths, *db, m__setting->m_skintype, false);
    if (headers.empty()) return;
    ::cw::g_cwpy->play_sound("signal", false, 100, 1, 0, 0, false);
    auto dialog = std::make_shared<::cw::dialog::scenarioinstall::ScenarioInstall>();
    dialog->initialize(parent, *db, std::move(headers), std::move(rejected),
                       m__setting->m_skintype, m__setting->get_scedir(std::nullopt));
    (void)show_drop_dialog(dialog, "ScenarioInstall");
}

void Frame::OnDestroy(std::shared_ptr<::cw::lib::Window> arg_event)
{
    ::cw::lib::RuntimeAccess runtime_access;
    (void)arg_event;
    ::cw::g_quit_app = true;
    if (::cw::g_cwpy) {
        ::cw::g_cwpy->m__running = false;
    }
    m_present_wait_cv.notify_all();
    if (::cw::g_fsync) ::cw::g_fsync->quit();
    // ワーカー終了待ちより先に、同期 UI 待機を例外で完了する。 ゲームスレッドが sync_exec
    // 中でも、UI スレッドの終了処理と互いに待ち続けないようにする。
    std::deque<PendingCall> pending;
    {
        std::scoped_lock lock(m_exec_mutex);
        m_exec_shutdown.store(true, std::memory_order_release);
        pending.swap(m_exec_queue);
    }
    const auto stopped = std::make_exception_ptr(
        std::runtime_error("Frame UI dispatcher has stopped"));
    for (auto& call : pending) {
        if (call.result) {
            try { call.result->set_exception(stopped); } catch (...) {}
        }
    }

    std::deque<PendingPresent> pending_presents;
    {
        std::scoped_lock lock(m_present_queue_mutex);
        pending_presents.swap(m_present_queue);
    }
    const auto presentation_stopped = std::make_exception_ptr(
        std::runtime_error("Frame presentation dispatcher has stopped"));
    for (auto& request : pending_presents) {
        if (request.completion) {
            try { request.completion->set_exception(presentation_stopped); } catch (...) {}
        }
    }
    m_exec_cv.notify_all();
    if (m_game_thread.joinable() && m_game_thread.get_id() != std::this_thread::get_id())
        ::cw::lib::without_runtime_access([&] { m_game_thread.join(); });
    // 更新完了後に繰り返し呼んでも安全とし、ALL の Home へ戻る時もワーカーの終了を待つ。
    m_scenariodb_updater.reset();
    // 共通の音声・ログ状態を破棄する前に、ゲーム所有の処理を止める。 終
    // 了中にメッセージイベントと UI スレッドが競合しないようにする。
    if (::cw::g_cwpy) {
        ::cw::g_cwpy->stop_allsounds(false);
        if (::cw::g_cwpy->m_advlog) ::cw::g_cwpy->m_advlog->force_quit();
    }
#ifdef _WIN32
    if (m_post_quit_message_on_destroy) PostQuitMessage(0);
#endif
}

void Frame::request_iconize(bool iconized)
{
    ::cw::lib::RuntimeAccess runtime_access;
    // この wx 互換の要求を伝播するのは、単体の子ウィンドウだけとする。
    if (!m_native_adapter || m_native_adapter->is_hosted() ||
        m_native_adapter->is_headless() || m_is_iconized == iconized) return;
    auto event = std::make_shared<::cw::lib::Window>();
    event->id = iconized ? 1 : 0;
    OnIconize(event); // 通知の循環を防ぐため、OS への送信前に状態を公開する。
    m_native_adapter->set_iconized(*this, iconized);
}

void Frame::OnIconize(std::shared_ptr<::cw::lib::Window> arg_event)
{
    ::cw::lib::RuntimeAccess runtime_access;
    const bool iconized = arg_event ? (arg_event->id != 0) : !m_is_iconized;
    if (m_is_iconized == iconized) return;
    m_is_iconized = iconized;
    if (iconized) {
        // SDL のモバイル・バックグラウンド遷移では、別のフォーカス喪失通
        // 知が来るとは限らない。 ここで操作を終了し、アプリ復帰後にカード・
        // ステータスボタンの押下、長押し、フリックが再開しないようにする。
        m_has_focus = false;
        m_mouse_active = false;
        OnPointerCancel(arg_event);
        if (::cw::g_cwpy && ::cw::g_cwpy->m_keyevent)
            ::cw::g_cwpy->m_keyevent->clear();
    }
    update_audio_suspension();
}

void Frame::set_host_covered(bool covered)
{
    m_host_covered.store(covered, std::memory_order_release);
    update_audio_suspension();
}

void Frame::update_audio_suspension()
{
    if (!::cw::g_cwpy) return;
    ::cw::g_cwpy->force_exec_func([this](const ::cw::lib::Arguments&, const ::cw::lib::KeywordArguments&) {
        apply_audio_suspension();
        return Value{};
    });
}

void Frame::apply_audio_suspension()
{
    if (!::cw::g_cwpy) return;
    ::cw::g_cwpy->assert_game_thread();
    // force_exec_func は push_front の優先キューを使う
    // ため、取得済みのイベント値では新しい再開の後に古い停止を適用する場合がある。
    // 代わりに現在の表示状態を整合させ、OS とホストページの理由は分けて扱う。
    const bool covered = m_host_covered.load(std::memory_order_acquire);
    const bool iconized = m_is_iconized || covered;
    // 最小化したウィンドウが隠れたままでも、ホスト側の被覆状態は変わり得る。
    // ミュートから一時停止へ変わる時は、表示状態だけでなく方針も整合させる。
    const bool pause = iconized && (covered ||
        (::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_stop_the_world_with_iconized));
    if (m_audio_suspended == iconized && m_audio_paused == pause) return;
    const bool previously_paused = m_audio_paused;
    m_audio_suspended = iconized;
    m_audio_paused = pause;
#if defined(CARDWIRTH_LOG_ENABLED)
    ::cardwirth::debug::log(iconized ? "Engine audio: suspended" : "Engine audio: resumed");
#endif
    const auto sdata = as_object<::cw::data::SystemData>(::cw::g_cwpy->m_sdata);
    if (iconized) {
        if (sdata) sdata->sleep_timekeeper();
        // ホストページに覆われた状態は、C++ の追加の一時停止理
        // 由である。 通常の最小化設定に関係なく、Engine 所
        // 有の再生を意図的に停止する。全ホストで同じ規則を使う。
        if (pause) {
            // Python は BASS を一時停止する。C++ の共通音楽・音声バックエンドで
            // も、 BASS 以外の音声を含めるため、同じ Engine 所有の停止境界を使う。
            for (const auto& music : ::cw::thread::CWPy::s_music)
                if (music) music->pause();
            ::cw::bassplayer::pause();
            ::cw::util::set_sound_suspended(true);
            for (const auto& sound : ::cw::g_cwpy->m_lastsound_scenario)
                if (sound && *sound) (*sound)->set_volume(true, std::nullopt);
            if (::cw::g_cwpy->m_lastsound_system && *::cw::g_cwpy->m_lastsound_system)
                (*::cw::g_cwpy->m_lastsound_system)->set_volume(false, std::nullopt);
        } else {
            if (previously_paused) {
                ::cw::bassplayer::start();
                ::cw::util::set_sound_suspended(false);
                for (const auto& music : ::cw::thread::CWPy::s_music)
                    if (music) music->resume();
            }
            for (const auto& music : ::cw::thread::CWPy::s_music)
                if (music) music->set_mastervolume(0);
            for (const auto& sound : ::cw::g_cwpy->m_lastsound_scenario)
                if (sound && *sound) (*sound)->set_mastervolume(true, 0);
            if (::cw::g_cwpy->m_lastsound_system && *::cw::g_cwpy->m_lastsound_system)
                (*::cw::g_cwpy->m_lastsound_system)->set_mastervolume(false, 0);
        }
    } else {
        if (sdata) sdata->resume_timekeeper();
        ::cw::bassplayer::start();
        ::cw::util::set_sound_suspended(false);
        Integer volume = 100;
        if (::cw::g_cwpy->m_setting)
            volume = static_cast<Integer>(::cw::g_cwpy->m_setting->m_vol_master * 100.0);
        for (const auto& music : ::cw::thread::CWPy::s_music)
            if (music) {
                music->set_mastervolume(volume);
                music->resume();
            }
        for (const auto& sound : ::cw::g_cwpy->m_lastsound_scenario)
            if (sound && *sound) (*sound)->set_mastervolume(true, volume);
        if (::cw::g_cwpy->m_lastsound_system && *::cw::g_cwpy->m_lastsound_system)
            (*::cw::g_cwpy->m_lastsound_system)->set_mastervolume(false, volume);
        ::cw::g_cwpy->add_lazydraw(nullptr);
    }
}

void Frame::OnCloseFromFrame(std::shared_ptr<::cw::lib::Window> arg_event)
{
    ::cw::lib::RuntimeAccess runtime_access;
    (void)arg_event;
    // call_modaldlg("CLOSE") も add_showingdlg() の後にこのイベント名
    // を送る。 Python のメニューの「終了」は、常に実行中イベントの確認を行う OnCLOSE を通す。
    if ((::cw::g_cwpy && ::cw::g_cwpy->is_showingdlg()) || ydata_is_changed()) {
        OnCLOSE(arg_event);
        return;
    }
    ::cw::g_quit_app = true;
    OnDestroy(arg_event);
}

void Frame::OnMove(std::shared_ptr<::cw::lib::Window> arg_event)
{
    ::cw::lib::RuntimeAccess runtime_access;
    (void)arg_event;
    const bool fullscreen = m__setting && m__setting->m_is_expanded &&
                            m__setting->m_expandmode == "FullScreen";
#ifdef _WIN32
    const HWND handle = reinterpret_cast<HWND>(m_native_window_handle);
    if (!handle || !IsWindow(handle) || fullscreen || IsZoomed(handle)) return;
    RECT bounds{};
    if (!GetWindowRect(handle, &bounds)) return;
    const Integer x = static_cast<Integer>(bounds.left);
    const Integer y = static_cast<Integer>(bounds.top);
    m_window_position = {x, y};
    if (m__setting) m__setting->m_window_position = {x, y};
#else
    if (!arg_event || fullscreen) return;
    m_window_position = {arg_event->bounds.x, arg_event->bounds.y};
    if (m__setting) m__setting->m_window_position = {arg_event->bounds.x, arg_event->bounds.y};
#endif
}

void Frame::OnCLOSE(std::shared_ptr<::cw::lib::Window> arg_event)
{
    ::cw::lib::RuntimeAccess runtime_access;
    (void)arg_event;
    if (!::cw::g_cwpy) {
        ::cw::g_quit_app = true;
        OnDestroy(nullptr);
        return;
    }
    const auto game = ::cw::g_cwpy;
    while (game->m_is_processing && !game->m_is_decompressing) {
        // Python の待機は確認の間にインタープリターロックを解放する。 ネイテ
        // ィブでもワーカーが完了できるよう、共通の操作ロックを解放する必要がある。
        ::cw::lib::RuntimeAccessSuspension suspend;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const bool changed = ydata_is_changed();
    const bool caution = m__setting && m__setting->m_caution_beforesaving;
    const bool need_confirm = (caution && changed) || ::cw::g_cwpy->is_runningevent();
    const bool from_modal = ::cw::g_cwpy->is_showingdlg();
    bool ok = true;
    if (need_confirm) {
        const auto text = changed
            ? frame_message("confirm_quit_changed", "宿データが変更されています。終了してもよろしいですか？")
            : frame_message("confirm_quit", "終了しますか？");
        m_active_dialog = std::make_shared<::cw::lib::Dialog>();
        m_active_dialog->title = "CLOSE";
        m_active_dialog->visible = true;
        const auto accepted = show_defined_yesno_dialog(
            as_object<::cw::lib::Window>(m_panel), text);
        ok = accepted.value_or(false);
    }
    if (from_modal)
        kill_dlg(std::nullopt, false, true);
    else {
        m_active_dialog.reset();
        m_active_dialog_model.reset();
    }
    if (ok) {
        ::cw::g_quit_app = true;
        OnDestroy(nullptr);
    }
}

void Frame::OnSETTINGS(std::shared_ptr<::cw::lib::Window> arg_event)
{
    ::cw::lib::RuntimeAccess runtime_access;
    (void)arg_event;
    if (::cw::g_cwpy && ::cw::g_cwpy->is_novel_mode()) {
        kill_dlg(std::nullopt, false, true);
        return;
    }
    auto dialog = std::make_shared<::cw::dialog::settings::SettingsDialog>();
    m_active_dialog = std::make_shared<::cw::lib::Dialog>();
    m_active_dialog->title = "SETTINGS";
    m_active_dialog->visible = true;
    m_active_dialog_model = dialog;
    m_dialog_history.push_back("SETTINGS");
    dialog->initialize(as_object<::cw::lib::Window>(m_panel));
}

void Frame::OnMENUCARDINFO(std::shared_ptr<::cw::lib::Window> arg_event)
{
    ::cw::lib::RuntimeAccess runtime_access;
    (void)arg_event;
#if defined(CARDWIRTH_LOG_ENABLED)
    ::cardwirth::debug::log("OnMENUCARDINFO begin");
#endif
    if (!::cw::g_cwpy) {
        kill_dlg(std::nullopt, false, true);
        return;
    }
    std::optional<::cw::dialog::cardinfo::CardInfoItem> captured_selection;
    if (const auto it = m_dialog_kwargs.find("selection"); it != m_dialog_kwargs.end()) {
        if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&it->second.storage());
            object && *object) {
            if (auto menu = std::dynamic_pointer_cast<::cw::sprite::card::MenuCard>(*object))
                captured_selection = ::cw::dialog::cardinfo::CardInfoItem{std::move(menu)};
            else if (auto header = std::dynamic_pointer_cast<::cw::header::CardHeader>(*object))
                captured_selection = ::cw::dialog::cardinfo::CardInfoItem{std::move(header)};
        }
    }
    auto dialog = std::make_shared<::cw::dialog::cardinfo::MenuCardInfo>();
    m_dialog_history.push_back("MENUCARDINFO");
    m_active_dialog_model = dialog;
    m_active_dialog = std::static_pointer_cast<::cw::lib::Dialog>(dialog);
    m_active_dialog->title = "MENUCARDINFO";
    dialog->set_parent_callbacks(
        [this](const ::cw::dialog::cardinfo::CardInfoItem& item) {
            std::visit([this](const auto& card) {
                if (!card) return;
                const auto object = std::static_pointer_cast<::cw::lib::Object>(card);
                if (const auto cwpy_card = std::dynamic_pointer_cast<::cw::sprite::card::CWPyCard>(object)) {
                    change_selection(cwpy_card);
                }
            }, item);
        },
        []() {});
    dialog->initialize(as_object<::cw::lib::Window>(m_panel), std::move(captured_selection));
    if (const auto backend = ::cw::dialog::cardinfo::get_ui_backend()) backend->layout(*dialog);
    move_dlg(m_active_dialog, {0, 0});
    m_active_dialog->visible = true;
    // モードレスとしてすぐに戻り、call_modaldlg がゲームスレッドで main_loop
    // を実行できるようにする。 ここで show_modal() を待機させると、sync_exec
    // 内の UI スレッドで GetMessageW が入れ子になり、アプリ全体が停止する。
    if (const auto backend = ::cw::dialog::cardinfo::get_ui_backend()) backend->present(*dialog);
#if defined(CARDWIRTH_LOG_ENABLED)
    ::cardwirth::debug::log("OnMENUCARDINFO end");
#endif
}

void Frame::OnYADOSELECT(std::shared_ptr<::cw::lib::Window> arg_event)
{
    ::cw::lib::RuntimeAccess runtime_access;
    (void)arg_event;
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_rsrc) {
        kill_dlg(std::nullopt, false, true);
        return;
    }
    auto dialog = std::make_shared<::cw::dialog::select::YadoSelect>();
    m_active_dialog = std::make_shared<::cw::lib::Dialog>();
    m_active_dialog->title = "YADOSELECT";
    m_active_dialog->visible = true;
    m_active_dialog_model = dialog;
    m_dialog_history.push_back("YADOSELECT");
    dialog->initialize(as_object<::cw::lib::Window>(m_panel));
    move_dlg(m_active_dialog, {0, 0});
}

void Frame::OnPARTYSELECT(std::shared_ptr<::cw::lib::Window> arg_event)
{
    ::cw::lib::RuntimeAccess runtime_access;
    (void)arg_event;
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_rsrc) {
        kill_dlg(std::nullopt, false, true);
        return;
    }
    auto dialog = std::make_shared<::cw::dialog::select::PartySelect>();
    m_active_dialog = std::make_shared<::cw::lib::Dialog>();
    m_active_dialog->title = "PARTYSELECT";
    m_active_dialog->visible = true;
    m_active_dialog_model = dialog;
    m_dialog_history.push_back("PARTYSELECT");
    dialog->initialize(as_object<::cw::lib::Window>(m_panel));
    move_dlg(m_active_dialog, {0, 0});
}

void Frame::OnPLAYERSELECT(std::shared_ptr<::cw::lib::Window> arg_event)
{
    ::cw::lib::RuntimeAccess runtime_access;
    (void)arg_event;
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_rsrc) {
        kill_dlg(std::nullopt, false, true);
        return;
    }
    auto dialog = std::make_shared<::cw::dialog::select::PlayerSelect>();
    m_active_dialog = std::make_shared<::cw::lib::Dialog>();
    m_active_dialog->title = "PLAYERSELECT";
    m_active_dialog->visible = true;
    m_active_dialog_model = dialog;
    m_dialog_history.push_back("PLAYERSELECT");
    dialog->initialize(as_object<::cw::lib::Window>(m_panel));
    move_dlg(m_active_dialog, {0, 0});
}

std::optional<std::shared_ptr<::cw::scenariodb::Scenariodb>> Frame::open_scenariodb()
{
    // 待機中は C++ のインタープリターロック相当を解放し、 ゲーム・
    // UI の所有者がワーカーや保留中の操作の完了を妨げないようにする。
    ::cw::lib::without_runtime_access([] {
        while (!::cw::scenariodb::ScenariodbUpdatingThread::is_finished())
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    });
    const auto scenario_root = std::filesystem::path("Scenario");
    if (::cardwirth::platform::resolve_mounted_user_path(scenario_root)) {
        // 暗号化 Engine の Scenario は UserData.data
        // 内の論理ディレクトリーである。 ここで従来の相対パスを実体化す
        // ると、ScenarioSelect を開くたびに実行ファイルの隣
        // へ空の平文 Scenario ディレクトリーが作られてしまう。
        (void)::cardwirth::platform::make_mounted_user_directory(scenario_root);
    } else {
        std::error_code ec;
        std::filesystem::create_directories(scenario_root, ec);
    }
    try {
        if (m_scenariodb_updater)
            m_db = m_scenariodb_updater->completed_database();
        // Scenariodb のコンストラクターがデータベースの接続と移行を行う。 initialize()
        // を再度呼ぶと最初の接続を保持したまま二つ目の SQLite 接続
        // を開き、ScenarioSelect の起動を不必要に遅らせる。
        if (!m_db || !*m_db) m_db = std::make_shared<::cw::scenariodb::Scenariodb>();
        return m_db;
    } catch (const std::exception& error) {
        _on_error(String("シナリオデータベースへの接続に失敗しました。\n") + error.what() +
                      "\n\nStack trace:\n" + ::cw::thread::current_exception_stacktrace(),
                  std::nullopt, false);
        return std::nullopt;
    } catch (...) {
        _on_error("シナリオデータベースへの接続に失敗しました。\n不明な例外\n\nStack trace:\n" +
                      ::cw::thread::current_exception_stacktrace(), std::nullopt, false);
        return std::nullopt;
    }
}

void Frame::OnSCENARIOSELECT(std::shared_ptr<::cw::lib::Window> arg_event)
{
    ::cw::lib::RuntimeAccess runtime_access;
    (void)arg_event;
    const auto db = open_scenariodb();
    if (!db || !*db) {
        kill_dlg(std::nullopt, false, true);
        return;
    }
    std::vector<::cw::lib::String> lastscenario;
    ::cw::lib::String lastscenariopath;
    std::vector<::cw::lib::String> lastfindresult;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting) {
        lastscenario = ::cw::g_cwpy->m_setting->m_lastscenario;
        lastscenariopath = ::cw::g_cwpy->m_setting->m_lastscenariopath;
        lastfindresult = ::cw::g_cwpy->m_setting->m_lastfindresult;
    }
    auto dialog = std::make_shared<::cw::dialog::scenarioselect::ScenarioSelect>();
    m_active_dialog = std::make_shared<::cw::lib::Dialog>();
    m_active_dialog->title = "SCENARIOSELECT";
    m_active_dialog->visible = true;
    m_active_dialog_model = dialog;
    m_dialog_history.push_back("SCENARIOSELECT");
    dialog->initialize(as_object<::cw::lib::Window>(m_panel), *db, std::move(lastscenario),
                       std::move(lastscenariopath), std::move(lastfindresult));
    const auto presentation = dialog->dialog_presentation();
    m_active_dialog->bounds.width = presentation.surface ? presentation.surface->size.width : 0;
    m_active_dialog->bounds.height = presentation.surface ? presentation.surface->size.height : 0;
    move_dlg(m_active_dialog, {0, 0});
}

void Frame::OnNOVELSAVESELECT(std::shared_ptr<::cw::lib::Window> arg_event)
{
    ::cw::lib::RuntimeAccess runtime_access;
    (void)arg_event;
    if (!::cw::g_cwpy || ::cw::g_cwpy->m_novel_slot_choices.empty()) {
        kill_dlg(std::nullopt, false, true);
        return;
    }
    auto dialog = std::make_shared<::cw::dialog::edit::SingleChoiceDialog>(
        "セーブデータ選択", "読み込むセーブデータを選択してください。",
        ::cw::g_cwpy->m_novel_slot_choices, 0);
    m_active_dialog = std::make_shared<::cw::lib::Dialog>();
    m_active_dialog->title = "NOVELSAVESELECT";
    m_active_dialog->visible = true;
    m_dialog_history.push_back("NOVELSAVESELECT");
    if (m_native_adapter && m_native_adapter->is_hosted()) {
        const auto dialog_session = m_active_dialog_session;
        const std::weak_ptr<::cw::dialog::edit::SingleChoiceDialog> expected_dialog = dialog;
        const std::weak_ptr<::cw::thread::CWPy> expected_game = ::cw::g_cwpy;
        dialog->set_complete_callback([this, dialog_session, expected_dialog, expected_game](std::optional<int> result) {
            const auto owner = expected_dialog.lock();
            const auto game = expected_game.lock();
            if (!owner || !game || game != ::cw::g_cwpy ||
                m_active_dialog_model != owner || m_active_dialog_session != dialog_session) return;
            game->m_novel_slot_result = result;
            kill_dlg(std::nullopt, false, true, dialog_session);
        });
        m_active_dialog_model = dialog;
        return;
    }
    if (::cw::dialog::show_common_modal_presentation(dialog))
        ::cw::g_cwpy->m_novel_slot_result = dialog->result();
    else
        ::cw::g_cwpy->m_novel_slot_result.reset();
    kill_dlg(std::nullopt, false, true);
}

void Frame::ok_scenarioselect(std::shared_ptr<::cw::dialog::scenarioselect::ScenarioSelect> arg_dlg)
{
#if defined(CARDWIRTH_LOG_ENABLED)
    ::cardwirth::debug::log("Frame ok_scenarioselect begin: dlg=" +
                            std::string(arg_dlg ? "1" : "0") +
                            " list=" + std::to_string(arg_dlg ? arg_dlg->m_list.size() : 0) +
                            " index=" + std::to_string(arg_dlg ? arg_dlg->m_index : -1));
#endif
    if (!::cw::g_cwpy) {
        kill_dlg(std::nullopt, false, true);
        return;
    }
    if (!arg_dlg || arg_dlg->m_list.empty()) {
        kill_dlg(std::nullopt, false, true);
        return;
    }
    const auto [sel, selpath] = arg_dlg->get_selected();
    if (::cw::g_cwpy->m_setting) {
        ::cw::g_cwpy->m_setting->m_lastscenario = sel;
        ::cw::g_cwpy->m_setting->m_lastscenariopath = selpath;
    }
    auto header = as_object<::cw::header::ScenarioHeader>(
        arg_dlg->m_index >= 0 && static_cast<std::size_t>(arg_dlg->m_index) < arg_dlg->m_list.size()
            ? arg_dlg->m_list[static_cast<std::size_t>(arg_dlg->m_index)]
            : Value{});
    if (!header) {
#if defined(CARDWIRTH_LOG_ENABLED)
        ::cardwirth::debug::log("Frame ok_scenarioselect: selected value is not ScenarioHeader");
#endif
        kill_dlg(std::nullopt, false, true);
        return;
    }
#if defined(CARDWIRTH_LOG_ENABLED)
    ::cardwirth::debug::log("Frame ok_scenarioselect selected: path=" + selpath +
                            " sequence=" + std::to_string(sel.size()));
#endif
    // 意図的な変更(Py版と順序が異なる): Py版は change_area(4) を依頼した後に
    // kill_dlg() するが、wxのダイアログはその場でDestroyされて消える。C++版の
    // kill_dlg() はダイアログを閉じる処理(モーダル状態の確定)もゲームスレッドへ
    // 依頼するため、後に積むとカード反転(シナリオ開始)が終わるまでダイアログが
    // 残る。Py版の見た目(開始→ダイアログが閉じる→カード反転)に合わせ、閉じる
    // 依頼を先に積む。
    const auto runtime = ::cw::g_cwpy;
    kill_dlg(std::nullopt, false, true);
    runtime->exec_func([header, sel, selpath](const ::cw::lib::Arguments&, const ::cw::lib::KeywordArguments&) {
        if (!::cw::g_cwpy) return Value{};
#if defined(CARDWIRTH_LOG_ENABLED)
        ::cardwirth::debug::log("Frame ok_scenarioselect exec begin: area=" +
                                std::to_string(::cw::g_cwpy->m_areaid));
#endif
        ::cw::g_cwpy->m_selectedscenario = header;
        if (::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata && (*::cw::g_cwpy->m_ydata)->m_party &&
            *(*::cw::g_cwpy->m_ydata)->m_party) {
            (*(*::cw::g_cwpy->m_ydata)->m_party)->set_lastscenario(sel, selpath);
            (*(*::cw::g_cwpy->m_ydata)->m_party)->set_numbercoupon();
        }
        // Python版の frame.ok_scenarioselect() は change_area(4) を呼ぶ。
        // デフォルト値では開始イベントを実行し、通常の(Default)遷移を使う。
        ::cw::g_cwpy->change_area(4, true, false, {"Default", Value("Default")}, false, false, false, true,
                                  std::nullopt, false, false, false, false, false);
#if defined(CARDWIRTH_LOG_ENABLED)
        ::cardwirth::debug::log("Frame ok_scenarioselect exec end: area=" +
                                std::to_string(::cw::g_cwpy->m_areaid) +
                                " status=" + ::cw::g_cwpy->m_status);
#endif
        return Value{};
    });
}

void Frame::append_killlist(std::shared_ptr<::cw::lib::Dialog> arg_dlg)
{
    if (!arg_dlg) return;
    std::lock_guard lock(g__killlist_mutex_value);
    m_kill_list.push_back(std::move(arg_dlg));
}

void Frame::check_killlist()
{
    std::lock_guard lock(g__killlist_mutex_value);
    if (m_kill_list.empty() || !::cw::g_cwpy || ::cw::g_cwpy->m_lock_menucards) return;
    if (!::cw::g_cwpy->is_runningevent() && !::cw::g_cwpy->is_showingdlg()) {
        m_kill_list.clear();
    }
}

void Frame::OnALBUM(std::shared_ptr<::cw::lib::Window> arg_event)
{
    ::cw::lib::RuntimeAccess runtime_access;
    (void)arg_event;
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_rsrc) {
        kill_dlg(std::nullopt, false, true);
        return;
    }
    auto dialog = std::make_shared<::cw::dialog::select::Album>();
    m_active_dialog = std::make_shared<::cw::lib::Dialog>();
    m_active_dialog->title = "ALBUM";
    m_active_dialog->visible = true;
    m_active_dialog_model = dialog;
    m_dialog_history.push_back("ALBUM");
    dialog->initialize(as_object<::cw::lib::Window>(m_panel));
    move_dlg(m_active_dialog, {0, 0});
}

void Frame::OnPARTYRECORD(std::shared_ptr<::cw::lib::Window> arg_event)
{
    ::cw::lib::RuntimeAccess runtime_access;
    (void)arg_event;
    if (!::cw::g_cwpy) {
        kill_dlg(std::nullopt, false, true);
        return;
    }
    auto dialog = std::make_shared<::cw::dialog::partyrecord::SelectPartyRecord>();
    m_active_dialog = std::make_shared<::cw::lib::Dialog>();
    m_active_dialog->title = "PARTYRECORD";
    m_active_dialog->visible = true;
    m_active_dialog_model = dialog;
    m_dialog_history.push_back("PARTYRECORD");
    dialog->initialize(Value(as_object<::cw::lib::Window>(m_panel)));
    move_dlg(m_active_dialog, {0, 0});
}

void Frame::OnBACKPACK(std::shared_ptr<::cw::lib::Window> arg_event)
{
    ::cw::lib::RuntimeAccess runtime_access;
    (void)arg_event;
#if defined(CARDWIRTH_LOG_ENABLED)
    ::cardwirth::debug::log("OnBACKPACK begin");
#endif
    auto [selection, preinfo] = _get_cardcontrolparams("BACKPACK");
    const auto areaid = change_cardcontrolarea("BACKPACK");
    if (!m__cardholder) {
        auto holder = std::make_shared<::cw::dialog::cardcontrol::SelectCard>();
        holder->initialize(as_object<::cw::lib::Window>(m_panel), "BACKPACK");
        m__cardholder = holder;
    }
    if (!*m__cardholder) {
        kill_dlg(std::nullopt, false, true);
        return;
    }
    // CardControl は Frame_kill_dlg_if_model()
    // を通して閉じる。 表示前に永続モデルを登録し、ネイティブの終了コールバックが
    // 対応する call_modaldlg セッションを所有・解放できるようにする。
    m_active_dialog_model = *m__cardholder;
    m_active_dialog = std::make_shared<::cw::lib::Dialog>();
    m_active_dialog->title = "BACKPACK"; m_active_dialog->visible = true;
    (*m__cardholder)->reconstruct_cardholder("BACKPACK", selection, preinfo, areaid);
#if defined(CARDWIRTH_LOG_ENABLED)
    ::cardwirth::debug::log("OnBACKPACK end");
#endif
}

void Frame::OnSTOREHOUSE(std::shared_ptr<::cw::lib::Window> arg_event)
{
    ::cw::lib::RuntimeAccess runtime_access;
    (void)arg_event;
    auto [selection, preinfo] = _get_cardcontrolparams("STOREHOUSE");
    const auto areaid = change_cardcontrolarea("STOREHOUSE");
    if (!m__cardholder) {
        auto holder = std::make_shared<::cw::dialog::cardcontrol::SelectCard>();
        holder->initialize(as_object<::cw::lib::Window>(m_panel), "STOREHOUSE");
        m__cardholder = holder;
    }
    if (!*m__cardholder) {
        kill_dlg(std::nullopt, false, true);
        return;
    }
    m_active_dialog_model = *m__cardholder;
    m_active_dialog = std::make_shared<::cw::lib::Dialog>();
    m_active_dialog->title = "STOREHOUSE"; m_active_dialog->visible = true;
    (*m__cardholder)->reconstruct_cardholder("STOREHOUSE", selection, preinfo, areaid);
}

void Frame::OnCARDPOCKETB(std::shared_ptr<::cw::lib::Window> arg_event)
{
    ::cw::lib::RuntimeAccess runtime_access;
    (void)arg_event; _cardpocket_impl("CARDPOCKETB");
}

void Frame::OnCARDPOCKET(std::shared_ptr<::cw::lib::Window> arg_event)
{
    ::cw::lib::RuntimeAccess runtime_access;
    (void)arg_event; _cardpocket_impl("CARDPOCKET");
}

void Frame::_cardpocket_impl(::cw::lib::String arg_callname)
{
    auto [selection, preinfo] = _get_cardcontrolparams("CARDPOCKET");
    // Python と同様、EnemyCard/FriendCard 用に開く場合
    // は現在のシナリオエリアを維持する。 カード操作用の特殊エリアへ入るのはパーテ
    // ィー・プレイヤーカードの経路だけとする。 NPC で変えるとシナリオのメニュ
    // ーカードレイヤーが外れ、(0,0) にメニューカードが露出する場合がある。
    const bool character_selection = selection && *selection &&
        (std::dynamic_pointer_cast<::cw::sprite::card::EnemyCard>(*selection) ||
         std::dynamic_pointer_cast<::cw::sprite::card::FriendCard>(*selection));
    const auto areaid = character_selection && ::cw::g_cwpy
        ? ::cw::g_cwpy->m_areaid
        : change_cardcontrolarea("CARDPOCKET");
    if (!m__cardholder) {
        auto holder = std::make_shared<::cw::dialog::cardcontrol::SelectCard>();
        holder->initialize(as_object<::cw::lib::Window>(m_panel), arg_callname);
        m__cardholder = holder;
    }
    if (!*m__cardholder) {
        kill_dlg(std::nullopt, false, true);
        return;
    }
    m_active_dialog_model = *m__cardholder;
    m_active_dialog = std::make_shared<::cw::lib::Dialog>();
    m_active_dialog->title = arg_callname; m_active_dialog->visible = true;
    (*m__cardholder)->reconstruct_cardholder(arg_callname, selection, preinfo, areaid);
}

void Frame::OnHANDVIEW(std::shared_ptr<::cw::lib::Window> arg_event)
{
    ::cw::lib::RuntimeAccess runtime_access;
    (void)arg_event;
    auto [selection, preinfo] = _get_cardcontrolparams("HANDVIEW");
    if (!selection || !*selection) {
        kill_dlg(std::nullopt, false, true);
        return;
    }
    if (!m__handview) {
        auto view = std::make_shared<::cw::dialog::cardcontrol::HandView>();
        view->initialize(as_object<::cw::lib::Window>(m_panel)); m__handview = view;
    }
    if (!*m__handview) {
        kill_dlg(std::nullopt, false, true);
        return;
    }
    m_active_dialog_model = *m__handview;
    m_active_dialog = std::make_shared<::cw::lib::Dialog>();
    m_active_dialog->title = "HANDVIEW"; m_active_dialog->visible = true;
    (*m__handview)->reconstruct_handview(*selection, preinfo);
}

void Frame::OnCARDPOCKET_REPLACE(std::shared_ptr<::cw::lib::Window> arg_event)
{
    ::cw::lib::RuntimeAccess runtime_access;
    (void)arg_event;
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_selection || !::cw::g_cwpy->m_selectedheader) {
        kill_dlg(std::nullopt, false, true); return;
    }
    auto selection = std::dynamic_pointer_cast<::cw::sprite::card::PlayerCard>(*::cw::g_cwpy->m_selection);
    if (!selection) { kill_dlg(std::nullopt, false, true); return; }
    if (!m__replcardholder) {
        auto holder = std::make_shared<::cw::dialog::cardcontrol::ReplCardHolder>();
        holder->initialize(as_object<::cw::lib::Window>(m_panel)); m__replcardholder = holder;
    }
    if (!*m__replcardholder) {
        kill_dlg(std::nullopt, false, true);
        return;
    }
    m_active_dialog_model = *m__replcardholder;
    m_active_dialog = std::make_shared<::cw::lib::Dialog>();
    m_active_dialog->title = "CARDPOCKET_REPLACE"; m_active_dialog->visible = true;
    const auto personal=m_dialog_kwargs.find("personal_cards");
    (*m__replcardholder)->reconstruct_replcardholder(selection, *::cw::g_cwpy->m_selectedheader,
        personal!=m_dialog_kwargs.end() && personal->second.as_bool());
}

std::tuple<std::optional<std::shared_ptr<::cw::sprite::card::CWPyCard>>, std::optional<std::tuple<::cw::lib::String, std::shared_ptr<::cw::sprite::card::CWPyCard>, std::tuple<::cw::lib::Integer, ::cw::lib::Integer>, ::cw::lib::Real>>> Frame::_get_cardcontrolparams(::cw::lib::String arg_callname)
{
    const auto runtime = ::cw::g_cwpy;
    if (!runtime) return {std::nullopt, std::nullopt};
    using Result = std::tuple<std::optional<std::shared_ptr<::cw::sprite::card::CWPyCard>>,
        std::optional<std::tuple<String, std::shared_ptr<::cw::sprite::card::CWPyCard>,
                                 std::tuple<Integer, Integer>, Real>>>;
    const auto result = std::make_shared<Result>();
    const auto kwargs = m_dialog_kwargs;
    query_game_state(runtime, [runtime, result, kwargs, arg_callname](const auto&, const auto&) {
        if (::cw::g_cwpy != runtime) return Value{};
        if (!runtime->m_pre_dialogs.empty()) {
            auto pre = runtime->m_pre_dialogs.back(); runtime->m_pre_dialogs.pop_back();
            *result = {std::get<1>(pre), pre}; return Value{};
        }
        if (arg_callname == "BACKPACK" || arg_callname == "STOREHOUSE") return Value{};
        if (const auto it = kwargs.find("selection"); it != kwargs.end()) {
            if (const auto card = as_object<::cw::sprite::card::CWPyCard>(it->second)) {
                std::get<0>(*result) = card; return Value{};
            }
        }
        if (runtime->m_selection && *runtime->m_selection)
            std::get<0>(*result) = std::dynamic_pointer_cast<::cw::sprite::card::CWPyCard>(*runtime->m_selection);
        return Value{};
    });
    return *result;
}

void Frame::OnINFOVIEW(std::shared_ptr<::cw::lib::Window> arg_event)
{
    ::cw::lib::RuntimeAccess runtime_access;
    (void)arg_event;
    if (::cw::g_cwpy) {
        ::cw::g_cwpy->exec_func([](const ::cw::lib::Arguments&, const ::cw::lib::KeywordArguments&) {
            if (!::cw::g_cwpy) return Value{};
            const auto sdata = as_object<::cw::data::SystemData>(::cw::g_cwpy->m_sdata);
            if (sdata && sdata->m_notice_infoview) {
                sdata->m_notice_infoview = false;
                if (::cw::g_cwpy->m_statusbar)
                    ::cw::g_cwpy->m_statusbar->change(::cw::g_cwpy->m_statusbar->m_showbuttons, false);
            }
            return Value{};
        });
    }
    if (!m__infoview) {
        auto view = std::make_shared<::cw::dialog::cardcontrol::InfoView>();
        view->initialize(as_object<::cw::lib::Window>(m_panel)); m__infoview = view;
    }
    if (!*m__infoview) {
        kill_dlg(std::nullopt, false, true);
        return;
    }
    m_active_dialog_model = *m__infoview;
    m_active_dialog = std::make_shared<::cw::lib::Dialog>();
    m_active_dialog->title = "INFOVIEW"; m_active_dialog->visible = true;
    (*m__infoview)->reconstruct_infoview();
}

void Frame::OnCHARAINFO(std::shared_ptr<::cw::lib::Window> arg_event)
{
    ::cw::lib::RuntimeAccess runtime_access;
    (void)arg_event;
#if defined(CARDWIRTH_LOG_ENABLED)
    ::cardwirth::debug::log("OnCHARAINFO begin");
#endif
    if (!::cw::g_cwpy) {
        kill_dlg(std::nullopt, false, true);
        return;
    }
    std::shared_ptr<::cw::sprite::card::CWPyCard> captured_selection;
    if (const auto it = m_dialog_kwargs.find("selection"); it != m_dialog_kwargs.end()) {
        if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&it->second.storage());
            object && *object)
            captured_selection = std::dynamic_pointer_cast<::cw::sprite::card::CWPyCard>(*object);
    }
    auto dialog = std::make_shared<::cw::dialog::charainfo::ActiveCharaInfo>();
    // このステータスバーの入口が Frame のモーダル枠を所有する。 入れ子のキャラクタ
    // ー情報ダイアログは、代わりに共通モーダル Presentation ホストを使う。
    dialog->m_frame_dialog_owned = true;
    m_dialog_history.push_back("CHARAINFO");
    m_active_dialog_model = dialog;
    m_active_dialog = std::make_shared<::cw::lib::Dialog>();
    m_active_dialog->title = "CHARAINFO";
    dialog->initialize(shared_from_this(), std::move(captured_selection));
    if (const auto backend = ::cw::dialog::charainfo::get_charainfo_ui_backend()) backend->layout(*dialog);
    if (const auto* width = std::get_if<::cw::lib::Integer>(&dialog->m_width.storage()))
        m_active_dialog->bounds.width = std::max<::cw::lib::Integer>(302, *width);
    else
        m_active_dialog->bounds.width = 302;
    m_active_dialog->bounds.height = 355;
    move_dlg(m_active_dialog, {0, 0});
    m_active_dialog->visible = true;
    if (const auto backend = ::cw::dialog::charainfo::get_charainfo_ui_backend()) backend->present(*dialog);
#if defined(CARDWIRTH_LOG_ENABLED)
    ::cardwirth::debug::log("OnCHARAINFO end");
#endif
}

void Frame::OnRETURNTITLE(std::shared_ptr<::cw::lib::Window> arg_event)
{
    ::cw::lib::RuntimeAccess runtime_access;
    (void)arg_event;
    bool ok = true;
    if (m__setting && m__setting->m_caution_beforesaving && ydata_is_changed()) {
        const auto text = frame_message("confirm_go_title", "タイトル画面に戻ります。よろしいですか？");
        m_active_dialog = std::make_shared<::cw::lib::Dialog>();
        m_active_dialog->title = "RETURNTITLE";
        m_active_dialog->visible = true;
        const auto accepted = show_defined_yesno_dialog(
            as_object<::cw::lib::Window>(m_panel), text);
        ok = accepted.value_or(false);
    }
    if (ok && ::cw::g_cwpy) {
        ::cw::g_cwpy->exec_func([](const ::cw::lib::Arguments&, const ::cw::lib::KeywordArguments&) {
            if (::cw::g_cwpy) ::cw::g_cwpy->set_title(false, {"", Value{}});
            return Value{};
        });
        m_dialog_history.push_back("RETURNTITLE");
    }
    kill_dlg(std::nullopt, false, true);
}

void Frame::OnSAVE(std::shared_ptr<::cw::lib::Window> arg_event)
{
    ::cw::lib::RuntimeAccess runtime_access;
    (void)arg_event;
    if (!::cw::g_cwpy) {
        kill_dlg(std::nullopt, false, true);
        return;
    }
    auto& cwpy = *::cw::g_cwpy;
    const bool playing_scenario = cwpy.is_playingscenario();
    const auto confirm_mode = cwpy.m_setting
        ? cwpy.m_setting->m_confirm_beforesaving
        : ::cw::setting::g_CONFIRM_BEFORESAVING_YES;
    const bool confirm =
        (confirm_mode == ::cw::setting::g_CONFIRM_BEFORESAVING_BASE && !playing_scenario) ||
        (confirm_mode != ::cw::setting::g_CONFIRM_BEFORESAVING_NO &&
         confirm_mode != ::cw::setting::g_CONFIRM_BEFORESAVING_BASE);

    bool save = true;
    std::optional<int> novel_slot;
    bool overwrite_slot = false;
    bool incomplete_slot = false;
    if (cwpy.is_novel_mode()) {
        const auto report_lookup_failure = [this](const std::exception& ex) {
            const auto text = String("Novelセーブ先を確認できません。\n") + ex.what();
            kill_dlg(std::nullopt, false, true);
            const auto game = ::cw::g_cwpy;
            if (game) game->exec_func([game, text](const ::cw::lib::Arguments&,
                                                  const ::cw::lib::KeywordArguments&) {
                game->play_sound("error", false, 100, 1, 0, 0, false);
                game->call_dlg("ERROR", {{"text", Value(text)}});
                return Value{};
            });
        };
        m_active_dialog = std::make_shared<::cw::lib::Dialog>();
        m_active_dialog->title = "SAVE";
        m_active_dialog->visible = true;
        const auto base = std::filesystem::u8path(cwpy.m_novel_base_yadodir);
        const auto max_saves = ::cw::novel::config().max_saves;
        std::vector<String> labels;
        try {
            labels = ::cw::novel::slot_labels(base, max_saves);
        } catch (const std::exception& ex) {
            report_lookup_failure(ex);
            return;
        }
        auto slots = std::make_shared<::cw::dialog::edit::SingleChoiceDialog>(
            "セーブ先選択", "保存するスロットを選択してください。",
            labels,
            std::clamp(cwpy.m_novel_active_slot - 1, 0, max_saves - 1));
        save = ::cw::dialog::show_common_modal_presentation(slots);
        const auto selected = save ? slots->result() : std::optional<int>{};
        save = selected && *selected >= 0 && *selected < max_saves;
        if (save) {
            novel_slot = *selected + 1;
            try {
                const auto state = ::cw::novel::slot_state(base, *novel_slot);
                overwrite_slot = state != ::cw::novel::SlotState::Missing;
                incomplete_slot = state == ::cw::novel::SlotState::Incomplete;
            } catch (const std::exception& ex) {
                report_lookup_failure(ex);
                return;
            }
            if (overwrite_slot)
                save = show_defined_yesno_dialog(as_object<::cw::lib::Window>(m_panel),
                    incomplete_slot
                        ? "セーブ " + std::to_string(*novel_slot) +
                            " の不完全なデータを置き換えますか？"
                        : "セーブ " + std::to_string(*novel_slot) +
                            " に上書きしますか？").value_or(false);
        }
    } else if (confirm) {
        const auto text = cwpy.m_setting && cwpy.m_setting->m_msgs
            ? cwpy.m_setting->m_msgs->get("confirm_save", "セーブしますか？")
            : String("セーブしますか？");
        m_active_dialog = std::make_shared<::cw::lib::Dialog>();
        m_active_dialog->title = "SAVE";
        m_active_dialog->visible = true;
        const auto accepted = show_defined_yesno_dialog(
            as_object<::cw::lib::Window>(m_panel), text);
        save = accepted.value_or(false);
    }

    m_dialog_history.push_back("SAVE");
    if (!save) {
        kill_dlg(std::nullopt, false, true);
        return;
    }

    // Python と同様、先に SAVE を閉じてから、ゲームスレッドでファイ
    // ル操作を行う。 保存と完了メッセージの順序をここで管理し、ゲームループが
    // 停止中に UI コールバックが YadoData に触れないようにする。
    kill_dlg(std::nullopt, true, true);
    const auto game = ::cw::g_cwpy;
    game->exec_func([this, game, novel_slot, overwrite_slot](
                        const ::cw::lib::Arguments&,
                        const ::cw::lib::KeywordArguments&) {
        if (!game->m_ydata || !*game->m_ydata) {
            _saved();
            return Value{};
        }
        if (novel_slot) {
            String error;
            if (!game->save_novel_slot(*novel_slot, overwrite_slot, error)) {
                game->play_sound("error", false, 100, 1, 0, 0, false);
                game->call_dlg("ERROR", {{"text", Value(error)}});
                return Value{};
            }
        } else {
            (*game->m_ydata)->save();
        }
        game->play_sound("signal", false, 100, 1, 0, 0, false);
        if (game->m_setting && game->m_setting->m_show_savedmessage) {
            const auto text = game->m_setting->m_msgs
                ? game->m_setting->m_msgs->get("saved", "セーブしました。")
                : String("セーブしました。");
            // Python と同様、保存が終わっても、ユーザーが完了メッセー
            // ジを確認するまでこの操作はモーダルのままとする。 共通の call_modaldlg
            // でゲーム・UI の終了確認を処理し、待たない call_dlg
            // や UI の同期待機へ置き換えない。
            game->call_modaldlg("SAVED_MESSAGE", {{"text", Value(text)}});
        } else {
            _saved();
        }
        return Value{};
    });
}

void Frame::OnSAVED_MESSAGE(std::shared_ptr<::cw::lib::Window> arg_event)
{
    ::cw::lib::RuntimeAccess runtime_access;
    OnMESSAGE(arg_event);
    if (::cw::g_cwpy) {
        const auto game = ::cw::g_cwpy;
        game->exec_func([this, game](
                            const ::cw::lib::Arguments&,
                            const ::cw::lib::KeywordArguments&) {
            (void)game;
            _saved();
            return Value{};
        });
    }
}

void Frame::_saved()
{
    if (!::cw::g_cwpy || ::cw::g_cwpy->is_playingscenario()) return;
    if (!::cw::g_cwpy->m_ydata || !*::cw::g_cwpy->m_ydata) return;
    const auto& ydata = *::cw::g_cwpy->m_ydata;
    const Integer areaid = ydata->m_party ? 2
        : (!ydata->is_empty() || ydata->is_changed()) ? 1 : 3;
    if (areaid == ::cw::g_cwpy->m_areaid) return;
    // Py の _saved と同様、change_area より前にパーティー読み込み状態
    // を解除する。 この遷移の監視側にとって、set_pcards まで待つのは遅すぎる。
    if (ydata->m_party && *ydata->m_party)
        (*ydata->m_party)->m__loading = false;
    // Python と同様、保存後に change_area() を呼び、
    // 宿・パーティー画面を実際に再構築する。 m_areaid の代入だ
    // けでは、新しい論理エリア番号の下に古いカードとコントロールが残る。
    ::cw::g_cwpy->change_area(
        areaid, true, false, {"Default", Value("Default")}, false, false,
        false, true, std::nullopt, false, false, false, false, false);
}

void Frame::OnLOAD(std::shared_ptr<::cw::lib::Window> arg_event)
{
    ::cw::lib::RuntimeAccess runtime_access;
    (void)arg_event;
    if (!::cw::g_cwpy) {
        kill_dlg(std::nullopt, false, true);
        return;
    }
    const auto text = m__setting && m__setting->m_msgs
        ? m__setting->m_msgs->get("confirm_load", "宿の状態を読み直しますか？")
        : String("宿の状態を読み直しますか？");
    m_active_dialog = std::make_shared<::cw::lib::Dialog>();
    m_active_dialog->title = "LOAD";
    m_active_dialog->visible = true;
    const auto accepted = show_defined_yesno_dialog(
        as_object<::cw::lib::Window>(m_panel), text);
    if (accepted.value_or(false))
        ::cw::g_cwpy->exec_func([](const ::cw::lib::Arguments&,
                                   const ::cw::lib::KeywordArguments&) {
            if (::cw::g_cwpy) ::cw::g_cwpy->reload_yado();
            return Value{};
        });
    kill_dlg(std::nullopt, false, true);
}

void Frame::OnRUNAWAY(std::shared_ptr<::cw::lib::Window> arg_event)
{
    ::cw::lib::RuntimeAccess runtime_access;
    (void)arg_event;
    if (::cw::g_cwpy) {
        const auto runtime = ::cw::g_cwpy;
        const auto text = m__setting && m__setting->m_msgs
            ? m__setting->m_msgs->get("confirm_runaway", "戦闘から逃走しますか？")
            : String("戦闘から逃走しますか？");
        m_active_dialog = std::make_shared<::cw::lib::Dialog>();
        m_active_dialog->title = "RUNAWAY";
        m_active_dialog->visible = true;
        const auto accepted = show_defined_yesno_dialog(
            as_object<::cw::lib::Window>(m_panel), text);
        if (accepted.value_or(false) && runtime->m_battle && *runtime->m_battle) {
            // Python と同様、戦闘の選択は ShowModal の前ではなく後に行う。
            const auto active_battle = *runtime->m_battle;
            // Python では、予約した逃走コールバックが動く前に ShowModal
            // がモーダルループを抜けている。 C++ の分離した UI
            // ・ゲームキューでも順序を明示し、逃走イベントの最初のメ
            // ッセージが ModalPaused 中に動いて、 入力
            // を受け取れずダイアログ終了も完了できない状態を防ぐ。
            runtime->defer_until_modal_closed(
                m_active_dialog_session,
                [runtime, active_battle](const ::cw::lib::Arguments&,
                                         const ::cw::lib::KeywordArguments&) {
                if (runtime && active_battle && runtime->m_battle &&
                    *runtime->m_battle == active_battle)
                    active_battle->runaway();
                return Value{};
            });
        }
        kill_dlg(std::nullopt, false, true);
    }
}

void Frame::OnUSECARD(std::shared_ptr<::cw::lib::Window> arg_event)
{
    ::cw::lib::RuntimeAccess runtime_access;
    (void)arg_event;
    const auto runtime = ::cw::g_cwpy;
    if (!runtime) return;
    struct PreparedUse {
        std::shared_ptr<::cw::header::CardHeader> header;
        std::shared_ptr<::cw::character::Character> owner;
        std::shared_ptr<::cw::sprite::card::CWPyCard> owner_card;
        Value targets;
        Value scenario;
        bool confirm{};
        String text;
    };
    const auto prepared = std::make_shared<PreparedUse>();
    query_game_state(runtime, [runtime, prepared](const auto&, const auto&) {
        if (::cw::g_cwpy != runtime || !runtime->m_selectedheader ||
            !*runtime->m_selectedheader) return Value{};
        const auto header = *runtime->m_selectedheader;
        const auto owner_value = header->get_owner();
        const auto owner = owner_value ? as_object<::cw::character::Character>(*owner_value) : nullptr;
        const auto owner_card = std::dynamic_pointer_cast<::cw::sprite::card::CWPyCard>(owner);
        if (!owner || !owner_card) return Value{};
        const auto selected = runtime->m_selection && *runtime->m_selection
            ? std::dynamic_pointer_cast<::cw::sprite::card::CWPyCard>(*runtime->m_selection) : nullptr;
        std::vector<std::shared_ptr<::cw::sprite::card::CWPyCard>> targets;
        const bool party_allrange = header->m_allrange &&
            (header->m_target == "Party" || header->m_target == "Both") &&
            std::dynamic_pointer_cast<::cw::sprite::card::PlayerCard>(selected);
        if (party_allrange) {
            for (const auto& card : runtime->get_pcards(Value("unreversed"))) targets.push_back(card);
            runtime->clear_selection();
        } else if (header->m_target == "User") targets.push_back(owner_card);
        else if (header->m_target != "None" && selected) targets.push_back(selected);
        auto sequence = std::make_shared<::cw::data::VariantSequence>();
        for (const auto& card : targets) if (card)
            sequence->m_values.emplace_back(std::static_pointer_cast<::cw::lib::Object>(card));
        prepared->owner = owner; prepared->owner_card = owner_card;
        prepared->targets = Value(std::static_pointer_cast<::cw::lib::Object>(sequence));
        prepared->scenario = runtime->m_sdata;
        prepared->confirm = runtime->m_setting && runtime->m_setting->m_confirm_beforeusingcard;
        // Python と同じ、カーテン、プレビュー、確認の順序を維持する。 これらのコ
        // ンテナーを UI スレッドで変更すると、ネイティブモーダルの描画と競合する。
        runtime->clear_curtain(true);
        if (prepared->confirm) {
            const auto alpha = runtime->m_setting ? runtime->m_setting->get_inusecardalpha(owner_card) : 255;
            runtime->set_inusecardimg(owner_card, header, "normal", false, alpha, true);
            if (header->m_target == "None") runtime->clear_targetarrow();
            else runtime->set_targetarrow(targets);
            const auto format = runtime->m_setting && runtime->m_setting->m_msgs
                ? runtime->m_setting->m_msgs->get("confirm_use_card", "%sを使用しますか？")
                : String("%sを使用しますか？");
            prepared->text = ::cw::lib::python_percent_string(format, header->m_name);
        }
        prepared->header = header;
        return Value{};
    });
    if (!prepared->header) { kill_dlg(std::nullopt, true, true); return; }
    bool use = true;
    m_dialog_history.push_back("USECARD");
    if (prepared->confirm) {
        m_active_dialog = std::make_shared<::cw::lib::Dialog>();
        m_active_dialog->title = "USECARD"; m_active_dialog->visible = true;
        use = show_defined_yesno_dialog(as_object<::cw::lib::Window>(m_panel), prepared->text).value_or(false);
    }
    const auto completion = [runtime, prepared, use](const auto&, const auto&) {
        if (::cw::g_cwpy != runtime || runtime->m_sdata.storage() != prepared->scenario.storage()) return Value{};
        if (use) {
            if (runtime->m_selectedheader && *runtime->m_selectedheader == prepared->header)
                (void)prepared->owner->use_card(prepared->targets, prepared->header);
        } else {
            runtime->clear_inusecardimg(prepared->owner_card);
            runtime->clear_targetarrow();
            runtime->clear_specialarea(true, false);
        }
        return Value{};
    };
    // ネイティブの終了処理は UI・ゲーム・ポインターの段階に分かれる。
    // 次のカード使用で CARDPOCKET が再度開く場合があるため
    // 、ゲーム側の終了の直後に予約するだけでなく、全段階の完了を待つ。
    runtime->defer_until_modal_closed(m_active_dialog_session, completion);
    kill_dlg(std::nullopt, true, true);
#if defined(CARDWIRTH_LOG_ENABLED)
    ::cardwirth::debug::log("OnUSECARD end: use=" + std::string(use ? "1" : "0"));
#endif
}

void Frame::OnDATACOMP(std::shared_ptr<::cw::lib::Window> arg_event)
{
    ::cw::lib::RuntimeAccess runtime_access;
    (void)arg_event;
    std::shared_ptr<::cw::sprite::card::FriendCard> ccard;
    if (const auto it = m_dialog_kwargs.find("ccard"); it != m_dialog_kwargs.end()) {
        if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&it->second.storage());
            object && *object) {
            ccard = std::dynamic_pointer_cast<::cw::sprite::card::FriendCard>(*object);
        }
    }
    auto dialog = std::make_shared<::cw::dialog::create::AdventurerDataComp>();
    // 表示モデルを公開する前に、必須データの既定値・選択肢を解決する。
    dialog->initialize(as_object<::cw::lib::Window>(m_panel), ccard);
    m_active_dialog = std::make_shared<::cw::lib::Dialog>();
    m_active_dialog->title = "DATACOMP";
    m_active_dialog->visible = true;
    m_active_dialog_model = dialog;
    m_dialog_history.push_back("DATACOMP");
    move_dlg(m_active_dialog, {0, 0});
}

void Frame::OnPARTYEDIT(std::shared_ptr<::cw::lib::Window> arg_event)
{
    ::cw::lib::RuntimeAccess runtime_access;
    (void)arg_event;
#if defined(CARDWIRTH_LOG_ENABLED)
    ::cardwirth::debug::log("OnPARTYEDIT begin");
#endif
    if (!::cw::g_cwpy) {
        kill_dlg(std::nullopt, false, true);
        return;
    }
    auto dialog = std::make_shared<::cw::dialog::edit::PartyEditor>();
    // 共通 PartyEditor は Frame の UI キューで完了する。 この
    // モーダルセッションだけを解放し、別のダイアログやデバッガーを移動・終了させない。
    const auto dialog_session = m_active_dialog_session;
    dialog->m_close_callback = [this, dialog_session]() {
        if (m_active_dialog_model)
            kill_dlg(std::nullopt, false, true, dialog_session);
    };
    m_dialog_history.push_back("PARTYEDIT");
    m_active_dialog_model = dialog;
    m_active_dialog = std::make_shared<::cw::lib::Dialog>();
    m_active_dialog->title = "PARTYEDIT"; m_active_dialog->visible = true;
    dialog->initialize(as_object<::cw::lib::Window>(m_panel), std::nullopt);
#if defined(CARDWIRTH_LOG_ENABLED)
    ::cardwirth::debug::log("OnPARTYEDIT end edit_open=" +
        std::string(::cw::dialog::edit::is_edit_ui_open() ? "true" : "false"));
#endif
}

void Frame::OnBATTLECOMMAND(std::shared_ptr<::cw::lib::Window> arg_event)
{
    ::cw::lib::RuntimeAccess runtime_access;
    (void)arg_event;
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_battle || !*::cw::g_cwpy->m_battle) {
        kill_dlg(std::nullopt, false, true);
        return;
    }
    auto dialog = std::make_shared<::cw::dialog::etc::BattleCommand>();
    m_active_dialog = std::make_shared<::cw::lib::Dialog>();
    m_active_dialog->title = "BATTLECOMMAND";
    m_active_dialog->visible = true;
    m_active_dialog_model = dialog;
    m_dialog_history.push_back("BATTLECOMMAND");
    dialog->initialize(as_object<::cw::lib::Window>(m_panel));
    move_dlg(m_active_dialog, {0, 0});
}

void Frame::OnINSTRUCTIONS(std::shared_ptr<::cw::lib::Window> arg_event)
{
    ::cw::lib::RuntimeAccess runtime_access;
    (void)arg_event;
    std::shared_ptr<::cw::data::SystemData> sdata;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata &&
        (*::cw::g_cwpy->m_ydata)->m_losted_sdata && *(*::cw::g_cwpy->m_ydata)->m_losted_sdata)
        sdata = *(*::cw::g_cwpy->m_ydata)->m_losted_sdata;
    else if (::cw::g_cwpy)
        sdata = as_object<::cw::data::SystemData>(::cw::g_cwpy->m_sdata);
    std::vector<std::shared_ptr<::cw::dialog::text::ReadmeData>> lists;
    if (sdata) {
        for (const auto& path : sdata->m_instructions) {
            auto data = std::make_shared<::cw::dialog::text::ReadmeData>();
            data->initialize(::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(path).filename()),
                             ::cw::util::read_file_bytes(path));
            lists.push_back(std::move(data));
        }
    }
    if (::cw::g_cwpy) ::cw::g_cwpy->play_sound("click", false, 100, 1, 0, 0, false);
    const auto title = frame_message("description", "解説");
    m_active_dialog = std::make_shared<::cw::lib::Dialog>();
    m_active_dialog->title = "INSTRUCTIONS";
    m_active_dialog->visible = true;
    m_dialog_history.push_back("INSTRUCTIONS");
    auto dialog = std::make_shared<::cw::dialog::text::Readme>();
    dialog->initialize(as_object<::cw::lib::Window>(m_panel), title, std::move(lists));
    if (m_native_adapter && m_native_adapter->is_hosted()) {
        m_active_dialog_model = dialog;
        return;
    }
    (void)::cw::dialog::show_common_modal_presentation(dialog);
    kill_dlg(std::nullopt, false, true);
}

void Frame::OnF9(std::shared_ptr<::cw::lib::Window> arg_event)
{
    ::cw::lib::RuntimeAccess runtime_access;
    (void)arg_event;
    if (::cw::g_cwpy && ::cw::g_cwpy->is_novel_mode()) {
        kill_dlg(std::nullopt, false, true);
        return;
    }
    const auto text = frame_message("f9_message", "F9(緊急避難)を実行しますか？");
    m_active_dialog = std::make_shared<::cw::lib::Dialog>();
    m_active_dialog->title = "F9";
    m_active_dialog->visible = true;
    const auto accepted = show_defined_yesno_dialog(
        as_object<::cw::lib::Window>(m_panel), text);
    if (accepted.value_or(false) && ::cw::g_cwpy) {
        // cw/frame.py::OnF9 と同じ緊急中断の順序を維持す
        // る。メッセージは同期モーダルループなので、 CWPy::f9()
        // を予約するだけではキューが処理されるまで MessageWindow
        // が残る。 Python と同様、先に実行中シナリオへ印を付けて
        // メッセージ結果に EffectBreakError を入れ、show_message()
        // が直ちに終了して中断を伝播できるようにする。 C++ の
        // call_modaldlg はゲームの表示ループを続け
        // るため、上の共通モデル操作でこの小さな中断通知を直列化し
        // 、 後のシナリオの後処理だけをゲームの所有者へ予約する。
        if (const auto message = ::cw::g_cwpy->get_messagewindow(); message && *message) {
            auto sdata = as_object<::cw::data::ScenarioData>(::cw::g_cwpy->m_sdata);
            if (::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata &&
                (*::cw::g_cwpy->m_ydata)->m_losted_sdata &&
                *(*::cw::g_cwpy->m_ydata)->m_losted_sdata) {
                sdata = *(*::cw::g_cwpy->m_ydata)->m_losted_sdata;
                ::cw::g_cwpy->m_sdata = std::static_pointer_cast<::cw::lib::Object>(sdata);
                (*::cw::g_cwpy->m_ydata)->m_losted_sdata.reset();
            }
            if (sdata) {
                sdata->m_in_f9 = true;
                if (!::cw::g_cwpy->m_pre_dialogs.empty())
                    ::cw::g_cwpy->m_pre_dialogs.pop_back();
                auto error = std::make_shared<::cw::event::EffectBreakError>();
                (*message)->m_result = std::static_pointer_cast<::cw::lib::Object>(error);
                (*message)->m_result_typed.reset();
                // Python と同様、予約したシナリオ中断より前
                // に、F9 確認をモーダル階層から取り除く。 ネイ
                // ティブの終了後処理もゲームスレッドのコールバック
                // なので先に予約し、 見えなくなった F9 ダイア
                // ログが入力を遮ったまま宿を表示しないようにする。
                kill_dlg(std::nullopt, false, true);
                ::cw::g_cwpy->exec_func(
                    [sdata](const ::cw::lib::Arguments&, const ::cw::lib::KeywordArguments&) {
                        sdata->f9();
                        return Value{};
                    });
                return;
            }
        }
        // メッセージ以外の F9 は、既存の CWPy の経路を維持す
        // る。 イベントループの処理は同期 MessageWindow
        // の中断とは別である。 宿画面全体を再構築し得る F9 を
        // 予約して開始する前に、ネイティブのモーダル終了を確定する。
        kill_dlg(std::nullopt, false, true);
        ::cw::g_cwpy->exec_func([](const ::cw::lib::Arguments&, const ::cw::lib::KeywordArguments&) {
            if (::cw::g_cwpy) ::cw::g_cwpy->f9(false, false);
            return Value{};
        });
        return;
    }
    kill_dlg(std::nullopt, false, true);
}

void Frame::OnERROR(std::shared_ptr<::cw::lib::Window> arg_event)
{
    ::cw::lib::RuntimeAccess runtime_access;
    const auto error_session = m_active_dialog_session;
    auto text = arg_event ? arg_event->title : ::cw::lib::String{};
    if (text.empty()) {
        if (const auto it = m_dialog_kwargs.find("text"); it != m_dialog_kwargs.end())
            text = value_string(it->second);
    }
    bool shutdown = arg_event && arg_event->id != 0;
    if (const auto it = m_dialog_kwargs.find("shutdown"); it != m_dialog_kwargs.end())
        shutdown = value_truthy(it->second);
    _on_error(std::move(text), message_parent(m_dialog_kwargs, m_panel), shutdown);
    if (!shutdown && error_session != 0)
        kill_dlg(std::nullopt, false, true, error_session);
}

void Frame::_on_error(::cw::lib::String arg_text, std::optional<std::shared_ptr<::cw::lib::Window>> arg_parent, bool arg_shutdown)
{
    const auto parent = arg_parent && *arg_parent ? *arg_parent
        : as_object<::cw::lib::Window>(m_panel);
    m_dialog_history.push_back(arg_text);
    const auto error_title = ::cw::g_cwpy && ::cw::g_cwpy->m_setting &&
            ::cw::g_cwpy->m_setting->m_msgs
        ? ::cw::g_cwpy->m_setting->m_msgs->get("error_message", "エラー")
        : String("エラー");
    if (::cw::g_cwpy)
        ::cw::g_cwpy->play_sound("error", false, 100, 1, 0, 0, false);
    std::optional<bool> shown;
    if (m_native_adapter && !std::dynamic_pointer_cast<::cw::lib::Dialog>(parent))
        shown = m_native_adapter->modal_message(*this, error_title, arg_text, false, true);
    if (!shown.has_value()) {
        // 単体 SDL アダプターには、意図的にプラットフォーム専用のメッ
        // セージボックスを設けない。 他ホストと同じ共通表示を使い、Linux/macOS
        // で Engine のエラーを黙って捨てない。
        auto dialog = std::make_shared<::cw::dialog::message::Message>();
        dialog->initialize(parent, error_title, arg_text, 2, std::nullopt);
        (void)::cw::dialog::show_common_modal_presentation(shared_from_this(), dialog, parent);
    }
    after_message(parent);
    if (arg_shutdown) {
        ::cw::g_quit_app = true;
        OnDestroy(nullptr);
    }
}

void Frame::OnNOTICE(std::shared_ptr<::cw::lib::Window> arg_event)
{
    ::cw::lib::RuntimeAccess runtime_access;
    if (::cw::g_cwpy) ::cw::g_cwpy->play_sound("error", false, 100, 1, 0, 0, false);
    if (!::cw::g_cwpy || !m__setting || !m__setting->m_noticeimpossibleaction) {
        kill_dlg(std::nullopt, false, true);
        return;
    }
    auto text = arg_event ? arg_event->title : String{};
    if (text.empty()) {
        if (const auto it = m_dialog_kwargs.find("text"); it != m_dialog_kwargs.end())
            text = value_string(it->second);
    }
    m_active_dialog = std::make_shared<::cw::lib::Dialog>();
    m_active_dialog->title = "NOTICE";
    m_active_dialog->visible = true;
    const auto parent = message_parent(m_dialog_kwargs, m_panel);
    if (show_defined_message_dialog(parent, text, 2)) after_message(parent);
    kill_dlg(std::nullopt, false, true);
}

void Frame::OnMESSAGE(std::shared_ptr<::cw::lib::Window> arg_event)
{
    ::cw::lib::RuntimeAccess runtime_access;
    const auto text = arg_event ? arg_event->title : ::cw::lib::String{};
    m_active_dialog = std::make_shared<::cw::lib::Dialog>();
    m_active_dialog->title = "MESSAGE"; m_active_dialog->visible = true;
    const auto parent = message_parent(m_dialog_kwargs, m_panel);
    if (show_defined_message_dialog(parent, text, 2)) after_message(parent);
    // kill_dlg() が処理するまでアクティブモデルを保持
    // する。 ここで消すと終了が重複と判断され、モーダル数が減
    // らず、SAVE 後もメインウィンドウが無効のままになる。
    if (::cw::g_cwpy && ::cw::g_cwpy->is_showingdlg())
        kill_dlg(std::nullopt, false, true);
}

void Frame::OnYESNO(std::shared_ptr<::cw::lib::Window> arg_event)
{
    ::cw::lib::RuntimeAccess runtime_access;
    const auto text = arg_event ? arg_event->title : ::cw::lib::String{};
    m_active_dialog = std::make_shared<::cw::lib::Dialog>();
    m_active_dialog->title = "YESNO"; m_active_dialog->visible = true;
    const auto parent = message_parent(m_dialog_kwargs, m_panel);
    const auto accepted = show_defined_yesno_dialog(
        parent,
        text.empty() ? "実行しますか？" : text);
    if (::cw::g_cwpy)
        ::cw::g_cwpy->m__yesnoresult = Value(accepted.value_or(false) ? 1 : 0);
    if (accepted) after_message(parent);
    kill_dlg(std::nullopt, false, true);
}

void Frame::move_dlg(std::shared_ptr<::cw::lib::Dialog> arg_dlg, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_point)
{
    if (!arg_dlg) return;
    auto panel = as_object<::cw::lib::Window>(m_panel);
    const auto pw = panel ? panel->bounds.width : std::get<0>(::cw::g_SIZE_GAME);
    const auto ph = panel ? panel->bounds.height : std::get<1>(::cw::g_SIZE_GAME);
    const auto x = panel ? panel->bounds.x + (pw - arg_dlg->bounds.width) / 2 : 0;
    const auto y = panel ? panel->bounds.y + (ph - arg_dlg->bounds.height) / 2 : 0;
    // Python と同様、要求されたずれに cwpy.scr_scale
    // を掛けてから、中央配置した位置へ加える。 この UI 境界で
    // はずれを整数で扱い、有限でない値や範囲外の倍率変換は拒否する。
    Real scale = 1.0;
    if (::cw::g_cwpy && std::isfinite(::cw::g_cwpy->m_scr_scale) && ::cw::g_cwpy->m_scr_scale > 0.0) {
        scale = ::cw::g_cwpy->m_scr_scale;
    }
    arg_dlg->bounds.x = x + scaled_offset(std::get<0>(arg_point), scale);
    arg_dlg->bounds.y = y + scaled_offset(std::get<1>(arg_point), scale);
    if (m_native_adapter) m_native_adapter->adjust_position(*this, arg_dlg);
}

void Frame::set_background_enabled(bool arg_enabled)
{
#ifdef _WIN32
    // Python は一つのイベントスレッドで wx ウィンドウとモーダル状態を管理する
    // 。 C++ の Win32 ホストでも明示的に同じ規則を守る。EnableWindow
    // は所有者へ同期メッセージを送る場合があるため、ゲームスレッドでは実行しない。
    assert_ui_thread();
    const HWND handle = reinterpret_cast<HWND>(m_native_window_handle);
    if (!handle || !IsWindow(handle)) return;
    if ((IsWindowEnabled(handle) != FALSE) != arg_enabled)
        EnableWindow(handle, arg_enabled ? TRUE : FALSE);
    if (arg_enabled && !::cardwirth::debug::nonactive_automation_enabled()) {
        // 所有関係のある SDL ダイアログを隠しても、Win32 の所有者が必ず再アクティブ化
        // するとは限らない。 次のパーティーフレームを公開する前に Engine を復元する。
        BringWindowToTop(handle);
        SetActiveWindow(handle);
        SetForegroundWindow(handle);
        SetFocus(handle);
    }
#else
    (void)arg_enabled;
#endif
}

void Frame::kill_dlg(std::optional<std::shared_ptr<::cw::lib::Dialog>> arg_dlg,
                     bool arg_lockmenucard, bool arg_redraw,
                     std::uint64_t arg_session)
{
    ::cw::lib::RuntimeAccess runtime_access;
    assert_ui_thread();
#if defined(CARDWIRTH_LOG_ENABLED)
    const auto title = m_active_dialog ? m_active_dialog->title : ::cw::lib::String{};
    ::cardwirth::debug::log(String("kill_dlg") + (title.empty() ? "" : ": " + title));
#endif
    const auto modal_session = arg_session != 0 ? arg_session
                                                : m_active_dialog_session;
    const auto runtime = ::cw::g_cwpy;
    if (arg_session != 0 && m_active_dialog_session != 0 &&
        modal_session != m_active_dialog_session) {
        const bool covered = std::any_of(
            m_suspended_dialog_sessions.begin(), m_suspended_dialog_sessions.end(),
            [modal_session](const auto& item) {
                return item.second.session == modal_session;
            });
        if (covered) {
            m_deferred_dialog_closes.insert_or_assign(
                modal_session, DeferredDialogClose{arg_lockmenucard, arg_redraw});
        }
        // 要求されたトークンが最上位のモーダルでなければ、有効な
        // トークンでも表示中の子を維持する。 途中のセッション
        // を閉じることで、所有関係を順序外に消去・復元しない。
        return;
    }
    // ネイティブバックエンドは最後の終了コールバックより前に HWND・モデルを破棄する
    // 場合がある。 繰り返し安全に終了するための判定には、表示状態ではなくセッショントー
    // クンを使う。 Python では wx のモーダルオブジェクトがその識別を担う。
    if (modal_session == 0 || !runtime ||
        !runtime->begin_modal_close(modal_session))
        return;
    if (arg_dlg && *arg_dlg) (*arg_dlg)->visible = false;
    if (m_active_dialog) m_active_dialog->visible = false;
    if (const auto suspended = m_suspended_dialog_sessions.find(modal_session);
        suspended != m_suspended_dialog_sessions.end()) {
        // 売却・破棄の確認では、その確認画面だけを閉じる。 元の CardControl
        // 所有者を正確に復元し、予約済みの転送完了処理から更新できるようにする。
        m_active_dialog = suspended->second.dialog;
        m_active_dialog_model = suspended->second.model;
        m_active_dialog_session = suspended->second.session;
        if (m_active_dialog) m_active_dialog->visible = true;
#if defined(CARDWIRTH_LOG_ENABLED)
        ::cardwirth::debug::log(
            "modal child restored parent: child=" + std::to_string(modal_session) +
            " parent=" + std::to_string(m_active_dialog_session));
#endif
        m_suspended_dialog_sessions.erase(suspended);
    } else {
        m_active_dialog.reset();
        m_active_dialog_model.reset();
        m_active_dialog_session = 0;
    }
    {
        bool return_down = false;
#ifdef _WIN32
        return_down = (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0;
#endif
        // Python/wx はモーダルループ内で閉じるクリックを消費する。 C++
        // では明示的にキャンセルして物理的な解除を待ち、ダイアログの GetAsyncKeyState
        // の押下ビットをゲーム入力へコピーしない。
        begin_modal_pointer_barrier(modal_session);
        // Python の kill_dlg は wx の終了イベント後に cwpy を変更す
        // る。 C++ ではその処理を明示的に名前付けして予約し、UI スレッドは HWND
        // ・モデルの表示だけを破棄する。 実行状態を書き換えるのはゲームスレッドだけとする。
        runtime->exec_func([runtime, arg_lockmenucard, arg_redraw,
                            return_down, modal_session](const auto&, const auto&) {
            if (!runtime) return Value{};
            String close_failure;
            const auto close_step = [&close_failure](const char* name,
                                                     auto&& action) {
                try {
                    action();
                } catch (const std::exception& error) {
                    if (!close_failure.empty()) close_failure += "\n";
                    close_failure += String(name) + ": " + error.what();
                } catch (...) {
                    if (!close_failure.empty()) close_failure += "\n";
                    close_failure += String(name) + ": unknown exception";
                }
            };
            // 通常のゲームコマンドと異なり、モーダルの終了後処理
            // は安全側の確定処理である。 ある後処理が例外を出し
            // ても全段階を試し、UI への完了確認を必ず送る。
            close_step("cancel input", [&] { runtime->cancel_modal_input_sequence(); });
            close_step("commit modal stack", [&] {
                runtime->kill_showingdlg(modal_session);
            });
            close_step("unlock menu cards", [&] {
                if (!arg_lockmenucard) runtime->m_lock_menucards = false;
            });
            close_step("invalidate game", [&] {
                if (arg_redraw && !runtime->m_is_updating_skin) {
                    runtime->add_lazydraw_full();
                }
            });
            close_step("suppress return key-up", [&] {
                if (runtime->m_keyevent && return_down)
                    runtime->m_keyevent->m_nokeyupevent = true;
            });
            if (!close_failure.empty()) {
                runtime->record_modal_close_failure(modal_session, close_failure);
#if defined(CARDWIRTH_LOG_ENABLED)
                ::cardwirth::debug::log(
                    "modal close recovered session=" +
                    std::to_string(modal_session) + " error=" + close_failure);
#endif
            }
            // Python の ShowModal は選択・マウス・モーダル
            // 階層の後処理完了後に戻る。 C++ ではこの明示的な確定通知
            // を最後に送り、一致する終了世代だけが実行状態を再開し、UI
            // スレッドで Win32 の所有者を再有効化できるようにする。
            if (const auto frame = as_object<Frame>(runtime->m_frame)) {
                frame->exec_func_11(
                    [frame, runtime, modal_session](const auto&, const auto&) {
                        if (runtime && runtime->finish_modal_close(modal_session)) {
                            frame->m_modal_game_committed_epoch = modal_session;
                            frame->complete_modal_ui_restore(modal_session);
                            const auto exposed_session = frame->m_active_dialog_session;
                            if (const auto deferred = frame->m_deferred_dialog_closes.find(exposed_session);
                                deferred != frame->m_deferred_dialog_closes.end()) {
                                const auto request = deferred->second;
                                frame->m_deferred_dialog_closes.erase(deferred);
                                frame->kill_dlg(std::nullopt, request.lock_menu_card,
                                                request.redraw, exposed_session);
                            }
                        }
                        return Value{};
                    }, {}, {});
            } else {
                (void)runtime->finish_modal_close(modal_session);
                (void)runtime->finish_modal_pointer_release(modal_session);
                (void)runtime->finish_modal_ui_restore(modal_session);
            }
            return Value{};
        });
    }
}

void Frame::begin_dialog_session(std::uint64_t arg_session)
{
    assert_ui_thread();
    if (arg_session == 0) return;
    if (m_active_dialog_session != 0 && m_active_dialog_session != arg_session) {
        // wx と同様、確認画面を開いている間も外側のモーダルを保持する。
        // 共通 Presentation ホストのアクティブ枠は一つなので
        // 、その所有関係を明示的に保存し、子セッションの終了時に復元する。
        m_suspended_dialog_sessions.insert_or_assign(
            arg_session,
            SuspendedDialogState{m_active_dialog, m_active_dialog_model,
                                 m_active_dialog_session});
#if defined(CARDWIRTH_LOG_ENABLED)
        ::cardwirth::debug::log(
            "modal child suspended parent: child=" + std::to_string(arg_session) +
            " parent=" + std::to_string(m_active_dialog_session));
#endif
        if (m_active_dialog) m_active_dialog->visible = false;
    }
    m_active_dialog_session = arg_session;
}

void Frame::OnActivateApp(std::shared_ptr<::cw::lib::Window> arg_event)
{
    ::cw::lib::RuntimeAccess runtime_access;
    if (!::cw::g_cwpy || !as_object<::cw::data::SystemData>(::cw::g_cwpy->m_sdata) ||
        !::cw::g_cwpy->is_running()) return;
    const bool active = arg_event && arg_event->id != 0;
    const auto game = ::cw::g_cwpy;
    // Python の Frame._bind() と同様、EVT_ACTIVATE_APP では SystemData
    // の時間管理だけを停止・再開する。 ゲームスレッドを管理する m__running は停止しない。
    game->force_exec_func([game, active](const ::cw::lib::Arguments&,
                                  const ::cw::lib::KeywordArguments&) {
        if (!game) return Value{};
        if (const auto sdata = as_object<::cw::data::SystemData>(game->m_sdata)) {
            if (active) sdata->resume_timekeeper();
            else sdata->sleep_timekeeper();
        }
        if (active) {
            if (game->m_event) game->m_event->m_waittime = 0;
            game->add_lazydraw(nullptr);
        }
        return Value{};
    });
}

namespace {
std::tuple<Integer, Integer, Integer> screenshot_color(const Value& value,
    std::tuple<Integer, Integer, Integer> fallback)
{
    if (const auto* bytes = std::get_if<::cw::lib::Bytes>(&value.storage()); bytes && bytes->size() >= 3)
        return {(*bytes)[0], (*bytes)[1], (*bytes)[2]};
    if (const auto sequence = as_object<::cw::data::VariantSequence>(value);
        sequence && sequence->m_values.size() >= 3)
        return {value_int(sequence->m_values[0]).value_or(std::get<0>(fallback)),
                value_int(sequence->m_values[1]).value_or(std::get<1>(fallback)),
                value_int(sequence->m_values[2]).value_or(std::get<2>(fallback))};
    return fallback;
}

void prepare_frame_screenshot_path(String& filename)
{
    const auto directory = std::filesystem::u8path(filename).parent_path();
    const bool mounted = ::cardwirth::platform::is_mounted_user_path(directory);
    const auto status = mounted ? ::cardwirth::platform::stat_mounted_user_path(directory)
                               : std::optional<::cardwirth::platform::StorageStat>{};
    const bool exists = mounted ? status && status->is_directory() : std::filesystem::is_directory(directory);
    if (exists) filename = ::cw::util::dupcheck_plus(filename, false, true);
    else if (mounted) {
        if (!::cardwirth::platform::make_mounted_user_directory(directory, true))
            throw std::runtime_error("dialog screenshot: cannot create output directory");
    } else std::filesystem::create_directories(directory);
}

// 内容高の比較も含め、Py の中央配置・範囲計算を維持する。 最
// 終の wx 描画座標は、描画境界でだけ整数ピクセルへ変換する。
std::tuple<Integer, Integer, Integer> screenshot_dialog_position(
    const ScreenshotRect& frame, const ScreenshotRect& child,
    Integer width, Integer content_height, Integer y, Integer client_width,
    Integer client_height, Integer title_height)
{
    // ネイティブの記述情報の座標は浮動小数点になり得るが、wx の Rect は
    // 有限の整数である。 ピクセルへキャストする前に不正な記述情報を拒否する。
    if (!std::isfinite(frame.x) || !std::isfinite(frame.y) ||
        !std::isfinite(frame.width) || !std::isfinite(frame.height) ||
        !std::isfinite(child.x) || !std::isfinite(child.y) ||
        !std::isfinite(child.width) || !std::isfinite(child.height) ||
        frame.width <= 0 || frame.height <= 0)
        throw std::invalid_argument("dialog screenshot: invalid frame bounds");
    const auto centerx = (child.x - frame.x + child.width / 2.0) / frame.width;
    const auto centery = (child.y - frame.y + child.height / 2.0) / frame.height;
    const auto total_height = client_height + title_height;
    auto x = width * centerx - client_width / 2;
    auto top = content_height * centery - total_height / 2 + y;
    if (width <= x + client_width + 2) x = width - client_width - 2;
    if (content_height <= top + y + total_height + 2) top = content_height + y - total_height - 2;
    x = std::max<double>(2, x);
    top = std::max<double>(2 + y, top);
    return {static_cast<Integer>(x), static_cast<Integer>(top), title_height};
}

std::shared_ptr<::cw::lib::Surface> frame_screenshot_text(
    const String& title, const ::cw::lib::ui::FontStyle& font, Integer width, Integer padding, bool white)
{
    const auto color = white ? std::tuple<Integer,Integer,Integer>{255,255,255}
                             : std::tuple<Integer,Integer,Integer>{0,0,0};
    auto raster = ::cw::lib::ui::render_label(title, font.pixels, font.bold, color, font.face, font.italic);
    if (!raster || raster->size.width <= 0 || raster->size.height <= 0) return {};
    Integer w = raster->size.width / 2, h = raster->size.height / 2;
    if (0 < width && w + padding * 2 > width) w = width - padding * 2;
    if (w <= 0 || h <= 0) throw std::invalid_argument("dialog screenshot: invalid title dimensions");
    return ::cw::image::smoothscale(raster, {w,h}, true, false);
}
}

bool Frame::can_screenshot()
{
    ::cw::lib::RuntimeAccess runtime_access;
    if (!::cw::g_cwpy || !::cw::g_cwpy->is_showingdlg()) return true;
    if (!m_native_adapter) return true;
    const auto snapshot = m_native_adapter->screenshot_windows(*this, false);
    for (const auto& focus : snapshot.windows) {
        if (!focus.focused) continue;
        const ScreenshotWindow* current = &focus;
        // ネイティブの記述情報は古い・破損している場合がある。 wx の親子関係は循
        // 環しないため、所有関係に循環があれば走査を打ち切り、UI を停止させない。
        std::set<std::uint64_t> seen;
        while (current && seen.insert(current->id).second) {
            if (current->debugger) return false;
            const auto parent = std::find_if(snapshot.windows.begin(), snapshot.windows.end(),
                [&](const auto& candidate) { return candidate.id == current->parent; });
            current = parent == snapshot.windows.end() ? nullptr : &*parent;
        }
    }
    return true;
}

std::optional<std::shared_ptr<::cw::lib::Dialog>> Frame::find_activedialog()
{
    ::cw::lib::RuntimeAccess runtime_access;
    if (!::cw::g_cwpy || !::cw::g_cwpy->is_showingdlg()) return std::nullopt;
    if (m_active_dialog && m_active_dialog->visible) return m_active_dialog;
    return std::nullopt;
}


bool Frame::save_screenshot()
{
    ::cw::lib::RuntimeAccess runtime_access;
    const auto runtime = ::cw::g_cwpy;
    // ネイティブの Frame は CWPy の初期化前に存在する場合がある。 Py の
    // グローバルフレームは初期化済みなので、この明示的な実行環境なしの境界を維持する。
    if (!runtime || !runtime->m_setting || !runtime->is_showingdlg() || !can_screenshot()) return false;
    const auto owner = shared_from_this();
    runtime->exec_func([owner, runtime](const auto&, const auto&) -> Value {
        ::cw::lib::RuntimeAccess runtime_access;
        // 予約されたゲーム・UI の処理をまたいで両方の所有者を保持する。 ただし停止し
        // た Frame や置き換えられた Engine へ、古い取得結果を届けない。
        if (::cw::g_cwpy != runtime || owner->m_exec_shutdown.load()) return {};
        runtime->play_sound("screenshot", false, 100, 1, 0, 0, false);
        const auto dictionary = as_object<::cw::thread::TitleDictionaryPair>(runtime->get_titledic(true, true));
        if (!dictionary) throw std::logic_error("dialog screenshot: expected title dictionary pair");
        auto [surface, y] = ::cw::util::create_screenshot(dictionary->values);
        if (!surface) throw std::runtime_error("dialog screenshot: missing game surface");
        auto bitmap = std::make_shared<::cw::lib::Bitmap>();
        static_cast<::cw::lib::Surface&>(*bitmap) = *surface;
        const auto fore = screenshot_color(runtime->m_setting->m_ssinfofontcolor, {0,0,0});
        const auto back = screenshot_color(runtime->m_setting->m_ssinfobackcolor, {255,255,255});
        owner->exec_func([owner, runtime, bitmap, y, fore, back,
            filename_values = dictionary->filename_values](const auto&, const auto&) -> Value {
            ::cw::lib::RuntimeAccess runtime_access;
            if (::cw::g_cwpy != runtime || owner->m_exec_shutdown.load()) return {};
            owner->_put_dlgscreenshots(bitmap, y, fore, back);
            auto filename = ::cw::util::create_screenshotfilename(filename_values);
            try {
                prepare_frame_screenshot_path(filename);
                auto pixels = bitmap->rgba;
                // wx の RGB バッファー取得は不透明とする。 Py の Bitmap.SetMaskColour
                // と同様、マスクはダイアログの合成後に適用する。
                for (std::size_t i=0; i+3<pixels.size(); i+=4) {
                    if (!bitmap->has_pixel_alpha) pixels[i+3]=255;
                    if (bitmap->has_colorkey && pixels[i]==std::get<0>(bitmap->colorkey) &&
                        pixels[i+1]==std::get<1>(bitmap->colorkey) && pixels[i+2]==std::get<2>(bitmap->colorkey)) pixels[i+3]=0;
                }
                auto bytes = ::cw::lib::imagecodec::encode_png({bitmap->size.width, bitmap->size.height, std::move(pixels)});
                // Frame の wx SaveFile と同様、拡張子に関係なく PNG とする
                // 。 既存の同期 write_file でマウントされたストレージにも対応する。
                ::cw::util::write_file(filename, Value(std::move(bytes)), nullptr, "wb", std::nullopt);
            } catch (...) {
                ::cw::util::print_ex(std::nullopt);
                const String text = "スクリーンショットの保存に失敗しました。\n" + filename;
                // ネイティブのモーダル要求はゲームスレッドが管理する。 UI
                // 側の保存失敗後はエラー通知部分だけを予約し、UI ディス
                // パッチャーからゲーム側の待機するモーダル処理を呼ばない。
                runtime->exec_func([owner, runtime, text](const auto&, const auto&) -> Value {
                    ::cw::lib::RuntimeAccess runtime_access;
                    if (::cw::g_cwpy == runtime && !owner->m_exec_shutdown.load()) runtime->call_modaldlg("ERROR", {{"text", Value(text)}});
                    return {};
                });
            }
            return {};
        });
        return {};
    });
    return true;
}

void Frame::_put_dlgscreenshots(std::shared_ptr<::cw::lib::Bitmap> bitmap, Integer y,
    std::tuple<Integer,Integer,Integer> fore, std::tuple<Integer,Integer,Integer> back)
{
    assert_ui_thread();
    if (!bitmap || !m_native_adapter) return;
    const auto runtime = ::cw::g_cwpy;
    if (!runtime || !runtime->m_setting) return;
    const auto snapshot = m_native_adapter->screenshot_windows(*this, true);
    const Integer width = bitmap->size.width, content_height = bitmap->size.height - y;
    // wx と同様、最初のダイアログの任意の背景を描いた後、初期の内容ク
    // リップを解除する。 入れ子・兄弟の描画でもこの切り替えを維持する。
    std::optional<::cw::lib::Rect> clip = ::cw::lib::Rect{0,y,width,content_height};
    const bool white = fore == std::tuple<Integer,Integer,Integer>{255,255,255};
    std::set<std::uint64_t> visited;
    std::function<void(std::uint64_t)> recurse = [&](std::uint64_t parent) {
        for (const auto& window : snapshot.windows) {
            if (window.parent != parent || !window.visible || !window.top_level || window.iconized ||
                window.debugger || window.touch_tools || !visited.insert(window.id).second) continue;
            if (!window.client) throw std::runtime_error("dialog screenshot: missing client capture");
            auto client = std::make_shared<::cw::lib::Bitmap>();
            static_cast<::cw::lib::Surface&>(*client) = *window.client;
            auto image = ::cw::util::convert_to_image(client);
            auto raster = as_object<::cw::lib::Surface>(::cw::mwin2scr_s(Value(std::static_pointer_cast<::cw::lib::Object>(image))));
            if (!raster || raster->size.width <= 0 || raster->size.height <= 0)
                throw std::runtime_error("dialog screenshot: invalid scaled client");
            const auto fonttypes = runtime->m_setting->get_fonttypes();
            const Integer pixels = static_cast<Integer>(std::get<2>(fonttypes.at("screenshot")) * 0.8);
            const auto scaled = [](Integer n) { return value_int(::cw::s(Value(n))).value_or(n); };
            const Integer title_height = scaled(pixels + 2) + 2;
            const auto font = ::cw::lib::ui::configured_font("screenshot", scaled(pixels) * 2);
            const auto [x, top, th] = screenshot_dialog_position(snapshot.frame_bounds, window.bounds,
                width, content_height, y, raster->size.width, raster->size.height, title_height);
            auto border = ::cw::util::empty_bitmap(raster->size.width+4,raster->size.height+4+th);
            ::cw::lib::ui::fill_rect(border, {0,0,border->size.width,border->size.height},
                {static_cast<std::uint8_t>(std::get<0>(back)), static_cast<std::uint8_t>(std::get<1>(back)),
                 static_cast<std::uint8_t>(std::get<2>(back)),255});
            ::cw::lib::ui::blend_surface(bitmap,border,x-2,top-2,0,0,clip);
            auto path = runtime->m_setting->m_ssinfobackimage;
            const auto custom = std::filesystem::u8path(path);
            const bool custom_exists = !path.empty() && (::cardwirth::platform::is_mounted_user_path(custom)
                ? ::cardwirth::platform::stat_mounted_user_path(custom).value_or(::cardwirth::platform::StorageStat{}).is_file()
                : std::filesystem::is_regular_file(custom));
            if (!custom_exists) path = ::cw::util::find_resource(::cw::util::join_paths({Value(runtime->m_skindir),
                Value("Resource/Image/Other/SCREENSHOT_HEADER")}), ::cw::g_M_IMG);
            std::shared_ptr<::cw::lib::Surface> tile;
            if (!path.empty()) {
                const auto bmp = ::cw::util::load_wxbmp(path, false, nullptr, Value("topleft"), std::nullopt, true, false, false, std::nullopt);
                tile = as_object<::cw::lib::Surface>(::cw::s(Value(std::static_pointer_cast<::cw::lib::Object>(bmp))));
            }
            if (tile) {
                // Py の fill_bitmap(csize=..., cpos=(rx,ry))
                // と同様、タイルの開始位置を境界からの相対座標とする。 新しい拡縮規則は追加しない。
                ::cw::util::fill_image(border, tile, {border->size.width,border->size.height}, {0,0}, {0,0});
                ::cw::lib::ui::blend_surface(bitmap,border,x-2,top-2,0,0,clip);
            }
            clip.reset(); // wx.MemoryDC.DestroyClippingRegion()
            ::cw::lib::ui::blend_surface(bitmap,raster,x,top+th);
            if (!window.title.empty()) {
                auto shadow = frame_screenshot_text(window.title, font, raster->size.width, scaled(5), !white);
                if (shadow) shadow = ::cw::imageretouch::mul_alpha(shadow,80);
                // 共通の RGBA 描画でアルファを一度だけ適用する。 wx のプラットフォー
                // ム別バッファーの癖によって、別のスクリーンショット不透明度の規則を選ばない。
                for (Integer dx=-1;dx<=1;++dx) for (Integer dy=-1;dy<=1;++dy)
                    if ((dx || dy) && shadow) ::cw::lib::ui::blend_surface(bitmap,shadow,x+scaled(5)+dx,top+1+dy);
                const auto ink = frame_screenshot_text(window.title,font,raster->size.width,scaled(5),white);
                if (ink) ::cw::lib::ui::blend_surface(bitmap,ink,x+scaled(5),top+1);
            }
            recurse(window.id);
        }
    };
    recurse(snapshot.root);
}

void Frame::change_selection(std::optional<std::shared_ptr<::cw::sprite::card::CWPyCard>> arg_selection)
{
    ::cw::lib::RuntimeAccess runtime_access;
    if (!::cw::g_cwpy) return;
    // Python の Frame.change_selection と同様、こ
    // の操作は常に CWPy のゲームスレッドへ予約する。 カード操作ボタンはネ
    // イティブのダイアログスレッドで動くため、そこで直接変更すると、 ダイアロ
    // グモデルだけが更新されて主画面の選択・反転画像が表示されない場合がある。
    ::cw::g_cwpy->exec_func(
        [selection = std::move(arg_selection)](const ::cw::lib::Arguments&,
                                                const ::cw::lib::KeywordArguments&) {
            if (!::cw::g_cwpy) return ::cw::lib::Value{};
            if (selection && *selection) {
                ::cw::g_cwpy->change_selection(
                    std::static_pointer_cast<::cw::sprite::base::SelectableSprite>(*selection),
                    std::nullopt);
            } else {
                ::cw::g_cwpy->clear_selection();
            }
            return ::cw::lib::Value{};
        });
}

::cw::lib::Integer Frame::change_cardcontrolarea(::cw::lib::String arg_callname)
{
    ::cw::lib::RuntimeAccess runtime_access;
    if (!::cw::g_cwpy) return 0;
    const auto area = ::cw::g_cwpy->m_areaid;
    if (area == value_int(::cw::g_AREA_TRADE1).value_or(-999) ||
        area == value_int(::cw::g_AREA_TRADE2).value_or(-999) ||
        area == value_int(::cw::g_AREA_TRADE3).value_or(-999)) return area;
    const auto game = ::cw::g_cwpy;
    const bool in_yado = game->m_status == "Yado";
    if (!in_yado && !(game->is_playingscenario() &&
        area == value_int(::cw::g_AREA_CAMP).value_or(-6))) return area;
    const auto target = in_yado
        ? (game->m_ydata && *game->m_ydata && (*game->m_ydata)->m_party
            ? value_int(::cw::g_AREA_TRADE2).value_or(-2)
            : value_int(::cw::g_AREA_TRADE1).value_or(-1))
        : value_int(::cw::g_AREA_TRADE3).value_or(-5);
    exec_func([game, target, in_yado, callname = std::move(arg_callname)](const auto&, const auto&) {
        game->exec_func([game, target, in_yado, callname](const auto&, const auto&) {
            game->change_specialarea(target, false);
            std::shared_ptr<::cw::sprite::base::SelectableSprite> selection;
            if (callname == "BACKPACK") {
                if (const auto card = game->find_backpackcard()) selection = *card;
                game->change_selection(selection, std::nullopt);
            } else if (in_yado && callname == "STOREHOUSE") {
                if (const auto card = game->find_storehousecard()) selection = *card;
                game->change_selection(selection, std::nullopt);
            }
            if (game->m_statusbar) game->m_statusbar->change(true, false);
            return Value{};
        });
        return Value{};
    });
    return target;
}

std::tuple<::cw::lib::Integer, ::cw::lib::Integer> Frame::GetClientPosition()
{
    assert_ui_thread();
    const auto geometry = native_frame_geometry(*this);
    if (geometry) {
        const auto& [outer, client] = *geometry;
        // Py の (GetSize - GetClientSize) + GetPosition
        // の式に正確に合わせる。 論理パネルの原点や定義の異なる ClientToScreen(0,0)
        // ではなく、両側の枠の寸法を含める。
        return {outer.x + outer.width - client.width,
                outer.y + outer.height - client.height};
    }
    // ネイティブウィンドウのない画面なし・ホスト内モデルには画面
    // 上の枠がないため、 渡されたパネル範囲を配置の境界とする。
    auto panel = as_object<::cw::lib::Window>(m_panel);
    if (!panel) return {0, 0};
    return {panel->bounds.x, panel->bounds.y};
}

void ::cw::frame::MyApp::initialize()
{
    m_flick_status = Value(g_FLICK_NONE);
    m_flick_window = Value{};
    m_flick_start_pos = {Value(-1), Value(-1)};
    m_flick_start_time = 0.0;
    m_skindlg.reset();
    m_frame_typed.reset();
}

void ::cw::frame::MyApp::set_native_adapter(
    std::shared_ptr<::cw::frame::NativeFrameAdapter> arg_adapter)
{
    if (m_frame_typed)
        throw std::logic_error("MyApp native frame adapter must be set before OnInit");
    m_native_adapter = std::move(arg_adapter);
}

bool ::cw::frame::MyApp::OnInit()
{
    // スキン変換ダイアログなど CWPy 初期化前の経路より先に
    // 、アダプターを解決する。 そうしないと Windows 以
    // 外で具体的な SDL アダプターがない場合、利用できるゲー
    // ムウィンドウを作らずにホストループが正常に終わってしまう。
    if (!m_native_adapter) m_native_adapter = make_native_adapter();
    initialize();
    const auto [skincount, unknown] = get_skincount();
    (void)unknown;
    String exe;
    if (!::cw::g_SKIN_CONV_ARGS.empty()) {
        const auto candidate = ::cw::g_SKIN_CONV_ARGS[0];
        if (ascii_lower(candidate).size() >= 4 &&
            ascii_lower(candidate).substr(ascii_lower(candidate).size() - 4) == ".exe")
            exe = candidate;
    }
    String selected_skin;
    if (skincount == 0 || !exe.empty()) {
        // 初回の変換は g_cwpy が Frame を所有する前に行われる。
        // 通常のダイアログと同じ Frame アダプター・共通 Presentation
        // を再利用し、プラットフォーム専用の別のスキン画面は作らない。
        m_frame_typed = std::make_shared<Frame>();
        m_frame_typed->set_native_adapter(m_native_adapter);
        try {
            ::cw::setting::Resource fonts;
            (void)fonts.get_fontpaths();
            std::set<String> faces;
            for (const auto& face : fonts.m_facenames)
                if (const auto* name = std::get_if<String>(&face.storage())) faces.insert(*name);
            auto paths = ::cw::setting::Resource::get_fontpaths_s("Data/Font", faces);
            std::map<String, String> installed;
            ::cw::setting::Resource::install_defaultfonts(std::move(paths), std::move(faces), installed);
        } catch (const ::cw::setting::NoFontError&) {
            (void)::cw::dialog::message::show_common_modal_message(m_frame_typed, "メッセージ",
                ::cw::g_APP_NAME + "の実行に必要なフォントがありません。\n"
                "Data/Font以下にIPAフォントをインストールしてください。", false, true);
            m_frame_typed.reset();
            return false;
        }
        m_skindlg = std::make_shared<::cw::dialog::skin::SkinConversionDialog>();
        m_skindlg->initialize(nullptr, exe, false, std::nullopt);
        if (!::cw::dialog::show_common_modal_presentation(m_frame_typed, m_skindlg)) {
            m_skindlg.reset();
            m_frame_typed.reset();
            return false;
        }
        if (m_skindlg->m_select_skin) selected_skin = m_skindlg->m_skindirname;
        m_skindlg.reset();
        if (std::get<0>(get_skincount()) == 0) {
            // Py と同様、最後の最上位の変換ウィンドウを閉じるとイベントループも終了する
            // 。 ネイティブの起動処理では、ゲームフレームを開かなかったことを通知する。
            m_frame_typed.reset();
            return false;
        }
    }
    if (!m_frame_typed) m_frame_typed = std::make_shared<Frame>();
    m_frame_typed->m_post_quit_message_on_destroy = !m_embedded_session;
    m_frame_typed->set_native_adapter(m_native_adapter);
    m_frame_typed->initialize(std::shared_ptr<MyApp>(this, [](MyApp*) {}), selected_skin);
    m_frame_typed->_start_wx(m_frame_typed);
    return true;
}

void ::cw::frame::MyApp::OnCloseSkinDialog(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (!m_skindlg) return;
    if (std::get<0>(get_skincount()) > 0) {
        m_frame_typed = std::make_shared<Frame>();
        m_frame_typed->m_post_quit_message_on_destroy = !m_embedded_session;
        m_frame_typed->set_native_adapter(m_native_adapter);
        m_frame_typed->initialize(std::shared_ptr<MyApp>(this, [](MyApp*) {}), m_skindlg->m_select_skin ? m_skindlg->m_skindirname : "");
        m_frame_typed->_start_wx(m_frame_typed);
    }
    m_skindlg.reset();
}

auto ::cw::frame::MyApp::FilterEvent(std::shared_ptr<::cw::lib::Event> arg_event) -> ::cw::lib::Integer
{
    if (!arg_event) return -1;
    auto frame = current_frame();
    if (!frame) return -1;
    if (frame->m_filter_event) {
        const auto result = (*frame->m_filter_event)({Value(std::static_pointer_cast<::cw::lib::Object>(arg_event))}, {});
        if (value_truthy(result)) return 1;
    }
    // 共通の互換境界では type 2 をキー解除に使う。 Frame の
    // Window イベントへ変換する時もキー ID・修飾キーを保持し、
    // ネイティブのダイアログ・フリックの処理は共通表示プロバイダーに残す。
    if (arg_event->type == 2) {
        auto event = std::make_shared<::cw::lib::Window>();
        event->id = arg_event->id;
        event->modifiers = arg_event->modifiers;
        frame->OnKeyUp(std::move(event));
    }
    return -1;
}

::cw::lib::Integer g_FLICK_NONE{0};
::cw::lib::Integer g_FLICK_START{1};
::cw::lib::Value g__Arg1{};
::cw::lib::Value g__Arg2{};
::cw::lib::Value g__Arg3{};
::cw::lib::Value g__Arg4{};
::cw::lib::Value g__Arg5{};
::cw::lib::Value g__Arg6{};
::cw::lib::Value g__Arg7{};
::cw::lib::Value g__Arg8{};
::cw::lib::Value g__Arg9{};
::cw::lib::Value g__Result{};
::cw::lib::Value g__killlist_mutex{};

void Frame___init___set_bounds()
{
    if (auto frame = current_frame()) frame->apply_window_bounds();
}

void Frame___init___set_bounds_adjust_position()
{
    if (auto frame = current_frame()) {
        const auto panel = as_object<::cw::lib::Window>(frame->m_panel);
        if (frame->m_native_adapter) frame->m_native_adapter->adjust_position(*frame, panel);
    }
}

void Frame_update_dialogparams_func(std::shared_ptr<::cw::frame::Frame> arg_self)
{
    if (arg_self) arg_self->update_dialogparams();
}

void Frame_update_dialogparams_func_func1(std::shared_ptr<::cw::frame::Frame> arg_self)
{
    if (arg_self && !arg_self->m__cardholder) arg_self->update_dialogparams();
}

void Frame_update_dialogparams_func_func2(std::shared_ptr<::cw::frame::Frame> arg_self)
{
    if (arg_self && !arg_self->m__handview) arg_self->update_dialogparams();
}

void Frame_update_dialogparams_func_func3(std::shared_ptr<::cw::frame::Frame> arg_self)
{
    if (arg_self && !arg_self->m__infoview) arg_self->update_dialogparams();
}

void Frame_update_dialogparams_func_func4(std::shared_ptr<::cw::frame::Frame> arg_self)
{
    if (arg_self && !arg_self->m__replcardholder) arg_self->update_dialogparams();
}

void Frame__bind_panel_setfocus(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (auto frame = current_frame()) frame->OnSetFocus(arg_event);
}

void Frame__bind_panel_killfocus(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (auto frame = current_frame()) frame->OnKillFocus(arg_event);
}

void Frame__bind_panel_close(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (auto frame = current_frame()) frame->OnCloseFromFrame(std::move(arg_event));
}

void Frame__bind_activate(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (auto frame = current_frame(); frame && !frame->m__keybind) frame->m__keybind = true;
    (void)arg_event;
}

void Frame__bind_activate_app(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (auto frame = current_frame()) frame->OnActivateApp(std::move(arg_event));
}

void Frame__bind_activate_app_func()
{
    if (::cw::g_cwpy && ::cw::g_cwpy->m_event) ::cw::g_cwpy->m_event->m_waittime = 0;
}

void Frame__bind_activate_app_func_2()
{
    if (::cw::g_cwpy && ::cw::g_cwpy->m_event) ::cw::g_cwpy->m_event->m_waittime = 0;
}

void Frame_show_debugger_func()
{
    if (auto frame = current_frame()) frame->show_debugger(true);
}

void Frame_close_debugger_func()
{
    if (auto frame = current_frame()) frame->close_debugger();
}

void Frame_sync_exec_func2(const ::cw::lib::Arguments& arg_args, const ::cw::lib::KeywordArguments& arg_kwargs)
{
    if (auto frame = current_frame()) frame->OnEXECFUNC(nullptr);
    (void)arg_args; (void)arg_kwargs;
}

::cw::lib::Value Frame_OnDropFiles_func(std::vector<::cw::lib::String> arg_paths)
{
    if (auto frame = current_frame()) frame->_on_drop_files(std::move(arg_paths));
    return Value{};
}

void Frame__on_drop_files_update_skin(::cw::lib::String arg_skindirname)
{
    if (::cw::g_cwpy) ::cw::g_cwpy->update_skin(std::move(arg_skindirname), true, false, std::nullopt, true, false);
}

void Frame_OnIconize_func()
{
    if (auto frame = current_frame()) frame->OnIconize(nullptr);
}

void Frame_OnIconize_func_2()
{
    if (auto frame = current_frame()) frame->OnIconize(std::make_shared<::cw::lib::Window>());
}

void Frame_set_app_suspended(bool suspended, ::cardwirth::platform::AppVisibility::Reason reason)
{
    if (auto frame = current_frame()) {
        if (!frame->m_app_visibility.set(reason, suspended)) return;
        auto event = std::make_shared<::cw::lib::Window>();
        event->id = frame->m_app_visibility.suspended() ? 1 : 0;
        frame->OnIconize(std::move(event));
    }
}

void Frame_OnPARTYSELECT_func()
{
    if (auto frame = current_frame()) frame->OnPARTYSELECT(nullptr);
}

void Frame_OnPARTYSELECT_func_2()
{
    if (auto frame = current_frame()) frame->OnPARTYSELECT(nullptr);
}

void Frame_OnPLAYERSELECT_func()
{
    if (auto frame = current_frame()) frame->OnPLAYERSELECT(nullptr);
}

void Frame_ok_scenarioselect_func(std::shared_ptr<::cw::header::ScenarioHeader> arg_header, std::vector<::cw::lib::String> arg_sel, ::cw::lib::String arg_selpath)
{
    (void)arg_header; (void)arg_sel; (void)arg_selpath;
    if (::cw::g_cwpy) ::cw::g_cwpy->m_areaid = 4;
}

void Frame_check_killlist_func()
{
    if (auto frame = current_frame()) frame->check_killlist();
}

void Frame_OnINFOVIEW_func()
{
    if (auto frame = current_frame()) frame->OnINFOVIEW(nullptr);
}

void Frame_OnSAVE_func()
{
    if (auto frame = current_frame()) frame->OnSAVE(nullptr);
}

void Frame_OnSAVED_MESSAGE_func()
{
    if (auto frame = current_frame()) frame->OnSAVED_MESSAGE(nullptr);
}

void Frame_OnUSECARD_func(::cw::lib::Value arg_owner, std::shared_ptr<::cw::header::CardHeader> arg_header, std::vector<std::shared_ptr<::cw::sprite::card::CWPyCard>> arg_targets)
{
    (void)arg_owner; (void)arg_header; (void)arg_targets;
    if (auto frame = current_frame()) frame->OnUSECARD(nullptr);
}

void Frame_OnUSECARD_use_card(std::shared_ptr<::cw::character::Character> arg_owner, ::cw::lib::Value arg_targets, std::shared_ptr<::cw::header::CardHeader> arg_header)
{
    if (arg_owner && arg_header) (void)arg_owner->use_card(std::move(arg_targets), std::move(arg_header));
}

void Frame_OnF9_func()
{
    if (auto frame = current_frame()) frame->OnF9(nullptr);
}

void Frame_OnF9_func_stop()
{
    if (::cw::g_cwpy) ::cw::g_cwpy->f9(false, false);
}

void Frame_move_dlg_OnIconize(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (auto frame = current_frame())
        frame->request_iconize(arg_event && arg_event->id != 0);
}

void Frame_kill_dlg_func(bool arg_lockmenucard, bool arg_redraw)
{
    if (auto frame = current_frame()) frame->kill_dlg(std::nullopt, arg_lockmenucard, arg_redraw);
}

void Frame_kill_dlg_if_model(::cw::lib::Object* arg_model)
{
    auto frame = current_frame();
    if (frame && arg_model && frame->m_active_dialog_model.get() == arg_model)
        frame->kill_dlg(std::nullopt, false, true);
}

bool Frame_poll_modal_pointer_release()
{
    if (auto frame = current_frame()) return frame->poll_modal_pointer_release();
    return true;
}

void restore_dialog_owner(std::uintptr_t arg_native_handle)
{
#ifdef _WIN32
    const HWND owner = reinterpret_cast<HWND>(arg_native_handle);
    if (!owner || !IsWindow(owner)) return;
    if (::cw::g_cwpy && ::cw::g_cwpy->is_modal_input_blocked()) {
        // 入れ子のネイティブダイアログは直近の所有者を復元できるが、Engine
        // の HWND 自体は世代付きの終了確認が管理する。 ゲー
        // ム側の後処理と物理的な解除が終わるまで無効のままにする。
        if (const auto frame = current_frame(); frame &&
            reinterpret_cast<HWND>(frame->m_native_window_handle) == owner)
            return;
    }
    EnableWindow(owner, TRUE);
    // ネイティブのモーダルが透明・独自コントロールで Engine を
    // 覆っていた場合がある。 次の入力を受け付ける前に保持中の Engine
    // 画像から再描画し、閉じたダイアログのピクセルを残さない。
    RedrawWindow(owner, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    if (!::cardwirth::debug::nonactive_automation_enabled()) SetForegroundWindow(owner);
#else
    (void)arg_native_handle;
#endif
}

void Frame_save_screenshot_func(std::shared_ptr<::cw::frame::Frame> arg_self)
{
    if (arg_self) (void)arg_self->save_screenshot();
}

void Frame_change_cardcontrolarea_func(::cw::lib::Integer arg_areaid)
{
    if (::cw::g_cwpy) ::cw::g_cwpy->change_specialarea(arg_areaid, false);
}

void Frame_change_cardcontrolarea_func_func(::cw::lib::Integer arg_areaid)
{
    Frame_change_cardcontrolarea_func(arg_areaid);
}

void Frame_change_cardcontrolarea_func2()
{
    if (auto frame = current_frame()) (void)frame->change_cardcontrolarea("BACKPACK");
}

void Frame_change_cardcontrolarea_func2_func()
{
    if (auto frame = current_frame()) (void)frame->change_cardcontrolarea("STOREHOUSE");
}

::cw::lib::Integer MyApp_FilterEvent_end_flick()
{
    const auto frame = current_frame();
    const auto app = frame ? as_object<::cw::frame::MyApp>(frame->m_app) : nullptr;
    if (!app || value_int(app->m_flick_status).value_or(g_FLICK_NONE) != g_FLICK_START) return -1;
    Integer result = -1;
#ifdef _WIN32
    POINT cursor{};
    GetCursorPos(&cursor);
    const auto sx = value_int(std::get<0>(app->m_flick_start_pos)).value_or(cursor.x);
    const auto elapsed = std::chrono::duration<Real>(std::chrono::steady_clock::now().time_since_epoch()).count() - app->m_flick_start_time;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting &&
        cursor.x - sx >= ::cw::g_cwpy->m_setting->m_flick_distance &&
        elapsed <= static_cast<Real>(::cw::g_cwpy->m_setting->m_flick_time_msec) / 1000.0) {
        auto event = std::make_shared<::cw::lib::Window>();
        event->id = 1;
        event->bounds.x = value_int(std::get<0>(app->m_flick_start_pos)).value_or(cursor.x);
        event->bounds.y = value_int(std::get<1>(app->m_flick_start_pos)).value_or(cursor.y);
        frame->dispatch_native_event("RIGHTUP", event);
        result = 1;
    }
#endif
    app->m_flick_status = Value(g_FLICK_NONE);
    app->m_flick_window = Value{};
    app->m_flick_start_pos = {Value(-1), Value(-1)};
    app->m_flick_start_time = 0.0;
    return result;
}

::cw::lib::Value MyApp_FilterEvent_end_flick_lambda_line_1797()
{
    return Value(MyApp_FilterEvent_end_flick());
}

void MyApp_FilterEvent_watch_mousebutton()
{
    const auto frame = current_frame();
    const auto app = frame ? as_object<::cw::frame::MyApp>(frame->m_app) : nullptr;
    if (!frame || !app || value_int(app->m_flick_status).value_or(g_FLICK_NONE) != g_FLICK_START) return;
    const auto now = std::chrono::duration<Real>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_setting ||
        now - app->m_flick_start_time >= static_cast<Real>(::cw::g_cwpy->m_setting->m_flick_time_msec) / 1000.0) return;
#ifdef _WIN32
    if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) == 0) {
        (void)MyApp_FilterEvent_end_flick();
        return;
    }
#endif
    if (frame->m_native_adapter)
        frame->m_native_adapter->watch_left_button(*frame, std::max<Integer>(1, ::cw::g_cwpy->m_setting->m_frametime));
}

void MyApp_FilterEvent_func(bool arg_updatedebug_on_dlg)
{
    if (!::cw::g_cwpy) return;
    if (arg_updatedebug_on_dlg) ::cw::g_cwpy->play_sound("page", false, 100, 1, 0, 0, false);
    ::cw::g_cwpy->set_debug(!::cw::g_cwpy->is_debugmode());
}

std::tuple<::cw::lib::Integer, ::cw::lib::Integer> get_skincount()
{
    Integer skincount = 0;
    Integer unknown_ver = 0;
    const std::filesystem::path root("Data/Skin");
    std::error_code ec;
    const bool mounted = ::cardwirth::platform::is_mounted_user_path(root);
    std::vector<std::filesystem::path> directories;
    if (mounted) {
        const auto root_status = ::cardwirth::platform::stat_mounted_user_path(root);
        if (!root_status || !root_status->is_directory()) return {0, 0};
        const auto entries = ::cardwirth::platform::list_mounted_user_path(root);
        if (!entries) return {0, 0};
        for (const auto& entry : *entries)
            if (entry.stat.is_directory()) directories.push_back(entry.path);
    } else {
        if (!std::filesystem::is_directory(root, ec)) return {0, 0};
        for (const auto& entry : std::filesystem::directory_iterator(root, ec))
            if (!ec && entry.is_directory(ec)) directories.push_back(entry.path());
    }
    const auto is_file = [mounted](const std::filesystem::path& path) {
        if (mounted) {
            const auto status = ::cardwirth::platform::stat_mounted_user_path(path);
            return status && status->is_file();
        }
        std::error_code error;
        return std::filesystem::is_regular_file(path, error);
    };
    for (const auto& directory : directories) {
        const auto skinpath = directory / "Skin.xml";
        if (!is_file(skinpath)) {
            const auto removed = directory / "Skin.xml_removed";
            if (is_file(removed)) {
                // Python と同様、名前を戻してからディレクトリーをご
                // み箱へ移し、 ツリーがまだ残っていれば目印を復元する。
                try {
                    const bool renamed = mounted
                        ? ::cardwirth::platform::rename_mounted_user_path(
                              removed, skinpath, false)
                        : ([&] {
                              std::filesystem::rename(removed, skinpath, ec);
                              return !ec;
                          })();
                    if (renamed) {
                        try {
                            ::cw::util::remove(::cw::lib::encoding::path_to_utf8(directory), true);
                        } catch (const std::exception&) {
                        }
                        if (is_file(skinpath)) {
                            if (mounted)
                                (void)::cardwirth::platform::rename_mounted_user_path(
                                    skinpath, removed, false);
                            else
                                std::filesystem::rename(skinpath, removed, ec);
                        }
                    }
                } catch (const std::exception&) {
                }
            }
            continue;
        }
        auto property = std::make_shared<::cw::header::GetProperty>();
        property->initialize(::cw::lib::encoding::path_to_utf8(skinpath), std::nullopt, std::nullopt);
        String version = "0";
        if (const auto it = property->m_attrs.find(std::nullopt); it != property->m_attrs.end()) {
            if (const auto data_version = it->second.find("dataVersion");
                data_version != it->second.end())
                version = data_version->second;
        }
        if (skin_version_supported(version)) ++skincount;
        else ++unknown_ver;
    }
    return {skincount, unknown_ver};
}

void main()
{
    struct ClosePresentationWindows {
        ~ClosePresentationWindows() { ::cw::dialog::close_standalone_presentations(); }
    } close_presentation_windows;
    if (const auto frame = current_frame();
        frame && frame->m_native_adapter && frame->m_native_adapter->is_hosted()) {
        // ALL がプロセスのネイティブイベントループを所有する。ホスト内の Engine
        // スレッドは明示的なアダプター受信箱と Frame の二つの UI
        // 受信箱だけを処理し、 別のネイティブウィンドウループを作らない。CWPy
        // のワーカーが m__running を公開する前に UI 側はこの
        // ループへ入るため、 起動時の false を終了要求とは扱わない。
        // 終了は OnDestroy が g_quit_app で明示する。
        while (!::cw::g_quit_app && ::cw::g_cwpy) {
            ::cw::lib::RuntimeAccess runtime_access;
            frame->m_native_adapter->hosted_loop_step(*frame);
            ::cardwirth::debug::refresh_debug_control_clickable_areas();
            frame->drain_present_queue();
            frame->OnEXECFUNC(nullptr);
            ::cw::lib::without_runtime_access([&] { std::this_thread::sleep_for(std::chrono::milliseconds(1)); });
        }
        return;
    }
#ifdef _WIN32
    // Win32 がメッセージ処理を所有し、ホストの WndProc が NativeFrameAdapter::EventSink
    // を通して正規化したメッセージを渡す。 SDL のキューは Win32
    // のメッセージ源ではないため、GetMessage() で無期限
    // に待つと処理できなくなる。 時間を区切って待ち、同じ UI ス
    // レッドで保留中の Win32 メッセージをすべて処理する。 入
    // 力遅延を一フレーム未満に保ちつつ、空回りしない待機時間を使う。
    for (;;) {
        ::cw::lib::RuntimeAccess runtime_access;
        // スキン変換ダイアログは CWPy 初期化前の有効な
        // UI 状態である。 ゲームワーカーが未生成でも
        // 、そこで Win32 メッセージの処理を続ける。
        if (::cw::g_quit_app) break;
        // SDL_PollEvent はプロセスの Win32 キューも処理する
        // 。 同じ UI スレッド上の移植済みネイティブダイアログが前面にある間
        // は、下の通常の Win32 ループで編集欄・コンボのキー入力を渡し、
        // SDL が無関係な Engine 入力として消費しないようにする。
        const auto standalone_frame = current_frame();
        ::cw::dialog::pump_standalone_presentations(standalone_frame);
        ::cardwirth::debug::refresh_debug_control_clickable_areas();
        const HWND engine_window = standalone_frame
            ? reinterpret_cast<HWND>(standalone_frame->m_native_window_handle) : nullptr;
        HWND native_dialog = nullptr;
        EnumThreadWindows(GetCurrentThreadId(), [](HWND candidate, LPARAM parameter) -> BOOL {
            if (!IsWindowVisible(candidate)) return TRUE;
            wchar_t klass[128]{};
            GetClassNameW(candidate, klass, static_cast<int>(std::size(klass)));
            if (std::wcscmp(klass, L"CardWirthSDLFrame") == 0 ||
                std::wcscmp(klass, L"SDL_app") == 0 ||
                std::wcscmp(klass, L"ConsoleWindowClass") == 0)
                return TRUE;
            *reinterpret_cast<HWND*>(parameter) = candidate;
            return FALSE;
        }, reinterpret_cast<LPARAM>(&native_dialog));
        // Python/wx と同様、モーダルの子には独自のイベント経
        // 路を使う。 前面状態ではなく所有関係で子を判定し、テストウィ
        // ンドウをアクティブにしなくても自動入力が機能するようにする。
        const bool native_dialog_open = native_dialog && native_dialog != engine_window;
        if (!native_dialog_open && ::cw::g_cwpy && ::cw::g_cwpy->m_eventhandler)
            ::cw::g_cwpy->m_eventhandler->native_loop_step();

        const DWORD wait_result = ::cw::lib::without_runtime_access([&] { return MsgWaitForMultipleObjectsEx(
            0, nullptr, 8, QS_ALLINPUT, MWMO_INPUTAVAILABLE); });
        if (wait_result == WAIT_FAILED) {
            throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                    "MsgWaitForMultipleObjectsEx(CardWirthPy_C) failed");
        }
        if (wait_result == WAIT_TIMEOUT) continue;
        if (wait_result != WAIT_OBJECT_0) continue;

        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                // 入れ子のネイティブダイアログは、自身のモーダルループを抜ける際に WM_QUIT
                // を再送する場合がある。 CardWirthPy_C の明示的な
                // 終了フラグが公開されるまでは、その余分なメッセージを無視する。
                if (::cw::g_quit_app || !::cw::g_cwpy) return;
#if defined(CARDWIRTH_LOG_ENABLED)
                ::cardwirth::debug::log("Ignoring stray WM_QUIT while CardWirthPy_C is running");
#endif
                continue;
            }
            if (native_dialog_open && IsWindow(native_dialog) &&
                IsDialogMessageW(native_dialog, &message))
                continue;
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
#else
    // SDL・画面なしアダプター用の小さなホストループを維持する。CWPy::run()
    // がワーカーで動く間、 ネイティブ入力の取得・表示受信箱・保留 UI
    // コールバックはこのスレッドで処理する。 UI ループ開始後にワー
    // カーが m__running を公開するため、起動時の false
    // は終了信号とみなさない。 ホスト内・Win32 経路と同じく、g_quit_app
    // と実行環境の生存期間を明示的な終了条件とする。
    while (!::cw::g_quit_app && ::cw::g_cwpy) {
            ::cw::lib::RuntimeAccess runtime_access;
        if (::cw::g_cwpy->m_eventhandler)
            ::cw::g_cwpy->m_eventhandler->native_loop_step();
        if (const auto frame = current_frame()) {
            ::cw::dialog::pump_standalone_presentations(frame);
            ::cardwirth::debug::refresh_debug_control_clickable_areas();
            frame->drain_present_queue();
            frame->OnEXECFUNC(nullptr);
        }
        ::cw::lib::without_runtime_access([&] { std::this_thread::sleep_for(std::chrono::milliseconds(1)); });
    }
#endif
}

}
