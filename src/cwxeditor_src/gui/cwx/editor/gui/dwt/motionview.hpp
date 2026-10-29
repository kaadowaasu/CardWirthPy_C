#pragma once
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/comment.hpp"

// cwx/editor/gui/dwt/motionview.d
// の移植可能なC++20実装。 MotionView
// が順序付きの効果モデルを所有し、同じ編
// 集操作を共通libの部品・サービス経
// 由で公開する。 SWT部品・D互換ラ
// ッパー・OSハンドルは持ち込まない。

#include "cwxeditor_src/core/cwx/card.hpp"
#include "cwxeditor_src/core/cwx/motion.hpp"
#include "cwxeditor_src/core/cwx/path.hpp"
#include "cwxeditor_src/core/cwx/summary.hpp"
#include "cwxeditor_src/core/cwx/system.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/core/cwx/usecounter.hpp"
#include "cwxeditor_src/core/cwx/versioninfo.hpp"
#include "cwxeditor_src/core/cwx/xml.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/flagspane.hpp"
#include "cwxeditor_src/lib/clipboard.hpp"
#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/drag_drop.hpp"
#include "cwxeditor_src/lib/events.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::dprops { class Props; }
namespace cwxeditor::cwx::editor::gui::dwt::commons { class Commons; }
namespace cwxeditor::cwx::skin { class Skin; }

namespace cwxeditor::cwx::editor::gui::dwt::motionview {

namespace card = ::cwxeditor::cwx::card;
namespace lib = ::cwxeditor::lib;
namespace motion = ::cwxeditor::cwx::motion;
namespace pane = ::cwxeditor::cwx::editor::gui::dwt::flagspane;
namespace path = ::cwxeditor::cwx::path;
namespace summary = ::cwxeditor::cwx::summary;
namespace system = ::cwxeditor::cwx::system;
namespace types = ::cwxeditor::cwx::types;
namespace usecounter = ::cwxeditor::cwx::usecounter;
namespace xml = ::cwxeditor::cwx::xml;
namespace dprops = ::cwxeditor::cwx::editor::gui::dwt::dprops;
namespace skin = ::cwxeditor::cwx::skin;

using String = lib::String;
using StringView = lib::StringView;
using Parent = pane::Parent;
using PanelPtr = pane::PanelPtr;
using Commons = pane::Commons;
using UndoManager = pane::UndoManager;
using Motion = motion::Motion;
using MotionPtr = lib::Owner<Motion>;
using BeastCard = card::BeastCard;
using BeastPtr = lib::Owner<BeastCard>;
using Summary = summary::Summary;
using SummaryPtr = lib::Owner<Summary>;
using CWXPath = path::CWXPath;
using UseCounter = usecounter::UseCounter;
using UseCounterPtr = lib::Owner<UseCounter>;
using SelectionEvent = pane::SelectionEvent;
using TreeItemId = lib::TreeItemId;

/// Dのビューがアプリ全体のプロパティから読む文字列
/// と制限値。 全体のPropsに依存せず、ヘッドレ
/// スでも既定値を使え、ホストから置き換えられる。
struct MotionMessages {
    String motionKind{"Motion"};
    String motionElement{"Element"};
    String motionBeast{"Beast"};
    String motionRound{"Round"};
    String motionEnhValue{"Ability"};
    String motionDamageType{"Value type"};
    String motionValue{"Value"};
    String beastNone{"(none)"};
    String setBeast{"Set beast"};
    String linkOption{"Reference options"};
    String cardIsReference{"「%1$s.%2$s」を参照しています"};
    String referencedCardIsNotFound{"参照先のカードが見つかりません(ID:%1$s)"};
    String warningUnknownMotion{"効果 [%1$s] は、CardWirth %2$sより前のバージョンでは使用できません。"};
    String warningUnknownMotionWsn{"効果 [%1$s] は、Wsn.%2$sより前のバージョンでは使用できません。"};
    String searchErrorLinkIdBeastNotFound{"参照先の召喚獣カード(ID:%1$s)が見つかりません。"};
    String warningSkillPowerWithFixedValue{
        "精神力操作の固定値指定は、Wsn.1以降の形式のシナリオしか行えません。"};
    String rangeHint{"{0}..{1}"};
    String noSelection{"No motion selected"};
    String noBeast{"No beast"};
    String unknown{"Unknown"};
    String copied{"Motion copied"};
    String pasted{"Motion pasted"};
    String deleted{"Motion deleted"};
    String descriptionSeparator{"\n"};
    String groupVitality{"Vitality"};
    String groupPhysical{"Physical"};
    String groupSkill{"Skill"};
    String groupMental{"Mental"};
    String groupMagic{"Magic"};
    String groupEnhance{"Enhance"};
    String groupVanish{"Vanish"};
    String groupCard{"Card"};
    String groupBeast{"Beast"};
    String groupEtc{"Other"};
    String undo{"Undo"};
    String redo{"Redo"};
    String deleteMotion{"Delete"};
    String up{"Up"};
    String down{"Down"};
    String comment{"Comment"};
    String newMotion{"New motion"};
    String cut{"Cut"};
    String copy{"Copy"};
    String paste{"Paste"};
    String clone{"Duplicate"};
    String motionRowFormat{"%1$s - %2$s"};

