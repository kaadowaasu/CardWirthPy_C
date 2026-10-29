#pragma once
#include "cellrender.hpp"

// bgimagedialog.dのツールキッ
// トに依存しないC++20実装。 背景セルを
// 直接編集し、操作部品は共通の部品・配置・イ
// ベント・サービスを通じて公開する。 ホス
// トから素材選択・画像寸法・警告更新を渡せ
// 、ネイティブ部品への依存は持ち込まない。

#include "cwxeditor_src/core/cwx/background.hpp"
#include "cwxeditor_src/core/cwx/flag.hpp"
#include "cwxeditor_src/core/cwx/skin.hpp"
#include "cwxeditor_src/core/cwx/summary.hpp"
#include "cwxeditor_src/core/cwx/structs.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/core/cwx/usecounter.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/absdialog.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/areaviewutils.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/chooser.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dprops.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dmenu.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/imageselect.hpp"
#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/dialogs.hpp"
#include "cwxeditor_src/lib/events.hpp"
#include "cwxeditor_src/lib/layout.hpp"
#include "cwxeditor_src/lib/services.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::messageutils {
class PreviewValues;
}
namespace cwxeditor::cwx::editor::gui::dwt::commons {
class Commons;
}

namespace cwxeditor::cwx::editor::gui::dwt::bgimagedialog {

namespace absdialog = ::cwxeditor::cwx::editor::gui::dwt::absdialog;
namespace areaviewutils = ::cwxeditor::cwx::editor::gui::dwt::areaviewutils;
namespace background = ::cwxeditor::cwx::background;
namespace chooser = ::cwxeditor::cwx::editor::gui::dwt::chooser;
namespace dprops = ::cwxeditor::cwx::editor::gui::dwt::dprops;
namespace flag = ::cwxeditor::cwx::flag;
namespace imageselect = ::cwxeditor::cwx::editor::gui::dwt::imageselect;
namespace lib = ::cwxeditor::lib;
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
using DSize = absdialog::DSize;
using BgImage = background::BgImage;
using BgImagePtr = background::BgImagePtr;
using ImageCell = background::ImageCell;
using ImageCellPtr = lib::Owner<ImageCell>;
using TextCell = background::TextCell;
using TextCellPtr = lib::Owner<TextCell>;
using ColorCell = background::ColorCell;
using ColorCellPtr = lib::Owner<ColorCell>;
using PCCell = background::PCCell;
using PCCellPtr = lib::Owner<PCCell>;

/// ホストの素材選択画面が返す結果。
struct MaterialResult {
    bool cancel{};
    lib::String path;
};

/// 背景セルのダイアログへ、周囲のEditorから渡す値。
struct BgImageDialogContext {
    areaviewutils::Commons common;
    lib::Services services;
    // 明示的に選ばれたシナリオスキンの共有所有者。areaviewutils::Commons
    // の生ポインターは借用なので、 子の選択部品へ渡す際に新
    // しい空の削除関数付きshared_ptrで包まない。
    Skin::Ptr skin_owner;
    std::vector<lib::String> flags;
    std::vector<lib::String> cell_names;
    std::function<std::vector<lib::String>()> cell_names_provider;
    std::function<void()> refresh_toolbar;
    std::function<void()> refresh_cell_names;
    std::function<void()> refresh_warnings;
    std::function<void(std::string_view)> status;
    std::function<MaterialResult(std::string_view)> select_material;
    std::function<std::optional<lib::Size>(std::string_view)> image_size;
    std::function<std::vector<structs::BgImageSetting>()> settings;
    std::function<lib::String(std::string_view)> format_text;
    /// 任意のDのCommons相当の通知元。ダイアログの部品
    /// が有効な間だけ購読し、 破棄前にすべての接続を外す。
    commons::Commons* editor_commons{};

    BgImageDialogContext() = default;
    explicit BgImageDialogContext(Props& prop)
        : common(prop), services(prop.images().services()) {}
    explicit BgImageDialogContext(areaviewutils::Commons value)
        : common(std::move(value)), services(common.services) {}
};

/// 共通の色選択部品。widget()はColorWellを持
/// つパネルを返し、ダイアログの部品ツリーへ配置できる。 モデ
/// ル値はCRGBとし、ツールキット固有の色型を公開しない。
class ColorPicker final {
public:
    using Change = lib::Action;

    std::vector<Change> modEvent;

