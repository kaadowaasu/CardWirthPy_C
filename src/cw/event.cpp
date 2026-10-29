// cw/event.py の実行処理の移植。 イベント状態の変更と
// 共通バックエンドの呼び出しを、Python 版と同じ順序で行う。
#include "cw/event.hpp"
#include "cw/keycode_level.hpp"
#include "cw/lib/python_integer.hpp"
#include "app/debug_control_server.hpp"
#include "cw/advlog.hpp"
#include "cw/animation.hpp"
#include "cw/argparser.hpp"
#include "cw/battle.hpp"
#include "cw/cw.hpp"
#include "cw/data.hpp"
#include "cw/character.hpp"
#include "cw/content.hpp"
#include "cw/debug/debugger.hpp"
#include "cw/deck.hpp"
#include "cw/dice.hpp"
#include "cw/effectmotion.hpp"
#include "cw/eventhandler.hpp"
#include "cw/frame.hpp"
#include "cw/game_event_queue.hpp"
#include "cw/header.hpp"
#include "cw/setting.hpp"
#include "cw/sprite/card.hpp"
#include "cw/sprite/background.hpp"
#include "cw/sprite/message.hpp"
#include "cw/sprite/statusbar.hpp"
#include "cw/sprite/base.hpp"
#include "cw/thread.hpp"
#include "cw/util.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace {
using ::cw::lib::Integer;
using ::cw::lib::String;
using ::cw::lib::Value;
using ::cw::lib::Real;
using ::cw::data::CWPyElement;
using ::cw::event::Event;
using ::cw::event::EventEngine;
using ::cw::sprite::card::CWPyCard;
using ::cw::character::Character;

template<class T>
std::shared_ptr<T> object_value(const Value& value) {
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
    return object && *object ? std::dynamic_pointer_cast<T>(*object) : nullptr;
}

Value object_value(const std::shared_ptr<::cw::lib::Object>& object) {
    return object ? Value(object) : Value{};
}

Value stack_trace_frame(const std::shared_ptr<Event>& arg_event,
                        std::shared_ptr<CWPyElement> arg_content,
                        Integer arg_line_index) {
    if (!arg_event) return {};
    auto frame = std::make_shared<::cw::debug::debugger::StackTraceView::StackTraceFrame>();
    frame->event = arg_event;
    frame->content = std::move(arg_content);
    frame->line_index = arg_line_index;
    return object_value(std::static_pointer_cast<::cw::lib::Object>(frame));
}

Value element_value(const std::shared_ptr<CWPyElement>& element) {
    return element ? Value(std::static_pointer_cast<::cw::lib::Object>(element)) : Value{};
}

std::shared_ptr<CWPyElement> event_element(const Value& value) {
    return object_value<CWPyElement>(value);
}

String value_string(const Value& value, String fallback = {}) {
    if (const auto* p = std::get_if<String>(&value.storage())) return *p;
    if (const auto* p = std::get_if<::cw::lib::Decimal>(&value.storage())) return p->to_string(true);
    if (const auto* p = std::get_if<Integer>(&value.storage())) return std::to_string(*p);
    if (const auto* p = std::get_if<bool>(&value.storage())) return *p ? "True" : "False";
    if (const auto* p = std::get_if<Real>(&value.storage())) {
        if (!std::isfinite(*p)) return fallback;
        std::ostringstream text;
        text << std::setprecision(std::numeric_limits<Real>::max_digits10) << *p;
        return text.str();
    }
    return fallback;
}

::cw::lib::Decimal decimal_from_real(Real value) {
    if (!std::isfinite(value)) throw std::invalid_argument("event: non-finite Real");
    std::ostringstream text;
    text << std::setprecision(std::numeric_limits<Real>::max_digits10) << value;
    return ::cw::lib::Decimal::parse(text.str());
}

Integer value_integer(const Value& value, Integer fallback = 0) {
    if (const auto* p = std::get_if<Integer>(&value.storage())) return *p;
    if (const auto* p = std::get_if<bool>(&value.storage())) return *p ? 1 : 0;
    if (const auto* p = std::get_if<::cw::lib::Decimal>(&value.storage())) {
        try { return p->to_integer(::cw::lib::DecimalRounding::Down); }
        catch (const ::cw::lib::DecimalError&) { return fallback; }
    }
    if (const auto* p = std::get_if<Real>(&value.storage())) {
        try { return decimal_from_real(*p).to_integer(::cw::lib::DecimalRounding::Down); }
        catch (...) { return fallback; }
    }
    if (const auto* p = std::get_if<String>(&value.storage())) {
        if (const auto normalized = ::cw::lib::normalize_python_integer(*p)) {
            try { return ::cw::lib::Decimal::parse(*normalized).to_integer(::cw::lib::DecimalRounding::Down); }
            catch (const ::cw::lib::DecimalError&) {}
        }
    }
    return fallback;
}

bool value_bool(const Value& value, bool fallback = false) {
    if (const auto* p = std::get_if<bool>(&value.storage())) return *p;
    if (const auto* p = std::get_if<Integer>(&value.storage())) return *p != 0;
    if (const auto* p = std::get_if<::cw::lib::Decimal>(&value.storage())) return !p->is_zero();
    if (const auto* p = std::get_if<Real>(&value.storage())) return std::isfinite(*p) && *p != 0.0;
    const auto text = value_string(value);
    if (text == "True" || text == "true" || text == "1") return true;
    if (text == "False" || text == "false" || text == "0") return false;
    return fallback;
}

Integer parse_integer_token(const String& token) {
    const auto normalized = ::cw::lib::normalize_python_integer(token);
    if (!normalized) throw std::invalid_argument("event: invalid ignition key number: " + token);
    try {
        return ::cw::lib::Decimal::parse(*normalized).to_integer(::cw::lib::DecimalRounding::Down);
    } catch (const ::cw::lib::DecimalError&) {
        throw std::invalid_argument("event: ignition key number is out of Integer range: " + token);
    }
}

Integer checked_size_to_integer(std::size_t value, const char* context) {
    if (value > static_cast<std::size_t>(std::numeric_limits<Integer>::max()))
        throw std::overflow_error(String("event: ") + context + " exceeds Integer range");
    return static_cast<Integer>(value);
}

std::vector<String> ignition_text_list(const String& text) {
    if (text.empty()) return {};
    auto tokens = ::cw::util::decodetextlist(text);
    tokens.erase(std::remove_if(tokens.begin(), tokens.end(), [](const String& token) { return token.empty(); }),
                 tokens.end());
    return tokens;
}

std::vector<std::shared_ptr<CWPyElement>> element_list(const std::shared_ptr<CWPyElement>& root, const String& path) {
    if (!root) return {};
    const auto found = root->find(path);
    if (!found || !*found) return {};
    if ((*found)->m_tag == "Contents" || (*found)->m_tag == "Events") return (*found)->m_children;
    return {*found};
}

std::shared_ptr<::cw::event::EventInterface> event_interface() {
    return ::cw::g_cwpy ? ::cw::g_cwpy->m_event : nullptr;
}

std::shared_ptr<Character> character_value(const Value& value) {
    return object_value<Character>(value);
}

std::shared_ptr<CWPyCard> card_value(const Value& value) {
    return object_value<CWPyCard>(value);
}

Value card_object(const std::shared_ptr<CWPyCard>& card) {
    return card ? Value(std::static_pointer_cast<::cw::lib::Object>(card)) : Value{};
}

Value backpack_value() {
    auto result = std::make_shared<::cw::data::VariantSequence>();
    if (::cw::g_cwpy && ::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata && (*::cw::g_cwpy->m_ydata)->m_party && *(*::cw::g_cwpy->m_ydata)->m_party) {
        for (const auto& header : (*(*::cw::g_cwpy->m_ydata)->m_party)->m_backpack) if (header) result->m_values.emplace_back(std::static_pointer_cast<::cw::lib::Object>(header));
    }
    return Value(result);
}

void set_event_value(Value& destination, const std::shared_ptr<::cw::lib::Object>& object) {
    destination = object ? Value(object) : Value{};
}

template<class T>
std::shared_ptr<T> clone_object(const std::shared_ptr<T>& source) {
    if (!source) return nullptr;
    auto result = std::make_shared<T>();
    *result = *source;
    return result;
}

bool has_tag(const std::shared_ptr<CWPyElement>& element, const String& tag) {
    return element && element->m_tag == tag;
}

std::shared_ptr<CWPyElement> child_named(const std::shared_ptr<CWPyElement>& element, const String& name) {
    if (!element) return nullptr;
    for (const auto& child : element->m_children) if (child && child->m_tag == name) return child;
    return nullptr;
}

std::shared_ptr<::cw::data::ScenarioData> scenario_data() {
    return ::cw::g_cwpy ? object_value<::cw::data::ScenarioData>(::cw::g_cwpy->m_sdata) : nullptr;
}

bool scenario_continues() {
    const auto data = scenario_data();
    return ::cw::g_cwpy && ::cw::g_cwpy->is_playingscenario() && data && !data->m_in_f9;
}

std::vector<String> can_unconscious_motions()
{
    const auto& types = ::cw::effectmotion::g_CAN_UNCONSCIOUS;
    return {std::get<0>(types), std::get<1>(types), std::get<2>(types), std::get<3>(types),
            std::get<4>(types), std::get<5>(types), std::get<6>(types), std::get<7>(types)};
}

String runaway_keycode()
{
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_msgs)
        return ::cw::g_cwpy->m_setting->m_msgs->get("runaway_keycode", "逃走");
    return "逃走";
}

Value clone_variant_value(const Value& source) {
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&source.storage());
    if (!object || !*object) return source;
    if (const auto sequence = std::dynamic_pointer_cast<::cw::data::VariantSequence>(*object)) {
        auto copy = std::make_shared<::cw::data::VariantSequence>();
        copy->m_values.reserve(sequence->m_values.size());
        for (const auto& value : sequence->m_values) copy->m_values.push_back(clone_variant_value(value));
        return Value(std::static_pointer_cast<::cw::lib::Object>(copy));
    }
    if (const auto structure = std::dynamic_pointer_cast<::cw::data::StructVal>(*object)) {
        auto copy = std::make_shared<::cw::data::StructVal>(*structure);
        copy->m_member_values.clear();
        copy->m_member_values.reserve(structure->m_member_values.size());
        for (const auto& value : structure->m_member_values) copy->m_member_values.push_back(clone_variant_value(value));
        auto members = std::make_shared<::cw::data::VariantSequence>();
        members->m_values = copy->m_member_values;
        copy->m_members = Value(std::static_pointer_cast<::cw::lib::Object>(members));
        return Value(std::static_pointer_cast<::cw::lib::Object>(copy));
    }
    return source;
}

Value initialization_value(const std::vector<String>& values) {
    auto sequence = std::make_shared<::cw::data::VariantSequence>();
    for (const auto& value : values) sequence->m_values.emplace_back(value);
    return Value(std::static_pointer_cast<::cw::lib::Object>(sequence));
}
}

namespace cw::event {
void Event::initialize(std::optional<std::shared_ptr<::cw::data::CWPyElement>> arg_event)
{
    m_parent.reset(); m_base.reset(); m_inusecard.reset(); m_error.reset();
    m_index = 0; m_line_index = 0; m_keynums.clear(); m_keycodes.clear();
    m_keycode_matching = "Or"; m_packageid = 0; m_trees.clear(); m_treekeys.clear();
    m_starttree.reset(); m_cur_content.reset(); m_nowrunningcontents.clear();
    m_force_nextcontent = Value{}; m_force_nextcontent_index = Value(-1); m_skip_action = false;
    m_is_active = true; m__reset_changestate = true; m__stored_in_cardeffectmotion.clear();
    m__stored_in_inusecardevent.clear(); m__stored_specialchars.clear();
    m_flags.clear(); m_steps.clear(); m_variants.clear();
    auto event = arg_event.value_or(nullptr); set_event_value(m__event, event);
    if (::cw::g_cwpy && ::cw::g_cwpy->is_playingscenario()) {
        const auto data = object_value<::cw::data::ScenarioData>(::cw::g_cwpy->m_sdata);
        if (data) { m_scenario = value_string(data->m_name); m_author = value_string(data->m_author); }
    } else { m_scenario.clear(); m_author.clear(); }
    if (!event) return;
    if (event->hasfind("Ignitions//Number", ""))
        for (const auto& token : ignition_text_list(event->gettext("Ignitions//Number", "")))
            m_keynums.push_back(parse_integer_token(token));
    if (event->hasfind("Ignitions//KeyCodes", ""))
        for (const auto& token : ignition_text_list(event->gettext("Ignitions//KeyCodes", ""))) m_keycodes.push_back(token);
    m_keycode_matching = event->getattr("Ignitions", "keyCodeMatchingType", "Or");
    const auto contents = child_named(event, "Contents");
    if (!contents) return;
    for (const auto& content : contents->m_children) {
        if (!content) continue;
        String name;
        if (content->m_tag == "ContentsLine" && !content->m_children.empty() && content->m_children.front()) name = content->m_children.front()->get("name", {});
        else name = content->get("name", {});
        if (!m_trees.count(name)) { m_trees[name] = content; m_treekeys.push_back(name); }
        if (!m_starttree) { m_starttree = content; m_cur_content = content; }
    }
}

std::shared_ptr<::cw::event::Event> Event::copy_event()
{
    auto result = std::make_shared<Event>(); result->initialize(::cw::data::copydata(event_element(m__event), false)); return result;
}

void Event::copy_from(std::shared_ptr<::cw::event::Event> arg_event, std::optional<std::tuple<::cw::lib::Integer, std::optional<std::tuple<::cw::lib::String, ::cw::lib::String, bool, bool, bool>>>> arg_versionhint_base)
{
    if (!arg_event) return;
    if (!m_base) { m_base = std::make_shared<Event>(); (*m_base)->copy_from_impl(shared_from_this()); }
    copy_from_impl(arg_event);
    if (!m__versionhint_base) m__versionhint_base = arg_versionhint_base;
}

void Event::copy_from_impl(std::shared_ptr<::cw::event::Event> arg_event)
{
    if (!arg_event) return;
    m_trees = arg_event->m_trees; m_treekeys = arg_event->m_treekeys; m_starttree = arg_event->m_starttree;
    m_scenario = arg_event->m_scenario; m_author = arg_event->m_author;
    m_flags = arg_event->m_flags; m_steps = arg_event->m_steps; m_variants = arg_event->m_variants;
}

void Event::set_variant_binding(const String& name, std::shared_ptr<::cw::data::Variant> variant)
{
    const auto found = m_variants.find(name);
    const auto previous = found == m_variants.end() ? nullptr : found->second;
    // Python はこれらの辞書を共有する。 この Event が元のカードスコー
    // プを使っている間だけ所属を同期し、パッケージやコピーしたマップには適用しない。
    if (!m_base && previous) {
        if (auto* card = dynamic_cast<CardEvent*>(this)) {
            const auto saved = card->m__variants.find(name);
            if (saved != card->m__variants.end() && object_value<::cw::data::Variant>(saved->second) == previous)
                saved->second = Value(std::static_pointer_cast<::cw::lib::Object>(variant));
        }
        if (const auto header = m_inusecard.value_or(nullptr)) {
            const auto binding = header->m_variants.find(name);
            if (binding != header->m_variants.end() && object_value<::cw::data::Variant>(binding->second) == previous)
                binding->second = Value(std::static_pointer_cast<::cw::lib::Object>(variant));
        }
    }
    m_variants[name] = std::move(variant);
}

void Event::store_inusedata(bool arg_selectuser)
{
    auto iface = event_interface(); if (!iface) return;
    if (arg_selectuser) if (auto* card = dynamic_cast<CardEvent*>(this)) {
        iface->set_selectedmember(character_value(card->m_user)); iface->set_inusecard(card->Event::m_inusecard);
    }
    m__stored_in_cardeffectmotion.push_back(iface->m_in_cardeffectmotion);
    m__stored_in_inusecardevent.push_back(iface->m_in_inusecardevent);
    iface->m_in_cardeffectmotion = false; iface->m_in_inusecardevent = false;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_rsrc) {
        m__stored_specialchars.push_back(::cw::g_cwpy->m_rsrc->m_specialchars);
        if (const auto data = scenario_data()) ::cw::g_cwpy->m_rsrc->m_specialchars = data->m_specialchars;
    } else {
        m__stored_specialchars.push_back(Value{});
    }
}

