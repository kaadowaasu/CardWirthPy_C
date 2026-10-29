#pragma once

// cwx/editor/gui/dwt/toolspane.d
// のC++20移植。 五種類の設
// 定記録で共用する汎用ペインのモ
// デルを保ち、共通の部品・イベン
// トを使う。 ネイティブ部品や
// 生成済み互換型は公開しない。

#include "cwxeditor_src/core/cwx/structs.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dmenu.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/splitpane.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/undo.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/xmlbytestransfer.hpp"
#include "cwxeditor_src/lib/clipboard.hpp"
#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/dialogs.hpp"
#include "cwxeditor_src/lib/drag_drop.hpp"
#include "cwxeditor_src/lib/events.hpp"
#include "cwxeditor_src/lib/layout.hpp"
#include "cwxeditor_src/lib/services.hpp"
#include "cwxeditor_src/lib/text.hpp"
#include "cwxeditor_src/lib/window_host.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::toolspane {

namespace lib = ::cwxeditor::lib;
namespace structs = ::cwxeditor::cwx::structs;
namespace types = ::cwxeditor::cwx::types;
namespace dmenu = ::cwxeditor::cwx::editor::gui::dwt::dmenu;
namespace customtext = ::cwxeditor::cwx::editor::gui::dwt::customtext;
namespace splitpane = ::cwxeditor::cwx::editor::gui::dwt::splitpane;
namespace undo = ::cwxeditor::cwx::editor::gui::dwt::undo;
namespace xmlbytestransfer =
    ::cwxeditor::cwx::editor::gui::dwt::xmlbytestransfer;

using MenuID = types::MenuID;
using Commons = dmenu::Commons;
using Parent = lib::Container*;

enum class SelectionDecision { Apply, Discard, Cancel };

/// ClassicEngineの特徴名表の一行。 DはProps/Skin
/// からラベルを得るが、共通実装ではコールバックを通じて渡す。
struct FeatureDefinition {
    enum class Group { Ok, Sex, Period, Nature, Makings, ActionCard };

    Group group{Group::Ok};
    lib::String key;
    std::int32_t action_card{};
    lib::String default_name;
    lib::String variant_name;
};

/// ToolsPaneが使う文字・画像・制限・特徴名・サービス
/// の規則。 ヘッドレス構築・テストにも使える既定値を持つ。
struct Presentation {
    std::function<lib::String(MenuID)> menuText;
    std::function<lib::ResourceId(MenuID)> menuImage;
    std::function<lib::String(lib::String, lib::String, lib::String, bool)>
        formatMenuSample;
    // DのClassicEngine・作業ディレクトリ選択と同じく、相対パスはProps.appPath
    // から解決する。 SettingsDialogから明示的に渡す。
    lib::String applicationPath;

