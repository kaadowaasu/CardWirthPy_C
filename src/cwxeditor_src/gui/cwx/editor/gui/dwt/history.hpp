#pragma once

#include "cwxeditor_src/gui/cwx/editor/gui/dwt/historydrag.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"

// cwx/editor/gui/dwt/history.d
// のC++20移植。 元のSWT・Commons
// との接続を共通部品とサービスへ
// 置き換え、履歴モデルをネイティ
// ブのツールキットから分離する。

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
#include <type_traits>
#include <utility>
#include <vector>

#include "cwxeditor_src/core/cwx/structs.hpp"
#include "cwxeditor_src/core/cwx/xml.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/editdialogsession.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/undo.hpp"
#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/portable.hpp"
#include "cwxeditor_src/lib/services.hpp"

namespace cwxeditor::cwx::summary {
class Summary;
}

namespace cwxeditor::cwx::variables {
class FlexEtcProps;
}

namespace cwxeditor::cwx::editor::gui::dwt::history {

namespace lib = ::cwxeditor::lib;
namespace structs = ::cwxeditor::cwx::structs;
namespace xml = ::cwxeditor::cwx::xml;
namespace types = ::cwxeditor::cwx::types;
using OpenHistory = structs::OpenHistory;
using XNode = xml::XNode;

/// 共通表でSWTのImageの代わりに使う資源ID。
struct HistoryImageIds {
    lib::ResourceId warning{};
    lib::ResourceId summary_file{};
    lib::ResourceId scenario_archive{};
    lib::ResourceId classic{};
    lib::ResourceId unknown{};
};

/// シナリオ履歴画面の設定。既定値はvariables.d/properties.d
/// に合わせ、 ネイティブGUIとヘッド
/// レステストの両方で使えるようにする。
struct HistorySettings {
    std::size_t history_max{10};
    bool remove_non_existing_with_priority{true};
    std::size_t undo_max{1024};
    lib::Size dialog_size{500.0F, 350.0F};
    std::int32_t name_column_width{150};
    std::int32_t path_column_width{300};
    lib::String scenario_bookmark_hint{
        "ブックマークに登録したいシナリオはチェックしてください。"};
    lib::String history_scenario_file_name{"ファイル名"};
    lib::String history_scenario_path{"場所"};
    lib::String warning_history_too_many{
        "履歴が多すぎます(%1$s件)。%2$s件を超えた分は適用時に破棄されます。"};
    lib::String ok_text{"OK"};
    lib::String cancel_text{"キャンセル(C)"};
    lib::String apply_text{"適用(A)"};
    HistoryImageIds images{};
    bool xmlCopy{};
    std::function<bool()> xmlCopyProvider;
    std::vector<lib::MenuItem> context_menu_items;
};

/// 共通のMenuID一覧と呼び出し元の表示設定から、履歴メニューを作る。
[[nodiscard]] std::vector<lib::MenuItem> historyContextMenuItems(
    std::function<lib::String(::cwxeditor::cwx::types::MenuID)> menu_text = {},
    std::function<lib::ResourceId(::cwxeditor::cwx::types::MenuID)> menu_image = {});

/// 具体的なコア設定から、共通の設定スナップショットを作る。
[[nodiscard]] HistorySettings historySettings(
    const ::cwxeditor::cwx::variables::FlexEtcProps& etc);

struct HistoryEntry {
    OpenHistory hist;
    bool bookmark{};

    HistoryEntry() = default;
    HistoryEntry(OpenHistory value, bool marked)
        : hist(std::move(value)), bookmark(marked) {}
};

using Hist = HistoryEntry;

/// Commons/ref*のデリゲートとDWTのメニュー操作に対応する通知。
struct HistoryCallbacks {
    lib::Action changed;
    lib::Action toolbar_refresh;
    std::function<void(const std::vector<lib::String>&)> warning_changed;
    std::function<void(const std::vector<OpenHistory>&,
                       const std::vector<OpenHistory>&)> persisted;
    std::function<void(const std::filesystem::path&)> open_path;
};

/// ClipboardData.formats内でXMLBytesTransfer相当のデータに使うMIME。
inline constexpr std::string_view OPEN_HISTORIES_MIME =
    "application/x-cwxeditor-open-histories+xml";

/// Dの順序・選択・クリップボード・ドラッグ・件数制限付きUndo
/// を保ち、 開いたシナリオの履歴とブックマークを編集する。
class ScenarioHistoryDialog : public editdialogsession::EditDialogSession {
public:
    ScenarioHistoryDialog(
        lib::Services services, std::shared_ptr<lib::Window> owner,
        lib::String title, lib::ResourceId icon,
        std::vector<OpenHistory> history = {},
        std::vector<OpenHistory> bookmark = {},
        HistorySettings settings = {}, HistoryCallbacks callbacks = {});

