#pragma once

#include "types.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace cwxeditor::lib {

enum class Key {
    Unknown, Backspace, Tab, Enter, Escape, Space, Insert, Delete, Home, End,
    PageUp, PageDown, Left, Right, Up, Down, Shift, Control, Alt, Meta,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12, F13, F14, F15,
    A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V,
    W, X, Y, Z, Digit0, Digit1, Digit2, Digit3, Digit4, Digit5, Digit6,
    Digit7, Digit8, Digit9,
};

enum class Modifiers : std::uint32_t {
    None = 0,
    Shift = 1U << 0U,
    Control = 1U << 1U,
    Alt = 1U << 2U,
    Meta = 1U << 3U,
    CapsLock = 1U << 4U,
    NumLock = 1U << 5U,
};

constexpr Modifiers operator|(Modifiers left, Modifiers right) noexcept {
    return static_cast<Modifiers>(static_cast<std::uint32_t>(left) |
                                  static_cast<std::uint32_t>(right));
}

enum class PointerType { Mouse, Touch, Pen };
enum class PointerButton { None, Primary, Secondary, Middle, Back, Forward };
enum class PointerAction { Down, Move, Up, Enter, Leave, Cancel, DoubleClick, LongPress };
// ネイティブのフォーカス喪失による終了と、明示的な巻き戻しを区別する。
enum class PointerCancelReason { Explicit, WindowDeactivated };

// コマンドを起こした操作の情報。メニュー・ツールバーはSDL
// やネイティブハンドル、 イベントの寿命を知らずに、Shift
// ・中クリック付きでも同じコマンドを使える。
struct CommandGesture {
    Modifiers modifiers{Modifiers::None};
    PointerButton button{PointerButton::None};
    // ネイティブの物理Ctrl。nulloptなら合成・他バックエンドの扱いを維持する。
    std::optional<bool> physical_control;
    // このコマンドの設定済みショートカットが消費した修飾キー。 DのisMenuAccelerating
    // は、グラフ操作の判定からこれらのビットだけを除く。
    Modifiers accelerator_modifiers{Modifiers::None};
    // 修飾キーがなくても、設定済みショートカットならtrue。
    bool is_accelerator{};
};

struct KeyEvent {
    Key key{Key::Unknown};
    Modifiers modifiers{Modifiers::None};
    bool repeat{};
    bool handled{};
    // Keyは共通の移動・コマンド用の列挙値として保つ。
    // +・[・;など列挙値のない配列依存の記号は、バ
    // ックエンドが表示可能なキー文字としてここへ残す。
    std::string printable;
    std::optional<bool> physical_control;
    // 監視側の代替処理向けの、キー配列変換後の一文字。 ショ
    // ートカットの表示名でも、IMEの確定文字列でもない。
    std::string key_character;
};

struct TextInputEvent {
    std::string text;
    bool composing{};
    bool handled{};
};

struct PointerEvent {
    std::uint64_t pointer_id{};
    PointerType pointer_type{PointerType::Mouse};
    PointerButton button{PointerButton::None};
    PointerAction action{PointerAction::Move};
    Point position{};
    // 画面・ホスト座標の位置。デスクトップは独立プレビ
    // ューを行の隣へ置くためネイティブ画面座標を渡す。
    // 単一画面の実装はpositionと同じ座標を使
    // う。(0,0)も有効で、取得可否は値と別に示す。
    Point screen_position{};
    // ポインター下のネイティブ項目。-1は見
    // 出し・空白を表す。 item_hit_test_available
    // で、正確な項目判定自体に
    // 未対応の実装と区別する。
    std::int64_t item_index{-1};
    bool item_hit_test_available{};
    Modifiers modifiers{Modifiers::None};
    float pressure{};
    bool handled{};
    // 元イベントの単調時刻をナノ秒で保持する。合成入力では省略でき
    // る。 キューから取り出す速さをイベントの時間として使わない。
    std::optional<std::uint64_t> timestamp_ns;
    std::optional<bool> physical_control;
    PointerCancelReason cancel_reason{PointerCancelReason::Explicit};
    // 既存の位置指定の初期化を崩さないよう、元の
    // 項目の末尾へ置く。 falseはバックエン
    // ドから画面座標が渡されていないことを示す。
    bool screen_position_available{};
    // このDownでは共通一覧のクリック選択を省略するが、未処理のまま
    // にする。 所有部品が範囲選択・キャプチャ状態を保てるようにする。
    bool suppress_default_item_view_selection{};
    // 2回目のクリックが認識されたら、共通マウス経路は別のDoubleClick
    // を消費せず、 一覧側の解放時の後処理を完了させる。後の所
    // 有者通知でhandledを設定すれば解放を消費できる。
    bool allow_deferred_double_click{};
};

struct WheelEvent {
    Point position{};
    float delta_x{};
    float delta_y{};
    Modifiers modifiers{Modifiers::None};
    bool handled{};
};

using Action = std::function<void()>;
using CommandGestureAction = std::function<void(CommandGesture)>;
using KeyHandler = std::function<void(KeyEvent&)>;
using TextInputHandler = std::function<void(TextInputEvent&)>;
using PointerHandler = std::function<void(PointerEvent&)>;
using WheelHandler = std::function<void(WheelEvent&)>;

}  // namespace cwxeditor::lib