    ColorPicker() = default;
    ColorPicker(Props& prop, lib::Container& parent, bool transparency);
    ColorPicker(Props& prop, const std::shared_ptr<lib::Container>& parent,
                bool transparency);
    ColorPicker(const ColorPicker&) = delete;
    ColorPicker& operator=(const ColorPicker&) = delete;
    ColorPicker(ColorPicker&& other) noexcept;
    ColorPicker& operator=(ColorPicker&& other) noexcept;
    ~ColorPicker();

    [[nodiscard]] const std::shared_ptr<lib::Panel>& widget() const noexcept {
        return panel_;
    }
    [[nodiscard]] structs::CRGB color() const noexcept { return color_; }
    void color(structs::CRGB value) noexcept;
    [[nodiscard]] int alpha() const noexcept {
        return transparency_ ? alpha_ : 255;
    }
    void alpha(int value) noexcept;
    void enabled(bool value) noexcept;
    [[nodiscard]] bool enabled() const noexcept { return enabled_; }
    [[nodiscard]] const std::shared_ptr<lib::ColorWell>& colorWell() const
        noexcept { return color_well_; }
    [[nodiscard]] const std::shared_ptr<lib::Button>& selectButton() const
        noexcept { return color_button_; }
    [[nodiscard]] const std::shared_ptr<lib::NumberEdit>& alphaControl() const
        noexcept { return alpha_control_; }
    void notifyChanged();

private:
    std::shared_ptr<lib::Panel> panel_;
    std::shared_ptr<lib::ColorWell> color_well_;
    std::shared_ptr<lib::Button> color_button_;
    std::shared_ptr<lib::NumberEdit> alpha_control_;
    lib::Services services_;
    structs::CRGB color_{0, 0, 0, 255};
    int alpha_{255};
    bool transparency_{};
    bool enabled_{true};
    // 非同期の色選択完了で使う、この部品の現在のア
    // ドレス。 移動時に更新し、破棄時に消去する。
    std::shared_ptr<ColorPicker*> self_;

    void initialize(Props& prop, lib::Container* parent, bool transparency);
    void bindCallbacks();
    void detachCallbacks() noexcept;
    void chooseColor();
};

/// すべての背景セルダイアログに共通する、モデルと操作部品の状態。
class BgImageDialog : public absdialog::AbsDialog {
public:
    BgImageDialog(BgImageDialogContext context, Props& prop,
                  const Summary* summ, std::shared_ptr<lib::Window> parent,
                  lib::String title, lib::ResourceId image, bool resizable,
                  std::shared_ptr<DSize> size, BgImagePtr back, bool create,
                  UseCounterPtr counter = {});
    BgImageDialog(BgImageDialogContext context, Props& prop,
                  const Summary* summ, std::shared_ptr<lib::Window> parent,
                  lib::String title, lib::ResourceId image, bool resizable,
                  DSize* size, BgImagePtr back, bool create,
                  UseCounterPtr counter = {});
    ~BgImageDialog() override;

    BgImageDialog(const BgImageDialog&) = delete;
    BgImageDialog& operator=(const BgImageDialog&) = delete;

    [[nodiscard]] BgImagePtr back() const noexcept { return back_; }
    [[nodiscard]] const BgImageDialogContext& context() const noexcept {
        return context_;
    }
    [[nodiscard]] const Summary* summary() const noexcept { return summ_; }
    [[nodiscard]] bool creating() const noexcept { return create_; }
    [[nodiscard]] bool selected() const noexcept { return selected_; }
    void selected(bool value) noexcept { selected_ = value; }
    [[nodiscard]] const std::vector<lib::String>& warnings() const noexcept {
        return warnings_;
    }

    [[nodiscard]] const std::shared_ptr<lib::ComboBox>& flagControl() const
        noexcept { return flag_; }
    [[nodiscard]] chooser::FlagChooser<flag::Flag, true>* flagChooser() const
        noexcept { return flag_chooser_.get(); }
    [[nodiscard]] const std::shared_ptr<lib::ComboBox>& cellNameControl() const
        noexcept { return cell_name_; }
    [[nodiscard]] const std::shared_ptr<lib::NumberEdit>& xControl() const
        noexcept { return x_; }
    [[nodiscard]] const std::shared_ptr<lib::NumberEdit>& yControl() const
        noexcept { return y_; }
    [[nodiscard]] const std::shared_ptr<lib::NumberEdit>& widthControl() const
        noexcept { return w_; }
    [[nodiscard]] const std::shared_ptr<lib::NumberEdit>& heightControl() const
        noexcept { return h_; }
    [[nodiscard]] const std::shared_ptr<lib::CheckBox>& maskControl() const
        noexcept { return mask_; }
    [[nodiscard]] const std::shared_ptr<lib::NumberEdit>& layerControl() const
        noexcept { return layer_; }
    [[nodiscard]] const std::shared_ptr<lib::ComboBox>& smoothingControl() const
        noexcept { return smoothing_; }
    [[nodiscard]] const std::shared_ptr<lib::ComboBox>& easyControl() const
        noexcept { return easy_; }

