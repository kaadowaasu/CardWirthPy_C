#include "cw/lib/runtime_access.hpp"
#include "cw/debug/cardedit.hpp"
#include "cw/lib/encoding.hpp"
#include "cw/debug/retained_presentation.hpp"

#include "cw/character.hpp"
#include "cw/content.hpp"
#include "cw/cw.hpp"
#include "cw/data.hpp"
#include "cw/dialog/cardinfo.hpp"
#include "cw/dialog/edit.hpp"
#include "cw/dialog/message.hpp"
#include "cw/dialog/presentation.hpp"
#include "cw/debug/edit.hpp"
#include "cw/frame.hpp"
#include "cw/header.hpp"
#include "cw/lib/ui_surface.hpp"
#include "cw/lib/simd_image.hpp"
#include "cw/scenariodb.hpp"
#include "cw/setting.hpp"
#include "cw/sprite/card.hpp"
#include "cw/thread.hpp"
#include "cw/util.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <functional>
#include <limits>
#include <set>
#include <stdexcept>
#include <variant>

#ifdef _MSC_VER
#pragma warning(disable: 4996) // std::filesystem::u8path により、Windowsでも lib::String のUTF-8の意味を保つ。
#endif

namespace {

// self引数のない生成コールバック用に、旧ABIに
// 対応するプロセス全体の現在セッションを持つ。 ただ
// し寿命トークンで守り、CardEditDialog
// の破棄後や別ダイアログの有効化後に、 遅れたコー
// ルバックが古い生ポインターを読まないようにする。
std::mutex g_active_dialog_mutex;
::cw::debug::cardedit::CardEditDialog* g_active_dialog = nullptr;
std::shared_ptr<::cw::debug::cardedit::CardEditDialog::DispatchState>
    g_active_dispatch_state{};

struct ActiveDialogLease {
    std::shared_ptr<::cw::debug::cardedit::CardEditDialog::DispatchState> state{};
    ~ActiveDialogLease()
    {
        if (!state) return;
        std::scoped_lock lock(state->mutex);
        if (state->callbacks > 0) --state->callbacks;
        if (state->callbacks == 0) state->cv.notify_all();
    }
};

template <typename Function>
void with_active_dialog(Function&& function)
{
    ::cw::debug::cardedit::CardEditDialog* dialog = nullptr;
    std::shared_ptr<::cw::debug::cardedit::CardEditDialog::DispatchState> state;
    {
        std::scoped_lock lock(g_active_dialog_mutex);
        dialog = g_active_dialog;
        state = g_active_dispatch_state;
    }
    if (!dialog || !state) return;

    {
        std::scoped_lock lock(state->mutex);
        if (!state->active) return;
        std::scoped_lock active_lock(g_active_dialog_mutex);
        if (dialog != g_active_dialog || state != g_active_dispatch_state) return;
        ++state->callbacks;
    }
    ActiveDialogLease lease{state};
    function(*dialog);
}

void deactivate_dialog(::cw::debug::cardedit::CardEditDialog& dialog, bool wait)
{
    const auto state = dialog.m_dispatch_state;
    if (state) {
        std::scoped_lock lock(state->mutex);
        state->active = false;
    }
    {
        std::scoped_lock lock(g_active_dialog_mutex);
        if (g_active_dialog == &dialog) {
            g_active_dialog = nullptr;
            g_active_dispatch_state.reset();
        }
    }
    if (wait && state) {
        std::unique_lock lock(state->mutex);
        ::cw::lib::without_runtime_access([&] {
            state->cv.wait(lock, [&state] { return state->callbacks == 0; });
            lock.unlock();
        });
    }
}

template <typename T>
std::shared_ptr<T> object_as(const ::cw::lib::Value& value)
{
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
    if (!object || !*object) return {};
    return std::dynamic_pointer_cast<T>(*object);
}

template <typename T>
::cw::lib::Value object_value(const std::shared_ptr<T>& object)
{
    if (!object) return {};
    return ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(object));
}

::cw::lib::String string_value(const ::cw::lib::Value& value)
{
    if (const auto* string = std::get_if<::cw::lib::String>(&value.storage())) return *string;
    return {};
}

::cw::lib::Integer integer_value(const ::cw::lib::Value& value,
                                 ::cw::lib::Integer fallback = 0)
{
    if (const auto* integer = std::get_if<::cw::lib::Integer>(&value.storage())) return *integer;
    if (const auto* real = std::get_if<::cw::lib::Real>(&value.storage()))
        return static_cast<::cw::lib::Integer>(*real);
    return fallback;
}

std::shared_ptr<::cw::data::CWPyElement> element_from_value(const ::cw::lib::Value& value)
{
    if (const auto element = object_as<::cw::data::CWPyElement>(value)) return element;
    if (const auto tree = object_as<::cw::data::CWPyElementTree>(value)) return tree->getroot();
    return {};
}

std::shared_ptr<::cw::data::CWPyElement> character_root(const std::shared_ptr<::cw::character::Character>& character)
{
    return character ? element_from_value(character->m_data) : nullptr;
}

::cw::lib::String card_type_from_matcher(const ::cw::debug::cardedit::Matcher& matcher)
{
    return std::get<0>(matcher);
}

std::shared_ptr<::cw::data::CWPyElement> card_property(const std::shared_ptr<::cw::data::CWPyElement>& data)
{
    if (!data) return {};
    const auto property = data->find("Property");
    return property && *property ? *property : data;
}

std::shared_ptr<::cw::data::CWPyElement> card_pocket(
    const std::shared_ptr<::cw::data::CWPyElement>& root, const ::cw::lib::String& type)
{
    if (!root) return {};
    const ::cw::lib::String name = type == "SkillCard" ? "SkillCards" :
                                   type == "ItemCard" ? "ItemCards" :
                                   type == "BeastCard" ? "BeastCards" : "";
    if (name.empty()) return {};
    const auto pocket = root->find(name);
    return pocket && *pocket ? *pocket : nullptr;
}

std::shared_ptr<::cw::character::Character> character_from_tree(
    const std::shared_ptr<::cw::data::CWPyElementTree>& tree)
{
    if (!tree) return {};
    auto character = std::make_shared<::cw::character::Character>();
    character->initialize(tree);
    character->m_data = object_value(tree);
    character->m_name = tree->gettext("Property/Name", "");
    return character;
}

std::shared_ptr<::cw::data::CWPyElementTree> tree_from_value(const ::cw::lib::Value& value)
{
    return object_as<::cw::data::CWPyElementTree>(value);
}

std::shared_ptr<::cw::data::CWPyElementTree> load_yado_tree(const ::cw::lib::String& path)
{
    if (path.empty()) return {};
    try {
        return ::cw::data::yadoxml2etree(path, "", std::nullopt);
    } catch (const std::exception&) {
        // ネイティブ版は、この検索境界で個別パスのツリー読み込み失敗を飛ばす。 Python
        // 版の対応するワーカーでは、その例外が検索を中断する場合がある。
        return {};
    }
}

std::shared_ptr<::cw::debug::cardedit::CardEditList> list_from_value(const ::cw::lib::Value& value)
{
    return object_as<::cw::debug::cardedit::CardEditList>(value);
}

::cw::lib::Value make_sequence_value(
    const std::vector<::cw::lib::Value>& values = {})
{
    auto sequence = std::make_shared<::cw::data::VariantSequence>();
    sequence->m_values = values;
    return ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(sequence));
}

void add_external_record(::cw::debug::cardedit::CardEditDialog& dialog,
                         const ::cw::debug::cardedit::Matcher& matcher,
                         const std::optional<::cw::lib::Value>& top_level,
                         const ::cw::lib::Value& owner, const ::cw::lib::Value& data,
                         bool in_scenario, const ::cw::lib::String& label,
                         std::vector<std::shared_ptr<::cw::header::CardHeader>>* headers = nullptr)
{
    if (dialog.m_target_cards.find(matcher) == dialog.m_target_cards.end()) return;
    ::cw::debug::cardedit::SearchRecord record;
    record.matcher = matcher;
    if (top_level && !top_level->is_none()) {
        if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&top_level->storage()); object && *object)
            record.top_level = *object;
    }
    record.party = object_as<::cw::data::Party>(owner);
    record.character = object_as<::cw::character::Character>(owner);
    record.headers = headers;
    record.not_scenario_card = !in_scenario;
    record.checked = true;
    record.label = label;
    if (const auto header = object_as<::cw::header::CardHeader>(data)) record.data = header;
    else if (const auto element = object_as<::cw::data::CWPyElement>(data)) record.data = element;
    else return;
    dialog.m_search_records.push_back(record);
    auto item = std::make_shared<::cw::lib::Window>();
    item->title = label;
    dialog.m_search_records.back().item = item;
    dialog.m_target_table[matcher][item] = {
        top_level.value_or(::cw::lib::Value{}), owner, data, record.not_scenario_card};
}

} // namespace


