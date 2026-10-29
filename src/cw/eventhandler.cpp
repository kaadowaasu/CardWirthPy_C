#include "cw/lib/exception_origin.hpp"
#include "cw/lib/runtime_access.hpp"
// cw/eventhandler.py の移植。pygame/wx の呼び出しは、 eventhandler.hpp
// で宣言した EventHandlerRuntimeContext を通す。
#include "cw/eventhandler.hpp"
#include "cw/battle.hpp"
#include "cw/cw.hpp"
#include "cw/content.hpp"
#include "cw/data.hpp"
#include "cw/dialog/dialog.hpp"
#include "cw/dialog/presentation.hpp"
#include "cw/debug/logging.hpp"
#include "cw/effectbooster.hpp"
#include "cw/event.hpp"
#include "cw/eventrelay.hpp"
#include "cw/frame.hpp"
#include "cw/lib/ui_surface.hpp"
#include "cw/lib/system_clipboard.hpp"
#include "cw/thread.hpp"
#include "cw/novel.hpp"
#include "cw/setting.hpp"
#include "cw/sprite/background.hpp"
#include "cw/sprite/bill.hpp"
#include "cw/sprite/card.hpp"
#include "cw/sprite/message.hpp"
#include "cw/sprite/scrollbar.hpp"
#include "cw/sprite/statusbar.hpp"
#include "cw/sprite/touchbutton.hpp"
#include "cw/util.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <utility>

#if __has_include(<SDL3/SDL.h>)
#include <SDL3/SDL.h>
#include "cw/lib/sdl_game_touch.hpp"
#include "cw/lib/sdl_key.hpp"
#include "cw/lib/sdl_input_clock.hpp"
#define CW_EVENTHANDLER_HAS_SDL3 1
#else
#define CW_EVENTHANDLER_HAS_SDL3 0
#endif

#if defined(_WIN32)
#include <windows.h>
#endif

namespace cw::eventhandler {
namespace {
// 長押しを右クリックとして処理した後は、指を離すか新
// しいタッチを開始するまで、次の長押しを認識しない。
// 右クリックから開いたダイアログが実行コンテキスト
// をリセットする場合があるため、その外で保持する。
bool g_long_press_handled = false;
}  // namespace

namespace {
thread_local const ::cw::thread::CWPy* pointer_dispatch_runtime{};
thread_local std::optional<::cw::lib::Point> pointer_dispatch_position;
class PointerDispatchScope final {
    const ::cw::thread::CWPy* previous_runtime = pointer_dispatch_runtime;
    std::optional<::cw::lib::Point> previous_position = pointer_dispatch_position;
public:
    explicit PointerDispatchScope(const std::shared_ptr<InputEvent>& event) {
        pointer_dispatch_runtime = ::cw::g_cwpy.get();
        pointer_dispatch_position.reset();
        // 合成した押下中ボタンの解除イベントは、既にゲーム座標を持つ。
        if (event && !event->ignore_up &&
            (event->type == EVENT_NONE || event->type == EVENT_MOUSEBUTTONDOWN ||
             event->type == EVENT_MOUSEBUTTONUP)) pointer_dispatch_position = event->pos;
    }
    ~PointerDispatchScope() {
        pointer_dispatch_runtime = previous_runtime;
        pointer_dispatch_position = previous_position;
    }
};
}
std::optional<::cw::lib::Point> dispatch_pointer_position(const ::cw::thread::CWPy* runtime) {
    return pointer_dispatch_runtime == runtime ? pointer_dispatch_position : std::nullopt;
}
LivePointerInputScope::LivePointerInputScope()
    : previous_runtime_(pointer_dispatch_runtime),
      previous_position_(pointer_dispatch_position) {
    pointer_dispatch_runtime = nullptr;
    pointer_dispatch_position.reset();
}
LivePointerInputScope::~LivePointerInputScope() {
    pointer_dispatch_runtime = previous_runtime_;
    pointer_dispatch_position = previous_position_;
}
namespace {

bool within_long_press_distance(const ::cw::lib::Point& position,
                                const ::cw::lib::Point& origin,
                                ::cw::lib::Integer limit)
{
    // 既存のタッチ拡張には Python の対応処理がない。 フレームごと
    // の確認と、予約された解除イベントによる代替処理で同じ移動規則を使う。
    return std::abs(position.x - origin.x) <= limit &&
           std::abs(position.y - origin.y) <= limit;
}

bool yado_transition_is_loading()
{
    return ::cw::g_cwpy && ::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata &&
           (*::cw::g_cwpy->m_ydata)->is_loading();
}

class SdlEventHandlerBackend : public EventHandlerBackend {
protected:
    EventSink m_sink{};
    LazyDraw m_lazy_draw{};
    std::function<void(::cw::lib::Integer)> m_remove_layer{};
    std::function<void(::cw::lib::Real)> m_expanddrawing{};
    std::function<void()> m_kill_message_layers{};
    std::function<void()> m_quit{};
    ::cw::lib::Point m_last_pos{-1, -1};

    static ::cw::lib::Integer translate_key(::cw::lib::Integer key)
    {
#if CW_EVENTHANDLER_HAS_SDL3
        switch (::cw::lib::engine_sdl_key(static_cast<SDL_Keycode>(key))) {
        case SDLK_RETURN: return KEY_RETURN;
        case SDLK_BACKSPACE: return KEY_BACKSPACE;
        case SDLK_ESCAPE: return KEY_ESCAPE;
        case SDLK_BACKSLASH: return KEY_BACKSLASH;
        case SDLK_PRINTSCREEN: return KEY_PRINT;
        case SDLK_LEFT: return KEY_LEFT;
        case SDLK_RIGHT: return KEY_RIGHT;
        case SDLK_UP: return KEY_UP;
        case SDLK_DOWN: return KEY_DOWN;
        case SDLK_PAGEUP: return KEY_PAGEUP;
        case SDLK_PAGEDOWN: return KEY_PAGEDOWN;
        case SDLK_HOME: return KEY_HOME;
        case SDLK_END: return KEY_END;
        case SDLK_LSHIFT: return KEY_LSHIFT;
        case SDLK_RSHIFT: return KEY_RSHIFT;
        case SDLK_LCTRL: return KEY_LCTRL;
        case SDLK_RCTRL: return KEY_RCTRL;
        case SDLK_F1: return KEY_F1;
        case SDLK_F2: return KEY_F2;
        case SDLK_F3: return KEY_F3;
        case SDLK_F4: return KEY_F4;
        case SDLK_F5: return KEY_F5;
        case SDLK_F6: return KEY_F6;
        case SDLK_F7: return KEY_F7;
        case SDLK_F8: return KEY_F8;
        case SDLK_F9: return KEY_F9;
        case SDLK_D: return KEY_D;
        case SDLK_P: return KEY_P;
        case SDLK_C: return KEY_C;
        default: break;
        }
#endif
        return key;
    }

    static ::cw::lib::Real timestamp_ms(std::uint64_t timestamp)
    {
#if CW_EVENTHANDLER_HAS_SDL3
        return ::cw::lib::sdl_input_timestamp_ms(timestamp);
#else
        return 0.0;
#endif
    }

    void emit(::cw::lib::Integer type, ::cw::lib::Integer key, ::cw::lib::Integer button,
              ::cw::lib::Point pos, ::cw::lib::Real timestamp, bool touch = false)
    {
        m_last_pos = pos;
        if (!m_sink) return;
        auto event = std::make_shared<InputEvent>();
        event->type = type;
        event->key = translate_key(key);
        event->button = button;
        event->pos = pos;
        event->touch = touch;
        event->timestamp = timestamp > 0.0 ? timestamp : static_cast<::cw::lib::Real>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        m_sink(std::move(event));
    }

public:
    void set_event_sink(EventSink arg_sink) override { m_sink = std::move(arg_sink); }
    void set_lazy_draw_callback(LazyDraw arg_callback) override { m_lazy_draw = std::move(arg_callback); }
    void set_remove_layer_callback(std::function<void(::cw::lib::Integer)> arg_callback) override { m_remove_layer = std::move(arg_callback); }
    void set_expanddrawing_callback(std::function<void(::cw::lib::Real)> arg_callback) override { m_expanddrawing = std::move(arg_callback); }
    void set_kill_message_layers_callback(std::function<void()> arg_callback) override { m_kill_message_layers = std::move(arg_callback); }
    void set_quit_callback(std::function<void()> arg_callback) override { m_quit = std::move(arg_callback); }

    void poll_events() override
    {
#if CW_EVENTHANDLER_HAS_SDL3
        // SDL3 のイベント取得は SDL を初期化したスレッドで行う必要がある。 ゲ
        // ームループが専用ワーカーで動く時、Windows の入力は Frame::WndProc
        // が中継するため、そこで SDL_PollEvent を呼ばない。
        if (!SDL_IsMainThread()) return;
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            // 外部の共通 Presentation ウィンドウは独自の SDL ID を持
            // つ。 そのポインター・キーイベントを Engine のゲーム側へ中継しない。
            if (::cw::dialog::dispatch_standalone_presentation_event(event)) continue;
            const auto touch = ::cw::lib::translate_game_touch(event);
            using Touch = ::cw::lib::SdlGameTouchInput;
            if (touch.kind != Touch::NotTouch) {
                if (touch.kind == Touch::Ignore) continue;
                const auto type = touch.kind == Touch::Down ? EVENT_MOUSEBUTTONDOWN :
                    touch.kind == Touch::Up ? EVENT_MOUSEBUTTONUP :
                    touch.kind == Touch::Cancel ? EVENT_POINTERCANCEL : EVENT_NONE;
                emit(type, 0, touch.kind == Touch::Down || touch.kind == Touch::Up ? 1 : 0,
                     touch.kind == Touch::Cancel ? m_last_pos : ::cw::lib::Point{touch.x, touch.y},
                     timestamp_ms(touch.timestamp), true);
                // キャンセルは生存期間イベントにも伴う。上の子 Presentation
                // のフィルターで所有ウィンドウは処理済みなので、 下のメイン・ア
                // プリ全体の Hidden/Background 通知は維持する。
                if (touch.kind != Touch::Cancel) continue;
            }
            switch (event.type) {
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                // Python の Frame.OnKillFocus と同様、ポインターキャプチャーだけでなく
                // Relay のキーも解放する。 子 Presentation のイベントは除外済みである。
                ::cw::frame::Frame__bind_panel_killfocus(nullptr);
                break;
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
                ::cw::frame::Frame__bind_panel_setfocus(nullptr);
                break;
            case SDL_EVENT_DISPLAY_CONTENT_SCALE_CHANGED:
                ::cw::lib::ui::set_system_dialog_scale(
                    SDL_GetDisplayContentScale(event.display.displayID));
                break;
            case SDL_EVENT_WINDOW_MINIMIZED:
            case SDL_EVENT_WINDOW_HIDDEN:
            case SDL_EVENT_WILL_ENTER_BACKGROUND:
            case SDL_EVENT_DID_ENTER_BACKGROUND:
                ::cw::frame::Frame_set_app_suspended(true,
                    event.type == SDL_EVENT_WINDOW_MINIMIZED ? ::cardwirth::platform::AppVisibility::Minimized :
                    event.type == SDL_EVENT_WINDOW_HIDDEN ? ::cardwirth::platform::AppVisibility::Hidden :
                    ::cardwirth::platform::AppVisibility::Background);
                break;
            case SDL_EVENT_WINDOW_RESTORED:
            case SDL_EVENT_WINDOW_MAXIMIZED:
            case SDL_EVENT_WINDOW_SHOWN:
            case SDL_EVENT_DID_ENTER_FOREGROUND:
                ::cw::frame::Frame_set_app_suspended(false,
                    event.type == SDL_EVENT_WINDOW_RESTORED || event.type == SDL_EVENT_WINDOW_MAXIMIZED
                        ? ::cardwirth::platform::AppVisibility::Minimized :
                    event.type == SDL_EVENT_WINDOW_SHOWN ? ::cardwirth::platform::AppVisibility::Hidden :
                    ::cardwirth::platform::AppVisibility::Background);
                break;
            case SDL_EVENT_QUIT:
                if (m_quit) m_quit();
                break;
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                // 単体の Presentation ウィンドウは上で除外済みである
                // 。 残った閉じる要求は Engine フレームのものとして、Frame::OnCloseFromFrame
                // の確認経路を通す。
                ::cw::frame::Frame__bind_panel_close(nullptr);
                break;
            case SDL_EVENT_KEY_DOWN:
                emit(EVENT_KEYDOWN, static_cast<::cw::lib::Integer>(event.key.key), 0,
                     m_last_pos, timestamp_ms(event.key.timestamp));
                break;
            case SDL_EVENT_KEY_UP:
                emit(EVENT_KEYUP, static_cast<::cw::lib::Integer>(event.key.key), 0,
                     m_last_pos, timestamp_ms(event.key.timestamp));
                break;
            case SDL_EVENT_MOUSE_MOTION:
                // SDL はタッチ入力からマウスイベントも生成
                // する。 下の指イベントが実際の座標付きで同じ
                // 操作を一度届けるため、重複して処理しない。
                if (event.motion.which == SDL_TOUCH_MOUSEID) break;
                emit(EVENT_NONE, 0, 0,
                     {static_cast<::cw::lib::Integer>(std::lround(event.motion.x)),
                      static_cast<::cw::lib::Integer>(std::lround(event.motion.y))},
                     timestamp_ms(event.motion.timestamp));
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP: {
                if (event.button.which == SDL_TOUCH_MOUSEID) break;
                auto button = static_cast<::cw::lib::Integer>(event.button.button);
                // SDL3 のボタン 4/5 は X1/X2 であり、pygame のホイー
                // ルイベントではない。 未割り当ての補助ボタンを旧ホイール経路へ送らない。
                if (button > 3) break;
                const auto pos = ::cw::lib::Point{
                    static_cast<::cw::lib::Integer>(std::lround(event.button.x)),
                    static_cast<::cw::lib::Integer>(std::lround(event.button.y))};
                emit(event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? EVENT_MOUSEBUTTONDOWN : EVENT_MOUSEBUTTONUP,
                     0, button, pos, timestamp_ms(event.button.timestamp));
                break;
            }
            case SDL_EVENT_MOUSE_WHEEL: {
                if (event.wheel.which == SDL_TOUCH_MOUSEID) break;
                auto amount = event.wheel.integer_y;
                if (!amount) amount = static_cast<::cw::lib::Integer>(event.wheel.y > 0.0f ? 1 : (event.wheel.y < 0.0f ? -1 : 0));
                if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) amount = -amount;
                if (amount) emit(EVENT_MOUSEBUTTONUP, 0, amount > 0 ? 4 : 5,
                                  {static_cast<::cw::lib::Integer>(std::lround(event.wheel.mouse_x)),
                                   static_cast<::cw::lib::Integer>(std::lround(event.wheel.mouse_y))},
                                  timestamp_ms(event.wheel.timestamp));
                break;
            }
            default: break;
            }
        }
#endif
    }

    bool native_loop_step() override { poll_events(); return true; }
    void request_lazy_draw(const ::cw::lib::Rect& arg_rect) override { if (m_lazy_draw) m_lazy_draw(arg_rect); }
    void remove_layer(::cw::lib::Integer arg_layer) override { if (m_remove_layer) m_remove_layer(arg_layer); }
    void apply_expanddrawing(::cw::lib::Real arg_scale) override { if (m_expanddrawing) m_expanddrawing(arg_scale); }
    void kill_message_layers() override { if (m_kill_message_layers) m_kill_message_layers(); }
};

#if defined(_WIN32)
class WindowsEventHandlerBackend final : public SdlEventHandlerBackend {
public:
    bool native_loop_step() override
    {
        MSG message{};
        while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                if (m_quit) m_quit();
                return false;
            }
            ::TranslateMessage(&message);
            ::DispatchMessageW(&message);
        }
        poll_events();
        return true;
    }
};
#endif

EventHandlerRuntimeContext g_default_context{};
EventHandlerRuntimeContext* g_context = &g_default_context;
// configure_backend はバックエンドの std::function
// コールバックを置き換える。 SDL のイベント取得は UI スレッド、EventHandler::run
// はゲームスレッドで動くため、一度だけ行う設定を直列化する。
std::mutex g_backend_dispatch_mutex{};
EventHandlerRuntimeContext& c() { return *g_context; }

bool clear_volume_bar_on_right_click(bool flick)
{
    // Py と同様、clear_volumebar() が実際に音量バーを隠した時だけクリックを消費
    // する。 表示状態をキャッシュせず、その時点で共通ステータスバーのコールバックに問い合わせる。
    return !flick && c().clear_volume_bar && c().clear_volume_bar();
}

bool is_pointer_or_key_event(const InputEvent& event)
{
    return event.type == EVENT_NONE || event.type == EVENT_KEYDOWN ||
           event.type == EVENT_KEYUP || event.type == EVENT_POINTERCANCEL ||
           event.type == EVENT_MOUSEBUTTONDOWN ||
           event.type == EVENT_MOUSEBUTTONUP;
}

bool stale_input_generation(const InputEvent& event)
{
    return event.modal_epoch_stamped && ::cw::g_cwpy &&
           event.modal_epoch != ::cw::g_cwpy->m_modal_epoch.load(std::memory_order_acquire);
}

// ダイアログ入力の除外条件は一つにまとめる。 実体の Engine がモー
// ダルの生存期間を管理し、Context は単独テスト用の代替だけに使う。
bool dialog_input_blocked()
{
    if (::cw::g_cwpy) return ::cw::g_cwpy->is_modal_input_blocked();
    return c().showing_dialog;
}

bool decompression_active()
{
    if (::cw::g_cwpy) return ::cw::g_cwpy->m_is_decompressing;
    return c().is_decompressing;
}

bool running_event_active()
{
    if (::cw::g_cwpy) return ::cw::g_cwpy->is_runningevent();
    return c().running_event;
}

bool processing_input_blocked()
{
    if (::cw::g_cwpy)
        return ::cw::g_cwpy->m_is_processing &&
               !::cw::g_cwpy->m_is_decompressing;
    return c().processing && !c().is_decompressing;
}

void reset_runtime_pointer_state(EventHandlerRuntimeContext& context)
{
    context.mouse_left = context.mouse_middle = context.mouse_right = false;
    context.current_down_time.reset();
    context.long_press_started = context.long_press_fired = false;
    context.long_press_start_time = context.long_press_fired_time = 0.0;
    context.long_press_selection.reset();
    context.flick_started = false;
    context.flick_start_time = 0.0;
    context.flick_selection.reset();
    context.wheel_mode_x = context.wheel_mode_y = -1;
}

template <typename T>
std::shared_ptr<T> value_object(const ::cw::lib::Value& value)
{
    const auto* ptr = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
    return ptr ? std::dynamic_pointer_cast<T>(*ptr) : nullptr;
}

::cw::lib::Integer value_integer(const ::cw::lib::Value& value, ::cw::lib::Integer fallback = 0)
{
    if (const auto* p = std::get_if<::cw::lib::Integer>(&value.storage())) return *p;
    if (const auto* p = std::get_if<::cw::lib::Real>(&value.storage())) return static_cast<::cw::lib::Integer>(*p);
    if (const auto* p = std::get_if<bool>(&value.storage())) return *p ? 1 : 0;
    return fallback;
}

bool value_bool(const ::cw::lib::Value& value, bool fallback = false)
{
    if (const auto* p = std::get_if<bool>(&value.storage())) return *p;
    if (const auto* p = std::get_if<::cw::lib::Integer>(&value.storage())) return *p != 0;
    return fallback;
}

::cw::lib::String value_string(const ::cw::lib::Value& value)
{
    if (const auto* p = std::get_if<::cw::lib::String>(&value.storage())) return *p;
    return {};
}

std::shared_ptr<::cw::lib::Object> value_object_ptr(const ::cw::lib::Value& value)
{
    const auto* ptr = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
    return ptr ? *ptr : nullptr;
}

// Py の backlog/backlog_all と同様、元の一覧を保持する。 ネイティブの vector
// メンバーには、所有する SystemData も保持するエイリアシング shared_ptr を
// 使う。 生参照では破棄後に無効となり、ValueList のコピーでは一覧の変更を反映できない。
struct BacklogSequence : ::cw::lib::Object {
    std::shared_ptr<std::vector<::cw::lib::Value>> records{};
};

std::shared_ptr<const std::vector<::cw::lib::Value>> backlog_records(const ::cw::lib::Value& value)
{
    if (const auto sequence = value_object<BacklogSequence>(value)) return sequence->records;
    // 明示的に渡されたネイティブ一覧オブジェクトは、既存の ValueList
    // 表現を維持する。 同じ実体の読み取り経路と所有規則を使う。
    if (const auto list = value_object<::cw::sprite::message::ValueList>(value))
        return std::shared_ptr<const std::vector<::cw::lib::Value>>(list, &list->values);
    return {};
}

std::vector<::cw::lib::Value> sequence_values(const ::cw::lib::Value& value)
{
    if (const auto sequence = value_object<::cw::data::VariantSequence>(value)) return sequence->m_values;
    return {};
}

void set_has_inputevent(bool value)
{
    c().has_inputevent = value;
    if (::cw::g_cwpy) ::cw::g_cwpy->m_has_inputevent = value;
}

void tick_clock_now()
{
    if (!::cw::g_cwpy) return;
    const auto fps = ::cw::g_cwpy->m_setting ? ::cw::g_cwpy->m_setting->m_fps : 60;
    ::cw::g_cwpy->tick_clock(fps);
}

::cw::lib::Integer scaled_ppi(::cw::lib::Integer value)
{
    return value_integer(::cw::ppis(::cw::lib::Value(value)), value);
}

void persist_selection_to_cwpy(const std::shared_ptr<::cw::sprite::base::SelectableSprite>& sprite,
                               bool synchronize_list)
{
    if (!::cw::g_cwpy) return;
    // Py と同様、change_selection より前にキーボード用の候補一
    // 覧を公開する。 フリックはその一覧を置き換えず、ポインターの選択を復元する。
    if (synchronize_list) ::cw::g_cwpy->m_list = c().selectable_list;
    ::cw::g_cwpy->m_index = c().index;
    ::cw::g_cwpy->change_selection(sprite, std::nullopt);
    ::cw::g_cwpy->m_wheelmode_cursorpos = ::cw::g_cwpy->m_mousepos;
}