void Event::restore_inusedata()
{
    auto iface = event_interface(); if (!iface) return;
    if (!m__stored_in_cardeffectmotion.empty()) { iface->m_in_cardeffectmotion = m__stored_in_cardeffectmotion.back(); m__stored_in_cardeffectmotion.pop_back(); }
    if (!m__stored_in_inusecardevent.empty()) { iface->m_in_inusecardevent = m__stored_in_inusecardevent.back(); m__stored_in_inusecardevent.pop_back(); }
    if (!m__stored_specialchars.empty()) {
        if (::cw::g_cwpy && ::cw::g_cwpy->m_rsrc) ::cw::g_cwpy->m_rsrc->m_specialchars = m__stored_specialchars.back();
        m__stored_specialchars.pop_back();
    }
}

void Event::start(bool arg_skip_effect_card_motion)
{
#if defined(CARDWIRTH_LOG_ENABLED)
    ::cardwirth::debug::log(
        "Event start begin: scenario=" + m_scenario +
        " keynums=" + std::to_string(m_keynums.size()) +
        " keycodes=" + std::to_string(m_keycodes.size()) +
        " has_starttree=" + (m_starttree && *m_starttree ? "true" : "false"));
#endif
    if (::cw::g_cwpy && !::cw::g_cwpy->m_in_actionevent) {
        const bool showbuttons = !::cw::g_cwpy->is_playingscenario() ||
            ::cw::g_cwpy->m_areaid == value_integer(std::get<0>(::cw::g_AREAS_SP)) ||
            ::cw::g_cwpy->m_areaid == value_integer(std::get<1>(::cw::g_AREAS_SP)) ||
            ::cw::g_cwpy->m_areaid == value_integer(std::get<2>(::cw::g_AREAS_SP)) ||
            ::cw::g_cwpy->m_areaid == value_integer(std::get<3>(::cw::g_AREAS_SP)) ||
            ::cw::g_cwpy->m_areaid == value_integer(std::get<4>(::cw::g_AREAS_SP));
        if (::cw::g_cwpy->m_statusbar)
            ::cw::g_cwpy->m_statusbar->change(showbuttons, false);
        if (::cw::g_cwpy->m_statusbar)
            ::cw::g_cwpy->update_statusimgs(
                true, std::make_shared<::cw::RectValue>(::cw::g_cwpy->m_statusbar->m_rect));
    }
    if (const auto data = scenario_data();
        data && !static_cast<::cw::data::SystemData*>(data.get())->m_infocards_beforeevent) {
        const auto infocards = data->get_infocards(false);
        static_cast<::cw::data::SystemData*>(data.get())->m_infocards_beforeevent =
            std::set<Integer>(infocards.begin(), infocards.end());
    }
    if (m__reset_changestate) if (auto iface = event_interface()) iface->m_is_changestate = false;
    auto remember_error = [&](const std::shared_ptr<EventError>& error) {
        m_error = error;
        if (auto iface = event_interface(); iface && iface->m_effectevent && *iface->m_effectevent) (*iface->m_effectevent)->m_error = error;
        stop();
    };
    try { run(false, false, arg_skip_effect_card_motion); }
    catch (const XmlContentCriticalError&) { throw; }
    catch (const ActionBreakError&) { throw; }
    catch (const StartBattleError& error) { remember_error(std::make_shared<StartBattleError>(error)); }
    catch (const ScenarioBadEndError& error) { remember_error(std::make_shared<ScenarioBadEndError>(error)); }
    catch (const ScenarioEndError& error) { remember_error(std::make_shared<ScenarioEndError>(error)); }
    catch (const EffectBreakError& error) {
        // 行動の更新はイベント全体の境界ではない。ここでキャンセルを飲み込むと、
        // 中断したパッケージが次のフレームで再開してしまう。 SetActionEvent
        // が元のインターフェースを復元してから、外側の Event::start
        // が同じ例外を処理する。Wasm ワーカーでも同様とする。
        if (::cw::g_cwpy && ::cw::g_cwpy->m_in_actionevent) throw;
        remember_error(std::make_shared<EffectBreakError>(error));
    }
    catch (const AreaChangeError& error) { remember_error(std::make_shared<AreaChangeError>(error)); }
    catch (const EventError& error) { remember_error(std::make_shared<EventError>(error)); }
    if (auto iface = event_interface(); iface && iface->m_exit_func) {
        if (::cw::g_cwpy) ::cw::g_cwpy->exec_func(*iface->m_exit_func); else (*iface->m_exit_func)({}, {});
        iface->m_exit_func.reset();
    }
    if (auto card = dynamic_cast<CardEvent*>(this)) card->end(); else end();
#if defined(CARDWIRTH_LOG_ENABLED)
    ::cardwirth::debug::log(
        "Event start end: scenario=" + m_scenario +
        " error=" + (m_error && *m_error ? "true" : "false"));
#endif
}

// Python のリストを反復中にイベントを削除すると、一つお
// きに処理を飛ばす不具合がある。 共通リストを消す前に全イベ
// ントを復元・消去し、停止したイベントにカーソルを残さない。
void Event::stop()
{
    if (::cw::g_cwpy && !(m_error && *m_error &&
        std::dynamic_pointer_cast<EffectBreakError>(*m_error)))
        ::cw::g_cwpy->m_pre_dialogs.clear();
    auto iface = event_interface(); if (!iface) return;
    for (const auto& event : iface->get_events()) if (event) { if (event->m_base) { event->copy_from_impl(*event->m_base); event->m_base.reset(); } event->clear(); }
    iface->clear_events();
}

void Event::run(bool arg_perf, bool arg_isinside, bool arg_skip_effect_card_motion)
{
    auto iface = event_interface(); if (!iface) return;
    const Integer inside_level = iface->get_currentstack();
    if (!arg_isinside) { iface->clear_stackinfo(); iface->append_stackinfo(Value(std::static_pointer_cast<::cw::lib::Object>(shared_from_this()))); iface->append_event(shared_from_this()); }
    // Py と同様、perf は未使用の公開引数として残す。 通常のイベ
    // ント実行中に、デバッガー・イベントの待機を省くためには使わない。
    (void)arg_perf;
    const auto restore_interrupted_versionhint = [&]() {
        if (m_nowrunningcontents.empty()) return;
        const auto& frame = m_nowrunningcontents.front();
        // Py の finally と同様、最初のパッケージフレームのコンテントと行を復元す
        // る。 そのフレームに復元すべきパッケージイベント・バージョン情報がない場合も行う。
        m_cur_content = std::get<1>(frame);
        m_line_index = std::get<2>(frame);
        if (std::get<0>(frame) && *std::get<0>(frame))
            if (const auto data = scenario_data())
                data->set_versionhint(::cw::g_HINT_AREA, std::get<3>(frame));
    };
    try {
        while (true) {
            m_index = 0; auto next = get_nextcontents();
            while (::cw::g_cwpy && ::cw::g_cwpy->is_running() && next && m_index >= 0) {
                if (next->empty()) break;
                if (m_index >= checked_size_to_integer(next->size(), "event content count")) m_index = 0;
                m_cur_content = (*next)[static_cast<std::size_t>(m_index)];
                if (!(::cw::g_cwpy && ::cw::g_cwpy->m_in_actionevent)) iface->wait();
                // 終了状態の公開によって wait() が終わる場合がある
                // 。 その境界の後に、選択済みのコンテントを実行しない。
                if (!::cw::g_cwpy || !::cw::g_cwpy->is_running()) throw ::cw::thread::CWPyRunningError{};
                action(); next = get_nextcontents();
            }
            if (!m_nowrunningcontents.empty()) {
                const auto stack = iface->get_currentstack();
                auto frame = m_nowrunningcontents.back(); m_nowrunningcontents.pop_back();
                m_cur_content = std::get<1>(frame); m_line_index = std::get<2>(frame); iface->pop_stackinfo();
                if (std::get<0>(frame) && *std::get<0>(frame)) { if (arg_isinside) (*std::get<0>(frame))->run_exit_inside(); else (*std::get<0>(frame))->run_exit(); }
                if (std::get<0>(frame) && *std::get<0>(frame))
                    if (const auto data = scenario_data()) data->set_versionhint(::cw::g_HINT_AREA, std::get<3>(frame));
                if (arg_isinside && inside_level == stack) break;
            } else {
                if (auto card = dynamic_cast<CardEvent*>(this)) card->run_exit(arg_skip_effect_card_motion); else run_exit();
                break;
            }
        }
    } catch (...) {
        restore_interrupted_versionhint();
        throw;
    }
}

void Event::run_exit()
{
    run_exit_inside(); clear();
}

void Event::run_exit_inside()
{
    if (m_base) { copy_from_impl(*m_base); m_base.reset(); }
    if (auto iface = event_interface(); iface && !iface->get_events().empty()) iface->pop_event();
}

void Event::end()
{
    if (m_base) {
        copy_from_impl(*m_base); clear(); m_base.reset();
        if (m__versionhint_base) if (const auto data = scenario_data()) data->set_versionhint(std::get<0>(*m__versionhint_base), std::get<1>(*m__versionhint_base));
        m__versionhint_base.reset();
    }
    auto iface = event_interface();
    const bool area_change = m_error && *m_error && std::dynamic_pointer_cast<AreaChangeError>(*m_error);
    const bool bad_end = m_error && *m_error && std::dynamic_pointer_cast<ScenarioBadEndError>(*m_error);
    const auto data = scenario_data();
    if (::cw::g_cwpy && !area_change && !bad_end &&
        ::cw::g_cwpy->m_status != "Title" && ::cw::g_cwpy->m_status != "GameOver" &&
        !(::cw::g_cwpy->is_gameover() && data && data->can_gameover()) &&
        iface && !iface->is_stoped()) {
        ::cw::g_cwpy->show_party();
        if (!::cw::g_cwpy->m_is_updating_skin)
            ::cw::g_cwpy->disposition_pcards();
        if (::cw::g_cwpy->m_background)
            ::cw::g_cwpy->m_background->m_reload_jpdcimage = true;
    }
    if (iface && !area_change && !(::cw::g_cwpy && ::cw::g_cwpy->is_battlestatus() && ::cw::g_cwpy->is_gameover() && data && data->can_gameover())) iface->set_selectedmember(std::nullopt);
    if (iface) iface->clear();
    if (::cw::g_cwpy && ::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata && (*::cw::g_cwpy->m_ydata)->m_party && *(*::cw::g_cwpy->m_ydata)->m_party) {
        auto party = *(*::cw::g_cwpy->m_ydata)->m_party; party->sort_backpack(std::nullopt, std::nullopt);
        for (const auto& header : party->m_backpack) if (header) header->do_write(false);
    }
    if (::cw::g_cwpy) ::cw::g_cwpy->update_mcardlist();
    if (::cw::g_cwpy && ::cw::g_cwpy->is_battlestatus()) {
        if (::cw::g_cwpy->is_gameover() && data && data->can_gameover()) {
            const bool old_gameover = data && ::cw::g_cwpy->m_sct &&
                ::cw::g_cwpy->m_sct->lessthan("1.15", data->get_versionhint(0));
            if (!old_gameover) ::cw::g_cwpy->set_gameoverstatus(false, true);
            throw ::cw::battle::BattleDefeatError{};
        }
        if (m_error && *m_error && std::dynamic_pointer_cast<StartBattleError>(*m_error)) throw ::cw::battle::BattleStartBattleError{};
        if (area_change) throw ::cw::battle::BattleAreaChangeError{};
        ::cw::g_cwpy->fix_updated_file(true);
    } else if (::cw::g_cwpy && ::cw::g_cwpy->is_gameover() && data && data->can_gameover() && ::cw::g_cwpy->is_playingscenario() && !data->m_in_f9 && ::cw::g_cwpy->m_areaid >= 0) {
        ::cw::g_cwpy->set_gameover();
    }
}

std::optional<std::shared_ptr<::cw::event::EventEngine>> Event::get_events(std::shared_ptr<::cw::sprite::card::CWPyCard> arg_target)
{
    if (!arg_target) return std::nullopt;
    if (auto enemy = std::dynamic_pointer_cast<::cw::sprite::card::EnemyCard>(arg_target)) return enemy->m_events;
    if (auto menu = std::dynamic_pointer_cast<::cw::sprite::card::MenuCard>(arg_target)) return menu->m_events;
    if (std::dynamic_pointer_cast<::cw::sprite::card::PlayerCard>(arg_target) && ::cw::g_cwpy) {
        auto data = object_value<::cw::data::SystemData>(::cw::g_cwpy->m_sdata);
        if (data) return data->m_playerevents;
    }
    return std::nullopt;
}

std::optional<std::shared_ptr<::cw::event::Event>> Event::ignition_characterevent(std::shared_ptr<::cw::character::Character> arg_target, bool arg_can_unconscious, std::vector<::cw::lib::String> arg_keycodes)
{
    auto card = std::dynamic_pointer_cast<CWPyCard>(arg_target);
    auto events = get_events(card);
    if (events && *events && arg_target && (arg_can_unconscious || (!arg_target->is_unconscious() && !arg_target->is_vanished()))) return (*events)->check_keycodes(std::move(arg_keycodes), false);
    return std::nullopt;
}

std::optional<std::shared_ptr<::cw::event::Event>> Event::ignition_deadevent(std::shared_ptr<::cw::character::Character> arg_target, std::vector<::cw::lib::String> arg_keycodes)
{
    const auto runaway = runaway_keycode();
    if (!runaway.empty() && std::find(arg_keycodes.begin(), arg_keycodes.end(), runaway) != arg_keycodes.end()) return std::nullopt;
    auto card = std::dynamic_pointer_cast<CWPyCard>(arg_target); auto events = get_events(card);
    if (events && *events && arg_target && ((arg_target->is_dead() && arg_target->get_card_status() != "hidden") || arg_target->is_vanished())) return (*events)->check_keynum(1);
    return std::nullopt;
}

