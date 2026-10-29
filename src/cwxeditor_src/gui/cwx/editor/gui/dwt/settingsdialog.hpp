#pragma once

// cwx/editor/gui/dwt/settingsdialog.d
// のC++20移植。 編集値を共通部品へ
// 保持し、一つの処理単位でdprops::Props
// へ確定する。 元のEditor全体
// の通知群はこの画面内のコールバック
// で表し、 ツールキット・ウィンドウ
// ・プロセスのハンドルは公開しない。

#include "cwxeditor_src/core/cwx/menu.hpp"
#include "cwxeditor_src/core/cwx/structs.hpp"
#include "cwxeditor_src/core/cwx/summary.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/core/cwx/variables.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/absdialog.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/autokeycode.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/contentinitializer.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/customtext.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dprops.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/etcsettings.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/incsearch.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/properties.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/skintypesettings.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/toolspane.hpp"
#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/events.hpp"
#include "cwxeditor_src/lib/services.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::settingsdialog {

namespace lib = ::cwxeditor::lib;
namespace dprops = ::cwxeditor::cwx::editor::gui::dwt::dprops;
namespace autokeycode = ::cwxeditor::cwx::editor::gui::dwt::autokeycode;
namespace contentinitializer = ::cwxeditor::cwx::editor::gui::dwt::contentinitializer;
namespace customtext = ::cwxeditor::cwx::editor::gui::dwt::customtext;
namespace etcsettings = ::cwxeditor::cwx::editor::gui::dwt::etcsettings;
namespace incsearch = ::cwxeditor::cwx::editor::gui::dwt::incsearch;
namespace properties = ::cwxeditor::cwx::editor::gui::dwt::properties;
namespace skintypesettings = ::cwxeditor::cwx::editor::gui::dwt::skintypesettings;
namespace toolspane = ::cwxeditor::cwx::editor::gui::dwt::toolspane;
namespace undo = ::cwxeditor::cwx::editor::gui::dwt::undo;
namespace structs = ::cwxeditor::cwx::structs;
namespace summary = ::cwxeditor::cwx::summary;
namespace types = ::cwxeditor::cwx::types;
namespace variables = ::cwxeditor::cwx::variables;
namespace menu = ::cwxeditor::cwx::menu;

using String = lib::String;
using Text = std::shared_ptr<lib::TextEdit>;
using ToggleButtonPtr = std::shared_ptr<lib::CheckBox>;
using RadioButtonPtr = std::shared_ptr<lib::RadioButton>;
using PushButton = std::shared_ptr<lib::Button>;
using Spinner = std::shared_ptr<lib::NumberEdit>;
using Combo = std::shared_ptr<lib::ComboBox>;
using Table = std::shared_ptr<lib::TableView>;
using TabItem = std::shared_ptr<lib::Panel>;

enum class BackupType : std::int32_t {
    Time = 0,
    Edit = 1,
};

enum class SummaryDirtyKind : std::uint8_t {
    XmlFileName,
    SaveSkinName,
    LegacyInnerImagePath,
    LegacyResourceExtension,
};

