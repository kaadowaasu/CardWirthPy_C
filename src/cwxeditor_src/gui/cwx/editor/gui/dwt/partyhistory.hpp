#pragma once

#include "cwxeditor_src/gui/cwx/editor/gui/dwt/historydrag.hpp"

// cwx/editor/gui/dwt/partyhistory.d
// のC++20移植。 SWTの代わりに共通部品・サービスを使い、パ
// ーティ履歴の保存とメニューの判断をヘッドレスでも利用可能にする。

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "cwxeditor_src/core/cwx/structs.hpp"
#include "cwxeditor_src/core/cwx/xml.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/editdialogsession.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/undo.hpp"
#include "cwxeditor_src/lib/clipboard.hpp"
#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/drag_drop.hpp"
#include "cwxeditor_src/lib/layout.hpp"
#include "cwxeditor_src/lib/portable.hpp"
#include "cwxeditor_src/lib/services.hpp"

namespace cwxeditor::cwx::variables {
class FlexEtcProps;
}

namespace cwxeditor::cwx::system {
class System;
}

namespace cwxeditor::cwx::editor::gui::dwt::partyhistory {

namespace lib = ::cwxeditor::lib;
namespace structs = ::cwxeditor::cwx::structs;
namespace xml = ::cwxeditor::cwx::xml;
using ExecutionParty = structs::ExecutionParty;
using XNode = xml::XNode;

struct PartyImageIds {
    lib::ResourceId execution_engine{};
    lib::ResourceId warning{};
    lib::ResourceId team{};
    lib::ResourceId yado{};
    lib::ResourceId debug_yado{};
    lib::ResourceId classic_engine{};
};

/// FlexEtcPropsと、partyhistory.d
/// が使うダイアログ・配置・メッセージに対応する設定。
struct PartyHistorySettings {
    std::size_t history_max{10};
    bool xmlCopy{};
    std::function<bool()> xmlCopyProvider;
    bool remove_non_existing_with_priority{true};
    std::size_t undo_max{1024};
    lib::Size dialog_size{500.0F, 350.0F};
    std::int32_t name_column_width{150};
    std::int32_t yado_column_width{150};
    std::int32_t engine_column_width{150};
    lib::String bookmark_hint{
        "ブックマークに登録したいパーティはチェックしてください。"};
    lib::String party_name{"パーティ"};
    lib::String yado_name_label{"拠点"};
    lib::String engine_name_label{"エンジン"};
    lib::String warning_history_too_many{
        "履歴が多すぎます(%1$s件)。%2$s件を超えた分は適用時に破棄されます。"};
    lib::String ok_text{"OK"};
    lib::String cancel_text{"キャンセル"};
    lib::String apply_text{"適用"};
    lib::String engine_path;
    lib::String app_path;
    lib::Array<structs::ClassicEngine> classic_engines;
    PartyImageIds images{};
    std::function<lib::String(lib::StringView)> yado_name_for_legacy;
    std::function<lib::String(lib::StringView)> read_classic_party_name;
};

[[nodiscard]] PartyHistorySettings partyHistorySettings(
    const ::cwxeditor::cwx::variables::FlexEtcProps& etc,
    const ::cwxeditor::cwx::system::System* system = nullptr);

struct PartyHistoryEntry {
    ExecutionParty hist;
    bool bookmark{};