void sync_base_context()
{
    if (auto* base = ::cw::sprite::base::runtime_context()) {
        base->index = c().index;
        base->mouse_pos = c().mouse_pos;
        base->pointed_tile = c().pointed_tile;
        base->running_event = running_event_active();
        base->dealing = c().dealing;
        base->selectable_list.clear();
        for (const auto& sprite : c().selectable_list) if (sprite) base->selectable_list.push_back(sprite.get());
        base->selection = c().selection.get();
    }
}

void set_selection(const std::shared_ptr<::cw::sprite::base::SelectableSprite>& sprite,
                   bool synchronize_list = false)
{
    c().selection = sprite;
    c().index = -1;
    for (std::size_t i = 0; i < c().selectable_list.size(); ++i) {
        if (c().selectable_list[i] == sprite) { c().index = static_cast<::cw::lib::Integer>(i); break; }
    }
    persist_selection_to_cwpy(sprite, synchronize_list);
    sync_base_context();
}

void register_sprite_group(::cw::sprite::base::CWPySprite& sprite, const ::cw::lib::Value& group)
{
    if (!sprite.m_ui_backend) sprite.m_ui_backend = ::cw::sprite::base::sprite_ui_backend();
    const auto object = value_object_ptr(group);
    if (!object || !sprite.m_ui_backend) return;
    sprite.m_ui_backend->register_sprite(sprite, {object});
}

void unregister_sprite(::cw::sprite::base::CWPySprite& sprite)
{
    if (!sprite.m_ui_backend) sprite.m_ui_backend = ::cw::sprite::base::sprite_ui_backend();
    if (sprite.m_ui_backend) sprite.m_ui_backend->unregister_sprite(sprite);
}

bool in_special_area()
{
    if (!::cw::g_cwpy) return false;
    const auto id = ::cw::g_cwpy->m_areaid;
    const auto& areas = ::cw::g_AREAS_SP;
    const auto match = [id](const ::cw::lib::Value& value) { return value_integer(value, -99999) == id; };
    return match(std::get<0>(areas)) || match(std::get<1>(areas)) || match(std::get<2>(areas)) ||
           match(std::get<3>(areas)) || match(std::get<4>(areas));
}

std::shared_ptr<::cw::data::SystemData> active_sdata()
{
    if (!::cw::g_cwpy) return {};
    if (::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata && (*::cw::g_cwpy->m_ydata)->m_losted_sdata &&
        *(*::cw::g_cwpy->m_ydata)->m_losted_sdata)
        return *(*::cw::g_cwpy->m_ydata)->m_losted_sdata;
    return value_object<::cw::data::SystemData>(::cw::g_cwpy->m_sdata);
}

std::shared_ptr<::cw::frame::Frame> current_game_frame()
{
    return ::cw::g_cwpy ? value_object<::cw::frame::Frame>(::cw::g_cwpy->m_frame) : nullptr;
}

bool pending_userevent()
{
    const bool queued_event = std::any_of(c().events.begin(), c().events.end(), [](const auto& object) {
        const auto event = std::dynamic_pointer_cast<InputEvent>(object);
        return event && event->type == EVENT_USEREVENT;
    });
    // Python の CWPy::exec_func は pygame.USEREVENT である。 C++
    // は c().events の外にコールバックを保持するが、can_input() の意味は同じに保つ。
    return queued_event || (::cw::g_cwpy && ::cw::g_cwpy->has_pending_game_commands(false));
}

::cw::lib::Integer backlog_height(const ::cw::lib::Value& value)
{
    if (const auto data = value_object<::cw::sprite::message::BacklogData>(value))
        return std::max<::cw::lib::Integer>(1, data->get_height_noscale());
    if (const auto bill = value_object<::cw::sprite::bill::Bill>(value))
        return std::max<::cw::lib::Integer>(1, bill->get_height_noscale());
    return std::max<::cw::lib::Integer>(1, c().visible_height / 3);
}

std::optional<::cw::lib::Value> create_backlog_view(const ::cw::lib::Value& value)
{
    if (const auto data = value_object<::cw::sprite::message::BacklogData>(value)) {
        auto message = data->create_message();
        if (value_object_ptr(message)) return message;
    }
    if (const auto bill = value_object<::cw::sprite::bill::Bill>(value)) {
        if (auto sprite = bill->create_message()) return ::cw::lib::Value(sprite);
    }
    return std::nullopt;
}

::cw::lib::Integer backlog_viewport_height()
{
    return std::get<1>(::cw::g_SIZE_AREA);
}

void unregister_backlog_view(const std::optional<::cw::lib::Value>& value)
{
    if (!value) return;
    if (const auto message = value_object<::cw::sprite::message::MessageWindow>(*value)) {
        unregister_sprite(*message);
        for (const auto& bar : message->m_selections)
            if (bar) unregister_sprite(*bar);
    } else if (const auto bill = value_object<::cw::sprite::bill::BillSprite>(*value)) {
        unregister_sprite(*bill);
    }
}

void position_and_register_backlog_view(const std::optional<::cw::lib::Value>& value,
                                        ::cw::lib::Integer y_noscale)
{
    if (!value || !::cw::g_cwpy) return;
    const auto& group = ::cw::g_cwpy->m_backloggrp;
    if (const auto message = value_object<::cw::sprite::message::MessageWindow>(*value)) {
        message->set_backlog_position(y_noscale);
        message->m_tlayer = ::cw::g_LAYER_LOG;
        register_sprite_group(*message, group);
        for (const auto& bar : message->m_selections) {
            if (!bar) continue;
            bar->m_tlayer = ::cw::g_LAYER_LOG_BAR;
            register_sprite_group(*bar, group);
        }
    } else if (const auto bill = value_object<::cw::sprite::bill::BillSprite>(*value)) {
        bill->set_backlog_position(y_noscale);
        bill->m_tlayer = ::cw::g_LAYER_LOG;
        register_sprite_group(*bill, group);
    }
}

std::shared_ptr<::cw::sprite::message::SelectionBar> statusbar_message_selection()
{
    if (!::cw::g_cwpy) return {};
    const auto group = value_object<::cw::lib::SpriteGroup>(::cw::g_cwpy->m_sbargrp);
    if (!group) return {};
    for (const auto& proxy : group->sprites) {
        auto* owner = ::cw::sprite::base::layered_group_sprite_owner(proxy);
        auto* raw = dynamic_cast<::cw::sprite::message::SelectionBar*>(owner);
        if (!raw || raw->m_backlog_typed) continue;
        return std::dynamic_pointer_cast<::cw::sprite::message::SelectionBar>(
            ::cw::sprite::message::find_owned_selectable(raw));
    }
    return {};
}

std::shared_ptr<::cw::sprite::base::SelectableSprite> selected()
{
    if (c().selection) return c().selection;
    if (c().index >= 0 && c().index < static_cast<::cw::lib::Integer>(c().selectable_list.size()))
        c().selection = c().selectable_list[static_cast<std::size_t>(c().index)];
    return c().selection;
}

bool contains(const ::cw::lib::Rect& rect, const ::cw::lib::Point& point)
{ return point.x >= rect.x && point.y >= rect.y && point.x < rect.x + rect.width && point.y < rect.y + rect.height; }

const char* selectable_kind(const std::shared_ptr<::cw::sprite::base::SelectableSprite>& sprite)
{
    if (!sprite) return "none";
    if (std::dynamic_pointer_cast<::cw::sprite::card::MenuCard>(sprite)) return "menu";
    if (std::dynamic_pointer_cast<::cw::character::Player>(sprite)) return "player";
    if (std::dynamic_pointer_cast<::cw::sprite::card::FriendCard>(sprite)) return "friend";
    return "other";
}

// Python の EventHandler と同様、物理マウスの押下状態だけでリピートクリ
// ックと判断しない。 KeyEventRelay::is_mousein() に問い合わせ
// 、CanRepeatLClick と設定された長押し閾値に従う。 Windows では対
// 応する MOUSEBUTTONDOWN がワーカーに届く前に Frame が WM_LBUTTONDOWN
// で cwpy.mousein を更新するため、この区別が必要である。
bool is_repeated_left_click()
{
    return ::cw::g_cwpy && ::cw::g_cwpy->m_keyevent &&
           ::cw::g_cwpy->m_keyevent->is_mousein(std::nullopt);
}

struct ModifierState {
    bool left_ctrl{}, right_ctrl{}, left_shift{}, right_shift{};
    bool control() const { return left_ctrl || right_ctrl; }
    bool shift() const { return left_shift || right_shift; }
};

ModifierState current_modifiers()
{
    // Python と同様、ハンドラーが記憶した KEYDOWN ビットではなく keyevent.keyin
    // を読む。 Relay のロック内でスナップショットを取り、リピート閾値は進め
    // ない。 フォーカス喪失やモーダルキャンセル時の clear() も反映する。
    if (::cw::g_cwpy && ::cw::g_cwpy->m_keyevent) {
        const auto pressed = ::cw::g_cwpy->m_keyevent->get_pressed();
        const auto down = [&](std::size_t key) {
            return key < pressed.size() && pressed[key] != 0;
        };
        return {down(306), down(305), down(304), down(303)};
    }
    // 実体の Engine がない独立したハンドラーでは、注入された状態を保持する。
    return {c().left_ctrl, c().right_ctrl, c().left_shift, c().right_shift};
}

bool is_repeated_key(::cw::lib::Integer key)
{
    // Context は OS のキーリピート回数ではなく送信状態を記録する
    // 。 Python の Relay の閾値と合成解除の動作を基準にする。
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_keyevent) return false;
    switch (key) {
    case KEY_LEFT: key = 276; break;
    case KEY_RIGHT: key = 275; break;
    case KEY_UP: key = 273; break;
    case KEY_DOWN: key = 274; break;
    case KEY_PAGEUP: key = 280; break;
    case KEY_PAGEDOWN: key = 281; break;
    default: break;
    }
    return ::cw::g_cwpy->m_keyevent->is_keyin(key);
}

::cw::lib::Rect sprite_rect(const ::cw::sprite::base::CWPySprite* sprite)
{
    return sprite ? sprite->m_rect : ::cw::lib::Rect{};
}

bool locked(const std::shared_ptr<::cw::sprite::base::SelectableSprite>& sprite)
{
    if (!sprite) return false;
    if (::cw::g_cwpy) return ::cw::g_cwpy->is_lockmenucards(sprite);
    if (c().lock_menu_cards) return true;
    if (auto* base = ::cw::sprite::base::runtime_context(); base && base->lock_menu_cards)
        return base->lock_menu_cards(sprite.get());
    return false;
}

bool selectable_on_event(const std::shared_ptr<::cw::sprite::base::SelectableSprite>& sprite)
{
    return sprite && (sprite->m_selectable_on_event ||
                      (!::cw::g_cwpy && c().selectable_on_event));
}

bool menu_input_locked(
    const std::optional<std::shared_ptr<::cw::sprite::base::SelectableSprite>>& sprite)
{
    if (::cw::g_cwpy) return ::cw::g_cwpy->is_lockmenucards(sprite);
    if (c().lock_menu_cards) return true;
    if (sprite && *sprite) return locked(*sprite);
    return false;
}
bool blocked_for_event(const std::shared_ptr<::cw::sprite::base::SelectableSprite>& sprite)
{ return running_event_active() && !(sprite && sprite->m_is_statusctrl && selectable_on_event(sprite)); }

::cw::lib::Real now_ticks()
{
    return c().ticks ? c().ticks() : static_cast<::cw::lib::Real>(
        std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count() * 1000.0);
}

std::shared_ptr<InputEvent> input_event(const std::shared_ptr<::cw::lib::Object>& object)
{ return std::dynamic_pointer_cast<InputEvent>(object); }

void clear_mouse_state(const std::shared_ptr<InputEvent>& event)
{
    if (!event) return;
    c().mouse_pos = event->pos;
    // ignore_up はタッチ・メニューの合成イベント用である。 Python
    // と同様、ネイティブの解除が来るまでは物理ボタンの押下を維持する。
    if (!event->ignore_up || event->type != EVENT_MOUSEBUTTONUP) {
        if (event->button == 1) {
            c().mouse_left = event->type == EVENT_MOUSEBUTTONDOWN;
            if (event->type == EVENT_MOUSEBUTTONDOWN)
                c().current_down_time = event->timestamp > 0.0 ? event->timestamp : now_ticks();
            else if (event->type == EVENT_MOUSEBUTTONUP)
                c().current_down_time.reset();
        }
        if (event->button == 2) c().mouse_middle = event->type == EVENT_MOUSEBUTTONDOWN;
        if (event->button == 3) c().mouse_right = event->type == EVENT_MOUSEBUTTONDOWN;
        // 単体 SDL には Frame のボタンコールバックがないため、 次フレー
        // ムのリピート判定が使う押下状態を公開する。合成解除では押下状態を維持する。
        if (::cw::g_cwpy && event->button >= 1 && event->button <= 3)
            ::cw::g_cwpy->m_mousein = {c().mouse_left, c().mouse_middle, c().mouse_right};
    }
}

void defer_event_exception(::cw::lib::ExceptionPtr& delayed, const std::shared_ptr<InputEvent>& event,
                           bool propagate_critical = false)
{
    try {
        if (event && event->func) event->func(event->args, event->kwargs);
        else if (c().execute_event) c().execute_event(event);
    } catch (const ::cw::event::XmlContentCriticalError&) {
        // Py と同様、通常の EventHandler は致命的な XML エラーで停止する。 メッセージ
        // ・バックログのハンドラーは、他の EventError とともに一括処理の終わりまで遅延させる。
        if (propagate_critical) throw;
        delayed = ::cw::lib::capture_exception();
    } catch (const ::cw::event::EventError&) {
        delayed = ::cw::lib::capture_exception();
    } catch (...) {
        throw;
    }
}

void dispatch_common_key(EventHandler& handler, ::cw::lib::Integer key)
{
    if (key == KEY_LEFT) { c().key_left = true; handler.dirkey_event(-1, 0, true, false, true); return; }
    if (key == KEY_RIGHT) { c().key_right = true; handler.dirkey_event(1, 0, true, false, true); return; }
    if (key == KEY_UP) { c().key_up = true; handler.dirkey_event(0, -1, true, false, true); return; }
    if (key == KEY_DOWN) { c().key_down = true; handler.dirkey_event(0, 1, true, false, true); return; }
    if (key == KEY_ESCAPE || key == KEY_BACKSPACE || key == KEY_BACKSLASH) { handler.escapekey_event(); return; }
    if (key == KEY_F1) { handler.f1key_event(); return; }
    if (key == KEY_F2) { handler.f2key_event(); return; }
    if (key == KEY_F3) { handler.f3key_event(); return; }
    if (key == KEY_F4) { handler.f4key_event(); return; }
    if (key == KEY_F5) { handler.f5key_event(); return; }
    if (key == KEY_F6) { handler.f6key_event(); return; }
    if (key == KEY_F7) { handler.f7key_event(); return; }
    if (key == KEY_F8) { handler.f8key_event(); return; }
    if (key == KEY_F9) { handler.f9key_event(); return; }
    handler.keydown_event(key);
}

void dispatch_common_keyup(EventHandler& handler, ::cw::lib::Integer key)
{
    handler.keyup_event(key);
    if (key == KEY_RETURN) handler.returnkey_event();
    else if (key == KEY_PRINT) handler.printkey_event();
}

void append_group(std::vector<std::shared_ptr<::cw::sprite::base::SelectableSprite>>& out,
                  const std::function<std::vector<std::shared_ptr<::cw::sprite::base::SelectableSprite>>()>& fn)
{
    if (fn) { auto group = fn(); out.insert(out.end(), group.begin(), group.end()); }
}

std::vector<std::shared_ptr<::cw::sprite::base::SelectableSprite>> top_clickable_group(
    ::cw::lib::Integer clickable_group)
{
    std::vector<std::shared_ptr<::cw::sprite::base::SelectableSprite>> out;
    if (!::cw::g_cwpy) return out;
    const auto group = value_object<::cw::lib::SpriteGroup>(::cw::g_cwpy->m_topgrp);
    if (!group) return out;
    // Py と同様、topgrp をグループ順に走査し、ClickableSprite・
    // グループだけを対象にする。 ネイティブのグループはプロキシーを持つため、非所有の
    // ポインター参照ではなく共通の所有者検索で Engine 所有のモデルを保持する。
    // 複数種のプロキシーが同じモデルを表しても、pygame と同様に一つだけ扱う。
    std::unordered_set<::cw::sprite::base::SelectableSprite*> seen;
    for (const auto& proxy : group->sprites) {
        auto* model = ::cw::sprite::base::layered_group_sprite_owner(proxy);
        if (!model) model = ::cw::sprite::background::background_layer_sprite_owner(proxy);
        auto* sprite = dynamic_cast<::cw::sprite::background::ClickableSprite*>(model);
        if (!sprite || sprite->m_clickable_group != clickable_group) continue;
        if (auto owner = ::cw::sprite::base::find_owned_selectable(sprite);
            owner && seen.insert(owner.get()).second)
            out.push_back(std::move(owner));
    }
    return out;
}

void update_backlog_page(const std::shared_ptr<::cw::sprite::message::BacklogPage>& page,
                         ::cw::lib::Integer index, ::cw::lib::Integer maxpage)
{ if (page) page->update_page(index, maxpage); }

std::shared_ptr<::cw::sprite::message::SelectionBar> message_selection_bar(
    const std::shared_ptr<::cw::sprite::message::MessageWindow>& window,
    const std::shared_ptr<::cw::sprite::base::SelectableSprite>& sprite)
{
    if (!window || !sprite) return {};
    for (const auto& bar : window->m_selections)
        if (bar && bar.get() == sprite.get()) return bar;
    return {};
}