/// プロパティの確定後に出す通知。 全項目を省略可能
/// にし、ヘッドレスのホストでも使えるようにする。
struct Callbacks {
    commons::Commons* editorCommons{};
    std::function<lib::ResourceId(types::MenuID)> menuImage;
    std::function<lib::ResourceId(types::CType)> contentImage;
    lib::Action refImageScale;
    lib::Action refTargetVersion;
    lib::Action refWallpaper;
    lib::Action refOuterTools;
    lib::Action refEventTemplates;
    lib::Action refIgnorePaths;
    lib::Action refCardState;
    lib::Action refSwitchFixedWithCheckBox;
    lib::Action refImagePaneSelectionFilter;
    lib::Action refSortCondition;
    lib::Action refSkin;
    lib::Action refClassicSkin;
    lib::Action refHistories;
    lib::Action refImportHistory;
    lib::Action refHistoryMax;
    lib::Action refSearchHistories;
    lib::Action refUndoMax;
    lib::Action refContentText;
    std::function<void(types::MenuID)> refMenu;
    lib::Action updateToolBarText;
    lib::Action refFloatMessagePreview;
    lib::Action refUseMessageWindowColorInTextContentDialog;
    lib::Action refSoundType;
    lib::Action refCardImageStatus;
    lib::Action refEventTreeStyle;
    lib::Action refRadarStyle;
    lib::Action refShowAptitudes;
    lib::Action refCastCardParameterEditStyle;
    lib::Action refCoupons;
    lib::Action refGossips;
    lib::Action refCompleteStamps;
    lib::Action refKeyCodes;
    lib::Action refCellNames;
    lib::Action refPreviewValues;
    lib::Action refShowCardListHeader;
    lib::Action refShowCardListTitle;
    lib::Action refVarSelectStyle;
    lib::Action refEventTreeViewStyle;
    lib::Action refEventEditorStyle;
    lib::Action refAreaTable;
    lib::Action refTableViewStyle;
    lib::Action refContentsToolBoxStyle;
    lib::Action refUpdateMotionBarStyle;
    lib::Action refMenuCardAndBgImageList;
    lib::Action refExecutedParties;
    lib::Action refPartyHistoryMax;
    lib::Action refBgImageSettings;
    lib::Action refStandardSelections;
    lib::Action refStandardKeyCodes;
    lib::Action refKeyCodesByFeatures;
    lib::Action refSettingsWithSkinTypes;
    lib::Action refElementOverrides;
    lib::Action updateCloseButtons;
    lib::Action updateMainToolBar;
    std::function<void(SummaryDirtyKind)> markSummaryChanged;
    lib::Action saved;
};

/// DのSettingsDialog.applyと同じ履歴制限
/// を適用する。 OSのダイアログを作らずに、ファイルシステム
/// の状態を優先する動作を保存テストで確認できるよう分離する。
void applyHistoryLimits(dprops::Props& prop);

template <typename T>
struct PaneState {
    lib::Array<T> array;
    String boxName;
    bool noApply{};
    bool enabled{true};

    void setup(lib::Array<T> value, String name = {}) {
        array = std::move(value);
        if (!name.empty()) boxName = std::move(name);
        noApply = false;
    }
    [[nodiscard]] bool apply() const noexcept { return !noApply; }
    void forceApply() noexcept { noApply = false; }
};

struct ViewState {
    bool enabled{true};
    void setEnabled(bool value) noexcept { enabled = value; }
};

struct UndoState {
    void reset() noexcept {}
};

struct SkinTypeState {
    int selected{-1};
    lib::Array<structs::SettingsWithSkinType> settingsWithSkinTypes;
};

struct IncrementalSearchState {
    String query;
    bool active{};

    void startIncSearch() noexcept { active = true; }
    void stop() noexcept { active = false; }
};

class SettingsDialog : public absdialog::AbsDialog {
private:
    Callbacks _comm;
    std::weak_ptr<const int> _editorCommonsLifetime;
    std::uint64_t _initializerUndoConnection{};
    std::uint64_t _elementOverridesUndoConnection{};
    dprops::Props* _prop{};
    summary::Summary* _summ{};
    std::shared_ptr<properties::DockingFolderCTC> _dock;
    lib::Action _sendReloadProps;
    bool _loadingSkinControls{};
    String _selectedSkinName;
    std::uint64_t _skinSelectionGeneration{};
    std::shared_ptr<int> _skinLifetime{std::make_shared<int>(0)};
    std::shared_ptr<int> _questionLifetime{std::make_shared<int>(0)};
    bool _applyQuestionPending{};
    std::optional<lib::MessageResult> _applyQuestionResult;
    lib::Array<structs::SettingsWithSkinType>& skinSettingsDraft();
    void confirmSkinSelection(bool deleting, std::function<void(bool)> completion);
    bool applyUncommittedForms();