    std::unordered_map<types::MType, String> names;
    std::unordered_map<types::MType, String> descriptions;

    [[nodiscard]] String motionName(types::MType type) const;
    [[nodiscard]] String motionDescription(types::MType type) const;
    [[nodiscard]] String elementName(types::Element element) const;
    [[nodiscard]] String damageTypeName(types::DamageType type) const;
};

struct MotionEtcProperties {
    std::size_t undoMaxEtc{100};
    std::size_t motionsWidth{260};
    std::size_t comboVisibleItemCount{12};
    std::uint32_t roundMax{100};
    std::uint32_t uValueMax{100};
    std::uint32_t skillPowerMax{100};
    std::uint32_t beastMaxNest{999};
    std::int32_t imageScale{100};
    bool showMotionDescription{false};
    bool linkCard{true};
    bool ignorePaths{false};
    bool xmlCopy{false};
};

struct MotionLookProperties {
    std::int32_t motionRoundDefault{10};
    std::int32_t cardWidth{160};
    std::int32_t cardHeight{220};
};

/// MotionViewが使うDのdprops::Propsの具体的な部分型。
struct Props {
    MotionEtcProperties etc{};
    MotionLookProperties looks{};
    MotionMessages msgs{};
    const system::System* sys{};
    lib::Services services{};
    // 画像はEditorの資源管理から取得する。MotionView
    // は共通のResourceIdだけを使い、 Windows
    // ・SDL側に効果種別とファイルの対応を重複させない。
    std::function<lib::ResourceId(types::MType)> motionIcon{};
    std::function<lib::ResourceId(types::Element)> elementIcon{};
    std::function<String(types::Element)> elementText{};
    std::function<String(types::Element)> elementTooltip{};
    lib::ResourceId deleteIcon{};
    lib::ResourceId upIcon{};
    lib::ResourceId downIcon{};
    std::function<bool(Summary*, const BeastPtr&, bool)> openUseEvents{};
    // Dと同じく、開いたイベント画面には未消費のCWXパスの残りを渡して移動する。
    std::function<bool(Summary*, const BeastPtr&, StringView, bool)> openUseEventsPath{};
    std::function<void(const BeastPtr&)> closeUseEvents{};
    std::function<bool(Summary*, const lib::Owner<CWXPath>&, const BeastPtr&,
                       bool, StringView, bool, lib::Action, lib::Action,
                       lib::Action)> openBeastCard{};
    std::function<bool(StringView, bool)> openCWXPath{};
    std::function<bool()> xmlCopyProvider{};
    [[nodiscard]] bool xmlCopy() const {
        return xmlCopyProvider ? xmlCopyProvider() : etc.xmlCopy;
    }
    std::function<bool()> linkCardProvider{};
    [[nodiscard]] bool linkCard() const {
        return linkCardProvider ? linkCardProvider() : etc.linkCard;
    }
    std::function<bool(StringView, bool, bool)> openFilePath{};
    // 共通のdutilsのカード合成へ渡す、所有権のないホスト表示情報。 所
    // 有するEventDialogはMotionViewより長く存続する。
    const dprops::Props* editorProps{};
    std::function<std::function<void()>(const std::shared_ptr<lib::Window>&)> commentSizeBinder{};
    const skin::Skin* skin{};
    std::weak_ptr<lib::Window> dialogOwner{};

