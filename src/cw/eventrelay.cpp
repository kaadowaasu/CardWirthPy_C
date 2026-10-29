#include "cw/eventrelay.hpp"

#include "cw/cw.hpp"
#include "cw/frame.hpp"
#include "cw/setting.hpp"
#include "cw/thread.hpp"
#include "cw/eventhandler.hpp"
#include "cw/dialog/presentation.hpp"
#include "cw/lib/ui_surface.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

#if __has_include(<SDL3/SDL.h>)
#include <SDL3/SDL.h>
#include "cw/lib/sdl_game_touch.hpp"
#include "cw/lib/sdl_key.hpp"
#include "cw/lib/sdl_input_clock.hpp"
#define CW_EVENTRELAY_HAS_SDL3 1
#else
#define CW_EVENTRELAY_HAS_SDL3 0
#endif

namespace {

using Integer = ::cw::lib::Integer;
using Real = ::cw::lib::Real;

// pygame の値はイベント属性 (pygame_type) に保持し、CWPy の既存入力
// フィルタが認識する互換イベント値を Event::type に格納する。
constexpr Integer kMouseButtonDown = ::cw::eventhandler::EVENT_MOUSEBUTTONDOWN;
constexpr Integer kMouseButtonUp = ::cw::eventhandler::EVENT_MOUSEBUTTONUP;
constexpr Integer kKeyDown = ::cw::eventhandler::EVENT_KEYDOWN;
constexpr Integer kKeyUp = ::cw::eventhandler::EVENT_KEYUP;
constexpr Integer kPygameMouseButtonDown = 1025;
constexpr Integer kPygameMouseButtonUp = 1026;
constexpr Integer kPygameKeyDown = 768;
constexpr Integer kPygameKeyUp = 769;

// 旧pygame互換のキーコード。
constexpr Integer kReturn = 13;
constexpr Integer kEscape = 27;
constexpr Integer kBackspace = 8;
constexpr Integer kBackslash = 92;
constexpr Integer kSpace = 32;
constexpr Integer kLeft = 276;
constexpr Integer kRight = 275;
constexpr Integer kUp = 273;
constexpr Integer kDown = 274;
constexpr Integer kF1 = 282;
constexpr Integer kF2 = 283;
constexpr Integer kF3 = 284;
constexpr Integer kF4 = 285;
constexpr Integer kF5 = 286;
constexpr Integer kF6 = 287;
constexpr Integer kF7 = 288;
constexpr Integer kF8 = 289;
constexpr Integer kF9 = 290;
constexpr Integer kF10 = 291;
constexpr Integer kF11 = 292;
constexpr Integer kF12 = 293;
constexpr Integer kPrint = 316;
constexpr Integer kHome = 278;
constexpr Integer kPageUp = 280;
constexpr Integer kEnd = 279;
constexpr Integer kPageDown = 281;
constexpr Integer kLControl = 306;
constexpr Integer kLShift = 304;

struct RelayEvent final : ::cw::eventhandler::InputEvent {
    Integer pygame_type{};
    bool has_key{};
    bool has_button{};
    Integer pointer_id{};
};

Integer value_integer(const ::cw::lib::Value& value, Integer fallback = 0)
{
    if (const auto* integer = std::get_if<Integer>(&value.storage())) return *integer;
    if (const auto* real = std::get_if<Real>(&value.storage())) return static_cast<Integer>(*real);
    return fallback;
}

Integer now_ticks()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

bool is_mouse_down(Integer type)
{
    return type == kMouseButtonDown || type == kPygameMouseButtonDown;
}

bool is_mouse_up(Integer type)
{
    return type == kMouseButtonUp || type == kPygameMouseButtonUp;
}

bool is_key_down(Integer type)
{
    return type == kKeyDown || type == kPygameKeyDown;
}

bool is_key_up(Integer type)
{
    return type == kKeyUp || type == kPygameKeyUp;
}

Integer handler_key(Integer key)
{
    using namespace ::cw::eventhandler;
    if(key==kLeft)return KEY_LEFT;if(key==kRight)return KEY_RIGHT;if(key==kUp)return KEY_UP;if(key==kDown)return KEY_DOWN;
    if(key==kPageUp)return KEY_PAGEUP;if(key==kPageDown)return KEY_PAGEDOWN;if(key==kHome)return KEY_HOME;if(key==kEnd)return KEY_END;
    if(key==kLShift||key==16)return KEY_LSHIFT;if(key==kLControl||key==17)return KEY_LCTRL;if(key==kPrint)return KEY_PRINT;
    if(key>=kF1&&key<=kF9)return KEY_F1+(key-kF1);if(key==static_cast<Integer>('D')||key==static_cast<Integer>('d'))return KEY_D;if(key==static_cast<Integer>('P')||key==static_cast<Integer>('p'))return KEY_P;if(key==static_cast<Integer>('C')||key==static_cast<Integer>('c'))return KEY_C;
    return key;
}

Integer relay_key_from_handler(Integer key)
{
    using namespace ::cw::eventhandler;
    // これは wx/VK の対応表ではない。正規化した P（112）は VK_F1
    // と、pygame の Print（316）は WXK_RIGHT と重なる。
    // 正規の KEY_PRINT は意図的に別の値とし、この境界でだけ変換する。
    switch (key) {
    case KEY_LEFT: return kLeft; case KEY_RIGHT: return kRight;
    case KEY_UP: return kUp; case KEY_DOWN: return kDown;
    case KEY_PAGEUP: return kPageUp; case KEY_PAGEDOWN: return kPageDown;
    case KEY_HOME: return kHome; case KEY_END: return kEnd;
    case KEY_LSHIFT: case KEY_RSHIFT: return kLShift;
    case KEY_LCTRL: case KEY_RCTRL: return kLControl;
    case KEY_PRINT: return kPrint;
    case KEY_D: return static_cast<Integer>('D');
    case KEY_P: return static_cast<Integer>('P');
    case KEY_C: return static_cast<Integer>('C');
    default:
        if (key >= KEY_F1 && key <= KEY_F9) return kF1 + (key - KEY_F1);
        return key;
    }
}

std::shared_ptr<RelayEvent> make_event(Integer type, Integer pygame_type)
{
    auto event = std::make_shared<RelayEvent>();
    event->type = type;
    event->pygame_type = pygame_type;
    event->has_key = false;
    event->has_button = false;
    event->timestamp=static_cast<Real>(now_ticks());
    if (::cw::g_cwpy) {
        event->modal_epoch = ::cw::g_cwpy->m_modal_epoch.load(std::memory_order_acquire);
        event->modal_epoch_stamped = true;
    }
    return event;
}

void post_event(::cw::eventrelay::KeyEventRelay& relay,
                const std::shared_ptr<::cw::lib::Object>& event)
{
    if (::cw::g_cwpy) {
        ::cw::thread::post_pygameevent(event);
    } else {
        // CWPy 初期化前も Python の post 呼出しを失わないよう保留する。
        relay.m_posted_events.push_back(event);
    }
}

// 呼び出し元が relay.m_mutex を保持する。 モーダル境界の全消去
// と異なり、入力列内のキャンセルでは完了済みタップと印の後のイベントを残す。
void cancel_held_pointer_state(::cw::eventrelay::KeyEventRelay& relay)
{
    std::fill(relay.m_mouse_buttondown.begin(), relay.m_mouse_buttondown.end(), false);
    std::fill(relay.m_mousein.begin(), relay.m_mousein.end(), 0);
    relay.m_flick_status = ::cw::lib::Value(::cw::frame::g_FLICK_NONE);
    relay.m_flick_sprite.reset();
}

std::shared_ptr<RelayEvent> convert_native(const ::cw::eventrelay::KeyEventRelay::NativeEvent& native)
{
    Integer type = native.type;
    Integer pygame_type = native.type;
    if (is_mouse_down(native.type)) {
        type = kMouseButtonDown;
        pygame_type = kPygameMouseButtonDown;
    } else if (is_mouse_up(native.type)) {
        type = kMouseButtonUp;
        pygame_type = kPygameMouseButtonUp;
    } else if (is_key_down(native.type)) {
        type = kKeyDown;
        pygame_type = kPygameKeyDown;
    } else if (is_key_up(native.type)) {
        type = kKeyUp;
        pygame_type = kPygameKeyUp;
    }
    auto event = make_event(type, pygame_type);
    event->key = native.key;
    event->button = native.button;
    event->has_key = native.has_key;
    event->has_button = native.has_button;
    event->ignore_up = native.ignoreup;
    event->timestamp=native.timestamp>0.0?native.timestamp:static_cast<Real>(now_ticks());
    event->pointer_id=native.pointer_id;
    event->touch=native.touch;
    if(event->has_key)event->key=handler_key(event->key);
    event->pos = {native.x, native.y};
    if (native.modal_epoch_stamped) {
        event->modal_epoch = native.modal_epoch;
        event->modal_epoch_stamped = true;
    }
    return event;
}

Integer legacy_sdl_key(Integer key)
{
#if CW_EVENTRELAY_HAS_SDL3
    switch(::cw::lib::engine_sdl_key(static_cast<SDL_Keycode>(key))){
    case SDLK_RETURN:return kReturn;case SDLK_ESCAPE:return kEscape;case SDLK_BACKSPACE:return kBackspace;case SDLK_SPACE:return kSpace;case SDLK_BACKSLASH:return kBackslash;
    case SDLK_LEFT:return kLeft;case SDLK_RIGHT:return kRight;case SDLK_UP:return kUp;case SDLK_DOWN:return kDown;case SDLK_HOME:return kHome;case SDLK_END:return kEnd;case SDLK_PAGEUP:return kPageUp;case SDLK_PAGEDOWN:return kPageDown;
    case SDLK_LSHIFT:case SDLK_RSHIFT:return kLShift;case SDLK_LCTRL:case SDLK_RCTRL:return kLControl;case SDLK_F1:return kF1;case SDLK_F2:return kF2;case SDLK_F3:return kF3;case SDLK_F4:return kF4;case SDLK_F5:return kF5;case SDLK_F6:return kF6;case SDLK_F7:return kF7;case SDLK_F8:return kF8;case SDLK_F9:return kF9;case SDLK_F10:return kF10;case SDLK_F11:return kF11;case SDLK_F12:return kF12;case SDLK_PRINTSCREEN:return kPrint;default:return key;
    }
#else
    return key;
#endif
}

class SdlEventRelayBackend final : public ::cw::eventrelay::EventRelayBackend {
public:
    void poll(const Sink& sink) override {
#if CW_EVENTRELAY_HAS_SDL3
        // SDL_PollEvent はネイティブのイベントキューを処理し、SDL3 では
        // メインスレッド専用である。 ゲームループがワーカースレッドで動く時、Win32
        // のイベントは Frame::WndProc を通してこの Relay へ届く。
        if (!SDL_IsMainThread()) return;
        SDL_Event event{};
        while(SDL_PollEvent(&event)){
            // SDL の二つのイベント取得入口の双方で、ゲーム
            // へ入力を渡す前に外部の共通ダイアログを優先する。
            if (::cw::dialog::dispatch_standalone_presentation_event(event)) continue;
            ::cw::eventrelay::RelayNativeEvent out{};
            const auto touch = ::cw::lib::translate_game_touch(event);
            using Touch = ::cw::lib::SdlGameTouchInput;
            if (touch.kind != Touch::NotTouch) {
                if (touch.kind == Touch::Ignore) continue;
                out.type = touch.kind == Touch::Down ? kMouseButtonDown :
                    touch.kind == Touch::Up ? kMouseButtonUp :
                    touch.kind == Touch::Cancel ? ::cw::eventhandler::EVENT_POINTERCANCEL : 0;
                out.has_button = touch.kind == Touch::Down || touch.kind == Touch::Up;
                out.button = out.has_button ? 1 : 0;
                out.has_position = touch.kind != Touch::Cancel;
                out.x = touch.x; out.y = touch.y; out.touch = true;
                out.pointer_id = static_cast<Integer>(touch.finger);
                out.timestamp = ::cw::lib::sdl_input_timestamp_ms(touch.timestamp);
                sink(std::move(out));
                // タッチをキャンセルした生存期間イベントを飲み込まない。
                // 所有する子ウィンドウのイベントは変換前に除外済みである。
                if (touch.kind != Touch::Cancel) continue;
                out = {};
            }
            switch(event.type){
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                // Presentation 所有の閉じる要求は上で処理済みである
                // 。 ここでは Engine フレームの通常の終了経路を使う。
                ::cw::frame::Frame__bind_panel_close(nullptr);continue;
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                // Win32/Python と同じフォーカス境界を
                // 使い、 タッチ操作中でなくてもキーを解除する。
                ::cw::frame::Frame__bind_panel_killfocus(nullptr);continue;
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
                ::cw::frame::Frame__bind_panel_setfocus(nullptr);continue;
            case SDL_EVENT_DISPLAY_CONTENT_SCALE_CHANGED:
                ::cw::lib::ui::set_system_dialog_scale(
                    SDL_GetDisplayContentScale(event.display.displayID));continue;
            case SDL_EVENT_WINDOW_MINIMIZED:case SDL_EVENT_WINDOW_HIDDEN:
            case SDL_EVENT_WILL_ENTER_BACKGROUND:
            case SDL_EVENT_DID_ENTER_BACKGROUND:
                ::cw::frame::Frame_set_app_suspended(true,
                    event.type == SDL_EVENT_WINDOW_MINIMIZED ? ::cardwirth::platform::AppVisibility::Minimized :
                    event.type == SDL_EVENT_WINDOW_HIDDEN ? ::cardwirth::platform::AppVisibility::Hidden :
                    ::cardwirth::platform::AppVisibility::Background);continue;
            case SDL_EVENT_WINDOW_RESTORED:case SDL_EVENT_WINDOW_MAXIMIZED:case SDL_EVENT_WINDOW_SHOWN:
            case SDL_EVENT_DID_ENTER_FOREGROUND:
                ::cw::frame::Frame_set_app_suspended(false,
                    event.type == SDL_EVENT_WINDOW_RESTORED || event.type == SDL_EVENT_WINDOW_MAXIMIZED
                        ? ::cardwirth::platform::AppVisibility::Minimized :
                    event.type == SDL_EVENT_WINDOW_SHOWN ? ::cardwirth::platform::AppVisibility::Hidden :
                    ::cardwirth::platform::AppVisibility::Background);continue;
            case SDL_EVENT_KEY_DOWN:case SDL_EVENT_KEY_UP:out.type=event.type==SDL_EVENT_KEY_DOWN?kKeyDown:kKeyUp;out.key=legacy_sdl_key(static_cast<Integer>(event.key.key));out.has_key=true;out.timestamp=::cw::lib::sdl_input_timestamp_ms(event.key.timestamp);break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:case SDL_EVENT_MOUSE_BUTTON_UP:
                // SDL は touch を mouse としても合成するため、finger 側との二重配送を除く。
                if(event.button.which==SDL_TOUCH_MOUSEID)continue;
                if(event.button.button>3)continue;
                out.type=event.type==SDL_EVENT_MOUSE_BUTTON_DOWN?kMouseButtonDown:kMouseButtonUp;out.button=event.button.button;out.x=static_cast<Integer>(event.button.x);out.y=static_cast<Integer>(event.button.y);out.has_button=true;out.has_position=true;out.timestamp=::cw::lib::sdl_input_timestamp_ms(event.button.timestamp);break;
            case SDL_EVENT_MOUSE_MOTION:
                if(event.motion.which==SDL_TOUCH_MOUSEID)continue;
                out.type=0;out.x=static_cast<Integer>(event.motion.x);out.y=static_cast<Integer>(event.motion.y);out.has_position=true;out.timestamp=::cw::lib::sdl_input_timestamp_ms(event.motion.timestamp);break;
            case SDL_EVENT_MOUSE_WHEEL:{
                if(event.wheel.which==SDL_TOUCH_MOUSEID)continue;
                auto amount=event.wheel.integer_y;
                if(!amount)amount=event.wheel.y>0.0f?1:(event.wheel.y<0.0f?-1:0);
                if(event.wheel.direction==SDL_MOUSEWHEEL_FLIPPED)amount=-amount;
                if(!amount)continue;
                out.type=kMouseButtonUp;out.button=amount>0?4:5;out.has_button=true;
                out.x=static_cast<Integer>(std::lround(event.wheel.mouse_x));
                out.y=static_cast<Integer>(std::lround(event.wheel.mouse_y));out.has_position=true;
                out.timestamp=::cw::lib::sdl_input_timestamp_ms(event.wheel.timestamp);break;
            }
            default:continue;
            }
            sink(std::move(out));
        }
#else
        (void)sink;
#endif
    }
};
std::mutex g_backend_mutex{};
std::shared_ptr<::cw::eventrelay::EventRelayBackend> g_backend{};

}  // namespace

