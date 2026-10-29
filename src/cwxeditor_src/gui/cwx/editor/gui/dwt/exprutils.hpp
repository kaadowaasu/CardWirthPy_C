#pragma once

// cwx/editor/gui/dwt/exprutils.d
// の移植可能なC++20実装。
// ダイアログ・式編集・関数呼び出
// し補助の状態とイベント動作を保
// ち、共通部品だけを公開する。

#include "cwxeditor_src/core/cwx/event.hpp"
#include "cwxeditor_src/core/cwx/expression.hpp"
#include "cwxeditor_src/core/cwx/flag.hpp"
#include "cwxeditor_src/core/cwx/summary.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/core/cwx/usecounter.hpp"
#include "cwxeditor_src/core/cwx/versioninfo.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/areaviewutils.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/incsearch.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/chooser.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dmenu.hpp"
#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/layout.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::exprutils {

namespace areaview = ::cwxeditor::cwx::editor::gui::dwt::areaviewutils;
namespace event = ::cwxeditor::cwx::event;
namespace expression = ::cwxeditor::cwx::expression;
namespace flag = ::cwxeditor::cwx::flag;
namespace lib = ::cwxeditor::lib;
namespace skin = ::cwxeditor::cwx::skin;
namespace summary = ::cwxeditor::cwx::summary;
namespace types = ::cwxeditor::cwx::types;
namespace usecounter = ::cwxeditor::cwx::usecounter;

using Commons = areaview::Commons;
using Props = areaview::Props;
using Skin = areaview::Skin;
using Summary = areaview::Summary;
using Content = event::Content;
using ContentPtr = event::ContentPtr;
using CounterOwner = usecounter::CounterOwner;
using VariantVal = expression::VariantVal;
using VariantType = types::VariantType;
using VariableType = types::VariableType;
using CType = types::CType;
using ArgType = expression::ArgType;
using ArgDef = expression::ArgDef;
using FuncDef = expression::FuncDef;
using String = lib::String;
using StringView = lib::StringView;
using FunctionArguments = std::vector<std::pair<ArgType, String>>;

class ExpressionEditor;
class FunctionCallEditor;

/// 真偽値を返すコモン分岐・条件式のダイアログ状態。
class ExpressionEventDialog final {
public:
    ExpressionEventDialog(Commons comm, Props& prop,
                           lib::Owner<lib::Window> shell,
                           const Summary* summ, const Skin* forceSkin,
                           CounterOwner uc, ContentPtr parent, CType cType,
                           ContentPtr evt = {});

    [[nodiscard]] bool openCWXPath(StringView path, bool shellActivate);
    void setup(lib::Container& area);
    [[nodiscard]] bool apply();
    void refreshWarning();

    [[nodiscard]] const lib::Owner<ExpressionEditor>& expressionEditor() const noexcept {
        return expr_;
    }
    [[nodiscard]] const ContentPtr& event() const noexcept { return event_; }
    [[nodiscard]] const std::vector<String>& warnings() const noexcept { return warnings_; }
    [[nodiscard]] const lib::Owner<lib::Window>& shell() const noexcept { return shell_; }

private:
    Commons comm_;
    Props* prop_{};
    lib::Owner<lib::Window> shell_;
    const Summary* summary_{};
    const Skin* force_skin_{};
    CounterOwner counter_{};
    ContentPtr parent_{};
    CType type_{};
    ContentPtr event_{};
    lib::Owner<ExpressionEditor> expr_{};
    lib::Owner<lib::GroupBox> expression_group_{};
    std::vector<String> warnings_{};
};

/// WSN.4のSetVariantのダイアログ状態。 DWTと同じ
/// く、三つの変数種別のラジオボタンの下に共有の変数選択部品を置く。
class SetVariantDialog final {
public:
    std::vector<lib::Action> warningChanged;

    SetVariantDialog(Commons comm, Props& prop,
                     lib::Owner<lib::Window> shell,
                     const Summary* summ, const Skin* forceSkin,
                     CounterOwner uc, ContentPtr parent, ContentPtr evt = {});