    [[nodiscard]] bool isTargetVersion(const Summary* summ,
                                       StringView version) const;
    [[nodiscard]] bool targetVersion(const Summary* summ,
                                     StringView version) const;
};

struct MotionRow {
    MotionPtr value{};
    String name;
    String element;
    String parameters;
    std::vector<String> warnings;
};

struct BeastChoice {
    BeastPtr value{};
    String text;
};

enum class ParameterPane {
    None,
    Value,
    Round,
    Ability,
    Beast,
    SkillPower,
};

/// Dのリスナーを、共通の値を受ける具体的なアダプターで
/// 表す。 入れ子の型として残し、元の所有関係を使うコー
/// ドがMotionViewごとに一つ持てるようにする。
class MotionView final {
public:
    pane::Signal<void()> warningEvent;
    pane::Signal<void()> modEvent;
    pane::Signal<void()> selectionChanged;
    pane::Signal<void()> modelChanged;
    pane::Signal<void(const MotionPtr&)> motionChanged;
    pane::Signal<void(const BeastPtr&)> beastChanged;

    static constexpr int ReadOnlyStyle = 1;

    class MVUndo {
    protected:
        MotionView* view_{};
        Commons comm_{};
        Summary* summary_{};
        std::ptrdiff_t selected_{-1};
        std::ptrdiff_t selectedBefore_{-1};

        MVUndo(MotionView* view, Commons comm, Summary* summary);
        void save(MotionView* view);
        void udb(MotionView* view);
        void uda(MotionView* view);
        [[nodiscard]] MotionView* view() const noexcept { return view_; }

    public:
        virtual ~MVUndo() = default;
        virtual void undo() = 0;
        virtual void redo() = 0;
        virtual void dispose();
    };

    class UndoEdit final : public MVUndo {
    public:
        UndoEdit(MotionView* view, Commons comm, Summary* summary,
                 std::ptrdiff_t index);
        void undo() override;
        void redo() override;
        void dispose() override;

    private:
        void impl();
        MotionPtr old_{};
        std::ptrdiff_t index_{-1};
    };

    class UndoSwap final : public MVUndo {
    public:
        UndoSwap(MotionView* view, Commons comm, Summary* summary,
                 std::ptrdiff_t first, std::ptrdiff_t second);
        void undo() override;
        void redo() override;
        void dispose() override;

    private:
        void impl();
        std::ptrdiff_t first_{-1};
        std::ptrdiff_t second_{-1};
    };

    class UndoMove final : public MVUndo {
    public:
        UndoMove(MotionView* view, Commons comm, Summary* summary,
                 std::ptrdiff_t from, std::ptrdiff_t to);
        void undo() override;
        void redo() override;
        void dispose() override;

    private:
        void impl();
        std::ptrdiff_t from_{-1};
        std::ptrdiff_t to_{-1};
    };

    class UndoInsertDelete final : public MVUndo {
    public:
        UndoInsertDelete(MotionView* view, Commons comm, Summary* summary,
                         std::ptrdiff_t index, bool insert);
        void undo() override;
        void redo() override;
        void dispose() override;

    private:
        void initUndoDelete(MotionView* view);
        void undoInsert();
        void undoDelete();
        std::ptrdiff_t index_{-1};
        bool insert_{};
        MotionPtr motion_{};
    };

    class DamageTypeListener final {
    public:
        explicit DamageTypeListener(MotionView& view) : view_(&view) {}
        void widgetSelected(types::DamageType value);
        MotionView* view_{};
        std::unordered_map<types::DamageType, bool> table;
        std::shared_ptr<lib::NumberEdit> valueSpinner;
        std::function<bool()> isEditable;
    };

    class AbiValListener final {
    public:
        explicit AbiValListener(MotionView& view) : view_(&view) {}
        void widgetSelected(std::int32_t value);
        MotionView* view_{};
    };

    class EditBeast final {
    public:
        explicit EditBeast(MotionView& view) : view_(&view) {}
        void mouseDoubleClick(lib::PointerEvent& event);
        void mouseDown(lib::PointerEvent& event);
        void mouseUp(lib::PointerEvent& event);
        void mouseMove(lib::PointerEvent& event);
        [[nodiscard]] std::optional<lib::Rect> cardBounds(
            const lib::PointerEvent& event) const;
        [[nodiscard]] std::optional<lib::Rect> eventTreeMarkRect(
            const lib::PointerEvent& event) const;
        void keyReleased(lib::KeyEvent& event);
        void keyPressed(lib::KeyEvent& event);

    private:
        MotionView* view_{};
        bool inBounds_{};
    };

