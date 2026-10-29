#pragma once

#include "cwxeditor_src/core/cwx/event.hpp"
#include "cwxeditor_src/core/cwx/motion.hpp"
#include "cwxeditor_src/core/cwx/path.hpp"
#include "cwxeditor_src/core/cwx/props.hpp"
#include "cwxeditor_src/core/cwx/skin.hpp"
#include "cwxeditor_src/core/cwx/summary.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/contentinitializer.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/abilityview.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/areaview.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/chooser.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/couponview.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/editdialogsession.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/incsearch.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/keycodeview.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/materialselect.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/messageutils.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/motionview.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/spcarddialog.hpp"
#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/portable.hpp"
#include "cwxeditor_src/lib/window_host.hpp"
#include "cwxeditor_src/lib/xml.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::effectcarddialog {
class SoundSelect;
}

namespace cwxeditor::cwx::editor::gui::dwt::bgimagedialog {
class BgImageDialog;
}

namespace cwxeditor::cwx::editor::gui::dwt::xmlcontentdialog { class XmlContentDialog; }

namespace cwxeditor::cwx::editor::gui::dwt::eventdialog {

namespace event = ::cwxeditor::cwx::event;
namespace motion = ::cwxeditor::cwx::motion;
namespace path = ::cwxeditor::cwx::path;
namespace props = ::cwxeditor::cwx::props;
namespace skin = ::cwxeditor::cwx::skin;
namespace summary = ::cwxeditor::cwx::summary;
namespace types = ::cwxeditor::cwx::types;
namespace lib = ::cwxeditor::lib;
namespace contentinitializer =
    ::cwxeditor::cwx::editor::gui::dwt::contentinitializer;

namespace messageutils =
    ::cwxeditor::cwx::editor::gui::dwt::messageutils;
namespace couponview =
    ::cwxeditor::cwx::editor::gui::dwt::couponview;
namespace abilityview =
    ::cwxeditor::cwx::editor::gui::dwt::abilityview;
namespace areaview =
    ::cwxeditor::cwx::editor::gui::dwt::areaview;
namespace background = ::cwxeditor::cwx::background;
namespace chooser =
    ::cwxeditor::cwx::editor::gui::dwt::chooser;
namespace materialselect =
    ::cwxeditor::cwx::editor::gui::dwt::materialselect;
namespace keycodeview =
    ::cwxeditor::cwx::editor::gui::dwt::keycodeview;
namespace motionview =
    ::cwxeditor::cwx::editor::gui::dwt::motionview;
namespace spcarddialog =
    ::cwxeditor::cwx::editor::gui::dwt::spcarddialog;
namespace effectcarddialog =
    ::cwxeditor::cwx::editor::gui::dwt::effectcarddialog;
namespace bgimagedialog =
    ::cwxeditor::cwx::editor::gui::dwt::bgimagedialog;
namespace incsearch =
    ::cwxeditor::cwx::editor::gui::dwt::incsearch;

using String = lib::String;
using StringView = lib::StringView;
using CType = types::CType;
using Content = event::Content;
using ContentPtr = event::ContentPtr;
using EventTreePtr = event::EventTreePtr;
using Summary = summary::Summary;
using SummaryPtr = summary::Summary::Owner;
using Skin = skin::Skin;
using SkinPtr = skin::Skin::Ptr;
using UseCounter = event::UseCounter;
using UseCounterPtr = lib::Owner<UseCounter>;
using Parent = std::shared_ptr<lib::Container>;
using ResourceId = lib::ResourceId;
using Context = contentinitializer::ContentInitializerContext;
using WarningList = lib::Array<String>;
using MotionPtr = lib::Owner<motion::Motion>;

inline constexpr std::int32_t transitionSpeedDef = 5;
inline constexpr std::int32_t successRateMax = 5;

template <CType Type, bool Field>
[[nodiscard]] std::vector<types::Range> couponRanges() {
    std::vector<types::Range> result{types::Range::Selected,
                                     types::Range::Random,
                                     types::Range::Party};
    if constexpr (Field) {
        result.push_back(types::Range::Field);
        result.push_back(types::Range::Npc);
    }
    if constexpr (Type == CType::GetCoupon || Type == CType::LoseCoupon)
        result.push_back(types::Range::CouponHolder);
    return result;
}

struct DialogCallbacks {
    lib::Action applied;
    lib::Action closed;
    lib::Callback<void(const ContentPtr&)> contentChanged;
    lib::Callback<bool(StringView, bool)> openPath;
};

class EventDialog : public editdialogsession::EditDialogSession,
                    public std::enable_shared_from_this<EventDialog> {
public:
    EventDialog(Context context, SummaryPtr summary, CType type, Parent parent = {},
                ContentPtr parentContent = {}, ContentPtr content = {},
                SkinPtr forceSkin = {}, UseCounterPtr useCounter = {},
                bool create = false, bool enterClose = true);
    virtual ~EventDialog();

    EventDialog(const EventDialog&) = delete;
    EventDialog& operator=(const EventDialog&) = delete;

    [[nodiscard]] CType type() const noexcept { return type_; }
    [[nodiscard]] const Context& context() const noexcept { return context_; }
    [[nodiscard]] Context& context() noexcept { return context_; }
    [[nodiscard]] const SummaryPtr& summary() const noexcept { return summary_; }
    [[nodiscard]] const SkinPtr& skin() const noexcept { return skin_; }
    [[nodiscard]] const UseCounterPtr& useCounter() const noexcept {
        return use_counter_;
    }
    [[nodiscard]] const Parent& parentWidget() const noexcept { return parent_; }
    [[nodiscard]] const ContentPtr& parentContent() const noexcept {
        return parent_content_;
    }
    [[nodiscard]] const ContentPtr& event() const noexcept { return event_; }
    [[nodiscard]] const ContentPtr& draft() const noexcept { return draft_; }
    [[nodiscard]] bool createMode() const noexcept { return create_; }
    void setCreateMode(bool value) noexcept { create_ = value; }
    [[nodiscard]] bool enterClose() const noexcept { return enter_close_; }
    [[nodiscard]] bool isOpen() const noexcept { return open_; }
    [[nodiscard]] bool applyEnabled() const noexcept { return apply_enabled_; }
    [[nodiscard]] bool usesRawXmlEditor() const noexcept {
        return static_cast<bool>(xml_editor_);
    }
    [[nodiscard]] const String& title() const noexcept { return title_; }
    [[nodiscard]] ResourceId icon() const noexcept { return icon_; }
    [[nodiscard]] const WarningList& warnings() const noexcept {
        return warnings_;
    }
    [[nodiscard]] const std::shared_ptr<lib::Dialog>& widget() const noexcept {
        return widget_;
    }

    virtual bool apply();
    [[nodiscard]] lib::Window* editDialogWindow() const noexcept override {
        return widget_.get();
    }
    [[nodiscard]] bool editDialogPending() const noexcept override {
        return apply_enabled_;
    }
    [[nodiscard]] bool applyEditDialog() override { return apply(); }
    bool applyAndClose();
    void setApplyEnabled(bool value) {
        apply_enabled_ = value;
        if (apply_button_) apply_button_->set_enabled(value);
        if (value && !refreshing_warning_) {
            refreshing_warning_ = true;
            try { refreshWarning(); }
            catch (...) { refreshing_warning_ = false; throw; }
            refreshing_warning_ = false;
        }
    }
    void open();
    void active();
    virtual void forceCancel();
    virtual void close();
    void setCallbacks(DialogCallbacks callbacks) {
        callbacks_ = std::move(callbacks);
    }
    [[nodiscard]] const DialogCallbacks& callbacks() const noexcept {
        return callbacks_;
    }
    std::function<void(const std::vector<std::pair<ContentPtr, String>>&)> childNamesApplied;
    std::vector<lib::Action> appliedEvent;
    std::vector<lib::Action> closeEvent;
    void setEvent(ContentPtr value);
    void setSummary(SummaryPtr value);
    void setSkin(SkinPtr value);

    virtual void refSkin();
    virtual void refDataVersion();
    virtual bool openCWXPath(String path, bool activate);

protected:
    [[nodiscard]] bool openExternalCWXPath(String path, bool activate);
    virtual void setupBody() { buildCommonControls(); }
    bool refreshing_warning_{};
    std::vector<lib::Action> version_control_updates_;
    void bindSpecialText(const lib::WidgetPtr& editor, std::function<bool()> enabled);
    void refreshSpecialTextPreviews();
    std::shared_ptr<const int> special_text_lifetime_{std::make_shared<const int>(0)};
    std::vector<std::pair<std::weak_ptr<lib::Widget>, lib::Action>> special_text_updates_;
    std::vector<std::pair<std::weak_ptr<lib::Widget>, dmenu::TextMenuModifyPtr>> special_text_menus_;
    template <typename Geometry>
    void bindDialogGeometry(Geometry& geometry) {
        auto bounds = widget_->bounds();
        if (geometry.width() > 0) bounds.width = static_cast<float>(geometry.width());
        if (geometry.height() > 0) bounds.height = static_cast<float>(geometry.height());
        widget_->set_bounds(bounds);
        // 内容の実測最小サイズと画面上限は、引き続きホスト側で適用する。
        closeEvent.push_back([this, saved = &geometry] {
            // 保存値は未スケールの寸法(再オープン時に二重スケールされ巨大化するのを防ぐ)。
            const auto actual = lib::authored_window_bounds(*widget_);
            saved->width(static_cast<std::int32_t>(actual.width));
            saved->height(static_cast<std::int32_t>(actual.height));
        });
    }
    virtual void refExternalNames() {}
    // 渡された独立モデルだけへ書き込み、シナリオへの適用・通知は行わない。
    virtual void writeEditedFields(const ContentPtr&) const {}
    virtual void refreshWarning();
    virtual void refExternalPath(StringView oldPath, StringView newPath,
                                 bool isDirectory);
    virtual void refExternalPaths(StringView parentPath);
    virtual void refExternalFlagAndStep();
    virtual void delExternalFlagAndStep();
    virtual void delExternalFlagAndStep(bool flagDeleted, bool stepDeleted);
    virtual void refExternalImageScale();
    virtual void refExternalUndoMax();
    virtual void refExternalResources(chooser::AreaChoiceKind kind);
    void delContent(const ContentPtr& value);
    [[nodiscard]] ContentPtr& mutableDraft() noexcept { return draft_; }
    [[nodiscard]] ContentPtr ensureDraft();
    bool commitDraft();
    // footerLeadingは、OK・キャンセル・適用の行の左
    // 端へ置く任意の部品。 ALLの「演算結果を得る」などに使う。
    void installTypedBody(const lib::WidgetPtr& body, lib::Size size,
                          bool showApply = true,
                          const lib::WidgetPtr& footerLeading = {});
    void installMessageBody(messageutils::AbstractMessageDialog& editor,
                            const lib::WidgetPtr& editorBody,
                            lib::Size fallbackSize);
    void refreshTypedBody();
    void replaceWarnings(WarningList value);
    void addWarning(String value);
    [[nodiscard]] WarningList modelWarnings(const Content& value) const;
    [[nodiscard]] bool targetVersion(StringView target,
                                     StringView wsn = {}) const;
    [[nodiscard]] String contentName() const;
    [[nodiscard]] String formatTitle(String name) const;
    void buildCommonControls();
    void rebuildPropertyTable();
    void updateProperty(std::size_t row, String value);
    bool applyXmlDraft();
    void finishClose(lib::DialogResult result);

    Context context_{};
    SummaryPtr summary_{};
    CType type_{CType::Start};
    Parent parent_{};
    ContentPtr parent_content_{};
    ContentPtr event_{};
    ContentPtr draft_{};
    SkinPtr skin_{};
    SkinPtr original_forced_skin_{};
    UseCounterPtr use_counter_{};
    std::shared_ptr<lib::Dialog> widget_{};
    DialogCallbacks callbacks_{};
    WarningList warnings_{};
    String title_{};
    ResourceId icon_{};
    std::shared_ptr<lib::TextEdit> xml_editor_{};
    std::shared_ptr<lib::TableView> property_table_{};
    struct PropertyBinding {
        lib::XmlNode node{};
        String attribute{};
        bool text{};
    };
    lib::XmlNode property_root_{};
    std::vector<PropertyBinding> property_bindings_{};
    std::shared_ptr<lib::Label> warning_label_{};
    std::shared_ptr<lib::Button> ok_button_{};
    std::shared_ptr<lib::Button> apply_button_{};
    std::shared_ptr<lib::Button> cancel_button_{};
    ResourceId warning_icon_{};
    bool create_{};
    bool enter_close_{true};
    bool open_{};
    bool setup_done_{};
    bool apply_enabled_{};
    lib::Action opened_body_{};

private:
    // モードレス画面が有効な間だけ、Editor共通の通知
    // を購読する。 関数の同一性ではなくトークンで管理し、
    // Dlgがハンドラーをコピー済みでも安全に解除する。
    void bindExternalNotifications();
    void unbindExternalNotifications() noexcept;
    void updateWarningPresentation();

    commons::Commons* notification_commons_{};
    std::uint64_t ref_gossips_connection_{};
    std::uint64_t ref_stamps_connection_{};
    std::uint64_t repl_text_connection_{};
    std::uint64_t ref_skin_connection_{};
    std::uint64_t ref_data_version_connection_{};
    std::uint64_t ref_target_version_connection_{};
    std::uint64_t ref_path_connection_{};
    std::uint64_t ref_paths_connection_{};
    std::uint64_t del_paths_connection_{};
    std::uint64_t ref_flag_step_connection_{};
    std::uint64_t del_flag_step_connection_{};
    std::uint64_t ref_image_scale_connection_{};
    std::uint64_t ref_undo_max_connection_{};
    std::uint64_t ref_preview_values_connection_{};
    std::uint64_t del_content_connection_{};
    std::uint64_t del_resource_area_connection_{};
    std::uint64_t del_resource_battle_connection_{};
    std::uint64_t del_resource_package_connection_{};
    std::uint64_t del_resource_cast_connection_{};
    std::uint64_t del_resource_info_connection_{};
};

using EventDialogPtr = lib::Owner<EventDialog>;

class ContentCommentDialog {
public:
    ContentCommentDialog(Context context, Parent parent, ContentPtr parentContent,
                         ContentPtr content);
    ContentCommentDialog(Context context, Parent parent, EventTreePtr eventTree);
    virtual ~ContentCommentDialog();

