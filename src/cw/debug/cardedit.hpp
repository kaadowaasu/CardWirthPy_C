#pragma once

// UIの実装は一つに統一し、
// 別経路の実装を設けない。

// cw/debug/cardedit.py の型付き移植。保持する Value
// メンバーで互換 ABI を保ち、実際の UI は全ホスト共通
// の DialogPresentation の一つの経路を使う。
#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"
#include "cw/dialog/presentation.hpp"
#include "cw/lib/documents.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <set>
#include <thread>

namespace cw::debug::cardedit {

using Matcher = std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::String,
                           ::cw::lib::String, ::cw::lib::String>;

// Python の _get_list は CardHeader のリストま
// たは XML 要素のリストを返す。Value はどちらの配列も保持できない
// ため、UI の実装型を公開せず、この小さなデータ専用ラッパーで区別を保つ。
class CardEditList : public ::cw::lib::Object {
public:
    std::vector<std::shared_ptr<::cw::header::CardHeader>> m_headers{};
    std::vector<std::shared_ptr<::cw::data::CWPyElement>> m_elements{};
    // Python版の storehouse は YadoData の属性ではなく、所有者として
    // 渡される生の list である。外部列を扱う場合だけ参照を保持する。
    std::vector<std::shared_ptr<::cw::header::CardHeader>>* m_headers_ref{};
};

using CardData = std::variant<std::shared_ptr<::cw::header::CardHeader>,
                              std::shared_ptr<::cw::data::CWPyElement>>;

class CardEditDialog;

// wx の GenericTreeItem に埋め込まれていた検索結果を、UI が無い環境でも
// 失わないための型付きレコード。checked/removed はツリー項目の状態に対応する。
struct SearchRecord {
    Matcher matcher{};
    std::shared_ptr<::cw::lib::Object> top_level{};
    std::shared_ptr<::cw::data::Party> party{};
    std::shared_ptr<::cw::character::Character> character{};
    std::vector<std::shared_ptr<::cw::header::CardHeader>>* headers{};
    CardData data{std::shared_ptr<::cw::header::CardHeader>{}};
    bool not_scenario_card{};
    bool checked{true};
    bool removed{};
    ::cw::lib::String label{};
    // ネイティブの平坦な一覧でも、Python のツリーの所有関係をここで保持する。
    ::cw::lib::String location{};
    // Python のツリーのキーは、表示名とは独立した所有者。
    std::vector<std::pair<::cw::lib::String, ::cw::lib::String>> location_nodes{};
    std::shared_ptr<::cw::lib::Window> item{};
};

struct CardRow {
    ::cw::lib::Integer id{};
    ::cw::lib::String name{};
    ::cw::lib::String description{};
    std::optional<::cw::lib::Integer> image{};
};

class CardEditDialog : public ::cw::lib::Object,
                       public ::cw::dialog::DialogPresentationProvider,
                       public std::enable_shared_from_this<CardEditDialog> {
public:
    struct DispatchState {
        std::mutex mutex{};
        std::condition_variable cv{};
        bool active{true};
        std::size_t callbacks{};
    };

    CardEditDialog() = default;
    virtual ~CardEditDialog();
    inline static std::shared_ptr<::cw::lib::Window> s__add_bookmark{};
    inline static std::shared_ptr<::cw::lib::Window> s__arrange_bookmark{};
    ::cw::lib::Value m__add_bookmark{};
    ::cw::lib::Value m__arrange_bookmark{};
    bool m__find{};
    ::cw::lib::Value m_bookmark{};
    ::cw::lib::Value m_bookmarkmenu{};
    ::cw::lib::Value m_bookmarkpath{};
    ::cw::lib::Value m_cards{};
    ::cw::lib::Value m_cardsbox{};
    ::cw::lib::Value m_closebtn{};
    bool m_cwpy_debug{};
    std::vector<std::shared_ptr<::cw::data::CWPyElementTree>> m_datalist{};
    ::cw::lib::Value m_dealbtn{};
    ::cw::lib::Value m_dealtarg{};
    ::cw::lib::Value m_dealtargbox{};
    ::cw::lib::Value m_delbtn{};
    ::cw::lib::Value m_dtlbtn{};
    ::cw::lib::Value m_findbtn{};
    ::cw::lib::Value m_imgidx_beast{};
    ::cw::lib::Value m_imgidx_item{};
    ::cw::lib::Value m_imgidx_skill{};
    ::cw::lib::Value m_imglist{};
    std::vector<std::shared_ptr<::cw::header::CardHeader>> m_list{};
    ::cw::lib::Value m_mauthor{};
    ::cw::lib::Value m_mdesc{};
    ::cw::lib::Value m_methodbox{};
    ::cw::lib::Value m_mname{};
    ::cw::lib::Value m_mscenario{};
    ::cw::lib::Integer m_notcast{};
    ::cw::lib::Value m_outer{};
    ::cw::lib::Value m_party{};
    ::cw::lib::Value m_root{};
    std::optional<std::shared_ptr<::cw::data::ScenarioData>> m_scdata{};
    ::cw::lib::Value m_scenario{};
    ::cw::lib::String m_scpath{};
    ::cw::lib::Value m_status{};
    ::cw::lib::Value m_stopbtn{};
    std::map<std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String>, std::tuple<std::shared_ptr<::cw::header::CardHeader>, std::shared_ptr<::cw::data::CWPyElementTree>>> m_target_cards{};
    std::map<std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String>, std::map<std::shared_ptr<::cw::lib::Window>, std::tuple<std::optional<::cw::lib::Value>, ::cw::lib::Value, ::cw::lib::Value, bool>>> m_target_table{};
    std::optional<std::set<Matcher>> m_find_matchers_override{};
    ::cw::lib::Value m_targets{};
    ::cw::lib::Value m_targetsbox{};
    ::cw::lib::Value m_timgidx_backpack{};
    ::cw::lib::Integer m_timgidx_beast{};
    ::cw::lib::Integer m_timgidx_item{};
    ::cw::lib::Value m_timgidx_member{};
    ::cw::lib::Value m_timgidx_party{};
    ::cw::lib::Integer m_timgidx_skill{};
    ::cw::lib::Value m_timgidx_storehouse{};
    ::cw::lib::Value m_timgidx_yado{};
    ::cw::lib::Value m_timglist{};
    ::cw::lib::Value m_updbtn{};

    // Python のダイアログの UI 以外の部分が使う具象データ。上
    // の Value メンバーは SDL UI との連携用に保持する。
    std::shared_ptr<::cw::data::Party> m_party_typed{};
    std::shared_ptr<::cw::data::ScenarioData> m_scdata_typed{};
    std::vector<::cw::lib::Integer> m_selected_indices{};
    bool m_match_name{true};
    bool m_match_desc{true};
    bool m_match_scenario{false};
    bool m_match_author{false};
    bool m_can_update{};
    bool m_can_delete{};
    bool m_add_bookmark_enabled{};
    bool m_arrange_bookmark_enabled{};
    bool m_detail_enabled{};
    bool m_deal_enabled{};
    bool m_find_enabled{};
    bool m_stop_enabled{};
    std::vector<CardRow> m_card_rows{};
    std::vector<SearchRecord> m_search_records{};
    // 遅延 UI コールバックがこの状態を保持し、破棄済みのダ
    // イアログを参照せずに終了・破棄を確認できるようにする。
    std::shared_ptr<DispatchState> m_dispatch_state{std::make_shared<DispatchState>()};
    std::thread m_find_thread{};
    std::atomic_bool m_find_cancel{false};
    std::atomic_bool m_find_update_posted{false};
    std::mutex m_find_mutex{};
    std::vector<SearchRecord> m_pending_find_records{};
    ::cw::lib::String m_pending_find_status{};
    bool m_pending_find_complete{};
    bool m_pending_find_cancelled{};
    bool m_pending_find_failed{};
    bool m_presentation_finished{};
    bool m_common_presentation_active{};
    std::shared_ptr<int> m_document_token{};
    std::shared_ptr<::cw::lib::documents::Ticket> m_document_ticket{};
    std::atomic_bool m_presentation_dirty{true};
    ::cw::lib::Integer m_presentation_card_scroll{};
    ::cw::lib::Integer m_presentation_target_scroll{};
    ::cw::lib::Integer m_presentation_deal_index{};
    ::cw::lib::Integer m_presentation_deal_committed_index{};
    ::cw::lib::Integer m_presentation_focus{16};
    ::cw::lib::Integer m_presentation_card_cursor{};
    ::cw::lib::Integer m_presentation_target_cursor{};
    bool m_presentation_deal_popup{};
    bool m_presentation_bookmark_popup{};
    ::cw::lib::Integer m_presentation_bookmark_index{};
    bool m_presentation_search_select_all{};
    ::cw::lib::String m_presentation_search_composition{};
    std::set<::cw::lib::String> m_presentation_collapsed_locations{};
    std::vector<::cw::lib::String> m_presentation_target_group_order{};
    // Windows native の検索入力を、モデル側の追加絞り込みとして保持する。
    // 空文字列はPython版と同じく検索条件を追加しない。
    ::cw::lib::String m_search_query{};
    // wx.Menu から切り離したブックマークの論理状態。表示用の Menu は
    // SDL adapter が後から組み立てても、項目順・表示名・遷移先を失わない。
    std::vector<std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::String>> m_bookmark_entries{};
    ::cw::lib::String m_scenario_label{"(シナリオ未選択)"};
    std::vector<::cw::lib::String> m_deal_target_labels{};
    ::cw::lib::String m_status_text{"対象はありません"};
    enum class DealTarget { Storehouse, Backpack, Party, Character };
    DealTarget m_deal_target{DealTarget::Backpack};
    std::shared_ptr<::cw::character::Character> m_deal_character{};
    bool show_modal();
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent);
    void _bind();
    void _do_layout();
    void OnScenario(std::shared_ptr<::cw::lib::Window> arg_event);
    bool request_scenario_document();
    void finish_scenario_document(const std::shared_ptr<int>& token,
                                  ::cw::lib::documents::Result result);
    void cancel_document_request() noexcept;
    void select_scenario(::cw::lib::String arg_fpath);
    void OnBookmark(std::shared_ptr<::cw::lib::Window> arg_event);
    void create_bookmarkmenu();
    void OnAddBookmark(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnArrangeBookmark(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnDetailBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void draw(bool arg_update);
    void OnDealBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnFindBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnStopBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnUpdateBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnDeleteBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    ::cw::lib::Value _get_list(Matcher arg_matcher, ::cw::lib::Value arg_owner, ::cw::lib::Value arg_data);
    ::cw::lib::Integer _indexof(Matcher arg_matcher, ::cw::lib::Value arg_owner, ::cw::lib::Value arg_data);
    std::shared_ptr<::cw::header::CardHeader> _remove(::cw::lib::Value arg_owner, ::cw::lib::Value arg_data, ::cw::lib::Integer arg_index);
    void _write_results(std::vector<::cw::lib::Value> arg_writes);
    void OnCardSelected(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnClose(std::shared_ptr<::cw::lib::Window> arg_event);
    void process_find_update();
    void queue_find_status(::cw::lib::String arg_status);
    void notify_find_update();
    void start_find_async();
    Matcher _get_matcher(::cw::lib::Value arg_data);
    std::optional<::cw::lib::Integer> _get_imgidx(::cw::lib::Value arg_data);
    std::map<Matcher, std::tuple<std::shared_ptr<::cw::header::CardHeader>, std::shared_ptr<::cw::data::CWPyElementTree>>> _get_cards(bool arg_selected);
    void _update_cards();
    void _update_bookmarkname();
    void _update_enable();

    // SDL/UI アダプタとテストから、wx の選択・チェック状態をデータ側へ渡す。
    void select_card(::cw::lib::Integer arg_index, bool arg_selected);
    void set_target_checked(::cw::lib::Integer arg_index, bool arg_checked);
    void set_deal_target(DealTarget arg_target, std::shared_ptr<::cw::character::Character> arg_character = {});
    const std::vector<SearchRecord>& search_records() const { return m_search_records; }
    const std::vector<CardRow>& card_rows() const { return m_card_rows; }
    ::cw::lib::String status_text() const { return m_status_text; }
    [[nodiscard]] ::cw::dialog::DialogPresentation dialog_presentation() override;
    void dialog_presentation_action(::cw::dialog::DialogPresentationAction arg_action,
                                    int arg_index) override;
    void dialog_presentation_text(const std::string& arg_text,
                                  bool arg_composition) override;
    void dialog_presentation_key(::cw::dialog::DialogPresentationKey arg_key,
                                 bool arg_pressed,
                                 ::cw::dialog::DialogPresentationModifiers arg_modifiers) override;
    void dialog_presentation_enter(
        ::cw::dialog::DialogPresentationModifiers arg_modifiers) override;
    void dialog_presentation_scroll(float arg_x, float arg_y, int arg_lines) override;
    [[nodiscard]] bool dialog_presentation_finished() const noexcept override;
    [[nodiscard]] bool dialog_presentation_needs_refresh() const noexcept override;
    void dialog_presentation_refreshed() noexcept override;
    void dialog_presentation_complete() override;
};

class CardEditDialog_OpenBookmark : public ::cw::lib::Object {
public:
    CardEditDialog_OpenBookmark() = default;
    virtual ~CardEditDialog_OpenBookmark() = default;
    std::weak_ptr<::cw::debug::cardedit::CardEditDialog> m_outer{};
    ::cw::lib::String m_bookmarkpath{};
    void initialize(std::shared_ptr<::cw::debug::cardedit::CardEditDialog> arg_outer,
                    ::cw::lib::String arg_bookmarkpath);
    void OnOpen(std::shared_ptr<::cw::lib::Window> arg_event);
};

extern ::cw::lib::Value g__CardDataType;
extern ::cw::lib::Value g__OwnerType;
extern ::cw::lib::Value g__TopLevelType;

void CardEditDialog_OnScenario_func(std::shared_ptr<::cw::debug::cardedit::CardEditDialog> arg_self);
void CardEditDialog_OnScenario_func_func(std::shared_ptr<::cw::debug::cardedit::CardEditDialog> arg_self);
void CardEditDialog_OnScenario_func_func_2(std::shared_ptr<::cw::debug::cardedit::CardEditDialog> arg_self);
void CardEditDialog_OnScenario_func_func2(std::shared_ptr<::cw::debug::cardedit::CardEditDialog> arg_self);
void CardEditDialog_OpenBookmark_create_bookmarkmenu___init__(::cw::lib::Value arg_self, std::shared_ptr<::cw::debug::cardedit::CardEditDialog> arg_outer, ::cw::lib::String arg_bookmarkpath);
void CardEditDialog_OpenBookmark_create_bookmarkmenu_OnOpen(::cw::lib::Value arg_self, std::shared_ptr<::cw::lib::Window> arg_event);
void CardEditDialog_OnFindBtn_func(std::set<std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String>> arg_cards);
void CardEditDialog_OnFindBtn_func_set_status(::cw::lib::String arg_text);
void CardEditDialog_OnFindBtn_func_set_status_func(::cw::lib::String arg_text);
std::shared_ptr<::cw::lib::Window> CardEditDialog_OnFindBtn_func_get_item(::cw::lib::Value arg_table, ::cw::lib::Value arg_key, std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::String arg_name, std::shared_ptr<::cw::lib::Bitmap> arg_image);
void CardEditDialog_OnFindBtn_func_add_target(std::shared_ptr<::cw::lib::Window> arg_item, std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String> arg_matcher, std::optional<::cw::lib::Value> arg_toplevel, ::cw::lib::Value arg_owner, ::cw::lib::Value arg_data, bool arg_insce);
void CardEditDialog_OnFindBtn_func_func(::cw::lib::Value arg_roots, ::cw::lib::Value arg_items, std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String> arg_matcher, std::shared_ptr<::cw::character::Character> arg_member, std::shared_ptr<::cw::header::CardHeader> arg_header);
void CardEditDialog_OnFindBtn_func_func_backpack(::cw::lib::Value arg_roots, ::cw::lib::Value arg_items, std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String> arg_matcher, std::shared_ptr<::cw::header::CardHeader> arg_header);
void CardEditDialog_OnFindBtn_func_func_storehouse(::cw::lib::Value arg_roots, ::cw::lib::Value arg_items, std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String> arg_matcher, std::shared_ptr<::cw::header::CardHeader> arg_header);
void CardEditDialog_OnFindBtn_func_func_pocket(::cw::lib::Value arg_roots, ::cw::lib::Value arg_items, std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String> arg_matcher, std::shared_ptr<::cw::data::CWPyElementTree> arg_etree, std::shared_ptr<::cw::character::Character> arg_pcard, std::shared_ptr<::cw::data::CWPyElement> arg_data);
void CardEditDialog_OnFindBtn_func_func_pcard(::cw::lib::Value arg_roots, ::cw::lib::Value arg_items, std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String> arg_matcher, std::shared_ptr<::cw::data::Party> arg_party, ::cw::lib::Integer arg_index, std::shared_ptr<::cw::character::Player> arg_pcard, std::vector<std::shared_ptr<::cw::header::CardHeader>> arg_cardpocket, std::shared_ptr<::cw::data::CWPyElement> arg_data, bool arg_insce);
void CardEditDialog_OnFindBtn_func_func_party(::cw::lib::Value arg_roots, ::cw::lib::Value arg_items, std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String> arg_matcher, std::shared_ptr<::cw::header::PartyHeader> arg_partyheader, std::shared_ptr<::cw::data::Party> arg_party, std::shared_ptr<::cw::header::CardHeader> arg_header, bool arg_insce);
void CardEditDialog_OnFindBtn_func_update_enable();
void CardEditDialog__update_cards_append_cards(::cw::lib::Callback arg_getids, ::cw::lib::Callback arg_getdata, std::shared_ptr<::cw::lib::Bitmap> arg_image);
void main();
}