    [[nodiscard]] bool openCWXPath(StringView path, bool shellActivate);
    void setup(lib::Container& area);
    [[nodiscard]] bool apply();
    void refreshWarning();
    void refFlagsAndSteps(const flag::FlagList& flags,
                          const flag::StepList& steps,
                          const flag::VariantList& variants);

    void configureChooser(chooser::Commons comm, chooser::ChooserProperties properties);

    [[nodiscard]] VariableType selectedType() const noexcept { return selected_type_; }
    void selectedType(VariableType value);
    [[nodiscard]] const String& selectedName() const noexcept { return selected_name_; }
    void selectedName(String value);
    [[nodiscard]] const String& calculationResult() const noexcept {
        return calculation_result_;
    }
    void showCalculationResult();
    [[nodiscard]] const lib::Owner<lib::Button>& calculationButton() const noexcept {
        return calculation_button_;
    }
    [[nodiscard]] const lib::Owner<ExpressionEditor>& expressionEditor() const noexcept {
        return expr_;
    }
    [[nodiscard]] const ContentPtr& event() const noexcept { return event_; }
    [[nodiscard]] const std::vector<String>& warnings() const noexcept { return warnings_; }
    [[nodiscard]] const lib::Owner<lib::Window>& shell() const noexcept { return shell_; }

private:
    void updateEnabled();
    void selectedVariableType();
    void setAvailableNames(VariableType type, const std::vector<String>& names);

    Commons comm_;
    Props* prop_{};
    lib::Owner<lib::Window> shell_;
    const Summary* summary_{};
    const Skin* force_skin_{};
    CounterOwner counter_{};
    ContentPtr parent_{};
    ContentPtr event_{};
    lib::Owner<ExpressionEditor> expr_{};
    std::map<VariableType, lib::Owner<lib::RadioButton>> variable_types_{};
    lib::Owner<lib::GroupBox> variable_group_{};
    std::unique_ptr<chooser::FlagChooserBase> variable_chooser_{};
    chooser::Commons chooser_comm_;
    chooser::ChooserProperties chooser_properties_;
    std::optional<VariableType> chooser_type_;
    bool synchronizing_chooser_{};
    lib::Owner<lib::Button> calculation_button_{};
    VariableType selected_type_{VariableType::Variant};
    String selected_name_{};
    std::map<VariableType, String> selected_names_;
    std::map<VariableType, chooser::ExpandedMap> expanded_directories_;
    std::map<VariableType, std::vector<String>> available_names_{};
    // 現在のパスだけでなく、選んだモデルの同一性も保持する
    // 。 Dの選択部品は改名後も対象を追うため、パスだけ
    // で改名を削除と誤認し、代入先を消さないようにする。
    std::map<VariableType, std::map<String, const void*>> available_identities_{};
    std::map<VariableType, const void*> selected_identities_{};
    String calculation_result_{};
    std::vector<String> warnings_{};
};

/// 式の文字列編集とコアの評価処理との接続。
class ExpressionEditor final : public lib::Panel {
public:
    std::vector<lib::Action> modEvent;
    std::vector<lib::Action> modReturnValueEvent;

    ExpressionEditor(Commons comm, const Summary* summ, const Skin* forceSkin,
                     CounterOwner uc, lib::Container* parent = nullptr,
                     int style = 0);
    ~ExpressionEditor() override;