    PartyHistoryEntry() = default;
    PartyHistoryEntry(ExecutionParty value, bool marked)
        : hist(std::move(value)), bookmark(marked) {}
};

using Hist = PartyHistoryEntry;

struct PartyHistoryCallbacks {
    lib::Action changed;
    lib::Action toolbar_refresh;
    lib::Action classic_skin_changed;
    std::function<void(const std::vector<lib::String>&)> warning_changed;
    std::function<void(const std::vector<ExecutionParty>&,
                       const std::vector<ExecutionParty>&)> persisted;
    std::function<void(lib::StringView scenario_path,
                       const ExecutionParty& party)> execute_engine;
};

inline constexpr std::string_view EXECUTION_PARTIES_MIME =
    "application/x-cwxeditor-execution-parties+xml";

class ExecutedPartyHistoryDialog : public editdialogsession::EditDialogSession {
public:
    ExecutedPartyHistoryDialog(
        lib::Services services, std::shared_ptr<lib::Window> owner,
        lib::String title, lib::ResourceId icon,
        std::vector<ExecutionParty> history = {},
        std::vector<ExecutionParty> bookmark = {},
        PartyHistorySettings settings = {}, PartyHistoryCallbacks callbacks = {});

    ExecutedPartyHistoryDialog(
        lib::Services services, lib::String title,
        std::vector<ExecutionParty> history = {},
        std::vector<ExecutionParty> bookmark = {},
        PartyHistorySettings settings = {}, PartyHistoryCallbacks callbacks = {})
        : ExecutedPartyHistoryDialog(std::move(services), {}, std::move(title), {},
                                     std::move(history), std::move(bookmark),
                                     std::move(settings), std::move(callbacks)) {}

    ExecutedPartyHistoryDialog(
        lib::Services services, std::shared_ptr<lib::Window> owner,
        lib::String title, lib::ResourceId icon,
        std::vector<ExecutionParty> history,
        std::vector<ExecutionParty> bookmark,
        const ::cwxeditor::cwx::variables::FlexEtcProps& etc,
        const ::cwxeditor::cwx::system::System* system = nullptr,
        PartyHistoryCallbacks callbacks = {});

    ~ExecutedPartyHistoryDialog();
    ExecutedPartyHistoryDialog(const ExecutedPartyHistoryDialog&) = delete;
    ExecutedPartyHistoryDialog& operator=(const ExecutedPartyHistoryDialog&) = delete;

    bool setup(lib::Container& area);
    bool setup();
    void show();
    void close();
    void cancel();

    [[nodiscard]] const std::shared_ptr<lib::Dialog>& dialog() const noexcept {
        return dialog_;
    }
    [[nodiscard]] const std::shared_ptr<lib::TableView>& list() const noexcept {
        return list_;
    }
    [[nodiscard]] const std::vector<PartyHistoryEntry>& entries() const noexcept {
        return hist_;
    }
    [[nodiscard]] const std::vector<lib::String>& warning() const noexcept {
        return warnings_;
    }
    [[nodiscard]] const PartyHistorySettings& settings() const noexcept {
        return settings_;
    }
    void set_settings(PartyHistorySettings settings);
    void update_live_settings(std::size_t historyMax, bool priority, std::size_t undoMax,
                              std::size_t committedCount);

    [[nodiscard]] std::vector<ExecutionParty>& history() noexcept { return history_; }
    [[nodiscard]] const std::vector<ExecutionParty>& history() const noexcept {
        return history_;
    }
    [[nodiscard]] std::vector<ExecutionParty>& bookmark() noexcept { return bookmark_; }
    [[nodiscard]] const std::vector<ExecutionParty>& bookmark() const noexcept {
        return bookmark_;
    }
    [[nodiscard]] const std::vector<std::size_t>& selection() const noexcept {
        return selected_;
    }
    void select(std::vector<std::size_t> indices);
    void set_bookmark(std::size_t index, bool value);

    [[nodiscard]] bool can_up() const noexcept;
    void up();
    void up_impl(std::size_t count);
    [[nodiscard]] bool can_down() const noexcept;
    void down();
    void down_impl(std::size_t count);

    [[nodiscard]] bool can_undo() const noexcept;
    [[nodiscard]] bool can_redo() const noexcept;
    bool undo();
    bool redo();
    void ref_undo_max();
    void ref_classic_skin(bool notify_owner = true);
    void ref_history_max();