    void delBgImage(std::string_view cwx_path);

protected:
    void setup(Container& area) override;
    bool apply() override = 0;
    virtual void setupSpecific(Container& area) = 0;
    virtual void initializeSpecific() {}
    [[nodiscard]] virtual bool usesMask() const noexcept { return false; }
    [[nodiscard]] virtual bool usesSmoothing() const noexcept { return false; }
    [[nodiscard]] virtual bool splitSpecificAndFlag() const noexcept {
        return false;
    }
    [[nodiscard]] virtual bool supportsOriginalSetting() const noexcept {
        return false;
    }
    virtual void refreshWarning();
    virtual void updatePreview() {}
    void connectExternalNotifications();
    virtual void updateMask();
    virtual void selectEasySetting();

    virtual void refDataVersion();
    [[nodiscard]] std::vector<lib::String> warningCommon() const;

    [[nodiscard]] std::shared_ptr<lib::GroupBox> createFlagPanel(
        lib::Container& parent);
    [[nodiscard]] std::shared_ptr<lib::GroupBox> createPositionPanel(
        lib::Container& parent, bool mask, bool smoothing);
    [[nodiscard]] std::shared_ptr<lib::Panel> createEasySettingsPanel(
        lib::Container& parent);
    void createPosPanel(lib::Container& parent, bool mask, bool smoothing);
    void setFirstParams();
    void applyParams(BgImage& back);
    void setBack(BgImagePtr back) noexcept { back_ = std::move(back); }
    void setCreate(bool value) noexcept { create_ = value; }
    void notifyToolbar() const;
    void markCustomSetting(bool size_changed = false);
    [[nodiscard]] std::string comboValue(const lib::ComboBox& combo) const;
    void selectSetting(std::size_t index);
    void setComboValue(lib::ComboBox& combo, std::string_view value);
    void addCommonControls(lib::Container& parent, bool include_flag);
    void refreshBgImageSettings();
    // Dは明示指定・独立したダイアログのスキンを保持する。 通常のシナリオでは、有
    // 効なダイアログから要求されるたびに所有Commonsの現在のスキンを読む。
    [[nodiscard]] Skin::Ptr resolveSummSkin(
        const Skin* force_skin, const Skin::Ptr& force_owner,
        const Skin* fallback_skin, const Skin::Ptr& fallback_owner) const noexcept;

    BgImageDialogContext context_;
    std::weak_ptr<const int> editor_commons_lifetime_;
    Props* prop_{};
    const Summary* summ_{};
    UseCounterPtr counter_;
    BgImagePtr back_;
    bool create_{};
    bool selected_{};
    bool original_setting_{};
    std::vector<structs::BgImageSetting> settings_;
    std::vector<types::Smoothing> smoothings_;
    std::vector<lib::String> warnings_;

    std::unique_ptr<chooser::FlagChooser<flag::Flag, true>> flag_chooser_;
    std::shared_ptr<lib::ComboBox> flag_;
    std::shared_ptr<lib::NumberEdit> x_;
    std::shared_ptr<lib::NumberEdit> y_;
    std::shared_ptr<lib::NumberEdit> w_;
    std::shared_ptr<lib::NumberEdit> h_;
    std::shared_ptr<lib::CheckBox> mask_;
    std::shared_ptr<lib::NumberEdit> layer_;
    std::shared_ptr<lib::ComboBox> smoothing_;
    std::shared_ptr<lib::ComboBox> easy_;
    std::shared_ptr<lib::ComboBox> cell_name_;
    std::shared_ptr<chooser::ChooserCombo> cell_name_chooser_;
    std::uint64_t del_bg_image_connection_{};
    std::uint64_t data_version_connection_{};
    std::uint64_t target_version_connection_{};
    std::uint64_t skin_connection_{};
    std::uint64_t image_scale_connection_{};
    std::uint64_t bg_settings_connection_{};
    std::uint64_t cell_names_connection_{};
    bool external_notifications_connected_{};

private:
    static lib::Services servicesFor(const BgImageDialogContext& context,
                                     Props& prop);
    void disconnectExternalNotifications() noexcept;
};

/// 素材パスと平滑化の状態を含む、画像セルの編集画面。
class ImageCellDialog final : public BgImageDialog {
public:
    ImageCellDialog(BgImageDialogContext context, Props& prop,
                    const Summary* summ, const Skin* force_skin,
                    ImageCellPtr back, bool create,
                    std::shared_ptr<lib::Window> parent = {},
                    UseCounterPtr counter = {});
    ImageCellDialog(areaviewutils::Commons common, Props& prop,
                    std::shared_ptr<lib::Window> parent, const Summary* summ,
                    const Skin* force_skin, UseCounterPtr counter,
                    ImageCellPtr back, bool create);

