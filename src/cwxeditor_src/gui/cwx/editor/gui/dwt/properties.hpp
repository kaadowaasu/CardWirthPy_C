#pragma once

// cwx/editor/gui/dwt/properties.d
// の移植可能なC++20実装。 ウィンドウ・ダイ
// アログの配置はEditor設定と共通部品で表し
// 、設定XMLはcwx::xmlで扱う。 ツール
// キットのウィンドウやOSハンドルは公開しない。

#include "cwxeditor_src/core/cwx/filesync.hpp"
#include "cwxeditor_src/core/cwx/menu.hpp"
#include "cwxeditor_src/core/cwx/settings.hpp"
#include "cwxeditor_src/core/cwx/skin.hpp"
#include "cwxeditor_src/core/cwx/structs.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/core/cwx/variables.hpp"
#include "cwxeditor_src/core/cwx/versioninfo.hpp"
#include "cwxeditor_src/core/cwx/xml.hpp"
#include "cwxeditor_src/lib/components.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <exception>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cardwirth::platform {
class Storage;
}
namespace cwxeditor::lib {
struct Services;
}

namespace cwxeditor::cwx::editor::gui::dwt::dockingfolder {
class DockingFolder;
}

namespace cwxeditor::cwx::editor::gui::dwt::properties {

namespace lib = ::cwxeditor::lib;
namespace settings = ::cwxeditor::cwx::settings;
namespace structs = ::cwxeditor::cwx::structs;
namespace types = ::cwxeditor::cwx::types;
namespace variables = ::cwxeditor::cwx::variables;
namespace menu = ::cwxeditor::cwx::menu;
namespace xml = ::cwxeditor::cwx::xml;

using String = lib::String;
using StringView = lib::StringView;
using XNode = xml::XNode;
using Path = std::filesystem::path;

// 読み込みと適用で共用する管理セッションの既定値。 終了したルート内の
// パスとは異なり、現在のUserData内の有効なシナリオ参照は残す。
bool applyManagedSessionDefaults(variables::FlexEtcProps& properties,
                                 cardwirth::platform::Storage* storage);

/// プラットフォーム既定の配置値は-1とし、
/// 共通レイアウトでも番兵値として保持する。
inline constexpr std::int32_t DEFAULT = -1;

template <std::size_t N>
struct StringLiteral {
    char value[N]{};

    constexpr StringLiteral(const char (&text)[N]) {
        for (std::size_t i = 0; i < N; ++i) value[i] = text[i];
    }

    [[nodiscard]] constexpr std::string_view view() const noexcept {
        return {value, N - 1U};
    }
};

namespace detail {

// 現在の設定読み込みに限って項目の失敗を収集する。 入れ子の読み込
// み後は呼び出し元の収集先へ戻し、別スレッドと診断を共有しない。
class PropertyReadDiagnostics {
public:
    explicit PropertyReadDiagnostics(String& output)
        : previous_(current_) { current_ = &output; }
    ~PropertyReadDiagnostics() { current_ = previous_; }
    PropertyReadDiagnostics(const PropertyReadDiagnostics&) = delete;
    PropertyReadDiagnostics& operator=(const PropertyReadDiagnostics&) = delete;
    static void report(StringView key, const std::exception& error) {
        if (current_) {
            if (!current_->empty()) *current_ += "\n";
            *current_ += String(key) + ": " + error.what();
        }
        static_cast<void>(utils::printStackTrace());
        utils::debugln("settings field ", key, ": ", error.what());
    }
private:
    inline static thread_local String* current_{};
    String* previous_{};
};

template <typename Property>
void writeProperty(XNode& node, const Property& property, bool write_all = false) {
    if (write_all || !(property.value == property.INIT))
        property.toNode(node);
}

template <typename Property>
void readProperty(const XNode& parent, Property& property,
                  std::uint64_t data_version) {
    const auto child = parent.name() == property.KEY
                           ? parent
                           : parent.child(property.KEY, false);
    if (!child.valid() || property.CHG_VERSION > data_version) return;
    try {
        // 一時値で変換し、複合項目の途中で
        // 失敗しても元の値・既定値を保つ。
        auto loaded = property;
        loaded.fromNode(child, data_version);
        property = std::move(loaded);
    } catch (const std::exception& error) {
        PropertyReadDiagnostics::report(property.KEY, error);
    }
}

[[nodiscard]] inline XNode elementFor(const XNode& parent, StringView name) {
    return parent.name() == name ? parent : parent.child(name, false);
}

}  // namespace detail

template <StringLiteral Name, std::int32_t Width,
          std::int32_t Height, std::uint64_t SizeChgVersion = 0>
class WindowProps : public settings::Properties, public structs::WSize {
public:
    inline static constexpr StringView XML_NAME = Name.view();