namespace cw::eventrelay {

std::shared_ptr<EventRelayBackend> make_default_eventrelay_backend(){return std::make_shared<SdlEventRelayBackend>();}
std::shared_ptr<EventRelayBackend> eventrelay_backend(){std::lock_guard<std::mutex> lock(g_backend_mutex);if(!g_backend)g_backend=make_default_eventrelay_backend();return g_backend;}
void set_eventrelay_backend(std::shared_ptr<EventRelayBackend> backend){std::lock_guard<std::mutex> lock(g_backend_mutex);g_backend=backend?std::move(backend):make_default_eventrelay_backend();}

void KeyEventRelay::initialize()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    m_keymap.clear();
    m_keymap_int.clear();
    m_keyin_state.assign(322, 0);
    m_native_events.clear();
    m_posted_events.clear();
    m_backend=eventrelay_backend();
    m_mousein.assign(3, 0);
    m_mouse_buttondown.assign(3, false);
    m_threshold = 1;
    m_nokeyupevent = false;
    m_last_native.reset();
    m_flick_status = ::cw::lib::Value(::cw::frame::g_FLICK_NONE);
    m_flick_sprite.reset();
    m_flick_start_pos = {::cw::lib::Value(-1), ::cw::lib::Value(-1)};
    m_flick_start_time = 0.0;

