#include "cw/advlog.hpp"
#include "cw/cw.hpp"
#include "cw/character.hpp"
#include "cw/data.hpp"
#include "cw/event.hpp"
#include "cw/header.hpp"
#include "cw/setting.hpp"
#include "cw/sprite/card.hpp"
#include "cw/sprite/message.hpp"
#include "cw/thread.hpp"
#include "cw/util.hpp"
#include "cw/lib/managed_userdata.hpp"
#include "platform/storage/storage_mount.hpp"

#include <array>
#include <chrono>
#include <ctime>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <thread>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace cw::advlog {

// Python版が現在ログへ出力しないカード効果以外の通知（カード配置・入手喪失・ NPC・BGM・背景など
// ）は、同じ仕様を保つためここでも意図的に記録しない。 MessageWindowの文字列化は描画状態ではな
// く message::get_messagelogtext へ接続する。 Loggerの既定実装は、std::condition_variable
// を使う先入れ先出しのキューと共通のファイルストリームとする。

namespace {
using S = ::cw::lib::String;
using I = ::cw::lib::Integer;
using V = ::cw::lib::Value;

S value_string(const V& value)
{
    if (const auto* s = std::get_if<S>(&value.storage())) return *s;
    if (const auto* i = std::get_if<I>(&value.storage())) return std::to_string(*i);
    if (const auto* r = std::get_if<::cw::lib::Real>(&value.storage())) {
        std::ostringstream os;
        os << *r;
        return os.str();
    }
    if (const auto* b = std::get_if<bool>(&value.storage())) return *b ? "True" : "False";
    return {};
}

S repeat_char(I n, char c)
{
    return n > 0 ? S(static_cast<std::size_t>(n), c) : S{};
}

S rjustify(S text, I width, char fill)
{
    return ::cw::util::rjustify(std::move(text), width, S(1, fill));
}

S ljustify(S text, I width, char fill)
{
    return ::cw::util::ljustify(std::move(text), width, S(1, fill));
}

double process_time()
{
#if defined(_WIN32)
    FILETIME create{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &create, &exit, &kernel, &user)) return 0.0;
    const auto filetime_seconds = [](const FILETIME& time) {
        ULARGE_INTEGER value{};
        value.LowPart = time.dwLowDateTime;
        value.HighPart = time.dwHighDateTime;
        return static_cast<double>(value.QuadPart) / 10000000.0;
    };
    return filetime_seconds(kernel) + filetime_seconds(user);
#else
    return static_cast<double>(std::clock()) / static_cast<double>(CLOCKS_PER_SEC);
#endif
}

std::optional<S> use_card_target_name(const V& arg_targets)
{
    // Python版と同様、要素が一つのリストは一度だけ取り出す。空の名前は None とは区別する。
    V target = arg_targets;
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&target.storage()); object && *object) {
        if (const auto sequence = std::dynamic_pointer_cast<::cw::data::VariantSequence>(*object)) {
            if (sequence->m_values.size() != 1) return std::nullopt;
            target = sequence->m_values.front();
        }
    }
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&target.storage()); object && *object) {
        if (const auto card = std::dynamic_pointer_cast<::cw::sprite::card::CWPyCard>(*object))
            return card->get_showingname();
    }
    return std::nullopt;
}

std::shared_ptr<::cw::data::ScenarioData> scenario_data()
{
    if (!::cw::g_cwpy) return {};
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_sdata.storage());
    return object && *object ? std::dynamic_pointer_cast<::cw::data::ScenarioData>(*object) : nullptr;
}

std::shared_ptr<::cw::data::Party> current_party()
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_ydata || !*::cw::g_cwpy->m_ydata) return {};
    const auto& party = (*::cw::g_cwpy->m_ydata)->m_party;
    return party && *party ? *party : nullptr;
}

S name_of(const std::shared_ptr<::cw::character::Character>& target)
{
    return target ? target->get_showingname() : S{};
}

S card_name(const std::shared_ptr<::cw::header::CardHeader>& header)
{
    return header ? header->get_showingname() : S{};
}

V object_value(const std::shared_ptr<::cw::lib::Object>& object)
{
    return object ? V(object) : V{};
}

S make_log_path(const S& format)
{
    // Python: format_title(setting.playlogformat, get_titledic(..., for_fname=True)[1])
    if (::cw::g_cwpy)
        return ::cw::util::format_title(format, ::cw::g_cwpy->get_titledic_filename_values(true), false);
    // ゲームスレッドが動いていない単体テスト・自己検査用の、C++版独自の代替処理。
    return format.empty() ? S{"playlog.log"} : format;
}

S safe_managed_log_fallback()
{
    // 組み込みの PlayLogFormat にできるだけ合わせ、管理対象だけの上
    // 書きが不正でも、 実行ごとにセッション内へ有用なログを一つ出せるようにする。
    return make_log_path(
        "PlayLog/%yado%/%party%_%year%%month%%day%_%hour%%minute%%second%_%scenario%.txt");
}

bool event_in_card_effect()
{
    return ::cw::g_cwpy && ::cw::g_cwpy->m_event &&
           (::cw::g_cwpy->m_event->m_in_cardeffectmotion || ::cw::g_cwpy->m_event->m_in_inusecardevent);
}

bool event_in_effect_motion()
{
    return ::cw::g_cwpy && ::cw::g_cwpy->m_event && ::cw::g_cwpy->m_event->m_in_cardeffectmotion;
}

} // namespace

AdventurerLogger::~AdventurerLogger()
{
    // ネイティブの所有者は、ロガーを手放す前にStopを送る必要がある。 メソッドに
    // 結び付いたワーカーに相当する処理が run() 完了までLoggerを保持する
    // ため、 外側の shared_ptr を解放するだけでは終了要求にならない。
    force_quit();
}

void AdventurerLogger::initialize()
{
    // 再初期化でも破棄と同じ明示的な終了境界を使う。 そうしないと
    // 、保持された以前のワーカーがキューを永久に待つ場合がある。
    force_quit();
    m__logger.reset();
    m__enable = false;
    m__last_logtype = g_VOID;
    // Python版のコンストラクターとは異なり、ネイティブ版
    // はこのインスタンスが再開先を保持する。 再初期化でその先
    // を消し、後のシナリオが古いパスを再利用しないようにする。
    m_logfilepath.clear();
}

void AdventurerLogger::start_scenario()
{
    const auto sdata = scenario_data();
    const auto party = current_party();
    if (!sdata || !party) throw std::logic_error("AdventurerLogger.start_scenario: ydata/party is missing");

    resume_scenario({});
    const auto author = value_string(sdata->m_author);
    S heading = "== シナリオ [ " + value_string(sdata->m_name) + " ]" +
                (author.empty() ? S{} : " (" + author + ")") + " 開始 ==";
    std::vector<S> lines{rjustify(heading, ::cw::g_LOG_SEPARATOR_LEN_LONG, '=')};
    lines.emplace_back();
    lines.push_back("[ " + party->get_showingname() + " ] - " +
                    ((*::cw::g_cwpy->m_ydata)->get_showingname()));
    lines.emplace_back();
    const bool show_personal = ::cw::g_cwpy && ::cw::g_cwpy->m_setting &&
                               ::cw::g_cwpy->m_setting->m_show_personal_cards;
    const std::array<std::pair<I, const char*>, 4> pockets{{
        {::cw::g_POCKET_SKILL, "Skill"}, {::cw::g_POCKET_ITEM, "Item"},
        {::cw::g_POCKET_BEAST, "Beast"}, {::cw::g_POCKET_PERSONAL, "Personal"}}};
    for (const auto& pcard : ::cw::g_cwpy->get_pcards(V{})) {
        if (!pcard) continue;
        lines.push_back(pcard->get_showingname());
        const I limit = pcard->get_limitlevel();
        lines.push_back(limit != pcard->m_level ? "  Level " + std::to_string(pcard->m_level) +
                                                       " / " + std::to_string(limit) :
                                                   "  Level " + std::to_string(pcard->m_level));
        const auto& cardpocket = pcard->m_cardpocket_typed;
        for (const auto& [pocket, pname] : pockets) {
            if (pocket == ::cw::g_POCKET_PERSONAL && !show_personal) continue;
            std::vector<std::shared_ptr<::cw::header::CardHeader>> cards;
            if (pocket == ::cw::g_POCKET_SKILL) cards = std::get<0>(cardpocket);
            else if (pocket == ::cw::g_POCKET_ITEM) cards = std::get<1>(cardpocket);
            else if (pocket == ::cw::g_POCKET_BEAST) cards = std::get<2>(cardpocket);
            else cards = pcard->m_personal_pocket;
            if (cards.empty()) continue;
            S names;
            bool first_name = true;
            for (const auto& card : cards) {
                // Pythonの join と同様、カード名が空でも項目間の区切りを入れる。
                if (!first_name) names += ", ";
                first_name = false;
                names += card_name(card);
            }
            const I pad = show_personal ? 8 : 5;
            S label = pname;
            if (static_cast<I>(label.size()) < pad) label += repeat_char(pad - static_cast<I>(label.size()), ' ');
            lines.push_back("  " + label + ": " + names);
        }
    }
    lines.emplace_back();
    lines.push_back(repeat_char(::cw::g_LOG_SEPARATOR_LEN_MIDDLE, '='));
    put_text(g_INITIAL, [&]() { S out; for (std::size_t i = 0; i < lines.size(); ++i) { if (i) out += '\n'; out += lines[i]; } return out; }());
}