    settings::Prop<bool> _maximized{"maximized", false};
    settings::Prop<bool> _minimized{"minimized", false};
    settings::Prop<std::int32_t, false, true> _x{"x", DEFAULT};
    settings::Prop<std::int32_t, false, true> _y{"y", DEFAULT};
    settings::Prop<std::int32_t, false, true> _width{"width", Width,
                                                       SizeChgVersion};
    settings::Prop<std::int32_t, false, true> _height{"height", Height,
                                                        SizeChgVersion};
    settings::Prop<bool> _visible{"visible", true};

    [[nodiscard]] bool maximized() const noexcept { return _maximized.value; }
    void maximized(bool value) noexcept { _maximized.value = value; }
    [[nodiscard]] bool maximized() noexcept override { return _maximized.value; }
    [[nodiscard]] bool minimized() const noexcept { return _minimized.value; }
    void minimized(bool value) noexcept { _minimized.value = value; }
    [[nodiscard]] std::int32_t x() const noexcept { return _x.value; }
    void x(std::int32_t value) noexcept override { _x.value = value; }
    [[nodiscard]] std::int32_t x() noexcept override { return _x.value; }
    [[nodiscard]] std::int32_t y() const noexcept { return _y.value; }
    void y(std::int32_t value) noexcept override { _y.value = value; }
    [[nodiscard]] std::int32_t y() noexcept override { return _y.value; }
    [[nodiscard]] std::int32_t width() const noexcept { return _width.value; }
    void width(std::int32_t value) noexcept override { _width.value = value; }
    [[nodiscard]] std::int32_t width() noexcept override { return _width.value; }
    [[nodiscard]] std::int32_t height() const noexcept { return _height.value; }
    void height(std::int32_t value) noexcept override { _height.value = value; }
    [[nodiscard]] std::int32_t height() noexcept override { return _height.value; }
    [[nodiscard]] bool visible() const noexcept { return _visible.value; }
    void visible(bool value) noexcept { _visible.value = value; }

    void toNode(XNode& parent, bool write_all = false) const {
        auto element = parent.newElement(XML_NAME, String{});
        detail::writeProperty(element, _maximized, write_all);
        detail::writeProperty(element, _minimized, write_all);
        detail::writeProperty(element, _x, write_all);
        detail::writeProperty(element, _y, write_all);
        detail::writeProperty(element, _width, write_all);
        detail::writeProperty(element, _height, write_all);
        detail::writeProperty(element, _visible, write_all);
    }
    [[nodiscard]] XNode toNode(bool write_all = false) const {
        auto element = XNode::create(XML_NAME, String{});
        detail::writeProperty(element, _maximized, write_all);
        detail::writeProperty(element, _minimized, write_all);
        detail::writeProperty(element, _x, write_all);
        detail::writeProperty(element, _y, write_all);
        detail::writeProperty(element, _width, write_all);
        detail::writeProperty(element, _height, write_all);
        detail::writeProperty(element, _visible, write_all);
        return element;
    }
    static WindowProps fromNode(const XNode& parent,
                                std::uint64_t data_version = std::numeric_limits<std::uint64_t>::max()) {
        WindowProps result;
        const auto element = detail::elementFor(parent, XML_NAME);
        if (!element.valid()) return result;
        detail::readProperty(element, result._maximized, data_version);
        detail::readProperty(element, result._minimized, data_version);
        detail::readProperty(element, result._x, data_version);
        detail::readProperty(element, result._y, data_version);
        detail::readProperty(element, result._width, data_version);
        detail::readProperty(element, result._height, data_version);
        detail::readProperty(element, result._visible, data_version);
        return result;
    }
};

class MainWin : public settings::Properties, public structs::WSize {
public:
    inline static constexpr StringView XML_NAME = "mainWindow";