    lib::String boxBgImage{"背景画像設定"};
    lib::String boxOuterTool{"外部ツール"};
    lib::String boxClassicEngine{"クラシックエンジン"};
    lib::String boxScenarioTemplate{"シナリオテンプレート"};
    lib::String boxEventTemplate{"イベントテンプレート"};
    lib::String noName{"(名称なし)"};
    lib::String newText{"新規"};
    lib::String alterText{"変更"};
    lib::String deleteText{"削除"};
    lib::String upText{"上へ"};
    lib::String downText{"下へ"};
    lib::String referenceText{"参照"};
    lib::String mnemonicText{"ニーモニック"};
    lib::String hotkeyText{"ショートカット"};
    lib::String typeText{"タイプ"};
    lib::String nameText{"名前"};
    lib::String templateNameText{"テンプレート名"};
    lib::String commandText{"コマンド"};
    lib::String outerToolDialogTitle{"外部ツールの選択"};
    lib::String workDirText{"作業ディレクトリ"};
    lib::String workDirDialogTitle{"作業ディレクトリの選択"};
    lib::String workDirDescription{"外部ツールの作業ディレクトリを選択してください。"};
    lib::String enginePathText{"エンジン"};
    lib::String dataDirText{"データディレクトリ"};
    lib::String dataDirDescription{"クラシックエンジンのデータフォルダを選択してください。"};
    lib::String enginePathDialogTitle{"クラシックエンジンの選択"};
    lib::String engineExecuteDialogTitle{"代替実行ファイルの選択"};
    lib::String scenarioTemplateDialogTitle{"テンプレートシナリオの選択"};
    lib::String executeText{"実行ファイル"};
    lib::String scenarioPathText{"シナリオ"};
    lib::String eventScriptText{"スクリプト"};
    lib::String leftText{"左"};
    lib::String topText{"上"};
    lib::String widthText{"幅"};
    lib::String heightText{"高さ"};
    lib::String layerText{"レイヤー"};
    lib::String layerHint{"(%d 未満は背面)"};
    lib::String maskTooltip{"マスク"};
    lib::String featureText{"特徴名"};
    lib::String featureDefaultText{"標準名"};
    lib::String featureVariantText{"バリアント名"};
    lib::String featureManualText{"手動名"};
    float featureDefaultNameWidth{180.0F};
    float featureVariantNameWidth{180.0F};
    float featureManualNameWidth{180.0F};
    std::function<void(std::size_t, float)> storeFeatureColumnWidth;
    lib::String outerToolHint1{"コマンドには実行するプログラムを指定します。"};
    lib::String outerToolHint2{"作業ディレクトリは空欄なら現在の場所です。"};
    lib::String outerToolHint3{"$F は選択ファイル、$S はシナリオの置換文字です。"};
    lib::String classicEngineHint{"相対パスはアプリケーションの場所を基準にします。"};
    lib::String eventTemplateHint1{"イベントテンプレートは新しいイベントの初期スクリプトです。"};
    lib::String eventTemplateHint2{"戻り値コードは保存時に改行へ正規化されます。"};
    lib::String questionTitle{"確認"};
    lib::String selectionApplyMessage{"編集中の値を適用しますか？"};

    std::int32_t posLeftMax{10000};
    std::int32_t posTopMax{10000};
    std::int32_t backWidthMax{10000};
    std::int32_t backHeightMax{10000};
    std::int32_t layerMax{10000};
    std::int32_t layerDefault{types::LAYER_BACK_CELL};
    std::size_t tabs{4};
    std::vector<lib::FileFilter> scenarioTemplateFilters;
    std::size_t undoMaxEtc{100};
    std::size_t comboVisibleItemCount{12};
    std::int32_t leftWidth{240};
    std::int32_t leftHeight{300};
    std::int32_t shortcutLeft{1};
    std::int32_t shortcutRight{1};
    bool xmlCopy{};
    lib::String undoShortcut{"Ctrl+Z"};
    lib::String redoShortcut{"Ctrl+Y"};
    std::vector<lib::String> skinTypes;
    std::function<std::vector<lib::String>()> skinTypeChoices;
    std::function<lib::Action(lib::Action)> subscribeSkinTypeChanges;
    std::function<lib::String(MenuID)> menuAccelerator;
    std::function<lib::Action(std::function<void(MenuID)>)> subscribeMenu;
    lib::String classicEngineRegex;
    lib::String classicDataDirRegex;
    lib::String classicMatchKey;
    lib::String executableFilterText{"Executable files"};
    lib::String allFilesFilterText{"All files"};
    std::vector<FeatureDefinition> featureRows;
    std::function<std::vector<FeatureDefinition>(
        const structs::ClassicEngine*)> featureRowsFor;

    /// 任意のネイティブ相当のパス選択。boolが真ならデ
    /// ィレクトリを選び、空文字列の戻り値は中止を表す。
    std::function<lib::String(lib::StringView, bool, lib::StringView)>
        browsePath;
    std::function<void(lib::StringView, bool)> openPath;
    std::function<SelectionDecision(lib::StringView)> confirmSelection;

    [[nodiscard]] lib::String text(MenuID id) const;
    [[nodiscard]] lib::ResourceId image(MenuID id) const;
    [[nodiscard]] lib::String menuSample(lib::String name,
                                          lib::String mnemonic,
                                          lib::String hotkey,
                                          bool escape = false) const;
};

struct Callbacks {
    std::function<void(bool)> ignoreMod;
    std::function<bool()> catchMod;
    std::function<void()> applyEnabled;
};

