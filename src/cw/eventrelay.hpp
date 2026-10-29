#pragma once

// cw/eventrelay.py のwxキー／pygameイベント境界をSDL非依存で保持する宣言。
#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"

#include <deque>
#include <map>
#include <mutex>
#include <vector>

namespace cw::eventrelay {
struct RelayNativeEvent;
class EventRelayBackend : public ::cw::lib::Object {
public:
    using Sink = std::function<void(RelayNativeEvent)>;
    virtual ~EventRelayBackend() = default;
    virtual void poll(const Sink& arg_sink) = 0;
};
std::shared_ptr<EventRelayBackend> make_default_eventrelay_backend();
std::shared_ptr<EventRelayBackend> eventrelay_backend();
void set_eventrelay_backend(std::shared_ptr<EventRelayBackend> arg_backend);

struct RelayNativeEvent {
    ::cw::lib::Integer type{}, key{}, button{}, x{}, y{};
    ::cw::lib::Real timestamp{};
    ::cw::lib::Integer pointer_id{};
    bool has_key{}, has_button{}, has_position{}, ignoreup{}, touch{};
    // ネイティブの待ち行列を通る間も、受け取り先のモーダル世代を保つ。
    std::uint64_t modal_epoch{};
    bool modal_epoch_stamped{};
};

class KeyEventRelay : public ::cw::lib::Object {
public:
    struct NativeEvent : RelayNativeEvent {
        // wx/SDL adapter がこの境界へ渡す最小イベント表現。type は
        // pygame (1025/1026) と互換層 (1/2) のどちらでも受け付ける。
    };

    KeyEventRelay() = default;
    virtual ~KeyEventRelay() = default;
    std::optional<std::shared_ptr<::cw::sprite::base::SelectableSprite>> m_flick_sprite{};
    std::tuple<::cw::lib::Value, ::cw::lib::Value> m_flick_start_pos{};
    ::cw::lib::Real m_flick_start_time{};
    ::cw::lib::Value m_flick_status{};
    ::cw::lib::Value m_keyin{};
    ::cw::lib::ValueMap m_keymap{};
    std::vector<bool> m_mouse_buttondown{};
    std::vector<::cw::lib::Integer> m_mousein{};
    bool m_nokeyupevent{};
    ::cw::lib::Integer m_threshold{};

    // Value/ValueMap は Python の動的属性を表す互換ABIとして残し、
    // 整数キーと固定長キー配列は型付き状態で保持する。
    std::map<::cw::lib::Integer, ::cw::lib::Integer> m_keymap_int{};
    std::vector<::cw::lib::Integer> m_keyin_state{};
    std::deque<NativeEvent> m_native_events{};
    std::vector<std::shared_ptr<::cw::lib::Object>> m_posted_events{};
    std::shared_ptr<EventRelayBackend> m_backend{};
    std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer,
                             ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Real,
                             ::cw::lib::Integer, bool>> m_last_native{};
    mutable std::mutex m_mutex{};

    void initialize();
    void clear();
    void cancel_pointer_sequence(bool include_keys = false);
    void suppress_mouse_repeat(::cw::lib::Integer arg_button);
    void peek_mousestate();
    void keydown(::cw::lib::Integer arg_keycode);
    void keyup(::cw::lib::Integer arg_keycode);
    std::vector<::cw::lib::Integer> get_pressed();
    bool is_keyin(::cw::lib::Integer arg_keycode);
    bool is_mousein(std::optional<::cw::lib::Integer> arg_button);

    // SDL/wx の実装から pygame.event.get() 相当を供給する入口。
    void enqueue_native_event(NativeEvent arg_event);
    // UIスレッドのSDL入力取得は、正規化したイベントをEventHandler
    // のCWPy受信箱へ送る。 ここではキー・ボタンの押下状態だけを反映し
    // 、イベントの実行はゲームスレッドに限定する。 イベントを実行へ渡す
    // べきかを返す。繰り返されたネイティブDOWNはしきい値を更新するが
    // 、 PythonのKEYDOWN操作を重ねて実行するものではない。
    bool apply_normalized_input(::cw::lib::Integer arg_type,
                                ::cw::lib::Integer arg_key,
                                ::cw::lib::Integer arg_button,
                                std::optional<std::uint64_t> modal_epoch = std::nullopt);
    void enqueue_win32_message(::cw::lib::Integer arg_message, ::cw::lib::Integer arg_wparam,
                               ::cw::lib::Integer arg_lparam, ::cw::lib::Real arg_timestamp = 0.0);
};

void main();
}