    settings::Prop<std::int32_t, false, true> _x{"x", DEFAULT};
    settings::Prop<std::int32_t, false, true> _y{"y", DEFAULT};
    settings::Prop<std::int32_t, false, true> _width{"width", 1024};
    settings::Prop<std::int32_t, false, true> _height{"height", 768};
    settings::Prop<bool> _maximized{"maximized", false};

    [[nodiscard]] std::int32_t x() const noexcept { return _x.value; }
    void x(std::int32_t value) noexcept override { _x.value = value; }
    [[nodiscard]] std::int32_t x() noexcept override { return _x.value; }
    [[nodiscard]] std::int32_t y() const noexcept { return _y.value; }
    void y(std::int32_t value) noexcept override { _y.value = value; }
    [[nodiscard]] std::int32_t y() noexcept override { return _y.value; }
    [[nodiscard]] std::int32_t width() const noexcept { return _width.value; }
    void width(std::int32_t value) noexcept override { _width.value = value; }
    [[nodiscard]] std::int32_t width() noexcept override { return _width.value; }
    [[nodiscard]] std::int32_t height() const noexcept { return _height.value; }
    void height(std::int32_t value) noexcept override { _height.value = value; }
    [[nodiscard]] std::int32_t height() noexcept override { return _height.value; }
    [[nodiscard]] bool maximized() const noexcept { return _maximized.value; }
    void maximized(bool value) noexcept override { _maximized.value = value; }
    [[nodiscard]] bool maximized() noexcept override { return _maximized.value; }

    void toNode(XNode& parent, bool write_all = false) const;
    [[nodiscard]] XNode toNode(bool write_all = false) const;
    static MainWin fromNode(const XNode& parent,
                            std::uint64_t data_version = std::numeric_limits<std::uint64_t>::max());
};

class ContWin : public settings::Properties {
public:
    inline static constexpr StringView XML_NAME = "contentsWindow";

    settings::Prop<std::int32_t, false, true> _x{"x", DEFAULT};
    settings::Prop<std::int32_t, false, true> _y{"y", DEFAULT};

    [[nodiscard]] std::int32_t x() const noexcept { return _x.value; }
    void x(std::int32_t value) noexcept { _x.value = value; }
    [[nodiscard]] std::int32_t y() const noexcept { return _y.value; }
    void y(std::int32_t value) noexcept { _y.value = value; }
    void toNode(XNode& parent, bool write_all = false) const;
    [[nodiscard]] XNode toNode(bool write_all = false) const;
    static ContWin fromNode(const XNode& parent,
                            std::uint64_t data_version = std::numeric_limits<std::uint64_t>::max());
};

template <StringLiteral Name, std::int32_t WidthDef = DEFAULT,
          std::int32_t HeightDef = DEFAULT,
          std::uint64_t SizeChgVersion = 0>
class DialogParam : public settings::Properties, public structs::DSize {
public:
    inline static constexpr StringView XML_NAME = Name.view();
    settings::Prop<std::int32_t, false, true> _width{"width", WidthDef,
                                                       SizeChgVersion};
    settings::Prop<std::int32_t, false, true> _height{"height", HeightDef,
                                                        SizeChgVersion};

    [[nodiscard]] std::int32_t width() const noexcept { return _width.value; }
    void width(std::int32_t value) noexcept override { _width.value = value; }
    [[nodiscard]] std::int32_t width() noexcept override { return _width.value; }
    [[nodiscard]] std::int32_t height() const noexcept { return _height.value; }
    void height(std::int32_t value) noexcept override { _height.value = value; }
    [[nodiscard]] std::int32_t height() noexcept override { return _height.value; }