    class ParamPaneChange final {
    public:
        explicit ParamPaneChange(MotionView& view) : view_(&view) {}
        void widgetSelected();
        MotionView* view_{};
    };

    class SetBeast final {
    public:
        explicit SetBeast(MotionView& view) : view_(&view) {}
        void widgetSelected();
        MotionView* view_{};
    };

    class PaintBeast final {
    public:
        explicit PaintBeast(MotionView& view) : view_(&view) {}
        void paintControl(lib::DrawingContext& context, lib::Rect bounds);
        MotionView* view_{};
    };

    class MDropListener final {
    public:
        explicit MDropListener(MotionView& view) : view_(&view) {}
        void dragEnter(lib::DragEvent& event);
        void dragOver(lib::DragEvent& event);
        void drop(lib::DragEvent& event);
        MotionView* view_{};
    };

    class MDragListener final {
    public:
        explicit MDragListener(MotionView& view) : view_(&view) {}
        void dragStart(lib::DragEvent& event);
        void dragSetData(lib::DragEvent& event);
        void dragFinished(lib::DropOperation operation);
        MotionView* view_{};
        std::ptrdiff_t item_{-1};
    };

    class SelElement final {
    public:
        explicit SelElement(MotionView& view) : view_(&view) {}
        void widgetSelected(std::size_t elementIndex);
        MotionView* view_{};
    };

    class Dispose final {
    public:
        explicit Dispose(MotionView& view) : view_(&view) {}
        void widgetDisposed();
        MotionView* view_{};
    };

    class KeyDownFilter final {
    public:
        explicit KeyDownFilter(MotionView& view) : view_(&view) {}
        void handleEvent(lib::KeyEvent& event);
        MotionView* view_{};
    };

    class BeastDragListener final {
    public:
        explicit BeastDragListener(MotionView& view) : view_(&view) {}
        void dragStart(lib::DragEvent& event);
        void dragSetData(lib::DragEvent& event);
        MotionView* view_{};
    };

    class BeastDropListener final {
    public:
        explicit BeastDropListener(MotionView& view) : view_(&view) {}
        void dragEnter(lib::DragEvent& event);
        void dragOver(lib::DragEvent& event);
        void drop(lib::DragEvent& event);
        MotionView* view_{};
    };

    class MotionTCPD final {
    public:
        explicit MotionTCPD(MotionView& view) : view_(&view) {}
        void cut(SelectionEvent& event);
        void copy(SelectionEvent& event);
        void paste(SelectionEvent& event);
        void del(SelectionEvent& event);
        void clone(SelectionEvent& event);
        [[nodiscard]] bool canDoTCPD() const noexcept;
        [[nodiscard]] bool canDoT() const noexcept;
        [[nodiscard]] bool canDoC() const noexcept;
        [[nodiscard]] bool canDoP() const noexcept;
        [[nodiscard]] bool canDoD() const noexcept;
        [[nodiscard]] bool canDoClone() const noexcept;
        MotionView* view_{};
    };

    class BeastTCPD final {
    public:
        explicit BeastTCPD(MotionView& view) : view_(&view) {}
        void cut(SelectionEvent& event);
        void copy(SelectionEvent& event);
        void paste(SelectionEvent& event);
        void del(SelectionEvent& event);
        void clone(SelectionEvent& event);
        [[nodiscard]] bool canDoTCPD() const noexcept;
        [[nodiscard]] bool canDoT() const noexcept;
        [[nodiscard]] bool canDoC() const noexcept;
        [[nodiscard]] bool canDoP() const noexcept;
        [[nodiscard]] bool canDoD() const noexcept;
        [[nodiscard]] bool canDoClone() const noexcept;
        MotionView* view_{};
    };

    MotionView(Commons comm, Props prop, Summary* summary,
               CWXPath* ucOwner = nullptr, UseCounterPtr useCounter = {},
               const Parent& parent = {}, int style = 0,
               UndoManager* undo = nullptr);
    MotionView(Commons comm, Props prop, SummaryPtr summary,
               lib::Owner<CWXPath> ucOwner = {}, UseCounterPtr useCounter = {},
               const Parent& parent = {}, int style = 0,
               UndoManager* undo = nullptr);
    ~MotionView();

    MotionView(const MotionView&) = delete;
    MotionView& operator=(const MotionView&) = delete;