std::optional<std::shared_ptr<::cw::event::Event>> Event::ignition_menucardevent(std::shared_ptr<::cw::sprite::card::MenuCard> arg_target, std::vector<::cw::lib::String> arg_keycodes)
{
    auto events = get_events(arg_target);
    if (events && *events) return (*events)->check_keycodes(std::move(arg_keycodes), false);
    return std::nullopt;
}

std::vector<::cw::lib::String> Event::_keycodes_for_successevent(std::vector<::cw::lib::String> arg_keycodes, bool arg_successflag)
{
    for (auto& keycode : arg_keycodes) keycode += arg_successflag ? "○" : "×";
    return arg_keycodes;
}

std::optional<std::shared_ptr<::cw::event::Event>> Event::ignition_successevent(std::shared_ptr<::cw::character::Character> arg_target, bool arg_successflag, std::vector<::cw::lib::String> arg_keycodes)
{
    auto card = std::dynamic_pointer_cast<CWPyCard>(arg_target); auto events = get_events(card);
    if (events && *events) return (*events)->check_keycodes(_keycodes_for_successevent(std::move(arg_keycodes), arg_successflag), true);
    return std::nullopt;
}

void Event::run_scenarioevent(bool arg_isinsideevent)
{
    auto iface = event_interface(); if (!iface) return;
    auto running = iface->get_event(); if (!running || !*running) return;
    auto outer = *running;
    std::shared_ptr<Event> temporary_base;
    const auto original_base = outer->m_base;
    if (outer->m_base && *outer->m_base) {
        temporary_base = std::make_shared<Event>(); temporary_base->initialize(std::nullopt); temporary_base->copy_from_impl(outer);
        outer->copy_from_impl(*outer->m_base); outer->m_base.reset();
    }
    const auto data = scenario_data();
    const auto area_index = static_cast<std::size_t>(::cw::g_HINT_AREA);
    const auto versionhint = data && area_index < data->m_versionhint.size()
        ? data->m_versionhint[area_index] : std::nullopt;
    if (!outer->m_cur_content) return;
    const auto saved_content = *outer->m_cur_content;
    const auto saved_line_index = outer->m_line_index;
    outer->m_nowrunningcontents.emplace_back(shared_from_this(), saved_content, saved_line_index, versionhint);
    iface->append_event(shared_from_this());
    if (outer.get() != this) m_parent = outer;
    iface->append_stackinfo(stack_trace_frame(shared_from_this(), saved_content, saved_line_index));
    outer->m_cur_content = m_starttree; outer->m_line_index = 0;
    if (::cw::g_cwpy && ::cw::g_cwpy->is_playingscenario())
        if (const auto data = scenario_data()) data->set_versionhint(::cw::g_HINT_AREA, versionhint);
    outer->store_inusedata(true);
    try { outer->run(false, arg_isinsideevent, false); }
    catch (...) {
        outer->restore_inusedata();
        if (temporary_base) { outer->copy_from_impl(temporary_base); outer->m_base = original_base; }
        throw;
    }
    outer->restore_inusedata();
    if (temporary_base) { outer->copy_from_impl(temporary_base); outer->m_base = original_base; }
}

void Event::clear()
{
    m_index = 0; m_cur_content = m_starttree; m_line_index = 0; m_nowrunningcontents.clear();
}

void Event::action()
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->is_running()) throw ::cw::thread::CWPyRunningError{};
    auto iface = event_interface(); if (!iface || iface->is_stoped()) throw EffectBreakError{};
    if (m_skip_action) { m_skip_action = false; return; }
    if (!m_cur_content) { m_index = 0; return; }
    auto content = *m_cur_content;
    if (content && content->m_tag == "ContentsLine" && m_line_index >= 0 &&
        m_line_index < checked_size_to_integer(content->m_children.size(), "ContentsLine child count"))
        content = content->m_children[static_cast<std::size_t>(m_line_index)];
    if (auto handler = content ? ::cw::content::get_content(content) : nullptr) {
        try { m_index = handler->action_with_log(); }
        catch (...) { iface->m_is_changestate |= value_bool(handler->m_is_changestate); throw; }
        iface->m_is_changestate |= value_bool(handler->m_is_changestate);
    } else m_index = 0;
    if (content && (content->m_tag == "Effect" || (content->m_tag == "Set" && content->get("type", {}) == "Coupon") || (content->m_tag == "Elapse" && content->get("type", {}) == "Time"))) check_gameover();
    if (iface->is_stoped() || (scenario_data() && scenario_data()->m_in_f9)) throw EffectBreakError{};
}

std::optional<std::vector<std::shared_ptr<::cw::data::CWPyElement>>> Event::get_nextcontents()
{
    auto forced = event_element(m_force_nextcontent);
    if (forced) { m_force_nextcontent = Value{}; m_line_index = value_integer(m_force_nextcontent_index, 0); m_force_nextcontent_index = Value(-1); return std::vector<std::shared_ptr<CWPyElement>>{forced}; }
    if (!m_cur_content) { m_line_index = 0; return std::nullopt; }
    const auto root = *m_cur_content;
    const bool isline = root->m_tag == "ContentsLine";
    const auto line_child = [&root](Integer position) {
        const auto count = checked_size_to_integer(root->m_children.size(), "ContentsLine child count");
        if (position < -count || position >= count)
            throw std::out_of_range("ContentsLine index out of range");
        const auto index = position < 0 ? count + position : position;
        return root->m_children[static_cast<std::size_t>(index)];
    };
    const auto first_content = [](const std::shared_ptr<CWPyElement>& element) {
        if (element && element->m_tag == "ContentsLine") {
            if (element->m_children.empty()) throw std::out_of_range("ContentsLine index out of range");
            return element->m_children.front();
        }
        return element;
    };
    std::shared_ptr<CWPyElement> current;
    if (isline) {
        // Python と同様、元のカーソルを進める前に行の添字を解決
        // する。 有効な負の位置は末尾から選び、無効なら状態をリセット
        // ・前進させずに例外を出す。行のラッパーを代わりに実行しない。
        current = line_child(m_line_index);
        ++m_line_index;
    } else { current = root; m_line_index = 0; }
    if (!current) { m_line_index = 0; return std::nullopt; }
    auto executable = [&first_content](const std::shared_ptr<CWPyElement>& element) {
        const auto check = first_content(element);
        if (!check) return false;
        const bool is_check = check->m_tag == "Check" ||
            (check->m_tag == "XmlContent" && value_bool(::cw::content::is_xmltype_check(element_value(check))));
        if (!is_check) return true;
        const auto handler = ::cw::content::get_content(check);
        return handler && handler->action() == 0;
    };

    if (!current->m_nextelements) {
        std::shared_ptr<CWPyElement> contents;
        if (!(isline && m_line_index < checked_size_to_integer(root->m_children.size(), "ContentsLine child count"))) contents = child_named(current, "Contents");
        current->m_nextelements = std::vector<std::shared_ptr<CWPyElement>>{};
        current->m_needcheck = false;
        if (contents && !contents->m_children.empty()) {
            std::vector<std::shared_ptr<CWPyElement>> result;
            for (const auto& element : contents->m_children) {
                const auto check = first_content(element);
                const bool needs_check = check && (check->m_tag == "Check" ||
                    (check->m_tag == "XmlContent" && value_bool(::cw::content::is_xmltype_check(element_value(check)))));
                current->m_needcheck = current->m_needcheck.value_or(false) || needs_check;
                if (executable(element)) result.push_back(element);
                current->m_nextelements->push_back(element);
            }
            m_line_index = 0; return result;
        }
        if (isline && m_line_index < checked_size_to_integer(root->m_children.size(), "ContentsLine child count")) {
            const auto next = line_child(m_line_index);
            current->m_nextelements->push_back(next);
            const auto check = first_content(next);
            current->m_needcheck = check && (check->m_tag == "Check" ||
                (check->m_tag == "XmlContent" && value_bool(::cw::content::is_xmltype_check(element_value(check)))));
            if (executable(next)) return std::vector<std::shared_ptr<CWPyElement>>{root};
            // Python と同様、最初の未キャッシュの Check 失敗ではこの位
            // 置を維持する。 下のキャッシュ済み再評価の分岐では別途リセットする。
            return std::nullopt;
        }
        m_line_index = 0; return std::nullopt;
    }
    if (current->m_needcheck.value_or(false)) {
        if (isline && m_line_index < checked_size_to_integer(root->m_children.size(), "ContentsLine child count")) {
            if (executable(line_child(m_line_index))) return std::vector<std::shared_ptr<CWPyElement>>{root};
            m_line_index = 0; return std::nullopt;
        }
        std::vector<std::shared_ptr<CWPyElement>> result;
        for (const auto& element : *current->m_nextelements) if (executable(element)) result.push_back(element);
        m_line_index = 0; return result;
    }
    if (isline && m_line_index < checked_size_to_integer(root->m_children.size(), "ContentsLine child count"))
        return std::vector<std::shared_ptr<CWPyElement>>{root};
    m_line_index = 0; return *current->m_nextelements;
}

void Event::check_gameover()
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->is_playingscenario()) return;
    const auto data = scenario_data(); if (!data || !data->can_gameover()) return;
    const auto pcards = ::cw::g_cwpy->get_pcards(Value("")); bool all_disabled = true;
    for (const auto& card : pcards) if (card && !card->is_paralyze() && !card->is_unconscious()) { all_disabled = false; break; }
    if (all_disabled) ::cw::g_cwpy->set_gameoverstatus(true, false);
}

void EventEngine::initialize(std::vector<std::shared_ptr<::cw::data::CWPyElement>> arg_data, ::cw::lib::String arg_role)
{
    m_role = std::move(arg_role); m__engine_role = Value(m_role); m_event_list.clear();
    m_synthetic_events_root.reset();
    for (const auto& element : arg_data) {
        if (element && element->m_parent.expired()) {
            if (!m_synthetic_events_root) m_synthetic_events_root = ::cw::data::make_element("Events");
            m_synthetic_events_root->append(element);
        }
        auto event = std::make_shared<Event>(); event->initialize(element); m_event_list.push_back(event);
    }
    auto list = std::make_shared<::cw::data::VariantSequence>(); for (const auto& event : m_event_list) list->m_values.emplace_back(std::static_pointer_cast<::cw::lib::Object>(event)); m_events = Value(list);
}

bool EventEngine::start(std::optional<::cw::lib::Integer> arg_keynum, std::optional<std::vector<::cw::lib::String>> arg_keycodes, bool arg_isinsideevent, bool arg_successevent)
{
#if defined(CARDWIRTH_LOG_ENABLED)
    ::cardwirth::debug::log(
        "EventEngine start begin: role=" + m_role +
        " key=" + (arg_keynum ? std::to_string(*arg_keynum) : ::cw::lib::String("none")) +
        " keycodes=" + (arg_keycodes ? std::to_string(arg_keycodes->size()) : ::cw::lib::String("none")) +
        " inside=" + (arg_isinsideevent ? "true" : "false") +
        " success=" + (arg_successevent ? "true" : "false") +
        " events=" + std::to_string(m_event_list.size()));
#endif
    auto event = arg_keynum ? check_keynum(*arg_keynum) : (arg_keycodes ? check_keycodes(*arg_keycodes, arg_successevent) : std::nullopt);
    if (!event || !*event) {
#if defined(CARDWIRTH_LOG_ENABLED)
        ::cardwirth::debug::log("EventEngine start: no matching event");
#endif
        return false;
    }
#if defined(CARDWIRTH_LOG_ENABLED)
    std::size_t event_index = 0;
    while (event_index < m_event_list.size() && m_event_list[event_index] != *event) ++event_index;
    ::cardwirth::debug::log(
        "EventEngine start: matched index=" + std::to_string(event_index) +
        " keynums=" + std::to_string((*event)->m_keynums.size()) +
        " keycodes=" + std::to_string((*event)->m_keycodes.size()) +
        " has_starttree=" + ((*event)->m_starttree && *(*event)->m_starttree ? "true" : "false"));
#endif
    (*event)->clear();
    arg_isinsideevent = arg_isinsideevent || (event_interface() && event_interface()->get_event());
    std::shared_ptr<::cw::sprite::card::MenuCard> last_selected;
    if (!arg_isinsideevent && ::cw::g_cwpy) {
        if (::cw::g_cwpy->m_index >= 0 && static_cast<std::size_t>(::cw::g_cwpy->m_index) < ::cw::g_cwpy->m_list.size())
            last_selected = std::dynamic_pointer_cast<::cw::sprite::card::MenuCard>(::cw::g_cwpy->m_list[static_cast<std::size_t>(::cw::g_cwpy->m_index)]);
        if (::cw::g_cwpy->m_selection && std::dynamic_pointer_cast<::cw::sprite::card::MenuCard>(*::cw::g_cwpy->m_selection)) ::cw::g_cwpy->clear_selection();
    }
    // Python と同様、実行中の別イベントから起動した場合も Event.run()
    // は既定の isinside=False で呼ぶ。 Event::run()
    // は選択イベントを仮想イベントスタックへ追加する必要がある。 ここで true
    // を渡すと追加を省略し、run_exit() が親イベントを取り出してしまう。
    if (arg_isinsideevent) (*event)->run(false, false, false); else (*event)->start(false);
#if defined(CARDWIRTH_LOG_ENABLED)
    ::cardwirth::debug::log(
        "EventEngine start end: role=" + m_role +
        " matched_index=" + std::to_string(event_index));
#endif
    if (!arg_isinsideevent && ::cw::g_cwpy) {
        ::cw::g_cwpy->m_list.clear();
        for (const auto& value : ::cw::g_cwpy->get_mcards(Value("selectable"), {})) if (auto card = card_value(value)) ::cw::g_cwpy->m_list.push_back(card);
        ::cw::g_cwpy->m_index = -1;
        if (last_selected) for (std::size_t i = 0; i < ::cw::g_cwpy->m_list.size(); ++i) if (::cw::g_cwpy->m_list[i] == last_selected) { ::cw::g_cwpy->m_index = checked_size_to_integer(i, "menu selection index"); break; }
    }
    return true;
}

std::optional<std::shared_ptr<::cw::event::Event>> EventEngine::check_keycodes(std::vector<::cw::lib::String> arg_keycodes, bool arg_successevent)
{
    std::set<String> keyset(arg_keycodes.begin(), arg_keycodes.end()); keyset.erase("");
    for (const auto& event : m_event_list) {
        if (!event) continue;
        auto keys = event->m_keycodes; keys.erase(std::remove_if(keys.begin(), keys.end(), [](const String& s){ return s.empty(); }), keys.end());
        String matching = event->m_keycode_matching;
        const auto data = scenario_data();
        if (matching == "And" && data && ::cw::g_cwpy && ::cw::g_cwpy->m_sct && ::cw::g_cwpy->m_sct->lessthan("1.30", data->get_versionhint(::cw::g_HINT_AREA))) {
            keys.insert(keys.begin(), "MatchingType=All"); matching = "Or";
        }
        if (keys.empty()) continue;
        std::vector<String> special_keycodes;
        for (const auto& key : keys)
            if (key.rfind("＠", 0) == 0 || key.rfind("@", 0) == 0) special_keycodes.push_back(key);
        if (!special_keycodes.empty()) {
            bool special_ok = false;
            for (const auto& key : special_keycodes) if (keyset.count(key) != 0) { special_ok = true; break; }
            if (!special_ok) continue;
        }
        if (matching == "And") {
            if (std::all_of(keys.begin(), keys.end(), [&](const String& key){ return check_keycode(key, keyset, arg_successevent); })) return event;
        } else if (std::any_of(keys.begin(), keys.end(), [&](const String& key){ return check_keycode(key, keyset, arg_successevent); })) return event;
    }
    return std::nullopt;
}