    ScenarioHistoryDialog(
        lib::Services services, lib::String title,
        std::vector<OpenHistory> history = {},
        std::vector<OpenHistory> bookmark = {},
        HistorySettings settings = {}, HistoryCallbacks callbacks = {})
        : ScenarioHistoryDialog(std::move(services), {}, std::move(title), {},
                                std::move(history), std::move(bookmark),
                                std::move(settings), std::move(callbacks)) {}

    ScenarioHistoryDialog(
        lib::Services services, std::shared_ptr<lib::Window> owner,
        lib::String title, lib::ResourceId icon,
        std::vector<OpenHistory> history,
        std::vector<OpenHistory> bookmark,
        const ::cwxeditor::cwx::variables::FlexEtcProps& etc,
        HistoryCallbacks callbacks = {});

    ~ScenarioHistoryDialog();

    ScenarioHistoryDialog(const ScenarioHistoryDialog&) = delete;
    ScenarioHistoryDialog& operator=(const ScenarioHistoryDialog&) = delete;

    /// area配下に共通の説明ラベルと表を作る。 2回目以
    /// 降のsetupでは重複作成せず、既存の表を更新する。
    bool setup(lib::Container& area);

    /// GuiBackendが利用可能なら共通ダイアログを作り、内容を構築す
    /// る。 ヘッドレスではsetup(Container&)を直接使える。
    bool setup();
    void show();
    void close();

    [[nodiscard]] const std::shared_ptr<lib::Dialog>& dialog() const noexcept {
        return dialog_;
    }
    [[nodiscard]] const std::shared_ptr<lib::TableView>& list() const noexcept {
        return list_;
    }
    [[nodiscard]] const std::vector<HistoryEntry>& entries() const noexcept {
        return hist_;
    }
    [[nodiscard]] const std::vector<lib::String>& warning() const noexcept {
        return warnings_;
    }
    [[nodiscard]] const HistorySettings& settings() const noexcept {
        return settings_;
    }
    void set_settings(HistorySettings settings);
    void update_live_settings(std::size_t historyMax, bool priority, std::size_t undoMax);

    /// Dのinoutプロパティを、変更可能な具体的vectorで表す。
    [[nodiscard]] std::vector<OpenHistory>& bookmark() noexcept { return bookmark_; }
    [[nodiscard]] const std::vector<OpenHistory>& bookmark() const noexcept {
        return bookmark_;
    }
    [[nodiscard]] std::vector<OpenHistory>& history() noexcept { return history_; }
    [[nodiscard]] const std::vector<OpenHistory>& history() const noexcept {
        return history_;
    }

    [[nodiscard]] const std::vector<std::size_t>& selection() const noexcept {
        return selected_;
    }
    void select(std::vector<std::size_t> indices);
    void set_bookmark(std::size_t index, bool value);

    [[nodiscard]] bool can_open_selection() const;
    void open_selection();
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
    [[nodiscard]] bool drop_files(const std::vector<std::filesystem::path>& files,
                                  std::size_t drop_index);
    void copy_as_text();
    void del_not_exists();

    /// 現在の統合一覧をブックマーク・履歴配列へ確定し、 AbsDialog.apply()
    /// と同じく保存通知を呼ぶ。
    bool apply();
    [[nodiscard]] lib::Window* editDialogWindow() const noexcept override {
        return dialog_.get();
    }
    [[nodiscard]] bool editDialogPending() const noexcept override {
        return apply_button_ && apply_button_->enabled();
    }
    [[nodiscard]] bool applyEditDialog() override;
    [[nodiscard]] const std::shared_ptr<lib::Menu>& contextMenu() const noexcept {
        return context_menu_;
    }

    [[nodiscard]] lib::String to_s_file_name(lib::StringView path) const;
    [[nodiscard]] lib::String to_key_file_name(const OpenHistory& history) const;

    // 移植済みの呼び出し元向けに、D形式の名前を薄い具体的ラッパーとして残す。
    [[nodiscard]] bool canOpenSelection() const { return can_open_selection(); }
    void openSelection() { open_selection(); }
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
    void replace_state(std::vector<HistoryEntry> value,
                       std::vector<std::size_t> selected);
    [[nodiscard]] bool paste_impl(XNode node);
    [[nodiscard]] static bool exists(const OpenHistory& value);
    void sync_bookmarks_from_table();
    void cancel();

