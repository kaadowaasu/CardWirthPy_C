#pragma once

// 共通の部品とモデルを使う、C++20のキャストカード編集画面。

#include "cwxeditor_src/core/cwx/card.hpp"
#include "cwxeditor_src/core/cwx/coupon.hpp"
#include "cwxeditor_src/core/cwx/features.hpp"
#include "cwxeditor_src/core/cwx/race.hpp"
#include "cwxeditor_src/core/cwx/skin.hpp"
#include "cwxeditor_src/core/cwx/summary.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/core/cwx/usecounter.hpp"
#include "cwxeditor_src/core/cwx/warning.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/absdialog.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/areaviewutils.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/bgimagedialog.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/couponview.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/customtext.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dprops.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dmenu.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/radarspinner.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/scales.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/spcarddialog.hpp"
#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/layout.hpp"
#include "cwxeditor_src/lib/services.hpp"

#include <cstddef>
#include <cstdint>
#include <array>
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::castcarddialog {

namespace absdialog = ::cwxeditor::cwx::editor::gui::dwt::absdialog;
namespace areaviewutils = ::cwxeditor::cwx::editor::gui::dwt::areaviewutils;
namespace bgimagedialog = ::cwxeditor::cwx::editor::gui::dwt::bgimagedialog;
namespace card = ::cwxeditor::cwx::card;
namespace coupon = ::cwxeditor::cwx::coupon;
namespace dprops = ::cwxeditor::cwx::editor::gui::dwt::dprops;
namespace couponview = ::cwxeditor::cwx::editor::gui::dwt::couponview;
namespace customtext = ::cwxeditor::cwx::editor::gui::dwt::customtext;
namespace editor_commons = ::cwxeditor::cwx::editor::gui::dwt::commons;
namespace customtable = ::cwxeditor::cwx::editor::gui::dwt::customtable;
namespace dutils = ::cwxeditor::cwx::editor::gui::dwt::dutils;
namespace features = ::cwxeditor::cwx::features;
namespace lib = ::cwxeditor::lib;
namespace props = ::cwxeditor::cwx::props;
namespace radarspinner = ::cwxeditor::cwx::editor::gui::dwt::radarspinner;
namespace race = ::cwxeditor::cwx::race;
namespace scales = ::cwxeditor::cwx::editor::gui::dwt::scales;
namespace skin = ::cwxeditor::cwx::skin;
namespace spcarddialog = ::cwxeditor::cwx::editor::gui::dwt::spcarddialog;
namespace summary = ::cwxeditor::cwx::summary;
namespace types = ::cwxeditor::cwx::types;
namespace warning = ::cwxeditor::cwx::warning;

using Props = dprops::Props;
using Commons = areaviewutils::Commons;
using Summary = summary::Summary;
using Skin = skin::Skin;
using CastCard = card::CastCard;
using CastCardPtr = lib::Owner<CastCard>;
using Coupon = coupon::Coupon;
using CouponPtr = lib::Owner<Coupon>;
using CouponArray = lib::Array<CouponPtr>;
using CardImage = card::CardImage;
using CardImageArray = lib::Array<lib::Owner<CardImage>>;
using UseCounter = ::cwxeditor::cwx::usecounter::UseCounter;
using UseCounterPtr = ::cwxeditor::cwx::usecounter::CounterOwner;
using DSize = absdialog::DSize;

/// ホストのコールバックは任意とし、ヘッドレスで生成しても全機能を使えるようにする。
struct CastCardDialogContext {
    Commons common;
    lib::Services services{};
    editor_commons::Commons* editor_commons{};
    std::function<void()> refresh_toolbar;
    std::function<void()> refresh_coupons;
    std::function<void()> refresh_warnings;
    std::function<void()> refresh_skin;
    std::function<void()> refresh_data_version;
    std::function<lib::Owner<Skin>(const Summary*)> resolve_source_skin;
    std::function<void(CastCard*)> changed_card;
    std::function<bool(std::string_view, bool)> open_cwx_path;
    std::function<bgimagedialog::MaterialResult(std::string_view)>
        select_material;
    std::function<CardImageArray(std::string_view)> select_images;
    std::function<lib::ResourceId(types::ActionCardType)> action_card_image;
    std::function<std::vector<lib::String>(std::string_view)> text_warnings;
    std::function<std::vector<lib::String>(const CardImageArray&)>
        image_warnings;
    std::function<std::vector<lib::String>(const CouponArray&)>
        coupon_warnings;
    std::function<std::vector<lib::String>(std::span<const types::Status>)>
        status_warnings;

    CastCardDialogContext() = default;
    explicit CastCardDialogContext(Props& prop)
        : common(prop), services(prop.images().services()) {}
    explicit CastCardDialogContext(Commons value)
        : common(std::move(value)), services(common.services) {}
};

/// 履歴ページで使う、ツールキットに依存しないクーポン一覧。
class CouponEditor final {
public:
    std::vector<lib::Action> modEvent;

    CouponEditor() = default;
    CouponEditor(lib::Services services, std::shared_ptr<lib::Container> parent,
                 bool read_only = false);
    ~CouponEditor();
    CouponEditor(CouponEditor&& value) noexcept;
    CouponEditor& operator=(CouponEditor&& value) noexcept;
    CouponEditor(const CouponEditor&) = delete;
    CouponEditor& operator=(const CouponEditor&) = delete;

    [[nodiscard]] const std::shared_ptr<lib::Panel>& widget() const noexcept {
        return panel_;
    }
    [[nodiscard]] const CouponArray& coupons() const noexcept { return coupons_; }
    void coupons(CouponArray value);
    void addCoupon(CouponPtr value);
    void removeCoupon(std::size_t index);
    [[nodiscard]] std::vector<lib::String> warnings(
        const Props* prop, const Summary* summ,
        const std::function<std::vector<lib::String>(const CouponArray&)>&
            callback = {}) const;
    void readOnly(bool value);

private:
    // 共通部品はラッパーの破棄後や、一時オブジェクトからの移動後も
    // 残る場合がある。 一つの接続情報で現在の所有者を追跡する。
    struct Binding { CouponEditor* owner{}; std::uint64_t revision{}; };
    std::shared_ptr<Binding> binding_{std::make_shared<Binding>(Binding{this})};
    lib::Services services_{};
    std::shared_ptr<lib::Panel> panel_;
    std::shared_ptr<lib::ListView> list_;
    std::shared_ptr<lib::TextEdit> name_;
    std::shared_ptr<lib::NumberEdit> value_;
    std::shared_ptr<lib::Button> add_;
    std::shared_ptr<lib::Button> remove_;
    CouponArray coupons_;
    bool read_only_{};

    void rebuild();
    void notifyChanged();
};

/// キャストカード設定画面の共通実装。
class CastCardDialog final : public absdialog::AbsDialog {
public:
    ~CastCardDialog() override;
    class SelectRace {
    public:
        explicit SelectRace(CastCardDialog& value) : this_(&value) {}
        void widgetSelected();
    private:
        CastCardDialog* this_{};
    };
    class BasicResist {
    public:
        explicit BasicResist(CastCardDialog& value) : this_(&value) {}
        void widgetSelected();
    private:
        CastCardDialog* this_{};
    };
    class BasicEnhance {
    public:
        explicit BasicEnhance(CastCardDialog& value) : this_(&value) {}
        void widgetSelected();
    private:
        CastCardDialog* this_{};
    };
    class SelLifeC {
    public:
        explicit SelLifeC(CastCardDialog& value) : this_(&value) {}
        void widgetSelected();
    private:
        CastCardDialog* this_{};
    };
    class LifeMaxL {
    public:
        explicit LifeMaxL(CastCardDialog& value) : this_(&value) {}
        void modifyText();
    private:
        CastCardDialog* this_{};
    };
    class MSListener {
    public:
        explicit MSListener(CastCardDialog& value) : this_(&value) {}
        void widgetSelected(features::Makings making);
    private:
        CastCardDialog* this_{};
    };
    class ESListener {
    public:
        ESListener(CastCardDialog& value, const std::shared_ptr<lib::CheckBox>& target)
            : this_(&value), target_(target) {}
        void widgetSelected();
    private:
        CastCardDialog* this_{};
        std::weak_ptr<lib::CheckBox> target_;
    };
    class CalcPhysical {
    public:
        explicit CalcPhysical(CastCardDialog& value) : this_(&value) {}
        void widgetSelected();
    private:
        CastCardDialog* this_{};
    };
    class CalcMental {
    public:
        explicit CalcMental(CastCardDialog& value) : this_(&value) {}
        void widgetSelected();
    private:
        CastCardDialog* this_{};
    };
    class LiveEnh {
    public:
        explicit LiveEnh(CastCardDialog& value) : this_(&value) {}
        void widgetSelected();
        void modifyText();
    private:
        CastCardDialog* this_{};
    };
    class SelMentality {
    public:
        explicit SelMentality(CastCardDialog& value) : this_(&value) {}
        void widgetSelected();
    private:
        CastCardDialog* this_{};
    };
    class LifeUseMax {
    public:
        explicit LifeUseMax(CastCardDialog& value) : this_(&value) {}
        void widgetSelected();
    private:
        CastCardDialog* this_{};
    };
    class ResetLiveStatus {
    public:
        explicit ResetLiveStatus(CastCardDialog& value) : this_(&value) {}
        void widgetSelected();
    private:
        CastCardDialog* this_{};
    };
    class Dispose {
    public:
        explicit Dispose(CastCardDialog& value) : this_(&value) {}
        void widgetDisposed();
    private:
        CastCardDialog* this_{};
    };

    CastCardDialog(CastCardDialogContext context, Props& prop,
                   std::shared_ptr<lib::Window> parent, const Summary* summ,
                   CastCardPtr card, bool read_only,
                   UseCounterPtr counter = {}, bool creationMode = false);
    CastCardDialog(Commons common, Props& prop, std::shared_ptr<lib::Window> parent,
                   const Summary* summ, CastCardPtr card, bool read_only,
                   UseCounterPtr counter = {});
    CastCardDialog(Commons common, Props& prop, std::shared_ptr<lib::Window> parent,
                   Summary& summ, CastCardPtr card, bool read_only,
                   UseCounterPtr counter = {})
        : CastCardDialog(std::move(common), prop, std::move(parent), &summ,
                         std::move(card), read_only, std::move(counter)) {}

    CastCardDialog(const CastCardDialog&) = delete;
    CastCardDialog& operator=(const CastCardDialog&) = delete;
    [[nodiscard]] CastCardPtr card() const noexcept { return card_; }
    [[nodiscard]] const Skin* summSkin() const noexcept;
    [[nodiscard]] const race::Race* selectedRace() const noexcept;
    [[nodiscard]] bool openCWXPath(std::string_view path,
                                   bool shell_activate);
    [[nodiscard]] const std::vector<lib::String>& warnings() const noexcept {
        return warnings_;
    }
    [[nodiscard]] const CastCardDialogContext& context() const noexcept {
        return context_;
    }
    [[nodiscard]] bool readOnly() const noexcept { return read_only_; }

    [[nodiscard]] const std::shared_ptr<spcarddialog::CardImageSelector>&
    imageControl() const noexcept { return image_selector_; }
    [[nodiscard]] const std::shared_ptr<lib::TextEdit>& nameControl() const noexcept {
        return name_;
    }
    [[nodiscard]] const std::shared_ptr<lib::TextEdit>& descriptionControl() const noexcept {
        return desc_;
    }
    [[nodiscard]] const std::shared_ptr<lib::NumberEdit>& levelControl() const noexcept {
        return level_;
    }
    [[nodiscard]] const std::shared_ptr<lib::NumberEdit>& lifeMaxControl() const noexcept {
        return life_max_;
    }
    [[nodiscard]] const std::shared_ptr<lib::ComboBox>& raceControl() const noexcept {
        return race_;
    }
    [[nodiscard]] const CouponEditor& couponControl() const noexcept {
        return coupon_view_;
    }
    [[nodiscard]] const std::shared_ptr<lib::NumberEdit>& lifeControl() const noexcept {
        return life_;
    }
    [[nodiscard]] const std::shared_ptr<lib::CheckBox>& lifeUseMaxControl() const noexcept {
        return life_use_max_;
    }

    // 中止可能な素材検証の後、実際のプロパティ書き込み前に実行する。
    lib::Action on_applying;
    [[nodiscard]] bool applyChanges() { return apply(); }
    void refreshWarning();
    void raceToolTip();
    void refCard(CastCardPtr card);
    void refScenario(const Summary* summ);
    void refSkin();
    void refDataVersion();
    void refreshSex();
    void refreshPeriod();
    void refreshNature();
    void refreshMakings();
    void updateNature();
    void constructBase(const std::shared_ptr<lib::Container>& parent);
    void constructDesc(const std::shared_ptr<lib::Container>& parent);
    void constructHistory(const std::shared_ptr<lib::Container>& parent);
    void constructMakings(const std::shared_ptr<lib::Container>& parent);
    void constructResist(const std::shared_ptr<lib::Container>& parent);
    void constructPhysical(const std::shared_ptr<lib::Container>& parent);
    void constructMental(const std::shared_ptr<lib::Container>& parent);
    void constructMentalTab();
    void constructEnhance(const std::shared_ptr<lib::Container>& parent);
    void constructStatus(const std::shared_ptr<lib::Container>& parent);
    void setMaxLife();
    void initPhysical();
    void modPhysical();
    void modPhysical(bool force);
    void updatePhysical();
    void constructPhysicalTab();
    void refCastCardParameterEditStyle();
    void initEnhance();
    void createAptitudes();
    void updateAptitudes();
    void updateAptitude(types::Physical physical, types::Mental mental);
    void changeLiveEnhance();
    void changeMentality();
    void changeLifeUseMax();
    void resetLiveStatus();
    void delCard(const CastCardPtr& card);

    [[nodiscard]] int physicalValue(types::Physical physical) const;
    [[nodiscard]] int mentalValue(types::Mental mental) const;
    [[nodiscard]] double calcPhy(types::Physical physical, bool all) const;
    [[nodiscard]] double calcMtl(types::Mental mental, bool all) const;

protected:
    void setup(Container& area) override;
    bool apply() override;

private:
    // Dと同じく、部品がある間だけEditor全体のモデル
    // ・スタイル通知を購読する。 Dlgがハンドラー一覧を
    // コピーして通知中でも、接続トークンで確実に解除する。
    void bindExternalNotifications();
    void unbindExternalNotifications() noexcept;

    CastCardDialogContext context_;
    editor_commons::Commons* notification_commons_{};
    std::uint64_t ref_cast_connection_{};
    std::uint64_t del_cast_connection_{};
    std::uint64_t ref_scenario_connection_{};
    std::uint64_t ref_skin_connection_{};
    std::uint64_t ref_image_scale_connection_{};
    std::uint64_t ref_data_version_connection_{};
    std::uint64_t ref_target_version_connection_{};
    std::uint64_t ref_coupons_connection_{};
    std::uint64_t ref_cast_card_parameter_edit_style_connection_{};
    std::uint64_t ref_radar_style_connection_{};
    std::uint64_t ref_element_overrides_connection_{};
    Props* prop_{};
    const Summary* summ_{};
    CastCardPtr card_;
    UseCounterPtr counter_;
    bool read_only_{};
    bool creation_mode_{};
    bool summary_skin_pinned_{};
    bool creation_initialized_{};
    const Skin* summ_skin_{};
    lib::Owner<Skin> source_skin_owner_;
    std::string id_;
    std::vector<lib::String> warnings_;
    std::shared_ptr<void> cast_lifetime_{std::make_shared<int>(0)};
    std::uint64_t cast_revision_{};
    [[nodiscard]] bool resolveSourceSkin();

    std::shared_ptr<lib::Panel> root_;
    std::shared_ptr<lib::Container> base_panel_;
    std::shared_ptr<lib::Container> description_panel_;
    std::shared_ptr<lib::Container> history_panel_;
    std::shared_ptr<lib::Container> makings_panel_;
    std::shared_ptr<lib::Container> resist_panel_;
    std::shared_ptr<lib::Container> physical_panel_;
    std::shared_ptr<lib::Container> mental_panel_;
    std::shared_ptr<lib::Container> enhance_panel_;
    std::shared_ptr<lib::Container> status_panel_;
    std::shared_ptr<spcarddialog::CardImageSelector> image_selector_;
    std::shared_ptr<lib::TextEdit> desc_;
    std::vector<std::shared_ptr<dmenu::TextMenuModify>> text_menus_;
    std::unique_ptr<customtext::GBLimitText> name_limit_;
    std::shared_ptr<lib::TextEdit> name_;
    std::shared_ptr<lib::NumberEdit> level_;
    std::shared_ptr<lib::NumberEdit> life_max_;
    CouponEditor coupon_view_;
    std::shared_ptr<couponview::CouponView<couponview::CVType::Cast>>
        full_coupon_view_;
    std::shared_ptr<lib::ComboBox> race_;

    std::map<features::Sex, std::shared_ptr<lib::RadioButton>> sex_;
    std::shared_ptr<lib::RadioButton> sex_unknown_;
    std::map<features::Period, std::shared_ptr<lib::RadioButton>> period_;
    std::shared_ptr<lib::RadioButton> period_unknown_;
    std::shared_ptr<lib::Container> nature_parent_;
    std::map<features::Nature, std::shared_ptr<lib::RadioButton>> nature_;
    std::shared_ptr<lib::RadioButton> nature_unknown_;
    std::map<features::Makings, std::shared_ptr<lib::CheckBox>> makings_;
    bool show_sp_nature_{};

    std::shared_ptr<lib::CheckBox> resist_weapon_;
    std::shared_ptr<lib::CheckBox> resist_magic_;
    std::shared_ptr<lib::CheckBox> undead_;
    std::shared_ptr<lib::CheckBox> automaton_;
    std::shared_ptr<lib::CheckBox> unholy_;
    std::shared_ptr<lib::CheckBox> constructure_;
    std::map<types::Element, std::shared_ptr<lib::CheckBox>> resist_;
    std::map<types::Element, std::shared_ptr<lib::CheckBox>> weakness_;

    std::shared_ptr<lib::Container> physical_parent_;
    std::shared_ptr<radarspinner::RadarSpinner> physical_radar_;
    std::shared_ptr<scales::Scales> physical_scales_;
    std::map<types::Physical, std::size_t> physical_indices_;
    std::shared_ptr<lib::Label> physical_sum_;
    std::shared_ptr<lib::Container> aptitude_physical_;
    std::map<types::Mental, std::shared_ptr<lib::Slider>> mental_;
    std::shared_ptr<lib::Container> aptitude_mental_;
    std::shared_ptr<radarspinner::RadarSpinner> enhance_radar_;
    std::shared_ptr<scales::Scales> enhance_scales_;
    std::map<types::Enhance, std::size_t> enhance_indices_;
    std::shared_ptr<lib::Container> enhance_parent_;

    struct AptitudeDisplay {
        skin::Aptitude aptitude{};
        std::shared_ptr<lib::ImageView> mark;
        std::shared_ptr<lib::Label> value;
    };
    std::vector<AptitudeDisplay> aptitude_displays_;
    std::array<lib::ResourceId, 4> aptitude_mark_images_{};

    std::shared_ptr<lib::NumberEdit> life_;
    std::shared_ptr<lib::CheckBox> life_use_max_;
    std::map<types::Enhance, std::shared_ptr<lib::NumberEdit>> live_enhance_;
    std::map<types::Enhance, std::shared_ptr<lib::NumberEdit>> enhance_round_;
    std::shared_ptr<lib::NumberEdit> paralyze_;
    std::shared_ptr<lib::NumberEdit> poison_;
    std::shared_ptr<lib::NumberEdit> bind_;
    std::shared_ptr<lib::NumberEdit> silence_;
    std::shared_ptr<lib::NumberEdit> face_up_;
    std::shared_ptr<lib::NumberEdit> anti_magic_;
    std::shared_ptr<lib::ComboBox> mentality_;
    std::vector<types::Mentality> mentality_table_;
    std::shared_ptr<lib::NumberEdit> mentality_round_;

    [[nodiscard]] static lib::String format(lib::String value,
                                             std::string_view first,
                                             std::string_view second = {},
                                             std::string_view third = {});
    [[nodiscard]] static std::shared_ptr<lib::Container> makePanel(
        const lib::Services& services, const std::shared_ptr<lib::Container>& parent,
        std::string title = {});
    template <typename T>
    [[nodiscard]] std::shared_ptr<T> makeControl(lib::ComponentKind kind) const;
    [[nodiscard]] std::shared_ptr<lib::RadioButton> makeRadio(
        const std::shared_ptr<lib::Container>& parent, std::string text,
        std::string group);
    [[nodiscard]] std::shared_ptr<lib::CheckBox> makeCheck(
        const std::shared_ptr<lib::Container>& parent, std::string text);
    [[nodiscard]] std::shared_ptr<lib::NumberEdit> makeNumber(
        const std::shared_ptr<lib::Container>& parent, double minimum,
        double maximum, double value = 0.0);
    [[nodiscard]] std::shared_ptr<lib::Slider> makeSlider(
        const std::shared_ptr<lib::Container>& parent, double minimum,
        double maximum, double value = 0.0);
    void setEditable(const std::shared_ptr<lib::Widget>& widget) const;
    void buildBase(const std::shared_ptr<lib::Container>& parent);
    void buildDescription(const std::shared_ptr<lib::Container>& parent);
    void refreshDescriptionFont();
    void buildHistory(const std::shared_ptr<lib::Container>& parent);
    void buildMakings(const std::shared_ptr<lib::Container>& parent);
    void buildResist(const std::shared_ptr<lib::Container>& parent);
    void buildPhysical(const std::shared_ptr<lib::Container>& parent);
    void buildMental(const std::shared_ptr<lib::Container>& parent);
    void buildEnhance(const std::shared_ptr<lib::Container>& parent);
    void buildStatus(const std::shared_ptr<lib::Container>& parent);
    [[nodiscard]] std::shared_ptr<lib::Button> createR(
        const std::shared_ptr<lib::Container>& parent, std::string text,
        std::string group = {});
    [[nodiscard]] std::shared_ptr<lib::Container> createButtonGroup(
        const std::shared_ptr<lib::Container>& parent, std::string title,
        unsigned columns = 1U);
    void setPhysicalValue(types::Physical physical, int value);
    [[nodiscard]] int defaultEnhanceValue(types::Enhance enhance) const;
    void setDefaultEnhanceValue(types::Enhance enhance, int value);
    void updateTitle(const CastCard* card);
    void setFeatureSelection();
    void appendSelectedCoupons(CouponArray& result) const;
    [[nodiscard]] bool hasCoupon(const CouponArray& values,
                                 std::string_view name) const;
    [[nodiscard]] static std::vector<types::Physical> physicals();
    [[nodiscard]] static std::vector<types::Mental> mentalPositives();
    [[nodiscard]] static std::vector<types::Enhance> enhancements();
    [[nodiscard]] static std::vector<types::Element> elements();
    [[nodiscard]] static std::vector<types::Mentality> mentalities();
    [[nodiscard]] std::vector<lib::String> inconsistencyWarnings(
        std::span<const types::Status> statuses) const;

    template <typename Feature, typename Control, typename CouponFactory>
    [[nodiscard]] CouponPtr createCoupon(
        const std::map<Feature, std::shared_ptr<Control>>& controls,
        CouponFactory&& coupon_factory) const {
        for (const auto& entry : controls) {
            if (entry.second && entry.second->checked)
                return std::make_shared<Coupon>(coupon_factory(entry.first), 0);
        }
        return {};
    }
};

void updateCoefficients(const Commons& common,
                        const lib::Array<CastCardPtr>& casts);
void updateCoefficients(const Commons& common,
                        lib::SharedSlice<const CastCardPtr> casts);
inline void updateCoefficients(const Commons& common,
                                std::initializer_list<CastCardPtr> casts) {
    updateCoefficients(common, lib::SharedSlice<const CastCardPtr>(casts));
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::castcarddialog