    void toNode(XNode& parent, bool write_all = false) const {
        auto element = parent.newElement(XML_NAME, String{});
        detail::writeProperty(element, _width, write_all);
        detail::writeProperty(element, _height, write_all);
    }
    [[nodiscard]] XNode toNode(bool write_all = false) const {
        auto element = XNode::create(XML_NAME, String{});
        detail::writeProperty(element, _width, write_all);
        detail::writeProperty(element, _height, write_all);
        return element;
    }
    static DialogParam fromNode(const XNode& parent,
                                std::uint64_t data_version = std::numeric_limits<std::uint64_t>::max()) {
        DialogParam result;
        const auto element = detail::elementFor(parent, XML_NAME);
        if (!element.valid()) return result;
        detail::readProperty(element, result._width, data_version);
        detail::readProperty(element, result._height, data_version);
        return result;
    }
};

template <StringLiteral Name, std::int32_t Width,
          std::int32_t Height, std::uint64_t SizeChgVersion = 0>
class EventWin : public settings::Properties {
public:
    inline static constexpr StringView XML_NAME = Name.view();
    settings::Prop<std::int32_t> eventSashL{"eventSashL", 2};
    settings::Prop<std::int32_t> eventSashR{"eventSashR", 7};

    void toNode(XNode& parent, bool write_all = false) const {
        auto element = parent.newElement(XML_NAME, String{});
        detail::writeProperty(element, eventSashL, write_all);
        detail::writeProperty(element, eventSashR, write_all);
    }
    [[nodiscard]] XNode toNode(bool write_all = false) const {
        auto element = XNode::create(XML_NAME, String{});
        detail::writeProperty(element, eventSashL, write_all);
        detail::writeProperty(element, eventSashR, write_all);
        return element;
    }
    static EventWin fromNode(const XNode& parent,
                             std::uint64_t data_version = std::numeric_limits<std::uint64_t>::max()) {
        EventWin result;
        const auto element = detail::elementFor(parent, XML_NAME);
        if (!element.valid()) return result;
        detail::readProperty(element, result.eventSashL, data_version);
        detail::readProperty(element, result.eventSashR, data_version);
        return result;
    }
};

template <StringLiteral Name, std::int32_t Width,
          std::int32_t Height, std::uint64_t SizeChgVersion = 0>
class ToolWin : public settings::Properties, public structs::DSize {
public:
    inline static constexpr StringView XML_NAME = Name.view();
    settings::Prop<std::int32_t, false, true> _x{"x", DEFAULT};
    settings::Prop<std::int32_t, false, true> _y{"y", DEFAULT};
    settings::Prop<std::int32_t, false, true> _width{"width", Width,
                                                       SizeChgVersion};
    settings::Prop<std::int32_t, false, true> _height{"height", Height,
                                                        SizeChgVersion};

    [[nodiscard]] std::int32_t x() const noexcept { return _x.value; }
    void x(std::int32_t value) noexcept { _x.value = value; }
    [[nodiscard]] std::int32_t y() const noexcept { return _y.value; }
    void y(std::int32_t value) noexcept { _y.value = value; }
    [[nodiscard]] std::int32_t width() const noexcept { return _width.value; }
    void width(std::int32_t value) noexcept override { _width.value = value; }
    [[nodiscard]] std::int32_t width() noexcept override { return _width.value; }
    [[nodiscard]] std::int32_t height() const noexcept { return _height.value; }
    void height(std::int32_t value) noexcept override { _height.value = value; }
    [[nodiscard]] std::int32_t height() noexcept override { return _height.value; }

