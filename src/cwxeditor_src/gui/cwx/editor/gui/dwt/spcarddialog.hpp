#pragma once

// 特殊カード編集ダイアログの共通C++20実装。 モ
// デルにはエリア・カードの参照構造を使い、操作部品
// はEditor共通の部品・サービスから提供する。

#include "cwxeditor_src/core/cwx/area.hpp"
#include "cwxeditor_src/core/cwx/card.hpp"
#include "cwxeditor_src/core/cwx/msgs.hpp"
#include "cwxeditor_src/core/cwx/skin.hpp"
#include "cwxeditor_src/core/cwx/summary.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/core/cwx/usecounter.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/absdialog.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/areaviewutils.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/bgimagedialog.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/chooser.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dprops.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dmenu.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dutils.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/imageselect.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/incsearch.hpp"
#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/layout.hpp"
#include "cwxeditor_src/lib/services.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::commons {
class Commons;
}

namespace cwxeditor::cwx::editor::gui::dwt::spcarddialog {

namespace absdialog = ::cwxeditor::cwx::editor::gui::dwt::absdialog;
namespace area = ::cwxeditor::cwx::area;
namespace areaviewutils = ::cwxeditor::cwx::editor::gui::dwt::areaviewutils;
namespace bgimagedialog = ::cwxeditor::cwx::editor::gui::dwt::bgimagedialog;
namespace card = ::cwxeditor::cwx::card;
namespace dprops = ::cwxeditor::cwx::editor::gui::dwt::dprops;
namespace dutils = ::cwxeditor::cwx::editor::gui::dwt::dutils;
namespace dmenu = ::cwxeditor::cwx::editor::gui::dwt::dmenu;
namespace editor_commons = ::cwxeditor::cwx::editor::gui::dwt::commons;
namespace chooser = ::cwxeditor::cwx::editor::gui::dwt::chooser;
namespace flag = ::cwxeditor::cwx::flag;
namespace lib = ::cwxeditor::lib;
namespace msgs = ::cwxeditor::cwx::msgs;
namespace skin = ::cwxeditor::cwx::skin;
namespace summary = ::cwxeditor::cwx::summary;
namespace structs = ::cwxeditor::cwx::structs;
namespace types = ::cwxeditor::cwx::types;
namespace usecounter = ::cwxeditor::cwx::usecounter;

using Props = dprops::Props;
using Summary = summary::Summary;
using Skin = skin::Skin;
using UseCounter = usecounter::UseCounter;
using UseCounterPtr = usecounter::CounterOwner;
using AbstractSpCard = area::AbstractSpCard;
using MenuCard = area::MenuCard;
using EnemyCard = area::EnemyCard;
using CastCard = card::CastCard;
using CastCardPtr = lib::Owner<CastCard>;
using ConstCastCardPtr = lib::Owner<const CastCard>;
using CardImage = card::CardImage;
using CardImageArray = area::CardImageArray;
using DSize = absdialog::DSize;

/// 元のEditorでデリゲートだった通知・サービスへのホス
/// ト接続。 ヘッドレスでも使えるよう、すべて任意とする。
struct SpCardDialogContext {
    areaviewutils::Commons common;
    lib::Services services{};
    editor_commons::Commons* editor_commons{};
    std::vector<lib::String> flags;
    std::vector<lib::String> card_groups;

    std::function<void()> refresh_toolbar;
    std::function<void()> refresh_card_groups;
    std::function<void()> refresh_warnings;
    std::function<void()> refresh_skin;
    std::function<void()> refresh_casts;
    std::function<void(AbstractSpCard*)> open_card_view;
    std::function<void(std::uint64_t)> open_cast_view;
    std::function<void(std::shared_ptr<AbstractSpCard>)> add_card;

