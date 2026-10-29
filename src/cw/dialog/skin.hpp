#pragma once

#include <set>

#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"
#include "cw/dialog/presentation.hpp"
#include "cw/dialog/textedit.hpp"
#include <array>
#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace cw::dialog::skin {

using SkinFeatureRow=std::array<::cw::lib::String,12>;
using SkinCardRow=std::array<::cw::lib::String,2>;

// 共通 Presentation がスキン編集を確定した後に使う、実
// 行環境との連携サービス。別の UI の作成・配置・終了は行わない。
struct SkinHostServices {
    std::function<void(::cw::lib::String)> reload_skin{};
    std::function<void()> update_titlebar{};
    std::function<void(bool)> update_vocation120{};
    std::function<void()> update_messagestyle{};
    std::function<void()> update_curtainstyle{};
    std::function<void()> update_fullscreenbackground{};
};

SkinHostServices make_default_skin_host_services();

// UIの実装は一つに統一し、
// 別経路の実装を設けない。
class SkinInfoPanel : public ::cw::lib::Object, public ::cw::dialog::DialogPresentationProvider {
public:
    SkinInfoPanel() = default;
    virtual ~SkinInfoPanel() = default;
    ::cw::lib::Value m_authorctrl{};
    ::cw::lib::Value m_authorlabel{};
    ::cw::lib::Value m_descctrl{};
    ::cw::lib::Value m_desclabel{};
    ::cw::lib::Value m_initialcash{};
    ::cw::lib::Value m_initialcashlabel{};
    ::cw::lib::Value m_namectrl{};
    ::cw::lib::Value m_namelabel{};
    ::cw::lib::Value m_typectrl{};
    ::cw::lib::Value m_typelabel{};
    ::cw::lib::ValueList m_types{};
    ::cw::lib::Value m_vocation120{};
    std::vector<::cw::lib::String> m_type_choices{};
    ::cw::lib::String m_type{};
    ::cw::lib::String m_name{};
    ::cw::lib::String m_author{};
    ::cw::lib::String m_description{};
    ::cw::lib::Integer m_initial_cash{};
    bool m_vocation120_value{};
    void initialize(std::shared_ptr<::cw::lib::Panel> arg_parent);
    void _bind();
    void _do_layout();
    void set_values(::cw::lib::String arg_type, ::cw::lib::String arg_name,
                    ::cw::lib::String arg_author, ::cw::lib::String arg_description,
                    bool arg_vocation120, ::cw::lib::Integer arg_initialcash);
    void validate();
    int m_presentation_height{480};
    bool m_type_popup{};
    void finish_edit();
    [[nodiscard]] ::cw::dialog::DialogPresentation dialog_presentation() override;
    void dialog_presentation_action(::cw::dialog::DialogPresentationAction,int) override;
    void dialog_presentation_text(const std::string&,bool) override;
    void dialog_presentation_text_event(const ::cw::dialog::DialogTextEditingEvent&) override;
    void dialog_presentation_key(::cw::dialog::DialogPresentationKey,bool,::cw::dialog::DialogPresentationModifiers) override;
    void dialog_presentation_enter(::cw::dialog::DialogPresentationModifiers) override;
    void dialog_presentation_modifiers(::cw::dialog::DialogPresentationModifiers value) override { m_modifiers=value; }
    void dialog_presentation_pointer(::cw::dialog::DialogPresentationAction,int,float,float,bool) override;
    void dialog_presentation_pointer_cancel() override { m_dragging=false; }
    void dialog_presentation_scroll(float,float,int) override;
    [[nodiscard]] bool dialog_presentation_finished() const noexcept override { return false; }
    void dialog_presentation_complete() override {}
    [[nodiscard]] bool multiline_focused() const { return m_focus==3; }
    [[nodiscard]] bool has_composition() const { return m_focus>=0 && m_focus<5 && !m_edits[m_focus].composition.empty(); }
    void clear_composition() { if(m_focus>=0 && m_focus<5) m_edits[m_focus].set_composition(""); request_dialog_presentation_refresh(); }
private:
    std::array<::cw::dialog::textedit::Buffer,5> m_edits;
    int m_focus{-1},m_choice{},m_choice_top{};
    bool m_dragging{};
    ::cw::dialog::DialogPresentationModifiers m_modifiers{};
    void sync_edits();
    void publish_edit();
    void focus_edit(int);
    [[nodiscard]] ::cw::lib::Rect edit_rect(int) const;
    [[nodiscard]] std::vector<::cw::dialog::textedit::Line> edit_lines() const;
};