    [[nodiscard]] const Context& context() const noexcept { return context_; }
    [[nodiscard]] const ContentPtr& event() const noexcept { return event_; }
    [[nodiscard]] const ContentPtr& draft() const noexcept { return draft_; }
    [[nodiscard]] const Parent& parentWidget() const noexcept { return parent_; }
    [[nodiscard]] EventTreePtr eventTree() const {
        return event_tree_ ? event_tree_ : event_ ? event_->tree() : EventTreePtr{};
    }
    [[nodiscard]] const String& comment() const noexcept { return comment_; }
    void setComment(String value) { comment_ = std::move(value); }
    [[nodiscard]] const std::shared_ptr<lib::Dialog>& widget() const noexcept {
        return widget_;
    }
    bool apply();
    // EventWindowは、対象コンテントを保持する履歴処理を渡す。
    // 単独の呼び出し元でも、同じ正規化と既定のモデル書き込みを使う。
    lib::Callback<bool(StringView)> commitComment;
    void open();
    void active();
    void forceCancel();
    void close();
    [[nodiscard]] bool isOpen() const noexcept { return open_; }
    std::vector<lib::Action> appliedEvent;
    std::vector<lib::Action> closeEvent;

private:
    void setupUi();
    void finishClose(lib::DialogResult result);

    Context context_{};
    Parent parent_{};
    ContentPtr parent_content_{};
    ContentPtr event_{};
    ContentPtr draft_{};
    EventTreePtr event_tree_{};
    std::shared_ptr<lib::Dialog> widget_{};
    std::shared_ptr<lib::TextEdit> editor_{};
    // コメント編集のUndo/Redo履歴（Ctrl+Z/Ctrl+Y
    // ・ALLのタイトルボタン）と、文字列のコンテキストメニュー。
    dmenu::TextMenuModifyPtr text_menu_{};
    std::shared_ptr<lib::Button> ok_button_{};
    std::shared_ptr<lib::Button> apply_button_{};
    std::shared_ptr<lib::Button> cancel_button_{};
    String comment_{};
    bool open_{};
    bool closed_{};
    bool applying_{};
    std::uint64_t operation_revision_{};
    std::uint64_t del_content_connection_{};
    commons::Commons* notification_commons_{};
    std::weak_ptr<const int> notification_commons_lifetime_;
    std::shared_ptr<int> notification_lifetime_{std::make_shared<int>(0)};
};

class RangePanel {
public:
    RangePanel(Context context, SummaryPtr summary, UseCounterPtr useCounter,
               std::vector<types::Range> ranges = {}, String initCoupon = {},
               CType type = CType::Effect, ContentPtr content = {});
    [[nodiscard]] types::Range range() const noexcept { return range_; }
    void setRange(types::Range value);
    [[nodiscard]] bool rangeEnabled(types::Range value) const noexcept;
    [[nodiscard]] const String& holdingCoupon() const noexcept {
        return (coupon_updated_ || range_ == types::Range::CouponHolder)
                   ? coupon_
                   : initial_coupon_;
    }
    void setCoupon(String value) {
        coupon_ = std::move(value);
        coupon_updated_ = true;
        updateEnabled();
    }
    void setCouponUpdated(bool value) noexcept { coupon_updated_ = value; }
    [[nodiscard]] bool couponUpdated() const noexcept { return coupon_updated_; }
    [[nodiscard]] const WarningList& warnings() const;
    void refDataVersion();
    void updateEnabled();
    void setWarningCallback(lib::Action callback) {
        refresh_warning_ = std::move(callback);
    }

private:
    Context context_{};
    SummaryPtr summary_{};
    UseCounterPtr use_counter_{};
    std::vector<types::Range> ranges_{};
    CType type_{CType::Effect};
    types::Range range_{types::Range::Selected};
    String initial_coupon_{};
    String coupon_{};
    mutable WarningList warnings_{};
    lib::Action refresh_warning_{};
    bool coupon_updated_{};
    bool enabled_{true};
};

class TransitionPanel {
public:
    TransitionPanel(Context context, SummaryPtr summary, ContentPtr content = {});
    ~TransitionPanel();
    TransitionPanel(const TransitionPanel&) = delete;
    TransitionPanel& operator=(const TransitionPanel&) = delete;
    void bindControls(const std::shared_ptr<lib::ComboBox>& kind,
                      const std::shared_ptr<lib::NumberEdit>& speed);
    void refreshControls();
    [[nodiscard]] types::Transition transition() const noexcept {
        return transition_;
    }
    void setTransition(types::Transition value) noexcept { transition_ = value; }
    [[nodiscard]] std::int32_t transitionSpeed() const noexcept {
        return transition_speed_;
    }
    void setTransitionSpeed(std::int32_t value) noexcept {
        transition_speed_ = value;
    }
    [[nodiscard]] const WarningList& warnings() const noexcept {
        return warnings_;
    }
    void refreshTS();
    std::vector<lib::Action> modEvent;

private:
    Context context_{};
    SummaryPtr summary_{};
    types::Transition transition_{types::Transition::Default};
    std::int32_t transition_speed_{transitionSpeedDef};
    WarningList warnings_{};
    std::weak_ptr<lib::ComboBox> kind_control_;
    std::weak_ptr<lib::NumberEdit> speed_control_;
    std::uint64_t version_connection_{};
    std::uint64_t scenario_connection_{};
};

template <CType Type, typename... Extra>
class AreaSelectDialog final : public EventDialog {
public:
    AreaSelectDialog(Context context, SummaryPtr summary, Parent parent = {},
                     ContentPtr parentContent = {}, ContentPtr content = {},
                     SkinPtr skin = {}, UseCounterPtr useCounter = {})
        : EventDialog(std::move(context), std::move(summary), Type,
                      std::move(parent), std::move(parentContent),
                      std::move(content), std::move(skin),
                      std::move(useCounter)),
          transition_(context_, summary_, draft_) {
        if constexpr (Type == CType::GetCast) {
            start_action_ = types::StartAction::Now;
            if (summary_ && (summary_->legacy() || targetVersion("2")))
                start_action_ = types::StartAction::NextRound;
        }
        loadDraft();
    }

    [[nodiscard]] std::uint64_t selected() const noexcept { return selected_; }
    void setSelected(std::uint64_t value) noexcept { selected_ = value; }
    [[nodiscard]] types::StartAction startAction() const noexcept {
        return start_action_;
    }
    void setStartAction(types::StartAction value) noexcept { start_action_ = value; }
    [[nodiscard]] TransitionPanel& transitionPanel() noexcept {
        return transition_;
    }
    void writeEditedFields(const ContentPtr& value) const override {
        if constexpr (Type == CType::ChangeArea) {
            value->area(selected_);
            value->transition(transition_.transition());
            value->transitionSpeed(transition_.transitionSpeed());
        } else if constexpr (Type == CType::StartBattle) {
            value->battle(selected_);
        } else if constexpr (Type == CType::CallPackage ||
                             Type == CType::LinkPackage) {
            value->packages(selected_);
        } else if constexpr (Type == CType::BranchCast ||
                             Type == CType::GetCast ||
                             Type == CType::LoseCast) {
            value->casts(selected_);
            if constexpr (Type == CType::GetCast)
                value->startAction(start_action_);
        } else if constexpr (Type == CType::BranchInfo ||
                             Type == CType::GetInfo ||
                             Type == CType::LoseInfo) {
            value->info(selected_);
        }
    }
    bool apply() override {
        if (resource_retired_ || (summary_ && !summary_->scenarioPath().empty() && !hasResources())) return false;
        writeEditedFields(ensureDraft());
        return commitDraft();
    }

private:
    void refreshWarning() override;
    [[nodiscard]] static constexpr chooser::AreaChoiceKind resourceKind() {
        if constexpr (Type == CType::ChangeArea) return chooser::AreaChoiceKind::Area;
        else if constexpr (Type == CType::StartBattle) return chooser::AreaChoiceKind::Battle;
        else if constexpr (Type == CType::CallPackage || Type == CType::LinkPackage)
            return chooser::AreaChoiceKind::Package;
        else if constexpr (Type == CType::BranchCast || Type == CType::GetCast || Type == CType::LoseCast)
            return chooser::AreaChoiceKind::Cast;
        else return chooser::AreaChoiceKind::Info;
    }
    void refExternalResources(chooser::AreaChoiceKind kind) override {
        if (kind == resourceKind() && summary_ && !summary_->scenarioPath().empty() && !hasResources()) {
            resource_retired_ = true;
            forceCancel();
        }
    }
    [[nodiscard]] bool hasResources() const {
        if (!summary_) return false;
        if constexpr (Type == CType::ChangeArea) return !summary_->areas().empty();
        else if constexpr (Type == CType::StartBattle) return !summary_->battles().empty();
        else if constexpr (Type == CType::CallPackage || Type == CType::LinkPackage)
            return !summary_->packages().empty();
        else if constexpr (Type == CType::BranchCast || Type == CType::GetCast || Type == CType::LoseCast)
            return !summary_->casts().empty();
        else return !summary_->infos().empty();
    }
    void setupBody() override { buildControls(); }
    void buildControls();
    void loadDraft() {
        if (!draft_) return;
        if constexpr (Type == CType::ChangeArea) selected_ = draft_->area();
        else if constexpr (Type == CType::StartBattle) selected_ = draft_->battle();
        else if constexpr (Type == CType::CallPackage ||
                           Type == CType::LinkPackage)
            selected_ = draft_->packages();
        else if constexpr (Type == CType::BranchCast ||
                           Type == CType::GetCast ||
                           Type == CType::LoseCast) {
            selected_ = draft_->casts();
            if constexpr (Type == CType::GetCast) start_action_ = draft_->startAction();
        } else if constexpr (Type == CType::BranchInfo ||
                             Type == CType::GetInfo ||
                             Type == CType::LoseInfo)
            selected_ = draft_->info();
        if constexpr (Type == CType::ChangeArea) {
            transition_.setTransition(draft_->transition());
            transition_.setTransitionSpeed(draft_->transitionSpeed());
        }
    }

