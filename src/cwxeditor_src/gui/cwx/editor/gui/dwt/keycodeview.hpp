#pragma once

// cwx/editor/gui/dwt/keycodeview.d
// の移植可能なC++20実装。 元の仮想表がCommons/Props
// から得る文字・アイコン・スキン対応・シナリ
// オ状態を、 小さな値とコールバックへ分離
// する。ネイティブは公開されたTableView
// ・ツールバー・メニューへ直接接続でき、
// ヘッドレスでも同じモデルを使える。

#include "cwxeditor_src/core/cwx/motion.hpp"
#include "cwxeditor_src/core/cwx/structs.hpp"
#include "cwxeditor_src/core/cwx/system.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/core/cwx/versioninfo.hpp"
#include "cwxeditor_src/core/cwx/xml.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/autokeycode.hpp"
#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/events.hpp"
#include "cwxeditor_src/lib/layout.hpp"
#include "cwxeditor_src/lib/portable.hpp"
#include "cwxeditor_src/lib/services.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::commons {
class Commons;
}

namespace cwxeditor::cwx::editor::gui::dwt::keycodeview {

using String = ::cwxeditor::lib::String;
using StringView = ::cwxeditor::lib::StringView;
using XNode = ::cwxeditor::cwx::xml::XNode;
using KeyCodeByFeature = ::cwxeditor::cwx::structs::KeyCodeByFeature;
using KeyCodeByMotion = ::cwxeditor::cwx::structs::KeyCodeByMotion;
using Element = ::cwxeditor::cwx::types::Element;
using MType = ::cwxeditor::cwx::types::MType;
using FKCKind = ::cwxeditor::cwx::types::FKCKind;
using KeyCodes = ::cwxeditor::lib::Array<String>;
using Indices = ::cwxeditor::lib::Array<std::int32_t>;
using SizeIndices = ::cwxeditor::lib::Array<std::size_t>;
using WarningList = ::cwxeditor::lib::Array<String>;
using ModifyHandler = ::cwxeditor::lib::Callback<void()>;

inline constexpr std::uint32_t STYLE_NONE = 0U;
inline constexpr std::uint32_t STYLE_READ_ONLY = 1U << 0U;
inline constexpr StringView KEY_CODES_XML_NAME = "KeyCodes";

/// 従来Props/Msgsから読んでいた画像と翻訳文字列。
struct KeyCodeViewResources {
    ::cwxeditor::lib::ResourceId keyCodeImage{};
    ::cwxeditor::lib::ResourceId addKeyCodeImage{};
    ::cwxeditor::lib::ResourceId delKeyCodeImage{};
    ::cwxeditor::lib::ResourceId warningImage{};
    ::cwxeditor::lib::ResourceId addByFeaturesImage{};
    ::cwxeditor::lib::ResourceId upImage{};
    ::cwxeditor::lib::ResourceId downImage{};
    std::array<::cwxeditor::lib::ResourceId, 4> timingImages{};

    std::array<String, 4> timingLabels{{"使用", "成功", "失敗", "不保有"}};
    String keyCodeLabel{"キーコード"};
    String addKeyCodeText{"キーコードを追加"};
    String delKeyCodeText{"キーコードを削除"};
    String keyCodeTimingText{"キーコードの発火条件"};
    String copyTimingConvertedText{"発火条件を変換してコピー"};
    String addByFeaturesText{"特徴からキーコードを自動生成"};
    String upText{"上へ"};
    String downText{"下へ"};
    String selectFeaturesText{"キーコードの自動生成"};
    String selectFeaturesHint{
        "特徴と効果からキーコードを自動生成します。\n全体的な特徴を選択してください。"};
    String addKeyCodesErrorText{"追加できなかったキーコード"};
    String addKeyCodesErrorDesc{
        "クラシックなシナリオではキーコードが%1$s件までに制限されているため、次のキーコードは追加できませんでした。"};
    String searchErrorKeyCodeMatchingAll{
        "「MatchingType=All」はシステムで使用されているキーコードのため、正しく機能しない場合があります。"};
    String warningHasNotKeyCode{
        "キーコードの不保有発火条件はCardWirth 1.50より前のバージョンでは機能しません。"};
    String warningRunAwayCard{
        "キーコード「%1$s」付きのカードは死亡イベントを発生させないため、シナリオを誤動作させる可能性があります。"};
    String warningKeyCodeCount{
        "%1$s件より多くのキーコードはクラシックなシナリオでは設定できません。"};
};

/// Dのデリゲートで使うMotionの値表現。 カード
/// ・効果のツリーを所有せず、Motion::type
/// とMotion::elementを正確に保持する。
struct MotionInfo {
    MType type{};
    Element element{Element::All};