    [[nodiscard]] ImageCellPtr imageBack() const noexcept;
    [[nodiscard]] const std::shared_ptr<lib::TextEdit>& imageControl() const
        noexcept { return image_path_; }
    [[nodiscard]] imageselect::ImageSelectBase* imageSelect() const noexcept {
        return image_select_.get();
    }
    [[nodiscard]] const Skin* summSkin() const noexcept;

protected:
    void setupSpecific(Container& area) override;
    void initializeSpecific() override;
    void refreshWarning() override;
    void updateMask() override;
    void selectEasySetting() override;
    [[nodiscard]] bool splitSpecificAndFlag() const noexcept override {
        return true;
    }
    [[nodiscard]] bool usesMask() const noexcept override { return true; }
    [[nodiscard]] bool usesSmoothing() const noexcept override { return true; }
    [[nodiscard]] bool supportsOriginalSetting() const noexcept override {
        return true;
    }
    bool apply() override;

private:
    const Skin* summ_skin_{};
    const Skin* force_skin_{};
    Skin::Ptr summ_skin_owner_{};
    Skin::Ptr force_skin_owner_{};
    mutable Skin::Ptr resolved_summ_skin_owner_{};
    ImageCellPtr image_back_;
    std::unique_ptr<imageselect::ImageSelectBase> image_select_;
    std::shared_ptr<lib::TextEdit> image_path_;
    std::shared_ptr<lib::Canvas> preview_;
};

/// フォント・スタイル・枠・更新の設定を持つ、テキストセルの編集画面。
class TextCellDialog final : public BgImageDialog {
public:
    TextCellDialog(BgImageDialogContext context, Props& prop,
                   const Summary* summ, const Skin* force_skin,
                   TextCellPtr back, bool create,
                   std::shared_ptr<lib::Window> parent = {},
                   UseCounterPtr counter = {});
    TextCellDialog(areaviewutils::Commons common, Props& prop,
                   std::shared_ptr<lib::Window> parent, const Summary* summ,
                   const Skin* force_skin, UseCounterPtr counter,
                   TextCellPtr back, bool create);

    [[nodiscard]] TextCellPtr textBack() const noexcept;
    [[nodiscard]] const std::shared_ptr<lib::TextEdit>& textControl() const
        noexcept { return text_; }
    [[nodiscard]] const std::shared_ptr<lib::ComboBox>& fontControl() const
        noexcept { return font_name_; }
    [[nodiscard]] const ColorPicker& colorPicker() const noexcept { return color_; }
    [[nodiscard]] const ColorPicker& borderingColorPicker() const noexcept {
        return bordering_color_;
    }
    [[nodiscard]] lib::String previewText(std::string_view base) const;
    [[nodiscard]] const lib::String& previewDescription() const noexcept {
        return preview_description_;
    }

protected:
    void setup(Container& area) override;
    void setupSpecific(Container& area) override;
    void initializeSpecific() override;
    void refreshWarning() override;
    void refDataVersion();
    bool apply() override;

private:
    const Skin* summ_skin_{};
    const Skin* force_skin_{};
    Skin::Ptr summ_skin_owner_{};
    Skin::Ptr force_skin_owner_{};
    TextCellPtr text_back_;
    std::shared_ptr<lib::Canvas> preview_;
    std::shared_ptr<lib::TextEdit> text_;
    dmenu::TextMenuModifyPtr text_menu_;
    std::shared_ptr<lib::ComboBox> font_name_;
    std::shared_ptr<lib::NumberEdit> size_;
    ColorPicker color_;
    std::shared_ptr<lib::CheckBox> bold_;
    std::shared_ptr<lib::CheckBox> italic_;
    std::shared_ptr<lib::CheckBox> underline_;
    std::shared_ptr<lib::CheckBox> strike_;
    std::shared_ptr<lib::CheckBox> vertical_;
    std::shared_ptr<lib::CheckBox> antialias_;
    std::shared_ptr<lib::ComboBox> bordering_type_;
    std::vector<types::BorderingType> bordering_types_;
    ColorPicker bordering_color_;
    std::shared_ptr<lib::NumberEdit> bordering_width_;
    std::shared_ptr<lib::ComboBox> update_type_;
    std::vector<types::UpdateType> update_types_;
    std::shared_ptr<::cwxeditor::cwx::editor::gui::dwt::messageutils::PreviewValues>
        preview_values_;
    lib::String preview_description_;
    std::uint64_t preview_revision_{};
    std::shared_ptr<cellrender::TextRasterCache> text_cache_{std::make_shared<cellrender::TextRasterCache>()};
    // 最後のメンバーとして先に破棄され、部品・モデルより前に保持中の描画クロージャーを無効化する。
    std::shared_ptr<void> preview_lifetime_{std::make_shared<int>(0)};

