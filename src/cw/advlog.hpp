#pragma once

#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"
#include <condition_variable>
#include <deque>
#include <filesystem>

namespace cw::advlog {
class AdventurerLogger : public ::cw::lib::Object {
public:
    AdventurerLogger() = default;
    virtual ~AdventurerLogger();
    bool m__enable{};
    ::cw::lib::Integer m__last_logtype{};
    std::optional<std::shared_ptr<::cw::advlog::Logger>> m__logger{};
    ::cw::lib::String m_logfilepath{};
    void initialize();
    void start_scenario();
    void resume_scenario(::cw::lib::String arg_logfilepath);
    void enable(bool arg_enable);
    void force_quit();
    void end_scenario(bool arg_end, bool arg_completestamp);
    void gameover();
    void f9();
    void _put_logtype(::cw::lib::Integer arg_logtype);
    void _put(::cw::lib::Integer arg_logtype, ::cw::lib::Value arg_data, std::optional<::cw::lib::Callback> arg_func, bool arg_usecard = false);
    void show_message(std::shared_ptr<::cw::sprite::message::MessageWindow> arg_mwin);
    void separator();
    void start_timeelapse();
    void click_menucard(std::shared_ptr<::cw::sprite::card::MenuCard> arg_mcard);
    void rename_party(::cw::lib::String arg_newname, ::cw::lib::String arg_oldname);
    void start_battle(std::shared_ptr<::cw::battle::BattleEngine> arg_battle);
    void end_battle(std::shared_ptr<::cw::battle::BattleEngine> arg_battle);
    void start_runaway();
    void runaway(bool arg_success);
    void start_round(::cw::lib::Integer arg_roundval);
    ::cw::lib::Integer _motion_type();
    void use_card(std::shared_ptr<::cw::character::Character> arg_ccard, std::shared_ptr<::cw::header::CardHeader> arg_header, ::cw::lib::Value arg_targets);
    ::cw::lib::String wrap_effectmotion(::cw::lib::String arg_s, bool arg_in_cardeffectmotion);
    bool in_cardeffectmotion();
    void avoid(std::shared_ptr<::cw::character::Character> arg_target);
    void noeffect(std::shared_ptr<::cw::character::Character> arg_target);
    void effect_failed(::cw::lib::Value arg_target, bool arg_ismenucard);
    ::cw::lib::Integer _get_lifestatus(::cw::lib::Integer arg_life, ::cw::lib::Integer arg_maxlife);
    void heal_motion(std::shared_ptr<::cw::character::Character> arg_target, ::cw::lib::Integer arg_value, ::cw::lib::Integer arg_newlife, ::cw::lib::Integer arg_oldlife);
    void damage_motion(std::shared_ptr<::cw::character::Character> arg_target, ::cw::lib::Integer arg_value, ::cw::lib::Integer arg_newlife, ::cw::lib::Integer arg_oldlife, bool arg_dissleep);
    void absorb_motion(std::optional<std::shared_ptr<::cw::character::Character>> arg_user, ::cw::lib::Integer arg_healvalue, ::cw::lib::Integer arg_newulife, ::cw::lib::Integer arg_oldulife, std::shared_ptr<::cw::character::Character> arg_target, ::cw::lib::Integer arg_value, ::cw::lib::Integer arg_newlife, ::cw::lib::Integer arg_oldlife, bool arg_dissleep);
    void paralyze_motion(std::shared_ptr<::cw::character::Character> arg_target, ::cw::lib::Integer arg_newvalue, ::cw::lib::Integer arg_oldvalue);
    void disparalyze_motion(std::shared_ptr<::cw::character::Character> arg_target, ::cw::lib::Integer arg_newvalue, ::cw::lib::Integer arg_oldvalue);
    void poison_motion(std::shared_ptr<::cw::character::Character> arg_target, ::cw::lib::Integer arg_newvalue, ::cw::lib::Integer arg_oldvalue);
    void dispoison_motion(std::shared_ptr<::cw::character::Character> arg_target, ::cw::lib::Integer arg_newvalue, ::cw::lib::Integer arg_oldvalue);
    void getskillpower_motion(std::shared_ptr<::cw::character::Character> arg_target, ::cw::lib::Integer arg_value);
    void loseskillpower_motion(std::shared_ptr<::cw::character::Character> arg_target, ::cw::lib::Integer arg_value);
    void mentality_motion(std::shared_ptr<::cw::character::Character> arg_target, ::cw::lib::String arg_mentality, ::cw::lib::Integer arg_duration, ::cw::lib::String arg_oldmentality, ::cw::lib::Integer arg_oldduration);
    void bind_motion(std::shared_ptr<::cw::character::Character> arg_target, ::cw::lib::Integer arg_newvalue, ::cw::lib::Integer arg_oldvalue);
    void disbind_motion(std::shared_ptr<::cw::character::Character> arg_target, ::cw::lib::Integer arg_newvalue, ::cw::lib::Integer arg_oldvalue);
    void silence_motion(std::shared_ptr<::cw::character::Character> arg_target, ::cw::lib::Integer arg_newvalue, ::cw::lib::Integer arg_oldvalue);
    void dissilence_motion(std::shared_ptr<::cw::character::Character> arg_target, ::cw::lib::Integer arg_newvalue, ::cw::lib::Integer arg_oldvalue);
    void faceup_motion(std::shared_ptr<::cw::character::Character> arg_target, ::cw::lib::Integer arg_newvalue, ::cw::lib::Integer arg_oldvalue);
    void facedown_motion(std::shared_ptr<::cw::character::Character> arg_target, ::cw::lib::Integer arg_newvalue, ::cw::lib::Integer arg_oldvalue);
    void antimagic_motion(std::shared_ptr<::cw::character::Character> arg_target, ::cw::lib::Integer arg_newvalue, ::cw::lib::Integer arg_oldvalue);
    void disantimagic_motion(std::shared_ptr<::cw::character::Character> arg_target, ::cw::lib::Integer arg_newvalue, ::cw::lib::Integer arg_oldvalue);
    ::cw::lib::String _enhanceaction_motion(std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::Integer, bool> arg_params);
    void enhanceaction_motion(std::shared_ptr<::cw::character::Character> arg_target, ::cw::lib::Integer arg_newvalue, ::cw::lib::Integer arg_oldvalue);
    void enhanceavoid_motion(std::shared_ptr<::cw::character::Character> arg_target, ::cw::lib::Integer arg_newvalue, ::cw::lib::Integer arg_oldvalue);
    void enhanceresist_motion(std::shared_ptr<::cw::character::Character> arg_target, ::cw::lib::Integer arg_newvalue, ::cw::lib::Integer arg_oldvalue);
    void enhancedefense_motion(std::shared_ptr<::cw::character::Character> arg_target, ::cw::lib::Integer arg_newvalue, ::cw::lib::Integer arg_oldvalue);
    void vanishtarget_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_runaway);
    void vanishcard_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_is_inactive, bool arg_is_battlestatus);
    void vanishbeast_motion(std::shared_ptr<::cw::character::Character> arg_target);
    void _deal_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_is_inactive, bool arg_is_battlestatus, ::cw::lib::Integer arg_resid);
    void dealattackcard_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_is_inactive, bool arg_is_battlestatus);
    void dealpowerfulattackcard_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_is_inactive, bool arg_is_battlestatus);
    void dealcriticalattackcard_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_is_inactive, bool arg_is_battlestatus);
    void dealfeintcard_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_is_inactive, bool arg_is_battlestatus);
    void dealdefensecard_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_is_inactive, bool arg_is_battlestatus);
    void dealdistancecard_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_is_inactive, bool arg_is_battlestatus);
    void dealconfusecard_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_is_inactive, bool arg_is_battlestatus);
    void dealskillcard_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_is_inactive, bool arg_is_battlestatus);
    void cancelaction_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_is_battlestatus);
    void summonbeast_motion(std::shared_ptr<::cw::character::Character> arg_target, std::shared_ptr<::cw::data::CWPyElement> arg_beastdata);
    void poison_damage(std::shared_ptr<::cw::character::Character> arg_ccard, ::cw::lib::Integer arg_value, ::cw::lib::Integer arg_newlife, ::cw::lib::Integer arg_oldlife);
    void recover_poison(std::shared_ptr<::cw::character::Character> arg_ccard);
    void recover_paralyze(std::shared_ptr<::cw::character::Character> arg_ccard);
    void recover_bind(std::shared_ptr<::cw::character::Character> arg_ccard);
    void recover_silence(std::shared_ptr<::cw::character::Character> arg_ccard);
    void recover_faceup(std::shared_ptr<::cw::character::Character> arg_ccard);
    void recover_antimagic(std::shared_ptr<::cw::character::Character> arg_ccard);
    void recover_mentality(std::shared_ptr<::cw::character::Character> arg_ccard, ::cw::lib::String arg_mentality);
    void recover_enhance_act(std::shared_ptr<::cw::character::Character> arg_ccard);
    void recover_enhance_avo(std::shared_ptr<::cw::character::Character> arg_ccard);
    void recover_enhance_res(std::shared_ptr<::cw::character::Character> arg_ccard);
    void recover_enhance_def(std::shared_ptr<::cw::character::Character> arg_ccard);