namespace detail {

template <typename Component>
[[nodiscard]] std::shared_ptr<Component> makeComponent(
    const lib::Services& services, lib::ComponentKind kind) {
    if (services.gui) {
        if (auto widget = services.gui->create_component(kind)) {
            if (auto result = std::dynamic_pointer_cast<Component>(
                    std::move(widget)))
                return result;
        }
    }
    return std::make_shared<Component>();
}

template <typename T>
[[nodiscard]] T copyValue(const T& value) {
    if constexpr (requires { value.dup(); }) return value.dup();
    else return value;
}

inline lib::String normalizeReturnCode(lib::StringView value) {
    // DのwrapReturnCodeと同じく、WindowsのCRLFだけを正規化し、単独のCRは残す。
    return lib::normalize_editor_control_newlines(lib::String(value));
}

inline lib::String layerHint(lib::String pattern, std::int32_t layer) {
    const auto marker = pattern.find("%d");
    if (marker != lib::String::npos)
        pattern.replace(marker, 2U, std::to_string(layer));
    return pattern;
}

}  // namespace detail

template <typename T>
class ToolsPane : public lib::Panel {
    static_assert(
        std::is_same_v<T, structs::BgImageSetting> ||
            std::is_same_v<T, structs::OuterTool> ||
            std::is_same_v<T, structs::ClassicEngine> ||
            std::is_same_v<T, structs::ScTemplate> ||
            std::is_same_v<T, structs::EvTemplate>,
        "ToolsPane supports the five D toolspane record types");

    class UndoU final : public undo::Undo {
    public:
        explicit UndoU(ToolsPane& owner) : owner_(&owner) { save(); }
        void undo() override { impl(); }
        void redo() override { impl(); }
        void dispose() override {}

    private:
        ToolsPane* owner_{};
        std::vector<T> old_{};
        std::size_t selected_{};
        void save() {
            old_.clear();
            old_.reserve(owner_->array_.size());
            for (const auto& value : owner_->array_)
                old_.push_back(detail::copyValue(value));
            selected_ = owner_->selectedIndex_;
        }
        void impl() {
            const auto old = old_;
            const auto selected = selected_;
            save();
            owner_->array_ = old;
            owner_->syncList(selected);
            owner_->selected();
            owner_->notifyChanged();
        }
    };

    struct FeatureUndo final : undo::Undo {
        ToolsPane* owner{};
        std::vector<lib::String> oldManual;
        std::vector<std::size_t> oldSelection;
        explicit FeatureUndo(ToolsPane& value) : owner(&value) { save(); }
        void undo() override { impl(); }
        void redo() override { impl(); }
        void dispose() override {}
        void save() {
            oldManual.clear();
            if (owner->featureTable_)
                for (const auto& row : owner->featureTable_->rows)
                    oldManual.push_back(row.cells.size() > 2U
                                            ? row.cells[2].text
                                            : lib::String{});
            oldSelection = owner->featureTable_
                               ? owner->featureTable_->selection
                               : std::vector<std::size_t>{};
        }
        void impl() {
            const auto manual = oldManual;
            const auto selection = oldSelection;
            save();
            owner->setFeatureManual(manual);
            owner->setFeatureSelection(selection, false);
            // ポップアップの履歴を使う前に行が確定済みでも、
            // 取り消した特徴表の編集を再適用できるようにする。
            owner->canApply_ = true;
            owner->altEnabled_ = owner->hasSelection();
            if (owner->alt_)
                owner->alt_->set_enabled(owner->enabled_ && owner->altEnabled_);
            owner->notifyChanged();
        }
    };

