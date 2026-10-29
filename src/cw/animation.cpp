#include "cw/animation.hpp"

#include "cw/cw.hpp"
#include "cw/event.hpp"
#include "cw/eventhandler.hpp"
#include "cw/eventrelay.hpp"
#include "cw/frame.hpp"
#include "cw/setting.hpp"
#include "cw/thread.hpp"
#include "cw/sprite/background.hpp"
#include "cw/sprite/animationcell.hpp"
#include "cw/sprite/card.hpp"
#include "cw/sprite/message.hpp"
#include "cw/sprite/scrollbar.hpp"
#include "cw/sprite/statusbar.hpp"
#include "cw/sprite/touchbutton.hpp"
#include "cw/sprite/transition.hpp"

#include <algorithm>
#include <chrono>
#include <functional>
#include <exception>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <typeinfo>
#include <utility>

namespace {

using Integer = ::cw::lib::Integer;
using String = ::cw::lib::String;
using Sprite = ::cw::sprite::base::CWPySprite;

// 中断したアニメーションで、カードが途中の見た目のまま残らないようにする。
struct CardAnimationRollback {
    std::shared_ptr<::cw::sprite::card::CWPyCard> card;
    std::shared_ptr<::cw::thread::CWPy> runtime = ::cw::g_cwpy;
    Integer area = runtime ? runtime->m_areaid : 0;
    String status, old_status, anitype;
    std::shared_ptr<::cw::lib::Surface> image;
    ::cw::lib::Rect rect{};
    Integer frame{};
    bool battlespeed{}, skipped{};
    int exceptions = std::uncaught_exceptions();
    explicit CardAnimationRollback(const std::shared_ptr<Sprite>& sprite)
        : card(std::dynamic_pointer_cast<::cw::sprite::card::CWPyCard>(sprite)) {
        if (!card) return;
        status = card->m_status; old_status = card->m_old_status; anitype = card->m_anitype;
        image = card->m_image; rect = card->m_rect; frame = card->m_frame;
        battlespeed = card->m_battlespeed; skipped = card->m_skipped;
    }
    ~CardAnimationRollback() {
        if (!card || std::uncaught_exceptions() <= exceptions ||
            ::cw::g_cwpy != runtime || (runtime && runtime->m_areaid != area)) return;
        // ネイティブのスナップショットはpygameの変更フラグを消費しない。 例
        // 外時に戻す場合は、中断したフレームを消して復元後のフレームを反映する。
        ::cw::sprite::base::invalidate_sprite_rect(card->m_rect);
        card->m_status.swap(status);
        card->m_old_status.swap(old_status);
        card->m_anitype.swap(anitype);
        card->m_image = std::move(image); card->m_rect = rect; card->m_frame = frame;
        card->m_battlespeed = battlespeed; card->m_skipped = skipped;
        card->m_dirty = 1;
        ::cw::sprite::base::invalidate_sprite_rect(card->m_rect);
    }
};

struct HeadlessThreadBinding {
    std::weak_ptr<::cw::thread::CWPy> runtime{};
    std::thread::id owner{};
};

std::mutex& runtime_thread_mutex()
{
    static std::mutex mutex;
    return mutex;
}

HeadlessThreadBinding& headless_thread_binding()
{
    static HeadlessThreadBinding binding;
    return binding;
}

class ScopeExit final {
public:
    explicit ScopeExit(std::function<void()> action) : m_action(std::move(action)) {}
    ~ScopeExit() noexcept { if (m_action) try { m_action(); } catch (...) {} }
    ScopeExit(const ScopeExit&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;
    void release() noexcept { m_action = {}; }
private:
    std::function<void()> m_action;
};

::cw::lib::Real now_ticks()
{
    return ::cw::sprite::base::animation_ticks();
}

std::shared_ptr<::cw::lib::Object> object_value(const ::cw::lib::Value& value)
{
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage())) return *object;
    return {};
}