class SkinFeaturePanel : public ::cw::lib::Object {
public:
    SkinFeaturePanel() = default;
    virtual ~SkinFeaturePanel() = default;
    ::cw::lib::Value m_grid{};
    std::vector<SkinFeatureRow> m_rows{};
    std::vector<::cw::lib::String> m_row_labels{};
    std::vector<::cw::lib::String> m_column_labels{};
    bool m_valid{true};
    void initialize(std::shared_ptr<::cw::lib::Panel> arg_parent, std::shared_ptr<::cw::skin::convert::Converter> arg_conv);
    void set_values(std::shared_ptr<::cw::skin::convert::Converter> arg_conv);
    void get_values(std::shared_ptr<::cw::skin::convert::Converter> arg_conv);
    void set_cell(::cw::lib::Integer arg_row, ::cw::lib::Integer arg_col, ::cw::lib::String arg_value);
    ::cw::lib::String get_cell(::cw::lib::Integer arg_row, ::cw::lib::Integer arg_col) const;
    void _do_layout();
};

class SkinSoundPanel : public ::cw::lib::Object {
public:
    SkinSoundPanel() = default;
    virtual ~SkinSoundPanel() = default;
    ::cw::lib::Value m_grid{};
    std::vector<::cw::lib::String> m_values{};
    std::vector<::cw::lib::String> m_row_labels{};
    bool m_valid{true};
    void initialize(std::shared_ptr<::cw::lib::Panel> arg_parent, std::shared_ptr<::cw::skin::convert::Converter> arg_conv);
    void set_values(std::shared_ptr<::cw::skin::convert::Converter> arg_conv);
    void get_values(std::shared_ptr<::cw::skin::convert::Converter> arg_conv);
    void set_cell(::cw::lib::Integer arg_row, ::cw::lib::String arg_value);
    ::cw::lib::String get_cell(::cw::lib::Integer arg_row) const;
    void _do_layout();
};

class SkinMessagePanel : public ::cw::lib::Object {
public:
    SkinMessagePanel() = default;
    virtual ~SkinMessagePanel() = default;
    ::cw::lib::Value m_grid{};
    std::vector<::cw::lib::String> m_values{};
    std::vector<::cw::lib::String> m_row_labels{};
    bool m_valid{true};
    void initialize(std::shared_ptr<::cw::lib::Panel> arg_parent, std::shared_ptr<::cw::skin::convert::Converter> arg_conv);
    void set_values(std::shared_ptr<::cw::skin::convert::Converter> arg_conv);
    void get_values(std::shared_ptr<::cw::skin::convert::Converter> arg_conv);
    void set_cell(::cw::lib::Integer arg_row, ::cw::lib::String arg_value);
    ::cw::lib::String get_cell(::cw::lib::Integer arg_row) const;
    void _do_layout();
};

class SkinCardPanel : public ::cw::lib::Object {
public:
    SkinCardPanel() = default;
    virtual ~SkinCardPanel() = default;
    ::cw::lib::Value m_grid{};
    std::vector<SkinCardRow> m_rows{};
    std::vector<::cw::lib::String> m_row_labels{};
    bool m_valid{true};
    void initialize(std::shared_ptr<::cw::lib::Panel> arg_parent, std::shared_ptr<::cw::skin::convert::Converter> arg_conv);
    void set_values(std::shared_ptr<::cw::skin::convert::Converter> arg_conv);
    void get_values(std::shared_ptr<::cw::skin::convert::Converter> arg_conv);
    void set_cell(::cw::lib::Integer arg_row, ::cw::lib::Integer arg_col, ::cw::lib::String arg_value);
    ::cw::lib::String get_cell(::cw::lib::Integer arg_row, ::cw::lib::Integer arg_col) const;
    void _do_layout();
};