namespace cw::debug::cardedit {

namespace {

struct CardEditHostServices {
    std::function<void(CardEditDialog&)> bind{};
    std::function<void(CardEditDialog&)> layout{};
    std::function<void(CardEditDialog&, const std::vector<std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::String>>&)> popup_bookmark{};
    std::function<void(CardEditDialog&)> arrange_bookmark{};
    std::function<void(CardEditDialog&)> show_detail{};
    std::function<void(CardEditDialog&, bool)> draw{};
    std::function<void(CardEditDialog&)> sync_enabled{};
    std::function<void(CardEditDialog&)> destroy{};
    std::function<bool(std::function<void()>)> post{};
};

const std::shared_ptr<CardEditHostServices>& card_edit_host_services()
{
    // UI実装は一つに統一する。配置はCardEditDialog
    // が公開し、 この固定オブジェクトに
    // はホストのサービスだけを持たせる。
    static const auto services = [] {
    auto result = std::make_shared<CardEditHostServices>();
    result->bind = [](CardEditDialog&) {};
    result->layout = [](CardEditDialog&) {};
    result->popup_bookmark = [](CardEditDialog&,
                                 const std::vector<std::tuple<::cw::lib::String,
                                                              ::cw::lib::String,
                                                              ::cw::lib::String>>&) {};
    result->arrange_bookmark = [](CardEditDialog& dialog) {
        if (!::cw::g_cwpy || !::cw::g_cwpy->m_setting ||
            ::cw::g_cwpy->m_setting->m_bookmarks_for_cardedit.empty()) {
            (void)::cw::dialog::message::show_common_modal_message(
                "ブックマーク", "登録されているブックマークがありません。", false, true);
            return;
        }
        auto editor = std::make_shared<::cw::debug::edit::EditBookmarksForCardEditDialog>();
        editor->initialize(nullptr, ::cw::g_cwpy->m_setting->m_bookmarks_for_cardedit);
        if (editor->show_modal())
            ::cw::g_cwpy->m_setting->m_bookmarks_for_cardedit = editor->m_list;
        dialog.create_bookmarkmenu();
    };
    result->show_detail = [](CardEditDialog& dialog) {
        if (dialog.m_list.empty()) return;
        std::size_t selected = dialog.m_selected_indices.empty()
            ? 0 : static_cast<std::size_t>((std::max<::cw::lib::Integer>)(0, dialog.m_selected_indices.front()));
        if (selected >= dialog.m_list.size()) selected = 0;
        std::vector<::cw::dialog::cardinfo::CardInfoItem> cards;
        for (const auto& header : dialog.m_list) if (header) cards.emplace_back(header);
        if (cards.empty() || selected >= cards.size()) return;
        auto detail = std::make_shared<::cw::dialog::cardinfo::YadoCardInfo>();
        const auto scedir = dialog.m_scdata_typed
            ? string_value(dialog.m_scdata_typed->m_tempdir) : ::cw::lib::String{};
        detail->set_parent_callbacks([&dialog](const auto&) { dialog.draw(true); },
                                     [&dialog]() { dialog.draw(true); });
        detail->initialize(nullptr, cards, cards[selected], scedir);
        detail->show_modal();
    };
    result->draw = [](CardEditDialog&, bool) {};
    result->sync_enabled = [](CardEditDialog&) {};
    result->destroy = [](CardEditDialog&) {};
    return result;
    }();
    return services;
}

} // namespace

bool CardEditDialog::show_modal()
{
    m_presentation_finished = false;
    m_common_presentation_active = true;
    const bool shown = ::cw::dialog::show_common_modal_presentation(shared_from_this());
    m_common_presentation_active = false;
    if (!m_presentation_finished) OnClose({});
    return shown && m_presentation_finished && !::cw::g_quit_app;
}

CardEditDialog::~CardEditDialog()
{
    cancel_document_request();
    deactivate_dialog(*this, false);
    m_find_cancel = true;
    if (m_find_thread.joinable() &&
        m_find_thread.get_id() != std::this_thread::get_id())
        ::cw::lib::without_runtime_access([&] { m_find_thread.join(); });
    deactivate_dialog(*this, true);
    const auto& services = card_edit_host_services();
    if (services->destroy) services->destroy(*this);
}

void CardEditDialog::initialize(std::shared_ptr<::cw::lib::Window> arg_parent)
{
    cancel_document_request();
    // 再初期化はまれでも、リセット後の同じ保存領域を以前
    // のセッションのコールバックが参照しないようにする。
    m_find_cancel = true;
    if (m_find_thread.joinable() &&
        m_find_thread.get_id() != std::this_thread::get_id())
        ::cw::lib::without_runtime_access([&] { m_find_thread.join(); });
    deactivate_dialog(*this, true);
    m_dispatch_state = std::make_shared<DispatchState>();
    m_outer = arg_parent ? object_value(arg_parent) : ::cw::lib::Value{};
    {
        std::scoped_lock lock(g_active_dialog_mutex);
        g_active_dialog = this;
        g_active_dispatch_state = m_dispatch_state;
    }
    // 構築・操作部品は共通のPresentationが管理する。 Python版の既定値とカ
    // ードの全データ状態はここに持ち、ホストはPresentationを配置するだけにする。
    m_cwpy_debug = true;
    m__find = false;
    m_list.clear();
    m_datalist.clear();
    m_target_cards.clear();
    m_target_table.clear();
    m_selected_indices.clear();
    m_card_rows.clear();
    m_search_records.clear();
    m_can_update = false;
    m_can_delete = false;
    m_add_bookmark_enabled = false;
    m_arrange_bookmark_enabled = false;
    m_detail_enabled = false;
    m_deal_enabled = false;
    m_find_enabled = false;
    m_stop_enabled = false;
    m_status_text = "対象はありません";
    m_scenario_label = "(シナリオ未選択)";
    m_bookmark_entries.clear();
    m_deal_target = DealTarget::Backpack;
    m_presentation_deal_committed_index = 0;
    m_deal_character.reset();
    m_match_name = true;
    m_match_desc = true;
    m_match_scenario = false;
    m_match_author = false;
    m_notcast = 0;
    m_timgidx_skill = 0;
    m_timgidx_item = 1;
    m_timgidx_beast = 2;
    m_deal_target_labels.clear();
    m_scdata.reset();
    m_scdata_typed.reset();
    m_scpath.clear();
    m_party_typed.reset();
    m_search_query.clear();
    m_presentation_finished = false;
    m_common_presentation_active = false;
    m_presentation_card_scroll = 0;
    m_presentation_target_scroll = 0;
    m_presentation_deal_index = 0;
    m_presentation_focus = 16;
    m_presentation_card_cursor = 0;
    m_presentation_target_cursor = 0;
    m_presentation_deal_popup = false;
    m_presentation_bookmark_popup = false;
    m_presentation_bookmark_index = 0;
    m_presentation_search_select_all = false;
    m_presentation_search_composition.clear();
    m_presentation_collapsed_locations.clear();
    m_presentation_target_group_order.clear();

    if (::cw::g_cwpy) {
        if (::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata) {
            const auto& yado = *::cw::g_cwpy->m_ydata;
            if (yado->m_party && *yado->m_party) {
                m_party_typed = *yado->m_party;
                m_party = object_value(m_party_typed);
            }
        }

        if (::cw::g_cwpy->is_playingscenario()) {
            const auto scenario = object_as<::cw::data::ScenarioData>(::cw::g_cwpy->m_sdata);
            if (scenario) {
                m_scdata_typed = scenario;
                m_scdata = scenario;
                m_scpath = ::cw::util::normalize_path(string_value(scenario->m_fpath));
                if (!m_scpath.empty() && std::filesystem::is_directory(std::filesystem::u8path(m_scpath))) {
                    const auto base = std::filesystem::u8path(m_scpath);
                    const auto wsm = base / "Summary.wsm";
                    const auto xml = base / "Summary.xml";
                    if (std::filesystem::is_regular_file(wsm)) m_scpath = ::cw::lib::encoding::path_to_utf8(wsm);
                    if (std::filesystem::is_regular_file(xml)) m_scpath = ::cw::lib::encoding::path_to_utf8(xml);
                }
            }
        }
    }
    if (!m_party_typed || !m_party_typed->is_adventuring()) {
        m_deal_target_labels.emplace_back("カード置場");
        ++m_notcast;
    }
    if (m_party_typed) {
        m_deal_target_labels.emplace_back("荷物袋");
        m_deal_target_labels.emplace_back("パーティ全員");
        m_notcast += 2;
        for (const auto& pcard : ::cw::g_cwpy->get_pcards({})) {
            if (pcard) m_deal_target_labels.push_back(pcard->m_name);
        }
    }
    m_deal_target = m_party_typed ? DealTarget::Backpack : DealTarget::Storehouse;
    for (std::size_t i = 0; i < m_deal_target_labels.size(); ++i) {
        const auto& label = m_deal_target_labels[i];
        if ((m_deal_target == DealTarget::Backpack && label == "荷物袋") ||
            (m_deal_target == DealTarget::Storehouse && label == "カード置場")) {
            m_presentation_deal_index = static_cast<::cw::lib::Integer>(i);
            m_presentation_deal_committed_index = m_presentation_deal_index;
            break;
        }
    }
    _bind();
    _do_layout();
    _update_cards();
    _update_enable();
}

void CardEditDialog::_bind()
{
    // 全ホストが、下の共通Presentationの操作振り分けに接続する。
    // ブックマークのボタンも公開APIと同じOnBookmarkを呼び、 固定
    // のホストサービスには基本機能だけを置き、別のダイアログ処理を作らない。
    const auto& services = card_edit_host_services();
    if (services->bind) {
        services->bind(*this);
    }
}

void CardEditDialog::_do_layout()
{
    const auto& services = card_edit_host_services();
    if (services->layout) {
        services->layout(*this);
    }
}

// ネイティブ版はwxのモーダル選択ではなく共通の文書サービスを使う。
// イベントのタイトルがあれば直接パスを選び、なければ成功した単一の
// ネイティブパスの結果だけをselect_scenarioへ渡す。
void CardEditDialog::OnScenario(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (arg_event && !arg_event->title.empty()) select_scenario(arg_event->title);
    else (void)request_scenario_document();
}

bool CardEditDialog::request_scenario_document()
{
    namespace doc = ::cw::lib::documents;
    if (m_presentation_finished || m_document_token || weak_from_this().expired()) return false;
    doc::Request request;
    request.operation = doc::Operation::OpenFiles;
    request.title = "シナリオの選択";
    request.initial_location = m_scpath;
    request.filters = {{"シナリオファイル", "wsn;wsm;zip;lzh;cab;xml"}};
    const auto token = std::make_shared<int>(0);
    m_document_token = token;
    m_presentation_dirty = true;
    const auto weak = weak_from_this();
    try {
        auto ticket = ::cw::dialog::request_presentation_document(
            request, [weak, token](doc::Result result) {
                if (const auto self = weak.lock())
                    self->finish_scenario_document(token, std::move(result));
            });
        if (m_document_token == token) {
            m_document_ticket = std::move(ticket);
            if (!m_document_ticket || !m_document_ticket->pending())
                finish_scenario_document(token, {doc::Status::Failed, {},
                    "ファイル選択を開始できませんでした。"});
        }
    } catch (const std::exception& error) {
        finish_scenario_document(token, {doc::Status::Failed, {}, error.what()});
    } catch (...) {
        finish_scenario_document(token, {doc::Status::Failed, {},
            "ファイル選択に失敗しました。"});
    }
    return true;
}

void CardEditDialog::finish_scenario_document(
    const std::shared_ptr<int>& token, ::cw::lib::documents::Result result)
{
    namespace doc = ::cw::lib::documents;
    if (m_document_token != token || m_presentation_finished) return;
    cancel_document_request();
    if (result.status == doc::Status::Cancelled) return;
    if (result.status != doc::Status::Success || result.documents.size() != 1) {
        (void)::cw::dialog::message::show_common_modal_message(
            "シナリオ読込エラー",
            result.error.empty() ? "シナリオファイルを選択できませんでした。" : result.error,
            false, true);
        return;
    }
    const auto& document = result.documents.front();
    if (document.kind != doc::LocationKind::NativePath || document.location.empty()) {
        (void)::cw::dialog::message::show_common_modal_message(
            "シナリオ読込エラー",
            "選択したシナリオを永続的なローカルファイルとして参照できません。",
            false, true);
        return;
    }
    select_scenario(document.location);
}

void CardEditDialog::cancel_document_request() noexcept
{
    if (m_document_ticket) m_document_ticket->cancel();
    m_document_ticket.reset();
    m_document_token.reset();
    m_presentation_dirty = true;
}

// 置換先を読む前に、実行中の検索をキャンセルして終了を待つ。 読み込み成
// 功後にパスを反映し、検索・ブックマークの操作欄を更新する。 エラーはPython
// の標準エラー出力ではなく、共通のモーダルメッセージで示す。
void CardEditDialog::select_scenario(::cw::lib::String arg_fpath)
{
    // 停止操作は、ワーカーが終了する前にUIフラグを消す場合がある。 ワ
    // ーカーの完了も待ち、古い結果が新しい一覧へ入り直さないようにする。
    if (m__find || m_find_thread.joinable()) {
        m_find_cancel = true;
        if (m_find_thread.joinable() &&
            m_find_thread.get_id() != std::this_thread::get_id())
            ::cw::lib::without_runtime_access([&] { m_find_thread.join(); });
        m__find = false;
        {
            std::scoped_lock lock(m_find_mutex);
            m_pending_find_records.clear();
            m_pending_find_status.clear();
            m_pending_find_complete = false;
            m_pending_find_cancelled = false;
            m_pending_find_failed = false;
        }
        m_find_update_posted = false;
        _update_enable();
    }
    try {
        const auto scenario = ::cw::scenariodb::get_scenario(arg_fpath);
        if (!scenario || !*scenario) {
            m_status_text = "シナリオを読み込めません: " + arg_fpath;
            draw(true);
            (void)::cw::dialog::message::show_common_modal_message(
                "シナリオ読込エラー", m_status_text, false, true);
            return;
        }
        m_scdata_typed = *scenario;
        m_scdata = *scenario;
        m_scpath = std::move(arg_fpath);
        _update_cards();
        _update_bookmarkname();
        _update_enable();
    } catch (const std::exception& error) {
        m_status_text = "シナリオ読込エラー: " + ::cw::lib::String(error.what());
        draw(true);
        (void)::cw::dialog::message::show_common_modal_message(
            "シナリオ読込エラー", m_status_text, false, true);
    } catch (...) {
        m_status_text = "シナリオ読込中に不明なエラーが発生しました。";
        draw(true);
        (void)::cw::dialog::message::show_common_modal_message(
            "シナリオ読込エラー", m_status_text, false, true);
    }
}

void CardEditDialog::OnBookmark(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (::cw::g_cwpy) ::cw::g_cwpy->play_sound("page", false, 100, 1, 0, 0, false);
    if (m_bookmarkmenu.is_none()) create_bookmarkmenu();
    m_add_bookmark_enabled = m_scdata_typed != nullptr;
    m_arrange_bookmark_enabled = ::cw::g_cwpy && ::cw::g_cwpy->m_setting &&
                                 !::cw::g_cwpy->m_setting->m_bookmarks_for_cardedit.empty();
    m_presentation_focus = 1;
    m_presentation_bookmark_index = 0;
    m_presentation_deal_popup = false;
    m_presentation_bookmark_popup = true;
    m_presentation_dirty = true;
    const auto& services = card_edit_host_services();
    if (services->sync_enabled) services->sync_enabled(*this);
    if (services->popup_bookmark) {
        services->popup_bookmark(*this, m_bookmark_entries);
    }
}

void CardEditDialog::create_bookmarkmenu()
{
    auto menu = std::make_shared<::cw::lib::Menu>();
    m_bookmarkmenu = object_value(menu);
    m_bookmark_entries.clear();
    // wx.MenuItemの代わりに、項目順・パス・表示名を論理状態
    // として保持する。 ネイティブのアダプターはこの列をそのまま描き、
    // bookmark_pathをOpenBookmarkへ渡せる。
    m_bookmark_entries.emplace_back("", "", "ブックマークの登録");
    m_bookmark_entries.emplace_back("", "", "ブックマークの整理");
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting) {
        for (const auto& [path, name] : ::cw::g_cwpy->m_setting->m_bookmarks_for_cardedit) {
            auto fname = ::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(path).filename());
            const auto lower = [&] {
                auto value = fname;
                std::transform(value.begin(), value.end(), value.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                return value;
            }();
            if (lower == "summary.xml" || lower == "summary.wsm")
                fname = ::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(path).parent_path().filename());
            auto label = name.empty() ? fname : name + "(" + fname + ")";
            m_bookmark_entries.emplace_back(path, name, std::move(label));
        }
    }
}

// wxの操作部品を一つの共通Presentationモデルで表す。
// ブックマーク・詳細の操作はそのモデルと共通ホストサービスを使い、
// シナリオ・実行環境・設定がない、または行が古い場合は部品へ触れず戻
// る。 Python版は有効なwxダイアログと全体状態を前提とする。
void CardEditDialog::OnAddBookmark(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (!m_scdata_typed || !::cw::g_cwpy || !::cw::g_cwpy->m_setting) return;
    auto path = m_scpath;
    const auto fname = ::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(path).filename());
    const auto lower = [&] {
        auto value = fname;
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }();
    if (lower == "summary.wsm" || lower == "summary.xml")
        path = ::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(path).parent_path());
    ::cw::g_cwpy->m_setting->m_bookmarks_for_cardedit.emplace_back(
        path, string_value(m_scdata_typed->m_name));
    create_bookmarkmenu();
}

void CardEditDialog::OnArrangeBookmark(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    const auto& services = card_edit_host_services();
    if (services->arrange_bookmark) {
        services->arrange_bookmark(*this);
        create_bookmarkmenu();
    }
}

void CardEditDialog::OnDetailBtn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (!m_scdata_typed || m_card_rows.empty()) return;
    auto index = m_selected_indices.empty() ? ::cw::lib::Integer{0} : m_selected_indices.front();
    if (index < 0 || static_cast<std::size_t>(index) >= m_list.size()) return;
    for (std::size_t i = 0; i < m_list.size(); ++i)
        if (m_list[i]) m_list[i]->m_negaflag = static_cast<::cw::lib::Integer>(i) == index;
    draw(true);
    const auto& services = card_edit_host_services();
    if (services->show_detail) {
        services->show_detail(*this);
    }
}

void CardEditDialog::draw(bool arg_update)
{
    (void)arg_update;
    m_selected_indices.clear();
    for (std::size_t i = 0; i < m_list.size(); ++i)
        if (m_list[i] && m_list[i]->m_negaflag) m_selected_indices.push_back(static_cast<::cw::lib::Integer>(i));
    const auto& services = card_edit_host_services();
    if (services->draw) services->draw(*this, arg_update);
    m_presentation_dirty = true;
}

void CardEditDialog::OnDealBtn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_ydata || !*::cw::g_cwpy->m_ydata) return;
    auto yado = *::cw::g_cwpy->m_ydata;
    std::vector<std::pair<std::shared_ptr<::cw::data::CWPyElementTree>,
                          std::shared_ptr<::cw::header::CardHeader>>> selected;
    // wx.ListCtrl.GetNextItem と同様、選択行を行番号の昇順にたどる。
    auto selected_indices = m_selected_indices;
    std::sort(selected_indices.begin(), selected_indices.end());
    selected_indices.erase(std::unique(selected_indices.begin(), selected_indices.end()),
                           selected_indices.end());
    for (const auto index : selected_indices) {
        if (index < 0 || static_cast<std::size_t>(index) >= m_datalist.size() ||
            static_cast<std::size_t>(index) >= m_list.size()) continue;
        selected.emplace_back(m_datalist[static_cast<std::size_t>(index)],
                              m_list[static_cast<std::size_t>(index)]);
    }
    if (selected.empty()) return;

    std::vector<::cw::lib::Value> targets;
    switch (m_deal_target) {
    case DealTarget::Storehouse: targets.emplace_back("STOREHOUSE"); break;
    case DealTarget::Backpack:
        if (m_party_typed) targets.push_back(make_sequence_value());
        break;
    case DealTarget::Party:
        for (const auto& pcard : ::cw::g_cwpy->get_pcards({}))
            if (const auto character = std::dynamic_pointer_cast<::cw::character::Character>(pcard))
                targets.push_back(object_value(character));
        break;
    case DealTarget::Character:
        if (m_deal_character) targets.push_back(object_value(m_deal_character));
        break;
    }
    std::size_t count = 0;
    // Python版と同様、対象を外側、選択行を内側のループにする。
    // 一人分の素材・交換の副作用を終えてから、次の受け手へ進む。
    for (const auto& target : targets) {
        for (const auto& [tree, source] : selected) {
            if (!tree || !source) continue;
            const bool storehouse = m_deal_target == DealTarget::Storehouse;
            const std::optional<std::shared_ptr<::cw::data::Party>> party =
                storehouse ? std::optional<std::shared_ptr<::cw::data::Party>>{} :
                             std::optional<std::shared_ptr<::cw::data::Party>>{m_party_typed};
            // カード置場と荷物袋の識別値も、既存の共通get_card API
            // を使う。 受け手の順序のために、別の素材・交換処理を追加しない。
            ::cw::content::get_card(::cw::data::copytree(tree, true), target,
                !::cw::g_cwpy->is_playingscenario(), -1, -1, party,
                source->m_scedir, true, false, !storehouse, true, true,
                std::nullopt, std::nullopt);
            ++count;
        }
    }
    if (m_deal_target == DealTarget::Storehouse) yado->changed();
    if (m_party_typed) m_party_typed->m_sorted_backpack_by_order = false;
    if (count && ::cw::g_cwpy) ::cw::g_cwpy->play_sound("harvest", false, 100, 1, 0, 0, false);
}

void CardEditDialog::queue_find_status(::cw::lib::String arg_status)
{
    {
        std::scoped_lock lock(m_find_mutex);
        m_pending_find_status = std::move(arg_status);
    }
    notify_find_update();
}

void CardEditDialog::notify_find_update()
{
    m_presentation_dirty = true;
    if (m_find_update_posted.exchange(true)) return;
    // wx.CallAfter と同様、ダイアログのUIループへ予約する。 CWPy::exec_func
    // はゲームスレッドのキューなので使わない。 draw()/sync_enabled
    // は共通Presentationホストにも触れるUIアダプターの処理。
    const auto services = card_edit_host_services();
    const auto state = m_dispatch_state;
    if (services->post && state) {
        const bool posted = services->post([this, state] {
            {
                std::scoped_lock lock(state->mutex);
                if (!state->active) return;
                ++state->callbacks;
            }
            ActiveDialogLease lease{state};
            process_find_update();
        });
        if (posted) return;
    }
    // UIバックエンドなしは未対応であり、ワーカー
    // ・ゲームの通知処理からモデルを変更する理由に
    // しない。 実際のUI予約機能を登録するホスト
    // 用に通知領域を残し、検索完了とは報告しない。
    if (!m_common_presentation_active)
        m_find_update_posted = false;
}

