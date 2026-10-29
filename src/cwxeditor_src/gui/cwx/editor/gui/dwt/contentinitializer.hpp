#pragma once

// cwx/editor/gui/dwt/contentinitializer.d
// の移植可能なC++20実装。 イベントコンテントのモデルは、デ
// スクトップのツールキットやネイティブウィンドウ型から分離する。

#include "cwxeditor_src/core/cwx/event.hpp"
#include "cwxeditor_src/core/cwx/props.hpp"
#include "cwxeditor_src/core/cwx/script.hpp"
#include "cwxeditor_src/core/cwx/skin.hpp"
#include "cwxeditor_src/core/cwx/summary.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/core/cwx/variables.hpp"
#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/portable.hpp"
#include "cwxeditor_src/lib/services.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/undo.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/xmlbytestransfer.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::eventdialog {
class EventDialog;
}

namespace cwxeditor::cwx::editor::gui::dwt::dprops {
class Props;
}
namespace cwxeditor::cwx::editor::gui::dwt::comment {
class CommentDialog;
}

namespace cwxeditor::cwx::editor::gui::dwt::commons {
class Commons;
}

namespace cwxeditor::cwx::area {
class AbstractArea;
}

namespace cwxeditor::cwx::card {
class BeastCard;
}

namespace cwxeditor::cwx::editor::gui::dwt::contentinitializer {

namespace lib = ::cwxeditor::lib;
namespace event = ::cwxeditor::cwx::event;
namespace card = ::cwxeditor::cwx::card;
namespace background = ::cwxeditor::cwx::background;
namespace path = ::cwxeditor::cwx::path;
namespace props = ::cwxeditor::cwx::props;
namespace script = ::cwxeditor::cwx::script;
namespace skin = ::cwxeditor::cwx::skin;
namespace summary = ::cwxeditor::cwx::summary;
namespace structs = ::cwxeditor::cwx::structs;
namespace types = ::cwxeditor::cwx::types;
namespace variables = ::cwxeditor::cwx::variables;

using String = lib::String;
using StringView = lib::StringView;
using CType = types::CType;
using Content = event::Content;
using ContentPtr = event::ContentPtr;
using Summary = summary::Summary;
using SummaryPtr = summary::Summary::Owner;
using Skin = skin::Skin;
using SkinPtr = skin::Skin::Ptr;
using UseCounter = event::UseCounter;
using UseCounterPtr = lib::Owner<UseCounter>;
using BgImageS = ::cwxeditor::cwx::structs::BgImageS;
using BgImageDefaults = lib::Array<BgImageS>;
using Parent = std::shared_ptr<lib::Container>;
using Table = lib::TableView;
using ResourceId = lib::ResourceId;

/// 従来Commonsから渡していた、GUI以外の依存先。
struct ContentInitializerContext {
    using ContentName = lib::Callback<String(CType)>;
    using ContentImage = lib::Callback<ResourceId(CType)>;
    using EventViewImage = lib::Callback<ResourceId(StringView)>;
    using ContentText = lib::Callback<String(const Content&, const Summary*)>;
    using SkinResolver = lib::Callback<SkinPtr(const Summary*, StringView, StringView)>;
    using VariableResolver = lib::Callback<lib::Array<script::VarSet>(
        const lib::Array<String>&, StringView)>;
    using ScriptVariableDialog = lib::Callback<std::optional<script::Contents>(
        const SummaryPtr&, const lib::Array<String>&, StringView, StringView,
        const script::CompileOption&)>;
    using ErrorReporter = lib::Callback<void(String)>;
    using ScriptErrorReporter = lib::Callback<void(const script::CWXScriptException&,
        StringView, const script::CompileOption&)>;
    using EditBeastCard = lib::Callback<bool(
        Summary*, const lib::Owner<path::CWXPath>&,
        const lib::Owner<card::BeastCard>&, bool, StringView, bool,
        lib::Action, lib::Action, lib::Action)>;
    using SceneCommentDialog = lib::Callback<std::shared_ptr<
        ::cwxeditor::cwx::editor::gui::dwt::comment::CommentDialog>(StringView)>;
    using SubscribeSceneCommentUpdates = lib::Callback<lib::Action(lib::Action)>;

    props::CProps* prop{};
    variables::FlexEtcProps* vars{};
    dprops::Props* editor_props{};
    commons::Commons* editor_commons{};
    // DのCommonsと同じく、移動は低水準のペイン登録表でなくMainWindowへ渡す。
    lib::Callback<bool(StringView, bool)> openCWXPath{};
    std::shared_ptr<lib::Window> dialog_owner{};
    lib::Services services{};
    xmlbytestransfer::ClipData clipboard{};

