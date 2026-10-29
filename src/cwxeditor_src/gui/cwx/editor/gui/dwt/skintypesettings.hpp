#pragma once

#include "cwxeditor_src/core/cwx/background.hpp"
#include "cwxeditor_src/core/cwx/skin.hpp"
#include "cwxeditor_src/core/cwx/structs.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dprops.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dskin.hpp"
#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/dialogs.hpp"
#include "cwxeditor_src/lib/services.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <map>
#include <string>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::areaview {class BgImagesView;}
namespace cwxeditor::cwx::editor::gui::dwt::bgimagedialog {class BgImageDialog;}
namespace cwxeditor::cwx::editor::gui::dwt::skintypesettings {

namespace lib = ::cwxeditor::lib;
namespace dprops = ::cwxeditor::cwx::editor::gui::dwt::dprops;
namespace dskin = ::cwxeditor::cwx::editor::gui::dwt::dskin;
namespace background = ::cwxeditor::cwx::background;
namespace skin = ::cwxeditor::cwx::skin;
namespace structs = ::cwxeditor::cwx::structs;
namespace types = ::cwxeditor::cwx::types;

using String = lib::String;
using StringView = lib::StringView;
using Props = dprops::Props;
using Services = lib::Services;
using SettingsWithSkinType = structs::SettingsWithSkinType;
using ElementOverride = structs::ElementOverride;
using BgImageS = structs::BgImageS;
using Element = types::Element;
using Skin = skin::Skin;
using Parent = std::shared_ptr<lib::Container>;
using Window = std::shared_ptr<lib::Window>;
using Action = lib::Action;

/// 設定画面が使うDの選択部品に対応する共通
/// 実装。 設定を作業コピーとして保持し、Props
/// へ戻すタイミングは所有者が決めるとい
/// うDの規則を保つ。 ネイティブ部品や
/// アプリのCommonsは公開しない。
class SkinTypeChooser final {
public:
    using CanSelect = std::function<bool(bool deleting)>;
    using CreateSettingsFromDefault = std::function<SettingsWithSkinType()>;

    std::vector<Action> selectEvent;
    Action beforeEdit;
    std::function<void(StringView, StringView)> renamed;
    std::function<void(bool, std::function<void(bool)>)> confirmSelection;

    SkinTypeChooser(Props& prop, Parent parent = {}, int style = 0,
                     CanSelect canSelect = {},
                     CreateSettingsFromDefault createSettingsFromDefault = {},
                     Services services = {}, Window owner = {});
    SkinTypeChooser(Props& prop, lib::Container& parent, int style = 0,
                     CanSelect canSelect = {},
                     CreateSettingsFromDefault createSettingsFromDefault = {},
                     Services services = {}, Window owner = {});

    SkinTypeChooser(const SkinTypeChooser&) = delete;
    SkinTypeChooser& operator=(const SkinTypeChooser&) = delete;
    SkinTypeChooser(SkinTypeChooser&&) = delete;
    SkinTypeChooser& operator=(SkinTypeChooser&&) = delete;
    ~SkinTypeChooser();

    [[nodiscard]] const Parent& widget() const noexcept { return root_; }
    [[nodiscard]] const std::shared_ptr<lib::ComboBox>& typeControl() const noexcept {
        return type_;
    }
    [[nodiscard]] const std::shared_ptr<lib::Button>& editButton() const noexcept {
        return edit_;
    }
    [[nodiscard]] const std::shared_ptr<lib::Button>& addButton() const noexcept {
        return add_;
    }
    [[nodiscard]] const std::shared_ptr<lib::Button>& deleteButton() const noexcept {
        return del_;
    }
    [[nodiscard]] const std::shared_ptr<lib::Button>& cloneButton() const noexcept {
        return clone_;
    }

    /// 設定の添字を返す。既定行が選ばれていれば-1。
    [[nodiscard]] int selected() const noexcept;
    [[nodiscard]] int lastSelectedRow() const noexcept { return last_selected_; }
    [[nodiscard]] const std::vector<SettingsWithSkinType>&
    settingsWithSkinTypes() const noexcept { return settings_; }
    [[nodiscard]] std::vector<SettingsWithSkinType>&
    settingsWithSkinTypes() noexcept { return settings_; }

    /// Comboの行を選ぶ。rowは実際の行番号で、0は既定値を表す。
    void select(int row, bool notify = true);
    void refresh();
    void setCanSelect(CanSelect callback) { can_select_ = std::move(callback); }