    /// 素材を選ぶ。引数には現在表示中のパスを渡す。
    std::function<bgimagedialog::MaterialResult(std::string_view)>
        select_material;
    /// 選択した素材をすべての画像記録へ変換する。 実際のホス
    /// トの選択画面で、PC・話者の値も保持するために使う。
    std::function<CardImageArray(std::string_view)> select_images;
    std::function<std::vector<lib::String>(std::string_view)> text_warnings;
    std::function<std::vector<lib::String>(const CardImageArray&)>
        image_warnings;
    std::function<lib::String(std::string_view)> format_text;
    // ダイアログはキャストを読み、独立したCombo行のスナップショットを作る。 Summary
    // のconst inout取得と同じく、所有対象の内容も読み取り専用にする。
    std::function<std::vector<ConstCastCardPtr>()> casts;

    SpCardDialogContext() = default;
    explicit SpCardDialogContext(Props& prop)
        : common(prop), services(prop.images().services()) {}
    explicit SpCardDialogContext(areaviewutils::Commons value)
        : common(std::move(value)), services(common.services) {}
};

[[nodiscard]] std::shared_ptr<lib::Panel> makePanel(
    const lib::Services& services);

template <typename T>
[[nodiscard]] std::shared_ptr<T> makeControl(const lib::Services& services,
                                               lib::ComponentKind kind) {
    if (services.gui) {
        if (auto candidate = services.gui->create_component(kind)) {
            if (auto typed = std::dynamic_pointer_cast<T>(std::move(candidate)))
                return typed;
        }
    }
    return std::make_shared<T>();
}

[[nodiscard]] std::string comboValue(const lib::ComboBox& combo);
void setComboValue(lib::ComboBox& combo, std::string_view value);
[[nodiscard]] std::string messageValue(const msgs::Message& message);
[[nodiscard]] std::string formatMessage(std::string value,
                                         std::string_view argument);
[[nodiscard]] bool isTargetVersion(const Props* prop, const Summary* summ,
                                   std::string_view version);

/// 両ダイアログが使うカード画像選択の共通実装。
/// 画像配列を公開し、PC・話者の項目をGUI
/// 固有の値へ変換せず保持できるようにする。
class CardImageSelector final {
public:
    std::vector<lib::Action> modEvent;

    CardImageSelector() = default;
    CardImageSelector(SpCardDialogContext context, Props& prop,
                      const Summary* summ, const std::shared_ptr<lib::Container>& parent,
                      types::CardImagePosition position, bool read_only = false,
                      bool sp_card_definitions = false,
                      std::optional<structs::CInsets> preview_insets = std::nullopt,
                      std::function<lib::String()> export_name = {},
                      const Skin* explicit_force_skin = nullptr);

    [[nodiscard]] const std::shared_ptr<lib::Panel>& widget() const noexcept {
        return panel_;
    }
    [[nodiscard]] const std::shared_ptr<lib::TextEdit>& pathControl() const
        noexcept { return path_; }
    [[nodiscard]] const std::shared_ptr<lib::Button>& chooseControl() const
        noexcept { return choose_; }
    [[nodiscard]] const CardImageArray& images() const noexcept {
        return images_;
    }
    void images(CardImageArray value);
    [[nodiscard]] std::vector<lib::String> warnings() const;
    [[nodiscard]] bgimagedialog::MaterialResult materialPath(bool force);
    void previewSize(std::int32_t, std::int32_t,
                     const structs::CInsets& = {}) noexcept;
    void imageListOwner(lib::Owner<lib::Window> value) noexcept;
    void active(bool value);
    void enabled(bool value);
    // ALLではファイル一覧を一行分短くし、レイヤーの見出しも詰める。
    void compactForAll();

private:
    SpCardDialogContext context_;
    std::weak_ptr<const int> editor_commons_lifetime_;
    Props* prop_{};
    const Summary* summ_{};
    types::CardImagePosition position_{types::CardImagePosition::Default};
    std::shared_ptr<lib::Panel> panel_;
    std::shared_ptr<lib::TextEdit> path_;
    std::shared_ptr<lib::Button> choose_;
    std::shared_ptr<lib::ListView> image_list_;
    std::shared_ptr<lib::Button> remove_;
    std::shared_ptr<lib::Button> up_;
    std::shared_ptr<lib::Button> down_;
    CardImageArray images_;
    std::unique_ptr<imageselect::ImageSelectBase> selector_;
    bool syncing_{};

