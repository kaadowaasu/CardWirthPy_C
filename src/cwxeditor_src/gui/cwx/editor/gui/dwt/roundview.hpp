#pragma once

// cwx.editor.gui.dwt.roundview
// の移植可能なC++20実装。 DのSWT仮想表を
// 使うAbstractEditableListView
// をlib::TableViewで表し、 同じ値・
// 編集・XML操作を通常のC++部品として公開す
// る。 ネイティブは表の通知へ直接接続でき、ヘッ
// ドレスもウィンドウ依存なしで同じモデルを使う。

#include "cwxeditor_src/core/cwx/system.hpp"
#include "cwxeditor_src/core/cwx/versioninfo.hpp"
#include "cwxeditor_src/core/cwx/xml.hpp"
#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/events.hpp"
#include "cwxeditor_src/lib/layout.hpp"
#include "cwxeditor_src/lib/portable.hpp"
#include "cwxeditor_src/lib/services.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::roundview {

using String = ::cwxeditor::lib::String;
using StringView = ::cwxeditor::lib::StringView;
using Round = std::uint32_t;
using Rounds = ::cwxeditor::lib::Array<Round>;
using Indices = ::cwxeditor::lib::Array<std::int32_t>;
using ModifyHandler = ::cwxeditor::lib::Callback<void()>;

inline constexpr StringView ROUNDS_XML_NAME = "Number";

/// 共通ビューのスタイルビット。スタイル指定で作る呼び出し
/// 元向けに、SWT.READ_ONLYを明示的に表す。
inline constexpr std::uint32_t STYLE_NONE = 0U;
inline constexpr std::uint32_t STYLE_READ_ONLY = 1U << 0U;

/// ラウンド一覧に必要な資源と翻訳文字列。ResourceId
/// は共通ハンドルで、実際の画像はホストが所有する。
struct RoundViewResources {
    ::cwxeditor::lib::ResourceId roundImage{};
    ::cwxeditor::lib::ResourceId addRoundImage{};
    ::cwxeditor::lib::ResourceId delRoundImage{};
    ::cwxeditor::lib::ResourceId addManyRoundsImage{};

    String addRoundText{"Add round"};
    String delRoundText{"Delete round"};
    String manyRoundsText{"Rounds"};
    String undoText{"Undo"};
    String redoText{"Redo"};
    String cutText{"Cut"};
    String copyText{"Copy"};
    String pasteText{"Paste"};
    String deleteText{"Delete"};
    String selectAllText{"Select all"};
    String roundSeparator{"-"};
    String rangeHint{"%u-%u"};
};

/// 従来Commons/Propsから取得してい
/// たサービスと設定。 RoundViewに必
/// 要な全データを小さな値型へまとめ、具体的な
/// SWTのCommonsへ依存せず、 ネイテ
/// ィブ・ヘッドレスの両方で使えるようにする。
class ManyRoundsDialog;

struct RoundViewConfig {
    Round roundMax{999U};
    bool readOnly{};
    bool canDuplicate{};
    std::size_t undoMax{100U};
    String dataVersion{::cwxeditor::cwx::versioninfo::LATEST_VERSION};
    RoundViewResources resources{};
    ::cwxeditor::lib::Services services{};
    const ::cwxeditor::cwx::system::System* system{};
    ::cwxeditor::lib::Callback<void()> refreshToolBar;
    ::cwxeditor::lib::Callback<bool(ManyRoundsDialog&)> manyRoundsDialog;
};