    TabItem _tabB;
    Text _enginePath;
    PushButton _refEnginePath;
    ToggleButtonPtr _findEnginePath;
    Text _tempDir;
    PushButton _tempRef;
    Text _backupDir;
    ToggleButtonPtr _backupEnabled;
    RadioButtonPtr _backupIntervalTypeTime;
    RadioButtonPtr _backupIntervalTypeEdit;
    Spinner _backupIntervalTime;
    Spinner _backupIntervalEdit;
    Spinner _backupCount;
    ToggleButtonPtr _backupRefAuthor;
    ToggleButtonPtr _autoSave;
    ToggleButtonPtr _backupArchived;
    PushButton _backupRef;
    PushButton _backupDirOpen;
    Text _backupBeforeSaveDir;
    ToggleButtonPtr _backupBeforeSaveEnabled;
    PushButton _backupBeforeSaveRef;
    PushButton _backupBeforeSaveDirOpen;
    Text _author;
    Text _newAreaName;
    Text _wallpaper;
    PushButton _wallpaperRef;
    Combo _wallpaperStyle;
    std::unordered_map<int, int> _wallpaperStyleTbl;
    std::unordered_map<int, int> _wallpaperStyleTbl2;
    PushButton _clearHist;
    Spinner _histMax;
    PushButton _clearSHist;
    Spinner _sHistMax;
    PushButton _clearPHist;
    Spinner _pHistMax;
    Spinner _undoMaxMainView;
    Spinner _undoMaxEvent;
    Spinner _undoMaxReplace;
    Spinner _undoMaxEtc;

    std::shared_ptr<lib::TabView> _tabf;
    std::vector<lib::Action> _openButtonEnabledRefreshes;

    TabItem _tabS;
    SkinTypeState _skinType;
    std::unique_ptr<skintypesettings::SkinTypeChooser> _skinTypeChooser;
    ToggleButtonPtr _overrideBgImages;
    ToggleButtonPtr _overrideSelections;
    ToggleButtonPtr _overrideKeyCodes;
    PushButton _defBgImg;
    std::unique_ptr<skintypesettings::DefBgImgDialog> _defBgImgDlg;
    lib::Array<structs::BgImageS> _bgImagesDefault;
    lib::Array<structs::BgImageSetting> _bgImageSettings;
    lib::Array<String> _standardSelections;
    lib::Array<String> _standardKeyCodes;
    lib::Array<structs::KeyCodeByFeature> _keyCodesByFeatures;
    lib::Array<structs::KeyCodeByMotion> _keyCodesByMotions;
    PaneState<structs::BgImageSetting> _bgStgs;
    std::shared_ptr<toolspane::ToolsPane<structs::BgImageSetting>> _bgStgsView;
    Text _selections;
    UndoState _selectionsUndo;
    Text _keyCodes;
    UndoState _keyCodesUndo;
    std::unique_ptr<autokeycode::KeyCodeByFeatureView> _keyCodesByFeaturesView;
    std::unique_ptr<autokeycode::KeyCodeByMotionView> _keyCodesByMotionsView;
    std::unique_ptr<skintypesettings::ElementOverrideView> _elementOverrides;

    TabItem _tabT;
    PaneState<structs::OuterTool> _tools;
    PaneState<structs::ClassicEngine> _cEngines;
    std::shared_ptr<toolspane::ToolsPane<structs::OuterTool>> _toolsView;
    std::shared_ptr<toolspane::ToolsPane<structs::ClassicEngine>> _cEnginesView;
    std::vector<std::int32_t> _toolsWeights{1, 1};
    std::vector<std::int32_t> _cEnginesWeights{1, 1};

    TabItem _tabC;
    PaneState<structs::ScTemplate> _scTempls;
    PaneState<structs::EvTemplate> _evTempls;
    std::shared_ptr<toolspane::ToolsPane<structs::ScTemplate>> _scTemplsView;
    std::shared_ptr<toolspane::ToolsPane<structs::EvTemplate>> _evTemplsView;
    std::vector<std::int32_t> _scTemplsWeights{1, 1};
    std::vector<std::int32_t> _evTemplsWeights{1, 1};
    std::unique_ptr<contentinitializer::ContentInitialValueEditor> _initializer;
    Combo _initializerChooser;
    lib::Array<variables::ContentInitializer> _initializers;
    lib::Array<String> _initializerKeys;
    std::vector<std::unique_ptr<undo::UndoManager>> _initializerUndos;
    int _selectedInitializer{};