void CardEditDialog::process_find_update()
{
    std::vector<SearchRecord> records;
    ::cw::lib::String status;
    bool complete = false;
    bool cancelled = false;
    bool failed = false;
    {
        std::scoped_lock lock(m_find_mutex);
        status = std::move(m_pending_find_status);
        if (m_pending_find_complete) {
            complete = true;
            cancelled = m_pending_find_cancelled;
            failed = m_pending_find_failed;
            records = std::move(m_pending_find_records);
            m_pending_find_complete = false;
            m_pending_find_cancelled = false;
            m_pending_find_failed = false;
        }
        m_find_update_posted = false;
    }
    if (!complete) {
        if (!status.empty()) m_status_text = std::move(status);
        _update_enable();
        draw(true);
        return;
    }

    m_search_records = std::move(records);
    m_target_table.clear();
    for (const auto& record : m_search_records) {
        if (!record.item) continue;
        ::cw::lib::Value owner;
        std::shared_ptr<CardEditList> list_owner;
        if (record.character) owner = object_value(record.character);
        else if (record.party) owner = object_value(record.party);
        else if (record.headers) {
            list_owner = std::make_shared<CardEditList>();
            list_owner->m_headers_ref = record.headers;
            owner = object_value(list_owner);
        }
        ::cw::lib::Value data;
        if (const auto* header =
                std::get_if<std::shared_ptr<::cw::header::CardHeader>>(&record.data))
            data = object_value(*header);
        else if (const auto* element =
                     std::get_if<std::shared_ptr<::cw::data::CWPyElement>>(&record.data))
            data = object_value(*element);
        m_target_table[record.matcher][record.item] = {
            record.top_level ? ::cw::lib::Value(record.top_level) : ::cw::lib::Value{},
            owner, data, record.not_scenario_card};
    }
    m__find = false;
    if (cancelled) {
        // Python版と同様、停止要求前に見つけた記録を保持し、
        // 同じ完了経路を通す。 途中結果も編集可能なままにする。
    }
    if (::cw::g_cwpy) ::cw::g_cwpy->play_sound("signal", false, 100, 1, 0, 0, false);
    m_status_text = failed && !status.empty()
        ? std::move(status)
        : std::to_string(m_search_records.size()) + "件のカードが見つかりました。";
    if (m_find_thread.joinable() &&
        m_find_thread.get_id() != std::this_thread::get_id())
        ::cw::lib::without_runtime_access([&] { m_find_thread.join(); });
    _update_enable();
    draw(true);
}

void CardEditDialog::start_find_async()
{
    if (m__find) return;
    if (m_find_thread.joinable()) ::cw::lib::without_runtime_access([&] { m_find_thread.join(); });

    m_target_cards = _get_cards(true);
    if (m_find_matchers_override) {
        for (auto it = m_target_cards.begin(); it != m_target_cards.end(); ) {
            if (m_find_matchers_override->find(it->first) == m_find_matchers_override->end())
                it = m_target_cards.erase(it);
            else ++it;
        }
        m_find_matchers_override.reset();
    }
    if (!m_search_query.empty()) {
        for (auto it = m_target_cards.begin(); it != m_target_cards.end(); ) {
            const auto& matcher = it->first;
            const bool found = std::get<1>(matcher).find(m_search_query) != ::cw::lib::String::npos ||
                               std::get<2>(matcher).find(m_search_query) != ::cw::lib::String::npos;
            if (!found) it = m_target_cards.erase(it);
            else ++it;
        }
    }
    m_target_table.clear();
    m_search_records.clear();
    {
        std::scoped_lock lock(m_find_mutex);
        m_pending_find_records.clear();
        m_pending_find_status.clear();
        m_pending_find_complete = false;
        m_pending_find_cancelled = false;
        m_pending_find_failed = false;
    }
    m_find_update_posted = false;
    m_find_cancel = false;
    m__find = true;
    m_status_text = "検索中...";
    _update_enable();
    draw(true);

    std::shared_ptr<::cw::data::YadoData> yado;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata)
        yado = *::cw::g_cwpy->m_ydata;
    if (m_target_cards.empty()) {
        m__find = false;
        m_status_text = "0件のカードが見つかりました。";
        if (::cw::g_cwpy) ::cw::g_cwpy->play_sound("signal", false, 100, 1, 0, 0, false);
        _update_enable();
        draw(true);
        return;
    }

    const auto target_cards = m_target_cards;
    const std::set<Matcher> cards = [&] {
        std::set<Matcher> result;
        for (const auto& [matcher, value] : target_cards) {
            (void)value;
            result.insert(matcher);
        }
        return result;
    }();
    const auto party = m_party_typed;
    const auto game = ::cw::g_cwpy;
    const bool match_name = m_match_name;
    const bool match_desc = m_match_desc;
    const bool match_scenario = m_match_scenario;
    const bool match_author = m_match_author;
    const bool in_scenario = game && game->is_playingscenario();

    m_find_thread = std::thread([this, cards, party, game, yado, match_name, match_desc,
                                 match_scenario, match_author, in_scenario]() {
        ::cw::lib::RuntimeAccess runtime_access;
        std::vector<SearchRecord> records;
        ::cw::lib::String find_error;
        const auto cancelled = [this] { return m_find_cancel.load(); };
        try {
        const auto matcher_for = [match_name, match_desc, match_scenario, match_author](
                                     const ::cw::lib::Value& data) -> Matcher {
            ::cw::lib::String cardtype;
            ::cw::lib::String name;
            ::cw::lib::String desc;
            ::cw::lib::String scenario;
            ::cw::lib::String author;
            if (const auto header = object_as<::cw::header::CardHeader>(data)) {
                cardtype = header->m_type;
                if (match_name) name = header->m_name;
                if (match_desc) desc = header->m_desc;
                if (match_scenario) scenario = header->m_scenario;
                if (match_author) author = header->m_author;
            } else if (const auto element = object_as<::cw::data::CWPyElement>(data)) {
                cardtype = element->m_tag;
                const auto property = card_property(element);
                if (property) {
                    if (match_name) name = property->gettext("Name", "");
                    if (match_desc) desc = property->gettext("Description", "");
                    if (match_scenario) scenario = property->gettext("Scenario", "");
                    if (match_author) author = property->gettext("Author", "");
                }
            }
            return {std::move(cardtype), std::move(name), std::move(desc),
                    std::move(scenario), std::move(author)};
        };
        const auto owner_key = [](const auto& owner) {
            return std::to_string(reinterpret_cast<std::uintptr_t>(owner.get()));
        };
        const auto add_record = [&](const Matcher& matcher,
                                    const std::shared_ptr<::cw::lib::Object>& top,
                                    const std::shared_ptr<::cw::data::Party>& owner_party,
                                    const std::shared_ptr<::cw::character::Character>& owner_character,
                                    std::vector<std::shared_ptr<::cw::header::CardHeader>>* headers,
                                    CardData data, bool card_in_scenario,
                                    const ::cw::lib::String& label,
                                    const ::cw::lib::String& location,
                                    const std::vector<std::pair<::cw::lib::String, ::cw::lib::String>>& groups) {
            if (cancelled() || cards.find(matcher) == cards.end()) return;
            SearchRecord record;
            record.matcher = matcher;
            record.top_level = top;
            record.party = owner_party;
            record.character = owner_character;
            record.headers = headers;
            record.data = std::move(data);
            record.not_scenario_card = !card_in_scenario;
            record.label = label;
            record.location = location;
            record.location_nodes = groups;
            record.item = std::make_shared<::cw::lib::Window>();
            record.item->title = location.empty() ? label : location + " / " + label;
            records.push_back(std::move(record));
        };
        const auto search_headers = [&](std::vector<std::shared_ptr<::cw::header::CardHeader>>* headers,
                                        const std::shared_ptr<::cw::lib::Object>& top,
                                        const std::shared_ptr<::cw::data::Party>& owner_party,
                                        const std::shared_ptr<::cw::character::Character>& owner_character,
                                        bool card_in_scenario,
                                        const ::cw::lib::String& location,
                                    const std::vector<std::pair<::cw::lib::String, ::cw::lib::String>>& groups) {
            if (!headers) return;
            // 処理を譲る前に各ヘッダーを保持する。 長い検索中にUI
            // ・ゲームが元のポケットを編集する場合があるため。
            const auto snapshot = *headers;
            for (const auto& header : snapshot) {
                if (cancelled()) break;
                if (!header) continue;
                const auto matcher = matcher_for(object_value(header));
                add_record(matcher, top, owner_party, owner_character, headers, header,
                           card_in_scenario, header->m_name, location, groups);
                ::cw::lib::without_runtime_access([] { std::this_thread::yield(); });
            }
        };

        if (party && game) {
            for (const auto& pcard : game->get_pcards({})) {
                if (cancelled()) break;
                if (!pcard) continue;
                const auto character = std::dynamic_pointer_cast<::cw::character::Character>(pcard);
                if (!character) continue;
                queue_find_status(character->m_name + "の手札カードを検索中...");
                const std::array<std::vector<std::shared_ptr<::cw::header::CardHeader>>*, 3> pockets{
                    &std::get<0>(character->m_cardpocket_typed),
                    &std::get<1>(character->m_cardpocket_typed),
                    &std::get<2>(character->m_cardpocket_typed)};
                for (auto* headers : pockets)
                    search_headers(headers, character, {}, character, in_scenario,
                                   party->m_name + " / " + character->m_name,
                                   {{"party:"+owner_key(party),party->m_name},{"character:"+owner_key(character),character->m_name}});
            }
            if (!cancelled()) {
                queue_find_status("荷物袋を検索中...");
                search_headers(&party->m_backpack, party, party, {}, in_scenario,
                               party->m_name + " / 荷物袋",
                               {{"party:"+owner_key(party),party->m_name},{"backpack","荷物袋"}});
            }
        }

        if (yado && !cancelled()) {
            queue_find_status("カード置場を検索中...");
            search_headers(&yado->m_storehouse, {}, {}, {}, false, "カード置場",{{"storehouse","カード置場"}});

            const auto standbys = yado->m_standbys;
            const auto parties = yado->m_partys;
            for (const auto& advheader : standbys) {
                ::cw::lib::without_runtime_access([] { std::this_thread::yield(); });
                if (cancelled()) break;
                if (!advheader) continue;
                queue_find_status(advheader->m_name + "の手札カードを検索中...");
                const auto* path = std::get_if<::cw::lib::String>(&advheader->m_fpath.storage());
                if (!path || path->empty()) continue;
                const auto tree = load_yado_tree(*path);
                const auto character = character_from_tree(tree);
                const auto root = tree ? tree->getroot() : nullptr;
                for (const auto& type : {::cw::lib::String("SkillCard"),
                                         ::cw::lib::String("ItemCard"),
                                         ::cw::lib::String("BeastCard")}) {
                    if (cancelled()) break;
                    const auto pocket = card_pocket(root, type);
                    if (!pocket) continue;
                    for (const auto& element : pocket->m_children) {
                        if (cancelled()) break;
                        if (!element) continue;
                        const auto matcher = matcher_for(object_value(element));
                        add_record(matcher, tree, {}, character, nullptr, element, false,
                                   element->gettext("Property/Name", ""),
                                   "待機中のメンバー / " + advheader->m_name,
                                   {{"standby","待機中のメンバー"},{owner_key(advheader),advheader->m_name}});
                    }
                }
            }

            for (const auto& party_header : parties) {
                ::cw::lib::without_runtime_access([] { std::this_thread::yield(); });
                if (cancelled()) break;
                if (!party_header) continue;
                auto saved_party = std::make_shared<::cw::data::Party>();
                // Pythonの Party(partyheader) と同様、trueで初期
                // 化し、 メンバーツリーとパーティDBの荷物袋の両方を復元してから検索する。
                saved_party->initialize(party_header, true);
                queue_find_status(saved_party->m_name + "の手札カードを検索中...");
                for (const auto& value : saved_party->m_members) {
                    if (cancelled()) break;
                    const auto tree = object_as<::cw::data::CWPyElementTree>(value);
                    const auto character = character_from_tree(tree);
                    const auto root = tree ? tree->getroot() : nullptr;
                    for (const auto& type : {::cw::lib::String("SkillCard"),
                                             ::cw::lib::String("ItemCard"),
                                             ::cw::lib::String("BeastCard")}) {
                        if (cancelled()) break;
                        const auto pocket = card_pocket(root, type);
                        if (!pocket) continue;
                        for (const auto& element : pocket->m_children) {
                            if (cancelled()) break;
                            if (!element) continue;
                            const auto matcher = matcher_for(object_value(element));
                            add_record(matcher, character, saved_party, character, nullptr,
                                       element, saved_party->is_adventuring(),
                                       element->gettext("Property/Name", ""),
                                       saved_party->m_name + " / " +
                                           (character ? character->m_name : "(メンバー)"),
                                       {{"party:"+owner_key(saved_party),saved_party->m_name},{"member:"+owner_key(tree),character ? character->m_name : "(メンバー)"}});
                        }
                    }
                }
                if (!cancelled()) {
                    queue_find_status(saved_party->m_name + "の荷物袋を検索中...");
                    search_headers(&saved_party->m_backpack, saved_party, saved_party, {},
                                   saved_party->is_adventuring(),
                                   saved_party->m_name + "の荷物袋",
                                   {{"party:"+owner_key(saved_party),saved_party->m_name},{"backpack","荷物袋"}});
                }
            }
        }
        } catch (const std::exception& error) {
            find_error = "検索エラー: " + ::cw::lib::String(error.what());
        } catch (...) {
            find_error = "検索中に不明なエラーが発生しました。";
        }
        {
            std::scoped_lock lock(m_find_mutex);
            if (!find_error.empty()) {
                m_pending_find_status = std::move(find_error);
                m_pending_find_failed = true;
            } else {
                m_pending_find_failed = false;
            }
            m_pending_find_records = std::move(records);
            m_pending_find_complete = true;
            m_pending_find_cancelled = cancelled();
        }
        notify_find_update();
    });
}

// 共通ワーカーは、Pythonのwxツリーへのコールバックではなく
// 、検索入力を複製してこのモデルへ保留結果を送る。 対象の読み込み
// 失敗は飛ばし、終了させるワーカーエラーは完了通知前に記録する。
void CardEditDialog::OnFindBtn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    start_find_async();
}

void CardEditDialog::OnStopBtn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (!m__find) return;
    m_find_cancel = true;
    m__find = false;
    m_status_text = "検索を中断しています...";
    _update_enable();
    draw(true);
}