    std::shared_ptr<void> lifetime_{std::make_shared<int>(0)};
    lib::Services services_;
    std::shared_ptr<lib::Window> owner_;
    std::shared_ptr<lib::Dialog> dialog_;
    std::shared_ptr<lib::TableView> list_;
    std::shared_ptr<lib::Menu> context_menu_;
    std::shared_ptr<lib::Label> hint_;
    std::shared_ptr<lib::Button> ok_button_;
    std::shared_ptr<lib::Button> cancel_button_;
    std::shared_ptr<lib::Button> apply_button_;
    lib::String title_;
    lib::ResourceId icon_{};
    HistorySettings settings_;
    // 同期的な終了をまたいでも、コールバックの同一性と保持領域を維持する。
    std::shared_ptr<HistoryCallbacks> callbacks_;

    std::vector<HistoryEntry> hist_;
    std::vector<HistoryEntry> original_hist_;
    std::vector<OpenHistory> history_;
    std::vector<OpenHistory> bookmark_;
    std::vector<std::size_t> selected_;
    historydrag::State drag_state_;
    std::vector<lib::String> warnings_;
    std::unique_ptr<::cwxeditor::cwx::editor::gui::dwt::undo::UndoManager> undo_;
};

[[nodiscard]] lib::ResourceId historyImage(const HistoryImageIds& images,
                                           lib::StringView hist,
                                           const lib::FileSystemBackend* filesystem = nullptr);
[[nodiscard]] lib::String fullHistToHist(lib::StringView hist);
[[nodiscard]] lib::Array<lib::String> fullHistToCWXPaths(lib::StringView hist);
[[nodiscard]] lib::String createHistString(const ::cwxeditor::cwx::summary::Summary* summary);
[[nodiscard]] inline lib::String createHistString(
    const ::cwxeditor::cwx::summary::Summary& summary) {
    return createHistString(&summary);
}

[[nodiscard]] OpenHistory findHistory(
    std::span<const OpenHistory> bookmarks,
    std::span<const OpenHistory> histories, lib::StringView hist,
    const lib::FileSystemBackend* filesystem = nullptr);

void addHistory(const HistorySettings& settings, OpenHistory hist,
                std::vector<OpenHistory>& open_histories,
                std::vector<OpenHistory>& bookmarks,
                const lib::FileSystemBackend* filesystem = nullptr);

[[nodiscard]] inline OpenHistory findHistory(
    const std::vector<OpenHistory>& bookmarks,
    const std::vector<OpenHistory>& histories, lib::StringView hist,
    const lib::FileSystemBackend* filesystem = nullptr) {
    return findHistory(std::span<const OpenHistory>(bookmarks),
                       std::span<const OpenHistory>(histories), hist, filesystem);
}

/// 古い項目から削除する。指定されていれば、存在しな
/// いファイルを優先する。 pathメンバーを持つ構
/// 造体へ汎用化し、設定側でもDの型消去層なしに具体
/// 的なOpenHistoryを使えるようにする。
template <typename T>
concept HistoryPathValue = requires(const T& value) {
    { value.path } -> std::convertible_to<lib::StringView>;
};

template <HistoryPathValue T>
[[nodiscard]] std::vector<T> removeHistoryNonExisting(
    bool remove_non_existing_with_priority, std::vector<T> histories,
    std::size_t history_max) {
    if (histories.size() <= history_max) return histories;
    if (remove_non_existing_with_priority) {
        for (std::size_t i = histories.size(); i-- > 0 && histories.size() > history_max;) {
            const auto path = fullHistToHist(histories[i].path);
            std::error_code error;
            if (path.empty() || !std::filesystem::exists(
                                   structs::pathFromUtf8(path), error)) {
                histories.erase(histories.begin() + static_cast<std::ptrdiff_t>(i));
            }
        }
    }
    if (histories.size() > history_max) histories.resize(history_max);
    return histories;
}

template <HistoryPathValue T>
[[nodiscard]] std::vector<T> removeHistoryNonExisting(
    const HistorySettings&, bool remove_non_existing_with_priority,
    std::vector<T> histories, std::size_t history_max) {
    return removeHistoryNonExisting(remove_non_existing_with_priority,
                                    std::move(histories), history_max);
}

[[nodiscard]] inline std::vector<OpenHistory> removeHistoryNonExisting(
    const HistorySettings& settings, std::vector<OpenHistory> histories) {
    return removeHistoryNonExisting(
        settings, settings.remove_non_existing_with_priority,
        std::move(histories), settings.history_max);
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::history
