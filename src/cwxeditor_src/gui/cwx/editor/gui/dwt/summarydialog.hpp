#pragma once

#include "cwxeditor_src/core/cwx/card.hpp"
#include "cwxeditor_src/core/cwx/area.hpp"
#include "cwxeditor_src/core/cwx/skin.hpp"
#include "cwxeditor_src/core/cwx/summary.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/core/cwx/versioninfo.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/absdialog.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/chooser.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dprops.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dskin.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/imageselect.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/incsearch.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/splitpane.hpp"
#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/dialogs.hpp"
#include "cwxeditor_src/lib/services.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::commons { class Commons; }

namespace cwxeditor::cwx::editor::gui::dwt::summarydialog {

namespace lib = ::cwxeditor::lib;
namespace area = ::cwxeditor::cwx::area;
namespace card = ::cwxeditor::cwx::card;
namespace chooser = ::cwxeditor::cwx::editor::gui::dwt::chooser;
namespace dprops = ::cwxeditor::cwx::editor::gui::dwt::dprops;
namespace dskin = ::cwxeditor::cwx::editor::gui::dwt::dskin;
namespace absdialog = ::cwxeditor::cwx::editor::gui::dwt::absdialog;
namespace imageselect = ::cwxeditor::cwx::editor::gui::dwt::imageselect;
namespace incsearch = ::cwxeditor::cwx::editor::gui::dwt::incsearch;
namespace summary = ::cwxeditor::cwx::summary;
namespace skin = ::cwxeditor::cwx::skin;
namespace structs = ::cwxeditor::cwx::structs;
namespace types = ::cwxeditor::cwx::types;
namespace versioninfo = ::cwxeditor::cwx::versioninfo;

using String = lib::String;
using StringView = lib::StringView;
using Props = dprops::Props;
using Summary = summary::Summary;
using Skin = skin::Skin;
using SkinPtr = skin::Skin::Ptr;
using CardImage = card::CardImage;
using CardImagePtr = lib::Owner<CardImage>;
using ClassicEngine = structs::ClassicEngine;
using Services = lib::Services;
using Window = std::shared_ptr<lib::Window>;
using Parent = std::shared_ptr<lib::Container>;
using Action = lib::Action;
using ImageDataWithScale = dskin::ImageDataWithScale;

/// XMLスキン選択欄に表示するスキン項目。
struct SkinInfo {
    String type;
    String name;
    String path;
};

/// シナリオ概要の共通プレビュー。 基本の
/// 画像はスキンサービスから受け取りImageView
/// で表示する。 タイトル・解説・対象
/// レベル・ページ情報は、Dと同じ内
/// 容を共通部品のラベルで保持する。
class SummaryPreview final {
public:
    using TextGetter = std::function<String()>;
    using ImagesGetter = std::function<std::vector<CardImagePtr>()>;
    using SkinGetter = std::function<SkinPtr()>;
    using LevelGetter = std::function<int()>;

    SummaryPreview(Props& prop, Summary& summary, Parent parent = {},
                   std::function<bool()> loadScaledImage = {},
                   TextGetter dataVersion = {}, Services services = {});
    SummaryPreview(Props& prop, Summary& summary, lib::Container& parent,
                   std::function<bool()> loadScaledImage = {},
                   TextGetter dataVersion = {}, Services services = {});
    ~SummaryPreview();

    SummaryPreview(const SummaryPreview&) = delete;
    SummaryPreview& operator=(const SummaryPreview&) = delete;

    [[nodiscard]] const Parent& widget() const noexcept { return root_; }
    [[nodiscard]] const std::shared_ptr<lib::ImageView>& image() const noexcept {
        return image_;
    }
    [[nodiscard]] const std::shared_ptr<lib::Canvas>& canvas() const noexcept {
        return canvas_;
    }
    [[nodiscard]] const ImageDataWithScale& buffer() const noexcept { return buffer_; }
    [[nodiscard]] lib::ResourceId resource() const noexcept { return resource_; }
    [[nodiscard]] float displayWidth() const noexcept;

    void updateScale();
    void setImageSelect(TextGetter scenarioName, ImagesGetter imagePaths,
                        SkinGetter selectedSkin, TextGetter description,
                        LevelGetter levelMin, LevelGetter levelMax);
    void redrawImage();
    void invalidatePortraitCache() noexcept;
    void clearBuf();

private:
    Props* prop_{};
    Summary* summary_{};
    Parent root_{};
    Services services_{};
    std::shared_ptr<lib::ImageView> image_{};
    std::shared_ptr<lib::GroupBox> frame_{};
    std::shared_ptr<lib::Canvas> canvas_{};
    std::function<bool()> load_scaled_image_{};
    TextGetter data_version_{};
    TextGetter scenario_name_{};
    ImagesGetter image_paths_{};
    SkinGetter selected_skin_{};
    TextGetter description_text_{};
    LevelGetter level_min_{};
    LevelGetter level_max_{};
    ImageDataWithScale buffer_{};
    lib::ResourceId resource_{};
    bool owns_resource_{};
    const Skin* rendered_skin_{};
    std::uint32_t rendered_scale_{};
    bool background_cache_valid_{};

