#pragma once

#include "cwxeditor_src/core/cwx/card.hpp"
#include "cwxeditor_src/core/cwx/event.hpp"
#include "cwxeditor_src/core/cwx/flag.hpp"
#include "cwxeditor_src/core/cwx/motion.hpp"
#include "cwxeditor_src/core/cwx/path.hpp"
#include "cwxeditor_src/core/cwx/props.hpp"
#include "cwxeditor_src/core/cwx/skin.hpp"
#include "cwxeditor_src/core/cwx/summary.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/abilityview.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/contentinitializer.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dmenu.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/eventdialog.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/imageselect.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/keycodeview.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/materialselect.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/motionview.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/radarspinner.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/scales.hpp"
#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/portable.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::effectcarddialog {

namespace card = ::cwxeditor::cwx::card;
namespace event = ::cwxeditor::cwx::event;
namespace motion = ::cwxeditor::cwx::motion;
namespace path = ::cwxeditor::cwx::path;
namespace props = ::cwxeditor::cwx::props;
namespace skin = ::cwxeditor::cwx::skin;
namespace summary = ::cwxeditor::cwx::summary;
namespace types = ::cwxeditor::cwx::types;
namespace abilityview = ::cwxeditor::cwx::editor::gui::dwt::abilityview;
namespace dmenu = ::cwxeditor::cwx::editor::gui::dwt::dmenu;
namespace contentinitializer = ::cwxeditor::cwx::editor::gui::dwt::contentinitializer;
namespace imageselect = ::cwxeditor::cwx::editor::gui::dwt::imageselect;
namespace keycodeview = ::cwxeditor::cwx::editor::gui::dwt::keycodeview;
namespace materialselect = ::cwxeditor::cwx::editor::gui::dwt::materialselect;
namespace fullmotion = ::cwxeditor::cwx::editor::gui::dwt::motionview;
namespace flagspane = ::cwxeditor::cwx::editor::gui::dwt::flagspane;
namespace radarspinner = ::cwxeditor::cwx::editor::gui::dwt::radarspinner;
namespace scales = ::cwxeditor::cwx::editor::gui::dwt::scales;
namespace lib = ::cwxeditor::lib;

using String = lib::String;
using StringView = lib::StringView;
using Context = contentinitializer::ContentInitializerContext;
using SummaryPtr = summary::Summary::Owner;
using SkinPtr = skin::Skin::Ptr;
using UseCounterPtr = lib::Owner<event::UseCounter>;
using CWXPathPtr = lib::Owner<path::CWXPath>;
using Parent = std::shared_ptr<lib::Container>;
using MotionPtr = lib::Owner<motion::Motion>;
using WarningList = lib::Array<String>;
using CardImagePtr = lib::Owner<card::CardImage>;
using CardImageArray = lib::Array<CardImagePtr>;
using RadioPtr = std::shared_ptr<lib::RadioButton>;
using CheckPtr = std::shared_ptr<lib::CheckBox>;
using TextPtr = std::shared_ptr<lib::TextEdit>;
using NumberPtr = std::shared_ptr<lib::NumberEdit>;
using SliderPtr = std::shared_ptr<lib::Slider>;
using ComboPtr = std::shared_ptr<lib::ComboBox>;
using GroupPtr = std::shared_ptr<lib::GroupBox>;
using PanelPtr = std::shared_ptr<lib::Panel>;
using TabFolderPtr = std::shared_ptr<lib::TabView>;

inline constexpr std::int32_t successRateMax = 5;
inline constexpr std::array<types::EffectType, 5> effectTypes{
    types::EffectType::Physic, types::EffectType::Magic,
    types::EffectType::MagicalPhysic, types::EffectType::PhysicalMagic,
    types::EffectType::None};
inline constexpr std::array<types::Resist, 3> resistTypes{
    types::Resist::Avoid, types::Resist::Resist, types::Resist::Unfail};
inline constexpr std::array<types::CardTarget, 5> targetTypes{
    types::CardTarget::None, types::CardTarget::User,
    types::CardTarget::Party, types::CardTarget::Enemy, types::CardTarget::Both};
inline constexpr std::array<types::CardVisual, 4> visualTypes{
    types::CardVisual::None, types::CardVisual::Horizontal,
    types::CardVisual::Reverse, types::CardVisual::Vertical};
inline constexpr std::array<types::Premium, 3> premiumTypes{
    types::Premium::Normal, types::Premium::Rare, types::Premium::Premium};
inline constexpr std::array<types::ShowStyle, 3> showStyles{
    types::ShowStyle::Invisible, types::ShowStyle::Center,
    types::ShowStyle::FrontOfUser};
