#pragma once

// UIの実装は一つに統一し、
// 別経路の実装を設けない。

// cw/dialog/transfer.py の共通表示と型付きの転送手順。
#include "cw/lib/compat.hpp"
#include "cw/lib/ui_surface.hpp"
#include "cw/dialog/presentation.hpp"
#include "cw/dialog/incrementalsearch.hpp"
#include "cw/fwd.hpp"
#include <atomic>

namespace cw::dialog::transfer {
using SavedVariableTransfer = std::tuple<std::tuple<::cw::lib::String, ::cw::lib::String>, std::shared_ptr<::cw::data::CWPyElement>, bool>;
using SkinVariableTransfer = std::tuple<::cw::lib::String, std::shared_ptr<::cw::data::CWPyElement>, bool, ::cw::lib::String, ::cw::lib::String>;
using SavedVariableState = std::tuple<
    std::shared_ptr<::cw::data::CWPyElement>,
    std::map<::cw::lib::String, bool>,
    std::map<::cw::lib::String, ::cw::lib::Integer>,
    std::map<::cw::lib::String, ::cw::lib::Value>>;
using SavedVariableTable = std::map<
    std::tuple<::cw::lib::String, ::cw::lib::String>, SavedVariableState>;
using SkinVariableState = std::tuple<
    std::shared_ptr<::cw::data::CWPyElement>,
    std::map<::cw::lib::String, bool>,
    std::map<::cw::lib::String, ::cw::lib::Integer>,
    std::map<::cw::lib::String, ::cw::lib::Value>>;
using SkinVariableTable = std::map<::cw::lib::String, SkinVariableState>;
using SkinVariables = std::tuple<std::shared_ptr<::cw::data::CWPyElementTree>, SkinVariableTable>;
using TransferItem = std::variant<
    std::shared_ptr<::cw::data::CWPyElement>,
    ::cw::lib::Integer,
    std::vector<std::shared_ptr<::cw::header::AdventurerHeader>>,
    std::vector<std::shared_ptr<::cw::header::PartyRecordHeader>>,
    std::shared_ptr<::cw::header::PartyHeader>,
    std::shared_ptr<::cw::header::AdventurerHeader>,
    std::shared_ptr<::cw::header::CardHeader>,
    std::shared_ptr<::cw::header::SavedJPDCImageHeader>,
    SavedVariableTransfer,
    SkinVariableTransfer>;

struct TransferUiState {
    std::vector<::cw::lib::String> yado_names{};
    std::vector<::cw::lib::String> item_labels{};
    std::vector<::cw::lib::String> item_icons{};
    std::vector<bool> checked{};
    ::cw::lib::Integer from_index{};
    ::cw::lib::Integer to_index{};
    bool ok_enabled{};
    bool list_enabled{true};
    std::uintptr_t parent_native_handle{};
    bool visible{true};
};

struct TransferUiCallbacks {
    std::function<void(::cw::lib::Integer)> select_from{};
    std::function<void(::cw::lib::Integer)> select_to{};
    std::function<void(::cw::lib::Integer)> toggle_item{};
    std::function<void()> accept{};
    std::function<void()> cancel{};
};

class TransferUiBackend : public ::cw::lib::Object {
public:
    using Token = std::uintptr_t;
    virtual ~TransferUiBackend() = default;
    virtual void bind(Token arg_token, TransferUiCallbacks arg_callbacks) = 0;
    virtual void unbind(Token arg_token) = 0;
    virtual void sync(Token arg_token, const TransferUiState& arg_state) = 0;
    virtual void pump() = 0;
    virtual bool confirm(const ::cw::lib::String& arg_title, const ::cw::lib::String& arg_message) = 0;
    virtual void show_message(const ::cw::lib::String& arg_title, const ::cw::lib::String& arg_message, bool arg_error) = 0;
};

std::shared_ptr<TransferUiBackend> get_transfer_ui_backend();

class TransferYadoDataDialog : public ::cw::lib::Dialog,
                               public ::cw::dialog::DialogPresentationProvider,
                               public std::enable_shared_from_this<TransferYadoDataDialog> {
public:
    TransferYadoDataDialog() = default;
    virtual ~TransferYadoDataDialog();
    std::shared_ptr<::cw::lib::Window> m_cnclbtn{};
    bool m_cwpy_debug{};
    std::vector<TransferItem> m_data{};
    std::shared_ptr<::cw::lib::Window> m_fromyado{};
    ::cw::lib::Value m_imgidx_album{};
    ::cw::lib::Value m_imgidx_album_nc{};
    ::cw::lib::Value m_imgidx_beast{};
    ::cw::lib::Value m_imgidx_beast_nc{};
    ::cw::lib::Value m_imgidx_bookmark{};
    ::cw::lib::Value m_imgidx_bookmark_nc{};
    ::cw::lib::Value m_imgidx_completestamp{};
    ::cw::lib::Value m_imgidx_completestamp_nc{};
    ::cw::lib::Value m_imgidx_gossip{};
    ::cw::lib::Value m_imgidx_gossip_nc{};
    ::cw::lib::Value m_imgidx_item{};
    ::cw::lib::Value m_imgidx_item_nc{};
    ::cw::lib::Value m_imgidx_money{};
    ::cw::lib::Value m_imgidx_money_nc{};
    ::cw::lib::Value m_imgidx_party{};
    ::cw::lib::Value m_imgidx_party_nc{};
    ::cw::lib::Value m_imgidx_partyrecord{};
    ::cw::lib::Value m_imgidx_partyrecord_nc{};
    ::cw::lib::Value m_imgidx_savedjpdcimage{};
    ::cw::lib::Value m_imgidx_savedjpdcimage_nc{};
    ::cw::lib::Value m_imgidx_skill{};
    ::cw::lib::Value m_imgidx_skill_nc{};
    ::cw::lib::Value m_imgidx_standby{};
    ::cw::lib::Value m_imgidx_standby_nc{};
    ::cw::lib::ValueMap m_imgidx_table_c{};
    ::cw::lib::ValueMap m_imgidx_table_nc{};
    ::cw::lib::Value m_imgidx_variables{};
    ::cw::lib::Value m_imgidx_variables_nc{};
    ::cw::lib::Value m_imglist{};
    ::cw::lib::Integer m_index{};
    std::shared_ptr<::cw::lib::Window> m_okbtn{};
    std::shared_ptr<::cw::lib::Window> m_toyado{};
    ::cw::lib::Integer m_from_index{};
    ::cw::lib::Integer m_to_index{};
    std::vector<bool> m_checked{};
   bool m_ok_enabled{};
   bool m_cancelled{};
    bool m_transfer_running{},m_owns_transfer_slot{},m_completion_handled{},m_transfer_joined{};
    void release_transfer_slot();
    ::cw::lib::String m_last_error{};
    std::shared_ptr<::cw::dialog::transfer::_TransferThread> m_thread{};
    std::vector<::cw::lib::String> m_yadodirs{};
    std::vector<::cw::lib::String> m_yadonames{};
    std::shared_ptr<::cw::lib::Window> m_parent{};
    std::shared_ptr<TransferUiBackend> m_ui_backend{};
    ::cw::lib::Integer m_modal_result{};
    ::cw::lib::Integer m_scroll_offset{};
    ::cw::lib::Integer m_open_combo{-1};
    ::cw::lib::Integer m_combo_cursor{},m_combo_offset{},m_horizontal_offset{};
    ::cw::lib::Integer m_row{-1},m_anchor{-1};
    std::vector<bool> m_selected{};
    int m_focus{},m_scroll_drag{-1};
    ::cw::dialog::IncrementalSearch m_type_search;
    void dialog_presentation_text(const std::string& text, bool composition) override;
    double m_scroll_drag_offset{};
    ::cw::dialog::DialogPresentationModifiers m_modifiers{};
    void open_combo(::cw::lib::Integer combo);
    void choose_combo(::cw::lib::Integer selected);
    void select_row(::cw::lib::Integer row,::cw::dialog::DialogPresentationModifiers modifiers);
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, std::vector<::cw::lib::String> arg_yadodirs, std::vector<::cw::lib::String> arg_yadonames, ::cw::lib::String arg_selected);
    void _update_list();
    void _enable_btn();
    void OnFromYado(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnToYado(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnOk(std::shared_ptr<::cw::lib::Window> arg_event);
    void transfer_bookmark(::cw::lib::String arg_fromscedir, ::cw::lib::String arg_toscedir, ::cw::lib::String arg_fromyado, ::cw::lib::String arg_toyado, std::shared_ptr<::cw::data::CWPyElement> arg_be, std::shared_ptr<::cw::dialog::transfer::_TransferThread> arg_counter);
    void transfer_gossip(::cw::lib::String arg_fromyado, ::cw::lib::String arg_toyado, std::shared_ptr<::cw::data::CWPyElement> arg_ge, std::shared_ptr<::cw::dialog::transfer::_TransferThread> arg_counter);
    void transfer_completestamp(::cw::lib::String arg_fromyado, ::cw::lib::String arg_toyado, std::shared_ptr<::cw::data::CWPyElement> arg_ce, std::shared_ptr<::cw::dialog::transfer::_TransferThread> arg_counter);
    void transfer_elementlist(::cw::lib::String arg_fromyado, ::cw::lib::String arg_toyado, std::shared_ptr<::cw::data::CWPyElement> arg_ee, std::shared_ptr<::cw::dialog::transfer::_TransferThread> arg_counter, ::cw::lib::String arg_tag);
    // 展開した転送ログ内の XML だけに使う。書き込み前にすべての名前を検証する。
    static void rewrite_log_members(::cw::lib::String directory, const std::map<::cw::lib::String, ::cw::lib::String>& names, const std::function<void(std::shared_ptr<::cw::data::CWPyElementTree>)>& rewrite);
    void transfer_party(::cw::lib::String arg_fromyado, ::cw::lib::String arg_toyado, std::shared_ptr<::cw::header::PartyHeader> arg_header, std::shared_ptr<::cw::yadodb::YadoDB> arg_yadodb, std::shared_ptr<::cw::dialog::transfer::_TransferThread> arg_counter);
    void transfer_album(::cw::lib::String arg_fromyado, ::cw::lib::String arg_toyado, std::vector<std::shared_ptr<::cw::header::AdventurerHeader>> arg_album, std::shared_ptr<::cw::yadodb::YadoDB> arg_yadodb, std::shared_ptr<::cw::dialog::transfer::_TransferThread> arg_counter);
    ::cw::lib::String transfer_adventurer(::cw::lib::String arg_fromyado, ::cw::lib::String arg_toyado, std::shared_ptr<::cw::data::CWPyElementTree> arg_data, std::optional<std::shared_ptr<::cw::yadodb::YadoDB>> arg_yadodb, std::shared_ptr<::cw::dialog::transfer::_TransferThread> arg_counter, bool arg_overwrite);
    void transfer_card(::cw::lib::String arg_fromyado, ::cw::lib::String arg_toyado, std::shared_ptr<::cw::data::CWPyElementTree> arg_data, std::optional<std::shared_ptr<::cw::yadodb::YadoDB>> arg_yadodb, std::shared_ptr<::cw::dialog::transfer::_TransferThread> arg_counter);
    void transfer_partyrecord(::cw::lib::String arg_fromyado, ::cw::lib::String arg_toyado, std::vector<std::shared_ptr<::cw::header::PartyRecordHeader>> arg_partyrecord, std::shared_ptr<::cw::yadodb::YadoDB> arg_yadodb, std::shared_ptr<::cw::dialog::transfer::_TransferThread> arg_counter);
    void transfer_savedjpdcimage(::cw::lib::String arg_fromyado, ::cw::lib::String arg_toyado, std::shared_ptr<::cw::header::SavedJPDCImageHeader> arg_header, std::shared_ptr<::cw::yadodb::YadoDB> arg_yadodb, std::map<std::tuple<::cw::lib::String, ::cw::lib::String>, std::shared_ptr<::cw::header::SavedJPDCImageHeader>> arg_table, std::shared_ptr<::cw::dialog::transfer::_TransferThread> arg_counter);
    void transfer_savedvariables(::cw::lib::String arg_fromyado, ::cw::lib::String arg_toyado, std::shared_ptr<::cw::yadodb::YadoDB> arg_yadodb, std::tuple<std::tuple<::cw::lib::String, ::cw::lib::String>, std::shared_ptr<::cw::data::CWPyElement>, bool> arg_d, std::shared_ptr<::cw::dialog::transfer::_TransferThread> arg_counter);
    void transfer_skinvariables(::cw::lib::String arg_fromyado, ::cw::lib::String arg_toyado, std::shared_ptr<::cw::yadodb::YadoDB> arg_yadodb, std::tuple<::cw::lib::String, std::shared_ptr<::cw::data::CWPyElement>, bool, ::cw::lib::String, ::cw::lib::String> arg_d, std::shared_ptr<::cw::dialog::transfer::_TransferThread> arg_counter);
    void OnCancel(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnPaint(std::shared_ptr<::cw::lib::Window> arg_event);
    void _bind();
    void _do_layout();
    void OnListItemChecked(std::shared_ptr<::cw::lib::Window> arg_event);
    void bind_ui();
    void sync_ui();
    void complete_transfer();
    // 全 OS で計測・描画が共有する、最終画素単位の wx の役割別フォント計測値。
    [[nodiscard]] virtual ::cw::lib::ui::FontStyle dialog_font(const ::cw::lib::String& role,
                                                             ::cw::lib::Integer pixels) const;
    [[nodiscard]] ::cw::dialog::DialogPresentation dialog_presentation() override;
    void dialog_presentation_action(::cw::dialog::DialogPresentationAction arg_action,
                                    int arg_index) override;
    void dialog_presentation_modifiers(::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_key(::cw::dialog::DialogPresentationKey key,bool pressed,
                                ::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_enter(::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_scroll(float x,float y,int lines) override;
    void dialog_presentation_pointer(::cw::dialog::DialogPresentationAction action,int index,
                                    float x,float y,bool pressed) override;
    void dialog_presentation_pointer_cancel() override;
    [[nodiscard]] bool dialog_presentation_finished() const noexcept override;
    void dialog_presentation_complete() override;
};

class _TransferThread : public ::cw::lib::Thread {
public:
    _TransferThread() = default;
    virtual ~_TransferThread() = default;
    std::shared_ptr<::cw::data::CWPyElementTree> m_environment{};
    ::cw::lib::String m_fromscedir{};
    ::cw::lib::String m_fromyado{};
    std::map<::cw::lib::String, ::cw::lib::String> m_imgpaths{};
    std::map<::cw::lib::String, ::cw::lib::String> m_membertable{};
    ::cw::lib::String m_msg{};
    ::cw::lib::Integer m_num{};
    std::weak_ptr<::cw::dialog::transfer::TransferYadoDataDialog> m_outer{};
    SavedVariableTable m_saved_variables{};
    std::vector<TransferItem> m_seq{};
    std::optional<SkinVariables> m_skin_vars{};
    ::cw::lib::String m_toscedir{};
    ::cw::lib::String m_toyado{};
    std::atomic_bool m_cancel_requested{false};
    mutable std::mutex m_progress_mutex{};
    // 実行前に設定し、ワーカーが呼び、join の完了後に解除する。
    std::function<void(::cw::lib::Integer,::cw::lib::String)> m_progress_callback;
    void publish_progress();
    bool m_finished{};
    bool m_cancelled{};
    bool m_failed{};
    ::cw::lib::String m_error{};
    void initialize(std::shared_ptr<::cw::dialog::transfer::TransferYadoDataDialog> arg_outer, ::cw::lib::String arg_fromyado, ::cw::lib::String arg_toyado, std::vector<TransferItem> arg_seq);
    void run();
    void set_progress_message(::cw::lib::String arg_message);
    void increment_progress();
    void mark_finished();
    void mark_cancelled();
    void mark_failed(::cw::lib::String arg_error);
    struct ProgressSnapshot {
        ::cw::lib::Integer num{};
        ::cw::lib::String msg{};
        bool finished{};
        bool cancelled{};
        bool failed{};
        ::cw::lib::String error{};
    };
    ProgressSnapshot progress_snapshot() const;
};

::cw::lib::String _TransferThread___init____skindir_to_scedir(::cw::lib::String arg_skindir);
void main();
}