void AdventurerLogger::resume_scenario(::cw::lib::String arg_logfilepath)
{
    end_scenario(false, false);
    if (arg_logfilepath.empty()) {
        const S format = ::cw::g_cwpy && ::cw::g_cwpy->m_setting ? ::cw::g_cwpy->m_setting->m_playlogformat : S{};
        arg_logfilepath = make_log_path(format);
    }
    const S fallback = safe_managed_log_fallback();
    m_logfilepath = ::cw::lib::managed_userdata::select_output_relative_path(
        arg_logfilepath, fallback);
    m__enable = ::cw::g_cwpy && ::cw::g_cwpy->m_setting ? ::cw::g_cwpy->m_setting->m_write_playlog : false;
    auto logger = std::make_shared<Logger>();
    logger->initialize(::cw::lib::managed_userdata::resolve_output_path(
                           m_logfilepath, fallback),
                       m__enable);
    logger->start_worker();
    m__logger = std::move(logger);
}

void AdventurerLogger::enable(bool arg_enable)
{
    // 下のネイティブの終了・再開では、以前のワーカーの処理完了と終了を待ってから置き換える。 Python
    // 版は切り替え値・Noneをキューへ入れ、メソッドに結び付いたワーカーの完了に任せる。
    if (m__enable == arg_enable) return;
    m__enable = arg_enable;
    if (m__logger && *m__logger) (*m__logger)->set_enable(arg_enable);
    if (::cw::g_cwpy && ::cw::g_cwpy->is_playingscenario()) {
        if (arg_enable) resume_scenario({});
        else end_scenario(false, false);
    }
}

void AdventurerLogger::force_quit()
{
    // ネイティブ版の意図的な停止境界として、外部の呼び出し元は保留記録の処理とワーカー
    // 終了を待ってから保存領域を解放する。 Python版はNoneをキューへ入れる
    // だけ。 ワーカー上のコールバックは、自身の終了を待たずStopだけを追加する。
    if (m__logger && *m__logger) (*m__logger)->stop_worker();
}

void AdventurerLogger::end_scenario(bool arg_end, bool arg_completestamp)
{
    if (m__logger && *m__logger) {
        if (arg_end) {
            const S s = arg_completestamp ? "== 済印をつけてシナリオを終了 ==" :
                                            "== 済印をつけずにシナリオを終了 ==";
            put_text(g_SYSTEM, ljustify(s, ::cw::g_LOG_SEPARATOR_LEN_LONG, '='));
        } else {
            _put_logtype(g_VOID);
        }
        // force_quit と同じく、シナリオの所有者を手放す前に、ネイティブの保
        // 留処理と終了を待つ。 ワーカーは run() を通じて自身の寿命を保持する。
        (*m__logger)->stop_worker();
    }
    m__logger.reset();
    m__last_logtype = g_VOID;
}

void AdventurerLogger::gameover()
{
    put_text(g_VOID, ljustify("== ゲームオーバー ==", ::cw::g_LOG_SEPARATOR_LEN_LONG, '='));
    end_scenario(false, false);
}

void AdventurerLogger::f9()
{
    put_text(g_VOID, ljustify("== 緊急避難 ==", ::cw::g_LOG_SEPARATOR_LEN_LONG, '='));
    end_scenario(false, false);
}

void AdventurerLogger::_put_logtype(::cw::lib::Integer arg_logtype)
{
    // ネイティブ版では、ロガーがなければ enqueue_text は出力しな
    // いが、このメソッドは最後の種類を更新する。 Python版は、前の種類が
    // VOID以外で区切りが必要な場合、ロガーの存在をassertで要求する。
    if (!m__enable) { m__last_logtype = g_VOID; return; }
    if (m__last_logtype != g_VOID) {
        if (m__last_logtype == g_MESSAGE && arg_logtype != g_MESSAGE) {
            enqueue_text(repeat_char(::cw::g_LOG_SEPARATOR_LEN_SHORT, '-'));
            if (arg_logtype != g_VOID && arg_logtype != g_SEPARATOR) enqueue_text({});
        } else if ((m__last_logtype != arg_logtype || arg_logtype == g_SYSTEM) &&
                   arg_logtype != g_VOID && arg_logtype != g_SEPARATOR) {
            enqueue_text({});
        }
    }
    m__last_logtype = arg_logtype;
}

void AdventurerLogger::_put(::cw::lib::Integer arg_logtype, ::cw::lib::Value arg_data, std::optional<::cw::lib::Callback> arg_func, bool arg_usecard)
{
    if ((arg_logtype == g_MOTION || arg_logtype == g_MOTION_IN_BATTLE) && !arg_usecard && !event_in_card_effect()) return;
    if (!m__logger || !*m__logger) return;
    _put_logtype(arg_logtype);
    if (arg_data.is_none() && !arg_func) {
        (*m__logger)->enqueue_callback({}, [](const ::cw::lib::Arguments&, const ::cw::lib::KeywordArguments&) { return V{}; });
    } else if (arg_func) {
        (*m__logger)->enqueue_callback(std::move(arg_data), std::move(*arg_func));
    } else {
        // 型付きのネイティブ変換として、整形処理がなければ Value を文
        // 字列化してから不変のテキストをキューへ入れる。 Python版は値
        // をそのまま渡し、ワーカーがstrを前提とする。 上のコールバック
        // 経由の値は、コールバックが消費するまでValueとして保持する。
        enqueue_text(value_string(arg_data));
    }
    m__last_logtype = arg_logtype;
}

void AdventurerLogger::enqueue_text(::cw::lib::String arg_text)
{
    if (m__logger && *m__logger) (*m__logger)->enqueue(std::move(arg_text));
}

void AdventurerLogger::put_text(::cw::lib::Integer arg_logtype, ::cw::lib::String arg_text, bool arg_usecard)
{
    if ((arg_logtype == g_MOTION || arg_logtype == g_MOTION_IN_BATTLE) && !arg_usecard && !event_in_card_effect()) return;
    if (!m__logger || !*m__logger) return;
    _put_logtype(arg_logtype);
    enqueue_text(std::move(arg_text));
    m__last_logtype = arg_logtype;
}

void AdventurerLogger::show_message(std::shared_ptr<::cw::sprite::message::MessageWindow> arg_mwin)
{
    if (!arg_mwin) return;
    // Python版のワーカーのコールバックはGILで保護される。
    // C++版は、変更可能なMessageWindowをゲームスレ
    // ッド上で不変の文字列へ変換してから、ロガーのキューへ入れる。
    const auto text = value_string(::cw::sprite::message::get_messagelogtext(
        {object_value(arg_mwin)}, false));
    _put(g_MESSAGE, ::cw::lib::Value(text), std::nullopt);
}

void AdventurerLogger::separator()
{
    put_text(g_VOID, {});
}

void AdventurerLogger::start_timeelapse()
{
    if (m__enable && m__last_logtype == g_ELAPSE_TIME) put_text(g_ELAPSE_TIME, {});
}

void AdventurerLogger::click_menucard(std::shared_ptr<::cw::sprite::card::MenuCard> arg_mcard)
{
    if (!arg_mcard) return;
    const S name = arg_mcard->get_showingname();
    const S s = name.empty() ? "==" : "==< " + name + " >==";
    put_text(g_SYSTEM, rjustify(s, ::cw::g_LOG_SEPARATOR_LEN_MIDDLE, '='));
}

void AdventurerLogger::rename_party(::cw::lib::String arg_newname, ::cw::lib::String arg_oldname)
{
    (void)arg_oldname;
    put_text(g_SYSTEM, "パーティ名を[ " + arg_newname + " ]に変更");
}

void AdventurerLogger::start_battle(std::shared_ptr<::cw::battle::BattleEngine> arg_battle)
{
    (void)arg_battle;
    put_text(g_SYSTEM, rjustify("==[ バトル開始 ]==", ::cw::g_LOG_SEPARATOR_LEN_LONG, '='));
}

void AdventurerLogger::end_battle(std::shared_ptr<::cw::battle::BattleEngine> arg_battle)
{
    (void)arg_battle;
    put_text(g_SYSTEM, rjustify("==[ バトル終了 ]==", ::cw::g_LOG_SEPARATOR_LEN_LONG, '='));
}

void AdventurerLogger::start_runaway()
{
    put_text(g_SYSTEM, "<<<< 逃走 >>>>");
}

void AdventurerLogger::runaway(bool arg_success)
{
    const auto party = current_party();
    if (!party) throw std::logic_error("AdventurerLogger.runaway: party is missing");
    put_text(g_SYSTEM, party->get_showingname() + (arg_success ? "は逃走した。" : "は逃走を試みたが、失敗した。"));
}

void AdventurerLogger::start_round(::cw::lib::Integer arg_roundval)
{
    put_text(g_SYSTEM, "<<<< ラウンド " + std::to_string(arg_roundval) + " >>>>");
}