// DのcreateStatusPaneは具体的な27種類の状態条件を公開する。
// Status::Noneはモデル用の番兵値で、編集可能な条件には含めない。
inline constexpr std::array<types::Status, 27> statusTypes{
    types::Status::Active, types::Status::Inactive, types::Status::Alive,
    types::Status::Dead, types::Status::Fine, types::Status::Injured,
    types::Status::HeavyInjured, types::Status::Unconscious,
    types::Status::Poison, types::Status::Sleep, types::Status::Bind,
    types::Status::Paralyze, types::Status::Confuse, types::Status::Overheat,
    types::Status::Brave, types::Status::Panic, types::Status::Silence,
    types::Status::FaceUp, types::Status::AntiMagic, types::Status::UpAction,
    types::Status::UpAvoid, types::Status::UpResist, types::Status::UpDefense,
    types::Status::DownAction, types::Status::DownAvoid,
    types::Status::DownResist, types::Status::DownDefense};
inline constexpr std::array<types::Enhance, 3> editableEnhancements{
    types::Enhance::Avoid, types::Enhance::Resist, types::Enhance::Defense};
inline constexpr std::size_t invalidEnhancementIndex =
    std::numeric_limits<std::size_t>::max();

class SoundSelect final {
public:
    SoundSelect() = default;
    SoundSelect(const SoundSelect&) = delete;
    SoundSelect& operator=(const SoundSelect&) = delete;
    SoundSelect(SoundSelect&&) = delete;
    SoundSelect& operator=(SoundSelect&&) = delete;

    Parent parent{};
    String path{};
    String filePath{};
    std::uint32_t volume{100};
    std::uint32_t loopCount{1};
    std::uint32_t fadeIn{};
    std::uint32_t channel{};
    bool enabled{true};
    bool selectedDefDir{true};
    bool readOnly{};
    WarningList warnings{};
    std::vector<lib::Action> modEvent;
    std::vector<lib::Action> loadedEvent;
    std::shared_ptr<lib::ComboBox> pathControl{};
    std::shared_ptr<lib::NumberEdit> volumeControl{};
    std::shared_ptr<lib::NumberEdit> loopControl{};
    std::unique_ptr<materialselect::MaterialSelectBase> selector{};
    lib::AudioHandle previewHandle{};
    std::shared_ptr<int> previewLifetime{std::make_shared<int>(0)};
    std::uint64_t previewRevision{};

    void setPath(String value);
    void setEnabled(bool value);
    void syncControls();
    void syncFromControls();
    void refreshPathChoices(String noneLabel);
    [[nodiscard]] bool synchronizing() const noexcept { return syncing_; }
    void notifyModified();
    void notifyLoaded();

private:
    void syncPathControl();
    bool syncing_{};
};

class ImageSelectState final {
public:
    struct MaterialPath {
        CardImageArray images;
        bool cancel{};
    };
    CardImageArray images;
    WarningList warnings;
    lib::Callback<MaterialPath(bool)> materializer;

    [[nodiscard]] MaterialPath materialPath(bool forceApplying) const;
};

class MotionEditor final : public lib::Panel {
public:
    using PathOpener = lib::Callback<bool(StringView, bool)>;
    std::vector<lib::Action> warningEvent;
    std::vector<lib::Action> modEvent;

    MotionEditor(const Context& context, SummaryPtr summary,
                 CWXPathPtr owner, UseCounterPtr useCounter,
                 bool readOnly = false);
    ~MotionEditor();
    [[nodiscard]] bool readOnly() const noexcept { return read_only_; }
    void setReadOnly(bool value) noexcept;
    [[nodiscard]] lib::Array<MotionPtr> motions() const;
    void setMotions(lib::Array<MotionPtr> value);
    void setPathOpener(PathOpener value);
    [[nodiscard]] const WarningList& warnings() const noexcept {
        return warnings_;
    }
    void refreshWarnings();
    void refDataVersion();
    void closeAuxiliaryEditors();
    void refSkin(SkinPtr skin);
    [[nodiscard]] bool openCWXPath(StringView value, bool activate) const;

private:
    std::shared_ptr<lib::Panel> host_{};
    SkinPtr preview_skin_{};
    std::unique_ptr<fullmotion::MotionView> view_{};
    flagspane::Signal<void()>::Token warning_connection_{};
    flagspane::Signal<void()>::Token model_connection_{};
    bool read_only_{};
    WarningList warnings_{};
    PathOpener path_opener_{};
    void retireCandidateSubscriptions() noexcept;
    std::shared_ptr<int> candidate_lifetime_{std::make_shared<int>(0)};
    ::cwxeditor::cwx::editor::gui::dwt::commons::Commons* candidate_commons_{};
    std::weak_ptr<const int> candidate_commons_lifetime_{};
    std::size_t candidate_ref_connection_{};
    std::size_t candidate_del_connection_{};
};