    class UTCPD final : public dmenu::TCPD {
    public:
        explicit UTCPD(ToolsPane& owner) : owner_(&owner) {}
        void cut(dmenu::SelectionEvent& event) override {
            copy(event);
            if (event.handled) owner_->del();
        }
        void copy(dmenu::SelectionEvent& event) override {
            if (!canDoC()) return;
            owner_->copySelected();
            event.handled = true;
        }
        void paste(dmenu::SelectionEvent& event) override {
            if (!canDoP()) return;
            const auto before = owner_->array_.size();
            owner_->pasteSelected();
            event.handled = owner_->array_.size() > before;
        }
        void del(dmenu::SelectionEvent& event) override {
            if (!canDoD()) return;
            const auto before = owner_->array_.size();
            owner_->del();
            event.handled = owner_->array_.size() < before;
        }
        void clone(dmenu::SelectionEvent& event) override {
            if (!canDoClone()) return;
            const auto before = owner_->array_.size();
            owner_->cloneSelected();
            event.handled = owner_->array_.size() > before;
        }
        [[nodiscard]] bool canDoTCPD() const override { return true; }
        [[nodiscard]] bool canDoT() const override {
            return owner_->hasSelection() && static_cast<bool>(owner_->clipboard_);
        }
        [[nodiscard]] bool canDoC() const override { return canDoT(); }
        [[nodiscard]] bool canDoP() const override { return owner_->clipboardHasXml(); }
        [[nodiscard]] bool canDoD() const override { return owner_->hasSelection(); }
        [[nodiscard]] bool canDoClone() const override { return canDoC(); }
    private:
        ToolsPane* owner_{};
    };

    class FeatureTCPD final : public dmenu::TCPD {
    public:
        explicit FeatureTCPD(ToolsPane& owner) : owner_(&owner) {}
        void cut(dmenu::SelectionEvent& event) override {
            copy(event);
            if (event.handled) del(event);
        }
        void copy(dmenu::SelectionEvent& event) override {
            if (!canDoC()) return;
            owner_->copyFeatures();
            event.handled = true;
        }
        void paste(dmenu::SelectionEvent& event) override {
            if (!canDoP()) return;
            owner_->pasteFeatures();
            event.handled = true;
        }
        void del(dmenu::SelectionEvent& event) override {
            if (!canDoD()) return;
            owner_->deleteFeatures();
            event.handled = true;
        }
        void clone(dmenu::SelectionEvent&) override {}
        [[nodiscard]] bool canDoTCPD() const override { return true; }
        [[nodiscard]] bool canDoT() const override {
            return owner_->featureHasSelection() && static_cast<bool>(owner_->clipboard_);
        }
        [[nodiscard]] bool canDoC() const override { return canDoT(); }
        [[nodiscard]] bool canDoP() const override {
            return owner_->featureHasSelection() && owner_->clipboardHasText();
        }
        [[nodiscard]] bool canDoD() const override {
            return owner_->featureHasSelection();
        }
        [[nodiscard]] bool canDoClone() const override { return false; }
    private:
        ToolsPane* owner_{};
    };

    Commons comm_{};
    Presentation presentation_{};
    Callbacks callbacks_{};
    Parent parent_{};
    undo::UndoManager undo_;
    std::unique_ptr<xmlbytestransfer::ClipData> clipboard_;
    std::vector<T> array_{};
    std::size_t selectedIndex_{std::numeric_limits<std::size_t>::max()};
    std::size_t selection_revision_{};
    bool canApply_{};
    bool altEnabled_{};
    bool ignoringMod_{};
    bool built_{};

