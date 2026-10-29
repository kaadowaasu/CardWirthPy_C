#pragma once

// メッセージ画面とプレビューの、共通のC++20インターフェース。

#include "cwxeditor_src/core/cwx/card.hpp"
#include "cwxeditor_src/core/cwx/event.hpp"
#include "cwxeditor_src/core/cwx/expression.hpp"
#include "cwxeditor_src/core/cwx/msgutils.hpp"
#include "cwxeditor_src/core/cwx/skin.hpp"
#include "cwxeditor_src/core/cwx/structs.hpp"
#include "cwxeditor_src/core/cwx/summary.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/core/cwx/usecounter.hpp"
#include "cwxeditor_src/core/cwx/warning.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dprops.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dmenu.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/couponview.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/imageselect.hpp"
#include "cwxeditor_src/lib/audio.hpp"
#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/graphics.hpp"
#include "cwxeditor_src/lib/services.hpp"

#include <cstddef>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::messageutils {

namespace lib = ::cwxeditor::lib;
namespace card = ::cwxeditor::cwx::card;
namespace event = ::cwxeditor::cwx::event;
namespace expression = ::cwxeditor::cwx::expression;
namespace msgutils = ::cwxeditor::cwx::msgutils;
namespace skin = ::cwxeditor::cwx::skin;
namespace structs = ::cwxeditor::cwx::structs;
namespace summary = ::cwxeditor::cwx::summary;
namespace types = ::cwxeditor::cwx::types;
namespace usecounter = ::cwxeditor::cwx::usecounter;
namespace warning = ::cwxeditor::cwx::warning;
namespace dprops = ::cwxeditor::cwx::editor::gui::dwt::dprops;
namespace commons = ::cwxeditor::cwx::editor::gui::dwt::commons;
namespace couponview = ::cwxeditor::cwx::editor::gui::dwt::couponview;
namespace imageselect = ::cwxeditor::cwx::editor::gui::dwt::imageselect;

using String = lib::String;
using StringView = lib::StringView;
using Props = dprops::Props;
using Summary = summary::Summary;
using Skin = skin::Skin;
using CardImage = card::CardImage;
using CardImagePtr = lib::Owner<CardImage>;
using CardImageList = lib::Array<CardImagePtr>;
using SDialog = event::SDialog;
using SDialogPtr = lib::Owner<SDialog>;
using Content = event::Content;
using VarValue = msgutils::VarValue;
using NameMap = msgutils::NameMap;
using ValueMap = lib::Dictionary<String, VarValue>;

struct WindowGeometry {
    std::int32_t x{};
    std::int32_t y{};
    std::int32_t width{};
    std::int32_t height{};
    bool maximized{};
};

struct MessageSettings {
    std::uint32_t selection_columns{1};
    bool center_x{};
    bool center_y{};
    bool boundary_check{};
    bool select_talker{};
    bool single_line{};
};

struct MessageContext {
    lib::Services services{};
    std::shared_ptr<lib::Window> owner{};
    Props* props{};
    const Summary* summary{};
    Summary::Owner summary_owner{};
    commons::Commons* editor_commons{};
    Skin::Ptr forced_skin{};
    const usecounter::UseCounter* use_counter{};
    usecounter::CounterOwner use_counter_owner{};
    lib::Action on_changed;
    std::function<void(const std::vector<String>&)> on_warnings_changed;
    std::function<Skin::Ptr()> current_skin;

    // プレビュー値の表の一時Combo編集で使う、
    // 任意の親ダイアログの履歴。 文字入力は自身の
    // ネイティブ履歴、表は独立した値の履歴を保つ。
    struct ParentHistoryHooks {
        lib::Action undo;
        lib::Action redo;
    };
    std::shared_ptr<ParentHistoryHooks> parent_history_hooks{};

    [[nodiscard]] bool valid() const noexcept { return props != nullptr; }
};

struct ImageDataWithScale {
    struct Resource {
        lib::ImageData data;
        lib::ResourceId id{};
        std::shared_ptr<lib::GraphicsBackend> graphics;
        ~Resource();
    };

    std::shared_ptr<Resource> value;
    std::uint32_t scale{1};

    ImageDataWithScale() = default;
    ImageDataWithScale(lib::ImageData image, std::uint32_t drawing_scale,
                       std::shared_ptr<lib::GraphicsBackend> backend = {});

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] lib::ResourceId resource() const noexcept;
    [[nodiscard]] const lib::ImageData* data() const noexcept;
    [[nodiscard]] lib::Size logicalSize() const noexcept;
};