bool EventEngine::check_keycode(::cw::lib::String arg_keycode, std::set<::cw::lib::String> arg_keycodes, bool arg_successevent)
{
    const auto data = scenario_data();
    const bool numeric = data && data->m_summary && *data->m_summary &&
        (*data->m_summary)->getbool("Property/Settings", "numericKeycodes", false);
    const auto contains = [&](const String& required) {
        return std::any_of(arg_keycodes.begin(), arg_keycodes.end(), [&](const String& supplied) {
            return ::cw::keycode_level::matches(required, supplied, numeric);
        });
    };
    if (data && ::cw::g_cwpy && ::cw::g_cwpy->m_sct && ::cw::g_cwpy->m_sct->lessthan("1.30", data->get_versionhint(::cw::g_HINT_AREA))) return contains(arg_keycode);
    static const String negation_prefix = "！";
    if (arg_keycode.rfind(negation_prefix, 0) == 0) {
        return !arg_successevent &&
               !contains(arg_keycode.substr(negation_prefix.size()));
    }
    return contains(arg_keycode);
}

std::optional<std::shared_ptr<::cw::event::Event>> EventEngine::check_keynum(::cw::lib::Integer arg_keynum)
{
    for (const auto& event : m_event_list) if (event && (std::find(event->m_keynums.begin(), event->m_keynums.end(), arg_keynum) != event->m_keynums.end() || (arg_keynum < 0 && std::find(event->m_keynums.begin(), event->m_keynums.end(), 4) != event->m_keynums.end()))) return event;
    return std::nullopt;
}

void EventInterface::initialize(::cw::lib::String arg_role)
{
    m__selectedmember.reset(); m__inusecard.reset(); m__selectedcard.reset(); m__nowrunningevents.clear(); m_nowrunningpacks.clear();
    m_stackinfo.assign(16, std::nullopt); m_stackinfo_len = 0; m_paused = false; m_stoped = false; m__step = false;
    m__targetstack = Value(-2); m_breakwait = false; m_waittime = 0; m_eventtimer = 1; m_effectevent.reset();
    m_in_cardeffectmotion = false; m_in_inusecardevent = false; m_exit_func.reset(); m_is_changestate = false; m__eventinterfalerole = Value(std::move(arg_role));
}

::cw::lib::String EventInterface::get_selectedmembername()
{
    return m__selectedmember && *m__selectedmember ? (*m__selectedmember)->m_name : "選択メンバ未定";
}

::cw::lib::String EventInterface::get_selectedcardname()
{
    return m__selectedcard && *m__selectedcard ? (*m__selectedcard)->m_name : "選択カード未定";
}

std::optional<::cw::lib::String> EventInterface::get_selectedcardtype()
{
    if (!m__selectedcard || !*m__selectedcard) return std::nullopt; return (*m__selectedcard)->m_type;
}

std::shared_ptr<::cw::event::Event> EventInterface::pop_event()
{
    if (m__nowrunningevents.empty()) throw std::out_of_range("event: pop from empty event stack"); auto result = m__nowrunningevents.back(); m__nowrunningevents.pop_back(); return result;
}

void EventInterface::remove_event(std::shared_ptr<::cw::event::Event> arg_event)
{
    const auto found = std::find(m__nowrunningevents.begin(), m__nowrunningevents.end(), arg_event);
    if (found == m__nowrunningevents.end()) throw std::out_of_range("event: event is not in the running stack");
    m__nowrunningevents.erase(found);
}

void EventInterface::append_event(std::shared_ptr<::cw::event::Event> arg_event)
{
    if (!arg_event) throw std::invalid_argument("event: null event cannot be appended");
    if (::cw::g_LIMIT_RECURSE <= get_currentstack()) {
        if (::cw::g_cwpy) ::cw::g_cwpy->call_modaldlg("ERROR", {{"text", Value("イベントの呼び出しが" + std::to_string(::cw::g_LIMIT_RECURSE) + "層を超えたので処理を中止します。スタートやパッケージのコールによってイベントが無限ループになっていないか確認してください。")}});
        throw EffectBreakError{};
    }
    m__nowrunningevents.push_back(std::move(arg_event));
}

void EventInterface::replace_event(std::shared_ptr<::cw::event::Event> arg_event, std::optional<std::tuple<::cw::lib::Integer, std::optional<std::tuple<::cw::lib::String, ::cw::lib::String, bool, bool, bool>>>> arg_versionhint_base)
{
    if (m__nowrunningevents.empty()) throw std::out_of_range("event: replace on empty event stack");
    if (!arg_event) throw std::invalid_argument("event: replacement event is null");
    m__nowrunningevents.back()->copy_from(std::move(arg_event), std::move(arg_versionhint_base));
}

void EventInterface::clear_events()
{
    m__nowrunningevents.clear(); clear_stackinfo();
}

std::optional<std::shared_ptr<::cw::event::Event>> EventInterface::get_event()
{
    return m__nowrunningevents.empty() ? std::nullopt : std::optional<std::shared_ptr<Event>>(m__nowrunningevents.front());
}

std::optional<std::shared_ptr<::cw::event::Targeting>> EventInterface::get_effectevent()
{
    return m_effectevent;
}

std::vector<std::shared_ptr<::cw::event::Event>> EventInterface::get_events()
{
    return m__nowrunningevents;
}

::cw::lib::Integer EventInterface::get_currentstack()
{
    if (m__nowrunningevents.empty()) return 0;
    const auto event_count = checked_size_to_integer(m__nowrunningevents.size() - 1, "event stack count");
    const auto content_count = checked_size_to_integer(m__nowrunningevents.front()->m_nowrunningcontents.size(), "running content count");
    if (content_count > std::numeric_limits<Integer>::max() - event_count)
        throw std::overflow_error("event: current stack exceeds Integer range");
    return event_count + content_count;
}

std::optional<std::map<::cw::lib::String, std::shared_ptr<::cw::data::CWPyElement>>> EventInterface::get_trees()
{
    if (m__nowrunningevents.empty()) return std::nullopt; return m__nowrunningevents.back()->m_trees;
}

std::vector<::cw::lib::String> EventInterface::get_treekeys()
{
    return m__nowrunningevents.empty() ? std::vector<String>{} : m__nowrunningevents.back()->m_treekeys;
}

std::optional<std::shared_ptr<::cw::event::Event>> EventInterface::get_nowrunningevent()
{
    return m__nowrunningevents.empty() ? std::nullopt : std::optional<std::shared_ptr<Event>>(m__nowrunningevents.back());
}

::cw::lib::Integer EventInterface::get_packageid()
{
    return m__nowrunningevents.empty() ? 0 : m__nowrunningevents.back()->m_packageid;
}

void EventInterface::clear()
{
    set_inusecard(std::nullopt); m_in_inusecardevent = false; m_effectevent.reset(); clear_events(); m_nowrunningpacks.clear();
    m_stoped = false; m_breakwait = false; m__targetstack = Value(-2); m_exit_func.reset(); refresh_tools(); refresh_activeitem();
}

void EventInterface::set_inusecard(std::optional<std::shared_ptr<::cw::header::CardHeader>> arg_header)
{
    m__inusecard = arg_header; set_selectedcard(arg_header);
}

void EventInterface::set_selectedmember(std::optional<std::shared_ptr<::cw::character::Character>> arg_ccard)
{
    if (m__selectedmember != arg_ccard) { m__selectedmember = arg_ccard; refresh_selectedmembername(); }
}

void EventInterface::set_selectedcard(std::optional<std::shared_ptr<::cw::header::CardHeader>> arg_header)
{
    if (m__selectedcard != arg_header) { m__selectedcard = arg_header; refresh_selectedcardname(); }
}

std::vector<::cw::lib::Value> EventInterface::get_targetscope(::cw::lib::String arg_scope, bool arg_unreversed, bool arg_cards, ::cw::lib::String arg_coupon)
{
    (void)arg_cards; const Value mode(arg_unreversed ? "unreversed" : ""); std::vector<Value> result;
    if (!::cw::g_cwpy) return result;
    if (arg_scope == "Selected") { auto value = get_selectedmember(); if (value && *value) result.emplace_back(std::static_pointer_cast<::cw::lib::Object>(*value)); }
    else if (arg_scope == "Random") { auto value = get_randommember(); if (value && *value) result.emplace_back(std::static_pointer_cast<::cw::lib::Object>(*value)); }
    else if (arg_scope == "Party") for (auto& card : ::cw::g_cwpy->get_pcards(mode)) result.emplace_back(card_object(card));
    else if (arg_scope == "Enemy") for (auto& card : ::cw::g_cwpy->get_ecards(mode)) result.emplace_back(card_object(card));
    else if (arg_scope == "Npc") for (auto& card : ::cw::g_cwpy->get_fcards(mode)) result.emplace_back(card_object(card));
    else if (arg_scope == "FieldCasts") { for (auto& card : ::cw::g_cwpy->get_pcards(mode)) result.emplace_back(card_object(card)); for (auto& card : ::cw::g_cwpy->get_ecards(mode)) result.emplace_back(card_object(card)); }
    else if (arg_scope == "CouponHolder") { if (!arg_coupon.empty()) { for (auto& card : ::cw::g_cwpy->get_pcards(Value("unreversed"))) if (card && card->has_coupon(arg_coupon)) result.emplace_back(card_object(card)); for (auto& card : ::cw::g_cwpy->get_ecards(Value("unreversed"))) if (card && card->has_coupon(arg_coupon)) result.emplace_back(card_object(card)); } }
    else if (arg_scope == "Backpack" || arg_scope == "PartyAndBackpack" || arg_scope == "Field") {
        if (arg_scope != "Backpack") for (auto& card : ::cw::g_cwpy->get_pcards(mode)) result.emplace_back(card_object(card));
        result.emplace_back(backpack_value());
        if (arg_scope == "Field") for (auto& card : ::cw::g_cwpy->get_ecards(mode)) result.emplace_back(card_object(card));
    } else throw std::invalid_argument(arg_scope + " is invalid value.");
    return result;
}

::cw::lib::Value EventInterface::get_targetmember(::cw::lib::String arg_targetm, bool arg_unreversed, ::cw::lib::String arg_coupon)
{
    if (arg_targetm == "Random") { auto v = get_randommember(); return v && *v ? Value(std::static_pointer_cast<::cw::lib::Object>(*v)) : Value{}; }
    if (arg_targetm == "Selected") { auto v = get_selectedmember(); return v && *v ? Value(std::static_pointer_cast<::cw::lib::Object>(*v)) : Value{}; }
    if (arg_targetm == "Unselected") { auto v = get_unselectedmember(); return v && *v ? Value(std::static_pointer_cast<::cw::lib::Object>(*v)) : Value{}; }
    if (arg_targetm == "Selectedcard") return m__selectedcard && *m__selectedcard ? Value(std::static_pointer_cast<::cw::lib::Object>(*m__selectedcard)) : Value{};
    if (arg_targetm == "First") { auto v = get_firstmember(Value(arg_unreversed ? "unreversed" : "")); return v && *v ? Value(std::static_pointer_cast<::cw::lib::Object>(*v)) : Value{}; }
    if (arg_targetm == "Party") { auto list = std::make_shared<::cw::data::VariantSequence>(); if (::cw::g_cwpy) for (auto& c : ::cw::g_cwpy->get_pcards(Value(arg_unreversed ? "unreversed" : ""))) list->m_values.emplace_back(card_object(c)); return Value(list); }
    if (arg_targetm == "CouponHolder") { auto list = std::make_shared<::cw::data::VariantSequence>(); if (::cw::g_cwpy && !arg_coupon.empty()) { for (auto& c : ::cw::g_cwpy->get_pcards(Value("unreversed"))) if (c && c->has_coupon(arg_coupon)) list->m_values.emplace_back(card_object(c)); for (auto& c : ::cw::g_cwpy->get_ecards(Value("unreversed"))) if (c && c->has_coupon(arg_coupon)) list->m_values.emplace_back(card_object(c)); } return Value(list); }
    throw std::invalid_argument(arg_targetm + " is invalid value.");
}

std::optional<std::shared_ptr<::cw::character::Character>> EventInterface::get_randommember()
{
    if (!::cw::g_cwpy) return std::nullopt; auto cards = ::cw::g_cwpy->get_pcards(Value("active")); if (cards.empty()) cards = ::cw::g_cwpy->get_pcards(Value("unreversed")); if (cards.empty()) return std::nullopt;
    if (::cw::g_cwpy->m_dice) { std::vector<Value> values; for (const auto& card : cards) values.push_back(card_object(card)); const auto selected = ::cw::g_cwpy->m_dice->choice(std::move(values)); if (selected) if (auto character = character_value(*selected)) return character; }
    return cards.front();
}

bool EventInterface::has_selectedmember()
{
    return m__selectedmember && *m__selectedmember && !(*m__selectedmember)->is_vanished();
}

std::optional<std::shared_ptr<::cw::character::Character>> EventInterface::get_selectedmember()
{
    if (!has_selectedmember()) set_selectedmember(get_randommember()); return m__selectedmember;
}

void EventInterface::clear_selectedmember()
{
    if (m__selectedmember) { m__selectedmember.reset(); refresh_selectedmembername(); }
}

std::optional<std::shared_ptr<::cw::character::Character>> EventInterface::get_unselectedmember()
{
    if (!has_selectedmember()) return get_randommember(); auto selected = get_selectedmember(); if (!::cw::g_cwpy) return std::nullopt; auto cards = ::cw::g_cwpy->get_pcards(Value("active")); cards.erase(std::remove_if(cards.begin(), cards.end(), [&](const auto& c){ return selected && *selected == c; }), cards.end()); if (cards.empty()) { cards = ::cw::g_cwpy->get_pcards(Value("unreversed")); cards.erase(std::remove_if(cards.begin(), cards.end(), [&](const auto& c){ return selected && *selected == c; }), cards.end()); } if (cards.empty()) return std::nullopt;
    if (::cw::g_cwpy->m_dice) { std::vector<Value> values; for (const auto& card : cards) values.push_back(card_object(card)); const auto choice = ::cw::g_cwpy->m_dice->choice(std::move(values)); if (choice) if (auto character = character_value(*choice)) return character; }
    return cards.front();
}

std::optional<std::shared_ptr<::cw::character::Character>> EventInterface::get_firstmember(::cw::lib::Value arg_mode)
{
    if (!::cw::g_cwpy) return std::nullopt; auto cards = ::cw::g_cwpy->get_pcards(arg_mode); return cards.empty() ? std::nullopt : std::optional<std::shared_ptr<Character>>(cards.front());
}