    void notifyChanged();
    void chooseMaterial(bool force = false);
    void refreshList(std::size_t selection = 0U);
};

enum class CardAnimationPanelType {
    MenuCard,
    SetFlag,
    MoveCard,
};

/// 特殊カード・イベント画面で共用する、カードの動作速度の操作部品。
class CardAnimationPanel final {
public:
    std::vector<lib::Action> modEvent;

    CardAnimationPanel() = default;
    CardAnimationPanel(areaviewutils::Commons common, const Summary* summ,
                       std::shared_ptr<lib::Container> parent,
                       CardAnimationPanelType type);
    CardAnimationPanel(areaviewutils::Commons common, const Summary* summ,
                       lib::Container& parent, CardAnimationPanelType type);

    [[nodiscard]] const std::shared_ptr<lib::Panel>& widget() const noexcept {
        return panel_;
    }
    [[nodiscard]] const std::shared_ptr<lib::CheckBox>& overrideControl() const
        noexcept { return override_card_speed_; }
    [[nodiscard]] const std::shared_ptr<lib::NumberEdit>& speedControl() const
        noexcept { return speed_; }
    [[nodiscard]] const std::shared_ptr<lib::CheckBox>& forceControl() const
        noexcept { return force_override_; }
    [[nodiscard]] std::vector<lib::String> warnings() const;
    void speed(int value);
    [[nodiscard]] int speed() const;
    void overrideCardSpeed(bool value);
    [[nodiscard]] bool overrideCardSpeed() const;
    void updateEnabled();

private:
    areaviewutils::Commons common_;
    const Summary* summ_{};
    CardAnimationPanelType type_{CardAnimationPanelType::MenuCard};
    std::shared_ptr<lib::Panel> panel_;
    std::shared_ptr<lib::CheckBox> override_card_speed_;
    std::shared_ptr<lib::NumberEdit> speed_;
    std::shared_ptr<lib::CheckBox> force_override_;

    void raiseModEvent();
};

/// 二つの具体的なテンプレート実体で共用する基底コンテキスト。
template <typename C>
class SpCardDialog : public absdialog::AbsDialog {
    static_assert(std::is_same_v<C, MenuCard> || std::is_same_v<C, EnemyCard>);

public:
    using CardPtr = lib::Owner<C>;

    class SDListener {
    public:
        std::function<void()> disposed;
        void widgetDisposed() { if (disposed) disposed(); }
    };

    class EnemyDrop {
    public:
        std::string summary_id;
        std::function<bool(std::uint64_t)> accepted;
        bool dragEnter() const noexcept { return true; }
        bool dragOver() const noexcept { return true; }
        bool drop(std::string_view data) const;
    };

    class CardPaint {
    public:
        std::function<void()> paint;
        void paintControl() const { if (paint) paint(); }
    };

    class Repaint {
    public:
        std::function<void()> selected;
        void widgetSelected() const { if (selected) selected(); }
    };

    SpCardDialog(SpCardDialogContext context, Props& prop, const Summary* summ,
                 const Skin* force_skin, CardPtr card, bool create,
                 std::shared_ptr<lib::Window> parent = {},
                 UseCounterPtr counter = {});
    SpCardDialog(areaviewutils::Commons common, Props& prop,
                 std::shared_ptr<lib::Window> parent, const Summary* summ,
                 const Skin* force_skin, UseCounterPtr counter, CardPtr card,
                 bool create);

    SpCardDialog(const SpCardDialog&) = delete;
    SpCardDialog& operator=(const SpCardDialog&) = delete;
    ~SpCardDialog() override;

    [[nodiscard]] CardPtr card() const noexcept { return card_; }
    [[nodiscard]] const Skin* summSkin() const noexcept {
        return summ_skin_ ? summ_skin_ : context_.common.skin;
    }
    [[nodiscard]] const SpCardDialogContext& context() const noexcept {
        return context_;
    }
    [[nodiscard]] const std::vector<lib::String>& warnings() const noexcept {
        return warnings_;
    }
    [[nodiscard]] bool creating() const noexcept { return create_; }