    [[nodiscard]] lib::TextEdit& editor() noexcept { return *expr_; }
    [[nodiscard]] const lib::TextEdit& editor() const noexcept { return *expr_; }
    void setFocus();
    [[nodiscard]] String expression() const;
    void expression(String value);
    [[nodiscard]] VariantVal returnValue() const noexcept { return return_value_; }
    [[nodiscard]] VariantVal calculateValue() const;
    [[nodiscard]] const std::vector<String>& warnings() const noexcept { return warnings_; }
    void checkExpression(bool raiseModEvent = true);
    void refFlagAndStep(const flag::FlagList&, const flag::StepList&,
                        const flag::VariantList&);
    void refDataVersion();
    void showFuncWin(String name, const FunctionArguments& args);
    void showFuncWin(bool open);
    [[nodiscard]] const lib::Owner<lib::ToggleButton>& functionButton() const noexcept {
        return function_button_;
    }
    [[nodiscard]] const lib::Owner<FunctionCallEditor>& functionEditor() const noexcept {
        return function_editor_;
    }

private:
    [[nodiscard]] expression::VariableInfo variableInfo() const;
    void checkThread();
    void modified();
    void insertExpressionText(String value);
    void showFuncWinImpl(bool open, bool show);
    void checkExpressionImpl(bool raiseModEvent);
    void refreshVariableChoices();

    Commons comm_;
    const Summary* summary_{};
    const Skin* force_skin_{};
    CounterOwner counter_{};
    lib::Owner<lib::TextEdit> expr_{};
    std::vector<std::pair<VariableType, lib::Owner<lib::ComboBox>>> variable_choices_;
    std::vector<lib::Owner<lib::Button>> variable_popup_buttons_;
    std::vector<std::shared_ptr<incsearch::IncSearch>> variable_inc_searches_;
    dmenu::TextMenuModifyPtr expression_text_menu_;
    std::shared_ptr<int> checking_lifetime_{std::make_shared<int>(0)};
    lib::TimerId checking_timer_{};
    VariantVal return_value_{VariantVal::invalidValue()};
    std::vector<String> warnings_{};
    String raised_expression_{};
    lib::Owner<lib::ToggleButton> function_button_{};
    lib::Owner<FunctionCallEditor> function_editor_{};
};

/// 共通の関数選択・編集部品。Dの分類・宣言・
/// 引数表の状態を保ち、 insert_を通じ
/// て呼び出し元の式へ関数呼び出しを挿入する。
class FunctionCallEditor final {
public:
    class ArgsUndo {
    public:
        ArgsUndo() = default;
        explicit ArgsUndo(FunctionCallEditor* owner);
        void store();
        void impl();
        void undo();
        void redo();
        void dispose() noexcept;

    private:
        FunctionCallEditor* owner_{};
        std::size_t selected_{};
        std::vector<ArgType> selected_arg_types_{};
        std::vector<String> values_{};
    };

    FunctionCallEditor(Commons comm, const Summary* summ, CounterOwner uc,
                       lib::Widget* parent, std::function<void(String)> insert);
    ~FunctionCallEditor();