class MsgPreview;
class MsgPreviewWindow;

class AbstractMessageDialog {
public:
    AbstractMessageDialog(MessageContext context,
                          types::CType type = types::CType::TalkMessage,
                          const Content* parent = nullptr,
                          Content* event = nullptr,
                          WindowGeometry size = {});
    virtual ~AbstractMessageDialog();

    AbstractMessageDialog(const AbstractMessageDialog&) = delete;
    AbstractMessageDialog& operator=(const AbstractMessageDialog&) = delete;

    [[nodiscard]] const MessageContext& context() const noexcept { return context_; }
    [[nodiscard]] MessageContext& context() noexcept { return context_; }
    [[nodiscard]] Props* props() const noexcept { return context_.props; }
    [[nodiscard]] const Summary* summary() const noexcept { return context_.summary; }
    [[nodiscard]] Content* event() const noexcept { return event_; }
    [[nodiscard]] types::CType type() const noexcept { return type_; }
    [[nodiscard]] const WindowGeometry& geometry() const noexcept { return geometry_; }
    [[nodiscard]] const MessageSettings& settings() const noexcept { return settings_; }
    void settings(MessageSettings value);

    [[nodiscard]] Skin::Ptr summSkin() const;
    virtual void refDataVersion();
    virtual void updateSelectTalkerEnabled();
    [[nodiscard]] virtual bool hasCharacterTalker() const;
    [[nodiscard]] virtual bool singleLine() const;
    [[nodiscard]] std::vector<String> warnings() const;
    [[nodiscard]] const std::vector<String>& currentWarnings() const noexcept {
        return warning_cache_;
    }
    virtual void refreshWarning();

    [[nodiscard]] warning::TextWarnings textWarnings(
        StringView text, std::span<const String> flags,
        std::span<const String> steps, std::span<const String> variants,
        std::span<const String> fonts, std::span<const char> colors,
        lib::Dictionary<String, bool>& warning_flags,
        lib::Dictionary<String, bool>& warning_steps,
        lib::Dictionary<String, bool>& warning_variants,
        lib::Dictionary<String, bool>& warning_fonts,
        lib::Dictionary<char, bool>& warning_colors) const;

    void updatePreview();
    void updateShowPreview(bool visible);
    void setPreviewLayoutData(bool visible, bool register_window = true);
    /// 編集欄・プレビュー欄を所有するEventDialogの画面を
    /// 接続する。 メッセージ編集部品を作った後でラッパーが下部操作
    /// とrightGroupを作るため、setup()とは分ける。
    void setPreviewHostWindow(const std::shared_ptr<lib::Window>& window);
    // Editor設定が変わったら、埋め込み・独立プレビューの所有関
    // 係を再構成する。 ホストの通知でダイアログの配置を再反映する。
    void previewModeChanged(lib::Action value) {
        preview_mode_changed_ = std::move(value);
    }
    void refFloatMessagePreview();
    [[nodiscard]] const std::shared_ptr<lib::Panel>& editorWidget() const noexcept {
        return editor_container_;
    }
    [[nodiscard]] const std::shared_ptr<lib::Panel>& previewWidget() const noexcept {
        return preview_container_;
    }
    [[nodiscard]] const std::shared_ptr<lib::ToggleButton>& previewToggle() const noexcept {
        return preview_toggle_;
    }
    [[nodiscard]] bool previewVisible() const noexcept {
        return preview_layout_visible_;
    }
    [[nodiscard]] float previewWidth() const noexcept;
    // 意図的な変更(D版と異なる): メッセージの右側プレビューは、スペース節約の
    // ためメッセージウィンドウ幅+左右1pxの固定幅にする。0 = 固定しない。
    [[nodiscard]] float previewFixedWidth() const noexcept;
    void refImageScale();
    [[nodiscard]] bool canHookKeyDown() const noexcept;
    virtual void refUndoMax();
    virtual void refSkin() {}

    void opened();
    [[nodiscard]] std::shared_ptr<lib::Panel> setup(
        const std::shared_ptr<lib::Container>& area = {});
    [[nodiscard]] std::shared_ptr<lib::Panel> createSelectionColumns(
        const std::shared_ptr<lib::Container>& parent);
    [[nodiscard]] std::shared_ptr<lib::Panel> createWsnSettingsBar(
        const std::shared_ptr<lib::Container>& parent);
    void refreshPreview();