    const auto add_key = [this](Integer wx_key, Integer pygame_key) {
        m_keymap_int[wx_key] = pygame_key;
        // ValueMap は整数キーを持てないため、診断・スクリプト境界用に
        // 文字列表現も同期する。実処理は m_keymap_int を参照する。
        m_keymap[std::to_string(wx_key)] = ::cw::lib::Value(pygame_key);
    };

    // wx.WXK_* → pygame.K_*。Win32 VK_* と EventHandler の KEY_* も受け付け、
    // Frame::OnKey* / Debugger の既存呼出しが keyin を更新できるようにする。
    add_key(370, kReturn);  // WXK_NUMPAD_ENTER
    add_key(13, kReturn);   // WXK_RETURN / KEY_RETURN / VK_RETURN
    add_key(27, kEscape);
    add_key(8, kBackspace);
    add_key(32, kSpace);
    const Integer pygame_fkeys[] = {kF1, kF2, kF3, kF4, kF5, kF6, kF7, kF8, kF9, kF10, kF11, kF12};
    for (Integer index = 0; index < 12; ++index) {
        add_key(340 + index, pygame_fkeys[index]);  // wx.WXK_F1..F12
        add_key(112 + index, pygame_fkeys[index]);  // Win32 VK_F1..F12
        add_key(1101 + index, pygame_fkeys[index]); // eventhandler::KEY_F1..F12
    }
    add_key(315, kUp);       // WXK_UP
    add_key(317, kDown);     // WXK_DOWN
    add_key(314, kLeft);     // WXK_LEFT
    add_key(316, kRight);    // WXK_RIGHT
    add_key(38, kUp);        // VK_UP
    add_key(40, kDown);      // VK_DOWN
    add_key(37, kLeft);      // VK_LEFT
    add_key(39, kRight);     // VK_RIGHT
    add_key(1002, kUp);      // KEY_UP
    add_key(1003, kDown);    // KEY_DOWN
    add_key(1000, kLeft);    // KEY_LEFT
    add_key(1001, kRight);   // KEY_RIGHT
    add_key(::cw::eventhandler::KEY_PRINT, kPrint); // WXK_RIGHT とは異なる正規のキー値。
    add_key(321, kPrint);    // WXK_SNAPSHOT
    add_key(319, kPrint);    // WXK_PRINT
    add_key(44, kPrint);     // VK_SNAPSHOT
    add_key(306, kLShift);   // WXK_SHIFT
    add_key(16, kLShift);    // Win32 VK_SHIFT
    add_key(1010, kLShift);  // KEY_LSHIFT
    add_key(1011, kLShift);  // KEY_RSHIFT (Python の keymap も右Shiftを左へ畳む)
    add_key(308, kLControl); // WXK_CONTROL
    add_key(17, kLControl);  // Win32 VK_CONTROL
    add_key(1012, kLControl); // KEY_LCTRL
    add_key(1013, kLControl); // KEY_RCTRL
    add_key(366, kPageUp);   // WXK_PAGEUP
    add_key(367, kPageDown); // WXK_PAGEDOWN
    add_key(33, kPageUp);    // VK_PRIOR
    add_key(34, kPageDown);  // VK_NEXT
    add_key(1004, kPageUp);  // KEY_PAGEUP
    add_key(1005, kPageDown); // KEY_PAGEDOWN
    add_key(313, kHome);     // WXK_HOME
    add_key(312, kEnd);      // WXK_END
    add_key(36, kHome);      // VK_HOME
    add_key(35, kEnd);       // VK_END
    add_key(1006, kHome);    // KEY_HOME
    add_key(1007, kEnd);     // KEY_END
    add_key(static_cast<Integer>('\\'), kBackslash);
    add_key(220, kBackslash); // VK_OEM_5
    add_key(static_cast<Integer>('D'), static_cast<Integer>('D'));
    add_key(static_cast<Integer>('P'), static_cast<Integer>('P'));
    add_key(static_cast<Integer>('C'), static_cast<Integer>('C'));
    // 旧 Frame::OnKeyUp が P を 300 に変換していた互換入口。
    add_key(300, static_cast<Integer>('P'));
}