// UIの実装は一つに統一し、
// 別経路の実装を設けない。
class SkinBasePanel : public ::cw::lib::Object, public ::cw::dialog::DialogPresentationProvider,
                      public std::enable_shared_from_this<SkinBasePanel> {
public:
    SkinBasePanel() = default;
    virtual ~SkinBasePanel() = default;
    ::cw::lib::Value m_box_base{};
    ::cw::lib::Value m_box_info{};
    ::cw::lib::Value m_conv{};
    std::shared_ptr<::cw::skin::convert::Converter> m_converter{};
    std::shared_ptr<::cw::lib::Window> m_datactrl{};
    ::cw::lib::Value m_datalabel{};
    ::cw::lib::Value m_dataref{};
    ::cw::lib::Value m_exe{};
    std::shared_ptr<::cw::lib::Window> m_exectrl{};
    ::cw::lib::Value m_exelabel{};
    ::cw::lib::Value m_exeref{};
    std::shared_ptr<SkinInfoPanel> m_info{};
    std::shared_ptr<::cw::lib::Window> m_scenarioctrl{};
    ::cw::lib::Value m_scenariolabel{};
    ::cw::lib::Value m_scenarioref{};
    std::shared_ptr<::cw::lib::Window> m_yadoctrl{};
    ::cw::lib::Value m_yadolabel{};
    ::cw::lib::Value m_yadoref{};
    ::cw::lib::String m_exe_value{};
    ::cw::lib::String m_datadir_value{};
    ::cw::lib::String m_scenariodir_value{};
    ::cw::lib::String m_yadodir_value{};
    ::cw::lib::String m_original_exe{};
    bool m_ok_enabled{};
    bool m_auto_extract_confirmed{};
    std::function<void()> m_on_extracted;
    ::cw::lib::String m_error_message;
    using DocumentRequest=std::function<std::shared_ptr<::cw::lib::documents::Ticket>(
        const ::cw::lib::documents::Request&,::cw::lib::documents::Completion)>;
    bool request_document(int row,DocumentRequest start={});
    void cancel_document_request();
    [[nodiscard]] bool document_request_pending() const { return bool(m_document_token); }
    void finish_edit(bool check_exe);
    void close_panel();
    [[nodiscard]] bool has_focus() const { return m_focus>=0; }
    [[nodiscard]] bool has_composition() const { return m_focus>=0 && !m_edits[m_focus].composition.empty(); }
    void clear_composition();
    [[nodiscard]] ::cw::dialog::DialogPresentation dialog_presentation() override;
    void dialog_presentation_action(::cw::dialog::DialogPresentationAction,int) override;
    void dialog_presentation_text(const std::string&,bool) override;
    void dialog_presentation_text_event(const ::cw::dialog::DialogTextEditingEvent&) override;
    void dialog_presentation_key(::cw::dialog::DialogPresentationKey,bool,::cw::dialog::DialogPresentationModifiers) override;
    void dialog_presentation_modifiers(::cw::dialog::DialogPresentationModifiers value) override { m_modifiers=value; }
    void dialog_presentation_pointer(::cw::dialog::DialogPresentationAction,int,float,float,bool) override;
    void dialog_presentation_pointer_cancel() override { m_dragging=false; }
    void dialog_presentation_hover(float,float) override;
    [[nodiscard]] bool dialog_presentation_finished() const noexcept override { return m_finished; }
    void dialog_presentation_complete() override {}
    void initialize(std::shared_ptr<::cw::lib::Panel> arg_parent, std::shared_ptr<::cw::skin::convert::Converter> arg_conv);
    void _bind();
    void _do_layout();
    void OnLeaveExeCtrl(std::shared_ptr<::cw::lib::Window> arg_event);
    ::cw::lib::String _get_basedir();
    void _selected_exe(::cw::lib::String arg_exe);
    void OnInput(std::shared_ptr<::cw::lib::Window> arg_event);
protected:
    virtual bool confirm_extraction(const ::cw::lib::String& message);
    virtual void extract_executable(const ::cw::lib::String& path);
private:
    std::array<::cw::dialog::textedit::Buffer,4> m_edits;
    std::array<::cw::lib::documents::Document,4> m_access;
    std::shared_ptr<int> m_document_token;
    std::shared_ptr<::cw::lib::documents::Ticket> m_document_ticket;
    int m_focus{-1};
    bool m_dragging{},m_finished{},m_exe_check_pending{};
    ::cw::dialog::DialogPresentationModifiers m_modifiers{};
    ::cw::lib::String& path_value(int);
    void sync_edits();
    void publish_edit();
    void queue_exe_check();
    void report_error(::cw::lib::String);
    void finish_document(const std::shared_ptr<int>&,int,const std::filesystem::path&,
                         ::cw::lib::documents::Result);
};