    void toNode(XNode& parent, bool write_all = false) const {
        auto element = parent.newElement(XML_NAME, String{});
        detail::writeProperty(element, _x, write_all);
        detail::writeProperty(element, _y, write_all);
        detail::writeProperty(element, _width, write_all);
        detail::writeProperty(element, _height, write_all);
    }
    [[nodiscard]] XNode toNode(bool write_all = false) const {
        auto element = XNode::create(XML_NAME, String{});
        detail::writeProperty(element, _x, write_all);
        detail::writeProperty(element, _y, write_all);
        detail::writeProperty(element, _width, write_all);
        detail::writeProperty(element, _height, write_all);
        return element;
    }
    static ToolWin fromNode(const XNode& parent,
                            std::uint64_t data_version = std::numeric_limits<std::uint64_t>::max()) {
        ToolWin result;
        const auto element = detail::elementFor(parent, XML_NAME);
        if (!element.valid()) return result;
        detail::readProperty(element, result._x, data_version);
        detail::readProperty(element, result._y, data_version);
        detail::readProperty(element, result._width, data_version);
        detail::readProperty(element, result._height, data_version);
        return result;
    }
};

using AreaSceneWin = EventWin<"areaSceneWindow", DEFAULT, DEFAULT>;
using AreaEventWin = EventWin<"areaEventWindow", DEFAULT, DEFAULT>;
using BattleSceneWin = EventWin<"battleSceneWindow", DEFAULT, DEFAULT>;
using BattleEventWin = EventWin<"battleEventWindow", DEFAULT, DEFAULT>;
using PackageWin = EventWin<"packageWindow", 800, 520>;
using CardEventWin = EventWin<"cardEventWindow", 800, 520>;

/// 保存済みドック配置の共通表現。
class DockingFolderCTC final {
public:
    using ContainerPtr = std::shared_ptr<lib::Container>;
    using HasCloseButton = std::function<bool(StringView)>;
    using CanVanish = std::function<bool(const DockingFolderCTC&, StringView)>;
    using CreateControl = std::function<lib::WidgetPtr(ContainerPtr, StringView)>;
    using FirstResize = std::function<bool(StringView)>;

    XNode layout{};
    String first_pane{};

    DockingFolderCTC() = default;
    explicit DockingFolderCTC(XNode value) : layout(std::move(value)) {}

    [[nodiscard]] bool valid() const noexcept { return layout.valid(); }
    void toNode(XNode& parent,
                const lib::Array<String>& exclude = {}) const;