std::optional<std::shared_ptr<::cw::header::CardHeader>> EventInterface::get_inusecard()
{
    return m__inusecard;
}

std::optional<std::shared_ptr<::cw::header::CardHeader>> EventInterface::get_selectedcard()
{
    return m__selectedcard;
}

void EventInterface::clear_selectedcard()
{
    if (m__selectedcard != m__inusecard) { m__selectedcard = m__inusecard; refresh_selectedcardname(); }
}

void EventInterface::refresh_tools()
{
    if (!::cw::g_cwpy) return;
    if (auto debugger = ::cw::g_cwpy->is_showingdebugger(); debugger && *debugger) (*debugger)->refresh_tools();
}

void EventInterface::refresh_showpartytools()
{
    if (!::cw::g_cwpy) return;
    if (auto debugger = ::cw::g_cwpy->is_showingdebugger(); debugger && *debugger) (*debugger)->refresh_showpartytools();
}

void EventInterface::refresh_variablelist()
{
    if (!::cw::g_cwpy) return;
    if (auto debugger = ::cw::g_cwpy->is_showingdebugger(); debugger && *debugger && (*debugger)->m_view_var)
        (*debugger)->m_view_var->refresh_variablelist(std::nullopt);
}

void EventInterface::refresh_variable(::cw::lib::Value arg_variable)
{
    if (!::cw::g_cwpy) return;
    if (auto debugger = ::cw::g_cwpy->is_showingdebugger(); debugger && *debugger && (*debugger)->m_view_var)
        (*debugger)->m_view_var->refresh_variable(std::move(arg_variable));
}

void EventInterface::refresh_selectedmembername()
{
    if (!::cw::g_cwpy) return;
    if (auto debugger = ::cw::g_cwpy->is_showingdebugger(); debugger && *debugger) (*debugger)->refresh_selectedmembername();
}

void EventInterface::refresh_selectedcardname()
{
    if (!::cw::g_cwpy) return;
    if (auto debugger = ::cw::g_cwpy->is_showingdebugger(); debugger && *debugger) (*debugger)->refresh_selectedcardname();
}

void EventInterface::refresh_areaname()
{
    if (!::cw::g_cwpy) return;
    if (auto debugger = ::cw::g_cwpy->is_showingdebugger(); debugger && *debugger) (*debugger)->refresh_areaname();
}

void EventInterface::refresh_activeitem()
{
    if (!::cw::g_cwpy) return;
    if (auto debugger = ::cw::g_cwpy->is_showingdebugger(); debugger && *debugger && (*debugger)->m_view_tree) {
        (*debugger)->m_view_tree->refresh_tree();
        (*debugger)->m_view_tree->refresh_activeitem();
    }
}

void EventInterface::append_stackinfo(::cw::lib::Value arg_item)
{
    if (m_stackinfo_len < 0) m_stackinfo_len = 0;
    if (m_stackinfo_len >= checked_size_to_integer(m_stackinfo.size(), "stackinfo count")) {
        const auto new_size = m_stackinfo.size() > (std::numeric_limits<std::size_t>::max() - 1) / 2
            ? std::numeric_limits<std::size_t>::max() : m_stackinfo.size() * 2 + 1;
        if (new_size <= m_stackinfo.size()) throw std::overflow_error("event: stackinfo exceeds size range");
        m_stackinfo.resize(new_size);
    }
    m_stackinfo[static_cast<std::size_t>(m_stackinfo_len++)] = arg_item;
    if (::cw::g_cwpy) if (auto debugger = ::cw::g_cwpy->is_showingdebugger(); debugger && *debugger)
        (*debugger)->append_stackinfo_cwpy(arg_item);
}

void EventInterface::pop_stackinfo()
{
    if (m_stackinfo_len <= 0) throw std::out_of_range("event: pop from empty stack trace");
    --m_stackinfo_len; m_stackinfo[static_cast<std::size_t>(m_stackinfo_len)].reset();
    if (::cw::g_cwpy) if (auto debugger = ::cw::g_cwpy->is_showingdebugger(); debugger && *debugger)
        (*debugger)->pop_stackinfo_cwpy();
}

void EventInterface::replace_stackinfo(::cw::lib::Integer arg_index, ::cw::lib::Value arg_item)
{
    const Integer index = m_stackinfo_len + arg_index;
    if (index < 0 || static_cast<std::size_t>(index) >= m_stackinfo.size() || !m_stackinfo[static_cast<std::size_t>(index)]) throw std::out_of_range("event: invalid stack trace replacement");
    m_stackinfo[static_cast<std::size_t>(index)] = arg_item;
    if (::cw::g_cwpy) if (auto debugger = ::cw::g_cwpy->is_showingdebugger(); debugger && *debugger)
        (*debugger)->replace_stackinfo_at_cwpy(index, arg_item);
}

void EventInterface::clear_stackinfo()
{
    m_stackinfo.assign(16, std::nullopt); m_stackinfo_len = 0;
    if (::cw::g_cwpy) if (auto debugger = ::cw::g_cwpy->is_showingdebugger(); debugger && *debugger)
        (*debugger)->clear_stackinfo_cwpy();
}

void EventInterface::wait()
{
    auto event = get_event(); if (!event || !*event) return;
    auto content = (*event)->m_cur_content.value_or(nullptr);
    if (content && content->m_tag == "ContentsLine") {
        const auto count = checked_size_to_integer(content->m_children.size(), "ContentsLine child count");
        auto index = (*event)->m_line_index;
        if (index < 0) index += count; // Python のリスト添字と同様、-1 を受け付ける。
        if (index < 0 || index >= count) throw std::out_of_range("Event wait content line index");
        content = content->m_children[static_cast<std::size_t>(index)];
    }
    if (!content) throw std::logic_error("Event wait requires current content");
    const auto can_debugger_wait = [] {
        if (!::cw::g_cwpy || ::cw::g_cwpy->m_areaid < 0) return false;
        const auto debugger = ::cw::g_cwpy->is_showingdebugger();
        const auto options = object_value<::cw::argparser::ArgResult>(::cw::g_OPTIONS);
        return debugger && *debugger && (::cw::g_cwpy->is_playingscenario() ||
            (options && options->getbool("debug_skin")));
    };
    if (!m_paused && content && can_debugger_wait()) {
        // スキン・宿イベントも SystemData を使う。 検索先を ScenarioData
        // に制限すると、元の debug_skin のブレークポイント経路が失われる。
        if (const auto data = object_value<::cw::data::SystemData>(::cw::g_cwpy->m_sdata)) {
            const String path = content->get_cwxpath();
            if (data->m_breakpoints.count(path)) {
                m_paused = true;
                const auto frame = object_value<::cw::frame::Frame>(::cw::g_cwpy->m_frame);
                if (!frame) throw std::logic_error("Debugger breakpoint requires Frame");
                // Py と同様、debugger.pause は UI スレッド
                // へ予約し、 ここで直接呼ばず、実行時にデバッガーを再確認する。
                const std::weak_ptr<::cw::thread::CWPy> owner = ::cw::g_cwpy;
                frame->exec_func([owner](const ::cw::lib::Arguments&, const ::cw::lib::KeywordArguments&) {
                    if (const auto runtime = owner.lock())
                        if (auto debugger = runtime->is_showingdebugger(); debugger && *debugger)
                            (*debugger)->pause(true);
                    return Value{};
                });
            }
        }
    }
    if (m_stoped) throw EffectBreakError{};
    // Python と同様、待機をメッセージ表示へ委ねる前に Talk のブレ
    // ークポイントを登録する。 この確認より前に戻ると、停止要求が失われる。
    if (content && content->m_tag == "Talk") return;
    if (::cw::g_cwpy) {
        const auto dispatch_wait_input = [] {
            // Python の exec_func は USEREVENT
            // としてここのハンドラーで処理される。 C++ では別の
            // ネイティブキューに保持するため、通常のメインループが待機
            // 中でも、 その共通の公開経路を使って入力順を維持する。
            ::cw::g_cwpy->stage_game_commands();
            ::cw::g_cwpy->input(false, false, false);
            if (auto handler = ::cw::g_cwpy->get_eventhandler()) handler->run();
        };
        // pygame.peek((USEREVENT, FORCE_USEREVENT)) と同様
        // 、これらは直ちに確認する。 通常の入力はコンテント待機 1000 回ごとにだけ確認する。
        if (::cw::thread::peek_pending_events(*::cw::g_cwpy, true) ||
            (m_eventtimer % 1000 == 0 && ::cw::thread::peek_pending_events(*::cw::g_cwpy))) {
            ::cw::g_cwpy->update_groups(
                {object_value<::cw::lib::Object>(::cw::g_cwpy->m_sbargrp)});
            dispatch_wait_input();
            m_eventtimer = 1;
        } else ++m_eventtimer;
        if (m_stoped) throw EffectBreakError{};
        const bool debugger_wait = can_debugger_wait();
        if (debugger_wait && m__step) m_paused = true;
        if (debugger_wait && m_waittime != 0) {
            auto timer = std::make_shared<::cw::sprite::base::StopTheWorld>();
            // Python と同様、ミリ秒単位で待機し、毎フレーム、デバッガー操作
            // を確認する。 C++ で表現できない持続時間の境界だけを飽和させる。
            const auto duration = m_waittime > std::numeric_limits<Integer>::max() / 100
                ? std::numeric_limits<Integer>::max()
                : m_waittime < std::numeric_limits<Integer>::min() / 100
                    ? std::numeric_limits<Integer>::min() : m_waittime * 100;
            timer->initialize(::cw::sprite::base::animation_ticks(), duration);
            bool first = true;
            while (::cw::g_cwpy->is_running() && ::cw::g_cwpy->is_showingdebugger() && *::cw::g_cwpy->is_showingdebugger() &&
                   timer->is_waiting() && !m_stoped) {
                const auto current = get_event();
                if (!current || !*current || !(*current)->m_force_nextcontent.is_none()) break;
                if (first) { refresh_tools(); refresh_activeitem(); first = false; }
                ::cw::g_cwpy->update_groups({object_value<::cw::lib::Object>(::cw::g_cwpy->m_sbargrp)});
                dispatch_wait_input();
                ::cw::g_cwpy->wait_frame(1, false, timer, 0);
            }
        }
        bool first_pause = true;
        while (debugger_wait && ::cw::g_cwpy->is_running() && ::cw::g_cwpy->is_showingdebugger() &&
               *::cw::g_cwpy->is_showingdebugger() && m_paused && !m_stoped) {
            if (value_integer(m__targetstack, -2) >= -1 && value_integer(m__targetstack, -2) < get_currentstack()) break;
            const auto current = get_event();
            if (!current || !*current || !(*current)->m_force_nextcontent.is_none()) break;
            if (first_pause) { refresh_tools(); refresh_activeitem(); first_pause = false; }
            ::cw::g_cwpy->update_groups(
                {object_value<::cw::lib::Object>(::cw::g_cwpy->m_sbargrp)});
            dispatch_wait_input();
            ::cw::g_cwpy->wait_frame(1, false, std::nullopt, 0);
        }
    }
    // Py の wait() と同様、breakwait を消費せず、設定された遅延時間も消去しない。
    if (m_stoped) throw EffectBreakError{};
}

void EventInterface::set_curcontent(std::shared_ptr<::cw::data::CWPyElement> arg_content, std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    if (!arg_content) return;
    auto running = get_nowrunningevent();
    if (running && *running && arg_event && *arg_event != *running) return;
    auto event = running && *running ? running : arg_event;
    if (!event || !*event) return;
    if (running && *running) {
        std::set<Event*> visited;
        while (visited.insert(event->get()).second) {
            const auto parent = (*event)->m_parent.lock();
            if (!parent || visited.count(parent.get())) break;
            event = parent;
        }
    }
    const auto metadata_parent = arg_content->m_cwxparent && *arg_content->m_cwxparent
        ? *arg_content->m_cwxparent : nullptr;
    const auto runtime_parent = arg_content->m_parent.lock();
    const auto parent = metadata_parent ? metadata_parent : runtime_parent;
    if (parent && parent->m_tag == "ContentsLine") {
        (*event)->m_force_nextcontent = element_value(parent);
        (*event)->m_force_nextcontent_index = Value(parent->index(arg_content, std::nullopt, std::nullopt));
    } else {
        (*event)->m_force_nextcontent = element_value(arg_content);
        (*event)->m_force_nextcontent_index = Value(0);
    }
    if (running && *running) {
        if (::cw::g_cwpy) {
            if (auto message = ::cw::g_cwpy->get_messagewindow(); message && *message) {
                (*message)->m_result = Value(Integer{0});
                (*message)->m_result_typed = Integer{0};
            } else {
                (*event)->m_skip_action = true;
                refresh_activeitem();
            }
        } else {
            (*event)->m_skip_action = true;
            refresh_activeitem();
        }
    } else {
        const auto process = [](std::shared_ptr<::cw::battle::BattleError> error) {
            if (::cw::g_cwpy && ::cw::g_cwpy->is_battlestatus()) {
                if (!::cw::g_cwpy->m_battle || !*::cw::g_cwpy->m_battle)
                    throw std::logic_error("set_curcontent: battle is missing");
                (*::cw::g_cwpy->m_battle)->process_exception(std::move(error));
            }
        };
        // Python と同様、戦闘外も含めここで BattleError を
        // 捕捉する。 C++ の例外値を共通処理 API へ渡す型付きコピー
        // では、BattleError へ切り詰めず各標準派生型を維持する。
        try { (*event)->start(false); }
        catch (const ::cw::battle::BattleAreaChangeError& error) { process(std::make_shared<::cw::battle::BattleAreaChangeError>(error)); }
        catch (const ::cw::battle::BattleDefeatError& error) { process(std::make_shared<::cw::battle::BattleDefeatError>(error)); }
        catch (const ::cw::battle::BattleStartBattleError& error) { process(std::make_shared<::cw::battle::BattleStartBattleError>(error)); }
        catch (const ::cw::battle::BattleWinError& error) { process(std::make_shared<::cw::battle::BattleWinError>(error)); }
        catch (const ::cw::battle::BattleError& error) { process(std::make_shared<::cw::battle::BattleError>(error)); }
    }
}

void EventInterface::set_stepexec(bool arg_step)
{
    m__step = arg_step;
}

bool EventInterface::is_stepexec()
{
    return m__step;
}

void EventInterface::set_stoped(bool arg_stoped)
{
    m_stoped = arg_stoped;
}

bool EventInterface::is_stoped()
{
    return m_stoped;
}

bool EventInterface::is_paused()
{
    return m_paused;
}

void Targeting::initialize(std::optional<::cw::lib::Value> arg_user, std::vector<std::shared_ptr<::cw::sprite::card::CWPyCard>> arg_targets, bool arg_setcardtarget)
{
    m_user = arg_user.value_or(Value{}); m_user_card = arg_user ? card_value(*arg_user) : nullptr; m_target_cards = std::move(arg_targets);
    m_targets.clear(); for (const auto& target : m_target_cards) m_targets.emplace_back(target ? Value(std::static_pointer_cast<::cw::lib::Object>(target)) : Value{});
    m__setcardtarget = Value(arg_setcardtarget); m_waited = false; m_coupon_owners.clear(); m_mcards.clear(); m__target_updated = false; m__target_index = 0; m_eff.reset(); m_error.reset();
}