    [[nodiscard]] virtual String text() const;
    [[nodiscard]] virtual CardImageList imgPaths() const;
    void text(String value);
    void imgPaths(CardImageList value);

    [[nodiscard]] bool refPath(StringView old_path, StringView new_path,
                               bool is_directory);
    void refPaths(StringView parent);
    void refFlagAndStep();

protected:
    MessageContext context_;
    const Content* parent_{};
    Content* event_{};
    types::CType type_{types::CType::TalkMessage};
    WindowGeometry geometry_{};
    MessageSettings settings_{};
    String text_;
    CardImageList image_paths_;
    std::shared_ptr<MsgPreview> preview_;
    std::shared_ptr<MsgPreviewWindow> preview_window_;
    std::shared_ptr<lib::ToggleButton> preview_toggle_;
    std::shared_ptr<lib::NumberEdit> selection_columns_edit_;
    std::shared_ptr<lib::CheckBox> center_x_check_;
    std::shared_ptr<lib::CheckBox> center_y_check_;
    std::shared_ptr<lib::CheckBox> boundary_check_;
    std::shared_ptr<lib::CheckBox> select_talker_check_;
    std::shared_ptr<lib::Panel> wsn_settings_panel_;
    std::shared_ptr<lib::ImageView> preview_image_view_;
    std::shared_ptr<lib::TableView> preview_values_view_;
    std::shared_ptr<lib::Panel> editor_container_;
    std::shared_ptr<lib::Panel> preview_container_;
    std::weak_ptr<lib::Window> preview_host_window_;
    std::shared_ptr<lib::Panel> root_panel_;
    float preview_layout_width_{};
    float preview_scaled_message_width_{};
    std::shared_ptr<int> preview_scale_lifetime_{std::make_shared<int>(0)};
    bool preview_layout_visible_{true};
    std::vector<String> warning_cache_;
    std::uint64_t message_color_connection_{};
    std::uint64_t float_preview_connection_{};
    lib::Action preview_mode_changed_;

    void initializeFromEvent();
    void createEmbeddedPreview();
    void createFloatingPreview();
    void notifyChanged();
    void publishWarnings();
};

struct DialogSnapshot {
    std::size_t selected{};
    CardImageList images;
    std::vector<String> texts;
    std::vector<lib::Array<String>> required_coupons;
};

class SpeakDialog final : public AbstractMessageDialog {
public:
    using AbstractMessageDialog::text;
    SpeakDialog(MessageContext context, const Content* parent = nullptr,
                Content* event = nullptr, WindowGeometry size = {});
    ~SpeakDialog() override;

    [[nodiscard]] DialogSnapshot readAPD() const;
    void writeAPD(const DialogSnapshot& snapshot);
    void storeEdit();
    void refreshWarning() override;

    [[nodiscard]] bool canHookKeyDown() const noexcept;
    [[nodiscard]] bool selectChanged(std::size_t index);
    [[nodiscard]] SDialogPtr createDialog(const SDialog& dialog);
    [[nodiscard]] SDialogPtr createDialog();
    bool insertDialog(SDialogPtr dialog, std::size_t index, bool store = true);
    bool deleteDialog(std::size_t index, bool store = true);
    [[nodiscard]] bool canDeleteDialog() const noexcept;
    bool deleteDialogSel();
    [[nodiscard]] bool canOverDialog() const noexcept;
    bool overDialog();
    [[nodiscard]] bool canUnderDialog() const noexcept;
    bool underDialog();
    [[nodiscard]] bool canUp() const noexcept;
    bool up();
    [[nodiscard]] bool canDown() const noexcept;
    bool down();
    [[nodiscard]] bool canCopyToUpper() const noexcept;
    bool copyToUpper();
    [[nodiscard]] bool canCopyToLower() const noexcept;
    bool copyToLower();
    [[nodiscard]] bool canCopyToDialogs() const noexcept;
    bool copyToDialogs();
    [[nodiscard]] bool canCopyDialog() const noexcept;
    [[nodiscard]] bool canPasteDialog() const;
    bool cutDialog();
    bool copyDialog();
    bool pasteDialog();
    bool cloneSelectedDialog();
    void put(char32_t value);
    void insert(StringView value);
    void putText(SDialog& dialog);
    void putRCoupons(SDialog& dialog);
    void updateTalker();
    void refreshDlgList();
    void refreshDlgList(std::size_t index);
    [[nodiscard]] SDialogPtr selection() const;
    void refSkin();
    void refUndoMax() override;
    [[nodiscard]] bool refMenu(StringView command);
    [[nodiscard]] bool refMenuImpl(StringView command,
                                    const lib::Action& action,
                                    const std::function<bool()>& enabled = {});

