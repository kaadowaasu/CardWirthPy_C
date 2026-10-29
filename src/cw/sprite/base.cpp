#include "cw/sprite/base.hpp"
#include "cw/lib/python_float.hpp"
#include "cw/cw.hpp"
#include "cw/thread.hpp"
#include "cw/setting.hpp"
#include "cw/battle.hpp"
#include "cw/frame.hpp"
#include "cw/eventrelay.hpp"
#include "cw/debug/logging.hpp"
#include "cw/sprite/card.hpp"
#include "cw/sprite/background.hpp"
#include "cw/sprite/message.hpp"
#include "cw/sprite/statusbar.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <map>
#include <mutex>
#include <stdexcept>

namespace {

double system_ticks()
{
    using namespace std::chrono;
    return static_cast<double>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

double current_ticks(const cw::sprite::base::SpriteRuntimeContext* context)
{
    return context && context->ticks ? context->ticks() : system_ticks();
}

bool stop_the_world_enabled(const cw::sprite::base::SpriteRuntimeContext* context)
{
    return context && context->stop_the_world_enabled && context->stop_the_world_enabled();
}

} // namespace

namespace cw::sprite::base {
namespace {
SpriteRuntimeContext* g_runtime{};
SpriteRuntimeContext g_default_runtime{};
struct LayeredSpriteProxy final : ::cw::lib::Sprite {
    CWPySprite* owner{};
    std::tuple<::cw::lib::Integer,::cw::lib::Integer,::cw::lib::Integer,::cw::lib::Integer> layer{};
    // ステータスバーのグループは、背景・カードのパック済みタプルではなく、
    // Python と同じ -1～7 の符号付きスカラーレイヤーを使う。
    // 登録形式を維持し、描画とグループ更新順が同じ値を読むようにする。
    bool scalar_layer{};
};
struct ProxyEntry { std::weak_ptr<::cw::lib::SpriteGroup> group{}; std::shared_ptr<LayeredSpriteProxy> proxy{}; };
auto& sprite_proxies()
{
    // EventHandlerRuntimeContext はスプ
    // ライトを全体の終了まで保持する。 この翻訳単位の静的オブジェ
    // クトの通常の破棄時期を過ぎても基底デストラクターから登録解除
    // できるよう、登録表はプロセスの生存期間中保持する。 グルー
    // プは弱参照にし、描画オブジェクト自体は通常どおり解放する。
    static auto* proxies = new std::map<CWPySprite*, std::vector<ProxyEntry>>();
    return *proxies;
}
auto& g_proxies = sprite_proxies();

std::optional<::cw::lib::Integer> value_integer(const ::cw::lib::Value& value){if(const auto*p=std::get_if<::cw::lib::Integer>(&value.storage()))return*p;if(const auto*p=std::get_if<::cw::lib::Real>(&value.storage()))return static_cast<::cw::lib::Integer>(*p);return std::nullopt;}
// フリック時間の不具合を修正する。Python の process_time()
// は CPU 時間なので、操作中の待機で制限時間が進まない。 入力ハンドラー
// が保存する開始時刻と同じ steady_clock の経過秒を使い、Engine
// の待機中も flick_time_msec が経過するようにする。
double process_seconds()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
std::shared_ptr<SelectableSprite> alias_selectable(SelectableSprite* sprite)
{
    if (!sprite) return {};
    return std::shared_ptr<SelectableSprite>(sprite, [](SelectableSprite*) {});
}

std::shared_ptr<SelectableSprite> find_owned_selectable(::cw::thread::CWPy* py,
                                                        const SelectableSprite* raw)
{
    if (!py || !raw) return {};
    for (const auto& item : py->m_list)
        if (item && item.get() == raw) return item;
    // メッセージはキーボード一覧と独立して選択肢を所有する。
    // 表示中にパーティー・カード更新で m_list が再構
    // 築されても、 ホバーの遅延選択操作は実際の共有所有者で
    // 保持し、非所有の別名や二つ目の入力経路で代替しない。
    if (const auto message = py->get_messagewindow(); message && *message)
        for (const auto& bar : (*message)->m_selections)
            if (bar && bar.get() == raw) return bar;
    // m_list はキーボード移動用の一覧に限る。 マウス選択で
    // は Python の pygame SpriteGroup
    // と同様、現在のスプライト集合に保持されたカードも対象にする。
    for (const auto& value : py->m_mcards) {
        const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
        if (!object || !*object) continue;
        const auto item = std::dynamic_pointer_cast<SelectableSprite>(*object);
        if (item && item.get() == raw) return item;
    }
    for (const auto& value : py->m_pcards) {
        const auto item = std::static_pointer_cast<SelectableSprite>(value);
        if (item && item.get() == raw) return item;
    }
    for (const auto& item : py->m_curtains)
        if (item && item.get() == raw) return item;
    const auto find_clickable = [raw](const auto& items) {
        for (const auto& item : items) {
            const auto selectable = std::static_pointer_cast<SelectableSprite>(item);
            if (selectable && selectable.get() == raw) return selectable;
        }
        return std::shared_ptr<SelectableSprite>{};
    };
    if (auto item = find_clickable(py->m_poschangearrows)) return item;
    if (auto item = find_clickable(py->m_numbercardbuttons)) return item;
    if (auto item = find_clickable(py->m_personalpocketbuttons)) return item;
    if (py->m_selection && *py->m_selection && (*py->m_selection).get() == raw)
        return *py->m_selection;
    if (py->m_lazy_selection && *py->m_lazy_selection && (*py->m_lazy_selection).get() == raw)
        return *py->m_lazy_selection;
    if (py->m_statusbar)
        if (auto item = ::cw::sprite::statusbar::find_retained_selectable(*py->m_statusbar, raw))
            return item;
    if (auto item = ::cw::sprite::message::find_owned_selectable(raw)) return item;
    return {};
}
void sort_layered_group(const std::shared_ptr<::cw::lib::SpriteGroup>& group)
{
    sort_sprite_group_by_layer(group);
}
void sync_proxy(CWPySprite& sprite)
{
    auto it = g_proxies.find(&sprite);
    if (it == g_proxies.end()) return;
    for (auto& entry : it->second) {
        if (!entry.proxy) continue;
        const auto old_layer = entry.proxy->layer;
        // pygame の DirtySprite と同様、visible は dirty と独立して保持する。
        sync_sprite_proxy(*entry.proxy, sprite.m_image, sprite.m_rect,
                          sprite.m_status != "hidden");
        entry.proxy->layer = sprite.m_tlayer;
        if (old_layer != sprite.m_tlayer)
            if (auto group = entry.group.lock())
                reposition_sprite_group_by_layer(group, entry.proxy);
    }
}

class CommonSpriteUiBackend final : public SpriteUiBackend {
public:
 void register_sprite(CWPySprite& sprite,const ::cw::lib::Arguments& groups) override {
  auto& entries=g_proxies[&sprite];
  entries.erase(std::remove_if(entries.begin(),entries.end(),[](const ProxyEntry& entry){return entry.group.expired();}),entries.end());
  for(const auto& value:groups){
   const auto*p=std::get_if<::cw::lib::ObjectPtr>(&value.storage());if(!p||!*p)continue;
   auto group=std::dynamic_pointer_cast<::cw::lib::SpriteGroup>(*p);if(!group)continue;
   const bool registered=std::any_of(entries.begin(),entries.end(),[&group](const ProxyEntry& entry){return entry.group.lock()==group;});
   if(registered)continue;
    auto proxy=std::make_shared<LayeredSpriteProxy>();proxy->owner=&sprite;proxy->image=sprite.m_image;proxy->rect=sprite.m_rect;proxy->visible=sprite.m_status!="hidden";proxy->layer=sprite.m_tlayer;
    const auto* status_group = ::cw::g_cwpy
        ? std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_sbargrp.storage()) : nullptr;
    proxy->scalar_layer = status_group && status_group->get() == group.get();
    insert_sprite_group_by_layer(group, proxy);entries.push_back({group,proxy});
  }
  if(entries.empty())g_proxies.erase(&sprite);else sync_proxy(sprite);
 }
 void unregister_sprite(CWPySprite& sprite) override {auto it=g_proxies.find(&sprite);if(it==g_proxies.end())return;for(auto& entry:it->second)if(auto group=entry.group.lock())remove_sprite_from_group(group,entry.proxy);g_proxies.erase(it);}
 void update_scale(CWPySprite& sprite) override {if(auto*c=runtime_context();c&&c->scale_sprite)c->scale_sprite(&sprite);sprite.m_dirty=2;sync_proxy(sprite);invalidate(sprite,sprite.m_rect);}
 void update_hover(MouseHandlerSprite& sprite,bool handling) override {if(auto*c=runtime_context();c&&c->hover_changed)c->hover_changed(&sprite,handling);if(handling&&sprite.m_handling_image)sprite.m_image=sprite.m_handling_image;else if(!handling&&sprite.m_normal_image)sprite.m_image=sprite.m_normal_image;sprite.m_dirty=1;sync_proxy(sprite);invalidate(sprite,sprite.m_rect);}
 void input(SelectableSprite& sprite,SpriteInputEvent event) override {if(auto*c=runtime_context();c&&c->input_event)c->input_event(&sprite,event);}
 void invalidate(CWPySprite& sprite,const ::cw::lib::Rect& rect) override {sprite.m_dirty=std::max<::cw::lib::Integer>(1,sprite.m_dirty);sync_proxy(sprite);if(auto*c=runtime_context();c&&c->invalidate)c->invalidate(&sprite,rect);else if(::cw::g_cwpy)::cw::g_cwpy->add_lazydraw_rect(rect);}
};
SpriteRuntimeContext* effective_runtime()
{
    if (g_runtime) return g_runtime;
    if (!::cw::g_cwpy) return nullptr;
    auto* py = ::cw::g_cwpy.get();
    g_default_runtime.dealing = py->is_dealing();
    g_default_runtime.running_event = py->is_runningevent();
    g_default_runtime.battle_exists = py->m_battle && *py->m_battle;
    g_default_runtime.battle_ready = !g_default_runtime.battle_exists || (*py->m_battle)->is_ready();
    g_default_runtime.mouse_motion = py->m_mousemotion;
    g_default_runtime.pointed_tile = py->m_pointed_tile && *py->m_pointed_tile;
    g_default_runtime.mouse_pos = {
        value_integer(std::get<0>(py->m_mousepos)).value_or(-1),
        value_integer(std::get<1>(py->m_mousepos)).value_or(-1)};
    g_default_runtime.index = py->m_index;
    // Py と同様、同じ更新中に先行スプライトが行った一覧・位置の変更を参照する。
    // null 項目も含む実体のビューを使い、O(n) のコピーを繰り返さない。
    g_default_runtime.live_selectable_list = &py->m_list;
    g_default_runtime.selection = py->m_selection && *py->m_selection ? py->m_selection->get() : nullptr;
    g_default_runtime.lazy_selection = py->m_lazy_selection && *py->m_lazy_selection
        ? py->m_lazy_selection->get() : nullptr;

    g_default_runtime.fps = [py]() { return py->m_setting ? py->m_setting->m_fps : 60; };
    g_default_runtime.stop_the_world_enabled = [py]() {
        return py->m_setting && py->m_setting->m_stop_the_world_with_iconized;
    };
    g_default_runtime.iconized = [py]() {
        if (const auto* p = std::get_if<::cw::lib::ObjectPtr>(&py->m_frame.storage()); p && *p)
            if (auto f = std::dynamic_pointer_cast<::cw::frame::Frame>(*p)) return f->m_is_iconized.load();
        return false;
    };
    g_default_runtime.lock_menu_cards = [py](const SelectableSprite* sprite) {
        if (const auto owner = find_owned_selectable(py, sprite))
            return py->is_lockmenucards(owner);
        // Python と同様、一覧への所属を条件にせず is_lockmenucards(self) を呼ぶ。
        // sprite は update()/update_selection() の実体の this を指す。
        return py->is_lockmenucards(alias_selectable(const_cast<SelectableSprite*>(sprite)));
    };
    g_default_runtime.flick_active = [py]() {
        if (!py->m_keyevent) return false;
        const auto status = value_integer(py->m_keyevent->m_flick_status).value_or(0);
        if (status != ::cw::frame::g_FLICK_START) return false;
        const auto limit = py->m_setting
            ? static_cast<double>(py->m_setting->m_flick_time_msec) / 1000.0
            : 0.3;
        if (process_seconds() - py->m_keyevent->m_flick_start_time > limit) return false;
        return true;
    };
    g_default_runtime.flick_selects = [py](const SelectableSprite* sprite) {
        const auto& flick = py->m_keyevent->m_flick_sprite;
        return flick && *flick && (*flick).get() == sprite;
    };
    g_default_runtime.invalidate = [py](CWPySprite*, const ::cw::lib::Rect& rect) {
        py->add_lazydraw_rect(rect);
    };
    g_default_runtime.selection_changed = [py](SelectableSprite*, SelectableSprite* next) {
        if (!next) {
            py->clear_selection();
            return;
        }
        if (const auto owner = find_owned_selectable(py, next)) {
            py->m_lazy_selection = owner;
            return;
        }
        // 非所有の別名を保存しない。 管理されていないスプライトは、次の更新で CWPy
        // が lazy_selection を使う前に消える可能性がある。
        py->m_lazy_selection.reset();
    };
    return &g_default_runtime;
}

CWPySprite* owner_of_layered_proxy(const std::shared_ptr<::cw::lib::Sprite>& sprite)
{
    if (const auto proxy = std::dynamic_pointer_cast<LayeredSpriteProxy>(sprite)) {
        if (!proxy->owner) return nullptr;
        const auto it = g_proxies.find(proxy->owner);
        if (it == g_proxies.end()) return nullptr;
        const bool registered = std::any_of(
            it->second.begin(), it->second.end(),
            [&](const ProxyEntry& entry) { return entry.proxy == proxy; });
        if (registered) return proxy->owner;
    }
    return nullptr;
}
}
void set_runtime_context(SpriteRuntimeContext* context){g_runtime=context;}
SpriteRuntimeContext* runtime_context(){return effective_runtime();}
SpriteRuntimeContext* explicit_runtime_context(){return g_runtime;}
std::shared_ptr<SpriteUiBackend> make_default_sprite_ui_backend(){return std::make_shared<CommonSpriteUiBackend>();}
std::shared_ptr<SpriteUiBackend> sprite_ui_backend(){static const auto backend=make_default_sprite_ui_backend();return backend;}
CWPySprite::~CWPySprite(){
    ::cw::sprite::background::remove_background_proxies_for_owner(this);
    if(m_ui_backend)m_ui_backend->unregister_sprite(*this);
}
void CWPySprite::initialize(const ::cw::lib::Arguments& arg_groups)
{
    // Python の DirtySprite と同様、基底初期化より前に派生クラスが
    // 設定した画像・矩形・レイヤーを保持する。 新しいネイティブオブジェクトには既定
    // メンバーがあり、初期化で戻すのはアニメーション状態だけとし、元の形は変えない。
    m_ui_backend=sprite_ui_backend();
    m_dirty = 2;
    m_status.clear();
    m_old_status.clear();
    m_anitype.clear();
    m_start_animation = 0.0;
    m_skipped = false;
    m_frame = 0;
    if(auto* context=runtime_context();context&&context->register_sprite)context->register_sprite(this,arg_groups);
    if(m_ui_backend)m_ui_backend->register_sprite(*this,arg_groups);
}

bool CWPySprite::is_initialized()
{
    return true;
}

void CWPySprite::update_scale()
{
    // Python の CWPySprite.update_scale
    // と同様、何もしない。 派生クラスが自身の描画面を再構築する。
}

::cw::lib::Integer CWPySprite::get_frame()
{
    const auto frame = get_frame_integer();
    // 旧ネイティブ API は Integer を返すため、
    // 戻り値の境界でだけ狭める。 大きいフレーム値でも終了・
    // リセットできる効果は、中間値を狭めず同じ計算を使う。
    if (frame < std::numeric_limits<::cw::lib::Integer>::min() ||
        frame > std::numeric_limits<::cw::lib::Integer>::max())
        throw std::overflow_error("sprite frame exceeds native Integer range");
    return frame.to_integer();
}

::cw::lib::PythonInteger CWPySprite::get_frame_integer()
{
    const auto* context = runtime_context();
    const auto tick = current_ticks(context);
    const auto fps = context && context->fps ? context->fps() : ::cw::lib::Integer{60};
    if (fps <= 0) throw std::domain_error("fps must be positive");

    // Python と同様、1000//fps の整数切り下げを維
    // 持する。 通常より高い設定フレームレートでの元の動作も保つ。
    if (m_start_animation == 0.0)
        m_start_animation = tick - static_cast<double>(1000 / fps);
    const auto next_frame = ::cw::lib::PythonInteger(m_frame) + 1;
    if (tick < m_start_animation) return next_frame;

    const auto elapsed_frame = ::cw::lib::python_float_floor_divide(
        (tick - m_start_animation) * static_cast<double>(fps), 1000.0);
    const auto calculated = ::cw::lib::PythonInteger::from_truncated_real(elapsed_frame);
    return std::max(next_frame, calculated);
}

::cw::lib::Real animation_ticks() { return current_ticks(runtime_context()); }

void StopTheWorld::initialize(::cw::lib::Real arg_start_ticks, ::cw::lib::Integer arg_waittime)
{
    m_start_ticks=arg_start_ticks;m_waittime=arg_waittime;m__stop_tick.reset();
}

bool StopTheWorld::is_waiting()
{
    return !is_done();
}

bool StopTheWorld::is_done()
{
    const auto* context = runtime_context();
    const bool enabled = stop_the_world_enabled(context);
    if (enabled && context->iconized && context->iconized()) {
        _stop();
        return false;
    }
    if (enabled && m__stop_tick) _resume();
    // Python と同様、_resume() の可能性がある処理の後にタ
    // イマーを読む。 調整前に確認した値を再利用せず、最新値を取得する。
    return m_start_ticks + static_cast<double>(m_waittime) <= current_ticks(context);
}

void StopTheWorld::_stop()
{
    if (!m__stop_tick)
        m__stop_tick = static_cast<::cw::lib::Integer>(current_ticks(runtime_context()));
}

void StopTheWorld::_resume()
{
    if (!m__stop_tick) return;
    if (stop_the_world_enabled(runtime_context())) {
        m_start_ticks += current_ticks(runtime_context()) - static_cast<double>(*m__stop_tick);
    }
    m__stop_tick.reset();
}

void MouseHandlerSprite::initialize(const ::cw::lib::Arguments& arg_groups)
{
    CWPySprite::initialize(arg_groups);m_handling_rect.reset();m_handling=false;
}

void MouseHandlerSprite::update(const ::cw::lib::Arguments& arg_args, const ::cw::lib::KeywordArguments& arg_kwargs)
{
    (void)arg_args;(void)arg_kwargs;update_selection();
}

void MouseHandlerSprite::update_selection()
{
    const auto* context = runtime_context();
    if (!context || !m_handling_rect || !context->mouse_motion) return;

    const auto& p = context->mouse_pos;
    const auto& r = *m_handling_rect;
    bool handling = false;
    if (r.width > 0 && r.height > 0 && p.x >= 0 && p.y >= 0) {
        handling = m_rect.x + r.x <= p.x && p.x < m_rect.x + r.x + r.width &&
            m_rect.y + r.y <= p.y && p.y < m_rect.y + r.y + r.height;
    }
    if (handling != m_handling) {
        m_handling = handling;
        update_image();
    }
}

void MouseHandlerSprite::update_image()
{
    // Python の MouseHandlerSprite.update_image
    // と同様、何もしない。 派生クラスが自身の上書き処理で画像を差し替える。
}

void SelectableSprite::initialize(const ::cw::lib::Arguments& arg_groups)
{
    m_selectable_on_event=false;m_is_statusctrl=false;CWPySprite::initialize(arg_groups);m_is_pointed=false;
}

void SelectableSprite::lclick_event()
{
    if(!m_ui_backend)m_ui_backend=sprite_ui_backend();if(m_ui_backend)m_ui_backend->input(*this,SpriteInputEvent::left_click);
}

void SelectableSprite::rclick_event()
{
    if(!m_ui_backend)m_ui_backend=sprite_ui_backend();if(m_ui_backend)m_ui_backend->input(*this,SpriteInputEvent::right_click);
}

void SelectableSprite::ldown_event()
{
    if(!m_ui_backend)m_ui_backend=sprite_ui_backend();if(m_ui_backend)m_ui_backend->input(*this,SpriteInputEvent::left_down);
}

std::shared_ptr<::cw::lib::Surface> SelectableSprite::get_selectedimage()
{
    return m_image;
}

std::shared_ptr<::cw::lib::Surface> SelectableSprite::get_unselectedimage()
{
    return m_image;
}

void SelectableSprite::update(const ::cw::lib::Arguments& arg_args, const ::cw::lib::KeywordArguments& arg_kwargs)
{
    (void)arg_args;(void)arg_kwargs;auto* context=runtime_context();if(!context||!context->lock_menu_cards||!context->lock_menu_cards(this))update_selection();
}

void SelectableSprite::update_selection()
{
    auto* context = runtime_context();
    if (!context) return;
    if (context->lock_menu_cards && context->lock_menu_cards(this)) return;
    if (context->pointed_tile) return;
    // 意図的な変更(Py版と異なる): Py版は対象外のカードを覆うCurtainが
    // 手前に重なることだけで選択・クリックを防ぐ。C++版では重なり順が
    // ずれる場面(戦闘外でのカード使用時など)で、敵対象カードを味方へ、
    // 味方対象カードを敵へ使えてしまったため、カーテン対象は明示的に
    // 選択不可にする(Py版でCurtainをクリックしても何も起きないのと同じ結果)。
    const bool curtain_blocked = ::cw::g_cwpy && ::cw::g_cwpy->is_curtain_blocked(this);
    if (!curtain_blocked && is_selection()) {
        if (!m_is_pointed) {
            context->lazy_selection = this;
            // Python と同様、実体の Engine がこのスプライトを所有する場合は cw.cwpy.lazy_selection
            // 相当へ直接書く。 EventHandler のコピーした実行コ
            // ンテキストは入力アダプターなので、選択の公開に必須とはしない。
            if (::cw::g_cwpy) {
                if (const auto owner = find_owned_selectable(::cw::g_cwpy.get(), this)) {
                    ::cw::g_cwpy->m_lazy_selection = owner;
#if defined(CARDWIRTH_LOG_ENABLED)
                    if (::cw::g_cwpy->m_areaid == -4)
                        ::cardwirth::debug::log("Camp direct lazy selection: accepted owner");
#endif
                } else {
                    ::cw::g_cwpy->m_lazy_selection.reset();
#if defined(CARDWIRTH_LOG_ENABLED)
                    if (::cw::g_cwpy->m_areaid == -4)
                        ::cardwirth::debug::log("Camp direct lazy selection: owner not found");
#endif
                }
            } else if (context->selection_changed)
                context->selection_changed(this, this);
        }
    } else if (context->selection == this) {
        context->selection = nullptr;
        context->lazy_selection = nullptr;
        if (::cw::g_cwpy)
            ::cw::g_cwpy->clear_selection();
        else if (context->selection_changed)
            context->selection_changed(this, nullptr);
    }
}

bool SelectableSprite::is_selection()
{
    auto* context=runtime_context();if(!context)return false;
    if(context->dealing&&!m_selectable_on_event)return false;
    if(!context->running_event&&context->battle_exists&&!context->battle_ready&&!m_selectable_on_event)return false;
    if(context->running_event&&!m_selectable_on_event)return false;
    if(!m_is_pointed&&context->flick_active&&context->flick_active())
        return context->flick_selects && context->flick_selects(this);
    if (!context->mouse_motion && context->index >= 0) {
        const auto* live = context->live_selectable_list;
        const auto count = live ? live->size() : context->selectable_list.size();
        if (context->index < static_cast<::cw::lib::Integer>(count)) {
            const auto index = static_cast<std::size_t>(context->index);
            return (live ? (*live)[index].get() : context->selectable_list[index]) == this;
        }
    }
    const auto& p=context->mouse_pos;
    if(p.x>=0&&p.y>=0&&m_rect.x<=p.x&&p.x<m_rect.x+m_rect.width&&m_rect.y<=p.y&&p.y<m_rect.y+m_rect.height){
        if(context->mouse_motion){
            context->index=-1;
            if(::cw::g_cwpy)::cw::g_cwpy->m_index=-1;
        }
        return true;
    }
    return false;
}

CWPySprite* layered_group_sprite_owner(const std::shared_ptr<::cw::lib::Sprite>& arg_sprite)
{
    return owner_of_layered_proxy(arg_sprite);
}

std::shared_ptr<SelectableSprite> find_owned_selectable(const SelectableSprite* arg_sprite)
{
    return find_owned_selectable(::cw::g_cwpy.get(), arg_sprite);
}

std::optional<::cw::lib::Integer> layered_group_sprite_layer(
    const std::shared_ptr<::cw::lib::Sprite>& arg_sprite)
{
    return layered_group_sprite_layer(arg_sprite, false);
}

std::optional<::cw::lib::Integer> layered_group_sprite_layer(
    const std::shared_ptr<::cw::lib::Sprite>& arg_sprite, bool arg_scalar_layer)
{
    const auto proxy = std::dynamic_pointer_cast<LayeredSpriteProxy>(arg_sprite);
    if (!proxy) return std::nullopt;
    if (arg_scalar_layer || proxy->scalar_layer) return std::get<0>(proxy->layer);
    try { return ::cw::layer_val(proxy->layer); }
    catch (const std::exception&) { return std::nullopt; }
}

void sort_sprite_group_by_layer(const std::shared_ptr<::cw::lib::SpriteGroup>& arg_group)
{
    if (!arg_group) return;
    const auto layer_of = [](const auto& sprite) -> std::optional<::cw::lib::Integer> {
        if (const auto layer = layered_group_sprite_layer(sprite)) return layer;
        if (const auto layer = ::cw::sprite::background::background_layer_sprite_layer(sprite)) return layer;
        return ::cw::sprite::card::card_layer_sprite_layer(sprite);
    };
    std::stable_sort(arg_group->sprites.begin(), arg_group->sprites.end(), [&](const auto& lhs, const auto& rhs) {
        const auto left = layer_of(lhs);
        const auto right = layer_of(rhs);
        if (left && right) return *left < *right;
        if (left) return true;
        if (right) return false;
        return false;
    });
}

namespace {
std::optional<::cw::lib::Integer> sprite_group_layer(
    const std::shared_ptr<::cw::lib::Sprite>& sprite)
{
    if (const auto layer = layered_group_sprite_layer(sprite)) return layer;
    if (const auto layer = ::cw::sprite::background::background_layer_sprite_layer(sprite)) return layer;
    return ::cw::sprite::card::card_layer_sprite_layer(sprite);
}
}

void insert_sprite_group_by_layer(
    const std::shared_ptr<::cw::lib::SpriteGroup>& arg_group,
    const std::shared_ptr<::cw::lib::Sprite>& arg_sprite)
{
    if (!arg_group || !arg_sprite) return;
    if (std::find(arg_group->sprites.begin(), arg_group->sprites.end(), arg_sprite) !=
        arg_group->sprites.end()) return;

    const auto layer_of = [&arg_group](const auto& sprite) {
        if (const auto layer = sprite_group_layer(sprite)) return layer;
        // 生の互換 Sprite は実際のグループに情報を保持し、
        // モデルのプロキシーは登録表から同じレイヤーを公開する。
        const auto it = arg_group->layers.find(sprite.get());
        return it == arg_group->layers.end() ? std::optional<::cw::lib::Integer>{}
                                             : std::optional<::cw::lib::Integer>{it->second};
    };
    const auto layer = layer_of(arg_sprite);
    if (!layer) {
        arg_group->sprites.push_back(arg_sprite);
        if (arg_sprite->visible) invalidate_sprite_rect(arg_sprite->rect);
        return;
    }
    auto position = arg_group->sprites.end();
    for (auto it = arg_group->sprites.begin(); it != arg_group->sprites.end(); ++it) {
        const auto existing_layer = layer_of(*it);
        // レイヤー付きの項目を、不明・レイヤーなしの互換スプライトよ
        // り前に置く。 同じレイヤーは通過し、挿入順を安定させる。
        if (!existing_layer || *layer < *existing_layer) {
            position = it;
            break;
        }
    }
    arg_group->sprites.insert(position, arg_sprite);
    if (arg_sprite->visible) invalidate_sprite_rect(arg_sprite->rect);
}

void reposition_sprite_group_by_layer(
    const std::shared_ptr<::cw::lib::SpriteGroup>& arg_group,
    const std::shared_ptr<::cw::lib::Sprite>& arg_sprite)
{
    if (!arg_group || !arg_sprite) return;
    const auto found = std::find(arg_group->sprites.begin(), arg_group->sprites.end(), arg_sprite);
    if (found == arg_group->sprites.end()) {
        insert_sprite_group_by_layer(arg_group, arg_sprite);
        return;
    }
    // 参照がグループの vector 要素自身を指す場合があるため、消去前に保持する。
    const auto moved = arg_sprite;
    arg_group->sprites.erase(found);
    // pygame の change_layer と同様、新しいレイヤーの同順位項
    // 目すべての後ろへ置く。 同じレイヤー値を明示的に指定した場合も同様とする。
    insert_sprite_group_by_layer(arg_group, moved);
}

void invalidate_sprite_rect(const ::cw::lib::Rect& rect) noexcept
{
    // デストラクターからの
    // 削除でも確保を行わず
    // 、正の幅・高さを持つ
    // 更新矩形だけを渡す。
    if (::cw::g_cwpy && ::cw::g_cwpy.use_count() > 0 && rect.width > 0 && rect.height > 0)
        ::cw::g_cwpy->add_lazydraw_rect(rect);
}

void sync_sprite_proxy(::cw::lib::Sprite& proxy,
                       const std::shared_ptr<::cw::lib::Surface>& image,
                       const ::cw::lib::Rect& rect, bool visible, bool compare_image)
{
    const bool changed = (compare_image && proxy.image != image) ||
        proxy.visible != visible || proxy.rect.x != rect.x || proxy.rect.y != rect.y ||
        proxy.rect.width != rect.width || proxy.rect.height != rect.height;
    if (changed && proxy.visible) invalidate_sprite_rect(proxy.rect);
    proxy.image = image;
    proxy.rect = rect;
    proxy.visible = visible;
    if (changed && visible) invalidate_sprite_rect(rect);
}

void remove_sprite_from_group(const std::shared_ptr<::cw::lib::SpriteGroup>& group,
                              const std::shared_ptr<::cw::lib::Sprite>& sprite) noexcept
{
    if (!group || !sprite) return;
    // 引数は、erase() で無効になる vector 要素自身を指す場合がある。
    const auto removed = sprite;
    const auto found = std::find(group->sprites.begin(), group->sprites.end(), removed);
    if (found == group->sprites.end()) return;
    if (removed->visible) invalidate_sprite_rect(removed->rect);
    group->sprites.erase(std::remove(found, group->sprites.end(), removed), group->sprites.end());
    group->layers.erase(removed.get());
}

void main()
{
    // Pythonのmainはエントリポイントを持つだけで、ゲーム起動は呼出側が担当する。
}

}