    [[nodiscard]] PanelPtr createControl(const Parent& parent = {});
    [[nodiscard]] PanelPtr control() const noexcept { return panel_; }
    [[nodiscard]] std::shared_ptr<lib::TableView> table() const noexcept {
        return motionsTable_;
    }
    [[nodiscard]] std::shared_ptr<lib::TableView> elementTable() const noexcept {
        return motionElementTable_;
    }
    [[nodiscard]] std::shared_ptr<lib::ComboBox> beastsControl() const noexcept {
        return beastsControl_;
    }
    [[nodiscard]] std::shared_ptr<lib::Canvas> beastCanvas() const noexcept {
        return beastImage_;
    }

    [[nodiscard]] Commons& commons() noexcept { return comm_; }
    [[nodiscard]] const Commons& commons() const noexcept { return comm_; }
    [[nodiscard]] Props& props() noexcept { return prop_; }
    [[nodiscard]] const Props& props() const noexcept { return prop_; }
    [[nodiscard]] Summary* summary() const noexcept { return summary_; }
    [[nodiscard]] UndoManager* undoManager() const noexcept { return undo_; }
    [[nodiscard]] UseCounterPtr useCounter() const noexcept { return useCounter_; }
    [[nodiscard]] bool readOnly() const noexcept { return readOnly_; }
    void setReadOnly(bool value) noexcept;
    [[nodiscard]] const String& id() const noexcept { return id_; }
    void id(String value) { id_ = std::move(value); }

    [[nodiscard]] MotionPtr motion(std::size_t index) const noexcept;
    void motion(std::size_t index, MotionPtr value, bool store = true);
    [[nodiscard]] MotionPtr selection() const noexcept;
    [[nodiscard]] std::ptrdiff_t selectionIndex() const noexcept { return selected_; }
    bool select(std::ptrdiff_t index);
    bool select(const MotionPtr& value);

    [[nodiscard]] std::vector<MotionPtr> motions() const;
    void motions(std::vector<MotionPtr> values);
    [[nodiscard]] std::vector<MotionRow> rows() const { return rows_; }
    [[nodiscard]] const MotionRow* row(std::size_t index) const noexcept;

    [[nodiscard]] MotionPtr createMT(types::MType type);
    [[nodiscard]] MotionPtr createMotion(types::MType type) { return createMT(type); }
    [[nodiscard]] std::ptrdiff_t indexOf(const MotionPtr& value) const noexcept;
    bool setElement(types::Element value);
    bool setDamageType(types::DamageType value);
    bool setAbilityValue(std::int32_t value);
    void roundEnter(std::int32_t value);
    [[nodiscard]] std::int32_t roundCancel(std::int32_t oldValue);
    void valEnter(std::int32_t value);
    [[nodiscard]] std::int32_t valCancel(std::int32_t oldValue);

    bool swap(std::ptrdiff_t first, std::ptrdiff_t second, bool store = true);
    bool up();
    bool down();
    bool removeMotion();
    bool removeMotion(std::ptrdiff_t index, bool callEvent = true,
                      bool callMod = true, bool store = true);
    bool appendMotion(MotionPtr value, std::ptrdiff_t index = -1,
                      bool callEvent = true, bool callMod = false,
                      bool store = true);

    [[nodiscard]] bool refreshSels(bool force = false);
    void refEnabled();
    [[nodiscard]] ParameterPane parameterPane() const noexcept { return pane_; }
    [[nodiscard]] types::DamageType selectedDamageType() const noexcept {
        return selectedDamageType_;
    }
    [[nodiscard]] types::Element selectedElement() const noexcept {
        return selectedElement_;
    }
    [[nodiscard]] std::int32_t abilityValue() const noexcept { return abilityValue_; }
    [[nodiscard]] std::int32_t value() const noexcept { return value_; }
    [[nodiscard]] std::int32_t roundValue() const noexcept { return roundValue_; }
    [[nodiscard]] std::int32_t maxNest() const noexcept { return maxNestValue_; }
    [[nodiscard]] bool hasVan() const noexcept;