    struct CachedPortrait {
        lib::ImageData data;
        lib::ResourceId resource{};
        lib::Size logical_size{};
        std::uint32_t source_scale{1U};
        types::CardImagePosition position{types::CardImagePosition::Default};
        std::shared_ptr<lib::OwnedImageResource> upload;
    };
    std::vector<CachedPortrait> portrait_cache_;
    std::vector<CardImagePtr> portrait_paths_;
    const Skin* portrait_skin_{};
    std::uint32_t portrait_scale_{};
    String portrait_version_;
    bool portrait_cache_valid_{};

    void build();
    void releaseResource() noexcept;
    void ensurePortraitCache(const SkinPtr& selected);
    void updateLabels();
    void paint(lib::DrawingContext& context, lib::Rect bounds);
};

/// シナリオの概要情報とスキン選択を編集するダイアログ。
class SummaryDialog final : public absdialog::AbsDialog {
public:
    std::vector<Action> cancelEvent;
    // 素材の準備に成功した後、Summaryへ書く直前にホ
    // ストの通知を取得する。 中止された適用では呼ばない。
    Action on_applying;


    SummaryDialog(Props& prop, std::shared_ptr<lib::Window> parent, Summary* summary,
                  bool readOnly = false, Services services = {},
                  commons::Commons* editorCommon = nullptr);
    SummaryDialog(Props& prop, std::shared_ptr<lib::Window> parent, Summary& summary,
                  bool readOnly = false, Services services = {},
                  commons::Commons* editorCommon = nullptr)
        : SummaryDialog(prop, std::move(parent), &summary, readOnly,
                        std::move(services), editorCommon) {}

    SummaryDialog(const SummaryDialog&) = delete;
    SummaryDialog& operator=(const SummaryDialog&) = delete;
    SummaryDialog(SummaryDialog&&) = delete;
    SummaryDialog& operator=(SummaryDialog&&) = delete;
    ~SummaryDialog();

    void setup(lib::Container& area) override;
    void setup(const Parent& area);
    [[nodiscard]] bool open();
    [[nodiscard]] bool apply();
    void cancel();

    [[nodiscard]] Summary* summaryPtr() const noexcept { return summary_; }
    [[nodiscard]] Summary& summary() const noexcept { return *summary_; }
    [[nodiscard]] bool readOnly() const noexcept { return read_only_; }
    [[nodiscard]] const Parent& content() const noexcept { return content_; }

    [[nodiscard]] SkinPtr summSkin() const;
    [[nodiscard]] SkinPtr selectedSkin() const;
    [[nodiscard]] String dataVersion() const;

    void refreshWarning();
    void levMaxEnter(int value);
    void levMinEnter(int value);
    void constructImage(const Parent& area);
    void showImagePreview(bool visible, bool resizeWindow = true);
    void refImageScale();
    // 所有するEditorが、DのImageSelectと同じCommonsを渡す。
    void bindEditorCommons(commons::Commons* common);
    void refreshPreview();
    void clearBuf();
    void constructTab1(lib::Container& area);
    void constructTab2(lib::Container& area);
    void constructTab3(lib::Container& area);
    void updateDataVersion();
    void refreshTypes(SkinPtr skin = {}, bool incrementalSearch = false);

    [[nodiscard]] std::vector<String> rCoupons() const;
    [[nodiscard]] const std::vector<String>& warnings() const noexcept {
        return warnings_;
    }
    [[nodiscard]] const std::vector<SkinInfo>& skinInfo() const noexcept {
        return skin_info_;
    }
    [[nodiscard]] const std::vector<ClassicEngine>& classicEngines() const noexcept {
        return classic_engines_;
    }
    [[nodiscard]] const std::vector<CardImagePtr>& imagePaths() const noexcept {
        return image_paths_;
    }
    void imagePaths(std::vector<CardImagePtr> value);
    [[nodiscard]] bool addImage(String path,
                                 types::CardImagePosition position =
                                     types::CardImagePosition::Default);
    [[nodiscard]] bool removeImage(std::size_t index);
    [[nodiscard]] bool moveImage(std::size_t from, std::size_t to);