void configure_backend(EventHandlerRuntimeContext& context)
{
    if (!context.copy_to_clipboard) context.copy_to_clipboard = ::cw::lib::set_system_clipboard_text;
    if (!context.show_help) context.show_help = [] {
        if (const auto frame = ::cw::g_cwpy ? value_object<::cw::frame::Frame>(::cw::g_cwpy->m_frame) : nullptr)
            frame->exec_func([](const auto&, const auto&) {
                ::cw::util::show_help(); return ::cw::lib::Value{};
            });
    };
    if (!context.backend) {
        context.backend = create_default_eventhandler_backend();
        context.backend_callbacks_configured = false;
    }
    if (!context.backend) return;
    if (context.backend_callbacks_configured) return;
    auto* owner = &context;
    context.backend->set_event_sink([owner](std::shared_ptr<InputEvent> event) {
        if (!event) return;
        // ネイティブのイベント取得は UI・メインスレッドで動く。ゲームスレッ
        // ドの deque へ直接追加せず、 正規化したイベントを CWPy
        // のミューテックス付き受信箱へ送る。 ゲームスレッドが次の sync_context_from_cwpy
        // でローカルのキューへ移す。
        if (::cw::g_cwpy) {
            if (!event->modal_epoch_stamped) {
                event->modal_epoch =
                    ::cw::g_cwpy->m_modal_epoch.load(std::memory_order_acquire);
                event->modal_epoch_stamped = true;
            }
            if (::cw::g_cwpy->m_keyevent &&
                !::cw::g_cwpy->m_keyevent->apply_normalized_input(
                    event->type, event->key, event->button, event->modal_epoch)) return;
            // 単体 SDL はポインター座標を管理する。従来の当たり判定は
            // m_wxmousepos を読む CWPy::update_mousepos()
            // を使うため、 全ポインターイベントを予約前にその値へ反
            // 映する。 そうしないと Linux でクリックは届いて
            // も、後の判定が初期位置 (-1,-1) のままになる。
            const bool pointer_event =
                event->type == EVENT_NONE ||
                event->type == EVENT_MOUSEBUTTONDOWN ||
                event->type == EVENT_MOUSEBUTTONUP ||
                event->type == EVENT_POINTERCANCEL;
            if (pointer_event && event->pos.x >= 0 && event->pos.y >= 0)
                ::cw::g_cwpy->m_wxmousepos = {
                    ::cw::lib::Value(event->pos.x),
                    ::cw::lib::Value(event->pos.y)};
            ::cw::thread::post_pygameevent(std::move(event));
        } else {
            owner->events.emplace_back(std::move(event));
        }
    });
    context.backend->set_lazy_draw_callback([](const ::cw::lib::Rect& rect) {
        if (!::cw::g_cwpy) return;
        // add_lazydraw は CWPy::update() と同じスプライト
        // オブジェクトを使う。 ステータスバーのアダプターにも、正確な更新矩形を渡す。
        if (auto adapter = ::cw::sprite::statusbar::host_services(); adapter && adapter->invalidate)
            adapter->invalidate(rect);
        std::shared_ptr<::cw::lib::Object> target;
        if (::cw::g_cwpy->m_statusbar) target = ::cw::g_cwpy->m_statusbar;
        else if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_cardgrp.storage()))
            target = *object;
        if (target) ::cw::g_cwpy->add_lazydraw(std::move(target));
    });
    context.backend->set_remove_layer_callback([](::cw::lib::Integer layer) {
        if (auto adapter = ::cw::sprite::statusbar::host_services(); adapter && adapter->remove_layer)
            adapter->remove_layer(layer);
    });
    context.backend->set_expanddrawing_callback([](::cw::lib::Real scale) {
        if (::cw::g_cwpy && std::isfinite(scale) && scale > 0.0)
            ::cw::g_cwpy->update_scale(scale, true, false, true, std::nullopt, std::nullopt);
    });
    context.backend->set_kill_message_layers_callback([] {
        if (auto adapter = ::cw::sprite::statusbar::host_services(); adapter && adapter->remove_layer) {
            adapter->remove_layer(::cw::sprite::statusbar::g_LAYER_MESSAGE);
        }
    });
    context.backend->set_quit_callback([owner] {
        owner->endprocess = true;
        if (::cw::g_cwpy) ::cw::g_cwpy->quit();
    });

    // これらのコールバックは wx のフレーム・ステータスバー呼び出しに相当
    // する。 アプリケーションは実行前に任意のコールバックを置き換えられる。
    if (!context.clear_touchmenu) context.clear_touchmenu = [] {
        if (::cw::g_cwpy && ::cw::g_cwpy->m_statusbar) ::cw::g_cwpy->m_statusbar->hide_touchbuttons(true);
    };
    if (!context.hide_touchmenu) context.hide_touchmenu = [] {
        // Py の background_event と同様、タッチボタンを隠すだけでは再描画
        // を強制しない。 この規則は clear_touchmenu の入力後処理とは分ける。
        if (::cw::g_cwpy && ::cw::g_cwpy->m_statusbar)
            ::cw::g_cwpy->m_statusbar->hide_touchbuttons(false);
    };
    if (!context.clear_volume_bar) context.clear_volume_bar = [] {
        return ::cw::g_cwpy && ::cw::g_cwpy->m_statusbar && ::cw::g_cwpy->m_statusbar->clear_volumebar();
    };
    if (!context.refresh_statusbar) context.refresh_statusbar = [] {
        if (::cw::g_cwpy && ::cw::g_cwpy->m_statusbar)
            ::cw::g_cwpy->m_statusbar->change(!::cw::g_cwpy->is_runningevent(), false);
    };
    if (!context.toggle_debug) context.toggle_debug = [] {
        if (::cw::g_cwpy) ::cw::g_cwpy->set_debug(!::cw::g_cwpy->is_debugmode());
    };
    if (!context.toggle_expand) context.toggle_expand = [] {
        if (::cw::g_cwpy)
            ::cw::g_cwpy->set_expanded(!::cw::g_cwpy->is_expanded(), ::cw::g_cwpy->m_expand_mode, false, std::nullopt);
    };
    if (!context.show_backlog) context.show_backlog = [] {
        if (::cw::g_cwpy) ::cw::g_cwpy->show_backlog(0);
    };
    // Python の capture_screenshot() と同様、Shift
    // 付きではパーティーのカード画像を保存する。 Print Screen・Ctrl+P
    // ・ALL のショートカットで保存するため、このコールバックを設定する。
    if (!context.capture_screenshot_callback) context.capture_screenshot_callback = [](const bool card) {
        try {
            if (card) ::cw::util::card_screenshot(); else ::cw::util::screenshot();
        } catch (const std::exception&) {
            // Py の capture_screenshot はユーティリティ外のエラ
            // ーを伝播する。保存失敗は共通ユーティリティが既に ERROR を出力・表
            // 示する。 この catch はその保存エラー通知ではなく、別の取得エラ
            // ーが制御処理の外へ出てもネイティブのゲームループを維持するために使う。
        }
    };
    if (!context.modal_action) context.modal_action = [](const ::cw::lib::String& name) {
        if (::cw::g_cwpy) ::cw::g_cwpy->call_modaldlg(name, {});
    };
    if (!context.play_sound) context.play_sound = [](const ::cw::lib::String& name) {
        if (::cw::g_cwpy) ::cw::g_cwpy->play_sound(name, false, 100, 1, 0, 0, false);
    };
    if (!context.set_area) context.set_area = [](const ::cw::lib::Integer area) {
        if (::cw::g_cwpy) ::cw::g_cwpy->change_specialarea(area, false);
    };
    if (!context.cancel_cardcontrol) context.cancel_cardcontrol = [] {
        if (::cw::g_cwpy) ::cw::g_cwpy->cancel_cardcontrol();
    };
    if (!context.clear_specialarea) context.clear_specialarea = [] {
        if (::cw::g_cwpy) ::cw::g_cwpy->clear_specialarea(true, false);
    };
    if (!context.change_specialarea) context.change_specialarea = [](const ::cw::lib::Integer area) {
        if (::cw::g_cwpy) ::cw::g_cwpy->change_specialarea(area, false);
    };
    if (!context.change_area) context.change_area = [](const ::cw::lib::Integer area,
                                                       std::tuple<::cw::lib::String, ::cw::lib::Value> transition) {
        if (::cw::g_cwpy)
            ::cw::g_cwpy->change_area(area, true, false, std::move(transition), false, false,
                                       false, true, std::nullopt, false, false, false, false, false);
    };
    if (!context.load_party) context.load_party = [] {
        // Python の background_event() と同様、load_party() の既定の chgarea=True
        // を使う。 パーティーを解除した後もエリア 2 を残すと、古い宿の UI が残ってしまう。
        if (!::cw::g_cwpy) return;
        const auto runtime = ::cw::g_cwpy;
        // Py と同様、その場でパーティーを解除せず、所有者に結び付いた load_party を予
        // 約する。 対象の同一性を保ちつつ、所有者を弱参照にしてネイティブキューの循環参照を避ける。
        const std::weak_ptr<::cw::thread::CWPy> target = runtime;
        runtime->exec_func([target](const ::cw::lib::Arguments&,
                                    const ::cw::lib::KeywordArguments&) {
            if (const auto owner = target.lock())
                owner->load_party(std::nullopt, true, false, true);
            return ::cw::lib::Value{};
        });
    };
    if (!context.call_package) context.call_package = [](const ::cw::lib::Integer resid) {
        try { return ::cw::content::call_package(resid, false); }
        catch (...) { return false; }
    };
    if (!context.area_exists) context.area_exists = [](const ::cw::lib::Integer resid) {
        if (!::cw::g_cwpy || !::cw::content::check_areaid(resid)) return false;
        const auto data = value_object<::cw::data::SystemData>(::cw::g_cwpy->m_sdata);
        return data && data->get_areaname(resid).has_value();
    };
    if (!context.deal_cards) context.deal_cards = [](const bool use_all_quickdeal) {
        if (!::cw::g_cwpy) return;
        const bool quickdeal = use_all_quickdeal && ::cw::g_cwpy->m_setting &&
                               ::cw::g_cwpy->m_setting->m_all_quickdeal;
        ::cw::g_cwpy->deal_cards(quickdeal, true, "", false, false);
    };
    if (!context.tick_clock) context.tick_clock = [] { tick_clock_now(); };
    if (!context.get_mcards) context.get_mcards = [] {
        std::vector<std::shared_ptr<::cw::sprite::base::SelectableSprite>> out;
        if (!::cw::g_cwpy || !::cw::g_cwpy->m_is_mcardsselectable) return out;
        for (const auto& value : ::cw::g_cwpy->get_mcards(::cw::lib::Value("selectable"), {}))
            if (const auto sprite = value_object<::cw::sprite::base::SelectableSprite>(value)) out.push_back(sprite);
        return out;
    };
    if (!context.get_pcards) context.get_pcards = [] {
        std::vector<std::shared_ptr<::cw::sprite::base::SelectableSprite>> out;
        if (!::cw::g_cwpy || !::cw::g_cwpy->m_is_pcardsselectable) return out;
        for (const auto& card : ::cw::g_cwpy->get_pcards(::cw::lib::Value("selectable")))
            if (card) out.push_back(card);
        return out;
    };
    if (!context.get_etc) context.get_etc = [] { return top_clickable_group(0); };
    if (!context.get_etc2) context.get_etc2 = [] { return top_clickable_group(1); };
    context.backend_callbacks_configured = true;
}

void configure_backend_safely(EventHandlerRuntimeContext& context)
{
    std::lock_guard<std::mutex> lock(g_backend_dispatch_mutex);
    configure_backend(context);
}

void poll_backend_safely()
{
    // configure_backend_safely() がイベント取得前に不変のコ
    // ールバック群を設定する。 QUIT のコールバックがゲームスレッドの終了を同期的
    // に待つ場合があるため、設定ミューテックスを保持したままバックエンドを呼ばない。
    if (c().backend) c().backend->poll_events();
}

void sync_context_from_cwpy()
{
    if (!::cw::g_cwpy) return;
    // Frame::WndProc は UI スレッドで型付き InputEvent
    // を CWPy のミューテックス付き互換キューへ入れる
    // 。 処理前にゲームスレッドのハンドラーキューへ移し、入
    // 力以外の独自イベントは CWPy 自身の処理用に残す。
    {
        std::scoped_lock lock(::cw::g_cwpy->m_events_mutex);
        auto& pending = ::cw::g_cwpy->m_events;
        for (auto it = pending.begin(); it != pending.end();) {
            if (const auto input = std::dynamic_pointer_cast<InputEvent>(*it)) {
                c().events.emplace_back(input);
                it = pending.erase(it);
            } else {
                ++it;
            }
        }
    }
    c().is_decompressing = ::cw::g_cwpy->m_is_decompressing;
    // Python は ShowModal の復帰と後処理を同じスレッドで
    // 行うため、その間にゲームのホバー・クリック入力は届かない。 C++
    // では同等の入力ロック状態に、明示的な終了処理の完了待ちを含める。
    c().showing_dialog = ::cw::g_cwpy->is_modal_input_blocked();
    c().running_event = ::cw::g_cwpy->is_runningevent();
    c().dealing = ::cw::g_cwpy->m__dealing;
    c().showing_backlog = ::cw::g_cwpy->is_showingbacklog();
    c().showing_message = ::cw::g_cwpy->is_showingmessage();
    c().playing_scenario = ::cw::g_cwpy->is_playingscenario();
    c().debug_mode = ::cw::g_cwpy->is_debugmode();
    c().processing = ::cw::g_cwpy->m_is_processing;
    c().battle = ::cw::g_cwpy->is_battlestatus();
    c().battle_ready = c().battle && ::cw::g_cwpy->m_battle && *::cw::g_cwpy->m_battle &&
                       (*::cw::g_cwpy->m_battle)->is_ready();
    c().endprocess = !::cw::g_cwpy->m__running;
    c().expanded = ::cw::g_cwpy->is_expanded();
    c().fullscreen = c().expanded && ::cw::g_cwpy->m_expand_mode == "FullScreen";
    c().areaid = ::cw::g_cwpy->m_areaid;
    c().index = ::cw::g_cwpy->m_index;
    c().status = ::cw::g_cwpy->m_status;
    c().scenario = c().status == "Scenario" || c().status == "ScenarioBattle";
    c().yado = c().status == "Yado";
    c().camp = c().areaid == value_integer(::cw::g_AREA_CAMP, -4);
    c().curtained = ::cw::g_cwpy->is_curtained();
    c().selected_header = ::cw::g_cwpy->m_selectedheader.has_value() &&
                          static_cast<bool>(*::cw::g_cwpy->m_selectedheader);
    c().wait_showcards = ::cw::g_cwpy->m_wait_showcards;
    c().lock_menu_cards = ::cw::g_cwpy->m_lock_menucards;
    c().pcards_selectable = ::cw::g_cwpy->m_is_pcardsselectable;
    c().mcards_selectable = ::cw::g_cwpy->m_is_mcardsselectable;
    c().statusbar_showbuttons = ::cw::g_cwpy->m_statusbar && ::cw::g_cwpy->m_statusbar->m_showbuttons;
    if (const auto setting = ::cw::g_cwpy->m_setting) {
        c().enabled_right_flick = setting->m_enabled_right_flick;
        c().flick_distance = std::max<::cw::lib::Integer>(0, setting->m_flick_distance);
        c().flick_time_msec = std::max<::cw::lib::Integer>(0, setting->m_flick_time_msec);
        c().autoenter_on_sprite = setting->m_autoenter_on_sprite;
        c().can_skipwait_with_wheel = setting->m_can_skipwait_with_wheel;
        c().can_forward_message_with_wheel = setting->m_can_forwardmessage_with_wheel;
        c().wheel_show_log = setting->m_wheelup_operation == ::cw::setting::g_WHEEL_SHOWLOG;
        c().volume_increment = setting->m_volume_increment;
        c().expanddrawing = setting->m_expanddrawing;
    }
    if (const auto data = value_object<::cw::data::SystemData>(::cw::g_cwpy->m_sdata)) {
        c().has_backlog = !data->m_backlog.empty();
        // BackgroundActionはXMLのProperty
        // 下の要素として保存される。解析した操作と
        // 画面切り替え効果を実行状態に保持する。
        if (data->m_data && *data->m_data) {
            const auto action = (*data->m_data)->find("Property/BackgroundAction");
            if (action && *action) {
                c().background_action_kind = (*action)->m_text;
                c().background_action_id = 0;
                c().background_action_id = (*action)->getint(".", "id", 0);
                c().background_action_transition =
                    ::cw::content::EventContentBase::get_transitiontype_static(*action);
            } else {
                c().background_action_kind.clear();
                c().background_action_id = 0;
                c().background_action_transition = {::cw::lib::String{}, ::cw::lib::Value{}};
            }
        } else {
            c().background_action_kind.clear();
            c().background_action_id = 0;
            c().background_action_transition = {::cw::lib::String{}, ::cw::lib::Value{}};
        }
    }
    c().mouse_left = std::get<0>(::cw::g_cwpy->m_mousein);
    c().mouse_middle = std::get<1>(::cw::g_cwpy->m_mousein);
    c().mouse_right = std::get<2>(::cw::g_cwpy->m_mousein);
    c().mouse_pos = {value_integer(std::get<0>(::cw::g_cwpy->m_mousepos), -1),
                     value_integer(std::get<1>(::cw::g_cwpy->m_mousepos), -1)};
    // Python と同様、選択の唯一の管理元を CWPy とする。 空
    // の状態も伝え、前フレームで破棄された SelectionBar が
    // ハンドラーのコンテキスト経由で選択可能なまま残らないようにする。
    if (::cw::g_cwpy->m_selection && *::cw::g_cwpy->m_selection)
        c().selection = *::cw::g_cwpy->m_selection;
    else
        c().selection.reset();
    sync_base_context();
}
} // namespace

void poll_input_events()
{
    configure_backend_safely(c());
    poll_backend_safely();
}

std::shared_ptr<EventHandlerBackend> create_sdl_eventhandler_backend()
{ return std::make_shared<SdlEventHandlerBackend>(); }

std::shared_ptr<EventHandlerBackend> create_windows_eventhandler_backend()
{
#if defined(_WIN32)
    return std::make_shared<WindowsEventHandlerBackend>();
#else
    return create_sdl_eventhandler_backend();
#endif
}

std::shared_ptr<EventHandlerBackend> create_default_eventhandler_backend()
{
#if defined(_WIN32)
    return create_windows_eventhandler_backend();
#else
    return create_sdl_eventhandler_backend();
#endif
}

void set_runtime_context(EventHandlerRuntimeContext* arg_context)
{
    g_context = arg_context ? arg_context : &g_default_context;
    {
        std::lock_guard<std::mutex> lock(g_backend_dispatch_mutex);
        g_context->backend_callbacks_configured = false;
        configure_backend(*g_context);
    }
    sync_context_from_cwpy();
    sync_base_context();
}
EventHandlerRuntimeContext* runtime_context() { return g_context; }

void cancel_runtime_input_sequence()
{
    auto& context = c();
    context.events.erase(
        std::remove_if(context.events.begin(), context.events.end(),
                       [](const auto& object) {
                           const auto event =
                               std::dynamic_pointer_cast<InputEvent>(object);
                           return event && is_pointer_or_key_event(*event);
                       }),
        context.events.end());
    reset_runtime_pointer_state(context);
    context.key_return = context.key_left = context.key_right = false;
    context.key_up = context.key_down = context.key_pageup = context.key_pagedown = false;
    context.left_ctrl = context.right_ctrl = context.left_shift = context.right_shift = false;
}

void EventHandler::native_loop_step()
{
    ::cw::lib::RuntimeAccess runtime_access;
    // この入口は所有する UI スレッドの Frame::main から呼ばれる。
    // ゲーム側のコンテキストの同期・実行は意図的に行わず、上のバックエンドの受け
    // 取り処理で CWPy の受信箱へ InputEvent を入れるだけとする。
    configure_backend_safely(c());
    if (c().backend) c().backend->native_loop_step();
}

void EventHandler::run()
{
    configure_backend_safely(c());
    sync_context_from_cwpy();
    discard_cancelled_pointer_events();
    set_has_inputevent(false);
    process_long_press();
    // pygame と同様、押し続けたキーのリピートはイベントキューを処理する前に行う。
    if (is_repeated_key(KEY_RETURN) && c().autoenter_on_sprite) { returnkey_event(); tick_clock_now(); }
    else if (is_repeated_key(KEY_LEFT)) { dirkey_event(-1, 0, true, false, true); tick_clock_now(); }
    else if (is_repeated_key(KEY_RIGHT)) { dirkey_event(1, 0, true, false, true); tick_clock_now(); }
    else if (is_repeated_left_click() && c().autoenter_on_sprite) { returnkey_event(); tick_clock_now(); }
    discard_cancelled_pointer_events();
    ::cw::lib::ExceptionPtr delayed;
    while (!c().events.empty()) {
        auto event = std::dynamic_pointer_cast<InputEvent>(c().events.front()); c().events.pop_front();
        PointerDispatchScope pointer_scope(event);
        if (!event || !check_puressedbutton(event)) continue;
        try {
            switch (event->type) {
            case EVENT_KEYDOWN: dispatch_common_key(*this, event->key); break;
            case EVENT_KEYUP: dispatch_common_keyup(*this, event->key); break;
            case EVENT_MOUSEBUTTONDOWN: if (event->button == 1) ldown_event(); break;
            case EVENT_MOUSEBUTTONUP:
                if (event->button == 1) lclick_event(); else if (event->button == 3) rclick_event(event->flick);
                else if (event->button == 4) wheel_event(-1); else if (event->button == 5) wheel_event(1); break;
            case EVENT_USEREVENT: case EVENT_FORCE_USEREVENT: defer_event_exception(delayed, event, true); break;
            default: break;
            }
        } catch (const ::cw::event::XmlContentCriticalError&) {
            throw;
        } catch (const ::cw::event::EventError&) {
            delayed = ::cw::lib::capture_exception();
        } catch (...) {
            throw;
        }
    }
    if (delayed) ::cw::lib::rethrow_exception(delayed);
}

void EventHandler::clear_touchmenu()
{
    if (c().pointed_tile) return;
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_statusbar) return;
    const auto sel = selected();
    if (std::dynamic_pointer_cast<::cw::sprite::touchbutton::TouchButton>(sel)) return;
    if (::cw::g_cwpy->m_statusbar->m_touchmenu && *::cw::g_cwpy->m_statusbar->m_touchmenu &&
        sel && sel.get() == (*::cw::g_cwpy->m_statusbar->m_touchmenu).get())
        return;
    ::cw::g_cwpy->m_statusbar->hide_touchbuttons(true);
}

void EventHandler::cancel_pointer_sequence()
{
    // ここではゲームスレッドの状態だけを扱う。Frame はネイティブ Relay のキャ
    // プチャーを解除済みであり、 再び消すとキューにある新しい押下まで消す可能性がある。
    reset_runtime_pointer_state(c());
}

void EventHandler::discard_cancelled_pointer_events()
{
    // キャンセルの前処理は通常のイベント検証より先に行う。 古
    // い印で新しい世代の押下・長押しタイマーを取り消さない。
    auto& queue = c().events;
    queue.erase(std::remove_if(queue.begin(), queue.end(), [](const auto& object) {
        const auto event = std::dynamic_pointer_cast<InputEvent>(object);
        return event && is_pointer_or_key_event(*event) && stale_input_generation(*event);
    }), queue.end());
    // 押し続けたボタンのタイマーが動く前にキャンセルする。 破棄するのは
    // 未完了の操作だけとし、完了済みのタップ・キー・独自処理は維持する。
    if (std::none_of(c().events.begin(), c().events.end(), [](const auto& object) {
        const auto event = std::dynamic_pointer_cast<InputEvent>(object);
        return event && event->type == EVENT_POINTERCANCEL;
    })) return;
    std::vector<bool> drop(c().events.size(), false);
    std::vector<std::size_t> pending[3], motion;
    std::size_t index = 0;
    bool cancelled = false;
    for (const auto& object : c().events) {
        const auto event = std::dynamic_pointer_cast<InputEvent>(object);
        if (event) {
            if (event->type == EVENT_POINTERCANCEL) {
                cancelled = true; drop[index] = true;
                for (auto& presses : pending) {
                    for (const auto i : presses) drop[i] = true;
                    presses.clear();
                }
                for (const auto i : motion) drop[i] = true;
                motion.clear();
            } else if (event->type == EVENT_NONE) {
                motion.push_back(index);
            } else if (event->button >= 1 && event->button <= 3) {
                auto& presses = pending[event->button - 1];
                if (event->type == EVENT_MOUSEBUTTONDOWN ||
                    (event->type == EVENT_MOUSEBUTTONUP && event->ignore_up)) presses.push_back(index);
                else if (event->type == EVENT_MOUSEBUTTONUP && !event->ignore_up) {
                    presses.clear();
                    motion.clear();
                }
            }
        }
        ++index;
    }
    if (!cancelled) return;
    cancel_pointer_sequence();
    index = 0;
    auto& events = c().events;
    events.erase(std::remove_if(events.begin(), events.end(), [&](const auto&) {
        return drop[index++];
    }), events.end());
}