    [[nodiscard]] String functionReferenceDirectory() const;
    [[nodiscard]] std::vector<String> functionReferenceFiles() const;
    [[nodiscard]] const lib::Owner<lib::Dialog>& shell() const noexcept { return shell_; }
    /// このEditorが所有する、再利用可能なD互換の関数
    /// 参照ウィンドウ。 参照操作が呼ばれるまでは空とする。
    [[nodiscard]] const lib::Owner<lib::Dialog>& referenceDialog() const noexcept {
        return reference_dialog_;
    }
    [[nodiscard]] const lib::Owner<lib::Dialog>& referenceInfoDialog() const noexcept {
        return reference_info_dialog_;
    }
    [[nodiscard]] const lib::Owner<lib::Dialog>& referenceErrorDialog() const noexcept {
        return reference_error_dialog_;
    }
    void selectFunction(String name, const FunctionArguments& args = {});
    void refFlagAndStep();
    void focusToArgs();
    void undo();
    void redo();
    [[nodiscard]] bool canUndo() const noexcept { return history_pos_ > 0; }
    [[nodiscard]] bool canRedo() const noexcept {
        return history_pos_ + 1U < history_.size();
    }
    [[nodiscard]] const String& selectedFunction() const noexcept { return selected_func_name_; }
    [[nodiscard]] const lib::Owner<lib::TableView>& arguments() const noexcept { return args_; }
    [[nodiscard]] const lib::Owner<lib::ComboBox>& functionControl() const noexcept {
        return function_;
    }
    [[nodiscard]] const lib::Owner<lib::TextEdit>& functionSearchControl() const noexcept {
        return function_search_;
    }
    [[nodiscard]] const std::shared_ptr<incsearch::IncSearch>&
    incrementalSearchControl() const noexcept { return function_inc_search_; }
    [[nodiscard]] const lib::Owner<lib::SplitView>& functionArgsSplit() const noexcept {
        return function_args_split_;
    }
    void saveWindowState() noexcept;

private:
    void store();
    void refUndoMax();
    void categorySelected();
    void functionSearchModified();
    void updateReferenceButton();
    void openFunctionReference();
    void selectReferenceFile(std::size_t index);
    void closeReference() noexcept;
    void functionSelected();
    [[nodiscard]] String argTypeName(ArgType argType) const;
    void updateFunctions();
    [[nodiscard]] std::vector<ArgType> argTypes(std::size_t index) const;
    bool typeEditEndImpl(std::size_t row, std::size_t typeIndex, bool force,
                         bool recordHistory = true);
    void configureValueCell(lib::ListItem& cell, ArgType type,
                            std::optional<std::size_t> argument = std::nullopt) const;
    [[nodiscard]] std::pair<ArgType, String> validArg(ArgType argType,
                                                       StringView text) const;
    void applySnapshot(std::size_t selected, const std::vector<ArgType>& types,
                       const std::vector<String>& values);

    Commons comm_;
    const Summary* summary_{};
    CounterOwner counter_{};
    std::function<void(String)> insert_{};
    std::shared_ptr<int> insertion_lifetime_{std::make_shared<int>(0)};
    std::weak_ptr<incsearch::IncSearch> argument_inc_search_;
    lib::Owner<lib::Dialog> shell_{};
    lib::Owner<lib::SplitView> function_args_split_{};
    lib::Owner<lib::ComboBox> category_{};
    lib::Owner<lib::TextEdit> function_search_{};
    lib::Owner<lib::Button> reference_{};
    lib::Owner<lib::Dialog> reference_dialog_{};
    lib::Owner<lib::Dialog> reference_info_dialog_{};
    lib::Owner<lib::Dialog> reference_error_dialog_{};
    lib::Owner<lib::ListView> reference_list_{};
    lib::Owner<lib::TextEdit> reference_viewer_{};
    dmenu::TextMenuModifyPtr reference_text_menu_{};
    lib::Owner<lib::Button> reference_copy_all_{};
    lib::Owner<lib::Button> reference_close_{};
    lib::Owner<lib::Button> reference_info_close_{};
    lib::Owner<lib::Button> reference_error_close_{};
    std::vector<String> reference_files_{};
    std::vector<types::FunctionCategory> categories_{};
    lib::Owner<lib::ComboBox> function_{};
    std::shared_ptr<incsearch::IncSearch> function_inc_search_{};
    lib::Owner<lib::Menu> function_menu_{};
    lib::Owner<lib::TextEdit> declaration_{};
    lib::Owner<lib::TextEdit> description_{};
    lib::Owner<lib::TextEdit> example_{};
    lib::Owner<lib::TableView> args_{};
    lib::Owner<lib::Menu> args_menu_{};
    lib::Owner<lib::Button> insert_button_{};
    std::vector<FuncDef> all_func_defs_{};
    std::vector<FuncDef> func_defs_{};
    String selected_func_name_{};
    std::vector<ArgDef> arg_defs_{};
    std::vector<std::vector<ArgType>> arg_types_{};
    std::vector<ArgType> selected_arg_types_{};
    lib::Action undo_max_unsubscribe_;
    std::vector<ArgsUndo> history_{};
    std::size_t history_pos_{};
    std::size_t selected_row_{};
};

}  // namespace cwxeditor::cwx::editor::gui::dwt::exprutils