    std::shared_ptr<splitpane::SplitPane> mainSplit_{};
    std::shared_ptr<splitpane::SplitPane> shortcutSplit_{};
    std::shared_ptr<splitpane::SplitPane> nameTypeSplit_{};
    std::shared_ptr<lib::Panel> leftPanel_{};
    std::shared_ptr<lib::Panel> rightPanel_{};
    std::shared_ptr<lib::ListView> list_{};
    std::shared_ptr<lib::TextEdit> name_{};
    std::shared_ptr<lib::Button> add_{};
    std::shared_ptr<lib::Button> alt_{};
    std::shared_ptr<lib::Button> del_{};
    std::shared_ptr<lib::Button> up_{};
    std::shared_ptr<lib::Button> down_{};
    std::shared_ptr<lib::NumberEdit> bgX_{};
    std::shared_ptr<lib::NumberEdit> bgY_{};
    std::shared_ptr<lib::NumberEdit> bgW_{};
    std::shared_ptr<lib::NumberEdit> bgH_{};
    std::shared_ptr<lib::CheckBox> bgMask_{};
    std::shared_ptr<lib::NumberEdit> bgLayer_{};
    std::shared_ptr<lib::TextEdit> toolCommand_{};
    std::shared_ptr<lib::Button> toolCommandRef_{};
    std::shared_ptr<lib::Button> toolCommandOpen_{};
    std::shared_ptr<lib::TextEdit> toolWorkDir_{};
    std::shared_ptr<lib::Button> toolWorkDirRef_{};
    std::shared_ptr<lib::Button> toolWorkDirOpen_{};
    std::shared_ptr<lib::TextEdit> enginePath_{};
    std::shared_ptr<lib::Button> enginePathRef_{};
    std::shared_ptr<lib::Button> enginePathOpen_{};
    std::shared_ptr<lib::TextEdit> engineDataDir_{};
    std::shared_ptr<lib::Button> engineDataDirRef_{};
    std::shared_ptr<lib::Button> engineDataDirOpen_{};
    std::shared_ptr<lib::TextEdit> engineExecute_{};
    std::shared_ptr<lib::Button> engineExecuteRef_{};
    std::shared_ptr<lib::Button> engineExecuteOpen_{};
    std::shared_ptr<lib::ComboBox> skinType_{};
    std::shared_ptr<lib::TextEdit> templatePath_{};
    std::shared_ptr<lib::Button> templatePathRef_{};
    std::shared_ptr<lib::Button> templatePathOpen_{};
    std::shared_ptr<lib::TextEdit> templateScript_{};
    std::shared_ptr<lib::TextEdit> mnemonic_{};
    std::shared_ptr<customtext::HotKeyField> hotkey_{};
    std::shared_ptr<lib::TextEdit> hotkeyWidget_{};
    std::shared_ptr<lib::ToggleButton> features_{};
    std::shared_ptr<lib::Window> featureWindow_{};
    std::shared_ptr<lib::TableView> featureTable_{};
    std::vector<FeatureDefinition> featureDefinitions_{};
    undo::UndoManager featureUndo_;
    std::shared_ptr<void> pointer_lifetime_{std::make_shared<int>(0)};
    std::shared_ptr<void> feature_lifetime_{std::make_shared<int>(0)};
    std::shared_ptr<dmenu::Menu> contextMenu_{};
    std::shared_ptr<dmenu::Menu> featureMenu_{};
    std::shared_ptr<UTCPD> tcpd_{};
    std::shared_ptr<FeatureTCPD> featureTcpd_{};
    std::vector<dmenu::TextMenuModifyPtr> textMenus_{};
    std::vector<lib::WidgetPtr> dragTargets_{};
    std::vector<lib::Action> openButtonEnabledRefreshes_{};
    bool enabled_{true};
    lib::Action skinTypesDisconnect_{};
    lib::Action menuDisconnect_{};
    lib::Action undoMaxDisconnect_{};
    std::uint32_t undoAccelerator_{};
    std::uint32_t redoAccelerator_{};

