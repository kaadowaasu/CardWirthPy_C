#pragma once

// cw/dialog/etc.py の C++ 移植。wx の実体を持たな
// い層なので、ウィジェットは Value に保持し、状態遷移をここで扱う。
#include "cw/lib/compat.hpp"
#include "cw/dialog/presentation.hpp"
#include "cw/dialog/incrementalsearch.hpp"
#include "cw/dialog/text.hpp"
#include "cw/dialog/textedit.hpp"
#include "cw/dialog/textedit_menu.hpp"
#include "cw/fwd.hpp"
#include "cw/image.hpp"

namespace cw::dialog::etc {
using Bookmark = std::tuple<std::vector<::cw::lib::String>, ::cw::lib::String>;

// メニューと整理ダイアログで一つのデータ表示モデルを使う。パーティーの適格性・状態
// は所有者が渡し、無関係な現在の ScenarioData を代わりに使わない。
struct BookmarkPresentation {
    ::cw::lib::String path, label, icon{"DIRECTORY"};
    std::shared_ptr<::cw::header::ScenarioHeader> header;
    bool enabled{true};
};
using BookmarkProjector = std::function<BookmarkPresentation(const Bookmark&)>;
[[nodiscard]] BookmarkPresentation resolve_bookmark(
    const Bookmark& bookmark, const ::cw::lib::String& scedir,
    const std::shared_ptr<::cw::scenariodb::Scenariodb>& db,
    const ::cw::lib::String& skintype);

// etc.py のダイアログの状態・サービス境界。すべての OS で Presentation
// が UI を所有し、バックエンドは別のウィジェット群を作らない。
enum class EtcUiDialogKind {
    input,
    battle_command,
    bookmark,
    comment,
    convert_yado,
    error_log,
    extension,
};

enum class EtcUiEventKind {
    button,
    cancel,
    close,
    input_changed,
    list_selected,
    list_activated,
    choice_changed,
    check_changed,
    enter,
    leave,
    move,
    paint,
};

struct EtcUiEvent {
    EtcUiEventKind kind{EtcUiEventKind::button};
    ::cw::lib::Integer id{};
    ::cw::lib::Integer index{-1};
    ::cw::lib::Integer value{};
    ::cw::lib::String text{};
};

struct EtcUiState {
    EtcUiDialogKind kind{EtcUiDialogKind::input};
    std::uintptr_t parent_handle{};
    ::cw::lib::Point position{};
    bool position_valid{};
    ::cw::lib::String title{};
    ::cw::lib::String message{};
    ::cw::lib::String text{};
    ::cw::lib::String detail{};
    std::vector<::cw::lib::String> items{};
    std::vector<::cw::lib::String> descriptions{};
    std::vector<::cw::lib::String> item_icons{};
    std::vector<std::shared_ptr<::cw::lib::Bitmap>> item_bitmaps{};
    std::vector<std::shared_ptr<::cw::lib::Bitmap>> item_nega_bitmaps{};
    std::vector<::cw::lib::String> action_labels{};
    std::vector<bool> enabled{};
    std::vector<::cw::lib::Integer> action_ids{};
    std::vector<bool> action_enabled{};
    std::vector<::cw::lib::Integer> selected{};
    ::cw::lib::Integer choice{};
    bool visible{true};
    bool enabled_window{true};
    bool modal{};
    bool processing{};
    ::cw::lib::Integer pressed{-1};
    ::cw::lib::Integer width{420};
    ::cw::lib::Integer height{260};
};

struct EtcUiCallbacks {
    std::function<void(const EtcUiEvent&)> on_event{};
};

class EtcUiBackend : public ::cw::lib::Object {
public:
    using Token = std::uintptr_t;
    EtcUiBackend() = default;
    virtual ~EtcUiBackend() = default;
    virtual void bind(Token arg_token, EtcUiCallbacks arg_callbacks) = 0;
    virtual void unbind(Token arg_token) = 0;
    virtual void sync(Token arg_token, const EtcUiState& arg_state) = 0;
    virtual void pump() = 0;
    virtual void post(Token arg_token, EtcUiEvent arg_event) = 0;
};

std::shared_ptr<EtcUiBackend> get_etc_ui_backend();

class AutoListCtrl : public ::cw::lib::Object {
public:
    AutoListCtrl() = default;
    virtual ~AutoListCtrl() = default;
    ::cw::lib::Integer m_cid{};
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer> m_size{};
    ::cw::lib::Integer m_style{};
    std::vector<::cw::lib::String> m_items{};
    void initialize(std::shared_ptr<::cw::lib::Panel> arg_parent, ::cw::lib::Integer arg_cid, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_size, ::cw::lib::Integer arg_style);
};

class BattleCommand : public ::cw::lib::Dialog,
                      public ::cw::dialog::DialogPresentationProvider
{
public:
    BattleCommand() = default;
    virtual ~BattleCommand();
    std::vector<std::shared_ptr<::cw::dialog::etc::BattleCommandCard>> m_cards{};
    ::cw::lib::Integer m__clickedflag_index{-1};
    ::cw::lib::Integer m_selected_index{-1};
    bool m_cwpy_debug{};
    ::cw::lib::Value m_leftkeyid{};
    ::cw::lib::Point m_pre_pos{};
    bool m_enabled{true};
    bool m_input_pending{};
    [[nodiscard]] bool accepts_input() const noexcept { return visible && m_enabled && !m_input_pending; }
    ::cw::lib::Value m_returnkeyid{};
    ::cw::lib::Value m_rightkeyid{};
    ::cw::lib::Value m_toppanel{};
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent);
    void _do_layout();
    void _bind();
    void OnMouseWheel(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnKeyDown(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnLeftUp(std::shared_ptr<::cw::lib::Window> arg_event);
    std::shared_ptr<int> m_close_lifetime{std::make_shared<int>(0)};
    void queue_close();
    void start();
    void runaway();
    void cancel();
    void OnCancel(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnMove(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnEnter(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnLeave(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnPaint(std::shared_ptr<::cw::lib::Window> arg_event);
    std::shared_ptr<::cw::lib::DeviceContext> draw(bool arg_update);
    void draw_card(std::shared_ptr<::cw::lib::DeviceContext> arg_dc, std::shared_ptr<::cw::dialog::etc::BattleCommandCard> arg_header, bool arg_fromkeyevent);
    void animate_click(std::shared_ptr<::cw::dialog::etc::BattleCommandCard> arg_header);
    void bind_ui();
    void sync_ui();
    std::shared_ptr<EtcUiBackend> m_ui_backend{};
    std::shared_ptr<::cw::lib::Surface> m_ui_surface{};
    [[nodiscard]] ::cw::dialog::DialogPresentation dialog_presentation() override;
    void dialog_presentation_action(::cw::dialog::DialogPresentationAction arg_action,
                                    int arg_index) override;
    void dialog_presentation_key(::cw::dialog::DialogPresentationKey key, bool pressed,
                                 ::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_enter(::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_hover(float x, float y) override;
    [[nodiscard]] bool dialog_presentation_finished() const noexcept override;
    void dialog_presentation_complete() override;
};

class BookmarkDialog : public ::cw::lib::Dialog,
                       public ::cw::dialog::DialogPresentationProvider {
public:
    BookmarkDialog() = default;
    virtual ~BookmarkDialog();
    bool m__processing{};
    std::vector<Bookmark> m_bookmark{};
    std::vector<::cw::lib::Integer> m_selectedindexes{};
    bool m_remove_enabled{},m_up_enabled{},m_down_enabled{},m_up2_enabled{},m_down2_enabled{};
    ::cw::lib::Value m_cnclbtn{};
    bool m_cwpy_debug{};
    ::cw::lib::Value m_down2btn{};
    ::cw::lib::Value m_downbtn{};
    ::cw::lib::Value m_okbtn{};
    ::cw::lib::Value m_rmvbtn{};
    ::cw::lib::Value m_up2btn{};
    ::cw::lib::Value m_upbtn{};
    std::shared_ptr<::cw::dialog::etc::AutoListCtrl> m_values{};
    ::cw::lib::String m_scedir{};
    std::shared_ptr<::cw::lib::Window> m_parent_window{};
    std::vector<::cw::lib::String> m_item_icons{};
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::String arg_scedir,
                    std::shared_ptr<::cw::scenariodb::Scenariodb> arg_db,
                    const BookmarkProjector& project = {});
    void set_bookmarks(std::vector<Bookmark> bookmarks, const BookmarkProjector& project);
    void _bind();
    void OnPaint(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnCancel(std::shared_ptr<::cw::lib::Window> arg_event);
    void _do_layout();
    void OnRemoveBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnUpBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnDownBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnUp2Btn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnDown2Btn(std::shared_ptr<::cw::lib::Window> arg_event);
    void _swap(::cw::lib::Integer arg_index1, ::cw::lib::Integer arg_index2);
    void OnItemSelected(std::shared_ptr<::cw::lib::Window> arg_event);
    std::vector<::cw::lib::Integer> get_selectedindexes();
    void _item_selected();
    void OnOkBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    // 通常・Ctrl・Shift の選択、フ
    // ォーカス、スクロールを扱う共通リスト
    // モデル。UI の実装は一系統とする。
    ::cw::lib::Integer m_list_focus{-1}, m_selection_anchor{-1};
    int m_keyboard_focus{};
    ::cw::dialog::IncrementalSearch m_type_search;
    void dialog_presentation_text(const std::string& text, bool composition) override;
    ::cw::dialog::DialogPresentationModifiers m_input_modifiers{};
    std::optional<double> m_scroll_drag_offset;
    void ensure_selection_visible();
    void select_bookmark(int index, ::cw::dialog::DialogPresentationModifiers modifiers);
    void dialog_presentation_modifiers(::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_key(::cw::dialog::DialogPresentationKey key, bool pressed,
                                ::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_enter(::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_scroll(float x, float y, int lines) override;
    void dialog_presentation_pointer(::cw::dialog::DialogPresentationAction action,
                                    int index, float x, float y, bool pressed) override;
    void dialog_presentation_pointer_cancel() override;
    void bind_ui();
    void sync_ui();
    std::shared_ptr<EtcUiBackend> m_ui_backend{};
    EtcUiState m_ui_state{};
    ::cw::lib::Integer m_presentation_page{};
    [[nodiscard]] virtual ::cw::lib::ui::FontStyle dialog_font(const ::cw::lib::String& role,
                                                             ::cw::lib::Integer pixels) const;
    [[nodiscard]] ::cw::dialog::DialogPresentation dialog_presentation() override;
    void dialog_presentation_action(::cw::dialog::DialogPresentationAction action, int index) override;
    [[nodiscard]] bool dialog_presentation_finished() const noexcept override;
    void dialog_presentation_complete() override;
};

class CommentDialog : public ::cw::dialog::text::Text {
public:
    CommentDialog() = default;
    virtual ~CommentDialog();
    ::cw::lib::Value m_btn_close{};
    bool m_cwpy_debug{};
    ::cw::lib::Value m_tc{};
    ::cw::lib::String m_comment{};
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::String arg_comment);
    void _do_layout();
    void bind_ui();
    void sync_ui();
    std::shared_ptr<EtcUiBackend> m_ui_backend{};
    EtcUiState m_ui_state{};
    ::cw::lib::Integer m_presentation_page{};
    [[nodiscard]] ::cw::dialog::DialogPresentation dialog_presentation() override;
    void dialog_presentation_action(::cw::dialog::DialogPresentationAction action, int index) override;
    [[nodiscard]] bool dialog_presentation_finished() const noexcept override;
    void dialog_presentation_complete() override;
};

class ConvertYadoDialog : public ::cw::lib::Dialog,
                          public ::cw::dialog::DialogPresentationProvider,
                          public std::enable_shared_from_this<ConvertYadoDialog> {
public:
    ConvertYadoDialog() = default;
    virtual ~ConvertYadoDialog();
    ::cw::lib::Value m_cnclbtn{};
    bool m_cwpy_debug{};
    ::cw::lib::String m_dstpath{};
    ::cw::lib::Value m_folder{};
    ::cw::lib::String m_message{};
    ::cw::lib::Value m_okbtn{};
    ::cw::lib::Value m_reffolder{};
    ::cw::lib::Value m_target{};
    ::cw::lib::Real m_targetengine{};
    ::cw::lib::Integer m_target_index{0};
    bool m_modal_ok{false};
    ::cw::lib::String m_folder_error;
    std::optional<::cw::lib::documents::Document> m_destination_document;
    using FolderRequest=std::function<std::shared_ptr<::cw::lib::documents::Ticket>(
        const ::cw::lib::documents::Request&,::cw::lib::documents::Completion)>;
    bool request_folder(FolderRequest start={});
    [[nodiscard]] bool folder_request_pending() const noexcept {return bool(m_folder_request_token);}
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::String arg_yadoname);
    void OnOk(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnCancel(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnPaint(std::shared_ptr<::cw::lib::Window> arg_event);
    void _bind();
    void _do_layout();
    void bind_ui();
    void sync_ui();
    std::shared_ptr<EtcUiBackend> m_ui_backend{};
    EtcUiState m_ui_state{};
    ::cw::lib::Integer m_presentation_page{};
    [[nodiscard]] virtual ::cw::lib::ui::FontStyle dialog_font(const ::cw::lib::String& role,
                                                             ::cw::lib::Integer pixels) const;
    [[nodiscard]] ::cw::dialog::DialogPresentation dialog_presentation() override;
    void dialog_presentation_action(::cw::dialog::DialogPresentationAction action, int index) override;
    void dialog_presentation_text(const std::string& text, bool composition) override;
    void dialog_presentation_text_event(const ::cw::dialog::DialogTextEditingEvent& event) override;
    void dialog_presentation_key(::cw::dialog::DialogPresentationKey key,bool pressed,
                                 ::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_enter(::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_modifiers(::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_pointer(::cw::dialog::DialogPresentationAction action,int index,
                                     float x,float y,bool pressed) override;
    void dialog_presentation_pointer_cancel() override;
    void dialog_presentation_hover(float x,float y) override;
    void dialog_presentation_context(float x,float y) override;
    void dialog_presentation_scroll(float x,float y,int lines) override;
    [[nodiscard]] bool dialog_presentation_finished() const noexcept override;
    void dialog_presentation_complete() override;
protected:
    virtual ::cw::lib::String read_folder_clipboard() const;
    virtual void write_folder_clipboard(const ::cw::lib::String& text);
    virtual void report_folder_error(const ::cw::lib::String& error);
private:
    ::cw::dialog::textedit::Buffer m_folder_edit;
    ::cw::dialog::textedit::EditMenu m_folder_menu;
    void open_folder_menu(float x,float y,bool keyboard);
    int m_focus{},m_choice_cursor{};
    bool m_closed{},m_dragging{},m_choice_open{};
    ::cw::dialog::DialogPresentationModifiers m_modifiers{};
    ::cw::lib::String m_yadoname,m_request_base;
    std::shared_ptr<int> m_folder_request_token;
    std::shared_ptr<::cw::lib::documents::Ticket> m_folder_ticket;
    void retire_folder_request() noexcept;
    void finish_folder_request(const std::shared_ptr<int>& token,::cw::lib::documents::Result result);
    void sync_folder();
    void publish_folder();
    void select_target(int index);
};

class ErrorLogDialog : public ::cw::dialog::text::Text {
public:
    ErrorLogDialog() = default;
    virtual ~ErrorLogDialog();
    ::cw::lib::Value m_btn_ok{};
    bool m_cwpy_debug{};
    ::cw::lib::Value m_tc{};
    ::cw::lib::String m_log{};
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::String arg_log);
    void _do_layout();
    void bind_ui();
    void sync_ui();
    std::shared_ptr<EtcUiBackend> m_ui_backend{};
    EtcUiState m_ui_state{};
    ::cw::lib::Integer m_presentation_page{};
    [[nodiscard]] ::cw::dialog::DialogPresentation dialog_presentation() override;
    void dialog_presentation_action(::cw::dialog::DialogPresentationAction action, int index) override;
    [[nodiscard]] bool dialog_presentation_finished() const noexcept override;
    void dialog_presentation_complete() override;
};

// Valueで渡す拡張ボタンの論理状態。表示は共通Presentationで構成する。
class ExtensionItem : public ::cw::lib::Object {
public:
    ExtensionItem() = default;
    virtual ~ExtensionItem() = default;
    ::cw::lib::String m_name{};
    ::cw::lib::String m_description{};
    ::cw::lib::Callback m_callback{};
    bool m_enabled{true};
};

class ExtensionDialog : public ::cw::lib::Dialog,
                        public ::cw::dialog::DialogPresentationProvider {
public:
    ExtensionDialog() = default;
    virtual ~ExtensionDialog();
    ::cw::lib::Value m_btn_cncl{};
    std::vector<::cw::lib::Value> m_buttons{};
    bool m_cwpy_debug{};
    ::cw::lib::Value m_desc{};
    ::cw::lib::Value m_items{};
    ::cw::lib::Value m_panel{};
    std::vector<::cw::lib::String> m_item_names{};
    std::vector<::cw::lib::String> m_item_descriptions{};
    std::vector<::cw::lib::Callback> m_item_callbacks{};
    std::vector<bool> m_item_enabled{};
    ::cw::lib::Integer m_hover_index{-1};
    ::cw::lib::Integer m_focus_index{-1}; // -1 は取り消し。
    ::cw::lib::Callback m_pending_callback{};
    bool m_modal_ok{false};
    std::shared_ptr<::cw::lib::Window> m_parent_window{};
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::String arg_title, std::vector<::cw::lib::Value> arg_items);
    void _bind();
    void _do_layout();
    void OnPaint(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnEnter(::cw::lib::Value arg_event);
    void OnLeave(::cw::lib::Value arg_event);
    void OnBotton(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnCancel(std::shared_ptr<::cw::lib::Window> arg_event);
    void bind_ui();
    void sync_ui();
    std::shared_ptr<EtcUiBackend> m_ui_backend{};
    EtcUiState m_ui_state{};
    ::cw::lib::Integer m_presentation_page{};
    [[nodiscard]] ::cw::dialog::DialogPresentation dialog_presentation() override;
    void dialog_presentation_action(::cw::dialog::DialogPresentationAction action, int index) override;
    void dialog_presentation_hover(float x, float y) override;
    void dialog_presentation_key(::cw::dialog::DialogPresentationKey key, bool pressed,
        ::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_enter(::cw::dialog::DialogPresentationModifiers modifiers) override;
    [[nodiscard]] bool dialog_presentation_finished() const noexcept override;
    void dialog_presentation_complete() override;
};

class InputDialog : public ::cw::lib::Dialog,
                    public ::cw::dialog::DialogPresentationProvider {
public:
    InputDialog() = default;
    virtual ~InputDialog();
    bool m__modal_ended{};
    ::cw::lib::Value m_cancelbtn{};
    bool m_cwpy_debug{};
    ::cw::lib::Value m_message{};
    ::cw::lib::Value m_okbtn{};
    ::cw::lib::Value m_textctrl{};
    ::cw::lib::String m_message_text{},m_value{};
    ::cw::lib::Integer m_modal_result{0};
    std::shared_ptr<::cw::lib::Window> m_parent_window{};
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::String arg_title, ::cw::lib::String arg_message, ::cw::lib::String arg_value, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_size);
    void _do_layout();
    void OnOk(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnCancel(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnClose(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnActivate(std::shared_ptr<::cw::lib::Window> arg_event);
    void _end_modal(::cw::lib::Integer arg_result);
    ::cw::lib::String get_value();
    void bind_ui();
    void sync_ui();
    std::shared_ptr<EtcUiBackend> m_ui_backend{};
    EtcUiState m_ui_state{};
    virtual ::cw::lib::ui::FontStyle dialog_font(const ::cw::lib::String& role,::cw::lib::Integer pixels) const;
    [[nodiscard]] ::cw::dialog::DialogPresentation dialog_presentation() override;
    void dialog_presentation_action(::cw::dialog::DialogPresentationAction action, int index) override;
    void dialog_presentation_text(const std::string& text, bool composition) override;
    void dialog_presentation_text_event(const ::cw::dialog::DialogTextEditingEvent& event) override;
    void dialog_presentation_key(::cw::dialog::DialogPresentationKey key, bool pressed,
                                 ::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_enter(::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_modifiers(::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_pointer(::cw::dialog::DialogPresentationAction action, int index,
                                     float x, float y, bool pressed) override;
    void dialog_presentation_pointer_cancel() override;
    void dialog_presentation_context(float x, float y) override;
    void dialog_presentation_hover(float x,float y) override;
    void dialog_presentation_scroll(float x,float y,int lines) override;
    [[nodiscard]] bool dialog_presentation_finished() const noexcept override;
    void dialog_presentation_complete() override;
protected:
    virtual ::cw::lib::String read_input_clipboard() const;
    virtual void write_input_clipboard(const ::cw::lib::String& text);
private:
    ::cw::dialog::textedit::EditMenu m_edit_menu;
    void open_edit_menu(float x,float y,bool keyboard);
    ::cw::dialog::textedit::Buffer m_edit;
    int m_focus{};
    bool m_dragging{};
    ::cw::dialog::DialogPresentationModifiers m_modifiers{};
    void sync_input();
    void publish_input();
};

class BattleCommandCard : public ::cw::image::CardImage {
public:
    BattleCommandCard() = default;
    virtual ~BattleCommandCard() = default;
    ::cw::lib::Callback m_func{};
    bool m_negaflag{};
    ::cw::lib::Rect m_ui_rect{};
    void initialize(std::vector<std::shared_ptr<::cw::image::ImageInfo>> arg_paths, ::cw::lib::String arg_bgtype, ::cw::lib::String arg_name, bool arg_can_loaded_scaledimage, ::cw::lib::Callback arg_func);
};

void BattleCommand_start_func(std::shared_ptr<::cw::dialog::etc::BattleCommand> arg_self);
void BattleCommand_start_func_func(std::shared_ptr<::cw::dialog::etc::BattleCommand> arg_self);
void BattleCommand_runaway_func(std::shared_ptr<::cw::dialog::etc::BattleCommand> arg_self);
void BattleCommand_runaway_func_func(std::shared_ptr<::cw::dialog::etc::BattleCommand> arg_self);
void BattleCommand_cancel_func(std::shared_ptr<::cw::dialog::etc::BattleCommand> arg_self);
void BattleCommand_cancel_func_func(std::shared_ptr<::cw::dialog::etc::BattleCommand> arg_self);
void BookmarkDialog__swap_set_item(::cw::lib::Integer arg_index, ::cw::lib::String arg_string, ::cw::lib::Integer arg_image);
void BookmarkDialog_OnOkBtn_func(std::vector<std::tuple<std::vector<::cw::lib::String>, ::cw::lib::String>> arg_bookmarks);
void main();
}