void KeyEventRelay::clear()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_keyin_state.assign(322, 0);
    m_mousein.assign(3, 0);
    // Python 版と同じく mouse_buttondown と native event queue は保持する。
    m_nokeyupevent = false;
    m_last_native.reset();
}

void KeyEventRelay::cancel_pointer_sequence(bool include_keys)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    // モーダル終了時は、以前の入力列全体を不可分に破棄する。 通
    // 常のポインターキャンセルでは無関係な物理キーを解除しない。
    if (include_keys) {
        std::fill(m_keyin_state.begin(), m_keyin_state.end(), 0);
        m_nokeyupevent = false;
    }
    // Python/wx は押下・解除の組をモーダルのイベントループ内で処理する。
    // C++ の UI・ゲーム分離では HWND が消えた後も片方がキューに残る場合
    // があるため、Relay の境界でポインター操作全体を明示的にキャンセルする。
    std::fill(m_mouse_buttondown.begin(), m_mouse_buttondown.end(), false);
    std::fill(m_mousein.begin(), m_mousein.end(), 0);
    m_native_events.erase(
        std::remove_if(m_native_events.begin(), m_native_events.end(),
                       [include_keys](const NativeEvent& event) {
                           return event.has_button || event.has_position ||
                                  event.type == ::cw::eventhandler::EVENT_NONE ||
                                  event.type == ::cw::eventhandler::EVENT_POINTERCANCEL ||
                                  is_mouse_down(event.type) || is_mouse_up(event.type) ||
                                  (include_keys && (event.has_key || is_key_down(event.type) || is_key_up(event.type)));
                       }),
        m_native_events.end());
    m_posted_events.erase(
        std::remove_if(m_posted_events.begin(), m_posted_events.end(),
                       [include_keys](const auto& object) {
                           const auto event = std::dynamic_pointer_cast<::cw::eventhandler::InputEvent>(object);
                           // ゲームの受信箱と同じポインター分類を使
                           // う。 移動と座標なしのキャンセルも破棄
                           // する操作に属し、次の画面には渡さない。
                           return event && (event->type == ::cw::eventhandler::EVENT_NONE ||
                               event->type == ::cw::eventhandler::EVENT_POINTERCANCEL ||
                               is_mouse_down(event->type) || is_mouse_up(event->type) ||
                               (include_keys && (is_key_down(event->type) || is_key_up(event->type))));
                       }),
        m_posted_events.end());
    m_last_native.reset();
    m_flick_status = ::cw::lib::Value(::cw::frame::g_FLICK_NONE);
    m_flick_sprite.reset();
}