    /// 以下の操作は四つのツールバーボタンにも接続する
    /// 。 空でないtypeを渡せばモーダル編集を開か
    /// ずに実行でき、ヘッドレスでも同じ動作を使える。
    [[nodiscard]] bool editSelected(StringView type = {});
    [[nodiscard]] bool addType(StringView type = {});
    [[nodiscard]] bool deleteSelected(bool confirm = true);
    [[nodiscard]] bool cloneSelected(StringView type = {});

private:
    Props* prop_{};
    Parent root_{};
    Window owner_{};
    Services services_{};
    CanSelect can_select_{};
    CreateSettingsFromDefault create_settings_{};
    std::vector<SettingsWithSkinType> settings_{};
    std::shared_ptr<lib::ComboBox> type_{};
    std::shared_ptr<lib::Button> edit_{};
    std::shared_ptr<lib::Button> add_{};
    std::shared_ptr<lib::Button> del_{};
    std::shared_ptr<lib::Button> clone_{};
    int last_selected_{};
    std::shared_ptr<int> lifetime_{std::make_shared<int>(0)};
    bool requestSelection(bool deleting, std::function<void()> accepted);

    template <typename Component>
    [[nodiscard]] std::shared_ptr<Component> createComponent(
        lib::ComponentKind kind) const;
    void build();
    void rebuildTypeItems(StringView selectedType = {});
    void sortTypes();
    void raiseSelectEvent();
    // Dのcomm.put(_edit/_del, () => selected != -1)に対応する有効判定。
    void refreshButtons();
    [[nodiscard]] bool validNewType(StringView type, int except = -1) const;
    [[nodiscard]] bool chooseType(String title, String desc,
                                  StringView defaultValue, String& result,
                                  lib::ResourceId icon = 0U);
    // 設定済みの全種類。Dの追加・複製用のexcludeTypesに対応する。
    [[nodiscard]] std::vector<String> configuredTypes() const;
    // 種類名を尋ねてからthen(type)を呼ぶ。デスクトップでは同
    // 期的に、ALLでは画面の終了時に呼ぶ。 ブラウザー内でshow_modal
    // を同期的に待ち、Editorを停止させないようにする。
    void askType(String title, String desc, lib::ResourceId icon,
                 std::vector<String> excluded, String initial,
                 std::function<void(String)> then);
    std::shared_ptr<class EditSkinTypeDialog> pending_type_dialog_{};
};

/// 一つのスキン種別名を入力・選択するモーダル画面。
class EditSkinTypeDialog final {
public:
    EditSkinTypeDialog(Props& prop, Window parent, String title,
                       lib::ResourceId icon, String desc,
                       std::vector<String> excludeTypes,
                       String defaultValue, Services services = {});

    EditSkinTypeDialog(const EditSkinTypeDialog&) = delete;
    EditSkinTypeDialog& operator=(const EditSkinTypeDialog&) = delete;
    ~EditSkinTypeDialog();

    void setup(lib::Container& area);
    void setup(const std::shared_ptr<lib::Container>& area);
    [[nodiscard]] bool open();
    // ALL/Web用の、処理をブロックしないモーダル（begin_modal）。 終了
    // 時にdone(accepted)を呼ぶ。バックエンドが未対応ならfalseを返す。
    [[nodiscard]] bool openAsync(std::function<void(bool)> done);
    void refreshTypes();
    void setType(String value);
    [[nodiscard]] const String& type() const noexcept { return type_; }
    [[nodiscard]] bool valid() const;
    [[nodiscard]] const std::shared_ptr<lib::Dialog>& dialog() const noexcept {
        return dialog_;
    }
    [[nodiscard]] const std::shared_ptr<lib::ComboBox>& typeControl() const noexcept {
        return type_control_;
    }

private:
    std::function<void()> clear_size_binding_{};
    Props* prop_{};
    Window parent_{};
    Services services_{};
    String title_{};
    lib::ResourceId icon_{};
    String desc_{};
    std::vector<String> exclude_types_{};
    String type_{};
    std::shared_ptr<lib::Dialog> dialog_{};
    std::function<void(bool)> async_done_{};
    void finishAsync(bool accepted);
    std::shared_ptr<lib::Container> content_{};
    std::shared_ptr<lib::ComboBox> type_control_{};
    std::shared_ptr<lib::Button> ok_button_{};
    bool initialized_{};

    void createDialog();
    void refreshValidity() noexcept;
};

/// 設定ページで既定の背景セルを編集するダイアログ。
class DefBgImgDialog final {
public:
    std::vector<Action> appliedEvent;
    std::vector<Action> closeEvent;

    DefBgImgDialog(Props& prop, Window parent,
                   const std::vector<BgImageS>& bgImagesDefault,
                   const Skin& skin, Services services = {},
                   std::function<lib::Action(lib::Action)> subscribeUndoMax = {});

    DefBgImgDialog(const DefBgImgDialog&) = delete;
    DefBgImgDialog& operator=(const DefBgImgDialog&) = delete;
    ~DefBgImgDialog();

    [[nodiscard]] bool noScenario() const noexcept { return true; }
    [[nodiscard]] std::vector<BgImageS> backs() const;
    void setBacks(const std::vector<BgImageS>& value);
    [[nodiscard]] bool open();
    [[nodiscard]] bool active();
    // 所有するスキンの識別子が変わったら、モードレスの作業コピーを終了する。
    void forceCancel();
    void setup(lib::Container& area);
    void setup(const std::shared_ptr<lib::Container>& area);
    [[nodiscard]] bool apply();
    [[nodiscard]] const std::shared_ptr<lib::Dialog>& dialog() const noexcept {
        return dialog_;
    }
    [[nodiscard]] const std::shared_ptr<lib::TableView>& view() const noexcept {
        return view_;
    }
    [[nodiscard]] const std::shared_ptr<background::BgImageContainer>&
    container() const noexcept { return container_; }