// UIの実装は一つに統一し、
// 別経路の実装を設けない。
class SkinGridPresentation : public ::cw::dialog::DialogPresentationProvider {
public:
    enum class CellKind { Text, Integer, Decimal };
    struct Column { ::cw::lib::String title; int width; CellKind kind{CellKind::Text}; };
    std::vector<Column> columns;
    std::function<int()> row_count;
    std::function<::cw::lib::String(int)> row_label;
    std::function<::cw::lib::String(int,int)> read_cell;
    std::function<void(int,int,::cw::lib::String)> write_cell;
    int label_width{}; // ゼロなら、共通の行ラベルからサイズを自動計算する。
    static constexpr int column_resize_base=-100,row_resize_base=-1000,row_label_resize=-4;
    [[nodiscard]] bool is_resize_control(int index) const noexcept;
    [[nodiscard]] int row_height(int row) const;
    [[nodiscard]] bool is_column_resize_control(int index) const noexcept;
    static constexpr int row_select_base=-10000,column_select_base=-20000,select_all_control=-5;
    [[nodiscard]] bool is_selection_control(int index) const noexcept;
    [[nodiscard]] bool cell_selected(int row,int column) const;
    [[nodiscard]] bool has_selection() const noexcept { return !m_selected.empty(); }
    [[nodiscard]] bool selection_dragging() const noexcept { return m_selecting; }
    [[nodiscard]] std::optional<::cw::lib::String> selection_text() const;
    std::function<void(const ::cw::lib::String&)> clipboard_write{::cw::dialog::presentation_set_clipboard_text};
    int m_row{},m_column{},m_scroll{},m_horizontal{};
    bool m_editing{};
    ::cw::lib::String m_validation_error;
    bool finish_edit(bool accept);
    void begin_edit();
    [[nodiscard]] ::cw::dialog::DialogPresentation dialog_presentation() override;
    void dialog_presentation_action(::cw::dialog::DialogPresentationAction,int) override;
    void dialog_presentation_text(const std::string&,bool) override;
    void dialog_presentation_text_event(const ::cw::dialog::DialogTextEditingEvent&) override;
    void dialog_presentation_key(::cw::dialog::DialogPresentationKey,bool,::cw::dialog::DialogPresentationModifiers) override;
    void dialog_presentation_enter(::cw::dialog::DialogPresentationModifiers) override;
    void dialog_presentation_modifiers(::cw::dialog::DialogPresentationModifiers value) override { m_modifiers=value; }
    void dialog_presentation_pointer(::cw::dialog::DialogPresentationAction,int,float,float,bool) override;
    void dialog_presentation_pointer_cancel() override;
    void dialog_presentation_scroll(float,float,int) override;
    [[nodiscard]] bool dialog_presentation_finished() const noexcept override { return false; }
    void dialog_presentation_complete() override {}
private:
    ::cw::dialog::textedit::Buffer m_edit;
    ::cw::dialog::DialogPresentationModifiers m_modifiers{};
    bool m_dragging{};
    enum class SelectionKind { Cells, Rows, Columns };
    using Cells=std::set<std::pair<int,int>>;
    Cells m_selected,m_extend_base,m_drag_selection;
    int m_extent_row{},m_extent_column{};
    int m_drag_extent_row{},m_drag_extent_column{};
    bool m_selecting{},m_selection_moved{},m_slow_edit{};
    bool m_suppress_drag_until_release{};
    SelectionKind m_selection_kind{SelectionKind::Cells};
    float m_select_x{},m_select_y{};
    void select_target(int row,int column,SelectionKind kind,
        ::cw::dialog::DialogPresentationModifiers modifiers);
    void extend_selection(int row,int column,SelectionKind kind);
    void clear_selection();
    void select_all();
    void prune_selection();
    void reveal_target(int row,int column);
    std::pair<int,int> point_cell(float x,float y) const;
    bool decode_selection_control(int index,int& row,int& column,SelectionKind& kind) const;
    mutable int m_label_extent{}; // 表示セルごとではなく、描画ごとに一度計測する。
    int m_resize_control{},m_resize_size{};
    std::vector<int> m_row_heights;
    ::cw::lib::Integer m_row_offset{};
    ::cw::lib::Integer row_top(int row) const;
    ::cw::lib::Integer vertical_position() const;
    int row_at(::cw::lib::Integer position) const;
    void set_vertical_position(::cw::lib::Integer position);
    float m_resize_origin{};
    int row_label_width() const;
    int total_width() const;
    ::cw::lib::Rect cell_rect(int row,int column) const;
    void reveal_cell();
    void auto_size(int control);
};