    TabItem _tabE;
    Combo _drawingScale;
    Combo _imageScale;
    Combo _uiTextScale;
    Combo _language;
    std::unordered_map<int, String> _msgsTableIndex;
    std::unordered_map<String, String> _msgsTableFile;
    Text _ignorePaths;

    std::unique_ptr<etcsettings::EtcSettings> _etcSettings;
    Combo _targetVersion;
    std::unordered_map<String, int> _targetVersionTbl;
    std::unordered_map<int, String> _targetVersionTbl2;
    Combo _soundPlayType;
    std::unordered_map<int, int> _soundPlayTypeTbl;
    std::unordered_map<int, int> _soundPlayTypeTbl2;
    Spinner _bgmVolume;
    Combo _soundEffectPlayType;
    std::unordered_map<int, int> _soundEffectPlayTypeTbl;
    std::unordered_map<int, int> _soundEffectPlayTypeTbl2;
    Spinner _seVolume;
    Combo _dialogStatus;
    std::unordered_map<int, int> _dialogStatusTbl;
    std::unordered_map<int, int> _dialogStatusTbl2;
    Combo _showStatusTime;
    std::unordered_map<int, int> _showStatusTimeTbl;
    std::unordered_map<int, int> _showStatusTimeTbl2;
    Text _savedSound;
    PushButton _savedSoundRef;

    Combo _flagInitValue;
    Combo _stepInitValue;
    Text _stepValueName;

    Text _mnemonic;
    dmenu::TextMenuModifyPtr _mnemonicTextMenu;
    std::shared_ptr<customtext::HotKeyField> _hotkey;
    Text _hotkeyWidget;
    menu::MenuProps _menuProps;
    std::unordered_map<types::MenuID, String> _appliedMnemonic;
    std::unordered_map<types::MenuID, String> _appliedHotkey;
    Table _menu;
    PushButton _menuApply;
    PushButton _menuDel;

    struct SMenuData {
        types::MenuID id{types::MenuID::None};
        String mnemonic;
        String hotkey;
    };
    std::vector<SMenuData> _menuData;
    std::unique_ptr<incsearch::IncSearch> _menuIncSearch;
    // DWTのDisplayフィルターはメニュー接続用のComposite
    // へ登録され、保持された子より長く残る場合がある。 このコ
    // ントローラーの破棄時点で、子孫への共通経路を無効にする。
    std::shared_ptr<int> _menuSearchLifetime{std::make_shared<int>(0)};
    std::shared_ptr<lib::Menu> _menuPopup;

    class RefE {
    public:
        explicit RefE(SettingsDialog* owner = nullptr) : owner_(owner) {}
        void widgetSelected();
        void modifyText();

    private:
        SettingsDialog* owner_{};
    };

    class SelEngine {
    public:
        explicit SelEngine(SettingsDialog* owner = nullptr) : owner_(owner) {}
        void widgetSelected();

    private:
        SettingsDialog* owner_{};
    };
    class SelTemp {
    public:
        explicit SelTemp(SettingsDialog* owner = nullptr) : owner_(owner) {}
        void widgetSelected();

    private:
        SettingsDialog* owner_{};
    };
    class SelBackup {
    public:
        explicit SelBackup(SettingsDialog* owner = nullptr) : owner_(owner) {}
        void widgetSelected();

    private:
        SettingsDialog* owner_{};
    };
    class SelBackupBeforeSave {
    public:
        explicit SelBackupBeforeSave(SettingsDialog* owner = nullptr) : owner_(owner) {}
        void widgetSelected();

    private:
        SettingsDialog* owner_{};
    };
    class SelWallpaper {
    public:
        explicit SelWallpaper(SettingsDialog* owner = nullptr) : owner_(owner) {}
        void widgetSelected();