    [[nodiscard]] XNode create_node(std::span<const std::size_t> indices) const;
    [[nodiscard]] XNode create_node(const std::vector<std::size_t>& indices) const {
        return create_node(std::span<const std::size_t>(indices));
    }
    void cut();
    void copy();
    bool paste();
    void del();
    void clone();
    [[nodiscard]] bool can_do_TCPD() const noexcept { return true; }
    [[nodiscard]] bool can_do_T() const noexcept { return can_do_C(); }
    [[nodiscard]] bool can_do_C() const noexcept;
    [[nodiscard]] bool can_do_P() const;
    [[nodiscard]] bool can_do_D() const noexcept;
    [[nodiscard]] bool can_do_clone() const noexcept { return false; }

    [[nodiscard]] bool paste_xml(std::string_view serialized);
    void del_not_exists();
    [[nodiscard]] bool drop(const lib::DragEvent& event,
                            std::size_t drop_index = static_cast<std::size_t>(-1));
    [[nodiscard]] bool apply();
    [[nodiscard]] lib::Window* editDialogWindow() const noexcept override {
        return dialog_.get();
    }
    [[nodiscard]] bool editDialogPending() const noexcept override {
        return apply_button_ && apply_button_->enabled();
    }
    [[nodiscard]] bool applyEditDialog() override { return apply(); }
    [[nodiscard]] const std::shared_ptr<lib::Menu>& contextMenu() const noexcept {
        return context_menu_;
    }

    // メニューのアダプターで使うD互換の名前。
    [[nodiscard]] bool canUp() const noexcept { return can_up(); }
    void upImpl(std::size_t count) { up_impl(count); }
    [[nodiscard]] bool canDown() const noexcept { return can_down(); }
    void downImpl(std::size_t count) { down_impl(count); }
    [[nodiscard]] XNode createNode(const std::vector<std::size_t>& indices) const {
        return create_node(indices);
    }
    [[nodiscard]] bool canDoTCPD() const noexcept { return can_do_TCPD(); }
    [[nodiscard]] bool canDoT() const noexcept { return can_do_T(); }
    [[nodiscard]] bool canDoC() const noexcept { return can_do_C(); }
    [[nodiscard]] bool canDoP() const { return can_do_P(); }
    [[nodiscard]] bool canDoD() const noexcept { return can_do_D(); }
    [[nodiscard]] bool canDoClone() const noexcept { return can_do_clone(); }

private:
    class HUndo;
    void store();
    void refresh_warning();
    void render();
    void notify_changed(bool toolbar = true);
    void replace_state(std::vector<PartyHistoryEntry> value,
                       std::vector<std::size_t> selected);
    [[nodiscard]] bool paste_impl(XNode node);

    lib::Services services_;
    std::shared_ptr<lib::Window> owner_;
    std::shared_ptr<lib::Dialog> dialog_;
    std::shared_ptr<lib::TableView> list_;
    std::shared_ptr<lib::Label> hint_;
    std::shared_ptr<lib::Menu> context_menu_;
    std::shared_ptr<lib::Button> ok_button_;
    std::shared_ptr<lib::Button> cancel_button_;
    std::shared_ptr<lib::Button> apply_button_;
    lib::String title_;
    lib::ResourceId icon_{};
    PartyHistorySettings settings_;
    PartyHistoryCallbacks callbacks_;
    std::vector<PartyHistoryEntry> hist_;
    std::vector<PartyHistoryEntry> original_hist_;
    std::vector<ExecutionParty> history_;
    std::vector<ExecutionParty> bookmark_;
    std::vector<std::size_t> selected_;
    historydrag::State drag_state_;
    std::vector<lib::String> warnings_;
    std::unique_ptr<::cwxeditor::cwx::editor::gui::dwt::undo::UndoManager> undo_;
};

struct EPKey {
    lib::String engine;
    lib::String yado;
    lib::String party;