bool EventHandler::check_puressedbutton(std::shared_ptr<::cw::lib::Object> arg_event)
{
    auto event = input_event(arg_event); if (!event) return true;
    // 拒否した古い解除・キャンセルは、現在の操作を所有していない。
    // それを破棄する副作用で、現在のポインター状態をリセットしない。
    if (is_pointer_or_key_event(*event) && stale_input_generation(*event)) return false;
    // wx のネイティブモーダルと同様、完全に戻るまでメインウ
    // ィンドウの入力を拒否する。 モーダルを開く前に転送済み
    // のイベントも含め、押下状態や選択を変える前に拒否する。
    if (is_pointer_or_key_event(*event) && dialog_input_blocked()) {
#if defined(CARDWIRTH_LOG_ENABLED)
        if (::cw::g_cwpy && ::cw::g_cwpy->m_areaid == -4) {
            ::cardwirth::debug::log(
                "Camp input event rejected before dispatch: type=" + std::to_string(event->type) +
                " dialog=" + (dialog_input_blocked() ? "1" : "0") +
                " stale_modal_epoch=" +
                    ((event->modal_epoch_stamped && event->modal_epoch !=
                      ::cw::g_cwpy->m_modal_epoch.load(std::memory_order_acquire)) ? "1" : "0"));
        }
#endif
        cancel_pointer_sequence();
        return false;
    }
    if (event->type == EVENT_POINTERCANCEL) { cancel_pointer_sequence(); return false; }
    c().wheel_mode_x = c().wheel_mode_y = -1;
    // Py と同様、受け付けたイベントの処理前に Engine のホイール起点を消去する。
    // CWPy::input が共通値を読むため、Context だけの消去では足りない。
    if (::cw::g_cwpy)
        ::cw::g_cwpy->m_wheelmode_cursorpos = {
            ::cw::lib::Value(-1), ::cw::lib::Value(-1)};
    const auto stamp = event->timestamp > 0.0 ? event->timestamp : now_ticks();
    clear_mouse_state(event);
    if (event->type != EVENT_USEREVENT && event->type != EVENT_FORCE_USEREVENT) clear_touchmenu();
    if (::cw::g_cwpy &&
        (event->type == EVENT_NONE ||
         event->type == EVENT_MOUSEBUTTONDOWN ||
         event->type == EVENT_MOUSEBUTTONUP)) {
        // UI の受信箱にある最新のポインター位置ではなく、このイベントの座標を変換す
        // る。 予約済みの移動・押下・解除がまとめて取り出される場合があるためである。
        auto& runtime = *::cw::g_cwpy;
        PointerDispatchScope pointer_scope(event);
        runtime.update_mousepos();
        c().mouse_pos = {
            value_integer(std::get<0>(runtime.m_mousepos), -1),
            value_integer(std::get<1>(runtime.m_mousepos), -1)};
    }
    // タッチ開始・解除時に、長押し処理済みフラグを消す。
    if (event->button == 1 &&
        (event->type == EVENT_MOUSEBUTTONDOWN || event->type == EVENT_MOUSEBUTTONUP))
        g_long_press_handled = false;
    if (event->button == 1) {
        if (event->type == EVENT_MOUSEBUTTONDOWN) {
            c().long_press_started = true;
            c().long_press_fired = false;
            // 操作はゲーム座標で保持する。ネイティブのイベント位置は表示領域のピク
            // セルであり、 2 倍表示では cwpy.mousepos と異なるた
            // め、混在させると即座に 600px ドラッグしたように見えてしまう。
            c().long_press_start_pos = c().mouse_pos;
            c().long_press_start_time = stamp;
            c().long_press_fired_time = 0.0;
            c().long_press_selection = selected();
        } else if (event->type == EVENT_MOUSEBUTTONUP && c().long_press_started) {
            // rclick_event からモーダルを開くと実行コンテキストがリセット
            // される場合があるが、 この物理的な解除は引き続き変換済みの操作に属する。
            bool consume_release = c().long_press_fired;
            // 押している間にプラットフォームがフレームを処理しなか
            // った場合、最後の代替として解除時に操作を認識する。
            if (!c().long_press_fired &&
                stamp - c().long_press_start_time >= 650.0 &&
                within_long_press_distance(c().mouse_pos, c().long_press_start_pos, scaled_ppi(12))) {
                c().long_press_fired = true;
                c().long_press_fired_time = stamp;
                consume_release = true;
                dispatch_long_press();
            }
            c().long_press_started = false;
            c().long_press_selection.reset();
            if (consume_release) {
                c().flick_started = false;
                c().flick_selection.reset();
                if (::cw::g_cwpy && ::cw::g_cwpy->m_keyevent) {
                    auto& key = *::cw::g_cwpy->m_keyevent;
                    key.m_flick_status = ::cw::lib::Value(::cw::frame::g_FLICK_NONE);
                    key.m_flick_sprite.reset();
                    key.m_flick_start_pos = {::cw::lib::Value(-1), ::cw::lib::Value(-1)};
                    key.m_flick_start_time = 0.0;
                }
                if (::cw::g_cwpy && ::cw::g_cwpy->m_keyevent)
                    ::cw::g_cwpy->m_keyevent->suppress_mouse_repeat(1);
                event->ignore_up = true;
                return false;
            }
        }
    }
    // pygame の右フリックと同様、ネイティブの押下・解除
    // 時刻から判定する。 両イベントの間のカーソル更新で右ク
    // リックの対象が変わらないよう、押下時の選択を保存する。
    if (c().enabled_right_flick && event->button == 1) {
        const auto native_pos = event->pos;
        auto pos = ::cw::g_cwpy
            ? ::cw::g_cwpy->client_to_screen_point(native_pos)
            : native_pos;
        const auto scaled_distance = scaled_ppi(c().flick_distance);
#if CW_EVENTHANDLER_HAS_SDL3
        if (!c().fullscreen && !SDL_GetMouseFocus()) {
            const auto edge = value_integer(::cw::scr2mwin_s(::cw::s(::cw::lib::Value(std::get<0>(::cw::g_SIZE_AREA)))), 0) - 1;
            if (native_pos.x == edge) pos.x = c().flick_start_pos.x + scaled_distance;
        }
#endif
        if (event->type == EVENT_MOUSEBUTTONDOWN) {
            c().flick_started = true;
            c().flick_start_pos = pos;
            c().flick_start_time = stamp;
            c().flick_selection = selected();
            if (::cw::g_cwpy && ::cw::g_cwpy->m_keyevent) {
                auto& key = *::cw::g_cwpy->m_keyevent;
                key.m_flick_status = ::cw::lib::Value(::cw::frame::g_FLICK_START);
                if (c().flick_selection) key.m_flick_sprite = c().flick_selection;
                else key.m_flick_sprite.reset();
                key.m_flick_start_pos = {::cw::lib::Value(pos.x), ::cw::lib::Value(pos.y)};
                key.m_flick_start_time = std::chrono::duration<double>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
            }
        } else if (event->type == EVENT_MOUSEBUTTONUP && c().flick_started) {
            const auto xmove = scaled_ppi(pos.x - c().flick_start_pos.x);
            const auto elapsed = std::max<::cw::lib::Real>(0.0, stamp - c().flick_start_time);
            event->flick = xmove >= scaled_distance && elapsed <= c().flick_time_msec;
            const auto flick_selection = c().flick_selection;
            c().flick_started = false;
            c().flick_selection.reset();
            if (::cw::g_cwpy && ::cw::g_cwpy->m_keyevent) {
                auto& key = *::cw::g_cwpy->m_keyevent;
                key.m_flick_status = ::cw::lib::Value(::cw::frame::g_FLICK_NONE);
                key.m_flick_sprite.reset();
                key.m_flick_start_pos = {::cw::lib::Value(-1), ::cw::lib::Value(-1)};
                key.m_flick_start_time = 0.0;
            }
            if (event->flick) {
                set_selection(flick_selection);
                if (::cw::g_cwpy) {
                    // flick_start_pos は既に共
                    // 通の描画座標なので、 ここで全画面や
                    // g_UP_* の変換を二度適用しない。
                    ::cw::g_cwpy->m_mousepos = {
                        ::cw::lib::Value(c().flick_start_pos.x),
                        ::cw::lib::Value(c().flick_start_pos.y)};
                    c().mouse_pos = {value_integer(std::get<0>(::cw::g_cwpy->m_mousepos), -1),
                                     value_integer(std::get<1>(::cw::g_cwpy->m_mousepos), -1)};
                }
                rclick_event(true);
                return false;
            }
        }
    }
    // Python の check_puressedbutton と同様、mousein に押下時刻を残し、is_mousein の連打判定に使う。
    if (::cw::g_cwpy && ::cw::g_cwpy->m_keyevent) {
        const auto button = event->button - 1;
        auto& mousein = ::cw::g_cwpy->m_keyevent->m_mousein;
        if (button >= 0 && button < static_cast<::cw::lib::Integer>(mousein.size())) {
            if (event->type == EVENT_MOUSEBUTTONDOWN)
                mousein[static_cast<std::size_t>(button)] = static_cast<::cw::lib::Integer>(stamp);
            else if (event->type == EVENT_MOUSEBUTTONUP && !event->ignore_up)
                mousein[static_cast<std::size_t>(button)] = 0;
        }
    }
    return true;
}

bool EventHandler::process_long_press()
{
    if (g_long_press_handled) return false;
    if (!c().long_press_started || c().long_press_fired || !c().mouse_left)
        return false;
    const auto now = now_ticks();
    if (now - c().long_press_start_time < 650.0) return false;
    // ドラッグ・フリックを長押しとして扱わない。 PPI に応じた小さな遊
    // び領域で通常の指の揺れを許容しつつ、スワイプを変換しないようにする。
    if (!within_long_press_distance(c().mouse_pos, c().long_press_start_pos, scaled_ppi(12))) {
        c().long_press_started = false;
        c().long_press_selection.reset();
        return false;
    }
    // 選択更新から Engine に再入する場合があるため、送信
    // 前に処理済みとする。 モーダル遷移中に同じ操作へ再入せず、
    // 長押しは左のリピートではなく一回の右クリックとして扱う。
    c().long_press_fired = true;
    c().long_press_fired_time = now;
    dispatch_long_press();
    return true;
}

void EventHandler::dispatch_long_press()
{
    g_long_press_handled = true;
    // 長押しはフリックではなく通常の副ボタンクリックとする。 コールバックや
    // アニメーションが入力を処理する前に、論理的な主ボタンの押下を解除する。
    c().mouse_left = false;
    if (::cw::g_cwpy) std::get<0>(::cw::g_cwpy->m_mousein) = false;
    c().current_down_time.reset();
    c().flick_started = false;
    c().flick_selection.reset();
    // 長押しは左クリックを右クリックに置き換えるため、左のリピート
    // を直ちに無効にする。 そうしないと約 667ms のタイマー
    // がバックログ終了直後に動き、露出した背景を操作してしまう。
    if (::cw::g_cwpy && ::cw::g_cwpy->m_keyevent)
        ::cw::g_cwpy->m_keyevent->suppress_mouse_repeat(1);
    rclick_event(false);
}

::cw::lib::Integer EventHandler::calc_index(::cw::lib::Integer arg_value)
{
    const auto size = static_cast<::cw::lib::Integer>(c().selectable_list.size());
    if (size <= 0) return -1;
    const auto previous = c().index;
    // Python は任意精度整数で加算してから端へ戻す。 端へ
    // 戻す規則は変えず、ネイティブの符号付き桁あふれを検出する。
    if (arg_value > 0 && previous > (std::numeric_limits<::cw::lib::Integer>::max)() - arg_value)
        return 0;
    if (arg_value < 0 && previous < (std::numeric_limits<::cw::lib::Integer>::min)() - arg_value)
        return size - 1;
    const auto index = previous + arg_value;
    if (index >= size) return 0;
    if (index < 0) return size - 1;
    return index;
}

bool EventHandler::can_input()
{
    // 展開中は入力可能とする。それ以外では、ダイアログ表
    // 示中またはUSEREVENT待機中の入力を防ぐ。
    if (decompression_active()) return true;
    if (dialog_input_blocked()) return false;
    // Python と同様、キュー内の USEREVENT だけを確認し、FORCE_USEREVENT は対象にしない。
    return !pending_userevent();
}
bool EventHandler::can_input_sys()
{
    return !dialog_input_blocked();
}
bool EventHandler::is_processing() { return processing_input_blocked(); }
bool EventHandler::can_copytext() { return false; }
void EventHandler::copy_text()
{
    // Py と同様、基底ハンドラーは何もしない。 コピーはテキストを持つハンドラーが実装する。
}

void EventHandler::dirkey_event(::cw::lib::Integer arg_x, ::cw::lib::Integer arg_y,
                                bool /*arg_pushing*/, bool arg_sidechange, bool arg_hidetouchmenu)
{
    if (dialog_input_blocked() || running_event_active() || is_processing() ||
        menu_input_locked(std::nullopt)) return;
    set_has_inputevent(true);
    if (arg_hidetouchmenu) {
        if (::cw::g_cwpy && ::cw::g_cwpy->m_statusbar) ::cw::g_cwpy->m_statusbar->hide_touchbuttons(true);
        else clear_touchmenu();
    }
    if (arg_sidechange && c().pcards_selectable && c().mcards_selectable && arg_x != 0) {
        if ((arg_x < 0 && c().index == 0) || (arg_x > 0 && c().index + 1 == static_cast<::cw::lib::Integer>(c().selectable_list.size()))) {
            arg_y = arg_x < 0 ? -1 : 1;
            arg_x = 0;
        }
    }
    if (arg_x != 0) {
        const auto idx = calc_index(arg_x); if (idx >= 0) set_selection(c().selectable_list[static_cast<std::size_t>(idx)], true); return;
    }
    if (!arg_y) return;
    const auto sel = selected();
    std::vector<std::function<std::vector<std::shared_ptr<::cw::sprite::base::SelectableSprite>>()>> funcs;
    if (!sel || std::dynamic_pointer_cast<::cw::sprite::background::Curtain>(sel)) {
        funcs = arg_y < 0 ? std::vector{c().get_etc2, c().get_pcards, c().get_etc, c().get_mcards}
                          : std::vector{c().get_mcards, c().get_etc, c().get_pcards, c().get_etc2};
    } else if (std::dynamic_pointer_cast<::cw::sprite::card::PlayerCard>(sel)) {
        funcs = arg_y < 0 ? std::vector{c().get_etc, c().get_mcards, c().get_etc2}
                          : std::vector{c().get_etc2, c().get_mcards, c().get_etc};
    } else if (const auto click = std::dynamic_pointer_cast<::cw::sprite::background::ClickableSprite>(sel)) {
        if (click->m_clickable_group == 0) {
            funcs = arg_y < 0 ? std::vector{c().get_mcards, c().get_etc2, c().get_pcards}
                              : std::vector{c().get_pcards, c().get_etc2, c().get_mcards};
        } else {
            funcs = arg_y < 0 ? std::vector{c().get_pcards, c().get_etc, c().get_mcards}
                              : std::vector{c().get_mcards, c().get_etc, c().get_pcards};
        }
    } else {
        funcs = arg_y < 0 ? std::vector{c().get_etc2, c().get_pcards, c().get_etc}
                          : std::vector{c().get_etc, c().get_pcards, c().get_etc2};
    }
    std::vector<std::shared_ptr<::cw::sprite::base::SelectableSprite>> choices;
    for (const auto& fn : funcs) { append_group(choices, fn); if (!choices.empty()) break; }
    if (choices.empty()) return;
    c().selectable_list = choices;
    const auto idx = arg_sidechange && arg_y < 0 ? static_cast<::cw::lib::Integer>(choices.size() - 1) : 0;
    set_selection(choices[static_cast<std::size_t>(idx)], true);
}

std::optional<std::shared_ptr<::cw::sprite::base::SelectableSprite>> EventHandler::_update_selection(bool arg_is_runningevent)
{
    // Python のマウスハンドラーと同様、当たり判定や Group.update より
    // 先に is_showingdlg() を確認する。 選択が変わった後には確認しない。
    if (dialog_input_blocked()) return std::nullopt;
    if (c().pointed_tile) return c().pointed_tile_selection;
    // 特にモーダル終了後など、ネイティブのクリックの前に移動イベントが来
    // るとは限らない。 明示的なポインターイベントでは当たり判定前にキー
    // ボード選択モードを抜け、キーボードによる実行では位置を維持する。
    if (::cw::g_cwpy && dispatch_pointer_position(::cw::g_cwpy.get()) &&
        !arg_is_runningevent && !is_processing()) {
        c().index = -1;
        ::cw::g_cwpy->m_index = -1;
        sync_base_context();
    }
    if (::cw::g_cwpy && c().index == -1 && !arg_is_runningevent && !is_processing()) {
        ::cw::g_cwpy->update_mousepos();
        std::vector<std::shared_ptr<::cw::lib::Object>> groups;
        if (const auto group = value_object_ptr(::cw::g_cwpy->m_cardgrp)) groups.push_back(group);
        if (const auto group = value_object_ptr(::cw::g_cwpy->m_topgrp)) groups.push_back(group);
        if (const auto group = value_object_ptr(::cw::g_cwpy->m_sbargrp)) groups.push_back(group);
        if (!groups.empty()) ::cw::g_cwpy->update_groups(groups);
        c().mouse_pos = {value_integer(std::get<0>(::cw::g_cwpy->m_mousepos), -1),
                         value_integer(std::get<1>(::cw::g_cwpy->m_mousepos), -1)};
        c().index = ::cw::g_cwpy->m_index;
        if (::cw::g_cwpy->m_selection && *::cw::g_cwpy->m_selection) c().selection = *::cw::g_cwpy->m_selection;
        else c().selection.reset();
        sync_base_context();
        return c().selection;
    }
    return selected();
}