struct TextMenuModify {
    void reset() noexcept { modified = false; }
    bool modified{};
};

template <typename C>
class EffectCardDialog final : public editdialogsession::EditDialogSession {
    static_assert(std::is_same_v<C, card::SkillCard> ||
                      std::is_same_v<C, card::ItemCard> ||
                      std::is_same_v<C, card::BeastCard>,
                  "EffectCardDialog requires a concrete effect card type");

public:
    using CardPtr = lib::Owner<C>;
    using TabItem = PanelPtr;
    using DialogCallbacks = eventdialog::DialogCallbacks;
    inline static constexpr bool SetUseCountCur = false;

    class ResetSource {
    public:
        explicit ResetSource(EffectCardDialog* owner = nullptr) : owner_(owner) {}
        void widgetSelected();
    private:
        EffectCardDialog* owner_{};
    };
    class ModSource {
    public:
        explicit ModSource(EffectCardDialog* owner = nullptr) : owner_(owner) {}
        void modifyText();
    private:
        EffectCardDialog* owner_{};
    };
    class OASelect {
    public:
        explicit OASelect(EffectCardDialog* owner = nullptr) : owner_(owner) {}
        void widgetSelected();
    private:
        EffectCardDialog* owner_{};
    };
    class SelEffectType {
    public:
        explicit SelEffectType(EffectCardDialog* owner = nullptr) : owner_(owner) {}
        void widgetSelected();
    private:
        EffectCardDialog* owner_{};
    };
    class Dispose {
    public:
        explicit Dispose(EffectCardDialog* owner = nullptr) : owner_(owner) {}
        void widgetDisposed();
    private:
        EffectCardDialog* owner_{};
    };

private:
    bool _readOnly{};
    bool _creationMode{};
    bool _creationInitialized{};
    std::shared_ptr<bool> _callbackLifetime{std::make_shared<bool>(true)};
    std::shared_ptr<bool> _keyCodeHostActive{std::make_shared<bool>(true)};
    Context _context{};
    props::CProps* _prop{};
    SummaryPtr _summ{};
    bool _summarySkinPinned{};
    Parent _parent{};
    CWXPathPtr _ucOwner{};
    UseCounterPtr _useCounter{};
    CardPtr _card{};
    CardPtr _draft{};
    ImageSelectState _imgPath{};
    std::unique_ptr<imageselect::ImageSelectBase> _imageSelect{};
    TextPtr _imagePathEdit{};
    TextPtr _desc{};
    TextPtr _name{};
    CheckPtr _needSpell{};
    std::array<RadioPtr, effectTypes.size()> _effTyp{};
    std::array<RadioPtr, resistTypes.size()> _res{};
    abilityview::AbilityView _ability{};
    NumberPtr _level{};
    std::uint32_t _lastAutoPrice{};
    NumberPtr _useCount{};
    NumberPtr _useCountCur{};
    CheckPtr _useCountIsMax{};
    NumberPtr _price{};
    std::shared_ptr<MotionEditor> _motions{};
    Parent _useModParent{};
    std::shared_ptr<radarspinner::RadarSpinner> _useModR{};
    std::shared_ptr<scales::Scales> _useModS{};
    std::array<std::size_t, 4> _useModTbl{};
    Parent _hasModParent{};
    std::shared_ptr<radarspinner::RadarSpinner> _hasModR{};
    std::shared_ptr<scales::Scales> _hasModS{};
    std::array<std::size_t, 4> _hasModTbl{};
    std::array<RadioPtr, targetTypes.size()> _targ{};
    RadioPtr _one{};
    RadioPtr _all{};
    GroupPtr _oneAllGrp{};
    std::array<RadioPtr, visualTypes.size()> _vis{};
    std::array<RadioPtr, premiumTypes.size()> _prem{};
    SliderPtr _sucRate{};
    SoundSelect _se1{};
    SoundSelect _se2{};
    std::shared_ptr<keycodeview::KeyCodeView> _keyCodes{};
    TextPtr _settingsComment{};
    ComboPtr _externalVariantWriteScope{};
    std::vector<String> _externalVariantWriteScopes{};
    ComboPtr _externalVariantWriteScenarioVar{};
    ComboPtr _externalVariantWriteYado{};
    TextPtr _scenario{};
    TextMenuModify _scenarioTM{};
    dmenu::TextMenuModifyPtr _scenarioTextMenu{};
    TextPtr _author{};
    TextMenuModify _authorTM{};
    dmenu::TextMenuModifyPtr _authorTextMenu{};
    std::array<CheckPtr, statusTypes.size()> _invokeCond{};
    CheckPtr _removeWithUncons{};
    ComboPtr _showStyle{};
    std::array<types::ShowStyle, 3> _showStyles{showStyles};
    SkinPtr _summSkin{};
    std::shared_ptr<lib::Dialog> _window{};
    std::shared_ptr<lib::Button> _okButton{};
    bool _readOnlyCloseEligible{true};
    std::shared_ptr<lib::Button> _applyButton{};
    std::shared_ptr<lib::Button> _cancelButton{};
    PanelPtr _area{};
    TabFolderPtr _tabf{};
    WarningList _warnings{};
    DialogCallbacks _callbacks{};
    std::vector<lib::Action> applyEvent;
    std::vector<lib::Action> appliedEvent;
    std::vector<lib::Action> closeEvent;
    bool _open{};
    std::uint64_t _hostRevision{};
    bool _forceApplying{};
    bool _ignoreMod{};
    // 通知元はCommonsが所有する。ここでは不透明な接続IDだけを持ち、
    // モードレス画面の部品・作業コピーを解放する前に登録を外せるようにする。
    std::uint64_t _ref_card_connection_{};
    std::uint64_t _del_card_connection_{};
    std::uint64_t _ref_skin_connection_{};
    std::uint64_t _ref_image_scale_connection_{};
    std::uint64_t _ref_data_version_connection_{};
    std::uint64_t _ref_target_version_connection_{};
    std::uint64_t _ref_scenario_connection_{};
    std::uint64_t _ref_event_tree_connection_{};
    std::uint64_t _del_event_tree_connection_{};
    std::uint64_t _ref_radar_style_connection_{};