::cw::lib::Integer AdventurerLogger::_motion_type()
{
    // 初期化・終了時の安全策として、Python版が必須とする cwpy
    // を参照せず、 実行環境がなければ MOTION を使う。
    return ::cw::g_cwpy && ::cw::g_cwpy->is_battlestatus() ? g_MOTION_IN_BATTLE : g_MOTION;
}

void AdventurerLogger::use_card(std::shared_ptr<::cw::character::Character> arg_ccard, std::shared_ptr<::cw::header::CardHeader> arg_header, ::cw::lib::Value arg_targets)
{
    if (!arg_ccard || !arg_header) return;
    const auto targetname = use_card_target_name(arg_targets);
    const bool battle = ::cw::g_cwpy && ::cw::g_cwpy->is_battlestatus();
    const bool beast = arg_header->m_type == "BeastCard";
    const S text = AdventurerLogger_use_card_use_card({arg_ccard->get_showingname(),
        arg_header->get_showingname(), beast, targetname, arg_header->m_target, battle});
    put_text(battle ? _motion_type() : g_SYSTEM, text, true);
}

::cw::lib::String AdventurerLogger::wrap_effectmotion(::cw::lib::String arg_s, bool arg_in_cardeffectmotion)
{
    return arg_in_cardeffectmotion ? "  " + arg_s : arg_s;
}

bool AdventurerLogger::in_cardeffectmotion()
{
    // 同じ寿命管理の保護として、実行環境・イベントがなければfalseを返す。
    // 初期化済みでは、Python版の戦闘中かつカード効果という条件を維持する。
    return ::cw::g_cwpy && ::cw::g_cwpy->is_battlestatus() && event_in_effect_motion();
}

void AdventurerLogger::avoid(std::shared_ptr<::cw::character::Character> arg_target)
{
    if (arg_target) put_text(_motion_type(), wrap_effectmotion(arg_target->get_showingname() + "は回避した。", in_cardeffectmotion()));
}

void AdventurerLogger::noeffect(std::shared_ptr<::cw::character::Character> arg_target)
{
    if (arg_target) put_text(_motion_type(), wrap_effectmotion(arg_target->get_showingname() + "は抵抗した。", in_cardeffectmotion()));
}

void AdventurerLogger::effect_failed(::cw::lib::Value arg_target, bool arg_ismenucard)
{
    // 型付きのネイティブAPIの安全策として、未対応・空の対象は記録しない。 Python
    // 版の get_showingname 属性取得の失敗は再現しない。
    S name;
    bool valid_target = false;
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&arg_target.storage()); object && *object) {
        if (const auto c = std::dynamic_pointer_cast<::cw::character::Character>(*object)) { name = c->get_showingname(); valid_target = true; }
        else if (const auto m = std::dynamic_pointer_cast<::cw::sprite::card::MenuCard>(*object)) { name = m->get_showingname(); valid_target = true; }
    }
    if (valid_target) put_text(arg_ismenucard ? g_SYSTEM : _motion_type(), wrap_effectmotion(name + "には効果がなかった。", in_cardeffectmotion()));
}

::cw::lib::Integer AdventurerLogger::_get_lifestatus(::cw::lib::Integer arg_life, ::cw::lib::Integer arg_maxlife)
{
    if (arg_life <= 0) return 0;
    if (::cw::character::Character::calc_heavyinjured(arg_life, arg_maxlife)) return 1;
    if (::cw::character::Character::calc_injured(arg_life, arg_maxlife)) return 2;
    return 3;
}

void AdventurerLogger::heal_motion(std::shared_ptr<::cw::character::Character> arg_target, ::cw::lib::Integer arg_value, ::cw::lib::Integer arg_newlife, ::cw::lib::Integer arg_oldlife)
{
    if (!arg_target || arg_newlife == arg_oldlife) return;
    const I oldstatus = _get_lifestatus(arg_oldlife, arg_target->m_maxlife), newstatus = _get_lifestatus(arg_newlife, arg_target->m_maxlife);
    const S vs = arg_value < 20 ? "小回復" : arg_value < 40 ? "中回復" : "大回復";
    S s = arg_target->get_showingname();
    if (newstatus == oldstatus) s += "は" + vs + "。";
    else { const char* state[] = {"意識不明状態になった", "重傷状態になった", "負傷状態になった", "健康になった"}; s += "は" + vs + "し、" + state[newstatus] + "。"; }
    put_text(_motion_type(), wrap_effectmotion(s, in_cardeffectmotion()));
}

void AdventurerLogger::damage_motion(std::shared_ptr<::cw::character::Character> arg_target, ::cw::lib::Integer arg_value, ::cw::lib::Integer arg_newlife, ::cw::lib::Integer arg_oldlife, bool arg_dissleep)
{
    if (!arg_target || arg_newlife == arg_oldlife) return;
    const I oldstatus = _get_lifestatus(arg_oldlife, arg_target->m_maxlife), newstatus = _get_lifestatus(arg_newlife, arg_target->m_maxlife);
    const S vs = arg_value < 20 ? "小ダメージ" : arg_value < 40 ? "中ダメージ" : "大ダメージ";
    const S name = arg_target->get_showingname();
    S s = name + "に" + vs + "。";
    if (newstatus != oldstatus) { const char* state[] = {"倒れた", "重傷を負った", "負傷した", "健康になった"}; s += name + "は" + state[newstatus] + "。"; }
    put_text(_motion_type(), wrap_effectmotion(s, in_cardeffectmotion()));
    if (arg_dissleep) put_text(_motion_type(), wrap_effectmotion(name + "は目を覚ました。", in_cardeffectmotion()));
}

void AdventurerLogger::absorb_motion(std::optional<std::shared_ptr<::cw::character::Character>> arg_user, ::cw::lib::Integer arg_healvalue, ::cw::lib::Integer arg_newulife, ::cw::lib::Integer arg_oldulife, std::shared_ptr<::cw::character::Character> arg_target, ::cw::lib::Integer arg_value, ::cw::lib::Integer arg_newlife, ::cw::lib::Integer arg_oldlife, bool arg_dissleep)
{
    damage_motion(arg_target, arg_value, arg_newlife, arg_oldlife, arg_dissleep);
    if (arg_user && *arg_user) heal_motion(*arg_user, arg_healvalue, arg_newulife, arg_oldulife);
}

void AdventurerLogger::paralyze_motion(std::shared_ptr<::cw::character::Character> arg_target, ::cw::lib::Integer arg_newvalue, ::cw::lib::Integer arg_oldvalue)
{
    // ネイティブ版ではnullの対象を保護する。Python版はここでCharacter
    // を必須とする。 値が変わらなければ記録しない規則は共通。
    if (!arg_target || arg_newvalue == arg_oldvalue) return;
    const bool oldp = ::cw::character::Character::calc_petrified(arg_oldvalue), newp = ::cw::character::Character::calc_petrified(arg_newvalue);
    S s; const S n = arg_target->get_showingname();
    if (arg_oldvalue <= 0 && arg_newvalue > 0) s = n + (newp ? "は石化した。" : "は麻痺した。");
    else if (arg_newvalue <= 0 && arg_oldvalue > 0) s = n + (oldp ? "の石化が解けた。" : "の麻痺は回復した。");
    else if (arg_oldvalue < arg_newvalue) {
        if (oldp && newp) s = n + "の石化が強化された。";
        else if (!oldp && newp) s = n + "は石化した。";
        else s = n + "の麻痺は悪化した。";
    } else {
        if (oldp && newp) s = n + "の石化は緩和された。";
        else if (oldp && !newp) s = n + "の石化が解けて麻痺状態になった。";
        else s = n + "の麻痺は緩和された。";
    }
    put_text(_motion_type(), wrap_effectmotion(s, in_cardeffectmotion()));
}

void AdventurerLogger::disparalyze_motion(std::shared_ptr<::cw::character::Character> arg_target, ::cw::lib::Integer arg_newvalue, ::cw::lib::Integer arg_oldvalue)
{
    paralyze_motion(std::move(arg_target), arg_newvalue, arg_oldvalue);
}

void AdventurerLogger::poison_motion(std::shared_ptr<::cw::character::Character> arg_target, ::cw::lib::Integer arg_newvalue, ::cw::lib::Integer arg_oldvalue)
{
    if (!arg_target || arg_newvalue == arg_oldvalue) return;
    const S n = arg_target->get_showingname();
    S s;
    if (arg_oldvalue <= 0 && arg_newvalue > 0) s = n + "は中毒した。";
    else if (arg_newvalue <= 0 && arg_oldvalue > 0) s = n + "の中毒は回復した。";
    else if (arg_oldvalue < arg_newvalue) s = n + "の中毒は悪化した。";
    else s = n + "の中毒は緩和された。";
    put_text(_motion_type(), wrap_effectmotion(s, in_cardeffectmotion()));
}

void AdventurerLogger::dispoison_motion(std::shared_ptr<::cw::character::Character> arg_target, ::cw::lib::Integer arg_newvalue, ::cw::lib::Integer arg_oldvalue)
{
    poison_motion(std::move(arg_target), arg_newvalue, arg_oldvalue);
}