    [[nodiscard]] std::shared_ptr<lib::TextEdit> makeText(
        lib::Container& parent, lib::String value = {}, bool multiline = false,
        bool readOnly = false);
    [[nodiscard]] std::shared_ptr<lib::NumberEdit> makeNumber(
        lib::Container& parent, lib::String label, double minimum,
        double maximum, lib::String hint = {});
    [[nodiscard]] std::shared_ptr<lib::Button> makeButton(
        lib::Container& parent, lib::String text, MenuID image,
        lib::Action action, lib::String tooltip = {});
    void attachTextMenu(const std::shared_ptr<lib::TextEdit>& text);
    void attachComboMenu(const std::shared_ptr<lib::ComboBox>& combo);
    void attachDrop(const std::shared_ptr<lib::TextEdit>& text, bool directory,
                    std::function<lib::String(lib::StringView)> converter = {});
    void bindOpenButtonEnabled(const std::shared_ptr<lib::Button>& button,
                               const std::shared_ptr<lib::TextEdit>& text);
    void refreshOpenButtonEnabledStates();
    void markModified();
    void notifyChanged() const;
    void build();
    void buildRight();
    void buildShortcut(lib::Container& parent);
    void buildFeatures();
    void buildContextMenu();
    void setupCallbacks();
    void attachKey(const lib::WidgetPtr& widget);
    void handleKey(lib::KeyEvent& event);
    void setupWeights(std::int32_t left, std::int32_t right);
    void browse(const std::shared_ptr<lib::TextEdit>& text, bool directory);
    void open(const std::shared_ptr<lib::TextEdit>& text, bool directory);
    void dropInto(const std::shared_ptr<lib::TextEdit>& text,
                  const std::vector<std::filesystem::path>& files,
                  bool directory);
    void applyPathSelection(const std::shared_ptr<lib::TextEdit>& text,
                            const std::filesystem::path& path);
    [[nodiscard]] std::filesystem::path classicApplicationDirectory() const;
    [[nodiscard]] std::filesystem::path classicEngineFile() const;
    [[nodiscard]] std::filesystem::path classicEngineDirectory() const;
    [[nodiscard]] lib::String classicSubpath(
        const std::filesystem::path& path) const;
    void refreshSkinTypeChoices();
    [[nodiscard]] bool hasSelection() const noexcept;
    [[nodiscard]] bool clipboardHasXml() const noexcept;
    [[nodiscard]] bool clipboardHasText() const noexcept;
    [[nodiscard]] lib::String clipboardText() const;
    [[nodiscard]] lib::String serialize(const T& value) const;
    [[nodiscard]] bool deserialize(lib::StringView value, T& result) const;
    void copySelected();
    void pasteSelected();
    void cloneSelected();
    void syncList(std::size_t selected);
    void setSelected(std::size_t index);
    void setFieldValues(const T* value);
    [[nodiscard]] bool selectionChanged(std::size_t index);
    void writeFields(T& value) const;
    [[nodiscard]] T readFields() const;
    [[nodiscard]] lib::String createName(const T& value) const;
    void buildFeatureDefinitions(const structs::ClassicEngine* value);
    void syncFeatureTable(const structs::ClassicEngine* value);
    void setFeatureSelection(std::vector<std::size_t> selection,
                             bool notify = false);
    void setFeatureManual(const std::vector<lib::String>& values);
    void storeFeatures();
    [[nodiscard]] bool featureHasSelection() const noexcept;
    void copyFeatures();
    void pasteFeatures();
    void deleteFeatures();
    void writeFeatures(structs::ClassicEngine& value) const;
    void featureCellEdited(std::size_t row, std::size_t column,
                           lib::String value);
    [[nodiscard]] bool confirmSelectionChange(std::size_t index);

public:
    ToolsPane(Commons comm, std::function<void(bool)> ignoreMod = {},
              std::function<bool()> catchMod = {},
              std::function<void()> applyEnabled = {}, Parent parent = nullptr,
              std::int32_t style = 0, Presentation presentation = {});
    ToolsPane(Commons comm, Callbacks callbacks, Parent parent = nullptr,
              Presentation presentation = {}, std::int32_t style = 0)
        : ToolsPane(std::move(comm), std::move(callbacks.ignoreMod),
                    std::move(callbacks.catchMod),
                    std::move(callbacks.applyEnabled), parent, style,
                    std::move(presentation)) {}
    ~ToolsPane() override {
        pointer_lifetime_.reset();
        if (skinTypesDisconnect_) skinTypesDisconnect_();
        if (menuDisconnect_) menuDisconnect_();
        if (undoMaxDisconnect_) undoMaxDisconnect_();
        retireFeatureWindow();
        if (contextMenu_) contextMenu_->retireBindings();
        if (featureMenu_) featureMenu_->retireBindings();
        if (comm_.services.drag_drop)
            for (const auto& target : dragTargets_)
                if (target) comm_.services.drag_drop->unbind(target);
    }
    ToolsPane(const ToolsPane&) = delete;
    ToolsPane& operator=(const ToolsPane&) = delete;