    void beastIncSearch(StringView text);
    [[nodiscard]] bool canSetBeast() const noexcept;
    bool setBeast(const BeastPtr& value);
    bool setSelectedBeast(std::optional<std::size_t> index);
    bool removeRef();
    [[nodiscard]] bool canEditBeast() const noexcept;
    [[nodiscard]] BeastPtr editBeast() const;
    void editBeastM();
    void editBeastUseEvent();
    [[nodiscard]] bool openBeastEventWin(const BeastPtr& value);
    [[nodiscard]] bool openBeastCardView(StringView nestedPath = {},
                                          bool shellActivate = false);
    void updateBeastToolTip();
    void refBeast(const BeastPtr& value);
    void delBeast(CWXPath* owner, const BeastPtr& value);
    void refBeasts();
    void refSkin(const skin::Skin* value);
    void refImageScale(std::int32_t value);
    void refCardImageStatus();
    [[nodiscard]] const std::vector<BeastChoice>& beastChoices() const noexcept {
        return beastChoices_;
    }
    [[nodiscard]] BeastPtr selectedBeast() const noexcept { return selectedBeast_; }
    [[nodiscard]] const std::shared_ptr<lib::Menu>& motionContextMenu() const noexcept {
        return motionContextMenu_;
    }
    [[nodiscard]] const std::shared_ptr<lib::Menu>& beastContextMenu() const noexcept {
        return beastContextMenu_;
    }
    [[nodiscard]] String beastToolTip() const noexcept { return beastToolTip_; }

    [[nodiscard]] bool canSelectConnectedResource() const noexcept;
    void selectConnectedResource();
    [[nodiscard]] bool canWriteComment() const noexcept;
    bool writeComment(String value);
    void writeComment();
    void closeAuxiliaryEditors();
    [[nodiscard]] std::vector<String> warnings() const;
    [[nodiscard]] std::vector<String> warnings(std::size_t index) const;
    [[nodiscard]] std::vector<String> getWarnings(std::size_t index) const {
        return warnings(index);
    }
    [[nodiscard]] bool openCWXPath(StringView value, bool shellActivate = false);

    [[nodiscard]] String itemToXML(std::ptrdiff_t index = -1,
                                   bool paneId = true) const;
    [[nodiscard]] lib::ClipboardData clipboardData() const;
    [[nodiscard]] bool copy();
    [[nodiscard]] bool cut();
    [[nodiscard]] bool paste();
    [[nodiscard]] bool clone();
    [[nodiscard]] bool pasteData(const lib::ClipboardData& data);
    [[nodiscard]] bool canDoTCPD() const noexcept { return true; }
    [[nodiscard]] bool canDoT() const noexcept;
    [[nodiscard]] bool canDoC() const noexcept;
    [[nodiscard]] bool canDoP() const noexcept;
    [[nodiscard]] bool canDoD() const noexcept;
    [[nodiscard]] bool canDoClone() const noexcept;

    void beginDrag();
    [[nodiscard]] lib::ClipboardData dragData() const;
    bool drop(lib::DragEvent& event);
    [[nodiscard]] lib::DragSourceBinding dragSourceBinding();
    [[nodiscard]] lib::DropTargetBinding dropTargetBinding();

    [[nodiscard]] bool canUndo() const noexcept;
    [[nodiscard]] bool canRedo() const noexcept;
    void undo();
    void redo();

    /// Dの通知処理を、共通モデルの通常の公開呼び出しへ対応付け
    /// る。 ホスト通知とテストから使う具体的な入口として残す。
    void changed();
    void refMenu(types::MenuID id);
    void refUndoMax();
    void refEnabledFromDataVersion() { refEnabled(); }
    [[nodiscard]] String summSkin() const;

private:
    enum class MaterialCopyResult {
        ContinuedWithoutCopy,
        Copied,
        CopiedWithErrors,
    };

    struct SnapshotReferences {
        std::vector<MotionPtr> values;
        ~SnapshotReferences() {
            for (const auto& value : values) if (value) value->removeUseCounter();
        }
    };
    struct Snapshot {
        std::vector<MotionPtr> values;
        std::ptrdiff_t selected{-1};
        // スナップショットのコピーは、一つの登録所有者を共有する。 切り離した
        // D形式の子Userの登録を外せるのは、最後の履歴が終了したときだけ。
        std::shared_ptr<SnapshotReferences> references;
    };
    struct PendingUndo {
        Snapshot before;
        bool active{};
    };
    struct ClipboardPayload {
        String kind;
        xml::XNode node;
    };

    struct MaterialCopyPlan {
        using Counter = ::cwxeditor::cwx::usecounter::UseCounter;
        using PathId = ::cwxeditor::cwx::usecounter::PathId;