void KeyEventRelay::suppress_mouse_repeat(Integer arg_button)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const Integer index = arg_button - 1;
    if (0 <= index && index < static_cast<Integer>(m_mousein.size()))
        m_mousein[static_cast<std::size_t>(index)] = 0;
}

void KeyEventRelay::enqueue_native_event(NativeEvent arg_event)
{
    // モーダル遷移後にキューから取り出す時ではなく、受信時に世代を記録
    // する。 生成側で明示的に記録されたイベントは元の世代を維持する。
    if (!arg_event.modal_epoch_stamped && ::cw::g_cwpy) {
        arg_event.modal_epoch = ::cw::g_cwpy->m_modal_epoch.load(std::memory_order_acquire);
        arg_event.modal_epoch_stamped = true;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    m_native_events.push_back(std::move(arg_event));
}

bool KeyEventRelay::apply_normalized_input(Integer arg_type, Integer arg_key, Integer arg_button,
                                          std::optional<std::uint64_t> modal_epoch)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    // モーダルのキャンセルと同じロックを保持して確認する。 ハンドラ
    // ーが拒否する前に、遅れた解除で新しい世代のキーを解放しない。
    if (modal_epoch && ::cw::g_cwpy &&
        (*modal_epoch != ::cw::g_cwpy->m_modal_epoch.load(std::memory_order_acquire) ||
         ::cw::g_cwpy->is_modal_input_blocked())) return false;
    if (arg_type == ::cw::eventhandler::EVENT_POINTERCANCEL) {
        cancel_held_pointer_state(*this);
        return true;
    }
    if (arg_type == kKeyDown || arg_type == kKeyUp) {
        // EventHandler は正規化イベント用の独自 KEY_* を
        // 使うが、m_keyin_state は pygame の整数キーに
        // 従う。 この変換は Frame の wx/VK の対応表と分ける。
        const Integer pygame_key = relay_key_from_handler(arg_key);
        if (pygame_key < 0 || pygame_key >= static_cast<Integer>(m_keyin_state.size())) return true;
        auto& state = m_keyin_state[static_cast<std::size_t>(pygame_key)];
        if (arg_type == kKeyDown) {
            const bool first_down = state == 0;
            if (state == 0) state = 1;
            else if (state <= m_threshold + 1) ++state;
            // Python の keydown と同様、解除から押下への変化だけを送る。 その
            // 後の OS のリピートは is_keyin に渡し、直接操作も重ねて実行しない。
            return first_down;
        } else {
            state = 0;
            m_nokeyupevent = false;
        }
        return true;
    }
    if ((arg_type == kMouseButtonDown || arg_type == kMouseButtonUp) &&
        arg_button >= 1 && arg_button <= static_cast<Integer>(m_mouse_buttondown.size())) {
        m_mouse_buttondown[static_cast<std::size_t>(arg_button - 1)] =
            arg_type == kMouseButtonDown;
    }
    return true;
}