    // SettingsDialog終了後もホストが部品モデルを保持する場合がある。
    // SWTの所有者破棄に合わせ、ネイティブの特徴ポップアップを明示的に終了する。
    void retireFeatureWindow() {
        if constexpr (std::is_same_v<T, structs::ClassicEngine>) {
            if (lib::editor_window_host_options().all_mode || !feature_lifetime_) return;
            feature_lifetime_.reset();
            const auto window = featureWindow_;
            const auto gui = comm_.services.gui;
            if (features_) features_->on_changed = {};
            if (window) {
                window->on_close_requested = {};
                if (gui) gui->close(window);
                else window->set_visible(false);
            }
        }
    }

    void setup(const std::vector<T>& values, std::int32_t& sashLeft,
               std::int32_t& sashRight);
    void setup(const std::vector<T>& values, std::vector<std::int32_t>& weights);
    [[nodiscard]] const std::shared_ptr<lib::ListView>& list() const noexcept { return list_; }
    [[nodiscard]] const std::shared_ptr<lib::TextEdit>& nameField() const noexcept { return name_; }
    [[nodiscard]] const std::shared_ptr<lib::Button>& addButton() const noexcept { return add_; }
    [[nodiscard]] const std::shared_ptr<lib::Button>& alterButton() const noexcept { return alt_; }
    [[nodiscard]] const std::shared_ptr<lib::Button>& deleteButton() const noexcept { return del_; }
    [[nodiscard]] const std::shared_ptr<lib::Button>& upButton() const noexcept { return up_; }
    [[nodiscard]] const std::shared_ptr<lib::Button>& downButton() const noexcept { return down_; }
    [[nodiscard]] const std::shared_ptr<dmenu::Menu>& menu() const noexcept { return contextMenu_; }
    [[nodiscard]] const std::shared_ptr<lib::TableView>& featureTable() const noexcept { return featureTable_; }
    [[nodiscard]] const std::shared_ptr<dmenu::Menu>& featureMenu() const noexcept { return featureMenu_; }
    [[nodiscard]] std::shared_ptr<lib::Window> ceNameWin() const noexcept {
        if constexpr (std::is_same_v<T, structs::ClassicEngine>) return featureWindow_;
        else return {};
    }
    [[nodiscard]] std::shared_ptr<lib::ToggleButton> features() const noexcept {
        if constexpr (std::is_same_v<T, structs::ClassicEngine>) return features_;
        else return {};
    }
    void closeFeatureWindow();
    [[nodiscard]] std::shared_ptr<splitpane::SplitPane> nameTypeSplit() const noexcept {
        if constexpr (std::is_same_v<T, structs::ClassicEngine>)
            return nameTypeSplit_;
        else
            return {};
    }
    [[nodiscard]] const Presentation& presentation() const noexcept { return presentation_; }
    [[nodiscard]] std::vector<T> array() const;
    void setArray(const std::vector<T>& values);
    [[nodiscard]] std::size_t selectedIndex() const noexcept { return selectedIndex_; }
    void select(std::size_t index) { setSelected(index); }
    [[nodiscard]] bool canUp() const noexcept;
    [[nodiscard]] bool canDown() const noexcept;
    [[nodiscard]] bool noApply() const noexcept { return canApply_; }
    void forceApply();
    [[nodiscard]] lib::String boxName() const;
    [[nodiscard]] lib::String editingName() const;
    void setShortcutWeights(std::int32_t& left, std::int32_t& right);
    void setNameTypeWeights(std::int32_t& left, std::int32_t& right);
    void enabled(bool value);
    [[nodiscard]] bool enabled() const noexcept { return enabled_; }
    void up();
    void down();
    void selected();
    void add(T value);
    void create();
    void alt();
    void del();
    void refUndoMax();
    void refMenu(MenuID id);
    [[nodiscard]] bool canDoTCPD() const noexcept { return true; }
    [[nodiscard]] bool canDoT() const noexcept { return hasSelection(); }
    [[nodiscard]] bool canDoC() const noexcept { return hasSelection(); }
    [[nodiscard]] bool canDoP() const noexcept { return clipboardHasXml(); }
    [[nodiscard]] bool canDoD() const noexcept { return hasSelection(); }
    [[nodiscard]] bool canDoClone() const noexcept { return hasSelection(); }
};

}  // namespace cwxeditor::cwx::editor::gui::dwt::toolspane