    void updatePreview() override;
};

/// 合成方法・グラデーション・二つの色を設定する、色セルの編集画面。
class ColorCellDialog final : public BgImageDialog {
public:
    ColorCellDialog(BgImageDialogContext context, Props& prop,
                    const Summary* summ, ColorCellPtr back, bool create,
                    std::shared_ptr<lib::Window> parent = {},
                    UseCounterPtr counter = {});
    ColorCellDialog(areaviewutils::Commons common, Props& prop,
                    std::shared_ptr<lib::Window> parent, const Summary* summ,
                    UseCounterPtr counter, ColorCellPtr back, bool create);

    [[nodiscard]] ColorCellPtr colorBack() const noexcept;
    [[nodiscard]] types::BlendMode blendMode() const noexcept { return blend_mode_; }
    [[nodiscard]] types::GradientDir gradientDir() const noexcept;
    [[nodiscard]] ColorPicker& color1() noexcept { return color1_; }
    [[nodiscard]] ColorPicker& color2() noexcept { return color2_; }
    [[nodiscard]] const std::shared_ptr<lib::Canvas>& previewControl() const noexcept {
        return preview_;
    }
    void copyColor1ToColor2();
    void copyColor2ToColor1();
    void exchangeColors();
    void refMenu(types::MenuID id);

protected:
    void setup(Container& area) override;
    void setupSpecific(Container& area) override;
    void initializeSpecific() override;
    void refreshWarning() override;
    bool apply() override;

private:
    ColorCellPtr color_back_;
    std::shared_ptr<lib::Canvas> preview_;
    std::map<types::BlendMode, std::shared_ptr<lib::RadioButton>> blend_controls_;
    types::BlendMode blend_mode_{types::BlendMode::Normal};
    std::shared_ptr<lib::ComboBox> gradient_dir_;
    std::shared_ptr<lib::Panel> color_tools_;
    std::vector<types::GradientDir> gradient_dirs_;
    ColorPicker color1_;
    ColorPicker color2_;
    lib::String preview_description_;
    std::shared_ptr<void> key_filter_lifetime_{std::make_shared<int>(0)};

    void updatePreview();
    void selectBlend(types::BlendMode mode);
};

/// プレイヤーキャラクターのセルの編集画面。
class PCCellDialog final : public BgImageDialog {
public:
    PCCellDialog(BgImageDialogContext context, Props& prop,
                 const Summary* summ, const Skin* skin, PCCellPtr back,
                 bool create, std::shared_ptr<lib::Window> parent = {},
                 UseCounterPtr counter = {});
    PCCellDialog(areaviewutils::Commons common, Props& prop,
                 std::shared_ptr<lib::Window> parent, const Summary* summ,
                 const Skin* skin, UseCounterPtr counter, PCCellPtr back,
                 bool create);

    [[nodiscard]] PCCellPtr pcBack() const noexcept;
    [[nodiscard]] const std::shared_ptr<lib::ComboBox>& pcNumberControl() const
        noexcept { return pc_number_; }
    [[nodiscard]] const std::shared_ptr<lib::CheckBox>& expandControl() const
        noexcept { return expand_; }

protected:
    void setupSpecific(Container& area) override;
    void initializeSpecific() override;
    void refreshWarning() override;
    void selectEasySetting() override;
    void updatePreview() override;
    [[nodiscard]] bool usesSmoothing() const noexcept override { return true; }
    [[nodiscard]] bool splitSpecificAndFlag() const noexcept override {
        return true;
    }
    [[nodiscard]] bool supportsOriginalSetting() const noexcept override {
        return true;
    }
    bool apply() override;

private:
    const Skin* summ_skin_{};
    PCCellPtr pc_back_;
    std::shared_ptr<lib::Canvas> preview_;
    std::shared_ptr<lib::ComboBox> pc_number_;
    std::shared_ptr<lib::CheckBox> expand_;
};

}  // namespace cwxeditor::cwx::editor::gui::dwt::bgimagedialog