    bool resource_retired_{};
    std::uint64_t selected_{};
    TransitionPanel transition_;
    types::StartAction start_action_{types::StartAction::Now};
    std::unique_ptr<chooser::AreaChooserBase> area_chooser_{};
    std::shared_ptr<lib::ComboBox> transition_control_{};
    std::shared_ptr<lib::NumberEdit> transition_speed_control_{};
};

template <CType Type>
class StartSelectDialog final : public EventDialog {
public:
    StartSelectDialog(Context context, SummaryPtr summary, Parent parent = {},
                      ContentPtr parentContent = {}, ContentPtr content = {},
                      SkinPtr skin = {}, UseCounterPtr useCounter = {})
        : EventDialog(std::move(context), std::move(summary), Type,
                      std::move(parent), std::move(parentContent),
                      std::move(content), std::move(skin),
                      std::move(useCounter)) {
        if (draft_) selected_ = draft_->start();
        if (selected_.empty() && parent_content_) {
            if (const auto tree = parent_content_->tree(); tree &&
                !tree->starts().empty()) {
                selected_ = tree->starts().front()->name();
            }
        }
    }
    ~StartSelectDialog() override;
    [[nodiscard]] const String& selected() const noexcept { return selected_; }
    void setSelected(String value) { selected_ = std::move(value); }
    [[nodiscard]] const std::shared_ptr<lib::ListView>& startControl() const
        noexcept { return start_control_; }
    [[nodiscard]] const std::shared_ptr<lib::Menu>& contextMenu() const
        noexcept { return context_menu_; }
    [[nodiscard]] const std::shared_ptr<incsearch::IncSearch>&
    incrementalSearchControl() const noexcept { return incremental_search_; }
    void writeEditedFields(const ContentPtr& value) const override {
        value->start(selected_);
    }
    bool apply() override {
        writeEditedFields(ensureDraft());
        return commitDraft();
    }

private:
    void setupBody() override { buildControls(); }
    void buildControls();
    void refreshStarts(const ContentPtr& deleted = {}, bool initial = false);
    void openSelected();
    [[nodiscard]] bool hasStarts() const;
    String selected_{};
    std::shared_ptr<lib::ListView> start_control_{};
    std::shared_ptr<lib::Menu> context_menu_{};
    std::shared_ptr<incsearch::IncSearch> incremental_search_{};
    std::vector<ContentPtr> visible_starts_{};
    std::shared_ptr<int> start_lifetime_{std::make_shared<int>(0)};
    std::uint64_t ref_content_connection_{};
    std::uint64_t del_content_connection_{};
    std::uint64_t repl_text_connection_{};
};

class ClearEventDialog final : public EventDialog {
public:
    ClearEventDialog(Context context, SummaryPtr summary, Parent parent = {},
                     ContentPtr parentContent = {}, ContentPtr content = {},
                     SkinPtr skin = {}, UseCounterPtr useCounter = {})
        : EventDialog(std::move(context), std::move(summary), CType::End,
                      std::move(parent), std::move(parentContent),
                       std::move(content), std::move(skin),
                       std::move(useCounter)) {
        complete_ = draft_ ? draft_->complete() : true;
    }
    [[nodiscard]] bool complete() const noexcept { return complete_; }
    void setComplete(bool value) noexcept { complete_ = value; }
    void writeEditedFields(const ContentPtr& value) const override {
        value->complete(complete_);
    }
    bool apply() override {
        writeEditedFields(ensureDraft());
        return commitDraft();
    }

private:
    void setupBody() override { buildControls(); }
    void buildControls();
    bool complete_{true};
};

template <CType Type, bool EditValue, bool Field>
class CouponEventDialog final : public EventDialog {
public:
    CouponEventDialog(Context context, SummaryPtr summary, Parent parent = {},
                      ContentPtr parentContent = {}, ContentPtr content = {},
                      SkinPtr skin = {}, UseCounterPtr useCounter = {})
        : EventDialog(std::move(context), std::move(summary), Type,
                      std::move(parent), std::move(parentContent),
                      std::move(content), std::move(skin),
                      std::move(useCounter)),
          range_(context_, summary_, use_counter_,
                 couponRanges<Type, Field>(),
                 {}, Type, draft_) {
        multi_mode_ = (Type == CType::BranchCoupon) &&
                      (!summary_ || !summary_->legacy());
        loadDraft();
        // 作業コピーの全項目を読み込んでから、Dの従来の範囲の代替値を適用する。
        range_.refDataVersion();
    }
    // EventDialog基底の破棄が始まる前に、選択部品のコールバックを無効化する。
    ~CouponEventDialog() override { coupon_callback_lifetime_.reset(); }
    [[nodiscard]] RangePanel& rangePanel() noexcept { return range_; }
    [[nodiscard]] const String& coupon() const noexcept { return coupon_; }
    void setCoupon(String value) { coupon_ = std::move(value); }
    [[nodiscard]] const lib::Array<String>& couponNames() const noexcept {
        return coupon_names_;
    }
    void setCouponNames(lib::Array<String> value) {
        coupon_names_ = std::move(value);
    }
    [[nodiscard]] types::MatchingType matchingType() const noexcept {
        return matching_type_;
    }
    void setMatchingType(types::MatchingType value) noexcept {
        matching_type_ = value;
    }
    [[nodiscard]] int couponValue() const noexcept { return coupon_value_; }
    void setCouponValue(int value) noexcept { coupon_value_ = value; }
    [[nodiscard]] bool invertResult() const noexcept { return invert_result_; }
    void setInvertResult(bool value) noexcept { invert_result_ = value; }
    [[nodiscard]] bool expandSPChars() const noexcept { return expand_sp_chars_; }
    bool openCWXPath(String path, bool activate) override;
    void setExpandSPChars(bool value) noexcept { expand_sp_chars_ = value; }
    [[nodiscard]] const std::shared_ptr<lib::SplitView>& couponSplit() const noexcept {
        return coupon_split_;
    }

    void writeEditedFields(const ContentPtr& value) const override {
        value->range(range_.range());
        value->holdingCoupon(range_.holdingCoupon());
        if (multi_mode_) {
            value->couponNames(lib::SharedSlice<String>(coupon_names_));
            value->matchingType(matching_type_);
        } else if (value->detail().use(types::CArg::CouponNames)) {
            value->couponNames(lib::SharedSlice<String>(
                coupon_.empty() ? lib::Array<String>{}
                                : lib::Array<String>{coupon_}));
        } else {
            value->coupon(coupon_);
        }
        if constexpr (EditValue) value->couponValue(coupon_value_);
        if constexpr (Type == CType::BranchCoupon)
            value->invertResult(invert_result_);
        value->expandSPChars(expand_sp_chars_);
    }
    bool apply() override {
        writeEditedFields(ensureDraft());
        return commitDraft();
    }

private:
    void refreshWarning() override;
    void setupBody() override { buildControls(); }
    void buildControls();
    void loadDraft() {
        if (!draft_) return;
        range_.setRange(draft_->range());
        range_.setCoupon(draft_->holdingCoupon());
        if (multi_mode_) {
            coupon_names_ = draft_->couponNames();
            matching_type_ = draft_->matchingType();
        } else if (!draft_->couponNames().empty() &&
                   draft_->detail().use(types::CArg::CouponNames)) {
            coupon_ = draft_->couponNames().front();
        } else {
            coupon_ = draft_->coupon();
        }
        if constexpr (EditValue) coupon_value_ = draft_->couponValue();
        if constexpr (Type == CType::BranchCoupon)
            invert_result_ = draft_->invertResult();
        expand_sp_chars_ = draft_->expandSPChars();
    }