    [[nodiscard]] bool append(const BgImageS& value);
    [[nodiscard]] bool remove(std::size_t index);
    [[nodiscard]] bool move(std::size_t first, std::size_t second);

private:
    std::function<void()> clear_size_binding_{};
    Props* prop_{};
    Window parent_{};
    const Skin* skin_{};
    Services services_{};
    std::function<lib::Action(lib::Action)> subscribe_undo_max_{};
    std::shared_ptr<lib::Dialog> dialog_{};
    std::shared_ptr<lib::Container> content_{};
    std::shared_ptr<lib::TableView> view_{};
    std::shared_ptr<lib::Button> apply_button_{};
    std::shared_ptr<background::BgImageContainer> container_{};
    std::shared_ptr<areaview::BgImagesView> background_view_{};
    std::map<const background::BgImage*,std::shared_ptr<bgimagedialog::BgImageDialog>> background_editors_;
    bool initialized_{};
    bool modified_{};
    bool retired_{};
    std::shared_ptr<void> lifetime_ = std::make_shared<int>(0);

    void createDialog();
    void refreshView();
    void openBackgroundEditor(background::BgImagePtr value,bool create);
    void createBackgroundEditor(int type);
    void retireBackgroundEditors();
    void raiseApplied();
    void raiseClosed();
};

/// 七種類の属性ごとのアイコン・名前・対象の上書きを編集する表。
class ElementOverrideView final {
public:
    std::vector<Action> modEvent;

    ElementOverrideView(Props& prop, Parent parent = {}, int style = 0,
                        Services services = {});
    ElementOverrideView(Props& prop, lib::Container& parent, int style = 0,
                        Services services = {});
    ~ElementOverrideView();

    ElementOverrideView(const ElementOverrideView&) = delete;
    ElementOverrideView& operator=(const ElementOverrideView&) = delete;
    ElementOverrideView(ElementOverrideView&&) = delete;
    ElementOverrideView& operator=(ElementOverrideView&&) = delete;

    void raiseModEvent();
    [[nodiscard]] const Parent& widget() const noexcept { return root_; }
    [[nodiscard]] const std::shared_ptr<lib::TableView>& table() const noexcept {
        return elements_;
    }
    [[nodiscard]] std::vector<ElementOverride> elementOverrides() const;
    void elementOverrides(const std::vector<ElementOverride>& list);

    [[nodiscard]] bool setName(Element element, String value);
    [[nodiscard]] bool setTargetType(Element element, String value);
    [[nodiscard]] bool setIcon(Element element, String value);
    [[nodiscard]] bool selectIcon(Element element);
    // ホストが現在のSkinの対応拡張子を渡す。DのCommons.skinに対応する。
    void imageExtensions(lib::Array<String> values) { image_extensions_ = std::move(values); }
    [[nodiscard]] bool setPresetIcon(Element element, StringView file);
    [[nodiscard]] bool deleteIcon(Element element);

    [[nodiscard]] bool undo();
    [[nodiscard]] bool redo();
    void clearHistory() noexcept;
    void refUndoMax() noexcept;
    void enabled(bool value) noexcept;
    [[nodiscard]] bool enabled() const noexcept { return enabled_; }

private:
    lib::Array<String> image_extensions_{".png", ".jpg", ".jpeg", ".gif", ".bmp"};
    Props* prop_{};
    Parent root_{};
    Services services_{};
    std::shared_ptr<lib::TableView> elements_{};
    std::vector<ElementOverride> list_{};
    std::vector<std::vector<ElementOverride>> undo_{};
    std::vector<std::vector<ElementOverride>> redo_{};
    dprops::ElementImageCache image_cache_{};
    bool enabled_{true};
    std::shared_ptr<void> lifetime_{std::make_shared<int>(0)};
    std::uint64_t model_revision_{};
    std::uint64_t picker_revision_{};
    std::shared_ptr<lib::Menu> menu_;
    void setupActions();

    template <typename Component>
    [[nodiscard]] std::shared_ptr<Component> createComponent(
        lib::ComponentKind kind) const;
    void build();
    void refreshTable(bool resetTop = false);
    void store();
    [[nodiscard]] std::size_t indexOf(Element element) const noexcept;
    [[nodiscard]] bool canEditTarget(Element element) const noexcept;
    [[nodiscard]] ElementOverride& overrideFor(Element element) noexcept;
    [[nodiscard]] const ElementOverride& overrideFor(Element element) const noexcept;
};

}  // namespace cwxeditor::cwx::editor::gui::dwt::skintypesettings