    lib::Action refreshToolBar{};
    lib::Callback<bool()> stopOpen{};
    ContentName contentName{};
    ContentImage contentImage{};
    EventViewImage eventViewImage{};
    lib::Action returnToScene{};
    lib::Callback<bool(const ContentPtr&)> findStartUsers{};
    lib::Callback<bool(const std::shared_ptr<::cwxeditor::cwx::path::CWXPath>&)> findLocalVariableUsers{};
    ContentText contentText{};
    SkinResolver skinResolver{};
    VariableResolver variableResolver{};
    ScriptVariableDialog scriptVariableDialog{};
    ErrorReporter errorReporter{};
    ScriptErrorReporter scriptErrorReporter{};
    // 内包する召喚獣の効果に対して、実際の効果系カード編集画面を
    // 開く。 MainWindowから処理を渡し、イベント・カー
    // ド画面で同じモードレス登録表を使う。 親のMotionView
    // 経由で浅いCWXPathを開き直す代替は使わない。
    EditBeastCard editBeastCard{};
    // イベントが所有するBgImagesViewも、Area/Battleと同じMainWindow
    // のコメント画面生成と、 場面コメントのタイトル更新の購読を使う。
    SceneCommentDialog make_scene_comment_dialog{};
    SubscribeSceneCommentUpdates subscribe_scene_comment_updates{};
    // 背景コンテントのダイアログを所有するArea/Battle
    // 。 Dと同じくBgImagesView.refTarget
    // へ渡し、任意のカード参照を最初のフレームから表示する。
    ::cwxeditor::cwx::area::AbstractArea* referenceArea{};

    String editedInitializer{"edited"};
    String defaultSkin{"MedievalFantasy"};
    String defaultSkinName{"Classic"};

    ContentInitializerContext() = default;
    ContentInitializerContext(props::CProps& parent,
                              variables::FlexEtcProps& properties,
                              lib::Services value = {});

    [[nodiscard]] bool isTargetVersion(bool legacy, StringView dataVersion,
                                       StringView wsnVersion) const;
    [[nodiscard]] SkinPtr findSkin(const Summary* summ,
                                   StringView type = {},
                                   StringView name = {}) const;
};

enum class DialogKind : std::int32_t {
    Generic, AreaSelect, StartSelect, Flag, Step, Card, Message, Effect,
    Audio, Background, Environment, Expression, Xml,
};

/// バックエンドに依存しないイベントダイアログの状態。 実際の画面へ結び付け
/// ても、DのappliedEvent/closeEventの動作を保つ。
using EventDialogPtr = lib::Owner<
    ::cwxeditor::cwx::editor::gui::dwt::eventdialog::EventDialog>;

[[nodiscard]] bool hasDialog(CType type, bool existsSummary = true);
[[nodiscard]] EventDialogPtr createEventDialog(
    const ContentInitializerContext& context, SummaryPtr summ, SkinPtr forceSkin,
    UseCounterPtr uc, Parent parent, ContentPtr evt, ContentPtr parentContent = {},
    bool create = false);

[[nodiscard]] ContentPtr initial(const ContentInitializerContext& context,
                                 const Skin* skin, bool legacy,
                                 StringView dataVersion,
                                 std::span<const BgImageS> bgImagesDefault,
                                 ContentPtr content);

class ContentInitialValueEditor final {
public:
    using Initializer = lib::OrderedDictionary<CType, ContentPtr>;
    using ModCallback = lib::Action;

    ContentInitialValueEditor(ContentInitializerContext context = {}, Parent parent = {});
    ~ContentInitialValueEditor();

    ContentInitialValueEditor(const ContentInitialValueEditor&) = delete;
    ContentInitialValueEditor& operator=(const ContentInitialValueEditor&) = delete;

    std::vector<ModCallback> modEvent;

    [[nodiscard]] const std::shared_ptr<Table>& widget() const noexcept { return list_; }
    [[nodiscard]] std::shared_ptr<Table>& widget() noexcept { return list_; }
    [[nodiscard]] const std::shared_ptr<lib::Menu>& contextMenu() const noexcept {
        return context_menu_;
    }
    [[nodiscard]] const ContentInitializerContext& context() const noexcept { return context_; }
    [[nodiscard]] ContentInitializerContext& context() noexcept { return context_; }

    [[nodiscard]] const BgImageDefaults& bgImagesDefault() const noexcept { return bg_images_; }
    void bgImagesDefault(BgImageDefaults value);