std::shared_ptr<::cw::RectValue> clip_value(const std::optional<std::shared_ptr<::cw::lib::Object>>& value)
{
    if (!value || !*value) return {};
    return std::dynamic_pointer_cast<::cw::RectValue>(*value);
}

std::shared_ptr<::cw::RectValue> make_clip(const ::cw::lib::Rect& rect)
{
    return std::make_shared<::cw::RectValue>(rect);
}

::cw::lib::Rect union_rect(::cw::lib::Rect left, const ::cw::lib::Rect& right)
{
    if (left.width <= 0 || left.height <= 0) return right;
    if (right.width <= 0 || right.height <= 0) return left;
    const auto x1 = std::min(left.x, right.x);
    const auto y1 = std::min(left.y, right.y);
    const auto x2 = std::max(left.x + left.width, right.x + right.width);
    const auto y2 = std::max(left.y + left.height, right.y + right.height);
    return {x1, y1, x2 - x1, y2 - y1};
}

String status_of(const std::shared_ptr<Sprite>& sprite)
{
    if (const auto clickable = std::dynamic_pointer_cast<::cw::sprite::background::ClickableSprite>(sprite)) return clickable->m_status;
    if (const auto button = std::dynamic_pointer_cast<::cw::sprite::statusbar::StatusBarButton>(sprite)) return button->m_status;
    if (const auto selection = std::dynamic_pointer_cast<::cw::sprite::message::SelectionBar>(sprite)) return selection->m_status;
    return sprite ? sprite->m_status : String{};
}

void set_status(const std::shared_ptr<Sprite>& sprite, String status)
{
    if (!sprite) return;
    // 派生側で同名の m_status を持つ場合も CWPySprite::m_status と同期し、 sprite.update()
    // と proc_animation が同じPython版のstatusを参照できるようにする。
    sprite->m_status = status;
    if (const auto clickable = std::dynamic_pointer_cast<::cw::sprite::background::ClickableSprite>(sprite))
        clickable->m_status = status;
    if (const auto button = std::dynamic_pointer_cast<::cw::sprite::statusbar::StatusBarButton>(sprite))
        button->m_status = status;
    if (const auto selection = std::dynamic_pointer_cast<::cw::sprite::message::SelectionBar>(sprite))
        selection->m_status = std::move(status);
}

void set_old_status(const std::shared_ptr<Sprite>& sprite, String status)
{
    if (!sprite) return;
    sprite->m_old_status = status;
    if (const auto clickable = std::dynamic_pointer_cast<::cw::sprite::background::ClickableSprite>(sprite))
        clickable->m_old_status = std::move(status);
}

void set_frame(const std::shared_ptr<Sprite>& sprite, Integer frame)
{
    if (!sprite) return;
    sprite->m_frame = frame;
    if (const auto clickable = std::dynamic_pointer_cast<::cw::sprite::background::ClickableSprite>(sprite))
        clickable->m_frame = frame;
    if (const auto button = std::dynamic_pointer_cast<::cw::sprite::statusbar::StatusBarButton>(sprite))
        button->m_frame = frame;
    if (const auto money = std::dynamic_pointer_cast<::cw::sprite::statusbar::YadoMoneyPanel>(sprite))
        money->m_frame = frame;
}

String sprite_name(const std::shared_ptr<Sprite>& sprite)
{
    if (!sprite) return "None";
    return typeid(*sprite).name();
}

void report_missing_animation(const String& anitype, const std::shared_ptr<Sprite>& sprite)
{
    std::cout << "Not found " << anitype << " animation.\n";
    std::cout << sprite_name(sprite) << '\n';
}

void report_missing_animations(
    const String& anitype,
    const std::vector<std::tuple<std::shared_ptr<Sprite>, String>>& sprandanimes)
{
    std::cout << "Not found " << anitype << " animation.\n";
    std::cout << '[';
    bool first = true;
    for (const auto& [sprite, type] : sprandanimes) {
        if (!first) std::cout << ", ";
        first = false;
        std::cout << '(' << sprite_name(sprite) << ", " << type << ')';
    }
    std::cout << "]\n";
}