    [[nodiscard]] const std::shared_ptr<lib::TextEdit>& scenarioNameControl() const noexcept {
        return scenario_name_;
    }
    [[nodiscard]] const std::shared_ptr<lib::TextEdit>& couponsControl() const noexcept {
        return coupons_;
    }
    [[nodiscard]] const std::shared_ptr<lib::TextEdit>& authorControl() const noexcept {
        return author_;
    }
    [[nodiscard]] const std::shared_ptr<lib::TextEdit>& descriptionControl() const noexcept {
        return description_;
    }
    [[nodiscard]] const std::shared_ptr<lib::NumberEdit>& levelMinControl() const noexcept {
        return level_min_;
    }
    [[nodiscard]] const std::shared_ptr<lib::NumberEdit>& levelMaxControl() const noexcept {
        return level_max_;
    }
    [[nodiscard]] const std::shared_ptr<lib::ComboBox>& skinControl() const noexcept {
        return type_;
    }
    [[nodiscard]] const std::shared_ptr<lib::ComboBox>& dataVersionControl() const noexcept {
        return data_version_control_;
    }
    [[nodiscard]] const std::shared_ptr<lib::RadioButton>& xmlSkinControl() const noexcept {
        return type_skin_;
    }
    [[nodiscard]] const std::shared_ptr<lib::RadioButton>& classicSkinControl() const noexcept {
        return type_classic_;
    }
    [[nodiscard]] const std::shared_ptr<lib::CheckBox>& loadScaledImageControl() const noexcept {
        return load_scaled_image_;
    }
    [[nodiscard]] const std::shared_ptr<lib::CheckBox>& useKeyInActionControl() const noexcept {
        return use_key_in_action_;
    }
    [[nodiscard]] const std::shared_ptr<lib::CheckBox>& noConsumeUnmatchedMenuCardControl() const noexcept {
        return no_consume_unmatched_menu_card_;
    }
    [[nodiscard]] auto numericKeycodesControl() const { return numeric_keycodes_; }
    [[nodiscard]] const std::shared_ptr<SummaryPreview>& preview() const noexcept {
        return summary_image_;
    }

protected:
    bool close(bool ok, bool& cancel) override;

private:
    std::shared_ptr<int> applying_lifetime_{std::make_shared<int>(0)};
    std::uint64_t applying_generation_{};
    commons::Commons* editor_commons_{};
    std::weak_ptr<const int> editor_commons_lifetime_{};
    std::uint64_t sort_refresh_connection_{};
    std::uint64_t classic_refresh_connection_{};
    std::uint64_t skin_refresh_connection_{};
    std::uint64_t image_scale_refresh_connection_{};
    std::uint64_t data_version_refresh_connection_{};
    std::uint64_t target_version_refresh_connection_{};
    Props* prop_{};
    Summary* summary_{};
    Services services_{};
    Parent content_{};
    bool read_only_{};
    bool initialized_{};
    bool modified_{};
    bool load_scaled_image_value_{};
    std::uint32_t last_image_scale_{};

    std::shared_ptr<lib::TextEdit> scenario_name_{};
    std::shared_ptr<lib::TextEdit> author_{};
    std::shared_ptr<lib::TextEdit> description_{};
    std::shared_ptr<lib::NumberEdit> level_min_{};
    std::shared_ptr<lib::NumberEdit> level_max_{};
    std::shared_ptr<lib::NumberEdit> coupon_number_{};
    std::shared_ptr<lib::TextEdit> coupons_{};
    std::shared_ptr<lib::TreeView> start_area_tree_{};
    std::uint64_t start_area_value_{};
    std::unique_ptr<chooser::AreaChooser<area::Area, true>> start_area_chooser_{};
    std::shared_ptr<lib::RadioButton> type_skin_{};
    std::shared_ptr<lib::RadioButton> type_classic_{};
    std::shared_ptr<lib::ComboBox> type_{};
    std::unique_ptr<incsearch::IncSearch> skin_inc_search_{};
    std::shared_ptr<lib::Menu> skin_popup_{};
    std::shared_ptr<lib::ComboBox> data_version_control_{};
    std::shared_ptr<lib::CheckBox> load_scaled_image_{};
    std::shared_ptr<lib::CheckBox> use_key_in_action_{};
    std::shared_ptr<lib::CheckBox> no_consume_unmatched_menu_card_{};
    std::shared_ptr<lib::CheckBox> numeric_keycodes_{};
    std::shared_ptr<lib::ToggleButton> preview_toggle_{};
    std::unique_ptr<imageselect::ImageSelectBase> image_select_{};
    Parent image_area_{};
    std::shared_ptr<SummaryPreview> summary_image_{};
    float preview_width_{};

    std::vector<CardImagePtr> image_paths_{};
    std::vector<SkinInfo> skin_info_{};
    bool has_legacy_skin_{};
    std::vector<ClassicEngine> classic_engines_{};
    SkinPtr selected_skin_{};
    SkinPtr summary_skin_{};
    std::vector<String> warnings_{};

    void buildControls();
    void buildTextField(lib::Container& area, String title,
                        const std::shared_ptr<lib::TextEdit>& control);
    void buildNumberField(lib::Container& area, String title,
                          const std::shared_ptr<lib::NumberEdit>& control);
    void setControlEnabled(bool enabled);
    void updateSkinMode();
    void refreshImageControls(std::size_t selection = 0U);
    void refSkin();
    void raiseCancelled();
};

}  // namespace cwxeditor::cwx::editor::gui::dwt::summarydialog