// wxのツリー項目のデータを共通の検索記録に置き換える
// 。 更新・削除はそのチェック・除去状態を読み、更新
// では消えた元の対応や古い所有者を飛ばす。 検索中の
// ガードで、置換時の共通スナップショットを保護する。
void CardEditDialog::OnUpdateBtn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (m__find) return;
    std::vector<::cw::lib::Value> writes;
    std::size_t count = 0;
    for (auto& record : m_search_records) {
        if (!record.checked || record.removed) continue;
        std::shared_ptr<CardEditList> list_owner;
        // 保存済みパーティのメンバーは、Partyとメンバーの両方を状
        // 況情報に持つ。 Python版と同様、所有者にはメンバーを使
        // い、Partyの荷物袋ではなくそのカードポケットを編集する。
        ::cw::lib::Value owner_value = record.character ? object_value(record.character) :
                                        record.party ? object_value(record.party) : ::cw::lib::Value{};
        if (!record.party && !record.character && record.headers) {
            list_owner = std::make_shared<CardEditList>();
            list_owner->m_headers_ref = record.headers;
            owner_value = object_value(list_owner);
        }
        ::cw::lib::Value data_value;
        if (const auto* header = std::get_if<std::shared_ptr<::cw::header::CardHeader>>(&record.data))
            data_value = object_value(*header);
        else if (const auto* element = std::get_if<std::shared_ptr<::cw::data::CWPyElement>>(&record.data))
            data_value = object_value(*element);
        const auto source_it = m_target_cards.find(record.matcher);
        if (source_it == m_target_cards.end() || !std::get<0>(source_it->second) ||
            !std::get<1>(source_it->second)) continue;
        const auto index = _indexof(record.matcher, owner_value, data_value);
        if (index < 0) continue;
        // Python版と同様、_remove()がごみ箱へ移す前に古いヘッダ
        // ーの順序を読み、 共通の get_card() 経路用に保持する。
        const auto old_order = [&]() {
            if (const auto* header =
                    std::get_if<std::shared_ptr<::cw::header::CardHeader>>(&record.data))
                return *header ? integer_value((*header)->m_order, -1) : ::cw::lib::Integer{-1};
            return ::cw::lib::Integer{-1};
        }();
        const auto old_header = _remove(owner_value, data_value, index);
        if (!old_header) continue;
        const auto& [source_header, source_tree] = source_it->second;
        const auto copy = ::cw::data::copytree(source_tree, true);
        if (!copy || !copy->getroot()) continue;
        // Python版と同様、Attachmentを保
        // 持するのは既存の召喚獣カードだけ。 置換先のス
        // キル・アイテムは必ず非付帯とし、召喚獣も種類
        // だけで付帯にせず、元カードの実際の値を保つ。
        const bool attachment = old_header->m_type == "BeastCard"
            ? old_header->m_attachment
            : false;
        std::shared_ptr<::cw::header::CardHeader> new_header;
        std::shared_ptr<::cw::data::CWPyElement> new_element;
        const auto type = std::get<0>(record.matcher);
        if (record.headers || record.character) {
            // Python版と同様、カード置場・荷物袋・Characterの移動先
            // は content.get_card() を通す。 単独・埋め込みの
            // 正しいファイル識別情報と所有者XMLの更新を、この一つの経路で行う。
            const auto previous_size = record.headers ? record.headers->size() : std::size_t{0};
            const auto destination = record.character
                ? object_value(record.character)
                : record.party ? ::cw::lib::Value("BACKPACK") : ::cw::lib::Value("STOREHOUSE");
            ::cw::content::get_card(copy, destination, record.not_scenario_card, index,
                                    old_order, record.party, source_header->m_scedir,
                                    false, false, attachment, true, true,
                                    std::nullopt, std::nullopt);
            if (record.headers && record.headers->size() > previous_size && !record.headers->empty()) {
                const auto position = std::clamp<::cw::lib::Integer>(
                    index, 0, static_cast<::cw::lib::Integer>(record.headers->size() - 1));
                new_header = (*record.headers)[static_cast<std::size_t>(position)];
            }
            if (!record.headers && record.character) {
                const auto pocket = card_pocket(character_root(record.character), type);
                if (pocket && !pocket->m_children.empty()) {
                    const auto position = std::clamp<::cw::lib::Integer>(
                        index, 0, static_cast<::cw::lib::Integer>(pocket->m_children.size() - 1));
                    new_element = pocket->m_children[static_cast<std::size_t>(position)];
                }
            }
        } else {
            continue;
        }
        if (record.top_level) writes.emplace_back(record.top_level);
        record.data = new_header ? CardData(new_header) : CardData(new_element);
        ::cw::lib::Value updated_data;
        if (new_header) updated_data = object_value(new_header);
        else if (new_element) updated_data = object_value(new_element);
        if (record.item) {
            m_target_table[record.matcher][record.item] = {
                record.top_level ? ::cw::lib::Value(record.top_level) : ::cw::lib::Value{},
                owner_value, updated_data, record.not_scenario_card};
        }
        // 更新のたびに置換カードの名前を使う。 以前の表示名へ追加すると
        // 二度目に[更新][更新]となるため、 Pythonの SetItemText(name
        // + "[更新]") と同じにする。
        record.label = source_header->m_name + "[更新]";
        if (record.item) record.item->title = record.label;
        ++count;
    }
    _write_results(std::move(writes));
    if (::cw::g_cwpy && ::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata) {
        auto yado = *::cw::g_cwpy->m_ydata;
        for (const auto& record : m_search_records)
            if (record.headers && yado && record.headers == &yado->m_storehouse) { yado->changed(); break; }
    }
    m_status_text = std::to_string(count) + "件のカードを更新しました。";
    _update_enable();
    if (::cw::g_cwpy) ::cw::g_cwpy->play_sound("harvest", false, 100, 1, 0, 0, false);
}

void CardEditDialog::OnDeleteBtn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (m__find) return;
    std::vector<::cw::lib::Value> writes;
    std::size_t count = 0;
    for (auto& record : m_search_records) {
        if (!record.checked || record.removed) continue;
        std::shared_ptr<CardEditList> list_owner;
        // OnUpdateBtnと同じ所有者の規則を使う。 保存済みパーティ
        // のメンバーは、自身のスキル・アイテム・召喚獣のポケットから除く。
        ::cw::lib::Value owner_value = record.character ? object_value(record.character) :
                                        record.party ? object_value(record.party) : ::cw::lib::Value{};
        if (!record.party && !record.character && record.headers) {
            list_owner = std::make_shared<CardEditList>();
            list_owner->m_headers_ref = record.headers;
            owner_value = object_value(list_owner);
        }
        ::cw::lib::Value data_value;
        if (const auto* header = std::get_if<std::shared_ptr<::cw::header::CardHeader>>(&record.data))
            data_value = object_value(*header);
        else if (const auto* element = std::get_if<std::shared_ptr<::cw::data::CWPyElement>>(&record.data))
            data_value = object_value(*element);
        // Python版と同様、所有者のポケットを探す前に検索対象を除き、
        // ラベルを更新する。 検索・交換が例外になっても、この順序を保つ。
        record.removed = true;
        // Python版と同様、検索・結果のラベルをカードの現在名で置
        // き換える。 以前のラベルの所有者・ファイルの注記は残さない。
        if (const auto* header = std::get_if<std::shared_ptr<::cw::header::CardHeader>>(&record.data))
            record.label = (*header ? (*header)->m_name : ::cw::lib::String{}) + "[削除済み]";
        else if (const auto* element = std::get_if<std::shared_ptr<::cw::data::CWPyElement>>(&record.data))
            record.label = (*element ? (*element)->gettext("Property/Name", "") : ::cw::lib::String{}) + "[削除済み]";
        if (record.item) record.item->title = record.label;
        if (record.item) {
            const auto table_it = m_target_table.find(record.matcher);
            if (table_it != m_target_table.end()) {
                table_it->second.erase(record.item);
                if (table_it->second.empty()) m_target_table.erase(table_it);
            }
        }
        const auto index = _indexof(record.matcher, owner_value, data_value);
        _remove(owner_value, data_value, index);
        if (record.top_level) writes.emplace_back(record.top_level);
        ++count;
    }
    _write_results(std::move(writes));
    if (::cw::g_cwpy && ::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata) {
        auto yado = *::cw::g_cwpy->m_ydata;
        for (const auto& record : m_search_records)
            if (record.headers && yado && record.headers == &yado->m_storehouse) { yado->changed(); break; }
    }
    m_status_text = std::to_string(count) + "件のカードを除去しました。";
    _update_enable();
    if (::cw::g_cwpy) ::cw::g_cwpy->play_sound("harvest", false, 100, 1, 0, 0, false);
}

// Pythonの動的な所有者を、Valueによる振り分けで表す。 削
// 除が所有者を変える必要がある場合、CardEditListは現在の
// ヘッダーベクターへの参照を持つ。 無関係なValue型は空として
// 扱うが、必須のXMLポケットなし・未知のカード種類は例外にする。
::cw::lib::Value CardEditDialog::_get_list(Matcher arg_matcher, ::cw::lib::Value arg_owner, ::cw::lib::Value arg_data)
{
    auto result = std::make_shared<CardEditList>();
    if (const auto party = object_as<::cw::data::Party>(arg_owner)) {
        result->m_headers = party->m_backpack;
    } else if (const auto yado = object_as<::cw::data::YadoData>(arg_owner)) {
        result->m_headers = yado->m_storehouse;
    } else if (const auto list = list_from_value(arg_owner)) {
        result->m_headers = list->m_headers_ref ? *list->m_headers_ref : list->m_headers;
        result->m_elements = list->m_elements;
    } else if (const auto character = object_as<::cw::character::Character>(arg_owner)) {
        const auto type = card_type_from_matcher(arg_matcher);
        if (type != "SkillCard" && type != "ItemCard" && type != "BeastCard")
            throw std::invalid_argument("unknown card pocket type");
        if (object_as<::cw::header::CardHeader>(arg_data)) {
            // load_cardpocket_from_xml() は新しい互換表示を
            // 作る。 削除は現在のポケットを対象にするため、Character::initialize()
            // が設定した型付きタプルへのポインターを保持する。
            if (type == "SkillCard") result->m_headers_ref = &std::get<0>(character->m_cardpocket_typed);
            else if (type == "ItemCard") result->m_headers_ref = &std::get<1>(character->m_cardpocket_typed);
            else if (type == "BeastCard") result->m_headers_ref = &std::get<2>(character->m_cardpocket_typed);
            if (result->m_headers_ref) result->m_headers = *result->m_headers_ref;
            // Python版と同様、このポケットそのものを空でも返す。
            // 個人カードや互換用の複製を、代替の検索一覧にしない。
        } else {
            const auto root = character_root(character);
            if (!root) throw std::invalid_argument("card pocket requires character XML");
            const auto name = type == "SkillCard" ? "SkillCards" :
                              type == "ItemCard" ? "ItemCards" : "BeastCards";
            result->m_elements = root->find_exists(name)->m_children;
        }
    }
    return object_value(result);
}

::cw::lib::Integer CardEditDialog::_indexof(Matcher arg_matcher, ::cw::lib::Value arg_owner, ::cw::lib::Value arg_data)
{
    const auto list = list_from_value(_get_list(std::move(arg_matcher), arg_owner, arg_data));
    if (!list) return -1;
    if (const auto header = object_as<::cw::header::CardHeader>(arg_data)) {
        for (std::size_t i = 0; i < list->m_headers.size(); ++i)
            if (list->m_headers[i] == header) return static_cast<::cw::lib::Integer>(i);
    } else if (const auto element = object_as<::cw::data::CWPyElement>(arg_data)) {
        for (std::size_t i = 0; i < list->m_elements.size(); ++i)
            if (list->m_elements[i] == element) return static_cast<::cw::lib::Integer>(i);
    }
    return -1;
}

// ネイティブの所有者振り分けはYadoDataも受け付け、未対応の所有者・データ
// にはnullを返す。 Partyは有効な荷物袋位置から不足ヘッダーを補えるが、
// Python版は指定ヘッダーの型をassertで要求する。 CardEditList
// はPython版と同様、負も含む owner[index] を使う。
std::shared_ptr<::cw::header::CardHeader> CardEditDialog::_remove(::cw::lib::Value arg_owner, ::cw::lib::Value arg_data, ::cw::lib::Integer arg_index)
{
        const auto supplied_header = object_as<::cw::header::CardHeader>(arg_data);
    auto trash = [&](const std::shared_ptr<::cw::header::CardHeader>& header,
                     const std::optional<std::shared_ptr<::cw::data::Party>>& party = std::nullopt) {
        if (!header || !::cw::g_cwpy) return;
        ::cw::g_cwpy->trade("TRASHBOX", std::nullopt, header, true, nullptr, -1, -1,
                            false, false, party, false, false, false, false, 0);
    };
    auto remove_from_headers = [&](std::vector<std::shared_ptr<::cw::header::CardHeader>>& cards,
                                    ::cw::lib::Integer index) -> std::shared_ptr<::cw::header::CardHeader> {
        // Python版と同様、古い検索結果が返す-1も含め、負
        // の位置を維持する。 空のポケットは引き続き例外とする。
        if (index < 0) index += static_cast<::cw::lib::Integer>(cards.size());
        if (index < 0 || static_cast<std::size_t>(index) >= cards.size())
            throw std::out_of_range("card pocket index out of range");
        const auto header = cards[static_cast<std::size_t>(index)];
        trash(header);
        return header;
    };

    if (const auto party = object_as<::cw::data::Party>(arg_owner)) {
        auto header = supplied_header;
        if (!header && arg_index >= 0 && static_cast<std::size_t>(arg_index) < party->m_backpack.size())
            header = party->m_backpack[static_cast<std::size_t>(arg_index)];
        if (header) {
            trash(header, party);
        }
        return header;
    }
    if (const auto yado = object_as<::cw::data::YadoData>(arg_owner)) {
        if (supplied_header) {
            trash(supplied_header);
            return supplied_header;
        }
        return remove_from_headers(yado->m_storehouse, arg_index);
    }
    if (const auto list = list_from_value(arg_owner)) {
        return remove_from_headers(list->m_headers_ref ? *list->m_headers_ref : list->m_headers, arg_index);
    }
    if (const auto character = object_as<::cw::character::Character>(arg_owner)) {
        if (supplied_header) {
            trash(supplied_header);
            return supplied_header;
        }

        const auto data = object_as<::cw::data::CWPyElement>(arg_data);
        if (data) {
            const auto type = data->m_tag;
            auto* cards = type == "SkillCard" ? &std::get<0>(character->m_cardpocket_typed) :
                          type == "ItemCard" ? &std::get<1>(character->m_cardpocket_typed) :
                          type == "BeastCard" ? &std::get<2>(character->m_cardpocket_typed) : nullptr;
            if (cards) {
                // Python版と同様、owner.cardpocketの正規のヘッダー
                // を選び、 一覧と埋め込みXMLの両方の除去をtrade()へ任せる。
                return remove_from_headers(*cards, arg_index);
            }
        }
    }
    return {};
}

// 通常のPython呼び出し元は集合を渡すが、ネイティブはベクターを集め
// るため、 同一オブジェクトの重複を除いて所有者ごとに一度だけ書く。
// 無関係・nullのValueは書き込み先なしとして無視し、裸のキャラ
// クターElementは共通のElementTree書き込み用に包む。
void CardEditDialog::_write_results(std::vector<::cw::lib::Value> arg_writes)
{
    // 複数の結果行が同じ保存対象を指す場合も、同一オブジェクトには一度だけ書く。 XML書き込みはElementTreeを受け取るため、裸のCharacter要素は包む。
    std::set<::cw::lib::Object*> written;
    for (const auto& value : arg_writes) {
        const auto object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
        if (!object || !*object || !written.insert(object->get()).second) continue;
        if (const auto party = std::dynamic_pointer_cast<::cw::data::Party>(*object)) {
            party->write();
        } else if (const auto tree = std::dynamic_pointer_cast<::cw::data::CWPyElementTree>(*object)) {
            tree->write_xml(false);
        } else if (const auto character = std::dynamic_pointer_cast<::cw::character::Character>(*object)) {
            if (const auto character_tree = object_as<::cw::data::CWPyElementTree>(character->m_data)) character_tree->write_xml(false);
            else if (const auto element = object_as<::cw::data::CWPyElement>(character->m_data)) {
                auto tree2 = std::make_shared<::cw::data::CWPyElementTree>(element);
                tree2->write_xml(false);
            }
        }
    }
}

void CardEditDialog::OnCardSelected(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    _update_enable();
}

// 共通Presentationは実行環境が切り離された後も閉じられ
// る。 ホスト破棄前に文書のコールバックをキャンセルし、検索ワーカ
// ーの終了を待つ。 整列は残る宿・パーティの所有者がある場合だけ行
// う。Python版はwxの寿命とydataの存在を前提とする。
void CardEditDialog::OnClose(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    cancel_document_request();
    deactivate_dialog(*this, false);
    m_find_cancel = true;
    if (m_find_thread.joinable() &&
        m_find_thread.get_id() != std::this_thread::get_id())
        ::cw::lib::without_runtime_access([&] { m_find_thread.join(); });
    m__find = false;
    m_presentation_finished = true;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata) {
        auto yado = *::cw::g_cwpy->m_ydata;
        if (yado) yado->sort_storehouse(std::nullopt);
        if (yado && yado->m_party && *yado->m_party)
            (*yado->m_party)->sort_backpack(std::nullopt, std::nullopt);
    }
    deactivate_dialog(*this, true);
    const auto& services = card_edit_host_services();
    if (services->destroy) services->destroy(*this);
}