// Pythonの hasattr(sprite, "update_" + anitype) と getattr
// による呼び出しに相当する。 C++にはリフレクションがないため、既知の update_<name>
// を最も派生した型から調べる。 invoke=false は hasattr に相当する。
bool dispatch_named_update(const std::shared_ptr<Sprite>& sprite, const String& name, bool invoke)
{
    if (!sprite || name.empty()) return false;
    const auto run = [&](const auto& fn) {
        if (invoke) fn();
        return true;
    };

    if (const auto battle = std::dynamic_pointer_cast<::cw::sprite::background::BattleCardImage>(sprite);
        battle && name == "battlestart")
        return run([&] { battle->update_battlestart(); });

    if (const auto player = std::dynamic_pointer_cast<::cw::sprite::card::PlayerCard>(sprite)) {
        if (name == "levelup") return run([&] { player->update_levelup(); });
        if (name == "vanish") return run([&] { player->update_vanish(); });
    }

    if (const auto card = std::dynamic_pointer_cast<::cw::sprite::card::CWPyCard>(sprite)) {
        if (name == "normal") return run([&] { card->update_normal(); });
        if (name == "delete") return run([&] { card->update_delete(); });
        if (name == "reversed") return run([&] { card->update_reversed(); });
        if (name == "hidden") return run([&] { card->update_hidden(); });
        if (name == "reverse") return run([&] { card->update_reverse(); });
        if (name == "click") return run([&] { card->update_click(); });
        if (name == "deal") return run([&] { card->update_deal(); });
        if (name == "hide") return run([&] { card->update_hide(); });
        if (name == "lateralvibe") return run([&] { card->update_lateralvibe(); });
        if (name == "axialvibe") return run([&] { card->update_axialvibe(); });
        if (name == "zoomin") return run([&] { card->update_zoomin(); });
        if (name == "zoomin_slow") return run([&] { card->update_zoomin_slow(); });
        if (name == "zoomout") return run([&] { card->update_zoomout(); });
        if (name == "zoomout_slow") return run([&] { card->update_zoomout_slow(); });
        if (name == "shiftup") return run([&] { card->update_shiftup(); });
        if (name == "shiftdown") return run([&] { card->update_shiftdown(); });
        if (name == "scale") return run([&] { card->update_scale(); });
    }

    if (const auto touch = std::dynamic_pointer_cast<::cw::sprite::touchbutton::TouchButton>(sprite);
        touch && name == "shiftup")
        return run([&] { touch->update_shiftup(); });

    if (const auto money = std::dynamic_pointer_cast<::cw::sprite::statusbar::YadoMoneyPanel>(sprite);
        money && name == "blink")
        return run([&] { money->update_blink(); });

    if (const auto button = std::dynamic_pointer_cast<::cw::sprite::statusbar::StatusBarButton>(sprite)) {
        if (name == "normal") return run([&] { button->update_normal(); });
        if (name == "click") return run([&] { button->update_click(); });
        if (name == "blink") return run([&] { button->update_blink(); });
    }

    if (const auto selection = std::dynamic_pointer_cast<::cw::sprite::message::SelectionBar>(sprite);
        selection && name == "click")
        return run([&] { selection->update_click(); });

    if (const auto clickable = std::dynamic_pointer_cast<::cw::sprite::background::ClickableSprite>(sprite)) {
        if (name == "normal") return run([&] { clickable->update_normal(); });
        if (name == "click") return run([&] { clickable->update_click(); });
        if (name == "hide") return run([&] { clickable->update_hide(); });
        if (name == "delete") return run([&] { clickable->update_delete(); });
    }

    if (const auto scrollbar = std::dynamic_pointer_cast<::cw::sprite::scrollbar::ScrollBar>(sprite);
        scrollbar && name == "lazyscroll")
        return run([&] { scrollbar->update_lazyscroll(); });

    if (const auto cell = std::dynamic_pointer_cast<::cw::sprite::animationcell::AnimationCell>(sprite)) {
        if (name == "animation") return run([&] { cell->update_animation(); });
        if (name == "normal") return run([&] { cell->update_normal(); });
    }

    if (const auto transition = std::dynamic_pointer_cast<::cw::sprite::transition::Transition>(sprite);
        transition && name == "transition")
        return run([&] { transition->update_transition(); });

    // Pythonの hasattr と一致するよう、すべての CWPySprite に update_scale を実装する。
    if (name == "scale") return run([&] { sprite->update_scale(); });
    return false;
}