    [[nodiscard]] bool openCWXPath(StringView path, bool shell_activate);
    [[nodiscard]] String text() const override;
    [[nodiscard]] types::Talker selectedTalker() const noexcept;
    [[nodiscard]] CardImageList imgPaths() const override;
    [[nodiscard]] std::shared_ptr<lib::Panel> setup(
        const std::shared_ptr<lib::Container>& area = {},
        bool combine_preview = true);
    [[nodiscard]] bool apply();

    [[nodiscard]] const std::vector<SDialogPtr>& dialogs() const noexcept {
        return dialogs_;
    }
    [[nodiscard]] std::size_t selectedIndex() const noexcept { return selected_; }
    void selectedIndex(std::size_t value);
    void selectedTalker(types::Talker value) noexcept { selected_talker_ = value; }
    [[nodiscard]] const lib::Array<String>& coupons() const noexcept { return coupons_; }
    void coupons(lib::Array<String> value) { coupons_ = std::move(value); }
    [[nodiscard]] int initValue() const noexcept { return init_value_; }
    void initValue(int value) noexcept { init_value_ = value; }

private:
    std::vector<SDialogPtr> dialogs_;
    std::size_t selected_{};
    types::Talker selected_talker_{types::Talker::Selected};
    lib::Array<String> coupons_;
    couponview::CouponArray coupon_records_;
    int init_value_{1};
    std::shared_ptr<lib::TableView> dialog_table_;
    std::shared_ptr<lib::Menu> dialog_menu_;
    std::shared_ptr<lib::ToolBar> dialog_toolbar_;
    std::shared_ptr<lib::TextEdit> text_editor_;
    std::shared_ptr<lib::ComboBox> tone_coupon_combo_;
    std::vector<dmenu::TextMenuModifyPtr> text_menus_;
    std::uint64_t text_sync_revision_{};
    std::shared_ptr<lib::TextEdit> coupon_editor_;
    std::shared_ptr<lib::GroupBox> valued_panel_;
    std::shared_ptr<lib::TextEdit> valued_coupon_editor_;
    std::shared_ptr<couponview::CouponViewBase> valued_coupon_view_;
    std::shared_ptr<lib::NumberEdit> init_value_editor_;
    std::shared_ptr<lib::ComboBox> talker_combo_;
    std::vector<DialogSnapshot> undo_history_;
    std::vector<DialogSnapshot> redo_history_;
    std::vector<std::vector<String>> dialog_warnings_;
    std::optional<std::size_t> hovered_dialog_;
    void refreshHoveredDialogWarning();
    bool restoring_history_{};
    bool repaired_empty_dialogs_{};
    std::shared_ptr<void> key_filter_lifetime_{std::make_shared<int>(0)};
    String pane_id_;
    SDialogPtr dragged_dialog_;

    void syncSelectedText();
    void setupTextMenu(const std::shared_ptr<lib::TextEdit>& editor, bool special);
    bool undoEdit();
    bool redoEdit();
    void setupDialogTransfers();
    void refreshDialogMenu();
    void refreshDialogActions();
    static SDialogPtr cloneDialog(const SDialog& dialog);
};

class MessageDialog final : public AbstractMessageDialog {
public:
    MessageDialog(MessageContext context, const Content* parent = nullptr,
                   Content* event = nullptr, WindowGeometry size = {});
    ~MessageDialog() override;

    void refDataVersion() override;
    void disposeSingleLine();
    void createSingleLineTab();
    void refreshWarning() override;
    [[nodiscard]] bool hasCharacterTalker() const override;
    [[nodiscard]] bool singleLine() const override;
    void tabChanged(std::size_t tab);
    void put(char32_t value);
    void insert(StringView value);
    void refSkin();