// 不完全なネイティブ互換Elementは、それ自身がPropertyの場
// 合があるため、 card_property は子のPropertyがな
// ければそのルートを使う。 Pythonの find_exists("Property")
// の例外規則とは異なる、既存の代替動作として残す。
Matcher CardEditDialog::_get_matcher(::cw::lib::Value arg_data)
{
    ::cw::lib::String cardtype;
    ::cw::lib::String name;
    ::cw::lib::String desc;
    ::cw::lib::String scenario;
    ::cw::lib::String author;

    if (const auto header = object_as<::cw::header::CardHeader>(arg_data)) {
        cardtype = header->m_type;
        if (m_match_name) name = header->m_name;
        if (m_match_desc) desc = header->m_desc;
        if (m_match_scenario) scenario = header->m_scenario;
        if (m_match_author) author = header->m_author;
    } else if (const auto element = object_as<::cw::data::CWPyElement>(arg_data)) {
        cardtype = element->m_tag;
        const auto property = card_property(element);
        // カードのプロパティはXML属性ではなく子要素。 ここでget()
        // を使うと、PythonのProperty.gettext()
        // と異なり、全XMLカードが空文字列として一致してしまう。
        if (property) {
            if (m_match_name) name = property->gettext("Name", "");
            if (m_match_desc) desc = property->gettext("Description", "");
            if (m_match_scenario) scenario = property->gettext("Scenario", "");
            if (m_match_author) author = property->gettext("Author", "");
        }
    }
    return {std::move(cardtype), std::move(name), std::move(desc),
            std::move(scenario), std::move(author)};
}

std::optional<::cw::lib::Integer> CardEditDialog::_get_imgidx(::cw::lib::Value arg_data)
{
    ::cw::lib::String cardtype;
    if (const auto header = object_as<::cw::header::CardHeader>(arg_data)) cardtype = header->m_type;
    else if (const auto element = object_as<::cw::data::CWPyElement>(arg_data)) cardtype = element->m_tag;
    if (cardtype == "SkillCard") return m_timgidx_skill;
    if (cardtype == "ItemCard") return m_timgidx_item;
    if (cardtype == "BeastCard") return m_timgidx_beast;
    return std::nullopt;
}

// 共通の選択は、wxのGetNextItem列挙ではなく行位置
// のベクターで表す。 範囲内で重複なしに正規化し、不一致・null
// のヘッダーとツリーの組を除いてから照合用のマップを作る。
std::map<Matcher, std::tuple<std::shared_ptr<::cw::header::CardHeader>, std::shared_ptr<::cw::data::CWPyElementTree>>> CardEditDialog::_get_cards(bool arg_selected)
{
    std::map<Matcher, std::tuple<std::shared_ptr<::cw::header::CardHeader>, std::shared_ptr<::cw::data::CWPyElementTree>>> cards;
    std::set<std::size_t> selected;
    if (arg_selected) {
        for (const auto index : m_selected_indices)
            if (index >= 0 && static_cast<std::size_t>(index) < m_list.size()) selected.insert(static_cast<std::size_t>(index));
    } else {
        for (std::size_t i = 0; i < m_list.size(); ++i) selected.insert(i);
    }
    for (const auto index : selected) {
        if (index >= m_datalist.size() || !m_list[index] || !m_datalist[index]) continue;
        cards[_get_matcher(object_value(m_list[index]))] = {m_list[index], m_datalist[index]};
    }
    return cards;
}

// 行・位置の対応表も含め、シナリオの結果スナップショットを再構築
// する。 wxの項目ハンドルの代わりにPresentation
// が保持するため、行・選択・検索・対象の状態をまとめて消す。
// 共通モデルはステータス文字列も提供し、initialize・
// select_scenarioが構築後に操作可否を更新する。
void CardEditDialog::_update_cards()
{
    m_list.clear();
    m_datalist.clear();
    m_selected_indices.clear();
    m_card_rows.clear();
    m_target_cards.clear();
    m_target_table.clear();
    m_search_records.clear();
    if (!m_scdata_typed) {
        m_scenario_label = "(シナリオ未選択)";
        m_status_text = "対象はありません";
        const auto& services = card_edit_host_services();
        if (services->draw) services->draw(*this, false);
        _update_enable();
        return;
    }

    m_scenario_label = string_value(m_scdata_typed->m_name);
    auto append_cards = [&](const std::vector<::cw::lib::Integer>& ids,
                            const std::function<std::optional<std::shared_ptr<::cw::data::CWPyElement>>(::cw::lib::Integer)>& get_data) {
        std::vector<::cw::lib::Integer> sorted_ids = ids;
        std::sort(sorted_ids.begin(), sorted_ids.end());
        for (const auto resid : sorted_ids) {
            const auto element = get_data(resid);
            if (!element || !*element) continue;
            auto tree = std::make_shared<::cw::data::CWPyElementTree>(*element);
            auto header = std::make_shared<::cw::header::CardHeader>();
            header->initialize(std::nullopt, std::nullopt, tree->getroot(), true,
                               m_scdata_typed->m_scedir, false, std::nullopt,
                               std::nullopt, {}, {});
            header->m_negaflag = false;
            m_list.push_back(header);
            m_datalist.push_back(std::move(tree));
            CardRow row;
            row.id = header->m_id;
            row.name = header->m_name;
            row.description = header->m_desc;
            for (std::size_t pos = 0; (pos = row.description.find("\\n", pos)) != ::cw::lib::String::npos; )
                row.description.erase(pos, 2);
            const auto type = header->m_type;
            row.image = type == "SkillCard" ? m_timgidx_skill :
                        type == "ItemCard" ? m_timgidx_item :
                        type == "BeastCard" ? m_timgidx_beast : std::optional<::cw::lib::Integer>{};
            m_card_rows.push_back(std::move(row));
        }
    };

    append_cards(m_scdata_typed->get_skillids(), [&](::cw::lib::Integer resid) {
        return m_scdata_typed->get_skilldata(resid, "", false, std::nullopt);
    });
    append_cards(m_scdata_typed->get_itemids(), [&](::cw::lib::Integer resid) {
        return m_scdata_typed->get_itemdata(resid, "", false, std::nullopt);
    });
    append_cards(m_scdata_typed->get_beastids(), [&](::cw::lib::Integer resid) {
        return m_scdata_typed->get_beastdata(resid, "", false, std::nullopt);
    });
    _update_bookmarkname();
    // ネイティブUIの拡張として、古い検索文を残さず、 新しい
    // 一覧の利用可能カード数を共通のステータス行に表示する。
    m_status_text = std::to_string(m_list.size()) + "件のカードを選択できます。";
    const auto& services = card_edit_host_services();
    if (services->draw) services->draw(*this, false);
}

// ブックマークの改名は、切り離されたPresentationの更新中にも呼
// ばれる。 パス・シナリオ・実行環境・設定がなければ、Pythonの全体状
// 態を参照せず何もしない。 有効な操作では、対応する正規化キーを更新する。
void CardEditDialog::_update_bookmarkname()
{
    if (m_scpath.empty() || !m_scdata_typed || !::cw::g_cwpy || !::cw::g_cwpy->m_setting) return;
    const auto key = ::cw::util::get_keypath(m_scpath);
    for (auto& [path, name] : ::cw::g_cwpy->m_setting->m_bookmarks_for_cardedit)
        if (::cw::util::get_keypath(path) == key) name = string_value(m_scdata_typed->m_name);
}

void CardEditDialog::_update_enable()
{
    const bool selected = !m_selected_indices.empty();
    bool has_card = false;
    // Python版と同様、更新・削除の可否は現在のチェック状態
    // ではなくtarget_tableで決める。 親・ツリーのチ
    // ェックで全カードを外しても、検索対象は残り編集可能なため。
    for (const auto& entries : m_target_table)
        if (!entries.second.empty()) { has_card = true; break; }
    m_detail_enabled = !m_list.empty();
    m_deal_enabled = selected;
    m_find_enabled = selected && !m__find;
    m_stop_enabled = m__find;
    m_can_update = has_card && !m__find;
    m_can_delete = has_card && !m__find;
    const auto& services = card_edit_host_services();
    if (services->sync_enabled) services->sync_enabled(*this);
    m_presentation_dirty = true;
}

void CardEditDialog::select_card(::cw::lib::Integer arg_index, bool arg_selected)
{
    if (arg_index < 0 || static_cast<std::size_t>(arg_index) >= m_list.size()) return;
    const auto it = std::find(m_selected_indices.begin(), m_selected_indices.end(), arg_index);
    if (arg_selected) {
        if (it == m_selected_indices.end()) m_selected_indices.push_back(arg_index);
    } else if (it != m_selected_indices.end()) {
        m_selected_indices.erase(it);
    }
    std::sort(m_selected_indices.begin(), m_selected_indices.end());
    for (std::size_t i = 0; i < m_list.size(); ++i)
        if (m_list[i]) m_list[i]->m_negaflag = std::binary_search(m_selected_indices.begin(),
                                                                    m_selected_indices.end(),
                                                                    static_cast<::cw::lib::Integer>(i));
    _update_enable();
}

void CardEditDialog::set_target_checked(::cw::lib::Integer arg_index, bool arg_checked)
{
    if (arg_index < 0 || static_cast<std::size_t>(arg_index) >= m_search_records.size()) return;
    m_search_records[static_cast<std::size_t>(arg_index)].checked = arg_checked;
    _update_enable();
}

void CardEditDialog::set_deal_target(DealTarget arg_target,
                                      std::shared_ptr<::cw::character::Character> arg_character)
{
    m_deal_target = arg_target;
    m_deal_character = std::move(arg_character);
    _update_enable();
}

namespace {

struct CardEditTargetRow {
    bool group{};
    ::cw::lib::String path{};
    ::cw::lib::String label{};
    ::cw::lib::Integer record{-1};
    ::cw::lib::Integer depth{};
    bool checked{};
    bool partial{};
};

std::vector<std::pair<::cw::lib::String, ::cw::lib::String>> card_location_nodes(const SearchRecord& record)
{
    if (!record.location_nodes.empty()) return record.location_nodes;
    // 旧APIから作った記録は、一つの所有者ノードを保持する。 表示用の
    // 区切りを階層の区切りにはせず、通常の検索は上で型付きノードを渡す。
    const auto owner = record.character ? static_cast<const void*>(record.character.get()) :
        record.party ? static_cast<const void*>(record.party.get()) : static_cast<const void*>(record.top_level.get());
    return {{owner ? std::to_string(reinterpret_cast<std::uintptr_t>(owner)) : "other",
             record.location.empty() ? "その他" : record.location}};
}

::cw::lib::String card_location_key(const SearchRecord& record)
{
    ::cw::lib::String path;
    for (const auto& [key,label] : card_location_nodes(record)) {
        if (!path.empty()) path += '/';
        path += key;
    }
    return path;
}

bool card_location_is_below(const ::cw::lib::String& location,
                            const ::cw::lib::String& path)
{
    return location == path ||
        (location.size() > path.size() && location.compare(0,path.size(),path)==0 && location[path.size()]=='/');
}

std::vector<CardEditTargetRow> build_card_target_rows(CardEditDialog& dialog)
{
    struct Node {
        ::cw::lib::String path{};
        ::cw::lib::String label{};
        std::vector<::cw::lib::String> children{};
        std::vector<::cw::lib::Integer> records{};
    };
    std::map<::cw::lib::String, Node> nodes;
    std::vector<::cw::lib::String> roots;
    for (std::size_t i = 0; i < dialog.m_search_records.size(); ++i) {
        const auto& record = dialog.m_search_records[i];
        const auto parts = card_location_nodes(record);
        ::cw::lib::String parent;
        for (const auto& [key,part] : parts) {
            const auto path = parent.empty() ? key : parent + "/" + key;
            auto [it, inserted] = nodes.try_emplace(path);
            if (inserted) {
                it->second.path = path;
                it->second.label = part;
                if (parent.empty()) roots.push_back(path);
                else nodes[parent].children.push_back(path);
            }
            parent = path;
        }
        nodes[parent].records.push_back(static_cast<::cw::lib::Integer>(i));
    }

    std::vector<CardEditTargetRow> rows;
    dialog.m_presentation_target_group_order.clear();
    std::function<void(const ::cw::lib::String&, ::cw::lib::Integer)> append =
        [&](const ::cw::lib::String& path, const ::cw::lib::Integer depth) {
            const auto node_it = nodes.find(path);
            if (node_it == nodes.end()) return;
            bool any = false;
            bool all = true;
            for (const auto& record : dialog.m_search_records) {
                const auto location = card_location_key(record);
                if (!card_location_is_below(location, path) || record.removed) continue;
                any = true;
                all = all && record.checked;
            }
            rows.push_back({true, path, node_it->second.label, -1, depth,
                            any && all, any && !all});
            dialog.m_presentation_target_group_order.push_back(path);
            if (dialog.m_presentation_collapsed_locations.contains(path)) return;
            for (const auto& child : node_it->second.children) append(child, depth + 1);
            for (const auto record_index : node_it->second.records) {
                const auto& record = dialog.m_search_records[static_cast<std::size_t>(record_index)];
                rows.push_back({false, path, record.label, record_index, depth + 1,
                                record.checked, false});
            }
        };
    for (const auto& root : roots) append(root, 0);
    return rows;
}

void draw_cardedit_debug_icon(const std::shared_ptr<::cw::lib::Surface>& surface,
                              const ::cw::lib::String& name,
                              const ::cw::lib::Integer x,
                              const ::cw::lib::Integer y)
{
    static std::map<::cw::lib::String, std::shared_ptr<::cw::lib::Surface>> cache;
    auto& icon = cache[name];
    if (!icon) icon = ::cw::util::load_wxbmp("Data/Debugger/" + name + ".png", true,
        {}, {}, std::nullopt, true, false, false, ::cw::lib::Value(1));
    if (icon) ::cw::lib::ui::blend_surface(surface, icon, x, y, 16, 16);
}

::cw::lib::String cardedit_card_icon_name(const std::optional<::cw::lib::Integer> image)
{
    if (!image) return {};
    if (*image == 0) return "EVT_GET_SKILL";
    if (*image == 1) return "EVT_GET_ITEM";
    if (*image == 2) return "EVT_GET_BEAST";
    return {};
}

::cw::lib::String cardedit_record_icon_name(const SearchRecord& record)
{
    ::cw::lib::String type;
    if (const auto* header = std::get_if<std::shared_ptr<::cw::header::CardHeader>>(&record.data)) {
        if (*header) type = (*header)->m_type;
    } else if (const auto* element = std::get_if<std::shared_ptr<::cw::data::CWPyElement>>(&record.data)) {
        if (*element) type = (*element)->m_tag;
    }
    if (type == "SkillCard") return "EVT_GET_SKILL";
    if (type == "ItemCard") return "EVT_GET_ITEM";
    if (type == "BeastCard") return "EVT_GET_BEAST";
    return {};
}

::cw::lib::String cardedit_group_icon_name(const ::cw::lib::String& path,
                                           const ::cw::lib::String& label)
{
    const auto begin = path.find_last_of('/');
    const auto key = path.substr(begin == ::cw::lib::String::npos ? 0 : begin+1);
    if (key == "backpack" || key == "storehouse") return "SACK";
    if (key == "standby") return "YADO";
    if (key.starts_with("party:")) return "MEMBER";
    (void)label;
    return "EVT_GET_CAST";
}

void erase_last_utf8_codepoint(::cw::lib::String& text)
{
    if (text.empty()) return;
    std::size_t pos = text.size() - 1;
    while (pos > 0 && (static_cast<unsigned char>(text[pos]) & 0xC0U) == 0x80U) --pos;
    text.erase(pos);
}

} // namespace