bool has_animation_method(const std::shared_ptr<Sprite>& sprite, const String& anitype)
{
    return dispatch_named_update(sprite, anitype, false);
}

void update_sprite(const std::shared_ptr<Sprite>& sprite)
{
    // Pythonの animate_sprite は常に sprite.update() を呼び
    // 、sprite自身がgetattrで振り分ける。 ここでクラスのメソッドを特別扱いしない。
    if (sprite) sprite->update({}, {});
}

bool runtime_available()
{
    const auto runtime = ::cw::g_cwpy;
    if (!runtime) return false;
    const auto current = std::this_thread::get_id();

    // Frame::_start_wx は、PythonのCWPyスレッドに相当するネイテ
    // ィブの実行を管理する。 m_game_thread が CWPy::run()、m_ui_thread
    // がWin32/wxのUIを担当する。 ゲームスレッドが存在したらそれを正と
    // し、画面なしとの推定でUI・ワーカーからアニメーション状態を変更させない。
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&runtime->m_frame.storage());
    const auto frame = object && *object ? std::dynamic_pointer_cast<::cw::frame::Frame>(*object) : nullptr;
    if (frame) {
        // OnDestroy はモデルへのアクセスを止めて std::thread
        // の終了を待つ。 そのthreadを同時に読むのは不正なので、この境界
        // 用に、CWPyがコマンドキューのロック下で所有者の識別情報を公開する。
        std::thread::id game_owner;
        {
            std::scoped_lock lock(runtime->m_game_exec_mutex);
            game_owner = runtime->m_game_thread_id;
        }
        if (game_owner != std::thread::id{}) return game_owner == current;
        // 初期化済みのUI Frameでゲームスレッドが未開始でも、画面なしの実行環境と
        // は扱わない。 Python版と同様、このスレッドでのアニメーションを拒否する。
        if (frame->m_ui_thread != std::thread::id{}) return false;
    }

    // 単体テスト・画面なしの呼び出し元にはFrame所有のスレッドがない。 CWPy
    // ごとに最初の呼び出し元を結び付け、他のスレッドを拒否する。 弱参照のインスタ
    // ンスキーは、テストが g_cwpy を置換・破棄すると自然にリセットされる。
    std::lock_guard lock(runtime_thread_mutex());
    auto& binding = headless_thread_binding();
    const auto bound_runtime = binding.runtime.lock();
    if (!bound_runtime || bound_runtime.get() != runtime.get()) {
        binding.runtime = runtime;
        binding.owner = current;
        return true;
    }
    return binding.owner == current;
}

bool iconized_stop_enabled()
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_setting || !::cw::g_cwpy->m_setting->m_stop_the_world_with_iconized) return false;
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_frame.storage());
    const auto frame = object && *object ? std::dynamic_pointer_cast<::cw::frame::Frame>(*object) : nullptr;
    return frame && frame->m_is_iconized;
}

bool target_selection_dispatching()
{
    return ::cw::g_cwpy &&
           ::cw::g_cwpy->m_target_selection_dispatching.load(
               std::memory_order_acquire);
}