    MotionInfo() = default;
    MotionInfo(MType motionType, Element motionElement)
        : type(motionType), element(motionElement) {}
    explicit MotionInfo(const ::cwxeditor::cwx::motion::Motion& motion)
        : type(motion.type()), element(motion.element()) {}
};

/// KeyCodeViewが使うCommons/Props/Summary
/// 状態への接続。 空のコールバックは、元のEditorでスキン
/// がない・シナリオが未作成の場合と同じ安全な空データを返す。
struct KeyCodeViewConfig {
    bool readOnly{};
    bool canDuplicate{};
    bool withIgnitionType{};
    bool summaryLegacy{};
    bool targetVersion150{true};
    std::size_t keyCodesMaxLegacy{5U};
    std::size_t undoMax{100U};
    String dataVersion{::cwxeditor::cwx::versioninfo::LATEST_VERSION};
    KeyCodeViewResources resources{};
    ::cwxeditor::lib::Services services{};
    /// 補助的な特徴・エラー画面のネイティブ所有者。 共通設
    /// 定に所有者を持ち、Windowsの代替処理に頼らず全
    /// バックエンドでDのAbsDialogの親を明示する。
    std::shared_ptr<::cwxeditor::lib::Window> dialogOwner{};
    /// 有効なCPropsのSystem表を使う。なければ、 単独・ヘッド
    /// レスの呼び出し元向けにKeyCodeViewの局所既定値を使う。
    const ::cwxeditor::cwx::system::System* system{};
    /// 任意のEditor全体のデータバージョン通知元。 DのCommons
    /// 直接購読に合わせつつ、ヘッドレスの呼び出し元は独立させる。
    ::cwxeditor::cwx::editor::gui::dwt::commons::Commons* editorCommons{};
    ModifyHandler refreshToolBar;
    ::cwxeditor::lib::Callback<::cwxeditor::lib::Array<KeyCodeByFeature>()>
        keyCodesByFeatures;
    ::cwxeditor::lib::Callback<::cwxeditor::lib::Array<KeyCodeByMotion>()>
        keyCodesByMotions;
    ::cwxeditor::lib::Callback<::cwxeditor::lib::Array<MotionInfo>()> getMotions;
    ::cwxeditor::lib::Callback<::cwxeditor::lib::Array<String>()> standardKeyCodes;
    ::cwxeditor::lib::Callback<::cwxeditor::lib::Array<String>(StringView)>
        warningProvider;
    ::cwxeditor::lib::Callback<void(StringView)> copyText;
    ::cwxeditor::lib::Callback<bool()> summaryLegacyProvider;
    ::cwxeditor::lib::Callback<String()> dataVersionProvider;
    ::cwxeditor::lib::Callback<std::size_t()> keyCodesMaxLegacyProvider;
    ::cwxeditor::lib::Callback<std::size_t()> undoMaxProvider;
    ::cwxeditor::lib::Callback<bool()> xmlCopyProvider;
    /// 任意の同期的な特徴選択処理。なければ、実際の共通GUIホ
    /// ストが再利用可能なモードレス画面を開く。 GUIがnull
    /// なら、単独の補助処理として固定の全選択の代替を保つ。
    ::cwxeditor::lib::Callback<::cwxeditor::lib::Array<String>()> featureSelection;
    ::cwxeditor::lib::Callback<bool()> catchModify;
};

class SelectCardFeaturesDialog;
class AddKeyCodeErrorDialog;

/// カードのKeyCodes要素を編集する共通部品。
class KeyCodeView final : public ::cwxeditor::lib::Panel,
                          public std::enable_shared_from_this<KeyCodeView> {
public:
    using Table = ::cwxeditor::lib::TableView;
    using TablePtr = ::cwxeditor::lib::Owner<Table>;
    using ComboBox = ::cwxeditor::lib::ComboBox;
    using ComboBoxPtr = ::cwxeditor::lib::Owner<ComboBox>;
    using TextEdit = ::cwxeditor::lib::TextEdit;
    using TextEditPtr = ::cwxeditor::lib::Owner<TextEdit>;
    using ToolBar = ::cwxeditor::lib::ToolBar;
    using ToolBarPtr = ::cwxeditor::lib::Owner<ToolBar>;
    using Menu = ::cwxeditor::lib::Menu;
    using MenuPtr = ::cwxeditor::lib::Owner<Menu>;
    using Editor = ::cwxeditor::lib::Widget;
    using EditorPtr = ::cwxeditor::lib::Owner<Editor>;
    using EditorArray = ::cwxeditor::lib::Array<EditorPtr>;

    KeyCodeView(::cwxeditor::lib::Container* parent = nullptr,
                std::uint32_t style = STYLE_NONE,
                KeyCodeViewConfig config = {},
                ::cwxeditor::lib::Callback<bool()> catchMod = {});
    KeyCodeView(KeyCodeViewConfig config,
                ::cwxeditor::lib::Container* parent = nullptr,
                std::uint32_t style = STYLE_NONE,
                ::cwxeditor::lib::Callback<bool()> catchMod = {});
    KeyCodeView(const KeyCodeView&) = delete;
    KeyCodeView& operator=(const KeyCodeView&) = delete;
    KeyCodeView(KeyCodeView&&) = delete;
    KeyCodeView& operator=(KeyCodeView&&) = delete;
    ~KeyCodeView() override;

    [[nodiscard]] ::cwxeditor::lib::Container* parent() const noexcept { return parent_; }
    [[nodiscard]] std::uint32_t style() const noexcept { return style_; }
    [[nodiscard]] const KeyCodeViewConfig& config() const noexcept { return config_; }

    ::cwxeditor::lib::Array<ModifyHandler> modEvent;
    void addModifyListener(ModifyHandler callback);
    void clearModifyListeners() noexcept { modEvent.clear(); }
    void setCatchModify(::cwxeditor::lib::Callback<bool()> callback) {
        catch_mod_ = std::move(callback);
    }
    [[nodiscard]] const ::cwxeditor::lib::Callback<bool()>& catchModify() const noexcept {
        return catch_mod_;
    }

    void setReadOnly(bool value);
    [[nodiscard]] bool readOnly() const noexcept { return read_only_; }
    void setEnabled(bool value);
    // Dと同じく、子部品とモードレス選択画面は所有するカード画面とともに終了する。
    void hostActive(bool value);
    [[nodiscard]] bool isEnabled() const noexcept { return enabled(); }
    void enabled(bool value) { setEnabled(value); }
    [[nodiscard]] bool enabled() const noexcept {
        return ::cwxeditor::lib::Widget::enabled();
    }
    [[nodiscard]] bool canDuplicate() const noexcept { return config_.canDuplicate; }
    [[nodiscard]] bool withIgnitionType() const noexcept { return config_.withIgnitionType; }

    [[nodiscard]] KeyCodes values() const;
    void setValues(const KeyCodes& values);
    void values(const KeyCodes& values) { setValues(values); }
    [[nodiscard]] KeyCodes keyCodes() const { return values(); }
    void setKeyCodes(const KeyCodes& values) { setValues(values); }
    [[nodiscard]] std::size_t valueCount() const noexcept { return values_.size(); }
    [[nodiscard]] const String& value(std::size_t index) const;
    void setValue(std::size_t index, String value);
    [[nodiscard]] const KeyCodes& valueStorage() const noexcept { return values_; }

    [[nodiscard]] const SizeIndices& selectionIndices() const noexcept { return selection_; }
    void setSelection(const SizeIndices& indices);
    void clearSelection();
    [[nodiscard]] bool canSelectAll() const noexcept;
    void selectAll();
    [[nodiscard]] bool copySelection(bool cut = false);
    [[nodiscard]] bool pasteClipboard();

    [[nodiscard]] bool isValidValue(StringView value) const;
    [[nodiscard]] std::size_t maxCount() const noexcept;
    [[nodiscard]] bool canAdd() const noexcept;
    [[nodiscard]] bool addKeyCode();
    [[nodiscard]] bool isNewItemEditing() const noexcept { return new_item_index_.has_value(); }
    void cancelEdit();
    [[nodiscard]] bool appendValues(const KeyCodes& values, bool force = false);
    [[nodiscard]] bool editValue(std::size_t index, String value);
    [[nodiscard]] bool deleteSelection();
    [[nodiscard]] bool moveUp();
    [[nodiscard]] bool moveDown();
    [[nodiscard]] bool canUp() const noexcept;
    [[nodiscard]] bool canDown() const noexcept;
    [[nodiscard]] bool undo();
    [[nodiscard]] bool redo();
    [[nodiscard]] bool canUndo() const noexcept { return !undo_.empty(); }
    [[nodiscard]] bool canRedo() const noexcept { return !redo_.empty(); }
    void clearUndo() noexcept;
    void setUndoLimit(std::size_t value);
    [[nodiscard]] std::size_t undoLimit() const noexcept { return config_.undoMax; }

    [[nodiscard]] StringView xmlName() const noexcept { return KEY_CODES_XML_NAME; }
    [[nodiscard]] KeyCodes createFromNode(XNode node) const;
    [[nodiscard]] XNode toNode(const Indices& indices) const;
    [[nodiscard]] XNode toNode(const SizeIndices& indices) const;
    [[nodiscard]] XNode toNode() const;
    void refDataVersion();
    void refreshTable();
    void clearAll() { refreshTable(); }

    [[nodiscard]] const TablePtr& table() const noexcept { return table_; }
    [[nodiscard]] const ToolBarPtr& toolBar() const noexcept { return toolbar_; }
    [[nodiscard]] const MenuPtr& popupMenu() const noexcept { return popup_menu_; }
    [[nodiscard]] const EditorArray& editors() const noexcept { return editors_; }
    [[nodiscard]] EditorPtr mainEditor() const noexcept;
    [[nodiscard]] ComboBoxPtr createKeyCodeEditor(std::size_t index);
    // DのTableTextEdit相当。行の上に編集可能なキーコードのドロップダウンを開く。
    bool beginKeyCodeEdit(std::size_t index);
    [[nodiscard]] ComboBoxPtr createEditor(std::size_t index) {
        return createKeyCodeEditor(index);
    }
    [[nodiscard]] TextEditPtr textEditor(std::size_t index);
    void editEnd(std::size_t index, StringView newText);
    [[nodiscard]] std::array<String, 4> ignitionTypeChoices(std::size_t index) const;
    void ignitionTypeCombo(std::size_t index, ::cwxeditor::lib::Array<String>& strings,
                           String& current) const;
    void ignitionTypeEditEnd(std::size_t index, std::optional<std::size_t> choice);
    [[nodiscard]] ComboBoxPtr createIgnitionEditor(std::size_t index);
    [[nodiscard]] ::cwxeditor::lib::ResourceId addIcon() const noexcept {
        return config_.resources.addKeyCodeImage;
    }
    [[nodiscard]] const String& addText() const noexcept {
        return config_.resources.addKeyCodeText;
    }
    [[nodiscard]] ::cwxeditor::lib::ResourceId delIcon() const noexcept {
        return config_.resources.delKeyCodeImage;
    }
    [[nodiscard]] const String& delText() const noexcept {
        return config_.resources.delKeyCodeText;
    }
    void updateItem(const String& value, ::cwxeditor::lib::TableRow& item) const;

    [[nodiscard]] WarningList warnings() const;
    [[nodiscard]] WarningList getWarnings(std::size_t index) const;
    [[nodiscard]] bool canConvKeyCode(FKCKind kind) const;
    bool convKeyCode(FKCKind kind);
    [[nodiscard]] bool canCopyWith() const noexcept;
    [[nodiscard]] String copyKeyCodeWith(FKCKind kind);
    [[nodiscard]] ::cwxeditor::lib::Array<String> keyCodeCandidates(
        StringView filter = {}) const;
    [[nodiscard]] bool catchModifier() const;

    [[nodiscard]] bool canAddKeyCodesByFeatures() const;
    [[nodiscard]] bool addKeyCodesByFeatures();
    [[nodiscard]] bool addKeyCodesByFeatures2(const KeyCodes& byFeatures);
    [[nodiscard]] const KeyCodes& lastAddErrors() const noexcept { return last_add_errors_; }

private:
    struct Snapshot {
        KeyCodes values;
        SizeIndices selection;
    };

    ::cwxeditor::lib::Container* parent_{};
    std::uint32_t style_{};
    KeyCodeViewConfig config_{};
    ::cwxeditor::cwx::system::System system_{};
    const ::cwxeditor::cwx::system::System* active_system_{};
    ::cwxeditor::lib::Callback<bool()> catch_mod_{};
    bool read_only_{};
    KeyCodes values_{};
    SizeIndices selection_{};
    std::optional<std::size_t> new_item_index_{};
    // 仮の編集処理と、後から同じ添字へ入った行を区別する。
    std::uint64_t provisional_revision_{};
    std::uint64_t values_revision_{};
    TablePtr table_{};
    ToolBarPtr toolbar_{};
    MenuPtr popup_menu_{};
    EditorArray editors_{};
    std::vector<Snapshot> undo_{};
    std::vector<Snapshot> redo_{};
    KeyCodes last_add_errors_{};
    ::cwxeditor::lib::Owner<SelectCardFeaturesDialog> select_features_dialog_{};

    void initializeTable();
    void rebuildTable(bool resetTop, bool changed = false, bool reveal = false);
    void initializeToolbar();
    void initializePopupMenu();
    void bindDragDrop();
    void unbindDragDrop() noexcept;
    [[nodiscard]] ::cwxeditor::lib::ClipboardData dragData();
    [[nodiscard]] bool drop(::cwxeditor::lib::DragEvent& event);
    void bindExternalNotifications();
    void unbindExternalNotifications() noexcept;
    void refreshPopupMenu();
    void normalizeSelection() noexcept;
    void raiseModifyEvent();
    void notifyRefreshToolBar() const;
    void storeSnapshot();
    void restoreSnapshot(const Snapshot& snapshot);
    [[nodiscard]] std::optional<std::size_t> findValue(
        StringView value, std::optional<std::size_t> except = {}) const;
    [[nodiscard]] bool appendValuesImpl(const KeyCodes& values, bool force);
    void updateEditors();
    void finishEditors() noexcept;
    void cancelEditors();
    [[nodiscard]] const ::cwxeditor::cwx::system::System& activeSystem() const noexcept {
        return active_system_ ? *active_system_ : system_;
    }
    [[nodiscard]] String warningFormat(String format, StringView value) const;
    [[nodiscard]] String warningFormat(String format, std::size_t value) const;
    // モデル・選択の変更と、入れ子の特徴選択要求も対象にする。
    // この同期操作が終わった後に、保持した述語を呼ばない。
    [[nodiscard]] std::function<bool()> operationCurrent() const;
    std::uint64_t feature_request_revision_{};
    std::uint64_t host_revision_{};
    bool host_active_{true};
    std::uint64_t render_revision_{};
    std::shared_ptr<int> callback_lifetime_{std::make_shared<int>(0)};
    ::cwxeditor::cwx::editor::gui::dwt::commons::Commons* notification_commons_{};
    std::uint64_t ref_data_version_connection_{};
    std::uint64_t ref_undo_max_connection_{};
    String pane_id_{};
    SizeIndices drag_selection_{};
};

/// DWTの特徴選択ダイアログに対応する共通実装。
class SelectCardFeaturesDialog final : public std::enable_shared_from_this<SelectCardFeaturesDialog> {
public:
    using Table = ::cwxeditor::lib::TableView;
    using TablePtr = ::cwxeditor::lib::Owner<Table>;
    using Dialog = ::cwxeditor::lib::Dialog;
    using DialogPtr = ::cwxeditor::lib::Owner<Dialog>;