    private:
        SettingsDialog* owner_{};
    };
    class SelSysSound {
    public:
        explicit SelSysSound(SettingsDialog* owner = nullptr, Text text = {})
            : owner_(owner), text_(std::move(text)) {}
        void widgetSelected();

    private:
        SettingsDialog* owner_{};
        Text text_;
    };
    class ClearHist {
    public:
        explicit ClearHist(SettingsDialog* owner = nullptr) : owner_(owner) {}
        void widgetSelected();

    private:
        SettingsDialog* owner_{};
    };
    class ClearSHist {
    public:
        explicit ClearSHist(SettingsDialog* owner = nullptr) : owner_(owner) {}
        void widgetSelected();

    private:
        SettingsDialog* owner_{};
    };
    class ClearPHist {
    public:
        explicit ClearPHist(SettingsDialog* owner = nullptr) : owner_(owner) {}
        void widgetSelected();

    private:
        SettingsDialog* owner_{};
    };
    class SelectMenu {
    public:
        explicit SelectMenu(SettingsDialog* owner = nullptr) : owner_(owner) {}
        void widgetSelected();

    private:
        SettingsDialog* owner_{};
    };
    class ModMenu {
    public:
        explicit ModMenu(SettingsDialog* owner = nullptr) : owner_(owner) {}
        void modifyText();

    private:
        SettingsDialog* owner_{};
    };
    class DelMenuAccel {
    public:
        explicit DelMenuAccel(SettingsDialog* owner = nullptr) : owner_(owner) {}
        void widgetSelected();

    private:
        SettingsDialog* owner_{};
    };
    class ApplyMenu {
    public:
        explicit ApplyMenu(SettingsDialog* owner = nullptr) : owner_(owner) {}
        void widgetSelected();

    private:
        SettingsDialog* owner_{};
    };

    RefE _refe;
    SelEngine _sele;
    SelTemp _selt;
    SelBackup _selb;
    SelBackupBeforeSave _selbs;
    SelWallpaper _selw;
    ClearHist _clearh;
    ClearSHist _clearsh;
    ClearPHist _clearph;
    SelectMenu _selectmenu;
    ModMenu _modmenu;
    DelMenuAccel _delmenuaccel;
    ApplyMenu _applymenu;

    void construct1(lib::TabView& tabf);
    void construct2(lib::TabView& tabf);
    void construct3(lib::TabView& tabf);
    void construct4(lib::TabView& tabf);
    void construct5(lib::TabView& tabf);

    template <typename I>
    [[nodiscard]] Combo createEnumC(
        lib::Panel& group, String title, const lib::Array<I>& values,
        const lib::Array<String>& names, std::unordered_map<I, int>& tableA,
        std::unordered_map<int, I>& tableB, int hSpan = 1) {
        (void)hSpan;
        auto combo = std::make_shared<lib::ComboBox>();
        combo->accessibility_name = std::move(title);
        for (std::size_t i = 0; i < values.size(); ++i) {
            const String label = i < names.size() ? names[i] : std::to_string(i);
            combo->items.push_back({label, {}, 0, true});
            tableA[values[i]] = static_cast<int>(i);
            tableB[static_cast<int>(i)] = values[i];
        }
        if (!values.empty()) combo->selected_index = 0;
        group.add(combo);
        return combo;
    }

    void syncControlsFromProperties();
    void updateOverride(bool bgImages, bool selections, bool keyCodes);
    void setComboItems(const Combo& combo, const lib::Array<String>& values);
    [[nodiscard]] int comboIndex(const Combo& combo, int fallback = 0) const;
    [[nodiscard]] String comboValue(const Combo& combo, String fallback = {}) const;
    [[nodiscard]] bool validEnginePath(String value) const;
    [[nodiscard]] lib::Array<String> splitLines(String value) const;
    [[nodiscard]] bool hasMenuSelection() const noexcept;