bool wait_animation_frame(bool can_skip)
{
    if (!::cw::g_cwpy) return false;
    if (!target_selection_dispatching())
        return ::cw::g_cwpy->wait_frame(1, can_skip, std::nullopt, 0);

    // wait_frame() は通常、毎フレーム EventHandler
    // を一度処理するが、戦闘対象のクリックはその処理の中で動
    // いている。 三フレームの視覚的な反応は保ち、入れ子の入力
    // ・イベントループには入らない。 再入しない経路でもメデ
    // ィアは動かす。 表示・クリックのフレームは呼び出し元が
    // 管理するため、ここでspriteグループを更新しない。
    ::cw::g_cwpy->update_presentation();
    ::cw::g_cwpy->lazy_draw();
    const auto fps = ::cw::g_cwpy->m_setting
        ? ::cw::g_cwpy->m_setting->m_fps : Integer{60};
    ::cw::g_cwpy->tick_clock(fps);
    return false;
}

void dispatch_animation_input(bool clearevent)
{
    if (!::cw::g_cwpy || target_selection_dispatching()) return;
    ::cw::g_cwpy->input(false, clearevent, false);
    // Python版の入力を消さないアニメーションは、通常のUSEREVENT
    // をEventHandlerで処理する。 ネイティブの exec_func
    // は共通のゲームキューを使うため、入力前に一回分だけ配信する。 入
    // 力を消すアニメーションは通常のコールバックを保留し、強制コールバッ
    // クだけを配信して、 Pythonの input(inputonly=True)
    // に合わせる。処理中に追加されたものは次回へ残す。
    ::cw::g_cwpy->process_game_commands(clearevent);
    if (const auto handler = ::cw::g_cwpy->get_eventhandler()) handler->run();
}

void add_clip(const std::optional<std::shared_ptr<::cw::lib::Object>>& clip)
{
    if (::cw::g_cwpy && clip && *clip) ::cw::g_cwpy->add_lazydraw(*clip);
}

void finish_animation(bool clearevent, bool statusbutton, bool old_lock, bool draw)
{
    if (!::cw::g_cwpy) return;
    if (target_selection_dispatching()) {
        // 対象アニメーションを始めたクリックは消費するが、 _select_action()
        // が確定する前に EventHandler をもう一度動かさない。
        ::cw::g_cwpy->clear_inputevents();
    } else if (statusbutton) {
        ::cw::g_cwpy->clear_inputevents();
    } else {
        ::cw::g_cwpy->update_mousepos();
        // 戦闘対象のクリックは、すでに EventHandler::lclick_event
        // 内で動く。 アニメーションの時間管理用にネイティブ入力は処理するが
        // 、 外側の操作が選択ガードを持つ間、別の対象を再帰的に実行しない。
        dispatch_animation_input(clearevent);
    }
    if (clearevent && ::cw::g_cwpy->m_lock_menucards) ::cw::g_cwpy->m_lock_menucards = old_lock;
    // 省略された、または一回更新だけのアニメーションでも、最後の描画が保留
    // 中の場合がある。 Python版と同様、最後の描画を完了してから戻り
    // 、 共通のUI通知キューに残してエリア・ZIP操作へ持ち越さない。
    if (draw || ::cw::g_cwpy->m__lazy_draw)
        ::cw::g_cwpy->flush_presentation();
}

}  // namespace