// 両方のスキンのノートブックに、同じ Settings
// の描画器と入力処理を組み込む
// 。UI の実装は一系統とする。
class SkinSettingsPresentation : public ::cw::dialog::DialogPresentationProvider {
public:
    static constexpr int local_control_base=20000;
    [[nodiscard]] std::shared_ptr<::cw::dialog::settings::SettingsDialog> local_settings_presenter() const { return m_local_settings; }
    [[nodiscard]] std::shared_ptr<SkinGridPresentation> grid_presenter(int tab) const { return tab>=1 && tab<=4?m_grid_panels[tab-1]:nullptr; }
    void dialog_presentation_modifiers(::cw::dialog::DialogPresentationModifiers) override;
    void dialog_presentation_text(const std::string&,bool) override;
    void dialog_presentation_text_event(const ::cw::dialog::DialogTextEditingEvent&) override;
    void dialog_presentation_key(::cw::dialog::DialogPresentationKey,bool,::cw::dialog::DialogPresentationModifiers) override;
    void dialog_presentation_enter(::cw::dialog::DialogPresentationModifiers) override;
    void dialog_presentation_pointer(::cw::dialog::DialogPresentationAction,int,float,float,bool) override;
    void dialog_presentation_pointer_cancel() override;
    void dialog_presentation_scroll(float,float,int) override;
    void dialog_presentation_hover(float,float) override;
    [[nodiscard]] bool dialog_presentation_needs_refresh() const noexcept override;
    void dialog_presentation_refreshed() noexcept override;
protected:
    void bind_local_settings(std::shared_ptr<::cw::dialog::settings::DrawingSettingPanel>,
                             std::shared_ptr<::cw::dialog::settings::FontSettingPanel>);
    virtual bool settings_input_blocked() const { return false; }
    void use_local_settings(int tab); // -1 はその他のページ、0 は描画、1 はフォント。
    bool local_settings_action(::cw::dialog::DialogPresentationAction,int);
    void finish_local_settings(bool accept);
    std::shared_ptr<::cw::dialog::settings::SettingsDialog> active_local_settings() const;
    void place_local_settings(int y) { m_local_y=y; }
    void use_info_panel(std::shared_ptr<SkinInfoPanel>,bool,int,int);
    std::shared_ptr<SkinInfoPanel> active_info_panel() const;
    bool info_panel_action(::cw::dialog::DialogPresentationAction,int);
    static constexpr int info_control_base=10000;
    void use_path_panel(std::shared_ptr<SkinBasePanel>,bool);
    std::shared_ptr<SkinBasePanel> active_path_panel() const;
    static constexpr int path_control_base=5000;
    void bind_grid_panels(std::shared_ptr<SkinFeaturePanel>,std::shared_ptr<SkinSoundPanel>,
                          std::shared_ptr<SkinMessagePanel>,std::shared_ptr<SkinCardPanel>);
    void use_grid_panel(int tab);
    std::shared_ptr<SkinGridPresentation> active_grid_panel() const;
    bool grid_panel_action(::cw::dialog::DialogPresentationAction,int);
    bool finish_grid_edit(bool accept);
    static constexpr int grid_control_base=30000;
private:
    std::array<std::shared_ptr<SkinGridPresentation>,4> m_grid_panels;
    int m_grid_tab{};
    std::shared_ptr<SkinBasePanel> m_path_panel;
    bool m_path_active{};
    std::shared_ptr<SkinInfoPanel> m_info_panel;
    bool m_info_active{};
    int m_info_y{};
    std::shared_ptr<::cw::dialog::settings::SettingsDialog> m_local_settings;
    bool m_local_active{};
    int m_local_y{72};
};