void KeyEventRelay::peek_mousestate()
{
    if(!m_backend)m_backend=eventrelay_backend();
    if (m_backend) m_backend->poll([this](RelayNativeEvent source) {
        if (!source.modal_epoch_stamped && ::cw::g_cwpy) {
            source.modal_epoch = ::cw::g_cwpy->m_modal_epoch.load(std::memory_order_acquire);
            source.modal_epoch_stamped = true;
        }
        // SDL には Frame::OnKeyDown/Up の生成側がないため、
        // 他の SDL アダプターと同様、ネイティブ境界でキー状態を更新する。 m_native_events
        // を取り出す時まで遅らせると、イベント取得中のフォーカス喪失
        // による消去が古い押下で取り消されるため、その場で更新する。
        if (source.has_key && (is_key_down(source.type) || is_key_up(source.type)) &&
            !apply_normalized_input(is_key_down(source.type) ? kKeyDown : kKeyUp,
                                    handler_key(source.key), 0,
                                    source.modal_epoch_stamped
                                        ? std::optional<std::uint64_t>(source.modal_epoch)
                                        : std::nullopt)) return;
        NativeEvent event;
        static_cast<RelayNativeEvent&>(event) = std::move(source);
        enqueue_native_event(std::move(event));
    });
    std::lock_guard<std::mutex> lock(m_mutex);
    if(::cw::g_cwpy&&!m_posted_events.empty()){for(auto& event:m_posted_events)::cw::thread::post_pygameevent(event);m_posted_events.clear();}
    while (!m_native_events.empty()) {
        const NativeEvent native = m_native_events.front();
        m_native_events.pop_front();
        // マウスの押下・キャンセルも下で押下状態を変える。 ゲーム側で
        // 処理する時だけでなく、その変更より前に古い世代を拒否する。
        if (native.modal_epoch_stamped && ::cw::g_cwpy &&
            (native.modal_epoch != ::cw::g_cwpy->m_modal_epoch.load(std::memory_order_acquire) ||
             ::cw::g_cwpy->is_modal_input_blocked())) continue;
        const auto signature=std::make_tuple(native.type,native.key,native.button,native.x,native.y,
                                             native.timestamp,native.pointer_id,native.touch);
        if(native.timestamp>0.0&&m_last_native&&*m_last_native==signature)continue;
        m_last_native=signature;
        const auto event = convert_native(native);
        if (native.type == ::cw::eventhandler::EVENT_POINTERCANCEL) {
            cancel_held_pointer_state(*this);
            post_event(*this, event);
        } else if (is_mouse_up(native.type) && native.has_button && native.button >= 1 && native.button <= static_cast<Integer>(m_mouse_buttondown.size())) {
            // Python peek_mousestate は対応する DOWN がある UP だけ再投入し、
            // mouse_buttondown は落とさない。
            if (m_mouse_buttondown[static_cast<std::size_t>(native.button - 1)]) post_event(*this, event);
        } else if (is_mouse_down(native.type) && native.has_button && native.button >= 1 && native.button <= static_cast<Integer>(m_mouse_buttondown.size())) {
            m_mouse_buttondown[static_cast<std::size_t>(native.button - 1)] = true;
            post_event(*this, event);
        } else {
            // キー状態は Frame または上の SDL 入力処理で更新
            // 済みである。 互換キューから取り出す際に二重に更新しない。
            post_event(*this, event);
        }
    }
}