    [[nodiscard]] static CardPtr duplicateCard(const CardPtr& value);
    void connectCommonsObservers();
    void disconnectCommonsObservers() noexcept;
    void prepareDraft();
    void createControls();
    void loadDefaults();
    void loadCard(const C& value);
    void writeControlsToDraft();
    void commitDraft();
    void invoke(const std::vector<lib::Action>& callbacks) const;
    void resetSource();
    void updatePriceByLevel(int value);
    void updateUseLimitMax();
    void updateEnabledImpl();
    [[nodiscard]] std::uint32_t skillPrice(std::uint32_t level) const;
    [[nodiscard]] std::uint32_t beastPrice() const noexcept { return 1000U; }
    [[nodiscard]] SkinPtr resolveSkin() const;
    [[nodiscard]] bool radarStyle() const;
    [[nodiscard]] std::uint32_t skillLevelMax() const;
    [[nodiscard]] std::uint32_t useCountMax() const;
    [[nodiscard]] std::uint32_t priceMax() const;
    [[nodiscard]] std::uint32_t enhanceMax() const;
    void addPage(String title, const PanelPtr& panel);
    void createMod(const Parent& parent, std::array<std::size_t, 4>& table,
                   std::shared_ptr<radarspinner::RadarSpinner>& radar,
                   std::shared_ptr<scales::Scales>& scalesControl);
    void initUseMod();
    void initHasMod();
    [[nodiscard]] GroupPtr createModParent(const Parent& parent, String title);
    [[nodiscard]] std::vector<int> readEnhanceControls(
        const std::array<std::size_t, 4>& table,
        const std::shared_ptr<radarspinner::RadarSpinner>& radar,
        const std::shared_ptr<scales::Scales>& scalesControl) const;
    void writeEnhanceControls(const std::array<std::size_t, 4>& table,
                              const std::vector<int>& values,
                              std::shared_ptr<radarspinner::RadarSpinner>& radar,
                              std::shared_ptr<scales::Scales>& scalesControl);
    void loadEnhanceControls(const C& value);
    void writeEnhanceControls(C& value);
    void refreshExternalVariantWriteEnabled();
    [[nodiscard]] String externalVariantWriteScopeSelection() const;
    void selectExternalVariantWriteScope(StringView accessScope);
    void refreshExternalVariantWriteScenarioVars(StringView selected);
    void refreshWarningsForCard();
    void abilityModified();
    void constructMain();
    void constructDesc();
    void constructApt();
    void constructUseModify();
    void constructHaveModify();
    void constructMotion();
    void constructProps();
    void constructBehavior();
    void constructKeyCode();
    void constructSettings();

public:
    EffectCardDialog(Context context, SummaryPtr summary, Parent parent = {},
                     CWXPathPtr ucOwner = {}, UseCounterPtr useCounter = {},
                     CardPtr value = {}, bool readOnly = false, bool creationMode = false);
    EffectCardDialog(Context context, SummaryPtr summary, CWXPathPtr ucOwner,
                     UseCounterPtr useCounter, CardPtr value,
                     bool readOnly = false, bool creationMode = false);
    EffectCardDialog(const EffectCardDialog&) = delete;
    EffectCardDialog& operator=(const EffectCardDialog&) = delete;
    ~EffectCardDialog();