void Targeting::targets_to_coupon()
{
    update_targets(); clear_eventcoupons();
    if (m_user_card && *m_user_card) if (auto character = std::dynamic_pointer_cast<Character>(*m_user_card)) character->set_coupon("＠使用者", 0);
    for (const auto& target : m_target_cards) if (target) {
        if (auto character = std::dynamic_pointer_cast<Character>(target)) character->set_coupon("＠効果対象", 0);
        else m_mcards.insert(target);
        m_coupon_owners.insert(target);
    }
    m__target_updated = false;
    m__target_index = 0;
}

void Targeting::clear_eventcoupons()
{
    update_targets();
    if (m_user_card && *m_user_card) if (auto character = std::dynamic_pointer_cast<Character>(*m_user_card)) character->remove_coupon("＠使用者");
    for (const auto& card : m_coupon_owners) if (card) {
        if (auto character = std::dynamic_pointer_cast<Character>(card)) { character->remove_coupon("＠効果対象"); character->remove_coupon("＠効果対象外"); }
        if (value_bool(m__setcardtarget)) card->clear_cardtarget();
    }
    m_coupon_owners.clear(); m__target_updated = false;
}

bool Targeting::in_effectmotionloop()
{
    return m_waited;
}

void Targeting::update_targets()
{
    if (!m__target_updated) return;
    m_target_cards.clear();
    if (::cw::g_cwpy) {
        std::vector<std::shared_ptr<CWPyCard>> cards;
        for (const auto& card : ::cw::g_cwpy->get_pcards(Value(""))) if (card) cards.push_back(card);
        for (const auto& value : ::cw::g_cwpy->get_mcards(Value("visible"), {})) if (auto card = card_value(value)) cards.push_back(card);
        for (const auto& card : ::cw::g_cwpy->get_fcards(Value(""))) if (card) cards.push_back(card);
        for (const auto& card : cards) {
            if (auto character = std::dynamic_pointer_cast<Character>(card)) {
                const bool enabled = character->has_coupon("＠効果対象") && (!m_eff || !*m_eff || (*m_eff)->check_enabledtarget(card_object(card), false));
                if (enabled) {
                    m_target_cards.push_back(card);
                    if (value_bool(m__setcardtarget) && !card->m_cardtarget && in_effectmotionloop()) {
                        card->set_cardtarget();
                        ::cw::g_cwpy->wait_frame(1, ::cw::g_cwpy->m_setting ? ::cw::g_cwpy->m_setting->m_can_skipanimation : true, std::nullopt, 0);
                    }
                } else if (value_bool(m__setcardtarget) && card->m_cardtarget && in_effectmotionloop()) {
                    card->clear_cardtarget();
                    ::cw::g_cwpy->wait_frame(1, ::cw::g_cwpy->m_setting ? ::cw::g_cwpy->m_setting->m_can_skipanimation : true, std::nullopt, 0);
                }
            } else if (m_mcards.count(card)) {
                m_target_cards.push_back(card);
            }
        }
    }
    m_targets.clear(); for (const auto& target : m_target_cards) m_targets.emplace_back(target ? Value(std::static_pointer_cast<::cw::lib::Object>(target)) : Value{});
    m__target_index = 0; m__target_updated = false;
}

std::optional<std::shared_ptr<::cw::sprite::card::CWPyCard>> Targeting::get_nexttarget()
{
    update_targets();
    if (m__target_index < 0 || m__target_index >= checked_size_to_integer(m_target_cards.size(), "target count")) return std::nullopt;
    return m_target_cards[static_cast<std::size_t>(m__target_index++)];
}

void Targeting::add_target(std::shared_ptr<::cw::character::Character> arg_ccard)
{
    if (!arg_ccard) return; m__target_updated = true; if (auto card = std::dynamic_pointer_cast<CWPyCard>(arg_ccard)) { m_coupon_owners.insert(card); }
}

void Targeting::remove_target(std::shared_ptr<::cw::character::Character> arg_ccard)
{
    if (!arg_ccard) return;
    m__target_updated = true;
}

void CardEvent::initialize(std::optional<std::shared_ptr<::cw::data::CWPyElement>> arg_event, std::shared_ptr<::cw::header::CardHeader> arg_inusecard, ::cw::lib::Value arg_user, std::vector<std::shared_ptr<::cw::sprite::card::CWPyCard>> arg_targets, bool arg_skip_card_end_event)
{
    s_USE_CARD_KEYCODE = "＠バトルで選択時";
    Event::initialize(arg_event); Targeting::initialize(arg_user, std::move(arg_targets), true);
    Event::m_inusecard = arg_inusecard;
    m_skip_card_end_event = Value(arg_skip_card_end_event); m__reset_changestate = false;
    m_no_consume_unmatched_menu_card = false;
    m_author = arg_inusecard ? arg_inusecard->m_author : String{};
    m_scenario = arg_inusecard ? arg_inusecard->m_scenario : String{};
    m__flags.clear(); m__steps.clear(); m__variants.clear();
    if (arg_inusecard) {
        for (const auto& [name, value] : arg_inusecard->m_flags) { m__flags[name] = value; if (auto item = object_value<::cw::data::Flag>(value)) Event::m_flags[name] = item; }
        for (const auto& [name, value] : arg_inusecard->m_steps) { m__steps[name] = value; if (auto item = object_value<::cw::data::Step>(value)) Event::m_steps[name] = item; }
        for (const auto& [name, value] : arg_inusecard->m_variants) { m__variants[name] = value; if (auto item = object_value<::cw::data::Variant>(value)) Event::m_variants[name] = item; }
    }
}

bool CardEvent::_is_normal_start_event(std::shared_ptr<::cw::data::CWPyElement> arg_event)
{
    if (!arg_event) return true;
    const auto ignition = child_named(arg_event, "Ignitions"); if (!ignition) return true;
    return ignition_text_list(child_named(ignition, "KeyCodes") ? child_named(ignition, "KeyCodes")->m_text : "").empty() &&
           ignition_text_list(child_named(ignition, "Number") ? child_named(ignition, "Number")->m_text : "").empty();
}

void CardEvent::_copy_normal_start_event(std::shared_ptr<::cw::event::Event> arg_event)
{
    if (!arg_event) return; Event::m_trees = arg_event->m_trees; Event::m_treekeys = arg_event->m_treekeys; Event::m_starttree = arg_event->m_starttree; Event::m_cur_content = arg_event->m_cur_content; Event::m_keynums = arg_event->m_keynums; Event::m_keycodes = arg_event->m_keycodes; Event::m_keycode_matching = arg_event->m_keycode_matching; Event::m__event = arg_event->m__event;
}

void CardEvent::_select_normal_start_event()
{
    auto card = Event::m_inusecard; if (!card || !*card) return; auto data = object_value<CWPyElement>((*card)->m_carddata); if (!data) return; auto events = child_named(data, "Events"); if (!events) return; for (const auto& event : events->m_children) if (event && _is_normal_start_event(event)) { auto normal = std::make_shared<Event>(); normal->initialize(event); _copy_normal_start_event(normal); return; } auto empty = std::make_shared<Event>(); empty->initialize(std::nullopt); _copy_normal_start_event(empty);
}

void CardEvent::copy_variables_from(::cw::lib::Value arg_other)
{
    std::map<String, std::shared_ptr<::cw::data::Flag>> flags;
    std::map<String, std::shared_ptr<::cw::data::Step>> steps;
    std::map<String, std::shared_ptr<::cw::data::Variant>> variants;
    if (auto other = object_value<Event>(arg_other)) {
        flags = CardEvent_copy_variables_from_clone_flags(other->m_flags);
        steps = CardEvent_copy_variables_from_clone_steps(other->m_steps);
        variants = CardEvent_copy_variables_from_clone_variants(other->m_variants);
    } else if (auto header = object_value<::cw::header::CardHeader>(arg_other)) {
        std::map<String, std::shared_ptr<::cw::data::Flag>> source_flags;
        std::map<String, std::shared_ptr<::cw::data::Step>> source_steps;
        std::map<String, std::shared_ptr<::cw::data::Variant>> source_variants;
        for (const auto& [name, value] : header->m_flags) if (auto item = object_value<::cw::data::Flag>(value)) source_flags[name] = item;
        for (const auto& [name, value] : header->m_steps) if (auto item = object_value<::cw::data::Step>(value)) source_steps[name] = item;
        for (const auto& [name, value] : header->m_variants) if (auto item = object_value<::cw::data::Variant>(value)) source_variants[name] = item;
        flags = CardEvent_copy_variables_from_clone_flags(std::move(source_flags));
        steps = CardEvent_copy_variables_from_clone_steps(std::move(source_steps));
        variants = CardEvent_copy_variables_from_clone_variants(std::move(source_variants));
    } else {
        return;
    }
    Event::m_flags = std::move(flags); Event::m_steps = std::move(steps); Event::m_variants = std::move(variants);
    m__flags.clear(); m__steps.clear(); m__variants.clear();
    for (const auto& [name, value] : Event::m_flags) m__flags[name] = Value(std::static_pointer_cast<::cw::lib::Object>(value));
    for (const auto& [name, value] : Event::m_steps) m__steps[name] = Value(std::static_pointer_cast<::cw::lib::Object>(value));
    for (const auto& [name, value] : Event::m_variants) m__variants[name] = Value(std::static_pointer_cast<::cw::lib::Object>(value));
}

void CardEvent::set_variable_initialize(::cw::lib::String arg_initType)
{
    const auto initialization = ::cw::data::parse_initialization(Value(std::move(arg_initType)));
    for (auto& [name, value] : m__flags) { (void)name; if (auto variable = object_value<::cw::data::Flag>(value)) variable->m_initialization = initialization; }
    for (auto& [name, value] : m__steps) { (void)name; if (auto variable = object_value<::cw::data::Step>(value)) variable->m_initialization = initialization; }
    for (auto& [name, value] : m__variants) { (void)name; if (auto variable = object_value<::cw::data::Variant>(value)) variable->m_initialization = initialization; }
    for (auto& [name, value] : Event::m_flags) { (void)name; if (value) value->m_initialization = initialization; }
    for (auto& [name, value] : Event::m_steps) { (void)name; if (value) value->m_initialization = initialization; }
    for (auto& [name, value] : Event::m_variants) { (void)name; if (value) value->m_initialization = initialization; }
}

void CardEvent::start(bool arg_skip_effect_card_motion)
{
    _select_normal_start_event();
    start_selected(arg_skip_effect_card_motion);
}

void CardEvent::start_with_content(std::shared_ptr<::cw::data::CWPyElement> arg_content,
                                   bool arg_skip_effect_card_motion)
{
    // CardEvent の使用者・対象・バージョン設定を維持し、 通常のカード開始行だけを
    // SetActionEvent が生成した ActionLink の行へ置き換える。
    _select_normal_start_event();
    Event::m_cur_content = std::move(arg_content);
    Event::m_line_index = 0;
    start_selected(arg_skip_effect_card_motion);
}

void CardEvent::start_selected(bool arg_skip_effect_card_motion)
{
    const auto data = scenario_data();
    if (data && Event::m_inusecard && *Event::m_inusecard && ::cw::g_cwpy && ::cw::g_cwpy->m_sct)
        data->set_versionhint(::cw::g_HINT_CARD, ::cw::g_cwpy->m_sct->from_basehint(value_string((*Event::m_inusecard)->m_versionhint)));
    if (auto iface = event_interface()) {
        iface->m_is_changestate = ::cw::g_cwpy && ::cw::g_cwpy->is_battlestatus();
        if (data && Event::m_inusecard && *Event::m_inusecard && data->is_wsnversion("2", value_string((*Event::m_inusecard)->m_wsnversion))) targets_to_coupon();
        iface->set_inusecard(Event::m_inusecard);
        iface->set_selectedmember(character_value(Targeting::m_user));
        iface->m_effectevent = std::shared_ptr<Targeting>(this, [](Targeting*){});
    }
    m_no_consume_unmatched_menu_card = is_unmatched_menu_card_use();
    Event::start(arg_skip_effect_card_motion);
}

bool CardEvent::is_unmatched_menu_card_use()
{
    return ::cw::event::is_unmatched_menu_card_use(Event::m_inusecard.value_or(nullptr), m_target_cards);
}

bool is_unmatched_menu_card_use(std::shared_ptr<::cw::header::CardHeader> header,
    const std::vector<std::shared_ptr<::cw::sprite::card::CWPyCard>>& targets)
{
    // ネイティブの任意のシナリオ拡張で、既定では無効。 メニューカードへの使
    // 用で一致するキーコードイベントが一つもなければ、カードを消費しない。
    const auto data = scenario_data();
    if (!data || !data->m_summary || !*data->m_summary ||
        !(*data->m_summary)->getbool("Property/Settings", "noConsumeUnmatchedMenuCard", false) ||
        !header || targets.empty()) return false;
    // 混在・全体範囲も含むキャラクター対象では、通常の消費規則を維
    // 持する。 このフラグはメニューカードへの使用だけに適用する。
    for (const auto& target : targets)
        if (!std::dynamic_pointer_cast<::cw::sprite::card::MenuCard>(target)) return false;
    const auto keys = header->get_keycodes(true);
    if (data->m_events && *data->m_events && (*data->m_events)->check_keycodes(keys, false)) return false;
    for (const auto& target : targets) {
        const auto menu = std::dynamic_pointer_cast<::cw::sprite::card::MenuCard>(target);
        if (menu->m_events && menu->m_events->check_keycodes(keys, false)) return false;
    }
    return true;
}

void CardEvent::run_exit(bool arg_skip_effect_card_motion)
{
    auto iface = event_interface();
    try {
        Event::run_exit();
        const auto data = scenario_data();
        if (data && data->is_wsnversion("2", std::nullopt)) targets_to_coupon(); else clear_eventcoupons();
        if (data) data->set_versionhint(::cw::g_HINT_CARD, std::nullopt);
        if (::cw::g_cwpy) ::cw::g_cwpy->show_party();
        if (m_user_card && *m_user_card && std::dynamic_pointer_cast<::cw::sprite::card::PlayerCard>(*m_user_card))
            run_areaevent();
        if (iface) iface->m_in_cardeffectmotion = true;
        try {
            if (::cw::g_cwpy && !::cw::g_cwpy->is_battlestatus()) ::cw::g_cwpy->update_statusimgs(false, std::nullopt);
            if (!arg_skip_effect_card_motion) effect_cardmotion();
        } catch (...) {
            if (iface) iface->m_in_cardeffectmotion = false;
            throw;
        }
        if (iface) iface->m_in_cardeffectmotion = false;
    } catch (...) {
        if (iface) iface->m_effectevent.reset();
        throw;
    }
    if (iface) iface->m_effectevent.reset();
}