    [[nodiscard]] const std::shared_ptr<lib::ComboBox>& flagControl() const
        noexcept { return flag_; }
    [[nodiscard]] const std::shared_ptr<lib::NumberEdit>& xControl() const
        noexcept { return x_; }
    [[nodiscard]] const std::shared_ptr<lib::NumberEdit>& yControl() const
        noexcept { return y_; }
    [[nodiscard]] const std::shared_ptr<lib::NumberEdit>& scaleControl() const
        noexcept { return scale_; }
    [[nodiscard]] const std::shared_ptr<lib::NumberEdit>& layerControl() const
        noexcept { return layer_; }
    [[nodiscard]] const std::shared_ptr<lib::ComboBox>& cardGroupControl() const
        noexcept { return card_group_; }
    [[nodiscard]] const std::shared_ptr<CardAnimationPanel>& animationControl()
        const noexcept { return animation_speed_; }

    /// ダイアログの適用ボタンと同じ操作を実行する。
    [[nodiscard]] bool applyChanges() { return apply(); }
    void refreshWarning();
    void updateEnabled();
    void refDataVersion();
    void refSkin();
    void refCast(const CastCard* cast);
    void delMenuCard(std::string_view cwx_path);

    template <typename T = C>
    [[nodiscard]] std::enable_if_t<std::is_same_v<T, EnemyCard>,
                                   const std::shared_ptr<lib::ComboBox>&>
    castControl() const noexcept { return casts_; }

    template <typename T = C>
    [[nodiscard]] std::enable_if_t<std::is_same_v<T, MenuCard>,
                                   const std::shared_ptr<lib::TextEdit>&>
    nameControl() const noexcept { return name_; }

    template <typename T = C>
    [[nodiscard]] std::enable_if_t<std::is_same_v<T, MenuCard>,
                                   const std::shared_ptr<lib::TextEdit>&>
    descriptionControl() const noexcept { return desc_; }

    template <typename T = C>
    [[nodiscard]] std::enable_if_t<std::is_same_v<T, MenuCard>,
                                   const std::shared_ptr<lib::CheckBox>&>
    expandSpecialCharactersControl() const noexcept { return expand_sp_chars_; }

    template <typename T = C>
    [[nodiscard]] std::enable_if_t<std::is_same_v<T, EnemyCard>,
                                   const std::shared_ptr<lib::CheckBox>&>
    overrideNameControl() const noexcept { return override_name_enabled_; }

    template <typename T = C>
    [[nodiscard]] std::enable_if_t<std::is_same_v<T, EnemyCard>,
                                   const std::shared_ptr<lib::TextEdit>&>
    overrideNameTextControl() const noexcept { return override_name_; }

    template <typename T = C>
    [[nodiscard]] std::enable_if_t<std::is_same_v<T, EnemyCard>,
                                   const std::shared_ptr<lib::CheckBox>&>
    overrideImageControl() const noexcept { return override_image_enabled_; }

    template <typename T = C>
    [[nodiscard]] std::enable_if_t<std::is_same_v<T, EnemyCard>,
                                   const std::map<types::ActionCardType,
                                                  std::shared_ptr<lib::CheckBox>>&>
    actionControls() const noexcept { return actions_; }

    void refreshCasts();
    void cardIncSearch(std::string_view value);
    void openCardView();

protected:
    void setup(Container& area) override;
    bool apply() override;

private:
    void bindExternalNotifications();
    void unbindExternalNotifications() noexcept;
    void refreshDescriptionLayout();

    SpCardDialogContext context_;
    editor_commons::Commons* notification_commons_{};
    std::uint64_t del_menu_card_connection_{};
    std::uint64_t ref_cast_connection_{};
    std::uint64_t ref_item_connection_{};
    std::uint64_t del_cast_connection_{};
    std::uint64_t ref_skin_connection_{};
    std::uint64_t ref_data_version_connection_{};
    std::uint64_t ref_target_version_connection_{};
    std::uint64_t ref_preview_values_connection_{};
    std::uint64_t ref_image_scale_connection_{};
    Props* prop_{};
    const Summary* summ_{};
    UseCounterPtr counter_;
    CardPtr card_;
    bool create_{};
    bool summary_skin_pinned_{};
    const Skin* summ_skin_{};
    const Skin* force_skin_{};
    Skin::Ptr source_skin_owner_;
    std::shared_ptr<lib::ComboBox> card_group_;
    std::shared_ptr<CardAnimationPanel> animation_speed_;
    std::shared_ptr<lib::ComboBox> flag_;
    std::unique_ptr<chooser::FlagChooser<flag::Flag, true>> flag_chooser_;
    std::shared_ptr<lib::NumberEdit> x_;
    std::shared_ptr<lib::NumberEdit> y_;
    std::shared_ptr<lib::NumberEdit> scale_;
    std::shared_ptr<lib::NumberEdit> layer_;
    std::vector<lib::String> warnings_;
    bool setup_complete_{};