    friend bool operator==(const EPKey&, const EPKey&) = default;
    friend bool operator<(const EPKey& left, const EPKey& right) noexcept {
        return std::tie(left.engine, left.yado, left.party) <
               std::tie(right.engine, right.yado, right.party);
    }
};

[[nodiscard]] EPKey epKey(lib::StringView engine, lib::StringView yado,
                          lib::StringView party,
                          const lib::FileSystemBackend* filesystem = nullptr);
[[nodiscard]] bool existsParty(const PartyHistorySettings& settings,
                               const ExecutionParty& party);

[[nodiscard]] std::vector<ExecutionParty> removePartyHistoryNonExisting(
    const PartyHistorySettings& settings, bool remove_non_existing_with_priority,
    std::vector<ExecutionParty> parties, std::size_t history_max);

/// Dの動的な実行Engineメニューに対応する共通実装。
struct ExecEngineMenuOptions {
    const lib::FileSystemBackend* filesystem{};
    std::function<bool()> can_exec_with_party;
    std::function<lib::String()> orig_scenario_path;
    std::function<bool(const EPKey&)> selectable_party;
    std::function<void(lib::StringView, lib::StringView, const ExecutionParty&)>
        exec_engine;
    std::function<bool()> can_exec_classic;
    lib::String app_path;
    // Dと同じく、宿を探すための設定上のEngineと、
    // ランチャーへ渡す実行ファイルを分ける。 スキンによ
    // っては前者がデータ配置先、後者がラッパーを指す。
    lib::String engine_launch_path;
    lib::String engine_scan_path;
    lib::String engine_version_path;
    // 上のパスを分離する前の呼び出し元向けの互換処理
    // 。 対応する明示的なパスが空の場合だけ使う。
    lib::String engine_path;
    lib::String engine_name;
    lib::String engine_display_name;
    lib::String yado_root_name{"Yado"};
    lib::Array<structs::ClassicEngine> classic_engines;
    // cwl::readPartyNameにはCPropsが必要だ
    // が、この共通メニューは所有しない。 製品はその読み取り処理を
    // 渡し、ヘッドレスでは拡張子を除いたファイル名を代替にする。
    std::function<lib::String(lib::StringView)> read_classic_party_name;
    // MainWindowからMenuProps::buildMenuを渡し、 ClassicEngine
    // のアクセスキー・ショートカットをDと同じ形式で表示する。
    std::function<lib::String(const structs::ClassicEngine&)> classic_menu_text;
    std::function<bool(lib::StringView)> is_debug_yado;
    PartyImageIds images{};
    // 任意の非同期呼び出し。成功したIDは画像の管理先が所有し続ける。
    std::function<void(lib::StringView,
                       std::function<void(lib::ResourceId)>,
                       std::function<bool()>)> application_icon;
    // ネイティブのホストも、同じ管理先が所有する結果を実体化した項目へ適用する。
    std::function<void(bool, std::size_t, lib::ResourceId)> application_icon_loaded;
};

void createExecEngineMenu(const ExecEngineMenuOptions& options,
                          const std::shared_ptr<lib::Menu>& menu,
                          const std::shared_ptr<lib::Menu>& with_party = {});

void putIcon(const std::shared_ptr<lib::MenuItem>&, const std::shared_ptr<lib::MenuItem>&,
             const PartyImageIds&, bool has_warning);
void putEngineIcon(const std::shared_ptr<lib::MenuItem>&,
                   const std::shared_ptr<lib::MenuItem>&,
                   const PartyImageIds&, lib::StringView engine_path,
                   bool has_warning = false);

// 共通MenuItemのvector API向けの、値を受け取る版。
void putIcon(lib::MenuItem* first, lib::MenuItem* second, const PartyImageIds& images,
             bool has_warning);
void putEngineIcon(lib::MenuItem* first, lib::MenuItem* second,
                   const PartyImageIds& images, lib::StringView engine_path,
                   bool has_warning = false);

}  // namespace cwxeditor::cwx::editor::gui::dwt::partyhistory
