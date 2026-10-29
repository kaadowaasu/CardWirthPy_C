#pragma once

// UIの実装は一つに統一し、
// 別経路の実装を設けない。
#include "cw/lib/compat.hpp"
#include "cw/dialog/presentation.hpp"
#include "cw/fwd.hpp"

namespace cw::dialog::partyrecord {
using PartyRecordPtr = std::shared_ptr<::cw::header::PartyRecordHeader>;
using StoredPartyPtr = std::shared_ptr<::cw::thread::StoredParty>;
using RestorableData = std::tuple<bool, std::map<::cw::lib::String,bool>, std::vector<bool>>;

class SelectPartyRecord : public ::cw::lib::Object,
                          public ::cw::dialog::DialogPresentationProvider,
                          public std::enable_shared_from_this<SelectPartyRecord> {
public:
    ~SelectPartyRecord() override;
    ::cw::lib::Value m_parent{};
    std::vector<std::optional<PartyRecordPtr>> m_list{};
    ::cw::lib::Integer m_index{0};
    std::vector<std::optional<RestorableData>> m_restorable{};
    std::vector<::cw::lib::String> m_names{};
    // wx コントロールはアダプターが所有する値で表す。下の状態フィ
    // ールドは SelectPartyRecord の公開 Python
    // メンバーに対応し、SDL・ネイティブ UI も使用する。
    ::cw::lib::Value m_toppanel{};
    ::cw::lib::Value m_panel{};
    ::cw::lib::Value m_restorebtn{};
    ::cw::lib::Value m_savebtn{};
    ::cw::lib::Value m_deletebtn{};
    ::cw::lib::Value m_closebtn{};
    std::vector<::cw::lib::Value> m_buttonlist{};
    bool m_left_enabled{false}, m_left2_enabled{false};
    bool m_right_enabled{false}, m_right2_enabled{false};
    bool m_restore_enabled{false}, m_save_enabled{false}, m_delete_enabled{false};
    bool m_processing{false}, m_has_current_party{false}, m_closed{false}, m_cancelled{false};
    int m_button_focus{0};
    ::cw::lib::Integer m_money{0};
    ::cw::lib::String m_party_name{};
    ::cw::lib::Integer m_page_number{1}, m_page_count{1};
    std::vector<bool> m_member_available{};
    std::vector<::cw::lib::String> m_card_names{};
    std::vector<bool> m_card_available{};
    std::shared_ptr<::cw::lib::Surface> m_ui_surface{};

    // 確認・エラー UI は既定で共通 Message 表示を
    // 使う。上書きは非 GUI の検査にキュー境界を公開する
    // ためであり、別の OS 専用ダイアログ実装にはしない。
    bool m_owns_parent_processing{false};
    ::cw::lib::String m_operation_error;
    virtual void post_record_game(::cw::lib::Callback callback);
    virtual void post_record_ui(::cw::lib::Callback callback);
    virtual void report_operation_error(const ::cw::lib::String& error);
    std::function<bool(::cw::lib::String)> m_confirm{};
    std::function<void(::cw::lib::String)> m_play_sound{};
    std::function<StoredPartyPtr()> m_party_snapshot{};
    std::function<bool()> m_parent_processing{};
    std::function<std::optional<::cw::lib::Value>()> m_parent_selected{};
    std::function<void(::cw::lib::Value)> m_update_parent_standbys{};
    std::function<void(bool)> m_set_parent_processing{};

    void initialize(::cw::lib::Value parent);
    void set_records(std::vector<PartyRecordPtr> records, bool has_current_party);
    void set_confirm_callback(std::function<bool(::cw::lib::String)> callback);
    void set_parent_callbacks(std::function<bool()> processing,
                              std::function<std::optional<::cw::lib::Value>()> selected,
                              std::function<void(::cw::lib::Value)> update_standbys,
                              std::function<void(bool)> set_processing);
    void set_party_snapshot(std::function<StoredPartyPtr()> snapshot);
    void OnSelect(std::shared_ptr<::cw::lib::Event> event);
    void OnCancel(std::shared_ptr<::cw::lib::Window> event);
    void OnClickLeftBtn(std::shared_ptr<::cw::lib::Window> event);
    void OnClickLeft2Btn(std::shared_ptr<::cw::lib::Window> event);
    void OnClickRightBtn(std::shared_ptr<::cw::lib::Window> event);
    void OnClickRight2Btn(std::shared_ptr<::cw::lib::Window> event);
    void OnClickSaveBtn(std::shared_ptr<::cw::lib::Window> event);
    void OnClickRestoreBtn(std::shared_ptr<::cw::lib::Window> event);
    void OnClickDeleteBtn(std::shared_ptr<::cw::lib::Window> event);
    bool can_clickcenter();
    bool can_clickside();
    void enable_btn();
    void _update_restorable();
    void draw(bool update);
    [[nodiscard]] ::cw::dialog::DialogPresentation dialog_presentation() override;
    void dialog_presentation_action(::cw::dialog::DialogPresentationAction arg_action,
                                    int arg_index) override;
    void dialog_presentation_key(::cw::dialog::DialogPresentationKey key, bool pressed,
                                ::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_enter(::cw::dialog::DialogPresentationModifiers modifiers) override;
    bool input_blocked() const;
    [[nodiscard]] bool dialog_presentation_finished() const noexcept override;
    void dialog_presentation_complete() override;
};

void SelectPartyRecord_OnClickSaveBtn_func(std::shared_ptr<SelectPartyRecord>, std::optional<PartyRecordPtr>, ::cw::lib::Integer);
void SelectPartyRecord_OnClickSaveBtn_func_func(std::shared_ptr<SelectPartyRecord>, PartyRecordPtr);
void SelectPartyRecord_OnClickRestoreBtn_func(PartyRecordPtr, std::shared_ptr<SelectPartyRecord>, ::cw::lib::Value, std::optional<::cw::lib::Value>);
void SelectPartyRecord_OnClickRestoreBtn_func_func(std::shared_ptr<SelectPartyRecord>, ::cw::lib::Value, std::optional<::cw::lib::Value>, bool);
void SelectPartyRecord_OnClickDeleteBtn_func(PartyRecordPtr);
void SelectPartyRecord_OnClickDeleteBtn_func2(std::shared_ptr<SelectPartyRecord>);
void SelectPartyRecord_OnClickDeleteBtn_func2_func(std::shared_ptr<SelectPartyRecord>);
void main();
}
