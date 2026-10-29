#pragma once

// UIの実装は一つに統一し、
// 別経路の実装を設けない。

// D:/codes/cwxeditor_code/cardwirthpy-reboot/cw/debug/edit.py
// から生成した宣言。wx/SDL の表示は下のアダプター
// を通し、編集状態は具象ダイアログに保持する。ネイティブ
// ウィンドウを置き換えても、データ側の実装は変更しない。
#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"

#include <atomic>
#include <cstdint>

namespace cw::dialog { class DialogPresentationProvider; }

namespace cw::debug::edit {
using Coupon = std::tuple<::cw::lib::String, ::cw::lib::Integer>;
using CouponList = std::vector<Coupon>;
using CouponLists = std::vector<CouponList>;
using Bookmark = std::tuple<::cw::lib::String, ::cw::lib::String>;
using BookmarkList = std::vector<Bookmark>;

bool show_modal(::cw::lib::Object& arg_dialog);
[[nodiscard]] std::shared_ptr<::cw::dialog::DialogPresentationProvider>
create_common_edit_presentation(::cw::lib::Object& arg_dialog);
class AutoWidthListCtrl : public ::cw::lib::Object {
public:
    AutoWidthListCtrl() = default;
    virtual ~AutoWidthListCtrl() = default;
    std::vector<std::vector<::cw::lib::String>> m_rows{};
    std::vector<::cw::lib::Integer> m_images{};
    std::vector<::cw::lib::Integer> m_selected{};
    void initialize(std::shared_ptr<::cw::debug::edit::BreakpointEditDialog> arg_parent, ::cw::lib::Integer arg_cid, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_size, ::cw::lib::Integer arg_style);
};

class BreakpointEditDialog : public ::cw::lib::Dialog {
public:
    BreakpointEditDialog() = default;
    virtual ~BreakpointEditDialog() = default;
    std::vector<std::tuple<::cw::lib::String, ::cw::lib::String>> m__removed{};
    std::uintptr_t m_native_parent_handle{};
    ::cw::lib::Value m_cnclbtn{};
    bool m_cwpy_debug{};
    // ネイティブ・共通のモーダルセッションは、OK と Cancel・閉じるを区別する。
    bool m_modal_accepted{};
    std::shared_ptr<::cw::debug::edit::FindPanel> m_find{};
    ::cw::lib::Value m_imgidx{};
    BookmarkList m_list{};
    std::vector<::cw::lib::Integer> m_selected{};
    ::cw::lib::Value m_okbtn{};
    ::cw::lib::Value m_rmvbtn{};
    std::shared_ptr<::cw::debug::edit::AutoWidthListCtrl> m_values{};
    void initialize(std::shared_ptr<::cw::debug::debugger::Debugger> arg_parent, std::map<std::tuple<::cw::lib::String, ::cw::lib::String>, std::set<::cw::lib::String>> arg_breakpoint_table, std::uintptr_t arg_native_parent_handle = 0);
    void _bind();
    void _do_layout();
    void OnRemoveBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnOkBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnItemSelected(std::shared_ptr<::cw::lib::Window> arg_event);
    std::vector<std::tuple<::cw::lib::String, ::cw::lib::String>> get_removed();
    std::vector<::cw::lib::Integer> get_selectedindexes();
    void _item_selected();
};

class CouponEditDialog : public ::cw::lib::Dialog,
                         public std::enable_shared_from_this<CouponEditDialog> {
public:
    CouponEditDialog() = default;
    virtual ~CouponEditDialog() = default;
    bool m__processing{};
    std::uintptr_t m_native_parent_handle{};
    ::cw::lib::Value m_addbtn{};
    ::cw::lib::Value m_adjust_level{};
    ::cw::lib::Value m_cnclbtn{};
    ::cw::lib::Value m_copybtn{};
    CouponLists m_coupons{};
    std::vector<::cw::lib::Integer> m_selected{};
    ::cw::lib::Integer m_target_index{};
    bool m_adjust_level_value{};
    bool m_cwpy_debug{};
    // 利用者が明示的に OK を押した後だけ設定する。
    bool m_modal_accepted{};
    ::cw::lib::Value m_down2btn{};
    ::cw::lib::Value m_downbtn{};
    std::shared_ptr<::cw::debug::edit::FindPanel> m_find{};
    ::cw::lib::Integer m_imgidx_0{};
    ::cw::lib::Integer m_imgidx_1{};
    ::cw::lib::Integer m_imgidx_2{};
    ::cw::lib::Integer m_imgidx_m1{};
    ::cw::lib::Value m_leftbtn{};
    ::cw::lib::Value m_okbtn{};
    std::vector<std::shared_ptr<::cw::character::Character>> m_pcards{};
    ::cw::lib::Value m_rightbtn{};
    ::cw::lib::Value m_rmvbtn{};
    std::set<::cw::lib::String> m_syscoupons{};
    ::cw::lib::Value m_target{};
    std::vector<::cw::lib::String> m_targets{};
    ::cw::lib::Value m_total{};
    ::cw::lib::String score_summary() const;
    ::cw::lib::Value m_up2btn{};
    ::cw::lib::Value m_upbtn{};
    ::cw::lib::Value m_valbtn{};
    std::shared_ptr<::cw::debug::edit::EditableListCtrl> m_values{};
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::Integer arg_selected);
    void _bind();
    void _do_layout();
    void OnSelectTarget(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnLeftBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnRightBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnItemSelected(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnAddBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnRemoveBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void _remove_coupon(::cw::lib::Integer arg_index);
    void OnCopyBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnValueBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnUpBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnDownBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void _swap(::cw::lib::Integer arg_index1, ::cw::lib::Integer arg_index2);
    void OnUp2Btn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnDown2Btn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnEndLabelEdit(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnOkBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    std::vector<::cw::lib::Integer> get_selectedindexes();
    ::cw::lib::Integer _get_valueimage(::cw::lib::Integer arg_value);
    void _append_couponlist(::cw::lib::String arg_name, ::cw::lib::Integer arg_value);
    void _select_target();
    std::vector<std::tuple<::cw::lib::String, ::cw::lib::Integer>> _get_coupons(std::shared_ptr<::cw::character::Player> arg_pcard);
    void _item_selected();
    void _set_name(::cw::lib::Integer arg_index, ::cw::lib::String arg_oldname, ::cw::lib::String arg_newname);
    void _set_value(::cw::lib::Integer arg_index, ::cw::lib::Integer arg_value);
};

class EditBookmarksForCardEditDialog : public ::cw::lib::Dialog,
                                       public std::enable_shared_from_this<EditBookmarksForCardEditDialog> {
public:
    EditBookmarksForCardEditDialog() = default;
    virtual ~EditBookmarksForCardEditDialog() = default;
    bool m_modal_accepted{};
    std::uintptr_t m_native_parent_handle{};
    bool m__processing{};
    std::vector<std::tuple<::cw::lib::String, ::cw::lib::String>> m__removed{};
    ::cw::lib::Value m_cnclbtn{};
    bool m_cwpy_debug{};
    ::cw::lib::Value m_down2btn{};
    ::cw::lib::Value m_downbtn{};
    ::cw::lib::Value m_imgidx{};
    BookmarkList m_list{};
    // Python の UI は保存済みのブックマーク名を置き換えず、一
    // 時的な読み込みエラーの印を表示する。その表示状態を別に保持する。
    std::vector<bool> m_update_failed{};
    std::vector<::cw::lib::Integer> m_selected{};
    ::cw::lib::Value m_okbtn{};
    ::cw::lib::Value m_rmvbtn{};
    ::cw::lib::Value m_up2btn{};
    ::cw::lib::Value m_upbtn{};
    ::cw::lib::Value m_updatebtn{};
    ::cw::lib::Value m_values{};
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, std::vector<std::tuple<::cw::lib::String, ::cw::lib::String>> arg_bookmarks);
    void _bind();
    void _do_layout();
    void OnUpdateBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnRemoveBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnUpBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnDownBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnUp2Btn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnDown2Btn(std::shared_ptr<::cw::lib::Window> arg_event);
    void _swap(::cw::lib::Integer arg_index1, ::cw::lib::Integer arg_index2);
    void OnOkBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnItemSelected(std::shared_ptr<::cw::lib::Window> arg_event);
    std::vector<::cw::lib::Integer> get_selectedindexes();
    void _item_selected();
    bool show_modal();
};

class EditableListCtrl : public ::cw::lib::Object {
public:
    EditableListCtrl() = default;
    virtual ~EditableListCtrl() = default;
    std::vector<::cw::lib::Integer> m_col_locs{};
    std::vector<std::vector<::cw::lib::String>> m_rows{};
    std::vector<::cw::lib::Integer> m_images{};
    std::vector<::cw::lib::Integer> m_selected{};
    std::optional<::cw::lib::Integer> m_ensure_visible_row{};
    void initialize(std::shared_ptr<::cw::debug::edit::CouponEditDialog> arg_parent, ::cw::lib::Integer arg_cid, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_size, ::cw::lib::Integer arg_style);
    void OpenEditor(::cw::lib::Integer arg_row, ::cw::lib::Integer arg_col);
};

class FindPanel : public ::cw::lib::Panel {
public:
    FindPanel() = default;
    virtual ~FindPanel() = default;
    ::cw::lib::Value m__color_found{};
    ::cw::lib::Value m__color_not_found{};
    ::cw::lib::Value m_finddown{};
    ::cw::lib::Value m_findup{};
    ::cw::lib::Value m_item_selected{};
    ::cw::lib::Value m_text{};
    ::cw::lib::Value m_title{};
    ::cw::lib::Value m_values{};
    ::cw::lib::String m_query{};
    ::cw::lib::Integer m_cursor{-1};
    bool m_found{true};
    ::cw::lib::Callback m_item_selected_callback{};
    void initialize(std::shared_ptr<::cw::lib::Object> arg_parent, std::shared_ptr<::cw::lib::Object> arg_values, ::cw::lib::Callback arg_item_selected, ::cw::lib::Integer arg_style);
    void _bind();
    void _do_layout();
    void find_up();
    void find_down();
    void OnFindUp(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnFindDown(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnTextChanged(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnEnter(std::shared_ptr<::cw::lib::Window> arg_event);
};

class ListEditDialog : public ::cw::lib::Dialog {
public:
    ListEditDialog() = default;
    virtual ~ListEditDialog() = default;
    bool m__processing{};
    std::uintptr_t m_native_parent_handle{};
    ::cw::lib::String m_dialog_title{};
    ::cw::lib::Value m_addbtn{};
    ::cw::lib::Value m_cnclbtn{};
    bool m_cwpy_debug{};
    // 共通の show_modal() は、ウィンドウの存在ではなくこの結果を返す。
    bool m_modal_accepted{};
    ::cw::lib::Value m_down2btn{};
    ::cw::lib::Value m_downbtn{};
    std::shared_ptr<::cw::debug::edit::FindPanel> m_find{};
    ::cw::lib::Value m_imgidx{};
    std::vector<::cw::lib::String> m_list{};
    std::vector<::cw::lib::Integer> m_selected{};
    ::cw::lib::Value m_okbtn{};
    ::cw::lib::Value m_rmvbtn{};
    ::cw::lib::Value m_up2btn{};
    ::cw::lib::Value m_upbtn{};
    std::shared_ptr<::cw::debug::edit::EditableListCtrl> m_values{};
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::String arg_title, std::vector<::cw::lib::String> arg_mlist, std::shared_ptr<::cw::lib::Bitmap> arg_image);
    void _bind();
    void _do_layout();
    void OnAddBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnRemoveBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnUpBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnDownBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void _swap(::cw::lib::Integer arg_index1, ::cw::lib::Integer arg_index2);
    void OnUp2Btn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnDown2Btn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnEndLabelEdit(std::shared_ptr<::cw::lib::Window> arg_event);
    virtual void OnOkBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnItemSelected(std::shared_ptr<::cw::lib::Window> arg_event);
    std::vector<::cw::lib::Integer> get_selectedindexes();
    void _item_selected();
};

class SavedJPDCImageEditDialog : public ::cw::lib::Dialog {
public:
    SavedJPDCImageEditDialog() = default;
    virtual ~SavedJPDCImageEditDialog() = default;
    std::vector<std::tuple<::cw::lib::String, ::cw::lib::String>> m__removed{};
    std::uintptr_t m_native_parent_handle{};
    ::cw::lib::Value m_cnclbtn{};
    bool m_cwpy_debug{};
    bool m_modal_accepted{};
    std::shared_ptr<::cw::debug::edit::FindPanel> m_find{};
    ::cw::lib::Value m_imgidx{};
    BookmarkList m_list{};
    std::vector<::cw::lib::Integer> m_selected{};
    ::cw::lib::Value m_okbtn{};
    ::cw::lib::Value m_rmvbtn{};
    std::shared_ptr<::cw::debug::edit::AutoWidthListCtrl> m_values{};
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, std::map<std::tuple<::cw::lib::String, ::cw::lib::String>, std::shared_ptr<::cw::header::SavedJPDCImageHeader>> arg_savedjpdcimage);
    void _bind();
    void _do_layout();
    void OnRemoveBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnOkBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnItemSelected(std::shared_ptr<::cw::lib::Window> arg_event);
    std::vector<::cw::lib::Integer> get_selectedindexes();
    void _item_selected();
};

class SavedVariablesEditDialog : public ::cw::lib::Dialog {
public:
    SavedVariablesEditDialog() = default;
    virtual ~SavedVariablesEditDialog() = default;
    std::vector<std::tuple<::cw::lib::String, ::cw::lib::String>> m__removed{};
    std::uintptr_t m_native_parent_handle{};
    ::cw::lib::Value m_cnclbtn{};
    bool m_cwpy_debug{};
    bool m_modal_accepted{};
    std::shared_ptr<::cw::debug::edit::FindPanel> m_find{};
    ::cw::lib::Value m_imgidx{};
    BookmarkList m_list{};
    std::vector<::cw::lib::Integer> m_selected{};
    ::cw::lib::Value m_okbtn{};
    ::cw::lib::Value m_rmvbtn{};
    std::shared_ptr<::cw::debug::edit::AutoWidthListCtrl> m_values{};
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, std::map<std::tuple<::cw::lib::String, ::cw::lib::String>, std::tuple<std::shared_ptr<::cw::data::CWPyElement>, std::map<::cw::lib::String, bool>, std::map<::cw::lib::String, ::cw::lib::Integer>, std::map<::cw::lib::String, ::cw::lib::Value>>> arg_savedvariables);
    void _bind();
    void _do_layout();
    void OnRemoveBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnOkBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnItemSelected(std::shared_ptr<::cw::lib::Window> arg_event);
    std::vector<::cw::lib::Integer> get_selectedindexes();
    void _item_selected();
};

class VariantEditDialog : public ::cw::lib::Dialog,
                          public std::enable_shared_from_this<VariantEditDialog> {
public:
    VariantEditDialog() = default;
    virtual ~VariantEditDialog() = default;
    ::cw::lib::String m__expression{};
    std::uintptr_t m_native_parent_handle{};
    ::cw::lib::String m_dialog_title{};
    ::cw::lib::String m_dialog_label{};
    ::cw::lib::Real m__time_modified{};
    ::cw::lib::Value m__value_expr_colour{};
    ::cw::lib::Value m__value_num_colour{};
    ::cw::lib::Value m_box{};
    ::cw::lib::Value m_cnclbtn{};
    bool m_cwpy_debug{};
    bool m_modal_accepted{};
    ::cw::lib::Value m_errors{};
    ::cw::lib::Value m_is_differentscenario{};
    ::cw::lib::Value m_okbtn{};
    ::cw::lib::Value m_type_bool{};
    ::cw::lib::Value m_type_expr{};
    ::cw::lib::Value m_type_num{};
    ::cw::lib::Value m_type_str{};
    std::optional<::cw::lib::Value> m_value{};
    ::cw::lib::Integer m_value_kind{}; // 0 は数値、1 は文字列、2 は真偽値、3 は式。
    ::cw::lib::String m_value_num_text{};
    ::cw::lib::String m_value_str_text{};
    ::cw::lib::String m_value_expr_text{};
    bool m_value_bool_value{};
    bool m_value_valid{};
    ::cw::lib::String m_error_text{};
    // 式の編集ごとに単調増加する世代番号を持つ。新しい入
    // 力やダイアログ終了の後にワーカーが完了しても、世
    // 代番号と自身への弱参照の確認で古い結果を捨てる。
    std::shared_ptr<std::atomic<std::uint64_t>> m_eval_generation{
        std::make_shared<std::atomic<std::uint64_t>>(0)};
    ::cw::lib::Value m_value_bool{};
    ::cw::lib::Value m_value_expr{};
    ::cw::lib::Value m_value_num{};
    ::cw::lib::Value m_value_str{};
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::String arg_title, ::cw::lib::String arg_label, ::cw::lib::Value arg_value, bool arg_is_differentscenario);
    void _bind();
    void _do_layout();
    void OnOkBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnType(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnValue(std::shared_ptr<::cw::lib::Window> arg_event);
    void _update_value();
    void _eval_thr();
};

class CompStampEditDialog : public ::cw::debug::edit::ListEditDialog {
public:
    CompStampEditDialog() = default;
    virtual ~CompStampEditDialog() = default;
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent);
    void OnOkBtn(std::shared_ptr<::cw::lib::Window> arg_event);
};

class GossipEditDialog : public ::cw::debug::edit::ListEditDialog {
public:
    GossipEditDialog() = default;
    virtual ~GossipEditDialog() = default;
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent);
    void OnOkBtn(std::shared_ptr<::cw::lib::Window> arg_event);
};