namespace cw::animation {

bool animate_sprite(std::shared_ptr<Sprite> arg_sprite, String arg_anitype, bool arg_clearevent, bool arg_background, bool arg_statusbutton, bool arg_battlespeed)
{
    (void)arg_background;  // Python互換の呼び出し元用に旧フラグを残すが、描画はSDLが管理する。
    if (!runtime_available()) throw std::runtime_error("animation must run with an initialized CWPy thread");
    if (!has_animation_method(arg_sprite, arg_anitype)) {
        report_missing_animation(arg_anitype, arg_sprite);
        return false;
    }

    CardAnimationRollback rollback(arg_sprite);
    const auto lock_runtime = ::cw::g_cwpy;
    const bool initial_lock = lock_runtime->m_lock_menucards;
    ScopeExit lock_cleanup([&]() {
        if (arg_clearevent) lock_runtime->m_lock_menucards = initial_lock;
    });
    bool old_lock = false;
    if (arg_clearevent) {
        old_lock = ::cw::g_cwpy->m_lock_menucards;
        ::cw::g_cwpy->m_lock_menucards = true;
    }
    if (!arg_sprite) return false;
    set_old_status(arg_sprite, status_of(arg_sprite));
    set_status(arg_sprite, arg_anitype);
    arg_sprite->m_skipped = false;
    arg_sprite->m_start_animation = static_cast<::cw::lib::Real>(now_ticks());
    auto stop_world = std::make_shared<::cw::sprite::base::StopTheWorld>();
    stop_world->initialize(arg_sprite->m_start_animation, 0);
    auto card = std::dynamic_pointer_cast<::cw::sprite::card::CWPyCard>(arg_sprite);
    if (arg_battlespeed && card) card->m_battlespeed = true;
    ScopeExit cleanup([&]() {
        arg_sprite->m_skipped = false;
        if (arg_battlespeed && card) card->m_battlespeed = false;
        if (arg_clearevent && ::cw::g_cwpy) ::cw::g_cwpy->m_lock_menucards = old_lock;
    });

    bool skip = _get_skipstatus(arg_clearevent);
    bool draw = false;
    while (::cw::g_cwpy->is_running() && !::cw::g_cwpy->m_cut_animation && status_of(arg_sprite) == arg_anitype) {
        stop_world->is_waiting();
        if (iconized_stop_enabled()) {
            dispatch_animation_input(arg_clearevent);
            ::cw::g_cwpy->wait_frame(1, false, std::nullopt, 0);
            continue;
        }
        arg_sprite->m_start_animation = stop_world->m_start_ticks;
        std::optional<std::shared_ptr<::cw::lib::Object>> clip =
            std::static_pointer_cast<::cw::lib::Object>(make_clip(arg_sprite->m_rect));
        arg_sprite->m_skipped = arg_sprite->m_skipped || skip;
        update_sprite(arg_sprite);
        const auto after_status = status_of(arg_sprite);
        if (const auto clip_object = clip_value(clip))
            clip_object->rect = union_rect(clip_object->rect, arg_sprite->m_rect);
        if (after_status != arg_anitype) {
            add_clip(clip);
            break;
        }
        // Pythonの |= と同様、前のフレームが省略済みでも入力を必ず確認する。
        skip = _get_skipstatus(arg_clearevent) || skip;
        if (skip) {
            add_clip(clip);
        } else {
            auto clip2 = _inputevent(clip, arg_clearevent, arg_statusbutton);
            if (!clip2 || !*clip2) throw std::logic_error("animation input event returned no clip");
            clip = clip2;
            add_clip(clip);
            (void)wait_animation_frame(draw);
        }
        draw = true;
    }
    arg_sprite->m_skipped = false;
    if (arg_battlespeed && card) card->m_battlespeed = false;
    finish_animation(arg_clearevent, arg_statusbutton, old_lock, draw);
    cleanup.release();
    // 正常な finish_animation は、通常入力で解除された
    // ロックをそのまま保つ。 無条件の復元ガードは例外終了だけに使う。
    lock_cleanup.release();
    return skip;
}

void animate_sprites(std::vector<std::shared_ptr<Sprite>> arg_sprites, String arg_anitype, bool arg_clearevent, bool arg_battlespeed)
{
    std::vector<std::tuple<std::shared_ptr<Sprite>, String>> pairs;
    pairs.reserve(arg_sprites.size());
    for (auto& sprite : arg_sprites) pairs.emplace_back(std::move(sprite), arg_anitype);
    (void)animate_sprites2(std::move(pairs), arg_clearevent, arg_battlespeed);
}

bool animate_sprites2(std::vector<std::tuple<std::shared_ptr<Sprite>, String>> arg_sprandanimes, bool arg_clearevent, bool arg_battlespeed)
{
    if (!runtime_available()) throw std::runtime_error("animation must run with an initialized CWPy thread");
    for (const auto& [sprite, anitype] : arg_sprandanimes) {
        if (has_animation_method(sprite, anitype)) continue;
        report_missing_animations(anitype, arg_sprandanimes);
        return false;
    }

    std::vector<CardAnimationRollback> rollbacks;
    rollbacks.reserve(arg_sprandanimes.size());
    for (const auto& [sprite, unused] : arg_sprandanimes) rollbacks.emplace_back(sprite);
    const auto lock_runtime = ::cw::g_cwpy;
    const bool initial_lock = lock_runtime->m_lock_menucards;
    ScopeExit lock_cleanup([&]() {
        if (arg_clearevent) lock_runtime->m_lock_menucards = initial_lock;
    });
    bool old_lock = false;
    if (arg_clearevent) {
        old_lock = ::cw::g_cwpy->m_lock_menucards;
        ::cw::g_cwpy->m_lock_menucards = true;
    }
    const auto tick = static_cast<::cw::lib::Real>(now_ticks());
    auto stop_world = std::make_shared<::cw::sprite::base::StopTheWorld>();
    stop_world->initialize(tick, 0);
    for (auto& [sprite, anitype] : arg_sprandanimes) {
        set_old_status(sprite, status_of(sprite));
        set_status(sprite, anitype);
        sprite->m_skipped = false;
        sprite->m_start_animation = tick;
        if (arg_battlespeed) if (const auto card = std::dynamic_pointer_cast<::cw::sprite::card::CWPyCard>(sprite)) card->m_battlespeed = true;
    }
    ScopeExit cleanup([&]() {
        for (auto& [sprite, unused] : arg_sprandanimes) {
            (void)unused;
            if (!sprite) continue;
            sprite->m_skipped = false;
            if (arg_battlespeed) if (const auto card = std::dynamic_pointer_cast<::cw::sprite::card::CWPyCard>(sprite)) card->m_battlespeed = false;
        }
        if (arg_clearevent && ::cw::g_cwpy) ::cw::g_cwpy->m_lock_menucards = old_lock;
    });
    bool skip = _get_skipstatus(arg_clearevent);
    bool draw = false;
    bool animating = true;
    while (::cw::g_cwpy->is_running() && !::cw::g_cwpy->m_cut_animation && animating) {
        stop_world->is_waiting();
        if (iconized_stop_enabled()) {
            dispatch_animation_input(arg_clearevent);
            ::cw::g_cwpy->wait_frame(1, false, std::nullopt, 0);
            continue;
        }
        std::optional<::cw::lib::Rect> union_clip;
        bool updated = false;
        for (auto& [sprite, anitype] : arg_sprandanimes) {
            if (sprite) sprite->m_start_animation = stop_world->m_start_ticks;
            if (status_of(sprite) != anitype) continue;
            if (union_clip) *union_clip = union_rect(*union_clip, sprite->m_rect);
            else union_clip = sprite->m_rect;
            sprite->m_skipped = sprite->m_skipped || skip;
            update_sprite(sprite);
            if (status_of(sprite) == anitype) updated = true;
            if (union_clip) *union_clip = union_rect(*union_clip, sprite->m_rect);
        }
        if (!updated) {
            if (union_clip) add_clip(make_clip(*union_clip));
            break;
        }
        // Pythonの |= と同様、前のフレームが省略済みでも入力を必ず確認する。
        skip = _get_skipstatus(arg_clearevent) || skip;
        if (skip) {
            if (union_clip) add_clip(make_clip(*union_clip));
        } else {
            auto clip = union_clip ? std::optional<std::shared_ptr<::cw::lib::Object>>(make_clip(*union_clip)) : std::nullopt;
            clip = _inputevent(clip, arg_clearevent, false);
            if (clip) add_clip(clip);
            (void)wait_animation_frame(draw);
        }
        draw = true;
        animating = false;
        for (const auto& [sprite, anitype] : arg_sprandanimes) if (status_of(sprite) == anitype) { animating = true; break; }
    }
    for (auto& [sprite, unused] : arg_sprandanimes) {
        sprite->m_skipped = false;
        if (arg_battlespeed) if (const auto card = std::dynamic_pointer_cast<::cw::sprite::card::CWPyCard>(sprite)) card->m_battlespeed = false;
    }
    finish_animation(arg_clearevent, false, old_lock, draw);
    cleanup.release();
    // 正常な finish_animation は、通常入力で解除された
    // ロックをそのまま保つ。 無条件の復元ガードは例外終了だけに使う。
    lock_cleanup.release();
    return skip;
}

std::optional<std::shared_ptr<::cw::lib::Object>> _inputevent(std::optional<std::shared_ptr<::cw::lib::Object>> arg_clip, bool arg_clearevent, bool arg_statusbutton)
{
    if (!::cw::g_cwpy) return arg_clip;
    if (!runtime_available()) throw std::runtime_error("animation input must run on the CWPy game thread");
    if (target_selection_dispatching()) return arg_clip;
    if (arg_statusbutton) {
        ::cw::g_cwpy->clear_inputevents();
    } else {
        ::cw::g_cwpy->update_mousepos();
        ::cw::g_cwpy->update_groups({object_value(::cw::g_cwpy->m_sbargrp)});
        dispatch_animation_input(arg_clearevent);
    }
    return arg_clip;
}

void start_animation(std::shared_ptr<Sprite> arg_sprite, String arg_anitype)
{
    if (!runtime_available()) throw std::runtime_error("animation must run with an initialized CWPy thread");
    if (!has_animation_method(arg_sprite, arg_anitype)) {
        report_missing_animation(arg_anitype, arg_sprite);
        return;
    }
    if (!arg_sprite) return;
    if (arg_sprite->m_anitype.empty()) set_old_status(arg_sprite, status_of(arg_sprite));
    set_status(arg_sprite, arg_anitype);
    arg_sprite->m_anitype = arg_anitype;
    arg_sprite->m_start_animation = static_cast<::cw::lib::Real>(now_ticks());
    set_frame(arg_sprite, 0);
    ::cw::g_cwpy->m_animations.insert(arg_sprite);
}

bool _get_skipstatus(bool arg_clearevent)
{
    if (!::cw::g_cwpy) return false;
    if (!runtime_available()) throw std::runtime_error("animation skip polling must run on the CWPy game thread");
    if (!arg_clearevent && ::cw::g_cwpy->m_keyevent && (::cw::g_cwpy->m_keyevent->is_keyin(13) || ::cw::g_cwpy->m_keyevent->is_mousein(std::nullopt))) {
        ::cw::g_cwpy->m_cut_animation = true;
        return true;
    }
    if (!arg_clearevent || !::cw::g_cwpy->m_setting || !::cw::g_cwpy->m_setting->m_can_skipanimation) return false;
    const bool breakflag = ::cw::g_cwpy->get_animation_breakflag(::cw::g_cwpy->m_setting->m_can_skipwait_with_wheel);
    if (breakflag) return true;
    if (::cw::g_cwpy->m_event) {
        const auto event = ::cw::g_cwpy->m_event->get_event();
        if (event && *event && ::cw::g_cwpy->m_event->is_stoped()) return true;
    }
    // Python版と同様、状態を変えるキー確認より先に、停止済みイベントで処理を打ち切る。
    if (::cw::g_cwpy->m_keyevent && (::cw::g_cwpy->m_keyevent->is_keyin(13) || ::cw::g_cwpy->m_keyevent->is_mousein(std::nullopt))) return true;
    return false;
}

::cw::lib::String sprite_status(const std::shared_ptr<Sprite>& arg_sprite)
{
    return status_of(arg_sprite);
}

void set_sprite_frame(const std::shared_ptr<Sprite>& arg_sprite, Integer arg_frame)
{
    set_frame(arg_sprite, arg_frame);
}

bool call_named_update(const std::shared_ptr<Sprite>& arg_sprite)
{
    if (!arg_sprite) return false;
    return dispatch_named_update(arg_sprite, status_of(arg_sprite), true);
}

void main()
{
    // Python版と同じく、import/リンク時の副作用は持たない。
}

}  // namespace cw::animation