    [[nodiscard]] const CardPtr& card() const noexcept { return _card; }
    [[nodiscard]] CardPtr& card() noexcept { return _card; }
    void card(CardPtr value);
    [[nodiscard]] const CardPtr& draft() const noexcept { return _draft; }
    [[nodiscard]] const Context& context() const noexcept { return _context; }
    [[nodiscard]] Context& context() noexcept { return _context; }
    [[nodiscard]] const SummaryPtr& summary() const noexcept { return _summ; }
    [[nodiscard]] const Parent& parentWidget() const noexcept { return _parent; }
    [[nodiscard]] bool readOnly() const noexcept { return _readOnly; }
    [[nodiscard]] bool isOpen() const noexcept { return _open; }
    [[nodiscard]] const std::shared_ptr<lib::Dialog>& widget() const noexcept { return _window; }
    [[nodiscard]] lib::Window* editDialogWindow() const noexcept override {
        return _window.get();
    }
    [[nodiscard]] bool editDialogPending() const noexcept override {
        return !_readOnly && (_creationMode || !_card || (_window && _window->modified));
    }
    [[nodiscard]] bool applyEditDialog() override {
        _forceApplying = true;
        struct Reset final {
            bool& value;
            ~Reset() { value = false; }
        } reset{_forceApplying};
        return apply();
    }
    [[nodiscard]] const WarningList& warnings() const noexcept { return _warnings; }
    [[nodiscard]] const std::shared_ptr<MotionEditor>& motions() const noexcept { return _motions; }
    [[nodiscard]] abilityview::AbilityView& ability() noexcept { return _ability; }
    [[nodiscard]] const abilityview::AbilityView& ability() const noexcept { return _ability; }
    [[nodiscard]] const SoundSelect& sound1() const noexcept { return _se1; }
    [[nodiscard]] const SoundSelect& sound2() const noexcept { return _se2; }
    [[nodiscard]] const std::shared_ptr<keycodeview::KeyCodeView>& keyCodes() const noexcept { return _keyCodes; }
    [[nodiscard]] SkinPtr summSkin() const;
    void setCallbacks(DialogCallbacks callbacks) { _callbacks = std::move(callbacks); }
    void open();
    void active();
    void forceCancel();
    void close();
    void forceApply() noexcept { _forceApplying = true; }
    [[nodiscard]] bool forceApplying() const noexcept { return _forceApplying; }
    [[nodiscard]] bool openCWXPath(String path, bool shellActivate);
    void setup(Parent area = {});
    void updateTitle(const CardPtr& value);
    void refCard(const CardPtr& value);
    void refreshEnblOneAll();
    void refreshWarning();
    void updateEnabled();
    void refEventTree(const event::EventTree* tree);
    void refEventTree(const lib::Owner<event::EventTree>& tree) { refEventTree(tree.get()); }
    void delCard(const path::CWXPath* owner, const CardPtr& value);
    void refScenario(const SummaryPtr& value);
    void refSkin();
    void refDataVersion();
    // 適用に成功するたびに、ホストの履歴は直前の実際の状態を取得する。
    lib::Action on_applying;
    bool apply();
};

using SkillCardDialog = EffectCardDialog<card::SkillCard>;
using ItemCardDialog = EffectCardDialog<card::ItemCard>;
using BeastCardDialog = EffectCardDialog<card::BeastCard>;

[[nodiscard]] std::shared_ptr<lib::GroupBox> createEffectSoundPanel(
    const Parent& parent, const Context& context, const SummaryPtr& summary,
    String name, bool readOnly, bool isEffectContent,
    std::shared_ptr<lib::CheckBox>& initialEffect, SoundSelect& se1,
    SoundSelect& se2);

}  // namespace cwxeditor::cwx::editor::gui::dwt::effectcarddialog