    // メニューカード用の操作部品。
    std::shared_ptr<lib::TextEdit> name_;
    std::vector<dmenu::TextMenuModifyPtr> text_menus_;
    std::shared_ptr<CardImageSelector> image_selector_;
    std::shared_ptr<lib::TextEdit> desc_;
    std::shared_ptr<lib::CheckBox> expand_sp_chars_;

    // エネミーカード用の操作部品。
    std::shared_ptr<lib::ComboBox> casts_;
    std::vector<std::uint64_t> cast_ids_;
    std::uint64_t selected_id_{};
    std::string cast_filter_;
    std::unique_ptr<incsearch::IncSearch> cast_inc_search_;
    std::shared_ptr<lib::Menu> cast_popup_;
    lib::WidgetPtr cast_drop_target_;
    std::shared_ptr<int> cast_host_lifetime_{std::make_shared<int>(0)};
    std::map<types::ActionCardType, std::shared_ptr<lib::CheckBox>> actions_;
    std::shared_ptr<lib::Canvas> image_;
    lib::ImageData preview_image_data_;
    lib::ImageResourceSlot preview_image_resource_{};
    std::shared_ptr<lib::CheckBox> override_name_enabled_;
    std::shared_ptr<lib::TextEdit> override_name_;
    std::shared_ptr<lib::CheckBox> override_image_enabled_;
    std::shared_ptr<lib::Container> actions_panel_;
    // ALLの左の分割列。カード位置のグループも配置する。
    std::shared_ptr<lib::Container> enemy_left_;

    [[nodiscard]] static std::string dialogTitle(Props& prop,
                                                   const Summary* summ,
                                                   const CardPtr& card,
                                                   bool create);
    void createCommonControls(const std::shared_ptr<lib::Container>& root,
                              const std::shared_ptr<lib::Container>& side);
    void createSideControls(const std::shared_ptr<lib::Container>& side);
    void createMenuControls(const std::shared_ptr<lib::Container>& root);
    void createEnemyControls(const std::shared_ptr<lib::Container>& root);
    void createActionControls();
    void setInitialValues();
    void refImageScale();
    void setCardGroupText(std::string_view value);
    void setFlagText(std::string_view value);
    [[nodiscard]] std::string flagValue() const;
    [[nodiscard]] std::string cardGroupValue() const;
    [[nodiscard]] CardImageArray selectedImages(bool force);
    [[nodiscard]] std::vector<lib::String> textWarnings(
        std::string_view value, std::string_view itemName) const;
    void updateToolTip();
    void notifyToolbar() const;
    void notifyCardGroups() const;
    void notifyWarnings() const;
    [[nodiscard]] dmenu::TextMenuModifyPtr setupSpecialTextMenu(
        const std::shared_ptr<lib::TextEdit>& editor,
        std::function<bool()> enabled);
    [[nodiscard]] bool hasExchangeItem() const;
    [[nodiscard]] bool allRequiredActionsSelected() const;
    [[nodiscard]] std::vector<ConstCastCardPtr> castList() const;
    [[nodiscard]] ConstCastCardPtr selectedCast() const;
    void redrawPreview();
    void paintPreview(lib::DrawingContext& context, lib::Rect bounds) const;
    void releasePreviewResource() noexcept;
};

using MenuCardDialog = SpCardDialog<MenuCard>;
using EnemyCardDialog = SpCardDialog<EnemyCard>;

}  // namespace cwxeditor::cwx::editor::gui::dwt::spcarddialog