    [[nodiscard]] CardImageList selectedTalkerParam() const;
    [[nodiscard]] CardImageList imgPaths() const override;
    [[nodiscard]] String text() const override;
    void text(String value);
    [[nodiscard]] bool openCWXPath(StringView path, bool shell_activate);
    [[nodiscard]] std::shared_ptr<lib::Panel> setup(
        const std::shared_ptr<lib::Container>& area = {},
        bool combine_preview = true);
    [[nodiscard]] bool apply();

    [[nodiscard]] std::size_t selectedTab() const noexcept { return selected_tab_; }
    [[nodiscard]] const CardImageList& selectedImages() const noexcept {
        return selected_images_;
    }
    void selectedImages(CardImageList value);
    [[nodiscard]] imageselect::ImageSelectBase* imageSelect() const noexcept {
        return image_select_.get();
    }
    [[nodiscard]] const std::shared_ptr<lib::TextEdit>& activeTextEditor()
        const noexcept {
        return text_editor_;
    }
    void refUndoMax() override;

private:
    std::size_t selected_tab_{1};
    CardImageList selected_images_;
    std::shared_ptr<lib::TabView> tabs_;
    std::shared_ptr<lib::Panel> single_line_page_;
    std::shared_ptr<lib::TextEdit> text_editor_;
    std::array<std::shared_ptr<lib::TextEdit>, 3> tab_text_editors_{};
    std::shared_ptr<undo::UndoManager> text_history_;
    std::array<dmenu::TextMenuModifyPtr, 3> text_menus_{};
    std::shared_ptr<lib::ComboBox> image_source_combo_;
    std::shared_ptr<lib::TextEdit> image_path_editor_;
    std::shared_ptr<lib::ImageView> selected_image_view_;
    ImageDataWithScale selected_image_data_;
    std::unique_ptr<imageselect::ImageSelectBase> image_select_;
    std::uint64_t image_scale_connection_{};
    bool syncing_text_{};

    void syncTextEditors(StringView value, std::size_t source);
    void refreshSelectedImageControls();
};

[[nodiscard]] std::shared_ptr<lib::Panel> createTalkerPane2(
    const std::shared_ptr<lib::Container>& parent, const MessageContext& context,
    const Summary* summ, const usecounter::UseCounter* uc,
    std::span<const String> coupons,
    std::shared_ptr<lib::TextEdit>& coupon_list,
    std::shared_ptr<lib::ComboBox>& coupon_combo);

[[nodiscard]] std::shared_ptr<lib::Panel> createTalkerPane(
    const std::shared_ptr<lib::Container>& parent, const MessageContext& context,
    const Summary* summ, const CardImageList& paths,
    std::shared_ptr<lib::ImageView>& image_select);

[[nodiscard]] std::shared_ptr<lib::TextEdit> createMessagePane(
    const MessageContext& context, bool image, const std::shared_ptr<lib::Container>& parent,
    const Summary* summ, const Skin::Ptr& skin);

class PutC {
public:
    PutC(std::function<void(StringView)> insert, String value);
    PutC(std::function<void(StringView)> insert, std::function<String()> value);
    void put() const;

private:
    std::function<void(StringView)> insert_;
    std::function<String()> value_;
};

class PutColor {
public:
    PutColor(std::function<void(char32_t)> put, char32_t color);
    void put() const;

private:
    std::function<void(char32_t)> put_;
    char32_t color_{};
};

[[nodiscard]] std::shared_ptr<lib::ToolBar> createSCharBar(
    const MessageContext& context, const Summary* summ,
    const std::shared_ptr<lib::Container>& parent,
    std::function<void(StringView)> insert,
    std::function<void(char32_t)> put_color, const Props* prop,
    // ALLのセリフ画面では、プレイヤー指定スピナーの手前を空けず、1桁の入力欄にする。
    bool compact_player = false,
    // icon_scale > 1でアイコンボタンを拡大する。ALLのメッセージ画面では1.3倍。
    float icon_scale = 1.0F);

[[nodiscard]] std::shared_ptr<lib::ToolBar> createSimpleSCharBar(
    const std::shared_ptr<lib::Container>& parent,
    std::function<void(StringView)> insert, const MessageContext& context,
    const Props* prop, const Summary* summ);

[[nodiscard]] std::shared_ptr<lib::NumberEdit> createPCSPCharBar(
    const MessageContext& context, const Summary* summ,
    const std::shared_ptr<lib::ToolBar>& bar,
    std::function<void(StringView)> insert,
    const std::shared_ptr<lib::Container>& control_parent = {});