void AdventurerLogger::getskillpower_motion(std::shared_ptr<::cw::character::Character> arg_target, ::cw::lib::Integer arg_value)
{
    if (!arg_target || arg_value <= 0) return;
    put_text(_motion_type(), wrap_effectmotion(arg_target->get_showingname() +
        (arg_value >= 9 ? "の精神力は完全に回復した。" : "の精神力は回復した。"), in_cardeffectmotion()));
}

void AdventurerLogger::loseskillpower_motion(std::shared_ptr<::cw::character::Character> arg_target, ::cw::lib::Integer arg_value)
{
    if (!arg_target || arg_value <= 0) return;
    put_text(_motion_type(), wrap_effectmotion(arg_target->get_showingname() +
        (arg_value >= 9 ? "は精神力を完全に喪失した。" : "は精神力を喪失した。"), in_cardeffectmotion()));
}

void AdventurerLogger::mentality_motion(std::shared_ptr<::cw::character::Character> arg_target, ::cw::lib::String arg_mentality, ::cw::lib::Integer arg_duration, ::cw::lib::String arg_oldmentality, ::cw::lib::Integer arg_oldduration)
{
    if (!arg_target) return;
    if (arg_mentality == "Normal") arg_duration = 0;
    else if (arg_duration == 0) arg_mentality = "Normal";
    if (arg_oldmentality == "Normal") arg_oldduration = 0;
    else if (arg_oldduration == 0) arg_oldmentality = "Normal";
    if (arg_mentality == arg_oldmentality && arg_duration == arg_oldduration) return;
    const S n = arg_target->get_showingname();
    S s;
    if (arg_mentality == "Normal") s = n + (arg_oldmentality == "Sleep" ? "は目を覚ました。" : "の精神は正常化した。");
    else if (arg_mentality == "Panic") s = n + "は恐慌状態になった。";
    else if (arg_mentality == "Brave") s = n + "は勇敢になった。";
    else if (arg_mentality == "Overheat") s = n + "は激昂した。";
    else if (arg_mentality == "Confuse") s = n + "は混乱した。";
    else if (arg_mentality == "Sleep") s = n + (arg_oldmentality == "Sleep" ? "は眠っている。" : "は眠った。");
    else throw std::invalid_argument("unknown mentality");
    put_text(_motion_type(), wrap_effectmotion(s, in_cardeffectmotion()));
}

void AdventurerLogger::bind_motion(std::shared_ptr<::cw::character::Character> arg_target, ::cw::lib::Integer, ::cw::lib::Integer)
{
    if (arg_target) put_text(_motion_type(), wrap_effectmotion(arg_target->get_showingname() + "は呪縛された。", in_cardeffectmotion()));
}

void AdventurerLogger::disbind_motion(std::shared_ptr<::cw::character::Character> arg_target, ::cw::lib::Integer, ::cw::lib::Integer)
{
    if (arg_target) put_text(_motion_type(), wrap_effectmotion(arg_target->get_showingname() + "の呪縛は解けた。", in_cardeffectmotion()));
}

void AdventurerLogger::silence_motion(std::shared_ptr<::cw::character::Character> arg_target, ::cw::lib::Integer, ::cw::lib::Integer)
{
    if (arg_target) put_text(_motion_type(), wrap_effectmotion(arg_target->get_showingname() + "は沈黙した。", in_cardeffectmotion()));
}

void AdventurerLogger::dissilence_motion(std::shared_ptr<::cw::character::Character> arg_target, ::cw::lib::Integer, ::cw::lib::Integer)
{
    if (arg_target) put_text(_motion_type(), wrap_effectmotion(arg_target->get_showingname() + "の沈黙は解けた。", in_cardeffectmotion()));
}

void AdventurerLogger::faceup_motion(std::shared_ptr<::cw::character::Character> arg_target, ::cw::lib::Integer, ::cw::lib::Integer)
{
    if (arg_target) put_text(_motion_type(), wrap_effectmotion(arg_target->get_showingname() + "は暴露された。", in_cardeffectmotion()));
}

void AdventurerLogger::facedown_motion(std::shared_ptr<::cw::character::Character> arg_target, ::cw::lib::Integer, ::cw::lib::Integer)
{
    if (arg_target) put_text(_motion_type(), wrap_effectmotion(arg_target->get_showingname() + "の暴露は解けた。", in_cardeffectmotion()));
}

void AdventurerLogger::antimagic_motion(std::shared_ptr<::cw::character::Character> arg_target, ::cw::lib::Integer, ::cw::lib::Integer)
{
    if (arg_target) put_text(_motion_type(), wrap_effectmotion(arg_target->get_showingname() + "は魔法無効化状態になった。", in_cardeffectmotion()));
}

void AdventurerLogger::disantimagic_motion(std::shared_ptr<::cw::character::Character> arg_target, ::cw::lib::Integer, ::cw::lib::Integer)
{
    if (arg_target) put_text(_motion_type(), wrap_effectmotion(arg_target->get_showingname() + "の魔法無効化状態は解けた。", in_cardeffectmotion()));
}

::cw::lib::String AdventurerLogger::_enhanceaction_motion(std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::Integer, bool> arg_params)
{
    auto [enhname, name, value, in_motion] = std::move(arg_params);
    ::cw::lib::String suffix;
    if (10 <= value) suffix = "は最大まで強化された。";
    else if (7 <= value) suffix = "は大幅に強化された。";
    else if (4 <= value) suffix = "は強化された。";
    else if (1 <= value) suffix = "はわずかに強化された。";
    else if (value <= -10) suffix = "は最低まで減少した。";
    else if (value <= -7) suffix = "は大幅に低下した。";
    else if (value <= -4) suffix = "は低下した。";
    else if (value <= -1) suffix = "はわずかに低下した。";
    else suffix = "は通常状態に戻った。";
    return wrap_effectmotion(name + "の" + enhname + suffix, in_motion);
}

void AdventurerLogger::enhanceaction_motion(std::shared_ptr<::cw::character::Character> arg_target, ::cw::lib::Integer arg_newvalue, ::cw::lib::Integer arg_oldvalue)
{
    if (arg_target && arg_newvalue != arg_oldvalue) put_text(_motion_type(), _enhanceaction_motion({"行動力", arg_target->get_showingname(), arg_newvalue, in_cardeffectmotion()}));
}

void AdventurerLogger::enhanceavoid_motion(std::shared_ptr<::cw::character::Character> arg_target, ::cw::lib::Integer arg_newvalue, ::cw::lib::Integer arg_oldvalue)
{
    if (arg_target && arg_newvalue != arg_oldvalue) put_text(_motion_type(), _enhanceaction_motion({"回避力", arg_target->get_showingname(), arg_newvalue, in_cardeffectmotion()}));
}

void AdventurerLogger::enhanceresist_motion(std::shared_ptr<::cw::character::Character> arg_target, ::cw::lib::Integer arg_newvalue, ::cw::lib::Integer arg_oldvalue)
{
    if (arg_target && arg_newvalue != arg_oldvalue) put_text(_motion_type(), _enhanceaction_motion({"抵抗力", arg_target->get_showingname(), arg_newvalue, in_cardeffectmotion()}));
}

void AdventurerLogger::enhancedefense_motion(std::shared_ptr<::cw::character::Character> arg_target, ::cw::lib::Integer arg_newvalue, ::cw::lib::Integer arg_oldvalue)
{
    if (arg_target && arg_newvalue != arg_oldvalue) put_text(_motion_type(), _enhanceaction_motion({"防御力", arg_target->get_showingname(), arg_newvalue, in_cardeffectmotion()}));
}

void AdventurerLogger::vanishtarget_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_runaway)
{
    if (arg_target) put_text(_motion_type(), wrap_effectmotion(arg_target->get_showingname() + (arg_runaway ? "は姿を消した。" : "は消滅した。"), in_cardeffectmotion()));
}

void AdventurerLogger::vanishcard_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_is_inactive, bool arg_is_battlestatus)
{
    if (arg_target && !arg_is_inactive && arg_is_battlestatus)
        put_text(_motion_type(), wrap_effectmotion(arg_target->get_showingname() + "の手札は破棄された。", in_cardeffectmotion()));
}

void AdventurerLogger::vanishbeast_motion(std::shared_ptr<::cw::character::Character> arg_target)
{
    if (arg_target) put_text(_motion_type(), wrap_effectmotion(arg_target->get_showingname() + "の召喚獣は消滅した。", in_cardeffectmotion()));
}

void AdventurerLogger::_deal_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_is_inactive, bool arg_is_battlestatus, ::cw::lib::Integer arg_resid)
{
    if (!arg_target || arg_is_inactive || !arg_is_battlestatus || !::cw::g_cwpy || !::cw::g_cwpy->m_rsrc) return;
    const auto it = ::cw::g_cwpy->m_rsrc->m_actioncards.find(arg_resid);
    if (it != ::cw::g_cwpy->m_rsrc->m_actioncards.end())
        put_text(_motion_type(), wrap_effectmotion(arg_target->get_showingname() + "に< " + card_name(it->second) + " >が配付された。", in_cardeffectmotion()));
}