void EventHandler::ldown_event()
{
    // Python は set_yado() から change_area() を同期的に
    // 完了し、宿のカードを半分だけ置き換えた状態へ入力が届かない。 ネイティブは change_area
    // を遅延するため、共通の入力境界でその期間を同じように不可分にする。
    if (dialog_input_blocked() || yado_transition_is_loading()) return;
    auto sprite = _update_selection(running_event_active()).value_or(nullptr);
    const bool event_blocked = blocked_for_event(sprite);
    const bool processing_blocked = is_processing();
    const bool menu_locked = sprite && locked(sprite);
#if defined(CARDWIRTH_LOG_ENABLED)
    if (::cw::g_cwpy && ::cw::g_cwpy->m_areaid == -4) {
        ::cardwirth::debug::log(
            "Camp left-down dispatch: selection=" + std::string(selectable_kind(sprite)) +
            " event_blocked=" + (event_blocked ? "1" : "0") +
            " processing=" + (processing_blocked ? "1" : "0") +
            " menu_locked=" + (menu_locked ? "1" : "0"));
    }
#endif
    if (event_blocked || processing_blocked) return;
    if (sprite && !menu_locked) { set_has_inputevent(true); sprite->ldown_event(); }
}
void EventHandler::lclick_event()
{
    if (dialog_input_blocked() || yado_transition_is_loading()) return;
    auto sprite = _update_selection(running_event_active()).value_or(nullptr);
    const bool event_blocked = blocked_for_event(sprite);
    const bool processing_blocked = is_processing();
    const bool menu_locked = sprite && locked(sprite);
#if defined(CARDWIRTH_LOG_ENABLED)
    if (::cw::g_cwpy && ::cw::g_cwpy->m_areaid == -4) {
        ::cardwirth::debug::log(
            "Camp left-click dispatch: selection=" + std::string(selectable_kind(sprite)) +
            " event_blocked=" + (event_blocked ? "1" : "0") +
            " processing=" + (processing_blocked ? "1" : "0") +
            " menu_locked=" + (menu_locked ? "1" : "0"));
    }
#endif
    if (event_blocked || processing_blocked) return;
    // 対象選択カーテンで覆われたカードは有効な対象ではなく、クリックしても何もしない。 SelectableSprite::update_selection
    // と同じ条件を使う。
    if (sprite && ::cw::g_cwpy && ::cw::g_cwpy->is_curtain_blocked(sprite.get())) return;
    // 選択可能な全スプライトは、同じ全体の入力条件を通る。 具体的なステータスボタン
    // は lclick_event() 内で自身の有効状態・操作だけを確認する。
    // ここでも確認すると別のタイミングの二つ目のロック経路ができるため、重ねない。
    if (sprite) {
        if (menu_locked) return;
        set_has_inputevent(true); sprite->lclick_event();
    }
    else if (c().wait_showcards && c().deal_cards) c().deal_cards(true);
}
void EventHandler::rclick_event(bool arg_flick)
{
    if (clear_volume_bar_on_right_click(arg_flick)) return;
    // Python と同じ音量バーを閉じる処理の後、この境界で C++
    // の非同期宿読み込み・ネイティブモーダル中のゲーム操作を拒否する。
    if (dialog_input_blocked() || yado_transition_is_loading()) return;
    auto sprite = arg_flick ? selected() : _update_selection(running_event_active()).value_or(nullptr);
    if (blocked_for_event(sprite) || is_processing()) return;
    if (sprite) {
        if (locked(sprite)) return;
        set_has_inputevent(true); sprite->rclick_event();
    }
    else if (::cw::g_cwpy &&
             ::cw::g_cwpy->m_background &&
             contains(sprite_rect(::cw::g_cwpy->m_background.get()), c().mouse_pos)) {
        if (menu_input_locked(std::nullopt)) return;
        background_event();
    }
    else if (c().wait_showcards && c().deal_cards) c().deal_cards(false);
}
void EventHandler::escapekey_event()
{
    // ネイティブの境界として、非同期の宿読み込み・ネイティブモーダル中は
    // Escape も防ぐ。 そのまま通すとゲーム操作を実行できてしまう。
    if (dialog_input_blocked() || yado_transition_is_loading() ||
        menu_input_locked(std::nullopt)) return;
    if (c().wait_showcards && c().deal_cards) c().deal_cards(false); else background_event();
}
void EventHandler::background_event()
{
    if (c().hide_touchmenu) c().hide_touchmenu(); else clear_touchmenu();
    if (::cw::g_cwpy && ::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata &&
        (*::cw::g_cwpy->m_ydata)->is_loading()) return;

    if (!running_event_active()) {
        if ((c().curtained && !c().camp || c().selected_header) && c().statusbar_showbuttons) {
            if (c().cancel_cardcontrol) c().cancel_cardcontrol();
            return;
        }
        if (c().battle && c().battle_ready) {
            if (c().play_sound) c().play_sound("click");
            if (c().modal_action) c().modal_action("BATTLECOMMAND");
            return;
        }
        if (c().scenario && !c().battle && !c().dealing) {
            set_has_inputevent(true);
            if (c().play_sound) c().play_sound("click");
            if (c().camp) {
                if (c().clear_specialarea) c().clear_specialarea();
            } else if (c().change_specialarea) {
                c().change_specialarea(-4);
            }
            return;
        }
        if (c().yado && c().areaid >= ::cw::g_SKIN_AREAS_MIN &&
            c().areaid <= ::cw::g_SKIN_AREAS_MAX) {
            const auto& action = c().background_action_kind;
            if (action == "ReturnTitle") {
                if (c().play_sound) c().play_sound("click");
                if (c().modal_action) c().modal_action("RETURNTITLE");
            } else if (action == "ReturnSystemArea") {
                if (c().play_sound) c().play_sound("click");
                const auto system_area = ::cw::g_cwpy && ::cw::g_cwpy->m_ydata &&
                                         *::cw::g_cwpy->m_ydata && (*::cw::g_cwpy->m_ydata)->m_party ? 2 : 1;
                if (c().change_area) c().change_area(system_area, c().background_action_transition);
            } else if (action == "Close") {
                if (c().play_sound) c().play_sound("click");
                if (c().modal_action) c().modal_action("CLOSE");
            } else if (action == "ChangeArea") {
                const auto resid = c().background_action_id;
                if (resid && c().area_exists && c().area_exists(resid)) {
                    if (c().change_area) c().change_area(resid, c().background_action_transition);
                }
            } else if (action == "CallPackage") {
                const auto resid = c().background_action_id;
                if (::cw::g_cwpy) {
                    ::cw::g_cwpy->exec_func([resid](const ::cw::lib::Arguments&, const ::cw::lib::KeywordArguments&) {
                        try {
                            if (!::cw::content::call_package(resid, false) && ::cw::g_cwpy)
                                ::cw::g_cwpy->play_sound("error", false, 100, 1, 0, 0, false);
                        } catch (const ::cw::event::EffectBreakError&) {
                            ::cw::util::print_ex(std::nullopt);
                        }
                        return ::cw::lib::Value{};
                    });
                }
            } else if (c().play_sound) {
                c().play_sound("error");
            }
            return;
        }
        if (c().yado && !c().dealing) {
            set_has_inputevent(true);
            if (c().play_sound) c().play_sound("click");
            if (c().areaid == 1 || c().areaid == 3) {
                if (c().modal_action) c().modal_action("RETURNTITLE");
            } else if (c().areaid == 2 && c().load_party) {
                c().load_party();
            }
            return;
        }
    }
    if (c().play_sound) c().play_sound("click");
    if (c().modal_action) c().modal_action("CLOSE");
}
void EventHandler::f1key_event()
{
    // ネイティブの Novel モードには Engine ヘルプがないため、その機能制限を維持する。
    if (::cw::novel::enabled()) return;
    if (c().show_help) c().show_help();
}
void EventHandler::f2key_event()
{
    // ネイティブの Novel モードには Engine 設定がないため、その機能制限を維持する。
    if (::cw::novel::enabled()) return;
    // Py と同様、ダイアログ状態だけを確認し、バックログの上にも設定を開く。
    // 共通のネイティブモーダル判定は二つのキューにまたがる終了・解除境界も含
    // むが、 保留中のユーザーイベントや展開処理は F2 の除外条件にしない。
    if (dialog_input_blocked()) return;
    set_has_inputevent(true);
    if (c().play_sound) c().play_sound("click");
    if (c().modal_action) c().modal_action("SETTINGS");
}
void EventHandler::f3key_event()
{
    if (!can_input_sys() || !c().debug_mode)
        return;
    if (c().play_sound) c().play_sound("page");
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_expandmode == "FullScreen")
        ::cw::g_cwpy->set_expanded(false, ::cw::g_cwpy->m_expand_mode, false, std::nullopt);
    const auto frame = current_game_frame();
    if (!frame) return;
    if (frame->m_debugger) {
        frame->exec_func([](const ::cw::lib::Arguments&, const ::cw::lib::KeywordArguments&) {
            if (const auto current = current_game_frame()) current->close_debugger();
            return ::cw::lib::Value{};
        });
    } else {
        if (::cw::g_cwpy && ::cw::g_cwpy->m_keyevent) ::cw::g_cwpy->m_keyevent->clear();
        frame->exec_func([](const ::cw::lib::Arguments&, const ::cw::lib::KeywordArguments&) {
            if (const auto current = current_game_frame()) current->show_debugger(true);
            return ::cw::lib::Value{};
        });
    }
}
void EventHandler::f4key_event()
{
    if (!can_input_sys())
        return;
    c().expanded = !c().expanded;
    if (c().toggle_expand) c().toggle_expand();
}
void EventHandler::f5key_event()
{
    if (!can_input_sys()) return;
    if (::cw::g_cwpy && ::cw::g_cwpy->is_showingbacklog()) {
        auto event = std::make_shared<InputEvent>();
        event->type = EVENT_KEYDOWN;
        event->key = ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->is_logscrollable() ? KEY_ESCAPE : KEY_UP;
        c().events.push_back(event);
        return;
    }
    if (c().has_backlog || (::cw::g_cwpy && value_object<::cw::data::SystemData>(::cw::g_cwpy->m_sdata) &&
                            !value_object<::cw::data::SystemData>(::cw::g_cwpy->m_sdata)->m_backlog.empty())) {
        if (c().play_sound) c().play_sound("page");
        if (c().show_backlog) c().show_backlog();
    }
}
void EventHandler::f6key_event()
{
    if (!can_input() || is_processing())
        return;
    if (!::cw::g_cwpy) return;
    if (::cw::g_cwpy->is_playingscenario()) {
        if (running_event_active()) return;
        const auto data = value_object<::cw::data::SystemData>(::cw::g_cwpy->m_sdata);
        if (!c().battle && data && data->has_infocards()) {
            if (c().play_sound) c().play_sound("click");
            ::cw::content::PostEventContent::do_action("ShowDialog", ::cw::lib::String("INFOVIEW"));
        } else if (c().battle && c().debug_mode && c().battle_ready && !::cw::g_cwpy->get_fcards({}).empty()) {
            if (c().play_sound) c().play_sound("page");
            if (::cw::g_cwpy->m_setting)
                ::cw::g_cwpy->m_setting->m_show_fcardsinbattle = !::cw::g_cwpy->m_setting->m_show_fcardsinbattle;
            if (::cw::g_cwpy->m_battle && *::cw::g_cwpy->m_battle) (*::cw::g_cwpy->m_battle)->update_showfcards();
            // ネイティブ宣言には既定引数がないため、Python の change() の既定値で
            // ある showbuttons=True、encounter=False を指定する。
            if (::cw::g_cwpy->m_statusbar)
                ::cw::g_cwpy->m_statusbar->change(true, false);
        }
        return;
    }
    const auto data = value_object<::cw::data::SystemData>(::cw::g_cwpy->m_sdata);
    if (::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_show_debuglogdialog && c().debug_mode &&
        data && data->m_debuglog && *data->m_debuglog) {
        if (c().play_sound) c().play_sound("click");
        const auto debuglog = *data->m_debuglog;
        if (const auto frame = value_object<::cw::frame::Frame>(::cw::g_cwpy->m_frame)) {
            // ネイティブのダイアログ HWND は Frame の Win32 UI スレッドに属する。
            // ゲーム・イベントスレッドから直接生成すると、ALL のホスト内メッセージループとの間で
            // EnableWindow/DestroyWindow がデッドロックする可能性がある。
            frame->exec_func_11(
                [frame, debuglog](const ::cw::lib::Arguments&,
                                  const ::cw::lib::KeywordArguments&) {
                    auto parent = std::make_shared<::cw::lib::Window>();
                    parent->native_handle = frame->m_native_window_handle;
                    auto dialog = std::make_shared<::cw::debug::logging::DebugLogDialog>();
                    dialog->initialize(parent, debuglog);
                    return ::cw::lib::Value{};
                }, {}, {});
        } else {
            auto dialog = std::make_shared<::cw::debug::logging::DebugLogDialog>();
            dialog->initialize(std::make_shared<::cw::lib::Window>(), debuglog);
        }
    }
}
void EventHandler::f7key_event()
{
    if (!can_input_sys())
        return;
    if (!(::cw::g_cwpy && ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_show_roundautostartbutton &&
          c().playing_scenario && c().battle))
        return;
    if (c().play_sound) c().play_sound("page");
    if (const auto data = value_object<::cw::data::SystemData>(::cw::g_cwpy->m_sdata))
        data->m_autostart_round = !data->m_autostart_round;
    if (::cw::g_cwpy->m_statusbar)
        ::cw::g_cwpy->m_statusbar->change(::cw::g_cwpy->m_statusbar->m_showbuttons, false);
}
void EventHandler::f8key_event()
{
    if (!can_input_sys())
        return;
    if (!c().playing_scenario && !(::cw::g_cwpy && ::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata &&
                                   (*::cw::g_cwpy->m_ydata)->m_losted_sdata && *(*::cw::g_cwpy->m_ydata)->m_losted_sdata))
        return;
    if (::cw::g_fsync) ::cw::g_fsync->sync();
    const auto data = active_sdata();
    if (data && !data->m_instructions.empty()) {
        if (c().modal_action) c().modal_action("INSTRUCTIONS");
    } else if (c().play_sound) {
        c().play_sound("error");
    }
}
void EventHandler::f9key_event()
{
    if (!can_input_sys())
        return;
    if (::cw::g_fsync) ::cw::g_fsync->sync();
    if (!can_f9()) return;
    set_has_inputevent(true);
    if (c().play_sound) c().play_sound("signal");
    if (c().show_f9) c().show_f9();
    else if (c().modal_action) c().modal_action("F9");
}
bool EventHandler::can_f9()
{
    if (::cw::novel::enabled()) return false;
    if (c().can_f9_callback) return c().can_f9_callback();
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_ydata || !*::cw::g_cwpy->m_ydata) return false;
    if (decompression_active()) return true;
    const auto yado = *::cw::g_cwpy->m_ydata;
    const bool playing = c().playing_scenario || (yado->m_losted_sdata && *yado->m_losted_sdata);
    const auto data = active_sdata();
    if (!playing || !data || data->m_in_endprocess || data->m_in_f9 ||
        dialog_input_blocked() || pending_userevent())
        return false;
    if (!yado->m_party || !*yado->m_party) return false;
    ::cw::lib::String fpath;
    if (const auto tree = value_object<::cw::data::CWPyElementTree>((*yado->m_party)->m_data)) fpath = tree->m_fpath;
    if (fpath.empty()) fpath = value_string((*yado->m_party)->m_path);
    if (fpath.empty()) return false;
    auto fname = fpath;
    if (const auto pos = fname.find_last_of("\\/"); pos != ::cw::lib::String::npos) fname = fname.substr(pos + 1);
    const auto path = ::cw::util::join_paths({
        ::cw::lib::Value(value_string(::cw::g_tempdir)),
        ::cw::lib::Value("ScenarioLog/Party"),
        ::cw::lib::Value(fname)});
    // Py と同様、ファイルのメタデータだけを確認する。 一時ログがマウント済み
    // Storage 内にある場合も、内容を読まず共通の stat 境界を使う。
    return ::cw::util::path_is_file(path);
}
void EventHandler::returnkey_event(bool /*arg_pushing*/)
{
    if (!can_input()) return; auto sprite = _update_selection(running_event_active()).value_or(nullptr);
    if ((running_event_active() || is_processing()) &&
        !(sprite && sprite->m_is_statusctrl && selectable_on_event(sprite))) return;
    if (sprite) {
        if (locked(sprite)) return;
        set_has_inputevent(true);
        // Python と同様、左 Ctrl が押され、かつ右 Ctrl が押されていない場合だけ右クリックとして扱う。
        const auto modifiers = current_modifiers();
        if (!modifiers.left_ctrl || modifiers.right_ctrl) sprite->lclick_event(); else sprite->rclick_event();
    }
    else if (c().wait_showcards && c().deal_cards) c().deal_cards(false);
}
void EventHandler::printkey_event() { if (can_input_sys()) capture_screenshot(); }
// Py の wx キー識別子は大文字 D/P（68/80）だが、ネイティブホスト
// は SDL のキーをこの共通ハンドラーの前に KEY_D/KEY_P（100/112
// ）へ変換する。 異なる文字キーではなく、同じ物理ショートカットを表す。
bool EventHandler::keydown_event(::cw::lib::Integer arg_key)
{
    if (!can_input_sys()) return false;
    if (current_modifiers().control() && arg_key == KEY_D) {
        if (::cw::novel::enabled()) return false;
        if (c().play_sound) c().play_sound("page");
        c().debug_mode = !c().debug_mode; if (c().toggle_debug) c().toggle_debug(); return false;
    }
    if (arg_key == KEY_RETURN) c().key_return = true; if (arg_key == KEY_LEFT) c().key_left = true; if (arg_key == KEY_RIGHT) c().key_right = true;
    if (arg_key == KEY_UP) c().key_up = true; if (arg_key == KEY_DOWN) c().key_down = true; if (arg_key == KEY_PAGEUP) c().key_pageup = true; if (arg_key == KEY_PAGEDOWN) c().key_pagedown = true;
    if (arg_key == KEY_LCTRL) c().left_ctrl = true; if (arg_key == KEY_RCTRL) c().right_ctrl = true;
    if (arg_key == KEY_LSHIFT) c().left_shift = true; if (arg_key == KEY_RSHIFT) c().right_shift = true; return true;
}
bool EventHandler::keyup_event(::cw::lib::Integer arg_key)
{
    if (!can_input_sys()) return false;
    if (current_modifiers().control() && arg_key == KEY_P) { capture_screenshot(); return false; }
    if (arg_key == KEY_RETURN) c().key_return = false; if (arg_key == KEY_LEFT) c().key_left = false; if (arg_key == KEY_RIGHT) c().key_right = false;
    if (arg_key == KEY_UP) c().key_up = false; if (arg_key == KEY_DOWN) c().key_down = false; if (arg_key == KEY_PAGEUP) c().key_pageup = false; if (arg_key == KEY_PAGEDOWN) c().key_pagedown = false;
    if (arg_key == KEY_LCTRL) c().left_ctrl = false; if (arg_key == KEY_RCTRL) c().right_ctrl = false;
    if (arg_key == KEY_LSHIFT) c().left_shift = false; if (arg_key == KEY_RSHIFT) c().right_shift = false; return true;
}
void EventHandler::capture_screenshot() { if (c().capture_screenshot_callback) c().capture_screenshot_callback(current_modifiers().shift()); }
bool EventHandler::change_volume(::cw::lib::Integer arg_val)
{
    if (!arg_val || !c().mouse_right || !::cw::g_cwpy) return false;
    if (::cw::g_cwpy->m_statusbar) ::cw::g_cwpy->m_statusbar->hide_touchbuttons(false);
    else clear_touchmenu();
    const auto increment = c().volume_increment;
    ::cw::lib::Integer volume = 0;
    for (const auto& music : ::cw::thread::CWPy::s_music) {
        if (!music) continue;
        volume = ::cw::util::numwrap(music->m_mastervolume_typed + arg_val * increment, 0, 100);
        music->set_mastervolume(volume);
        if (::cw::g_cwpy->m_setting) ::cw::g_cwpy->m_setting->m_vol_master = static_cast<::cw::lib::Real>(volume) / 100.0;
    }
    for (const auto& sound : ::cw::g_cwpy->m_lastsound_scenario) {
        if (!sound || !*sound) continue;
        const auto next = (*sound)->m_mastervolume_typed + arg_val * increment;
        (*sound)->set_mastervolume(true, next);
    }
    if (::cw::g_cwpy->m_lastsound_system && *::cw::g_cwpy->m_lastsound_system) {
        const auto next = (*::cw::g_cwpy->m_lastsound_system)->m_mastervolume_typed + arg_val * increment;
        (*::cw::g_cwpy->m_lastsound_system)->set_mastervolume(false, next);
    }
    if (::cw::g_cwpy->m_statusbar) ::cw::g_cwpy->m_statusbar->update_volumebar();
    return true;
}
void EventHandler::wheel_event(::cw::lib::Integer arg_y)
{
    if (!can_input_sys() || change_volume(-arg_y)) return; if (!can_input()) return;
    if (arg_y < 0 && c().wheel_show_log) f5key_event(); else dirkey_event(arg_y, 0, true, true, true);
}
void EventHandler::executing_event(std::shared_ptr<::cw::lib::Object> arg_event)
{
    set_has_inputevent(true); auto event = input_event(arg_event);
    if (event && event->func) event->func(event->args, event->kwargs); else if (c().execute_event) c().execute_event(arg_event);
}

void EventHandlerForBacklog::initialize(std::vector<::cw::lib::Value> arg_backlog, ::cw::lib::Integer arg_index)
{
    // 値を渡す呼び出し側は新しい一覧を所有し、Engine は元の一覧を initialize_shared_backlog
    // へ渡す。 どちらも同じ初期化・読み取り経路を使う。
    initialize_shared_backlog(
        std::make_shared<std::vector<::cw::lib::Value>>(std::move(arg_backlog)), arg_index);
}

void EventHandlerForBacklog::initialize_shared_backlog(
    std::shared_ptr<std::vector<::cw::lib::Value>> arg_backlog, ::cw::lib::Integer arg_index)
{
    if (!arg_backlog) throw std::invalid_argument("backlog list is null");
    // ログが前面にある間も、メッセージハンドラーの SelectionBars
    // は意図的に保持する。 バックログが引
    // き継ぐ前に共有一覧を保存し、再開したメ
    // ッセージのキーボード選択肢を消さない。
    m__saved_selectable_list = c().selectable_list;
    m__saved_index = c().index;
    auto list = std::make_shared<BacklogSequence>(); list->records = std::move(arg_backlog);
    m_backlog_all = ::cw::lib::Value(list); m_backlog = m_backlog_all; m_index = arg_index;
    m__scrollnum_noscale = backlog_viewport_height() / 4;
    m__space_noscale = 5;
    m__lock_menucards = ::cw::g_cwpy
        ? ::cw::lib::Value(::cw::g_cwpy->m_lock_menucards)
        : ::cw::lib::Value(c().lock_menu_cards);
    m__upscr = ::cw::lib::Value(::cw::g_UP_SCR);
    m__messagelog_type = (::cw::g_cwpy && ::cw::g_cwpy->m_setting)
        ? ::cw::lib::Value(::cw::g_cwpy->m_setting->m_messagelog_type)
        : ::cw::lib::Value{};
    c().showing_backlog = true;
    c().lock_menu_cards = false;
    if (::cw::g_cwpy) ::cw::g_cwpy->m_lock_menucards = false;
    m__in_scroll = false;
    m__swipe_active = false; m__swipe_dragged = false;
    m__swipe_touch = false; m__current_pointer_touch = false;
    m__swipe_start_y_noscale = 0; m__swipe_last_y_noscale = 0;
    if (::cw::g_cwpy) {
        ::cw::g_cwpy->clear_selection();
        // clear_selection() は選択スプライトがな
        // い場合、意図的に何もしない。 ネイティブのイベントコンテ
        // キストに古いキーボード位置・選択を残すと、ログ内のクリッ
        // クがログの前進・終了ではなく古いカードへ向かう。 Python
        // にはハンドラー側の別の選択キャッシュはない。
        ::cw::g_cwpy->m_index = -1;
        ::cw::g_cwpy->m_lazy_selection.reset();
    }
    c().selection.reset(); c().index = -1; c().selectable_list.clear();
    if (auto* base = ::cw::sprite::base::explicit_runtime_context())
        base->lazy_selection = nullptr;
    // カードアニメーションと StopTheWorld と同じ時計を使う。
    // 起点の異なるスプライト時計が明示的に設定されている場合も含める。
    m_stw = std::make_shared<::cw::sprite::base::StopTheWorld>();
    m_stw->initialize(::cw::sprite::base::animation_ticks(), 0);
    _update_posdata(true);
}
void EventHandlerForBacklog::_update_posdata(bool arg_init)
{
    auto sbarbar = statusbar_message_selection();
    if (m__sbarbar && *m__sbarbar && sbarbar) {
        // 拡大率更新で実体のメッセージとステータスバーのプロキシーが再構
        // 築された場合、 以前保持したオブジェクトを重複して復元しない。
        m__sbarbar.reset();
    }
    if (!arg_init) _clear_sprites();
    const auto list = backlog_records(m_backlog); m__height_noscale.clear(); m__pos_noscale.clear(); m__bottom_noscale.clear();
    // Python と同様、最初のログはゼロではなく記録
    // 自身の rect.top から始める。Bill は
    // 領域内で縦中央に配置される。 そのずれを捨てると張
    // り紙の位置とスクロール範囲が変わるため、維持する。
    auto record_top = [](const ::cw::lib::Value& value) -> ::cw::lib::Integer {
        if (const auto data = value_object<::cw::sprite::message::BacklogData>(value))
            return data->m_rect_noscale_typed.y;
        if (const auto bill = value_object<::cw::sprite::bill::Bill>(value))
            return bill->m_rect_noscale.y;
        return 0;
    };
    const bool scrollable = ::cw::g_cwpy && ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->is_logscrollable();
    ::cw::lib::Integer cursor = 0;
    if (scrollable && list && !list->empty()) {
        cursor = record_top(list->front());
        for (const auto& value : *list) {
            const auto h = backlog_height(value);
            m__pos_noscale.emplace_back(cursor); m__height_noscale.emplace_back(h);
            m__bottom_noscale.emplace_back(cursor + h);
            cursor += h + m__space_noscale;
        }
    }
    ::cw::lib::Integer scrsize_noscale = cursor;
    if (scrollable && list && !list->empty()) {
        const auto last_height = value_integer(m__height_noscale.back());
        const auto first_top = value_integer(m__pos_noscale.front(), 0);
        const auto last_top = value_integer(m__pos_noscale.back(), 0);
        if (::cw::g_cwpy && ::cw::g_cwpy->m_setting &&
            ::cw::g_cwpy->m_setting->m_messagelog_type == ::cw::setting::g_LOG_COMPRESS) {
            scrsize_noscale = last_top + last_height + first_top;
        } else {
            const auto area_height = backlog_viewport_height();
            scrsize_noscale = last_top + last_height +
                area_height - (last_height + record_top(list->back()));
            scrsize_noscale = (std::max)(scrsize_noscale,
                value_integer(m__bottom_noscale.back(), 0) +
                value_integer(::cw::s(::cw::lib::Value(5)), 5));
        }
    }
    if (sbarbar && ::cw::g_cwpy) {
        if (scrollable) {
            // Python と同様、スクロールするログの表示中は、
            // ステータスバーへはみ出す選択バーを一時的に取り除く。
            unregister_sprite(*sbarbar);
            register_sprite_group(*sbarbar, ::cw::g_cwpy->m_cardgrp);
            m__sbarbar = sbarbar;
            m__curtain2.reset();
        } else {
            m__curtain2 = std::make_shared<::cw::sprite::message::BacklogCurtain>();
            m__curtain2.value()->initialize(
                value_object_ptr(::cw::g_cwpy->m_sbargrp),
                ::cw::lib::Value(::cw::sprite::statusbar::g_LAYER_MESSAGE_LOG_CURTAIN),
                {sbarbar->m_size_noscale_typed.width, sbarbar->m_size_noscale_typed.height},
                {sbarbar->m_pos_noscale_typed.x, sbarbar->m_pos_noscale_typed.y},
                std::nullopt);
            m__sbarbar.reset();
        }
    } else {
        m__curtain2.reset();
        m__sbarbar.reset();
    }
    if (!m__scrollbar) m__scrollbar = std::make_shared<::cw::sprite::scrollbar::ScrollBar>();
    const auto scroll_position = arg_init
        ? std::max<::cw::lib::Integer>(0, scrsize_noscale - backlog_viewport_height())
        : std::max<::cw::lib::Integer>(0, m__scrollbar->get_pos());
    m__scrollbar->initialize(scroll_position, scrsize_noscale, scrollable); m__scrollbar->m_lazyscroll_func = [this]() { update_sprites(false); };
    if (::cw::g_cwpy) {
        const auto backlog_group = ::cw::g_cwpy->m_backloggrp;
        m__curtain = std::make_shared<::cw::sprite::message::BacklogCurtain>();
        m__curtain->initialize(value_object_ptr(backlog_group),
            ::cw::lib::Value(std::get<0>(::cw::g_LAYER_LOG_CURTAIN)),
            ::cw::g_SIZE_AREA, {0, 0}, std::nullopt);
        m__scrollbar->m_tlayer = ::cw::g_LAYER_LOG_SCROLLBAR;
        register_sprite_group(*m__scrollbar, backlog_group);
    }
    const auto maxpage = _get_maxpage(); if (arg_init) m_index = std::clamp(m_index, ::cw::lib::Integer(0), std::max<::cw::lib::Integer>(0, maxpage - 1));
    if (!m__page) m__page = std::make_shared<::cw::sprite::message::BacklogPage>();
    m__page->initialize(m_index + 1, maxpage,
        ::cw::g_cwpy ? value_object_ptr(::cw::g_cwpy->m_backloggrp) : nullptr);
    m_mwin.reset(); m__mwins.assign(list ? list->size() : 0, std::nullopt);
    c().showing_backlog = true;
    // Py と同様、初回もログ設定・拡大率の変更後も、 再構築
    // したログを描く前にステータスのコントロールを再構築する。
    if (c().refresh_statusbar) c().refresh_statusbar();
    update_sprites(false);
}