    void menuIncSearch();
    String dropEngine(const lib::Array<String>& files) const;
    String dropSysSound(const lib::Array<String>& files) const;
    String dropWallpaper(const lib::Array<String>& files) const;
    void selectEngine();
    Text selectSysSound(Text widget);
    void selectTemp();
    void selectBackup();
    void selectBackupBeforeSave();
    void selectWallpaper();
    void refHistories();
    void refSearchHistories();
    void refExecutedParties();
    [[nodiscard]] bool confirmClear(lib::StringView message) const;
    bool selectSkinType();
    void storeSkinControls();
    void updateOverrideBgImages();
    void editDefaultBgImages();
    void updateOverrideSelections();
    void updateOverrideKeyCodes();
    void selectInitializers();
    void updateInitializers();
    void selectInitializers2();
    void selectMenu();
    void applyMenu();
    void refreshMenu();
    void refreshScenario(summary::Summary* summ);
    void refreshEnabled();
    void refEngineEnabled();
    void refreshOpenButtonEnabledStates();

public:
    SettingsDialog(dprops::Props& prop,
                   std::shared_ptr<lib::Window> parent = {},
                   std::shared_ptr<properties::DockingFolderCTC> dock = {},
                   summary::Summary* summ = nullptr,
                   lib::Action sendReloadProps = {},
                   Callbacks callbacks = {},
                   lib::Services services = {});

    SettingsDialog(const SettingsDialog&) = delete;
    SettingsDialog& operator=(const SettingsDialog&) = delete;
    SettingsDialog(SettingsDialog&&) = delete;
    SettingsDialog& operator=(SettingsDialog&&) = delete;
    ~SettingsDialog() override;

    [[nodiscard]] dprops::Props& properties() const noexcept { return *_prop; }
    [[nodiscard]] const Callbacks& callbacks() const noexcept { return _comm; }
    [[nodiscard]] const std::shared_ptr<lib::TabView>& tabFolder() const noexcept {
        return _tabf;
    }
    [[nodiscard]] std::size_t publishedFeatureKeyCodeRows() const noexcept {
        const auto& table = _keyCodesByFeaturesView
                                ? _keyCodesByFeaturesView->widget()
                                : std::shared_ptr<lib::TableView>{};
        return table ? table->rows.size() : 0U;
    }
    [[nodiscard]] std::size_t featureKeyCodeValueCount() const noexcept {
        return _keyCodesByFeaturesView ? _keyCodesByFeaturesView->valueCount() : 0U;
    }
    [[nodiscard]] std::size_t publishedMotionKeyCodeRows() const noexcept {
        const auto& table = _keyCodesByMotionsView
                                ? _keyCodesByMotionsView->widget()
                                : std::shared_ptr<lib::TableView>{};
        return table ? table->rows.size() : 0U;
    }
    [[nodiscard]] std::size_t motionKeyCodeValueCount() const noexcept {
        return _keyCodesByMotionsView ? _keyCodesByMotionsView->valueCount() : 0U;
    }
    [[nodiscard]] int selectedSkinSettingsIndex() const noexcept {
        return _skinType.selected;
    }
    [[nodiscard]] skintypesettings::SkinTypeChooser* skinTypeChooser() const noexcept { return _skinTypeChooser.get(); }
    [[nodiscard]] auto backgroundTools() const noexcept { return _bgStgsView; }
    [[nodiscard]] auto outerTools() const noexcept { return _toolsView; }
    [[nodiscard]] auto menuBindings() const noexcept { return _menu; }
    [[nodiscard]] auto menuMnemonic() const noexcept { return _mnemonic; }
    [[nodiscard]] auto deleteMenuBinding() const noexcept { return _menuDel; }
    [[nodiscard]] bool noScenario() const noexcept;
    void refreshScenario(summary::Summary& summ) { refreshScenario(&summ); }
    [[nodiscard]] String dropEnginePath(const lib::Array<String>& files) const;
    [[nodiscard]] String dropSystemSound(const lib::Array<String>& files) const {
        return dropSysSound(files);
    }
    [[nodiscard]] String dropWallpaperPath(const lib::Array<String>& files) const {
        return dropWallpaper(files);
    }

protected:
    void setup(Container& area) override;
    [[nodiscard]] bool apply() override;
};