void AdventurerLogger::dealattackcard_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_is_inactive, bool arg_is_battlestatus)
{
    _deal_motion(std::move(arg_target), arg_is_inactive, arg_is_battlestatus, 1);
}

void AdventurerLogger::dealpowerfulattackcard_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_is_inactive, bool arg_is_battlestatus)
{
    _deal_motion(std::move(arg_target), arg_is_inactive, arg_is_battlestatus, 2);
}

void AdventurerLogger::dealcriticalattackcard_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_is_inactive, bool arg_is_battlestatus)
{
    _deal_motion(std::move(arg_target), arg_is_inactive, arg_is_battlestatus, 3);
}

void AdventurerLogger::dealfeintcard_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_is_inactive, bool arg_is_battlestatus)
{
    _deal_motion(std::move(arg_target), arg_is_inactive, arg_is_battlestatus, 4);
}

void AdventurerLogger::dealdefensecard_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_is_inactive, bool arg_is_battlestatus)
{
    _deal_motion(std::move(arg_target), arg_is_inactive, arg_is_battlestatus, 5);
}

void AdventurerLogger::dealdistancecard_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_is_inactive, bool arg_is_battlestatus)
{
    _deal_motion(std::move(arg_target), arg_is_inactive, arg_is_battlestatus, 6);
}

void AdventurerLogger::dealconfusecard_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_is_inactive, bool arg_is_battlestatus)
{
    _deal_motion(std::move(arg_target), arg_is_inactive, arg_is_battlestatus, -1);
}

void AdventurerLogger::dealskillcard_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_is_inactive, bool arg_is_battlestatus)
{
    if (!arg_target) return;
    const S name = arg_target->get_showingname();
    const bool in_effect = in_cardeffectmotion();
    _put(_motion_type(), V{}, [name, inactive = arg_is_inactive, battle = arg_is_battlestatus, in_effect](const ::cw::lib::Arguments&, const ::cw::lib::KeywordArguments&) {
        const auto text = AdventurerLogger_dealskillcard_motion_dealskillcard_motion({name, inactive, battle, in_effect});
        return text ? V(*text) : V{};
    });
}

void AdventurerLogger::cancelaction_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_is_battlestatus)
{
    (void)arg_is_battlestatus;
    if (arg_target) put_text(_motion_type(), wrap_effectmotion(arg_target->get_showingname() + "の行動は止まった。", in_cardeffectmotion()));
}

void AdventurerLogger::summonbeast_motion(std::shared_ptr<::cw::character::Character> arg_target, std::shared_ptr<::cw::data::CWPyElement> arg_beastdata)
{
    if (!arg_target || !arg_beastdata) return;
    const S name = arg_target->get_showingname();
    const bool in_effect = in_cardeffectmotion();
    const auto beastdata = arg_beastdata;
    _put(_motion_type(), object_value(beastdata), [name, beastdata, in_effect](const ::cw::lib::Arguments&, const ::cw::lib::KeywordArguments&) {
        return V(AdventurerLogger_summonbeast_motion_summonbeast_motion({name, beastdata, in_effect}));
    });
}

void AdventurerLogger::poison_damage(std::shared_ptr<::cw::character::Character> arg_ccard, ::cw::lib::Integer arg_value, ::cw::lib::Integer arg_newlife, ::cw::lib::Integer arg_oldlife)
{
    (void)arg_value;
    if (!arg_ccard || arg_newlife == arg_oldlife) return;
    const I oldstatus = _get_lifestatus(arg_oldlife, arg_ccard->m_maxlife), newstatus = _get_lifestatus(arg_newlife, arg_ccard->m_maxlife);
    const S name = arg_ccard->get_showingname();
    S s = name + "は毒のダメージを受けた。";
    if (newstatus != oldstatus) { const char* state[] = {"毒で倒れた", "毒で重傷を負った", "毒で負傷した", "毒で健康になった"}; s = name + "は" + state[newstatus] + "。"; }
    put_text(g_ELAPSE_TIME, s);
}

void AdventurerLogger::recover_poison(std::shared_ptr<::cw::character::Character> arg_ccard)
{
    if (arg_ccard) put_text(g_ELAPSE_TIME, arg_ccard->get_showingname() + "の毒は抜けた。");
}

void AdventurerLogger::recover_paralyze(std::shared_ptr<::cw::character::Character> arg_ccard)
{
    if (arg_ccard) put_text(g_ELAPSE_TIME, arg_ccard->get_showingname() + "の麻痺は回復した。");
}

void AdventurerLogger::recover_bind(std::shared_ptr<::cw::character::Character> arg_ccard)
{
    if (arg_ccard) put_text(g_ELAPSE_TIME, arg_ccard->get_showingname() + "の呪縛は解けた。");
}

void AdventurerLogger::recover_silence(std::shared_ptr<::cw::character::Character> arg_ccard)
{
    if (arg_ccard) put_text(g_ELAPSE_TIME, arg_ccard->get_showingname() + "の沈黙は解けた。");
}

void AdventurerLogger::recover_faceup(std::shared_ptr<::cw::character::Character> arg_ccard)
{
    if (arg_ccard) put_text(g_ELAPSE_TIME, arg_ccard->get_showingname() + "の暴露は解けた。");
}

void AdventurerLogger::recover_antimagic(std::shared_ptr<::cw::character::Character> arg_ccard)
{
    if (arg_ccard) put_text(g_ELAPSE_TIME, arg_ccard->get_showingname() + "の魔法無効化状態は切れた。");
}

void AdventurerLogger::recover_mentality(std::shared_ptr<::cw::character::Character> arg_ccard, ::cw::lib::String arg_mentality)
{
    (void)arg_mentality;
    if (arg_ccard) put_text(g_ELAPSE_TIME, arg_ccard->get_showingname() + "の精神は正常化した。");
}

void AdventurerLogger::recover_enhance_act(std::shared_ptr<::cw::character::Character> arg_ccard)
{
    if (arg_ccard) put_text(g_ELAPSE_TIME, arg_ccard->get_showingname() + "の行動力は通常状態に戻った。");
}

void AdventurerLogger::recover_enhance_avo(std::shared_ptr<::cw::character::Character> arg_ccard)
{
    if (arg_ccard) put_text(g_ELAPSE_TIME, arg_ccard->get_showingname() + "の回避力は通常状態に戻った。");
}

void AdventurerLogger::recover_enhance_res(std::shared_ptr<::cw::character::Character> arg_ccard)
{
    if (arg_ccard) put_text(g_ELAPSE_TIME, arg_ccard->get_showingname() + "の抵抗力は通常状態に戻った。");
}

void AdventurerLogger::recover_enhance_def(std::shared_ptr<::cw::character::Character> arg_ccard)
{
    if (arg_ccard) put_text(g_ELAPSE_TIME, arg_ccard->get_showingname() + "の防御力は通常状態に戻った。");
}

namespace {
class FileLogOutput final : public NativeLogOutput {
    std::ofstream file_;
    bool nonempty_{};
public:
    bool open(const std::filesystem::path& path) override {
        file_.open(path, std::ios::out | std::ios::app);
        if (!file_) return false;
        file_.seekp(0, std::ios::end);
        const auto size = file_.tellp();
        nonempty_ = size > 0;
        return file_ && size >= 0;
    }
    bool is_open() const override { return file_.is_open(); }
    bool nonempty() const override { return nonempty_; }
    bool write(const S& text) override {
        file_.write(text.data(), static_cast<std::streamsize>(text.size()));
        return static_cast<bool>(file_);
    }
    bool flush() override { file_.flush(); return static_cast<bool>(file_); }
    bool close() override { file_.close(); return !file_.fail(); }
};
}

void Logger::initialize(::cw::lib::String arg_fpath, bool arg_enable)
{
    {
        std::lock_guard lock(m_worker_mutex);
        std::lock_guard queue_lock(m_queue_mutex);
        if (m_started && m_worker_id == std::this_thread::get_id())
            throw std::logic_error("Logger cannot reinitialize from its own worker");
    }
    std::lock_guard lifecycle_lock(m_lifecycle_mutex);
    stop_worker();
    m_fpath = std::move(arg_fpath);
    m_enable = arg_enable;
    std::lock_guard lock(m_queue_mutex);
    m_records.clear();
    m_stopping = false;
    m_started = false;
}

Logger::~Logger()
{
    stop_worker_impl(true);
}