    static std::shared_ptr<DockingFolderCTC> fromNode(
        const XNode& node, ContainerPtr parent, std::int32_t style,
        HasCloseButton has_close_button, CanVanish can_vanish,
        CreateControl create, FirstResize first_resize = {});

private:
    // 従来のデータ転送型と通知APIを残し、実際のUI復
    // 元・保存は共通DockingFolderへ通す。
    std::shared_ptr<::cwxeditor::cwx::editor::gui::dwt::dockingfolder::DockingFolder>
        implementation_{};
};

class FlexProps final {
public:
    MainWin mainWin{};
    AreaSceneWin areaSceneWin{};
    AreaEventWin areaEventWin{};
    BattleSceneWin battleSceneWin{};
    BattleEventWin battleEventWin{};
    PackageWin packageWin{};
    CardEventWin cardEventWin{};
    ContWin contentsWin{};
    DialogParam<"settingsDialog", DEFAULT, DEFAULT, 2012080500> settingsDlg{};
    ToolWin<"featuresWindow", DEFAULT, 300> featuresWin{};
    WindowProps<"replaceDialog", 900, DEFAULT, 2012090100> replaceDlg{};
    DialogParam<"summaryDialog", DEFAULT, DEFAULT, 2012101100> summaryDlg{};
    DialogParam<"menuCardDialog", DEFAULT, DEFAULT, 2012101100> menuCardDlg{};
    DialogParam<"areaBackgroundDialog"> areaBackgroundDlg{};
    DialogParam<"areaBackgroundNFDialog"> areaBackgroundNFDlg{};
    DialogParam<"areaTextCellDialog", 750, 650, 2014111700> areaTextCellDlg{};
    DialogParam<"areaTextCellNFDialog", 750, 650, 2014111700> areaTextCellNFDlg{};
    DialogParam<"areaColorCellDialog", 700, 450> areaColorCellDlg{};
    DialogParam<"areaColorCellNFDialog"> areaColorCellNFDlg{};
    DialogParam<"areaPCCellDialog"> areaPCCellDlg{};
    DialogParam<"areaPCCellNFDialog"> areaPCCellNFDlg{};
    DialogParam<"enemyCardDialog"> enemyCardDlg{};
    DialogParam<"castCardDialog", DEFAULT, DEFAULT, 2012101100> castCardDlg{};
    DialogParam<"skillCardDialog", DEFAULT, DEFAULT, 2012101100> skillCardDlg{};
    DialogParam<"itemCardDialog", DEFAULT, DEFAULT, 2012101100> itemCardDlg{};
    DialogParam<"beastCardDialog", DEFAULT, DEFAULT, 2012101100> beastCardDlg{};
    DialogParam<"infoCardDialog", DEFAULT, DEFAULT, 2012101100> infoCardDlg{};
    DialogParam<"bgImagesDialog", 1000> bgImagesDlg{};
    DialogParam<"defaultBgImagesDialog", 900> defBgImagesDlg{};
    DialogParam<"flagDialog", 400, 200, 2017032600> flagDlg{};
    DialogParam<"stepDialog", 400, 400, 2015111500> stepDlg{};
    DialogParam<"variantDialog", -1, -1> variantDlg{};
    DialogParam<"newScenarioDialog", DEFAULT, DEFAULT, 2012072100> newScDlg{};
    WindowProps<"speakDialog", DEFAULT, DEFAULT, 2012111500> speakDlg{};
    WindowProps<"messageDialog", DEFAULT, DEFAULT, 2012101100> msgDlg{};
    DialogParam<"branchMemberDialog", DEFAULT, 400> brMemberDlg{};
    DialogParam<"cardEventDialog", DEFAULT, DEFAULT, 2019011300> cardEvtDlg{};
    DialogParam<"flagEventDialog", 350> flagEvtDlg{};
    DialogParam<"effectEventDialog", DEFAULT, DEFAULT, 2019033000> effEvtDlg{};
    DialogParam<"soundEventDialog"> soundEvtDlg{};
    DialogParam<"couponEventDialog", DEFAULT, DEFAULT, 2019033000> couponEvtDlg{};
    DialogParam<"multiCouponEventDialog", DEFAULT, DEFAULT, 2019033000> multiCouponEvtDlg{};
    DialogParam<"inputEventDialog"> inputEvtDlg{};
    DialogParam<"gossipEventDialog"> gossipEvtDlg{};
    DialogParam<"selectEventDialog"> selEvtDlg{};
    DialogParam<"loseBgImageEventDialog"> loseBgImageEvtDlg{};
    DialogParam<"scriptDialog", 400, 300> scriptDlg{};
    DialogParam<"commentDialog", 350, 200, 2013080800> commentDlg{};
    WindowProps<"dialogPreview", DEFAULT, 500> dlgPrev{};
    WindowProps<"messagePreview", DEFAULT, 500> msgPrev{};
    WindowProps<"scriptVariablesDialog", 400, 400> scriptVarSetDlg{};
    DialogParam<"flagCombiDialog", 350> flagCombiDlg{};
    DialogParam<"eventTemplateDialog", 600, 400> evTemplDlg{};
    DialogParam<"toolBarCustomizeDialog", 650, 400> toolBarCustomDlg{};
    DialogParam<"importResultDialog", 400, 400> importResultDlg{};
    DialogParam<"scenarioLoadErrorsDialog", 300, 300> scenarioLoadErrorsDlg{};
    DialogParam<"eventIgnitionsDialog", 350, 300> eventIgnitionsDlg{};
    DialogParam<"eventIgnitionsWithRoundsDialog", 450, 300> eventIgnitionsWithRoundsDlg{};
    DialogParam<"scenarioHistoryDialog", 500, 350> scenarioHistoryDlg{};
    DialogParam<"executedPartyHistoryDialog", 500, 350> executedPartyHistoryDlg{};
    DialogParam<"expressionDialog", 500, DEFAULT> expressionDlg{};
    DialogParam<"expressionWithTargetDialog", 600, DEFAULT> expressionWithTargetDlg{};
    DialogParam<"xmlContentDialog", 600, 400> xmlContentDlg{};
    DialogParam<"selectFeaturesDialog", 300, 300> selectFeaturesDlg{};
    DialogParam<"addKeyCodesErrorDialog", 300, 300> addKeyCodesErrorDlg{};
    DialogParam<"editSkinTypeDialog", 300, DEFAULT> editSkinTypeDlg{};
    menu::MenuProps menu{};
    variables::FlexEtcProps etc{};