struct OldSettings {
    dprops::Props* prop{};
    const menu::MenuProps* menuProps{};
    std::uint32_t drawingScale{};
    std::uint32_t imageScale{};
    std::uint32_t drawingScalePercent{};
    std::uint32_t imageScalePercent{};
    std::uint32_t uiTextScale{};
    String targetVersion;
    String oldEnginePath;
    bool findEnginePath{};
    String oldWallpaper;
    std::int32_t oldWallpaperStyle{};
    lib::SharedSlice<String> oldSelections;
    lib::SharedSlice<String> oldKeyCodes;
    lib::Array<structs::OuterTool> tools;
    lib::Array<structs::ClassicEngine> cEngines;
    lib::Array<structs::EvTemplate> eventTemplates;
    lib::Array<String> oldIgnorePaths;
    bool oldSmoothingCard{};
    bool oldLogicalSort{};
    lib::Array<structs::OpenHistory> oldOpenHistories;
    lib::Array<structs::OpenHistory> importHistory;
    std::uint32_t historyMax{};
    lib::Array<String> oldSearchHistories;
    lib::Array<String> oldReplaceHistories;
    lib::Array<String> oldGrepDirHistories;
    std::int32_t oldUndoMaxMainView{};
    std::int32_t oldUndoMaxEvent{};
    std::int32_t oldUndoMaxReplace{};
    std::int32_t oldUndoMaxEtc{};
    std::int32_t oldDialogStatus{};
    std::int32_t showStatusTime{};
    std::unordered_map<types::MenuID, String> oldMnemonic;
    std::unordered_map<types::MenuID, String> oldHotkey;
    bool floatMessagePreview{};
    bool useMessageWindowColorInTextContentDialog{};
    std::int32_t bgmVolume{};
    std::int32_t seVolume{};
    std::int32_t soundPlayType{};
    std::int32_t soundEffectPlayType{};
    bool showHandMark{};
    bool showEventTreeMark{};
    bool showCardListHeader{};
    bool showCardListTitle{};
    bool showSkillCardLevel{};
    bool ignoreEmptyStart{};
    bool classicStyleTree{};
    bool radarStyleParams{};
    bool showAptitudeOnCastCardEditor{};
    bool showSpNature{};
    bool showVariableValuesInEventText{};
    bool useNamesAfterStandard{};
    bool selectVariableWithTree{};
    bool straightEventTreeView{};
    bool forceIndentBranchContent{};
    bool showTerminalMark{};
    bool showTargetStartLineNumber{};
    bool showSummaryInAreaTable{};
    bool showAreaDirTree{};
    bool showContentsGroupName{};
    bool showEventContentDescription{};
    bool contentsFloat{};
    bool contentsAutoHide{};
    bool showCloseButtonAllTab{};
    bool showMotionDescription{};
    bool xmlFileNameIsIDOnly{};
    bool replaceClassicResourceExtension{};
    bool saveInnerImagePath{};
    bool showSummaryPreview{};
    bool showMessagePreview{};
    structs::ToolBarSettings mainToolBar;
    String stepValueName;
    bool showCurrentValueOnTopAlways{};
    bool switchFixedWithCheckBox{};
    bool resizingImageWithRatio{};
    bool drawXORSelectionLine{};
    bool showSceneViewSelectionFilter{};
    bool saveSkinName{};
    bool showItemNumberOfSceneAndEventView{};
    structs::ExecutionParty lastExecutedParty;
    lib::Array<structs::ExecutionParty> executedParties;
    std::uint32_t executedPartiesMax{};
    bool useCoolBar{};
    lib::SharedSlice<structs::KeyCodeByFeature> keyCodesByFeatures;
    lib::Array<structs::KeyCodeByMotion> keyCodesByMotions;
    lib::SharedSlice<structs::BgImageSetting> bgImageSettings;
    lib::SharedSlice<structs::SettingsWithSkinType> settingsWithSkinTypes;

    OldSettings(dprops::Props& prop, const menu::MenuProps& menuProps);
    void raiseEvent(const Callbacks& callbacks);
};

}  // namespace cwxeditor::cwx::editor::gui::dwt::settingsdialog