::cw::dialog::DialogPresentation CardEditDialog::dialog_presentation()
{
    using Action = ::cw::dialog::DialogPresentationAction;
    namespace retained_ui=::cw::debug::retained_presentation;
    constexpr ::cw::lib::Integer width = 640;
    constexpr ::cw::lib::Integer height = 416;
    // 出力解像度のフレーム。文字は最終表示サイズでラスタライズする。
    auto surface = retained_ui::scaled_frame(width, height);

    ::cw::dialog::DialogPresentation out;
    out.route = "DEBUG_CARD_EDIT";
    out.title = "手札カードの編集";
    out.refresh_interval_ms = ::cw::dialog::platform_refresh_interval(m__find ? 33U : 0U);

    const auto button = [&](const ::cw::lib::String& label, const ::cw::lib::Rect& rect,
                            const Action action, const int index, const bool enabled = true) {
        ::cw::dialog::add_presentation_control(out, surface, label, rect, action,
                                               index, enabled, "button", true);
    };
    const auto label = [&](const ::cw::lib::String& text, const ::cw::lib::Integer x,
                           const ::cw::lib::Integer y, const bool bold = false) {
        ::cw::lib::ui::draw_label(surface, text, x, y, 13, bold, {0, 0, 0});
    };
    const auto checkbox = [&](const ::cw::lib::String& text, const ::cw::lib::Rect& rect,
                              const bool checked, const Action action, const int index,
                              const bool enabled = true, const bool partial = false) {
        const ::cw::lib::Rect box{rect.x,rect.y+(rect.height-14)/2,14,14};
        ::cw::lib::ui::draw_checkbox(surface,box,checked,enabled,partial);
        if(!text.empty()) label(text,rect.x+20,rect.y+(rect.height-13)/2);
        out.controls.push_back({action,index,static_cast<float>(rect.x),static_cast<float>(rect.y),
            static_cast<float>(rect.width),static_cast<float>(rect.height),enabled,false,text,{}});
    };

    button(m_scenario_label, {8, 8, 500, 30}, Action::Auxiliary, 0, true);
    button("★", {514, 8, 44, 30}, Action::Auxiliary, 1, true);
    label("検索語:", 8, 51);
    const ::cw::lib::Rect search_rect{67, 44, 491, 28};
    ::cw::lib::ui::fill_rect(surface, search_rect,
        m_presentation_focus == 16 ? ::cw::lib::Color{255, 255, 255, 255}
                                   : ::cw::lib::Color{248, 248, 248, 255});
    ::cw::lib::ui::draw_sunken_border(surface, search_rect);
    auto search_display = m_search_query;
    if (m_presentation_focus == 16 && !m_presentation_search_composition.empty())
        search_display += m_presentation_search_composition;
    label(search_display, search_rect.x + 5, search_rect.y + 6);
    out.controls.push_back({Action::Auxiliary, 16, static_cast<float>(search_rect.x),
        static_cast<float>(search_rect.y), static_cast<float>(search_rect.width),
        static_cast<float>(search_rect.height), !m__find, false});
    out.wants_text_input = m_presentation_focus == 16 && !m__find &&
        !m_presentation_deal_popup && !m_presentation_bookmark_popup;

    label("カードの選択", 8, 78, true);
    const ::cw::lib::Rect cards_rect{8, 96, 300, 196};
    ::cw::lib::ui::fill_rect(surface, cards_rect, {255, 255, 255, 255});
    ::cw::lib::ui::draw_sunken_border(surface, cards_rect);
    constexpr ::cw::lib::Integer row_height = 23;
    constexpr ::cw::lib::Integer visible_rows = 7;
    const ::cw::lib::Rect card_header{10, 98, 296, 24};
    ::cw::lib::ui::fill_rect(surface, card_header, {235, 235, 235, 255});
    label("ID", 14, 103, true);
    label("カード名", 64, 103, true);
    label("解説", 167, 103, true);
    ::cw::lib::ui::fill_rect(surface, {58, 98, 1, 24}, {170, 170, 170, 255});
    ::cw::lib::ui::fill_rect(surface, {161, 98, 1, 24}, {170, 170, 170, 255});
    const auto card_max = (std::max<::cw::lib::Integer>)(
        0, static_cast<::cw::lib::Integer>(m_card_rows.size()) - visible_rows);
    m_presentation_card_scroll = std::clamp(
        m_presentation_card_scroll, ::cw::lib::Integer{0}, card_max);
    for (::cw::lib::Integer row = 0; row < visible_rows; ++row) {
        const auto absolute = m_presentation_card_scroll + row;
        if (absolute < 0 || static_cast<std::size_t>(absolute) >= m_card_rows.size()) break;
        const auto& card = m_card_rows[static_cast<std::size_t>(absolute)];
        const ::cw::lib::Rect rect{10, 123 + row * row_height, 296, row_height};
        const bool selected = std::find(m_selected_indices.begin(), m_selected_indices.end(),
                                        absolute) != m_selected_indices.end();
        if (selected) ::cw::lib::ui::fill_rect(surface, rect, {210, 225, 248, 255});
        const auto icon_name = cardedit_card_icon_name(card.image);
        if (!icon_name.empty()) draw_cardedit_debug_icon(surface, icon_name, rect.x + 48,
                                                          retained_ui::centered_y(rect,16));
        const auto text_y=retained_ui::centered_y(rect,13);
        label(std::to_string(card.id), rect.x + 4, text_y);
        label(card.name, rect.x + 70, text_y);
        label(card.description, rect.x + 153, text_y);
        out.controls.push_back({Action::Select, static_cast<int>(absolute),
            static_cast<float>(rect.x), static_cast<float>(rect.y),
            static_cast<float>(rect.width), static_cast<float>(rect.height), true, false});
    }
    if (card_max > 0) {
        button("▲", {278, 123, 28, 25}, Action::Previous, 0);
        button("▼", {278, 265, 28, 25}, Action::Next, 0);
    }

    label("処理対象", 320, 78, true);
    const ::cw::lib::Rect target_rect{320, 96, 312, 196};
    ::cw::lib::ui::fill_rect(surface, target_rect, {255, 255, 255, 255});
    ::cw::lib::ui::draw_sunken_border(surface, target_rect);
    const auto target_rows = build_card_target_rows(*this);
    const auto target_max = (std::max<::cw::lib::Integer>)(
        0, static_cast<::cw::lib::Integer>(target_rows.size()) - visible_rows);
    m_presentation_target_scroll = std::clamp(
        m_presentation_target_scroll, ::cw::lib::Integer{0}, target_max);
    for (::cw::lib::Integer row = 0; row < visible_rows; ++row) {
        const auto absolute = m_presentation_target_scroll + row;
        if (absolute < 0 || static_cast<std::size_t>(absolute) >= target_rows.size()) break;
        const auto& target = target_rows[static_cast<std::size_t>(absolute)];
        const ::cw::lib::Rect rect{322, 98 + row * row_height, 308, row_height};
        const auto record_removed = !target.group && target.record >= 0 &&
            m_search_records[static_cast<std::size_t>(target.record)].removed;
        if (record_removed) ::cw::lib::ui::fill_rect(surface, rect, {230, 230, 230, 255});
        const auto indent = target.depth * 14;
        if (target.group) {
            const bool collapsed = m_presentation_collapsed_locations.contains(target.path);
            label(collapsed ? "▶" : "▼", rect.x + 3 + indent, retained_ui::centered_y(rect,13));
            ::cw::lib::ui::draw_checkbox(surface,{rect.x+20+indent,retained_ui::centered_y(rect,14),14,14},
                                         target.checked,true,target.partial);
            draw_cardedit_debug_icon(surface, cardedit_group_icon_name(target.path, target.label),
                                     rect.x + 40 + indent, retained_ui::centered_y(rect,16));
            label(target.label, rect.x + 60 + indent, retained_ui::centered_y(rect,13), true);
            const auto group_it = std::find(m_presentation_target_group_order.begin(),
                                            m_presentation_target_group_order.end(), target.path);
            const auto group_index = static_cast<int>(std::distance(
                m_presentation_target_group_order.begin(), group_it));
            out.controls.push_back({Action::Auxiliary, 20000 + group_index,
                static_cast<float>(rect.x), static_cast<float>(rect.y),
                static_cast<float>(rect.width), static_cast<float>(rect.height), true, false});
            out.controls.push_back({Action::Toggle, 30000 + group_index,
                static_cast<float>(rect.x + 18 + indent), static_cast<float>(rect.y),
                22.0F, static_cast<float>(rect.height), true, false});
            continue;
        }
        const auto& record = m_search_records[static_cast<std::size_t>(target.record)];
        ::cw::lib::ui::draw_checkbox(surface,{rect.x+20+indent,retained_ui::centered_y(rect,14),14,14},target.checked,!record_removed);
        const auto icon_name = cardedit_record_icon_name(record);
        if (!icon_name.empty()) draw_cardedit_debug_icon(surface, icon_name, rect.x + 40 + indent,
                                                          retained_ui::centered_y(rect,16));
        label(target.label, rect.x + 60 + indent, retained_ui::centered_y(rect,13));
        out.controls.push_back({Action::Toggle, static_cast<int>(target.record),
            static_cast<float>(rect.x), static_cast<float>(rect.y),
            static_cast<float>(rect.width), static_cast<float>(rect.height),
            !record_removed, false});
    }
    if (target_max > 0) {
        button("▲", {602, 98, 28, 25}, Action::Previous, 1);
        button("▼", {602, 265, 28, 25}, Action::Next, 1);
    }

    checkbox("カード名",{8,300,105,27},m_match_name,Action::Auxiliary,9,!m__find);
    checkbox("解説",{117,300,85,27},m_match_desc,Action::Auxiliary,10,!m__find);
    checkbox("シナリオ",{206,300,105,27},m_match_scenario,Action::Auxiliary,11,!m__find);
    checkbox("作者",{315,300,85,27},m_match_author,Action::Auxiliary,12,!m__find);

    const auto deal_label = m_deal_target_labels.empty()
        ? ::cw::lib::String("配付先なし")
        : m_deal_target_labels[static_cast<std::size_t>(std::clamp<::cw::lib::Integer>(
              m_presentation_deal_committed_index, 0, static_cast<::cw::lib::Integer>(m_deal_target_labels.size() - 1)))];
    const auto deal_icon = deal_label == "カード置場" ? ::cw::lib::String("SACK") :
        deal_label == "荷物袋" ? ::cw::lib::String("SACK") :
        deal_label == "パーティ全員" ? ::cw::lib::String("MEMBER") :
        ::cw::lib::String("EVT_GET_CAST");
    button("   " + deal_label + "  ▼", {8, 333, 201, 27}, Action::Auxiliary, 14,
           !m_deal_target_labels.empty());
    draw_cardedit_debug_icon(surface, deal_icon, 14, 338);
    button("情報", {215, 333, 52, 27}, Action::Auxiliary, 2, m_detail_enabled);
    button("配付", {271, 333, 52, 27}, Action::Auxiliary, 3, m_deal_enabled);
    button("検索", {327, 333, 52, 27}, Action::Auxiliary, 4, m_find_enabled);
    button("中断", {383, 333, 52, 27}, Action::Auxiliary, 5, m_stop_enabled);
    button("更新", {439, 333, 52, 27}, Action::Auxiliary, 6, m_can_update);
    button("除去", {495, 333, 52, 27}, Action::Auxiliary, 7, m_can_delete);
    button("閉じる", {551, 333, 81, 27}, Action::Cancel, 8, true);
    label(m_status_text, 8, 374);

    if (m_presentation_deal_popup && !m_deal_target_labels.empty()) {
        const auto count = (std::min<std::size_t>)(m_deal_target_labels.size(), 8);
        const auto first = static_cast<std::size_t>(std::clamp<::cw::lib::Integer>(
            m_presentation_deal_index - 3, 0,
            (std::max<::cw::lib::Integer>)(0,
                static_cast<::cw::lib::Integer>(m_deal_target_labels.size() - count))));
        const ::cw::lib::Rect popup{8, 333 - static_cast<::cw::lib::Integer>(count * 25),
                                    201, static_cast<::cw::lib::Integer>(count * 25)};
        ::cw::lib::ui::fill_rect(surface, popup, {255, 255, 255, 255});
        ::cw::lib::ui::draw_sunken_border(surface, popup);
        for (std::size_t row = 0; row < count; ++row) {
            const auto item = first + row;
            const ::cw::lib::Rect rect{popup.x + 2,
                popup.y + 2 + static_cast<::cw::lib::Integer>(row * 25), popup.width - 4, 23};
            if (item == static_cast<std::size_t>(m_presentation_deal_index))
                ::cw::lib::ui::fill_rect(surface, rect, {210, 225, 248, 255});
            const auto& item_label = m_deal_target_labels[item];
            const auto item_icon = item_label == "カード置場" ? ::cw::lib::String("SACK") :
                item_label == "荷物袋" ? ::cw::lib::String("SACK") :
                item_label == "パーティ全員" ? ::cw::lib::String("MEMBER") :
                ::cw::lib::String("EVT_GET_CAST");
            draw_cardedit_debug_icon(surface, item_icon, rect.x + 4, retained_ui::centered_y(rect,16));
            label(item_label, rect.x + 25, retained_ui::centered_y(rect,13));
            out.controls.push_back({Action::Create, 4000 + static_cast<int>(item),
                static_cast<float>(rect.x), static_cast<float>(rect.y),
                static_cast<float>(rect.width), static_cast<float>(rect.height), true, false});
        }
    }
    if (m_presentation_bookmark_popup && !m_bookmark_entries.empty()) {
        const auto count = (std::min<std::size_t>)(m_bookmark_entries.size(), 10);
        const auto first = static_cast<std::size_t>(std::clamp<::cw::lib::Integer>(
            m_presentation_bookmark_index - 4, 0,
            (std::max<::cw::lib::Integer>)(0,
                static_cast<::cw::lib::Integer>(m_bookmark_entries.size() - count))));
        const ::cw::lib::Rect popup{356, 38, 276, static_cast<::cw::lib::Integer>(count * 25 + 4)};
        ::cw::lib::ui::fill_rect(surface, popup, {255, 255, 255, 255});
        ::cw::lib::ui::draw_sunken_border(surface, popup);
        for (std::size_t row = 0; row < count; ++row) {
            const auto item = first + row;
            const ::cw::lib::Rect rect{popup.x + 2,
                popup.y + 2 + static_cast<::cw::lib::Integer>(row * 25), popup.width - 4, 23};
            if (item == static_cast<std::size_t>(m_presentation_bookmark_index))
                ::cw::lib::ui::fill_rect(surface, rect, {210, 225, 248, 255});
            const bool enabled = item == 0 ? m_add_bookmark_enabled :
                                 item == 1 ? m_arrange_bookmark_enabled : true;
            label(std::get<2>(m_bookmark_entries[item]), rect.x + 6,
                  retained_ui::centered_y(rect,13));
            out.controls.push_back({Action::Create, 5000 + static_cast<int>(item),
                static_cast<float>(rect.x), static_cast<float>(rect.y),
                static_cast<float>(rect.width), static_cast<float>(rect.height), enabled, false});
        }
    }

    auto& retained=dialog_retained_surface();
    ::cw::lib::ui::ScaledRetainedCanvas canvas(
        retained,{width,height},retained_ui::dpi_scale(),{245,245,245,255});
    const ::cw::lib::Rect header_region{0,0,width,76},cards_region{0,76,316,220},
        targets_region{316,76,324,220},options_region{0,296,width,34},
        commands_region{0,330,width,40},status_region{0,370,width,46};
    std::uint64_t header_signature{};retained_ui::mix(header_signature,m_scenario_label);
    retained_ui::mix(header_signature,m_search_query);retained_ui::mix(header_signature,m_presentation_search_composition);
    retained_ui::mix(header_signature,static_cast<std::uint64_t>(m_presentation_focus+1));
    retained_ui::mix(header_signature,static_cast<std::uint64_t>(m__find));
    retained_ui::mix(header_signature,static_cast<std::uint64_t>(m_presentation_bookmark_popup));
    std::uint64_t cards_signature=static_cast<std::uint64_t>(m_presentation_card_scroll+1);
    for(const auto index:m_selected_indices)retained_ui::mix(cards_signature,static_cast<std::uint64_t>(index+1));
    for(const auto& card:m_card_rows){retained_ui::mix(cards_signature,static_cast<std::uint64_t>(card.id+1));
        retained_ui::mix(cards_signature,card.name);retained_ui::mix(cards_signature,card.description);
        retained_ui::mix(cards_signature,static_cast<std::uint64_t>(card.image.value_or(-1)+1));}
    std::uint64_t targets_signature=static_cast<std::uint64_t>(m_presentation_target_scroll+1);
    for(const auto& record:m_search_records){retained_ui::mix(targets_signature,record.location);
        retained_ui::mix(targets_signature,record.label);
        for(const auto& [key,label]:record.location_nodes){retained_ui::mix(targets_signature,key);retained_ui::mix(targets_signature,label);}
        retained_ui::mix(targets_signature,static_cast<std::uint64_t>(record.removed));
        retained_ui::mix(targets_signature,static_cast<std::uint64_t>(record.checked));}
    for(const auto& path:m_presentation_collapsed_locations)retained_ui::mix(targets_signature,path);
    std::uint64_t options_signature=static_cast<std::uint64_t>(m_match_name);
    retained_ui::mix(options_signature,static_cast<std::uint64_t>(m_match_desc));
    retained_ui::mix(options_signature,static_cast<std::uint64_t>(m_match_scenario));
    retained_ui::mix(options_signature,static_cast<std::uint64_t>(m_match_author));
    retained_ui::mix(options_signature,static_cast<std::uint64_t>(m__find));
    std::uint64_t commands_signature=static_cast<std::uint64_t>(m_presentation_deal_index+1);
    retained_ui::mix(commands_signature,static_cast<std::uint64_t>(m_detail_enabled));
    retained_ui::mix(commands_signature,static_cast<std::uint64_t>(m_presentation_deal_committed_index+1));
    retained_ui::mix(commands_signature,static_cast<std::uint64_t>(m_deal_enabled));
    retained_ui::mix(commands_signature,static_cast<std::uint64_t>(m_find_enabled));
    retained_ui::mix(commands_signature,static_cast<std::uint64_t>(m_stop_enabled));
    retained_ui::mix(commands_signature,static_cast<std::uint64_t>(m_can_update));
    retained_ui::mix(commands_signature,static_cast<std::uint64_t>(m_can_delete));
    retained_ui::mix(commands_signature,static_cast<std::uint64_t>(m_presentation_deal_popup));
    for(const auto& value:m_deal_target_labels)retained_ui::mix(commands_signature,value);
    const bool paint_header=canvas.component_changed("debug.cardedit.header",header_region,header_signature);
    const bool paint_cards=canvas.component_changed("debug.cardedit.cards",cards_region,cards_signature);
    const bool paint_targets=canvas.component_changed("debug.cardedit.targets",targets_region,targets_signature);
    const bool paint_options=canvas.component_changed("debug.cardedit.options",options_region,options_signature);
    const bool paint_commands=canvas.component_changed("debug.cardedit.commands",commands_region,commands_signature);
    const bool paint_status=canvas.component_changed("debug.cardedit.status",status_region,
        std::hash<::cw::lib::String>{}(m_status_text));
    ::cw::lib::Rect deal_popup_region{},bookmark_popup_region{};bool paint_deal_popup=false,paint_bookmark_popup=false;
    if(m_presentation_deal_popup&&!m_deal_target_labels.empty()){
        const auto count=(std::min<std::size_t>)(m_deal_target_labels.size(),8);
        deal_popup_region={8,333-static_cast<::cw::lib::Integer>(count*25),201,static_cast<::cw::lib::Integer>(count*25)};
        paint_deal_popup=canvas.component_changed("debug.cardedit.deal-popup",deal_popup_region,commands_signature);
    }else (void)retained.remove_component("debug.cardedit.deal-popup");
    if(m_presentation_bookmark_popup&&!m_bookmark_entries.empty()){
        const auto count=(std::min<std::size_t>)(m_bookmark_entries.size(),10);
        bookmark_popup_region={356,38,276,static_cast<::cw::lib::Integer>(count*25+4)};
        std::uint64_t signature=static_cast<std::uint64_t>(m_presentation_bookmark_index+1);
        for(const auto& item:m_bookmark_entries){retained_ui::mix(signature,std::get<0>(item));retained_ui::mix(signature,std::get<2>(item));}
        paint_bookmark_popup=canvas.component_changed("debug.cardedit.bookmark-popup",bookmark_popup_region,signature);
    }else (void)retained.remove_component("debug.cardedit.bookmark-popup");
    canvas.capture_base_and_restore();
    retained_ui::composite_changed(canvas,surface,header_region,paint_header);
    retained_ui::composite_changed(canvas,surface,cards_region,paint_cards);
    retained_ui::composite_changed(canvas,surface,targets_region,paint_targets);
    retained_ui::composite_changed(canvas,surface,options_region,paint_options);
    retained_ui::composite_changed(canvas,surface,commands_region,paint_commands);
    retained_ui::composite_changed(canvas,surface,status_region,paint_status);
    retained_ui::composite_changed(canvas,surface,deal_popup_region,paint_deal_popup);
    retained_ui::composite_changed(canvas,surface,bookmark_popup_region,paint_bookmark_popup);
    retained_ui::finish(out,canvas);
    return out;
}