/// Number・ラウンドの発火条件一覧を編集する共通部品。
class RoundView final : public ::cwxeditor::lib::Panel,
                        public std::enable_shared_from_this<RoundView> {
public:
    using Table = ::cwxeditor::lib::TableView;
    using TablePtr = ::cwxeditor::lib::Owner<Table>;
    using Editor = ::cwxeditor::lib::NumberEdit;
    using EditorPtr = ::cwxeditor::lib::Owner<Editor>;
    using EditorArray = ::cwxeditor::lib::Array<EditorPtr>;
    using ToolBar = ::cwxeditor::lib::ToolBar;
    using ToolBarPtr = ::cwxeditor::lib::Owner<ToolBar>;
    using Menu = ::cwxeditor::lib::Menu;
    using MenuPtr = ::cwxeditor::lib::Owner<Menu>;

    RoundView(::cwxeditor::lib::Container* parent = nullptr,
              std::uint32_t style = STYLE_NONE,
              RoundViewConfig config = {},
              ::cwxeditor::lib::Callback<bool()> catchMod = {});

    RoundView(RoundViewConfig config, ::cwxeditor::lib::Container* parent = nullptr,
              std::uint32_t style = STYLE_NONE,
              ::cwxeditor::lib::Callback<bool()> catchMod = {});

    RoundView(const RoundView&) = delete;
    RoundView& operator=(const RoundView&) = delete;
    RoundView(RoundView&&) = delete;
    RoundView& operator=(RoundView&&) = delete;
    ~RoundView() override;

    [[nodiscard]] ::cwxeditor::lib::Container* parent() const noexcept { return parent_; }
    [[nodiscard]] std::uint32_t style() const noexcept { return style_; }
    [[nodiscard]] const RoundViewConfig& config() const noexcept { return config_; }

    /// Dのデリゲート配列。通知中はスナップショットを使い、 コール
    /// バックから安全に別のリスナーを追加・削除できるようにする。
    ::cwxeditor::lib::Array<ModifyHandler> modEvent;
    void addModifyListener(ModifyHandler handler);
    void clearModifyListeners() noexcept { modEvent.clear(); }

    void setCatchModify(::cwxeditor::lib::Callback<bool()> handler) {
        catch_mod_ = std::move(handler);
    }
    [[nodiscard]] const ::cwxeditor::lib::Callback<bool()>& catchModify() const noexcept {
        return catch_mod_;
    }

    void setReadOnly(bool value);
    [[nodiscard]] bool readOnly() const noexcept { return read_only_; }
    void setEnabled(bool value);
    [[nodiscard]] bool isEnabled() const noexcept { return enabled(); }
    void enabled(bool value) { setEnabled(value); }
    [[nodiscard]] bool enabled() const noexcept {
        return ::cwxeditor::lib::Widget::enabled();
    }

    void setRoundMax(Round value);
    [[nodiscard]] Round roundMax() const noexcept { return config_.roundMax; }

    /// Dのプロパティと同じく、値をコピーして返す。
    [[nodiscard]] Rounds values() const {
        auto result = values_;
        if (new_item_index_ && *new_item_index_ < result.size())
            result.erase(result.begin() + static_cast<std::ptrdiff_t>(*new_item_index_));
        return result;
    }
    [[nodiscard]] Rounds rounds() const { return values(); }
    void setValues(const Rounds& values);
    void values(const Rounds& values) { setValues(values); }
    void setRounds(const Rounds& values) { setValues(values); }
    [[nodiscard]] std::size_t valueCount() const noexcept { return values_.size(); }
    [[nodiscard]] Round value(std::size_t index) const;
    void setValue(std::size_t index, Round value);

    [[nodiscard]] const Rounds& valueStorage() const noexcept { return values_; }
    [[nodiscard]] const ::cwxeditor::lib::Array<std::size_t>& selectionIndices() const noexcept {
        return selection_;
    }
    void setSelection(const ::cwxeditor::lib::Array<std::size_t>& indices, bool reveal = false);
    void clearSelection();
    [[nodiscard]] bool canSelectAll() const noexcept;
    void selectAll();

    [[nodiscard]] bool canAddRound() const noexcept { return can_add_round_; }
    void updateCanAddRound() noexcept;
    [[nodiscard]] bool isValidValue(Round value) const noexcept { return value > 0U; }
    /// RoundViewはAbstractEditableListViewの項目数無制限を
    /// 引き継ぐ。 roundMaxは選択可能な値の上限であり、XML項目数の上限ではない。
    [[nodiscard]] std::size_t maxCount() const noexcept {
        return std::numeric_limits<std::size_t>::max();
    }

    /// 最初の未使用ラウンドを選ぶ。indexはDWTの挿入位置。
    [[nodiscard]] Round createNewValue(std::int32_t& index, bool& cancel) const noexcept;
    [[nodiscard]] bool addRound();
    [[nodiscard]] bool addManyRounds(Round from, Round to);
    [[nodiscard]] bool addManyRounds();
    [[nodiscard]] bool appendValues(const Rounds& values, bool force = false);
    [[nodiscard]] bool deleteSelection();
    [[nodiscard]] bool editValue(std::size_t index, Round value);
    [[nodiscard]] bool beginEdit(std::size_t index);
    void cancelEdit();
    [[nodiscard]] bool isNewItemEditing() const noexcept { return new_item_index_.has_value(); }

    [[nodiscard]] bool canUndo() const noexcept { return !undo_.empty(); }
    [[nodiscard]] bool canRedo() const noexcept { return !redo_.empty(); }
    [[nodiscard]] bool undo();
    [[nodiscard]] bool redo();
    void clearUndo() noexcept;
    void setUndoLimit(std::size_t value);
    [[nodiscard]] std::size_t undoLimit() const noexcept { return config_.undoMax; }
    [[nodiscard]] bool copySelection();
    [[nodiscard]] bool cutSelection();
    [[nodiscard]] bool pasteClipboard();

    [[nodiscard]] StringView xmlName() const noexcept;
    [[nodiscard]] Rounds createFromNode(const ::cwxeditor::cwx::xml::XNode& node) const;
    [[nodiscard]] ::cwxeditor::cwx::xml::XNode toNode(const Indices& indices) const;
    [[nodiscard]] ::cwxeditor::cwx::xml::XNode toNode(
        const ::cwxeditor::lib::Array<std::size_t>& indices) const;

    /// モデル変更後に、共通の表の行を再構成する。
    void refreshTable();
    void clearAll() { refreshTable(); }
    [[nodiscard]] const TablePtr& table() const noexcept { return table_; }
    [[nodiscard]] const ToolBarPtr& toolBar() const noexcept { return toolbar_; }
    [[nodiscard]] const MenuPtr& popupMenu() const noexcept { return popup_menu_; }
    [[nodiscard]] const EditorArray& editors() const noexcept { return editors_; }
    [[nodiscard]] EditorPtr mainEditor() const noexcept;
    [[nodiscard]] ::cwxeditor::lib::ResourceId addIcon() const noexcept {
        return config_.resources.addRoundImage;
    }
    [[nodiscard]] const String& addText() const noexcept { return config_.resources.addRoundText; }
    [[nodiscard]] ::cwxeditor::lib::ResourceId delIcon() const noexcept {
        return config_.resources.delRoundImage;
    }
    [[nodiscard]] const String& delText() const noexcept { return config_.resources.delRoundText; }
    void updateItem(Round round, ::cwxeditor::lib::TableRow& item) const;
    [[nodiscard]] ::cwxeditor::lib::Array<String> warnings() const { return {}; }

private:
    struct Snapshot {
        Rounds values;
        ::cwxeditor::lib::Array<std::size_t> selection;
    };

    ::cwxeditor::lib::Container* parent_{};
    std::uint32_t style_{};
    RoundViewConfig config_{};
    ::cwxeditor::cwx::system::System system_{};
    ::cwxeditor::lib::Callback<bool()> catch_mod_{};
    bool read_only_{};
    bool can_add_round_{};
    Rounds values_{};
    ::cwxeditor::lib::Array<std::size_t> selection_{};
    std::optional<std::size_t> new_item_index_{};
    std::optional<std::size_t> editing_index_{};
    TablePtr table_{};
    ToolBarPtr toolbar_{};
    MenuPtr popup_menu_{};
    EditorArray editors_{};
    std::vector<Snapshot> undo_{};
    std::vector<Snapshot> redo_{};

    void initializeTable();
    void raiseModifyEvent();
    void notifyRefreshToolBar() const;
    void storeSnapshot();
    void restoreSnapshot(const Snapshot& snapshot);
    void sortValues();
    void normalizeSelection() noexcept;
    void refreshPopupMenu();
    [[nodiscard]] bool canPasteClipboard() const;
    [[nodiscard]] bool finishInlineEditForAction();
    [[nodiscard]] int existingValue(std::size_t index, Round value) const noexcept;
    [[nodiscard]] bool appendValuesImpl(const Rounds& values, std::int32_t index,
                                        bool force);
    void updateEditors();
    void rebuildTable(bool resetTop, bool changed = false, bool reveal = false);
    std::uint64_t render_revision_{};
    std::shared_ptr<int> callback_lifetime_{std::make_shared<int>(0)};
};