    explicit FlexProps(String app_path, const lib::Services& services,
                       String conf_file_name = {},
                       cardwirth::platform::Storage* storage = nullptr);
    // 設定全体のスナップショットはコピー可能にする。dprops::Props
    // が設定画面の編集に使い、 中止時にはFlexEtcProps
    // だけでなく、配置・メニューのショートカット
    // ・ツールバー順・ドック設定もまとめて戻す。
    FlexProps(const FlexProps&) = default;
    FlexProps& operator=(const FlexProps&) = default;
    FlexProps(FlexProps&&) noexcept = default;
    FlexProps& operator=(FlexProps&&) noexcept = default;
    ~FlexProps() = default;

    [[nodiscard]] const String& cwxDir() const noexcept { return cwx_dir_; }
    void cleanup() noexcept;
    [[nodiscard]] bool reload();
    void delNodeTemp() noexcept { node_ = {}; }

    using ContainerPtr = DockingFolderCTC::ContainerPtr;
    using HasCloseButton = DockingFolderCTC::HasCloseButton;
    using CanVanish = DockingFolderCTC::CanVanish;
    using CreateControl = DockingFolderCTC::CreateControl;
    using FirstResize = DockingFolderCTC::FirstResize;

    [[nodiscard]] std::shared_ptr<DockingFolderCTC> loadDock(
        ContainerPtr parent, std::int32_t style,
        HasCloseButton has_close_button, CanVanish can_vanish,
        CreateControl create, FirstResize first_resize = {});
    void save(const std::shared_ptr<DockingFolderCTC>& dock);
    void save(String xml_file_name,
              const std::shared_ptr<DockingFolderCTC>& dock);

    /// 保存対象のウィンドウ・メニュー・変数プロパティをすべ
    /// てノードへ追加する。 dpropsの編集管理が、etc
    /// 部分だけでなく設定全体を比較・記録するために使う。
    void toNode(XNode& node) const { writeNode(node); }
    [[nodiscard]] XNode toNode() const {
        auto node = XNode::create("cwxeditor", String{});
        writeNode(node);
        return node;
    }

private:
    enum class IniLocation { Standard, Local, Copy, Nothing };

    IniLocation location_{IniLocation::Standard};
    String path_{};
    String app_path_{};
    String cwx_dir_{};
    XNode node_{};
    // dpi_はメモリ内の配置の基準DPI。読み込み中は
    // 保存されたDPIへ一時的に置き換える。 使用・再
    // 保存する前に、reload()や構築処理でcurrentDisplayDpi()
    // へ変換すること。
    std::int32_t dpi_{96};
    bool no_file_{};
    String no_file_temp_{};
    const lib::Services* services_{};
    cardwirth::platform::Storage* storage_{};

    [[nodiscard]] std::int32_t currentDisplayDpi() const noexcept;
    [[nodiscard]] bool updatePPIs(std::int32_t dpi);
    [[nodiscard]] bool reloadImpl(bool force, String& diagnostics);
    void createBackup() noexcept;
    void writeNode(XNode& node) const;
};

}  // namespace cwxeditor::cwx::editor::gui::dwt::properties