void CardEvent::end()
{
    if (value_bool(m_skip_card_end_event)) return;
    const auto data = scenario_data();
    if (data) data->set_versionhint(::cw::g_HINT_CARD, std::nullopt);
    auto iface = event_interface();
    const auto header = Event::m_inusecard ? *Event::m_inusecard : nullptr;
    const auto effect_break = Event::m_error && *Event::m_error ? std::dynamic_pointer_cast<EffectBreakError>(*Event::m_error) : nullptr;
    const bool consume = !effect_break || value_bool(effect_break->m_consumecard, true);
    const bool scenario_end = Event::m_error && *Event::m_error && std::dynamic_pointer_cast<ScenarioEndError>(*Event::m_error);
    const bool spend = ::cw::g_cwpy && ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_spend_noeffectcard;
    if (header && iface && (spend || iface->m_is_changestate) && consume &&
        !m_no_consume_unmatched_menu_card && !(header->m_type == "SkillCard" && scenario_end))
        header->set_uselimit(-1, !scenario_end);
    if (iface) iface->m_is_changestate = false;

    if (header) {
        header->set_resetvariables(false);
        const auto mark_owner_edited = [&]() {
            if (const auto owner = header->get_owner(); owner)
                if (auto character = character_value(*owner); character && character->m_data_typed)
                    character->m_data_typed->m_is_edited = true;
        };
        auto save_flag = [&](const std::shared_ptr<::cw::data::Flag>& variable) {
            if (!variable) return;
            const bool event_exit = std::find(variable->m_initialization.begin(), variable->m_initialization.end(), "EventExit") != variable->m_initialization.end();
            if (event_exit) variable->m_value = variable->m_defaultvalue;
            else { variable->write_value(); mark_owner_edited(); }
            if (std::find(variable->m_initialization.begin(), variable->m_initialization.end(), "Leave") != variable->m_initialization.end() && variable->m_value != variable->m_defaultvalue) header->set_resetvariables(true);
        };
        auto save_step = [&](const std::shared_ptr<::cw::data::Step>& variable) {
            if (!variable) return;
            const bool event_exit = std::find(variable->m_initialization.begin(), variable->m_initialization.end(), "EventExit") != variable->m_initialization.end();
            if (event_exit) variable->m_value = variable->m_defaultvalue;
            else { variable->write_value(); mark_owner_edited(); }
            if (std::find(variable->m_initialization.begin(), variable->m_initialization.end(), "Leave") != variable->m_initialization.end() && variable->m_value != variable->m_defaultvalue) header->set_resetvariables(true);
        };
        auto save_variant = [&](const std::shared_ptr<::cw::data::Variant>& variable) {
            if (!variable) return;
            const bool event_exit = std::find(variable->m_initialization.begin(), variable->m_initialization.end(), "EventExit") != variable->m_initialization.end();
            if (event_exit) variable->m_value = clone_variant_value(variable->m_defaultvalue);
            else { variable->write_value(); mark_owner_edited(); }
            if (std::find(variable->m_initialization.begin(), variable->m_initialization.end(), "Leave") != variable->m_initialization.end() &&
                !::cw::data::Variant::values_equal(variable->m_value, variable->m_defaultvalue)) header->set_resetvariables(true);
        };
        for (const auto& [name, value] : m__flags) { (void)name; save_flag(object_value<::cw::data::Flag>(value)); }
        for (const auto& [name, value] : m__steps) { (void)name; save_step(object_value<::cw::data::Step>(value)); }
        for (const auto& [name, value] : m__variants) { (void)name; save_variant(object_value<::cw::data::Variant>(value)); }
    }
    if (::cw::g_cwpy) {
        if (!Targeting::m_waited && ::cw::g_cwpy->m_setting) {
            const Integer waitrate = (::cw::g_cwpy->m_setting->get_dealspeed(::cw::g_cwpy->is_battlestatus()) + 1) * 2;
            ::cw::g_cwpy->wait_frame(waitrate, ::cw::g_cwpy->m_setting->m_can_skipanimation, std::nullopt, 0);
        }
        ::cw::g_cwpy->clear_inusecardimg(m_user_card);
        for (const auto& target : m_target_cards) if (target) target->clear_cardtarget();
        if (m_user_card && *m_user_card && !(*m_user_card)->m_zoomimgs.empty())
            ::cw::animation::animate_sprite(*m_user_card, "zoomout", true, false, false, ::cw::g_cwpy->is_battlestatus());
    }
    clear_eventcoupons();
    Event::end();
    if (::cw::g_cwpy) ::cw::g_cwpy->clear_specialarea(true, false);
}

void CardEvent::_exit_event()
{
    const bool area_change = Event::m_error && *Event::m_error && std::dynamic_pointer_cast<AreaChangeError>(*Event::m_error);
    const bool bad_end = Event::m_error && *Event::m_error && std::dynamic_pointer_cast<ScenarioBadEndError>(*Event::m_error);
    auto iface = event_interface();
    const auto data = scenario_data();
    if (!area_change && !bad_end && ::cw::g_cwpy &&
        !(::cw::g_cwpy->is_gameover() && data && data->can_gameover()) &&
        !(iface && iface->is_stoped())) {
        ::cw::g_cwpy->show_party();
    }
}

void CardEvent::run_areaevent()
{
    auto iface = event_interface(); if (!iface || !Event::m_inusecard || !*Event::m_inusecard) return;
    auto data = scenario_data(); if (data && data->m_events && *data->m_events) {
        const auto keycodes = (*Event::m_inusecard)->get_keycodes(true);
        if (m_no_consume_unmatched_menu_card && (*data->m_events)->check_keycodes(keycodes, false))
            m_no_consume_unmatched_menu_card = false;
        store_inusedata(true);
        { (*data->m_events)->start(std::nullopt, keycodes, true, false); }restore_inusedata(); _exit_event();
    }
}

void CardEvent::run_characterevent(std::shared_ptr<::cw::character::Character> arg_target, bool arg_can_unconscious)
{
    if (!arg_target || !Event::m_inusecard || !*Event::m_inusecard) return;
    const auto keycodes = (*Event::m_inusecard)->get_keycodes(true);
    if (!ignition_characterevent(arg_target, arg_can_unconscious, keycodes)) return;
    const auto events = get_events(std::dynamic_pointer_cast<CWPyCard>(arg_target));
    if (!events || !*events) return;
    store_inusedata(true);
    try { (*events)->start(std::nullopt, keycodes, true, false); }
    catch (...) { restore_inusedata(); throw; }
    restore_inusedata(); _exit_event();
}

bool CardEvent::_has_usecard_event_keycode(std::shared_ptr<::cw::header::CardHeader> arg_header)
{
    if (!arg_header) return false; const auto events = arg_header->get_events(); if (!events || !*events) return false; for (const auto& event : (*events)->m_event_list) if (event && std::find(event->m_keycodes.begin(), event->m_keycodes.end(), s_USE_CARD_KEYCODE) != event->m_keycodes.end()) return true; return false;
}

std::optional<std::shared_ptr<::cw::event::Event>> CardEvent::_check_usecard_event(std::shared_ptr<::cw::event::EventEngine> arg_events, std::vector<::cw::lib::String> arg_keycodes, bool arg_successevent)
{
    if (!arg_events) return std::nullopt; std::set<String> set(arg_keycodes.begin(), arg_keycodes.end()); set.erase("");
    for (const auto& event : arg_events->m_event_list) {
        if (!event || std::find(event->m_keycodes.begin(), event->m_keycodes.end(), s_USE_CARD_KEYCODE) == event->m_keycodes.end()) continue;
        std::vector<String> keys; for (const auto& key : event->m_keycodes) if (!key.empty() && key != s_USE_CARD_KEYCODE) keys.push_back(key);
        String matching = event->m_keycode_matching; const auto data = scenario_data();
        if (matching == "And" && data && ::cw::g_cwpy && ::cw::g_cwpy->m_sct && ::cw::g_cwpy->m_sct->lessthan("1.30", data->get_versionhint(::cw::g_HINT_AREA))) { keys.insert(keys.begin(), "MatchingType=All"); matching = "Or"; }
        if (keys.empty() || (matching == "And" ? std::all_of(keys.begin(), keys.end(), [&](const String& key){ return arg_events->check_keycode(key, set, arg_successevent); }) : std::any_of(keys.begin(), keys.end(), [&](const String& key){ return arg_events->check_keycode(key, set, arg_successevent); }))) return event;
    }
    return std::nullopt;
}

std::optional<std::shared_ptr<::cw::header::CardHeader>> CardEvent::_get_selected_usecard_for_keycodeevent(std::shared_ptr<::cw::character::Character> arg_target)
{
    if (!arg_target || !::cw::g_cwpy || !::cw::g_cwpy->is_battlestatus()) return std::nullopt;
    auto header = arg_target->m_action_header_typed;
    if ((!header || !*header) && arg_target->m_deck) header = arg_target->m_deck->get_used();
    if (header && *header && _has_usecard_event_keycode(*header)) return header;
    return std::nullopt;
}

void CardEvent::run_target_selctcardevent(std::shared_ptr<::cw::character::Character> arg_target)
{
    auto header = _get_selected_usecard_for_keycodeevent(arg_target); if (!header || !Event::m_inusecard || !*Event::m_inusecard) return; auto events = (*header)->get_events(); auto selected = events ? _check_usecard_event(*events, (*Event::m_inusecard)->get_keycodes(true), false) : std::nullopt; if (selected && *selected) { store_inusedata(true); { (*selected)->clear(); (*selected)->run(false, false, false); }restore_inusedata(); _exit_event(); }
}

void CardEvent::run_ownedcardevent(std::shared_ptr<::cw::character::Character> arg_target)
{
    if (!arg_target || !Event::m_inusecard || !*Event::m_inusecard) return;
    const auto keycodes = (*Event::m_inusecard)->get_keycodes(true);
    for (const Integer pocket : {::cw::g_POCKET_SKILL, ::cw::g_POCKET_ITEM, ::cw::g_POCKET_BEAST}) {
        const auto cards = arg_target->get_pocketcards(pocket);
        for (const auto& header : cards) {
            if (!header) continue;
            const auto events = header->get_events();
            if (!events || !*events || !(*events)->check_keycodes(keycodes, false)) continue;
            // 通常は get_events() がこの関連付けを設定するが、実行境界
            // でも繰り返す。 LATESTUSECARDOWNER() が外側の行動
            // カードではなく、この受動カードの所有者を解決する必要があるためである。
            for (const auto& event : (*events)->m_event_list)
                if (event) event->m_inusecard = header;
            store_inusedata(true);
            { (*events)->start(std::nullopt, keycodes, true, false); }restore_inusedata(); _exit_event();
            if (!scenario_continues()) return;
        }
    }
}

bool CardEvent::run_deadevent(std::shared_ptr<::cw::character::Character> arg_target)
{
    const auto keycodes = Event::m_inusecard && *Event::m_inusecard ? (*Event::m_inusecard)->get_keycodes(false) : std::vector<String>{}; auto event = ignition_deadevent(arg_target, keycodes); if (!event || !*event) return false; store_inusedata(true); { (*event)->clear(); (*event)->run(false, false, false); }restore_inusedata(); _exit_event(); return true;
}

void CardEvent::run_menucardevent(std::shared_ptr<::cw::sprite::card::MenuCard> arg_target)
{
    if (!arg_target) return;
    const auto keycodes = Event::m_inusecard && *Event::m_inusecard ? (*Event::m_inusecard)->get_keycodes(true) : std::vector<String>{};
    auto event = ignition_menucardevent(arg_target, keycodes);
    if (event && *event) {
        m_no_consume_unmatched_menu_card = false;
        const bool lock = ::cw::g_cwpy && ::cw::g_cwpy->m_lock_menucards;
        if (::cw::g_cwpy) ::cw::g_cwpy->m_lock_menucards = false;
        try {
            const auto events = get_events(arg_target);
            if (!events || !*events) throw std::logic_error("menu keycode event engine missing");
            store_inusedata(true);
            // Py と同様、EventEngine.start(keycodes=keycodes)
            // を通し、 イベント内部かの判断とルート Event.start の経路も使う。
            (*events)->start(std::nullopt, keycodes, false, false);
            restore_inusedata(); _exit_event();
        } catch (...) {
            // Py の finally と同様、例外時に復元するのはメニューロックだけとする。
            // 選択・使用中状態の復元は上の正常終了処理で行い、例外の巻き戻しには含めない。
            if (::cw::g_cwpy) ::cw::g_cwpy->m_lock_menucards = lock;
            throw;
        }
        if (::cw::g_cwpy) ::cw::g_cwpy->m_lock_menucards = lock;
    } else if (::cw::g_cwpy) {
        ::cw::g_cwpy->play_sound("ineffective", true, 100, 1, 0, 0, false);
        if (::cw::g_cwpy->m_advlog) ::cw::g_cwpy->m_advlog->effect_failed(card_object(arg_target), true);
    }
}

void CardEvent::run_successevent(std::shared_ptr<::cw::character::Character> arg_target, bool arg_successflag)
{
    if (!arg_target || !Event::m_inusecard || !*Event::m_inusecard) return;
    const auto keys = (*Event::m_inusecard)->get_keycodes(true);
    if (!ignition_successevent(arg_target, arg_successflag, keys)) return;
    const auto events = get_events(std::dynamic_pointer_cast<CWPyCard>(arg_target));
    if (!events || !*events) return;
    store_inusedata(true);
    try { (*events)->start(std::nullopt, _keycodes_for_successevent(keys, arg_successflag), true, true); }
    catch (...) { restore_inusedata(); throw; }
    restore_inusedata(); _exit_event();
}

void CardEvent::run_usecard_successevent(std::shared_ptr<::cw::character::Character> arg_target, bool arg_successflag)
{
    auto header = _get_selected_usecard_for_keycodeevent(arg_target);
    if (!header || !*header || !Event::m_inusecard || !*Event::m_inusecard) return;
    const auto keycodes = _keycodes_for_successevent((*Event::m_inusecard)->get_keycodes(true), arg_successflag);
    const auto events = (*header)->get_events();
    const auto selected = events && *events ? _check_usecard_event(*events, keycodes, true) : std::nullopt;
    if (!selected || !*selected) return;
    store_inusedata(true);
    { (*selected)->clear(); (*selected)->run(false, false, false); }restore_inusedata(); _exit_event();
}

void CardEvent::run_ownedcard_successevent(std::shared_ptr<::cw::character::Character> arg_target, bool arg_successflag)
{
    if (!arg_target || !Event::m_inusecard || !*Event::m_inusecard) return;
    const auto keycodes = _keycodes_for_successevent((*Event::m_inusecard)->get_keycodes(true), arg_successflag);
    for (const Integer pocket : {::cw::g_POCKET_SKILL, ::cw::g_POCKET_ITEM, ::cw::g_POCKET_BEAST}) {
        const auto cards = arg_target->get_pocketcards(pocket);
        for (const auto& header : cards) {
            if (!header) continue;
            const auto events = header->get_events();
            if (!events || !*events || !(*events)->check_keycodes(keycodes, true)) continue;
            for (const auto& event : (*events)->m_event_list)
                if (event) event->m_inusecard = header;
            store_inusedata(true);
            { (*events)->start(std::nullopt, keycodes, true, true); }restore_inusedata(); _exit_event();
            if (!scenario_continues()) return;
        }
    }
}