void CardEditDialog::dialog_presentation_action(
    const ::cw::dialog::DialogPresentationAction action, const int index)
{
    using Action = ::cw::dialog::DialogPresentationAction;
    if (m_presentation_finished || m_document_token) return;
    if (action == Action::Create && index >= 4000) {
        if (index >= 5000) {
            const auto chosen = index - 5000;
            if (chosen == 0 && m_add_bookmark_enabled) OnAddBookmark({});
            else if (chosen == 1 && m_arrange_bookmark_enabled) OnArrangeBookmark({});
            else if (chosen >= 2 && static_cast<std::size_t>(chosen) < m_bookmark_entries.size())
                select_scenario(std::get<0>(m_bookmark_entries[static_cast<std::size_t>(chosen)]));
            m_presentation_bookmark_popup = false;
            m_presentation_dirty = true;
            return;
        }
        const auto selected_index = index - 4000;
        if (selected_index >= 0 && static_cast<std::size_t>(selected_index) < m_deal_target_labels.size()) {
            m_presentation_deal_index = selected_index;
            m_presentation_deal_committed_index = selected_index;
            const auto& selected = m_deal_target_labels[static_cast<std::size_t>(selected_index)];
            if (selected == "カード置場") set_deal_target(DealTarget::Storehouse);
            else if (selected == "荷物袋") set_deal_target(DealTarget::Backpack);
            else if (selected == "パーティ全員") set_deal_target(DealTarget::Party);
            else {
                std::shared_ptr<::cw::character::Character> character;
                const auto character_index = selected_index - m_notcast;
                const auto pcards = ::cw::g_cwpy ? ::cw::g_cwpy->get_pcards({}) :
                    std::vector<std::shared_ptr<::cw::sprite::card::PlayerCard>>{};
                if (character_index >= 0 && static_cast<std::size_t>(character_index) < pcards.size())
                    character = std::dynamic_pointer_cast<::cw::character::Character>(
                        pcards[static_cast<std::size_t>(character_index)]);
                set_deal_target(DealTarget::Character, std::move(character));
            }
        }
        m_presentation_deal_popup = false;
        m_presentation_dirty = true;
        return;
    }
    if (action == Action::Cancel || (action == Action::Auxiliary && index == 8)) {
        if (m_presentation_bookmark_popup) {
            m_presentation_bookmark_popup = false;
            m_presentation_dirty = true;
            return;
        }
        if (m_presentation_deal_popup) {
            m_presentation_deal_popup = false;
            m_presentation_deal_index = m_presentation_deal_committed_index;
            m_presentation_dirty = true;
            return;
        }
        OnClose({});
        return;
    }
    if (action == Action::Select) {
        m_presentation_focus = 100;
        m_presentation_card_cursor = index;
        const bool selected = std::find(m_selected_indices.begin(), m_selected_indices.end(),
                                        static_cast<::cw::lib::Integer>(index)) != m_selected_indices.end();
        select_card(index, !selected);
        return;
    }
    if (action == Action::Toggle) {
        if (index >= 30000) {
            const auto group_index = index - 30000;
            if (group_index >= 0 && static_cast<std::size_t>(group_index) < m_presentation_target_group_order.size()) {
                const auto path = m_presentation_target_group_order[static_cast<std::size_t>(group_index)];
                bool all = true;
                for (const auto& record : m_search_records) {
                    const auto location = card_location_key(record);
                    if (card_location_is_below(location, path) && !record.removed) all = all && record.checked;
                }
                for (auto& record : m_search_records) {
                    const auto location = card_location_key(record);
                    if (card_location_is_below(location, path) && !record.removed) record.checked = !all;
                }
                _update_enable();
            }
            return;
        }
        if (index >= 0 && static_cast<std::size_t>(index) < m_search_records.size()) {
            m_presentation_focus = 101;
            const auto rows = build_card_target_rows(*this);
            const auto it = std::find_if(rows.begin(), rows.end(), [index](const auto& row) {
                return !row.group && row.record == index;
            });
            if (it != rows.end()) m_presentation_target_cursor = static_cast<::cw::lib::Integer>(
                std::distance(rows.begin(), it));
            set_target_checked(index, !m_search_records[static_cast<std::size_t>(index)].checked);
        }
        return;
    }
    if (action == Action::Previous || action == Action::Next) {
        auto& scroll = index == 1 ? m_presentation_target_scroll : m_presentation_card_scroll;
        scroll = (std::max<::cw::lib::Integer>)(0, scroll + (action == Action::Previous ? -4 : 4));
        m_presentation_dirty = true;
        return;
    }
    if (action != Action::Auxiliary) return;

    if (index >= 20000) {
        const auto group_index = index - 20000;
        if (group_index >= 0 && static_cast<std::size_t>(group_index) < m_presentation_target_group_order.size()) {
            const auto path = m_presentation_target_group_order[static_cast<std::size_t>(group_index)];
            const auto rows = build_card_target_rows(*this);
            const auto it = std::find_if(rows.begin(), rows.end(), [&](const auto& row) {
                return row.group && row.path == path;
            });
            if (it != rows.end()) m_presentation_target_cursor = static_cast<::cw::lib::Integer>(
                std::distance(rows.begin(), it));
            m_presentation_focus = 101;
            if (!m_presentation_collapsed_locations.erase(path))
                m_presentation_collapsed_locations.insert(path);
            m_presentation_dirty = true;
        }
        return;
    }

    switch (index) {
    case 0: OnScenario({}); break;
    case 1: {
        if (m_presentation_bookmark_popup) {
            // PythonのブロックするPopupMenuと同様、外側クリックは終了
            // として消費する。 保持型の表示処理でも、同じキャンセルをここで表す。
            m_presentation_bookmark_popup = false;
            m_presentation_dirty = true;
        } else OnBookmark({});
        break;
    }
    case 2: OnDetailBtn({}); break;
    case 3: OnDealBtn({}); break;
    case 4: OnFindBtn({}); break;
    case 5: OnStopBtn({}); break;
    case 6: OnUpdateBtn({}); break;
    case 7: OnDeleteBtn({}); break;
    case 9: m_match_name = !m_match_name; _update_enable(); break;
    case 10: m_match_desc = !m_match_desc; _update_enable(); break;
    case 11: m_match_scenario = !m_match_scenario; _update_enable(); break;
    case 12: m_match_author = !m_match_author; _update_enable(); break;
    case 14: {
        if (m_deal_target_labels.empty()) break;
        m_presentation_focus = 14;
        m_presentation_bookmark_popup = false;
        m_presentation_deal_index = m_presentation_deal_committed_index;
        m_presentation_deal_popup = !m_presentation_deal_popup;
        m_presentation_dirty = true;
        break;
    }
    case 16:
        m_presentation_focus = 16;
        m_presentation_search_select_all = true;
        m_presentation_deal_popup = false;
        m_presentation_bookmark_popup = false;
        m_presentation_dirty = true;
        break;
    default: break;
    }
}

void CardEditDialog::dialog_presentation_text(const std::string& text, const bool composition)
{
    if (m_presentation_focus != 16 || m__find || m_presentation_deal_popup ||
        m_presentation_bookmark_popup) return;
    if (composition) {
        m_presentation_search_composition = text;
        m_presentation_dirty = true;
        return;
    }
    m_presentation_search_composition.clear();
    if (m_presentation_search_select_all) {
        m_search_query.clear();
        m_presentation_search_select_all = false;
    }
    if (text == "\b") {
        erase_last_utf8_codepoint(m_search_query);
    } else {
        const auto room = static_cast<std::size_t>(255) > m_search_query.size()
            ? static_cast<std::size_t>(255) - m_search_query.size() : 0U;
        m_search_query += text.substr(0, room);
    }
    m_presentation_dirty = true;
}

void CardEditDialog::dialog_presentation_key(
    const ::cw::dialog::DialogPresentationKey key, const bool pressed,
    const ::cw::dialog::DialogPresentationModifiers modifiers)
{
    using Key = ::cw::dialog::DialogPresentationKey;
    using Action = ::cw::dialog::DialogPresentationAction;
    if (!pressed || m_presentation_finished) return;
    const bool shift = (modifiers & ::cw::dialog::DialogPresentationModifierShift) != 0;
    const bool control = (modifiers & ::cw::dialog::DialogPresentationModifierControl) != 0;
    if (key == Key::Escape) {
        dialog_presentation_action(Action::Cancel, 8);
        return;
    }
    if (m_presentation_bookmark_popup) {
        if (key == Key::Up) --m_presentation_bookmark_index;
        else if (key == Key::Down) ++m_presentation_bookmark_index;
        else if (key == Key::Home) m_presentation_bookmark_index = 0;
        else if (key == Key::End) m_presentation_bookmark_index =
            static_cast<::cw::lib::Integer>(m_bookmark_entries.size()) - 1;
        else if (key == Key::PageUp) m_presentation_bookmark_index -= 10;
        else if (key == Key::PageDown) m_presentation_bookmark_index += 10;
        else if (key == Key::Enter || key == Key::Space) {
            dialog_presentation_action(Action::Create, 5000 + m_presentation_bookmark_index);
            return;
        } else if (key == Key::Tab) {
            m_presentation_bookmark_popup = false;
        } else return;
        if (!m_bookmark_entries.empty()) m_presentation_bookmark_index = std::clamp<::cw::lib::Integer>(
            m_presentation_bookmark_index, 0,
            static_cast<::cw::lib::Integer>(m_bookmark_entries.size()) - 1);
        m_presentation_dirty = true;
        if (key != Key::Tab) return;
    }
    if (m_presentation_deal_popup) {
        if (key == Key::Up) --m_presentation_deal_index;
        else if (key == Key::Down) ++m_presentation_deal_index;
        else if (key == Key::Home) m_presentation_deal_index = 0;
        else if (key == Key::End) m_presentation_deal_index =
            static_cast<::cw::lib::Integer>(m_deal_target_labels.size()) - 1;
        else if (key == Key::PageUp) m_presentation_deal_index -= 8;
        else if (key == Key::PageDown) m_presentation_deal_index += 8;
        else if (key == Key::Enter || key == Key::Space) {
            dialog_presentation_action(Action::Create, 4000 + m_presentation_deal_index);
            return;
        } else if (key == Key::Tab) {
            m_presentation_deal_popup = false;
            m_presentation_deal_index = m_presentation_deal_committed_index;
        } else return;
        if (!m_deal_target_labels.empty()) m_presentation_deal_index = std::clamp<::cw::lib::Integer>(
            m_presentation_deal_index, 0,
            static_cast<::cw::lib::Integer>(m_deal_target_labels.size()) - 1);
        m_presentation_dirty = true;
        if (key != Key::Tab) return;
    }
    if (m_presentation_focus == 16) {
        if (control && key == Key::A) {
            m_presentation_search_select_all = true;
            m_presentation_dirty = true;
            return;
        }
        if (key == Key::Backspace || key == Key::Delete) {
            dialog_presentation_text("\b", false);
            return;
        }
    }
    if (key == Key::Tab) {
        const std::array<::cw::lib::Integer, 13> order{0, 1, 16, 100, 101, 14, 2, 3, 4, 5, 6, 7, 8};
        const auto it = std::find(order.begin(), order.end(), m_presentation_focus);
        auto pos = it == order.end() ? std::size_t{0} : static_cast<std::size_t>(std::distance(order.begin(), it));
        pos = (pos + (shift ? order.size() - 1 : 1)) % order.size();
        m_presentation_focus = order[pos];
        m_presentation_search_select_all = m_presentation_focus == 16;
        m_presentation_dirty = true;
        return;
    }
    if (m_presentation_focus == 14 && (key == Key::F4 || key == Key::Space || key == Key::Enter)) {
        dialog_presentation_action(Action::Auxiliary, 14);
        return;
    }
    if (m_presentation_focus == 100 && !m_card_rows.empty()) {
        if (key == Key::Up) --m_presentation_card_cursor;
        else if (key == Key::Down) ++m_presentation_card_cursor;
        else if (key == Key::Home) m_presentation_card_cursor = 0;
        else if (key == Key::End) m_presentation_card_cursor = static_cast<::cw::lib::Integer>(m_card_rows.size()) - 1;
        else if (key == Key::PageUp) m_presentation_card_cursor -= 7;
        else if (key == Key::PageDown) m_presentation_card_cursor += 7;
        else if (key == Key::Enter || key == Key::Space) {
            dialog_presentation_action(Action::Select, m_presentation_card_cursor);
            return;
        } else return;
        m_presentation_card_cursor = std::clamp<::cw::lib::Integer>(m_presentation_card_cursor, 0,
            static_cast<::cw::lib::Integer>(m_card_rows.size()) - 1);
        m_presentation_card_scroll = std::clamp(m_presentation_card_scroll,
            (std::max<::cw::lib::Integer>)(0, m_presentation_card_cursor - 6),
            m_presentation_card_cursor);
        m_presentation_dirty = true;
        return;
    }
    if (m_presentation_focus == 101) {
        const auto rows = build_card_target_rows(*this);
        if (rows.empty()) return;
        if (key == Key::Up) --m_presentation_target_cursor;
        else if (key == Key::Down) ++m_presentation_target_cursor;
        else if (key == Key::Home) m_presentation_target_cursor = 0;
        else if (key == Key::End) m_presentation_target_cursor = static_cast<::cw::lib::Integer>(rows.size()) - 1;
        else if (key == Key::PageUp) m_presentation_target_cursor -= 7;
        else if (key == Key::PageDown) m_presentation_target_cursor += 7;
        else if (key == Key::Enter || key == Key::Space || key == Key::Left || key == Key::Right) {
            const auto& row = rows[static_cast<std::size_t>(std::clamp<::cw::lib::Integer>(
                m_presentation_target_cursor, 0, static_cast<::cw::lib::Integer>(rows.size()) - 1))];
            if (row.group) {
                const auto it = std::find(m_presentation_target_group_order.begin(),
                                          m_presentation_target_group_order.end(), row.path);
                const auto group = static_cast<int>(std::distance(m_presentation_target_group_order.begin(), it));
                if (key == Key::Space) dialog_presentation_action(Action::Toggle, 30000 + group);
                else dialog_presentation_action(Action::Auxiliary, 20000 + group);
            } else if (key == Key::Enter || key == Key::Space) {
                dialog_presentation_action(Action::Toggle, row.record);
            }
            return;
        } else return;
        m_presentation_target_cursor = std::clamp<::cw::lib::Integer>(m_presentation_target_cursor, 0,
            static_cast<::cw::lib::Integer>(rows.size()) - 1);
        m_presentation_target_scroll = std::clamp(m_presentation_target_scroll,
            (std::max<::cw::lib::Integer>)(0, m_presentation_target_cursor - 6),
            m_presentation_target_cursor);
        m_presentation_dirty = true;
        return;
    }
    if (key == Key::Enter || key == Key::Space) dialog_presentation_enter(modifiers);
}