extern ::cw::lib::Value g__T;

// 生成されたクロージャーのヘルパーには、所有するダイアログを明示的に渡す。従来のオ
// ーバーロードはソース互換性のため残すが、意図的に何もしない。プロセス共通の生ポ
// インターのアクティブダイアログでは、遅延コールバックを安全に振り分けられない。
void CouponEditDialog__swap_set_item(std::shared_ptr<::cw::debug::edit::CouponEditDialog> arg_self, ::cw::lib::Integer arg_index);
void CouponEditDialog__swap_set_item(::cw::lib::Integer arg_index);
void CouponEditDialog_OnOkBtn_func(std::vector<std::shared_ptr<::cw::character::Character>> arg_pcards, std::vector<std::vector<std::tuple<::cw::lib::String, ::cw::lib::Integer>>> arg_coupons, std::set<::cw::lib::String> arg_syscoupons, ::cw::lib::Integer arg_cindex, bool arg_adjust_level);
void GossipEditDialog_OnOkBtn_func(std::vector<::cw::lib::String> arg_seq);
void CompStampEditDialog_OnOkBtn_func(std::vector<::cw::lib::String> arg_seq);
void SavedJPDCImageEditDialog_OnOkBtn_func(std::vector<std::tuple<::cw::lib::String, ::cw::lib::String>> arg_removedlist);
void SavedVariablesEditDialog_OnOkBtn_func(std::vector<std::tuple<::cw::lib::String, ::cw::lib::String>> arg_removedlist);
void BreakpointEditDialog_OnOkBtn_func(std::vector<std::tuple<::cw::lib::String, ::cw::lib::String>> arg_removedlist);
void EditBookmarksForCardEditDialog__swap_set_item(std::shared_ptr<::cw::debug::edit::EditBookmarksForCardEditDialog> arg_self, ::cw::lib::Integer arg_index, std::tuple<::cw::lib::String, ::cw::lib::String> arg_string, ::cw::lib::Integer arg_image);
void EditBookmarksForCardEditDialog__swap_set_item(::cw::lib::Integer arg_index, std::tuple<::cw::lib::String, ::cw::lib::String> arg_string, ::cw::lib::Integer arg_image);
std::tuple<std::vector<std::vector<::cw::lib::String>>, std::vector<::cw::lib::Integer>> _get_iteminfos(std::shared_ptr<::cw::debug::edit::EditableListCtrl> arg_values);
void up_to_top(std::shared_ptr<::cw::debug::edit::EditableListCtrl> arg_values, std::vector<::cw::lib::Value>& arg_seq, ::cw::lib::Value arg_indexes);
void down_to_bottom(std::shared_ptr<::cw::debug::edit::EditableListCtrl> arg_values, std::vector<::cw::lib::Value>& arg_seq, ::cw::lib::Value arg_indexes);
void VariantEditDialog__eval_thr_func(std::shared_ptr<::cw::debug::edit::VariantEditDialog> arg_self, std::optional<::cw::lib::Value> arg_value, ::cw::lib::String arg_desc);
void main();
}