    explicit SelectCardFeaturesDialog(KeyCodeViewConfig config = {},
                                      ::cwxeditor::lib::Window* parent = nullptr);
    ~SelectCardFeaturesDialog();
    SelectCardFeaturesDialog(const SelectCardFeaturesDialog&) = delete;
    SelectCardFeaturesDialog& operator=(const SelectCardFeaturesDialog&) = delete;
    [[nodiscard]] bool noScenario() const noexcept { return true; }
    [[nodiscard]] ::cwxeditor::lib::ResourceId icon() const noexcept {
        return config_.resources.addByFeaturesImage;
    }
    [[nodiscard]] const DialogPtr& dialog() const noexcept { return dialog_; }
    [[nodiscard]] const TablePtr& table() const noexcept { return table_; }
    [[nodiscard]] const KeyCodes& keyCodes() const noexcept { return key_codes_; }
    void setFeatureEntries(const ::cwxeditor::lib::Array<KeyCodeByFeature>& entries);
    [[nodiscard]] const ::cwxeditor::lib::Array<KeyCodeByFeature>& featureEntries() const noexcept {
        return features_;
    }
    void setup(::cwxeditor::lib::Container& area);
    [[nodiscard]] bool open();
    void active();
    void forceCancel();
    [[nodiscard]] bool isOpen() const noexcept { return open_; }
    ::cwxeditor::lib::Action appliedEvent;
    ::cwxeditor::lib::Action closeEvent;
    void selectAll();
    void setChecked(const SizeIndices& indices);
    [[nodiscard]] const SizeIndices& checked() const noexcept { return checked_; }
    [[nodiscard]] bool close(bool ok, bool& cancel);
    [[nodiscard]] bool close(bool ok);

private:
    KeyCodeViewConfig config_{};
    ::cwxeditor::lib::Window* parent_{};
    DialogPtr dialog_{};
    TablePtr table_{};
    ::cwxeditor::lib::Array<KeyCodeByFeature> features_{};
    SizeIndices selection_{};
    SizeIndices checked_{};
    KeyCodes key_codes_{};
    bool initialized_{};
    std::uint64_t rows_revision_{};
    std::uint64_t published_revision_{};
    ::cwxeditor::lib::Array<KeyCodeByFeature> published_features_;
    std::shared_ptr<int> callback_lifetime_{std::make_shared<int>(0)};
    void rebuildRows();
    void refreshFeatures();
    bool open_{};
    bool closing_{};
    ::cwxeditor::cwx::editor::gui::dwt::commons::Commons* notification_commons_{};
    std::weak_ptr<const int> commons_lifetime_;
    std::uint64_t skin_connection_{}, feature_connection_{};
};

/// Classicシナリオの制限で拒否されたキーコードを表示するダイアログ。
class AddKeyCodeErrorDialog final {
public:
    using Table = ::cwxeditor::lib::TableView;
    using TablePtr = ::cwxeditor::lib::Owner<Table>;
    using Dialog = ::cwxeditor::lib::Dialog;
    using DialogPtr = ::cwxeditor::lib::Owner<Dialog>;