// rows > 1なら指定行数へボタンを折り返し、icon_scaleで大きさを変える。
[[nodiscard]] std::shared_ptr<lib::ToolBar> createSkinSCharBar(
    const MessageContext& context, const std::shared_ptr<lib::Container>& parent,
    std::function<void(StringView)> insert, const std::function<Skin::Ptr()>& skin,
    std::size_t rows = 1U, float icon_scale = 1.0F);

void updateSkinSCharBar(const MessageContext& context,
                        const std::shared_ptr<lib::ToolBar>& bar,
                        std::function<void(StringView)> insert,
                        const std::function<Skin::Ptr()>& skin);

[[nodiscard]] std::shared_ptr<lib::Panel> createFlagStepBar(
    const std::shared_ptr<lib::Container>& parent,
    std::function<void(StringView)> insert, const MessageContext& context,
    const Props* prop, const Summary* summ, const usecounter::UseCounter* uc,
    const Skin::Ptr& force_skin, bool image_font,
    const std::function<std::shared_ptr<lib::Button>(
        const std::shared_ptr<lib::Container>&, types::VariableType)>& create_button = {},
    // 各参照ドロップダウンの閉じた状態の幅。0なら標準の160px。
    float combo_width = 0.0F);

void putColor(lib::TextEdit& control, char32_t color);
void putColor(String& value, char32_t color);

class MsgPreviewWindow {
public:
    MsgPreviewWindow(MessageContext context, WindowGeometry size = {});
    ~MsgPreviewWindow();
    [[nodiscard]] bool isVisible() const noexcept { return visible_; }
    void refImageScale();
    void invalidateImageCache();
    void saveWin();
    void open();
    void close();
    void dispose();
    void setOwner(const std::shared_ptr<lib::Window>& owner);
    void text(const CardImageList& paths, bool single_line, String message,
              bool center_x, bool center_y, bool boundary_check);
    void refresh();
    [[nodiscard]] std::shared_ptr<MsgPreview> preview() const noexcept {
        return preview_;
    }
    [[nodiscard]] const WindowGeometry& geometry() const noexcept { return geometry_; }
    [[nodiscard]] const std::shared_ptr<lib::Dialog>& window() const noexcept {
        return window_;
    }
    std::function<void(bool)> visibilityChanged;
    std::function<void(const WindowGeometry&)> geometryChanged;

private:
    MessageContext context_;
    WindowGeometry geometry_{};
    bool visible_{};
    std::shared_ptr<lib::Dialog> window_;
    std::shared_ptr<MsgPreview> preview_;
    std::shared_ptr<lib::ImageView> preview_image_view_;
    std::shared_ptr<lib::TableView> preview_values_view_;
    std::function<void()> disconnect_owner_move_;
    lib::Size scaled_message_size_{};
    std::shared_ptr<int> close_lifetime_{std::make_shared<int>(0)};
};

enum class SPChar {
    M,
    U,
    R,
    C,
    I,
    T,
    Y,
};

inline constexpr std::array<SPChar, 7> SPCHAR_ALL{
    SPChar::M, SPChar::U, SPChar::R, SPChar::C,
    SPChar::I, SPChar::T, SPChar::Y};
inline constexpr std::array<SPChar, 5> SPCHAR_TEXT_CLASSIC{
    SPChar::M, SPChar::U, SPChar::R, SPChar::T, SPChar::Y};
inline constexpr std::array<SPChar, 6> SPCHAR_TEXT{
    SPChar::M, SPChar::U, SPChar::R, SPChar::C, SPChar::T, SPChar::Y};
inline constexpr std::array<char, 7> C_TBL{'M', 'U', 'R', 'C', 'I', 'T', 'Y'};

struct PreviewSnapshot {
    NameMap names;
    lib::Dictionary<String, bool> flags;
    lib::Dictionary<String, std::int32_t> steps;
    lib::Dictionary<String, expression::VariantVal> variants;
    lib::Dictionary<String, bool> flag_expansion;
    lib::Dictionary<String, bool> step_expansion;
    std::uint32_t selected_player_card_number{};
    lib::Array<String> player_card_names;
};

class PreviewValues {
public:
    using ChangeHandler = lib::Action;

    PreviewValues(MessageContext context, bool message,
                  std::vector<SPChar> target_chars = {});
    ~PreviewValues();
    PreviewValues(const PreviewValues&) = delete;
    PreviewValues& operator=(const PreviewValues&) = delete;

