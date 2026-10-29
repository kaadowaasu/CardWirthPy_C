#pragma once

// cw/skin/convert.py のConverter宣言。Win32リソースを読み、SkinBase XMLを派生スキンへ変換する。
#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"
#include <atomic>
#include <functional>

namespace cw::skin::convert {
class Converter : public ::cw::lib::Thread {
public:
    Converter() = default;
    explicit Converter(::cw::lib::String arg_exe) { initialize(std::move(arg_exe)); }
    ~Converter() override;
    std::map<::cw::lib::String, std::shared_ptr<::cw::data::CWPyElementTree>> m_actioncard{};
    std::optional<::cw::lib::String> m_adventurersinn{};
    bool m_complete{};
    std::atomic<::cw::lib::Integer> m_curnum{};
    std::shared_ptr<::cw::data::CWPyElementTree> m_data{};
    ::cw::lib::String m_datadir{};
    ::cw::lib::String m_errormessage{};
    ::cw::lib::String m_exe{};
    ::cw::lib::Bytes m_exebinary{};
    bool m_failure{};
    std::map<::cw::lib::String, std::shared_ptr<::cw::data::CWPyElementTree>> m_gameover{};
    ::cw::lib::Integer m_initialcash{};
    ::cw::lib::Integer m_maximum{};
    ::cw::lib::String m_message{};
    std::optional<::cw::lib::String> m_partyinfo_res{};
    std::optional<std::shared_ptr<::cw::skin::win32res::Win32Res>> m_res{};
    std::map<::cw::lib::String, std::shared_ptr<::cw::data::CWPyElementTree>> m_scenario{};
    ::cw::lib::String m_scenariodir{};
    ::cw::lib::String m_skindirname{};
    ::cw::lib::String m_skintype{};
    std::map<::cw::lib::String, std::shared_ptr<::cw::data::CWPyElementTree>> m_specialcard{};
    std::map<::cw::lib::String, std::shared_ptr<::cw::data::CWPyElementTree>> m_title{};
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> m_version{1, 2, 8, 0};
    std::map<::cw::lib::String, std::shared_ptr<::cw::data::CWPyElementTree>> m_yado{};
    ::cw::lib::String m_yadodir{};
    void initialize(::cw::lib::String arg_exe);
    void py_del();
    void dispose();
    void init(::cw::lib::String arg_exe);
    std::map<::cw::lib::String, std::shared_ptr<::cw::data::CWPyElementTree>> _get_resources(::cw::lib::String arg_dpath);
    void _write_data(::cw::lib::String arg_dpath, std::map<::cw::lib::String, std::shared_ptr<::cw::data::CWPyElementTree>> arg_table);
    ::cw::lib::String find_skinname();
    ::cw::lib::String find_description();
    ::cw::lib::String find_datadir();
    ::cw::lib::String find_scenariodir();
    ::cw::lib::String find_yadodir();
    ::cw::lib::String find_type();
    ::cw::lib::String find_author();
    ::cw::lib::Integer find_initialcash();
    void _get_features();
    void _get_sounds();
    void _get_partyinfo();
    void _get_cards();
    void _get_bgs();
    void _get_messages();
    std::tuple<::cw::lib::String, ::cw::lib::Integer> _get_text(::cw::lib::Integer arg_index, bool arg_cutzero);
    using ProgressReport=std::function<void(::cw::lib::Integer, ::cw::lib::String)>;
    void run();
    // 変換ワーカーで呼ぶ。受信側が同期された UI 状態を保持する。
    void run(const ProgressReport& report);
};

extern std::map<::cw::lib::String, ::cw::lib::String> g_CURTBL;
extern std::map<::cw::lib::String, ::cw::lib::String> g_IMGTBL;
extern std::map<::cw::lib::String, ::cw::lib::String> g_IMGTBL_C;

::cw::lib::Integer Converter__get_features_set_params(std::shared_ptr<::cw::data::CWPyElement> arg_data, ::cw::lib::Integer arg_index, bool arg_isnature, std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String> arg_slist, ::cw::lib::Real arg_sperb);
void Converter__get_sounds_get_keybefore(std::shared_ptr<::cw::data::CWPyElement> arg_e, ::cw::lib::Bytes arg_key, ::cw::lib::Integer arg_length, ::cw::lib::Integer arg_less);
void Converter__get_sounds_get_keyafter(std::shared_ptr<::cw::data::CWPyElement> arg_e, ::cw::lib::Bytes arg_key, ::cw::lib::Integer arg_length, ::cw::lib::Integer arg_than);
::cw::lib::Integer Converter__get_cards_get_actioncard(::cw::lib::String arg_cardkey, ::cw::lib::Integer arg_index, ::cw::lib::Integer arg_keycodenum);
::cw::lib::Integer Converter__get_cards_get_menucard(std::vector<std::tuple<std::shared_ptr<::cw::data::CWPyElementTree>, ::cw::lib::Integer>> arg_area, ::cw::lib::Integer arg_index);
void main();
}