void KeyEventRelay::enqueue_win32_message(Integer message,Integer wparam,Integer lparam,Real timestamp)
{
    NativeEvent event{};event.timestamp=timestamp>0.0?timestamp:static_cast<Real>(now_ticks());
    const auto point=[&](){event.x=static_cast<short>(lparam&0xffff);event.y=static_cast<short>((lparam>>16)&0xffff);event.has_position=true;};
    if(message==0x0100||message==0x0104||message==0x0101||message==0x0105){
        event.type=(message==0x0100||message==0x0104)?kKeyDown:kKeyUp;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            const auto it=m_keymap_int.find(wparam);
            if(it==m_keymap_int.end())return;
            event.key=it->second;
        }
        event.has_key=true;
    }
    else if(message==0x0201||message==0x0202){event.type=message==0x0201?kMouseButtonDown:kMouseButtonUp;event.button=1;event.has_button=true;point();}
    else if(message==0x0204||message==0x0205){event.type=message==0x0204?kMouseButtonDown:kMouseButtonUp;event.button=3;event.has_button=true;point();}
    else if(message==0x0207||message==0x0208){event.type=message==0x0207?kMouseButtonDown:kMouseButtonUp;event.button=2;event.has_button=true;point();}
    else if(message==0x0245||message==0x0246||message==0x0247){event.type=message==0x0246?kMouseButtonDown:(message==0x0247?kMouseButtonUp:0);event.button=message==0x0245?0:1;event.has_button=message!=0x0245;event.touch=true;event.pointer_id=wparam&0xffff;point();}
    else return;
    enqueue_native_event(std::move(event));
}

void KeyEventRelay::keydown(Integer arg_keycode)
{
    const auto runtime = ::cw::g_cwpy;
    const auto epoch = runtime ? runtime->m_modal_epoch.load(std::memory_order_acquire) : 0;
    std::lock_guard<std::mutex> lock(m_mutex);
    // Frame の入力判定はこのロックより前に行われる。 生成側が待っている間に
    // モーダル終了で古いキーを破棄する場合があるため、キャンセル後に復元しない。
    if (runtime && (epoch != runtime->m_modal_epoch.load(std::memory_order_acquire) ||
                    runtime->is_modal_input_blocked())) return;
    const auto found = m_keymap_int.find(arg_keycode);
    if (found == m_keymap_int.end()) return;
    const Integer key = found->second;
    if (key < 0 || key >= static_cast<Integer>(m_keyin_state.size())) throw std::out_of_range("KeyEventRelay::keydown key");
    if (m_keyin_state[static_cast<std::size_t>(key)] == 0) {
        const auto event = make_event(kKeyDown, kPygameKeyDown);
        event->key = handler_key(key);
        event->has_key = true;
        post_event(*this, event);
    }
    if (m_keyin_state[static_cast<std::size_t>(key)] <= m_threshold + 1) {
        ++m_keyin_state[static_cast<std::size_t>(key)];
    }
    if(key==kLControl&&arg_keycode>=0&&arg_keycode<static_cast<Integer>(m_keyin_state.size()))m_keyin_state[static_cast<std::size_t>(arg_keycode)]=m_keyin_state[static_cast<std::size_t>(key)];
}