bool EventHandlerForBacklog::_pointer_in_backlog_view() const
{
    const auto scale = std::isfinite(::cw::g_UP_SCR) && ::cw::g_UP_SCR > 0.0
        ? ::cw::g_UP_SCR : 1.0;
    const auto width = static_cast<::cw::lib::Integer>(
        std::lround(static_cast<::cw::lib::Real>(std::get<0>(::cw::g_SIZE_AREA)) * scale));
    const auto height = static_cast<::cw::lib::Integer>(
        std::lround(static_cast<::cw::lib::Real>(std::get<1>(::cw::g_SIZE_AREA)) * scale));
    return 0 <= c().mouse_pos.x && c().mouse_pos.x < width &&
           0 <= c().mouse_pos.y && c().mouse_pos.y < height;
}

bool EventHandlerForBacklog::_pointer_on_statusbar() const
{
    const auto scale = std::isfinite(::cw::g_UP_SCR) && ::cw::g_UP_SCR > 0.0
        ? ::cw::g_UP_SCR : 1.0;
    const auto width = static_cast<::cw::lib::Integer>(
        std::lround(static_cast<::cw::lib::Real>(std::get<0>(::cw::g_SIZE_GAME)) * scale));
    const auto area_height = static_cast<::cw::lib::Integer>(
        std::lround(static_cast<::cw::lib::Real>(std::get<1>(::cw::g_SIZE_AREA)) * scale));
    const auto game_height = static_cast<::cw::lib::Integer>(
        std::lround(static_cast<::cw::lib::Real>(std::get<1>(::cw::g_SIZE_GAME)) * scale));
    return 0 <= c().mouse_pos.x && c().mouse_pos.x < width &&
           area_height <= c().mouse_pos.y && c().mouse_pos.y < game_height;
}

bool EventHandlerForBacklog::_pointer_on_scrollbar() const
{
    return m__scrollbar && m__scrollbar->m_visible &&
           contains(m__scrollbar->m_rect, c().mouse_pos);
}

::cw::lib::Integer EventHandlerForBacklog::_pointer_y_noscale() const
{
    const auto scale = std::isfinite(::cw::g_UP_SCR) && ::cw::g_UP_SCR > 0.0
        ? ::cw::g_UP_SCR : 1.0;
    return static_cast<::cw::lib::Integer>(
        std::lround(static_cast<::cw::lib::Real>(c().mouse_pos.y) / scale));
}

std::optional<std::shared_ptr<::cw::sprite::base::SelectableSprite>>
EventHandlerForBacklog::_update_backlog_selection()
{
    if (::cw::g_cwpy) {
        // ネイティブのボタンイベントはウィンドウ座標を持つ。 二倍・全画
        // 面表示でも正しく判定できるよう、ここでゲーム座標を取り直す。
        if (m__current_pointer_touch &&
            !dispatch_pointer_position(::cw::g_cwpy.get())) {
            ::cw::g_cwpy->m_wxmousepos = {
                ::cw::lib::Value(c().mouse_pos.x),
                ::cw::lib::Value(c().mouse_pos.y)};
        }
        ::cw::g_cwpy->update_mousepos();
        c().mouse_pos = {
            value_integer(std::get<0>(::cw::g_cwpy->m_mousepos), -1),
            value_integer(std::get<1>(::cw::g_cwpy->m_mousepos), -1)};
        sync_base_context();
    }

    if (_pointer_in_backlog_view()) {
        // ログのカーテンは backloggrp にあるため、基底の
        // cardgrp/topgrp 更新では背後のメニューカードを
        // 選んでしまう場合がある。 誤操作だけでなく、ログのスクロール
        // 中に保持背景を無効にして表示を崩すため、その更新は行わない。
        if (::cw::g_cwpy && ::cw::g_cwpy->m_selection &&
            *::cw::g_cwpy->m_selection) {
            ::cw::g_cwpy->clear_selection();
            set_has_inputevent(false);
        }
        c().selection.reset();
        c().index = -1;
        c().selectable_list.clear();
        sync_base_context();
        return std::nullopt;
    }

    // バックログ表示中もステータスバーは意図的に操作可
    // 能にする。 そのグループだけを更新し、覆われた
    // カードや最上位グループのホバー選択は行わない。
    if (::cw::g_cwpy) {
        ::cw::g_cwpy->update_groups(
            {value_object_ptr(::cw::g_cwpy->m_sbargrp)});
        c().index = ::cw::g_cwpy->m_index;
        if (::cw::g_cwpy->m_selection && *::cw::g_cwpy->m_selection)
            c().selection = *::cw::g_cwpy->m_selection;
        else
            c().selection.reset();
    }
    if (c().selection && c().selection->m_is_statusctrl) {
        sync_base_context();
        return c().selection;
    }
    if (::cw::g_cwpy && ::cw::g_cwpy->m_selection &&
        *::cw::g_cwpy->m_selection) {
        ::cw::g_cwpy->clear_selection();
        set_has_inputevent(false);
    }
    c().selection.reset();
    c().index = -1;
    c().selectable_list.clear();
    sync_base_context();
    return std::nullopt;
}

void EventHandlerForBacklog::_begin_swipe()
{
    if (!_pointer_in_backlog_view() || _pointer_on_scrollbar()) return;
    m__swipe_active = true;
    m__swipe_dragged = false;
    m__swipe_touch = m__current_pointer_touch;
    m__swipe_start_y_noscale = _pointer_y_noscale();
    m__swipe_last_y_noscale = m__swipe_start_y_noscale;
}

void EventHandlerForBacklog::_update_swipe()
{
    if (!m__swipe_active) return;
    if (::cw::g_cwpy) {
        if ((m__swipe_touch || m__current_pointer_touch) &&
            !dispatch_pointer_position(::cw::g_cwpy.get())) {
            ::cw::g_cwpy->m_wxmousepos = {
                ::cw::lib::Value(c().mouse_pos.x),
                ::cw::lib::Value(c().mouse_pos.y)};
        }
        ::cw::g_cwpy->update_mousepos();
        c().mouse_pos = {
            value_integer(std::get<0>(::cw::g_cwpy->m_mousepos), -1),
            value_integer(std::get<1>(::cw::g_cwpy->m_mousepos), -1)};
    }
    const auto current = _pointer_y_noscale();
    const auto movement = std::abs(current - m__swipe_start_y_noscale);
    constexpr ::cw::lib::Integer swipe_threshold_noscale = 6;
    if (!m__swipe_dragged && movement >= swipe_threshold_noscale) {
        m__swipe_dragged = true;
        // 縦ドラッグとして始まったポインター操作は、その処理
        // で最後まで扱う。 汎用の長押し判定で遅いスワイプを
        // 右クリックに変換し、解除時にバックログを閉じない。
        c().long_press_started = false;
        c().long_press_selection.reset();
    }
    if (m__swipe_dragged && m__scrollbar && m__scrollbar->m_visible) {
        // 内容はポインターに追従する。 上へドラッグすると
        // 新しいログ、下へドラッグすると古いログへ移る。
        const auto delta = m__swipe_last_y_noscale - current;
        if (delta != 0) {
            m__scrollbar->set_pos(m__scrollbar->get_pos() + delta, false);
            update_sprites(false);
        }
    }
    m__swipe_last_y_noscale = current;
}

bool EventHandlerForBacklog::_end_swipe()
{
    if (!m__swipe_active) return false;
    _update_swipe();
    const bool consumed = m__swipe_dragged;
    const auto direction = m__swipe_start_y_noscale - m__swipe_last_y_noscale;
    m__swipe_active = false;
    m__swipe_dragged = false;
    m__swipe_touch = false;
    if (consumed && (!m__scrollbar || !m__scrollbar->m_visible) && direction != 0)
        _wheel_event(direction > 0 ? 1 : -1, true);
    return consumed;
}

void EventHandlerForBacklog::cancel_pointer_sequence()
{
    EventHandler::cancel_pointer_sequence();
    m__in_scroll = m__swipe_active = m__swipe_dragged = false;
    m__swipe_touch = m__current_pointer_touch = false;
    // 解除時の代替処理でログのページが変わるため、_end_swipe() は呼ばない。
}