private:
    void enqueue_text(::cw::lib::String arg_text);
    void put_text(::cw::lib::Integer arg_logtype, ::cw::lib::String arg_text,
                  bool arg_usecard = false);
};

struct AdventurerLogQueueRecord {
    enum class Kind { Text, Toggle, Stop, Callback } kind{Kind::Text};
    ::cw::lib::String text{};
    bool enable{};
    ::cw::lib::Value data{};
    ::cw::lib::Callback func{};
};

// OS依存部分はストリームの基本操作に限る。FIFO・先頭記録の区切り・失敗
// 処理・ ワーカーの寿命は、すべてのホストでLogger::runが管理する
// 。 実際の保存先の失敗を通知するため、呼び出し元からストリームを渡せる。
class NativeLogOutput {
public:
    virtual ~NativeLogOutput() = default;
    virtual bool open(const std::filesystem::path& path) = 0;
    virtual bool is_open() const = 0;
    virtual bool nonempty() const = 0;
    virtual bool write(const ::cw::lib::String& text) = 0;
    virtual bool flush() = 0;
    virtual bool close() = 0;
};

class Logger : public ::cw::lib::Thread, public std::enable_shared_from_this<Logger> {
public:
    Logger() = default;
    virtual ~Logger();
    bool m_enable{};
    ::cw::lib::String m_fpath{};
    // PythonのQueue[bool|Tuple|None]に対応する型付きFIFO
    // 。 可変のゲームオブジェクトは、このキューへ入れる前に不変の文字列へ変換する。
    std::mutex m_queue_mutex{};
    std::condition_variable m_queue_condition{};
    std::deque<AdventurerLogQueueRecord> m_records{};
    bool m_started{};
    bool m_stopping{};
    ::cw::lib::Value m_queue{};
    std::function<std::unique_ptr<NativeLogOutput>()> m_native_output_factory{};
    // std::threadのハンドル操作は、FIFOのロックと独立して直列化する。
    std::mutex m_worker_mutex{};
    // 再初期化と起動は制御状態を共有する。外部からの再初期化が終了を待
    // っている間も、 Stopのコールバックは実行できるようにする。
    std::mutex m_lifecycle_mutex{};
    std::thread::id m_worker_id{};
    std::uint64_t m_worker_generation{};
    std::uint64_t m_finished_generation{};
    void initialize(::cw::lib::String arg_fpath, bool arg_enable);
    void start_worker();
    void enqueue(::cw::lib::String arg_text);
    void enqueue_callback(::cw::lib::Value arg_data, ::cw::lib::Callback arg_func);
    void set_enable(bool arg_enable);
    void stop_worker();
    void run();
private:
    void stop_worker_impl(bool destroying);
};