void Logger::start_worker()
{
    // 外部のinitializeが終了を待っている間に、実行中のコールバック
    // がstartを求める場合がある。 開始済みなら何もしない動作を、寿命用
    // ロックを待たずに維持し、 コールバックと終了待ちのデッドロックを防ぐ。
    { std::lock_guard lock(m_queue_mutex); if (m_started) return; }
    std::lock_guard lifecycle_lock(m_lifecycle_mutex);
    // Python版はThreadのメソッドを起動し、run() 中はオブジェクトを
    // 保持する。 共有のネイティブロガーも、コールバックが最後の外部所有者を解放して
    // も同じ寿命を必要とする。 スタック上のロガーは、呼び出し元が寿命を管理する。
    const auto keep_alive = weak_from_this().lock();
    std::unique_lock worker_lock(m_worker_mutex);
    std::unique_lock queue_lock(m_queue_mutex);
    if (m_started) return;
    // 前回の実行はキューの処理を完了済みなので、新しいスレッドを代入する前に
    // ハンドルを回収する。 終了部分では、もうキューのロックを必要としない。
    if (worker.joinable()) worker.join();
    m_started = true;
    m_stopping = false;
    const auto generation = ++m_worker_generation;
    try {
        worker = std::thread([this, keep_alive, generation] {
            // 起動時の同期として、速いコールバックが自己停止・外部所有者
            // の解放を要求する前に、 作成側がワーカーとIDを反映する。
            { std::lock_guard lock(m_queue_mutex); }
            run();
            {
                std::lock_guard lock(m_queue_mutex);
                m_finished_generation = generation;
                m_started = false;
            }
            m_queue_condition.notify_all();
        });
        m_worker_id = worker.get_id();
    } catch (...) {
        m_started = false;
        m_finished_generation = generation;
        m_queue_condition.notify_all();
        throw;
    }
}

void Logger::enqueue(::cw::lib::String arg_text)
{
    {
        std::lock_guard lock(m_queue_mutex);
        if (m_stopping) return;
        m_records.push_back({AdventurerLogQueueRecord::Kind::Text, std::move(arg_text), false});
    }
    m_queue_condition.notify_one();
}

void Logger::enqueue_callback(::cw::lib::Value arg_data, ::cw::lib::Callback arg_func)
{
    {
        std::lock_guard lock(m_queue_mutex);
        if (m_stopping) return;
        m_records.push_back({AdventurerLogQueueRecord::Kind::Callback, {}, false, std::move(arg_data), std::move(arg_func)});
    }
    m_queue_condition.notify_one();
}

void Logger::set_enable(bool arg_enable)
{
    {
        std::lock_guard lock(m_queue_mutex);
        m_records.push_back({AdventurerLogQueueRecord::Kind::Toggle, {}, arg_enable});
    }
    m_queue_condition.notify_one();
}

void Logger::stop_worker()
{
    stop_worker_impl(false);
}

void Logger::stop_worker_impl(bool destroying)
{
    // 別の停止要求元がワーカーの終了を待つ間、このオブジェクトを保持する。 破棄中は
    // weak_from_this が空だが、デストラクター自身が保存領域を保持する。
    const auto keep_alive = weak_from_this().lock();
    std::uint64_t generation;
    std::thread joining_worker;
    bool self_stop;
    {
        std::lock_guard lock(m_worker_mutex);
        // start_worker と同じロック順にする。世代の取得、
        // Stopの追加、ハンドル取得を一緒に行い、 途中の再起動でハ
        // ンドルが置換され、停止していない実行の終了を待つことを防ぐ。
        std::lock_guard queue_lock(m_queue_mutex);
        generation = m_worker_generation;
        if (m_started && !m_stopping) {
            m_stopping = true;
            m_records.push_back({AdventurerLogQueueRecord::Kind::Stop, {}, false});
        }
        self_stop = m_worker_id == std::this_thread::get_id();
        if (worker.joinable()) {
            if (self_stop) {
                // 自身の完了済みスレッドハンドルを切り離せるのは、最後の所有者のデスト
                // ラクターだけ。 有効なコールバックはStopをキューへ入れて戻る。
                if (destroying) worker.detach();
            } else {
                joining_worker = std::move(worker);
            }
        }
    }
    m_queue_condition.notify_one();
    if (self_stop) return; // このコールバック自身の終了を待つと、デッドロックする。
    if (joining_worker.joinable()) joining_worker.join();
    else {
        // 別の呼び出し元が終了待ちのハンドルを持つ場合がある。 ハンドルのミュ
        // ーテックスを保持せず、後で再起動した世代ではなく、対象の実行を待つ。
        std::unique_lock lock(m_queue_mutex);
        m_queue_condition.wait(lock, [&] { return m_finished_generation >= generation; });
    }
}

void Logger::run()
{
    std::unique_ptr<NativeLogOutput> file;
    try {
        file = m_native_output_factory ? m_native_output_factory() : std::make_unique<FileLogOutput>();
        if (!file) throw std::invalid_argument("native play log output is missing");
    } catch (...) {
        ::cw::util::print_ex(std::nullopt);
        return;
    }
    std::filesystem::path log_path;
    ::cw::lib::String mounted_text;
    bool mounted = false;
    bool mounted_open = false;
    bool first = true;
    auto open_file = [&]() -> bool {
        if (file->is_open() || mounted_open) return true;
        try {
            log_path = std::filesystem::u8path(m_fpath);
            // Python互換の形式では ScenarioLog.xml に相対パス
            // で保存するが、PlayLog は明示的にUserDataとして扱う。
            if (const auto resolved =
                    ::cardwirth::platform::resolve_mounted_user_path(log_path))
                log_path = *resolved;
            mounted = ::cardwirth::platform::is_mounted_user_path(log_path);
            if (mounted) {
                const auto status = ::cardwirth::platform::stat_mounted_user_path(log_path);
                if (status && status->is_file()) {
                    const auto bytes = ::cardwirth::platform::read_mounted_user_file(log_path);
                    if (!bytes) throw std::runtime_error("failed to read encrypted play log: " + m_fpath);
                    mounted_text.assign(reinterpret_cast<const char*>(bytes->data()), bytes->size());
                } else {
                    mounted_text.clear();
                }
                if (first && !mounted_text.empty()) mounted_text.push_back('\n');
                first = false;
                mounted_open = true;
                return true;
            }
            if (log_path.has_parent_path()) std::filesystem::create_directories(log_path.parent_path());
            if (!file->open(log_path)) throw std::runtime_error("failed to open play log: " + m_fpath);
            if (first && file->nonempty() && !file->write("\n"))
                throw std::runtime_error("failed to write play log: " + m_fpath);
            first = false;
            return true;
        } catch (...) {
            ::cw::util::print_ex(std::nullopt);
            return false;
        }
    };
    const auto close_file = [&]() {
        try {
            if (!file->is_open()) return true;
            if (file->close()) return true;
        } catch (...) {
            ::cw::util::print_ex(std::nullopt);
            return false;
        }
        // Pythonの TextIO.close と同様、待機時・最終
        // 終了時のどちらの失敗も通知する。 その後でワーカーを停止する。
        ::cw::util::print_ex(std::make_optional<::cw::lib::Value>(
            "failed to close play log: " + m_fpath));
        return false;
    };
    // ネイティブのスレッド境界として、Python版の失敗時の終了とfinally
    // で閉じる順序を維持する。 ただし std::thread か
    // ら例外を外へ出してアプリ全体を終了させず、失敗を報告する。
    try {
    while (true) {
        AdventurerLogQueueRecord record;
        {
            std::unique_lock lock(m_queue_mutex);
            // ネイティブ版の意図的な時刻管理。待機中のキューワーカーはCPU時間を
            // 消費しないため、 Pythonの time.process_time
            // のしきい値ではなく、実際の待機時間でファイルハンドルを解放する。
            if (!m_queue_condition.wait_for(lock, std::chrono::seconds(10), [&] {
                    return !m_records.empty();
                })) {
                lock.unlock(); // キューのミューテックスを保持したまま、ホストの診断を通知しない。
                if (!close_file()) break;
                mounted_open = false;
                continue;
            }
            record = std::move(m_records.front());
            m_records.pop_front();
        }
        bool skip_sleep = false;
        if (record.kind == AdventurerLogQueueRecord::Kind::Stop) break;
        if (record.kind == AdventurerLogQueueRecord::Kind::Toggle) {
            m_enable = record.enable;
            skip_sleep = true;
        } else if (m_enable) {
            S text;
            if (record.kind == AdventurerLogQueueRecord::Kind::Callback) {
                const V result = record.func ? record.func({record.data}, {}) : V{};
                if (result.is_none()) skip_sleep = true;
                else text = value_string(result);
            } else {
                text = std::move(record.text);
            }
            if (!skip_sleep) {
                if (!open_file()) break;
                if (mounted) {
                    mounted_text += text;
                    mounted_text.push_back('\n');
                    const auto bytes = std::as_bytes(std::span(mounted_text.data(), mounted_text.size()));
                    if (!::cardwirth::platform::write_mounted_user_file(log_path, bytes, true)) {
                        ::cw::util::print_ex(std::make_optional<::cw::lib::Value>(
                            "failed to write encrypted play log: " + m_fpath));
                        break;
                    }
                } else {
                    if (!file->write(text + '\n') || !file->flush()) {
                        ::cw::util::print_ex(std::make_optional<::cw::lib::Value>(
                            "failed to write play log: " + m_fpath));
                        break;
                    }
                }
            }
        }
        if (skip_sleep) continue;
    }
    } catch (...) {
        ::cw::util::print_ex(std::nullopt);
    }
    (void)close_file();
    mounted_open = false;
}

::cw::lib::Integer g_ELAPSE_TIME{6};
::cw::lib::Integer g_INITIAL{1};
::cw::lib::Integer g_MESSAGE{3};
::cw::lib::Integer g_MOTION{4};
::cw::lib::Integer g_MOTION_IN_BATTLE{5};
::cw::lib::Integer g_SEPARATOR{7};
::cw::lib::Integer g_SYSTEM{2};
::cw::lib::Integer g_VOID{0};
::cw::lib::Value g__LogType{};