    // weak_from_this()が空になるスタック上のダイアログも、弱いトークンで保護する。
    std::shared_ptr<const int> coupon_callback_lifetime_{
        std::make_shared<const int>(0)};
    RangePanel range_;
    String coupon_{};
    void refDataVersion() override;
    void buildCouponFields();
    std::shared_ptr<lib::Panel> coupon_fields_;
    lib::Array<String> coupon_names_{};
    types::MatchingType matching_type_{types::MatchingType::And};
    int coupon_value_{};
    bool invert_result_{};
    bool expand_sp_chars_{};
    bool multi_mode_{};
    std::unique_ptr<couponview::CouponViewBase> coupon_view_{};
    std::shared_ptr<lib::SplitView> coupon_split_{};
};

using BranchCouponDialog =
    CouponEventDialog<CType::BranchCoupon, false, true>;
using GetCouponDialog = CouponEventDialog<CType::GetCoupon, true, false>;
using LoseCouponDialog = CouponEventDialog<CType::LoseCoupon, false, false>;

template <CType Type, bool Gossip>
class OneTextEventDialog final : public EventDialog {
public:
    OneTextEventDialog(Context context, SummaryPtr summary, Parent parent = {},
                       ContentPtr parentContent = {}, ContentPtr content = {},
                       SkinPtr skin = {}, UseCounterPtr useCounter = {})
        : EventDialog(std::move(context), std::move(summary), Type,
                      std::move(parent), std::move(parentContent),
                      std::move(content), std::move(skin),
                      std::move(useCounter)) {
        if (draft_) text_ = Gossip ? draft_->gossip() : draft_->completeStamp();
        if constexpr (Gossip) {
            if (draft_) expand_sp_chars_ = draft_->expandSPChars();
        }
    }
    [[nodiscard]] const String& text() const noexcept { return text_; }
    void setText(String value) { text_ = std::move(value); }
    [[nodiscard]] bool expandSPChars() const noexcept { return expand_sp_chars_; }
    void setExpandSPChars(bool value) noexcept { expand_sp_chars_ = value; }
    bool openCWXPath(String pathValue, const bool activate) override {
        if constexpr (Gossip) {
            if (path::cpcategory(pathValue) == "gossip")
                return path::cpempty(path::cpbottom(pathValue));
        }
        return EventDialog::openCWXPath(std::move(pathValue), activate);
    }
    void writeEditedFields(const ContentPtr& value) const override {
        if constexpr (Gossip) value->gossip(text_);
        else value->completeStamp(text_);
        if constexpr (Gossip) value->expandSPChars(expand_sp_chars_);
    }
    bool apply() override {
        writeEditedFields(ensureDraft());
        return commitDraft();
    }

private:
    void refreshWarning() override;
    void refExternalNames() override {
        if (name_combo_ && name_combo_->refresh) name_combo_->refresh();
    }
    std::shared_ptr<chooser::ChooserCombo> name_combo_;
    void setupBody() override { buildControls(); }
    void buildControls();
    String text_{};
    bool expand_sp_chars_{};
};

template <CType Type>
using GossipEventDialog = OneTextEventDialog<Type, true>;
template <CType Type>
using EndEventDialog = OneTextEventDialog<Type, false>;

class BgImagesDialog final : public EventDialog {
public:
    BgImagesDialog(Context context, SummaryPtr summary, Parent parent = {},
                   ContentPtr parentContent = {}, ContentPtr content = {},
                   CType type = CType::ChangeBgImage, SkinPtr skin = {},
                   UseCounterPtr useCounter = {}, Parent referenceTarget = {})
        : EventDialog(std::move(context), std::move(summary), type,
                      std::move(parent), std::move(parentContent),
                      std::move(content), std::move(skin),
                      std::move(useCounter)),
          reference_target_(std::move(referenceTarget)),
          transition_(context_, summary_, draft_) {
        if (draft_) {
            backs_ = draft_->backs();
            cell_name_ = draft_->cellName();
            do_anime_ = draft_->doAnime();
            ignore_effect_booster_ = draft_->ignoreEffectBooster();
        } else {
            do_anime_ = true;
        }
        background::BgImageArray cells;
        if (draft_) {
            for (const auto& cell : draft_->backgroundImages())
                if (cell) cells.push_back(cell->dup());
        }
        // この一時的な参照範囲を所有するのは、背景編集だけ。
        if (use_counter_) use_counter_ = use_counter_->sub();
        background_container_ =
            std::make_shared<background::BgImageContainer>(std::move(cells), use_counter_);
        closeEvent.push_back([this] { disposeBackgroundDraft(); });
    }
    ~BgImagesDialog() override { disposeBackgroundDraft(); }
    [[nodiscard]] const lib::Array<String>& backs() const noexcept { return backs_; }
    void setBacks(lib::Array<String> value) {
        backs_ = std::move(value);
        legacy_backs_override_ = true;
        if (!background_container_) return;
        background::BgImageArray cells;
        cells.reserve(backs_.size());
        for (const auto& path : backs_) {
            auto image = std::make_shared<background::ImageCell>();
            image->path(path);
            cells.push_back(std::move(image));
        }
        background_container_->backs(std::move(cells));
    }
    [[nodiscard]] TransitionPanel& transitionPanel() noexcept { return transition_; }
    [[nodiscard]] const String& cellName() const noexcept { return cell_name_; }
    void setCellName(String value) { cell_name_ = std::move(value); }
    [[nodiscard]] bool doAnime() const noexcept { return do_anime_; }
    void setDoAnime(bool value) noexcept { do_anime_ = value; }
    [[nodiscard]] bool ignoreEffectBooster() const noexcept {
        return ignore_effect_booster_;
    }
    void setIgnoreEffectBooster(bool value) noexcept {
        ignore_effect_booster_ = value;
    }
    [[nodiscard]] const Parent& referenceTarget() const noexcept {
        return reference_target_;
    }
    bool apply() override;
    bool openCWXPath(String path, bool activate) override;

private:
    void setupBody() override { buildControls(); }
    void buildControls();
    void editBackgroundCell(int type, bool create,
                            std::shared_ptr<background::BgImage> target = {});
    void disposeBackgroundDraft();
    void refreshCellNameChoices();
    void refreshWarning() override;
    void refSkin() override;
    void refDataVersion() override;
    void refExternalPath(StringView oldPath, StringView newPath,
                         bool isDirectory) override;
    void refExternalPaths(StringView parentPath) override;
    void refExternalFlagAndStep() override;
    void refExternalImageScale() override;
    void refExternalUndoMax() override;
    lib::Array<String> backs_{};
    String cell_name_{};
    Parent reference_target_{};
    TransitionPanel transition_;
    std::shared_ptr<background::BgImageContainer> background_container_;
    areaview::AreaViewContext background_context_;
    std::shared_ptr<undo::UndoManager> background_undo_;
    std::unique_ptr<areaview::BgImagesView> background_view_;
    std::shared_ptr<lib::ComboBox> cell_name_control_;
    std::vector<std::shared_ptr<bgimagedialog::BgImageDialog>>
        active_background_dialogs_;
    bool do_anime_{true};
    bool ignore_effect_booster_{};
    bool legacy_backs_override_{};
};

class LoseBgImageDialog final : public EventDialog {
public:
    LoseBgImageDialog(Context context, SummaryPtr summary, Parent parent = {},
                      ContentPtr parentContent = {}, ContentPtr content = {},
                      SkinPtr skin = {}, UseCounterPtr useCounter = {})
        : EventDialog(std::move(context), std::move(summary),
                      CType::LoseBgImage, std::move(parent),
                      std::move(parentContent), std::move(content),
                      std::move(skin), std::move(useCounter)),
          transition_(context_, summary_, draft_) {
        if (draft_) {
            cell_name_ = draft_->cellName();
            do_anime_ = draft_->doAnime();
            ignore_effect_booster_ = draft_->ignoreEffectBooster();
        } else {
            do_anime_ = true;
        }
    }
    [[nodiscard]] TransitionPanel& transitionPanel() noexcept { return transition_; }
    [[nodiscard]] const String& cellName() const noexcept { return cell_name_; }
    void setCellName(String value) { cell_name_ = std::move(value); }
    void setDoAnime(bool value) noexcept { do_anime_ = value; }
    void setIgnoreEffectBooster(bool value) noexcept {
        ignore_effect_booster_ = value;
    }
    void writeEditedFields(const ContentPtr& value) const override;
    bool apply() override;

private:
    void refreshWarning() override;
    void setupBody() override { buildControls(); }
    void buildControls();
    String cell_name_{};
    TransitionPanel transition_;
    bool do_anime_{true};
    bool ignore_effect_booster_{};
};

class BgmDialog final : public EventDialog {
public:
    BgmDialog(Context context, SummaryPtr summary, Parent parent = {},
              ContentPtr parentContent = {}, ContentPtr content = {},
              SkinPtr skin = {}, UseCounterPtr useCounter = {});
    [[nodiscard]] const String& path() const noexcept { return path_; }
    void setPath(String value) { path_ = std::move(value); }
    [[nodiscard]] std::uint32_t volume() const noexcept { return volume_; }
    void setVolume(std::uint32_t value) noexcept { volume_ = value; }
    [[nodiscard]] std::uint32_t loopCount() const noexcept { return loop_count_; }
    void setLoopCount(std::uint32_t value) noexcept { loop_count_ = value; }
    [[nodiscard]] std::uint32_t fadeIn() const noexcept { return fade_in_; }
    void setFadeIn(std::uint32_t value) noexcept { fade_in_ = value; }
    [[nodiscard]] std::uint32_t channel() const noexcept { return channel_; }
    void setChannel(std::uint32_t value) noexcept { channel_ = value; }
    void writeEditedFields(const ContentPtr& value) const override;
    void refSkin() override;
    bool apply() override;

private:
    void refreshWarning() override;
    void setupBody() override { buildControls(); }
    void buildControls();
    void syncSelector();
    String path_{};
    std::uint32_t volume_{100};
    std::uint32_t loop_count_{};
    std::uint32_t fade_in_{};
    std::uint32_t channel_{};
    std::unique_ptr<materialselect::MaterialSelectBase> selector_{};
};

class SeDialog final : public EventDialog {
public:
    SeDialog(Context context, SummaryPtr summary, Parent parent = {},
             ContentPtr parentContent = {}, ContentPtr content = {},
             SkinPtr skin = {}, UseCounterPtr useCounter = {});
    [[nodiscard]] const String& path() const noexcept { return path_; }
    void setPath(String value) { path_ = std::move(value); }
    void setVolume(std::uint32_t value) noexcept { volume_ = value; }
    void setLoopCount(std::uint32_t value) noexcept { loop_count_ = value; }
    void setFadeIn(std::uint32_t value) noexcept { fade_in_ = value; }
    void setChannel(std::uint32_t value) noexcept { channel_ = value; }
    void writeEditedFields(const ContentPtr& value) const override;
    void refSkin() override;
    bool apply() override;

private:
    void refreshWarning() override;
    void setupBody() override { buildControls(); }
    void buildControls();
    void syncSelector();
    String path_{};
    std::uint32_t volume_{100};
    std::uint32_t loop_count_{1};
    std::uint32_t fade_in_{};
    std::uint32_t channel_{};
    std::unique_ptr<materialselect::MaterialSelectBase> selector_{};
};

template <CType Type>
class NumericEventDialog final : public EventDialog {
public:
    NumericEventDialog(Context context, SummaryPtr summary, Parent parent = {},
                       ContentPtr parentContent = {}, ContentPtr content = {},
                       SkinPtr skin = {}, UseCounterPtr useCounter = {})
        : EventDialog(std::move(context), std::move(summary), Type,
                      std::move(parent), std::move(parentContent),
                      std::move(content), std::move(skin),
                      std::move(useCounter)) {
        if (draft_) value_ = readValue(*draft_);
        else if constexpr (Type == CType::BranchRandom) value_ = 50;
        else if constexpr (Type == CType::BranchPartyNumber) value_ = 1;
    }
    [[nodiscard]] int value() const noexcept { return value_; }
    void setValue(int value) noexcept { value_ = value; }
    [[nodiscard]] int minimum() const noexcept { return minimum_; }
    [[nodiscard]] int maximum() const noexcept { return maximum_; }
    void setRange(int minimum, int maximum) noexcept {
        minimum_ = minimum;
        maximum_ = maximum;
    }
    void writeEditedFields(const ContentPtr& value) const override {
        if constexpr (Type == CType::Wait) value->wait(value_);
        else if constexpr (Type == CType::BranchRandom) value->percent(value_);
        else if constexpr (Type == CType::BranchPartyNumber)
            value->partyNumber(value_);
        else if constexpr (Type == CType::BranchMoney ||
                           Type == CType::GetMoney || Type == CType::LoseMoney)
            value->money(value_);
    }
    bool apply() override {
        writeEditedFields(ensureDraft());
        return commitDraft();
    }

private:
    void setupBody() override { buildControls(); }
    void buildControls();
    static int readValue(const Content& value) {
        if constexpr (Type == CType::Wait) return value.wait();
        else if constexpr (Type == CType::BranchRandom) return value.percent();
        else if constexpr (Type == CType::BranchPartyNumber) return value.partyNumber();
        else return value.money();
    }

