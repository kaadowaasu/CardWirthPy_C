#pragma once

// cw/sprite/base.py の移植。SpriteRuntimeContext でゲー
// ムスレッドの状態を注入可能にし、この低レベルモジュールを OS の UI から独立させる。
#include "cw/lib/compat.hpp"
#include "cw/lib/python_integer_value.hpp"
#include "cw/fwd.hpp"

namespace cw::sprite::base {
class SelectableSprite;
class CWPySprite;
class MouseHandlerSprite;
enum class SpriteInputEvent { left_click, right_click, left_down };
struct SpriteRuntimeContext {
    std::function<::cw::lib::Real()> ticks{};
    std::function<::cw::lib::Integer()> fps{};
    std::function<bool()> iconized{};
    std::function<bool()> stop_the_world_enabled{};
    std::function<bool(const SelectableSprite*)> lock_menu_cards{};
    std::function<bool(const SelectableSprite*)> flick_selects{};
    std::function<bool()> flick_active{};
    bool dealing{}, running_event{}, battle_exists{}, battle_ready{true};
    bool mouse_motion{}, pointed_tile{};
    ::cw::lib::Point mouse_pos{-1,-1};
    ::cw::lib::Integer index{-1};
    std::vector<SelectableSprite*> selectable_list{};
    // Py は現在の一覧を参照で読む。Engine のアダプターも選択問い合わせ
    // ごとに m_list を複製・平坦化せず、このビューを使う。注入した文脈に
    // 現在のビューがなければ、従来の selectable_list を使える。
    const std::vector<std::shared_ptr<SelectableSprite>>* live_selectable_list{};
    SelectableSprite* selection{};
    SelectableSprite* lazy_selection{};
    std::function<void(CWPySprite*, const ::cw::lib::Arguments&)> register_sprite{};
    std::function<void(CWPySprite*)> scale_sprite{};
    std::function<void(MouseHandlerSprite*, bool)> hover_changed{};
    std::function<void(SelectableSprite*, SpriteInputEvent)> input_event{};
    std::function<void(CWPySprite*, const ::cw::lib::Rect&)> invalidate{};
    std::function<void(SelectableSprite*, SelectableSprite*)> selection_changed{};
};
void set_runtime_context(SpriteRuntimeContext* context);
SpriteRuntimeContext* runtime_context();
// 既定の CWPy アダプターを作り直さず、明示的に設定された文脈だ
// けを返す。呼び出し側が注入した上書きを後で復元できるようにする。
SpriteRuntimeContext* explicit_runtime_context();
// 独自の基準時点も含め、アニメーション開始と StopTheWorld に同じ時計を使う。
::cw::lib::Real animation_ticks();

class SpriteUiBackend : public ::cw::lib::Object {
public:
    virtual ~SpriteUiBackend() = default;
    virtual void register_sprite(CWPySprite& arg_sprite, const ::cw::lib::Arguments& arg_groups) = 0;
    virtual void unregister_sprite(CWPySprite& arg_sprite) = 0;
    virtual void update_scale(CWPySprite& arg_sprite) = 0;
    virtual void update_hover(MouseHandlerSprite& arg_sprite, bool arg_handling) = 0;
    virtual void input(SelectableSprite& arg_sprite, SpriteInputEvent arg_event) = 0;
    virtual void invalidate(CWPySprite& arg_sprite, const ::cw::lib::Rect& arg_rect) = 0;
};
std::shared_ptr<SpriteUiBackend> make_default_sprite_ui_backend();
std::shared_ptr<SpriteUiBackend> sprite_ui_backend();

class CWPySprite : public ::cw::lib::Object {
public:
    CWPySprite() = default;
    virtual ~CWPySprite();
    std::shared_ptr<::cw::lib::Surface> m_image{};
    ::cw::lib::Rect m_rect{};
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> m_tlayer{};
    ::cw::lib::String m_anitype{};
    ::cw::lib::Integer m_dirty{};
    ::cw::lib::Integer m_frame{};
    ::cw::lib::String m_old_status{};
    bool m_skipped{};
    ::cw::lib::Real m_start_animation{};
    ::cw::lib::String m_status{};
    std::shared_ptr<SpriteUiBackend> m_ui_backend{};
    void initialize(const ::cw::lib::Arguments& arg_groups);
    virtual bool is_initialized();
    // SDL側の画像再スケールは描画層で行う。状態更新の入口だけ保持する。
    virtual void update_scale();
    virtual void update(const ::cw::lib::Arguments&, const ::cw::lib::KeywordArguments&) {}
    ::cw::lib::Integer get_frame();
    ::cw::lib::PythonInteger get_frame_integer();
};

// SpriteGroup が持つのは CWPySprite ではなく LayeredSpriteProxy
// 。所有者を解決し、group.update() がモデルのスプライトを一度だけ呼べるようにする。
CWPySprite* layered_group_sprite_owner(const std::shared_ptr<::cw::lib::Sprite>& arg_sprite);
// 代理から解決したモデルスプライトの、実際の Engine 側の所有者を保持する。
// 所有権のない別名参照を、グループの生の所有者ポインターより長く残してはならない。
std::shared_ptr<SelectableSprite> find_owned_selectable(const SelectableSprite* arg_sprite);
std::optional<::cw::lib::Integer> layered_group_sprite_layer(
    const std::shared_ptr<::cw::lib::Sprite>& arg_sprite);
// StatusBar の公開合成処理は、呼び出し側が指定する数値レイヤーのグル
// ープも受け入れる。既存のカード・背景の利用側には、一引数の API を保つ。
std::optional<::cw::lib::Integer> layered_group_sprite_layer(
    const std::shared_ptr<::cw::lib::Sprite>& arg_sprite, bool arg_scalar_layer);

// すべての描画用代理で一つの LayeredUpdates 互換の順序を共有する
// 。種類が混在する SpriteGroup を個別アダプター内でソートしない。
void sort_sprite_group_by_layer(const std::shared_ptr<::cw::lib::SpriteGroup>& arg_group);
// pygame.sprite.LayeredUpdates.add()
// は既存要素の順を保ち、新しい要素を同じレイヤーの既存要素の後に置く。
// 順序付きの挿入を使い、一つ追加するためにグループ全体をソートしない。
void insert_sprite_group_by_layer(
    const std::shared_ptr<::cw::lib::SpriteGroup>& arg_group,
    const std::shared_ptr<::cw::lib::Sprite>& arg_sprite);
// pygame の change_layer は要素を取り除き、移
// 動先レイヤーの全要素の後に挿入する。ほかの要素の相対順は保つ。
void reposition_sprite_group_by_layer(
    const std::shared_ptr<::cw::lib::SpriteGroup>& arg_group,
    const std::shared_ptr<::cw::lib::Sprite>& arg_sprite);

// pygame は現在のグループを描くが、ネイティブの不変フレームでは代理の所属・表
// 示状態の変更時に更新領域を明示する必要がある。空の領域を全面更新として扱わない。
void invalidate_sprite_rect(const ::cw::lib::Rect& rect) noexcept;
void sync_sprite_proxy(::cw::lib::Sprite& proxy,
                       const std::shared_ptr<::cw::lib::Surface>& image,
                       const ::cw::lib::Rect& rect, bool visible,
                       bool compare_image = true);
void remove_sprite_from_group(const std::shared_ptr<::cw::lib::SpriteGroup>& group,
                              const std::shared_ptr<::cw::lib::Sprite>& sprite) noexcept;

class StopTheWorld : public ::cw::lib::Object {
public:
    StopTheWorld() = default;
    virtual ~StopTheWorld() = default;
    std::optional<::cw::lib::Integer> m__stop_tick{};
    ::cw::lib::Real m_start_ticks{};
    ::cw::lib::Integer m_waittime{};
    void initialize(::cw::lib::Real arg_start_ticks, ::cw::lib::Integer arg_waittime);
    bool is_waiting();
    bool is_done();
    void _stop();
    void _resume();
};

class MouseHandlerSprite : public ::cw::sprite::base::CWPySprite {
public:
    MouseHandlerSprite() = default;
    virtual ~MouseHandlerSprite() = default;
    bool m_handling{};
    std::optional<::cw::lib::Rect> m_handling_rect{};
    std::shared_ptr<::cw::lib::Surface> m_normal_image{};
    std::shared_ptr<::cw::lib::Surface> m_handling_image{};
    void initialize(const ::cw::lib::Arguments& arg_groups);
    void update(const ::cw::lib::Arguments& arg_args, const ::cw::lib::KeywordArguments& arg_kwargs) override;
    virtual void update_selection();
    virtual void update_image();
};

class SelectableSprite : public ::cw::sprite::base::CWPySprite {
public:
    SelectableSprite() = default;
    virtual ~SelectableSprite() = default;
    bool m_is_pointed{};
    bool m_is_statusctrl{};
    bool m_selectable_on_event{};
    void initialize(const ::cw::lib::Arguments& arg_groups);
    virtual void lclick_event();
    virtual void rclick_event();
    virtual void ldown_event();
    virtual std::shared_ptr<::cw::lib::Surface> get_selectedimage();
    virtual std::shared_ptr<::cw::lib::Surface> get_unselectedimage();
    void update(const ::cw::lib::Arguments& arg_args, const ::cw::lib::KeywordArguments& arg_kwargs) override;
    virtual void update_selection();
    virtual bool is_selection();
};

void main();
}