::cw::lib::Value AdventurerLogger_start_scenario_lambda_line_94(::cw::lib::Value arg_lines)
{
    return arg_lines;
}

::cw::lib::Value AdventurerLogger_end_scenario_lambda_line_133(::cw::lib::Value arg_s)
{
    return ljustify(value_string(arg_s), ::cw::g_LOG_SEPARATOR_LEN_LONG, '=');
}

::cw::lib::Value AdventurerLogger_end_scenario_lambda_line_136(::cw::lib::Value arg_s)
{
    return ljustify(value_string(arg_s), ::cw::g_LOG_SEPARATOR_LEN_LONG, '=');
}

::cw::lib::Value AdventurerLogger_gameover_lambda_line_144(::cw::lib::Value arg_s)
{
    return ljustify(value_string(arg_s), ::cw::g_LOG_SEPARATOR_LEN_LONG, '=');
}

::cw::lib::Value AdventurerLogger_f9_lambda_line_148(::cw::lib::Value arg_s)
{
    return ljustify(value_string(arg_s), ::cw::g_LOG_SEPARATOR_LEN_LONG, '=');
}

::cw::lib::Value AdventurerLogger__put_lambda_line_177(::cw::lib::Value arg_s)
{
    return arg_s;
}

::cw::lib::Value AdventurerLogger_show_message_lambda_line_183(::cw::lib::Value arg_mwin)
{
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&arg_mwin.storage()); object && *object) {
        if (const auto window = std::dynamic_pointer_cast<::cw::sprite::message::MessageWindow>(*object))
            return ::cw::sprite::message::get_messagelogtext({arg_mwin}, false);
    }
    return {};
}

::cw::lib::String AdventurerLogger_click_menucard_click_menucard(::cw::lib::String arg_name)
{
    const S s = arg_name.empty() ? "==" : "==< " + arg_name + " >==";
    return rjustify(s, ::cw::g_LOG_SEPARATOR_LEN_MIDDLE, '=');
}

::cw::lib::Value AdventurerLogger_rename_party_lambda_line_203(::cw::lib::Value arg_name)
{
    return "パーティ名を[ " + value_string(arg_name) + " ]に変更";
}

::cw::lib::Value AdventurerLogger_start_battle_lambda_line_206(::cw::lib::Value arg_dummy)
{
    (void)arg_dummy;
    return rjustify("==[ バトル開始 ]==", ::cw::g_LOG_SEPARATOR_LEN_LONG, '=');
}

::cw::lib::Value AdventurerLogger_end_battle_lambda_line_211(::cw::lib::Value arg_dummy)
{
    (void)arg_dummy;
    return rjustify("==[ バトル終了 ]==", ::cw::g_LOG_SEPARATOR_LEN_LONG, '=');
}

::cw::lib::String AdventurerLogger_runaway_runaway(std::tuple<::cw::lib::String, bool> arg_params)
{
    const auto& [name, success] = arg_params;
    return name + (success ? "は逃走した。" : "は逃走を試みたが、失敗した。");
}

::cw::lib::Value AdventurerLogger_start_round_lambda_line_230(::cw::lib::Value arg_roundval)
{
    return "<<<< ラウンド " + value_string(arg_roundval) + " >>>>";
}

::cw::lib::String AdventurerLogger_use_card_use_card(std::tuple<::cw::lib::String, ::cw::lib::String, bool, std::optional<::cw::lib::String>, ::cw::lib::String, bool> arg_params)
{
    const auto& [castname, cardname, isbeast, targetname, targettype, battle] = arg_params;
    if (battle) return castname + (isbeast ? "の< " : "は< ") + cardname + (isbeast ? " >が発動。" : " >を使用。");
    S s = targetname && targettype != "User" && targettype != "None" ?
        "==" + castname + "が< " + cardname + " >を< " + *targetname + " >に使用==" :
        "==" + castname + "が< " + cardname + " >を使用==";
    return rjustify(s, ::cw::g_LOG_SEPARATOR_LEN_MIDDLE, '=');
}

::cw::lib::String AdventurerLogger_avoid_avoid(std::tuple<::cw::lib::String, bool> arg_params)
{
    const auto& [name, in_motion] = arg_params;
    return in_motion ? "  " + name + "は回避した。" : name + "は回避した。";
}

::cw::lib::String AdventurerLogger_noeffect_noeffect(std::tuple<::cw::lib::String, bool> arg_params)
{
    const auto& [name, in_motion] = arg_params;
    return in_motion ? "  " + name + "は抵抗した。" : name + "は抵抗した。";
}

::cw::lib::String AdventurerLogger_effect_failed_effect_failed(std::tuple<::cw::lib::String, bool> arg_params)
{
    const auto& [name, in_motion] = arg_params;
    return in_motion ? "  " + name + "には効果がなかった。" : name + "には効果がなかった。";
}

::cw::lib::String AdventurerLogger_heal_motion_heal_motion(std::tuple<::cw::lib::String, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, bool> arg_params)
{
    const auto& [name, value, newlife, oldlife, maxlife, in_motion] = arg_params;
    const I ns = newlife <= 0 ? 0 : ::cw::character::Character::calc_heavyinjured(newlife, maxlife) ? 1 : ::cw::character::Character::calc_injured(newlife, maxlife) ? 2 : 3;
    const I os = oldlife <= 0 ? 0 : ::cw::character::Character::calc_heavyinjured(oldlife, maxlife) ? 1 : ::cw::character::Character::calc_injured(oldlife, maxlife) ? 2 : 3;
    const S vs = value < 20 ? "小回復" : value < 40 ? "中回復" : "大回復";
    S s = name + "は" + vs + "。";
    if (ns != os) { const char* state[] = {"意識不明状態になった", "重傷状態になった", "負傷状態になった", "健康になった"}; s = name + "は" + vs + "し、" + state[ns] + "。"; }
    return in_motion ? "  " + s : s;
}

::cw::lib::String AdventurerLogger_damage_motion_damage_motion(std::tuple<::cw::lib::String, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, bool> arg_params)
{
    const auto& [name, value, newlife, oldlife, maxlife, in_motion] = arg_params;
    const I ns = newlife <= 0 ? 0 : ::cw::character::Character::calc_heavyinjured(newlife, maxlife) ? 1 : ::cw::character::Character::calc_injured(newlife, maxlife) ? 2 : 3;
    const I os = oldlife <= 0 ? 0 : ::cw::character::Character::calc_heavyinjured(oldlife, maxlife) ? 1 : ::cw::character::Character::calc_injured(oldlife, maxlife) ? 2 : 3;
    const S vs = value < 20 ? "小ダメージ" : value < 40 ? "中ダメージ" : "大ダメージ";
    S s = name + "に" + vs + "。";
    if (ns != os) { const char* state[] = {"倒れた", "重傷を負った", "負傷した", "健康になった"}; s += name + "は" + state[ns] + "。"; }
    return in_motion ? "  " + s : s;
}

::cw::lib::String AdventurerLogger_damage_motion_dissleep_func(std::tuple<::cw::lib::String, bool> arg_params)
{
    const auto& [name, in_motion] = arg_params;
    return in_motion ? "  " + name + "は目を覚ました。" : name + "は目を覚ました。";
}

::cw::lib::String AdventurerLogger_paralyze_motion_paralyze_motion(std::tuple<::cw::lib::String, ::cw::lib::Integer, ::cw::lib::Integer, bool> arg_params)
{
    const auto& [name, nv, ov, in_motion] = arg_params;
    const bool op = ::cw::character::Character::calc_petrified(ov), np = ::cw::character::Character::calc_petrified(nv);
    S s;
    if (ov <= 0 && nv > 0) s = name + (np ? "は石化した。" : "は麻痺した。");
    else if (nv <= 0 && ov > 0) s = name + (op ? "の石化が解けた。" : "の麻痺は回復した。");
    else if (ov < nv) s = op && np ? name + "の石化が強化された。" : !op && np ? name + "は石化した。" : name + "の麻痺は悪化した。";
    else s = op && np ? name + "の石化は緩和された。" : op && !np ? name + "の石化が解けて麻痺状態になった。" : name + "の麻痺は緩和された。";
    return in_motion ? "  " + s : s;
}

::cw::lib::String AdventurerLogger_poison_motion_poison_motion(std::tuple<::cw::lib::String, ::cw::lib::Integer, ::cw::lib::Integer, bool> arg_params)
{
    const auto& [name, nv, ov, in_motion] = arg_params;
    S s = ov <= 0 && nv > 0 ? name + "は中毒した。" : nv <= 0 && ov > 0 ? name + "の中毒は回復した。" : ov < nv ? name + "の中毒は悪化した。" : name + "の中毒は緩和された。";
    return in_motion ? "  " + s : s;
}

::cw::lib::String AdventurerLogger_getskillpower_motion_getskillpower_motion(std::tuple<::cw::lib::String, ::cw::lib::Integer, bool> arg_params)
{
    const auto& [name, value, in_motion] = arg_params;
    S s = name + (value >= 9 ? "の精神力は完全に回復した。" : "の精神力は回復した。");
    return in_motion ? "  " + s : s;
}