    std::vector<ChangeHandler> modEvent;
    [[nodiscard]] bool message() const noexcept { return message_; }
    [[nodiscard]] const std::vector<SPChar>& targetChars() const noexcept {
        return target_chars_;
    }
    void targetChars(std::vector<SPChar> value);

    void getValues(NameMap& names, ValueMap& flags, ValueMap& steps,
                   ValueMap& variants, ValueMap& sys_steps) const;
    [[nodiscard]] PreviewSnapshot getValues2() const;
    void setValues2(const PreviewSnapshot& values);
    [[nodiscard]] bool focusInValues() const noexcept { return focus_in_values_; }
    void focusInValues(bool value) noexcept { focus_in_values_ = value; }

    void store();
    void refUndoMax();
    void savePreviewValues();
    void refreshFlags();
    void refFlagAndStep();
    void refDataVersion();
    [[nodiscard]] std::shared_ptr<lib::TextEdit> createEditor(
        const std::shared_ptr<lib::Container>& parent, std::size_t column,
        String value) const;
    static String ctrlText(const std::shared_ptr<lib::Widget>& control);
    void editEnd(std::size_t row, std::size_t column,
                 const std::shared_ptr<lib::Widget>& control);
    void raiseModEvent(bool save_values = true);

    void resetValues();
    void resetValuesAll();
    void resetValues(std::span<const std::size_t> indices);
    [[nodiscard]] bool isInitialValues() const;
    [[nodiscard]] bool isInitialValuesAll() const;
    [[nodiscard]] bool isInitialValues(std::span<const std::size_t> indices) const;
    [[nodiscard]] bool undo();
    [[nodiscard]] bool redo();
    [[nodiscard]] std::shared_ptr<lib::TableView> control();

private:
    MessageContext context_;
    std::weak_ptr<void> props_lifetime_;
    std::weak_ptr<const int> editor_commons_lifetime_;
    bool message_{};
    std::vector<SPChar> target_chars_;
    NameMap names_;
    ValueMap flags_;
    ValueMap steps_;
    ValueMap variants_;
    ValueMap sys_steps_;
    PreviewSnapshot typed_values_;
    std::shared_ptr<bool> notification_lifetime_{std::make_shared<bool>(true)};
    std::vector<lib::Action> disconnect_notifications_;
    lib::Dictionary<String, String> flag_on_values_;
    lib::Dictionary<String, String> flag_off_values_;
    lib::Dictionary<String, lib::Array<String>> step_values_;
    std::uint32_t selected_player_card_number_{};
    lib::Array<String> player_card_names_;
    bool include_player_values_{};
    PreviewSnapshot initial_;
    std::vector<PreviewSnapshot> history_;
    std::size_t history_index_{};
    std::size_t undo_max_{128};
    bool focus_in_values_{};
    enum class ValueKind {
        Name,
        SelectedPlayerNumber,
        PlayerName,
        Flag,
        Step,
        Variant,
        SystemStep,
    };
    struct ValueRow {
        ValueKind kind{ValueKind::Name};
        char name_key{};
        String key;
        std::size_t index{};
    };
    struct InlineEditOrigin {
        std::size_t row{};
        std::size_t column{};
        String displayed;
    };
    struct LiveVariantEdit {
        String key;
        expression::VariantVal original;
        expression::VariantVal published;
        bool stored{};
        std::size_t history_index{};
        std::vector<PreviewSnapshot> history_before{};
        std::size_t history_index_before{};
    };
    std::shared_ptr<LiveVariantEdit> live_variant_edit_;
    std::optional<InlineEditOrigin> inline_edit_origin_;
    void cancelLiveVariantEdit(bool notify = true);
    std::shared_ptr<lib::TableView> table_;
    std::shared_ptr<lib::Menu> values_menu_;
    std::vector<ValueRow> value_rows_;
    void refreshValuesMenu();
    void valuesCommand(StringView command);
    void copyValues();
    void pasteValues();

    void rebuild(bool preserve_names = true, bool preserve_typed_values = true);
    void refreshTable();
    void applySnapshot(const PreviewSnapshot& snapshot);
    void savePreviewValuesOnClose();
    void saveColumnWidths();
    [[nodiscard]] PreviewSnapshot defaultValues() const;
    void applyEditedValue(const ValueRow& binding, String value);
    void commitEditedValue(std::size_t row, std::size_t column, String value,
                           String previous_display);
    void raiseModEventThenSaveValues();
};