extern ::cw::lib::Integer g_ELAPSE_TIME;
extern ::cw::lib::Integer g_INITIAL;
extern ::cw::lib::Integer g_MESSAGE;
extern ::cw::lib::Integer g_MOTION;
extern ::cw::lib::Integer g_MOTION_IN_BATTLE;
extern ::cw::lib::Integer g_SEPARATOR;
extern ::cw::lib::Integer g_SYSTEM;
extern ::cw::lib::Integer g_VOID;
extern ::cw::lib::Value g__LogType;

::cw::lib::Value AdventurerLogger_start_scenario_lambda_line_94(::cw::lib::Value arg_lines);
::cw::lib::Value AdventurerLogger_end_scenario_lambda_line_133(::cw::lib::Value arg_s);
::cw::lib::Value AdventurerLogger_end_scenario_lambda_line_136(::cw::lib::Value arg_s);
::cw::lib::Value AdventurerLogger_gameover_lambda_line_144(::cw::lib::Value arg_s);
::cw::lib::Value AdventurerLogger_f9_lambda_line_148(::cw::lib::Value arg_s);
::cw::lib::Value AdventurerLogger__put_lambda_line_177(::cw::lib::Value arg_s);
::cw::lib::Value AdventurerLogger_show_message_lambda_line_183(::cw::lib::Value arg_mwin);
::cw::lib::String AdventurerLogger_click_menucard_click_menucard(::cw::lib::String arg_name);
::cw::lib::Value AdventurerLogger_rename_party_lambda_line_203(::cw::lib::Value arg_name);
::cw::lib::Value AdventurerLogger_start_battle_lambda_line_206(::cw::lib::Value arg_dummy);
::cw::lib::Value AdventurerLogger_end_battle_lambda_line_211(::cw::lib::Value arg_dummy);
::cw::lib::String AdventurerLogger_runaway_runaway(std::tuple<::cw::lib::String, bool> arg_params);
::cw::lib::Value AdventurerLogger_start_round_lambda_line_230(::cw::lib::Value arg_roundval);
::cw::lib::String AdventurerLogger_use_card_use_card(std::tuple<::cw::lib::String, ::cw::lib::String, bool, std::optional<::cw::lib::String>, ::cw::lib::String, bool> arg_params);
::cw::lib::String AdventurerLogger_avoid_avoid(std::tuple<::cw::lib::String, bool> arg_params);
::cw::lib::String AdventurerLogger_noeffect_noeffect(std::tuple<::cw::lib::String, bool> arg_params);
::cw::lib::String AdventurerLogger_effect_failed_effect_failed(std::tuple<::cw::lib::String, bool> arg_params);
::cw::lib::String AdventurerLogger_heal_motion_heal_motion(std::tuple<::cw::lib::String, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, bool> arg_params);
::cw::lib::String AdventurerLogger_damage_motion_damage_motion(std::tuple<::cw::lib::String, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, bool> arg_params);
::cw::lib::String AdventurerLogger_damage_motion_dissleep_func(std::tuple<::cw::lib::String, bool> arg_params);
::cw::lib::String AdventurerLogger_paralyze_motion_paralyze_motion(std::tuple<::cw::lib::String, ::cw::lib::Integer, ::cw::lib::Integer, bool> arg_params);
::cw::lib::String AdventurerLogger_poison_motion_poison_motion(std::tuple<::cw::lib::String, ::cw::lib::Integer, ::cw::lib::Integer, bool> arg_params);
::cw::lib::String AdventurerLogger_getskillpower_motion_getskillpower_motion(std::tuple<::cw::lib::String, ::cw::lib::Integer, bool> arg_params);
::cw::lib::String AdventurerLogger_loseskillpower_motion_loseskillpower_motion(std::tuple<::cw::lib::String, ::cw::lib::Integer, bool> arg_params);
::cw::lib::String AdventurerLogger_mentality_motion_mentality_motion(std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::Integer, ::cw::lib::String, ::cw::lib::Integer, bool> arg_params);
::cw::lib::String AdventurerLogger_bind_motion_bind_motion(std::tuple<::cw::lib::String, bool> arg_params);
::cw::lib::String AdventurerLogger_disbind_motion_disbind_motion(std::tuple<::cw::lib::String, bool> arg_params);
::cw::lib::String AdventurerLogger_silence_motion_silence_motion(std::tuple<::cw::lib::String, bool> arg_params);
::cw::lib::String AdventurerLogger_dissilence_motion_dissilence_motion(std::tuple<::cw::lib::String, bool> arg_params);
::cw::lib::String AdventurerLogger_faceup_motion_faceup_motion(std::tuple<::cw::lib::String, bool> arg_params);
::cw::lib::String AdventurerLogger_facedown_motion_facedown_motion(std::tuple<::cw::lib::String, bool> arg_params);
::cw::lib::String AdventurerLogger_antimagic_motion_antimagic_motion(std::tuple<::cw::lib::String, bool> arg_params);
::cw::lib::String AdventurerLogger_disantimagic_motion_disantimagic_motion(std::tuple<::cw::lib::String, bool> arg_params);
::cw::lib::String AdventurerLogger_vanishtarget_motion_vanishtarget_motion(std::tuple<::cw::lib::String, bool, bool> arg_params);
::cw::lib::String AdventurerLogger_vanishcard_motion_vanishcard_motion(std::tuple<::cw::lib::String, bool> arg_params);
::cw::lib::String AdventurerLogger_vanishbeast_motion_vanishbeast_motion(std::tuple<::cw::lib::String, bool> arg_params);
::cw::lib::String AdventurerLogger__deal_motion_deal_motion(std::tuple<::cw::lib::String, ::cw::lib::String, bool> arg_params);
std::optional<::cw::lib::String> AdventurerLogger_dealskillcard_motion_dealskillcard_motion(std::tuple<::cw::lib::String, bool, bool, bool> arg_params);
::cw::lib::String AdventurerLogger_cancelaction_motion_cancelaction_motion(std::tuple<::cw::lib::String, bool> arg_params);
::cw::lib::String AdventurerLogger_summonbeast_motion_summonbeast_motion(std::tuple<::cw::lib::String, std::shared_ptr<::cw::data::CWPyElement>, bool> arg_params);
::cw::lib::String AdventurerLogger_poison_damage_poison_damage(std::tuple<::cw::lib::String, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> arg_params);
::cw::lib::Value AdventurerLogger_recover_poison_lambda_line_742(::cw::lib::Value arg_name);
::cw::lib::Value AdventurerLogger_recover_paralyze_lambda_line_745(::cw::lib::Value arg_name);
::cw::lib::Value AdventurerLogger_recover_bind_lambda_line_748(::cw::lib::Value arg_name);
::cw::lib::Value AdventurerLogger_recover_silence_lambda_line_751(::cw::lib::Value arg_name);
::cw::lib::Value AdventurerLogger_recover_faceup_lambda_line_754(::cw::lib::Value arg_name);
::cw::lib::Value AdventurerLogger_recover_antimagic_lambda_line_757(::cw::lib::Value arg_name);
::cw::lib::Value AdventurerLogger_recover_mentality_lambda_line_760(::cw::lib::Value arg_name);
::cw::lib::Value AdventurerLogger_recover_enhance_act_lambda_line_763(::cw::lib::Value arg_name);
::cw::lib::Value AdventurerLogger_recover_enhance_avo_lambda_line_766(::cw::lib::Value arg_name);
::cw::lib::Value AdventurerLogger_recover_enhance_res_lambda_line_769(::cw::lib::Value arg_name);
::cw::lib::Value AdventurerLogger_recover_enhance_def_lambda_line_772(::cw::lib::Value arg_name);
void main();
}