        struct MaterialPath {
            PathId id;
            String value;
            std::filesystem::path source;
            bool binary_image{};
        };

        BeastPtr value;
        std::shared_ptr<Counter> counter;
        std::vector<MaterialPath> candidates;
        std::vector<String> normal_source_names;
        String source_name;
        String source_author;
        std::filesystem::path target_root;
        std::size_t binary_count{};

        ~MaterialCopyPlan();
        void detach() noexcept;
    };

    [[nodiscard]] Snapshot snapshot() const;
    [[nodiscard]] Snapshot snapshot(const std::vector<MotionPtr>& values,
                                    std::ptrdiff_t selected) const;
    void restore(const Snapshot& value);
    bool mutate(const std::function<bool()>& action, bool useCount = true,
                bool warning = true);
    void beginPendingUndo();
    void commitPendingUndo();
    void cancelPendingUndo() noexcept;
    void attachMotion(const MotionPtr& value);
    void detachMotion(const MotionPtr& value) noexcept;
    void retireDetachedBeastEvents();
    void ensureWidgets(const Parent& parent);
    void refreshWidgets();
    void refreshRows();
    void refreshParameterPane();
    void refreshBeastImage();
    [[nodiscard]] std::optional<lib::Rect> beastDrawBounds() const;
    void rebuildMotionContextMenu();
    void rebuildBeastContextMenu();
    void showMotionContextMenu(const lib::WidgetPtr& owner, lib::Point position);
    void showBeastContextMenu(const lib::WidgetPtr& owner, lib::Point position);
    void notify(bool warning = true, bool useCount = true);
    [[nodiscard]] String motionText(const Motion& value) const;
    [[nodiscard]] String parameterText(const Motion& value) const;
    [[nodiscard]] std::optional<ClipboardPayload> parseClipboard(
        StringView text) const;
    [[nodiscard]] String makeClipboard(StringView kind, const xml::XNode& node,
                                       bool paneId) const;
    [[nodiscard]] bool pasteMotion(const ClipboardPayload& payload);
    [[nodiscard]] bool pasteBeast(const ClipboardPayload& payload);
    [[nodiscard]] bool pasteBeast(xml::XNode& node);
    [[nodiscard]] bool qCardMaterialCopy(const BeastPtr& value,
                                          StringView fromScenarioPath,
                                          StringView sourceName = {},
                                          StringView sourceAuthor = {}) const;
    [[nodiscard]] std::shared_ptr<MaterialCopyPlan> prepareMaterialCopyPlan(
        const BeastPtr& value, StringView fromScenarioPath,
        StringView sourceName, StringView sourceAuthor) const;
    [[nodiscard]] MaterialCopyResult applyMaterialCopy(
        const std::shared_ptr<MaterialCopyPlan>& plan,
        bool externalizeBinary, bool copyReferences, bool showWarning) const;
    void showMaterialCopyWarningAsync() const;
    [[nodiscard]] bool beginExternalMaterialDrop(
        const ClipboardPayload& payload, std::optional<std::size_t> targetIndex);
    [[nodiscard]] bool setBeastNode(xml::XNode& node);
    [[nodiscard]] BeastPtr linkedBeast(const BeastPtr& value) const;
    [[nodiscard]] bool isMotionTargetVersion(StringView version) const;
    [[nodiscard]] std::shared_ptr<card::XMLOption> xmlOption() const;
    [[nodiscard]] system::XMLInfo xmlInfo() const;
    [[nodiscard]] bool clipboardAvailable() const noexcept;
    void writeClipboard(const lib::ClipboardData& value);
    [[nodiscard]] lib::ClipboardData readClipboard() const;
    void bindDragDrop();
    void unbindDragDrop();
    void subscribeDataVersion();
    void setSelectedIndex(std::ptrdiff_t index, bool notifySelection);
    void resetMaxNest(const MotionPtr& value);
    void storeEdit(std::ptrdiff_t index);
    void storeSwap(std::ptrdiff_t first, std::ptrdiff_t second);
    void storeMove(std::ptrdiff_t from, std::ptrdiff_t to);
    void storeInsert(std::ptrdiff_t index);
    void storeDelete(std::ptrdiff_t index);