    int value_{};
    int minimum_{};
    int maximum_{};
};

using WaitEventDialog = NumericEventDialog<CType::Wait>;
using BrRandomEventDialog = NumericEventDialog<CType::BranchRandom>;
using BrNumEventDialog = NumericEventDialog<CType::BranchPartyNumber>;
template <CType Type>
using MoneyEventDialog = NumericEventDialog<Type>;

template <CType Type, typename F = void, bool SelValue = false>
class FlagStepDialog final : public EventDialog {
public:
    FlagStepDialog(Context context, SummaryPtr summary, Parent parent = {},
                   ContentPtr parentContent = {}, ContentPtr content = {},
                   SkinPtr skin = {}, UseCounterPtr useCounter = {})
        : EventDialog(std::move(context), std::move(summary), Type,
                      std::move(parent), std::move(parentContent),
                      std::move(content), std::move(skin),
                      std::move(useCounter)) {
        loadDraft();
    }
    [[nodiscard]] const chooser::FlagChooserBase* variableChooser() const noexcept {
        return flag_chooser_.get();
    }
    [[nodiscard]] const String& selected() const noexcept { return selected_; }
    void setSelected(String value) {
        selected_ = std::move(value);
        if constexpr (Type == CType::SetFlag) {
            if (flag_chooser_) flag_chooser_->selected(selected_);
            refreshFlagValues();
        }
    }
    [[nodiscard]] int selectedValue() const noexcept { return selected_value_; }
    void setSelectedValue(int value) noexcept {
        selected_value_ = value;
        if constexpr (Type == CType::SetFlag) refreshFlagValues();
    }
    [[nodiscard]] types::Comparison4 comparison() const noexcept {
        return comparison_;
    }
    void setComparison(types::Comparison4 value) noexcept { comparison_ = value; }
    void setCardSpeed(int value) noexcept {
        card_speed_ = value;
        if (animation_control_) animation_control_->speed(value);
    }
    void setOverrideCardSpeed(bool value) noexcept {
        override_card_speed_ = value;
        if (animation_control_) animation_control_->overrideCardSpeed(value);
    }
    void writeEditedFields(const ContentPtr& value) const override {
        if constexpr (Type == CType::BranchFlag || Type == CType::SetFlag ||
                      Type == CType::ReverseFlag || Type == CType::CheckFlag ||
                      Type == CType::BranchFlagCmp) {
            value->flag(selected_);
            if constexpr (SelValue) {
                // Dは値の表の現在行から適用し、行がなければFalse
                // とする。 指定された演算対象を残すのは、表
                // を構築する前のモデルスナップショットだけ。
                const bool flag_value = flag_value_control_
                    ? !flag_value_control_->selection.empty() &&
                          flag_value_control_->selection.front() == 0U
                    : selected_value_ != 0;
                value->flagValue(flag_value);
            }
        } else {
            value->step(selected_);
            if constexpr (SelValue) {
                // Dは値の表が未選択なら添字0として扱う。 UI作成前のモ
                // デルスナップショットでは、キャッシュした演算対象を保つ。
                const int step_value = flag_value_control_
                    ? (flag_value_control_->selection.empty()
                           ? 0
                           : static_cast<int>(flag_value_control_->selection.front()))
                    : selected_value_;
                value->stepValue(step_value);
            }
            if constexpr (Type == CType::CheckStep)
                value->comparison4(comparison_);
        }
        if constexpr (Type == CType::SetFlag || Type == CType::ReverseFlag) {
            value->cardSpeed(card_speed_);
            value->overrideCardSpeed(override_card_speed_);
        }
    }
    void refExternalFlagAndStep() override;
    void delExternalFlagAndStep(bool flagDeleted, bool stepDeleted) override;
    bool apply() override {
        if (variables_retired_) return false;
        writeEditedFields(ensureDraft());
        return commitDraft();
    }

protected:
    void refreshWarning() override;

private:
    bool variables_retired_{};
    void setupBody() override { buildControls(); }
    void buildControls();
    void refreshFlagValues();
    void loadDraft() {
        if (!draft_) return;
        if constexpr (Type == CType::BranchFlag || Type == CType::SetFlag ||
                      Type == CType::ReverseFlag || Type == CType::CheckFlag ||
                      Type == CType::BranchFlagCmp)
            selected_ = draft_->flag();
        else
            selected_ = draft_->step();
        if constexpr (SelValue) {
            selected_value_ = (Type == CType::BranchFlag ||
                               Type == CType::SetFlag || Type == CType::ReverseFlag)
                                  ? (draft_->flagValue() ? 1 : 0)
                                  : draft_->stepValue();
        }
        if constexpr (Type == CType::CheckStep) comparison_ = draft_->comparison4();
        if constexpr (Type == CType::SetFlag || Type == CType::ReverseFlag) {
            card_speed_ = draft_->cardSpeed();
            override_card_speed_ = draft_->overrideCardSpeed();
        }
    }