class SkinConversionDialog : public ::cw::lib::Object,
                             public SkinSettingsPresentation {
public:
    SkinConversionDialog() = default;
    virtual ~SkinConversionDialog();
    ::cw::lib::Value m_btn_cncl{};
    ::cw::lib::Value m_btn_ok{};
    std::shared_ptr<::cw::skin::convert::Converter> m_conv{};
    bool m_cwpy_debug{};
    ::cw::lib::Value m_from_settings{};
    bool m_from_settings_value{};
    std::shared_ptr<::cw::setting::LocalSetting> m_local{};
    ::cw::lib::Value m_note{};
    std::shared_ptr<SkinBasePanel> m_pane_base{};
    std::shared_ptr<SkinCardPanel> m_pane_card{};
    std::shared_ptr<::cw::dialog::settings::DrawingSettingPanel> m_pane_draw{};
    std::shared_ptr<SkinFeaturePanel> m_pane_feature{};
    std::shared_ptr<::cw::dialog::settings::FontSettingPanel> m_pane_font{};
    std::shared_ptr<SkinMessagePanel> m_pane_message{};
    std::shared_ptr<SkinSoundPanel> m_pane_sound{};
    bool m_select_skin{};
    ::cw::lib::String m_skindirname{};
    bool m_successful{};
    ::cw::lib::Value m_warning{};
    ::cw::lib::String m_warning_text{};
    ::cw::lib::String m_status_message{};
    ::cw::lib::String m_error_message{};
    bool m_ok_enabled{};
    std::function<void()> m_close_callback{};
    bool m_closed{};
    bool m_cancelled{};
    bool m_progress_active{};
    std::uintptr_t m_native_parent_handle{};
    std::atomic_bool m_conversion_worker_done{false};
    std::thread m_conversion_worker{};
    std::mutex m_conversion_mutex{};
    ::cw::lib::String m_conversion_worker_error{};
    std::function<void(::cw::lib::String)> m_scenario_registration{};
    std::function<void(::cw::lib::String)> m_yado_registration{};
    ::cw::lib::Integer m_presentation_tab{};
    ::cw::lib::Integer m_presentation_page{};
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::String arg_exe, bool arg_from_settings, std::optional<::cw::lib::Callback> arg_get_localsettings);
    void _bind();
    void OnOk(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnCancel(std::shared_ptr<::cw::lib::Window> arg_event);
    void complete_conversion();
    void set_scenario_registration(std::function<void(::cw::lib::String)> arg_callback);
    void set_yado_registration(std::function<void(::cw::lib::String)> arg_callback);
    void _do_layout();
    [[nodiscard]] ::cw::dialog::DialogPresentation dialog_presentation() override;
    void dialog_presentation_action(::cw::dialog::DialogPresentationAction action,
                                    int index) override;
    [[nodiscard]] bool dialog_presentation_finished() const noexcept override;
    void dialog_presentation_complete() override;
};