void EventHandlerForBacklog::run()
{
    configure_backend_safely(c());
    sync_context_from_cwpy();
    discard_cancelled_pointer_events();
    set_has_inputevent(false);
    // Python と同様、バックログが左ボタンの操作全体を管
    // 理する。押下は解除まで維持し、移動した場合はスクロール
    // にする。 C++ のタッチ拡張で静止した長押しを右クリ
    // ックに変え、まだ押されている間に閉じない。 先に閉じる
    // と、後の解除が復元された通常ハンドラーへ流れてしまう。
    c().long_press_started = false;
    c().long_press_fired = false;
    c().long_press_fired_time = 0.0;
    c().long_press_selection.reset();
    _check_updatesettings();
    // Py と同様、押し続けたキーの操作ごとに待機を行い、その後で予約済み入力を処理する。
    if (is_repeated_key(KEY_RETURN)) { returnkey_event(true); tick_clock_now(); }
    else if (is_repeated_key(KEY_UP)) { dirkey_event(0, -1, true, false, true); tick_clock_now(); }
    else if (is_repeated_key(KEY_DOWN)) { dirkey_event(0, 1, true, false, true); tick_clock_now(); }
    else if (is_repeated_key(KEY_PAGEUP)) { keydown_event(KEY_PAGEUP); tick_clock_now(); }
    else if (is_repeated_key(KEY_PAGEDOWN)) { keydown_event(KEY_PAGEDOWN); tick_clock_now(); }
    else if (!m__swipe_active && is_repeated_left_click()) {
        // Python の任意の左長押しリピートは、スワイプ外で維持する。 CanRepeatLClick
        // と長押し閾値は Relay 自身が適用する。
        returnkey_event(true);
        tick_clock_now();
    }
    if (m__swipe_active && !m__swipe_touch && c().mouse_left)
        _update_swipe();
    if (m__in_scroll) {
        if (c().mouse_left) ldown_event();
        else m__in_scroll = false;
    }
    discard_cancelled_pointer_events();
    ::cw::lib::ExceptionPtr delayed;
    while (!c().events.empty()) {
        auto event = std::dynamic_pointer_cast<InputEvent>(c().events.front());
        c().events.pop_front();
        if (!event) continue;
        PointerDispatchScope pointer_scope(event);
        // check_puressedbutton() が解除
        // 時の長押し判定を行う前に、ドラッグを認識する。 プラ
        // ットフォームが途中のゲームフレームなしで押下・解除を
        // 届けた場合も、一回のスワイプを認識できるようにする。
        if (m__swipe_active &&
            (event->type == EVENT_NONE ||
             (event->type == EVENT_MOUSEBUTTONUP && event->button == 1))) {
            if (::cw::g_cwpy) {
                ::cw::g_cwpy->update_mousepos();
                c().mouse_pos = {
                    value_integer(std::get<0>(::cw::g_cwpy->m_mousepos), -1),
                    value_integer(std::get<1>(::cw::g_cwpy->m_mousepos), -1)};
            } else {
                c().mouse_pos = event->pos;
            }
            m__current_pointer_touch = event->touch;
            _update_swipe();
            m__current_pointer_touch = false;
        }
        if (!check_puressedbutton(event)) {
            // check_puressedbutton() 内の解除時の長
            // 押し判定で、バックログが閉じる場合がある。 その解除イベント
            // も消費されるため、次のイベントへ進む前に終了状態を確認する。
            if (!c().showing_backlog) break;
            continue;
        }
        // Python と同様、EventError を遅延するのは予約済みのユーザーコールバックだけとする。
        switch (event->type) {
        case EVENT_KEYDOWN:
            if (event->key == KEY_LEFT) { c().key_left = true; }
            else if (event->key == KEY_RIGHT) { c().key_right = true; }
            else if (event->key == KEY_UP) { c().key_up = true; dirkey_event(0, -1, true, false, true); }
            else if (event->key == KEY_DOWN) { c().key_down = true; dirkey_event(0, 1, true, false, true); }
            else if (event->key == KEY_ESCAPE || event->key == KEY_BACKSPACE || event->key == KEY_BACKSLASH) escapekey_event();
            else if (event->key == KEY_F4) EventHandler::f4key_event();
            else dispatch_common_key(*this, event->key); break;
        case EVENT_KEYUP:
            if (event->key == KEY_RETURN) { keyup_event(event->key); returnkey_event(false); } else if (event->key == KEY_PRINT) printkey_event(); else keyup_event(event->key); break;
        case EVENT_NONE:
            if (event->touch && m__swipe_active) m__swipe_touch = true;
            if (m__swipe_active && c().mouse_left) _update_swipe();
            break;
        case EVENT_MOUSEBUTTONDOWN:
            if (event->button == 1) {
                m__current_pointer_touch = event->touch;
                ldown_event();
                m__current_pointer_touch = false;
                // check_puressedbutton() は汎用
                // の長押し認識を有効にする。 バックログでは意図的に
                // 使わず、解除とスワイプはこのハンドラーで管理する。
                c().long_press_started = false;
                c().long_press_fired = false;
                c().long_press_fired_time = 0.0;
                c().long_press_selection.reset();
            }
            break;
        case EVENT_MOUSEBUTTONUP:
            if (event->button == 1) {
                m__current_pointer_touch = event->touch;
                lclick_event();
                m__current_pointer_touch = false;
            } else if (event->button == 2) mclick_event();
            else if (event->button == 3) rclick_event(event->flick);
            else if (event->button == 4) wheel_event(-1);
            else if (event->button == 5) wheel_event(1);
            break;
        case EVENT_USEREVENT: case EVENT_FORCE_USEREVENT: defer_event_exception(delayed, event); if (!c().showing_backlog) exit_backlog(false); break; default: break;
        }

        if (!c().showing_backlog) break;
    }
    if (delayed) ::cw::lib::rethrow_exception(delayed);
}
void EventHandlerForBacklog::ldown_event()
{
    // ALL ではメッセージログの表示中、ステータスバーを閉じる
    // 専用の対象にする。 SDL はタップをマウス・タッチのどち
    // らとしても届け得るため、入力機器のフラグには依存しない。
    if (::cw::dialog::is_all_mode() && _pointer_on_statusbar())
        return;
    auto sprite = _update_backlog_selection().value_or(nullptr);
    if (sprite) {
        if (!locked(sprite)) { set_has_inputevent(true); sprite->ldown_event(); }
        return;
    }
    if (!_pointer_in_backlog_view()) return;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting &&
        ::cw::g_cwpy->m_setting->is_logscrollable() &&
        m__scrollbar && m__scrollbar->m_visible &&
        (!m__current_pointer_touch || _pointer_on_scrollbar())) {
        const bool lazy = !m__in_scroll;
        m__scrollbar->scroll_to_mousepos(lazy);
        if (!lazy) update_sprites(false);
        m__in_scroll = true;
        m__swipe_active = false;
        m__swipe_dragged = false;
        return;
    }
    // タッチのスワイプはネイティブの入力拡張である。 マウス押下では、上の
    // ログ背景全体で Py のポインター位置へのスクロール動作を維持する。
    if (m__current_pointer_touch && !m__swipe_active) _begin_swipe();
}
void EventHandlerForBacklog::lclick_event()
{
    m__in_scroll = false;
    // スワイプの確定より先に処理する。 短いタップ
    // と SDL の合成マウス解除のどちらも、下の
    // 有効なステータス操作へ届く前にログを閉じる。
    if (::cw::dialog::is_all_mode() && _pointer_on_statusbar()) {
        exit_backlog(true);
        return;
    }
    if (_end_swipe()) return;
    (void)_update_backlog_selection();
    returnkey_event(false);
}
void EventHandlerForBacklog::mclick_event() { lclick_event(); }
void EventHandlerForBacklog::rclick_event(bool arg_flick)
{
    // C++ の重ね合わせ表示の所有・再入保護。 終了済みや再帰的に呼ば
    // れたバックログが、音量も含む他の表示の入力を消費しないようにする。
    if (!c().showing_backlog || m_input_dispatching) return;
    struct DispatchGuard { bool& busy; ~DispatchGuard() { busy = false; } } guard{m_input_dispatching};
    m_input_dispatching = true;
    if (clear_volume_bar_on_right_click(arg_flick)) return;
    if (!can_input()) return;

    // Python と同様、実際の横フリックでは押下時の選択を保持
    // する。 通常の右クリックは現在のバックログ・ステータスバーに
    // 対して解決し、覆われたメニューカードへ届かないようにする。
    auto sprite = arg_flick ? selected()
                             : _update_backlog_selection().value_or(nullptr);
    if (sprite) {
        set_has_inputevent(true);
        sprite->rclick_event();
    } else {
        exit_backlog(true);
    }
}
void EventHandlerForBacklog::escapekey_event() { if (can_input()) exit_backlog(true); }
void EventHandlerForBacklog::returnkey_event(bool /*arg_pushing*/)
{
    if (!c().showing_backlog || m_input_dispatching) return;
    struct DispatchGuard { bool& busy; ~DispatchGuard() { busy = false; } } guard{m_input_dispatching};
    m_input_dispatching = true;
    if (!can_input()) return;
    auto sprite=_update_backlog_selection().value_or(nullptr);
    if (sprite&&!locked(sprite)) {
        set_has_inputevent(true);
        if(sprite->m_is_statusctrl&&::cw::g_cwpy) {
            // バックログは入れ子の入力ループを持つ。その中でモーダルを開いたり
            // デバッガー・エリアを変えたりすると、 二つのハンドラーが同じス
            // テータススプライトを操作してしまう。 先に閉じ、show_backlog()
            // が通常ハンドラーを復元してからステータスコマンドを一度
            // だけ実行する。バックログボタン自身は閉じるだけとする。
            if(!std::dynamic_pointer_cast<::cw::sprite::statusbar::BacklogButton>(sprite))
                ::cw::g_cwpy->m_after_backlog_action=[sprite]{sprite->lclick_event();};
            exit_backlog(true);
            return;
        }
        sprite->lclick_event();
        return;
    }
    if (!m__scrollbar || !m__scrollbar->m_visible) _wheel_event(1,true);
}
void EventHandlerForBacklog::dirkey_event(::cw::lib::Integer arg_x, ::cw::lib::Integer arg_y, bool /*arg_pushing*/, bool /*arg_sidechange*/, bool arg_hidetouchmenu)
{ if (!can_input()) return; if (arg_hidetouchmenu) clear_touchmenu(); if (arg_y) _wheel_event(arg_y, true); else if (arg_x) _wheel_event(arg_x, true); }
void EventHandlerForBacklog::wheel_event(::cw::lib::Integer arg_y)
{ if (!can_input_sys()) return; (void)_update_backlog_selection(); if (c().has_inputevent || !is_showing()) return; _wheel_event(arg_y, false); }
void EventHandlerForBacklog::_wheel_event(::cw::lib::Integer arg_y, bool arg_key)
{
    if (!can_input() || c().has_inputevent) return;
    // 全入力経路で、ログを移動する前に右ボタンの音量操作を消費する。
    if (change_volume(-arg_y)) return;
    if (m__scrollbar && m__scrollbar->m_visible) {
        if (!arg_key && arg_y > 0 && m__scrollbar->m_scrpos_noscale == m__scrollbar->m_scrsize_noscale - backlog_viewport_height()) {
            exit_backlog(true); return;
        }
        m__scrollbar->set_pos(m__scrollbar->get_pos() + arg_y * m__scrollnum_noscale, true);
        return;
    }
    const auto maxpage = _get_maxpage();
    if (arg_y > 0) {
        if (m_index + 1 >= maxpage) { exit_backlog(true); return; }
        if (c().play_sound) c().play_sound("page");
        ++m_index; update_sprites(false);
    } else {
        if (m_index <= 0) { if (c().play_sound) c().play_sound("error"); return; }
        if (c().play_sound) c().play_sound("page");
        --m_index; update_sprites(false);
    }
}
void EventHandlerForBacklog::_check_updatesettings()
{
    const auto current_type = (::cw::g_cwpy && ::cw::g_cwpy->m_setting)
        ? ::cw::g_cwpy->m_setting->m_messagelog_type : ::cw::lib::String{};
    const auto stored_scale = [&]() {
        if (const auto* value = std::get_if<::cw::lib::Real>(&m__upscr.storage())) return *value;
        if (const auto* value = std::get_if<::cw::lib::Integer>(&m__upscr.storage())) return static_cast<::cw::lib::Real>(*value);
        return 0.0;
    }();
    const auto stored_type = value_string(m__messagelog_type);
    const auto scrollnum = backlog_viewport_height() / 4;
    if (m__scrollnum_noscale != scrollnum || stored_scale != ::cw::g_UP_SCR || stored_type != current_type) {
        m__scrollnum_noscale = scrollnum;
        m__upscr = ::cw::lib::Value(::cw::g_UP_SCR);
        m__messagelog_type = ::cw::lib::Value(current_type);
        _update_posdata(false);
    }
}
bool EventHandlerForBacklog::is_showing()
{
    _check_updatesettings();
    if (m__scrollbar && m__scrollbar->m_visible) {
        const auto list = backlog_records(m_backlog);
        return list && !list->empty();
    }
    return m_mwin && !m_mwin->is_none();
}
// Py はステータスグループの LOG/LOG_CURTAIN レイヤ
// ー全体も消す。ネイティブ版では所有するカーテン・メッセージ表示・バー
// を全プロキシーグループから個別に解除し、 同じレイヤーを使う無関係
// なステータススプライトは残す。 所有する実体の重なりバーは、所有参
// 照を解除する前に下で明示的に復元する。後処理・復元とも破棄を待たない。
void EventHandlerForBacklog::_clear_sprites()
{
    if (m__curtain) unregister_sprite(*m__curtain);
    if (m__curtain2 && *m__curtain2) unregister_sprite(**m__curtain2);
    if (m__page) unregister_sprite(*m__page);
    if (m__scrollbar) unregister_sprite(*m__scrollbar);
    unregister_backlog_view(m_mwin);
    for (const auto& value : m__mwins) unregister_backlog_view(value);
    if (m__sbarbar && *m__sbarbar && ::cw::g_cwpy) {
        // ログ終了後、ステータスバーに重なる実体の選択バーを復元する。 unregister_sprite
        // は全プロキシーグループから除くため、通常のメッセ
        // ージグループとステータスバーグループの両方へ戻す。
        const auto bar = *m__sbarbar;
        unregister_sprite(*bar);
        register_sprite_group(*bar, ::cw::g_cwpy->m_cardgrp);
        const auto layer = bar->m_tlayer;
        bar->m_tlayer = {::cw::sprite::statusbar::g_LAYER_MESSAGE, 0, 0, 0};
        register_sprite_group(*bar, ::cw::g_cwpy->m_sbargrp);
        bar->m_tlayer = layer;
    }
    m_mwin.reset(); m__mwins.clear(); m__curtain.reset(); m__curtain2.reset(); m__page.reset(); m__sbarbar.reset();
    if (c().backend) {
        c().backend->remove_layer(std::get<0>(::cw::g_LAYER_LOG));
        c().backend->remove_layer(std::get<0>(::cw::g_LAYER_LOG_BAR));
        c().backend->remove_layer(std::get<0>(::cw::g_LAYER_LOG_CURTAIN));
        c().backend->remove_layer(std::get<0>(::cw::g_LAYER_LOG_PAGE));
        c().backend->remove_layer(std::get<0>(::cw::g_LAYER_LOG_SCROLLBAR));
    }
}
void EventHandlerForBacklog::restore_selection_context()
{
    std::vector<std::shared_ptr<::cw::sprite::base::SelectableSprite>> restored;
    ::cw::lib::Integer restored_index = m__saved_index;

    if (::cw::g_cwpy) {
        // 中断していたメッセージハンドラーを戻したら、そのウ
        // ィンドウを正本とする。 ログ表示中の拡大率変更で
        // SelectionBar がすべて置き換わる場合が
        // あるため、保存した vector だけを使わない。
        if (const auto message = ::cw::g_cwpy->get_messagewindow();
            message && *message) {
            for (const auto& bar : (*message)->m_selections)
                if (bar) restored.push_back(bar);
            restored_index = ::cw::g_cwpy->m_index;
        } else {
            // メッセージ以外の呼び出し元では、CWPy の現在の
            // 一覧が、入れ子のログループ中の画面再構築を反映する
            // 。 エリア遷移後などの空一覧も意味を持つため、こ
            // こでログ表示前のスナップショットを復活させない。
            restored = ::cw::g_cwpy->m_list;
            restored_index = ::cw::g_cwpy->m_index;
        }

        // ログ終了時もステータスバーのボタンがホバー状態の場合がある
        // 。その有効な選択は維持するが、 復元した一覧にない、覆わ
        // れた・古いメッセージやカードのスプライトは復活させない。
        std::shared_ptr<::cw::sprite::base::SelectableSprite> current;
        if (::cw::g_cwpy->m_selection && *::cw::g_cwpy->m_selection)
            current = *::cw::g_cwpy->m_selection;
        const bool current_is_restored = current &&
            (current->m_is_statusctrl ||
             std::find(restored.begin(), restored.end(), current) != restored.end());
        if (current && current_is_restored) {
            c().selection = current;
        } else {
            c().selection.reset();
            if (current && !current_is_restored)
                ::cw::g_cwpy->m_selection.reset();
        }
        // 覆われていたメッセージの保留中のホバーを、 ロ
        // グ終了後の最初の通常グループ更新で反映しない。
        ::cw::g_cwpy->m_lazy_selection.reset();

        // CWPy の互換一覧とイベントハンドラーの明示的
        // な一覧を同期させる。 通常は m_list に同
        // じオブジェクトがあるが、ここで代入することで入
        // れ子ループ中の拡大率変更・再構築にも対応する。
        ::cw::g_cwpy->m_list = restored;
        ::cw::g_cwpy->m_index = restored_index;
    } else {
        restored = m__saved_selectable_list;
        restored_index = m__saved_index;
        c().selection.reset();
    }

    if (restored_index < 0 ||
        static_cast<std::size_t>(restored_index) >= restored.size())
        restored_index = -1;

    c().selectable_list = std::move(restored);
    c().index = restored_index;
    c().pointed_tile_selection.reset();
    sync_base_context();
    if (auto* base = ::cw::sprite::base::explicit_runtime_context())
        base->lazy_selection = nullptr;
}
void EventHandlerForBacklog::exit_backlog(bool arg_playsound)
{
    // show_backlog() は必ず最終後処理を行うが
    // 、入力経路で既に閉じている場合もある。 Python
    // のスプライトグループ削除は繰り返しを許すが、C++ の
    // 保持オブジェクト・レイヤーの終了処理は一度だけ行う。
    if (!c().showing_backlog) {
        restore_selection_context();
        return;
    }
    if (arg_playsound && c().play_sound) c().play_sound("click");
    m__in_scroll = false;
    m__swipe_active = false;
    m__swipe_dragged = false;
    m__swipe_touch = false;
    m__current_pointer_touch = false;
    c().long_press_started = false;
    c().long_press_fired = false;
    c().long_press_fired_time = 0.0;
    c().long_press_selection.reset();
    if (::cw::g_cwpy && ::cw::g_cwpy->m_keyevent)
        ::cw::g_cwpy->m_keyevent->suppress_mouse_repeat(1);
    // Win32 とホスト内 SDL では、一フレームに複
    // 数のホイール入力が入る場合がある。 最初の入力でログ
    // を閉じたら、残りを通常ハンドラーへ渡さず、消えたログ
    // の背後のメニューカードのキーボード選択を変えない。
    c().events.erase(std::remove_if(c().events.begin(), c().events.end(),
        [](const auto& object) {
            const auto event = input_event(object);
            return event && event->type == EVENT_MOUSEBUTTONUP &&
                   (event->button == 4 || event->button == 5);
        }), c().events.end());
    _clear_sprites();
    c().showing_backlog = false;
    const bool saved_lock_menucards = value_bool(m__lock_menucards);
    const bool restore_lock_menucards = ::cw::g_cwpy
        ? ::cw::g_cwpy->m_lock_menucards
        : c().lock_menu_cards;
    if (restore_lock_menucards)
        c().lock_menu_cards = saved_lock_menucards;
    if (::cw::g_cwpy) {
        // Python と同様、バックログ処理が再びロックを立てた場合だけ、
        // 保存したロックを復元する。 常に復元すると、ログ終了後に露出した
        // ステータスバーやメニューカードが操作不能のままになる場合がある。
        if (restore_lock_menucards)
            ::cw::g_cwpy->m_lock_menucards = saved_lock_menucards;
        c().lock_menu_cards = ::cw::g_cwpy->m_lock_menucards;
        ::cw::g_cwpy->m__is_showingbacklog = false;
    }
    // Py と同様、ログ表示に使った時間だけカードグループのアニメーション時刻をず
    // らす。 ネイティブのグループは一つのモデルに複数の描画プロキシーを持つ場合
    // があるため、全種を解決して所有者ごとに一度だけ調整する。 C++ の initialize
    // が失敗すると stw の構築前に後処理へ来る場合もある。
    if (::cw::g_cwpy && m_stw) {
        if (const auto group = value_object<::cw::lib::SpriteGroup>(::cw::g_cwpy->m_cardgrp)) {
            const auto proxies = group->sprites;
            std::unordered_set<::cw::sprite::base::CWPySprite*> seen;
            for (const auto& proxy : proxies) {
                auto* sprite = ::cw::sprite::base::layered_group_sprite_owner(proxy);
                if (!sprite) sprite = ::cw::sprite::background::background_layer_sprite_owner(proxy);
                if (!sprite) sprite = ::cw::sprite::card::card_layer_sprite_owner(proxy);
                if (!sprite || !seen.insert(sprite).second) continue;
                // Py と同様、タイマー取得前に確認するのはアニメーション中のスプライト
                // ではなく、 現在選択しているスプライトのステータス操作フラグである。
                const auto& selection = ::cw::g_cwpy->m_selection;
                if (sprite->m_start_animation &&
                    !(selection && *selection && (*selection)->m_is_statusctrl)) {
                    const auto elapsed = ::cw::sprite::base::animation_ticks() - m_stw->m_start_ticks;
                    if (elapsed > 0) sprite->m_start_animation += elapsed;
                }
            }
        }
    }
    if (c().refresh_statusbar) c().refresh_statusbar();
    restore_selection_context();
    // Python と同様、保持したバックログのレイヤーを除いた後
    // 、ゲーム描画面全体を再描画対象にする。 そうしないと、無関係
    // なスプライトが同じ領域を更新するまで最後のログ画像が残る。
    if (::cw::g_cwpy)
        ::cw::g_cwpy->add_lazydraw_rect(
            {0, 0,
             value_integer(::cw::s(::cw::lib::Value(std::get<0>(::cw::g_SIZE_GAME)))),
             value_integer(::cw::s(::cw::lib::Value(std::get<1>(::cw::g_SIZE_GAME))))});
}
bool EventHandlerForBacklog::keydown_event(::cw::lib::Integer arg_key)
{
    if (!EventHandler::keydown_event(arg_key)) return false;
    if (current_modifiers().control() && arg_key == KEY_C && can_copytext()) { copy_text(); return false; }
    // Py と同様、この分岐はスクロールバーの表示ではなく設定されたログモード
    // で判断する。 スクロールモードでは未知のキーでも false を返す。
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_setting ||
        !::cw::g_cwpy->m_setting->is_logscrollable()) return true;
    if (m__scrollbar) {
        if (arg_key == KEY_PAGEUP) m__scrollbar->set_pos(m__scrollbar->get_pos() - backlog_viewport_height() * 80 / 100, true);
        else if (arg_key == KEY_PAGEDOWN) m__scrollbar->set_pos(m__scrollbar->get_pos() + backlog_viewport_height() * 80 / 100, true);
        else if (arg_key == KEY_HOME) m__scrollbar->set_pos(0, true);
        else if (arg_key == KEY_END) m__scrollbar->set_pos(m__scrollbar->m_scrsize_noscale - backlog_viewport_height(), true);
    }
    // Py と同様、再描画はスクロールバーの lazyscroll コールバックに委ねる。
    return false;
}
bool EventHandlerForBacklog::can_copytext() { return can_input(); }
void EventHandlerForBacklog::copy_text()
{
    if (c().play_sound) c().play_sound("equipment");
    auto list = backlog_records(m_backlog_all);
    const auto text = list ? ::cw::sprite::message::get_messagelogtext(*list, true)
                           : (c().get_messagelog ? c().get_messagelog() : c().messagelog_text);
    if (c().copy_to_clipboard) c().copy_to_clipboard(text);
}
::cw::lib::Integer EventHandlerForBacklog::_get_maxpage()
{
    const auto list = backlog_records(m_backlog);
    const auto n = list ? static_cast<::cw::lib::Integer>(list->size()) : 0;
    if (!n) return 1;
    if (m__scrollbar && m__scrollbar->m_visible) {
        const auto btop = m__scrollbar->m_scrsize_noscale - backlog_viewport_height();
        auto page = n;
        while (page > 0 && page - 1 < static_cast<::cw::lib::Integer>(m__pos_noscale.size()) &&
               btop <= value_integer(m__pos_noscale[static_cast<std::size_t>(page - 1)])) --page;
        return page + 1;
    }
    return n;
}
void EventHandlerForBacklog::update_sprites(bool arg_clearcache)
{
    if (!c().showing_backlog) return; if (arg_clearcache) { _update_posdata(false); return; }
    const auto list = backlog_records(m_backlog);
    if (!list || list->empty()) {
        // C++ では共有ログが外部から空にされた後の後処理も許す。 Python はこ
        // こで空でない一ページの一覧を前提とするが、解放済みデータを添字で参照しない。
        unregister_backlog_view(m_mwin);
        for (auto& value : m__mwins) unregister_backlog_view(value);
        m_mwin.reset();
        return;
    }
    // Python と同様、ここでは LAYER_LOG/LAYER_LOG_BAR
    // だけを取り除く。 描画オブジェクトはキャッシュに残してプロキシー
    // を切り離し、下で表示項目の位置を設定して再登録する。 ホイール入力
    // ごとに作り直すと重なりと激しいちらつきが起きるため、再利用する。
    unregister_backlog_view(m_mwin);
    for (auto& value : m__mwins) unregister_backlog_view(value);
    m_mwin.reset();
    m_index = std::clamp(m_index, ::cw::lib::Integer(0), static_cast<::cw::lib::Integer>(list->size() - 1)); if (m__mwins.size() != list->size()) m__mwins.assign(list->size(), std::nullopt);
    if (m__scrollbar && m__scrollbar->m_visible) {
        // スクロール可能なログは表示領域と交差する全メッセージを表示し、 Python
        // のバックログモードと同様、意図的に m_mwin を空にする。
        m_mwin.reset();
        const auto top = m__scrollbar->m_scrpos_noscale;
        const auto bottom = top + backlog_viewport_height();
        const auto first_it = std::upper_bound(
            m__bottom_noscale.begin(), m__bottom_noscale.end(), top,
            [](::cw::lib::Integer lhs, const ::cw::lib::Value& rhs) {
                return lhs < value_integer(rhs, 0);
            });
        const auto last_it = std::lower_bound(
            m__pos_noscale.begin(), m__pos_noscale.end(), bottom,
            [](const ::cw::lib::Value& lhs, ::cw::lib::Integer rhs) {
                return value_integer(lhs, 0) < rhs;
            });
        const auto first = static_cast<::cw::lib::Integer>(
            std::distance(m__bottom_noscale.begin(), first_it));
        const auto last = std::min<::cw::lib::Integer>(
            static_cast<::cw::lib::Integer>(list->size() - 1),
            static_cast<::cw::lib::Integer>(std::distance(m__pos_noscale.begin(), last_it)));
        for (auto index = first; index <= last; ++index) {
            auto& view = m__mwins[static_cast<std::size_t>(index)];
            if (!view) view = create_backlog_view((*list)[static_cast<std::size_t>(index)]);
            position_and_register_backlog_view(
                view, value_integer(m__pos_noscale[static_cast<std::size_t>(index)]) - top);
        }
        const auto page_it = std::lower_bound(
            m__pos_noscale.begin(), m__pos_noscale.end(), top,
            [](const ::cw::lib::Value& lhs, ::cw::lib::Integer rhs) {
                return value_integer(lhs, 0) < rhs;
            });
        const auto page_index = std::clamp<::cw::lib::Integer>(
            static_cast<::cw::lib::Integer>(std::distance(m__pos_noscale.begin(), page_it)),
            0, static_cast<::cw::lib::Integer>(list->size()));
        if (page_index != m_index) {
            m_index = page_index;
            update_backlog_page(m__page, m_index + 1, _get_maxpage());
        }
    } else {
        // Python と同様、LOG_SINGLE は現在の表示だけを作る。 元
        // の記録位置は create_backlog_view() で維持する。
        m_mwin = create_backlog_view((*list)[static_cast<std::size_t>(m_index)]);
        update_backlog_page(m__page, m_index + 1, _get_maxpage());
    }
    if (::cw::g_cwpy)
        ::cw::g_cwpy->add_lazydraw_rect(
            {0, 0,
             value_integer(::cw::s(::cw::lib::Value(std::get<0>(::cw::g_SIZE_GAME)))),
             value_integer(::cw::s(::cw::lib::Value(std::get<1>(::cw::g_SIZE_GAME))))});
}

void EventHandlerForEffectBooster::initialize() { m_running = true; }
void EventHandlerForEffectBooster::run()
{
    configure_backend_safely(c());
    sync_context_from_cwpy();
    discard_cancelled_pointer_events();
    set_has_inputevent(false);
    if (m_running) process_long_press();
    if (m_running && is_repeated_key(KEY_RETURN)) { returnkey_event(true); tick_clock_now(); }
    else if (m_running && is_repeated_left_click()) { returnkey_event(true); tick_clock_now(); }
    discard_cancelled_pointer_events();
    ::cw::lib::ExceptionPtr delayed;
    while (!c().events.empty()) { auto event = std::dynamic_pointer_cast<InputEvent>(c().events.front()); c().events.pop_front();
        if (!event) continue;
        PointerDispatchScope pointer_scope(event);
        // 操作認識自体が長押しの処理を実行する場合がある。 予約済み
        // ユーザーコマンドは残しつつ、その前に古い入力を拒否する。
        if (!m_running && event->type != EVENT_USEREVENT && event->type != EVENT_FORCE_USEREVENT) continue;
        if (!check_puressedbutton(event)) continue;
        // Python と同様、EventError を遅延す
        // るのは予約済みのユーザーコールバックだけとする。 ス
        // キップ入力で待機が終わったら、残りのポインター・キー
        // イベントは次の通常ハンドラーへ送らず取り除く。 ユ
        // ーザーイベントとその遅延例外は元の順序を維持する。
        if (!m_running && event->type != EVENT_USEREVENT && event->type != EVENT_FORCE_USEREVENT)
            continue;
        switch (event->type) {
        case EVENT_KEYDOWN:
            if (event->key == KEY_ESCAPE || event->key == KEY_BACKSPACE || event->key == KEY_BACKSLASH) escapekey_event();
            else if (event->key == KEY_F4) f4key_event();
            else if (event->key == KEY_F6) break;
            else dispatch_common_key(*this, event->key); break;
        case EVENT_KEYUP: if (event->key == KEY_RETURN) { keyup_event(event->key); returnkey_event(false); } else if (event->key == KEY_PRINT) printkey_event(); else keyup_event(event->key); break;
        case EVENT_MOUSEBUTTONDOWN: if (event->button == 1) ldown_event(); break;
        case EVENT_MOUSEBUTTONUP: if (event->button == 1) lclick_event(); else if (event->button == 2) mclick_event(); else if (event->button == 3) rclick_event(event->flick); else if (event->button == 4) wheel_event(-1); else if (event->button == 5) wheel_event(1); break;
        case EVENT_USEREVENT: case EVENT_FORCE_USEREVENT: defer_event_exception(delayed, event); break; default: break;
        }

    } if (delayed) ::cw::lib::rethrow_exception(delayed);
}
void EventHandlerForEffectBooster::mclick_event() { rclick_event(false); }
void EventHandlerForEffectBooster::lclick_event() {
    if (::cw::g_cwpy && ::cw::g_cwpy->m_effectbooster_click_wait) {
        if (m_running && can_input()) m_running = false;
        return;
    }
    EventHandler::lclick_event();
}
void EventHandlerForEffectBooster::rclick_event(bool arg_flick)
{
    if (clear_volume_bar_on_right_click(arg_flick)) return;
    if (!can_input()) return;
    auto sprite = arg_flick ? selected() : _update_selection(false).value_or(nullptr);
    if (sprite) { set_has_inputevent(true); sprite->lclick_event(); }
    else m_running = false;
}
void EventHandlerForEffectBooster::wheel_event(::cw::lib::Integer arg_y) { if (!can_input() || change_volume(-arg_y)) return; if (arg_y < 0 && c().wheel_show_log) { f5key_event(); return; } if (c().can_skipwait_with_wheel) m_running = false; }
void EventHandlerForEffectBooster::escapekey_event() { if (can_input()) m_running = false; }
void EventHandlerForEffectBooster::returnkey_event(bool /*arg_pushing*/) { if (can_input()) m_running = false; }
void EventHandlerForEffectBooster::f4key_event()
{
    if (!can_input()) return;
    // 先に実行中の JPY の呼び出し階層を巻き戻し、その後、通常のゲームス
    // レッドの F4 ハンドラーで拡大率変更を一度だけ行う。 Py と異なり
    // expanddrawing が 1.0 でも ScreenRescale
    // を送出する。ネイティブでは拡縮を予約するため、 遅延処理が実行でき
    // るよう、現在の JPY の描画コンテキストを先に解放する必要がある。
    if (::cw::g_cwpy) {
        ::cw::g_cwpy->exec_func([](const ::cw::lib::Arguments&, const ::cw::lib::KeywordArguments&) {
            ::cw::eventhandler::EventHandler handler;
            handler.f4key_event();
            return ::cw::lib::Value{};
        });
    }
    throw ::cw::effectbooster::ScreenRescale();
}

