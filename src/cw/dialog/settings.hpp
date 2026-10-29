#pragma once

// UIの実装は一つに統一し、
// 別経路の実装を設けない。

// cw/dialog/settings.py から移植した C++ の設定ダ
// イアログモデル。wx/SDL のウィジェットは compat の値・Window
// モデルで表し、状態と保存の動作を UI バックエンドから分離する。
#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"
#include "cw/dialog/presentation.hpp"
#include "cw/dialog/readonly_text.hpp"
#include "cw/setting.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>

namespace cw::dialog::settings {
class SettingsDialog;
struct SoundFontTable : public ::cw::lib::Object {
    std::vector<std::tuple<::cw::lib::String, bool, ::cw::lib::Integer>> m_rows{};
};

// 設定モデル用のホストサービス。インストール済みフォントの列挙と実
// 行時更新のフックだけを提供する。ファイル・フォルダー取得は SettingsDialog
// の共通非同期文書境界だけが行い、設定画面は一つの共
// 通 DialogPresentation を使う。
class SettingsHostServices : public ::cw::lib::Object {
public:
    SettingsHostServices() = default;
    virtual ~SettingsHostServices() = default;

    virtual std::vector<::cw::lib::String> enumerate_font_faces() const;

    // 実行時更新のフックは Engine のサービスを呼ぶだけで、UI 配置は所有しない。
    virtual void restart_soundfonts();
    // Python の効果テーブル再読み込みには、確定前に存在したかの明示的な情報
    // が必要。既存のホスト・呼び出し元のため、引数なしのフックも使えるようにする。
    virtual void restart_soundfonts(bool arg_had_soundfonts);
    virtual void refresh_cards(bool arg_menu_cards);
    virtual void refresh_statusbar();

};

std::shared_ptr<SettingsHostServices> get_host_services();

struct FontSettingRow {
    ::cw::lib::String m_fonttype{};
    ::cw::lib::String m_face{};
    ::cw::lib::Integer m_pixels{};
    bool m_bold{};
    bool m_bold_upscr{};
    bool m_italic{};
};

class AudioSettingPanel : public ::cw::lib::Object {
public:
    SettingsDialog* m_settings_parent{};
    void add_soundfont_files(const std::vector<::cw::lib::String>& paths);
    AudioSettingPanel() = default;
    virtual ~AudioSettingPanel() = default;
    ::cw::lib::Value m_box_gene{};
    ::cw::lib::Value m_box_master{};
    ::cw::lib::Value m_box_midi{};
    ::cw::lib::Value m_box_music{};
    ::cw::lib::Value m_box_sound{};
    ::cw::lib::Value m_box_soundfont{};
    ::cw::lib::Value m_btn_addsoundfont{};
    ::cw::lib::Value m_btn_downsoundfont{};
    ::cw::lib::Value m_btn_rmvsoundfont{};
    ::cw::lib::Value m_btn_upsoundfont{};
    ::cw::lib::Value m_cb_playbgm{};
    ::cw::lib::Value m_cb_playsound{};
    ::cw::lib::Value m_grid_soundfont{};
    ::cw::lib::Value m_sl_master{};
    ::cw::lib::Value m_sl_midi{};
    ::cw::lib::Value m_sl_music{};
    ::cw::lib::Value m_sl_sound{};
    // wx.grid の代替となる純粋なSoundFont行モデル。SDL UI接続時に表へ投影する。
    std::vector<std::tuple<::cw::lib::String, bool, ::cw::lib::Integer>> m__soundfonts{};
    std::vector<::cw::lib::Integer> m__selected_soundfont_rows{};
    bool m__remove_soundfont_enabled{};
    bool m__up_soundfont_enabled{};
    bool m__down_soundfont_enabled{};
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent);
    void load(std::shared_ptr<::cw::setting::Setting> arg_setting);
    void init_values(std::shared_ptr<::cw::setting::Setting> arg_setting);
    void _init_soundfont(std::vector<std::tuple<::cw::lib::String, bool, ::cw::lib::Integer>> arg_soundfonts);
    void set_soundfont(::cw::lib::Integer arg_row, std::tuple<::cw::lib::String, bool, ::cw::lib::Integer> arg_soundfont);
    std::tuple<::cw::lib::String, bool, ::cw::lib::Integer> get_soundfont(::cw::lib::Integer arg_row);
    void _bind();
    void _do_layout();
    void OnGridRangeSelect(std::shared_ptr<::cw::lib::Window> arg_event);
    void _select_changed_soundfonts();
    void OnAddSoundFontBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnRemoveSoundFontBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnUpSoundFontBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnDownSoundFontBtn(std::shared_ptr<::cw::lib::Window> arg_event);
};