    void setInitializer(const Initializer& initializer, bool legacy,
                        String dataVersion, undo::UndoManager* undo);
    void setInitializer(const Initializer& initializer, bool legacy,
                        String dataVersion, undo::UndoManager& undo) {
        setInitializer(initializer, legacy, std::move(dataVersion), &undo);
    }
    [[nodiscard]] Initializer getInitializer() const;

    void setSelection(lib::Array<std::size_t> indices);
    [[nodiscard]] const lib::Array<std::size_t>& selection() const noexcept { return list_->selection; }

    void editContent();
    void undo();
    void redo();
    void resetValues();
    void resetValuesAll();
    void selectAll();
    [[nodiscard]] bool canEditContent() const noexcept;
    [[nodiscard]] bool isInitialValues() const;
    [[nodiscard]] bool isInitialValuesAll() const;

    void cut();
    void copy();
    void paste();
    void del();
    void clone();

    [[nodiscard]] bool canDoTCPD() const noexcept { return true; }
    [[nodiscard]] bool canDoT() const noexcept { return false; }
    [[nodiscard]] bool canDoC() const noexcept { return canEditContent(); }
    [[nodiscard]] bool canDoP() const;
    [[nodiscard]] bool canDoClone() const noexcept { return false; }
    [[nodiscard]] bool canDoD() const noexcept { return false; }

    /// コンパイル済みのスクリプト項目を、初期値編集と同じ対応付けで変換する。 イ
    /// ベントテンプレートの挿入もこの経路を使う。propがあればContent::add(prop,
    /// child)で子を追加し、 DのCWXScript.analyzeSemanticsImpl
    /// のparent.add(_prop, c)と同じくvalidText
    /// で名前を正規化する。 CPropsを持たない呼び出し元
    /// のnullptrでは、従来の正規化なしの動作を保つ。
    [[nodiscard]] static ContentPtr fromScript(const script::Content& source,
                                               const props::CProps* prop = nullptr);
    /// EventTreeViewの「CWXScriptとしてコピー
    /// 」用の、fromScriptの逆変換。 保存対象のイベント
    /// モデルだけを読み、変更やUndoの作成は行わない。 shallow
    /// はDのToScript1Contentに対応する。
    [[nodiscard]] static script::Content toScript(const Content& source,
                                                   bool shallow = false);

private:
    class CUndo final : public undo::Undo {
    public:
        CUndo(ContentInitialValueEditor& owner, const lib::Array<ContentPtr>& contents);
        void undo() override;
        void redo() override;
        void dispose() override {}

    private:
        ContentInitialValueEditor* owner_{};
        std::weak_ptr<void> lifetime_;
        undo::UndoManager* undo_manager_{};
        bool legacy_{};
        String data_version_;
        lib::Array<ContentPtr> contents_{};
        void apply();
    };

    ContentInitializerContext context_{};
    Parent parent_{};
    std::shared_ptr<Table> list_{};
    std::shared_ptr<lib::Menu> context_menu_{};
    std::map<CType, std::size_t> type_rows_{};
    std::map<CType, bool> edited_{};
    std::map<CType, ContentPtr> initial_values_{};
    std::map<CType, ContentPtr> values_{};
    BgImageDefaults bg_images_{};
    bool legacy_{};
    String data_version_{};
    SummaryPtr summary_{};
    undo::UndoManager* undo_{};
    std::map<CType, EventDialogPtr> edit_dialogs_{};
    std::shared_ptr<void> lifetime_{std::make_shared<int>(0)};
    std::uint64_t session_generation_{};
    std::optional<std::size_t> hovered_row_{};
    std::uint64_t tooltip_revision_{};
    std::uint64_t menu_connection_{},undo_max_connection_{};
    std::weak_ptr<const int> publisher_lifetime_;

    [[nodiscard]] ContentPtr initial(const Skin* skin, CType type) const;
    bool store(const lib::Array<ContentPtr>& contents);
    bool updateRow(CType type);
    bool notifyChanged();
    bool refreshToolBar();
    void updateToolTip();
    void rebuildContextMenu();
    void resetValuesImpl(const lib::Array<std::size_t>& rows);
    [[nodiscard]] bool isInitialValuesImpl(const lib::Array<std::size_t>& rows) const;
    void editContentImpl(std::size_t row);
    void pasteContents(lib::Array<ContentPtr> contents);
    [[nodiscard]] static bool sameContent(const ContentPtr& left,
                                           const ContentPtr& right) noexcept;
};

}  // namespace cwxeditor::cwx::editor::gui::dwt::contentinitializer