void CardEvent::effect_cardmotion()
{
    update_targets();
    auto iface = event_interface();
    if (m_target_cards.empty()) { if (iface) iface->m_is_changestate = true; return; }
    const auto header = Event::m_inusecard ? *Event::m_inusecard : nullptr;
    const auto data = header ? object_value<CWPyElement>(header->m_carddata) : nullptr;
    if (!header || !data) return;

    auto params = std::make_shared<::cw::effectmotion::EffectParams>();
    params->initialize(); params->m_inusecard = header;
    params->set_item("user", Targeting::m_user);
    params->set_item("absorbto", Value("User"));
    params->set_item("successrate", Value(data->getint("Property/SuccessRate", std::nullopt, 0)));
    params->set_item("effecttype", Value(data->gettext("Property/EffectType", "Physic")));
    params->set_item("resisttype", Value(data->gettext("Property/ResistType", "Avoid")));
    params->set_item("soundpath", Value(::cw::util::validate_filepath(data->gettext("Property/SoundPath2", ""))));
    params->set_item("volume", Value(data->getint("Property/SoundPath2", Value("volume"), 100)));
    params->set_item("loopcount", Value(data->getint("Property/SoundPath2", Value("loopcount"), 1)));
    params->set_item("channel", Value(data->getint("Property/SoundPath2", Value("channel"), 0)));
    params->set_item("fadein", Value(data->getint("Property/SoundPath2", Value("fadein"), 0)));
    params->set_item("visualeffect", Value(data->gettext("Property/VisualEffect", "None")));
    params->set_item("target", Value(data->gettext("Property/Target", "None")));
    const bool allrange = data->getbool("Property/Target", Value("allrange"), false);
    params->set_item("allrange", Value(allrange));

    auto effect = std::make_shared<::cw::effectmotion::Effect>();
    const auto motions = child_named(data, "Motions");
    effect->initialize(motions ? motions->m_children : std::vector<std::shared_ptr<CWPyElement>>{}, params,
                       ::cw::g_cwpy && ::cw::g_cwpy->is_battlestatus());
    Targeting::m_eff = effect;
    const auto selected = iface ? iface->get_selectedmember() : std::nullopt;
    effect->update_status(selected);
    // Effect の初期化中にカードイベントが対象の称号・状態を変える場
    // 合がある。 Python と同じこの境界で、対象をもう一度解決する。
    update_targets();

    String initial_path;
    Integer initial_volume = 100, initial_loopcount = 1, initial_channel = 0, initial_fade = 0;
    if (allrange || m_target_cards.size() != 1) {
        initial_path = ::cw::util::validate_filepath(data->gettext("Property/SoundPath", ""));
        initial_volume = data->getint("Property/SoundPath", Value("volume"), 100);
        initial_loopcount = data->getint("Property/SoundPath", Value("loopcount"), 1);
        initial_channel = data->getint("Property/SoundPath", Value("channel"), 0);
        initial_fade = data->getint("Property/SoundPath", Value("fadein"), 0);
    }
    m_target_cards = initial_effect(effect, m_target_cards, allrange, initial_path, initial_volume,
                                    initial_loopcount, initial_channel, initial_fade, std::nullopt);
    Targeting::m_targets.clear(); for (const auto& target : m_target_cards) Targeting::m_targets.emplace_back(card_object(target));
    Targeting::m_waited = true; Targeting::m__target_index = 0;

    while (scenario_continues()) {
        auto next = get_nexttarget(); if (!next || !*next) break;
        const auto target = *next;
        const auto menu = std::dynamic_pointer_cast<::cw::sprite::card::MenuCard>(target);
        const auto character = std::dynamic_pointer_cast<Character>(target);
        if (iface && !menu && character && !std::dynamic_pointer_cast<::cw::character::Player>(character)) iface->m_is_changestate = true;
        auto clear_params = [&]() { CardEvent_effect_cardmotion_clear_params(card_object(target)); };
        if (character) {
            const bool permits_unconscious = effect->has_motions(can_unconscious_motions()) || effect->has_addablebeast(card_object(target)) || effect->has_removablebeast(card_object(target));
            if ((character->is_unconscious() && !permits_unconscious) ||
                (character->get_card_status() == "hidden" && !std::dynamic_pointer_cast<::cw::sprite::card::FriendCard>(target)) || character->is_vanished()) {
                clear_params(); continue;
            }
            const auto [unconscious, paralyzed] = get_effecttargetstatus(card_object(target), effect); (void)paralyzed;
            if ((character->is_unconscious() && !unconscious) || params->get("target", "None") == "None") {
                character->remove_coupon("＠効果対象"); continue;
            }
            const auto scenario = scenario_data();
            if (scenario && scenario->is_wsnversion("2", std::nullopt)) character->set_coupon("＠イベント対象", 0);
            struct EventTargetCouponGuard {
                std::shared_ptr<Character> character;
                ~EventTargetCouponGuard() { if (character) character->remove_coupon("＠イベント対象"); }
            } event_target_guard{character};
            run_characterevent(character, unconscious); if (!scenario_continues()) break;
            run_target_selctcardevent(character); if (!scenario_continues()) break;
            run_ownedcardevent(character); if (!scenario_continues()) break;
            update_targets();
            if (scenario && scenario->is_wsnversion("2", std::nullopt) && !character->has_coupon("＠効果対象")) { clear_params(); continue; }
            if (!effect->check_enabledtarget(card_object(target), false)) { clear_params(); continue; }
            target->clear_cardtarget();
            const bool already_dead = character->is_unconscious() || character->is_paralyze();
            const bool success = effect->apply(card_object(target), false, selected);
            character->remove_coupon("＠効果対象");
            const bool death_event = !already_dead && run_deadevent(character);
            if (!scenario_continues()) break;
            if (!death_event) {
                run_successevent(character, success); if (!scenario_continues()) break;
                run_usecard_successevent(character, success); if (!scenario_continues()) break;
                run_ownedcard_successevent(character, success);
            }
        } else if (menu) {
            // Py と同様、initial_effect の待機後に全対象を再確認
            // する。 待機中に隠れたメニューでは、効果音やキーコードを実行しない。
            if (target->m_status == "hidden") {
                clear_params();
                continue;
            }
            target->clear_cardtarget();
            if (::cw::g_cwpy) ::cw::g_cwpy->play_sound_with(value_string(effect->m_soundpath), header,
                value_integer(effect->m_volume, 100), value_integer(effect->m_loopcount, 1),
                value_integer(effect->m_channel, 0), value_integer(effect->m_fade, 0));
            // Python と同様、メニュー画像は変更せず、 アニメーション
            // 後、メニューイベントを実行する前にその領域を再描画対象にする。
            effect->animate(target, false);
            if (::cw::g_cwpy) ::cw::g_cwpy->add_lazydraw(std::static_pointer_cast<::cw::lib::Object>(target));
            m_mcards.erase(target); run_menucardevent(menu);
        }
    }
    if (!scenario_continues()) return;
    if (!(Event::m_error && *Event::m_error && std::dynamic_pointer_cast<AreaChangeError>(*Event::m_error)) && iface) iface->set_selectedmember(std::nullopt);
    check_gameover();
}

void EffectBreakError::initialize(bool arg_consumecard)
{
    m_consumecard = Value(arg_consumecard);
}

void EventInterface_refresh_activeitem_func()
{
    if (auto iface = event_interface()) iface->refresh_activeitem();
}

void EventInterface_wait_func()
{
    if (auto iface = event_interface()) iface->wait();
}

std::vector<::cw::lib::String> _get_targetinfo()
{
    std::vector<String> user, eventtarget, targets, outtargets;
    if (::cw::g_cwpy) {
        auto collect = [&](const auto& cards) { for (const auto& card : cards) if (card) { const String name = card->get_showingname(); if (card->has_coupon("＠使用者")) user.push_back(name); if (card->has_coupon("＠イベント対象")) eventtarget.push_back(name); if (card->has_coupon("＠効果対象")) targets.push_back(name); if (card->has_coupon("＠効果対象外")) outtargets.push_back(name); } };
        collect(::cw::g_cwpy->get_pcards(Value(""))); collect(::cw::g_cwpy->get_ecards(Value(""))); collect(::cw::g_cwpy->get_fcards(Value("")));
    }
    auto join = [](const std::vector<String>& values) { String text; for (const auto& value : values) { if (!text.empty()) text += ", "; text += value; } return text; };
    return {"User          : " + join(user), "Event Target  : " + join(eventtarget), "Targets       : " + join(targets), "Out of Targets: " + join(outtargets)};
}

std::map<::cw::lib::String, std::shared_ptr<::cw::data::Flag>> CardEvent_copy_variables_from_clone_flags(std::map<::cw::lib::String, std::shared_ptr<::cw::data::Flag>> arg_src)
{
    std::map<String, std::shared_ptr<::cw::data::Flag>> result; for (const auto& [name, source] : arg_src) if (source) { auto copy = std::make_shared<::cw::data::Flag>(*source); copy->m__parent = Value{}; copy->m__data.reset(); copy->m_is_writable = false; result[name] = copy; } return result;
}

std::map<::cw::lib::String, std::shared_ptr<::cw::data::Step>> CardEvent_copy_variables_from_clone_steps(std::map<::cw::lib::String, std::shared_ptr<::cw::data::Step>> arg_src)
{
    std::map<String, std::shared_ptr<::cw::data::Step>> result; for (const auto& [name, source] : arg_src) if (source) { auto copy = std::make_shared<::cw::data::Step>(*source); copy->m__parent = Value{}; copy->m__data.reset(); copy->m_is_writable = false; result[name] = copy; } return result;
}

std::map<::cw::lib::String, std::shared_ptr<::cw::data::Variant>> CardEvent_copy_variables_from_clone_variants(std::map<::cw::lib::String, std::shared_ptr<::cw::data::Variant>> arg_src)
{
    std::map<String, std::shared_ptr<::cw::data::Variant>> result;
    for (const auto& [name, source] : arg_src) if (source) {
        auto copy = std::make_shared<::cw::data::Variant>(*source);
        copy->m_value = clone_variant_value(source->m_value);
        copy->m_defaultvalue = clone_variant_value(source->m_defaultvalue);
        copy->m__parent = Value{}; copy->m__data = Value{}; copy->m_is_writable = false;
        result[name] = std::move(copy);
    }
    return result;
}

void CardEvent_end_save_var(::cw::lib::Value arg_v)
{
    if (auto flag = object_value<::cw::data::Flag>(arg_v)) {
        if (std::find(flag->m_initialization.begin(), flag->m_initialization.end(), "EventExit") != flag->m_initialization.end()) flag->m_value = flag->m_defaultvalue;
        else flag->write_value();
    } else if (auto step = object_value<::cw::data::Step>(arg_v)) {
        if (std::find(step->m_initialization.begin(), step->m_initialization.end(), "EventExit") != step->m_initialization.end()) step->m_value = step->m_defaultvalue;
        else step->write_value();
    } else if (auto variant = object_value<::cw::data::Variant>(arg_v)) {
        if (std::find(variant->m_initialization.begin(), variant->m_initialization.end(), "EventExit") != variant->m_initialization.end()) variant->m_value = clone_variant_value(variant->m_defaultvalue);
        else variant->write_value();
    }
}

void CardEvent_effect_cardmotion_clear_params(::cw::lib::Value arg_target)
{
    auto card = card_value(arg_target); if (card) { if (auto character = std::dynamic_pointer_cast<Character>(card)) character->remove_coupon("＠効果対象"); card->clear_cardtarget(); }
}

std::tuple<bool, bool> get_effecttargetstatus(::cw::lib::Value arg_target, std::shared_ptr<::cw::effectmotion::Effect> arg_eff)
{
    auto card = card_value(arg_target); auto character = card ? std::dynamic_pointer_cast<Character>(card) : nullptr; if (!character || !arg_eff) return {false, false};
    const bool unconscious = (arg_eff->has_motions(can_unconscious_motions()) || arg_eff->has_addablebeast(arg_target) || arg_eff->has_removablebeast(arg_target)) && character->is_unconscious();
    return {unconscious, character->is_paralyze()};
}

std::vector<std::shared_ptr<::cw::sprite::card::CWPyCard>> initial_effect(std::shared_ptr<::cw::effectmotion::Effect> arg_eff, std::vector<std::shared_ptr<::cw::sprite::card::CWPyCard>> arg_targets, bool arg_allrange, ::cw::lib::String arg_path, ::cw::lib::Integer arg_volume, ::cw::lib::Integer arg_loopcount, ::cw::lib::Integer arg_channel, ::cw::lib::Integer arg_fade, std::optional<::cw::lib::Callback> arg_remove_target)
{
    std::vector<std::shared_ptr<CWPyCard>> result; if (!arg_eff) return result;
    bool skipped = false;
    auto remove = [&](const std::shared_ptr<CWPyCard>& target) { if (arg_remove_target) (*arg_remove_target)({card_object(target)}, {}); };
    if (!arg_allrange && arg_targets.size() == 1) {
        const auto& target = arg_targets.front(); if (!target) return result;
        target->set_cardtarget();
        if (::cw::g_cwpy && !arg_path.empty()) ::cw::g_cwpy->play_sound_with(arg_path, std::nullopt, arg_volume, arg_loopcount, arg_channel, arg_fade);
        const bool enabled = arg_eff->check_enabledtarget(card_object(target), false);
        if (::cw::g_cwpy && ::cw::g_cwpy->m_setting) {
            const Integer rate = (::cw::g_cwpy->m_setting->get_dealspeed(::cw::g_cwpy->is_battlestatus()) + 1) * (enabled ? 2 : 1);
            skipped = ::cw::g_cwpy->wait_frame(rate, ::cw::g_cwpy->m_setting->m_can_skipanimation, std::nullopt, 0);
        }
        if (enabled) result.push_back(target); else { target->clear_cardtarget(); remove(target); }
    } else {
        for (const auto& target : arg_targets) {
            if (!target) continue;
            if (arg_eff->check_enabledtarget(card_object(target), false)) {
                target->set_cardtarget(); result.push_back(target);
                if (::cw::g_cwpy && !arg_path.empty()) ::cw::g_cwpy->play_sound_with(arg_path, std::nullopt, arg_volume, arg_loopcount, arg_channel, arg_fade);
                if (::cw::g_cwpy && ::cw::g_cwpy->m_setting) skipped = ::cw::g_cwpy->wait_frame(
                    ::cw::g_cwpy->m_setting->get_dealspeed(::cw::g_cwpy->is_battlestatus()) + 1,
                    ::cw::g_cwpy->m_setting->m_can_skipanimation, std::nullopt, 0);
            } else remove(target);
        }
    }
    if (!skipped && ::cw::g_cwpy && ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_wait_usecard)
        ::cw::g_cwpy->wait_frame(::cw::g_cwpy->m_setting->get_dealspeed(::cw::g_cwpy->is_battlestatus()),
                                 ::cw::g_cwpy->m_setting->m_can_skipanimation, std::nullopt, 0);
    return result;
}

void main()
{
    // Python版のmain()もpass。モジュール初期化はCWPy生成時に行う。
}

}