class DrawingSettingPanel : public ::cw::lib::Object {
public:
    DrawingSettingPanel() = default;
    virtual ~DrawingSettingPanel() = default;
    ::cw::lib::Value m__for_local{};
    ::cw::lib::Value m__get_localsettings{};
    // ネイティブダイアログが Python の _get_localsettings 相当のコール
    // バックを渡した場合は保持する。互換モデルは指定がなければ cwpy.setting を使う。
    std::optional<::cw::lib::Callback> m__get_localsettings_cb{};
    ::cw::lib::Value m_box_blcurtain{};
    ::cw::lib::Value m_box_curtain{};
    ::cw::lib::Value m_box_fscrback{};
    ::cw::lib::Value m_box_gene{};
    ::cw::lib::Value m_box_mframe{};
    ::cw::lib::Value m_box_mwin{};
    ::cw::lib::Value m_cb_important{};
    ::cw::lib::Value m_cb_smooth_bg{};
    ::cw::lib::Value m_cb_smoothing_card_down{};
    ::cw::lib::Value m_cb_smoothing_card_up{};
    ::cw::lib::Value m_cb_whitecursor{};
    ::cw::lib::Value m_ch_fscrbacktype{};
    ::cw::lib::Value m_copybtn{};
    ::cw::lib::Value m_cs_blcurtain{};
    ::cw::lib::Value m_cs_blframe{};
    ::cw::lib::Value m_cs_blwin{};
    ::cw::lib::Value m_cs_curtain{};
    ::cw::lib::Value m_cs_allengineframe{};
    ::cw::lib::Value m_sc_allengineframe{};
    ::cw::lib::Value m_cs_mframe{};
    ::cw::lib::Value m_cs_mwin{};
    ::cw::lib::Value m_initbtn{};
    ::cw::lib::Value m_ref_fscrbackfile{};
    ::cw::lib::Value m_sc_blcurtain{};
    ::cw::lib::Value m_sc_curtain{};
    ::cw::lib::Value m_sc_mframe{};
    ::cw::lib::Value m_sc_mwin{};
    std::shared_ptr<::cw::dialog::settings::SpeedPanel> m_speed{};
    ::cw::lib::Value m_st_blcurtain{};
    ::cw::lib::Value m_st_blcurtain2{};
    ::cw::lib::Value m_st_blframe{};
    ::cw::lib::Value m_st_blwin{};
    ::cw::lib::Value m_st_curtain{};
    ::cw::lib::Value m_st_curtain2{};
    ::cw::lib::Value m_st_mframe{};
    ::cw::lib::Value m_st_mframe2{};
    ::cw::lib::Value m_st_mwin{};
    ::cw::lib::Value m_st_mwin2{};
    ::cw::lib::Value m_tx_fscrbackfile{};
    // 描画ペインの有効状態。全体設定のペインは最初の Presentation
    // から操作できる。ローカルのペインは initialize/_update_enabled
    // で、スキン設定の上書きチ
    // ェックに従って更新する。
    bool m__controls_enabled{true};
    bool m__fullscreen_file_enabled{};
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, bool arg_for_local, std::optional<::cw::lib::Callback> arg_get_localsettings, bool arg_use_copybase);
    void load(std::optional<std::shared_ptr<::cw::setting::Setting>> arg_setting, std::shared_ptr<::cw::setting::LocalSetting> arg_local);
    void init_values(std::optional<std::shared_ptr<::cw::setting::Setting>> arg_setting, std::shared_ptr<::cw::setting::LocalSetting> arg_local);
    std::tuple<bool, bool, bool> apply_localsettings(std::shared_ptr<::cw::setting::LocalSetting> arg_local);
    void _bind();
    void _do_layout();
    void OnFullScreenBackgroundType(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnImportant(std::shared_ptr<::cw::lib::Window> arg_event);
    void _update_enabled();
    void OnInitValue(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnCopyBase(std::shared_ptr<::cw::lib::Window> arg_event);
};

class ExpandPanel : public ::cw::lib::Object {
public:
    ExpandPanel() = default;
    virtual ~ExpandPanel() = default;
    bool m__expand_enable{};
    ::cw::lib::Value m_cb_fullscreen{};
    ::cw::lib::Value m_cb_smoothexpand{};
    ::cw::lib::Value m_ch_expanddrawing{};
    ::cw::lib::Value m_ln_expand{};
    ::cw::lib::Value m_options{};
    ::cw::lib::Value m_sl_expand{};
    ::cw::lib::Value m_st_expand{};
    ::cw::lib::Value m_st_expandscr{};
    ::cw::lib::Value m_st_expandwin{};
    ::cw::lib::Integer m__expand_max{100};
    void initialize(std::shared_ptr<::cw::lib::Panel> arg_parent, bool arg_options);
    void _bind();
    void load(std::shared_ptr<::cw::setting::Setting> arg_setting);
    void make_expandinfo();
    void OnExpandChange(std::shared_ptr<::cw::lib::Window> arg_event);
    void _do_layout();
    void apply_expand(std::shared_ptr<::cw::setting::Setting> arg_setting);
    void copy_values(std::shared_ptr<::cw::dialog::settings::ExpandPanel> arg_expand);
};

class FontSettingPanel : public ::cw::lib::Object {
public:
    FontSettingPanel() = default;
    virtual ~FontSettingPanel() = default;
    ::cw::lib::Value m__example_panel{};
    std::vector<::cw::lib::Value> m__fontface_array{};
    ::cw::lib::Value m__for_local{};
    ::cw::lib::Value m__get_localsettings{};
    ::cw::lib::Value m__has_default{};
    ::cw::lib::Value m__last_face{};
    std::vector<::cw::lib::String> m__types{};
    std::vector<::cw::lib::String> m__types_with_inherit{};
    ::cw::lib::Value m_base{};
    std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String> m_bases{};
    ::cw::lib::Value m_box_base{};
    ::cw::lib::Value m_box_edit_example{};
    ::cw::lib::Value m_box_example{};
    ::cw::lib::Value m_box_gene{};
    ::cw::lib::Value m_box_type{};
    ::cw::lib::Value m_btn_init_example{};
    ::cw::lib::Value m_cb_bordering_cardname{};
    ::cw::lib::Value m_cb_decorationfont{};
    ::cw::lib::Value m_cb_fontsmoothingcardname{};
    ::cw::lib::Value m_cb_fontsmoothingmessage{};
    ::cw::lib::Value m_cb_fontsmoothingstatusbar{};
    ::cw::lib::Value m_cb_important{};
    ::cw::lib::Value m_choicebases{};
    ::cw::lib::Value m_choicetypes{};
    ::cw::lib::Value m_copybtn{};
    ::cw::lib::Value m_initbtn{};
    std::set<::cw::lib::String> m_msg_exfonttypes{};
    std::map<::cw::lib::String, ::cw::lib::String> m__basefont_rows{};
    std::map<::cw::lib::String, ::cw::dialog::settings::FontSettingRow> m__fonttype_rows{};
    std::map<::cw::lib::String, ::cw::dialog::settings::FontSettingRow> m__msg_exfont_rows{};
    std::vector<::cw::lib::String> m__bases_order{};
    std::vector<::cw::lib::String> m__fontfaces{};
    std::vector<bool> m__has_default_faces{};
    bool m__controls_enabled{true};
    std::optional<::cw::lib::Callback> m__get_localsettings_cb{};
    ::cw::lib::Integer m__base_cursor{-1};
    ::cw::lib::Integer m__type_cursor{-1};
    ::cw::lib::Value m_sc_example{};
    ::cw::lib::Value m_st_example{};
    ::cw::lib::String m_str_default{};
    ::cw::lib::Value m_tx_example{};
    ::cw::lib::Value m_type{};
    std::map<::cw::lib::String, ::cw::lib::String> m_typenames{};
    std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String> m_types{};
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, bool arg_for_local, std::optional<::cw::lib::Callback> arg_get_localsettings, bool arg_use_copybase);
    void load(std::optional<std::shared_ptr<::cw::setting::Setting>> arg_setting, std::shared_ptr<::cw::setting::LocalSetting> arg_local);
    void update_example();
    void _update_rowcolour(::cw::lib::String arg_fonttype, ::cw::lib::Integer arg_row);
    void init_values(std::optional<std::shared_ptr<::cw::setting::Setting>> arg_setting, std::shared_ptr<::cw::setting::LocalSetting> arg_local);
    std::tuple<bool, bool, bool, bool> apply_localsettings(std::shared_ptr<::cw::setting::LocalSetting> arg_local);
    void _bind();
    void _select_base(::cw::lib::Integer arg_i);
    void OnExampleText(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnInitExample(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnCellChangeBase(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnSelectFontBase(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnEditorCreatedBase(std::shared_ptr<::cw::lib::Window> arg_event);
    ::cw::lib::String get_basefontface(::cw::lib::String arg_fonttype);
    void _select_type(::cw::lib::Integer arg_i);
    void OnCellChangeType(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnSelectFontType(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnEditorCreatedType(std::shared_ptr<::cw::lib::Window> arg_event);
    ::cw::lib::String get_typefontface(::cw::lib::String arg_fonttype, bool arg_refeditors);
    void _do_layout();
    void OnImportant(std::shared_ptr<::cw::lib::Window> arg_event);
    void _update_enabled();
    void OnInitValue(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnCopyBase(std::shared_ptr<::cw::lib::Window> arg_event);
};

class GeneralSettingPanel : public ::cw::lib::Object {
public:
    GeneralSettingPanel() = default;
    virtual ~GeneralSettingPanel() = default;
    ::cw::lib::Value m_box_expandmode{};
    ::cw::lib::Value m_box_gene{};
    ::cw::lib::Value m_box_messagelog{};
    ::cw::lib::Value m_box_party{};
    ::cw::lib::Value m_box_skin{};
    ::cw::lib::Value m_box_ss{};
    ::cw::lib::Value m_box_tablet{};
    ::cw::lib::Value m_cb_autosavepartyrecord{};
    ::cw::lib::Value m_cb_can_repeatlclick{};
    ::cw::lib::Value m_cb_debug{};
    ::cw::lib::Value m_cb_enabled_right_flick{};
    ::cw::lib::Value m_cb_initmoneyisinitialcash{};
    ::cw::lib::Value m_cb_nolevelup{};
    ::cw::lib::Value m_cb_overwritepartyrecord{};
    ::cw::lib::Value m_cb_show_tiles{};
    ::cw::lib::Value m_cb_sswithstatusbar{};
    ::cw::lib::Value m_ch_messagelog_type{};
    ::cw::lib::Value m_ch_ssinfocolor{};
    ::cw::lib::Value m_ch_startupscene{};
    std::shared_ptr<::cw::dialog::settings::ExpandPanel> m_expand{};
    ::cw::lib::Value m_ref_ssinfobackimage{};
    ::cw::lib::Value m_sc_backlogmax{};
    ::cw::lib::Value m_sc_initmoneyamount{};
    std::shared_ptr<::cw::dialog::settings::SkinPanel> m_skin{};
    ::cw::lib::ValueSet m_ss_tx{};
    ::cw::lib::ValueMap m_ssdic{};
    ::cw::lib::Value m_ssinsmenu{};
    ::cw::lib::Value m_sstoolbar{};
    // UI状態をSDLへ橋渡しするまでの純粋ロジック用フラグ。
    bool m__money_enabled{};
    bool m__overwrite_enabled{};
    bool m__ss_toolbar_enabled{};
    ::cw::lib::Value m_st_backlogmax{};
    ::cw::lib::Value m_st_cardssfnameformat{};
    ::cw::lib::Value m_st_initmoneyamount{};
    ::cw::lib::Value m_st_messagelog_type{};
    ::cw::lib::Value m_st_ssfnameformat{};
    ::cw::lib::Value m_st_ssinfo_brackets{};
    ::cw::lib::Value m_st_ssinfobackimage{};
    ::cw::lib::Value m_st_startupscene{};
    ::cw::lib::Value m_ti_ssins{};
    ::cw::lib::Value m_tx_cardssfnameformat{};
    ::cw::lib::Value m_tx_ssfnameformat{};
    ::cw::lib::Value m_tx_ssinfobackimage{};
    ::cw::lib::Value m_tx_ssinfoformat{};
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent);
    void _bind();
    void load(std::shared_ptr<::cw::setting::Setting> arg_setting);
    void init_values(std::shared_ptr<::cw::setting::Setting> arg_setting);
    void OnSSTool(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnSSFocus(std::shared_ptr<::cw::lib::Window> arg_event);
    void _ss_focus();
    void OnAutoSavePartyRecord(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnInitMoneyIsInitialCash(std::shared_ptr<::cw::lib::Window> arg_event);
    void _do_layout();
};

class SettingsDialog;

class ScenarioSettingPanel : public ::cw::lib::Object {
public:
    ScenarioSettingPanel() = default;
    virtual ~ScenarioSettingPanel() = default;
    ::cw::lib::Value m_box_application{};
    ::cw::lib::Value m_box_folderoftype{};
    ::cw::lib::Value m_box_gene{};
    ::cw::lib::Value m_btn_constructdb{};
    ::cw::lib::Value m_btn_downfolder{};
    ::cw::lib::Value m_btn_reffolder{};
    ::cw::lib::Value m_btn_removefolder{};
    ::cw::lib::Value m_btn_upfolder{};
    ::cw::lib::Value m_cb_can_installscenariofromdrop{};
    ::cw::lib::Value m_cb_delete_sourceafterinstalled{};
    ::cw::lib::Value m_cb_open_lastscenario{};
    ::cw::lib::Value m_cb_selectscenariofromtype{};
    ::cw::lib::Value m_cb_show_paperandtree{};
    ::cw::lib::Value m_cb_write_playlog{};
    ::cw::lib::Value m_celleditor{};
    ::cw::lib::Value m_grid_folderoftype{};
    ::cw::lib::Value m_ref_editor{};
    ::cw::lib::Value m_ref_filer_dir{};
    ::cw::lib::Value m_ref_filer_file{};
    ::cw::lib::Value m_st_editor{};
    ::cw::lib::Value m_st_filer_dir{};
    ::cw::lib::Value m_st_filer_file{};
    ::cw::lib::Value m_tx_editor{};
    ::cw::lib::Value m_tx_filer_dir{};
    ::cw::lib::Value m_tx_filer_file{};
    // 末尾の空行を含むスキンタイプ別フォルダ行。設定反映時にSettingへ変換する。
    std::vector<std::tuple<::cw::lib::String, ::cw::lib::String>> m__folderoftype{};
    ::cw::lib::Integer m__folder_cursor{-1};
    bool m__ref_folder_enabled{};
    bool m__remove_folder_enabled{};
    bool m__up_folder_enabled{};
    bool m__down_folder_enabled{};
    std::shared_ptr<::cw::dialog::editscenariodb::ConstructScenarioDB> m_scenariodb_dialog{};
    SettingsDialog* m_settings_parent{};
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent);
    void load(std::shared_ptr<::cw::setting::Setting> arg_setting);
    void init_values(std::shared_ptr<::cw::setting::Setting> arg_setting);
    void OnGirdSelectCell(std::shared_ptr<::cw::lib::Window> arg_event);
    void _select_changed_folderoftype();
    void _bind();
    void _do_layout();
    void OnGridCellChange(std::shared_ptr<::cw::lib::Window> arg_event);
    void _grid_cell_change(::cw::lib::Integer arg_row, ::cw::lib::Integer arg_col);
    void OnRefFolderBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void apply_folder_choice(::cw::lib::Integer row, ::cw::lib::String path);
    void OnRemoveFolderBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnUpFolderBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnDownFolderBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnConstructDBBtn(std::shared_ptr<::cw::lib::Window> arg_event);
};

// Python の wx ダイアログの暗黙のライフサイクルを、C++ では SettingsDialogState の段階として明示する。
enum class SettingsDialogState {
    Constructing,
    Visible,
    Applying,
    Closing,
    Closed
};
enum class SettingsDocumentAction {
    Load, Save, SoundFonts, ScenarioFolder, FullscreenBackground,
    BackgroundImage, EditorApplication, FolderApplication, FileApplication,
    SkinArchive
};

class SettingsDialog : public ::cw::lib::Object,
                       public std::enable_shared_from_this<SettingsDialog>,
                       public ::cw::dialog::DialogPresentationProvider
                       {
public:
    SettingsDialog() = default;
    virtual ~SettingsDialog() = default;
    bool m_cwpy_debug{};
    // 実際の所有者は m_native_handle と分離して保持する。m_native_handle
    // はネイティブウィンドウ作成後、このダイアログの HWND に置き換わる。
    std::uintptr_t m_parent_native_handle{};
    std::uintptr_t m_native_handle{};
    ::cw::lib::Value m_panel{};
    std::atomic<SettingsDialogState> m_state{SettingsDialogState::Constructing};
    std::atomic_bool m_close_started{false};
    ::cw::lib::Integer m_presentation_tab{};
    ::cw::lib::Integer m_presentation_page{};
    // 論理表示領域は ALL が 770x420、単体版が 600x420。wins() は位置・
    // サイズとフォントの両方に適用する。外側のスクロールバーは連続したポインター操作に対応する。
    ::cw::lib::Integer m_detail_view_x{}, m_detail_view_y{};
    std::shared_ptr<::cw::lib::Surface> m_settings_view_surface;
    std::optional<std::tuple<std::uint64_t,::cw::lib::Integer,::cw::lib::Integer,::cw::lib::Integer>> m_settings_view_key;
    std::uint64_t m_settings_view_revision{};
    ::cw::dialog::DialogPresentation settings_content_presentation();
    ::cw::lib::Integer m_soundfont_scroll{};
    ::cw::lib::Integer m_soundfont_cursor{-1};
    ::cw::lib::Integer m_soundfont_edit_row{-1};
    ::cw::lib::String m_soundfont_edit_text;
    bool m_soundfont_replace_text{};
    int m_audio_slider{-1};
    ::cw::lib::Integer m_scenario_scroll{};
    // シナリオ表・アプリと一般設定欄（6100..6105）で共有す
    // る一行編集欄。ホストに関係なく入力・確定の実装を一つにする。
    int m_scenario_edit{-1};
    ::cw::lib::String m_scenario_edit_text;
    std::size_t m_scenario_caret{};
    bool m_scenario_select_all{};
    bool m_scenario_type_popup{};
    int m_scenario_type_choice{};
    // 簡易・詳細設定で共有する選択・説明の状態。
    int m_settings_popup{};
    int m_settings_choice{};
    ::cw::lib::Integer m_skin_description_scroll{};
    ::cw::lib::Rect m_skin_description_rect{};
    ::cw::lib::Integer m_skin_description_lines{};
    std::optional<std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::Integer,
                             bool, bool, ::cw::lib::Real, ::cw::lib::Integer>> m_skin_description_wrap_key;
    std::vector<::cw::dialog::textedit::Line> m_skin_description_wrapped_lines;
    ::cw::dialog::textedit::ReadonlyText m_skin_description_text;
    int m_expand_focus{-1};
    ::cw::dialog::textedit::ReadonlyText m_version_text;
    ::cw::lib::Integer m_font_scroll{};
    int m_font_focus{7000};
    int m_drawing_focus{8000};
    ::cw::lib::Integer m_ui_scroll{};
    ::cw::lib::Integer m_ui_content_height{};
    int m_ui_focus{8};
    std::map<int,::cw::lib::Rect> m_ui_control_rects;
    std::vector<int> m_ui_control_order;
    ::cw::dialog::DialogPresentationModifiers m_presentation_modifiers{};
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent);
    // 設定パネルと同じ描画・入力処理をスキンのノートブック内でも使う。こ
    // の状態オブジェクトを別のダイアログ・ウィンドウとして登録しない。
    void configure_local_panels(std::shared_ptr<DrawingSettingPanel> drawing,
                                std::shared_ptr<FontSettingPanel> fonts);
    void select_local_panel(bool fonts);
    void finish_panel_edit(bool accept);
    [[nodiscard]] bool local_panel_only() const noexcept { return m_local_panel_only; }
    static constexpr int local_panel_top = 44, local_panel_height = 480;
    void _bind();
    void _do_layout();
    bool OnFilterEvent(std::shared_ptr<::cw::lib::Event> arg_event);
    void applied();
    void clear_applied();
    void OnClose(std::shared_ptr<::cw::lib::Window> arg_event);
    void request_apply(bool arg_close_after_apply);
    [[nodiscard]] bool is_applying() const noexcept;
    // 適用で現在のスキンリソースが置き換わる場合がある。そのため Windows
    // の Python のダイアログは、同じコマンドのコールバック内でネイ
    // ティブウィンドウを破棄せず、次のゲームから UI への処理で閉じる。
    void close_after_apply();
    void show_details();
    using DocumentRequest = std::function<std::shared_ptr<::cw::lib::documents::Ticket>(
        const ::cw::lib::documents::Request&, ::cw::lib::documents::Completion)>;
    using DocumentWrite = std::function<std::shared_ptr<::cw::lib::documents::Ticket>(
        const ::cw::lib::documents::Document&,std::vector<std::uint8_t>,::cw::lib::documents::Completion)>;
    bool request_document(SettingsDocumentAction action, DocumentRequest start = {}, DocumentWrite write = {});
    void cancel_document_request();
    [[nodiscard]] bool document_request_pending() const noexcept { return bool(m_document_token); }
    [[nodiscard]] ::cw::dialog::DialogPresentation dialog_presentation() override;
    void dialog_presentation_action(::cw::dialog::DialogPresentationAction action,
                                    int index) override;
    void dialog_presentation_modifiers(::cw::dialog::DialogPresentationModifiers modifiers) override {
        m_presentation_modifiers = modifiers;
    }
    void dialog_presentation_text(const std::string& text, bool composition) override;
    void dialog_presentation_key(::cw::dialog::DialogPresentationKey key, bool pressed,
                                ::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_enter(::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_pointer(::cw::dialog::DialogPresentationAction action, int index,
                                     float x, float y, bool pressed) override;
    void dialog_presentation_hover(float x, float y) override;
    void dialog_presentation_context(float x, float y) override;
    void dialog_presentation_scroll(float x, float y, int lines) override;
    void dialog_presentation_pointer_cancel() override {
        m_audio_slider=-1;
        m_version_text.dragging=false;
        request_dialog_presentation_refresh();
    }
    [[nodiscard]] bool dialog_presentation_finished() const noexcept override;
    void dialog_presentation_complete() override;
private:
    bool m_local_panel_only{};
    std::shared_ptr<int> m_document_token;
    std::shared_ptr<::cw::lib::documents::Ticket> m_document_ticket;
    void finish_document_request(const std::shared_ptr<int>& token,
        SettingsDocumentAction action, ::cw::lib::Integer row,
        ::cw::lib::documents::Result result, DocumentWrite write = {});
    void finish_document_write(const std::shared_ptr<int>& token,::cw::lib::documents::Result result);
    void commit_soundfont_edit();
    void begin_scenario_edit(int field);
    void commit_scenario_edit();
    void finish_apply(bool arg_close_after_apply, bool arg_success,
                      ::cw::lib::String arg_error);
    void close_once();
};

class SettingsPanel : public ::cw::lib::Object {
public:
    SettingsPanel() = default;
    virtual ~SettingsPanel() = default;
    inline static std::shared_ptr<::cw::lib::Window> s_versioninfo{};
    std::shared_ptr<::cw::lib::Window> m_btn_apply{};
    ::cw::lib::Value m_btn_cncl{};
    ::cw::lib::Value m_btn_dflt{};
    ::cw::lib::Value m_btn_load{};
    ::cw::lib::Value m_btn_ok{};
    ::cw::lib::Value m_btn_save{};
    ::cw::lib::Value m_note{};
    std::shared_ptr<::cw::dialog::settings::DrawingSettingPanel> m_pane_draw{};
    std::shared_ptr<::cw::dialog::settings::FontSettingPanel> m_pane_font{};
    std::shared_ptr<::cw::dialog::settings::GeneralSettingPanel> m_pane_gene{};
    std::shared_ptr<::cw::dialog::settings::ScenarioSettingPanel> m_pane_scenario{};
    std::shared_ptr<::cw::dialog::settings::AudioSettingPanel> m_pane_sound{};
    std::shared_ptr<::cw::dialog::settings::UISettingPanel> m_pane_ui{};
    ::cw::dialog::settings::SettingsDialog* m_parent{};
    void initialize(std::shared_ptr<::cw::dialog::settings::SettingsDialog> arg_parent);
    void _bind();
    void load(std::shared_ptr<::cw::setting::Setting> arg_setting);
    void OnSave(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnLoad(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnDefault(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnOk(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnApply(std::shared_ptr<::cw::lib::Window> arg_event);
    void apply(std::shared_ptr<::cw::setting::Setting> arg_setting);
    void OnClose(std::shared_ptr<::cw::lib::Window> arg_event);
    void close();
    void _do_layout();
};

class SimpleSettingsPanel : public ::cw::lib::Object {
public:
    SimpleSettingsPanel() = default;
    virtual ~SimpleSettingsPanel() = default;
    inline static std::shared_ptr<::cw::lib::Window> s_versioninfo{};
    ::cw::lib::Value m_box_audio{};
    ::cw::lib::Value m_box_debug{};
    ::cw::lib::Value m_box_expandmode{};
    ::cw::lib::Value m_box_skin{};
    std::shared_ptr<::cw::lib::Window> m_btn_apply{};
    ::cw::lib::Value m_btn_cncl{};
    ::cw::lib::Value m_btn_details{};
    ::cw::lib::Value m_btn_ok{};
    ::cw::lib::Value m_cb_debug{};
    std::shared_ptr<::cw::lib::Window> m_cb_playbgm{};
    std::shared_ptr<::cw::lib::Window> m_cb_playsound{};
    std::shared_ptr<::cw::lib::Window> m_cb_all_status_dialog{};
    std::shared_ptr<::cw::dialog::settings::ExpandPanel> m_expand{};
    ::cw::lib::Value m_panel{};
    std::shared_ptr<::cw::dialog::settings::SkinPanel> m_skin{};
    std::shared_ptr<::cw::dialog::settings::SpeedPanel> m_speed{};
    ::cw::dialog::settings::SettingsDialog* m_parent{};
    void initialize(std::shared_ptr<::cw::dialog::settings::SettingsDialog> arg_parent);
    void _bind();
    void OnOk(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnApply(std::shared_ptr<::cw::lib::Window> arg_event);
    void apply();
    void OnClose(std::shared_ptr<::cw::lib::Window> arg_event);
    void close();
    void OnDetails(std::shared_ptr<::cw::lib::Window> arg_event);
    void _do_layout();
};

class SkinPanel : public ::cw::lib::Object {
public:
    SkinPanel() = default;
    virtual ~SkinPanel() = default;
    ::cw::lib::ValueSet m_all_skintypes{};
    ::cw::lib::Integer m_basecash{};
    ::cw::lib::Value m_btn_convertskin{};
    ::cw::lib::Value m_btn_copyskin{};
    ::cw::lib::Value m_btn_deleteskin{};
    ::cw::lib::Value m_btn_editskin{};
    ::cw::lib::Value m_btn_installskin{};
    ::cw::lib::Value m_cb_show_allskin{};
    ::cw::lib::Value m_ch_skin{};
    ::cw::lib::Value m_editbuttons{};
    ::cw::lib::Value m_pane_scenario{};
    std::map<::cw::lib::String, std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, bool, ::cw::lib::Integer>> m_skin_summarys{};
    std::vector<::cw::lib::Value> m_skindirs{};
    std::vector<::cw::lib::Value> m_skins{};
    ::cw::lib::Value m_tx_skin{};
    ::cw::dialog::settings::SettingsDialog* m_parent{};
    std::shared_ptr<::cw::dialog::skin::SkinConversionDialog> m_conversion_dialog{};
    std::shared_ptr<::cw::dialog::skin::SkinEditDialog> m_edit_dialog{};
    void initialize(std::shared_ptr<::cw::lib::Panel> arg_parent, bool arg_editbuttons);
    void _bind();
    void update_skins(::cw::lib::String arg_skindirname, bool arg_applied);
    void load_allskins();
    void _load_skinproperties(::cw::lib::String arg_name);
    void OnSkinChoice(std::shared_ptr<::cw::lib::Window> arg_event);
    void _choice_skin(bool arg_init, bool arg_applied);
    std::shared_ptr<::cw::setting::LocalSetting> _get_localsettings();
    void OnInstallSkin(std::shared_ptr<::cw::lib::Window> arg_event);
    bool install_skin_files(const std::vector<::cw::lib::String>& paths);
    void OnConvertSkin(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnEditSkin(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnCopySkin(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnDeleteSkin(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnShowAllSkin(std::shared_ptr<::cw::lib::Window> arg_event);
    void _do_layout();
    bool apply_skin(bool arg_forceupdate);
    void copy_values(std::shared_ptr<::cw::dialog::settings::SkinPanel> arg_skin);
};

class SpeedPanel : public ::cw::lib::Object {
public:
    SpeedPanel() = default;
    virtual ~SpeedPanel() = default;
    ::cw::lib::Value m_battlespeed{};
    ::cw::lib::Value m_box_deal{};
    ::cw::lib::Value m_box_deal_battle{};
    ::cw::lib::Value m_box_msgs{};
    ::cw::lib::Value m_box_tran{};
    ::cw::lib::Value m_cb_use_battlespeed{};
    ::cw::lib::Value m_ch_tran{};
    std::vector<::cw::lib::String> m_choices_tran{};
    ::cw::lib::Value m_sl_deal{};
    ::cw::lib::Value m_sl_deal_battle{};
    ::cw::lib::Value m_sl_msgs{};
    ::cw::lib::Value m_sl_tran{};
    std::vector<::cw::lib::String> m_transitions{};
    bool m__battle_enabled{};
    void initialize(std::shared_ptr<::cw::lib::Panel> arg_parent, bool arg_battlespeed);
    void load(std::shared_ptr<::cw::setting::Setting> arg_setting);
    void _bind();
    void _do_layout();
    void OnUseBattleSpeed(std::shared_ptr<::cw::lib::Window> arg_event);
    bool apply_speed(std::shared_ptr<::cw::setting::Setting> arg_setting);
    void copy_values(std::shared_ptr<::cw::dialog::settings::SpeedPanel> arg_speed);
};

class UISettingPanel : public ::cw::lib::Object {
public:
    UISettingPanel() = default;
    virtual ~UISettingPanel() = default;
    ::cw::lib::Value m_box_card{};
    ::cw::lib::Value m_box_confirm_and_description{};
    ::cw::lib::Value m_box_control{};
    ::cw::lib::Value m_box_dialog{};
    ::cw::lib::Value m_box_save_and_load{};
    ::cw::lib::Value m_box_skip_and_wait{};
    ::cw::lib::Value m_cb_allquickdeal{};
    ::cw::lib::Value m_cb_autoenter_on_sprite{};
    ::cw::lib::Value m_cb_blink_partymoney{};
    ::cw::lib::Value m_cb_blink_statusbutton{};
    ::cw::lib::Value m_cb_can_clicksidesofcardcontrol{};
    ::cw::lib::Value m_cb_can_forwardmessage_with_wheel{};
    ::cw::lib::Value m_cb_can_skipanimation{};
    ::cw::lib::Value m_cb_can_skipwait{};
    ::cw::lib::Value m_cb_can_skipwait_with_wheel{};
    ::cw::lib::Value m_cb_cautionbeforesaving{};
    ::cw::lib::Value m_cb_confirmbeforeusingcard{};
    ::cw::lib::Value m_cb_enlarge_beastcardzoomingratio{};
    ::cw::lib::Value m_cb_noticeimpossibleaction{};
    ::cw::lib::Value m_cb_protect_premiercard{};
    ::cw::lib::Value m_cb_protect_staredcard{};
    ::cw::lib::Value m_cb_quickdeal{};
    ::cw::lib::Value m_cb_revertcardpocket{};
    ::cw::lib::Value m_cb_show_addctrlbtn{};
    ::cw::lib::Value m_cb_show_advancedsettings{};
    ::cw::lib::Value m_cb_show_btndesc{};
    ::cw::lib::Value m_cb_show_cardkind{};
    ::cw::lib::Value m_cb_show_experiencebar{};
    ::cw::lib::Value m_cb_show_personal_cards{};
    ::cw::lib::Value m_cb_show_premiumicon{};
    ::cw::lib::Value m_cb_showallselectedcards{};
    ::cw::lib::Value m_cb_showautobuttoninentrydialog{};
    ::cw::lib::Value m_cb_showbackpackcard{};
    ::cw::lib::Value m_cb_showbackpackcardatend{};
    ::cw::lib::Value m_cb_showlogwithwheelup{};
    ::cw::lib::Value m_cb_showroundautostartbutton{};
    ::cw::lib::Value m_cb_show_all_status_dialog{};
    // CardWirthPy_C_All 専用。右側ショートカットパネルの各枠の選択番号
    // （AllStatusShortcut）を、ステータスダイアログのタブで編集する。
    std::array<::cw::lib::Value, ::cw::setting::kAllStatusShortcutSlots> m_ch_all_shortcuts{};
    // CardWirthPy_C_All 専用。カスタム状態ショートカットに登録する状態（::cw::setting::Setting::m_all_custom_status
    // ）。
    ::cw::lib::String m_all_custom_status{};
    ::cw::lib::Value m_cb_showsavedmessage{};
    ::cw::lib::Value m_cb_skip_pc_click_motion{true};
    ::cw::lib::Value m_cb_statusbarmask{};
    ::cw::lib::Value m_cb_wait_usecard{};
    // 宿ごとの PC の容量は、詳細 UI 設定の下部に置く
    // 。シナリオ設定へ移したり、ALL で除いたりしない。
    ::cw::lib::Value m_cb_pc_card_limits{false};
    ::cw::lib::Value m_sc_pc_skill_limit{static_cast<::cw::lib::Integer>(50)};
    ::cw::lib::Value m_sc_pc_personal_limit{static_cast<::cw::lib::Integer>(50)};
    ::cw::lib::Value m_sc_pc_beast_limit{static_cast<::cw::lib::Integer>(50)};
    std::weak_ptr<::cw::data::YadoData> m_pc_limits_inn;
    ::cw::lib::Value m_ch_confirm_beforesaving{};
    ::cw::lib::Value m_ch_confirm_dumpcard{};
    ::cw::lib::Value m_ch_show_statustime{};
    ::cw::lib::Value m_panel_confirm_beforesaving{};
    ::cw::lib::Value m_panel_confirm_dumpcard{};
    ::cw::lib::Value m_panel_radius_notdetectmovement{};
    ::cw::lib::Value m_panel_show_statustime{};
    ::cw::lib::Value m_sc_radius_notdetectmovement{};
    ::cw::lib::Value m_st_confirm_beforesaving{};
    ::cw::lib::Value m_st_confirm_dumpcard{};
    ::cw::lib::Value m_st_panel_radius_notdetectmovement{};
    ::cw::lib::Value m_st_panel_radius_notdetectmovement_2{};
    ::cw::lib::Value m_st_panel_show_statustime{};
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent);
    void load(std::shared_ptr<::cw::setting::Setting> arg_setting);
    void init_values(std::shared_ptr<::cw::setting::Setting> arg_setting);
    void OnQuickDeal(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnAllQuickDeal(std::shared_ptr<::cw::lib::Window> arg_event);
    void _bind();
    void _do_layout();
};

extern ::cw::lib::Integer g__spin_w_addition;
extern ::cw::lib::Value g_versioninfo;

::cw::lib::Integer _settings_width();
void create_versioninfo(::cw::lib::Value arg_parent);
void apply_levelupparams(bool arg_can_levelup);
void apply_levelupparams_check_levelup(bool arg_can_levelup_old);
void SimpleSettingsPanel_OnOk_func(std::shared_ptr<::cw::dialog::settings::SimpleSettingsPanel> arg_self);
void SimpleSettingsPanel_OnOk_func_func(std::shared_ptr<::cw::dialog::settings::SimpleSettingsPanel> arg_self);
void SettingsPanel_OnOk_func(std::shared_ptr<::cw::dialog::settings::SettingsPanel> arg_self);
void SettingsPanel_OnOk_func_func(std::shared_ptr<::cw::dialog::settings::SettingsPanel> arg_self);
void SettingsPanel_apply_func(::cw::lib::Integer arg_backlogmax);
void SettingsPanel_apply_func_cursor();
void SettingsPanel_apply_func_sfonts();
void SettingsPanel_apply_func_sfonts(bool arg_had_soundfonts);
void SettingsPanel_apply_func1();
void SettingsPanel_apply_func_advlog();
void SettingsPanel_apply_func_personal();
void SettingsPanel_apply_func_autostart();
void SettingsPanel_apply_func_redrawcard();
void SettingsPanel_apply_func_updatebg();
void SettingsPanel_apply_func_updateimg(bool arg_menu_cards);
void SettingsPanel_apply_func_statusbar();
::cw::lib::String SkinPanel_OnInstallSkin_validate_path(::cw::lib::String arg_path);
::cw::lib::Value SkinPanel_OnInstallSkin_lambda_line_1056(::cw::lib::Value arg_path);
void SkinPanel_OnInstallSkin_func(std::shared_ptr<::cw::dialog::settings::SkinPanel> arg_self);
void SkinPanel_OnInstallSkin_func_func(std::shared_ptr<::cw::dialog::settings::SkinPanel> arg_self);
void SkinPanel_apply_skin_update_skin(::cw::lib::String arg_skindirname, bool arg_restartop);
void ExpandPanel_apply_expand_func(::cw::lib::String arg_value_s);
void ScenarioSettingPanel__do_layout_add_application(std::shared_ptr<::cw::lib::Control> arg_ctrl, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos, ::cw::lib::Integer arg_flag);
::cw::lib::Value FontSettingPanel___init___lambda_line_3129();
std::vector<std::shared_ptr<::cw::lib::Window>> FontSettingPanel___init___create_grid(std::shared_ptr<::cw::lib::Window> arg_grid, std::vector<::cw::lib::String> arg_seq, ::cw::lib::Callback arg_faces, ::cw::lib::Integer arg_cols, ::cw::lib::Integer arg_rowlblsize);
bool FontSettingPanel___init___has_defaultfont(::cw::lib::String arg_name);
std::vector<::cw::lib::String> FontSettingPanel___init___faces_from_basefont(::cw::lib::String arg_name);
std::vector<::cw::lib::String> FontSettingPanel___init___get_choices(::cw::lib::String arg_ftype);
void FontSettingPanel_load_create_grid(std::shared_ptr<::cw::lib::Window> arg_grid, std::vector<::cw::lib::String> arg_seq);
void FontSettingPanel_OnSelectFontBase_func(std::shared_ptr<::cw::dialog::settings::FontSettingPanel> arg_self);
void FontSettingPanel_OnSelectFontType_func(std::shared_ptr<::cw::dialog::settings::FontSettingPanel> arg_self);
void main();
}