    String selected_{};
    String selected_with_dir_{};
    // Dは新しく選んだ変数の最初の表示値を選ぶ。Flagの
    // 行はTRUE/FALSE順だが、 保存値はbool
    // 相当の1/0。Stepの行は0始まりの添字を使う。
    int selected_value_{Type == CType::SetFlag ? 1 : 0};
    types::Comparison4 comparison_{types::Comparison4::Eq};
    int card_speed_{-1};
    bool override_card_speed_{};
    std::unique_ptr<chooser::FlagChooserBase> flag_chooser_{};
    std::shared_ptr<lib::TableView> flag_value_control_{};
    std::shared_ptr<lib::SplitView> flag_split_{};
    std::shared_ptr<lib::Label> step_comparison_label_{};
    std::shared_ptr<spcarddialog::CardAnimationPanel> animation_control_{};
};

using BrFlagDialog = FlagStepDialog<CType::BranchFlag, void, false>;
using BrStepNDialog = FlagStepDialog<CType::BranchMultiStep, void, false>;
using BrStepULDialog = FlagStepDialog<CType::BranchStep, void, true>;
using FlagSetDialog = FlagStepDialog<CType::SetFlag, void, true>;
using StepSetDialog = FlagStepDialog<CType::SetStep, void, true>;
using StepPlusDialog = FlagStepDialog<CType::SetStepUp, void, false>;
using StepMinusDialog = FlagStepDialog<CType::SetStepDown, void, false>;
using FlagRDialog = FlagStepDialog<CType::ReverseFlag, void, false>;
using FlagJudgeDialog = FlagStepDialog<CType::CheckFlag, void, false>;
using CheckStepDialog = FlagStepDialog<CType::CheckStep, void, true>;

template <CType Type, typename F = void, bool Random = false>
class FlagStepCombiDialog final : public EventDialog {
public:
    FlagStepCombiDialog(Context context, SummaryPtr summary, Parent parent = {},
                        ContentPtr parentContent = {}, ContentPtr content = {},
                        SkinPtr skin = {}, UseCounterPtr useCounter = {})
        : EventDialog(std::move(context), std::move(summary), Type,
                      std::move(parent), std::move(parentContent),
                      std::move(content), std::move(skin),
                      std::move(useCounter)) {
        if (draft_) {
            if constexpr (Type == CType::SubstituteFlag ||
                          Type == CType::BranchFlagCmp)
                first_ = draft_->flag();
            else
                first_ = draft_->step();
            if constexpr (Type == CType::SubstituteFlag ||
                          Type == CType::BranchFlagCmp)
                second_ = draft_->flag2();
            else
                second_ = draft_->step2();
            if constexpr (Type == CType::SubstituteFlag) {
                card_speed_ = draft_->cardSpeed();
                override_card_speed_ = draft_->overrideCardSpeed();
            }
        }
    }
    [[nodiscard]] const String& first() const noexcept { return first_; }
    [[nodiscard]] const String& second() const noexcept { return second_; }
    void setFirst(String value) { first_ = std::move(value); }
    void setSecond(String value) { second_ = std::move(value); }
    void setCardSpeed(int value) noexcept { card_speed_ = value; }
    void setOverrideCardSpeed(bool value) noexcept {
        override_card_speed_ = value;
    }
    [[nodiscard]] const std::shared_ptr<lib::SplitView>& variableSplit() const noexcept {
        return variable_split_;
    }
    void writeEditedFields(const ContentPtr& value) const override {
        if constexpr (Type == CType::SubstituteFlag ||
                      Type == CType::BranchFlagCmp) {
            value->flag(first_);
            value->flag2(second_);
        } else {
            value->step(first_);
            value->step2(second_);
        }
        if constexpr (Type == CType::SubstituteFlag) {
            value->cardSpeed(card_speed_);
            value->overrideCardSpeed(override_card_speed_);
        }
    }
    void refExternalFlagAndStep() override;
    void delExternalFlagAndStep() override;
    bool apply() override {
        if (variables_retired_) return false;
        writeEditedFields(ensureDraft());
        return commitDraft();
    }

private:
    void refreshWarning() override;
    bool variables_retired_{};
    void setupBody() override { buildControls(); }
    void buildControls();
    void refreshVariableValues(bool first);
    String first_{};
    String second_{};
    int card_speed_{-1};
    bool override_card_speed_{};
    std::unique_ptr<chooser::FlagChooserBase> first_chooser_{};
    std::unique_ptr<chooser::FlagChooserBase> second_chooser_{};
    std::shared_ptr<lib::TableView> first_values_{};
    std::shared_ptr<lib::TableView> second_values_{};
    std::shared_ptr<lib::SplitView> variable_split_{};
    std::shared_ptr<spcarddialog::CardAnimationPanel> animation_control_{};
};

using SubstituteStepDialog =
    FlagStepCombiDialog<CType::SubstituteStep, void, true>;
using SubstituteFlagDialog =
    FlagStepCombiDialog<CType::SubstituteFlag, void, true>;
using BrStepCmpDialog =
    FlagStepCombiDialog<CType::BranchStepCmp, void, false>;
using BrFlagCmpDialog =
    FlagStepCombiDialog<CType::BranchFlagCmp, void, false>;

class BrMemberDialog final : public EventDialog {
public:
    BrMemberDialog(Context context, SummaryPtr summary, Parent parent = {},
                   ContentPtr parentContent = {}, ContentPtr content = {},
                   UseCounterPtr useCounter = {}, SkinPtr skin = {});
    void setTargetAll(bool value) noexcept { target_all_ = value; }
    void setSelectionMethod(types::SelectionMethod value) noexcept {
        selection_method_ = value;
    }
    void setCoupons(lib::Array<String> value) {
        coupons_ = std::move(value);
        coupon_records_.clear();
        for (const auto& name : coupons_)
            coupon_records_.push_back(std::make_shared<
                ::cwxeditor::cwx::coupon::Coupon>(name, 0));
    }
    void setInitValue(int value) noexcept { init_value_ = value; }
    [[nodiscard]] bool targetAll() const noexcept { return target_all_; }
    [[nodiscard]] types::SelectionMethod selectionMethod() const noexcept {
        return selection_method_;
    }
    [[nodiscard]] const lib::Array<String>& coupons() const noexcept {
        return coupons_;
    }
    [[nodiscard]] int initValue() const noexcept { return init_value_; }
    void refDataVersion() override;
    void refreshWarning() override;
    bool apply() override;

private:
    void setupBody() override;
    void refreshMemberControls();
    bool target_all_{};
    types::SelectionMethod selection_method_{types::SelectionMethod::Manual};
    lib::Array<String> coupons_{};
    lib::Array<lib::Owner<::cwxeditor::cwx::coupon::Coupon>> coupon_records_{};
    int init_value_{1};
    lib::Array<std::shared_ptr<lib::RadioButton>> target_buttons_{};
    lib::Array<std::shared_ptr<lib::RadioButton>> method_buttons_{};
    std::shared_ptr<couponview::CouponView<couponview::CVType::Valued>> coupon_view_{};
    std::shared_ptr<lib::NumberEdit> init_value_editor_{};
};

class BrPowerDialog final : public EventDialog {
public:
    BrPowerDialog(Context context, SummaryPtr summary, Parent parent = {},
                  ContentPtr parentContent = {}, ContentPtr content = {},
                  SkinPtr skin = {}, UseCounterPtr useCounter = {});
    void setSignedLevel(int value) noexcept { signed_level_ = value; }
    void setTarget(types::Target value) noexcept { target_ = value; }
    void setPhysical(types::Physical value) noexcept {
        physical_ = value;
        if (ability_control_) ability_control_->physical(value);
    }
    void setMental(types::Mental value) noexcept {
        mental_ = value;
        if (ability_control_) ability_control_->mental(value);
    }
    void setInvertResult(bool value) noexcept { invert_result_ = value; }
    void writeEditedFields(const ContentPtr& value) const override;
    bool apply() override;

private:
    void setupBody() override { buildControls(); }
    void buildControls();
    void refreshWarning() override;
    int signed_level_{};
    types::Target target_{};
    types::Physical physical_{types::Physical::Min};
    types::Mental mental_{types::Mental::Aggressive};
    bool invert_result_{true};
    std::unique_ptr<abilityview::AbilityView> ability_control_{};
    std::shared_ptr<lib::CheckBox> invert_result_control_{};
};

class BrLevelDialog final : public EventDialog {
public:
    BrLevelDialog(Context context, SummaryPtr summary, Parent parent = {},
                  ContentPtr parentContent = {}, ContentPtr content = {},
                  SkinPtr skin = {}, UseCounterPtr useCounter = {})
        : EventDialog(std::move(context), std::move(summary),
                      CType::BranchLevel, std::move(parent),
                      std::move(parentContent), std::move(content),
                      std::move(skin), std::move(useCounter)) {
        if (draft_) {
            average_ = draft_->average();
            unsigned_level_ = draft_->unsignedLevel();
        }
    }
    void setAverage(bool value) noexcept { average_ = value; }
    void setUnsignedLevel(int value) noexcept { unsigned_level_ = value; }
    void writeEditedFields(const ContentPtr& value) const override;
    bool apply() override;

private:
    void setupBody() override { buildControls(); }
    void buildControls();
    bool average_{};
    int unsigned_level_{1};
};

class BrStateDialog final : public EventDialog {
public:
    BrStateDialog(Context context, SummaryPtr summary, Parent parent = {},
                  ContentPtr parentContent = {}, ContentPtr content = {},
                  UseCounterPtr useCounter = {}, SkinPtr skin = {})
        : EventDialog(std::move(context), std::move(summary),
                      CType::BranchStatus, std::move(parent),
                      std::move(parentContent), std::move(content),
                      std::move(skin), std::move(useCounter)),
          range_(context_, summary_, use_counter_,
                 {types::Range::Selected, types::Range::Random,
                  types::Range::Party},
                 {}, CType::BranchStatus, draft_) {
        if (draft_) {
            status_ = draft_->status();
            invert_result_ = draft_->invertResult();
        }
    }
    RangePanel& rangePanel() noexcept { return range_; }
    void setStatus(types::Status value) noexcept { status_ = value; }
    void setInvertResult(bool value) noexcept { invert_result_ = value; }
    void writeEditedFields(const ContentPtr& value) const override;
    bool apply() override;

private:
    void setupBody() override { buildControls(); }
    void buildControls();
    void refreshWarning() override;
    RangePanel range_;
    types::Status status_{types::Status::Active};
    bool invert_result_{};
};

template <CType Type, typename C = void, bool Delete = false,
          types::Range RangeDef = types::Range::Field>
class CardEventDialog final : public EventDialog {
public:
    ~CardEventDialog() override {
        card_lifetime_.reset();
        for (const auto& disconnect : card_disconnect_) disconnect();
        if (card_search_) { card_search_->modEvent.clear(); card_search_->close(); }
        if (card_menu_) { card_menu_->on_command = {}; card_menu_->on_open = {}; card_menu_->items.clear(); }
        if (card_table_) { card_table_->on_selection_changed = {}; card_table_->on_cell_activate = {};
            card_table_->on_column_resized = {}; card_table_->on_pointer = {}; card_table_->on_key_down = {}; card_table_->on_text_input = {}; }
    }
    CardEventDialog(Context context, SummaryPtr summary, Parent parent = {},
                    ContentPtr parentContent = {}, ContentPtr content = {},
                    SkinPtr skin = {}, UseCounterPtr useCounter = {})
        : EventDialog(std::move(context), std::move(summary), Type,
                      std::move(parent), std::move(parentContent),
                      std::move(content), std::move(skin),
                      std::move(useCounter)) {
        if (draft_) {
            if constexpr (Type == CType::BranchSkill || Type == CType::GetSkill ||
                          Type == CType::LoseSkill)
                selected_id_ = draft_->skill();
            else if constexpr (Type == CType::BranchItem || Type == CType::GetItem ||
                               Type == CType::LoseItem)
                selected_id_ = draft_->item();
            else
                selected_id_ = draft_->beast();
            range_ = draft_->range();
            card_number_ = draft_->cardNumber();
            if constexpr (Delete) {
                // Dと同じく「全て削除」はcardNumber
                // == 0で保存し、スピナー自身の値は1に保つ。
                delete_all_ = card_number_ == 0;
                if (delete_all_) card_number_ = 1;
            }
            select_card_ = draft_->selectCard();
            invert_result_ = draft_->invertResult();
        } else {
            range_ = RangeDef;
            card_number_ = 1;
        }
    }
    [[nodiscard]] std::uint64_t selectedId() const noexcept { return selected_id_; }
    void setSelectedId(std::uint64_t value) noexcept { selected_id_ = value; }
    [[nodiscard]] types::Range range() const noexcept { return range_; }
    void setRange(types::Range value) noexcept { range_ = value; }
    [[nodiscard]] int cardNumber() const noexcept { return card_number_; }
    void setCardNumber(int value) noexcept { card_number_ = value; }
    void setSelectCard(bool value) noexcept { select_card_ = value; }
    void setInvertResult(bool value) noexcept { invert_result_ = value; }
    [[nodiscard]] const std::shared_ptr<lib::TableView>& cardTable() const noexcept {
        return card_table_;
    }
    [[nodiscard]] const std::shared_ptr<incsearch::IncSearch>& cardSearch() const noexcept { return card_search_; }
    void writeEditedFields(const ContentPtr& value) const override {
        if constexpr (Type == CType::BranchSkill || Type == CType::GetSkill ||
                      Type == CType::LoseSkill)
            value->skill(selected_id_);
        else if constexpr (Type == CType::BranchItem || Type == CType::GetItem ||
                           Type == CType::LoseItem)
            value->item(selected_id_);
        else
            value->beast(selected_id_);
        value->range(range_);
        if constexpr (Delete) value->cardNumber(delete_all_ ? 0 : card_number_);
        else value->cardNumber(card_number_);
        if constexpr (Type == CType::BranchSkill || Type == CType::BranchItem ||
                      Type == CType::BranchBeast) {
            value->selectCard(select_card_);
            value->invertResult(invert_result_);
        }
    }
    bool apply() override {
        // モデル通知で一覧は既に更新されている。 確定時に再構成
        // して、絞り込み・空表示の中の有効な選択IDを消さない。
        writeEditedFields(ensureDraft());
        return commitDraft();
    }
    void setDeleteAll(bool value) noexcept { delete_all_ = value; }
    void refDataVersion() override {
        const auto keep_alive = weak_from_this().lock();
        EventDialog::refDataVersion();
        if (!keep_alive || !keep_alive->isOpen()) return;
        if constexpr (Delete) {
            // Dは旧バージョンへの更新で表が空になると削除ダイアログを閉じ
            // る。 C++の逐次検索中も、同じ描画行に絞り込みを適用する。
            if (summary_ && summary_->legacy() && card_table_ &&
                card_table_->rows.empty()) forceCancel();
        }
    }

private:
    void setupBody() override { buildControls(); }
    void buildControls();
    void refreshWarning() override;
    void refreshCardList(bool deleted = false, bool notify = true);
    std::shared_ptr<void> card_lifetime_{std::make_shared<int>(0)};
    std::vector<lib::Action> card_disconnect_;
    lib::Action refresh_card_enabled_;
    std::shared_ptr<incsearch::IncSearch> card_search_;
    std::shared_ptr<lib::Menu> card_menu_;
    std::size_t card_count_{};
    std::uint64_t selected_id_{};
    types::Range range_{RangeDef};
    int card_number_{1};
    bool delete_all_{};
    bool select_card_{};
    bool invert_result_{};
    std::shared_ptr<lib::TableView> card_table_{};
};

using BrSkillDialog =
    CardEventDialog<CType::BranchSkill, void, false, types::Range::Field>;
using BrItemDialog =
    CardEventDialog<CType::BranchItem, void, false, types::Range::Field>;
using BrBeastDialog =
    CardEventDialog<CType::BranchBeast, void, false, types::Range::Field>;
using GetSkillDialog =
    CardEventDialog<CType::GetSkill, void, false, types::Range::Selected>;
using GetItemDialog =
    CardEventDialog<CType::GetItem, void, false, types::Range::Selected>;
using GetBeastDialog =
    CardEventDialog<CType::GetBeast, void, false, types::Range::Selected>;
using LostSkillDialog =
    CardEventDialog<CType::LoseSkill, void, true, types::Range::Field>;
using LostItemDialog =
    CardEventDialog<CType::LoseItem, void, true, types::Range::Field>;
using LostBeastDialog =
    CardEventDialog<CType::LoseBeast, void, true, types::Range::Field>;

class EffectDialog final : public EventDialog {
public:
    EffectDialog(Context context, SummaryPtr summary, Parent parent = {},
                 ContentPtr parentContent = {}, ContentPtr content = {},
                 UseCounterPtr useCounter = {}, SkinPtr skin = {});
    ~EffectDialog() override;
    [[nodiscard]] lib::Array<MotionPtr>& motions() noexcept { return motions_; }
    void setMotions(lib::Array<MotionPtr> value) {
        motions_ = std::move(value);
        if (motion_view_) motion_view_->motions(motions_);
    }
    void setSignedLevel(int value) noexcept {
        signed_level_ = value;
        if (signed_level_control_) signed_level_control_->value = value;
    }
    void setInitialEffect(bool value) noexcept {
        initial_effect_ = value;
        if (initial_effect_control_) initial_effect_control_->checked = value;
        refreshEffectEnabled();
    }
    void setInitialSoundPath(String value) { initial_sound_path_ = std::move(value); }
    void setInitialSoundVolume(std::uint32_t value) noexcept { initial_sound_volume_ = value; }
    void setInitialSoundLoopCount(std::uint32_t value) noexcept { initial_sound_loop_count_ = value; }
    void setInitialSoundChannel(std::uint32_t value) noexcept { initial_sound_channel_ = value; }
    void setInitialSoundFadeIn(std::uint32_t value) noexcept { initial_sound_fade_in_ = value; }
    void setSoundPath(String value) { sound_path_ = std::move(value); }
    void setSoundVolume(std::uint32_t value) noexcept { sound_volume_ = value; }
    void setSoundLoopCount(std::uint32_t value) noexcept { sound_loop_count_ = value; }
    void setSoundChannel(std::uint32_t value) noexcept { sound_channel_ = value; }
    void setSoundFadeIn(std::uint32_t value) noexcept { sound_fade_in_ = value; }
    void setSuccessRate(int value) noexcept { success_rate_ = value; }
    void setEffectType(types::EffectType value) noexcept { effect_type_ = value; }
    void setResist(types::Resist value) noexcept { resist_ = value; }
    void setCardVisual(types::CardVisual value) noexcept { card_visual_ = value; }
    RangePanel& rangePanel() noexcept { return range_; }
    void setRefAbility(bool value) noexcept {
        ref_ability_ = value;
        if (ref_ability_control_) ref_ability_control_->checked = value;
        refreshEffectEnabled();
    }
    void setPhysical(types::Physical value) noexcept {
        physical_ = value;
        if (ability_control_) ability_control_->physical(value);
    }
    void setMental(types::Mental value) noexcept {
        mental_ = value;
        if (ability_control_) ability_control_->mental(value);
    }
    void setIgnite(bool value) noexcept {
        ignite_ = value;
        if (ignite_control_) ignite_control_->checked = value;
        refreshEffectEnabled();
    }
    void setKeyCodes(lib::Array<String> value) {
        key_codes_ = std::move(value);
        if (key_code_view_) key_code_view_->setKeyCodes(key_codes_);
        refreshEffectEnabled();
    }
    void setCardSpeed(int value) noexcept {
        card_speed_ = value;
        if (animation_control_) animation_control_->speed(value);
    }
    void setOverrideCardSpeed(bool value) noexcept {
        override_card_speed_ = value;
        if (animation_control_) animation_control_->overrideCardSpeed(value);
    }
    void setAbsorbTo(types::AbsorbTo value) noexcept {
        absorb_to_ = value;
        if (absorb_control_)
            absorb_control_->checked = value == types::AbsorbTo::Selected;
        refreshEffectEnabled();
    }
    bool openCWXPath(String path, bool activate) override;
    void refSkin() override;
    bool apply() override;

private:
    void refExternalUndoMax() override;
    void loadDraft();
    void setupBody() override { buildControls(); }
    void buildControls();
    void refreshMotionTable();
    void refreshWarning() override;
    void refDataVersion() override;
    void refreshEffectEnabled();
    lib::Array<MotionPtr> motions_{};
    int signed_level_{};
    bool initial_effect_{};
    String initial_sound_path_{};
    std::uint32_t initial_sound_volume_{100};
    std::uint32_t initial_sound_loop_count_{1};
    std::uint32_t initial_sound_channel_{};
    std::uint32_t initial_sound_fade_in_{};
    String sound_path_{};
    std::uint32_t sound_volume_{100};
    std::uint32_t sound_loop_count_{1};
    std::uint32_t sound_channel_{};
    std::uint32_t sound_fade_in_{};
    int success_rate_{successRateMax};
    types::EffectType effect_type_{types::EffectType::None};
    types::Resist resist_{types::Resist::Unfail};
    types::CardVisual card_visual_{types::CardVisual::None};
    std::shared_ptr<int> key_host_lifetime_{std::make_shared<int>(0)};
    RangePanel range_;
    std::vector<std::pair<types::Range, std::shared_ptr<lib::RadioButton>>> range_controls_{};
    String holding_coupon_{};
    bool ref_ability_{};
    types::Physical physical_{types::Physical::Min};
    types::Mental mental_{types::Mental::Aggressive};
    bool ignite_{};
    lib::Array<String> key_codes_{};
    int card_speed_{-1};
    bool override_card_speed_{};
    types::AbsorbTo absorb_to_{types::AbsorbTo::None};
    std::shared_ptr<lib::TableView> motion_table_{};
    std::shared_ptr<keycodeview::KeyCodeView> key_code_view_{};
    std::unique_ptr<motionview::MotionView> motion_view_{};
    std::size_t motion_mod_connection_{};
    std::size_t motion_warning_connection_{};
    std::uint64_t motion_ref_beast_connection_{};
    std::uint64_t motion_del_beast_connection_{};
    std::uint64_t motion_ref_image_scale_connection_{};
    std::uint64_t motion_ref_card_image_status_connection_{};
    std::uint64_t motion_ref_element_connection_{};
    std::unique_ptr<abilityview::AbilityView> ability_control_{};
    std::shared_ptr<spcarddialog::CardAnimationPanel> animation_control_{};
    std::shared_ptr<lib::NumberEdit> signed_level_control_{};
    std::shared_ptr<lib::CheckBox> ref_ability_control_{};
    std::shared_ptr<lib::CheckBox> ignite_control_{};
    std::shared_ptr<lib::CheckBox> initial_effect_control_{};
    std::shared_ptr<lib::CheckBox> absorb_control_{};
    std::shared_ptr<lib::ComboBox> holding_coupon_control_{};
    std::shared_ptr<lib::ComboBox> holding_coupon_kind_control_{};
    std::shared_ptr<lib::Container> key_code_group_{};
    std::shared_ptr<lib::SplitView> ignite_split_{};
    std::vector<lib::WidgetPtr> initial_sound_controls_{};
    std::unique_ptr<effectcarddialog::SoundSelect> initial_sound_select_{};
    std::unique_ptr<effectcarddialog::SoundSelect> sound_select_{};
    std::vector<std::shared_ptr<lib::ToolBar>> motion_category_tools_{};
};

class MessageEventDialog final : public EventDialog {
public:
    MessageEventDialog(Context context, SummaryPtr summary, Parent parent = {},
                       ContentPtr parentContent = {}, ContentPtr content = {},
                       SkinPtr skin = {}, UseCounterPtr useCounter = {});
    bool apply() override;
    void refSkin() override;
    void refDataVersion() override;
    bool openCWXPath(String path, bool activate) override;
    [[nodiscard]] messageutils::MessageDialog* editor() const noexcept {
        return editor_.get();
    }

private:
    void setupBody() override;
    void refreshWarning() override;
    void refExternalPath(StringView oldPath, StringView newPath,
                         bool isDirectory) override;
    void refExternalPaths(StringView parentPath) override;
    void refExternalFlagAndStep() override;
    void refExternalImageScale() override;
    void refExternalUndoMax() override;
    std::unique_ptr<messageutils::MessageDialog> editor_{};
};

class SpeakEventDialog final : public EventDialog {
public:
    SpeakEventDialog(Context context, SummaryPtr summary, Parent parent = {},
                     ContentPtr parentContent = {}, ContentPtr content = {},
                     SkinPtr skin = {}, UseCounterPtr useCounter = {});
    ~SpeakEventDialog() override;
    bool apply() override;
    void refSkin() override;
    void refDataVersion() override;
    bool openCWXPath(String path, bool activate) override;

private:
    void setupBody() override;
    void refreshWarning() override;
    void refExternalPath(StringView oldPath, StringView newPath,
                         bool isDirectory) override;
    void refExternalPaths(StringView parentPath) override;
    void refExternalFlagAndStep() override;
    void refExternalImageScale() override;
    void refExternalUndoMax() override;
    std::unique_ptr<messageutils::SpeakDialog> editor_{};
};

class ShowHidePartyDialog final : public EventDialog {
public:
    ShowHidePartyDialog(CType type, Context context, SummaryPtr summary,
                        Parent parent = {}, ContentPtr parentContent = {},
                        ContentPtr content = {}, SkinPtr skin = {},
                        UseCounterPtr useCounter = {})
        : EventDialog(std::move(context), std::move(summary), type,
                      std::move(parent), std::move(parentContent),
                      std::move(content), std::move(skin),
                      std::move(useCounter)) {
        if (draft_) card_speed_ = draft_->cardSpeed();
    }
    void setCardSpeed(int value) noexcept { card_speed_ = value; }
    [[nodiscard]] int cardSpeed() const noexcept { return card_speed_; }
    void writeEditedFields(const ContentPtr& value) const override;
    bool apply() override;

private:
    void refreshWarning() override;
    void setupBody() override { buildControls(); }
    void buildControls();
    int card_speed_{-1};
    std::shared_ptr<spcarddialog::CardAnimationPanel> animation_control_{};
};

class RefreshDialog final : public EventDialog {
public:
    RefreshDialog(Context context, SummaryPtr summary, Parent parent = {},
                  ContentPtr parentContent = {}, ContentPtr content = {},
                  SkinPtr skin = {}, UseCounterPtr useCounter = {})
        : EventDialog(std::move(context), std::move(summary), CType::Redisplay,
                      std::move(parent), std::move(parentContent),
                      std::move(content), std::move(skin),
                      std::move(useCounter)),
          transition_(context_, summary_, draft_) {
    }
    TransitionPanel& transitionPanel() noexcept { return transition_; }
    void writeEditedFields(const ContentPtr& value) const override;
    bool apply() override;

private:
    void refreshWarning() override;
    void setupBody() override { buildControls(); }
    void buildControls();
    TransitionPanel transition_;
};

class BrRandomSelectDialog final : public EventDialog {
public:
    BrRandomSelectDialog(Context context, SummaryPtr summary, Parent parent = {},
                         ContentPtr parentContent = {}, ContentPtr content = {},
                         SkinPtr skin = {}, UseCounterPtr useCounter = {})
        : EventDialog(std::move(context), std::move(summary),
                      CType::BranchRandomSelect, std::move(parent),
                      std::move(parentContent), std::move(content),
                      std::move(skin), std::move(useCounter)) {
        if (draft_) {
            cast_range_ = draft_->castRange();
            level_min_ = draft_->levelMin();
            level_max_ = draft_->levelMax();
            status_ = draft_->status();
            invert_result_ = draft_->invertResult();
            has_level_ = level_max_ != 0;
            has_status_ = status_ != types::Status::None;
        } else {
            cast_range_.push_back(types::CastRange::Party);
        }
        // 編集値は、有効になっているフィルターと独立して保持する。
        if (!has_level_) level_min_ = level_max_ = 1;
        if (!has_status_) status_ = types::Status::Active;
    }
    void setCastRange(lib::Array<types::CastRange> value) { cast_range_ = std::move(value); }
    void setHasLevel(bool value) noexcept { has_level_ = value; }
    void setLevelMin(int value) noexcept { level_min_ = value; }
    void setLevelMax(int value) noexcept { level_max_ = value; }
    void setHasStatus(bool value) noexcept { has_status_ = value; }
    void setStatus(types::Status value) noexcept { status_ = value; }
    void setInvertResult(bool value) noexcept { invert_result_ = value; }
    void writeEditedFields(const ContentPtr& value) const override {
        value->castRange(cast_range_);
        value->levelMin(has_level_ ? (level_max_ != 0 && level_min_ > level_max_ ? level_max_ : level_min_) : 0);
        value->levelMax(has_level_ ? level_max_ : 0);
        value->status(has_status_ ? status_ : types::Status::None);
        value->invertResult(invert_result_);
    }
    bool apply() override;

private:
    void refreshWarning() override;
    void setupBody() override { buildControls(); }
    void buildControls();
    lib::Array<types::CastRange> cast_range_{};
    bool has_level_{};
    bool has_status_{};
    int level_min_{};
    int level_max_{};
    types::Status status_{types::Status::Active};
    bool invert_result_{};
};

class BrKeyCodeDialog final : public EventDialog {
public:
    BrKeyCodeDialog(Context context, SummaryPtr summary, Parent parent = {},
                    ContentPtr parentContent = {}, ContentPtr content = {},
                    SkinPtr skin = {}, UseCounterPtr useCounter = {})
        : EventDialog(std::move(context), std::move(summary),
                      CType::BranchKeyCode, std::move(parent),
                      std::move(parentContent), std::move(content),
                      std::move(skin), std::move(useCounter)) {
        if (draft_) {
            range_ = draft_->keyCodeRange();
            target_skill_ = draft_->targetIsSkill();
            target_item_ = draft_->targetIsItem();
            target_beast_ = draft_->targetIsBeast();
            target_hand_ = draft_->targetIsHand();
            key_code_ = draft_->keyCode();
            matching_condition_ = draft_->matchingCondition();
            invert_result_ = draft_->invertResult();
            select_card_ = draft_->selectCard();
        } else {
            target_skill_ = target_item_ = target_beast_ = true;
            target_hand_ = summary_ && summary_->legacy();
        }
    }
    void setRange(types::Range value) noexcept { range_ = value; }
    void setTargetTypes(bool skill, bool item, bool beast, bool hand) noexcept {
        target_skill_ = skill;
        target_item_ = item;
        target_beast_ = beast;
        target_hand_ = hand;
    }
    void setKeyCode(String value) { key_code_ = std::move(value); }
    void setMatchingCondition(types::MatchingCondition value) noexcept {
        matching_condition_ = value;
    }
    void setInvertResult(bool value) noexcept { invert_result_ = value; }
    void setSelectCard(bool value) noexcept { select_card_ = value; }
    void writeEditedFields(const ContentPtr& value) const override;
    bool apply() override;

private:
    void refreshWarning() override;
    void setupBody() override { buildControls(); }
    void buildControls();
    types::Range range_{types::Range::Selected};
    bool target_skill_{true};
    bool target_item_{true};
    bool target_beast_{true};
    bool target_hand_{};
    String key_code_{};
    types::MatchingCondition matching_condition_{types::MatchingCondition::Has};
    bool invert_result_{};
    bool select_card_{};
};

class BranchRoundDialog final : public EventDialog {
public:
    BranchRoundDialog(Context context, SummaryPtr summary, Parent parent = {},
                      ContentPtr parentContent = {}, ContentPtr content = {},
                      SkinPtr skin = {}, UseCounterPtr useCounter = {})
        : EventDialog(std::move(context), std::move(summary),
                      CType::BranchRound, std::move(parent),
                      std::move(parentContent), std::move(content),
                      std::move(skin), std::move(useCounter)) {
        if (draft_) {
            round_ = draft_->round();
            comparison_ = draft_->comparison3();
        }
    }
    void setRound(std::uint32_t value) noexcept { round_ = value; }
    void setComparison(types::Comparison3 value) noexcept { comparison_ = value; }
    void writeEditedFields(const ContentPtr& value) const override;
    bool apply() override;

private:
    void refreshWarning() override;
    void setupBody() override { buildControls(); }
    void buildControls();
    std::uint32_t round_{};
    types::Comparison3 comparison_{types::Comparison3::Eq};
};

class MoveBgImageDialog final : public EventDialog {
public:
    MoveBgImageDialog(Context context, SummaryPtr summary, Parent parent = {},
                      ContentPtr parentContent = {}, ContentPtr content = {},
                      SkinPtr skin = {}, UseCounterPtr useCounter = {})
        : EventDialog(std::move(context), std::move(summary),
                      CType::MoveBgImage, std::move(parent),
                      std::move(parentContent), std::move(content),
                      std::move(skin), std::move(useCounter)),
          transition_(context_, summary_, draft_) {
        if (draft_) {
            cell_name_ = draft_->cellName();
            position_type_ = draft_->positionType();
            x_ = draft_->x();
            y_ = draft_->y();
            size_type_ = draft_->sizeType();
            width_ = draft_->width();
            height_ = draft_->height();
            do_anime_ = draft_->doAnime();
            ignore_effect_booster_ = draft_->ignoreEffectBooster();
        } else {
            do_anime_ = true;
        }
    }
    void setCellName(String value) { cell_name_ = std::move(value); }
    void setPosition(types::CoordinateType value) noexcept { position_type_ = value; }
    void setPositionValue(int x, int y) noexcept { x_ = x; y_ = y; }
    void setSize(types::CoordinateType value) noexcept { size_type_ = value; }
    void setSizeValue(int width, int height) noexcept { width_ = width; height_ = height; }
    void setDoAnime(bool value) noexcept { do_anime_ = value; }
    void setIgnoreEffectBooster(bool value) noexcept { ignore_effect_booster_ = value; }
    TransitionPanel& transitionPanel() noexcept { return transition_; }
    void writeEditedFields(const ContentPtr& value) const override;
    bool apply() override;

private:
    void refreshWarning() override;
    void setupBody() override { buildControls(); }
    void buildControls();
    String cell_name_{};
    types::CoordinateType position_type_{types::CoordinateType::None};
    int x_{};
    int y_{};
    types::CoordinateType size_type_{types::CoordinateType::None};
    int width_{};
    int height_{};
    TransitionPanel transition_;
    bool do_anime_{true};
    bool ignore_effect_booster_{};
};

class MoveCardDialog final : public EventDialog {
public:
    MoveCardDialog(Context context, SummaryPtr summary, Parent parent = {},
                   ContentPtr parentContent = {}, ContentPtr content = {},
                   SkinPtr skin = {}, UseCounterPtr useCounter = {})
        : EventDialog(std::move(context), std::move(summary),
                      CType::MoveCard, std::move(parent),
                      std::move(parentContent), std::move(content),
                      std::move(skin), std::move(useCounter)) {
        if (draft_) {
            card_group_ = draft_->cardGroup();
            position_type_ = draft_->positionType();
            x_ = draft_->x();
            y_ = draft_->y();
            scale_ = draft_->scale();
            layer_ = draft_->layer();
            change_scale_ = scale_ != -1;
            change_layer_ = layer_ != -1;
            if (!change_scale_) scale_ = 100;
            if (!change_layer_) layer_ = types::LAYER_MENU_CARD;
            card_speed_ = draft_->cardSpeed();
            override_card_speed_ = draft_->overrideCardSpeed();
        } else {
            scale_ = 100;
            layer_ = types::LAYER_MENU_CARD;
        }
    }
    void setCardGroup(String value) { card_group_ = std::move(value); }
    void setPosition(types::CoordinateType value) noexcept { position_type_ = value; }
    void setPositionValue(int x, int y) noexcept { x_ = x; y_ = y; }
    void setScale(int value) noexcept { scale_ = value; }
    void setLayer(int value) noexcept { layer_ = value; }
    void setChangeScale(bool value) noexcept { change_scale_ = value; }
    void setChangeLayer(bool value) noexcept { change_layer_ = value; }
    void setCardSpeed(int value) noexcept { card_speed_ = value; }
    void setOverrideCardSpeed(bool value) noexcept { override_card_speed_ = value; }
    void writeEditedFields(const ContentPtr& value) const override;
    bool apply() override;

private:
    void setupBody() override { buildControls(); }
    void buildControls();
    void refreshWarning() override;
    String card_group_{};
    types::CoordinateType position_type_{types::CoordinateType::None};
    int x_{};
    int y_{};
    int scale_{100};
    int layer_{types::LAYER_MENU_CARD};
    int card_speed_{-1};
    bool override_card_speed_{};
    bool change_scale_{};
    bool change_layer_{};
    std::shared_ptr<spcarddialog::CardAnimationPanel> animation_control_{};
};

class BranchMultiCouponDialog final : public EventDialog {
public:
    BranchMultiCouponDialog(Context context, SummaryPtr summary,
                            Parent parent = {}, ContentPtr parentContent = {},
                            ContentPtr content = {}, SkinPtr skin = {},
                            UseCounterPtr useCounter = {})
        : EventDialog(std::move(context), std::move(summary),
                      CType::BranchMultiCoupon, std::move(parent),
                      std::move(parentContent), std::move(content),
                      std::move(skin), std::move(useCounter)),
          range_(context_, summary_, use_counter_,
                 {types::Range::Selected, types::Range::Random,
                  types::Range::Party, types::Range::Field,
                  types::Range::Npc},
                 {}, CType::BranchMultiCoupon, draft_) {
        if (draft_) expand_sp_chars_ = draft_->expandSPChars();
    }
    RangePanel& rangePanel() noexcept { return range_; }
    void setExpandSPChars(bool value) noexcept { expand_sp_chars_ = value; }
    void writeEditedFields(const ContentPtr& value) const override;
    bool apply() override;

private:
    void refreshWarning() override;
    void setupBody() override { buildControls(); }
    void buildControls();
    RangePanel range_;
    bool expand_sp_chars_{};
};

class EffectBreakDialog final : public EventDialog {
public:
    EffectBreakDialog(Context context, SummaryPtr summary, Parent parent = {},
                      ContentPtr parentContent = {}, ContentPtr content = {},
                      SkinPtr skin = {}, UseCounterPtr useCounter = {})
        : EventDialog(std::move(context), std::move(summary),
                      CType::EffectBreak, std::move(parent),
                      std::move(parentContent), std::move(content),
                      std::move(skin), std::move(useCounter)) {
        if (draft_) consume_card_ = draft_->consumeCard();
    }
    void setConsumeCard(bool value) noexcept { consume_card_ = value; }
    [[nodiscard]] bool consumeCard() const noexcept { return consume_card_; }
    void writeEditedFields(const ContentPtr& value) const override;
    bool apply() override;
    void refDataVersion() override;

private:
    void setupBody() override { buildControls(); }
    void buildControls();
    void refreshWarning() override;
    bool consume_card_{true};
    std::weak_ptr<lib::CheckBox> consume_card_control_{};
};

class ChangeEnvironmentDialog final : public EventDialog {
public:
    ChangeEnvironmentDialog(Context context, SummaryPtr summary,
                            Parent parent = {}, ContentPtr parentContent = {},
                            ContentPtr content = {}, SkinPtr skin = {},
                            UseCounterPtr useCounter = {})
        : EventDialog(std::move(context), std::move(summary),
                      CType::ChangeEnvironment, std::move(parent),
                      std::move(parentContent), std::move(content),
                      std::move(skin), std::move(useCounter)) {
        if (draft_) {
            backpack_enabled_ = draft_->backpackEnabled();
            game_over_enabled_ = draft_->gameOverEnabled();
            run_away_enabled_ = draft_->runAwayEnabled();
        }
    }
    void setEnvironment(types::EnvironmentStatus backpack,
                        types::EnvironmentStatus gameOver,
                        types::EnvironmentStatus runAway) noexcept {
        backpack_enabled_ = backpack;
        game_over_enabled_ = gameOver;
        run_away_enabled_ = runAway;
    }
    void writeEditedFields(const ContentPtr& value) const override;
    bool apply() override;

private:
    void setupBody() override { buildControls(); }
    void buildControls();
    void refreshWarning() override;
    types::EnvironmentStatus backpack_enabled_{types::EnvironmentStatus::NotSet};
    types::EnvironmentStatus game_over_enabled_{types::EnvironmentStatus::NotSet};
    types::EnvironmentStatus run_away_enabled_{types::EnvironmentStatus::NotSet};
};

[[nodiscard]] bool hasDialog(CType type, bool existsSummary = true);
// 単独のXML APIも、同じイベントダイアログのアダプターと処理本体へ委譲する。
[[nodiscard]] EventDialogPtr createXmlContentHost(
    const Context& context, xmlcontentdialog::XmlContentDialog& editor);

[[nodiscard]] EventDialogPtr createEventDialog(
    const Context& context, SummaryPtr summary, SkinPtr forceSkin,
    UseCounterPtr useCounter, Parent parent, ContentPtr event,
    ContentPtr parentContent = {}, bool create = false);

}  // namespace cwxeditor::cwx::editor::gui::dwt::eventdialog