void configureVariantValueEditor(const std::shared_ptr<lib::TextEdit>& editor,
                                 String hint, String invalid_format,
                                 lib::Action on_changed = {});

[[nodiscard]] std::shared_ptr<lib::TextEdit> variantValueEditor(
    const MessageContext& context, const std::shared_ptr<lib::Container>& parent,
    String value);

void getPreviewValues(const Props& prop, const Summary* summ,
                      const usecounter::UseCounter* uc,
                      std::span<const SPChar> target_chars, NameMap& names,
                      msgutils::VarGetter& flags, msgutils::VarGetter& steps,
                      msgutils::VarGetter& variants,
                      msgutils::VarGetter& sys_steps);

[[nodiscard]] msgutils::VarGetter getPreviewSysSteps(
    const Props& prop, const Summary* summ, bool expand_sharps);

class MsgPreview {
public:
    MsgPreview(MessageContext context);

    [[nodiscard]] Skin::Ptr summSkin() const;
    void refImageScale();
    void refImageScaleImpl();
    void invalidateImageCache();
    void text(const CardImageList& paths, bool single_line, String message,
              bool center_x, bool center_y, bool boundary_check,
              bool refresh_now = true);
    void refresh();
    void refreshImpl();
    [[nodiscard]] bool focusInValues() const;
    [[nodiscard]] bool undo();
    [[nodiscard]] bool redo();
    [[nodiscard]] std::shared_ptr<lib::TableView> valuesControl();
    [[nodiscard]] const ImageDataWithScale& image() const noexcept { return image_; }
    [[nodiscard]] const String& message() const noexcept { return message_; }
    [[nodiscard]] PreviewSnapshot valuesSnapshot() const;
    void valuesSnapshot(const PreviewSnapshot& value);
    [[nodiscard]] lib::AudioHandle playSound(StringView path, float volume = 1.0F);
    void stopSound() noexcept;

private:
    MessageContext context_;
    Skin::Ptr force_skin_;
    std::shared_ptr<PreviewValues> values_;
    CardImageList image_paths_;
    CardImageList decoded_image_paths_;
    std::vector<ImageDataWithScale> talker_images_;
    std::vector<types::CardImagePosition> image_positions_;
    // 読み込んだSkinを保持し、固定の素材識別子で比較する。 summSkin()
    // は更新ごとに同等のSkinを作り直す場合があるため、生
    // ポインターを使うと refreshValues()後に
    // 無効になり、通常の入力までスキン変更と誤認してしまう。
    Skin::Ptr decoded_skin_;
    std::uint32_t decoded_scale_{};
    bool image_cache_valid_{};
    bool single_line_{};
    String message_;
    bool center_x_{};
    bool center_y_{};
    bool boundary_check_{};
    bool text_refresh_pending_{true};
    ImageDataWithScale image_;
    lib::AudioHandle audio_handle_{};

    void refreshValues();
};

[[nodiscard]] ImageDataWithScale previewMessage(
    const MessageContext& context, const Props& prop, const Summary* summ,
    const std::function<Skin::Ptr()>& skin,
    std::span<const ImageDataWithScale> talkers,
    std::span<const types::CardImagePosition> poses, bool single_line,
    StringView message, std::span<const String> selections,
    const NameMap& names, const ValueMap& flags, const ValueMap& steps,
    const ValueMap& variants, const ValueMap& sys_steps, bool center_x,
    bool center_y, bool boundary_check, bool upload = true);

[[nodiscard]] std::shared_ptr<lib::Menu> setupSPCharsMenu(
    const MessageContext& context, const Summary* summ,
    const std::function<Skin::Ptr()>& skin,
    const usecounter::UseCounter* uc,
    const std::shared_ptr<lib::Menu>& parent_menu, bool full,
    bool expand_sharps, const std::function<bool()>& is_expand_sp_chars,
    const std::function<void(StringView)>& insert);

[[nodiscard]] String createSPCharPreview(
    const Props& prop, const Summary* summ, const Skin::Ptr& skin,
    const usecounter::UseCounter* uc, String text, bool expand_sharps,
    const msgutils::VarGetter& override_flag_value = {},
    const msgutils::VarGetter& override_step_value = {});

[[nodiscard]] String getSPCharPreviewValue(const Props& prop, char name);

}  // namespace cwxeditor::cwx::editor::gui::dwt::messageutils