::cw::lib::Value EventHandlerForMessageWindow::initialize(std::shared_ptr<::cw::sprite::message::MessageWindow> arg_mwin)
{
    m_mwin = ::cw::lib::Value(arg_mwin); m_message_hidden = false; c().showing_message = static_cast<bool>(arg_mwin);
    // 前のメッセージが、共通イベントコンテキストに非所有のホバー選択参照を残してい
    // る場合がある。 その SelectionBar は teardown_display()
    // で破棄されるため、次のウィンドウへポインターを持ち越さない。
    c().selection.reset();
    c().pointed_tile_selection.reset();
    c().selectable_list.clear(); c().index = -1;
    if (arg_mwin) for (const auto& bar : arg_mwin->m_selections) c().selectable_list.push_back(bar);
    sync_base_context();
    return m_mwin;
}
void EventHandlerForMessageWindow::run()
{
    configure_backend_safely(c());
    sync_context_from_cwpy();
    discard_cancelled_pointer_events();
    set_has_inputevent(false);
    process_long_press();
    auto held_mwin = value_object<::cw::sprite::message::MessageWindow>(m_mwin);
    const bool autoenter = c().autoenter_on_sprite || (held_mwin && held_mwin->m_selections.size() <= 1);
    if (is_repeated_key(KEY_RETURN) && autoenter) { returnkey_event(true); tick_clock_now(); }
    else if (is_repeated_key(KEY_UP)) { dirkey_event(0, -1, true, false, true); tick_clock_now(); }
    else if (is_repeated_key(KEY_DOWN)) { dirkey_event(0, 1, true, false, true); tick_clock_now(); }
    else if (is_repeated_key(KEY_LEFT)) { dirkey_event(-1, 0, true, false, true); tick_clock_now(); }
    else if (is_repeated_key(KEY_RIGHT)) { dirkey_event(1, 0, true, false, true); tick_clock_now(); }
    else if (is_repeated_left_click() && autoenter) { returnkey_event(true); tick_clock_now(); }
    discard_cancelled_pointer_events();
    ::cw::lib::ExceptionPtr delayed;
    while (!c().events.empty()) { auto event = std::dynamic_pointer_cast<InputEvent>(c().events.front()); c().events.pop_front(); PointerDispatchScope pointer_scope(event); if (!event || !check_puressedbutton(event)) continue;
        // Python と同様、EventError を遅延するのは予約済みのユーザーコールバックだけとする。
        switch (event->type) {
        case EVENT_KEYDOWN:
            if (event->key == KEY_LSHIFT || event->key == KEY_RSHIFT) shiftkey_event(true, true);
            else if (event->key == KEY_LEFT) { c().key_left = true; dirkey_event(-1, 0, true, false, true); }
            else if (event->key == KEY_RIGHT) { c().key_right = true; dirkey_event(1, 0, true, false, true); }
            else if (event->key == KEY_UP) { c().key_up = true; dirkey_event(0, -1, true, false, true); }
            else if (event->key == KEY_DOWN) { c().key_down = true; dirkey_event(0, 1, true, false, true); }
            else if (event->key == KEY_F4) f4key_event();
            else if (event->key == KEY_F6) break;
            else dispatch_common_key(*this, event->key); break;
        case EVENT_KEYUP:
            if (event->key == KEY_LSHIFT || event->key == KEY_RSHIFT) shiftkey_event(false, true);
            else if (event->key == KEY_RETURN) { keyup_event(event->key); returnkey_event(false); } else if (event->key == KEY_PRINT) printkey_event(); else keyup_event(event->key); break;
        case EVENT_MOUSEBUTTONDOWN: if (event->button == 3) shiftkey_event(true, true); else if (event->button == 1) ldown_event(); break;
        case EVENT_MOUSEBUTTONUP: {
            const auto current_selection = selected();
            if (held_mwin && held_mwin->m_is_drawing &&
                !(current_selection && current_selection->m_selectable_on_event) &&
                !(event->button == 4 && c().wheel_show_log))
                mouse_event(event->button);
            else if (event->button == 1) lclick_event();
            else if (event->button == 2) mclick_event();
            else if (event->button == 3) rclick_event(event->flick);
            else if (event->button == 4) wheel_event(-1);
            else if (event->button == 5) wheel_event(1);
            break;
        }
        case EVENT_USEREVENT: case EVENT_FORCE_USEREVENT: defer_event_exception(delayed, event); break; default: break;
        }

    } if (delayed) ::cw::lib::rethrow_exception(delayed);
}
void EventHandlerForMessageWindow::mouse_event(::cw::lib::Integer arg_button) { auto mwin = value_object<::cw::sprite::message::MessageWindow>(m_mwin); if (mwin && mwin->m_is_drawing) { set_has_inputevent(true); mwin->draw_all(); } else if (arg_button == 1) EventHandler::ldown_event(); }
void EventHandlerForMessageWindow::ldown_event()
{
    auto mwin = value_object<::cw::sprite::message::MessageWindow>(m_mwin);
    if (::cw::g_cwpy) ::cw::g_cwpy->update_mousepos();
    sync_context_from_cwpy();
    if (c().enabled_right_flick && ::cw::g_cwpy && ::cw::g_cwpy->m_statusbar &&
        contains(sprite_rect(::cw::g_cwpy->m_statusbar.get()), c().mouse_pos) &&
        !_update_selection(mwin && mwin->m_is_drawing).has_value()) shiftkey_event(true, true);
    else EventHandler::ldown_event();
}
void EventHandlerForMessageWindow::lclick_event()
{
    auto mwin = value_object<::cw::sprite::message::MessageWindow>(m_mwin);
    if (!_has_message()) { shiftkey_event(false, true); return; }
    if (!can_input()) return;
    auto sprite = _update_selection(mwin && mwin->m_is_drawing).value_or(nullptr);
    if (sprite) {
        if (contains(sprite_rect(sprite.get()), c().mouse_pos) ||
            std::dynamic_pointer_cast<::cw::sprite::message::SelectionBar>(sprite)) {
            set_has_inputevent(true);
            sprite->lclick_event();
        }
        return;
    }
    if (mwin && !c().selectable_list.empty() &&
        (c().selectable_list.size() == 1 || c().index >= 0) && _has_message() &&
        ::cw::g_cwpy && ::cw::g_cwpy->m_background &&
        contains(sprite_rect(::cw::g_cwpy->m_background.get()), c().mouse_pos)) {
        const auto index = c().index < 0 ? c().selectable_list.size() - 1 : static_cast<std::size_t>(c().index);
        if (index < c().selectable_list.size()) {
            set_has_inputevent(true);
            if (const auto bar = std::dynamic_pointer_cast<::cw::sprite::message::SelectionBar>(c().selectable_list[index]))
                bar->lclick_event(true);
        }
    }
}
bool EventHandlerForMessageWindow::_has_message()
{
    if (::cw::g_cwpy) {
        const auto group = value_object<::cw::lib::SpriteGroup>(::cw::g_cwpy->m_cardgrp);
        if (!group) return false;
        const auto encoded_layer = [](const auto& layer) {
            return ::cw::layer_val({std::get<0>(layer), value_integer(std::get<1>(layer)),
                                   std::get<2>(layer), std::get<3>(layer)});
        };
        const auto message_layer = encoded_layer(::cw::g_LAYER_MESSAGE);
        const auto special_message_layer = encoded_layer(::cw::g_LAYER_SPMESSAGE);
        for (const auto& proxy : group->sprites) {
            const auto stored = group->layers.find(proxy.get());
            const auto layer = stored == group->layers.end()
                ? ::cw::sprite::base::layered_group_sprite_layer(proxy)
                : std::optional<::cw::lib::Integer>(stored->second);
            if (layer && (*layer == message_layer || *layer == special_message_layer)) return true;
        }
        return false;
    }
    // 画面なしの呼び出し側にはスプライトグループがないため、明示的な表示状態を維持する
    // 。 実体の Engine では Python と同様、実際のレイヤー所属を使う。
    return !m_message_hidden && static_cast<bool>(value_object<::cw::sprite::message::MessageWindow>(m_mwin));
}
void EventHandlerForMessageWindow::mclick_event()
{
    if (!can_input()) return;
    auto mwin = value_object<::cw::sprite::message::MessageWindow>(m_mwin);
    auto sprite = _update_selection(mwin && mwin->m_is_drawing).value_or(nullptr);
    if (const auto bar = std::dynamic_pointer_cast<::cw::sprite::message::SelectionBar>(sprite); bar && c().selectable_list.size() > 1) {
        set_has_inputevent(true);
        bar->lclick_event(true);
    }
}
void EventHandlerForMessageWindow::rclick_event(bool arg_flick)
{
    if (clear_volume_bar_on_right_click(arg_flick)) { if (!_has_message()) shiftkey_event(false, true); return; }
    if (!_has_message()) shiftkey_event(false, true);
    if (!can_input()) return;
    auto mwin = value_object<::cw::sprite::message::MessageWindow>(m_mwin);
    auto sprite = arg_flick ? selected() : _update_selection(mwin && mwin->m_is_drawing).value_or(nullptr);
    if (sprite && contains(sprite_rect(sprite.get()), c().mouse_pos)) { set_has_inputevent(true); sprite->rclick_event(); }
}
void EventHandlerForMessageWindow::f4key_event()
{
    if (!can_input()) return;
    const bool hidden = !_has_message();
    if (hidden) shiftkey_event(false, false);
    EventHandler::f4key_event();
    if (hidden) shiftkey_event(true, false);
}
void EventHandlerForMessageWindow::returnkey_event(bool arg_pushing)
{
    if (!can_input()) return;
    auto mwin = value_object<::cw::sprite::message::MessageWindow>(m_mwin);
    if (!mwin) return;
    if (mwin->m_is_drawing) { set_has_inputevent(true); mwin->draw_all(); return; }
    // Py の CWPy.list と同様、表示中の選択肢には、 メッセージウィン
    // ドウがバーを作る時に設定した共通の selectable_list を使う。
    if (c().selectable_list.size() == 1) {
        set_has_inputevent(true);
        if (const auto bar = std::dynamic_pointer_cast<::cw::sprite::message::SelectionBar>(c().selectable_list.front()))
            bar->lclick_event(true);
        return;
    }
    auto sprite = selected();
    if (const auto bar = std::dynamic_pointer_cast<::cw::sprite::message::SelectionBar>(sprite)) {
        set_has_inputevent(true); bar->lclick_event(true); return;
    }
    if (!arg_pushing && c().index >= 0 && c().index < static_cast<::cw::lib::Integer>(c().selectable_list.size())) {
        set_has_inputevent(true);
        if (const auto bar = std::dynamic_pointer_cast<::cw::sprite::message::SelectionBar>(c().selectable_list[static_cast<std::size_t>(c().index)]))
            bar->lclick_event(true);
    }
}
void EventHandlerForMessageWindow::dirkey_event(::cw::lib::Integer arg_x, ::cw::lib::Integer arg_y, bool arg_pushing, bool arg_sidechange, bool arg_hidetouchmenu)
{
    if (!can_input()) return;
    if (arg_hidetouchmenu) clear_touchmenu();
    auto mwin = value_object<::cw::sprite::message::MessageWindow>(m_mwin);
    if (!mwin || mwin->m_is_drawing) return;
    set_has_inputevent(true);
    if (mwin->m_selections.empty()) return;
    c().selectable_list.clear();
    for (const auto& bar : mwin->m_selections) c().selectable_list.push_back(bar);
    if (c().index < 0 || c().index >= static_cast<::cw::lib::Integer>(mwin->m_selections.size())) {
        // Python と同様、最初の方向キーでは先頭項目を選ぶ。
        // 既に選択されている場合だけ、要求された移動量を適用する。
        c().index = 0;
    } else if (arg_y) {
        const auto columns = std::max<::cw::lib::Integer>(1, mwin->m_columns_typed);
        const auto rows = (static_cast<::cw::lib::Integer>(mwin->m_selections.size()) + columns - 1) / columns;
        c().index += arg_y * columns;
        if (arg_y > 0 && c().index >= static_cast<::cw::lib::Integer>(mwin->m_selections.size())) c().index = (c().index + 1) % (rows * columns) % columns;
        if (arg_y < 0 && c().index < 0) {
            c().index += rows * columns + columns - 1;
            while (c().index >= static_cast<::cw::lib::Integer>(mwin->m_selections.size())) c().index -= columns;
        }
    } else if (arg_x) {
        c().index = calc_index(arg_x);
    }
    if (c().index >= 0 && c().index < static_cast<::cw::lib::Integer>(mwin->m_selections.size())) {
        const auto bar = mwin->m_selections[static_cast<std::size_t>(c().index)];
        // set_selection() は候補・位置・選択・ホイール起点を既に CWPy へ保存する。
        // Python と同様、ここでは change_selection() 相当の操作を一度だけ行う。
        set_selection(bar, true);
    }
    (void)arg_pushing; (void)arg_sidechange;
}
bool EventHandlerForMessageWindow::keydown_event(::cw::lib::Integer arg_key) { if (!EventHandler::keydown_event(arg_key)) return false; if (!can_input()) return false; if (current_modifiers().control() && arg_key == KEY_C && can_copytext()) { copy_text(); return false; } return true; }
bool EventHandlerForMessageWindow::can_copytext()
{ auto mwin = value_object<::cw::sprite::message::MessageWindow>(m_mwin); return mwin && !mwin->m_is_drawing; }
void EventHandlerForMessageWindow::copy_text()
{
    if (c().play_sound) c().play_sound("equipment");
    auto mwin = value_object<::cw::sprite::message::MessageWindow>(m_mwin);
    const auto text = mwin ? ::cw::sprite::message::get_messagelogtext({::cw::lib::Value(mwin)}, true)
                           : (c().get_messagelog ? c().get_messagelog() : c().messagelog_text);
    if (c().copy_to_clipboard) c().copy_to_clipboard(text);
}
void EventHandlerForMessageWindow::wheel_event(::cw::lib::Integer arg_y)
{
    if (!can_input()) return;
    if (change_volume(-arg_y)) return;
    if (arg_y < 0 && c().wheel_show_log) { f5key_event(); return; }
    if (c().has_inputevent || !_has_message()) return;
    auto mwin = value_object<::cw::sprite::message::MessageWindow>(m_mwin);
    if (mwin && mwin->m_selections.size() == 1 && arg_y > 0) {
        if (!c().can_forward_message_with_wheel) return;
        set_has_inputevent(true); mwin->m_selections.front()->lclick_event(true);
    } else if (mwin && !mwin->m_selections.empty()) { set_has_inputevent(true); dirkey_event(arg_y, 0, true, false, true); }
}
// Py はメッセージ・選択レイヤー全体を隠す。ネイティブ版はこの
// ウィンドウと所有する全バーを全描画プロキシーグループから解除し
// 、 同じレイヤーの無関係なオブジェクトは残す。Shift 解除
// 時に同じインスタンスを再登録できるようバーを保持し、 ステータ
// スグループにまたがるものも含め、デストラクターには依存しない。
void EventHandlerForMessageWindow::shiftkey_event(bool arg_down, bool arg_redraw)
{
    auto mwin = value_object<::cw::sprite::message::MessageWindow>(m_mwin); if (!mwin) return;
    if (arg_down) {
        if (mwin->m_is_drawing) { mwin->draw_all(); return; }
        if (::cw::g_cwpy) ::cw::g_cwpy->clear_selection();
        unregister_sprite(*mwin);
        for (const auto& bar : mwin->m_selections) if (bar) unregister_sprite(*bar);
        m_message_hidden = true;
        c().showing_message = false;
    // ネイティブ版で隠すのは、このウィンドウと所有するバーだけであ
    // る。 同じレイヤーに別のメッセージ所有者が残っていても、こ
    // のハンドラーで明示的に隠したウィンドウの復元を抑止しない。
    } else if (m_message_hidden || !_has_message()) {
        m_message_hidden = false;
        c().showing_message = true;
        const bool special = (::cw::g_cwpy && ::cw::g_cwpy->m_background && ::cw::g_cwpy->m_background->m_curtain_all) || in_special_area();
        mwin->m_tlayer = special
            ? std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer>{
                  std::get<0>(::cw::g_LAYER_SPMESSAGE), value_integer(std::get<1>(::cw::g_LAYER_SPMESSAGE), 1),
                  std::get<2>(::cw::g_LAYER_SPMESSAGE), std::get<3>(::cw::g_LAYER_SPMESSAGE)}
            : std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer>{
                  std::get<0>(::cw::g_LAYER_MESSAGE), value_integer(std::get<1>(::cw::g_LAYER_MESSAGE), 1),
                  std::get<2>(::cw::g_LAYER_MESSAGE), std::get<3>(::cw::g_LAYER_MESSAGE)};
        if (::cw::g_cwpy) register_sprite_group(*mwin, ::cw::g_cwpy->m_cardgrp);
        const auto sellayer = special ? ::cw::g_LAYER_SPSELECTIONBAR_1 : ::cw::g_LAYER_SELECTIONBAR_1;
        const auto area_h = value_integer(::cw::s(::cw::lib::Value(std::get<1>(::cw::g_SIZE_AREA))), 0);
        for (const auto& bar : mwin->m_selections) {
            if (!bar) continue;
            bar->m_tlayer = {std::get<0>(sellayer), value_integer(std::get<1>(sellayer), 1),
                             std::get<2>(sellayer), std::get<3>(sellayer)};
            if (::cw::g_cwpy) register_sprite_group(*bar, ::cw::g_cwpy->m_cardgrp);
            const auto rect = sprite_rect(bar.get());
            if (::cw::g_cwpy && area_h <= rect.y + rect.height && rect.y <= area_h)
                register_sprite_group(*bar, ::cw::g_cwpy->m_sbargrp);
        }
        if (arg_redraw) mwin->draw_all();
    }
    if (arg_redraw && ::cw::g_cwpy) {
        if (::cw::g_cwpy->m_background) ::cw::g_cwpy->add_lazydraw(::cw::g_cwpy->m_background);
        if (::cw::g_cwpy->m_statusbar) ::cw::g_cwpy->add_lazydraw(::cw::g_cwpy->m_statusbar);
    }
}

void EventHandlerForUserAction::initialize() { m_event_boarder_time.reset(); m_event_engine.reset(); m_last_known_left_down_time.reset(); m_last_known_left_up_time.reset(); m_latest_left_up_status_time.reset(); m_running = true; m_skip_card_click_event = false; m_pending_touch.reset(); m_action_buttons = 0; }
void EventHandlerForUserAction::set_event_boarder_time(::cw::lib::Integer arg_plus) { m_event_boarder_time = now_ticks() + arg_plus; c().event_border_time = *m_event_boarder_time; }
bool EventHandlerForUserAction::detect_left_click_press()
{
    // Py は keyevent.mousein[0] を読んで最後の解除時刻を更新するが、 ローカルの
    // current_down_time は None のままなので、この判定は false を返す。
    const bool pressed = ::cw::g_cwpy && ::cw::g_cwpy->m_keyevent &&
        !::cw::g_cwpy->m_keyevent->m_mousein.empty() &&
        ::cw::g_cwpy->m_keyevent->m_mousein[0] != 0;
    const auto current_up_time = pressed ? m_last_known_left_up_time
        : std::optional<::cw::lib::Real>(now_ticks());
    if (current_up_time && (!m_last_known_left_up_time ||
                           *current_up_time > *m_last_known_left_up_time))
        m_last_known_left_up_time = current_up_time;
    return false;
}
void EventHandlerForUserAction::run_init() { set_has_inputevent(false); }
bool EventHandlerForUserAction::run_user_action()
{
    configure_backend_safely(c());
    sync_context_from_cwpy();
    discard_cancelled_pointer_events();
    run_init();
    poll_backend_safely();
    discard_cancelled_pointer_events();
    if (detect_left_click_press()) {
        auto sprite = _update_selection(running_event_active()).value_or(nullptr);
        if (sprite && !locked(sprite) && !sprite->m_is_statusctrl) {
            set_has_inputevent(true); set_event_boarder_time(5);
            if (c().run_event_engine) c().run_event_engine(); return true;
        }
    }
    c().events.clear(); return false;
}

void EventHandlerForUserAction::run()
{
    configure_backend_safely(c());
    sync_context_from_cwpy();
    poll_backend_safely();
    if (::cw::g_cwpy) {
        std::scoped_lock lock(::cw::g_cwpy->m_events_mutex);
        if (auto& pending = ::cw::g_cwpy->m_action_pointer_events; pending) {
            c().events.insert(c().events.end(), pending->begin(), pending->end());
            pending->clear();
        }
    }
    if (std::any_of(c().events.begin(), c().events.end(), [](const auto& object) {
            const auto event = std::dynamic_pointer_cast<InputEvent>(object);
            return event && event->type == EVENT_POINTERCANCEL &&
                !stale_input_generation(*event);
        })) {
        cancel_touch();
    }
    discard_cancelled_pointer_events();
    run_init();
    // Python の MouseWatcher は、入れ子のメッセージ
    // ループへ入るコールバックから独立している。 他の入力処理が未消費の
    // 押下・解除を読めるようになる前に、この一括入力の取得を完了する。
    std::vector<std::shared_ptr<InputEvent>> callbacks;
    ::cw::lib::ExceptionPtr delayed;
    while (!c().events.empty()) {
        const auto event = std::dynamic_pointer_cast<InputEvent>(c().events.front());
        c().events.pop_front();
        if (!event) continue;
        if ((event->button == 1 || event->button == 3) &&
            (event->type == EVENT_MOUSEBUTTONDOWN || event->type == EVENT_MOUSEBUTTONUP)) {
            const unsigned bit = 1U << (event->button - 1);
            if (event->type == EVENT_MOUSEBUTTONDOWN) {
                // 保持したこの位置を使う時、表示領域から描画面への変換は set_action_mouse_input()
                // で一度だけ行う。
                if (!m_action_buttons) m_pending_touch = event->pos;
                m_action_buttons |= bit;
            } else m_action_buttons &= ~bit;
        } else if (event->type == EVENT_USEREVENT || event->type == EVENT_FORCE_USEREVENT) {
            callbacks.push_back(event);
        }
    }
    for (const auto& event : callbacks) defer_event_exception(delayed, event);
    if (delayed) ::cw::lib::rethrow_exception(delayed);
}

void EventHandlerForUserAction::cancel_touch()
{
    m_pending_touch.reset();
    m_action_buttons = 0;
}

std::optional<::cw::lib::Point> EventHandlerForUserAction::take_touch()
{
    return std::exchange(m_pending_touch, std::nullopt);
}

std::vector<std::shared_ptr<::cw::sprite::base::SelectableSprite>> EventHandler_dirkey_event_get_mcards() { return c().get_mcards ? c().get_mcards() : std::vector<std::shared_ptr<::cw::sprite::base::SelectableSprite>>{}; }
std::vector<std::shared_ptr<::cw::sprite::base::SelectableSprite>> EventHandler_dirkey_event_get_pcards() { return c().get_pcards ? c().get_pcards() : std::vector<std::shared_ptr<::cw::sprite::base::SelectableSprite>>{}; }
std::vector<std::shared_ptr<::cw::sprite::base::SelectableSprite>> EventHandler_dirkey_event_get_etc() { return c().get_etc ? c().get_etc() : std::vector<std::shared_ptr<::cw::sprite::base::SelectableSprite>>{}; }
std::vector<std::shared_ptr<::cw::sprite::base::SelectableSprite>> EventHandler_dirkey_event_get_etc2() { return c().get_etc2 ? c().get_etc2() : std::vector<std::shared_ptr<::cw::sprite::base::SelectableSprite>>{}; }
void EventHandler_background_event_func(::cw::lib::Integer arg_resid) { if (c().background_action) c().background_action(arg_resid); }
void EventHandler_f1key_event_func()
{
    if (!::cw::novel::enabled() && c().show_help) c().show_help();
}
void EventHandler_f6key_event_func(std::shared_ptr<::cw::debug::logging::DebugLog> /*arg_debuglog*/) { if (c().show_debug_log) c().show_debug_log(); }
void main()
{
    // 継続的な描画ループは CWPy のフレームが管理する。 ここで
    // は非待機のネイティブ処理を一段だけ登録・実行し、競合するゲーム
    // ループを作らず Python のインポート時設定に対応させる。
    set_runtime_context(runtime_context());
    if (c().backend) c().backend->native_loop_step();
}
} // namespace cw::eventhandler