/// addManyRounds()が使うDWTの範囲指定画面に対応する共通型。
class ManyRoundsDialog final {
public:
    using Dialog = ::cwxeditor::lib::Dialog;
    using DialogPtr = ::cwxeditor::lib::Owner<Dialog>;
    using Container = ::cwxeditor::lib::Container;
    using NumberEdit = ::cwxeditor::lib::NumberEdit;
    using NumberEditPtr = ::cwxeditor::lib::Owner<NumberEdit>;

    explicit ManyRoundsDialog(
        RoundViewConfig config = {},
        ::cwxeditor::lib::Owner<::cwxeditor::lib::Window> owner = {},
        ::cwxeditor::lib::Window* parent = nullptr);

    [[nodiscard]] bool noScenario() const noexcept { return true; }
    [[nodiscard]] ::cwxeditor::lib::ResourceId icon() const noexcept {
        return config_.resources.addManyRoundsImage;
    }
    [[nodiscard]] const Rounds& rounds() const noexcept { return rounds_; }
    [[nodiscard]] const DialogPtr& dialog() const noexcept { return dialog_; }
    [[nodiscard]] const NumberEditPtr& fromEditor() const noexcept { return from_; }
    [[nodiscard]] const NumberEditPtr& toEditor() const noexcept { return to_; }
    [[nodiscard]] Round from() const noexcept { return from_value_; }
    [[nodiscard]] Round to() const noexcept { return to_value_; }
    void setFrom(Round value) noexcept;
    void setTo(Round value) noexcept;
    void setRange(Round from, Round to) noexcept;

    /// 既存の共通コンテナー内に部品を作る。
    void setup(Container& area);
    [[nodiscard]] bool open();
    void accept() noexcept { accepted_ = true; }
    void cancel() noexcept { accepted_ = false; }
    [[nodiscard]] bool accepted() const noexcept { return accepted_; }
    void setOwner(::cwxeditor::lib::Owner<::cwxeditor::lib::Window> owner);
    [[nodiscard]] bool close(bool ok);

private:
    RoundViewConfig config_{};
    ::cwxeditor::lib::Owner<::cwxeditor::lib::Window> dialog_owner_{};
    ::cwxeditor::lib::Window* parent_{};
    DialogPtr dialog_{};
    NumberEditPtr from_{};
    NumberEditPtr to_{};
    ::cwxeditor::lib::Owner<::cwxeditor::lib::Button> ok_button_{};
    ::cwxeditor::lib::Owner<::cwxeditor::lib::Button> cancel_button_{};
    Rounds rounds_{};
    Round from_value_{1U};
    Round to_value_{1U};
    bool accepted_{};
    bool initialized_{};
    void syncEditors();
};

}  // namespace cwxeditor::cwx::editor::gui::dwt::roundview
