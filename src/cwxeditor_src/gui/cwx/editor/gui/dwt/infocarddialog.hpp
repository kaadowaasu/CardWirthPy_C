#pragma once

// cwx/editor/gui/dwt/infocarddialog.d
// の移植可能なC++20実装。 元の
// SWT画面とEditor専用部品の
// 状態・遷移をlib::Dialog
// で保ち、 名前・解説・画像の操作を
// 共通部品で表す。OSのウィンドウ
// や生成済み互換型は必要としない。

#include "cwxeditor_src/core/cwx/card.hpp"
#include "cwxeditor_src/core/cwx/props.hpp"
#include "cwxeditor_src/core/cwx/skin.hpp"
#include "cwxeditor_src/core/cwx/summary.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/core/cwx/warning.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dprops.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dmenu.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/editdialogsession.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/imageselect.hpp"
#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/graphics.hpp"
#include "cwxeditor_src/lib/layout.hpp"
#include "cwxeditor_src/lib/portable.hpp"
#include "cwxeditor_src/lib/services.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::commons {
class Commons;
}

namespace cwxeditor::cwx::editor::gui::dwt::infocarddialog {

using CardImage = ::cwxeditor::cwx::card::CardImage;
using InfoCard = ::cwxeditor::cwx::card::InfoCard;
using CardImagePosition = ::cwxeditor::cwx::types::CardImagePosition;
using CardImageType = ::cwxeditor::cwx::types::CardImageType;
using Summary = ::cwxeditor::cwx::summary::Summary;
using CProps = ::cwxeditor::cwx::props::CProps;
using Skin = ::cwxeditor::cwx::skin::Skin;
using WarningList = ::cwxeditor::cwx::warning::WarningList;

/// カード画像が未選択の場合の添字。
inline constexpr std::size_t no_image =
    (std::numeric_limits<std::size_t>::max)();

/// ImageSelect!(MtType.CARD)の編集状態に対
/// 応する共通型。 CardImageを所有参照で保持し、ダイアログ
/// の適用でCard.paths()の所有規則を失わないようにする。
struct ImageSelection {
    lib::Array<lib::Owner<CardImage>> images;
    std::size_t selected_index{no_image};
    bool cancel{};
    bool dirty{};
    lib::Size preview_size{};
    lib::Insets preview_insets{};
    CardImagePosition default_position{CardImagePosition::TopLeft};
};

/// 素材パス操作の戻り値。ホストがネイティブのファイル・画像
/// 選択を実装しても、 ダイアログ自体は共通のままにできる。
struct MaterialPathResult {
    lib::Array<lib::Owner<CardImage>> images;
    WarningList warnings;
    bool cancel{};
};

/// 選択したCardImageを一つlib::ImageViewへ表示するためのデータ。
struct ImagePreview {
    lib::String path;
    CardImageType type{CardImageType::File};
    CardImagePosition position{CardImagePosition::Default};
    lib::ResourceId resource{};
    lib::Rect bounds{};
};

/// ヘッドレスとネイティブGUIのホストへ公開する状態。
struct DialogModel {
    lib::String name;
    lib::String description;
    ImageSelection image;
    WarningList warnings;
    bool read_only{};
    bool modified{};
};

using SkinResolver =
    lib::Callback<lib::Owner<Skin>(const CProps&, const Summary*)>;
using ExportedImageName =
    lib::Callback<lib::String(const CProps&, lib::StringView,
                              lib::StringView, lib::StringView)>;
using MaterialPathProvider =
    lib::Callback<MaterialPathResult(bool, const ImageSelection&)>;
using ImageWarningProvider = lib::Callback<
    WarningList(const CProps&, const Summary*, const ImageSelection&)>;
using ImageResourceProvider =
    lib::Callback<lib::ResourceId(lib::StringView)>;

/// 任意の統合用コールバック。DWTのCommons/ImageSelect
/// サービスに代わり、 ダイアログを特定のGUI実装へ依存させない。
struct Options {
    lib::ResourceId icon{};
    lib::Size window_size{};
    std::shared_ptr<lib::Window> owner;
    lib::Owner<Skin> skin;
    SkinResolver skin_resolver;
    ExportedImageName exported_image_name;
    MaterialPathProvider material_path;
    ImageWarningProvider image_warnings;
    ImageResourceProvider image_resource;
    lib::Callback<float(float)> scale;
    lib::GraphicsBackend* graphics{};
    ::cwxeditor::cwx::editor::gui::dwt::dprops::Props* editor_props{};
    // 任意のDWT相当の通知元。指定時はDのCommons
    // と同じ、型別の変更通知と表示通知を購読する。
    ::cwxeditor::cwx::editor::gui::dwt::commons::Commons* editor_commons{};
    lib::Services services{};
    bool creationMode{};
};

/// 情報カード編集ダイアログの共通実装。 Summary&
/// 版は借用し、Owner<Summary>
/// 版は画面の寿命中にシナリオを保持する
/// 。 既存カードも生ポインター・共有所
/// 有のどちらでも受け取り、新規カードは
/// cardOwner()が所有する。
class InfoCardDialog : public lib::Dialog,
                       public editdialogsession::EditDialogSession {
public:
    using DialogResult = lib::DialogResult;
    using Container = lib::Container;

    InfoCardDialog(const CProps& prop, lib::Owner<Summary> summary,
                   lib::Owner<InfoCard> card = {}, bool read_only = false,
                   Options options = {});

    InfoCardDialog(const CProps& prop, Summary& summary,
                   InfoCard* card = nullptr, bool read_only = false,
                   Options options = {});

    InfoCardDialog(const InfoCardDialog&) = delete;
    InfoCardDialog& operator=(const InfoCardDialog&) = delete;
    InfoCardDialog(InfoCardDialog&&) = delete;
    InfoCardDialog& operator=(InfoCardDialog&&) = delete;
    ~InfoCardDialog() override;

    /// 現在のカード。新規作成中はnullptr。
    [[nodiscard]] InfoCard* card() noexcept { return card_; }
    [[nodiscard]] const InfoCard* card() const noexcept { return card_; }
    [[nodiscard]] const lib::Owner<InfoCard>& cardOwner() const noexcept {
        return card_owner_;
    }
    [[nodiscard]] const Summary* summary() const noexcept { return summary_; }
    [[nodiscard]] const CProps& props() const noexcept { return *prop_; }

    [[nodiscard]] bool readOnly() const noexcept { return read_only_; }
    [[nodiscard]] lib::ResourceId icon() const noexcept { return icon_; }
    [[nodiscard]] bool initialized() const noexcept { return initialized_; }
    [[nodiscard]] bool closed() const noexcept { return closed_; }
    [[nodiscard]] bool forceApplying() const noexcept { return force_applying_; }
    void forceApplying(bool value) noexcept { force_applying_ = value; }

    [[nodiscard]] const DialogModel& model() const noexcept { return model_; }
    [[nodiscard]] const lib::String& name() const noexcept {
        return model_.name;
    }
    [[nodiscard]] const lib::String& description() const noexcept {
        return model_.description;
    }
    void setName(lib::String value);
    void setDescription(lib::String value);

    [[nodiscard]] const ImageSelection& imageSelection() const noexcept {
        return model_.image;
    }
    void setImageSelection(ImageSelection value);
    void setImages(lib::Array<lib::Owner<CardImage>> value);
    [[nodiscard]] const lib::Array<ImagePreview>& imagePreviews() const noexcept {
        return image_previews_;
    }
    [[nodiscard]] const ImagePreview* selectedImage() const noexcept;
    bool selectImage(std::size_t index);
    void clearImageSelection();
    [[nodiscard]] lib::String exportedImageName() const;

    [[nodiscard]] const WarningList& warnings() const noexcept {
        return model_.warnings;
    }
    [[nodiscard]] const lib::Owner<Skin>& summarySkinOwner() const noexcept {
        return summary_skin_;
    }
    [[nodiscard]] const Skin* summSkin() const noexcept {
        return effectiveSummarySkin().get();
    }
    [[nodiscard]] const lib::Owner<lib::Panel>& content() const noexcept {
        return area_;
    }
    [[nodiscard]] const lib::Owner<lib::TextEdit>& nameEditor() const noexcept {
        return name_edit_;
    }
    [[nodiscard]] const lib::Owner<lib::TextEdit>& descriptionEditor() const noexcept {
        return description_edit_;
    }
    [[nodiscard]] const lib::Owner<lib::ImageView>& imageView() const noexcept {
        return image_view_;
    }
    [[nodiscard]] const lib::Owner<lib::Button>& okButton() const noexcept {
        return ok_button_;
    }
    [[nodiscard]] const lib::Owner<lib::Button>& cancelButton() const noexcept {
        return cancel_button_;
    }
    [[nodiscard]] const lib::Owner<lib::Button>& applyButton() const noexcept {
        return apply_button_;
    }

    /// このダイアログ内、または外部の領域へ共通の部品ツリーを作る。
    void setup();
    void setup(Container& area);

    /// DWT相当の推奨サイズと子の矩形を再計算する。
    [[nodiscard]] lib::Size preferredSize() const;
    void layoutModel(lib::Rect client_area);

    /// DのopenCWXPathは空パスの検査以外は何もしない。 shellActivate
    /// もソース上の動作互換のため受け付ける。
    [[nodiscard]] bool openCWXPath(lib::StringView path, bool shellActivate);

    /// ダイアログに表示するShift-JISと画像の警告を更新する。
    void refreshWarning();
    void delCard(const InfoCard* card);
    void delCard(const InfoCard& card) { delCard(&card); }
    void delCard(const lib::Owner<InfoCard>& card) { delCard(card.get()); }
    void refScenario(const Summary* summary);
    void refScenario(const Summary& summary) { refScenario(&summary); }
    void refSkin();
    void refImageScale();
    void refDataVersion();
    void updateTitle(const InfoCard* card);
    void updateTitle(const InfoCard& card) { updateTitle(&card); }
    void refCard(const InfoCard* card);
    void refCard(const InfoCard& card) { refCard(&card); }
    void refCard(const lib::Owner<InfoCard>& card) { refCard(card.get()); }

    /// Dのapply()と同じく、既存・新規のInfoCardへ
    /// モデルを確定する。 素材パスの処理がcancel=true
    /// を返した場合は、モデルを変えず画面を開いたままにする。
    [[nodiscard]] bool apply();
    [[nodiscard]] lib::Window* editDialogWindow() const noexcept override {
        return const_cast<InfoCardDialog*>(this);
    }
    [[nodiscard]] bool editDialogPending() const noexcept override {
        return apply_button_ && apply_button_->enabled();
    }
    [[nodiscard]] bool applyEditDialog() override {
        force_applying_ = true;
        struct Reset final {
            bool& value;
            ~Reset() { value = false; }
        } reset{force_applying_};
        return apply();
    }
    [[nodiscard]] bool accept();
    [[nodiscard]] bool open();
    [[nodiscard]] bool cancel();
    void close(DialogResult result);
    void forceCancel();

    /// Commonsのデリゲート通知に対応するホストのコールバック。
    lib::Callback<void(const WarningList&)> on_warning_changed;
    lib::Callback<void(const InfoCard&)> on_title_changed;
    // 素材処理の成功後、プロパティを書き込む直前の実際のカー
    // ドから履歴を取る。 中止された適用では記録を作らない。
    lib::Action on_applying;
    lib::Callback<void(const InfoCard&)> on_applied;
    lib::Action on_cancelled;
    lib::Action on_closed;

private:
    std::shared_ptr<int> applying_lifetime_{std::make_shared<int>(0)};
    const CProps* prop_{};
    lib::Owner<Summary> summary_owner_;
    const Summary* summary_{};
    lib::Owner<InfoCard> card_owner_;
    InfoCard* card_{};
    Options options_{};
    lib::ResourceId icon_{};
    lib::Owner<Skin> summary_skin_;
    bool summary_skin_pinned_{};
    DialogModel model_{};
    WarningList image_warnings_;
    lib::Array<ImagePreview> image_previews_;

    lib::Owner<lib::Panel> area_;
    lib::Owner<lib::GroupBox> name_group_;
    lib::Owner<lib::TextEdit> name_edit_;
    lib::Owner<lib::Panel> image_panel_;
    lib::Owner<lib::ListView> image_list_;
    lib::Owner<lib::ImageView> image_view_;
    lib::Owner<lib::Panel> image_buttons_;
    lib::Owner<lib::Button> image_up_button_;
    lib::Owner<lib::Button> image_down_button_;
    lib::Owner<lib::Button> image_remove_button_;
    std::unique_ptr<
        ::cwxeditor::cwx::editor::gui::dwt::imageselect::ImageSelectBase>
        image_select_;
    lib::Owner<lib::GroupBox> description_group_;
    lib::Owner<lib::TextEdit> description_edit_;
    std::vector<dmenu::TextMenuModifyPtr> text_menus_;
    lib::Owner<lib::Panel> buttons_panel_;
    lib::Owner<lib::Button> ok_button_;
    lib::Owner<lib::Button> cancel_button_;
    lib::Owner<lib::Button> apply_button_;

    bool read_only_{};
    bool creation_mode_{};
    bool creation_initialized_{};
    bool force_applying_{};
    bool initialized_{};
    bool closed_{};
    bool updating_{};
    std::uint64_t ref_info_connection_{};
    std::uint64_t del_info_connection_{};
    std::uint64_t ref_scenario_connection_{};
    std::uint64_t ref_skin_connection_{};
    std::uint64_t ref_image_scale_connection_{};
    std::uint64_t ref_data_version_connection_{};
    std::uint64_t ref_target_version_connection_{};

    [[nodiscard]] lib::Owner<Skin> effectiveSummarySkin() const noexcept;
    static lib::Owner<InfoCard> nonOwning(InfoCard* card);
    void connectCommonsObservers();
    void disconnectCommonsObservers() noexcept;
    void initialize();
    void syncWidgets();
    void setImagesImpl(lib::Array<lib::Owner<CardImage>> value,
                       bool mark_modified);
    void rebuildImagePreviews();
    void refreshImageControls();
    void updateImageView();
    void markModified();
    void setDescriptionFont();
    [[nodiscard]] float scale(float value) const;
    [[nodiscard]] bool legacyForLayout() const noexcept;
    [[nodiscard]] static lib::FontSpec makeFont(const props::CFont& value);
    [[nodiscard]] static lib::String normalizeReturnCode(lib::StringView value);
};

}  // namespace cwxeditor::cwx::editor::gui::dwt::infocarddialog