    String id_;
    bool readOnly_{};
    bool disposed_{};
    Commons comm_{};
    Props prop_{};
    Summary* summary_{};
    SummaryPtr summaryHolder_{};
    lib::Owner<CWXPath> ucOwner_{};
    UseCounterPtr useCounter_{};
    UndoManager* undo_{};
    std::unique_ptr<UndoManager> ownedUndo_{};
    // UndoManagerは親の編集画面が所有し、このビューよ
    // り長く残る場合がある。 そこへ保存する全コールバックは、生
    // のMotionViewへ触れる前にこのトークンを確認する。
    std::shared_ptr<void> lifetimeToken_{std::make_shared<int>(0)};
    pane::Signal<void()>::Token refDataVersionToken_{};
    system::System xmlSystem_{};

    // Dの_beWinは内包イベント画面の寿命を管理する。 一覧が置き換
    // わった後も、その画面が終わるまで正確なモデルの参照連鎖を保持する。
    std::vector<std::pair<MotionPtr, BeastPtr>> openedBeastEvents_{};
    void trackBeastEvent(const BeastPtr& beast);
    std::vector<MotionPtr> motions_{};
    std::vector<MotionRow> rows_{};
    std::ptrdiff_t selected_{-1};
    std::ptrdiff_t oldIndex_{-1};
    ParameterPane pane_{ParameterPane::None};
    types::DamageType selectedDamageType_{types::DamageType::LevelRatio};
    types::Element selectedElement_{types::Element::All};
    std::int32_t abilityValue_{};
    std::int32_t value_{1};
    std::int32_t roundValue_{1};
    std::int32_t maxNestValue_{1};

    String beastSearch_;
    BeastPtr selectedBeast_{};
    std::vector<BeastChoice> beastChoices_{};
    String beastToolTip_;
    std::ptrdiff_t dragIndex_{-1};

    PanelPtr panel_{};
    std::shared_ptr<lib::TabView> categoryTabs_{};
    std::shared_ptr<lib::ToolBar> toolbar_{};
    std::shared_ptr<lib::ComboBox> newMotionTypeControl_{};
    std::vector<types::MType> newMotionTypes_{};
    std::shared_ptr<lib::TableView> motionsTable_{};
    std::shared_ptr<lib::TableView> motionElementTable_{};
    std::shared_ptr<lib::ComboBox> beastsControl_{};
    std::shared_ptr<lib::TextEdit> beastSearchControl_{};
    std::shared_ptr<lib::Button> setBeastControl_{};
    std::shared_ptr<lib::Canvas> beastImage_{};
    lib::ImageResourceSlot beastPreviewResource_{};
    lib::Size beastPreviewSize_{};
    std::shared_ptr<lib::Menu> motionContextMenu_{};
    std::shared_ptr<lib::Menu> beastContextMenu_{};
    std::shared_ptr<lib::NumberEdit> maxNestControl_{};
    std::shared_ptr<lib::NumberEdit> roundControl_{};
    std::shared_ptr<lib::NumberEdit> valueControl_{};
    std::shared_ptr<lib::NumberEdit> skillPowerControl_{};
    std::shared_ptr<lib::NumberEdit> abilityRoundControl_{};
    std::shared_ptr<lib::Slider> abilityValueControl_{};
    std::shared_ptr<lib::StatusBar> status_{};

    std::unordered_map<types::DamageType, std::shared_ptr<lib::RadioButton>>
        damageTypeControls_{};
    std::unordered_map<types::DamageType, std::shared_ptr<lib::RadioButton>>
        skillPowerTypeControls_{};
    std::shared_ptr<lib::Container> editPanel_{};
    struct CommentSession {
        std::shared_ptr<comment::Commons> commons;
        std::shared_ptr<comment::CommentDialog> dialog;
        std::weak_ptr<Motion> target;
    };
    std::unordered_map<String, CommentSession> commentDialogs_{};
    // 並べ替え履歴の反映は、共有の履歴位置の操作が戻った後に行う。
    std::shared_ptr<comment::CommentDialog> pendingReorderCommentCancel_{};
    std::unordered_map<ParameterPane, std::shared_ptr<lib::Panel>> parameterPanels_{};

    PendingUndo pendingUndo_{};
    lib::ClipboardData clipboardMemory_{};
    bool hasClipboardMemory_{};
    std::ptrdiff_t dragSource_{-1};
};

// すべてのMotionViewホストで共用し、実際に開いたパネルを記録するアダプター。
void bindUseEventHost(Props& props, commons::Commons* common, dprops::Props* editorProps);

}  // namespace cwxeditor::cwx::editor::gui::dwt::motionview