    AddKeyCodeErrorDialog(KeyCodeViewConfig config, KeyCodes keyCodes,
                          ::cwxeditor::lib::Window* parent = nullptr);
    ~AddKeyCodeErrorDialog() { callback_lifetime_.reset(); }
    AddKeyCodeErrorDialog(const AddKeyCodeErrorDialog&) = delete;
    AddKeyCodeErrorDialog& operator=(const AddKeyCodeErrorDialog&) = delete;
    [[nodiscard]] bool noScenario() const noexcept { return true; }
    [[nodiscard]] const DialogPtr& dialog() const noexcept { return dialog_; }
    [[nodiscard]] const TablePtr& table() const noexcept { return table_; }
    [[nodiscard]] const KeyCodes& keyCodes() const noexcept { return key_codes_; }
    void setup(::cwxeditor::lib::Container& area);
    [[nodiscard]] bool open();
    void selectAll();
    void setSelection(const SizeIndices& indices);
    [[nodiscard]] const SizeIndices& selection() const noexcept { return table_ ? table_->selection : selection_; }
    [[nodiscard]] String copySelectedXML();
    void copy();
    [[nodiscard]] bool close(bool ok);

private:
    KeyCodeViewConfig config_{};
    ::cwxeditor::lib::Window* parent_{};
    DialogPtr dialog_{};
    TablePtr table_{};
    KeyCodes key_codes_{};
    SizeIndices selection_{};
    bool initialized_{};
    bool rows_ready_{};
    std::uint64_t rows_revision_{};
    std::shared_ptr<int> callback_lifetime_{std::make_shared<int>(0)};
    void rebuildRows();
};

}  // namespace cwxeditor::cwx::editor::gui::dwt::keycodeview