class SkinEditDialog : public ::cw::lib::Object,
                       public SkinSettingsPresentation,
                       public std::enable_shared_from_this<SkinEditDialog> {
public:
    SkinEditDialog() = default;
    virtual ~SkinEditDialog();
    ::cw::lib::Value m_box_info{};
    ::cw::lib::Value m_btn_cncl{};
    ::cw::lib::Value m_btn_importres{};
    ::cw::lib::Value m_btn_ok{};
    bool m_cwpy_debug{};
    std::shared_ptr<SkinInfoPanel> m_info{};
    std::shared_ptr<::cw::setting::LocalSetting> m_local{};
    ::cw::lib::Value m_note{};
    std::shared_ptr<::cw::dialog::settings::DrawingSettingPanel> m_pane_draw{};
    std::shared_ptr<::cw::dialog::settings::FontSettingPanel> m_pane_font{};
    ::cw::lib::Value m_pane_info{};
    ::cw::lib::Value m_skindirname{};
    std::tuple<::cw::lib::Value, ::cw::lib::Value, ::cw::lib::Value, ::cw::lib::Value, ::cw::lib::Value, ::cw::lib::Value> m_skinsummary{};
    ::cw::lib::Value m_warning{};
    ::cw::lib::String m_skindirname_value{};
    std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, bool, ::cw::lib::Integer> m_skinsummary_typed{};
    ::cw::lib::String m_warning_text{};
    ::cw::lib::String m_status_message{};
    ::cw::lib::String m_import_source_dir{};
    ::cw::lib::String m_import_error{};
    ::cw::lib::Integer m_imported_resources{};
    std::function<void()> m_close_callback{};
    bool m_closed{};
    bool m_cancelled{};
    bool m_saved{};
    std::uintptr_t m_native_parent_handle{};
    ::cw::lib::Integer m_presentation_tab{};
    ::cw::lib::Integer m_presentation_page{};
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::String arg_skindirname, std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, bool, ::cw::lib::Integer> arg_skinsummary, ::cw::lib::Callback arg_get_localsettings);
    void _bind();
    void OnOk(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnCancel(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnImportResource(std::shared_ptr<::cw::lib::Window> arg_event);
    bool request_import(SkinBasePanel::DocumentRequest start={});
    void cancel_import_request();
    [[nodiscard]] bool import_request_pending() const { return bool(m_import_token); }
    void set_import_source(::cw::lib::String arg_source_dir);
    void _do_layout();
    [[nodiscard]] ::cw::dialog::DialogPresentation dialog_presentation() override;
    void dialog_presentation_action(::cw::dialog::DialogPresentationAction action,
                                    int index) override;
    [[nodiscard]] bool dialog_presentation_finished() const noexcept override;
    void dialog_presentation_complete() override;
protected:
    virtual bool confirm_resource_import(const ::cw::lib::String&);
    bool settings_input_blocked() const override { return import_request_pending(); }
private:
    std::shared_ptr<int> m_import_token;
    std::shared_ptr<::cw::lib::documents::Ticket> m_import_ticket;
    void finish_import(const std::shared_ptr<int>&,const std::filesystem::path&,::cw::lib::documents::Result);
    void report_import_result();
};

std::shared_ptr<::cw::setting::LocalSetting> SkinConversionDialog___init___get_local();
std::shared_ptr<::cw::setting::LocalSetting> SkinConversionDialog___init___get_local_2();
void SkinConversionDialog_OnOk_progress();
void SkinEditDialog_OnOk_func(std::shared_ptr<::cw::setting::LocalSetting> arg_local, ::cw::lib::String arg_skinname, bool arg_vocation120, ::cw::lib::Integer arg_initialcash, ::cw::lib::String arg_skintype);
void SkinEditDialog_OnOk_update_skin(::cw::lib::String arg_skindirname);
void SkinEditDialog_OnImportResource_import_res(::cw::lib::String arg_dpath, ::cw::lib::String arg_fname, ::cw::lib::String arg_ext, ::cw::lib::String arg_newresname);
void SkinBasePanel__do_layout_add_base(std::shared_ptr<::cw::lib::Control> arg_ctrl, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos);
void SkinInfoPanel__do_layout_add_info(std::shared_ptr<::cw::lib::Control> arg_ctrl, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos, ::cw::lib::Integer arg_colspan, ::cw::lib::Integer arg_rowspan, bool arg_expand, bool arg_growable);
::cw::lib::Integer SkinFeaturePanel_set_values_set_rowdata(std::shared_ptr<::cw::data::CWPyElement> arg_data, ::cw::lib::Integer arg_row);
::cw::lib::Integer SkinFeaturePanel_get_values_get_rowdata(std::shared_ptr<::cw::data::CWPyElement> arg_data, ::cw::lib::Integer arg_row);
::cw::lib::Integer SkinCardPanel___init___put_areacards(std::map<::cw::lib::String, std::shared_ptr<::cw::data::CWPyElementTree>> arg_table, ::cw::lib::Integer arg_row);
::cw::lib::Integer SkinCardPanel_set_values_put_areacards(std::map<::cw::lib::String, std::shared_ptr<::cw::data::CWPyElementTree>> arg_table, ::cw::lib::Integer arg_row);
::cw::lib::Integer SkinCardPanel_get_values_get_areacards(std::map<::cw::lib::String, std::shared_ptr<::cw::data::CWPyElementTree>> arg_table, ::cw::lib::Integer arg_row);
void main();
}
