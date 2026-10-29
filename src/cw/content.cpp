#include "cw/lib/exception_origin.hpp"
#include "cw/content.hpp"
#include <iostream>
#include "cw/lib/ui_surface.hpp"
#include "cw/dialog/dialog.hpp"
#include "cw/dialog/message.hpp"
#include "cw/lib/encoding.hpp"
#include "cw/lib/python_integer.hpp"
#include "cw/lib/python_integer_value.hpp"
#include "cw/lib/python_text.hpp"
#include "cw/cw.hpp"
#include "cw/calculator.hpp"
#include "cw/character.hpp"
#include "cw/data.hpp"
#include "cw/deck.hpp"
#include "cw/dice.hpp"
#include "cw/dice_random.hpp"
#include "cw/event.hpp"
#include "cw/eventhandler.hpp"
#include "cw/effectmotion.hpp"
#include "cw/animation.hpp"
#include "cw/header.hpp"
#include "cw/image.hpp"
#include "cw/setting.hpp"
#include "cw/sprite/card.hpp"
#include "cw/sprite/background.hpp"
#include "cw/sprite/message.hpp"
#include "cw/thread.hpp"
#include "cw/game_event_queue.hpp"
#include "cw/novel.hpp"
#include "cw/battle.hpp"
#include "cw/advlog.hpp"
#include "cw/util.hpp"
#include "cw/frame.hpp"
#include "cw/lib/python_float.hpp"
#include "cw/lib/python_percent_string.hpp"
#include "cw/dialog/etc.hpp"
#include "cw/binary/image.hpp"
#include "cw/debug/debugger.hpp"
#include "app/debug_control_server.hpp"

#include <algorithm>
#include <filesystem>
#include <cctype>
#include <cmath>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <chrono>
#include <limits>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace cw::content {

static std::shared_ptr<EventContentBase> get_typed_content(
    std::shared_ptr<::cw::data::CWPyElement> data,
    const ::cw::lib::String& classname);

namespace {
using S = ::cw::lib::String;
using I = ::cw::lib::Integer;
using V = ::cw::lib::Value;

// ネイティブのコンテント操作は、Pythonの動的な戻り値と異なり型付きのインデ
// ックスを返す。 選択肢の結果は整数・真偽値なのでその境界を検証し、 show_message()
// が返した文字列を解析したり浮動小数点を切り捨てたりしない。
I message_selection_index(const V& value)
{
    if (const auto* index = std::get_if<I>(&value.storage())) return *index;
    if (const auto* index = std::get_if<bool>(&value.storage())) return *index ? 1 : 0;
    throw std::invalid_argument("message selection result is not an integer index");
}

bool message_result_is_zero(const V& value)
{
    if (const auto* index = std::get_if<I>(&value.storage())) return *index == 0;
    if (const auto* index = std::get_if<bool>(&value.storage())) return !*index;
    if (const auto* index = std::get_if<::cw::lib::Real>(&value.storage())) return *index == 0;
    if (const auto* index = value.as_decimal()) return index->is_zero();
    return false; // Python版では、文字列・オブジェクトの結果は整数ゼロと等しくない。
}

// Python版は継承メソッドも含め、実際のキャラクター・カードで is_<status>
// を解決する。 C++版にはPythonのインスタンスへの動的メソッド変更がないため、共通
// のネイティブ公開関数へ振り分ける。 メソッドなしとfalse、呼び出しエラーを区別する。
std::optional<bool> character_status(const std::shared_ptr<::cw::character::Character>& target, const S& status)
{
    if (status == "normal") return target->is_normal();
    if (status == "panic") return target->is_panic();
    if (status == "brave") return target->is_brave();
    if (status == "overheat") return target->is_overheat();
    if (status == "confuse") return target->is_confuse();
    if (status == "sleep") return target->is_sleep();
    if (status == "paralyze") return target->is_paralyze();
    if (status == "poison") return target->is_poison();
    if (status == "bind") return target->is_bind();
    if (status == "silence") return target->is_silence();
    if (status == "faceup") return target->is_faceup();
    if (status == "antimagic") return target->is_antimagic();
    if (status == "petrified") return target->is_petrified();
    if (status == "unconscious") return target->is_unconscious();
    if (status == "heavyinjured") return target->is_heavyinjured();
    if (status == "injured") return target->is_injured();
    if (status == "injuredall") return target->is_injuredall();
    if (status == "inactive") return target->is_inactive();
    if (status == "active") return target->is_active();
    if (status == "dead") return target->is_dead();
    if (status == "alive") return target->is_alive();
    if (status == "fine") return target->is_fine();
    if (status == "analyzable") return target->is_analyzable();
    if (status == "avoidable") return target->is_avoidable();
    if (status == "resistable") return target->is_resistable();
    if (status == "reversed") return target->is_reversed();
    if (status == "vanished") return target->is_vanished();
    if (status == "enhanced_act") return target->is_enhanced_act();
    if (status == "enhanced_res") return target->is_enhanced_res();
    if (status == "enhanced_avo") return target->is_enhanced_avo();
    if (status == "enhanced_def") return target->is_enhanced_def();
    if (status == "upaction") return target->is_upaction();
    if (status == "upresist") return target->is_upresist();
    if (status == "upavoid") return target->is_upavoid();
    if (status == "updefense") return target->is_updefense();
    if (status == "downaction") return target->is_downaction();
    if (status == "downresist") return target->is_downresist();
    if (status == "downavoid") return target->is_downavoid();
    if (status == "downdefense") return target->is_downdefense();
    if (status == "autoselectedpenalty") return target->is_autoselectedpenalty(std::nullopt);
    if (status == "effective")
        throw std::invalid_argument("is_effective() requires header and motion"); // PythonのTypeErrorに相当する。
    if (auto* card = dynamic_cast<::cw::sprite::card::CWPyCard*>(target.get())) {
        if (status == "initialized") return card->is_initialized();
        if (status == "selection") return card->is_selection();
        if (status == "flagtrue") return card->is_flagtrue();
        if (status == "flagtrue_static")
            throw std::invalid_argument("is_flagtrue_static() requires data"); // PythonのTypeErrorに相当する。
    }
    return std::nullopt;
}

S remove_status_wraps(S text)
{
    // Python版のデバッガーのラベルと同様、二文字のエスケープを除き、実際のLFは残す。
    for (std::size_t pos = 0; (pos = text.find("\\n", pos)) != S::npos;) text.erase(pos, 2);
    return text;
}

S background_status_text(S text)
{
    const auto order = ::cw::lib::encoding::ByteOrder::Little;
    if (::cw::lib::encoding::utf8_to_utf32(text, order).size() <= 10 * 4) return text;
    text = remove_status_wraps(std::move(text));
    auto characters = ::cw::lib::encoding::utf8_to_utf32(text, order);
    if (characters.size() > 11 * 4) characters.resize(11 * 4);
    return ::cw::lib::encoding::utf32_to_utf8(characters, order, ::cw::lib::encoding::ErrorPolicy::Strict, false) + "...";
}

S value_string(const V& value)
{
    if (const auto* s = std::get_if<S>(&value.storage())) return *s;
    if (const auto* i = std::get_if<I>(&value.storage())) return std::to_string(*i);
    if (const auto* d = std::get_if<::cw::lib::Decimal>(&value.storage())) return d->to_string(true);
    if (const auto* r = std::get_if<::cw::lib::Real>(&value.storage())) { std::ostringstream os; os << *r; return os.str(); }
    if (const auto* b = std::get_if<bool>(&value.storage())) return *b ? "True" : "False";
    return {};
}

I value_int(const V& value, I fallback = 0)
{
    if (const auto* i = std::get_if<I>(&value.storage())) return *i;
    if (const auto* b = std::get_if<bool>(&value.storage())) return *b ? 1 : 0;
    if (const auto* r = std::get_if<::cw::lib::Real>(&value.storage())) {
        if (!std::isfinite(*r)) return fallback;
        constexpr auto minimum = static_cast<::cw::lib::Real>((std::numeric_limits<I>::lowest)());
        constexpr auto maximum_exclusive = -minimum;
        if (*r < minimum || *r >= maximum_exclusive) return fallback;
        return static_cast<I>(*r);
    }
    if (const auto* d = std::get_if<::cw::lib::Decimal>(&value.storage())) {
        try { return d->to_integer(::cw::lib::DecimalRounding::Down); } catch (...) { return fallback; }
    }
    if (const auto* s = std::get_if<S>(&value.storage())) {
        if (const auto parsed = ::cw::lib::parse_python_integer(*s)) return *parsed;
    }
    return fallback;
}

V stack_trace_frame(const std::shared_ptr<::cw::event::Event>& arg_event,
                    std::shared_ptr<::cw::data::CWPyElement> arg_content,
                    I arg_line_index)
{
    if (!arg_event) return {};
    auto frame = std::make_shared<::cw::debug::debugger::StackTraceView::StackTraceFrame>();
    frame->event = arg_event;
    frame->content = std::move(arg_content);
    frame->line_index = arg_line_index;
    return V(std::static_pointer_cast<::cw::lib::Object>(frame));
}

std::shared_ptr<::cw::event::EventEngine> make_action_event_engine(I arg_update, I arg_touch)
{
    const auto make_event = [](I arg_keynum, I arg_package, const S& arg_start_name) {
        auto event = ::cw::data::make_element("Event");
        auto ignitions = ::cw::data::make_element("Ignitions");
        ignitions->append(::cw::data::make_element("Number", std::to_string(arg_keynum)));
        ignitions->append(::cw::data::make_element("KeyCodes"));
        auto contents = ::cw::data::make_element("Contents");
        auto line = ::cw::data::make_element("ContentsLine");
        line->append(::cw::data::make_element("Start", {}, std::map<S, S>{{"name", arg_start_name}}));
        line->append(::cw::data::make_element(
            "Call", {}, std::map<S, S>{{"type", "Package"}, {"name", arg_start_name}, {"call", std::to_string(arg_package)}}));
        contents->append(line);
        event->append(ignitions);
        event->append(contents);
        return event;
    };
    std::vector<std::shared_ptr<::cw::data::CWPyElement>> events;
    events.push_back(make_event(1, arg_update, "クリック"));
    events.push_back(make_event(2, arg_touch, "スクリーンタッチ"));
    auto engine = std::make_shared<::cw::event::EventEngine>();
    engine->initialize(std::move(events), "Action");
    return engine;
}

bool set_action_mouse_input(const ::cw::lib::Point& arg_pos)
{
    if (!::cw::g_cwpy) return false;
    // Pythonの MouseWatcher と同様、クライアント座標を取得し、 操作ルー
    // プで cw.uns を適用してから SIZE_AREA/MOUSEX/MOUSEY を
    // 判定する。 呼び出し元が固定した開始クリックや、より新しい現在のマウス位置は使わない。
    const auto screen = ::cw::g_cwpy->client_to_screen_point(arg_pos);
    const auto x = value_int(::cw::uns(V(screen.x)), -1);
    const auto y = value_int(::cw::uns(V(screen.y)), -1);
    if (x < 0 || y < 0 || x > std::get<0>(::cw::g_SIZE_AREA) ||
        y > std::get<1>(::cw::g_SIZE_AREA)) return false;
    auto position = std::make_shared<::cw::data::VariantSequence>();
    position->m_values = {V(x), V(y)};
    ::cw::g_cwpy->m__actionevent_input["mouse.x"] = V(x);
    ::cw::g_cwpy->m__actionevent_input["mouse.y"] = V(y);
    ::cw::g_cwpy->m__actionevent_input["mouse.pos"] =
        V(std::static_pointer_cast<::cw::lib::Object>(position));
    return true;
}

[[noreturn]] void rethrow_action_event_error(
    const std::shared_ptr<::cw::event::EventError>& arg_error)
{
    // Event::start は制御用の例外を Event::m_error に保存
    // する。 Python版がここで cardevent.error を再送出するの
    // と同様、 EventErrorへ切り詰めず、具体的なC++の例外型を保持する。
    if (const auto error = std::dynamic_pointer_cast<::cw::event::StartBattleError>(arg_error))
        throw *error;
    if (const auto error = std::dynamic_pointer_cast<::cw::event::ScenarioBadEndError>(arg_error))
        throw *error;
    if (const auto error = std::dynamic_pointer_cast<::cw::event::ScenarioEndError>(arg_error))
        throw *error;
    if (const auto error = std::dynamic_pointer_cast<::cw::event::EffectBreakError>(arg_error))
        throw *error;
    if (const auto error = std::dynamic_pointer_cast<::cw::event::AreaChangeError>(arg_error))
        throw *error;
    if (const auto error = std::dynamic_pointer_cast<::cw::event::ActionBreakError>(arg_error))
        throw *error;
    if (const auto error = std::dynamic_pointer_cast<::cw::event::XmlContentCriticalError>(arg_error))
        throw *error;
    throw *arg_error;
}

bool value_bool(const V& value, bool fallback = false)
{
    if (const auto* b = std::get_if<bool>(&value.storage())) return *b;
    if (const auto* i = std::get_if<I>(&value.storage())) return *i != 0;
    if (const auto* r = std::get_if<::cw::lib::Real>(&value.storage())) return *r != 0.0;
    if (const auto* d = std::get_if<::cw::lib::Decimal>(&value.storage())) return !d->is_zero();
    if (const auto* s = std::get_if<S>(&value.storage())) return *s == "True" || *s == "true" || *s == "1";
    return fallback;
}

std::shared_ptr<::cw::data::CWPyElement> element_of(const V& value)
{
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
    return object && *object ? std::dynamic_pointer_cast<::cw::data::CWPyElement>(*object) : nullptr;
}

std::shared_ptr<::cw::data::ScenarioData> scenario()
{
    if (!::cw::g_cwpy) return {};
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_sdata.storage());
    return object && *object ? std::dynamic_pointer_cast<::cw::data::ScenarioData>(*object) : nullptr;
}

std::shared_ptr<::cw::event::EventInterface> event_interface()
{
    return ::cw::g_cwpy ? ::cw::g_cwpy->m_event : nullptr;
}

I tree_end()
{
    return value_int(::cw::g_IDX_TREEEND, -1);
}

#if defined(CARDWIRTH_LOG_ENABLED)
S scenario_name_for_log()
{
    if (const auto sdata = scenario()) {
        return sdata->m_name;
    }
    return {};
}

S talk_content_context(const std::shared_ptr<::cw::data::CWPyElement>& data)
{
    if (!data) return "cwxpath=none";
    return "path=" + data->get("path", "") + " cwxpath=" + data->get_cwxpath();
}
#endif

V object_value(const std::shared_ptr<::cw::lib::Object>& object)
{
    return object ? V(object) : V{};
}

template<typename T>
std::shared_ptr<T> typed_object(const V& value)
{
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
    return object && *object ? std::dynamic_pointer_cast<T>(*object) : nullptr;
}

V list_value(const std::vector<V>& values)
{
    auto list = std::make_shared<::cw::calculator::ListValue>();
    list->initialize(values, 0, 0);
    return object_value(list);
}

V variant_list_value(const std::vector<V>& values)
{
    auto list = std::make_shared<::cw::data::VariantSequence>();
    list->m_values = values;
    return object_value(list);
}

V struct_value(const S& name, std::vector<std::pair<S, V>> members)
{
    auto result = std::make_shared<::cw::data::StructVal>();
    result->m_name = V(name);
    result->m_member_names.reserve(members.size());
    result->m_member_values.reserve(members.size());
    for (auto& [key, value] : members) {
        result->m_member_names.push_back(std::move(key));
        result->m_member_values.push_back(std::move(value));
    }
    return object_value(result);
}

std::shared_ptr<::cw::frame::Frame> current_frame()
{
    if (!::cw::g_cwpy) return {};
    return typed_object<::cw::frame::Frame>(::cw::g_cwpy->m_frame);
}

S status_name(const S& key)
{
    static const std::map<S, S> names{{"backpack", "荷物袋"}, {"partyandbackpack", "パーティ全体(荷物袋含む)"},
        {"field", "フィールド全体"}, {"couponholder", "称号所有者"}, {"cardtarget", "カードの使用対象"},
        {"selectedcard", "選択カード"}, {"random", "ランダムメンバ"}, {"selected", "選択中メンバ"},
        {"unselected", "選択外メンバ"}, {"inusecard", "使用中カード"}, {"party", "パーティ全体"},
        {"enemy", "敵全体"}, {"npc", "同行キャスト全体"}, {"valued", "評価メンバ"}, {"dex", "器用度"},
        {"agl", "敏捷度"}, {"int", "知力"}, {"str", "筋力"}, {"vit", "生命力"}, {"min", "精神力"},
        {"mental_aggressive", "好戦性"}, {"mental_unaggressive", "平和性"}, {"mental_cheerful", "社交性"},
        {"mental_uncheerful", "内向性"}, {"mental_brave", "勇猛性"}, {"mental_unbrave", "臆病性"},
        {"mental_cautious", "慎重性"}, {"mental_uncautious", "大胆性"}, {"mental_trickish", "狡猾性"},
        {"mental_untrickish", "正直性"},
        {"active", "行動可能"}, {"inactive", "行動不可"}, {"alive", "生存"}, {"dead", "非生存"},
        {"fine", "健康"}, {"injured", "負傷"}, {"heavyinjured", "重傷"}, {"unconscious", "意識不明"},
        {"poison", "中毒"}, {"sleep", "眠り"}, {"bind", "呪縛"}, {"paralyze", "麻痺／石化"},
        {"confuse", "混乱"}, {"overheat", "激昂"}, {"brave", "勇敢"}, {"panic", "恐慌"},
        {"silence", "沈黙"}, {"faceup", "暴露"}, {"antimagic", "魔法無効化"},
        {"upaction", "行動力上昇"}, {"upavoid", "回避力上昇"}, {"upresist", "抵抗力上昇"},
        {"updefense", "防御力上昇"}, {"downaction", "行動力低下"}, {"downavoid", "回避力低下"},
        {"downresist", "抵抗力低下"}, {"downdefense", "防御力低下"}, {"all", "全てのカード"},
        {"skill", "特殊技能カード"}, {"item", "アイテムカード"}, {"beast", "召喚獣カード"}};
    S lowered = key;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const auto it = names.find(lowered); return it == names.end() ? S{} : it->second;
}

bool content_is_checker(const std::shared_ptr<::cw::data::CWPyElement>& element)
{
    if (!element) return false;
    if (element->m_tag == "Check") return true;
    if (element->m_tag != "XmlContent") return false;
    const S xmltype = element->get("xmltype", "");
    return xmltype == "CheckStep" || xmltype == "CheckFlag" || xmltype == "CheckVariant";
}

S upper_ascii(S value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return value;
}

std::shared_ptr<::cw::data::VariantSequence> value_sequence(const V& value)
{
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
    return object && *object ? std::dynamic_pointer_cast<::cw::data::VariantSequence>(*object) : nullptr;
}

std::shared_ptr<::cw::data::SystemData> system_data()
{
    if (!::cw::g_cwpy) return {};
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_sdata.storage());
    return object && *object ? std::dynamic_pointer_cast<::cw::data::SystemData>(*object) : nullptr;
}

// コンテント操作は、任意のネイティブ実行環境・宿の取得処理を共
// 有する。 終了中のゴシップ・終了印の獲得喪失は、空でない名
// 前でも、不在の cw.cwpy を参照せず何もしない場合が
// ある。 この境界で、共通の履歴の所属・変更方針は変えない。
std::shared_ptr<::cw::data::YadoData> yado_data()
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_ydata || !*::cw::g_cwpy->m_ydata) return {};
    return *::cw::g_cwpy->m_ydata;
}

std::shared_ptr<::cw::character::Character> character_target(const V& value)
{
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
    return object && *object ? std::dynamic_pointer_cast<::cw::character::Character>(*object) : nullptr;
}

std::shared_ptr<::cw::sprite::card::CWPyCard> card_target(const V& value)
{
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
    return object && *object ? std::dynamic_pointer_cast<::cw::sprite::card::CWPyCard>(*object) : nullptr;
}

std::shared_ptr<::cw::header::CardHeader> card_header_target(const V& value)
{
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
    return object && *object ? std::dynamic_pointer_cast<::cw::header::CardHeader>(*object) : nullptr;
}

using MessageTalker = std::tuple<std::shared_ptr<::cw::image::ImageInfo>, bool, std::optional<V>,
                                 std::map<I, std::shared_ptr<::cw::lib::Surface>>>;
using MessageTalkers = std::vector<MessageTalker>;

std::vector<std::shared_ptr<::cw::image::ImageInfo>> character_imgpaths(
    const std::shared_ptr<::cw::character::Character>& arg_character)
{
    if (!arg_character) return {};
    if (const auto player = std::dynamic_pointer_cast<::cw::sprite::card::PlayerCard>(arg_character))
        return player->m_imgpaths;
    if (const auto enemy = std::dynamic_pointer_cast<::cw::sprite::card::EnemyCard>(arg_character))
        return enemy->m_imgpaths;
    if (const auto friend_card = std::dynamic_pointer_cast<::cw::sprite::card::FriendCard>(arg_character))
        return friend_card->m_imgpaths;
    return {};
}

bool character_scaledimage(const std::shared_ptr<::cw::character::Character>& arg_character)
{
    if (!arg_character) return false;
    if (arg_character->m_data_typed)
        return arg_character->m_data_typed->getbool(".", "scaledimage", false);
    if (const auto player = std::dynamic_pointer_cast<::cw::sprite::card::PlayerCard>(arg_character)) {
        if (player->m_data_typed && player->m_data_typed->getroot())
            return player->m_data_typed->getroot()->getbool(".", "scaledimage", false);
        return player->m_can_loaded_scaledimage;
    }
    if (const auto enemy = std::dynamic_pointer_cast<::cw::sprite::card::EnemyCard>(arg_character))
        if (enemy->m_data_typed && enemy->m_data_typed->getroot())
            return enemy->m_data_typed->getroot()->getbool(".", "scaledimage", false);
    if (const auto friend_card = std::dynamic_pointer_cast<::cw::sprite::card::FriendCard>(arg_character))
        if (friend_card->m_data_typed && friend_card->m_data_typed->getroot())
            return friend_card->m_data_typed->getroot()->getbool(".", "scaledimage", false);
    return false;
}

std::optional<std::tuple<S, S, bool, bool, bool>> object_versionhint(const V& arg_talker)
{
    if (const auto character = character_target(arg_talker))
        return character->m_versionhint_typed;
    if (const auto header = card_header_target(arg_talker)) {
        if (::cw::g_cwpy && ::cw::g_cwpy->m_sct) {
            if (const auto* hint = std::get_if<S>(&header->m_versionhint.storage()))
                return ::cw::g_cwpy->m_sct->from_basehint(*hint);
        }
    }
    return std::nullopt;
}

S resolve_talker_material_path(S arg_path, const std::shared_ptr<::cw::header::CardHeader>& arg_header)
{
    if (arg_path.empty() || ::cw::binary::image::path_is_code(arg_path)) return arg_path;
    if (!arg_header) return arg_path;
    bool another_scenario = false;
    if (const auto cardimg = arg_header->get_cardimg())
        another_scenario = value_bool(cardimg->m_anotherscenariocard);
    if (!arg_header->m_scenariocard || another_scenario) {
        if (arg_header->m_type == "ActionCard")
            return ::cw::util::get_materialpath(arg_path, ::cw::g_M_IMG, {}, true, true);
        const auto yado_path = ::cw::util::join_yadodir(arg_path);
        if (::cw::util::path_is_file(yado_path)) return yado_path;
        return ::cw::util::get_materialpathfromskin(arg_path, ::cw::g_M_IMG, true);
    }
    // Python版と同様、シナリオカードの話者画像は、 古い可能性のあるカ
    // ードヘッダーの元ディレクトリではなく、現在のシナリオを基準に解決する。
    const auto sdata = system_data();
    const S scedir = sdata ? sdata->m_scedir : S{};
    const auto scenario_path = ::cw::util::join_paths({::cw::lib::Value(scedir), ::cw::lib::Value(arg_path)});
    if (::cw::g_fsync && ::cw::g_fsync->is_waiting(scenario_path)) ::cw::g_fsync->sync();
    if (::cw::util::path_is_file(scenario_path)) return scenario_path;
    return ::cw::util::get_materialpathfromskin(arg_path, ::cw::g_M_IMG, true);
}

void append_message_talker(MessageTalkers& arg_talkers, std::shared_ptr<::cw::image::ImageInfo> arg_info,
                           bool arg_can_scaled, std::optional<V> arg_talker, S arg_basecardtype)
{
    if (!arg_info) return;
    auto info = std::make_shared<::cw::image::ImageInfo>();
    info->initialize(arg_info->m_path, 0, arg_info, "Default", arg_basecardtype);
    arg_talkers.emplace_back(std::move(info), arg_can_scaled, std::move(arg_talker), std::map<I, std::shared_ptr<::cw::lib::Surface>>{});
}

void append_character_talkers(MessageTalkers& arg_talkers, const std::shared_ptr<::cw::character::Character>& arg_character,
                              V arg_talker_value, bool arg_talker_is_card,
                              const std::shared_ptr<::cw::header::CardHeader>& arg_card_header)
{
    const bool can_scaled = arg_talker_is_card && arg_card_header
                                ? arg_card_header->get_can_loaded_scaledimage()
                                : character_scaledimage(arg_character);
    const auto bases = arg_talker_is_card && arg_card_header
                           ? [&] {
                                 std::vector<std::shared_ptr<::cw::image::ImageInfo>> paths;
                                 if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&arg_card_header->m_imgpaths.storage());
                                     object && *object) {
                                     if (const auto seq = std::dynamic_pointer_cast<::cw::data::VariantSequence>(*object)) {
                                         for (const auto& item : seq->m_values) {
                                             if (const auto* path = std::get_if<S>(&item.storage()); path && !path->empty()) {
                                                 auto info = std::make_shared<::cw::image::ImageInfo>();
                                                 info->initialize(*path, 0, std::nullopt, "Default", std::nullopt);
                                                 paths.push_back(std::move(info));
                                             } else if (const auto* info_object = std::get_if<::cw::lib::ObjectPtr>(&item.storage());
                                                        info_object && *info_object) {
                                                 if (const auto info = std::dynamic_pointer_cast<::cw::image::ImageInfo>(*info_object))
                                                     paths.push_back(info);
                                             }
                                         }
                                     }
                                 }
                                 if (paths.empty() && arg_card_header->get_cardimg())
                                     return arg_character ? character_imgpaths(arg_character) : paths;
                                 return paths;
                             }()
                           : character_imgpaths(arg_character);
    for (const auto& base : bases) {
        if (!base) continue;
        S imgpath = base->m_path;
        S basecardtype = "LargeCard";
        if (arg_talker_is_card && arg_card_header) {
            imgpath = resolve_talker_material_path(imgpath, arg_card_header);
        }
        // Pythonの ImageInfo(imgpath, base=base) と同様、PCNumber
        // を素材パスへ解決し、 ルートの基底参照を保持してpcnumberを解除する。
        auto info = std::make_shared<::cw::image::ImageInfo>();
        info->initialize(std::move(imgpath), 0, base, "Default", basecardtype);
        arg_talkers.emplace_back(std::move(info), can_scaled, arg_talker_value, std::map<I, std::shared_ptr<::cw::lib::Surface>>{});
    }
}

struct TalkMessageBuildResult {
    MessageTalkers talkers{};
    std::optional<V> firsttalker{};
    std::shared_ptr<::cw::character::Character> firstchartalker{};
    bool talk{false};
    std::optional<std::tuple<S, S, bool, bool, bool>> versionhint{};
};

TalkMessageBuildResult build_talk_message_talkers(std::shared_ptr<::cw::data::CWPyElement> arg_data, bool arg_singleline)
{
    TalkMessageBuildResult result;
    if (!arg_data) return result;
    if (arg_singleline) {
        // Python版と同様、一行表示では画像パス一覧を空にし、画像がない場合をセリフとして扱う。
        result.talk = true;
        if (const auto sdata = system_data())
            result.versionhint = sdata->get_versionhint(::cw::g_HINT_MESSAGE);
        return result;
    }
    const auto imgpaths = ::cw::image::get_imageinfos(arg_data, false);
    result.talk = imgpaths.empty();
    auto* event = ::cw::g_cwpy ? ::cw::g_cwpy->m_event.get() : nullptr;
    for (const auto& info : imgpaths) {
        if (!info) continue;
        S imgpath = info->m_path;
        bool talker_is_card = false;
        V talker_v{};
        std::shared_ptr<::cw::character::Character> talker_char;
        std::shared_ptr<::cw::header::CardHeader> talker_card;
        if (imgpath.ends_with("??Random")) {
            if (event) talker_v = event->get_targetmember("Random", false, "");
            result.talk = true;
        } else if (imgpath.ends_with("??Selected")) {
            if (event) talker_v = event->get_targetmember("Selected", false, "");
            result.talk = true;
        } else if (imgpath.ends_with("??Unselected")) {
            if (!event) continue;
            talker_v = event->get_targetmember("Unselected", false, "");
            if (talker_v.is_none()) continue;
        } else if (imgpath.ends_with("??Card")) {
            if (!event) continue;
            talker_v = event->get_targetmember("Selectedcard", false, "");
            if (talker_v.is_none()) continue;
            talker_is_card = true;
        }
        talker_char = character_target(talker_v);
        talker_card = card_header_target(talker_v);
        if (talker_char || talker_card) {
            append_character_talkers(result.talkers, talker_char, talker_v, talker_is_card, talker_card);
        } else if (!imgpath.empty()) {
            const auto inuse_path = ::cw::util::get_inusecardmaterialpath(imgpath, ::cw::g_M_IMG, std::nullopt, true);
            bool can_scaled = false;
            if (::cw::util::path_is_file(inuse_path)) {
                imgpath = inuse_path;
                if (event) {
                    const auto inuse = event->get_inusecard();
                    if (inuse && *inuse) {
                        const auto* object =
                            std::get_if<::cw::lib::ObjectPtr>(&(*inuse)->m_carddata.storage());
                        if (object && *object) {
                            if (const auto element = std::dynamic_pointer_cast<::cw::data::CWPyElement>(*object))
                                can_scaled = element->getbool(".", "scaledimage", false);
                            else if (const auto tree = std::dynamic_pointer_cast<::cw::data::CWPyElementTree>(*object);
                                     tree && tree->getroot())
                                can_scaled = tree->getroot()->getbool(".", "scaledimage", false);
                        }
                    }
                }
            } else {
                const bool system = ::cw::g_cwpy && ::cw::g_cwpy->m_areaid < 0;
                imgpath = ::cw::util::get_materialpath(imgpath, ::cw::g_M_IMG, {}, system, true);
                if (system) {
                    can_scaled = true;
                } else if (event && event->m_in_inusecardevent) {
                    const auto inuse = event->get_inusecard();
                    if (inuse && *inuse) {
                        const auto* object =
                            std::get_if<::cw::lib::ObjectPtr>(&(*inuse)->m_carddata.storage());
                        if (object && *object) {
                            if (const auto element = std::dynamic_pointer_cast<::cw::data::CWPyElement>(*object))
                                can_scaled = element->getbool(".", "scaledimage", false);
                            else if (const auto tree = std::dynamic_pointer_cast<::cw::data::CWPyElementTree>(*object);
                                     tree && tree->getroot())
                                can_scaled = tree->getroot()->getbool(".", "scaledimage", false);
                        }
                    }
                } else if (::cw::g_cwpy) {
                    const auto sdata = system_data();
                    if (sdata) can_scaled = sdata->m_can_loaded_scaledimage;
                }
            }
            auto path_info = std::make_shared<::cw::image::ImageInfo>();
            path_info->initialize(imgpath, 0, info, "Default", std::nullopt);
            result.talkers.emplace_back(std::move(path_info), can_scaled, std::nullopt,
                                        std::map<I, std::shared_ptr<::cw::lib::Surface>>{});
        }
        if (!result.firsttalker && (talker_char || talker_card))
            result.firsttalker = talker_v;
        if (!result.firstchartalker && talker_char)
            result.firstchartalker = talker_char;
        result.talk = true;
    }
    if (result.firsttalker) {
        result.versionhint = object_versionhint(*result.firsttalker);
    } else if (const auto sdata = system_data()) {
        result.versionhint = sdata->get_versionhint(::cw::g_HINT_MESSAGE);
    }
    return result;
}

bool can_build_talk_message_talkers(
    const std::shared_ptr<::cw::data::CWPyElement>& data, const bool singleline)
{
    // ネイティブの意図的な拡張。Pythonの can_action
    // は旧path属性だけを見るが、 共通の可否判定は action()
    // と同じ顔画像一覧を使い、複数画像のうち解決できるもの
    // があれば実行可能にする。 一行メッセージは顔画像不要で
    // 、未使用の旧話者が文字・選択肢を隠さないようにする。
    if (!data) return false;
    if (singleline) return true;
    const auto image_paths = ::cw::image::get_imageinfos(data, false);
    if (image_paths.empty()) return true;
    auto* event = ::cw::g_cwpy ? ::cw::g_cwpy->m_event.get() : nullptr;
    for (const auto& info : image_paths) {
        if (!info) continue;
        const auto& path = info->m_path;
        if (path.ends_with("??Random") || path.ends_with("??Selected"))
            return true;
        if (path.ends_with("??Unselected")) {
            if (event && !event->get_targetmember("Unselected", false, "").is_none())
                return true;
            continue;
        }
        if (path.ends_with("??Card")) {
            if (event && !event->get_targetmember("Selectedcard", false, "").is_none())
                return true;
            continue;
        }
        // 明示的に空の旧項目も含め、通常のImagePath
        // は action() と同じ代替分岐へ進み、 顔画
        // 像が解決できなくてもメッセージを実行可能にする。
        return true;
    }
    return false;
}

MessageTalkers build_dialog_talkers(const V& arg_talker)
{
    MessageTalkers talkers;
    const auto character = character_target(arg_talker);
    if (!character) return talkers;
    const bool can_scaled = character_scaledimage(character);
    for (const auto& base : character_imgpaths(character)) {
        if (!base) continue;
        auto info = std::make_shared<::cw::image::ImageInfo>();
        info->initialize(base->m_path, 0, base, "Default", "LargeCard");
        talkers.emplace_back(std::move(info), can_scaled, arg_talker, std::map<I, std::shared_ptr<::cw::lib::Surface>>{});
    }
    return talkers;
}

std::optional<V> message_specialchars()
{
    if (::cw::g_cwpy && ::cw::g_cwpy->m_rsrc) return ::cw::g_cwpy->m_rsrc->m_specialchars;
    return std::nullopt;
}

} // namespace
std::shared_ptr<::cw::data::CWPyElement> ContentElementReference::operator->() const
{
    auto data = lock();
    if (!data) throw std::logic_error("content XML lifetime has ended");
    return data;
}

void EventContentBase::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data, bool arg_is_changestate)
{
    m_data_typed = std::move(arg_data);
    m_is_changestate_typed = arg_is_changestate;
    m_is_changestate = arg_is_changestate;
    m__author.reset();
    m__scenario.reset();
    m__inusecard = false;
    m__init_values = false;
    m_coupons.clear();
}

::cw::lib::Integer EventContentBase::action()
{
    return 0;
}

::cw::lib::Integer EventContentBase::action_with_log()
{
#if defined(CARDWIRTH_LOG_ENABLED)
    S content_class = m_data_typed ? m_data_typed->m_tag + m_data_typed->get("type", "") + "Content" : "<null>";
    S details = "Content action begin: class=" + content_class;
    if (m_data_typed) {
        const S xmltype = m_data_typed->get("xmltype", "");
        const S name = m_data_typed->get("name", "");
        if (!xmltype.empty()) details += " xmltype=" + xmltype;
        if (!name.empty()) details += " name=" + name;
        // ログ・デバッグ用の情報でイベント実行を変えない。 Python版が生
        // 成する行動イベントXMLには、正当にシナリオパスがない場合がある。
        try {
            const S cwxpath = m_data_typed->get_cwxpath();
            if (!cwxpath.empty()) details += " cwxpath=" + cwxpath;
        } catch (const std::exception& error) {
            details += " cwxpath=<unavailable:" + S(error.what()) + ">";
        } catch (...) {
            details += " cwxpath=<unavailable>";
        }
    }
    ::cardwirth::debug::log(details);
#endif
    try {
        const I result = action();
#if defined(CARDWIRTH_LOG_ENABLED)
        ::cardwirth::debug::log("Content action end: class=" + content_class +
                                " result=" + std::to_string(result));
#endif
        return result;
    } catch (...) {
#if defined(CARDWIRTH_LOG_ENABLED)
        ::cardwirth::debug::log("Content action exception: class=" + content_class);
#endif
        throw;
    }
}

bool EventContentBase::can_action()
{
    return true;
}

::cw::lib::String EventContentBase::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    return m_data_typed ? m_data_typed->m_tag + m_data_typed->get("type", "") : S{};
}

::cw::lib::String EventContentBase::get_childname(std::shared_ptr<::cw::data::CWPyElement> arg_child, std::shared_ptr<::cw::event::Event> arg_event)
{
    (void)arg_event;
    return get_contentname(arg_child, {});
}

::cw::lib::String EventContentBase::get_contentname(std::shared_ptr<::cw::data::CWPyElement> arg_child, ::cw::lib::String arg_py_default)
{
    if (!arg_child) return arg_py_default;
    if (arg_child->m_tag == "ContentsLine") {
        const auto children = arg_child->m_children;
        // Python版と同様、名前の検索前に child[0] を参照
        // する。 空のContentsLineでは範囲検査付きの参照で対
        // 応する例外を出し、 渡された名前の代替へ黙って切り替えない。
        return children.at(0)->get("name", arg_py_default);
    }
    return arg_child->get("name", arg_py_default);
}

std::vector<std::shared_ptr<::cw::data::CWPyElement>> EventContentBase::get_children()
{
    const auto event = event_interface();
    const auto current = event ? event->get_event() : std::nullopt;
    if (!current || !*current || !m_data_typed) return {};
    // Python版はイベントツリーの構造に cwxparent を使う。 ネイティ
    // ブで読んだツリーは実行時の弱参照の親だけを持つ場合があるため、代替として残す。
    const auto metadata_parent = m_data_typed->m_cwxparent && *m_data_typed->m_cwxparent
        ? *m_data_typed->m_cwxparent : nullptr;
    const auto runtime_parent = m_data_typed->m_parent.lock();
    const auto parent = metadata_parent ? metadata_parent : runtime_parent;
    if (parent && parent->m_tag == "ContentsLine") {
        // スタートへのリンク・呼び出しは、移動時の制御用行番
        // 号を残したまま現在のイベントツリーを置き換える場合
        // がある。 可能ならコンテントの実位置を使い、ContentsLine
        // 末尾の空Talkが、 内側の選択肢でなく
        // 自身や別の兄弟を表示しないようにする。
        I line = (*current)->m_line_index;
        const auto it = std::find(parent->m_children.begin(), parent->m_children.end(), m_data_typed.lock());
        if (it != parent->m_children.end())
            line = static_cast<I>(std::distance(parent->m_children.begin(), it));
        if (line + 1 < static_cast<I>(parent->m_children.size()))
            return {parent->m_children[static_cast<std::size_t>(line + 1)]};
    }
    const auto found = m_data_typed->find("Contents");
    return found && *found ? (*found)->m_children : std::vector<std::shared_ptr<::cw::data::CWPyElement>>{};
}

::cw::lib::Integer EventContentBase::get_children_num()
{
    return static_cast<I>(get_children().size());
}

std::tuple<::cw::lib::String, ::cw::lib::Value> EventContentBase::get_transitiontype_static(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    if (!arg_data) return {"Default", "Default"};
    const S name = arg_data->get("transition", "Default");
    if (name == "Default") return {name, "Default"};
    const S speed = arg_data->get("transitionspeed", "Default");
    if (const auto parsed = ::cw::lib::parse_python_integer_value(speed)) return {name, *parsed};
    return {name, speed};
}

std::tuple<::cw::lib::String, ::cw::lib::Value> EventContentBase::get_transitiontype()
{
    return get_transitiontype_static(m_data_typed);
}

void EventContentBase::init_values()
{
    if (m__init_values) return;
    m__init_values = true;
    m_initvalue = m_data_typed ? m_data_typed->getint(".", "initialValue", 0) : 0;
    m_coupons.clear();
    if (!m_data_typed) return;
    const auto found = m_data_typed->find("Coupons");
    if (!found || !*found) return;
    for (const auto& e : (*found)->m_children) {
        if (!e) continue;
        const S name = e->m_text;
        m_coupons[name] += e->getint(".", "value", 0);
    }
}

std::optional<std::shared_ptr<::cw::sprite::card::PlayerCard>> EventContentBase::get_valuedmember(::cw::lib::Value arg_mode, bool arg_silenced_member)
{
    if (!m__init_values) init_values();
    if (!::cw::g_cwpy) return {};
    const auto pcards = ::cw::g_cwpy->get_pcards(arg_mode);
    std::vector<std::shared_ptr<::cw::sprite::card::PlayerCard>> candidates;
    I maxvalue = 0;
    for (const auto& card : pcards) {
        if (!card || (!arg_silenced_member && card->is_silence())) continue;
        I value = m_initvalue;
        for (const auto& [coupon, cvalue] : m_coupons) if (card->has_coupon(coupon)) value += cvalue;
        if (value > maxvalue) { maxvalue = value; candidates.clear(); }
        if (value == maxvalue) candidates.push_back(card);
    }
    if (maxvalue <= 0 || candidates.empty()) return {};
    if (!::cw::g_cwpy->m_dice) return candidates.front();
    // Pythonのchoiceと同様、値のある候補が一つだけでも randbelow を消費する。
    const I index = static_cast<I>(::cw::dice::choice_index(candidates.size()));
    return index >= 0 && index < static_cast<I>(candidates.size()) ? candidates[static_cast<std::size_t>(index)] : candidates.front();
}

bool EventContentBase::is_differentscenario(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    if (arg_event && *arg_event) {
        const auto& event = **arg_event;
        if (!event.m_is_active) return false;
        const bool hascard = event.m_inusecard.has_value() && *event.m_inusecard;
        const auto sdata = system_data();
        return hascard && sdata && (event.m_scenario != value_string(sdata->m_name) || event.m_author != value_string(sdata->m_author));
    }
    if (!m__scenario.has_value()) {
        const auto sdata = system_data();
        if (::cw::g_cwpy && ::cw::g_cwpy->is_playingscenario() && ::cw::g_cwpy->m_event) {
            const auto card = ::cw::g_cwpy->m_event->get_inusecard();
            if (card && *card && ::cw::g_cwpy->m_event->m_in_inusecardevent) {
                m__scenario = (*card)->m_scenario; m__author = (*card)->m_author; m__inusecard = true;
            } else { m__scenario = ""; m__author = ""; m__inusecard = false; }
        } else { m__scenario = ""; m__author = ""; m__inusecard = false; }
        (void)sdata;
    }
    const auto sdata = system_data();
    return m__inusecard && sdata && (*m__scenario != value_string(sdata->m_name) || *m__author != value_string(sdata->m_author));
}

void EventContentBase::set_result_variable(::cw::lib::String arg_name, ::cw::lib::Value arg_value)
{
    if (arg_name.empty() || !::cw::g_cwpy) return;
    const auto sdata = system_data();
    if (!sdata) return;
    const auto event = ::cw::g_cwpy->m_event ? ::cw::g_cwpy->m_event->get_nowrunningevent() : std::nullopt;
    const auto variant = sdata->find_variant(arg_name, is_differentscenario(std::nullopt), event);
    if (variant && *variant) (*variant)->set(arg_value, true);
}

std::optional<std::shared_ptr<::cw::character::Character>> EventContentBase::get_variable_target(::cw::lib::String arg_name)
{
    if (!::cw::g_cwpy) return {};
    const auto sdata = system_data();
    if (!sdata) return {};
    const auto event = ::cw::g_cwpy->m_event ? ::cw::g_cwpy->m_event->get_nowrunningevent() : std::nullopt;
    const auto variant = sdata->find_variant(arg_name, is_differentscenario(event), event);
    if (!variant || !*variant) return {};
    const V& value = (*variant)->m_value;
    I raw_index = 0;
    if (const auto* integer = std::get_if<I>(&value.storage())) raw_index = *integer;
    else if (const auto* real = std::get_if<::cw::lib::Real>(&value.storage()); real && std::isfinite(*real) && std::floor(*real) == *real) {
        if (*real < 1 || *real >= -static_cast<::cw::lib::Real>((std::numeric_limits<I>::lowest)())) return {};
        raw_index = static_cast<I>(*real);
    }
    else if (const auto* decimal = std::get_if<::cw::lib::Decimal>(&value.storage())) {
        {
            // Pythonの任意精度整数の参照値は、全カードの位置を超
            // えられる。 C++の位置型へ狭める前にNoneを返す。
            if (*decimal < ::cw::lib::Decimal(1) || *decimal > ::cw::lib::Decimal((std::numeric_limits<I>::max)())) return {};
            raw_index = decimal->to_integer(::cw::lib::DecimalRounding::Down);
            if (*decimal != ::cw::lib::Decimal(raw_index)) return {};
        }}
    else return {};
    if (raw_index < 1) return {};
    I index = raw_index - 1;
    if (index < 0) return {};
    for (const auto& card : ::cw::g_cwpy->get_pcards(V{})) { if (index == 0) return std::dynamic_pointer_cast<::cw::character::Character>(card); if (index > 0) --index; }
    auto enemies = ::cw::g_cwpy->get_ecards(V{}); for (const auto& card : enemies) { if (index == 0) return std::dynamic_pointer_cast<::cw::character::Character>(card); if (index > 0) --index; }
    auto friends = ::cw::g_cwpy->get_fcards(V{}); for (const auto& card : friends) { if (index == 0) return std::dynamic_pointer_cast<::cw::character::Character>(card); if (index > 0) --index; }
    return {};
}

void EventContentBase::update_bg_after()
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_event || ::cw::g_cwpy->m_event->is_stoped()) return;
    ::cw::g_cwpy->stage_game_commands();
    ::cw::g_cwpy->input(false, false, false);
    if (const auto handler = ::cw::g_cwpy->get_eventhandler()) handler->run();
    // Python版と同様、USEREVENT/FORCE_USEREVENT
    // を消費せずに確認し、 キューが空かイベントが再開するまで処理する。
    // 入力フラグはキューへの所属ではなく、64回の打ち切りも設けない。
    while (::cw::thread::peek_pending_events(*::cw::g_cwpy, true) &&
           ::cw::g_cwpy->m_event->is_stoped()) {
        ::cw::g_cwpy->stage_game_commands();
        ::cw::g_cwpy->input(false, false, false);
        if (const auto handler = ::cw::g_cwpy->get_eventhandler()) handler->run();
    }
}

void EventContentBase::variant_error(::cw::lib::String arg_msg, const ::cw::calculator::ComputeException* arg_ex, ::cw::lib::String arg_expression)
{
    S desc = "コモンの処理でエラーが発生しました。";
    if (!arg_msg.empty()) desc += "\n" + arg_msg;
    if (arg_ex) desc += "\n" + variant_error_msg(*arg_ex);
    if (!arg_expression.empty() && ::cw::g_cwpy && ::cw::g_cwpy->m_debug)
        desc += "\n\n式:\n" + arg_expression;
#if defined(CARDWIRTH_LOG_ENABLED)
    ::cardwirth::debug::log("Variant error: " + desc);
#endif
    if (::cw::g_cwpy) {
        // Pythonの play_sound("error") と同様、一回だ
        // け再生する。 SDLミキサーのループ数ゼロは未指定ではなく無限再生。
        ::cw::g_cwpy->play_sound("error", false, 100, 1, 0, 0, false);
        // call_dlg が返すのはユーザーの回答ではなくセッションID。 Python
        // 版と同様、共通のモーダル表示で実際の選択を待つ。 Wasmではホストが
        // ゲームワーカーとUIデータを交換するため、その境界が戻ってからだけ例
        // 外を出し、 JS・UIコールバックやnoexcept内では投げない。
        std::vector<::cw::dialog::message::MessageChoice> choices{
            {"続行", ::cw::dialog::message::kWxIdOk, ::cw::lib::ui::win_i(100), {}},
            {"イベント中止", ::cw::dialog::message::kWxIdCancel, ::cw::lib::ui::win_i(100), {}}};
        // ALL は同じホスト内にエディタを持ち、外部エデ
        // ィタの起動を無効にする。 全構成で共通のダイア
        // ログ内に、無効な選択肢としてその機能差を示す。
        const bool can_open_editor = !::cw::dialog::is_all_mode() &&
            ::cw::g_cwpy->m_setting && !::cw::g_cwpy->m_setting->m_editor.empty();
        const I editor_id = value_int(::cw::debug::debugger::g_ID_EDITOR, -1);
        choices.push_back({"エディタで開く", editor_id, ::cw::lib::ui::win_i(100), {}, can_open_editor});
        auto messages = typed_object<::cw::setting::MsgDict>(::cw::g_cwpy->m_msgs);
        if (!messages) messages = ::cw::g_cwpy->m_setting ? ::cw::g_cwpy->m_setting->m_msgs : ::cw::setting::Setting::s_msgs;
        const auto result = ::cw::dialog::message::show_common_modal_choices(
            messages ? messages->get("message", "メッセージ") : "メッセージ", desc, choices);
#if defined(CARDWIRTH_LOG_ENABLED)
        ::cardwirth::debug::log(S("Variant error decision: ") +
            (result && *result == ::cw::dialog::message::kWxIdOk ? "continue" : "abort"));
#endif
        if (result && can_open_editor && *result == editor_id) {
            auto debugger = ::cw::g_cwpy->is_showingdebugger();
            auto parent = debugger && *debugger
                ? *debugger
                : std::make_shared<::cw::debug::debugger::Debugger>();
            ::cw::debug::debugger::Debugger::exec_editor(
                std::move(parent), m_data_typed.lock());
            throw ::cw::event::EffectBreakError{};
        }
        if (!result || *result != ::cw::dialog::message::kWxIdOk)
            throw ::cw::event::EffectBreakError{};
    }
}

std::map<::cw::lib::String, ::cw::lib::String> EventContentBase::get_textdict()
{
    // このネイティブの公開結果は、一定したキー検索のため std::map
    // を使う。 走査はPython辞書の挿入順ではなく、キー順となる。
    return {{"backpack", "荷物袋"}, {"partyandbackpack", "パーティ全体(荷物袋含む)"}, {"field", "フィールド全体"},
        {"couponholder", "称号所有者"}, {"cardtarget", "カードの使用対象"}, {"selectedcard", "選択カード"},
        {"random", "ランダムメンバ"}, {"selected", "選択中メンバ"}, {"unselected", "選択外メンバ"}, {"inusecard", "使用中カード"},
        {"party", "パーティ全体"}, {"enemy", "敵全体"}, {"npc", "同行キャスト全体"}, {"valued", "評価メンバ"},
        {"dex", "器用度"}, {"agl", "敏捷度"}, {"int", "知力"}, {"str", "筋力"}, {"vit", "生命力"}, {"min", "精神力"},
        {"mental_aggressive", "好戦性"}, {"mental_unaggressive", "平和性"}, {"mental_cheerful", "社交性"},
        {"mental_uncheerful", "内向性"}, {"mental_brave", "勇猛性"}, {"mental_unbrave", "臆病性"},
        {"mental_cautious", "慎重性"}, {"mental_uncautious", "大胆性"}, {"mental_trickish", "狡猾性"},
        {"mental_untrickish", "正直性"}, {"active", "行動可能"}, {"inactive", "行動不可"}, {"alive", "生存"}, {"dead", "非生存"}, {"fine", "健康"},
        {"injured", "負傷"}, {"heavyinjured", "重傷"}, {"unconscious", "意識不明"}, {"poison", "中毒"}, {"sleep", "眠り"},
        {"bind", "呪縛"}, {"paralyze", "麻痺／石化"}, {"confuse", "混乱"}, {"overheat", "激昂"}, {"brave", "勇敢"},
        {"panic", "恐慌"}, {"silence", "沈黙"}, {"faceup", "暴露"}, {"antimagic", "魔法無効化"}, {"upaction", "行動力上昇"},
        {"upavoid", "回避力上昇"}, {"upresist", "抵抗力上昇"}, {"updefense", "防御力上昇"}, {"downaction", "行動力低下"},
        {"downavoid", "回避力低下"}, {"downresist", "抵抗力低下"}, {"downdefense", "防御力低下"}, {"all", "全てのカード"},
        {"skill", "特殊技能カード"}, {"item", "アイテムカード"}, {"beast", "召喚獣カード"}};
}

::cw::lib::Value MouseWatcher::initialize()
{
    std::scoped_lock lock(m_state_mutex);
    m_running = true; m__down = false; m__touch = false; m__suppress_until_release = false;
    m__was_foreground = false; m__last_mouse_pos = list_value({V(I{-1}), V(I{-1})});
    m__last_mouse_button.clear(); m__key_downs.clear(); return {};
}

::cw::lib::Value MouseWatcher::run()
{
#ifdef _WIN32
    while (true) {
        {
            std::scoped_lock lock(m_state_mutex);
            if (!m_running) break;
        }
        const auto frame = current_frame();
        const HWND hwnd = frame ? reinterpret_cast<HWND>(frame->m_native_window_handle) : nullptr;
        const bool foreground = hwnd && ::GetForegroundWindow() == hwnd;
        const bool left = foreground && ((::GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0);
        const bool right = foreground && ((::GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0);
        {
            std::scoped_lock lock(m_state_mutex);
            if (!foreground) {
                m__down = false;
                m__was_foreground = false;
            } else {
                const bool pressed = left || right;
                // Python版と同様、先にフォーカス復帰用の仮の接触を
                // 処理する。 端を含むSIZE_AREA検査で拒否されて
                // も、下の通常の入力は押下を受け付け、自身の位置を読む。
                if (!m__was_foreground && pressed && !m__suppress_until_release) {
                    POINT point{};
                    ::GetCursorPos(&point);
                    ::ScreenToClient(hwnd, &point);
                    if (point.x >= 0 && point.x <= std::get<0>(::cw::g_SIZE_AREA) &&
                        point.y >= 0 && point.y <= std::get<1>(::cw::g_SIZE_AREA)) {
                        m__touch = true;
                        m__last_mouse_pos = list_value({V(static_cast<I>(point.x)), V(static_cast<I>(point.y))});
                        m__last_mouse_button = left ? "left" : "right";
                        m__down = true;
                    }
                }
                if (pressed && !m__down) {
                    if (!m__suppress_until_release) {
                        m__touch = true;
                        m__last_mouse_button = left ? "left" : "right";
                        POINT point{};
                        ::GetCursorPos(&point);
                        ::ScreenToClient(hwnd, &point);
                        m__last_mouse_pos = list_value({V(static_cast<I>(point.x)), V(static_cast<I>(point.y))});
                    }
                    m__down = true;
                } else if (!pressed) {
                    m__down = false;
                }
                m__was_foreground = true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
#else
    while (true) {
        { std::scoped_lock lock(m_state_mutex); if (!m_running) break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
#endif
    return {};
}

::cw::lib::Value MouseWatcher::pop_touch(bool arg_nextlock)
{
    std::scoped_lock lock(m_state_mutex);
    std::vector<std::pair<S, V>> keys;
#ifdef _WIN32
    const auto frame = current_frame();
    const HWND hwnd = frame ? reinterpret_cast<HWND>(frame->m_native_window_handle) : nullptr;
    if (!hwnd || ::GetForegroundWindow() != hwnd) {
        m__suppress_until_release = false; m__touch = false; m__down = false;
    } else if (!m__suppress_until_release) {
        for (int code = 'A'; code <= 'Z'; ++code) {
            S key(1, static_cast<char>(code - 'A' + 'a'));
            const bool down = (::GetAsyncKeyState(code) & 0x8000) != 0;
            const bool previous = value_bool(m__key_downs[key], false);
            if (down && !previous) keys.emplace_back(key, V(true));
            m__key_downs[key] = V(down);
        }
    }
#endif
    const bool touch = !m__suppress_until_release && m__touch;
    const V pos = touch ? m__last_mouse_pos : list_value({V(I{-1}), V(I{-1})});
    const V button = touch && !m__last_mouse_button.empty() ? V(m__last_mouse_button) : V{};
    if (touch) {
        m__touch = false;
        if (arg_nextlock) m__suppress_until_release = true;
    }
    V key_map = struct_value("key", std::move(keys));
    V mouse_map = struct_value("mouse", {{"pos", pos}, {"button", button}, {"touch", V(touch)}});
    return struct_value("MouseInput", {{"key", std::move(key_map)}, {"mouse", std::move(mouse_map)}});
}

void MouseWatcher::release_next_lock()
{
    std::scoped_lock lock(m_state_mutex);
    m__suppress_until_release = false; m__touch = false; m__down = false;
}

void MouseWatcher::reset()
{
    std::scoped_lock lock(m_state_mutex);
    m__suppress_until_release = false; m__touch = false; m__down = false;
}

void ActionLinkStartContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    EventContentBase::initialize(std::move(arg_data), false);
    m_startname = m_data_typed ? m_data_typed->get("link", "") : V{};
}

::cw::lib::Integer ActionLinkStartContent::action()
{
    const auto iface = event_interface();
    const auto event = iface ? iface->get_event() : std::nullopt;
    const auto trees = iface ? iface->get_trees() : std::nullopt;
    if (event && *event && trees) {
        const auto it = trees->find(value_string(m_startname));
        if (it != trees->end()) { (*event)->m_cur_content = it->second; (*event)->m_line_index = 0; }
    }
    return 0;
}

::cw::lib::String ActionLinkStartContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    const S name = m_data_typed ? m_data_typed->get("link", "") : S{};
    return name.empty() ? "スタートコンテントが指定されていません" : "スタートコンテント『" + name + "』へのリンク";
}

void BranchContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    EventContentBase::initialize(std::move(arg_data), false);
    m__boolean_checked = false; m__index_checked = false;
    m__boolean_table = {}; m__index_table = {}; m__index_default = tree_end();
    m_boolean_cache.reset(); m_boolean_checked_cache.clear(); m_index_cache.reset(); m_index_checked_cache.clear();
    m_boolean_cache_checked = false; m_index_cache_checked = false;
    if (!m_data_typed) return;
    m_resid = m_data_typed->getint(".", "id", 0);
    m_num = m_data_typed->getint(".", "number", 0);
    // XML文字列取得の、対象なしを示す空文字列を維持する。 Python
    // の get("targets") はNoneを残すため、後の不正
    // 範囲の診断表記だけが異なる。 有効な範囲を選ぶことにはならない。
    m_scope = m_data_typed->get("targets", "");
    m_selectcard = m_data_typed->getbool(".", "selectcard", false);
    m_invert = m_data_typed->getbool(".", "invert", false);
}

::cw::lib::Integer BranchContent::branch_cards(::cw::lib::String arg_cardtype)
{
    if (is_differentscenario(std::nullopt) || !::cw::g_cwpy) return 0;
    auto sdata = system_data(); if (!sdata) return get_boolean_index(false);
    std::optional<std::shared_ptr<::cw::data::CWPyElement>> data;
    I pocket = ::cw::g_POCKET_SKILL;
    if (arg_cardtype == "SkillCard") { data = sdata->get_skilldata(m_resid, "Property", false, std::nullopt); pocket = ::cw::g_POCKET_SKILL; }
    else if (arg_cardtype == "ItemCard") { data = sdata->get_itemdata(m_resid, "Property", false, std::nullopt); pocket = ::cw::g_POCKET_ITEM; }
    else if (arg_cardtype == "BeastCard") { data = sdata->get_beastdata(m_resid, "Property", false, std::nullopt); pocket = ::cw::g_POCKET_BEAST; }
    else throw std::invalid_argument(arg_cardtype + " is invalid cardtype");
    if (!data || !*data) return get_boolean_index(false);
    const S cardname = (*data)->gettext("Name", S{}), carddesc = (*data)->gettext("Description", S{});
    S scope = value_string(m_scope); bool someone = true;
    if (scope == "Random") { scope = "Party"; someone = true; }
    else if (scope == "Party") someone = false;
    bool flag = false; I cardnum = 0; std::shared_ptr<::cw::character::Character> selected;
    auto event = event_interface();
    if (scope == "SelectedCard") {
        const auto selectedcard = event ? event->get_selectedcard() : std::nullopt;
        bool success = selectedcard && *selectedcard && (*selectedcard)->m_type == arg_cardtype && (*selectedcard)->m_name == cardname && (*selectedcard)->m_desc == carddesc;
        if (m_invert) success = !success;
        flag = success;
        if (success && selectedcard && *selectedcard)
            selected = (*selectedcard)->get_owner_character();
        if (selected && event) event->set_selectedmember(selected);
    } else {
        // Python版と同様、Backpack/PartyAndBackpack
        // にはCharacterと、一つのカードヘッダー一覧を返す。 その形を
        // 保ち、一覧を落として get_targetmember() へ進まな
        // い。 そこでBackpackは意図的に有効なメンバー対象ではない。
        const auto targets = event ? event->get_targetscope(scope, true, false, "")
                                   : std::vector<V>{};
        std::shared_ptr<::cw::header::CardHeader> header;
        bool result = false;
        for (const auto& target : targets) {
            std::vector<std::shared_ptr<::cw::header::CardHeader>> cards;
            const auto character = character_target(target);
            if (character) cards = character->get_pocketcards(pocket);
            else if (const auto sequence = value_sequence(target))
                for (const auto& item : sequence->m_values)
                    if (const auto card = typed_object<::cw::header::CardHeader>(item))
                        cards.push_back(card);
            std::vector<std::shared_ptr<::cw::header::CardHeader>> matches;
            for (const auto& h : cards) if (h && h->m_name == cardname && h->m_desc == carddesc) { if (!header) header = h; matches.push_back(h); }
            cardnum += static_cast<I>(matches.size());
            if (result) continue;
            flag = static_cast<I>(matches.size()) >= m_num;
            if (m_invert) flag = !flag;
            if (flag && someone) { if (character) selected = character; result = true; if (scope != "PartyAndBackpack") break; }
            else if (!flag && !someone) { if (character) selected = character; result = true; if (scope != "PartyAndBackpack") break; }
        }
        if (scope == "PartyAndBackpack") { flag = cardnum >= m_num; if (m_invert) flag = !flag; }
        if (flag && header && m_selectcard && event) event->set_selectedcard(header);
        if (scope != "Selected" && event) {
            if (selected) {
                event->set_selectedmember(selected);
            } else if (!someone) {
                const V random = event->get_targetmember("Random", true, "");
                const auto* object = std::get_if<::cw::lib::ObjectPtr>(&random.storage());
                if (object && *object) {
                    event->set_selectedmember(std::dynamic_pointer_cast<::cw::character::Character>(*object));
                } else {
                    // CardWirth 1.50・Python互換
                    // として、パーティ範囲のカード分岐は、 全員が非
                    // 表示で代替メンバーも選べない場合に成功する。
                    flag = true;
                }
            }
        }
    }
    if (selected && event && scope != "SelectedCard") event->set_selectedmember(selected);
    return get_boolean_index(flag);
}

::cw::lib::Integer BranchContent::get_boolean_index(bool arg_flag)
{
    const I end = tree_end();
    if (m_boolean_cache || !m_boolean_checked_cache.empty()) {
        if (!m_boolean_cache_checked) return arg_flag ? std::get<0>(*m_boolean_cache) : std::get<1>(*m_boolean_cache);
        I index = end; I i = 0;
        for (const auto& [checker, name] : m_boolean_checked_cache) { if (checker && checker->action() != 0) continue; if (arg_flag == name) { index = i; break; } ++i; }
        return index;
    }
    I idx_true = end, idx_false = end, index = 0; m_boolean_checked_cache.clear(); m_boolean_cache_checked = false;
    for (auto element : get_children()) {
        if (!element) continue; if (element->m_tag == "ContentsLine") element = element->get_item(0);
        const S name = element->get("name", "");
        std::shared_ptr<EventContentBase> checker;
        if (content_is_checker(element)) { checker = get_content(element); m_boolean_cache_checked = true; }
        if (name == "○") {
            m_boolean_checked_cache.emplace_back(checker, true);
            if (checker && checker->action() != 0) continue;
            if (idx_true == end) idx_true = index;
        } else if (name == "×") {
            m_boolean_checked_cache.emplace_back(checker, false);
            if (checker && checker->action() != 0) continue;
            if (idx_false == end) idx_false = index;
        }
        ++index;
    }
    if (!m_boolean_cache_checked) m_boolean_cache = std::make_tuple(idx_true, idx_false);
    return arg_flag ? idx_true : idx_false;
}

::cw::lib::Integer BranchContent::get_value_index(::cw::lib::Value arg_value)
{
    const I end = tree_end();
    // Python版と同様、文字列を変換せず整数ラベルと比較する。 bool
    // ・Integer・Decimalと正確な整数のRealは数値
    // として等価だが、他のValue型は整数ラベルに一致しない。
    const auto matches = [&arg_value](I name) {
        const auto& storage = arg_value.storage();
        if (const auto* value = std::get_if<I>(&storage)) return *value == name;
        if (const auto* value = std::get_if<bool>(&storage)) return I(*value) == name;
        if (const auto* value = std::get_if<::cw::lib::Decimal>(&storage))
            return *value == ::cw::lib::Decimal(name);
        if (const auto* value = std::get_if<::cw::lib::Real>(&storage)) {
            // 型変換前にネイティブIntegerの境界を守る。 INT64_MAX
            // を丸めたReal表現を、そのラベルと一致させない。
            const auto limit = std::ldexp(::cw::lib::Real(1), 63);
            if (!std::isfinite(*value) || *value < -limit || *value >= limit) return false;
            const I integer = static_cast<I>(*value);
            return *value == static_cast<::cw::lib::Real>(integer) && integer == name;
        }
        return false;
    };
    if (m_index_cache || !m_index_checked_cache.empty()) {
        if (!m_index_cache_checked) { const auto it = std::find_if(m_index_cache->begin(), m_index_cache->end(), [&matches](const auto& entry) { return matches(entry.first); }); return it == m_index_cache->end() ? value_int(m__index_default, end) : it->second; }
        I index = end, fallback = end, i = 0;
        for (const auto& [checker, name] : m_index_checked_cache) { if (checker && checker->action() != 0) continue; if (matches(name)) { index = i; break; } if (name == -1) fallback = i; ++i; }
        return index == end ? fallback : index;
    }
    std::map<I, I> table; I idx_value = end, idx_default = end, index = 0; m_index_checked_cache.clear(); m_index_cache_checked = false;
    for (auto element : get_children()) {
        if (!element) continue; if (element->m_tag == "ContentsLine") element = element->get_item(0);
        S text = element->get("name", ""); I name = -2; if (text == "Default") name = -1; else if (const auto parsed = ::cw::lib::parse_python_integer(text)) name = *parsed;
        std::shared_ptr<EventContentBase> checker; if (content_is_checker(element)) { checker = get_content(element); m_index_cache_checked = true; }
        if (!table.count(name)) table[name] = index; if (name == -1 && value_int(m__index_default, end) < 0) m__index_default = index;
        m_index_checked_cache.emplace_back(checker, name); if (!checker || checker->action() == 0) { if (idx_value == end && matches(name)) idx_value = index; if (idx_default == end && name == -1) idx_default = index; ++index; }
    }
    if (!m_index_cache_checked) m_index_cache = table;
    return idx_value == end ? idx_default : idx_value;
}

::cw::lib::Integer BranchContent::get_compare_index(::cw::lib::Integer arg_cmptype)
{
    const I end = tree_end(); I less = end, equal = end, greater = end, index = 0;
    for (auto element : get_children()) {
        if (!element) continue; if (element->m_tag == "ContentsLine") element = element->get_item(0);
        if (content_is_checker(element)) { auto checker = get_content(element); if (checker && checker->action() != 0) continue; }
        const S name = element->get("name", ""); if (name == "<" && less == end) less = index; else if (name == "=" && equal == end) equal = index; else if (name == ">" && greater == end) greater = index; ++index;
    }
    return arg_cmptype < 0 ? less : arg_cmptype == 0 ? equal : greater;
}

void CallPackageContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    EventContentBase::initialize(std::move(arg_data), false);
}

::cw::lib::Integer CallPackageContent::action()
{
    // 不完全な実行環境に対する意図的な境界。現在のイベントがなければ戻り先を作れないため直接・リンク経路を使う。 Python版はイベントの存在をassertで要求する。依存先は共通のパッケージ処理で検証する。
    if (is_differentscenario(std::nullopt)) return 0;
    const I resid = m_data_typed ? m_data_typed->getint(".", "call", 0) : 0;
    const auto event = event_interface() ? event_interface()->get_event() : std::nullopt;
    const bool call = event && *event && (!(*event)->m_nowrunningcontents.empty() || get_children_num() > 0);
    call_package(resid, call);
    return 0;
}

::cw::lib::String CallPackageContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    const I resid = m_data_typed ? m_data_typed->getint(".", "call", 0) : 0;
    const auto name = system_data() ? system_data()->get_packagename(resid) : std::nullopt;
    return name ? "パッケージ『" + *name + "』コール" : "パッケージが指定されていません";
}

void CallStartContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    EventContentBase::initialize(std::move(arg_data), false);
    m_startname = m_data_typed ? m_data_typed->get("call", "") : V{};
}

::cw::lib::Integer CallStartContent::action()
{
    // 不完全な実行環境では、インターフェース・ツリー・イベントがなければ何もしない。 Python版のassert・参照エラーを再現しない。現在のコンテントがなければ、 nullのスタック所有者を保存せず、戻り先の記録を省く。
    auto iface = event_interface();
    if (!iface) return 0;
    const auto trees = iface->get_trees();
    if (!trees) return 0;
    const auto it = trees->find(value_string(m_startname));
    if (it == trees->end() || !it->second) return 0;
    const auto event = iface->get_event();
    if (!event || !*event) return 0;
    if (get_children_num() > 0) {
        if (::cw::g_LIMIT_RECURSE <= iface->get_currentstack()) {
            if (::cw::g_cwpy) ::cw::g_cwpy->call_modaldlg("ERROR", {{"text", V("イベントの呼び出しが" + std::to_string(::cw::g_LIMIT_RECURSE) + "層を超えたので処理を中止します。スタートやパッケージのコールによってイベントが無限ループになっていないか確認してください。")}});
            throw ::cw::event::EffectBreakError{};
        }
        if ((*event)->m_cur_content) {
            const auto running = iface->get_nowrunningevent();
            const auto saved_content = *(*event)->m_cur_content;
            const auto saved_line_index = (*event)->m_line_index;
            (*event)->m_nowrunningcontents.emplace_back(std::nullopt, saved_content, saved_line_index, std::nullopt);
            if (running && *running)
                iface->append_stackinfo(stack_trace_frame(*running, saved_content, saved_line_index));
        }
    }
    (*event)->m_cur_content = it->second;
    (*event)->m_line_index = 0;
    return 0;
}

::cw::lib::String CallStartContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    const S name = m_data_typed ? m_data_typed->get("call", "") : "";
    return name.empty() ? "スタートコンテントが指定されていません" : "スタートコンテント『" + name + "』のコール";
}

void ChangeAreaContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    EventContentBase::initialize(std::move(arg_data), true);
}

::cw::lib::Integer ChangeAreaContent::action()
{
    if (is_differentscenario(std::nullopt) || !::cw::g_cwpy || ::cw::g_cwpy->m_in_actionevent) return -1;
    const I resid = m_data_typed ? m_data_typed->getint(".", "id", 0) : 0;
    if (!check_areaid(resid)) return -1;
    const auto ttype = get_transitiontype();
    const auto sdata = system_data();
    const auto name = sdata ? sdata->get_areaname(resid) : std::nullopt;
    if (name) {
        ChangeAreaContent_action_change_area(resid, ttype);
        // 予約済みの遷移は is_runningevent()/is_lockmenucards()
        // で入力を除外済み。 m__dealing は実行中のカードアニメーションの範囲に
        // 属するため、 ここでtrueにして、各範囲が永久にtrueへ戻す状態を作らない。
        throw ::cw::event::AreaChangeError{};
    }
    if (::cw::g_cwpy->m_status == "Yado") {
        ::cw::g_cwpy->play_sound("error", false, 100, 1, 0, 0, false);
    } else {
        end_scenario(false);
    }
    return -1;
}

::cw::lib::String ChangeAreaContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    const I resid = m_data_typed ? m_data_typed->getint(".", "id", 0) : 0;
    const auto name = system_data() ? system_data()->get_areaname(resid) : std::nullopt;
    return name ? "エリア『" + *name + "』へ移動" : "エリアが指定されていません";
}

void ChangeBgImageContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    EventContentBase::initialize(std::move(arg_data), true);
}

::cw::lib::Integer ChangeBgImageContent::action()
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_background || !system_data()) return 0;
    const auto total_started = std::chrono::steady_clock::now();
    if (!m_data_typed) throw std::logic_error("ChangeBgImageContent requires XML data");
    const auto input = m_data_typed->find_exists("BgImages")->m_children;
    const auto elements = system_data()->get_bgdata(input);
    const auto parse_finished = std::chrono::steady_clock::now();
    const auto ttype = get_transitiontype();
    const bool changed = ::cw::g_cwpy->m_background->load(elements, V(true), ttype, true, false, true);
    const auto load_finished = std::chrono::steady_clock::now();
    if (changed && !::cw::g_cwpy->m_in_actionevent)
        ::cw::g_cwpy->wait_frame(1, true, std::nullopt, 30);
    const auto wait_finished = std::chrono::steady_clock::now();
    update_bg_after();
#if defined(CARDWIRTH_LOG_ENABLED)
    const auto update_finished = std::chrono::steady_clock::now();
    const auto elapsed_us = [](const auto& begin, const auto& end) {
        return std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count();
    };
    ::cardwirth::debug::log(
        "Background change timing: total_us=" + std::to_string(elapsed_us(total_started, update_finished)) +
        " parse_us=" + std::to_string(elapsed_us(total_started, parse_finished)) +
        " load_us=" + std::to_string(elapsed_us(parse_finished, load_finished)) +
        " wait_us=" + std::to_string(elapsed_us(load_finished, wait_finished)) +
        " update_us=" + std::to_string(elapsed_us(wait_finished, update_finished)) +
        " elements=" + std::to_string(elements.size()) +
        " changed=" + (changed ? "1" : "0"));
#endif
    return 0;
}

::cw::lib::String ChangeBgImageContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    std::vector<S> values;
    if (m_data_typed) if (const auto bg = m_data_typed->find("BgImages"); bg && *bg) {
        for (const auto& element : (*bg)->m_children) {
            if (!element) continue;
            if (element->m_tag == "BgImage") {
                const auto path = ::cw::util::validate_filepath(element->gettext("ImagePath", "")); if (!path.empty()) values.push_back(path);
            } else if (element->m_tag == "TextCell") {
                S text = background_status_text(element->gettext("Text", ""));
                values.push_back("テキスト「" + text + "」");
            } else if (element->m_tag == "ColorCell") values.push_back("カラーセル");
            else if (element->m_tag == "PCCell") values.push_back("PCイメージ[" + element->gettext("PCNumber", "") + "]");
        }
    }
    S joined; for (const auto& value : values) { if (!joined.empty()) joined += "】【"; joined += value; }
    if (joined.empty()) joined = "無し";
    return "背景 = 【" + joined + "】";
}

void ChangeEnemyContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data, bool arg_is_changestate)
{
    EventContentBase::initialize(std::move(arg_data), arg_is_changestate);
    if (!m_data_typed) return;
    m_cardgroup = m_data_typed->get("cardGroup", m_data_typed->get("cardgroup", ""));
    m_regex = m_data_typed->getbool(".", "regex", false);
    if (const auto enemy = m_data_typed->find("EnemyCard"); enemy && *enemy) {
        m_enemycard_typed = *enemy;
        // Idは Property/Id の要素値。 Propertyの属性と
        // して扱うと、生成したChangeEnemyがすべて黙って-1になる。
        m_enemycard = (*enemy)->getint("Property/Id", std::nullopt, -1);
        m_castid = (*enemy)->getint("Property/Id", std::nullopt, -1);
    } else m_castid = -1;
}

// 移植版のリソース所有者の境界として、CastCard検索を提供す
// る共通のSystemData基底を受け付ける。 Python版
// のScenarioData限定より、意図的に所有者の型を広げる
// 。 実行環境・XML・リソースがなければ、カードは変更しない。
::cw::lib::Integer ChangeEnemyContent::action()
{
    const I castid = value_int(m_castid, -1);
    if (!::cw::g_cwpy || value_string(m_cardgroup).empty() || !m_enemycard_typed || castid < 0) return 0;
    const auto sdata = system_data(); if (!sdata || !sdata->get_castdata(castid, "", true, std::nullopt)) return 0;
    const auto match = [this](const S& actual) {
        if (!m_regex) return actual == value_string(m_cardgroup);
        return ::cw::util::regex_fullmatch(value_string(m_cardgroup), actual);
    };
    bool changed = false;
    std::set<S> changed_groups;
    std::vector<std::shared_ptr<::cw::sprite::card::EnemyCard>> matches;
    for (const auto& value : ::cw::g_cwpy->m_mcards) {
        const auto enemy = typed_object<::cw::sprite::card::EnemyCard>(value);
        if (enemy && match(enemy->m_cardgroup)) matches.push_back(enemy);
    }
    for (const auto& enemy : matches) {
        if (!enemy) continue;
        const S old_group = enemy->m_cardgroup;
        const S old_status = std::static_pointer_cast<::cw::sprite::card::CWPyCard>(enemy)->m_status;
        const I old_index = enemy->m_index;
        const auto oldrect = std::static_pointer_cast<::cw::sprite::card::CWPyCard>(enemy)->m_rect;
        const auto copied = ::cw::data::copydata(m_enemycard_typed, true);
        const auto location = std::make_tuple(copied->getint("Property/Location", "left", 0),
                                              copied->getint("Property/Location", "top", 0));
        if (::cw::g_cwpy->m_selection && *::cw::g_cwpy->m_selection == enemy) ::cw::g_cwpy->clear_selection();
        ::cw::g_cwpy->clear_inusecardimg(enemy);
        enemy->release_animated_images();
        ::cw::sprite::card::remove_from_cardgrp(*enemy);
        std::erase_if(::cw::g_cwpy->m_mcards, [&](const V& value) {
            return typed_object<::cw::sprite::card::EnemyCard>(value) == enemy;
        });
        std::erase_if(::cw::g_cwpy->m_mcards_expandspchars, [&](const V& value) {
            return typed_object<::cw::sprite::card::EnemyCard>(value) == enemy;
        });
        ::cw::g_cwpy->m_file_updates.erase(enemy);
        auto replacement = std::make_shared<::cw::sprite::card::EnemyCard>();
        replacement->initialize(copied, location, "hidden", true, old_index, std::nullopt);
        if (old_status != "hidden" && replacement->is_flagtrue() && replacement->initialize_2()) replacement->deal();
        ::cw::g_cwpy->add_lazydraw_rect(oldrect);
        ::cw::g_cwpy->add_lazydraw_rect(
            std::static_pointer_cast<::cw::sprite::card::CWPyCard>(replacement)->m_rect);
        changed_groups.insert(old_group); changed = true;
    }
    if (changed) {
        if (const auto current_scenario = system_data()) {
            std::erase_if(current_scenario->m_moved_mcards, [&](const auto& pair) { return changed_groups.contains(std::get<0>(pair.first)); });
        }
        ::cw::g_cwpy->_update_mcardlist();
        if (::cw::g_cwpy->m_battle && *::cw::g_cwpy->m_battle) {
            (*::cw::g_cwpy->m_battle)->set_members(); (*::cw::g_cwpy->m_battle)->set_actionorder();
        }
    }
    return 0;
}

::cw::lib::String ChangeEnemyContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    return "カードグループ = " + value_string(m_cardgroup) + " キャストID = " + std::to_string(value_int(m_castid, -1));
}

void ChangeEnvironmentContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    EventContentBase::initialize(std::move(arg_data), true);
    if (m_data_typed) {
        m_backpack = m_data_typed->get("backpack", "NotSet");
        m_gameover = m_data_typed->get("gameover", "NotSet");
        m_runaway = m_data_typed->get("runaway", "NotSet");
    }
}

::cw::lib::Integer ChangeEnvironmentContent::action()
{
    // 不完全な実行環境では、シナリオデータがなければ環境状態を変えない。 Python版の直接参照とは異なり、切り離されたコンテントから不在の所有者を参照しないようにする。
    const auto sdata = system_data();
    if (sdata) {
        const auto backpack = value_string(m_backpack), gameover = value_string(m_gameover), runaway = value_string(m_runaway);
        if (backpack == "Enable") sdata->m_party_environment_backpack = true;
        else if (backpack == "Disable") sdata->m_party_environment_backpack = false;
        if (gameover == "Enable") sdata->m_party_environment_gameover = true;
        else if (gameover == "Disable") sdata->m_party_environment_gameover = false;
        if (runaway == "Enable") sdata->m_party_environment_runaway = true;
        else if (runaway == "Disable") sdata->m_party_environment_runaway = false;
    }
    return 0;
}

::cw::lib::String ChangeEnvironmentContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    std::vector<S> values;
    const auto append = [&](const V& value, const S& name, const S& on, const S& off) {
        const auto state = value_string(value);
        if (state == "Enable") values.push_back(name + " = " + on);
        else if (state == "Disable") values.push_back(name + " = " + off);
    };
    append(m_backpack, "荷物袋", "使用可", "使用不可");
    append(m_gameover, "敗北・ゲームオーバー", "発生有り", "発生無し");
    append(m_runaway, "逃走", "可能", "不可能");
    S result;
    for (const auto& value : values) { if (!result.empty()) result += " "; result += value; }
    return result.empty() ? "状況を変更しない" : result;
}

void CheckFlagContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    EventContentBase::initialize(std::move(arg_data), false);
    m_flag = m_data_typed ? m_data_typed->get("flag", "") : V{};
}

::cw::lib::Integer CheckFlagContent::action()
{
    const auto sdata = system_data();
    const auto event = event_interface() ? event_interface()->get_nowrunningevent() : std::nullopt;
    const auto flag = sdata ? sdata->find_flag(value_string(m_flag), is_differentscenario(std::nullopt), event) : std::nullopt;
    return flag && *flag && (*flag)->m_value ? 0 : tree_end();
}

::cw::lib::String CheckFlagContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    const auto sdata = system_data();
    const auto flag = sdata ? sdata->find_flag(value_string(m_flag), is_differentscenario(arg_event), arg_event) : std::nullopt;
    return flag ? "フラグ『" + value_string(m_flag) + "』の値で判定" : "フラグが指定されていません";
}

void CheckStepContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    EventContentBase::initialize(std::move(arg_data), false);
    if (m_data_typed) {
        m_step = m_data_typed->get("step", "");
        m_value1 = m_data_typed->getint(".", "value", 0);
        m_comparison = m_data_typed->get("comparison", "");
    }
}

::cw::lib::Integer CheckStepContent::action()
{
    const auto sdata = system_data();
    const auto event = event_interface() ? event_interface()->get_nowrunningevent() : std::nullopt;
    const auto step = sdata ? sdata->find_step(value_string(m_step), is_differentscenario(std::nullopt), event) : std::nullopt;
    if (step && *step) {
        const I value = (*step)->m_value;
        const S comparison = value_string(m_comparison);
        if ((comparison == "=" && m_value1 == value) || (comparison == "<>" && m_value1 != value) ||
            (comparison == "<" && m_value1 < value) || (comparison == ">" && m_value1 > value)) return 0;
    }
    return tree_end();
}

::cw::lib::String CheckStepContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    const auto sdata = system_data();
    const auto step = sdata ? sdata->find_step(value_string(m_step), is_differentscenario(arg_event), arg_event) : std::nullopt;
    return step ? std::to_string(m_value1) + " " + value_string(m_comparison) + " ステップ『" + (*step)->m_name + "』" : "ステップが指定されていません";
}

void CheckVariantContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    EventContentBase::initialize(std::move(arg_data), false);
    m_expression = m_data_typed ? m_data_typed->gettext("Expression", "") : S{};
    m_parsed_expression.reset();
}

::cw::lib::Integer CheckVariantContent::action()
{
    // ネイティブの型付きComputeExceptionには、Pythonのprint_ex()
    // 用トレースバックがない。 評価失敗を示す接頭辞を付け、共通のエラーダ
    // イアログ・ビルド設定に従うログへ渡す。 Python版と同様、捕捉
    // するのはComputeExceptionだけで、他の失敗は伝える。
    try {
        if (!m_parsed_expression) m_parsed_expression = ::cw::calculator::parse(m_expression);
        if (m_parsed_expression->empty()) return tree_end();
        auto option = std::make_shared<::cw::calculator::CalcOption>();
        option->initialize("Event", is_differentscenario(std::nullopt));
        const auto variant = ::cw::calculator::eval_expr(*m_parsed_expression, option);
        if (variant && value_string(variant->m_type) == "Boolean") return value_bool(variant->m_value) ? 0 : tree_end();
        if (variant) variant_error("計算結果 " + variant->string_value() + " は真偽値ではありません。", nullptr, m_expression);
        return tree_end();
    } catch (const ::cw::calculator::ComputeException& ex) {
        variant_error("式の評価に失敗しました。", &ex, m_expression);
        return tree_end();
    }
}

::cw::lib::String CheckVariantContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    return m_expression;
}

void EffectBreakContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    EventContentBase::initialize(std::move(arg_data), true);
}

::cw::lib::Integer EffectBreakContent::action()
{
    ::cw::event::EffectBreakError error;
    error.initialize(m_data_typed ? m_data_typed->getbool(".", "consumecard", true) : true);
    throw error;
}

::cw::lib::String EffectBreakContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    return "効果中断コンテント";
}

void EffectContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    EventContentBase::initialize(std::move(arg_data), false);
    if (!m_data_typed) return;
    auto params = std::make_shared<::cw::effectmotion::EffectParams>();
    params->initialize();
    params->set_item("level", m_data_typed->getint(".", "level", 0));
    params->set_item("successrate", m_data_typed->getint(".", "successrate", 0));
    params->set_item("effecttype", m_data_typed->get("effecttype", "Physic"));
    params->set_item("resisttype", m_data_typed->get("resisttype", "Avoid"));
    params->set_item("soundpath", ::cw::util::validate_filepath(m_data_typed->get("sound", "")));
    params->set_item("visualeffect", m_data_typed->get("visual", "None"));
    params->set_item("volume", m_data_typed->getint(".", "volume", 100));
    params->set_item("loopcount", m_data_typed->getint(".", "loopcount", 1));
    params->set_item("fadein", m_data_typed->getint(".", "fadein", 0));
    params->set_item("channel", m_data_typed->getint(".", "channel", 0));
    params->set_item("refability", m_data_typed->getbool(".", "refability", false));
    params->set_item("physical", m_data_typed->get("physical", "Dex"));
    params->set_item("mental", m_data_typed->get("mental", "Aggressive"));
    const S cardspeed = m_data_typed->get("cardspeed", "Default");
    if (cardspeed == "Default") {
        params->set_item("cardspeed", -1);
        params->set_item("overridecardspeed", false);
    } else {
        params->set_item("cardspeed", ::cw::lib::require_python_integer(cardspeed));
        params->set_item("overridecardspeed", m_data_typed->getbool(".", "overridecardspeed", false));
    }
    params->set_item("absorbto", m_data_typed->get("absorbto", "None"));
    m_initialeffect = m_data_typed->getbool(".", "initialeffect", false);
    if (m_initialeffect) {
        m_initialsoundpath = ::cw::util::validate_filepath(m_data_typed->get("initialsound", ""));
        m_initialvolume = m_data_typed->getint(".", "initialvolume", 100);
        m_initialloopcount = m_data_typed->getint(".", "initialloopcount", 1);
        m_initialfadein = m_data_typed->getint(".", "initialfadein", 0);
        m_initialchannel = m_data_typed->getint(".", "initialchannel", 0);
    } else {
        m_initialsoundpath = S{}; m_initialvolume = 100; m_initialloopcount = 1;
        m_initialfadein = 0; m_initialchannel = 0;
    }
    m_eff = std::make_shared<::cw::effectmotion::Effect>();
    // Python版と同様、効果モーションがゼロでもMotionsコンテ
    // ナーを必須とする。 同じ初期化の境界で、不足したXMLを拒否する。
    const auto motions = m_data_typed->find_exists("Motions");
    m_eff->initialize(motions->m_children, params, false);
    m_targetm = m_data_typed->get("targetm", "Selected");
    m_target_var = m_data_typed->get("target_var", "");
    m_ignite = m_data_typed->getbool(".", "ignite", false);
    m_keycodes = m_ignite ? V(m_data_typed->gettext("KeyCodes", "")) : V{};
    m_holdingcoupon = m_data_typed->get("holdingcoupon", "");
}

::cw::lib::Integer EffectContent::action()
{
    // 不完全な実行環境では、インターフェース・効果・実行環境がなければ、切り離された効果は何もしない。 Python版の初期化済み前提とは異なり、不在の所有者の参照を防ぐ。
    auto iface = event_interface();
    if (!iface || !m_eff || !::cw::g_cwpy) return 0;
    std::optional<std::shared_ptr<::cw::character::Character>> selectedmember;
    if (const auto selected = iface->has_selectedmember() ? iface->get_selectedmember() : std::nullopt; selected && *selected) {
        selectedmember = *selected;
        if ((*selected)->get_card_status() == "hidden" && std::dynamic_pointer_cast<::cw::character::Enemy>(*selected)) {
            iface->clear_selectedmember();
            iface->m_is_changestate = true;
            if (value_string(m_target_var).empty() && value_string(m_targetm) == "Selected") return 0;
        }
    }
    V target{};
    if (!value_string(m_target_var).empty()) {
        const auto variable_target = get_variable_target(value_string(m_target_var));
        if (variable_target && *variable_target) target = object_value(std::static_pointer_cast<::cw::lib::Object>(*variable_target));
    }
    else if (value_string(m_targetm) == "CardTarget") {
        const auto effectevent = iface->get_effectevent();
        if (iface->get_inusecard() && effectevent && *effectevent) {
            (*effectevent)->update_targets();
            std::vector<V> effect_targets;
            for (const auto& card : (*effectevent)->m_target_cards)
                if (card) effect_targets.emplace_back(std::static_pointer_cast<::cw::lib::Object>(card));
            // 効果対象には、Party/CouponHolderと同じ型付きの列を使う。
            target = variant_list_value(effect_targets);
        }
    } else {
        const auto targets = iface->get_targetmember(value_string(m_targetm), true, value_string(m_holdingcoupon));
        target = targets;
    }
    m_eff->update_status(selectedmember);
    std::vector<std::shared_ptr<::cw::sprite::card::CWPyCard>> targets;
    auto append_target = [&](const V& value) {
        if (auto card = card_target(value)) targets.push_back(card);
    };
    if (auto sequence = value_sequence(target)) for (const auto& value : sequence->m_values) append_target(value);
    else append_target(target);
    if (targets.empty()) iface->m_is_changestate = true;

    const auto event = iface->get_event();
    const auto keycodes = ::cw::util::decodetextlist(value_string(m_keycodes));
    auto scenario_active = []() {
        return ::cw::g_cwpy && ::cw::g_cwpy->is_playingscenario() && scenario() && !scenario()->m_in_f9;
    };
    auto apply_target = [&](const std::shared_ptr<::cw::sprite::card::CWPyCard>& member) {
        if (!member) return;
        if (auto character = std::dynamic_pointer_cast<::cw::character::Character>(member)) {
            iface->m_is_changestate = true;
            const auto status = ::cw::event::get_effecttargetstatus(
                object_value(std::static_pointer_cast<::cw::lib::Object>(character)), m_eff);
            if (character->is_unconscious() && !std::get<0>(status)) {
                if (m_ignite) character->remove_coupon("＠効果対象");
                return;
            }
            if (m_ignite) character->set_coupon("＠イベント対象", 0);
            struct EventTargetCleanup {
                std::shared_ptr<::cw::character::Character> character;
                bool active;
                void remove() { if (!active) return; active = false; character->remove_coupon("＠イベント対象"); }
                ~EventTargetCleanup() noexcept(false) { remove(); }
            } cleanup{character, m_ignite};
            try {
                if (m_ignite && event && *event) {
                    const auto runevent = (*event)->ignition_characterevent(character, std::get<0>(status), keycodes);
                    if (runevent && *runevent) {
                        (*runevent)->run_scenarioevent(true);
                        if (!scenario_active()) {
                            character->remove_coupon("＠効果対象");
                            return;
                        }
                        if (const auto targeting = iface->get_effectevent(); targeting && *targeting)
                            (*targeting)->update_targets();
                        if (!character->has_coupon("＠効果対象")) return;
                        if (!m_eff->check_enabledtarget(
                                object_value(std::static_pointer_cast<::cw::lib::Object>(character)), false)) {
                            character->remove_coupon("＠効果対象");
                            return;
                        }
                    }
                }
                const bool wasdead = character->is_unconscious() || character->is_paralyze();
                const bool success = m_eff->apply(
                    object_value(std::static_pointer_cast<::cw::lib::Object>(character)), !m_ignite, selectedmember);
                if (m_ignite) character->remove_coupon("＠効果対象");
                bool deadevent = false;
                if (m_ignite && event && *event && !wasdead) {
                    const auto dead = (*event)->ignition_deadevent(character, keycodes);
                    if (dead && *dead) {
                        deadevent = true;
                        (*dead)->run_scenarioevent(true);
                        if (!scenario_active()) return;
                    }
                }
                if (m_ignite && !deadevent && event && *event) {
                    const auto success_event = (*event)->ignition_successevent(character, success, keycodes);
                    if (success_event && *success_event) (*success_event)->run_scenarioevent(true);
                }
            } catch (...) {
                cleanup.remove();
                throw;
            }
        } else if (auto menu = std::dynamic_pointer_cast<::cw::sprite::card::MenuCard>(member)) {
            ::cw::g_cwpy->play_sound_with(value_string(m_eff->m_soundpath), std::nullopt,
                value_int(m_eff->m_volume), value_int(m_eff->m_loopcount),
                value_int(m_eff->m_channel), value_int(m_eff->m_fade));
            // メニューへの効果では、Pythonの animate(target) の既
            // 定 update_image=false と同様、既存のカード画像を使う。
            m_eff->animate(member, false);
            if (const auto targeting = iface->get_effectevent(); targeting && *targeting)
                (*targeting)->m_mcards.erase(member);
            std::optional<std::shared_ptr<::cw::event::Event>> runevent;
            if (m_ignite && event && *event) runevent = (*event)->ignition_menucardevent(menu, keycodes);
            if (runevent && *runevent) (*runevent)->run_scenarioevent(true);
            else {
                ::cw::g_cwpy->play_sound("ineffective", true, 100, 1, 0, 0, false);
                if (::cw::g_cwpy->m_advlog)
                    ::cw::g_cwpy->m_advlog->effect_failed(
                        object_value(std::static_pointer_cast<::cw::lib::Object>(member)), true);
            }
        } else {
            m_eff->animate(member, true);
        }
    };
    if (m_ignite) {
        std::optional<std::shared_ptr<::cw::event::Targeting>> old_effectevent = iface->m_effectevent;
        // 中断中の外側の対象選択状態には通知しない。
        iface->m_effectevent.reset();
        std::vector<std::shared_ptr<::cw::sprite::card::CWPyCard>> saved_targets;
        std::vector<std::shared_ptr<::cw::character::Character>> saved_outoftargets;
        std::shared_ptr<::cw::character::Character> saved_eventtarget;
        std::set<std::shared_ptr<::cw::sprite::card::CWPyCard>> saved_mcards;
        if (old_effectevent && *old_effectevent) {
            (*old_effectevent)->update_targets();
            saved_targets = (*old_effectevent)->m_target_cards;
            saved_mcards = (*old_effectevent)->m_mcards;
            for (const auto& card : (*old_effectevent)->m_coupon_owners) {
                if (auto character = std::dynamic_pointer_cast<::cw::character::Character>(card)) {
                    if (character->has_coupon("＠効果対象外")) saved_outoftargets.push_back(character);
                    if (character->has_coupon("＠イベント対象")) saved_eventtarget = character;
                    character->remove_coupon("＠効果対象");
                    character->remove_coupon("＠効果対象外");
                    character->remove_coupon("＠イベント対象");
                }
            }
        } else {
            auto take_eventtarget = [&](const auto& cards) {
                if (saved_eventtarget) return;
                for (const auto& card : cards) {
                    if (!card || !card->has_coupon("＠イベント対象")) continue;
                    saved_eventtarget = card;
                    card->remove_coupon("＠イベント対象");
                    return;
                }
            };
            take_eventtarget(::cw::g_cwpy->get_pcards(V{}));
            take_eventtarget(::cw::g_cwpy->get_ecards(V{}));
            take_eventtarget(::cw::g_cwpy->get_fcards(V{}));
        }
        auto tevent = std::make_shared<::cw::event::Targeting>();
        tevent->initialize(std::nullopt, targets, m_initialeffect);
        if (!saved_mcards.empty() && value_string(m_targetm) == "CardTarget") tevent->m_mcards = saved_mcards;
        iface->m_effectevent = tevent;
        tevent->targets_to_coupon();
        auto restore_nested = [&]() {
            tevent->clear_eventcoupons();
            if (old_effectevent && *old_effectevent) {
                for (const auto& card : saved_targets)
                    if (auto character = std::dynamic_pointer_cast<::cw::character::Character>(card))
                        character->set_coupon("＠効果対象", 0);
                for (const auto& character : saved_outoftargets)
                    if (character) character->set_coupon("＠効果対象外", 0);
                if (saved_eventtarget) saved_eventtarget->set_coupon("＠イベント対象", 0);
                if (value_string(m_targetm) == "CardTarget") (*old_effectevent)->m_mcards = tevent->m_mcards;
                iface->m_effectevent = old_effectevent;
            } else if (saved_eventtarget) {
                saved_eventtarget->set_coupon("＠イベント対象", 0);
            }
        };
        try {
            if (const auto sdata = system_data(); sdata && sdata->m_events && *sdata->m_events) {
                const auto runevent = (*sdata->m_events)->check_keycodes(keycodes, false);
                if (runevent && *runevent) (*runevent)->run_scenarioevent(true);
            }
            tevent->m_waited = true;
            if (m_initialeffect) {
                ::cw::lib::Callback remove_cb = [tevent](const ::cw::lib::Arguments& args, const ::cw::lib::KeywordArguments&) -> V {
                    if (args.empty()) return {};
                    if (auto card = card_target(args[0])) {
                        if (auto character = std::dynamic_pointer_cast<::cw::character::Character>(card)) {
                            character->remove_coupon("＠効果対象");
                            tevent->remove_target(character);
                        }
                    }
                    return {};
                };
                ::cw::event::initial_effect(m_eff, targets, false, value_string(m_initialsoundpath),
                    m_initialvolume, m_initialloopcount, m_initialchannel, m_initialfadein, remove_cb);
            }
            while (true) {
                const auto member = tevent->get_nexttarget();
                if (!member) break;
                apply_target(*member);
                if (!scenario_active()) break;
            }
        } catch (...) {
            restore_nested();
            throw;
        }
        restore_nested();
    } else {
        if (m_initialeffect) {
            ::cw::lib::Callback remove_cb = [](const ::cw::lib::Arguments& args, const ::cw::lib::KeywordArguments&) -> V {
                if (args.empty()) return {};
                if (auto card = card_target(args[0]))
                    if (auto character = std::dynamic_pointer_cast<::cw::character::Character>(card))
                        character->remove_coupon("＠効果対象");
                return {};
            };
            targets = ::cw::event::initial_effect(m_eff, targets, false, value_string(m_initialsoundpath),
                m_initialvolume, m_initialloopcount, m_initialchannel, m_initialfadein, remove_cb);
        }
        for (const auto& member : targets) {
            if (!member) continue;
            apply_target(member);
            member->clear_cardtarget();
            if (!scenario_active()) break;
        }
    }
    if (::cw::g_cwpy->is_gameover() && system_data() && system_data()->can_gameover()) {
        ::cw::event::EffectBreakError error; error.initialize(true); throw error;
    }
    return 0;
}

::cw::lib::String EffectContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    static const std::map<S, S> names{{"Heal", "回復"}, {"Damage", "ダメージ"}, {"Absorb", "吸収"},
        {"Paralyze", "麻痺状態"}, {"DisParalyze", "麻痺解除"}, {"Poison", "中毒状態"},
        {"DisPoison", "中毒解除"}, {"GetSkillPower", "精神力回復"}, {"LoseSkillPower", "精神力不能"},
        {"Sleep", "睡眠状態"}, {"Confuse", "混乱状態"}, {"Overheat", "激昂状態"}, {"Brave", "勇敢状態"},
        {"Panic", "恐慌状態"}, {"Normal", "正常状態"}, {"Bind", "束縛状態"}, {"DisBind", "束縛解除"},
        {"Silence", "沈黙状態"}, {"DisSilence", "沈黙解除"}, {"FaceUp", "暴露状態"}, {"FaceDown", "暴露解除"},
        {"AntiMagic", "魔法無効化状態"}, {"DisAntiMagic", "魔法無効化解除"}, {"EnhanceAction", "行動力変化"},
        {"EnhanceAvoid", "回避力変化"}, {"EnhanceResist", "抵抗力変化"}, {"EnhanceDefense", "防御力変化"},
        {"VanishTarget", "対象消去"}, {"VanishCard", "カード消去"}, {"VanishBeast", "召喚獣消去"},
        {"DealAttackCard", "通常攻撃"}, {"DealPowerfulAttackCard", "渾身の一撃"}, {"DealCriticalAttackCard", "会心の一撃"},
        {"DealFeintCard", "フェイント"}, {"DealDefenseCard", "防御"}, {"DealDistanceCard", "見切り"},
        {"DealConfuseCard", "混乱"}, {"DealSkillCard", "特殊技能"}, {"CancelAction", "行動キャンセル"},
        {"SummonBeast", "召喚獣召喚"}};
    S targetm = !value_string(m_target_var).empty() ? "コモン『" + value_string(m_target_var) + "』の参照先" : status_name(value_string(m_targetm));
    if (value_string(m_target_var).empty() && value_string(m_targetm) == "CouponHolder") {
        const auto coupon = value_string(m_holdingcoupon);
        targetm += coupon.empty() ? "(指定なし)" : "(" + coupon + ")";
    }
    std::vector<S> motions;
    if (m_data_typed) if (const auto e = m_data_typed->find("Motions"); e && *e)
        for (const auto& motion : (*e)->m_children) if (motion) motions.push_back(names.count(motion->get("type", "")) ? names.at(motion->get("type", "")) : S{});
    // Python版と同様、不明なモーションの空ラベルも含め、一覧の位置に基づいて
    // 連結する。 「なし」のラベルを使うのは、モーション一覧自体が空の場合だけ。
    S seq;
    for (std::size_t i = 0; i < motions.size(); ++i) {
        if (i != 0) seq += "】【";
        seq += motions[i];
    }
    if (motions.empty()) seq = "無し";
    return targetm + "への効果【" + seq + "】";
}

void ElapseTimeContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    EventContentBase::initialize(std::move(arg_data), true);
}

::cw::lib::Integer ElapseTimeContent::action()
{
    if (::cw::g_cwpy) ::cw::g_cwpy->elapse_time(true, true);
    return 0;
}

::cw::lib::String ElapseTimeContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    return "ターン数経過コンテント";
}

void EndActionEventContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    EventContentBase::initialize(std::move(arg_data), false);
    m_data = m_data_typed ? m_data_typed->get("data", "") : V{};
}

::cw::lib::Integer EndActionEventContent::action()
{
    // Python版は常に ActionBreakError を投げ、ActionEvent の親イベントへ
    // EventEngine の復帰処理を委譲する。C++ 側も戻り値で擬似終了させず、
    // 同じ型付き制御例外を上位へ伝播させる。
    throw ::cw::event::ActionBreakError{"EndActionEvent"};
}

::cw::lib::String EndActionEventContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    return "EndActionEvent";
}

void EndBadEndContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    EventContentBase::initialize(std::move(arg_data), true);
}

::cw::lib::Integer EndBadEndContent::action()
{
    // ノベルモードは、シナリオ終了・ゲームオーバーを抑止するアプリの選択
    // 機能（docs/NOVEL_MODE.md）。 0を返して次のコ
    // ンテントへ進み、Python版の終了後処理・宿への遷移を行わない
    // 。 通常のシナリオモードはPython互換の終了経路を維持する。
    if (::cw::novel::enabled()) return 0;
    if (!::cw::g_cwpy || ::cw::g_cwpy->m_in_actionevent) return -1;
    ::cw::g_cwpy->set_gameoverstatus(true, true);
    const auto sdata = scenario();
    const bool old_gameover = sdata && ::cw::g_cwpy->m_sct && ::cw::g_cwpy->m_sct->lessthan("1.15", sdata->get_versionhint(0));
    if (old_gameover || !::cw::g_cwpy->is_battlestatus()) {
        // Python版と同様、set_gameover を予約してからイベントの中
        // 断を送出する。 有効なコンテントのスタック内でシーンを置き換えず、 Event::end
        // が以前のシナリオで後処理・敗北処理を終えられるようにする。
        const auto engine=::cw::g_cwpy;
        engine->exec_func([engine](const ::cw::lib::Arguments&,const ::cw::lib::KeywordArguments&) {
            engine->set_gameover();
            return ::cw::lib::Value{};
        });
    }
    throw ::cw::event::ScenarioBadEndError{};
}

::cw::lib::String EndBadEndContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    return "パーティ全滅";
}

void EndContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    EventContentBase::initialize(std::move(arg_data), true);
}

::cw::lib::Integer EndContent::action()
{
    // EndBadEndと同じ、選択されたノベルモードの終了方針。 通
    // 常のシナリオ終了・宿の後処理をせず、0を返してイベントを続ける。
    if (::cw::novel::enabled()) return 0;
    if (!::cw::g_cwpy || ::cw::g_cwpy->m_in_actionevent) return -1;
    end_scenario(m_data_typed ? m_data_typed->getbool(".", "complete", false) : false);
    return -1;
}

::cw::lib::String EndContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    return (m_data_typed && m_data_typed->getbool(".", "complete", false)) ? "済印をつけて終了" : "済印をつけずに終了";
}

void GetContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    EventContentBase::initialize(std::move(arg_data), true);
}

void GetContent::get_cards(::cw::lib::String arg_cardtype, std::optional<std::shared_ptr<::cw::header::CardHeader>> arg_header_for_update_variables, std::optional<std::vector<std::shared_ptr<::cw::header::CardHeader>>> arg_ignore_list_for_update_variables)
{
    if (is_differentscenario(std::nullopt)) return;
    const auto yado = yado_data();
    const auto sdata = system_data();
    if (!yado || !yado->m_party || !*yado->m_party || !sdata || !event_interface()) return;
    const I resid = m_data_typed ? m_data_typed->getint(".", "id", 0) : 0;
    const I number = m_data_typed ? m_data_typed->getint(".", "number", 0) : 0;
    S scope = m_data_typed ? m_data_typed->get("targets", "") : S{};
    if (scope == "Field" || scope == "PartyAndBackpack") scope = "Backpack";
    std::function<std::optional<std::shared_ptr<::cw::data::CWPyElement>>(I, bool)> getter;
    if (arg_cardtype == "SkillCard") getter = [sdata](I id, bool nocache) { return sdata->get_skilldata(id, "", nocache, std::nullopt); };
    else if (arg_cardtype == "ItemCard") getter = [sdata](I id, bool nocache) { return sdata->get_itemdata(id, "", nocache, std::nullopt); };
    else if (arg_cardtype == "BeastCard") getter = [sdata](I id, bool nocache) { return sdata->get_beastdata(id, "", nocache, std::nullopt); };
    else throw std::invalid_argument(arg_cardtype + " is invalid cardtype");

    auto is_nocache = [&](const V& target) {
        if (value_sequence(target)) return false;
        if (auto character = character_target(target)) {
            const auto space = character->get_cardpocketspace();
            const I pocket = arg_cardtype == "SkillCard" ? ::cw::g_POCKET_SKILL : arg_cardtype == "ItemCard" ? ::cw::g_POCKET_ITEM : ::cw::g_POCKET_BEAST;
            const auto cards = character->get_pocketcards(pocket);
            const I capacity = pocket == ::cw::g_POCKET_SKILL ? std::get<0>(space) : pocket == ::cw::g_POCKET_ITEM ? std::get<1>(space) : std::get<2>(space);
            return capacity > static_cast<I>(cards.size());
        }
        return true;
    };

    auto event = event_interface();
    if (scope == "SelectedCard") {
        const auto selected = event->get_selectedcard();
        if (!selected || !*selected) return;
        const auto owner_value = (*selected)->get_owner();
        const auto owner = owner_value ? character_target(*owner_value) : nullptr;
        const bool backpack_owner = owner_value && value_string(*owner_value) == "BACKPACK" && yado->m_party && *yado->m_party;
        const bool storehouse_owner = owner_value && value_string(*owner_value) == "STOREHOUSE";
        V target;
        if (owner) {
            target = object_value(std::static_pointer_cast<::cw::lib::Object>(owner));
        } else if (backpack_owner) {
            std::vector<V> items;
            for (const auto& header : (*yado->m_party)->m_backpack) items.push_back(object_value(header));
            target = list_value(items);
        } else if (storehouse_owner) {
            // Python版は現在の袋の一覧を返す。 ネイティブは
            // カード置場を識別値で表し、共通の交換境界で解決する。
            target = V("STOREHOUSE");
        } else {
            throw std::logic_error("SelectedCard requires a known owner");
        }
        const bool nocache = is_nocache(target);
        const auto data = getter(resid, nocache);
        if (!data || !*data || (*data)->m_tag != (*selected)->m_type) return;
        if ((*selected)->m_type == "ActionCard")
            throw std::logic_error("SelectedCard replacement does not support ActionCard");
        // Python版と同様、seq.index より前に元XML
        // を読み込み・検証する。 元がない・タグが違う場合は、古い所
        // 有者の例外なしで戻る。 有効なら移動前に未所属を拒否し、
        // 末尾追加の-1で古いSelectedCardを隠さない。
        const auto require_index = [&selected](const auto& cards) -> I {
            const auto it = std::find(cards.begin(), cards.end(), *selected);
            if (it == cards.end()) throw std::invalid_argument("SelectedCard is not in its owner collection");
            return static_cast<I>(std::distance(cards.begin(), it));
        };
        I toindex = -1;
        if (owner) {
            I pocket;
            if ((*selected)->m_type == "SkillCard") pocket = ::cw::g_POCKET_SKILL;
            else if ((*selected)->m_type == "ItemCard") pocket = ::cw::g_POCKET_ITEM;
            else if ((*selected)->m_type == "BeastCard") pocket = ::cw::g_POCKET_BEAST;
            else throw std::logic_error("SelectedCard has no character pocket");
            toindex = require_index(owner->get_pocketcards(pocket));
        } else if (backpack_owner) {
            toindex = require_index((*yado->m_party)->m_backpack);
        } else {
            toindex = require_index(yado->m_storehouse);
        }
        const I insertorder = value_int((*selected)->m_order, -1);
        ::cw::g_cwpy->trade("TRASHBOX", std::nullopt, *selected, true, nullptr, -1, -1, false, false, std::nullopt, false, false, true, true, 0);
        const auto tree = ::cw::data::xml2etree("", "", std::nullopt, *data, nocache);
        // 選択カードの置換でも通常の獲得と同様、呼び出し元が明示し
        // た変数移行用ヘッダーを意図的に使う。 Python版は
        // この分岐で省略するが、ネイティブ版は指定ヘッダーから一
        // 致変数を復元できる。 指定がなければ両経路は一致する。
        get_card(tree, target, false, toindex, insertorder, std::nullopt, {}, false, true, false, true, false, arg_header_for_update_variables, arg_ignore_list_for_update_variables);
        return;
    }
    for (I count = 0; count < number; ++count) {
        for (const auto& target : event->get_targetscope(scope, true, true, "")) {
            const bool nocache = is_nocache(target);
            auto data = getter(resid, nocache);
            if (!data || !*data) return;
            // ネイティブの意図的な所有権の境界として、空でも変数コンテ
            // ナーがあれば、インスタンス所有の編集可能なルートを使う。
            // Python版は空でない場合だけ複製するため、空の場合
            // はルート・可変ノードの複製が追加される。 不変の子孫はcopydata
            // の共有方針を維持し、XML値は同じにする。
            const bool has_variables = ((*data)->find("Flags") && *(*data)->find("Flags")) ||
                ((*data)->find("Steps") && *(*data)->find("Steps")) || ((*data)->find("Variants") && *(*data)->find("Variants"));
            if (has_variables) data = ::cw::data::copydata(*data, true);
            const auto tree = ::cw::data::xml2etree("", "", std::nullopt, *data, nocache || has_variables);
            get_card(tree, target, false, -1, -1, std::nullopt, {}, false, true, false, true, false, arg_header_for_update_variables, arg_ignore_list_for_update_variables);
        }
    }
}

void HidePartyContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    EventContentBase::initialize(std::move(arg_data), false);
    const S speed = m_data_typed ? m_data_typed->get("cardspeed", "Default") : "Default";
    m_cardspeed = speed == "Default" ? -1 : ::cw::lib::require_python_integer(speed);
}

::cw::lib::Integer HidePartyContent::action()
{
    if (!::cw::g_cwpy) return 0;
    const V old = ::cw::g_cwpy->m_override_dealspeed;
    try {
        if (m_cardspeed != -1) ::cw::g_cwpy->m_override_dealspeed = m_cardspeed;
        ::cw::g_cwpy->hide_party();
    } catch (...) { ::cw::g_cwpy->m_override_dealspeed = old; throw; }
    ::cw::g_cwpy->m_override_dealspeed = old;
    return 0;
}

::cw::lib::String HidePartyContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    return "パーティ非表示コンテント";
}

void InputDialogContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data, ::cw::lib::String arg_title, ::cw::lib::String arg_message)
{
    EventContentBase::initialize(std::move(arg_data), true); m_title = arg_title; m_message = arg_message;
}

::cw::lib::Integer InputDialogContent::action()
{
    if (!m_data_typed || m_data_typed->m_tag != "InputDialog") throw std::invalid_argument("InputDialog XML is required");
    const S name = m_data_typed->getattr(".", "Variable", m_data_typed->getattr(".", "variable", ""));
    const auto event = event_interface() ? event_interface()->get_nowrunningevent() : std::nullopt; const auto variant = system_data() ? system_data()->find_variant(name, false, event) : std::nullopt;
    if (!variant || !*variant)
        throw ::cw::event::XmlContentCriticalError(
            "InputDialogの格納先変数『" + name + "』が見つかりません。");
    const I width = m_data_typed->getint(".", "width", 318);
    const I height = m_data_typed->getint(".", "height", 180);
    const S initial = m_data_typed->getattr(".", "default", "");
    if (const auto value = InputDialogContent_action_show_dialog(value_string(m_title), value_string(m_message), initial, {width, height}))
        (*variant)->set(V(*value), true);
    return 0;
}

void LinkPackageContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    EventContentBase::initialize(std::move(arg_data), false);
    m_resid = m_data_typed ? m_data_typed->getint(".", "link", 0) : 0;
}

::cw::lib::Integer LinkPackageContent::action()
{
    // 不完全な実行環境で現在のイベントがなければ、置換する実行枠がないためリンクは何もしない。 Python版のassertは再現せず、通常経路では共通のパッケージ処理を使う。
    if (!is_differentscenario(std::nullopt)) {
        const auto iface = event_interface();
        const auto event = iface ? iface->get_event() : std::nullopt;
        if (event && *event) call_package(m_resid, !(*event)->m_nowrunningcontents.empty());
    }
    return 0;
}

::cw::lib::String LinkPackageContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    const auto name = system_data() ? system_data()->get_packagename(m_data_typed->getint(".", "link", 0)) : std::nullopt;
    return name ? "パッケージビュー『" + *name + "』" : "パッケージが指定されていません";
}

void LinkStartContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    EventContentBase::initialize(std::move(arg_data), false);
    m_startname = m_data_typed ? m_data_typed->get("link", "") : V{};
}

::cw::lib::Integer LinkStartContent::action()
{
    // 不完全な実行環境でイベント・ツリーの集合がなければ、更新する実行枠がないため何もしない。 Python版のassertは再現しない。
    const auto iface = event_interface();
    const auto event = iface ? iface->get_event() : std::nullopt;
    const auto trees = iface ? iface->get_trees() : std::nullopt;
    if (event && *event && trees) {
        const auto it = trees->find(value_string(m_startname));
        if (it != trees->end()) { (*event)->m_cur_content = it->second; (*event)->m_line_index = 0; }
    }
    return 0;
}

::cw::lib::String LinkStartContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    const S name = m_data_typed ? m_data_typed->get("link", "") : S{};
    return name.empty() ? "スタートコンテントが指定されていません" : "スタートコンテント『" + name + "』へのリンク";
}

void LoseBgImageContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    EventContentBase::initialize(std::move(arg_data), true);
    if (!m_data_typed) return;
    m_cellname = m_data_typed->get("cellname", "");
    m_doanime = m_data_typed->getbool(".", "doanime", true);
    m_ignoreeffectbooster = m_data_typed->getbool(".", "ignoreeffectbooster", false);
    m_regex = m_data_typed->getbool(".", "regex", false);
}

::cw::lib::Integer LoseBgImageContent::action()
{
    if (!::cw::g_cwpy || value_string(m_cellname).empty() || !::cw::g_cwpy->m_background) return 0;
    std::vector<S> cells{value_string(m_cellname)};
    if (m_regex) {
        cells = ::cw::util::match_cellnames_by_regex(
            value_string(m_cellname),
            ::cw::g_cwpy->m_background->get_cellnames(true));
    }
    const bool changed = ::cw::g_cwpy->m_background->reload(m_doanime, get_transitiontype(), true, cells, {}, {}, m_ignoreeffectbooster, false);
    if (changed && !::cw::g_cwpy->m_in_actionevent) ::cw::g_cwpy->wait_frame(1, true, std::nullopt, 30);
    update_bg_after();
    return 0;
}

::cw::lib::String LoseBgImageContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    return "セル名称 = 【" + value_string(m_cellname) + "】";
}

void LoseContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    EventContentBase::initialize(std::move(arg_data), true);
    if (m_data_typed) { m_resid = m_data_typed->getint(".", "id", 0); m_num = m_data_typed->getint(".", "number", 0); m_scope = m_data_typed->get("targets", ""); }
}

void LoseContent::lose_cards(::cw::lib::String arg_cardtype)
{
    const S scope = value_string(m_scope);
    if (scope != "SelectedCard" && is_differentscenario(std::nullopt)) return;
    I pocket = -1;
    std::function<std::optional<std::shared_ptr<::cw::data::CWPyElement>>(I, S, bool, std::optional<std::map<S,S>>)> getter;
    if (arg_cardtype == "SkillCard") { pocket = ::cw::g_POCKET_SKILL; getter = [this](I id, S tag, bool cache, std::optional<std::map<S,S>> attrs) { return system_data() ? system_data()->get_skilldata(id, tag, cache, attrs) : std::nullopt; }; }
    else if (arg_cardtype == "ItemCard") { pocket = ::cw::g_POCKET_ITEM; getter = [this](I id, S tag, bool cache, std::optional<std::map<S,S>> attrs) { return system_data() ? system_data()->get_itemdata(id, tag, cache, attrs) : std::nullopt; }; }
    else if (arg_cardtype == "BeastCard") { pocket = ::cw::g_POCKET_BEAST; getter = [this](I id, S tag, bool cache, std::optional<std::map<S,S>> attrs) { return system_data() ? system_data()->get_beastdata(id, tag, cache, attrs) : std::nullopt; }; }
    else throw std::invalid_argument(arg_cardtype + " is invalid cardtype");
    auto iface = event_interface();
    if (!iface || !::cw::g_cwpy) return;
    if (scope == "SelectedCard") {
        const auto selected = iface->get_selectedcard();
        if (selected && *selected && (*selected)->m_type == arg_cardtype)
            ::cw::g_cwpy->trade("TRASHBOX", std::nullopt, *selected, true, nullptr, -1, -1, false, false, std::nullopt, false, false, true, true, 0);
        return;
    }
    const auto data = getter(m_resid, "Property", false, std::nullopt);
    if (!data || !*data) return;
    const S name = (*data)->gettext("Name", ""), desc = (*data)->gettext("Description", "");
    I remaining = m_num == 0 ? 0x7fffffff : m_num;
    const bool unreversed = scope != "Field";
    for (const auto& value : iface->get_targetscope(scope, unreversed, true, "")) {
        std::vector<std::shared_ptr<::cw::header::CardHeader>> headers;
        if (const auto character = character_target(value)) headers = character->get_pocketcards(pocket);
        else if (const auto sequence = value_sequence(value)) for (const auto& item : sequence->m_values) if (auto h = typed_object<::cw::header::CardHeader>(item)) headers.push_back(h);
        const auto result = lose_card(name, desc, headers, remaining);
        remaining -= std::get<1>(result);
        if (remaining <= 0) break;
    }
}

std::tuple<std::vector<std::shared_ptr<::cw::header::CardHeader>>, ::cw::lib::Integer> LoseContent::lose_card(::cw::lib::String arg_name, ::cw::lib::String arg_desc, std::vector<std::shared_ptr<::cw::header::CardHeader>> arg_target, ::cw::lib::Integer arg_num)
{
    std::vector<std::shared_ptr<::cw::header::CardHeader>> headers;
    for (const auto& header : arg_target) if (header && header->m_name == arg_name && header->m_desc == arg_desc) headers.push_back(header);
    I count = 0;
    if (!headers.empty()) {
        count = arg_num == 0 ? static_cast<I>(headers.size()) : std::min<I>(std::max<I>(arg_num, 1), static_cast<I>(headers.size()));
        for (I index = 0; index < count; ++index) if (::cw::g_cwpy)
            ::cw::g_cwpy->trade("TRASHBOX", std::nullopt, headers[static_cast<std::size_t>(index)], true, nullptr, -1, -1, false, false, std::nullopt, false, false, true, true, 0);
    }
    return {headers, count};
}

void MoveBgImageContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data, bool arg_is_changestate)
{
    // 移植版APIの拡張として、明示的なchangestate引数を尊重する。 Python
    // 版はtrueを強制するが、共通生成処理の既定trueはPython版に合わせる。
    EventContentBase::initialize(std::move(arg_data), arg_is_changestate);
    if (!m_data_typed) return;
    m_cellname = m_data_typed->get("cellname", ""); m_positiontype = m_data_typed->get("positiontype", "None");
    m_x = m_data_typed->getint(".", "x", 0); m_y = m_data_typed->getint(".", "y", 0);
    m_sizetype = m_data_typed->get("sizetype", "None"); m_width = m_data_typed->getint(".", "width", 0); m_height = m_data_typed->getint(".", "height", 0);
    m_doanime = m_data_typed->getbool(".", "doanime", true); m_ignoreeffectbooster = m_data_typed->getbool(".", "ignoreeffectbooster", false); m_regex = m_data_typed->getbool(".", "regex", false);
}

::cw::lib::Integer MoveBgImageContent::action()
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_background || value_string(m_cellname).empty() ||
        (value_string(m_positiontype) == "None" && value_string(m_sizetype) == "None")) return 0;
    const auto moved = std::make_tuple(value_string(m_positiontype), m_x, m_y, value_string(m_sizetype), m_width, m_height);
    std::vector<S> cells{value_string(m_cellname)};
    if (m_regex) {
        cells = ::cw::util::match_cellnames_by_regex(
            value_string(m_cellname),
            ::cw::g_cwpy->m_background->get_cellnames(true));
    }
    std::vector<std::optional<std::tuple<S, I, I, S, I, I>>> movedata(cells.size(), moved);
    const bool changed = ::cw::g_cwpy->m_background->reload(m_doanime, get_transitiontype(), true, cells, {}, movedata, m_ignoreeffectbooster, false);
    if (changed && !::cw::g_cwpy->m_in_actionevent) ::cw::g_cwpy->wait_frame(1, true, std::nullopt, 30);
    update_bg_after(); return 0;
}

::cw::lib::String MoveBgImageContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    std::vector<S> seq{"セル名称 = 【" + value_string(m_cellname) + "】"};
    const S pos = value_string(m_positiontype), size = value_string(m_sizetype);
    if (pos != "None") seq.push_back(pos == "Absolute" ? "(" + std::to_string(m_x) + ", " + std::to_string(m_y) + ")pxへ移動" : pos == "Relative" ? "現在位置+(" + std::to_string(m_x) + ", " + std::to_string(m_y) + ")pxへ移動" : pos == "Percentage" ? "現在位置×(" + std::to_string(m_x) + ", " + std::to_string(m_y) + ")%へ移動" : "未知の位置指定方式");
    if (size != "None") seq.push_back(size == "Absolute" ? "(" + std::to_string(m_width) + ", " + std::to_string(m_height) + ")pxにリサイズ" : size == "Relative" ? "現在サイズ+(" + std::to_string(m_width) + ", " + std::to_string(m_height) + ")pxにリサイズ" : size == "Percentage" ? "現在サイズ×(" + std::to_string(m_width) + ", " + std::to_string(m_height) + ")%にリサイズ" : "未知のサイズ指定方式");
    S out; for (const auto& s : seq) { if (!out.empty()) out += " "; out += s; } return out;
}

void MoveCardContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data, bool arg_is_changestate)
{
    // 移植版APIの拡張として、明示的なchangestate引数を尊重する。 Python
    // 版はtrueを強制するが、共通生成処理の既定trueはPython版に合わせる。
    EventContentBase::initialize(std::move(arg_data), arg_is_changestate);
    if (!m_data_typed) return;
    m_cardgroup = m_data_typed->get("cardgroup", ""); m_positiontype = m_data_typed->get("positiontype", "None");
    m_x = m_data_typed->getint(".", "x", 0); m_y = m_data_typed->getint(".", "y", 0); m_scale = m_data_typed->getint(".", "scale", -1); m_layer = m_data_typed->getint(".", "layer", -1); m_width = m_data_typed->getint(".", "width", -1); m_height = m_data_typed->getint(".", "height", -1);
    m_width = m_width == -1 ? -1 : std::clamp(m_width, I{30}, I{300}); m_height = m_height == -1 ? -1 : std::clamp(m_height, I{30}, I{300});
    const S speed = m_data_typed->get("cardspeed", "Default"); m_cardspeed = speed == "Default" ? -1 : ::cw::lib::require_python_integer(speed); m_overridecardspeed = speed != "Default" && m_data_typed->getbool(".", "overridecardspeed", false);
}

::cw::lib::Integer MoveCardContent::action()
{
    if (value_string(m_cardgroup).empty() || (value_string(m_positiontype) == "None" && m_scale == -1 && m_layer == -1 && m_width == -1 && m_height == -1)) return 0;
    const V old_override = ::cw::g_cwpy ? ::cw::g_cwpy->m_override_dealspeed : V{};
    const I old_force = ::cw::g_cwpy ? ::cw::g_cwpy->m_force_dealspeed : I{-1};
    try {
        if (::cw::g_cwpy && m_cardspeed != -1) {
            if (m_overridecardspeed) ::cw::g_cwpy->m_force_dealspeed = m_cardspeed;
            else ::cw::g_cwpy->m_override_dealspeed = m_cardspeed;
        }
        const I result = _do_action();
        if (::cw::g_cwpy) { ::cw::g_cwpy->m_override_dealspeed = old_override; ::cw::g_cwpy->m_force_dealspeed = old_force; }
        return result;
    }
    catch (...) {
        if (::cw::g_cwpy) { ::cw::g_cwpy->m_override_dealspeed = old_override; ::cw::g_cwpy->m_force_dealspeed = old_force; }
        throw;
    }
}

::cw::lib::Integer MoveCardContent::_do_action()
{
    if (!::cw::g_cwpy) return 0;
    I index = 0;
    std::vector<std::shared_ptr<::cw::sprite::card::CWPyCard>> deals;
    for (const auto& value : ::cw::g_cwpy->get_mcards(V{}, "")) {
        const auto card = typed_object<::cw::sprite::card::CWPyCard>(value); if (!card) continue;
        if (std::dynamic_pointer_cast<::cw::sprite::card::FriendCard>(card)) continue;
        S group;
        if (const auto enemy = std::dynamic_pointer_cast<::cw::sprite::card::EnemyCard>(card)) group = enemy->m_cardgroup;
        else if (const auto menu = std::dynamic_pointer_cast<::cw::sprite::card::MenuCard>(card)) group = menu->m_cardgroup;
        if (group != value_string(m_cardgroup)) continue;
        auto pos = card->get_pos_noscale(); I x = std::get<0>(pos), y = std::get<1>(pos);
        if (value_string(m_positiontype) == "Absolute") { x = m_x; y = m_y; }
        else if (value_string(m_positiontype) == "Relative") { x += m_x; y += m_y; }
        else if (value_string(m_positiontype) == "Percentage") {
            // Pythonの床関数による除算と、y計算に更新済みのxを使う動作を維持する。
            const auto scale_percent = [](I position, I percent) {
                ::cw::lib::DecimalContext exact; exact.precision = 0;
                return ::cw::lib::Decimal(position).multiply(::cw::lib::Decimal(percent), exact).floor_divide(::cw::lib::Decimal(100), exact).to_integer();
            };
            x = scale_percent(x, m_x); y = scale_percent(x, m_y);
        }
        I scale = card->m_scale; if (m_scale != -1) scale = m_scale;
        auto layer = card->m_tlayer; if (m_layer != -1) std::get<0>(layer) = m_layer;
        const bool shown = card->m_status != "hidden";
        if (shown) ::cw::animation::animate_sprite(std::static_pointer_cast<::cw::sprite::base::CWPySprite>(card), "hide");
        card->set_pos_noscale(std::make_tuple(x, y), std::nullopt);
        card->set_scale(scale);
        card->set_size(m_width, m_height);
        card->m_tlayer = layer;
        ::cw::sprite::card::refresh_card_layer(*card);
        if (shown) deals.push_back(card);
        if (auto menu = std::dynamic_pointer_cast<::cw::sprite::card::MenuCard>(card)) {
            if (auto yado = yado_data()) yado->changed();
            if (auto sdata = system_data()) sdata->m_moved_mcards[{group, index}] = {x, y, scale, std::get<0>(layer), menu->m_width, menu->m_height};
        }
        ++index;
    }
    for (const auto& card : deals)
        ::cw::animation::animate_sprite(std::static_pointer_cast<::cw::sprite::base::CWPySprite>(card), "deal");
    return 0;
}

::cw::lib::String MoveCardContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event; S out = "カードグループ = 【" + value_string(m_cardgroup) + "】";
    const S pos = value_string(m_positiontype); if (pos != "None") out += " " + (pos == "Absolute" ? "(" + std::to_string(m_x) + ", " + std::to_string(m_y) + ")pxへ移動" : pos == "Relative" ? "現在位置+(" + std::to_string(m_x) + ", " + std::to_string(m_y) + ")pxへ移動" : pos == "Percentage" ? "現在位置×(" + std::to_string(m_x) + ", " + std::to_string(m_y) + ")%へ移動" : "未知の位置指定方式");
    if (m_scale != -1) out += " 拡大率 = " + std::to_string(m_scale) + "%"; if (m_layer != -1) out += " レイヤ = " + std::to_string(m_layer); if (m_width != -1 || m_height != -1) out += " サイズ = (" + std::to_string(m_width) + ", " + std::to_string(m_height) + ")px"; return out;
}

void MultipleMoveLoseBgImageContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data, bool arg_is_changestate)
{
    // 移植版APIの拡張として、明示的なchangestate引数を尊重する。 Python
    // 版はtrueを強制するが、共通生成処理の既定trueはPython版に合わせる。
    EventContentBase::initialize(std::move(arg_data), arg_is_changestate);
    m_moves.clear();
    if (!m_data_typed) { m_loses.clear(); return; }
    const auto append_move = [this](const std::shared_ptr<::cw::data::CWPyElement>& m) {
        if (!m) return;
        const S cell = m->get("cellname", "");
        const S positiontype = m->get("positiontype", "None");
        const I x = m->getint(".", "x", 0);
        const I y = m->getint(".", "y", 0);
        const S sizetype = m->get("sizetype", "None");
        const I width = m->getint(".", "width", 0);
        const I height = m->getint(".", "height", 0);
        const auto move = std::make_tuple(positiontype, x, y, sizetype, width, height);
        if (!cell.empty()) m_moves.emplace_back(cell, move);
    };
    if (const auto moves = m_data_typed->find("Moves"); moves && *moves)
        for (const auto& entry : (*moves)->m_children) {
            if (!entry) continue;
            if (entry->m_tag == "Move") append_move(entry);
            else if (const auto nested = entry->find("Move"); nested && *nested)
                for (const auto& child : (*nested)->m_children) append_move(child);
        }
    m_loses.clear();
    const auto append_lose = [this](const std::shared_ptr<::cw::data::CWPyElement>& l) {
        if (!l) return;
        const S cell = l->get("cellname", "");
        if (!cell.empty()) m_loses.push_back(cell);
    };
    if (const auto loses = m_data_typed->find("Loses"); loses && *loses)
        for (const auto& entry : (*loses)->m_children) {
            if (!entry) continue;
            if (entry->m_tag == "Lose") append_lose(entry);
            else if (const auto nested = entry->find("Lose"); nested && *nested)
                for (const auto& child : (*nested)->m_children) append_lose(child);
        }
    m_doanime = m_data_typed->getbool(".", "doanime", true); m_ignoreeffectbooster = m_data_typed->getbool(".", "ignoreeffectbooster", false); m_regex = m_data_typed->getbool(".", "regex", false);
}

::cw::lib::Integer MultipleMoveLoseBgImageContent::action()
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_background || (m_moves.empty() && m_loses.empty())) return 0;
    const auto transition = get_transitiontype();
    std::vector<S> current_cellnames;
    if (m_regex) current_cellnames = ::cw::g_cwpy->m_background->get_cellnames(true);
    std::vector<S> cells; std::vector<std::optional<std::vector<std::shared_ptr<::cw::data::CWPyElement>>>> repl; std::vector<std::optional<std::tuple<S,I,I,S,I,I>>> moved;
    const auto append_move = [&](const S& cell, const auto& md) { if (std::find(cells.begin(), cells.end(), cell) == cells.end()) { cells.push_back(cell); repl.push_back(std::nullopt); moved.push_back(md); } };
    const auto append_lose = [&](const S& cell) { const auto it = std::find(cells.begin(), cells.end(), cell); if (it == cells.end()) { cells.push_back(cell); repl.push_back(std::nullopt); moved.push_back(std::nullopt); } else moved[static_cast<std::size_t>(std::distance(cells.begin(), it))] = std::nullopt; };
    const auto expand = [this, &current_cellnames](const S& pattern) {
        return m_regex ? ::cw::util::match_cellnames_by_regex(
                             pattern, current_cellnames)
                       : std::vector<S>{pattern};
    };
    for (const auto& [cell, data] : m_moves) for (const auto& match : expand(cell)) append_move(match, data); for (const auto& cell : m_loses) for (const auto& match : expand(cell)) append_lose(match);
    const bool changed = ::cw::g_cwpy->m_background->reload(m_doanime, transition, true, cells, repl, moved, m_ignoreeffectbooster, false);
    if (changed && !::cw::g_cwpy->m_in_actionevent) ::cw::g_cwpy->wait_frame(1, true, std::nullopt, 30); update_bg_after(); return 0;
}

::cw::lib::String MultipleMoveLoseBgImageContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event; std::vector<S> seq;
    for (const auto& [cell, data] : m_moves) { const auto& [pos, x, y, size, w, h] = data; S part = "セル名称 = 【" + cell + "】"; if (pos != "None") part += " " + (pos == "Absolute" ? "(" + std::to_string(x) + ", " + std::to_string(y) + ")pxへ移動" : pos == "Relative" ? "現在位置+(" + std::to_string(x) + ", " + std::to_string(y) + ")pxへ移動" : pos == "Percentage" ? "現在位置×(" + std::to_string(x) + ", " + std::to_string(y) + ")%へ移動" : "未知の位置指定方式"); if (size != "None") part += " " + (size == "Absolute" ? "(" + std::to_string(w) + ", " + std::to_string(h) + ")pxにリサイズ" : size == "Relative" ? "現在サイズ+(" + std::to_string(w) + ", " + std::to_string(h) + ")pxにリサイズ" : size == "Percentage" ? "現在サイズ×(" + std::to_string(w) + ", " + std::to_string(h) + ")%にリサイズ" : "未知のサイズ指定方式"); seq.push_back(part); } for (const auto& cell : m_loses) seq.push_back("削除: " + cell); S out; for (const auto& s : seq) { if (!out.empty()) out += "; "; out += s; } return out.empty() ? "無し" : out;
}

void PlayBgmContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    EventContentBase::initialize(std::move(arg_data), true);
}

::cw::lib::Integer PlayBgmContent::action()
{
    if (!::cw::g_cwpy || !m_data_typed) return 0;
    const S path = ::cw::util::validate_filepath(m_data_typed->get("path", ""));
    const I volume = m_data_typed->getint(".", "volume", 100);
    const I loop = m_data_typed->getint(".", "loopcount", 0);
    const I channel = m_data_typed->getint(".", "channel", 0);
    const I fade = m_data_typed->getint(".", "fadein", 0);
    if (channel >= 0 && channel < static_cast<I>(::cw::thread::CWPy::s_music.size())) {
        const auto& music = ::cw::thread::CWPy::s_music[static_cast<std::size_t>(channel)];
        if (music) {
            if (path.empty()) music->stop(fade);
            // pathはXMLの素材パスであり、解決済みのネイティブのファイル名では
            // ない。 fullpathとして渡すとスキン・シナリオの検索を飛ばし、Bgm/DefOpen.mid
            // がプロセスの作業ディレクトリ相対になる。
            else music->play(path, true, false, false, volume, loop, fade, {});
        }
    }
    return 0;
}

::cw::lib::String PlayBgmContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    if (!m_data_typed) return "BGM停止";
    const S path = m_data_typed->get("path", "");
    const I loop = m_data_typed->getint(".", "loopcount", 0);
    const I channel = m_data_typed->getint(".", "channel", 0);
    const I fade = m_data_typed->getint(".", "fadein", 0);
    if (path.empty()) return "BGM停止";
    const S ch = channel == 0 ? "主音声" : "副音声" + std::to_string(channel);
    return "BGMを【" + path + "】へ変更(音量:" + std::to_string(m_data_typed->getint(".", "volume", 100)) +
           " ループ回数:" + (loop == 0 ? "∞" : std::to_string(loop)) + " Ch.:" + ch +
           (fade > 0 ? " " + ::cw::lib::python_float_repr(static_cast<double>(fade) / 1000.0) + "秒かけてフェードイン" : "") + ")";
}

void PlaySoundContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    EventContentBase::initialize(std::move(arg_data), false);
}

::cw::lib::Integer PlaySoundContent::action()
{
    if (::cw::g_cwpy && m_data_typed) {
        const S path = ::cw::util::validate_filepath(m_data_typed->get("path", ""));
        ::cw::g_cwpy->play_sound_with(path, std::nullopt, m_data_typed->getint(".", "volume", 100),
                                      m_data_typed->getint(".", "loopcount", 1), m_data_typed->getint(".", "channel", 0),
                                      m_data_typed->getint(".", "fadein", 0));
    }
    return 0;
}

::cw::lib::String PlaySoundContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    if (!m_data_typed) return "効果音が指定されていません";
    const S path = m_data_typed->get("path", "");
    const I volume = m_data_typed->getint(".", "volume", 100);
    const I loopcount = m_data_typed->getint(".", "loopcount", 1);
    const I fade = m_data_typed->getint(".", "fadein", 0);
    const S loop_label = loopcount == 0 ? "∞" : std::to_string(loopcount);
    const I channel = m_data_typed->getint(".", "channel", 0);
    const S channel_label = channel == 0 ? "主音声" : "副音声" + std::to_string(channel);
    const S fade_label = fade > 0 ? " " + ::cw::lib::python_float_repr(static_cast<double>(fade) / 1000.0) + "秒かけてフェードイン" : "";
    return path.empty() ? "効果音が指定されていません" : "効果音【" + path + "】を鳴らす(音量:" +
        std::to_string(volume) + " ループ回数:" + loop_label + " Ch.:" + channel_label + fade_label + ")";
}

void PostEventContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    EventContentBase::initialize(std::move(arg_data), false);
}

::cw::lib::Integer PostEventContent::action()
{
    if (::cw::g_cwpy && (!::cw::g_cwpy->is_playingscenario() || ::cw::g_cwpy->m_areaid < 0) && m_data_typed)
        do_action(m_data_typed->get("command", ""), m_data_typed->get("arg", ""));
    return tree_end();
}

void PostEventContent::do_action(::cw::lib::String arg_command, std::optional<::cw::lib::String> arg_arg)
{
    if (!::cw::g_cwpy) return;
    // Python版と同様、メニューカードのロックを取る前にmethoddict
    // を調べる。 先にロックすると、不明なスキンコマンドで解除用の予
    // 約処理がなく、m_lock_menucardsが残り続ける。
    if (!g_methoddict.contains(arg_command)) {
        ::cw::g_cwpy->call_dlg("ERROR", {{"text", V("コマンド「" + arg_command + "」は存在しません。")}});
        return;
    }
    I arg_i = 0;
    bool numeric = false;
    std::optional<V> forwarded_arg;
    if (arg_arg && !arg_arg->empty()) {
        // Pythonの int(arg) と同様、ASCII限定のfrom_chars
        // ではなく、Unicode十進数字、前後のUnicode空白
        // 、桁区切りを許す。 失敗時は PostEventContent.do_action
        // と同様、引数を文字列のまま残す。
        if (const auto parsed = ::cw::lib::parse_python_integer(*arg_arg)) {
            arg_i = *parsed;
            numeric = true;
            if (arg_i != 0) forwarded_arg = V(arg_i);
        } else if (const auto normalized = ::cw::lib::normalize_python_integer(*arg_arg)) {
            // Pythonの任意精度整数の引数は、正確な
            // Decimalで保持する。 int64の変
            // 換があふれた後に、元の文字列へ戻さない。
            const ::cw::lib::Decimal integer(*normalized);
            if (!integer.is_zero()) forwarded_arg = V(integer);
        } else {
            forwarded_arg = V(*arg_arg);
        }
    }
    const bool old_lock = ::cw::g_cwpy->m_lock_menucards;
    ::cw::g_cwpy->m_lock_menucards = true;
    const auto restore_lock = [old_lock]() {
        if (::cw::g_cwpy) ::cw::g_cwpy->m_lock_menucards = old_lock;
    };
    const auto exec_on_game_thread = [&](::cw::lib::Callback callback,
                                        std::optional<V> argument = std::nullopt) {
        if (!::cw::g_cwpy) return;
        if (argument) ::cw::g_cwpy->exec_func_2(std::move(callback), std::move(*argument));
        else ::cw::g_cwpy->exec_func(std::move(callback));
        if (arg_command != "ShowDialog") {
            ::cw::g_cwpy->exec_func([restore_lock](const auto&, const auto&) {
                restore_lock();
                return V{};
            });
        }
    };
    try {
        if (arg_command == "MoveToYado" || arg_command == "Exit" || arg_command == "Load" ||
            arg_command == "StartScenario" || arg_command == "InterruptAdventure") {
            exec_on_game_thread([command = arg_command](const auto& args, const auto&) {
                // Python版の引数の不一致は、PostEventで
                // 予約するときではなく、実行時に発生する。 引数を捨て
                // て、破壊的な引数なしコマンドを代わりに実行しない。
                if (!args.empty()) throw std::invalid_argument(command + " takes no positional argument");
                if (::cw::g_cwpy) {
                    if (command == "MoveToYado") ::cw::g_cwpy->set_yado();
                    else if (command == "Exit") ::cw::g_cwpy->quit2();
                    else if (command == "Load") ::cw::g_cwpy->reload_yado();
                    else if (command == "StartScenario") ::cw::g_cwpy->start_scenario();
                    else ::cw::g_cwpy->interrupt_adventure();
                }
                return V{};
            }, forwarded_arg);
        }
        else if (arg_command == "MoveToTitle") exec_on_game_thread([](const auto& args, const auto&) {
            if (::cw::g_cwpy) ::cw::g_cwpy->set_title(args.empty() ? true : args.front().as_bool(), {"Default", V("Default")});
            return V{};
        }, forwarded_arg);
        else if (arg_command == "DissolveParty") exec_on_game_thread([](const auto& args, const auto&) {
            if (::cw::g_cwpy) ::cw::g_cwpy->dissolve_party_value(args.empty() ? V{} : args.front(), true);
            return V{};
        }, forwarded_arg);
        else if (arg_command == "LoadParty") exec_on_game_thread([](const auto& args, const auto&) {
            if (::cw::g_cwpy) {
                if (args.empty()) ::cw::g_cwpy->load_party(std::nullopt, true, false, true);
                else {
                    // PostEventの単一値のヘッダーは、Python版
                    // でも属性取得の失敗前にYadoDataローダーへ届く
                    // 。 黙ってNoneへ置き換えず、同じ共通処理を通す。
                    if (!::cw::g_cwpy->m_ydata || !*::cw::g_cwpy->m_ydata)
                        throw std::logic_error("load_party requires inn data");
                    (*::cw::g_cwpy->m_ydata)->load_party_value(args.front());
                }
            }
            return V{};
        }, forwarded_arg);
        else if (arg_command == "ChangeToSpecialArea" && numeric && arg_i != 0) {
            const I area = arg_i;
            exec_on_game_thread([area](const auto&, const auto&) {
                if (::cw::g_cwpy) ::cw::g_cwpy->change_specialarea(area, false);
                return V{};
            });
        }
        else if (arg_command == "MoveCard") {
            // CardControl は CWPy::m_selectedheader
            // に選択ヘッダーを設定する。 イベントのselectedcard
            // はシナリオ内のカード選択でだけ設定されるため、 それだけを見る
            // とカードをクリックしても移動先操作で未選択と判断してしまう。
            auto selected = ::cw::g_cwpy->m_event ? ::cw::g_cwpy->m_event->get_selectedcard() : std::nullopt;
            if ((!selected || !*selected) && ::cw::g_cwpy->m_selectedheader)
                selected = ::cw::g_cwpy->m_selectedheader;
#if defined(CARDWIRTH_LOG_ENABLED)
            ::cardwirth::debug::log("MoveCard selection event=" +
                ((selected && *selected) ? (*selected)->m_name : ::cw::lib::String("<none>")) +
                " runtime=" + (::cw::g_cwpy->m_selectedheader && *::cw::g_cwpy->m_selectedheader
                    ? (*::cw::g_cwpy->m_selectedheader)->m_name : ::cw::lib::String("<none>")));
#endif
            if (selected && *selected) {
                ::cw::g_cwpy->trade(arg_arg.value_or("BACKPACK"), std::nullopt, *selected, false, nullptr,
                                    -1, -1, true, true, std::nullopt, false, true, true, true, 1);
            } else {
                // Pythonの trade() はselectedheaderをassertで
                // 要求する。 その失敗をUIに見えるエラーとして保ち、イベントを黙って捨てない。
                ::cw::g_cwpy->call_dlg("ERROR", {{"text", V("移動対象カードが選択されていません。")}});
            }
        }
        else if (arg_command == "ShowDialog" && arg_arg.value_or("") == "YADOSELECT" &&
                 ::cw::g_cwpy->is_novel_mode() && !::cw::novel::config().yado.empty()) {
            // ノベルモードでは、タイトルの開始カードは常に
            // 設定された宿を対象とする。 タイトルへ戻っ
            // た後も、通常の複数宿の選択画面を出さない。
            exec_on_game_thread([restore_lock](const auto&, const auto&) {
                // この拡張は、メニューロックを引き取り解除するダイアログを開かない。
                try {
                    if (::cw::g_cwpy) (void)::cw::g_cwpy->load_novel_default_yado();
                } catch (...) {
                    restore_lock();
                    throw;
                }
                restore_lock();
                return V{};
            });
        }
        else if (arg_command == "ShowDialog") exec_on_game_thread([restore_lock](const auto& args, const auto&) {
            if (args.size() != 1 || !std::holds_alternative<S>(args.front().storage()))
                throw std::invalid_argument("call_dlg requires a dialog name");
            if (::cw::g_cwpy) {
                const auto session = ::cw::g_cwpy->call_dlg(std::get<S>(args.front().storage()), {});
                // ネイティブの起動時には、ダイアログが未登録の場合がある。 Python版の再
                // 帰的なERRORダイアログと異なり、その診断はモーダルのロックを持たない。
                if (!session && !::cw::g_cwpy->is_showingdlg()) restore_lock();
            }
            return V{};
        }, forwarded_arg);
        else {
            // Python版は、空・ゼロなら引数なし、それ以
            // 外は元の文字列でメソッドを呼び、TypeError
            // を出す。 C++側は整数引数に固定されるため
            // 、既知のコマンドを未存在と誤報せず、不正な引
            // 数を明示する。 ロックは下で同期的に戻す。
            ::cw::g_cwpy->call_dlg("ERROR", {{"text", V("コマンド「" + arg_command + "」の引数が正しくありません。")}});
        }
    } catch (...) {
        restore_lock();
        throw;
    }
    if (arg_command == "MoveCard" ||
        (arg_command == "ChangeToSpecialArea" && (!numeric || arg_i == 0)))
        restore_lock();
}

void RedisplayContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    EventContentBase::initialize(std::move(arg_data), true);
}

::cw::lib::Integer RedisplayContent::action()
{
    if (::cw::g_cwpy && ::cw::g_cwpy->m_background) {
        const auto total_started = std::chrono::steady_clock::now();
        const bool changed = ::cw::g_cwpy->m_background->reload(true, get_transitiontype(), true, {}, {}, {}, false, false);
        const auto reload_finished = std::chrono::steady_clock::now();
        if (changed && !::cw::g_cwpy->m_in_actionevent) ::cw::g_cwpy->wait_frame(1, true, std::nullopt, 30);
        const auto wait_finished = std::chrono::steady_clock::now();
        update_bg_after();
#if defined(CARDWIRTH_LOG_ENABLED)
        const auto update_finished = std::chrono::steady_clock::now();
        const auto elapsed_us = [](const auto& begin, const auto& end) {
            return std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count();
        };
        ::cardwirth::debug::log(
            "Background redisplay timing: total_us=" + std::to_string(elapsed_us(total_started, update_finished)) +
            " reload_us=" + std::to_string(elapsed_us(total_started, reload_finished)) +
            " wait_us=" + std::to_string(elapsed_us(reload_finished, wait_finished)) +
            " update_us=" + std::to_string(elapsed_us(wait_finished, update_finished)) +
            " changed=" + (changed ? "1" : "0"));
#endif
    }
    return 0;
}

::cw::lib::String RedisplayContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    return "画面再構築コンテント";
}

void ReplaceBgImageContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    EventContentBase::initialize(std::move(arg_data), true); if (!m_data_typed) return;
    m_cellname = m_data_typed->get("cellname", ""); m_doanime = m_data_typed->getbool(".", "doanime", true); m_ignoreeffectbooster = m_data_typed->getbool(".", "ignoreeffectbooster", false); m_regex = m_data_typed->getbool(".", "regex", false);
}

::cw::lib::Integer ReplaceBgImageContent::action()
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_background || value_string(m_cellname).empty()) return 0;
    if (!m_data_typed) throw std::logic_error("ReplaceBgImageContent requires XML data");
    const auto input = m_data_typed->find_exists("BgImages")->m_children;
    const auto elements = system_data() ? system_data()->get_bgdata(input) : std::vector<std::shared_ptr<::cw::data::CWPyElement>>{}; std::vector<S> cells{value_string(m_cellname)};
    if (m_regex)
        cells = ::cw::util::match_cellnames_by_regex(
            value_string(m_cellname),
            ::cw::g_cwpy->m_background->get_cellnames(true));
    std::vector<std::optional<std::vector<std::shared_ptr<::cw::data::CWPyElement>>>> replacements(cells.size(), elements); const bool changed = ::cw::g_cwpy->m_background->reload(m_doanime, get_transitiontype(), true, cells, replacements, {}, m_ignoreeffectbooster, false);
    if (changed && !::cw::g_cwpy->m_in_actionevent) ::cw::g_cwpy->wait_frame(1, true, std::nullopt, 30); update_bg_after(); return 0;
}

::cw::lib::String ReplaceBgImageContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    std::vector<S> values;
    if (const auto bg = m_data_typed ? m_data_typed->find("BgImages") : std::nullopt; bg && *bg) {
        for (const auto& e : (*bg)->m_children) if (e) {
            if (e->m_tag == "BgImage") values.push_back(::cw::util::validate_filepath(e->gettext("ImagePath", "")));
            else if (e->m_tag == "TextCell") values.push_back("テキスト「" + background_status_text(e->gettext("Text", "")) + "」");
            else if (e->m_tag == "ColorCell") values.push_back("カラーセル");
            else if (e->m_tag == "PCCell") values.push_back("PCイメージ[" + e->gettext("PCNumber", "") + "]");
        }
    }
    S joined;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i) joined += "】【";
        joined += values[i];
    }
    if (values.empty()) joined = "無し";
    return "セル名称 = 【" + value_string(m_cellname) + "】 背景 = 【" + joined + "】";
}

void ReverseFlagContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    EventContentBase::initialize(std::move(arg_data), true);
    if (!m_data_typed) return;
    m_flag = m_data_typed->get("flag", "");
    const S speed = m_data_typed->get("cardspeed", "Default");
    m_cardspeed = speed == "Default" ? -1 : ::cw::lib::require_python_integer(speed);
    m_overridecardspeed = speed != "Default" && m_data_typed->getbool(".", "overridecardspeed", false);
}

::cw::lib::Integer ReverseFlagContent::action()
{
    const auto sdata = system_data();
    const auto event = event_interface() ? event_interface()->get_nowrunningevent() : std::nullopt;
    const auto flag = sdata ? sdata->find_flag(value_string(m_flag), is_differentscenario(std::nullopt), event) : std::nullopt;
    if (flag && *flag) { (*flag)->reverse(); (*flag)->redraw_cards(m_cardspeed, m_overridecardspeed); }
    return 0;
}

::cw::lib::String ReverseFlagContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    const auto sdata = system_data();
    const auto flag = sdata ? sdata->find_flag(value_string(m_flag), is_differentscenario(arg_event), arg_event) : std::nullopt;
    return flag ? "フラグ『" + (*flag)->m_name + "』の値を反転" : "フラグが指定されていません";
}

void SetActionEventContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    EventContentBase::initialize(std::move(arg_data), true); if (m_data_typed) { m_data = m_data_typed->get("data", ""); m_type = m_data_typed->get("type", "package"); }
}

::cw::lib::Integer SetActionEventContent::action()
{
    if (!::cw::g_cwpy) return 0;
    if (::cw::g_cwpy->m_in_actionevent)
        throw ::cw::event::XmlContentCriticalError("アクションイベントが既にセットされています。");
    const S type = value_string(m_type);
    if (type != "package" && type != "startcontent")
        throw ::cw::event::XmlContentCriticalError("アクションイベントのtype属性はpackageもしくはstartcontentのみ有効です。");
    if (type == "startcontent" && (!event_interface() || !event_interface()->m_in_inusecardevent))
        throw ::cw::event::XmlContentCriticalError("startcontentタイプのアクションイベントは、カードイベント内でのみ有効です。");
    // Python版と同様、パッケージIDを調べる前に以前の接触状態を消
    // す。 負の更新・行動IDで早期に戻る場合も、その入力を終了する。
    ::cw::g_cwpy->clear_actionevent_input();
    const I update = type == "package" && m_data_typed ? m_data_typed->getint(".", "update", -1) : -1;
    if (type == "package" && update < 0) return 0;
    const I touch_action = type == "package" && m_data_typed ? m_data_typed->getint(".", "action", -1) : -1;
    if (type == "package" && touch_action < 0) return 0;
    const auto origin_event = ::cw::g_cwpy->m_event;
    const auto origin_handler = ::cw::g_cwpy->m_eventhandler;
    auto action_handler = std::make_shared<::cw::eventhandler::EventHandlerForUserAction>();
    action_handler->initialize();
    auto action_event = std::make_shared<::cw::event::EventInterface>();
    action_event->initialize("Action");
    if (origin_event) {
        action_event->m_in_inusecardevent = origin_event->m_in_inusecardevent;
        if (action_event->m_in_inusecardevent) action_event->set_inusecard(origin_event->get_inusecard());
    }
    ::cw::g_cwpy->m_event = action_event;
    ::cw::g_cwpy->m_in_actionevent = true;
    // 背景・アニメーションの処理中は、Pythonの独立したMouseWatcher
    // と同様にクリックを保持し、 通常のカード入力として配信しない。
    ::cw::g_cwpy->m_eventhandler = action_handler;
    decltype(::cw::g_cwpy->m_action_pointer_events) origin_pointer_events;
    {
        std::scoped_lock lock(::cw::g_cwpy->m_events_mutex);
        origin_pointer_events = std::move(::cw::g_cwpy->m_action_pointer_events);
        ::cw::g_cwpy->m_action_pointer_events.emplace();
    }
    const auto sdata = scenario();
    bool restored = false;
    const auto restore = [&](bool preserve_active_exception = false) {
        if (restored) return;
        restored = true;
        ::cw::lib::ExceptionPtr update_error;
        try {
            // Python版と同様、行動用EventInterface
            // がまだ有効な間に最後の通常更新を一度行い、その後で呼び出
            // し元を戻す。 EndActionがフレーム更新前にループ
            // を終えても、最後のフラグ・背景・カード変更を確定する。
            ::cw::g_cwpy->update(false, {}, {});
            ::cw::g_cwpy->draw(true, std::nullopt);
        } catch (...) {
            update_error = ::cw::lib::capture_exception();
        }
        ::cw::g_cwpy->clear_actionevent_input();
        action_event->clear();
        ::cw::g_cwpy->m_event = origin_event;
        ::cw::g_cwpy->m_eventhandler = origin_handler;
        {
            std::scoped_lock lock(::cw::g_cwpy->m_events_mutex);
            ::cw::g_cwpy->m_action_pointer_events = std::move(origin_pointer_events);
        }
        ::cw::g_cwpy->m_in_actionevent = false;
        if (sdata) sdata->disableFastLoadCache();
        if (update_error && !preserve_active_exception) ::cw::lib::rethrow_exception(update_error);
    };
    try {
        // Python版と同様、行動ループへ入る前に一度描く。 この初回
        // フレームがないと、入力・更新まで生成した行動UIが空になる。
        ::cw::g_cwpy->clear_selection();
        if (sdata) sdata->enableFastLoadCache();
        ::cw::g_cwpy->update(false, {}, {});
        ::cw::g_cwpy->draw(true, std::nullopt);
        std::shared_ptr<::cw::event::EventEngine> action_engine;
        std::shared_ptr<::cw::event::CardEvent> card_event;
        std::shared_ptr<::cw::data::CWPyElement> start_line;
        std::shared_ptr<::cw::data::CWPyElement> touch_line;
        if (type == "package") {
            // Python版と同様、行動用EventEngine
            // で毎フレームキー1を実行し、マウス接触時だけキ
            // ー2を実行する。 更新・接触のパッケージは別々
            // のリンクなので、同じ呼び出し枠を共有しない。
            action_engine = make_action_event_engine(update, touch_action);
        } else {
            std::shared_ptr<::cw::event::CardEvent> original_card_event;
            if (origin_event && !origin_event->m_stackinfo.empty() && origin_event->m_stackinfo.front())
                original_card_event = typed_object<::cw::event::CardEvent>(*origin_event->m_stackinfo.front());
            if (!original_card_event && origin_event) {
                const auto current = origin_event->get_nowrunningevent();
                if (current && *current) original_card_event = std::dynamic_pointer_cast<::cw::event::CardEvent>(*current);
            }
            const S update_name = m_data_typed ? m_data_typed->get("update", "") : S{};
            const S touch_name = m_data_typed ? m_data_typed->get("action", "") : S{};
            if (!original_card_event || update_name.empty()) {
                restore();
                return 0;
            }
            const auto source = element_of(original_card_event->m__event);
            const auto& original_event_base =
                static_cast<const ::cw::event::Event&>(*original_card_event);
            card_event = std::make_shared<::cw::event::CardEvent>();
            card_event->initialize(source ? std::optional<std::shared_ptr<::cw::data::CWPyElement>>(
                                             ::cw::data::copydata(source, false)) : std::nullopt,
                                   original_event_base.m_inusecard.value_or(nullptr),
                                   original_card_event->m_user,
                                   original_card_event->m_target_cards, true);
            const std::shared_ptr<::cw::event::Event> original_event = original_card_event;
            card_event->copy_variables_from(
                object_value(std::static_pointer_cast<::cw::lib::Object>(original_event)));
            card_event->set_variable_initialize("");
            const auto make_line = [](const S& name) {
                auto line = ::cw::data::make_element("ContentsLine");
                line->append(::cw::data::make_element("Start", {}, std::map<S, S>{{"name", "dummy"}}));
                line->append(::cw::data::make_element("ActionLink", {}, std::map<S, S>{{"type", "Start"}, {"link", name}}));
                return line;
            };
            start_line = make_line(update_name);
            if (!touch_name.empty()) touch_line = make_line(touch_name);
        }

        const I fps = m_data_typed ? m_data_typed->getint(".", "fps", 60) : 60;
        while (::cw::g_cwpy->is_running() && ::cw::g_cwpy->is_playingscenario()) {
            const auto frame_started = std::chrono::steady_clock::now();
            try {
                ::cw::g_cwpy->process_game_commands();
                if (action_engine) {
                    action_engine->start(I(1), std::nullopt, false, false);
                    ::cw::g_cwpy->update(false, {}, {});
                } else if (card_event) {
                    card_event->start_with_content(start_line, true);
                    if (card_event->::cw::event::Event::m_error &&
                        *card_event->::cw::event::Event::m_error)
                        rethrow_action_event_error(*card_event->::cw::event::Event::m_error);
                }
                // Pythonのspriteグループは更新時に背景メディアを
                // 進めるが、C++版は共通の表示段階へ分けている。 この入
                // れ子のループでもstartcontentを含めて処理し、
                // シミュレーション・グループ更新自体を二重に追加しない。
                ::cw::g_cwpy->update_presentation();
                ::cw::g_cwpy->draw(true, std::nullopt);

                if (auto* context = ::cw::eventhandler::runtime_context())
                    if (context->backend) context->backend->poll_events();
                ::cw::g_cwpy->input(false, false, false);
                action_handler->run();
                const auto touch = action_handler->take_touch();
                const bool touched = touch && set_action_mouse_input(*touch);
                if (touched) {
                    if (action_engine) {
                        action_engine->start(I(2), std::nullopt, false, false);
                    } else if (card_event && touch_line) {
                        card_event->start_with_content(touch_line, true);
                        if (card_event->::cw::event::Event::m_error &&
                            *card_event->::cw::event::Event::m_error)
                            rethrow_action_event_error(*card_event->::cw::event::Event::m_error);
                    }
                    ::cw::g_cwpy->update(false, {}, {});
                    ::cw::g_cwpy->draw(true, std::nullopt);
                }
                // Python版と同様、有効な接触は次の更新パッケージまで残す
                // 。 有効な接触のない入力処理だけが、その座標を失効させる。
                if (!touched) ::cw::g_cwpy->clear_actionevent_input();
            } catch (const ::cw::event::ActionBreakError&) {
                break;
            }
            // pygameの Clock.tick_busy_loop(0)
            // と同様、ゼロ・負の独自値に1FPS制限を掛けない。
            if (fps > 0) {
                const auto frame_duration = std::chrono::nanoseconds(1'000'000'000LL / fps);
                std::this_thread::sleep_until(frame_started + frame_duration);
            }
        }
    } catch (...) {
        restore(true);
        throw;
    }
    restore();
    // Event::start and call_package drain their own native/SDL input boundary;
    // restore only after that action-event transaction has completed.
    return 0;
}

::cw::lib::String SetActionEventContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event; return "アクションイベントをセット";
}

void SetFlagContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    EventContentBase::initialize(std::move(arg_data), true);
    if (!m_data_typed) return;
    m_flag = m_data_typed->get("flag", ""); m_value = m_data_typed->getbool(".", "value", false);
    const S speed = m_data_typed->get("cardspeed", "Default");
    m_cardspeed = speed == "Default" ? -1 : ::cw::lib::require_python_integer(speed);
    m_overridecardspeed = speed != "Default" && m_data_typed->getbool(".", "overridecardspeed", false);
}

::cw::lib::Integer SetFlagContent::action()
{
    const auto sdata = system_data();
    const auto event = event_interface() ? event_interface()->get_nowrunningevent() : std::nullopt;
    const auto flag = sdata ? sdata->find_flag(value_string(m_flag), is_differentscenario(std::nullopt), event) : std::nullopt;
    if (flag && *flag) { (*flag)->set(m_value); (*flag)->redraw_cards(m_cardspeed, m_overridecardspeed); }
    return 0;
}

::cw::lib::String SetFlagContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    const auto sdata = system_data();
    const auto flag = sdata ? sdata->find_flag(value_string(m_flag), is_differentscenario(arg_event), arg_event) : std::nullopt;
    return flag ? "フラグ『" + (*flag)->m_name + "』を【" + (*flag)->get_valuename(m_value) + "】に変更" : "フラグが指定されていません";
}

void SetStepContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    EventContentBase::initialize(std::move(arg_data), true);
    if (m_data_typed) { m_step = m_data_typed->get("step", ""); m_value = m_data_typed->getint(".", "value", 0); }
}

::cw::lib::Integer SetStepContent::action()
{
    const auto sdata = system_data(); const auto event = event_interface() ? event_interface()->get_nowrunningevent() : std::nullopt;
    const auto step = sdata ? sdata->find_step(value_string(m_step), is_differentscenario(std::nullopt), event) : std::nullopt;
    if (step && *step) (*step)->set(m_value);
    return 0;
}

::cw::lib::String SetStepContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    const auto sdata = system_data(); const auto step = sdata ? sdata->find_step(value_string(m_step), is_differentscenario(arg_event), arg_event) : std::nullopt;
    return step ? "ステップ『" + (*step)->m_name + "』を【" + (*step)->get_valuename(m_value) + "】に変更" : "ステップが指定されていません";
}

void SetStepDownContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    EventContentBase::initialize(std::move(arg_data), true); if (m_data_typed) m_step = m_data_typed->get("step", "");
}

::cw::lib::Integer SetStepDownContent::action()
{
    const auto sdata = system_data(); const auto event = event_interface() ? event_interface()->get_nowrunningevent() : std::nullopt;
    const auto step = sdata ? sdata->find_step(value_string(m_step), is_differentscenario(std::nullopt), event) : std::nullopt;
    if (step && *step) (*step)->down(); return 0;
}

::cw::lib::String SetStepDownContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    const auto sdata = system_data(); const auto step = sdata ? sdata->find_step(value_string(m_step), is_differentscenario(arg_event), arg_event) : std::nullopt;
    return step ? "ステップ『" + (*step)->m_name + "』の値を1減少" : "ステップが指定されていません";
}

void SetStepUpContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    EventContentBase::initialize(std::move(arg_data), true); if (m_data_typed) m_step = m_data_typed->get("step", "");
}

::cw::lib::Integer SetStepUpContent::action()
{
    const auto sdata = system_data(); const auto event = event_interface() ? event_interface()->get_nowrunningevent() : std::nullopt;
    const auto step = sdata ? sdata->find_step(value_string(m_step), is_differentscenario(std::nullopt), event) : std::nullopt;
    if (step && *step) (*step)->up(); return 0;
}

::cw::lib::String SetStepUpContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    const auto sdata = system_data(); const auto step = sdata ? sdata->find_step(value_string(m_step), is_differentscenario(arg_event), arg_event) : std::nullopt;
    return step ? "ステップ『" + (*step)->m_name + "』の値を1増加" : "ステップが指定されていません";
}

void ShowPartyContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    EventContentBase::initialize(std::move(arg_data), false);
    const S speed = m_data_typed ? m_data_typed->get("cardspeed", "Default") : "Default";
    m_cardspeed = speed == "Default" ? -1 : ::cw::lib::require_python_integer(speed);
}

::cw::lib::Integer ShowPartyContent::action()
{
    if (!::cw::g_cwpy) return 0;
    const V old = ::cw::g_cwpy->m_override_dealspeed;
    try { if (m_cardspeed != -1) ::cw::g_cwpy->m_override_dealspeed = m_cardspeed; ::cw::g_cwpy->show_party(); }
    catch (...) { ::cw::g_cwpy->m_override_dealspeed = old; throw; }
    ::cw::g_cwpy->m_override_dealspeed = old;
    return 0;
}

::cw::lib::String ShowPartyContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    return "パーティ表示コンテント";
}

void StartBattleContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    EventContentBase::initialize(std::move(arg_data), true);
}

::cw::lib::Integer StartBattleContent::action()
{
    if (!::cw::g_cwpy || is_differentscenario(std::nullopt) || ::cw::g_cwpy->m_in_actionevent) return 0;
    const I areaid = m_data_typed ? m_data_typed->getint(".", "id", 0) : 0;
    if (const auto sdata = system_data(); sdata && sdata->get_battlename(areaid)) {
        ::cw::g_cwpy->m_lock_menucards = true;
        // Python queues the area transition before raising StartBattleError.
        // The event is still unwinding when this content returns; entering the
        // battle synchronously here makes Event::end() see the new battle and
        // rethrow BattleStartBattleError into CWPy::run(), which terminates the
        // worker instead of allowing the queued transition to start normally.
        // Keep the callback on the game queue so it runs after the current
        // event has performed its normal cleanup.
        const auto runtime = ::cw::g_cwpy;
        runtime->exec_func([runtime, areaid](const ::cw::lib::Arguments&, const ::cw::lib::KeywordArguments&) {
            if (runtime && runtime->is_running()) runtime->change_battlearea(areaid);
            return ::cw::lib::Value{};
        });
        // The game-command queue owns the transition input barrier. Do not
        // leave an ownerless animation flag for hide_cards/deal_cards to
        // preserve after the battle has become ready.
        throw ::cw::event::StartBattleError();
    }
    return 0;
}

::cw::lib::String StartBattleContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    const I areaid = m_data_typed ? m_data_typed->getint(".", "id", 0) : 0;
    const auto name = system_data() ? system_data()->get_battlename(areaid) : std::nullopt;
    return name ? "バトルビュー『" + *name + "』" : "バトルエリアが指定されていません";
}

void StartContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    EventContentBase::initialize(std::move(arg_data), false);
}

::cw::lib::String StartContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    return "スタートコンテント: " + (m_data_typed ? m_data_typed->get("name", "") : "");
}

void SubstituteFlagContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    EventContentBase::initialize(std::move(arg_data), true);
    if (!m_data_typed) return;
    m_fromflag = m_data_typed->get("from", ""); m_toflag = m_data_typed->get("to", "");
    const S speed = m_data_typed->get("cardspeed", "Default");
    m_cardspeed = speed == "Default" ? -1 : ::cw::lib::require_python_integer(speed);
    m_overridecardspeed = speed != "Default" && m_data_typed->getbool(".", "overridecardspeed", false);
}

::cw::lib::Integer SubstituteFlagContent::action()
{
    const auto sdata = system_data(); const auto event = event_interface() ? event_interface()->get_nowrunningevent() : std::nullopt;
    const bool different = is_differentscenario(std::nullopt);
    const auto from = sdata ? sdata->find_flag(value_string(m_fromflag), different, event) : std::nullopt;
    const auto to = sdata ? sdata->find_flag(value_string(m_toflag), different, event) : std::nullopt;
    if (to && *to) {
        if (from && *from) {
            (*to)->set((*from)->m_value);
            (*to)->redraw_cards(m_cardspeed, m_overridecardspeed);
        } else if (upper_ascii(value_string(m_fromflag)) == "??RANDOM") {
            (*to)->set(::cw::g_cwpy && ::cw::g_cwpy->m_dice ? ::cw::g_cwpy->m_dice->roll(1, 2) == 1 : false);
            (*to)->redraw_cards(m_cardspeed, m_overridecardspeed);
        }
    }
    return 0;
}

::cw::lib::String SubstituteFlagContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    const auto sdata = system_data();
    const auto from = sdata ? sdata->find_flag(value_string(m_fromflag), is_differentscenario(arg_event), arg_event) : std::nullopt;
    const auto to = sdata ? sdata->find_flag(value_string(m_toflag), is_differentscenario(arg_event), arg_event) : std::nullopt;
    if (from && to) return "フラグ『" + value_string(m_fromflag) + "』の値を『" + value_string(m_toflag) + "』へ代入";
    return to && upper_ascii(value_string(m_fromflag)) == "??RANDOM" ? "ランダム値を『" + value_string(m_toflag) + "』へ代入" : "フラグが指定されていません";
}

void SubstituteStepContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    EventContentBase::initialize(std::move(arg_data), true);
    if (m_data_typed) { m_fromstep = m_data_typed->get("from", ""); m_tostep = m_data_typed->get("to", ""); }
}

::cw::lib::Integer SubstituteStepContent::action()
{
    const auto sdata = system_data(); const auto event = event_interface() ? event_interface()->get_nowrunningevent() : std::nullopt;
    const bool different = is_differentscenario(std::nullopt);
    const auto from = sdata ? sdata->find_step(value_string(m_fromstep), different, event) : std::nullopt;
    const auto to = sdata ? sdata->find_step(value_string(m_tostep), different, event) : std::nullopt;
    if (to && *to) {
        if (from && *from) (*to)->set((*from)->m_value);
        else if (upper_ascii(value_string(m_fromstep)) == "??RANDOM") (*to)->set(::cw::g_cwpy && ::cw::g_cwpy->m_dice ? ::cw::g_cwpy->m_dice->roll(1, static_cast<I>((*to)->m_valuenames.size())) - 1 : 0);
        else if (upper_ascii(value_string(m_fromstep)) == "??SELECTEDPLAYER") {
            I selected = 0;
            if (event_interface() && event_interface()->has_selectedmember()) {
                const auto target = event_interface()->get_targetmember("Selected", false, "");
                if (const auto pcard = typed_object<::cw::sprite::card::PlayerCard>(target)) selected = pcard->m_index_typed + 1;
            }
            (*to)->set(selected);
        }
    }
    return 0;
}

::cw::lib::String SubstituteStepContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    const auto sdata = system_data(); const auto from = sdata ? sdata->find_step(value_string(m_fromstep), is_differentscenario(arg_event), arg_event) : std::nullopt;
    const auto to = sdata ? sdata->find_step(value_string(m_tostep), is_differentscenario(arg_event), arg_event) : std::nullopt;
    if (from && to) return "ステップ『" + (*from)->m_name + "』の値を『" + (*to)->m_name + "』へ代入";
    if (to && upper_ascii(value_string(m_fromstep)) == "??RANDOM") return "ランダム値を『" + (*to)->m_name + "』へ代入";
    return "ステップが指定されていません";
}

void TalkContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    EventContentBase::initialize(std::move(arg_data), true);
}

std::vector<std::tuple<::cw::lib::Integer, ::cw::lib::String>> TalkContent::get_selections_and_indexes()
{
    std::vector<std::tuple<I,S>> result; I index = 0;
    const auto children = get_children();
#if defined(CARDWIRTH_LOG_ENABLED)
    std::vector<S> selection_details;
#endif
    for (const auto& original : children) {
        // ContentsLineは先頭の子を分岐として使い、子がなければ範囲外例外となる。
        // 元の項目、または取得した子がnullなら、その項目は処理しない。
        // 空行を名前なしの分岐として残す処理は行わない。
        if (!original) continue; auto child = original;
        if (child->m_tag == "ContentsLine") child = child->get_item(0);
        if (!child) continue;
        // Python keeps the content index unchanged when a Check content
        // suppresses an option.  The index is the branch/content index that
        // the event resumes at, not the ordinal of every XML child.  Counting
        // the suppressed Check here shifts all following return values by one
        // compared with TalkContent.get_selections_and_indexes().
        if (child->m_tag == "Check") { const auto content = get_content(child); if (content && content->action() != 0) {
#if defined(CARDWIRTH_LOG_ENABLED)
            selection_details.push_back("hidden:" + child->m_tag + ":" + child->get("name", ""));
#endif
            continue;
        } }
        else if (child->m_tag == "XmlContent" && (child->get("xmltype", "") == "CheckStep" || child->get("xmltype", "") == "CheckFlag" || child->get("xmltype", "") == "CheckVariant")) { const auto content = get_content(child); if (content && content->action() != 0) {
#if defined(CARDWIRTH_LOG_ENABLED)
            selection_details.push_back("hidden:" + child->m_tag + ":" + child->get("name", ""));
#endif
            continue;
        } }
        const S name = child->get("name", "");
#if defined(CARDWIRTH_LOG_ENABLED)
        selection_details.push_back(std::to_string(index) + ":" + child->m_tag + ":" + name);
#endif
        if (!name.empty()) result.emplace_back(index, name); ++index;
    }
    if (result.empty()) {
        // Intentional native message-resource boundary: Py indexes msgs["ok"].
        // A missing/unavailable/empty native resource retains "OK", ensuring
        // this synthetic confirmation option always has a visible label.
        S label = "OK";
        if (::cw::g_cwpy) {
            const auto* object = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_msgs.storage());
            if (object && *object) {
                if (auto messages = std::dynamic_pointer_cast<::cw::setting::MsgDict>(*object)) {
                    const auto configured = messages->get_item("ok");
                    if (!configured.empty()) label = configured;
                }
            }
        }
        result.emplace_back(0, label);
    }
#if defined(CARDWIRTH_LOG_ENABLED)
    if (result.size() != 1) {
        S details;
        for (const auto& item : selection_details) {
            if (!details.empty()) details += " | ";
            details += item;
        }
        ::cardwirth::debug::log("Talk selections: visible=" + std::to_string(result.size()) +
            " children=" + std::to_string(children.size()) +
            " cwxpath=" + (m_data_typed ? m_data_typed->get_cwxpath() : S{}) +
            " entries=[" + details + "]");
    }
#endif
    return result;
}

void WaitContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    EventContentBase::initialize(std::move(arg_data), false);
}

::cw::lib::Integer WaitContent::action()
{
    if (!::cw::g_cwpy || !m_data_typed) return 0;
    const I value = m_data_typed->getint(".", "value", 0);
    const auto start = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    auto stop_the_world = std::make_shared<::cw::sprite::base::StopTheWorld>();
    stop_the_world->initialize(start, value * 100);
    const auto event = ::cw::g_cwpy->m_event;
    if (event) event->m_breakwait = false;
    while (::cw::g_cwpy->is_running() && stop_the_world->is_waiting() &&
           !(event && event->is_stoped())) {
        const bool can_skip = ::cw::g_cwpy->m_setting &&
                              ::cw::g_cwpy->m_setting->m_can_skipwait;
        if (event) event->refresh_activeitem();
        if (::cw::g_cwpy->wait_frame(1, can_skip, stop_the_world, 0)) break;
        if (can_skip && event && event->m_breakwait) break;
    }
    return 0;
}

::cw::lib::String WaitContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event; const I value = m_data_typed ? m_data_typed->getint(".", "value", 0) : 0; return ::cw::lib::python_float_repr(static_cast<double>(value) / 10.0) + "秒間待機";
}

void XmlContentCustomContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    EventContentBase::initialize(std::move(arg_data), true);
    if (!m_data_typed) return;
    m_xmltype = m_data_typed->get("xmltype", "");
    m_assignment = m_data_typed->getint(".", "assignment", 0);
    // Py retains the first Str element itself at construction. Later child
    // replacement must not redirect this content to a different element.
    const auto data = m_data_typed.lock();
    m_str_elem = data && !data->m_children.empty() && data->m_children.front() &&
        data->m_children.front()->m_tag == "Str" ? data->m_children.front() : nullptr;
}

::cw::lib::Integer XmlContentCustomContent::action()
{
    S active_xmltype = value_string(m_xmltype);
    S active_tag;
    try {
        const auto& str = m_str_elem;
        if (!str || str->m_text.empty()) return 0;
        S xml = str->m_text;
        // Match Python str.replace('\\\\', '\\') exactly: replace each
        // non-overlapping escaped pair once.  Keeping pos at the replacement
        // used to collapse any longer run all the way to one backslash.  That
        // could corrupt a custom XML attribute before UTF-8 XML parsing (in
        // particular variable paths).  Japanese text itself remains untouched.
        std::string::size_type pos = 0;
        while ((pos = xml.find("\\\\", pos)) != S::npos) {
            xml.replace(pos, 2, "\\");
            ++pos;
        }
        if (m_assignment == 1) {
            xml = _replace_variant_placeholders(xml);
        }
        const V cleaned = clean_xmltext(V(xml));
        const S text = value_string(cleaned);
        auto element = ::cw::data::xml2element("", "", std::nullopt, text, true, std::nullopt);
        if (!element) throw std::runtime_error("カスタムXMLコンテントのXMLパースでエラーが発生しました。");
        _copy_element_context(m_data_typed, element);

        const S xmltype = active_xmltype;
        active_tag = element->m_tag;
        // Python's generated element keeps the XmlContent wrapper's event-line
        // context.  Therefore a custom Branch without an embedded Contents
        // node sees the wrapper's following sibling as its ○/× child.  The C++
        // action() 時にはイベントループが m_line_index
        // を進めているため、 切り離された生成要素は get_children()
        // で次の兄弟を取得できず、誤ってTREE_ENDを返す。
        // 生成したBranchにも同じ行内の継続先を設定する。
        const auto embedded_contents = element->find("Contents");
        if (active_tag == "Branch" && (!embedded_contents || !*embedded_contents)) {
            const auto metadata_parent = m_data_typed->m_cwxparent && *m_data_typed->m_cwxparent
                ? *m_data_typed->m_cwxparent : nullptr;
            const auto runtime_parent = m_data_typed->m_parent.lock();
            const auto parent = metadata_parent ? metadata_parent : runtime_parent;
            if (parent && parent->m_tag == "ContentsLine") {
                const auto source = std::find(parent->m_children.begin(), parent->m_children.end(), m_data_typed.lock());
                if (source != parent->m_children.end() && std::next(source) != parent->m_children.end()) {
                    auto contents = ::cw::data::make_element("Contents");
                    contents->append(::cw::data::copydata(*std::next(source), true));
                    element->append(contents);
                    // 生成ルート自体は元の行の要素ではないため、 get_children()
                    // に上で複製した継続先を使わせる。
                    element->m_cwxparent.reset();
                }
            }
        }
        // SetVariantStr は XML 本体ではなく、Set/Expression へ変換してから
        // 通常の SetVariantContent と同じ calculator 経路を通す。
        if (xmltype == "SetVariantStr") {
            const S variant = element->get("variant", "");
            S raw = element->m_text;
            std::size_t quote = 0;
            while ((quote = raw.find('"', quote)) != S::npos) { raw.insert(quote, 1, '"'); quote += 2; }
            const bool insert = element->getbool(".", "insert", false);
            const S expression = insert ? ("@\"" + variant + "\" ~ \"" + raw + "\"") : ("\"" + raw + "\"");
            auto target = ::cw::data::make_element("Set", "", std::map<S, S>{{"type", "Variant"}, {"variant", variant}});
            target->append(::cw::data::make_element("Expression", expression));
            SetVariantContent content;
            content.initialize(target);
            return content.action();
        }
        if (xmltype == "EndAction") {
            EndActionEventContent content;
            content.initialize(element);
            return content.action();
        }
        if (xmltype == "SetActionEvent") {
            SetActionEventContent content;
            content.initialize(element);
            return content.action();
        }
        if (xmltype == "CheckVariant") {
            // CardWirthPyと同様、独自CheckVariant
            // の不正な式は、シナリオを中止せず意図的にfalseとする。
            try {
                CheckVariantContent content;
                content.initialize(element);
                return content.action();
            } catch (const ::cw::event::EventError&) {
                // variant_error でユーザーがイベント中止を選んだ場合は
                // 、式のfalseではない。 この独自の代替処理からも外へ伝える。
                throw;
            } catch (...) {
                return 0;
            }
        }
        if (xmltype == "InputDialog") {
            const S title = m_data_typed ? m_data_typed->get("name", "") : S{};
            const S message = str->get("name", "");
            InputDialogContent content;
            content.initialize(element, title, message);
            return content.action();
        }
        if (xmltype == "MultipleMoveLose") {
            MultipleMoveLoseBgImageContent content;
            content.initialize(element, true);
            return content.action();
        }
        if (xmltype == "ChangeEnemy") {
            // 生成XMLのルートは通常のContentではなくEnemyCard
            // なので、 Python版と同様、この独自種類を明示的に振り分ける。
            ChangeEnemyContent content;
            content.initialize(element, true);
            return content.action();
        }
        // Python版と同様、独自XMLは生成ルートのタグ・種類で
        // はなく、ラッパーのxmltypeで振り分ける。 その識別子
        // を明示して通常の生成・処理を使い、 GetItemの文字列
        // にLoseルートがあってもLoseItemを実行しない。
        if (xmltype == "SubstituteFlagContent") {
            SubstituteFlagContent content;
            content.initialize(element);
            return content.action();
        }
        if (xmltype.empty()) return 0;
        // Python版の独自XMLの振り分けと同様、明示したxmltype
        // の一覧だけを許可する。 通常のコンテント生成が扱える追加の
        // 種類も、このラッパーで名前を指定するだけでは実行できない。
        static const std::vector<S> allowed_xmltypes{
            "ChangeBgImage",
            "MoveBgImage",
            "LoseBgImage",
            "ReplaceBgImage",
            "Effect",
            "MoveCard",
            "ChangeEnemy",
            "Wait",
            "GetMoney",
            "SetFlag",
            "ReverseFlag",
            "BranchFlag",
            "BranchFlagValue",
            "BranchStep",
            "BranchStepValue",
            "SetStep",
            "SetStepUp",
            "SetStepDown",
            "SubstituteStep",
            "BranchSelect",
            "BranchAbility",
            "BranchRandom",
            "BranchLevel",
            "BranchStatus",
            "BranchPartyNumber",
            "BranchRound",
            "BranchCast",
            "BranchItem",
            "BranchSkill",
            "BranchBeast",
            "BranchInfo",
            "BranchMoney",
            "BranchCoupon",
            "BranchMultiCoupon",
            "BranchCompleteStamp",
            "BranchGossip",
            "BranchKeyCode",
            "TalkMessage",
            "CheckStep",
            "CheckFlag",
            "CheckVariant",
            "SetVariant",
            "SetVariantStr",
            "SetActionEvent",
            "EndAction",
            "TalkDialog",
            "InputDialog",
            "PlayBgm",
            "PlaySound",
            "GetCast",
            "GetItem",
            "GetSkill",
            "GetBeast",
            "GetInfo",
            "GetCompleteStamp",
            "GetGossip",
            "GetCoupon",
            "LoseCast",
            "LoseItem",
            "LoseSkill",
            "LoseBeast",
            "LoseInfo",
            "LoseMoney",
            "LoseCoupon",
            "LoseCompleteStamp",
            "LoseGossip",
            "MultipleMoveLose",
            "SubstituteFlagContent"
        };
        if (std::none_of(allowed_xmltypes.begin(), allowed_xmltypes.end(),
                         [&](const S& allowed) { return xmltype == allowed; }))
            throw std::runtime_error("Unknown xmltype: " + xmltype);
        const auto content = get_typed_content(element, xmltype + "Content");
        if (!content) throw std::runtime_error("Unknown xmltype: " + xmltype);
        return content->action();
    } catch (const ::cw::event::ActionBreakError&) {
        throw;
    } catch (const ::cw::event::AreaChangeError&) {
        throw;
    } catch (const ::cw::event::ScenarioEndError&) {
        throw;
    } catch (const ::cw::event::ScenarioBadEndError&) {
        throw;
    } catch (const ::cw::event::XmlContentCriticalError& error) {
        if (!error.m_msg.empty()) throw;
        throw ::cw::event::XmlContentCriticalError(
            "カスタムXMLコンテントでエラーが発生しました。理由: 詳細情報のないXMLコンテント例外です。");
    } catch (const ::cw::event::EventError&) {
        // キャンセルした式エラーを含むEffectBreakError
        // などのイベント制御型は、std::exceptionを継
        // 承しない。 不明な致命的エラーへ変えず、実際の型を保つ。
        throw;
    } catch (const ::cw::calculator::ComputeException& error) {
        throw ::cw::event::XmlContentCriticalError(
            "カスタムXMLコンテントでエラーが発生しました。理由: " + variant_error_msg(error));
    } catch (const std::exception& error) {
        // Python版と同じく、XML構文/未知xmltype/下位Contentの標準例外を
        // カスタムXML失敗へ正規化する（制御用EventErrorは上記で保持）。
        const S reason = error.what();
        const S context = " [xmltype=" + (active_xmltype.empty() ? S("<empty>") : active_xmltype) +
                          " tag=" + (active_tag.empty() ? S("<unparsed>") : active_tag) + "]";
        throw ::cw::event::XmlContentCriticalError(
            S("カスタムXMLコンテントでエラーが発生しました。理由:") + context + " " +
            (reason.empty() ? "詳細情報のない標準例外です。" : reason));
    } catch (...) {
        throw ::cw::event::XmlContentCriticalError(
            "カスタムXMLコンテントでエラーが発生しました。理由: 例外の詳細情報を取得できませんでした。");
    }
}

::cw::lib::String XmlContentCustomContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event; return "XMLコンテント: " + value_string(m_xmltype);
}

void BranchAbilityContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    BranchContent::initialize(std::move(arg_data));
    if (!m_data_typed) return;
    m_level = m_data_typed->getint(".", "value", 0);
    m_vocation = {m_data_typed->get("physical", ""), m_data_typed->get("mental", "")};
    m_targetm = m_data_typed->get("targetm", "");
    m_invert = m_data_typed->getbool(".", "invert", false);
    if (m_targetm.size() >= 5 && m_targetm.compare(m_targetm.size() - 5, 5, "Sleep") == 0) {
        // Python版と同様、元の範囲名がSleepで終わる場合だけ、すべての出現を除く。
        std::size_t sleep_pos = 0;
        while ((sleep_pos = m_targetm.find("Sleep", sleep_pos)) != S::npos) {
            m_targetm.erase(sleep_pos, 5);
        }
        m_sleep = true;
    } else m_sleep = false;
    if (m_targetm == "Random") { m_targetm = "Party"; m_someone = true; }
    else if (m_targetm == "Party") m_someone = false;
    else m_someone = true;
}

::cw::lib::Integer BranchAbilityContent::action()
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_event) return get_boolean_index(false);
    std::vector<std::shared_ptr<::cw::character::Character>> targets;
    const V target = ::cw::g_cwpy->m_event->get_targetmember(m_targetm, true, "");
    if (auto character = typed_object<::cw::character::Character>(target)) targets.push_back(character);
    else if (const auto sequence = typed_object<::cw::data::VariantSequence>(target))
        for (const auto& item : sequence->m_values)
            if (auto target_character = typed_object<::cw::character::Character>(item)) targets.push_back(target_character);
    bool flag = false; std::shared_ptr<::cw::character::Character> selected;
    for (const auto& targetcard : targets) {
        if (!targetcard || !targetcard->is_alive() || (!m_sleep && (targetcard->is_sleep() || targetcard->is_bind()))) continue;
        const bool outcome = targetcard->decide_outcome(m_level,
            {value_string(std::get<0>(m_vocation)), value_string(std::get<1>(m_vocation))},
            6, targetcard->get_enhance_act(), 0);
        bool result = m_invert ? !outcome : outcome;
        flag = result;
        if (result && m_someone) { selected = targetcard; break; }
        if (!result && !m_someone) { selected = targetcard; break; }
    }
    if (m_targetm != "Selected" && ::cw::g_cwpy->m_event) {
        if (m_targetm == "Party" && !m_someone && flag) selected.reset();
        if (selected) ::cw::g_cwpy->m_event->set_selectedmember(selected); else ::cw::g_cwpy->m_event->clear_selectedmember();
    }
    return get_boolean_index(flag);
}

::cw::lib::String BranchAbilityContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    return "判定分岐コンテント";
}

::cw::lib::String BranchAbilityContent::get_childname(std::shared_ptr<::cw::data::CWPyElement> arg_child, std::shared_ptr<::cw::event::Event> arg_event)
{
    (void)arg_event;
    // 不正・不明な適性ラベルは、ネイティブの診断では空にする。 Python
    // 版は未知の身体・精神の辞書検索をNoneと表示し、属性なし
    // では失敗する場合がある。 有効なラベルは同じ表示名を使う。
    const S physical = status_name(m_data_typed ? m_data_typed->get("physical", "") : "");
    const S mental = status_name("mental_" + (m_data_typed ? m_data_typed->get("mental", "") : ""));
    const S scope = status_name(m_data_typed ? m_data_typed->get("targetm", "") : "");
    S result = scope + "がレベル" + (m_data_typed ? m_data_typed->get("value", "0") : "0") + "で " + physical + "と " + mental + "で行う";
    result += m_invert ? "能力不足" : "能力";
    result += get_contentname(arg_child, "") == "○" ? "判定に成功" : "判定に失敗";
    return result;
}

void BranchAreaContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    BranchContent::initialize(std::move(arg_data));
}

::cw::lib::Integer BranchAreaContent::action()
{
    if (is_differentscenario(std::nullopt)) return 0;
    auto sdata = system_data(); if (!sdata || !::cw::g_cwpy) return get_value_index(-1);
    // 不正な戦闘状態では、戦闘前の情報がない場合がある。 Python
    // 版のように不在の記録を参照せず現在エリアを代わりに
    // 使うが、 通常の戦闘は記録された元エリアを使う。
    I value = ::cw::g_cwpy->m_areaid;
    if (::cw::g_cwpy->m_battle && sdata->m_pre_battleareadata) value = std::get<0>(*sdata->m_pre_battleareadata);
    return get_value_index(value);
}

::cw::lib::String BranchAreaContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    return "エリア分岐コンテント";
}

::cw::lib::String BranchAreaContent::get_childname(std::shared_ptr<::cw::data::CWPyElement> arg_child, std::shared_ptr<::cw::event::Event> arg_event)
{
    (void)arg_event;
    const auto resid = ::cw::lib::PythonInteger::parse(get_contentname(arg_child, ""));
    S name = "その他";
    if (resid && !(*resid == ::cw::lib::PythonInteger(-1))) {
        if (auto sdata = system_data()) {
            // リソース表のキーはネイティブ整数の範囲を持つ。 その範囲外の有効なPython
            // 整数は、不正な文字列や-1ではなく、存在しないIDとして扱う。
            const bool native_id = *resid >= ::cw::lib::PythonInteger(std::numeric_limits<I>::min()) &&
                *resid <= ::cw::lib::PythonInteger(std::numeric_limits<I>::max());
            const auto n = native_id ? sdata->get_areaname(resid->to_integer()) : std::nullopt;
            name = n ? *n : "指定無し";
        }
    }
    return "エリア = " + name;
}

void BranchBattleContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    BranchContent::initialize(std::move(arg_data));
}

::cw::lib::Integer BranchBattleContent::action()
{
    // Python版と同様、別シナリオからのイベントは何もしない
    // 。 この分岐のDefault/-1の子を選ぶものではない。
    if (is_differentscenario(std::nullopt)) return 0;
    if (!::cw::g_cwpy) return get_value_index(-1);
    const I value = ::cw::g_cwpy->m_battle && *::cw::g_cwpy->m_battle
        ? ::cw::g_cwpy->m_areaid
        : (::cw::g_cwpy->m_winevent_areaid && *::cw::g_cwpy->m_winevent_areaid != 0
            ? *::cw::g_cwpy->m_winevent_areaid : -1);
    return get_value_index(value);
}

::cw::lib::String BranchBattleContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    return "バトル分岐コンテント";
}

::cw::lib::String BranchBattleContent::get_childname(std::shared_ptr<::cw::data::CWPyElement> arg_child, std::shared_ptr<::cw::event::Event> arg_event)
{
    (void)arg_event;
    const auto resid = ::cw::lib::PythonInteger::parse(get_contentname(arg_child, ""));
    S name = "その他";
    if (resid && !(*resid == ::cw::lib::PythonInteger(-1))) {
        if (auto sdata = system_data()) {
            // エリアIDと同様、int64の範囲外はPythonのintの解析失敗ではない。
            const bool native_id = *resid >= ::cw::lib::PythonInteger(std::numeric_limits<I>::min()) &&
                *resid <= ::cw::lib::PythonInteger(std::numeric_limits<I>::max());
            const auto n = native_id ? sdata->get_battlename(resid->to_integer()) : std::nullopt;
            name = n ? *n : "指定無し";
        }
    }
    return "バトル = " + name;
}

void BranchBeastContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    BranchContent::initialize(std::move(arg_data));
}

::cw::lib::Integer BranchBeastContent::action()
{
    return branch_cards("BeastCard");
}

::cw::lib::String BranchBeastContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event; auto sdata = system_data(); const auto name = sdata ? sdata->get_beastname(m_data_typed->getint(".", "id", 0)) : std::nullopt;
    return name ? "召喚獣カード『" + *name + "』所持分岐" : "召喚獣カードが指定されていません";
}

::cw::lib::String BranchBeastContent::get_childname(std::shared_ptr<::cw::data::CWPyElement> arg_child, std::shared_ptr<::cw::event::Event> arg_event)
{
    (void)arg_event; auto sdata = system_data(); const auto name = sdata ? sdata->get_beastname(m_data_typed->getint(".", "id", 0)) : std::nullopt;
    if (!name) return "召喚獣カードが指定されていません"; bool success = get_contentname(arg_child, "") == "○"; if (m_invert) success = !success; const S scope = m_data_typed->get("targets", ""); if (scope == "SelectedCard") return "選択カードが『" + *name + "』と一致" + (success ? "する" : "しない"); const S target = status_name(scope); return target + "が『" + *name + "』を所有して" + (success ? "いる" : "いない");
}

void BranchCastContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    BranchContent::initialize(std::move(arg_data));
}

::cw::lib::Integer BranchCastContent::action()
{
    if (is_differentscenario(std::nullopt)) return 0; const auto sdata = system_data(); if (!sdata) return get_boolean_index(false); const I resid = m_data_typed->getint(".", "id", 0); bool flag = false; for (const auto& card : sdata->m_friendcards) if (card && card->m_id == resid) { flag = true; break; } return get_boolean_index(flag);
}

::cw::lib::String BranchCastContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event; const auto sdata = system_data(); const auto name = sdata ? sdata->get_castname(m_data_typed->getint(".", "id", 0)) : std::nullopt; return name ? "キャスト『" + *name + "』存在分岐" : "キャストが指定されていません";
}

::cw::lib::String BranchCastContent::get_childname(std::shared_ptr<::cw::data::CWPyElement> arg_child, std::shared_ptr<::cw::event::Event> arg_event)
{
    (void)arg_event; const auto sdata = system_data(); const auto name = sdata ? sdata->get_castname(m_data_typed->getint(".", "id", 0)) : std::nullopt; const S n = name ? *name : "指定無し"; return "キャスト『" + n + "』が" + (get_contentname(arg_child, "") == "○" ? "加わっている" : "加わっていない");
}

void BranchCompleteStampContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    BranchContent::initialize(std::move(arg_data));
}

::cw::lib::Integer BranchCompleteStampContent::action()
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_ydata || !*::cw::g_cwpy->m_ydata) return 0; return get_boolean_index((*::cw::g_cwpy->m_ydata)->has_compstamp(m_data_typed->get("scenario", "")));
}

::cw::lib::String BranchCompleteStampContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event; const S name = m_data_typed->get("scenario", ""); return name.empty() ? "終了シナリオが指定されていません" : "終了シナリオ『" + name + "』分岐";
}

::cw::lib::String BranchCompleteStampContent::get_childname(std::shared_ptr<::cw::data::CWPyElement> arg_child, std::shared_ptr<::cw::event::Event> arg_event)
{
    (void)arg_event; const S name = m_data_typed->get("scenario", ""); return "シナリオ『" + name + "』が終了済" + (get_contentname(arg_child, "") == "○" ? "である" : "ではない");
}

void BranchCouponContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    BranchContent::initialize(std::move(arg_data));
    if (!m_data_typed) return;
    const auto scope = _get_couponscope(m_data_typed->get("targets", ""));
    m_scope = std::get<0>(scope); m_someone = std::get<1>(scope); m_unreversed = std::get<2>(scope);
    // Python版と同様、構築時の称号一覧を保持する。 後のXML
    // 編集では、操作の入力もデバッガーの称号名表示も変えない。
    std::vector<V> names;
    const S coupon=m_data_typed->get("coupon", "");
    if(!coupon.empty())names.emplace_back(coupon);
    if(const auto coupons=m_data_typed->find("Coupons");coupons && *coupons)
        for(const auto& item:(*coupons)->m_children)if(item && !item->m_text.empty())names.emplace_back(item->m_text);
    m_couponnames=variant_list_value(names);
    m_matchingtype = m_data_typed->get("matchingtype", ""); m_invert = m_data_typed->getbool(".", "invert", false); m_spchars = m_data_typed->getbool(".", "spchars", false); m_result_var = m_data_typed->get("result_var", "");
}

::cw::lib::Integer BranchCouponContent::action()
{
    const I false_index = get_boolean_index(false);
    if (!m_data_typed) return false_index;
    std::vector<S> names;
    if(const auto sequence=value_sequence(m_couponnames))for(const auto& name:sequence->m_values)names.push_back(value_string(name));
    if (names.empty()) { set_result_variable(value_string(m_result_var), variant_list_value({})); return false_index; }
    if (m_spchars) for (auto& name : names) name = std::get<0>(::cw::sprite::message::rpl_specialstr(name, std::nullopt, true, "All", true, true, std::nullopt));
    const bool allmatch = value_string(m_matchingtype) == "And";
    S scope = value_string(m_scope); bool someone = value_bool(m_someone, true); bool unreversed = value_bool(m_unreversed, false);
    // 1.20以前の互換上の不具合を維持する。選択メンバ
    // ーがいない場合、Selectedの称号判定はパーテ
    // ィを調べる。 Python版が旧シナリオ用に残す
    // 分岐なので、現行の対象なしの結果へ置き換えない。
    if (::cw::g_cwpy && ::cw::g_cwpy->m_event && !::cw::g_cwpy->m_event->has_selectedmember() && scope == "Selected" && ::cw::g_cwpy->m_sct && system_data() && ::cw::g_cwpy->m_sct->lessthan("1.20", system_data()->get_versionhint(0))) { scope = "Party"; }
    std::vector<std::shared_ptr<::cw::character::Character>> targets;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_event) for (const auto& item : ::cw::g_cwpy->m_event->get_targetscope(scope, unreversed, false, "")) if (auto target = typed_object<::cw::character::Character>(item)) targets.push_back(target);
    // Python・CardWirthの対象ゼロの互換結果を維持する
    // 。 Selected・Npc以外の範囲では、全対象が非表示・不
    // 在なら成功とし、対象なしを失敗とする扱いとは意図的に分ける。
    if (targets.empty() && scope != "Npc") {
        if (::cw::g_cwpy && ::cw::g_cwpy->m_event) ::cw::g_cwpy->m_event->clear_selectedmember();
        set_result_variable(value_string(m_result_var), variant_list_value({}));
        return get_boolean_index(scope != "Selected");
    }
    const bool success = _has_coupon(targets, names, scope, someone, allmatch, false, m_invert);
    if (!value_string(m_result_var).empty()) {
        std::vector<V> indexes; for (const auto& owner : _get_coupon_owners(targets, names, allmatch)) indexes.emplace_back(::cw::calculator::_ccard_index(owner));
        set_result_variable(value_string(m_result_var), variant_list_value(indexes));
    }
    return get_boolean_index(success);
}

::cw::lib::String BranchCouponContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    std::vector<S> names;
    if(const auto sequence=value_sequence(m_couponnames))for(const auto& name:sequence->m_values)names.push_back(value_string(name));
    if (names.empty() || names.front().empty()) return "称号が指定されていません";
    S joined; for (const auto& name : names) { if (!joined.empty()) joined += "」「"; joined += name; }
    const S type = names.size() > 1 ? (value_string(m_matchingtype) == "And" ? "全ての" : "どれか一つの") : "";
    return "称号「" + joined + "」の" + type + (m_invert ? "不所有" : "所有") + "で分岐" + (m_spchars ? "(特殊文字を展開)" : "");
}

::cw::lib::String BranchCouponContent::get_childname(std::shared_ptr<::cw::data::CWPyElement> arg_child, std::shared_ptr<::cw::event::Event> arg_event)
{
    (void)arg_event;
    std::vector<S> names;
    if(const auto sequence=value_sequence(m_couponnames))for(const auto& name:sequence->m_values)names.push_back(value_string(name));
    S joined;
    const bool named=!names.empty() && !names.front().empty();
    if(named)for(std::size_t i=0;i<names.size();++i){if(i)joined+="」「";joined+=names[i];}
    const S scope = m_data_typed ? m_data_typed->get("targets", "") : ""; const S target = status_name(scope);
    const S type = named && names.size() > 1 ? (value_string(m_matchingtype) == "And" ? "の全て" : "のどれか一つ") : "";
    bool success = get_contentname(arg_child, "") == "○"; if (m_invert) success = !success;
    return target + "が称号「" + joined + "」" + type + "を所有して" + (success ? "いる" : "いない") + (m_spchars ? "(特殊文字を展開)" : "");
}

void BranchFlagContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    BranchContent::initialize(std::move(arg_data));
    m_flag = m_data_typed ? m_data_typed->get("flag", "") : V{};
}

::cw::lib::Integer BranchFlagContent::action()
{
    auto sdata = system_data();
    if (!sdata) return get_children_num() ? 0 : tree_end();
    const auto flag = sdata->find_flag(value_string(m_flag), is_differentscenario(std::nullopt),
        ::cw::g_cwpy && ::cw::g_cwpy->m_event ? ::cw::g_cwpy->m_event->get_nowrunningevent() : std::nullopt);
    return flag ? get_boolean_index((*flag)->m_value) : (get_children_num() ? 0 : tree_end());
}

::cw::lib::String BranchFlagContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    const auto sdata = system_data();
    if (!sdata) return "フラグが指定されていません";
    const auto flag = sdata->find_flag(value_string(m_flag), is_differentscenario(arg_event), arg_event);
    return flag ? "フラグ『" + (*flag)->m_name + "』分岐" : "フラグが指定されていません";
}

::cw::lib::String BranchFlagContent::get_childname(std::shared_ptr<::cw::data::CWPyElement> arg_child, std::shared_ptr<::cw::event::Event> arg_event)
{
    const auto sdata = system_data();
    if (!sdata) return "フラグが指定されていません";
    const auto flag = sdata->find_flag(value_string(m_flag), is_differentscenario(arg_event), arg_event);
    if (!flag) return "フラグが指定されていません";
    return (*flag)->m_name + " = " + (*flag)->get_valuename(get_contentname(arg_child, "") == "○");
}

void BranchFlagValueContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    BranchContent::initialize(std::move(arg_data));
    if (m_data_typed) { m_fromflag = m_data_typed->get("from", ""); m_toflag = m_data_typed->get("to", ""); }
}

::cw::lib::Integer BranchFlagValueContent::action()
{
    const auto sdata = system_data(); const auto event = event_interface() ? event_interface()->get_nowrunningevent() : std::nullopt;
    const bool different = is_differentscenario(std::nullopt);
    const auto from = sdata ? sdata->find_flag(value_string(m_fromflag), different, event) : std::nullopt;
    const auto to = sdata ? sdata->find_flag(value_string(m_toflag), different, event) : std::nullopt;
    if (from && to) return get_boolean_index((*from)->m_value == (*to)->m_value);
    return different ? 0 : tree_end();
}

::cw::lib::String BranchFlagValueContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    const auto sdata = system_data();
    const auto from = sdata ? sdata->find_flag(value_string(m_fromflag), is_differentscenario(arg_event), arg_event) : std::nullopt;
    const auto to = sdata ? sdata->find_flag(value_string(m_toflag), is_differentscenario(arg_event), arg_event) : std::nullopt;
    return from && to ? "フラグ『" + (*from)->m_name + "』と『" + (*to)->m_name + "』を比較" : "フラグが指定されていません";
}

::cw::lib::String BranchFlagValueContent::get_childname(std::shared_ptr<::cw::data::CWPyElement> arg_child, std::shared_ptr<::cw::event::Event> arg_event)
{
    const auto sdata = system_data();
    const auto from = sdata ? sdata->find_flag(value_string(m_fromflag), is_differentscenario(std::optional<std::shared_ptr<::cw::event::Event>>(arg_event)), arg_event) : std::nullopt;
    const auto to = sdata ? sdata->find_flag(value_string(m_toflag), is_differentscenario(std::optional<std::shared_ptr<::cw::event::Event>>(arg_event)), arg_event) : std::nullopt;
    if (!from || !to) return "フラグが指定されていません";
    return get_contentname(arg_child, "") == "○" ? "フラグ『" + (*from)->m_name + "』が『" + (*to)->m_name + "』と同値" :
           "フラグ『" + (*from)->m_name + "』が『" + (*to)->m_name + "』と異なる";
}

void BranchGossipContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    BranchContent::initialize(std::move(arg_data)); m_gossip = m_data_typed->get("gossip", ""); m_spchars = m_data_typed->getbool(".", "spchars", false);
}

::cw::lib::Integer BranchGossipContent::action()
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_ydata || !*::cw::g_cwpy->m_ydata) return 0;
    S gossip = value_string(m_gossip);
    if (m_spchars) gossip = std::get<0>(::cw::sprite::message::rpl_specialstr(gossip, std::nullopt, true, "All", true, true, std::nullopt));
    return get_boolean_index((*::cw::g_cwpy->m_ydata)->has_gossip(gossip));
}

::cw::lib::String BranchGossipContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    return "ゴシップ分岐コンテント";
}

::cw::lib::String BranchGossipContent::get_childname(std::shared_ptr<::cw::data::CWPyElement> arg_child, std::shared_ptr<::cw::event::Event> arg_event)
{
    (void)arg_event; return "ゴシップ『" + value_string(m_gossip) + "』が宿屋に" + (get_contentname(arg_child, "") == "○" ? "ある" : "ない") + (m_spchars ? "(特殊文字を展開)" : "");
}

void BranchInfoContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    BranchContent::initialize(std::move(arg_data)); m_resid = m_data_typed->getint(".", "id", 0);
}

::cw::lib::Integer BranchInfoContent::action()
{
    if (is_differentscenario(std::nullopt) || !system_data()) return 0; return get_boolean_index(system_data()->has_infocard(m_resid));
}

::cw::lib::String BranchInfoContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event; const auto name = system_data() ? system_data()->get_infoname(m_data_typed->getint(".", "id", 0)) : std::nullopt; return name ? "情報カード『" + *name + "』存在分岐" : "情報カードが指定されていません";
}

::cw::lib::String BranchInfoContent::get_childname(std::shared_ptr<::cw::data::CWPyElement> arg_child, std::shared_ptr<::cw::event::Event> arg_event)
{
    (void)arg_event; const auto name = system_data() ? system_data()->get_infoname(m_data_typed->getint(".", "id", 0)) : std::nullopt; const S n = name ? *name : "指定無し"; return "情報カード『" + n + "』を所持して" + (get_contentname(arg_child, "") == "○" ? "いる" : "いない");
}

void BranchIsBattleContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    BranchContent::initialize(std::move(arg_data));
}

::cw::lib::Integer BranchIsBattleContent::action()
{
    return get_boolean_index(::cw::g_cwpy && ::cw::g_cwpy->m_battle.has_value() && *::cw::g_cwpy->m_battle);
}

::cw::lib::String BranchIsBattleContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    return "戦闘判定コンテント";
}

::cw::lib::String BranchIsBattleContent::get_childname(std::shared_ptr<::cw::data::CWPyElement> arg_child, std::shared_ptr<::cw::event::Event> arg_event)
{
    (void)arg_event; return get_contentname(arg_child, "") == "○" ? "イベント発生時の状況が戦闘中" : "イベント発生時の状況が戦闘以外";
}

void BranchItemContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    BranchContent::initialize(std::move(arg_data));
}

::cw::lib::Integer BranchItemContent::action()
{
    return branch_cards("ItemCard");
}

::cw::lib::String BranchItemContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event; auto sdata = system_data(); const auto name = sdata ? sdata->get_itemname(m_data_typed->getint(".", "id", 0)) : std::nullopt; return name ? "アイテムカード『" + *name + "』所持分岐" : "アイテムカードが指定されていません";
}

::cw::lib::String BranchItemContent::get_childname(std::shared_ptr<::cw::data::CWPyElement> arg_child, std::shared_ptr<::cw::event::Event> arg_event)
{
    (void)arg_event; auto sdata = system_data(); const auto name = sdata ? sdata->get_itemname(m_data_typed->getint(".", "id", 0)) : std::nullopt; if (!name) return "アイテムカードが指定されていません"; bool success = get_contentname(arg_child, "") == "○"; if (m_invert) success = !success; const S scope = m_data_typed->get("targets", ""); if (scope == "SelectedCard") return "選択カードが『" + *name + "』と一致" + (success ? "する" : "しない"); return status_name(scope) + "が『" + *name + "』を所有して" + (success ? "いる" : "いない");
}

void BranchKeyCodeContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    BranchContent::initialize(std::move(arg_data));
    if (!m_data_typed) return;
    m_targetkc = m_data_typed->get("targetkc", "Selected");
    m_keycode = m_data_typed->get("keyCode", "");
    m_result_var = m_data_typed->get("result_var", "");
    const S cardtype = m_data_typed->get("effectCardType", "All");
    m_skill = cardtype == "All" || cardtype == "Skill";
    m_item = cardtype == "All" || cardtype == "Item";
    m_beast = cardtype == "All" || cardtype == "Beast";
    m_hand = false;
    // 不正XMLへの意図的な境界として、明示的に空のWsn.2
    // 上書きは、初期化を拒否せずそのカード種類を無効にする。
    // Python版は既定値なしのgetboolで空属性を拒否
    // する。 空でない値は、全OSで共通の真偽値解析を使う。
    if (m_data_typed->m_attributes.count("skill")) m_skill = m_data_typed->getbool(".", "skill", false);
    if (m_data_typed->m_attributes.count("item")) m_item = m_data_typed->getbool(".", "item", false);
    if (m_data_typed->m_attributes.count("beast")) m_beast = m_data_typed->getbool(".", "beast", false);
    if (m_data_typed->m_attributes.count("hand")) m_hand = m_data_typed->getbool(".", "hand", false);
    m_selectcard = m_data_typed->getbool(".", "selectcard", false);
    m_invert = m_data_typed->getbool(".", "invert", false);
    m_condition = m_data_typed->get("condition", "Has");
}

bool BranchKeyCodeContent::_matches_keycode(std::shared_ptr<::cw::header::CardHeader> arg_header)
{
    if (!arg_header) return false;
    // Pythonの get_keycodes() と同じ、バージ
    // ョンを考慮した一覧を使う。 保存領域を直接読むと、1.20以
    // 前のシナリオでも誤ってカード名をキーコードに含めてしまう。
    const auto keycodes = arg_header->get_keycodes(true);
    const bool has = std::find(keycodes.begin(), keycodes.end(), value_string(m_keycode)) != keycodes.end();
    return value_string(m_condition) == "HasNot" ? !has : has;
}

std::vector<std::shared_ptr<::cw::header::CardHeader>> BranchKeyCodeContent::_find_keycode_cards(::cw::lib::Value arg_target)
{
    std::vector<std::shared_ptr<::cw::header::CardHeader>> result;
    std::set<const ::cw::header::CardHeader*> seen;
    auto append = [&](const std::shared_ptr<::cw::header::CardHeader>& header) {
        if (!header || !_matches_keycode(header)) return;
        std::shared_ptr<::cw::header::CardHeader> original = header->get_original();
        if (!original) original = header;
        if (seen.insert(original.get()).second) result.push_back(header);
    };
    if (auto character = typed_object<::cw::character::Character>(arg_target)) {
        // Python版と同様、実際の手札、actiondataの選択カード
        // 、Deck.get_used()を含む戦闘手札全体を先に検索する
        // 。 行動の選択・実行中は、それぞれ別のオブジェクトの場合がある。
        if (m_hand && character->m_deck) {
            for (const auto& h : character->m_deck->get_hand(character)) append(h);
            if (const auto action = typed_object<::cw::header::CardHeader>(std::get<1>(character->m_actiondata)))
                append(action);
            if (const auto used = character->m_deck->get_used(); used && *used) append(*used);
        }
        // load_cardpocket_from_xml() は Character::initialize
        // 用の読み込みで、 ここで呼ぶと新しいCardHeaderを作り、XML
        // の子を書き換える。 Python版と同様、現在のポケットを検索する。
        const auto& pockets = character->m_cardpocket_typed;
        if (m_skill) for (const auto& h : std::get<0>(pockets)) append(h);
        if (m_item) for (const auto& h : std::get<1>(pockets)) append(h);
        if (m_beast) for (const auto& h : std::get<2>(pockets)) append(h);
    } else if (auto party = typed_object<::cw::data::Party>(arg_target)) {
        if (!party->m_sorted_backpack_by_order)
            party->sort_backpack(std::optional<::cw::lib::String>("order"), std::nullopt);
        for (const auto& h : party->m_backpack) {
            if (!h) continue;
            if ((h->m_type == "SkillCard" && !m_skill) ||
                (h->m_type == "ItemCard" && !m_item) ||
                (h->m_type == "BeastCard" && !m_beast)) continue;
            append(h);
        }
    }
    return result;
}

std::optional<std::shared_ptr<::cw::data::StructVal>> BranchKeyCodeContent::_cardinfo_from_header(std::shared_ptr<::cw::header::CardHeader> arg_header)
{
    if (!arg_header) return std::nullopt;
    std::shared_ptr<::cw::header::CardHeader> original = arg_header->get_original();
    if (!original) original = arg_header;
    // ネイティブのCARDINFOは、所有者フィールドより先に現在
    // の所持一覧への所属を解決し、 古い・矛盾する所有者が有効なコ
    // ンテナー識別値を上書きしないようにする。 Python版は
    // 所有者を先に見るが、整合した通常の状態では同じ位置になる。
    I castindex = 0, cardindex = 0, actioncardid = -2;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_ydata) {
        auto yado = *::cw::g_cwpy->m_ydata;
        auto store = std::find(yado->m_storehouse.begin(), yado->m_storehouse.end(), original);
        if (store != yado->m_storehouse.end()) { castindex = -2; cardindex = static_cast<I>(store - yado->m_storehouse.begin()) + 1; }
        else if (yado->m_party) {
            auto& bag = (*yado->m_party)->m_backpack;
            auto found = std::find(bag.begin(), bag.end(), original);
            if (found != bag.end()) { castindex = -1; cardindex = static_cast<I>(found - bag.begin()) + 1; }
        }
    }
    if (cardindex == 0) {
        const auto owner = arg_header->get_owner();
        const auto character = owner ? typed_object<::cw::character::Character>(*owner) : nullptr;
        if (!character) return std::nullopt;
        try { castindex = ::cw::calculator::_ccard_index(character); }
        catch (const std::out_of_range&) { return std::nullopt; }
        // Pythonの list.index は、所有者が現在の場のキャ
        // ラクターでなければValueErrorを出し、BranchKeyCode
        // はCARDINFOを省略する。 C++補助処理のゼロも同じ未
        // 発見値なので、キャスト位置ゼロの有効そうな構造を出さない。
        if (castindex == 0) return std::nullopt;
        if (original->m_type == "ActionCard") { cardindex = 0; actioncardid = original->m_id; }
        else {
            try {
                // 未知のカード種類を含むネイティブの位置解決失敗では、
                // 部分的に有効なCARDINFOを出さず省略する。 Python
                // 版の位置検索前の型エラーは伝播しない。 位置は現在
                // の所持一覧を基準にする。ここでXMLを再読するとヘ
                // ッダーが変わり、 上で見つけた全カードが位置検索
                // に失敗してresult_varから消えてしまう。
                const auto& pockets = character->m_cardpocket_typed;
                const I pocket = ::cw::header::cardtype_to_pocket(original->m_type);
                const auto& selected = pocket == ::cw::g_POCKET_SKILL ? std::get<0>(pockets) :
                    (pocket == ::cw::g_POCKET_ITEM ? std::get<1>(pockets) : std::get<2>(pockets));
                const auto found = std::find(selected.begin(), selected.end(), original);
                if (found == selected.end()) return std::nullopt;
                cardindex = 1 + static_cast<I>(found - selected.begin());
                if (pocket > ::cw::g_POCKET_SKILL) cardindex += static_cast<I>(std::get<0>(pockets).size());
                if (pocket > ::cw::g_POCKET_ITEM) cardindex += static_cast<I>(std::get<1>(pockets).size());
            } catch (...) { return std::nullopt; }
        }
    }
    auto result = std::make_shared<::cw::data::StructVal>();
    result->initialize("cardinfo", {V(castindex), V(cardindex), V(actioncardid)});
    return result;
}

::cw::lib::Integer BranchKeyCodeContent::action()
{
    bool success = false;
    std::shared_ptr<::cw::header::CardHeader> header;
    std::shared_ptr<::cw::character::Character> selectedmember;
    std::vector<std::shared_ptr<::cw::header::CardHeader>> result_cards;
    const S keycode = value_string(m_keycode);
    if (keycode.empty()) { set_result_variable(value_string(m_result_var), variant_list_value({})); return get_boolean_index(false); }
    const S target = value_string(m_targetkc);
    if (target == "SelectedCard") {
        if (::cw::g_cwpy && ::cw::g_cwpy->m_event) {
            const auto selected = ::cw::g_cwpy->m_event->get_selectedcard();
            if (selected && *selected) {
                const auto& h = *selected;
                bool in_hand = false;
                if (m_hand && ::cw::g_cwpy->is_battlestatus()) {
                    const auto owner_value = h->get_owner();
                    if (const auto owner = owner_value ? typed_object<::cw::character::Character>(*owner_value) : nullptr) {
                        if (owner->m_deck) {
                            // Python版と同様、SelectedCardのhand
                            // 判定には実際の deck.hand を使う。 get_hand()
                            // はactiondata・使用済みカード
                            // も含む表示・検索用の広い一覧なので、 手
                            // 札から消えたカードまで受け入れてしまう。
                            for (const auto& hand_card : owner->m_deck->m_hand) {
                                if (!hand_card) continue;
                                if (hand_card == h) { in_hand = true; break; }
                                const auto lhs = hand_card->get_original();
                                const auto rhs = h->get_original();
                                if (lhs && rhs && lhs == rhs) { in_hand = true; break; }
                            }
                        }
                    }
                }
                const bool typeok = (h->m_type == "SkillCard" && m_skill) || (h->m_type == "ItemCard" && m_item) ||
                    (h->m_type == "BeastCard" && m_beast) || in_hand;
                if (typeok && _matches_keycode(h)) { header = h; result_cards.push_back(h); success = true; }
                if (header) { const auto owner = header->get_owner(); if (owner) selectedmember = typed_object<::cw::character::Character>(*owner); }
            }
        }
        if (m_invert) success = !success;
    } else {
        std::vector<V> targets;
        if (::cw::g_cwpy && ::cw::g_cwpy->m_event) {
            if (target == "Selected") {
                const V selected = ::cw::g_cwpy->m_event->get_targetmember("Selected", true, "");
                if (!selected.is_none()) targets.push_back(selected);
            } else if (target == "Random") {
                targets = ::cw::g_cwpy->m_event->get_targetscope("Party", true, false, "");
            } else if ((target == "Backpack" || target == "PartyAndBackpack") && ::cw::g_cwpy->m_ydata && (*::cw::g_cwpy->m_ydata)->m_party) {
                if (target == "PartyAndBackpack") targets = ::cw::g_cwpy->m_event->get_targetscope("Party", true, false, "");
                targets.push_back(object_value(std::static_pointer_cast<::cw::lib::Object>((*::cw::g_cwpy->m_ydata)->m_party.value())));
            }
        }
        for (const auto& value : targets) {
            std::optional<std::shared_ptr<::cw::header::CardHeader>> found;
            if (auto character = typed_object<::cw::character::Character>(value)) {
                found = character->find_keycode(keycode, m_skill, m_item, m_beast, m_hand, value_string(m_condition));
            } else if (auto party = typed_object<::cw::data::Party>(value)) {
                found = party->find_keycode(keycode, m_skill, m_item, m_beast, m_hand, value_string(m_condition));
            }
            header = found && *found ? *found : nullptr;
            bool current = found && *found;
            if (m_invert) current = !current;
            success = current;
            if (current) {
                selectedmember = typed_object<::cw::character::Character>(value);
                break;
            }
        }
        if (!value_string(m_result_var).empty()) {
            for (const auto& value : targets) {
                auto cards = _find_keycode_cards(value);
                result_cards.insert(result_cards.end(), cards.begin(), cards.end());
            }
        }
    }
    if (::cw::g_cwpy && ::cw::g_cwpy->m_event && selectedmember) ::cw::g_cwpy->m_event->set_selectedmember(selectedmember);
    if (success && header && m_selectcard && ::cw::g_cwpy && ::cw::g_cwpy->m_event) ::cw::g_cwpy->m_event->set_selectedcard(header);
    if (!value_string(m_result_var).empty()) {
        std::vector<V> infos;
        for (const auto& card : result_cards) if (const auto info = _cardinfo_from_header(card)) infos.push_back(object_value(*info));
        set_result_variable(value_string(m_result_var), variant_list_value(infos));
    }
    return get_boolean_index(success);
}

::cw::lib::String BranchKeyCodeContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    return "キーコード所持分岐コンテント";
}

::cw::lib::String BranchKeyCodeContent::get_childname(std::shared_ptr<::cw::data::CWPyElement> arg_child, std::shared_ptr<::cw::event::Event> arg_event)
{
    (void)arg_event;
    S types;
    if (m_skill) types += "特殊技能";
    if (m_item) { if (!types.empty()) types += "・"; types += "アイテム"; }
    if (m_beast) { if (!types.empty()) types += "・"; types += "召喚獣"; }
    if (m_hand) { if (!types.empty()) types += "・"; types += "手札"; }
    if (types.empty()) types = "(指定無し)";
    bool success = get_contentname(arg_child, "") == "○"; if (m_invert) success = !success;
    const S has = value_string(m_condition) == "HasNot" ? "持たない" : "持つ";
    return status_name(value_string(m_targetkc)) + "の" + types + "からキーコード『" + value_string(m_keycode) + "』を" + has +
        "カードを所有" + (success ? "する" : "しない");
}

void BranchLevelContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    BranchContent::initialize(std::move(arg_data));
}

::cw::lib::Integer BranchLevelContent::action()
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_event) return get_boolean_index(false);
    const bool average = m_data_typed && m_data_typed->getbool(".", "average", false);
    const I required = m_data_typed ? m_data_typed->getint(".", "value", 0) : 0;
    I level = 0;
    if (average) {
        const auto cards = ::cw::g_cwpy->get_pcards("unreversed");
        if (cards.empty()) return get_boolean_index(false);
        // Python版と同様、任意精度整数を合計して床関数の除算を行う。 正確
        // なDecimal演算で、中間のネイティブ整数のオーバーフローを避ける。
        auto context = ::cw::lib::DecimalContext::default_context();
        context.precision = 0;
        ::cw::lib::Decimal total;
        for (const auto& card : cards) if (card)
            total = total.add(::cw::lib::Decimal(card->m_level), context);
        level = total.floor_divide(::cw::lib::Decimal(static_cast<I>(cards.size())), context).to_integer();
    } else {
        const V selected = ::cw::g_cwpy->m_event->get_targetmember("Selected", true, "");
        const auto card = typed_object<::cw::character::Character>(selected);
        if (!card) return get_boolean_index(false);
        level = card->m_level;
    }
    return get_boolean_index(level >= required);
}

::cw::lib::String BranchLevelContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    return "レベル分岐コンテント";
}

::cw::lib::String BranchLevelContent::get_childname(std::shared_ptr<::cw::data::CWPyElement> arg_child, std::shared_ptr<::cw::event::Event> arg_event)
{
    (void)arg_event;
    const S who = m_data_typed && m_data_typed->getbool(".", "average", false) ? "全員の平均値" : "選択中のキャラ";
    return who + "がレベル" + (m_data_typed ? m_data_typed->get("value", "") : "") + (get_contentname(arg_child, "") == "○" ? "以上" : "未満");
}

void BranchMoneyContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    BranchContent::initialize(std::move(arg_data));
}

::cw::lib::Integer BranchMoneyContent::action()
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_ydata || !*::cw::g_cwpy->m_ydata) return 0;
    const auto party = (*::cw::g_cwpy->m_ydata)->m_party;
    if (!party || !*party) return 0;
    return get_boolean_index((*party)->m_money >= (m_data_typed ? m_data_typed->getint(".", "value", 0) : 0));
}

::cw::lib::String BranchMoneyContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    return "金額 = " + (m_data_typed ? m_data_typed->get("value", "0") : "0");
}

::cw::lib::String BranchMoneyContent::get_childname(std::shared_ptr<::cw::data::CWPyElement> arg_child, std::shared_ptr<::cw::event::Event> arg_event)
{
    (void)arg_event;
    const S value = m_data_typed ? m_data_typed->get("value", "0") : "0";
    return value + (get_contentname(arg_child, "") == "○" ? " sp以上所持している" : " sp以上所持していない");
}

void BranchMultiCouponContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    BranchContent::initialize(std::move(arg_data));
    const auto scope = _get_couponscope(m_data_typed ? m_data_typed->get("targets", "Selected") : "Selected");
    m_scope = std::get<0>(scope); m_someone = std::get<1>(scope); m_unreversed = std::get<2>(scope);
    m_spchars = m_data_typed && m_data_typed->getbool(".", "spchars", false);
}

::cw::lib::Integer BranchMultiCouponContent::action()
{
    std::vector<std::shared_ptr<::cw::character::Character>> targets;
    const S scope = value_string(m_scope);
    if (::cw::g_cwpy && ::cw::g_cwpy->m_event)
        for (const auto& value : ::cw::g_cwpy->m_event->get_targetscope(scope, value_bool(m_unreversed), false, ""))
            if (auto target = typed_object<::cw::character::Character>(value)) targets.push_back(target);
    I index = 0, default_index = tree_end();
    for (auto child : get_children()) {
        if (!child) continue;
        if (child->m_tag == "ContentsLine") child = child->get_item(0);
        if (content_is_checker(child)) {
            const auto checker = get_content(child);
            if (checker && checker->action() != 0) continue;
        }
        const S coupon = child->get("name", "");
        S expanded = coupon;
        if (m_spchars) expanded = std::get<0>(::cw::sprite::message::rpl_specialstr(expanded, std::nullopt, true, "All", true, true, std::nullopt));
        if (!expanded.empty()) {
            if (_has_coupon(targets, {expanded}, scope, value_bool(m_someone), false, true, false)) return index;
            ++index;
        } else {
            if (default_index == tree_end()) default_index = index;
            if (targets.empty()) break;
            ++index;
        }
    }
    return default_index;
}

::cw::lib::String BranchMultiCouponContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    return "クーポン多岐分岐コンテント";
}

::cw::lib::String BranchMultiCouponContent::get_childname(std::shared_ptr<::cw::data::CWPyElement> arg_child, std::shared_ptr<::cw::event::Event> arg_event)
{
    (void)arg_event;
    const S name = get_contentname(arg_child, "");
    const S scope = m_data_typed ? status_name(m_data_typed->get("targets", "Selected")) : status_name("Selected");
    const S suffix = m_spchars ? "(特殊文字を展開)" : "";
    return name.empty() ? scope + "が全ての称号を所有していない" + suffix : scope + "が称号「" + name + "」を所有している" + suffix;
}

void BranchMultiRandomContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    BranchContent::initialize(std::move(arg_data));
}

::cw::lib::Integer BranchMultiRandomContent::action()
{
    std::vector<V> indexes;
    I index = 0;
    for (auto child : get_children()) {
        if (!child) continue;
        if (child->m_tag == "ContentsLine") child = child->get_item(0);
        if (content_is_checker(child)) {
            const auto checker = get_content(child);
            if (checker && checker->action() != 0) continue;
        }
        indexes.emplace_back(index++);
    }
    if (indexes.empty()) return tree_end();
    // 不完全なネイティブ環境にはDiceがない場合がある。 Python版は有
    // 効なDiceを必須とするが、ここでは最初の有効な分岐を一定の規則で選ぶ。
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_dice) return value_int(indexes.front());
    return value_int(::cw::g_cwpy->m_dice->choice_exists(indexes), tree_end());
}

::cw::lib::String BranchMultiRandomContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    return "ランダム多岐分岐コンテント";
}

void BranchMultiStepContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    BranchContent::initialize(std::move(arg_data));
    m_step = m_data_typed ? m_data_typed->get("step", "") : V{};
    m_nextlen = get_children_num();
}

::cw::lib::Integer BranchMultiStepContent::action()
{
    const auto sdata = system_data();
    const auto step = sdata ? sdata->find_step(value_string(m_step), is_differentscenario(std::nullopt),
        ::cw::g_cwpy && ::cw::g_cwpy->m_event ? ::cw::g_cwpy->m_event->get_nowrunningevent() : std::nullopt) : std::nullopt;
    if (step) return get_value_index((*step)->m_value);
    return value_int(m_nextlen, 0) ? 0 : tree_end();
}

::cw::lib::String BranchMultiStepContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    const auto sdata = system_data();
    const auto step = sdata ? sdata->find_step(value_string(m_step), is_differentscenario(arg_event), arg_event) : std::nullopt;
    return step ? "ステップ『" + (*step)->m_name + "』多岐分岐" : "ステップが指定されていません";
}

::cw::lib::String BranchMultiStepContent::get_childname(std::shared_ptr<::cw::data::CWPyElement> arg_child, std::shared_ptr<::cw::event::Event> arg_event)
{
    const auto sdata = system_data();
    const auto step = sdata ? sdata->find_step(value_string(m_step), is_differentscenario(arg_event), arg_event) : std::nullopt;
    if (!step) return "ステップが指定されていません";
    const S text = get_contentname(arg_child, "Default");
    if (const auto parsed = ::cw::lib::PythonInteger::parse(text)) {
        try {
            // Step::get_valuename は有限のラベル一覧の範
            // 囲に収める。 ネイティブ整数の境界でだけ飽和させ、有効な大きい
            // 整数を「その他」の分岐にせず、Python版と同じ結果にする。
            const auto value = *parsed < ::cw::lib::PythonInteger(std::numeric_limits<I>::min()) ? std::numeric_limits<I>::min() :
                *parsed > ::cw::lib::PythonInteger(std::numeric_limits<I>::max()) ? std::numeric_limits<I>::max() : parsed->to_integer();
            return (*step)->m_name + " = " + (*step)->get_valuename(value);
        } catch (const std::exception&) {
            // Python版と同様、空のラベル一覧などの名前取得エラーも捕捉する。
        }
    }
    return (*step)->m_name + " = その他";
}

void BranchPartyNumberContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    BranchContent::initialize(std::move(arg_data));
}

::cw::lib::Integer BranchPartyNumberContent::action()
{
    const I value = m_data_typed ? m_data_typed->getint(".", "value", 0) : 0;
    return get_boolean_index(::cw::g_cwpy && static_cast<I>(::cw::g_cwpy->get_pcards(V{}).size()) >= value);
}

::cw::lib::String BranchPartyNumberContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event; return "人数 = " + (m_data_typed ? m_data_typed->get("value", "0") : "0");
}

::cw::lib::String BranchPartyNumberContent::get_childname(std::shared_ptr<::cw::data::CWPyElement> arg_child, std::shared_ptr<::cw::event::Event> arg_event)
{
    (void)arg_event; const S value = m_data_typed ? m_data_typed->get("value", "0") : "0"; return "パーティ人数が" + value + "人" + (get_contentname(arg_child, "") == "○" ? "以上" : "未満");
}

void BranchRandomContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    BranchContent::initialize(std::move(arg_data));
}

::cw::lib::Integer BranchRandomContent::action()
{
    const I configured = m_data_typed ? m_data_typed->getint(".", "value", 0) : 0;
    I threshold = configured;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_sct && system_data() &&
        ::cw::g_cwpy->m_sct->lessthan("1.28", system_data()->get_versionhint(0))) ++threshold;
    // Python版は有効なDiceを必須とする。 不完全なネイティ
    // ブ環境では乱数状態を新設・null参照せず、最大の出目を使う。
    const I roll = ::cw::g_cwpy && ::cw::g_cwpy->m_dice ? ::cw::g_cwpy->m_dice->roll(1, 100) : 100;
    return get_boolean_index(roll <= threshold);
}

::cw::lib::String BranchRandomContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    return "確率 = " + (m_data_typed ? m_data_typed->get("value", "0") : "0") + "%";
}

::cw::lib::String BranchRandomContent::get_childname(std::shared_ptr<::cw::data::CWPyElement> arg_child, std::shared_ptr<::cw::event::Event> arg_event)
{
    (void)arg_event;
    const S value = m_data_typed ? m_data_typed->get("value", "") : "";
    return value + (get_contentname(arg_child, "") == "○" ? " %成功" : " %失敗");
}

void BranchRandomSelectContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    BranchContent::initialize(std::move(arg_data));
    if (!m_data_typed) return;
    const auto parse_level = [](const S& text) -> V {
        const auto normalized = ::cw::lib::normalize_python_integer(text);
        if (!normalized)
            throw std::invalid_argument("invalid literal for int() with base 10: " + ::cw::lib::python_percent_detail::repr(text, false));
        if (const auto value = ::cw::lib::parse_python_integer(*normalized)) return *value;
        // Pythonのintはint64に限定されない。 Decimalの
        // 規定のメモリ上限内で、比較・表示用の整数の大きさを正確に保持する。
        return ::cw::lib::Decimal::parse(*normalized);
    };
    m_minlevel = parse_level(m_data_typed->get("minLevel", "0"));
    m_maxlevel = parse_level(m_data_typed->get("maxLevel", "0"));
    m_status = m_data_typed->get("status", "None");
    auto ranges = std::make_shared<::cw::data::VariantSequence>();
    for (const auto& range : get_castranges()) ranges->m_values.emplace_back(range);
    m_ranges = std::static_pointer_cast<::cw::lib::Object>(ranges);
    m_invert = m_data_typed->getbool(".", "invert", false);
}

::cw::lib::Integer BranchRandomSelectContent::action()
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_event) return get_boolean_index(false);
    std::set<S> ranges;
    if (const auto cached = value_sequence(m_ranges))
        for (const auto& range : cached->m_values) ranges.insert(value_string(range));
    std::vector<std::shared_ptr<::cw::character::Character>> targets;
    for (const S& scope : {S("Party"), S("Enemy"), S("Npc")}) {
        if (!ranges.count(scope)) continue;
        for (const auto& value : ::cw::g_cwpy->m_event->get_targetscope(scope, true, false, "")) {
            if (auto target = typed_object<::cw::character::Character>(value)) targets.push_back(target);
        }
    }
    const S raw_status = value_string(m_status);
    const S status = ::cw::lib::encoding::lower(raw_status);
    auto status_ok = [&](const std::shared_ptr<::cw::character::Character>& target) {
        // Python版と同様、メソッド名を小文字化する前に、正確に一致する特別値だけを除外する。
        if (raw_status.empty() || raw_status == "None") return true;
        return character_status(target, status).value_or(false);
    };
    std::vector<std::shared_ptr<::cw::character::Character>> candidates;
    const auto minlevel = m_minlevel.as_decimal() ? *m_minlevel.as_decimal() : ::cw::lib::Decimal(value_int(m_minlevel, 0));
    const auto maxlevel = m_maxlevel.as_decimal() ? *m_maxlevel.as_decimal() : ::cw::lib::Decimal(value_int(m_maxlevel, 0));
    const ::cw::lib::Decimal zero(I{0});
    for (const auto& target : targets) {
        const ::cw::lib::Decimal level(target->m_level);
        bool failure = !status_ok(target) || (zero < minlevel && level < minlevel) ||
            (zero < maxlevel && maxlevel < level);
        if (m_invert) failure = !failure;
        if (!failure) candidates.push_back(target);
    }
    if (!candidates.empty() && ::cw::g_cwpy->m_dice) {
        const I index = static_cast<I>(::cw::dice::choice_index(candidates.size()));
        if (index >= 0 && index < static_cast<I>(candidates.size())) {
            ::cw::g_cwpy->m_event->set_selectedmember(candidates[static_cast<std::size_t>(index)]);
            return get_boolean_index(true);
        }
    }
    return get_boolean_index(false);
}

std::set<::cw::lib::String> BranchRandomSelectContent::get_castranges()
{
    std::set<S> result;
    if (!m_data_typed) return result;
    for (const auto& child : m_data_typed->find_exists("CastRanges")->m_children)
        if (child) result.insert(child->gettext(".", ""));
    return result;
}

::cw::lib::String BranchRandomSelectContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    return "ランダム選択分岐コンテント";
}

::cw::lib::String BranchRandomSelectContent::get_childname(std::shared_ptr<::cw::data::CWPyElement> arg_child, std::shared_ptr<::cw::event::Event> arg_event)
{
    (void)arg_event;
    std::set<S> ranges;
    if (const auto cached = value_sequence(m_ranges))
        for (const auto& range : cached->m_values) ranges.insert(value_string(range));
    S scope;
    if (ranges.count("Party") && ranges.count("Enemy") && ranges.count("Npc")) scope = status_name("field");
    else {
        // Pythonのsetの走査順は未規定だが、ネイティブの std::set
        // は名前順に固定する。 所属と範囲ごとの文言はPython版に合わせる。
        for (const auto& item : ranges) {
            if (!scope.empty()) scope += "と";
            const auto name = status_name(item);
            scope += name.empty() ? "不明な範囲" : name;
        }
    }
    S conditions;
    const auto minlevel = m_minlevel.as_decimal() ? *m_minlevel.as_decimal() : ::cw::lib::Decimal(value_int(m_minlevel, 0));
    if (::cw::lib::Decimal(I{0}) < minlevel) conditions = "レベルが" + value_string(m_minlevel) + "～" + value_string(m_maxlevel);
    if (!value_string(m_status).empty()) {
        if (!conditions.empty()) conditions += "で";
        conditions += "【" + status_name(m_data_typed ? m_data_typed->get("status", "") : "") + "】";
    }
    conditions += m_invert ? "でない" : "の";
    return scope + "から" + conditions + "キャラクターの選択に" +
        (get_contentname(arg_child, "") == "○" ? "成功" : "失敗");
}

void BranchRoundContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    BranchContent::initialize(std::move(arg_data));
}

::cw::lib::Integer BranchRoundContent::action()
{
    const S raw_round = m_data_typed ? m_data_typed->get("round", "1") : "1";
    const auto normalized = ::cw::lib::normalize_python_integer(raw_round);
    if (!normalized)
        throw std::invalid_argument("invalid literal for int() with base 10: " + ::cw::lib::python_percent_detail::repr(raw_round, false));
    // 比較だけなので、XML値をネイティブのgetint・既
    // 定値へ狭めず、Python整数の大きさ全体を保持する。
    const auto round1 = ::cw::lib::Decimal::parse(*normalized);
    const S comparison = m_data_typed ? m_data_typed->get("comparison", "") : "";
    bool flag = false;
    if (::cw::g_cwpy && ::cw::g_cwpy->is_battlestatus() && ::cw::g_cwpy->m_battle && *::cw::g_cwpy->m_battle) {
        const auto round2 = ::cw::lib::Decimal((*::cw::g_cwpy->m_battle)->m_round);
        if (comparison == "=") flag = round1 == round2;
        else if (comparison == "<") flag = round1 < round2;
        else if (comparison == ">") flag = round1 > round2;
    }
    return get_boolean_index(flag);
}

::cw::lib::String BranchRoundContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    return "ラウンド分岐コンテント";
}

::cw::lib::String BranchRoundContent::get_childname(std::shared_ptr<::cw::data::CWPyElement> arg_child, std::shared_ptr<::cw::event::Event> arg_event)
{
    (void)arg_event;
    const S raw_round = m_data_typed ? m_data_typed->get("round", "1") : "1";
    const auto normalized = ::cw::lib::normalize_python_integer(raw_round);
    if (!normalized)
        throw std::invalid_argument("invalid literal for int() with base 10: " + ::cw::lib::python_percent_detail::repr(raw_round, false));
    const bool negative = normalized->front() == '-';
    const auto first_digit = normalized->find_first_not_of("+-0");
    S round = first_digit == S::npos ? "0" : normalized->substr(first_digit);
    if (negative && round != "0") round.insert(round.begin(), '-');
    const S comparison = m_data_typed ? m_data_typed->get("comparison", "None") : "None";
    return round + " " + comparison + " 現在のバトルラウンド" + (get_contentname(arg_child, "") == "○" ? "である" : "でない");
}

void BranchSelectContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    BranchContent::initialize(std::move(arg_data));
    m__init_values = false;
    if (!m_data_typed) return;
    m_targetall = m_data_typed->getbool(".", "targetall", true);
    m_method = m_data_typed->get("method", "");
    if (m_method.empty()) m_method = m_data_typed->getbool(".", "random", false) ? "Random" : "Manual";
}

::cw::lib::Integer BranchSelectContent::action()
{
    const S mode = m_targetall ? "unreversed" : "active";
    I index = -1;
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_event) return get_boolean_index(false);
    if (m_method == "Random") {
        const auto pcards = ::cw::g_cwpy->get_pcards(mode);
        if (!pcards.empty() && ::cw::g_cwpy->m_dice) {
            const I selected = static_cast<I>(::cw::dice::choice_index(pcards.size()));
            if (selected >= 0 && selected < static_cast<I>(pcards.size()) && pcards[static_cast<std::size_t>(selected)]) {
                ::cw::g_cwpy->m_event->set_selectedmember(std::static_pointer_cast<::cw::character::Character>(pcards[static_cast<std::size_t>(selected)]));
                index = 0;
            }
        }
        if (index < 0) ::cw::g_cwpy->m_event->clear_selectedmember();
    } else if (m_method == "Valued") {
        const auto member = get_valuedmember(mode, true);
        if (member && *member) { ::cw::g_cwpy->m_event->set_selectedmember(*member); index = 0; }
    } else {
        std::vector<std::shared_ptr<::cw::character::Character>> members;
        for (const auto& card : ::cw::g_cwpy->get_pcards(mode))
            if (const auto character = std::dynamic_pointer_cast<::cw::character::Character>(card)) members.push_back(character);
        auto window = std::make_shared<::cw::sprite::message::MemberSelectWindow>();
        window->initialize(std::move(members), std::nullopt, std::nullopt);
        return get_boolean_index(message_result_is_zero(::cw::g_cwpy->show_message(window)));
    }
    return get_boolean_index(index == 0);
}

::cw::lib::String BranchSelectContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    return "選択分岐コンテント";
}

::cw::lib::String BranchSelectContent::get_childname(std::shared_ptr<::cw::data::CWPyElement> arg_child, std::shared_ptr<::cw::event::Event> arg_event)
{
    (void)arg_event;
    init_values();
    S result = m_targetall ? "パーティ全員から" : "動けるメンバから";
    if (m_method == "Random") result += "ランダムで ";
    else if (m_method == "Valued") {
        S values = "初期値 = " + std::to_string(m_initvalue);
        std::vector<std::pair<S, I>> coupons(m_coupons.begin(), m_coupons.end());
        std::stable_sort(coupons.begin(), coupons.end(), [](const auto& lhs, const auto& rhs) {
            return ::cw::util::logical_compare(lhs.first, rhs.first) < 0;
        });
        for (const auto& [name, value] : coupons)
            values += ", " + name + " = " + std::to_string(value);
        result += "評価条件(" + values + ")で";
    }
    else result += "手動で ";
    if (get_contentname(arg_child, "") == "○") result += "キャラクターを選択";
    else result += m_method == "Manual" ? "の選択をキャンセル" : "の選択に失敗";
    return result;
}

void BranchSkillContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    BranchContent::initialize(std::move(arg_data));
}

::cw::lib::Integer BranchSkillContent::action()
{
    return branch_cards("SkillCard");
}

::cw::lib::String BranchSkillContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event; auto sdata = system_data(); const auto name = sdata ? sdata->get_skillname(m_data_typed->getint(".", "id", 0)) : std::nullopt; return name ? "特殊技能カード『" + *name + "』所持分岐" : "特殊技能カードが指定されていません";
}

::cw::lib::String BranchSkillContent::get_childname(std::shared_ptr<::cw::data::CWPyElement> arg_child, std::shared_ptr<::cw::event::Event> arg_event)
{
    (void)arg_event; auto sdata = system_data(); const auto name = sdata ? sdata->get_skillname(m_data_typed->getint(".", "id", 0)) : std::nullopt; if (!name) return "特殊技能カードが指定されていません"; bool success = get_contentname(arg_child, "") == "○"; if (m_invert) success = !success; const S scope = m_data_typed->get("targets", ""); if (scope == "SelectedCard") return "選択カードが『" + *name + "』と一致" + (success ? "する" : "しない"); return status_name(scope) + "が『" + *name + "』を所有して" + (success ? "いる" : "いない");
}

void BranchStatusContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    BranchContent::initialize(std::move(arg_data));
    if (!m_data_typed) return;
    m_targetm = m_data_typed->get("targetm", ""); m_target_var = m_data_typed->get("target_var", ""); m_status = m_data_typed->get("status", ""); m_andor = m_data_typed->get("andor", "and");
    if (const auto* andor = std::get_if<S>(&m_andor.storage())) { S lowered = *andor; std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); }); m_andor = lowered; }
    m_invert = m_data_typed->getbool(".", "invert", false);
}

::cw::lib::Integer BranchStatusContent::action()
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_event) return get_boolean_index(false);
    S targetm = value_string(m_targetm); const S targetvar = value_string(m_target_var); S statuses = value_string(m_status); S andor = value_string(m_andor);
    std::transform(andor.begin(), andor.end(), andor.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::vector<S> names = ::cw::lib::python_split_whitespace(statuses);
    if (system_data() && ::cw::g_cwpy->m_sct && ::cw::g_cwpy->m_sct->lessthan("1.20", system_data()->get_versionhint(0))) {
        const std::map<S, S> compatibility{{"Poison", "Sleep"}, {"Sleep", "Bind"}, {"Bind", "Paralyze"}, {"Paralyze", "Poison"}};
        for (auto& name : names) { const auto it = compatibility.find(name); if (it != compatibility.end()) name = it->second; }
    }
    const bool someone = !targetvar.empty() || targetm != "Party";
    if (targetvar.empty() && targetm == "Random") targetm = "Party";
    std::vector<std::shared_ptr<::cw::character::Character>> targets;
    if (!targetvar.empty()) { auto target = get_variable_target(targetvar); if (target) targets.push_back(*target); }
    else {
        const auto resolved = ::cw::g_cwpy->m_event->get_targetmember(targetm, true, "");
        const auto sequence = value_sequence(resolved);
        const auto members = sequence ? sequence->m_values : std::vector<V>{resolved};
        for (const auto& target : members) {
            if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&target.storage()); object && *object) {
                if (auto character = std::dynamic_pointer_cast<::cw::character::Character>(*object)) targets.push_back(character);
            }
        }
    }
    if (targets.empty()) return get_boolean_index(false);
    bool flag = true; std::shared_ptr<::cw::character::Character> selected;
    for (const auto& target : targets) {
        if (!target) continue;
        // Python版と同様、状態が未指定なら対象自体を飛ば
        // す。 空のall()/any()の結果を適用・反転
        // しない。 空一覧を通常の真偽式と扱うと、status=""
        // の拡張XMLで選択分岐が変わってしまう。
        if (names.empty()) continue;
        std::vector<bool> results;
        bool has_unknown = false;
        for (auto status : names) {
            status = ::cw::lib::encoding::lower(status);
            const auto result = character_status(target, status);
            if (result) results.push_back(*result);
            else has_unknown = true;
        }
        if (names.size() == 1 && has_unknown) continue;
        if (has_unknown) results.push_back(false);
        bool result = andor == "or" ? std::any_of(results.begin(), results.end(), [](bool v){ return v; }) : std::all_of(results.begin(), results.end(), [](bool v){ return v; }); if (m_invert) result = !result;
        if (result && someone) { flag = true; selected = target; break; } if (!result) { flag = false; if (!someone) { selected = target; break; } }
    }
    if (targetvar.empty() && targetm != "Selected" && selected) ::cw::g_cwpy->m_event->set_selectedmember(selected);
    return get_boolean_index(flag);
}

::cw::lib::String BranchStatusContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    return "状態分岐コンテント";
}

::cw::lib::String BranchStatusContent::get_childname(std::shared_ptr<::cw::data::CWPyElement> arg_child, std::shared_ptr<::cw::event::Event> arg_event)
{
    (void)arg_event; const S target = !value_string(m_target_var).empty() ? "コモン『" + value_string(m_target_var) + "』の参照先" : status_name(m_data_typed->get("targetm", "")); S joined; bool first = true; for (const auto& status : ::cw::lib::python_split_whitespace(m_data_typed->get("status", ""))) { if (!first) joined += value_string(m_andor) == "or" ? "または" : "かつ"; first = false; const S translated = status_name(status); joined += translated.empty() ? status : translated; } const bool success = get_contentname(arg_child, "") == "○" ? !m_invert : m_invert; return target + "が【" + joined + "】で" + (success ? "ある" : "ない");
}

void BranchStepContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    BranchContent::initialize(std::move(arg_data));
    m_step=m_data_typed ? m_data_typed->get("step", "") : V{};
    m_value=m_data_typed ? m_data_typed->getint(".", "value", 0) : 0;
    m_nextlen=get_children_num();
}

::cw::lib::Integer BranchStepContent::action()
{
    const auto sdata = system_data();
    const S name = value_string(m_step);
    const I required = m_value;
    const auto step = sdata ? sdata->find_step(name, is_differentscenario(std::nullopt),
        ::cw::g_cwpy && ::cw::g_cwpy->m_event ? ::cw::g_cwpy->m_event->get_nowrunningevent() : std::nullopt) : std::nullopt;
    if (step) return get_boolean_index((*step)->m_value >= required);
    return value_int(m_nextlen) ? 0 : tree_end();
}

::cw::lib::String BranchStepContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    const auto sdata = system_data();
    const S name = value_string(m_step);
    const auto step = sdata ? sdata->find_step(name, is_differentscenario(arg_event), arg_event) : std::nullopt;
    return step ? "ステップ『" + (*step)->m_name + "』分岐" : "ステップが指定されていません";
}

::cw::lib::String BranchStepContent::get_childname(std::shared_ptr<::cw::data::CWPyElement> arg_child, std::shared_ptr<::cw::event::Event> arg_event)
{
    const auto sdata = system_data();
    const S name = value_string(m_step);
    const I required = m_value;
    const auto step = sdata ? sdata->find_step(name, is_differentscenario(arg_event), arg_event) : std::nullopt;
    if (!step) return "ステップが指定されていません";
    return "ステップ『" + (*step)->m_name + "』が『" + (*step)->get_valuename(required) + "』" +
        (get_contentname(arg_child, "") == "○" ? "以上" : "未満");
}

void BranchStepValueContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    BranchContent::initialize(std::move(arg_data));
    if (m_data_typed) { m_fromstep = m_data_typed->get("from", ""); m_tostep = m_data_typed->get("to", ""); }
}

::cw::lib::Integer BranchStepValueContent::action()
{
    const auto sdata = system_data(); const auto event = event_interface() ? event_interface()->get_nowrunningevent() : std::nullopt;
    const auto from = sdata ? sdata->find_step(value_string(m_fromstep), is_differentscenario(std::nullopt), event) : std::nullopt;
    const auto to = sdata ? sdata->find_step(value_string(m_tostep), is_differentscenario(std::nullopt), event) : std::nullopt;
    return from && to ? get_compare_index((*from)->m_value == (*to)->m_value ? 0 : ((*from)->m_value > (*to)->m_value ? 1 : -1)) : tree_end();
}

::cw::lib::String BranchStepValueContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    const auto sdata = system_data(); const auto from = sdata ? sdata->find_step(value_string(m_fromstep), is_differentscenario(arg_event), arg_event) : std::nullopt;
    const auto to = sdata ? sdata->find_step(value_string(m_tostep), is_differentscenario(arg_event), arg_event) : std::nullopt;
    return from && to ? "ステップ『" + (*from)->m_name + "』と『" + (*to)->m_name + "』を比較" : "ステップが指定されていません";
}

::cw::lib::String BranchStepValueContent::get_childname(std::shared_ptr<::cw::data::CWPyElement> arg_child, std::shared_ptr<::cw::event::Event> arg_event)
{
    const auto sdata = system_data(); const auto from = sdata ? sdata->find_step(value_string(m_fromstep), is_differentscenario(std::optional<std::shared_ptr<::cw::event::Event>>(arg_event)), arg_event) : std::nullopt;
    const auto to = sdata ? sdata->find_step(value_string(m_tostep), is_differentscenario(std::optional<std::shared_ptr<::cw::event::Event>>(arg_event)), arg_event) : std::nullopt;
    if (!from || !to) return "ステップが指定されていません";
    const S op = get_contentname(arg_child, "");
    return op == ">" ? "ステップ『" + (*from)->m_name + "』が『" + (*to)->m_name + "』より大きい" :
           (op == "=" ? "ステップ『" + (*from)->m_name + "』が『" + (*to)->m_name + "』と等しい" :
            "ステップ『" + (*from)->m_name + "』が『" + (*to)->m_name + "』より小さい");
}

void BranchVariantContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    BranchContent::initialize(std::move(arg_data));
    m_expression = m_data_typed ? m_data_typed->gettext("Expression", "") : "";
    m_parsed_expression.reset();
}

::cw::lib::Integer BranchVariantContent::action()
{
    // ネイティブのComputeExceptionにはPythonのprint_ex()
    // 用トレースバックがないため、 評価失敗を示す接頭辞を付け、共通のエラ
    // ーダイアログ・ビルド設定に従うログへ渡す。 捕捉するのはPython
    // 版と同じComputeExceptionだけで、他の失敗は伝える。
    try {
        if (!m_parsed_expression || m_parsed_expression->empty()) m_parsed_expression = ::cw::calculator::parse(m_expression);
        if (!m_parsed_expression || m_parsed_expression->empty()) return get_boolean_index(false);
        auto option = std::make_shared<::cw::calculator::CalcOption>();
        option->initialize("Event", is_differentscenario(std::nullopt));
        const auto variant = ::cw::calculator::eval_expr(*m_parsed_expression, option);
        if (variant && value_string(variant->m_type) == "Boolean") return get_boolean_index(value_bool(variant->m_value));
        const S text = variant ? ::cw::data::Variant::value_to_str(variant->m_value, false) : S{};
        // Python版はこのメッセージを整形後にもう一度 %
        // value を適用し、通常の非真偽値でTypeError
        // になる。 その不具合は再現せず、意図された variant_error
        // のダイアログを表示する。
        variant_error("計算結果 " + text + " は真偽値ではありません。", nullptr, m_expression);
    // Python版と同様、ここで扱うのはComputeExceptionだけ。 Decimal
    // ・計算コンテキストなどの失敗は、続行確認を開かず呼び出し元へ伝える。
    } catch (const ::cw::calculator::ComputeException& ex) {
        variant_error("式の評価に失敗しました。", &ex, m_expression);
    }
    return get_boolean_index(false);
}

::cw::lib::String BranchVariantContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    return m_expression;
}

::cw::lib::String BranchVariantContent::get_childname(std::shared_ptr<::cw::data::CWPyElement> arg_child, std::shared_ptr<::cw::event::Event> arg_event)
{
    (void)arg_event;
    const S value = get_contentname(arg_child, "") == "○" ? "TRUE" : "FALSE";
    return value + " = 〔 " + m_expression + " 〕";
}

void GetBeastContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    GetContent::initialize(std::move(arg_data));
}

::cw::lib::Integer GetBeastContent::action()
{
    get_cards("BeastCard", std::nullopt, std::nullopt);
    return 0;
}

::cw::lib::String GetBeastContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    const auto sdata = system_data();
    const auto name = sdata ? sdata->get_beastname(m_data_typed ? m_data_typed->getint(".", "id", 0) : 0) : std::nullopt;
    if (!name) return "召喚獣カードが指定されていません";
    const S scope = m_data_typed ? m_data_typed->get("targets", "") : S{};
    if (scope == "SelectedCard") return "選択カードと交換して召喚獣カード『" + *name + "』を取得";
    const I number = m_data_typed ? m_data_typed->getint(".", "number", 0) : 0;
    return status_name(scope) + "が召喚獣カード『" + *name + "』を" + std::to_string(number) + "枚取得";
}

void GetCastContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    GetContent::initialize(std::move(arg_data));
}

::cw::lib::Integer GetCastContent::action()
{
    if (is_differentscenario(std::nullopt)) return 0;
    const auto sdata = system_data();
    if (!sdata || !::cw::g_cwpy) return 0;
    const I resid = m_data_typed ? m_data_typed->getint(".", "id", 0) : 0;
    const S startaction = m_data_typed ? m_data_typed->get("startaction", "Now") : "Now";
    const bool exists = std::any_of(sdata->m_friendcards.begin(), sdata->m_friendcards.end(), [resid](const auto& card) { return card && card->m_id == resid; });
    if (!exists && sdata->m_friendcards.size() < 6) {
        // Python版と同様、キャスト検索前に宿を変更済みにする。
        // 検索・FriendCardの生成が失敗する場合も同様。
        if (auto yado = yado_data()) yado->changed();
        if (const auto data = sdata->get_castdata(resid, "", true, std::nullopt); data && *data) {
            auto fcard = std::make_shared<::cw::sprite::card::FriendCard>();
            fcard->initialize(object_value(std::static_pointer_cast<::cw::lib::Object>(*data)), static_cast<I>(sdata->m_friendcards.size()));
            sdata->m_friendcards.push_back(fcard);
            if (::cw::g_cwpy->is_battlestatus() && fcard->is_alive() && ::cw::g_cwpy->m_battle && *::cw::g_cwpy->m_battle) {
                const auto& battle = *::cw::g_cwpy->m_battle;
                if (startaction == "Now" || (startaction == "CurrentRound" && (battle->is_ready() || battle->m_in_roundevent))) {
                    battle->m_members.push_back(std::static_pointer_cast<::cw::character::Character>(
                        std::static_pointer_cast<::cw::character::Friend>(fcard)));
                    fcard->decide_action();
                }
            }
        }
    }
    return 0;
}

::cw::lib::String GetCastContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    const auto name = system_data() ? system_data()->get_castname(m_data_typed ? m_data_typed->getint(".", "id", 0) : 0) : std::nullopt;
    return name ? "キャストカード『" + *name + "』加入" : "キャストカードが指定されていません";
}

void GetCompleteStampContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    GetContent::initialize(std::move(arg_data));
}

::cw::lib::Integer GetCompleteStampContent::action()
{
    const S name = m_data_typed ? m_data_typed->get("scenario", "") : S{};
    if (!name.empty()) if (auto yado = yado_data()) yado->set_compstamp(name);
    return 0;
}

::cw::lib::String GetCompleteStampContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    const S name = m_data_typed ? m_data_typed->get("scenario", "") : S{};
    return name.empty() ? "終了済みシナリオが指定されていません" : "終了済みシナリオ『" + name + "』追加";
}

void GetCouponContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    GetContent::initialize(std::move(arg_data));
    if (!m_data_typed) return;
    m_coupon = m_data_typed->get("coupon", "");
    m_value = m_data_typed->getint(".", "value", 0);
    m_scope = m_data_typed->get("targets", "");
    m_holdingcoupon = m_data_typed->get("holdingcoupon", "");
    m_spchars = m_data_typed->getbool(".", "spchars", false);
}

::cw::lib::Integer GetCouponContent::action()
{
    S coupon = value_string(m_coupon);
    if (m_spchars) {
        coupon = std::get<0>(::cw::sprite::message::rpl_specialstr(
            coupon, std::nullopt, true, "All", true, true, std::nullopt));
    }
    if (is_addablecoupon(coupon)) {
        const auto targets = event_interface() ? event_interface()->get_targetscope(value_string(m_scope), false, false, value_string(m_holdingcoupon)) : std::vector<V>{};
        const auto effectevent = event_interface() ? event_interface()->get_effectevent() : std::nullopt;
        const bool targetout = effectevent && *effectevent && (*effectevent)->in_effectmotionloop() && coupon == "＠効果対象";
        for (const auto& value : targets) if (const auto target = character_target(value)) {
            if (targetout && target->has_coupon("＠効果対象外")) continue;
            target->set_coupon(coupon, m_value);
        }
    }
    return 0;
}

::cw::lib::String GetCouponContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    const S coupon = value_string(m_coupon);
    if (coupon.empty()) return "称号が指定されていません";
    const S value = m_value >= 0 ? "+" + std::to_string(m_value) : std::to_string(m_value);
    return status_name(value_string(m_scope)) + "に称号『" + coupon + "(" + value + ")』を付与" + (m_spchars ? "(特殊文字を展開)" : "");
}

void GetGossipContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    GetContent::initialize(std::move(arg_data));
    m_gossip = m_data_typed ? m_data_typed->get("gossip", "") : V{};
    m_spchars = m_data_typed && m_data_typed->getbool(".", "spchars", false);
}

::cw::lib::Integer GetGossipContent::action()
{
    S gossip = value_string(m_gossip);
    if (m_spchars) {
        gossip = std::get<0>(::cw::sprite::message::rpl_specialstr(
            gossip, std::nullopt, true, "All", true, true, std::nullopt));
    }
    if (!gossip.empty()) if (auto yado = yado_data()) yado->set_gossip(gossip);
    return 0;
}

::cw::lib::String GetGossipContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    const S gossip = value_string(m_gossip);
    return gossip.empty() ? "ゴシップが指定されていません" : "ゴシップ『" + gossip + "』取得" + (m_spchars ? "(特殊文字を展開)" : "");
}

void GetInfoContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    GetContent::initialize(std::move(arg_data));
    m_resid = m_data_typed ? m_data_typed->getint(".", "id", 0) : 0;
}

::cw::lib::Integer GetInfoContent::action()
{
    if (is_differentscenario(std::nullopt)) return 0;
    const auto sdata = system_data();
    if (!sdata) return 0;
    if (sdata->get_infoname(m_resid)) {
        if (auto yado = yado_data()) yado->changed();
        if (sdata->has_infocard(m_resid)) sdata->remove_infocard(m_resid);
        sdata->append_infocard(m_resid);
    }
    return 0;
}

::cw::lib::String GetInfoContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    const auto name = system_data() ? system_data()->get_infoname(m_data_typed->getint(".", "id", 0)) : std::nullopt;
    return name ? "情報カード『" + *name + "』入手" : "情報カードが指定されていません";
}

void GetItemContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    GetContent::initialize(std::move(arg_data));
}

::cw::lib::Integer GetItemContent::action()
{
    get_cards("ItemCard", std::nullopt, std::nullopt);
    return 0;
}

::cw::lib::String GetItemContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    const auto name = system_data() ? system_data()->get_itemname(m_data_typed ? m_data_typed->getint(".", "id", 0) : 0) : std::nullopt;
    if (!name) return "アイテムカードが指定されていません";
    const S scope = m_data_typed ? m_data_typed->get("targets", "") : S{};
    if (scope == "SelectedCard") return "選択カードと交換してアイテムカード『" + *name + "』を取得";
    const I number = m_data_typed ? m_data_typed->getint(".", "number", 0) : 0;
    return status_name(scope) + "がアイテムカード『" + *name + "』を" + std::to_string(number) + "枚取得";
}

void GetMoneyContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    GetContent::initialize(std::move(arg_data));
}

::cw::lib::Integer GetMoneyContent::action()
{
    if (const auto yado = yado_data(); yado && yado->m_party && *yado->m_party)
        (*yado->m_party)->set_money(m_data_typed ? m_data_typed->getint(".", "value", 0) : 0, true, true);
    return 0;
}

::cw::lib::String GetMoneyContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    return (m_data_typed ? m_data_typed->get("value", "0") : "0") + "sp取得";
}

void GetSkillContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    GetContent::initialize(std::move(arg_data));
}

::cw::lib::Integer GetSkillContent::action()
{
    get_cards("SkillCard", std::nullopt, std::nullopt);
    return 0;
}

::cw::lib::String GetSkillContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    const auto name = system_data() ? system_data()->get_skillname(m_data_typed ? m_data_typed->getint(".", "id", 0) : 0) : std::nullopt;
    if (!name) return "特殊技能カードが指定されていません";
    const S scope = m_data_typed ? m_data_typed->get("targets", "") : S{};
    if (scope == "SelectedCard") return "選択カードと交換して特殊技能カード『" + *name + "』を取得";
    const I number = m_data_typed ? m_data_typed->getint(".", "number", 0) : 0;
    return status_name(scope) + "が特殊技能カード『" + *name + "』を" + std::to_string(number) + "枚取得";
}

void LoseBeastContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    LoseContent::initialize(std::move(arg_data));
}

::cw::lib::Integer LoseBeastContent::action()
{
    lose_cards("BeastCard");
    return 0;
}

::cw::lib::String LoseBeastContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    if (m_data_typed->get("targets", "") == "SelectedCard") return "選択カードを喪失";
    const auto name = system_data() ? system_data()->get_beastname(m_data_typed->getint(".", "id", 0)) : std::nullopt;
    if (!name) return "召喚獣カードが指定されていません";
    const I number = m_data_typed->getint(".", "number", 0);
    return status_name(m_data_typed->get("targets", "")) + "が召喚獣カード『" + *name + "』を" +
           (number ? std::to_string(number) + "枚" : "全て") + "喪失";
}

void LoseCastContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    LoseContent::initialize(std::move(arg_data));
}

::cw::lib::Integer LoseCastContent::action()
{
    if (is_differentscenario(std::nullopt)) return 0;
    const auto sdata = system_data();
    if (!sdata || !::cw::g_cwpy) return 0;
    const auto it = std::find_if(sdata->m_friendcards.begin(), sdata->m_friendcards.end(),
                                 [this](const auto& card) { return card && card->m_id == m_resid; });
    if (it == sdata->m_friendcards.end()) return 0;
    const auto card = *it;
    if (auto yado = yado_data()) yado->changed();
    if (::cw::g_cwpy->is_battlestatus() && ::cw::g_cwpy->m_battle && *::cw::g_cwpy->m_battle) {
        auto& members = (*::cw::g_cwpy->m_battle)->m_members;
        // Python版と同様、参加中の一致を一つだけ除き、戦闘中の場合だ
        // け行動を消す。 古いメンバー一覧に重複があっても同じ規則にする。
        if (const auto member = std::find(members.begin(), members.end(), card); member != members.end()) {
            members.erase(member);
            card->clear_action();
        }
    }
    if (card->m_inusecardimg) ::cw::g_cwpy->clear_inusecardimg(card);
    sdata->m_friendcards.erase(it);
    card->set_vanished();
    return 0;
}

::cw::lib::String LoseCastContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    const auto name = system_data() ? system_data()->get_castname(m_data_typed->getint(".", "id", 0)) : std::nullopt;
    return name ? "キャストカード『" + *name + "』離脱" : "キャストカードが指定されていません";
}

void LoseCompleteStampContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    LoseContent::initialize(std::move(arg_data));
}

::cw::lib::Integer LoseCompleteStampContent::action()
{
    const S name = m_data_typed ? m_data_typed->get("scenario", "") : S{};
    if (!name.empty()) if (auto yado = yado_data()) yado->remove_compstamp(name);
    return 0;
}

::cw::lib::String LoseCompleteStampContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    const S name = m_data_typed ? m_data_typed->get("scenario", "") : S{};
    return name.empty() ? "終了シナリオが指定されていません" : "終了済みシナリオ『" + name + "』削除";
}

void LoseCouponContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    LoseContent::initialize(std::move(arg_data));
    if (!m_data_typed) return;
    m_coupon = m_data_typed->get("coupon", "");
    m_scope = m_data_typed->get("targets", "");
    m_holdingcoupon = m_data_typed->get("holdingcoupon", "");
    m_spchars = m_data_typed->getbool(".", "spchars", false);
}

::cw::lib::Integer LoseCouponContent::action()
{
    S coupon = value_string(m_coupon);
    if (m_spchars) {
        coupon = std::get<0>(::cw::sprite::message::rpl_specialstr(
            coupon, std::nullopt, true, "All", true, true, std::nullopt));
    }
    if (is_addablecoupon(coupon)) {
        const auto iface = event_interface();
        const auto targets = iface ? iface->get_targetscope(value_string(m_scope), false, false, value_string(m_holdingcoupon)) : std::vector<V>{};
        const auto effectevent = iface ? iface->get_effectevent() : std::nullopt;
        const bool targetout = effectevent && *effectevent && (*effectevent)->in_effectmotionloop() && coupon == "＠効果対象";
        for (const auto& value : targets) if (const auto target = character_target(value)) {
            target->remove_coupon(coupon);
            if (targetout) target->set_coupon("＠効果対象外", 0);
        }
    }
    return 0;
}

::cw::lib::String LoseCouponContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    const S coupon = value_string(m_coupon);
    return coupon.empty() ? "称号が指定されていません" :
           status_name(value_string(m_scope)) + "から称号『" + coupon + "』剥奪" + (m_spchars ? "(特殊文字を展開)" : "");
}

void LoseGossipContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    LoseContent::initialize(std::move(arg_data));
    m_gossip = m_data_typed ? m_data_typed->get("gossip", "") : V{};
    m_spchars = m_data_typed && m_data_typed->getbool(".", "spchars", false);
}

::cw::lib::Integer LoseGossipContent::action()
{
    S gossip = value_string(m_gossip);
    if (m_spchars) {
        gossip = std::get<0>(::cw::sprite::message::rpl_specialstr(
            gossip, std::nullopt, true, "All", true, true, std::nullopt));
    }
    if (!gossip.empty()) if (auto yado = yado_data()) yado->remove_gossip(gossip);
    return 0;
}

::cw::lib::String LoseGossipContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    const S gossip = value_string(m_gossip);
    return gossip.empty() ? "ゴシップが指定されていません" : "ゴシップ『" + gossip + "』削除" + (m_spchars ? "(特殊文字を展開)" : "");
}

void LoseInfoContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    LoseContent::initialize(std::move(arg_data));
    const auto name=system_data() ? system_data()->get_infoname(m_resid) : std::nullopt;
    m_name=name ? V(*name) : V{};
}

::cw::lib::Integer LoseInfoContent::action()
{
    if (is_differentscenario(std::nullopt)) return 0;
    if (system_data() && !m_name.is_none()) {
        if (auto yado = yado_data()) yado->changed();
        system_data()->remove_infocard(m_resid);
    }
    return 0;
}

::cw::lib::String LoseInfoContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    const auto name = system_data() ? system_data()->get_infoname(m_data_typed->getint(".", "id", 0)) : std::nullopt;
    return name ? "情報カード『" + *name + "』喪失" : "情報カードが指定されていません";
}

void LoseItemContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    LoseContent::initialize(std::move(arg_data));
}

::cw::lib::Integer LoseItemContent::action()
{
    lose_cards("ItemCard");
    return 0;
}

::cw::lib::String LoseItemContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    if (m_data_typed->get("targets", "") == "SelectedCard") return "選択カードを喪失";
    const auto name = system_data() ? system_data()->get_itemname(m_data_typed->getint(".", "id", 0)) : std::nullopt;
    if (!name) return "アイテムカードが指定されていません";
    const I number = m_data_typed->getint(".", "number", 0);
    return status_name(m_data_typed->get("targets", "")) + "がアイテムカード『" + *name + "』を" +
           (number ? std::to_string(number) + "枚" : "全て") + "喪失";
}

void LoseMoneyContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    LoseContent::initialize(std::move(arg_data));
}

::cw::lib::Integer LoseMoneyContent::action()
{
    if (const auto yado = yado_data(); yado && yado->m_party && *yado->m_party) {
        const auto value = m_data_typed ? m_data_typed->getint(".", "value", 0) : 0;
        // Python版は最小の固定幅相当の整数も正確に符号反転できる。 ネイ
        // ティブの正の範囲を超える値でも、引き続き所持金の上限へ飽和させる。
        const auto delta = value == (std::numeric_limits<I>::min)()
            ? (std::numeric_limits<I>::max)() : -value;
        (*yado->m_party)->set_money(delta, true, true);
    }
    return 0;
}

::cw::lib::String LoseMoneyContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    return (m_data_typed ? m_data_typed->get("value", "0") : "0") + "sp減少";
}

void LoseSkillContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    LoseContent::initialize(std::move(arg_data));
}

::cw::lib::Integer LoseSkillContent::action()
{
    lose_cards("SkillCard");
    return 0;
}

::cw::lib::String LoseSkillContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    if (m_data_typed->get("targets", "") == "SelectedCard") return "選択カードを喪失";
    const auto name = system_data() ? system_data()->get_skillname(m_data_typed->getint(".", "id", 0)) : std::nullopt;
    if (!name) return "特殊技能カードが指定されていません";
    const I number = m_data_typed->getint(".", "number", 0);
    return status_name(m_data_typed->get("targets", "")) + "が特殊技能カード『" + *name + "』を" +
           (number ? std::to_string(number) + "枚" : "全て") + "喪失";
}

void SetVariantContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    EventContentBase::initialize(std::move(arg_data), true);
    if (!m_data_typed) return;
    m_expression = m_data_typed->gettext("Expression", "");
    m_variant = m_data_typed->get("variant", "");
    m_step = m_data_typed->get("step", "");
    m_flag = m_data_typed->get("flag", "");
    m_parsed_expression.reset();
}

::cw::lib::Integer SetVariantContent::action()
{
    // ネイティブのComputeExceptionにはPythonのprint_ex()
    // 用トレースバックがないため、 評価失敗を示す接頭辞を付け、共通のエラ
    // ーダイアログ・ビルド設定に従うログへ渡す。 捕捉するのはPython
    // 版と同じComputeExceptionだけで、他の失敗は伝える。
    const auto sdata = system_data();
    if (!sdata) return 0;
    const auto event = event_interface() ? event_interface()->get_nowrunningevent() : std::nullopt;
    const bool different = is_differentscenario(std::nullopt);
    // 結果ではなく構文をキャッシュする。 Python版と同様、存在す
    // る対象ごとに前の代入後に一度評価し、対象がなければ評価しない。
    const auto evaluate = [&]() -> std::shared_ptr<::cw::data::Variant> {
        try {
            if (!m_parsed_expression) m_parsed_expression = ::cw::calculator::parse(m_expression);
            if (!m_parsed_expression || m_parsed_expression->empty()) return nullptr;
            auto option = std::make_shared<::cw::calculator::CalcOption>(); option->initialize("Event", different);
            return ::cw::calculator::eval_expr(*m_parsed_expression, option);
        } catch (const ::cw::calculator::ComputeException& ex) {
            variant_error("式の評価に失敗しました。", &ex, m_expression);
            return nullptr;
        }
    };
    if (const auto variant = sdata->find_variant(value_string(m_variant), different, event); variant && *variant) {
        if (const auto result = evaluate()) (*variant)->set(result->m_value, true);
    }
    if (const auto step = sdata->find_step(value_string(m_step), different, event); step && *step) {
        if (const auto result = evaluate()) {
        if (value_string(result->m_type) == "Number") {
            const I maximum = static_cast<I>((*step)->m_valuenames.size()) - 1;
            if (const auto* number = std::get_if<::cw::lib::Decimal>(&result->m_value.storage())) {
                // 巨大な結果も含め、狭い型へ変換する前にDecimal上で制限する。 Python
                // 版はintへ切り捨ててから下限優先のnumwrapを行うため、
                // 空のStepでは int(-0.5)==0 が上限-1に収まる。
                const I bounded = maximum < 0 ? (number->compare(::cw::lib::Decimal(I{-1})) <= 0 ? 0 : -1) :
                    number->compare(::cw::lib::Decimal(I{0})) <= 0 ? 0 :
                    number->compare(::cw::lib::Decimal(maximum)) >= 0 ? maximum :
                    number->to_integer(::cw::lib::DecimalRounding::Down);
                (*step)->set(bounded);
            } else (*step)->set(::cw::util::numwrap(value_int(result->m_value), I{0}, maximum));
        }
        else variant_error("計算結果 " + result->string_value() + " は数値ではありません。", nullptr, m_expression);
        }
    }
    if (const auto flag = sdata->find_flag(value_string(m_flag), different, event); flag && *flag) {
        if (const auto result = evaluate()) {
        if (value_string(result->m_type) == "Boolean") { (*flag)->set(value_bool(result->m_value)); (*flag)->redraw_cards(-1, false); }
        else variant_error("計算結果 " + result->string_value() + " は真偽値ではありません。", nullptr, m_expression);
        }
    }
    return 0;
}

::cw::lib::String SetVariantContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    const auto sdata = system_data();
    const bool different = is_differentscenario(arg_event);
    const auto variant = sdata ? sdata->find_variant(value_string(m_variant), different, arg_event) : std::nullopt;
    if (variant && *variant) return "〔 " + m_expression + " 〕の結果をコモン『" + value_string((*variant)->m_name) + "』へ代入";
    const auto step = sdata ? sdata->find_step(value_string(m_step), different, arg_event) : std::nullopt;
    if (step && *step) return "〔 " + m_expression + " 〕の結果をステップ『" + (*step)->m_name + "』へ代入";
    const auto flag = sdata ? sdata->find_flag(value_string(m_flag), different, arg_event) : std::nullopt;
    if (flag && *flag) return "〔 " + m_expression + " 〕の結果をフラグ『" + (*flag)->m_name + "』へ代入";
    return "代入先が指定されていません";
}

void TalkDialogContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    TalkContent::initialize(std::move(arg_data)); m__init_values = false;
}

::cw::lib::Integer TalkDialogContent::action()
{
    if (!m_data_typed) {
#if defined(CARDWIRTH_LOG_ENABLED)
        ::cardwirth::debug::log("TalkDialogContent: skip (no data)");
#endif
        return 0;
    }
    const auto names = get_selections_and_indexes();
    const S targetname = m_data_typed->get("targetm", "");
    V target;
    if (targetname == "Valued") {
        init_values();
        if (!m_coupons.empty()) {
            if (const auto valued = get_valuedmember("active", false); valued && *valued)
                target = std::static_pointer_cast<::cw::lib::Object>(
                    std::static_pointer_cast<::cw::sprite::card::CWPyCard>(*valued));
        } else {
            std::vector<std::shared_ptr<::cw::sprite::card::PlayerCard>> candidates;
            if (::cw::g_cwpy)
                for (const auto& card : ::cw::g_cwpy->get_pcards(V("active")))
                    if (card && !card->is_silence()) candidates.push_back(card);
            if (!candidates.empty()) {
                std::size_t index = 0;
                if (::cw::g_cwpy && ::cw::g_cwpy->m_dice)
                    index = ::cw::dice::choice_index(candidates.size());
                target = std::static_pointer_cast<::cw::lib::Object>(
                    std::static_pointer_cast<::cw::sprite::card::CWPyCard>(candidates[index]));
            }
        }
    } else if (event_interface()) target = event_interface()->get_targetmember(targetname, true, "");
    const auto character = character_target(target);
    if (!character) {
#if defined(CARDWIRTH_LOG_ENABLED)
        ::cardwirth::debug::log("TalkDialogContent: skip (no character) target=" + targetname +
            " " + talk_content_context(m_data_typed) + " scenario=" + scenario_name_for_log());
#endif
        return 0;
    }
    const auto coupons = character->get_coupons();
    const std::vector<S> coupon_names(coupons.begin(), coupons.end());
    const auto text = get_dialogtext(get_dialogs(), coupon_names);
    if (!text || (text->empty() && names.size() <= 1)) {
#if defined(CARDWIRTH_LOG_ENABLED)
        ::cardwirth::debug::log("TalkDialogContent: skip (empty dialog) target=" + targetname +
            " dialogs=" + std::to_string(get_dialogs().size()) + " selections=" + std::to_string(names.size()) +
            " " + talk_content_context(m_data_typed) + " scenario=" + scenario_name_for_log());
#endif
        if (m_data_typed->getbool(".", "selecttalker", false) && event_interface()) event_interface()->set_selectedmember(character);
        return 0;
    }
    if (!::cw::g_cwpy) {
#if defined(CARDWIRTH_LOG_ENABLED)
        ::cardwirth::debug::log("TalkDialogContent: skip (no cwpy) " + talk_content_context(m_data_typed));
#endif
        return 0;
    }
    if (text && !text->empty()) {
#if defined(CARDWIRTH_LOG_ENABLED)
        ::cardwirth::debug::log("TalkDialogContent: show begin type=MessageWindow target=" + targetname +
            " text_len=" + std::to_string(text->size()) + " selections=" + std::to_string(names.size()) +
            " " + talk_content_context(m_data_typed) + " scenario=" + scenario_name_for_log());
#endif
        auto window = std::make_shared<::cw::sprite::message::MessageWindow>();
        const auto imgpaths = build_dialog_talkers(target);
        const auto versionhint = object_versionhint(target);
        window->initialize(*text, names,
                           imgpaths.empty() ? std::nullopt : std::optional<MessageTalkers>(imgpaths),
                           target, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                           std::nullopt, std::nullopt, false, std::nullopt, 0, versionhint, std::nullopt, 0,
                           std::max<I>(1, m_data_typed->getint(".", "columns", 1)), std::nullopt,
                           m_data_typed->getbool(".", "centeringx", false),
                           m_data_typed->getbool(".", "centeringy", false),
                           m_data_typed->getbool(".", "boundarycheck", false));
        const I index = message_selection_index(::cw::g_cwpy->show_message(window)); if (m_data_typed->getbool(".", "selecttalker", false)) if (const auto c = character_target(target)) if (event_interface()) event_interface()->set_selectedmember(c);
#if defined(CARDWIRTH_LOG_ENABLED)
        ::cardwirth::debug::log("TalkDialogContent: show end type=MessageWindow result=" + std::to_string(index) +
            " " + talk_content_context(m_data_typed));
#endif
        return index;
    }
#if defined(CARDWIRTH_LOG_ENABLED)
    ::cardwirth::debug::log("TalkDialogContent: show begin type=SelectWindow target=" + targetname +
        " selections=" + std::to_string(names.size()) + " columns=" +
        std::to_string(std::max<I>(1, m_data_typed->getint(".", "columns", 1))) +
        " " + talk_content_context(m_data_typed) + " scenario=" + scenario_name_for_log());
#endif
    auto window = std::make_shared<::cw::sprite::message::SelectWindow>();
    window->initialize(names, "", std::nullopt, std::nullopt, false, std::nullopt, 0,
                       std::max<I>(1, m_data_typed->getint(".", "columns", 1)), true, std::nullopt,
                       std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                       std::nullopt, false);
    const I index = message_selection_index(::cw::g_cwpy->show_message(window));
    if (m_data_typed->getbool(".", "selecttalker", false) && character &&
        event_interface())
        event_interface()->set_selectedmember(character);
#if defined(CARDWIRTH_LOG_ENABLED)
    ::cardwirth::debug::log("TalkDialogContent: show end type=SelectWindow result=" + std::to_string(index) +
        " " + talk_content_context(m_data_typed));
#endif
    return index;
}

bool TalkDialogContent::can_action()
{
    const S targetname = m_data_typed ? m_data_typed->get("targetm", "") : S{};
    V target;
    if (targetname == "Valued") {
        // Python版と同様、可否判定には常に正の評価点を使う。
        // 操作側の称号なしのランダムな代替は、ここへ適用しない。
        if (const auto valued = get_valuedmember("active", false);
                   valued && *valued) {
            target = std::static_pointer_cast<::cw::lib::Object>(
                std::static_pointer_cast<::cw::sprite::card::CWPyCard>(*valued));
        }
    } else if (event_interface()) {
        target = event_interface()->get_targetmember(targetname, true, "");
    }
    const auto character = character_target(target);
    if (!character) return false;
    const auto coupons = character->get_coupons();
    const std::vector<S> coupon_names(coupons.begin(), coupons.end());
    const auto text = get_dialogtext(get_dialogs(), coupon_names);
    return (text && !text->empty()) || get_selections_and_indexes().size() > 1;
}

std::vector<std::tuple<std::vector<::cw::lib::String>, ::cw::lib::String>> TalkDialogContent::get_dialogs()
{
    std::vector<std::tuple<std::vector<S>, S>> result;
    if (!m_data_typed) throw std::logic_error("TalkDialogContent has no XML data");
    // Dialogs要素を必須とし、欠落を空のセリフ一覧として扱わない。
    const auto dialogs = m_data_typed->find_exists("Dialogs");
    for (const auto& d : dialogs->m_children) if (d) {
        std::vector<S> req;
        const S encoded = d->gettext("RequiredCoupons", "");
        if (!encoded.empty())
            for (const auto& coupon : ::cw::util::decodetextlist(encoded))
                if (!coupon.empty()) req.push_back(coupon);
        result.emplace_back(std::move(req), d->gettext("Text", ""));
    }
    return result;
}

std::optional<::cw::lib::String> TalkDialogContent::get_dialogtext(std::vector<std::tuple<std::vector<::cw::lib::String>, ::cw::lib::String>> arg_dialogs, std::vector<::cw::lib::String> arg_coupons)
{
    for (const auto& [required, text] : arg_dialogs) {
        bool all = true;
        for (const auto& coupon : required)
            if (std::find(arg_coupons.begin(), arg_coupons.end(), coupon) == arg_coupons.end()) {
                all = false;
                break;
            }
        if (all || required.empty()) return ::cw::util::decodewrap(text, "\n");
    }
    return std::nullopt;
}

::cw::lib::String TalkDialogContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    S text;
    if (m_data_typed) if (const auto dialogs = m_data_typed->find("Dialogs"); dialogs && *dialogs && !(*dialogs)->m_children.empty())
        text = (*dialogs)->m_children.front()->gettext("Text", "");
    return "[" + status_name(m_data_typed ? m_data_typed->get("targetm", "") : "") + "] " + remove_status_wraps(std::move(text));
}

void TalkMessageContent::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    TalkContent::initialize(std::move(arg_data));
}

::cw::lib::Integer TalkMessageContent::action()
{
    if (!m_data_typed || !::cw::g_cwpy) {
#if defined(CARDWIRTH_LOG_ENABLED)
        ::cardwirth::debug::log("TalkMessageContent: skip (no data or cwpy)");
#endif
        return 0;
    }
    const S text = ::cw::util::decodewrap(m_data_typed->gettext("Text", ""));
    const auto names = get_selections_and_indexes();
    const bool singleline = m_data_typed->getbool(".", "singleline", false);
    const I columns = std::max<I>(1, m_data_typed->getint(".", "columns", 1));
    const bool centering_x = m_data_typed->getbool(".", "centeringx", false);
    const bool centering_y = m_data_typed->getbool(".", "centeringy", false);
    const bool boundarycheck = m_data_typed->getbool(".", "boundarycheck", false);
    const bool selecttalker = m_data_typed->getbool(".", "selecttalker", false);
    const auto built = build_talk_message_talkers(m_data_typed, singleline);
    if (!built.talk) {
#if defined(CARDWIRTH_LOG_ENABLED)
        ::cardwirth::debug::log("TalkMessageContent: skip (no talker)");
#endif
        return 0;
    }
    if (!text.empty()) {
#if defined(CARDWIRTH_LOG_ENABLED)
        ::cardwirth::debug::log("TalkMessageContent: show begin type=MessageWindow text_len=" +
            std::to_string(text.size()) + " selections=" + std::to_string(names.size()) +
            " columns=" + std::to_string(columns) + " " + talk_content_context(m_data_typed) +
            " scenario=" + scenario_name_for_log());
#endif
        if (singleline) {
            auto window = std::make_shared<::cw::sprite::message::SelectWindow>();
            window->initialize(names, text, std::nullopt, std::make_tuple<I, I>(470, 40), false, std::nullopt, 0,
                               columns, true, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                               built.versionhint, message_specialchars(), std::nullopt, centering_x);
            const I index = message_selection_index(::cw::g_cwpy->show_message(window));
            if (selecttalker && built.firstchartalker && event_interface())
                event_interface()->set_selectedmember(built.firstchartalker);
#if defined(CARDWIRTH_LOG_ENABLED)
            ::cardwirth::debug::log("TalkMessageContent: show end type=SelectWindow(singleline) result=" +
                std::to_string(index) + " " + talk_content_context(m_data_typed));
#endif
            return index;
        }
        auto window = std::make_shared<::cw::sprite::message::MessageWindow>();
        window->initialize(text, names,
                           built.talkers.empty() ? std::nullopt : std::optional<MessageTalkers>(built.talkers),
                           built.firsttalker, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                           std::nullopt, std::nullopt, false, std::nullopt, 0, built.versionhint, std::nullopt, 0,
                           columns, std::nullopt, centering_x, centering_y, boundarycheck);
        const I index = message_selection_index(::cw::g_cwpy->show_message(window));
        if (selecttalker && built.firstchartalker && event_interface())
            event_interface()->set_selectedmember(built.firstchartalker);
#if defined(CARDWIRTH_LOG_ENABLED)
        ::cardwirth::debug::log("TalkMessageContent: show end type=MessageWindow result=" +
            std::to_string(index) + " " + talk_content_context(m_data_typed));
#endif
        return index;
    }
    if (names.size() > 1) {
#if defined(CARDWIRTH_LOG_ENABLED)
        ::cardwirth::debug::log("TalkMessageContent: show begin type=SelectWindow selections=" +
            std::to_string(names.size()) + " columns=" + std::to_string(columns) + " " +
            talk_content_context(m_data_typed) + " scenario=" + scenario_name_for_log());
#endif
        auto window = std::make_shared<::cw::sprite::message::SelectWindow>();
        window->initialize(names, "", std::nullopt, std::nullopt, false, std::nullopt, 0, columns, true,
                           std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                           std::nullopt, std::nullopt, false);
        const I index = message_selection_index(::cw::g_cwpy->show_message(window));
        if (selecttalker && built.firstchartalker && event_interface())
            event_interface()->set_selectedmember(built.firstchartalker);
#if defined(CARDWIRTH_LOG_ENABLED)
        ::cardwirth::debug::log("TalkMessageContent: show end type=SelectWindow result=" +
            std::to_string(index) + " " + talk_content_context(m_data_typed));
#endif
        return index;
    }
    if (selecttalker && built.firstchartalker && event_interface())
        event_interface()->set_selectedmember(built.firstchartalker);
    return 0;
}

bool TalkMessageContent::can_action()
{
    if (!m_data_typed) return false;
    const bool singleline = m_data_typed->getbool(".", "singleline", false);
    if (!can_build_talk_message_talkers(m_data_typed, singleline)) return false;
    return !m_data_typed->gettext("Text", "").empty() ||
           get_selections_and_indexes().size() > 1;
}

::cw::lib::String TalkMessageContent::get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    (void)arg_event;
    if (!m_data_typed) return {};
    const S path = m_data_typed->get("path", "");
    S prefix;
    if (path.ends_with("??Random")) prefix = "[ランダム] ";
    else if (path.ends_with("??Selected")) prefix = "[選択中] ";
    else if (path.ends_with("??Unselected")) prefix = "[選択外] ";
    else if (path.ends_with("??Card")) prefix = "[カード] ";
    else if (!path.empty()) prefix = "[" + path + "] ";
    return prefix + remove_status_wraps(m_data_typed->gettext("Text", ""));
}

::cw::lib::Integer g_VK_LBUTTON{};
std::map<::cw::lib::String, ::cw::lib::String> g_methoddict{
    {"MoveToYado", "set_yado"}, {"MoveToTitle", "set_title"},
    {"Exit", "quit2"}, {"ShowDialog", "call_dlg"}, {"MoveCard", "trade"},
    {"ChangeToSpecialArea", "change_specialarea"}, {"LoadParty", "load_party"},
    {"InterruptAdventure", "interrupt_adventure"}, {"DissolveParty", "dissolve_party"},
    {"Load", "reload_yado"}, {"StartScenario", "start_scenario"}
};
std::shared_ptr<::cw::content::MouseWatcher> g_mosuewatcher = [] {
    auto watcher = std::make_shared<::cw::content::MouseWatcher>();
    watcher->initialize();
    return watcher;
}();

::cw::lib::String variant_error_msg(std::shared_ptr<::cw::calculator::ComputeException> arg_ex)
{
    if (!arg_ex) return {};
    return variant_error_msg(*arg_ex);
}

::cw::lib::String variant_error_msg(const ::cw::calculator::ComputeException& arg_ex)
{
    const I line = value_int(arg_ex.m_line, 0), pos = value_int(arg_ex.m_pos, 0);
    // Python版は引数位置に1を加える。内側の文字列位置ではTypeErrorに
    // なるため、 黙って位置0にせず、ネイティブのCompute以外の失敗にする。
    const auto argument_number = [](const V& index) {
        if (std::holds_alternative<S>(index.storage())) throw std::invalid_argument("argument index cannot add String and Integer");
        return std::to_string(value_int(index, 0) + 1);
    };
    const auto function_argument = [&](const S& function, const V& index, const V& value, const S& type) {
        return "関数 " + function + " の " + argument_number(index) + " 番目の引数が" + type + "ではありません(値=" + value_string(value) + ")";
    };
    if (dynamic_cast<const ::cw::calculator::TokanizeException*>(&arg_ex)) return "使用できない文字があります(行:" + std::to_string(line) + " 位置:" + std::to_string(pos) + ")";
    if (dynamic_cast<const ::cw::calculator::SemanticsException*>(&arg_ex)) return "構文が正しくありません(行:" + std::to_string(line) + " 位置:" + std::to_string(pos) + ")";
    if (const auto ex = dynamic_cast<const ::cw::calculator::FunctionIsNotDefinedException*>(&arg_ex)) return "関数 " + value_string(ex->m_func_name) + " は定義されていません。";
    if (const auto ex = dynamic_cast<const ::cw::calculator::ArgumentIsNotDecimalException*>(&arg_ex)) return function_argument(value_string(ex->m_func_name), ex->m_arg_index, ex->m_arg_value, "数値");
    if (const auto ex = dynamic_cast<const ::cw::calculator::ArgumentIsNotStringException*>(&arg_ex)) return function_argument(value_string(ex->m_func_name), ex->m_arg_index, ex->m_arg_value, "文字列");
    if (const auto ex = dynamic_cast<const ::cw::calculator::ArgumentIsNotBooleanException*>(&arg_ex)) return function_argument(value_string(ex->m_func_name), ex->m_arg_index, ex->m_arg_value, "真偽値");
    if (const auto ex = dynamic_cast<const ::cw::calculator::ArgumentsCountException*>(&arg_ex)) return "関数 " + value_string(ex->m_func_name) + " の呼び出し引数の数が間違っています。";
    if (const auto ex = dynamic_cast<const ::cw::calculator::InvalidArgumentException*>(&arg_ex)) return "関数 " + value_string(ex->m_func_name) + " の " + argument_number(ex->m_arg_index) + " 番目の引数の値が間違っています(値=" + value_string(ex->m_arg_value) + ")";
    if (const auto ex = dynamic_cast<const ::cw::calculator::VariantNotFoundException*>(&arg_ex)) return "コモン『" + value_string(ex->m_path) + "』はありません。";
    if (const auto ex = dynamic_cast<const ::cw::calculator::FlagNotFoundException*>(&arg_ex)) return "フラグ『" + value_string(ex->m_path) + "』はありません。";
    if (const auto ex = dynamic_cast<const ::cw::calculator::StepNotFoundException*>(&arg_ex)) return "ステップ『" + value_string(ex->m_path) + "』はありません。";
    if (dynamic_cast<const ::cw::calculator::DifferentScenarioException*>(&arg_ex)) return "シナリオ名と作者名が一致しないため、状態変数にアクセスできません。";
    if (const auto ex = dynamic_cast<const ::cw::calculator::ListIndexOutOfRangeException*>(&arg_ex)) return "リストに" + value_string(ex->m_n) + "番目の要素は存在しません(リストの長さ = " + value_string(ex->m_list_len) + ")";
    if (const auto ex = dynamic_cast<const ::cw::calculator::ArgumentIsNotListException*>(&arg_ex)) return function_argument(value_string(ex->m_func_name), ex->m_arg_index, ex->m_arg_value, "リスト");
    if (const auto ex = dynamic_cast<const ::cw::calculator::ArgumentIsNotStructureException*>(&arg_ex)) return "関数 " + value_string(ex->m_func_name) + " の " + argument_number(ex->m_arg_index) + " 番目の引数が" + value_string(ex->m_struct_name) + "ではありません(値=" + value_string(ex->m_arg_value) + ")";
    if (const auto ex = dynamic_cast<const ::cw::calculator::ExprPermissionError*>(&arg_ex)) {
        const auto info = std::get_if<::cw::lib::ObjectPtr>(&ex->m_info.storage());
        const auto member = std::get_if<::cw::lib::ObjectPtr>(&ex->m_m.storage());
        const auto structure = info && *info ? std::dynamic_pointer_cast<::cw::calculator::StructureInfo>(*info) : nullptr;
        const auto field = member && *member ? std::dynamic_pointer_cast<::cw::calculator::StructureMember>(*member) : nullptr;
        const S structure_name = structure ? ::cw::lib::encoding::python_upper(value_string(structure->m_name)) : S{};
        if (field) return structure_name + "." + ::cw::lib::encoding::python_upper(value_string(field->m_name)) + "は参照できません。";
        return structure_name + "は関数で生成しなければなりません。";
    }
    if (const auto ex = dynamic_cast<const ::cw::calculator::DifferentStructureException*>(&arg_ex)) return "異なる構造体を比較しようとしました(" + value_string(ex->m_lhs_name) + ":" + value_string(ex->m_rhs_name) + ")";
    if (dynamic_cast<const ::cw::calculator::ZeroDivisionException*>(&arg_ex)) return "ゼロによる除算が発生しました(行:" + std::to_string(line) + " 位置:" + std::to_string(pos) + ")";
    // Python版は不明な例外をassertで拒否する。 ネイティ
    // ブのRelease構成でも、文言を作り上げず拒否を維持する。
    throw std::logic_error("unknown compute exception in variant_error_msg");
}

void BranchKeyCodeContent__find_keycode_cards_append_card(std::optional<std::shared_ptr<::cw::header::CardHeader>> arg_header)
{
    // Python版は、結果ベクターと既出集合を取り込んだクロージャーとして
    // 定義する。 登録用のシンボルにはその状態がないため実行時に使わず、 完
    // 全な処理を持つ _find_keycode_cards() を使う。
    (void)arg_header;
}

std::tuple<::cw::lib::String, bool, bool> _get_couponscope(::cw::lib::String arg_scope)
{
    if (arg_scope == "Random") return {"Party", true, false};
    if (arg_scope == "Party") return {"Party", false, true};
    if (arg_scope == "Field") return {"FieldCasts", true, false};
    return {arg_scope, true, false};
}

bool _target_has_coupon(std::shared_ptr<::cw::character::Character> arg_target, std::vector<::cw::lib::String> arg_names, bool arg_allmatch)
{
    // Pythonのall()/any()と同様、空の一覧では対象を評価しない。
    if (arg_names.empty()) return arg_allmatch;
    if (!arg_target) return false;
    auto has = [&](const S& coupon) {
        const bool system = ::cw::g_cwpy && ::cw::g_cwpy->m_syscoupons &&
            (::cw::g_cwpy->m_syscoupons->match(coupon) || ::cw::g_cwpy->m_syscoupons->unconditional_match(coupon));
        return system || (::cw::g_cwpy && ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_skinsyscoupons &&
            ::cw::g_cwpy->m_setting->m_skinsyscoupons->match(coupon)) || arg_target->has_coupon(coupon);
    };
    if (arg_allmatch) return std::all_of(arg_names.begin(), arg_names.end(), has);
    return std::any_of(arg_names.begin(), arg_names.end(), has);
}

bool _target_has_coupon_has_coupon(::cw::lib::String arg_coupon)
{
    return ::cw::g_cwpy && ::cw::g_cwpy->m_syscoupons &&
        ::cw::g_cwpy->m_syscoupons->unconditional_match(arg_coupon);
}

std::vector<std::shared_ptr<::cw::character::Character>> _get_coupon_owners(std::vector<std::shared_ptr<::cw::character::Character>> arg_targets, std::vector<::cw::lib::String> arg_names, bool arg_allmatch)
{
    std::vector<std::shared_ptr<::cw::character::Character>> result;
    for (const auto& target : arg_targets) if (_target_has_coupon(target, arg_names, arg_allmatch)) result.push_back(target);
    return result;
}

bool _has_coupon(std::vector<std::shared_ptr<::cw::character::Character>> arg_targets, std::vector<::cw::lib::String> arg_names, ::cw::lib::String arg_scope, bool arg_someone, bool arg_allmatch, bool arg_multi, bool arg_invert)
{
    if (arg_names.empty()) return false;
    if (!arg_multi && arg_names.size() == 1 && _target_has_coupon_has_coupon(arg_names.front())) return !arg_invert;
    bool flag = false;
    std::shared_ptr<::cw::character::Character> selected;
    for (const auto& target : arg_targets) {
        flag = _target_has_coupon(target, arg_names, arg_allmatch);
        if (arg_invert) flag = !flag;
        if (flag && arg_someone) { selected = target; break; }
        if (!flag && !arg_someone) { selected = target; break; }
    }
    if ((!arg_multi || flag) && arg_scope != "Selected" && ::cw::g_cwpy && ::cw::g_cwpy->m_event) {
        if (arg_scope == "Party" && !arg_someone && flag) selected.reset();
        if (selected) ::cw::g_cwpy->m_event->set_selectedmember(selected);
        else ::cw::g_cwpy->m_event->clear_selectedmember();
    }
    return flag;
}

bool _has_coupon_has_coupon(std::shared_ptr<::cw::character::Character> arg_target, ::cw::lib::String arg_coupon)
{
    return _target_has_coupon(arg_target, {arg_coupon}, false);
}

bool call_package(::cw::lib::Integer arg_resid, bool arg_call)
{
    // 不完全なネイティブ環境では、Pythonの直接参照と異なり不足した依存先を拒否する。 戻り先コンテント・実行イベント情報がなければnull所有者を保存せず、そのスタック項目を省く。 有効なパッケージは、Python版と同様に現在イベントなしでも開始できる。
    auto iface = event_interface();
    auto sdata = system_data();
    if (!iface || !sdata || !::cw::g_cwpy || !::cw::g_cwpy->m_sct) return false;
    std::shared_ptr<::cw::data::CWPyElement> element;
    std::tuple<S, S, bool, bool, bool> versionhint{};
    const auto cached = iface->m_nowrunningpacks.find(arg_resid);
    if (cached == iface->m_nowrunningpacks.end()) {
        const auto data = sdata->get_packagedata(arg_resid, "", false, std::nullopt);
        if (!data || !*data) return false;
        element = *data;
        if (const auto property = element->find("Property"); property && *property)
            versionhint = ::cw::g_cwpy->m_sct->from_basehint((*property)->get("versionHint", ""));
        const auto event_element = element->find("Events/Event");
        if (!event_element || !*event_element) return false;
        element = *event_element;
        iface->m_nowrunningpacks.emplace(arg_resid, std::make_tuple(element, versionhint));
    } else {
        element = std::get<0>(cached->second);
        versionhint = std::get<1>(cached->second);
    }
    auto packevent = std::make_shared<::cw::event::Event>();
    // elementは共有キャッシュのEvents/Event XMLノ
    // ード。 共有ルートを変更せずに読んで実行時Eventを組み立て、
    // 後のパッケージ呼び出しでも同じ元ノードを参照できるようにする。
    packevent->initialize(element);
    packevent->m_packageid = arg_resid;
    if (!packevent->m_starttree) return false;
    const auto current = iface->get_event();
    if (!current || !*current) {
        iface->append_event(packevent);
        const auto started = iface->get_event();
        if (started && *started) (*started)->start(false);
        return true;
    }
    auto event = *current;
    // カード実行で隠された実効ヒントではなく、実際のスロットを保存する。
    const auto area_index = static_cast<std::size_t>(::cw::g_HINT_AREA);
    const auto versionhint_base = area_index < sdata->m_versionhint.size()
        ? sdata->m_versionhint[area_index] : std::nullopt;
    const auto nowrunning = iface->get_nowrunningevent();
    if (arg_call) {
        const auto saved_content = event->m_cur_content;
        const auto saved_line_index = event->m_line_index;
        if (saved_content)
            event->m_nowrunningcontents.emplace_back(packevent, *saved_content, saved_line_index, versionhint_base);
        iface->append_event(packevent);
        packevent->m_parent = event;
        if (nowrunning && *nowrunning)
            iface->append_stackinfo(stack_trace_frame(*nowrunning, saved_content ? *saved_content : nullptr, saved_line_index));
    } else {
        iface->replace_event(packevent, std::make_tuple(::cw::g_HINT_AREA, versionhint_base));
        const auto replaced = iface->get_event();
        if (replaced && *replaced) iface->replace_stackinfo(-1, object_value(std::static_pointer_cast<::cw::lib::Object>(*replaced)));
        if (replaced && *replaced) event = *replaced;
    }
    // ルート・制御用Eventが、既存の Event::run() ループを続け
    // る。 Python版と同様、その制御用Eventをパッケージツリーへ切り替
    // え、 このローカルポインターを切り離されたパッケージEventへ変えない。
    event->m_cur_content = packevent->m_starttree;
    event->m_line_index = 0;
    if (::cw::g_cwpy->is_playingscenario()) sdata->set_versionhint(::cw::g_HINT_AREA, versionhint);
    return true;
}

void ChangeAreaContent_action_change_area(::cw::lib::Integer arg_resid, std::tuple<::cw::lib::String, ::cw::lib::Value> arg_ttype)
{
    if (!::cw::g_cwpy) return;
    // Python版と同様、遷移を予約してからAreaChangeError
    // を出す。 ここでchange_areaを同期実行すると、
    // 到着イベントがスタックに残る間に移動先へ再入し、 後処理
    // が親イベントを消費して入力を永久に禁止する場合がある。
    ::cw::g_cwpy->exec_func(
        [arg_resid, transition = std::move(arg_ttype)](
            const ::cw::lib::Arguments&, const ::cw::lib::KeywordArguments&) {
            if (::cw::g_cwpy) {
                ::cw::g_cwpy->change_area(arg_resid, true, false, transition,
                                          false, false, false, true, std::nullopt,
                                          false, false, false, false, false);
            }
            return ::cw::lib::Value{};
        });
}

bool check_areaid(::cw::lib::Integer arg_resid)
{
    const auto yado = yado_data();
    if (!::cw::g_cwpy || ::cw::g_cwpy->m_status != "Yado" || !yado) return true;
    const bool skin_area = ::cw::g_SKIN_AREAS_MIN <= arg_resid && arg_resid <= ::cw::g_SKIN_AREAS_MAX;
    const bool base_area = (arg_resid == 1 && !yado->m_party) || (arg_resid == 2 && yado->m_party);
    if (skin_area || base_area) return true;
    const S message = "スキン固有エリアのIDは" + std::to_string(::cw::g_SKIN_AREAS_MIN) + "-" +
        std::to_string(::cw::g_SKIN_AREAS_MAX) + "の間で設定してください(指定されたID=" +
        std::to_string(arg_resid) + ")。";
    ::cw::g_cwpy->call_dlg("ERROR", {{"text", V(message)}});
    return false;
}

std::vector<std::shared_ptr<::cw::sprite::card::CWPyCard>> EffectContent_action_initial_effect(std::vector<std::shared_ptr<::cw::sprite::card::CWPyCard>> arg_targets, std::optional<std::shared_ptr<::cw::event::Targeting>> arg_tevent)
{
    if (!arg_tevent || !*arg_tevent) return arg_targets;
    // UI描画・待機はevent::initial_effectが担当し、ここでは対象クーポンを
    // 保持したまま呼出し側が順序どおり適用できるようにする。
    for (const auto& target : arg_targets) if (target) target->set_cardtarget();
    return arg_targets;
}

void EffectContent_action_initial_effect_remove_target(std::shared_ptr<::cw::sprite::card::CWPyCard> arg_target)
{
    if (arg_target) {
        arg_target->clear_cardtarget();
        if (auto character = std::dynamic_pointer_cast<::cw::character::Character>(arg_target)) character->remove_coupon("＠効果対象");
    }
}

void EffectContent_action_apply(std::shared_ptr<::cw::sprite::card::CWPyCard> arg_target)
{
    if (!arg_target) return;
    arg_target->set_cardtarget();
    // 注記(互換API/効果): Pythonのローカルapplyクロージャが保持する
    // EffectContextは引数に存在しないため、対象フラグ更新までをここで行う。
    // 効果モーション本体はEffectContent::actionのコンテキスト付き経路へ接続済み。
}

void end_scenario(bool arg_complete)
{
    const auto yado = yado_data();
    if (!::cw::g_cwpy || !yado || !yado->m_party || !*yado->m_party) return;
    const auto party = *yado->m_party;
    if (party->m_members.empty()) {
        ::cw::g_cwpy->set_gameover();
        return;
    }
    if (::cw::g_cwpy->m_advlog) ::cw::g_cwpy->m_advlog->separator();
    const auto sdata = scenario();
    if (arg_complete && sdata) yado->set_compstamp(value_string(sdata->m_name));
    if (::cw::g_cwpy->m_battle && *::cw::g_cwpy->m_battle &&
        (*::cw::g_cwpy->m_battle)->is_running())
        (*::cw::g_cwpy->m_battle)->end(false, true);
    for (const auto& pcard : ::cw::g_cwpy->get_pcards(V{})) {
        if (pcard && !pcard->m_zoomimgs.empty())
            ::cw::animation::animate_sprite(std::static_pointer_cast<::cw::sprite::base::CWPySprite>(
                std::static_pointer_cast<::cw::sprite::card::CWPyCard>(pcard)), "zoomout");
    }
    ::cw::g_cwpy->clear_inusecardimg(std::nullopt);
    ::cw::g_cwpy->clear_guardcardimg();
    ::cw::g_cwpy->hide_cards(true, true, false, true, "", false);
    if (sdata && sdata->m_in_f9) return;
    if (sdata) sdata->m_in_endprocess = true;
    if (!party->m_vanished_pcards.empty()) {
        // Pythonの sort_by_attr と同様に安定整列し、 消去の
        // 取り消し・表示アニメーションで、同じ位置のカードの元の順序を維持する。
        std::stable_sort(party->m_vanished_pcards.begin(), party->m_vanished_pcards.end(),
                  [](const auto& lhs, const auto& rhs) { return lhs && rhs ? lhs->m_index_typed < rhs->m_index_typed : static_cast<bool>(lhs); });
        std::vector<std::shared_ptr<::cw::sprite::base::CWPySprite>> deals;
        const bool show_deal = ::cw::g_cwpy->m_is_showparty;
        const bool all_quickdeal = ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_all_quickdeal;
        for (const auto& card : party->m_vanished_pcards) {
            if (!card) continue;
            card->cancel_vanish();
            auto sprite = std::static_pointer_cast<::cw::sprite::base::CWPySprite>(
                std::static_pointer_cast<::cw::sprite::card::CWPyCard>(card));
            if (show_deal && !all_quickdeal) ::cw::animation::animate_sprite(sprite, "deal", true, false, false, false);
            else if (show_deal && all_quickdeal) deals.push_back(sprite);
        }
        if (!deals.empty()) ::cw::animation::animate_sprites(deals, "deal", true, false);
        party->m_vanished_pcards.clear();
    }
    for (const auto& card : ::cw::g_cwpy->get_pcards(V{})) if (card) card->remove_timedcoupons(false);
    for (const auto& card : ::cw::g_cwpy->get_fcards(V{})) if (card) card->remove_timedcoupons(false);
    ::cw::g_cwpy->show_party();
    ::cw::g_cwpy->check_level(true);
    if (::cw::g_cwpy->m_rsrc && ::cw::g_cwpy->m_rsrc->m_specialchars_is_changed)
        ::cw::g_cwpy->m_rsrc->m_specialchars = ::cw::g_cwpy->m_rsrc->get_specialchars();
    if (sdata) sdata->end(true, arg_complete, false);
    party->write();
    if (::cw::g_cwpy->m_advlog) ::cw::g_cwpy->m_advlog->end_scenario(true, arg_complete);
    for (const auto& music : ::cw::thread::CWPy::s_music) if (music) music->stop(0);
    // Python版と同様、set_yadoを予約してからScenarioEndErrorを出
    // す。 現在のイベント・カードの後処理を終えてから、シナリオデータを宿データへ置き換える。
    const auto engine = ::cw::g_cwpy;
    engine->exec_func([engine](const ::cw::lib::Arguments&, const ::cw::lib::KeywordArguments&) {
        engine->set_yado();
        return ::cw::lib::Value{};
    });
    // 意図的な状態表現の違いとして、Python版は保留中の遷移に_dealing=true
    // を使うが、 ネイティブの is_runningevent()/is_lockmenucards()
    // は予約済みexec_funcも含む。 m__dealing
    // は実行中のカード操作専用にし、復元処理でtrue
    // が残って宿への遷移後も入力を禁止しないようにする。
    throw ::cw::event::ScenarioEndError{};
}

bool GetContent_get_cards_is_nocache(::cw::lib::Value arg_target)
{
    if (value_sequence(arg_target)) return false; // 荷物袋は共有キャッシュを使用
    if (const auto character = character_target(arg_target)) {
        const auto space = character->get_cardpocketspace();
        const bool skill_full = std::get<0>(space) <= static_cast<I>(character->get_pocketcards(::cw::g_POCKET_SKILL).size());
        const bool item_full = std::get<1>(space) <= static_cast<I>(character->get_pocketcards(::cw::g_POCKET_ITEM).size());
        const bool beast_full = std::get<2>(space) <= static_cast<I>(character->get_pocketcards(::cw::g_POCKET_BEAST).size());
        // 呼出し側でカード種別を絞る前の共通入口なので、どれかが満杯なら
        // 個人カードXMLを共有キャッシュから分離する。
        return skill_full || item_full || beast_full;
    }
    return true;
}

void get_card(std::shared_ptr<::cw::data::CWPyElementTree> arg_etree, ::cw::lib::Value arg_target, bool arg_notscenariocard, ::cw::lib::Integer arg_toindex, ::cw::lib::Integer arg_insertorder, std::optional<std::shared_ptr<::cw::data::Party>> arg_party, ::cw::lib::String arg_copymaterialfrom, bool arg_fromdebugger, bool arg_from_getcontent, bool arg_attachment, bool arg_update_image, bool arg_anotherscenariocard, std::optional<std::shared_ptr<::cw::header::CardHeader>> arg_header_for_update_variables, std::optional<std::vector<std::shared_ptr<::cw::header::CardHeader>>> arg_ignore_list_for_update_variables)
{
    const auto yado = yado_data();
    if (!::cw::g_cwpy || !yado || !arg_etree) return;
    const auto root = arg_etree->getroot();
    if (!root) return;
    S name = ::cw::util::repl_dischar(arg_etree->gettext("Property/Name", "noname"));
    const bool from_scenario = !arg_notscenariocard;
    if (from_scenario) root->set("scenariocard", "True");
    if (arg_anotherscenariocard) root->set("anotherscenariocard", "True");
    if (root->m_tag == "BeastCard") {
        if (arg_fromdebugger || arg_from_getcontent) arg_attachment = true;
        // Python版と同様、付帯召喚でもUseLimitを必須
        // とし、その文字列が正確に0ならKeyCodesも必須とす
        // る。 任意検索・既定値で不足XMLを獲得成功へ変えない。
        if (arg_etree->gettext("Property/UseLimit", std::nullopt) == "0") {
            const auto messages = typed_object<::cw::setting::MsgDict>(::cw::g_cwpy->m_msgs);
            if (!messages) throw std::invalid_argument("get_card: message resources are unavailable");
            const auto recycle_keycode = messages->get_item("recycle_keycode");
            const auto keycodes = ::cw::util::decodetextlist(
                arg_etree->gettext("Property/KeyCodes", std::nullopt));
            const bool recycle = std::find(keycodes.begin(), keycodes.end(), recycle_keycode) != keycodes.end();
            if (!(recycle && arg_attachment) && !arg_attachment)
                arg_etree->edit("Property/UseLimit", "1", std::nullopt);
        }
        auto attachment = root->find("Property/Attachment");
        if (attachment && *attachment) (*attachment)->set_text(arg_attachment ? "True" : "False");
        else if (const auto property = root->find("Property"); property && *property)
            (*property)->append(::cw::data::make_element("Attachment", arg_attachment ? "True" : "False"));
    }
    std::optional<std::shared_ptr<::cw::character::Character>> owner;
    if (const auto character = character_target(arg_target)) owner = character;
    auto header = std::make_shared<::cw::header::CardHeader>();
    const auto sdata = system_data();
    // Python版と同様、ここでは CardHeader(carddata=...)
    // を使う。dataとして渡すと、 新規カードに元
    // シナリオのfpathが残り、後の破棄・移動がシナ
    // リオXMLへ触れてしまう。 一時ヘッダーのowner
    // もNoneとし、ポケットへXMLを追加した後にtrade
    // が対象キャラクターを設定する。 先に対象を付
    // けると、新規召喚獣を既存カードと誤認し、BeastCards
    // から除こうとして失敗する。
    header->initialize(std::nullopt, std::nullopt, root, from_scenario, sdata ? sdata->m_scedir : S{}, false,
                       std::nullopt, std::nullopt, {}, {});
    header->m_fpath.clear();
    // 変数は共通のネイティブValueMapで複製する。
    // std::mapの走査はPython辞書の挿入順では
    // なくキー順だが、 ネイティブ変数APIの一貫性を保ち
    // 、Python版と同じ通知・書き込み順とはしない。
    if (arg_header_for_update_variables && *arg_header_for_update_variables) {
        for (const auto& [key, value] : (*arg_header_for_update_variables)->m_flags) {
            if (!header->m_flags.contains(key)) continue;
            const auto dest = typed_object<::cw::data::Flag>(header->m_flags[key]);
            const auto src = typed_object<::cw::data::Flag>(value);
            if (dest && src) { dest->set(src->m_value); dest->write_value(); }
        }
        for (const auto& [key, value] : (*arg_header_for_update_variables)->m_steps) {
            if (!header->m_steps.contains(key)) continue;
            const auto dest = typed_object<::cw::data::Step>(header->m_steps[key]);
            const auto src = typed_object<::cw::data::Step>(value);
            if (dest && src) { dest->set(src->m_value); dest->write_value(); }
        }
        for (const auto& [key, value] : (*arg_header_for_update_variables)->m_variants) {
            if (!header->m_variants.contains(key)) continue;
            const auto dest = typed_object<::cw::data::Variant>(header->m_variants[key]);
            const auto src = typed_object<::cw::data::Variant>(value);
            if (dest && src) { dest->set(src->m_value, true); dest->write_value(); }
        }
    }
    (void)arg_ignore_list_for_update_variables;
    if (!arg_copymaterialfrom.empty()) {
        // GetContent のシナリオカード取得時は画像・素材を宿側へ複製し、
        // CardHeaderのImageInfoも複製先へ組み直す。 Python
        // 版と同様にUnicodeでパス要素を連結し、 UTF-8のカード名をWindows
        // のANSIコードページとして解釈する狭い文字列の追加を使わない。
        const auto base = std::filesystem::u8path(::cw::g_cwpy->m_yadodir) /
                          "Material" / std::filesystem::u8path(header->m_type) /
                          std::filesystem::u8path(name.empty() ? S("noname") : name);
        const S destination = ::cw::util::dupcheck_plus(::cw::lib::encoding::path_to_utf8(base), false, false);
        const bool scaled = root->getbool(".", "scaledimage", false);
        ::cw::g_cwpy->copy_materials(
            V(std::static_pointer_cast<::cw::lib::Object>(root)), destination, true,
            arg_copymaterialfrom, std::nullopt, std::nullopt, false, std::nullopt,
            false, scaled);
        const auto infos = ::cw::image::get_imageinfos(root->find_exists("Property"), false);
        auto sequence = std::make_shared<::cw::data::VariantSequence>();
        for (const auto& info : infos) if (info)
            sequence->m_values.emplace_back(V(std::static_pointer_cast<::cw::lib::Object>(info)));
        header->m_imgpaths = V(std::static_pointer_cast<::cw::lib::Object>(sequence));
    }
    const S target_marker = value_string(arg_target);
    S targettype;
    if (target_marker == "STOREHOUSE") targettype = "STOREHOUSE";
    else if (target_marker == "BACKPACK" || value_sequence(arg_target)) targettype = "BACKPACK";
    else targettype = owner ? "PLAYERCARD" : "BACKPACK";
    std::optional<V> trade_target;
    if (owner) trade_target = arg_target;
    ::cw::g_cwpy->trade(targettype, trade_target, header, true, nullptr, arg_toindex, arg_insertorder,
                         false, false, arg_party, arg_from_getcontent, false, false, arg_update_image, 1);
    (void)name;
}

bool is_addablecoupon(::cw::lib::String arg_coupon)
{
    if (arg_coupon.empty()) return false;
    if (!arg_coupon.starts_with("＠")) return true;
    const auto iface = event_interface();
    const auto sdata = system_data();
    if (iface && ::cw::g_cwpy && sdata && arg_coupon == "＠効果対象") {
        std::optional<S> cardversion;
        if (const auto inusecard = iface->get_inusecard(); iface->m_in_inusecardevent && inusecard && *inusecard)
            cardversion = value_string((*inusecard)->m_wsnversion);
        if (sdata->is_wsnversion("2", cardversion)) {
            const auto effectevent = iface->get_effectevent();
            return effectevent && *effectevent;
        }
    }
    return false;
}

::cw::lib::Value is_xmltype_check(::cw::lib::Value arg_e)
{
    const auto element = element_of(arg_e); if (!element) return V(false);
    const S type = element->get("xmltype", ""); return V(type == "CheckStep" || type == "CheckFlag" || type == "CheckVariant");
}

::cw::lib::String _replace_variant_placeholders(::cw::lib::String arg_text)
{
    const std::regex pattern("@([^@]+)@");
    S out; std::size_t last = 0;
    for (std::sregex_iterator it(arg_text.begin(), arg_text.end(), pattern), end; it != end; ++it) {
        out += arg_text.substr(last, static_cast<std::size_t>(it->position()) - last);
        S replacement = it->str();
        // Python版と同様、各プレースホルダーを個別の例外範
        // 囲で解決する。 失敗したものはそのまま残して次へ進み
        // 、何もないテキストでは実行イベントを問い合わせない。
        try {
            const auto interface = event_interface();
            const auto event = interface ? interface->get_nowrunningevent() : std::nullopt;
            if (event && *event) {
                const auto sdata = system_data();
                if (sdata) {
                    const auto variant = sdata->find_variant((*it)[1].str(), false, event);
                    if (variant && *variant)
                        replacement = ::cw::data::Variant::value_to_str((*variant)->m_value, false);
                }
            }
        } catch (...) {}
        out += replacement;
        last = static_cast<std::size_t>(it->position() + it->length());
    }
    out += arg_text.substr(last);
    return out;
}

void _copy_element_context(std::shared_ptr<::cw::data::CWPyElement> arg_src, std::shared_ptr<::cw::data::CWPyElement> arg_dst)
{
    if (!arg_src || !arg_dst) return;
    arg_dst->m_cwxparent = arg_src->m_cwxparent;
    arg_dst->m_nextelements = arg_src->m_nextelements;
    if (arg_src->m_needcheck.has_value()) arg_dst->m_needcheck = arg_src->m_needcheck;
    // C++版は生成コンテントの診断用に元のXMLパスも保持する。 Python
    // 版はこのネイティブ診断情報を複製せず、検査フラグを表すものでもない。
    arg_dst->m_cwxpath = arg_src->m_cwxpath;
    if (const auto contents = arg_src->find("Contents"); contents && *contents) arg_dst->append(::cw::data::copydata(*contents, true));
}

::cw::lib::Value clean_xmltext(::cw::lib::Value arg_xmltext)
{
    namespace enc = ::cw::lib::encoding;
    if (const auto* bytes = std::get_if<::cw::lib::Bytes>(&arg_xmltext.storage()))
        arg_xmltext = enc::decode_utf8(*bytes, enc::ErrorPolicy::Replace, false);
    if (!arg_xmltext.as_bool()) return arg_xmltext;
    const auto* source = std::get_if<S>(&arg_xmltext.storage());
    if (!source) throw std::invalid_argument("clean_xmltext requires a string or bytes");
    const auto encoded = enc::utf8_to_utf32(*source, enc::ByteOrder::Little);
    std::vector<std::uint32_t> points;
    points.reserve(encoded.size()/4);
    for (std::size_t i=0;i<encoded.size();i+=4)
        points.push_back(encoded[i] | (std::uint32_t(encoded[i+1])<<8) |
            (std::uint32_t(encoded[i+2])<<16) | (std::uint32_t(encoded[i+3])<<24));
    const auto first = std::find_if(points.begin(),points.end(),[](auto cp){return cp!=0xfeff;});
    points.erase(points.begin(),first);
    std::size_t start=0,end=points.size();
    const auto escaped = [&](std::size_t i) {
        return points[i]=='\\' && (points[i+1]=='n'||points[i+1]=='r'||points[i+1]=='t');
    };
    const auto whitespace = [](auto cp){return cp=='\n'||cp=='\r'||cp==' '||cp=='\t';};
    while(start<end) {
        if(start+1<end && escaped(start))start+=2;
        else if(whitespace(points[start]))++start;
        else break;
    }
    while(end>start) {
        if(end-start>=2 && escaped(end-2))end-=2;
        else if(whitespace(points[end-1]))--end;
        else break;
    }
    // Python版の順序を維持し、先に文字位置を計算してから
    // NULを除き、 短くなった文字列へ元の位置を適用する。
    points.erase(std::remove(points.begin(),points.end(),0),points.end());
    start=std::min(start,points.size());end=std::min(end,points.size());
    ::cw::lib::Bytes result;
    for(std::size_t i=start;i<end;++i)
        for(unsigned shift=0;shift<32;shift+=8)result.push_back(static_cast<std::uint8_t>(points[i]>>shift));
    return V(enc::utf32_to_utf8(result,enc::ByteOrder::Little,enc::ErrorPolicy::Strict,false));
}

std::optional<::cw::lib::String> InputDialogContent_action_show_dialog(
    ::cw::lib::String arg_title, ::cw::lib::String arg_message,
    ::cw::lib::String arg_initial, std::tuple<I, I> arg_size)
{
    auto dialog = std::make_shared<::cw::dialog::etc::InputDialog>();
    dialog->initialize(nullptr, std::move(arg_title), std::move(arg_message), std::move(arg_initial), arg_size);
    (void)::cw::dialog::show_common_modal_presentation(dialog);
    if (dialog->m_modal_result == 1) return dialog->get_value();
    return std::nullopt;
}

std::shared_ptr<::cw::content::EventContentBase> get_content(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    if (!arg_data) throw std::invalid_argument("NoContent: null");
    const auto classname = arg_data->m_tag + arg_data->get("type", "") + "Content";
    // Python版と同様、既存のインスタンスは生成エラーの処理範囲外で返す。
    if (arg_data->m_content && *arg_data->m_content)
        return get_typed_content(arg_data, classname);
    try {
        return get_typed_content(arg_data, classname);
    } catch (...) {
        ::cw::util::print_ex(std::nullopt);
        std::cout << "NoContent:  " << classname << std::endl;
        throw std::invalid_argument("NoContent: " + classname);
    }
}

static std::shared_ptr<EventContentBase> get_typed_content(
    std::shared_ptr<::cw::data::CWPyElement> arg_data, const ::cw::lib::String& classname)
{
    if (!arg_data) throw std::invalid_argument("NoContent: null");
    const auto retain = [](const std::shared_ptr<EventContentBase>& content) {
        struct ExecutionLifetime {
            std::shared_ptr<::cw::data::CWPyElement> data;
            std::shared_ptr<EventContentBase> content;
        };
        auto data = content->m_data_typed.lock();
        if (!data) throw std::logic_error("cached content XML lifetime has ended");
        auto lifetime = std::make_shared<ExecutionLifetime>(ExecutionLifetime{std::move(data), content});
        return std::shared_ptr<EventContentBase>(std::move(lifetime), content.get());
    };
    if (arg_data->m_content && *arg_data->m_content) return retain(*arg_data->m_content);
    const auto install = [&](const std::shared_ptr<EventContentBase>& content) {
        arg_data->m_content = content;
        content->m_data_typed.weaken();
        return retain(content);
    };
#define CW_MAKE_CONTENT(Type) \
    if (classname == #Type) { auto content = std::make_shared<Type>(); content->initialize(arg_data); return install(content); }
    // ネイティブの生成処理は具体的なXML命令だけを登録し、 BranchContent/GetContent/LoseContent/TalkContent
    // やEventContentBaseの補助基
    // 底は除く。 Pythonのglobals
    // 検索では基底も作れるが、具体的な操作がな
    // い補助コンテントを実行可能にしないため拒
    // 否する。 具体的な分岐・判定は後に続く。
    CW_MAKE_CONTENT(BranchSkillContent)
    CW_MAKE_CONTENT(BranchItemContent)
    CW_MAKE_CONTENT(BranchBeastContent)
    CW_MAKE_CONTENT(BranchCastContent)
    CW_MAKE_CONTENT(BranchAbilityContent)
    CW_MAKE_CONTENT(BranchInfoContent)
    CW_MAKE_CONTENT(BranchIsBattleContent)
    CW_MAKE_CONTENT(BranchBattleContent)
    CW_MAKE_CONTENT(BranchAreaContent)
    CW_MAKE_CONTENT(BranchStatusContent)
    CW_MAKE_CONTENT(BranchCouponContent)
    CW_MAKE_CONTENT(BranchFlagContent)
    CW_MAKE_CONTENT(BranchFlagValueContent)
    CW_MAKE_CONTENT(BranchKeyCodeContent)
    CW_MAKE_CONTENT(BranchLevelContent)
    CW_MAKE_CONTENT(BranchMoneyContent)
    CW_MAKE_CONTENT(BranchMultiCouponContent)
    CW_MAKE_CONTENT(BranchMultiRandomContent)
    CW_MAKE_CONTENT(BranchMultiStepContent)
    CW_MAKE_CONTENT(BranchRandomContent)
    CW_MAKE_CONTENT(BranchRandomSelectContent)
    CW_MAKE_CONTENT(BranchRoundContent)
    CW_MAKE_CONTENT(BranchSelectContent)
    CW_MAKE_CONTENT(BranchStepContent)
    CW_MAKE_CONTENT(BranchVariantContent)
    CW_MAKE_CONTENT(BranchGossipContent)
    CW_MAKE_CONTENT(BranchCompleteStampContent)
    CW_MAKE_CONTENT(BranchPartyNumberContent)
    CW_MAKE_CONTENT(ChangeBgImageContent)
    CW_MAKE_CONTENT(ChangeAreaContent)
    CW_MAKE_CONTENT(ChangeEnvironmentContent)
    CW_MAKE_CONTENT(CheckFlagContent)
    CW_MAKE_CONTENT(CheckStepContent)
    CW_MAKE_CONTENT(CheckVariantContent)
    CW_MAKE_CONTENT(EffectContent)
    CW_MAKE_CONTENT(EffectBreakContent)
    CW_MAKE_CONTENT(ElapseTimeContent)
    CW_MAKE_CONTENT(EndContent)
    CW_MAKE_CONTENT(EndBadEndContent)
    CW_MAKE_CONTENT(GetSkillContent)
    CW_MAKE_CONTENT(GetItemContent)
    CW_MAKE_CONTENT(GetBeastContent)
    CW_MAKE_CONTENT(GetCastContent)
    CW_MAKE_CONTENT(GetInfoContent)
    CW_MAKE_CONTENT(GetMoneyContent)
    CW_MAKE_CONTENT(GetCompleteStampContent)
    CW_MAKE_CONTENT(GetGossipContent)
    CW_MAKE_CONTENT(GetCouponContent)
    CW_MAKE_CONTENT(HidePartyContent)
    CW_MAKE_CONTENT(LinkStartContent)
    CW_MAKE_CONTENT(LinkPackageContent)
    CW_MAKE_CONTENT(ActionLinkStartContent)
    CW_MAKE_CONTENT(LoseBgImageContent)
    CW_MAKE_CONTENT(LoseSkillContent)
    CW_MAKE_CONTENT(LoseItemContent)
    CW_MAKE_CONTENT(LoseBeastContent)
    CW_MAKE_CONTENT(LoseCastContent)
    CW_MAKE_CONTENT(LoseInfoContent)
    CW_MAKE_CONTENT(LoseMoneyContent)
    CW_MAKE_CONTENT(LoseCompleteStampContent)
    CW_MAKE_CONTENT(LoseGossipContent)
    CW_MAKE_CONTENT(LoseCouponContent)
    CW_MAKE_CONTENT(PlayBgmContent)
    CW_MAKE_CONTENT(PlaySoundContent)
    CW_MAKE_CONTENT(PostEventContent)
    CW_MAKE_CONTENT(RedisplayContent)
    CW_MAKE_CONTENT(ReplaceBgImageContent)
    CW_MAKE_CONTENT(ReverseFlagContent)
    CW_MAKE_CONTENT(SetFlagContent)
    CW_MAKE_CONTENT(SetStepContent)
    CW_MAKE_CONTENT(SetStepUpContent)
    CW_MAKE_CONTENT(SetStepDownContent)
    CW_MAKE_CONTENT(SetVariantContent)
    CW_MAKE_CONTENT(ShowPartyContent)
    CW_MAKE_CONTENT(StartBattleContent)
    CW_MAKE_CONTENT(StartContent)
    CW_MAKE_CONTENT(SubstituteFlagContent)
    CW_MAKE_CONTENT(SubstituteStepContent)
    CW_MAKE_CONTENT(TalkMessageContent)
    CW_MAKE_CONTENT(TalkDialogContent)
    CW_MAKE_CONTENT(WaitContent)
    CW_MAKE_CONTENT(EndActionEventContent)
    CW_MAKE_CONTENT(SetActionEventContent)
    CW_MAKE_CONTENT(CallPackageContent)
    CW_MAKE_CONTENT(CallStartContent)
    CW_MAKE_CONTENT(BranchStepValueContent)
    CW_MAKE_CONTENT(XmlContentCustomContent)
    if (classname == "InputDialogContent") { auto content = std::make_shared<InputDialogContent>(); content->initialize(arg_data, arg_data->get("title", ""), arg_data->gettext("Message", "")); return install(content); }
    if (classname == "ChangeEnemyContent") { auto content = std::make_shared<ChangeEnemyContent>(); content->initialize(arg_data, true); return install(content); }
    if (classname == "MoveBgImageContent") { auto content = std::make_shared<MoveBgImageContent>(); content->initialize(arg_data, true); return install(content); }
    if (classname == "MoveCardContent") { auto content = std::make_shared<MoveCardContent>(); content->initialize(arg_data, true); return install(content); }
    if (classname == "MultipleMoveLoseBgImageContent") { auto content = std::make_shared<MultipleMoveLoseBgImageContent>(); content->initialize(arg_data, true); return install(content); }
#undef CW_MAKE_CONTENT
    throw std::invalid_argument("NoContent: " + classname);
}

void main()
{
    // Pythonモジュールの__main__入口はpassのみ。ゲーム起動処理はアプリ側に属する。
}

}