void KeyEventRelay::keyup(Integer arg_keycode)
{
    const auto runtime = ::cw::g_cwpy;
    const auto epoch = runtime ? runtime->m_modal_epoch.load(std::memory_order_acquire) : 0;
    std::lock_guard<std::mutex> lock(m_mutex);
    // 正規化した SDL 経路と同様、押下状態を変更する前に拒否する。
    if (runtime && (epoch != runtime->m_modal_epoch.load(std::memory_order_acquire) ||
                    runtime->is_modal_input_blocked())) return;
    const auto found = m_keymap_int.find(arg_keycode);
    if (found == m_keymap_int.end()) return;
    const Integer key = found->second;
    if (key < 0 || key >= static_cast<Integer>(m_keyin_state.size())) throw std::out_of_range("KeyEventRelay::keyup key");
    const auto event = make_event(kKeyUp, kPygameKeyUp);
    event->key = handler_key(key);
    event->has_key = true;
    post_event(*this, event);
    m_keyin_state[static_cast<std::size_t>(key)] = 0;
    if(key==kLControl&&arg_keycode>=0&&arg_keycode<static_cast<Integer>(m_keyin_state.size()))m_keyin_state[static_cast<std::size_t>(arg_keycode)]=0;
    m_nokeyupevent = false;
}

std::vector<Integer> KeyEventRelay::get_pressed()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_keyin_state;
}

bool KeyEventRelay::is_keyin(Integer arg_keycode)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (arg_keycode < 0 || arg_keycode >= static_cast<Integer>(m_keyin_state.size())) {
        // Python のリスト添字と異なり、ここでは負のキーコードを末尾のキ
        // ーとして扱わない。 Engine の呼び出し側は非負の pygame
        // キー番号を渡すため、負の識別子は無関係なキーに対応付けず拒否する。
        throw std::out_of_range("KeyEventRelay::is_keyin keycode");
    }
    auto& key_state = m_keyin_state[static_cast<std::size_t>(arg_keycode)];
    if (m_threshold + 1 == key_state) {
        const bool autoenter = ::cw::g_cwpy && ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_autoenter_on_sprite;
        if (!autoenter && !m_nokeyupevent) {
            const auto event = make_event(kKeyUp, kPygameKeyUp);
            event->key = handler_key(arg_keycode);
            event->has_key = true;
            post_event(*this, event);
        }
        ++key_state;
    }
    if (m_threshold < key_state) {
        m_nokeyupevent = false;
        return true;
    }
    return false;
}

bool KeyEventRelay::is_mousein(std::optional<Integer> arg_button)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const Integer button_number = arg_button.value_or(1);
    const auto setting = ::cw::g_cwpy ? ::cw::g_cwpy->m_setting : nullptr;
    if (!setting || !setting->m_can_repeatlclick) return false;

    const Integer index = button_number - 1;
    if (index < 0 || index >= static_cast<Integer>(m_mousein.size())) return false;
    const auto pressed = ::cw::g_cwpy->m_mousein;
    const bool app_pressed = (index == 0 ? std::get<0>(pressed) : (index == 1 ? std::get<1>(pressed) : std::get<2>(pressed)));
    auto& state = m_mousein[static_cast<std::size_t>(index)];
    if (state == 0 || !app_pressed) {
        // Python は押下が解けた時に自分のタイムスタンプを0へ戻す。
        if (!app_pressed) state = 0;
        return false;
    }
    if (state != -1) {
        const Integer fps = setting->m_fps;
        // Setting の既定値は60。壊れた設定でのゼロ除算だけは防ぎ、
        // 通常経路では Python の 1000/fps*40 をそのまま適用する。
        const Real mouse_threshold = fps > 0 ? 1000.0 / static_cast<Real>(fps) * 40.0 : 0.0;
        if (static_cast<Real>(state) + mouse_threshold <= static_cast<Real>(now_ticks())) {
            if (!setting->m_autoenter_on_sprite) {
                const auto event = make_event(kMouseButtonUp, kPygameMouseButtonUp);
                event->button = button_number;
                event->has_button = true;
                event->ignore_up = true;
                event->pos = {
                    value_integer(std::get<0>(::cw::g_cwpy->m_mousepos), 0),
                    value_integer(std::get<1>(::cw::g_cwpy->m_mousepos), 0)};
                post_event(*this, event);
            }
            state = -1;
        }
    }
    return state == -1;
}

void main()
{
    // Python の main は import 時の副作用を持たない。
}

}  // namespace cw::eventrelay