::cw::lib::String AdventurerLogger_loseskillpower_motion_loseskillpower_motion(std::tuple<::cw::lib::String, ::cw::lib::Integer, bool> arg_params)
{
    const auto& [name, value, in_motion] = arg_params;
    S s = name + (value >= 9 ? "は精神力を完全に喪失した。" : "は精神力を喪失した。");
    return in_motion ? "  " + s : s;
}

::cw::lib::String AdventurerLogger_mentality_motion_mentality_motion(std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::Integer, ::cw::lib::String, ::cw::lib::Integer, bool> arg_params)
{
    const auto& [name, mentality, duration, oldmentality, oldduration, in_motion] = arg_params;
    (void)duration; (void)oldduration;
    S s;
    if (mentality == "Normal") s = name + (oldmentality == "Sleep" ? "は目を覚ました。" : "の精神は正常化した。");
    else if (mentality == "Panic") s = name + "は恐慌状態になった。";
    else if (mentality == "Brave") s = name + "は勇敢になった。";
    else if (mentality == "Overheat") s = name + "は激昂した。";
    else if (mentality == "Confuse") s = name + "は混乱した。";
    else s = name + (oldmentality == "Sleep" ? "は眠っている。" : "は眠った。");
    return in_motion ? "  " + s : s;
}

::cw::lib::String AdventurerLogger_bind_motion_bind_motion(std::tuple<::cw::lib::String, bool> arg_params)
{
    const auto& [name, in_motion] = arg_params; return in_motion ? "  " + name + "は呪縛された。" : name + "は呪縛された。";
}

::cw::lib::String AdventurerLogger_disbind_motion_disbind_motion(std::tuple<::cw::lib::String, bool> arg_params)
{
    const auto& [name, in_motion] = arg_params; return in_motion ? "  " + name + "の呪縛は解けた。" : name + "の呪縛は解けた。";
}

::cw::lib::String AdventurerLogger_silence_motion_silence_motion(std::tuple<::cw::lib::String, bool> arg_params)
{
    const auto& [name, in_motion] = arg_params; return in_motion ? "  " + name + "は沈黙した。" : name + "は沈黙した。";
}

::cw::lib::String AdventurerLogger_dissilence_motion_dissilence_motion(std::tuple<::cw::lib::String, bool> arg_params)
{
    const auto& [name, in_motion] = arg_params; return in_motion ? "  " + name + "の沈黙は解けた。" : name + "の沈黙は解けた。";
}

::cw::lib::String AdventurerLogger_faceup_motion_faceup_motion(std::tuple<::cw::lib::String, bool> arg_params)
{
    const auto& [name, in_motion] = arg_params; return in_motion ? "  " + name + "は暴露された。" : name + "は暴露された。";
}

::cw::lib::String AdventurerLogger_facedown_motion_facedown_motion(std::tuple<::cw::lib::String, bool> arg_params)
{
    const auto& [name, in_motion] = arg_params; return in_motion ? "  " + name + "の暴露は解けた。" : name + "の暴露は解けた。";
}

::cw::lib::String AdventurerLogger_antimagic_motion_antimagic_motion(std::tuple<::cw::lib::String, bool> arg_params)
{
    const auto& [name, in_motion] = arg_params; return in_motion ? "  " + name + "は魔法無効化状態になった。" : name + "は魔法無効化状態になった。";
}

::cw::lib::String AdventurerLogger_disantimagic_motion_disantimagic_motion(std::tuple<::cw::lib::String, bool> arg_params)
{
    const auto& [name, in_motion] = arg_params; return in_motion ? "  " + name + "の魔法無効化状態は解けた。" : name + "の魔法無効化状態は解けた。";
}

::cw::lib::String AdventurerLogger_vanishtarget_motion_vanishtarget_motion(std::tuple<::cw::lib::String, bool, bool> arg_params)
{
    const auto& [name, runaway, in_motion] = arg_params;
    const S s = name + (runaway ? "は姿を消した。" : "は消滅した。");
    return in_motion ? "  " + s : s;
}

::cw::lib::String AdventurerLogger_vanishcard_motion_vanishcard_motion(std::tuple<::cw::lib::String, bool> arg_params)
{
    const auto& [name, in_motion] = arg_params; const S s = name + "の手札は破棄された。"; return in_motion ? "  " + s : s;
}

::cw::lib::String AdventurerLogger_vanishbeast_motion_vanishbeast_motion(std::tuple<::cw::lib::String, bool> arg_params)
{
    const auto& [name, in_motion] = arg_params; const S s = name + "の召喚獣は消滅した。"; return in_motion ? "  " + s : s;
}

::cw::lib::String AdventurerLogger__deal_motion_deal_motion(std::tuple<::cw::lib::String, ::cw::lib::String, bool> arg_params)
{
    const auto& [name, card, in_motion] = arg_params; const S s = name + "に< " + card + " >が配付された。"; return in_motion ? "  " + s : s;
}

std::optional<::cw::lib::String> AdventurerLogger_dealskillcard_motion_dealskillcard_motion(std::tuple<::cw::lib::String, bool, bool, bool> arg_params)
{
    const auto& [name, inactive, battle, in_motion] = arg_params;
    if (inactive || !battle) return std::nullopt;
    const S s = name + "に特殊技能カードが配付された。"; return in_motion ? "  " + s : s;
}

::cw::lib::String AdventurerLogger_cancelaction_motion_cancelaction_motion(std::tuple<::cw::lib::String, bool> arg_params)
{
    const auto& [name, in_motion] = arg_params; const S s = name + "の行動は止まった。"; return in_motion ? "  " + s : s;
}

::cw::lib::String AdventurerLogger_summonbeast_motion_summonbeast_motion(std::tuple<::cw::lib::String, std::shared_ptr<::cw::data::CWPyElement>, bool> arg_params)
{
    const auto& [name, data, in_motion] = arg_params; const S s = name + "に召喚獣< " + (data ? data->gettext("Property/Name", std::nullopt) : S{}) + " >が付与された。"; return in_motion ? "  " + s : s;
}

::cw::lib::String AdventurerLogger_poison_damage_poison_damage(std::tuple<::cw::lib::String, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> arg_params)
{
    const auto& [name, value, newlife, oldlife, maxlife] = arg_params; (void)value;
    const I ns = newlife <= 0 ? 0 : ::cw::character::Character::calc_heavyinjured(newlife, maxlife) ? 1 : ::cw::character::Character::calc_injured(newlife, maxlife) ? 2 : 3;
    const I os = oldlife <= 0 ? 0 : ::cw::character::Character::calc_heavyinjured(oldlife, maxlife) ? 1 : ::cw::character::Character::calc_injured(oldlife, maxlife) ? 2 : 3;
    if (ns == os) return name + "は毒のダメージを受けた。";
    const char* state[] = {"毒で倒れた", "毒で重傷を負った", "毒で負傷した", "毒で健康になった"}; return name + "は" + state[ns] + "。";
}

::cw::lib::Value AdventurerLogger_recover_poison_lambda_line_742(::cw::lib::Value arg_name)
{
    return value_string(arg_name) + "の毒は抜けた。";
}

::cw::lib::Value AdventurerLogger_recover_paralyze_lambda_line_745(::cw::lib::Value arg_name)
{
    return value_string(arg_name) + "の麻痺は回復した。";
}

::cw::lib::Value AdventurerLogger_recover_bind_lambda_line_748(::cw::lib::Value arg_name)
{
    return value_string(arg_name) + "の呪縛は解けた。";
}

::cw::lib::Value AdventurerLogger_recover_silence_lambda_line_751(::cw::lib::Value arg_name)
{
    return value_string(arg_name) + "の沈黙は解けた。";
}

::cw::lib::Value AdventurerLogger_recover_faceup_lambda_line_754(::cw::lib::Value arg_name)
{
    return value_string(arg_name) + "の暴露は解けた。";
}

::cw::lib::Value AdventurerLogger_recover_antimagic_lambda_line_757(::cw::lib::Value arg_name)
{
    return value_string(arg_name) + "の魔法無効化状態は切れた。";
}

::cw::lib::Value AdventurerLogger_recover_mentality_lambda_line_760(::cw::lib::Value arg_name)
{
    return value_string(arg_name) + "の精神は正常化した。";
}

::cw::lib::Value AdventurerLogger_recover_enhance_act_lambda_line_763(::cw::lib::Value arg_name)
{
    return value_string(arg_name) + "の行動力は通常状態に戻った。";
}

::cw::lib::Value AdventurerLogger_recover_enhance_avo_lambda_line_766(::cw::lib::Value arg_name)
{
    return value_string(arg_name) + "の回避力は通常状態に戻った。";
}

::cw::lib::Value AdventurerLogger_recover_enhance_res_lambda_line_769(::cw::lib::Value arg_name)
{
    return value_string(arg_name) + "の抵抗力は通常状態に戻った。";
}

::cw::lib::Value AdventurerLogger_recover_enhance_def_lambda_line_772(::cw::lib::Value arg_name)
{
    return value_string(arg_name) + "の防御力は通常状態に戻った。";
}

void main()
{
}

}