void CardEditDialog::dialog_presentation_enter(
    ::cw::dialog::DialogPresentationModifiers)
{
    using Action = ::cw::dialog::DialogPresentationAction;
    if (m_presentation_focus == 16) return;
    if (m_presentation_focus == 0 || m_presentation_focus == 1 || m_presentation_focus == 14)
        dialog_presentation_action(Action::Auxiliary, m_presentation_focus);
    else if (m_presentation_focus >= 2 && m_presentation_focus <= 7)
        dialog_presentation_action(Action::Auxiliary, m_presentation_focus);
    else if (m_presentation_focus == 8) dialog_presentation_action(Action::Cancel, 8);
}

void CardEditDialog::dialog_presentation_scroll(const float x, const float,
                                                const int lines)
{
    auto& scroll = x < 315.0F ? m_presentation_card_scroll : m_presentation_target_scroll;
    scroll = (std::max<::cw::lib::Integer>)(0, scroll + lines);
    m_presentation_dirty = true;
}

bool CardEditDialog::dialog_presentation_finished() const noexcept
{
    auto* self = const_cast<CardEditDialog*>(this);
    try {
        if (self->m_find_update_posted.load()) self->process_find_update();
    } catch (...) {
        self->m_status_text = "非同期更新の反映に失敗しました。";
        self->m_presentation_dirty = true;
    }
    return m_presentation_finished;
}

bool CardEditDialog::dialog_presentation_needs_refresh() const noexcept
{
    return ::cw::dialog::DialogPresentationProvider::dialog_presentation_needs_refresh() ||
        m_presentation_dirty.load(std::memory_order_acquire);
}

void CardEditDialog::dialog_presentation_refreshed() noexcept
{
    ::cw::dialog::DialogPresentationProvider::dialog_presentation_refreshed();
    m_presentation_dirty.store(false, std::memory_order_release);
}

void CardEditDialog::dialog_presentation_complete()
{
    if (!m_presentation_finished) OnClose({});
}

void CardEditDialog_OpenBookmark::initialize(
    std::shared_ptr<::cw::debug::cardedit::CardEditDialog> arg_outer,
    ::cw::lib::String arg_bookmarkpath)
{
    m_outer = std::move(arg_outer);
    m_bookmarkpath = std::move(arg_bookmarkpath);
}

void CardEditDialog_OpenBookmark::OnOpen(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (const auto outer = m_outer.lock()) outer->select_scenario(m_bookmarkpath);
}

::cw::lib::Value g__CardDataType{};
::cw::lib::Value g__OwnerType{};
::cw::lib::Value g__TopLevelType{};

void CardEditDialog_OnScenario_func(std::shared_ptr<::cw::debug::cardedit::CardEditDialog> arg_self)
{
    if (arg_self) arg_self-> _update_enable();
}

void CardEditDialog_OnScenario_func_func(std::shared_ptr<::cw::debug::cardedit::CardEditDialog> arg_self)
{
    if (arg_self) arg_self-> _update_enable();
}

void CardEditDialog_OnScenario_func_func_2(std::shared_ptr<::cw::debug::cardedit::CardEditDialog> arg_self)
{
    if (arg_self) arg_self-> _update_enable();
}

void CardEditDialog_OnScenario_func_func2(std::shared_ptr<::cw::debug::cardedit::CardEditDialog> arg_self)
{
    if (arg_self) arg_self-> _update_enable();
}

void CardEditDialog_OpenBookmark_create_bookmarkmenu___init__(::cw::lib::Value arg_self, std::shared_ptr<::cw::debug::cardedit::CardEditDialog> arg_outer, ::cw::lib::String arg_bookmarkpath)
{
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&arg_self.storage()); object && *object) {
        if (const auto bookmark = std::dynamic_pointer_cast<::cw::debug::cardedit::CardEditDialog_OpenBookmark>(*object)) {
            bookmark->initialize(std::move(arg_outer), std::move(arg_bookmarkpath));
            return;
        }
    }
    if (arg_outer) arg_outer->select_scenario(std::move(arg_bookmarkpath));
}

void CardEditDialog_OpenBookmark_create_bookmarkmenu_OnOpen(::cw::lib::Value arg_self, std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&arg_self.storage()); object && *object) {
        if (const auto bookmark = std::dynamic_pointer_cast<::cw::debug::cardedit::CardEditDialog_OpenBookmark>(*object)) {
            bookmark->OnOpen(std::move(arg_event));
            return;
        }
    }
}

void CardEditDialog_OnFindBtn_func(std::set<std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String>> arg_cards)
{
    // 生成コールバックにはself引数がないため、ネイティブの呼び出
    // し元は先に現在のダイアログを初期化する。 通常の経路を呼び、選
    // 択・照合フラグ・記録の所有権・書き込み順を一つの実装に保つ。
    if (arg_cards.empty()) return;
    with_active_dialog([&arg_cards](auto& dialog) {
        dialog.m_find_matchers_override = std::move(arg_cards);
        dialog.OnFindBtn({});
    });
}

void CardEditDialog_OnFindBtn_func_set_status(::cw::lib::String arg_text)
{
    with_active_dialog([&arg_text](auto& dialog) {
        if (dialog.m__find) dialog.m_status_text = std::move(arg_text);
    });
}

void CardEditDialog_OnFindBtn_func_set_status_func(::cw::lib::String arg_text)
{
    with_active_dialog([&arg_text](auto& dialog) {
        if (dialog.m__find) dialog.m_status_text = std::move(arg_text);
    });
}

std::shared_ptr<::cw::lib::Window> CardEditDialog_OnFindBtn_func_get_item(::cw::lib::Value arg_table, ::cw::lib::Value arg_key, std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::String arg_name, std::shared_ptr<::cw::lib::Bitmap> arg_image)
{
    (void)arg_table;
    (void)arg_key;
    (void)arg_image;
    auto item = std::make_shared<::cw::lib::Window>();
    item->title = std::move(arg_name);
    if (arg_parent) item->visible = arg_parent->visible;
    return item;
}

void CardEditDialog_OnFindBtn_func_add_target(std::shared_ptr<::cw::lib::Window> arg_item, std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String> arg_matcher, std::optional<::cw::lib::Value> arg_toplevel, ::cw::lib::Value arg_owner, ::cw::lib::Value arg_data, bool arg_insce)
{
    if (!arg_item) return;
    with_active_dialog([&](auto& dialog) {
        add_external_record(dialog, arg_matcher, arg_toplevel, arg_owner, arg_data,
                            !arg_insce, arg_item->title);
    });
}

void CardEditDialog_OnFindBtn_func_func(::cw::lib::Value arg_roots, ::cw::lib::Value arg_items, std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String> arg_matcher, std::shared_ptr<::cw::character::Character> arg_member, std::shared_ptr<::cw::header::CardHeader> arg_header)
{
    (void)arg_roots;
    (void)arg_items;
    if (arg_member && arg_header) {
        with_active_dialog([&](auto& dialog) {
            std::vector<std::shared_ptr<::cw::header::CardHeader>>* headers = nullptr;
            if (std::get<0>(arg_matcher) == "SkillCard")
                headers = &std::get<0>(arg_member->m_cardpocket_typed);
            else if (std::get<0>(arg_matcher) == "ItemCard")
                headers = &std::get<1>(arg_member->m_cardpocket_typed);
            else if (std::get<0>(arg_matcher) == "BeastCard")
                headers = &std::get<2>(arg_member->m_cardpocket_typed);
            add_external_record(dialog, arg_matcher,
                ::cw::lib::Value(arg_member), ::cw::lib::Value(arg_member),
                ::cw::lib::Value(arg_header), ::cw::g_cwpy && ::cw::g_cwpy->is_playingscenario(),
                arg_header->m_name, headers);
        });
    }
}

void CardEditDialog_OnFindBtn_func_func_backpack(::cw::lib::Value arg_roots, ::cw::lib::Value arg_items, std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String> arg_matcher, std::shared_ptr<::cw::header::CardHeader> arg_header)
{
    (void)arg_roots;
    (void)arg_items;
    if (arg_header) with_active_dialog([&](auto& dialog) {
        add_external_record(dialog, arg_matcher, {},
            dialog.m_party_typed ? ::cw::lib::Value(dialog.m_party_typed) : ::cw::lib::Value{},
            ::cw::lib::Value(arg_header), ::cw::g_cwpy && ::cw::g_cwpy->is_playingscenario(),
            arg_header->m_name, dialog.m_party_typed ? &dialog.m_party_typed->m_backpack : nullptr);
    });
}

void CardEditDialog_OnFindBtn_func_func_storehouse(::cw::lib::Value arg_roots, ::cw::lib::Value arg_items, std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String> arg_matcher, std::shared_ptr<::cw::header::CardHeader> arg_header)
{
    (void)arg_roots;
    (void)arg_items;
    if (arg_header) with_active_dialog([&](auto& dialog) {
        add_external_record(dialog, arg_matcher, {}, {},
            ::cw::lib::Value(arg_header), false, arg_header->m_name,
            ::cw::g_cwpy && ::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata
                ? &(*::cw::g_cwpy->m_ydata)->m_storehouse : nullptr);
    });
}

void CardEditDialog_OnFindBtn_func_func_pocket(::cw::lib::Value arg_roots, ::cw::lib::Value arg_items, std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String> arg_matcher, std::shared_ptr<::cw::data::CWPyElementTree> arg_etree, std::shared_ptr<::cw::character::Character> arg_pcard, std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    (void)arg_roots;
    (void)arg_items;
    if (arg_pcard && arg_data) with_active_dialog([&](auto& dialog) {
        add_external_record(dialog, arg_matcher,
            ::cw::lib::Value(arg_etree), ::cw::lib::Value(arg_pcard),
            ::cw::lib::Value(arg_data), false, arg_data->gettext("Property/Name", ""));
    });
}

void CardEditDialog_OnFindBtn_func_func_pcard(::cw::lib::Value arg_roots, ::cw::lib::Value arg_items, std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String> arg_matcher, std::shared_ptr<::cw::data::Party> arg_party, ::cw::lib::Integer arg_index, std::shared_ptr<::cw::character::Player> arg_pcard, std::vector<std::shared_ptr<::cw::header::CardHeader>> arg_cardpocket, std::shared_ptr<::cw::data::CWPyElement> arg_data, bool arg_insce)
{
    (void)arg_roots;
    (void)arg_items;
    (void)arg_party;
    (void)arg_index;
    (void)arg_cardpocket;
    if (arg_pcard && arg_data) with_active_dialog([&](auto& dialog) {
        add_external_record(dialog, arg_matcher,
            ::cw::lib::Value(arg_pcard), ::cw::lib::Value(arg_pcard),
            ::cw::lib::Value(arg_data), arg_insce, arg_data->gettext("Property/Name", ""));
    });
}

void CardEditDialog_OnFindBtn_func_func_party(::cw::lib::Value arg_roots, ::cw::lib::Value arg_items, std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String> arg_matcher, std::shared_ptr<::cw::header::PartyHeader> arg_partyheader, std::shared_ptr<::cw::data::Party> arg_party, std::shared_ptr<::cw::header::CardHeader> arg_header, bool arg_insce)
{
    (void)arg_roots;
    (void)arg_items;
    (void)arg_partyheader;
    if (arg_party && arg_header) with_active_dialog([&](auto& dialog) {
        add_external_record(dialog, arg_matcher,
            ::cw::lib::Value(arg_party), ::cw::lib::Value(arg_party),
            ::cw::lib::Value(arg_header), arg_insce, arg_header->m_name,
            &arg_party->m_backpack);
    });
}

void CardEditDialog_OnFindBtn_func_update_enable()
{
    with_active_dialog([](auto& dialog) {
        dialog.m_status_text = std::to_string(dialog.m_search_records.size()) +
                                         "件のカードが見つかりました。";
        dialog.m__find = false;
        dialog._update_enable();
    });
}

void CardEditDialog__update_cards_append_cards(::cw::lib::Callback arg_getids, ::cw::lib::Callback arg_getdata, std::shared_ptr<::cw::lib::Bitmap> arg_image)
{
    if (!arg_getids || !arg_getdata) return;
    with_active_dialog([&](auto& dialog) {
        const auto ids = arg_getids({}, {});
        const auto sequence = std::get_if<::cw::lib::ObjectPtr>(&ids.storage());
        if (!sequence || !*sequence) return;
        const auto values = std::dynamic_pointer_cast<::cw::data::VariantSequence>(*sequence);
        if (!values) return;
        for (const auto& id : values->m_values) {
            const auto data_value = arg_getdata({id}, {});
            const auto element = object_as<::cw::data::CWPyElement>(data_value);
            if (!element) continue;
            auto tree = std::make_shared<::cw::data::CWPyElementTree>(element);
            auto header = std::make_shared<::cw::header::CardHeader>();
            header->initialize(std::nullopt, {}, tree->getroot(), true,
                               dialog.m_scdata_typed ? dialog.m_scdata_typed->m_scedir : "",
                               false, std::nullopt, std::nullopt, {}, {});
            dialog.m_list.push_back(header);
            dialog.m_datalist.push_back(tree);
            std::optional<::cw::lib::Integer> image_index;
            if (header->m_type == "SkillCard") image_index = dialog.m_timgidx_skill;
            else if (header->m_type == "ItemCard") image_index = dialog.m_timgidx_item;
            else if (header->m_type == "BeastCard") image_index = dialog.m_timgidx_beast;
            if (arg_image && !arg_image->rgba.empty() && !image_index) image_index = 0;
            ::cw::debug::cardedit::CardRow row{header->m_id, header->m_name, header->m_desc, image_index};
            dialog.m_card_rows.push_back(std::move(row));
        }
        dialog._update_bookmarkname();
        dialog._update_enable();
    });
}

void main()
{
}

}
