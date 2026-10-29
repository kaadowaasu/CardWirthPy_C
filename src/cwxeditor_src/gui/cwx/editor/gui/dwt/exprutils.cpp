#include "cwxeditor_src/gui/cwx/editor/gui/dwt/exprutils.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/scene_flag_compare.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dmenu.hpp"
#include "cwxeditor_src/core/cwx/structs.hpp"
#include "cwxeditor_src/core/cwx/enumutils.hpp"
#include "cwxeditor_src/core/cwx/path.hpp"
#include "cwxeditor_src/core/cwx/warning.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/messageutils.hpp"
#include "cwxeditor_src/lib/text.hpp"
#include "cwxeditor_src/lib/ui_actions.hpp"
#include "cwxeditor_src/lib/ui_metrics.hpp"
#include "cwxeditor_src/lib/ui_window_lifecycle.hpp"
#include "cwxeditor_src/lib/window_host.hpp"
// UIの実装は一つに統一し、
// 別経路の実装を設けない。

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace cwxeditor::cwx::editor::gui::dwt::exprutils {
namespace {

namespace msgutils = ::cwxeditor::cwx::msgutils;
namespace structs = ::cwxeditor::cwx::structs;

[[nodiscard]] String lowerCopy(StringView value) {
    String result(value);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](const unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    return result;
}

[[nodiscard]] String trimCopy(StringView value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == StringView::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return String(value.substr(first, last - first + 1U));
}

[[nodiscard]] bool parseNumber(StringView value, double& result) {
    const String text = trimCopy(value);
    if (text.empty()) return false;
    std::size_t index = 0;
    try {
        result = std::stod(text, &index);
    } catch (...) {
        return false;
    }
    return index == text.size() && std::isfinite(result);
}

[[nodiscard]] bool isTrue(StringView value) {
    const auto lower = lowerCopy(trimCopy(value));
    return lower == "true" || lower == "1";
}

[[nodiscard]] String quoteText(StringView value, StringView prefix = {}) {
    String result(prefix);
    result.push_back('"');
    for (const char c : value) {
        if (c == '"') result += "\"\"";
        else result.push_back(c);
    }
    result.push_back('"');
    return result;
}

[[nodiscard]] incsearch::IncSearchSettings makeIncSearchSettings(
    const Props* prop) {
    incsearch::IncSearchSettings settings;
    if (!prop) return settings;
    const auto& messages = prop->requiredMsgs();
    settings.startIncrementalSearchWhenKeyDown =
        prop->var().startIncrementalSearchWhenKeyDown.value;
    settings.startIncrementalSearchWhenKeyDownProvider = [prop] {
        return prop && prop->var().startIncrementalSearchWhenKeyDown.value;
    };
    settings.incrementalSearchBoxWidth =
        prop->var().incrementalSearchBoxWidth.value;
    settings.incrementalSearchType = prop->var().incrementalSearchType.value;
    settings.containsLabel = messages.incSearchContains.value;
    settings.wildcardLabel = messages.incSearchWildcard.value;
    settings.regexLabel = messages.incSearchRegex.value;
    settings.windowTitle = messages.menuText(types::MenuID::IncSearch);
    settings.closeLabel = messages.menuTextCloseIncSearch.value;
    return settings;
}

[[nodiscard]] String variantTypeName(const VariantType type) {
    switch (type) {
    case VariantType::Number: return "number";
    case VariantType::String: return "string";
    case VariantType::Boolean: return "boolean";
    case VariantType::List: return "list";
    case VariantType::Structure: return "structure";
    }
    return "unknown";
}

[[nodiscard]] String argTypeNameText(const ArgType type) {
    switch (type) {
    case ArgType::Number: return "Number";
    case ArgType::String: return "String";
    case ArgType::Boolean: return "Boolean";
    case ArgType::List: return "List";
    case ArgType::Expression: return "Expression";
    case ArgType::Status: return "Status";
    case ArgType::CardInfo: return "Card information";
    case ArgType::NumberOrString: return "Number or string";
    case ArgType::NumberOrList: return "Number or list";
    case ArgType::Any: return "Any";
    case ArgType::Flag: return "Flag";
    case ArgType::Step: return "Step";
    case ArgType::Variant: return "Variant";
    case ArgType::VariantRef: return "Variant reference";
    case ArgType::NoArgument: return "No argument";
    }
    return "Unknown";
}

[[nodiscard]] String variableTypeName(const VariableType type) {
    switch (type) {
    case VariableType::Flag: return "フラグ";
    case VariableType::Step: return "ステップ";
    case VariableType::Variant: return "コモン";
    }
    return "変数";
}

[[nodiscard]] String functionCategoryName(const types::FunctionCategory category) {
    switch (category) {
    case types::FunctionCategory::StringOperation: return "String";
    case types::FunctionCategory::NumberOperation: return "Number";
    case types::FunctionCategory::ListOperation: return "List";
    case types::FunctionCategory::Conversion: return "Conversion";
    case types::FunctionCategory::VariableOperation: return "Variable";
    case types::FunctionCategory::CastInformation: return "Cast";
    case types::FunctionCategory::CardInformation: return "Card";
    case types::FunctionCategory::CouponInformation: return "Coupon";
    case types::FunctionCategory::PlayingInformation: return "Playing";
    case types::FunctionCategory::Etc: return "Other";
    }
    return "Other";
}

[[nodiscard]] std::vector<ArgType> candidateArgTypes(const ArgDef& arg) {
    std::vector<ArgType> result;
    switch (arg.type) {
    case ArgType::Number:
        result = {ArgType::Number, ArgType::Expression, ArgType::VariantRef};
        break;
    case ArgType::String:
        result = {ArgType::String, ArgType::Expression, ArgType::VariantRef};
        break;
    case ArgType::Flag:
        result = {ArgType::Flag, ArgType::Expression, ArgType::VariantRef};
        break;
    case ArgType::Step:
        result = {ArgType::Step, ArgType::Expression, ArgType::VariantRef};
        break;
    case ArgType::Variant:
        result = {ArgType::Variant, ArgType::Expression, ArgType::VariantRef};
        break;
    case ArgType::Boolean:
        result = {ArgType::Boolean, ArgType::Expression, ArgType::VariantRef};
        break;
    case ArgType::Any:
        result = {ArgType::String, ArgType::Number, ArgType::Boolean,
                  ArgType::Expression, ArgType::VariantRef};
        break;
    case ArgType::NumberOrString:
        result = {ArgType::String, ArgType::Number, ArgType::Expression,
                  ArgType::VariantRef};
        break;
    case ArgType::NumberOrList:
        result = {ArgType::Number, ArgType::List, ArgType::Expression,
                  ArgType::VariantRef};
        break;
    case ArgType::Status:
        result = {ArgType::Status, ArgType::Expression, ArgType::VariantRef};
        break;
    case ArgType::List:
        result = {ArgType::List, ArgType::Expression, ArgType::VariantRef};
        break;
    case ArgType::CardInfo:
        result = {ArgType::CardInfo, ArgType::Expression, ArgType::VariantRef};
        break;
    case ArgType::Expression:
    case ArgType::VariantRef:
        result = {arg.type};
        break;
    case ArgType::NoArgument:
        result = {ArgType::NoArgument};
        break;
    }
    if (arg.optional && std::find(result.begin(), result.end(), ArgType::NoArgument)
                         == result.end()) {
        result.insert(result.begin(), ArgType::NoArgument);
    }
    return result;
}

[[nodiscard]] String expressionErrorText(const expression::ExprError& error) {
    String result;
    if (error.errLine != 0U) {
        result = "line " + std::to_string(error.errLine);
        if (error.errPos != 0U) result += ", column " + std::to_string(error.errPos);
        result += ": ";
    }
    result += error.message;
    if (result.empty()) result = "invalid expression";
    return result;
}

[[nodiscard]] bool sameVariantType(const VariantVal& first,
                                   const VariantVal& second) noexcept {
    return first.valid == second.valid &&
           (!first.valid || first.type == second.type);
}

[[nodiscard]] String firstNonEmpty(StringView first, StringView second) {
    return first.empty() ? String(second) : String(first);
}

}  // namespace

ExpressionEventDialog::ExpressionEventDialog(
    Commons comm, Props& prop, lib::Owner<lib::Window> shell,
    const Summary* summ, const Skin* forceSkin, CounterOwner uc,
    ContentPtr parent, const CType cType, ContentPtr evt)
    : comm_(std::move(comm)), prop_(&prop), shell_(std::move(shell)),
      summary_(summ), force_skin_(forceSkin), counter_(std::move(uc)),
      parent_(std::move(parent)), type_(cType), event_(std::move(evt)) {
    if (!shell_) shell_ = std::make_shared<lib::Window>();
}

void ExpressionEventDialog::refreshWarning() {
    warnings_.clear();
    if (prop_ && !prop_->isTargetVersion(summary_, "4")) {
        warnings_.push_back(::cwxeditor::cwx::utils::tryFormat(
            prop_->requiredMsgs().warningUnknownContentWsn.value,
            prop_->requiredMsgs().contentName(type_), "4"));
    }
    if (expr_) {
        if (expr_->expression().empty() && prop_)
            warnings_.push_back(prop_->requiredMsgs().searchErrorNoExpression.value);
        const auto value = expr_->returnValue();
        if (prop_ && value.valid && value.type != VariantType::Boolean) {
            const auto type_name = [&]() -> String {
                switch (value.type) {
                case VariantType::Number: return prop_->requiredMsgs().numberValue.value;
                case VariantType::String: return prop_->requiredMsgs().stringValue.value;
                case VariantType::Boolean: return prop_->requiredMsgs().booleanValue.value;
                case VariantType::List: return prop_->requiredMsgs().listValue.value;
                case VariantType::Structure:
                    return ::cwxeditor::cwx::utils::tryFormat(
                        prop_->requiredMsgs().structValue.value, value.structName);
                }
                return {};
            }();
            warnings_.push_back(::cwxeditor::cwx::utils::tryFormat(
                prop_->requiredMsgs().warningExpressionNeedBooleanReturnType.value,
                type_name, prop_->requiredMsgs().contentName(type_)));
        }
        const auto& expressionWarnings = expr_->warnings();
        warnings_.insert(warnings_.end(), expressionWarnings.begin(),
                         expressionWarnings.end());
    }
}

bool ExpressionEventDialog::openCWXPath(const StringView path,
                                        const bool /*shellActivate*/) {
    const auto category = ::cwxeditor::cwx::path::cpcategory(path);
    if (category.empty()) return true;
    if (category != "expr") return false;
    if (expr_) expr_->setFocus();
    return true;
}

void ExpressionEventDialog::setup(lib::Container& area) {
    auto area_layout = std::make_shared<lib::GridLayout>();
    area_layout->columns = 1U;
    area_layout->padding = {6.0F, 4.0F, 6.0F, 4.0F};
    area.set_layout(area_layout);
    expression_group_ = std::make_shared<lib::GroupBox>();
    expression_group_->title = prop_ ? prop_->requiredMsgs().expression.value : "Expression";
    expression_group_->set_layout(std::make_shared<lib::FillLayout>());
    expression_group_->set_layout_item({.preferred={466.0F,226.0F},.grow=1.0F});
    area.add(expression_group_);
    expr_ = std::make_shared<ExpressionEditor>(
        comm_, summary_, force_skin_, counter_, expression_group_.get());
    expression_group_->add(expr_);
    expr_->modEvent.push_back([this] { refreshWarning(); });
    expr_->modReturnValueEvent.push_back([this] { refreshWarning(); });
    expr_->expression(event_ ? event_->expression() : String{});
    refreshWarning();
}

bool ExpressionEventDialog::apply() {
    if (!event_) {
        event_ = std::make_shared<Content>(type_, String{});
        if (parent_) event_->parent(parent_);
    }
    if (expr_) event_->expression(expr_->expression());
    return true;
}

SetVariantDialog::SetVariantDialog(
    Commons comm, Props& prop, lib::Owner<lib::Window> shell,
    const Summary* summ, const Skin* forceSkin, CounterOwner uc,
    ContentPtr parent, ContentPtr evt)
    : comm_(std::move(comm)), prop_(&prop), shell_(std::move(shell)),
      summary_(summ), force_skin_(forceSkin), counter_(std::move(uc)),
      parent_(std::move(parent)), event_(std::move(evt)) {
    if (!shell_) shell_ = std::make_shared<lib::Window>();
    chooser_comm_.services = comm_.services;
    chooser_comm_.menuText = [&prop](auto id) { return prop.buildMenu(id); };
    chooser_comm_.menuImage = [&prop](auto id) { return prop.images().menu(id); };
    chooser_properties_.editor_props = &prop;
    chooser_properties_.selectVariableWithTree = prop.var().selectVariableWithTree.value;
    chooser_properties_.logicalSort = prop.var().logicalSort.value;
    chooser_properties_.expandChooserItems = prop.var().expandChooserItems.value;
    chooser_properties_.flagsWidth = prop.var().flagsWidth.value;
    chooser_properties_.flagsHeight = prop.var().flagsHeight.value;
    chooser_properties_.images.flag = prop.images().flag();
    chooser_properties_.images.step = prop.images().step();
    chooser_properties_.images.variant = prop.images().variant();
    chooser_properties_.images.flagDir = prop.images().load(dprops::StringView{"flagdir.png"});
}

void SetVariantDialog::configureChooser(chooser::Commons comm,
                                         chooser::ChooserProperties properties) {
    chooser_comm_ = std::move(comm);
    chooser_properties_ = std::move(properties);
}

void SetVariantDialog::setAvailableNames(const VariableType type,
                                          const std::vector<String>& names) {
    available_names_[type] = names;
    // ローカルのルートを先にし、変数ディレクトリを直下の変数より
    // 先に並べる。兄弟名は設定された自然順・名前順で比較する。
    const auto parts = [](const String& path) {
        std::vector<String> result;
        std::size_t begin = 0;
        while (begin <= path.size()) {
            const auto end = path.find('\\', begin);
            if (end != begin) result.push_back(path.substr(begin, end == String::npos ? end : end - begin));
            if (end == String::npos) break;
            begin = end + 1;
        }
        return result;
    };
    std::stable_sort(available_names_[type].begin(), available_names_[type].end(),
        [this, &parts](const String& a, const String& b) {
            const bool aLocal = a.starts_with('\\'), bLocal = b.starts_with('\\');
            if (aLocal != bLocal) return aLocal;
            const auto left = parts(a), right = parts(b);
            for (std::size_t i = 0; i < std::min(left.size(), right.size()); ++i) {
                const bool leftDirectory = i + 1 < left.size();
                const bool rightDirectory = i + 1 < right.size();
                if (leftDirectory != rightDirectory) return leftDirectory;
                const auto cmp = prop_ && prop_->var().logicalSort.value
                    ? ::cwxeditor::cwx::utils::ncmp(left[i], right[i]) : left[i].compare(right[i]);
                if (cmp) return cmp < 0;
            }
            return a < b;
        });
    available_names_[type].erase(
        std::unique(available_names_[type].begin(), available_names_[type].end()),
        available_names_[type].end());
}

void SetVariantDialog::refFlagsAndSteps(
    const flag::FlagList& flags, const flag::StepList& steps,
    const flag::VariantList& variants) {
    const auto remember_identity = [this](const VariableType type) {
        if (selected_identities_[type]) return;
        const auto names = available_identities_.find(type);
        const auto selected = selected_names_.find(type);
        if (names == available_identities_.end() || selected == selected_names_.end()) return;
        if (const auto found = names->second.find(selected->second);
            found != names->second.end()) selected_identities_[type] = found->second;
    };
    remember_identity(VariableType::Flag);
    remember_identity(VariableType::Step);
    remember_identity(VariableType::Variant);

    std::vector<String> flag_names;
    std::vector<String> step_names;
    std::vector<String> variant_names;
    std::map<String, const void*> flag_identities, step_identities, variant_identities;
    for (const auto& value : flags) if (value) {
        flag_names.push_back(value->path());
        flag_identities[value->path()] = value.get();
    }
    for (const auto& value : steps) if (value) {
        step_names.push_back(value->path());
        step_identities[value->path()] = value.get();
    }
    for (const auto& value : variants) if (value) {
        variant_names.push_back(value->path());
        variant_identities[value->path()] = value.get();
    }
    const auto follow_rename = [this](const VariableType type,
                                      const std::map<String, const void*>& identities) {
        const auto identity = selected_identities_[type];
        if (!identity) return;
        const auto found = std::find_if(identities.begin(), identities.end(),
            [identity](const auto& item) { return item.second == identity; });
        if (found == identities.end()) {
            selected_identities_[type] = nullptr;
            return;
        }
        selected_names_[type] = found->first;
        if (selected_type_ == type) selected_name_ = found->first;
    };
    follow_rename(VariableType::Flag, flag_identities);
    follow_rename(VariableType::Step, step_identities);
    follow_rename(VariableType::Variant, variant_identities);
    available_identities_[VariableType::Flag] = std::move(flag_identities);
    available_identities_[VariableType::Step] = std::move(step_identities);
    available_identities_[VariableType::Variant] = std::move(variant_identities);
    setAvailableNames(VariableType::Flag, flag_names);
    setAvailableNames(VariableType::Step, step_names);
    setAvailableNames(VariableType::Variant, variant_names);
    updateEnabled();
}

void SetVariantDialog::updateEnabled() {
    const auto enabled = [this](const VariableType type) {
        const auto it = available_names_.find(type);
        return it != available_names_.end() && !it->second.empty();
    };
    if (!enabled(selected_type_)) {
        selected_names_[selected_type_] = selected_name_;
        if (enabled(VariableType::Variant)) selected_type_ = VariableType::Variant;
        else if (enabled(VariableType::Step)) selected_type_ = VariableType::Step;
        else if (enabled(VariableType::Flag)) selected_type_ = VariableType::Flag;
        selected_name_ = selected_names_[selected_type_];
    }
    for (const auto& [type, button] : variable_types_) {
        if (button) {
            button->set_enabled(enabled(type));
            button->checked = (type == selected_type_);
        }
    }
    selectedVariableType();
}

void SetVariantDialog::selectedType(const VariableType value) {
    if (selected_type_ != value) {
        selected_names_[selected_type_] = selected_name_;
        selected_name_ = selected_names_[value];
    }
    selected_type_ = value;
    for (const auto& [type, button] : variable_types_) {
        if (button) button->checked = type == value;
    }
    selectedVariableType();
}

void SetVariantDialog::selectedName(String value) {
    selected_name_ = std::move(value);
    selected_names_[selected_type_] = selected_name_;
    selected_identities_[selected_type_] = nullptr;
    if (const auto identities = available_identities_.find(selected_type_);
        identities != available_identities_.end()) {
        if (const auto found = identities->second.find(selected_name_);
            found != identities->second.end())
            selected_identities_[selected_type_] = found->second;
    }
    if (variable_chooser_) {
        synchronizing_chooser_ = true;
        variable_chooser_->selected(selected_name_);
        synchronizing_chooser_ = false;
        selected_name_ = variable_chooser_->selected();
        selected_names_[selected_type_] = selected_name_;
    }
}

void SetVariantDialog::selectedVariableType() {
    if (!variable_group_) return;
    const auto found = available_names_.find(selected_type_);
    const auto names = found == available_names_.end()
                           ? std::vector<String>{} : found->second;
    if (std::find(names.begin(), names.end(), selected_name_) == names.end()) selected_name_.clear();
    selected_names_[selected_type_] = selected_name_;
    if (!variable_chooser_ || chooser_type_ != selected_type_) {
        if (variable_chooser_) {
            variable_chooser_->saveExpanded();
            expanded_directories_[*chooser_type_] = variable_chooser_->expandedTable();
            variable_group_->remove(variable_chooser_->widget());
            variable_chooser_.reset();
        }
        chooser_type_ = selected_type_;
        const auto kind = selected_type_ == VariableType::Flag ? chooser::VariableKind::Flag
            : selected_type_ == VariableType::Step ? chooser::VariableKind::Step
            : chooser::VariableKind::Variant;
        // 従来の直接コンストラクターは Summary を借用する。ダイアログよ
        // り長く存続することが 前提であり、実際のホストはその所有者を保持する。
        auto summary_owner = summary_ ? std::const_pointer_cast<Summary>(summary_->weak_from_this().lock())
                                      : chooser::SummaryPtr{};
        if (!summary_owner && summary_)
            summary_owner = chooser::SummaryPtr(const_cast<Summary*>(summary_), [](Summary*) {});
        variable_chooser_ = std::make_unique<chooser::FlagChooserBase>(
            chooser_comm_, std::move(summary_owner), counter_, variable_group_.get(),
            true, false, false, kind, chooser_properties_);
        if (const auto saved = expanded_directories_.find(selected_type_);
            saved != expanded_directories_.end()) variable_chooser_->expandedTable() = saved->second;
        variable_chooser_->widget()->set_layout_item({.row=1U,.column=0U,.column_span=3U,
            .preferred={548.0F, lib::editor_window_host_options().all_mode ? 182.0F : 212.0F},
            .grow=1.0F});
        variable_chooser_->modEvent.push_back([this] {
            if (synchronizing_chooser_ || !variable_chooser_) return;
            // 共有の選択画面がアダプターより先に改名通知を受ける場合
            // がある。 パスだけによる代替検索で識別情報が上書きされ
            // る前に、記憶しているオブジェクトとの対応を確認する。
            const auto root = summary_ ? summary_->flagDirRoot() : flag::FlagDirPtr{};
            const auto flags = flag::allVars<flag::Flag>(root, counter_);
            const auto steps = flag::allVars<flag::Step>(root, counter_);
            const auto variants = flag::allVars<flag::Variant>(root, counter_);
            const auto renamed = [this](const auto& values) {
                const auto identity = selected_identities_[selected_type_];
                return identity && std::any_of(values.begin(), values.end(),
                    [this, identity](const auto& value) {
                        return value && value.get() == identity && value->path() != selected_name_;
                    });
            };
            if ((selected_type_ == VariableType::Flag && renamed(flags)) ||
                (selected_type_ == VariableType::Step && renamed(steps)) ||
                (selected_type_ == VariableType::Variant && renamed(variants))) {
                refFlagsAndSteps(flags, steps, variants);
                return;
            }
            selected_name_ = variable_chooser_->selected();
            selected_names_[selected_type_] = selected_name_;
            selected_identities_[selected_type_] = nullptr;
            if (const auto values = available_identities_.find(selected_type_);
                values != available_identities_.end()) {
                if (const auto item = values->second.find(selected_name_); item != values->second.end())
                    selected_identities_[selected_type_] = item->second;
            }
            refreshWarning();
        });
    }
    synchronizing_chooser_ = true;
    variable_chooser_->selected(selected_name_);
    synchronizing_chooser_ = false;
    selected_name_ = variable_chooser_->selected();
    selected_names_[selected_type_] = selected_name_;
    selected_identities_[selected_type_] = nullptr;
    if (const auto identities = available_identities_.find(selected_type_);
        identities != available_identities_.end()) {
        if (const auto value = identities->second.find(selected_name_);
            value != identities->second.end()) selected_identities_[selected_type_] = value->second;
    }
    refreshWarning();
}

void SetVariantDialog::showCalculationResult() {
    try {
        const auto value = expr_ ? expr_->calculateValue() : VariantVal::invalidValue();
        calculation_result_ = value.valid
                                   ? expression::variantValueToText(value)
                                   : (prop_ ? prop_->requiredMsgs().expressionResultUnavailable.value
                                            : "演算結果を取得できません。");
    } catch (const std::exception& error) {
        calculation_result_ = error.what();
    }
}

void SetVariantDialog::refreshWarning() {
    warnings_.clear();
    if (prop_ && !prop_->isTargetVersion(summary_, "4")) {
        warnings_.push_back(::cwxeditor::cwx::utils::tryFormat(
            prop_->requiredMsgs().warningUnknownContentWsn.value,
            prop_->requiredMsgs().contentName(CType::SetVariant), "4"));
    }
    if (expr_) {
        if (prop_ && selected_name_.empty())
            warnings_.push_back(prop_->requiredMsgs().searchErrorNoExpressionTarget.value);
        if (prop_ && expr_->expression().empty())
            warnings_.push_back(prop_->requiredMsgs().searchErrorNoExpression.value);
        const auto& expressionWarnings = expr_->warnings();
        warnings_.insert(warnings_.end(), expressionWarnings.begin(),
                         expressionWarnings.end());
        const auto value = expr_->returnValue();
        const bool flagTarget = selected_type_ == VariableType::Flag;
        const bool stepTarget = selected_type_ == VariableType::Step;
        if (value.valid &&
            ((flagTarget && value.type != VariantType::Boolean) ||
              (stepTarget && value.type != VariantType::Number))) {
            const auto type_name = [&]() -> String {
                if (!prop_) return variantTypeName(value.type);
                switch (value.type) {
                case VariantType::Number: return prop_->requiredMsgs().numberValue.value;
                case VariantType::String: return prop_->requiredMsgs().stringValue.value;
                case VariantType::Boolean: return prop_->requiredMsgs().booleanValue.value;
                case VariantType::List: return prop_->requiredMsgs().listValue.value;
                case VariantType::Structure:
                    return ::cwxeditor::cwx::utils::tryFormat(
                        prop_->requiredMsgs().structValue.value, value.structName);
                }
                return {};
            }();
            if (prop_) {
                warnings_.push_back(::cwxeditor::cwx::utils::tryFormat(
                    flagTarget ? prop_->requiredMsgs().warningExpressionToFlag.value
                               : prop_->requiredMsgs().warningExpressionToStep.value,
                    type_name));
            }
        }
    }
    const auto callbacks = warningChanged;
    for (const auto& callback : callbacks)
        if (callback) callback();
}

bool SetVariantDialog::openCWXPath(const StringView path,
                                   const bool /*shellActivate*/) {
    const auto category = ::cwxeditor::cwx::path::cpcategory(path);
    if (category.empty()) return true;
    if (category != "expr") return false;
    if (expr_) expr_->setFocus();
    return true;
}

void SetVariantDialog::setup(lib::Container& area) {
    auto area_layout = std::make_shared<lib::GridLayout>();
    area_layout->columns = 1U;
    area_layout->padding = {6.0F, 4.0F, 6.0F, 4.0F};
    area_layout->spacing = 5.0F;
    area.set_layout(area_layout);
    expr_ = std::make_shared<ExpressionEditor>(
        comm_, summary_, force_skin_, counter_, &area);
    expr_->set_layout_item({.preferred={558.0F,226.0F},.grow=1.0F});

    auto expression_group = std::make_shared<lib::GroupBox>();
    expression_group->title = prop_ ? prop_->requiredMsgs().expression.value : "式";
    expression_group->set_layout(std::make_shared<lib::FillLayout>());
    expression_group->set_layout_item({.preferred={558.0F,226.0F},.grow=1.0F});
    expression_group->add(expr_);

    auto expression_target_split = std::make_shared<lib::SplitView>();
    expression_target_split->orientation = lib::Orientation::Vertical;
    expression_target_split->weights = {
        static_cast<float>(std::max(1, prop_ ?
            prop_->var().expressionAndTargetSashT.value : 1)),
        static_cast<float>(std::max(1, prop_ ?
            prop_->var().expressionAndTargetSashB.value : 1))};
    expression_target_split->set_layout_item({.preferred={558.0F,
        lib::editor_window_host_options().all_mode ? 446.0F : 476.0F},.grow=1.0F});
    // D版の保存済み比率で初期配置した後、対象欄の初期の高さを維持
    // する。 枠のサイズ変更では式ペインだけを変える。仕切りを明示
    // 的にドラッグした場合は、 対象欄のサイズも変更して保存する。
    const auto target_height = std::make_shared<std::optional<float>>();
    const float target_ratio = expression_target_split->weights[1] /
        (expression_target_split->weights[0] + expression_target_split->weights[1]);
    const auto resize_target = [target_height](const std::vector<float>& weights) {
        if (weights.size() >= 2U && std::isfinite(weights[1]))
            *target_height = std::max(0.0F, weights[1]) / lib::scaled_ui_metric(1.0F);
    };
    expression_target_split->on_weights_previewed = resize_target;
    expression_target_split->on_weights_changed = [this, resize_target](const std::vector<float>& weights) {
        resize_target(weights);
        if (!prop_ || weights.size() < 2U) return;
        prop_->var().expressionAndTargetSashT.value =
            std::max(1, static_cast<std::int32_t>(std::lround(weights[0])));
        prop_->var().expressionAndTargetSashB.value =
            std::max(1, static_cast<std::int32_t>(std::lround(weights[1])));
    };
    area.add(expression_target_split);
    expression_target_split->add(expression_group);

    variable_group_ = std::make_shared<lib::GroupBox>();
    variable_group_->title = prop_ ? prop_->requiredMsgs().expressionTarget.value : "Expression target";
    auto variable_layout = std::make_shared<lib::GridLayout>();
    variable_layout->columns = 3U;
    variable_layout->spacing = 5.0F;
    variable_group_->set_layout(variable_layout);
    const float all_tree_reduction =
        lib::editor_window_host_options().all_mode ? 30.0F : 0.0F;
    variable_group_->set_layout_item({.preferred={558.0F,250.0F - all_tree_reduction}});
    expression_target_split->add(variable_group_);
    expression_target_split->set_child_layout_provider(
        [expression_group, target = variable_group_, target_height, target_ratio,
         all_tree_reduction](const lib::Rect bounds) {
            const float gap = std::min(std::max(0.0F, bounds.height),
                lib::scaled_ui_metric(lib::kEditorUiMetrics.split_spacing));
            const float available = std::max(0.0F, bounds.height - gap);
            const float expression_minimum = lib::minimum_ui_size(expression_group).height;
            const float target_minimum = lib::minimum_ui_size(target).height;
            const float minimum = expression_minimum + target_minimum;
            // ALL では、対象欄の初期の高さを保存済み比率による値より 30px 小さくする。
            const float requested = *target_height
                ? lib::scaled_ui_metric(**target_height)
                : available * target_ratio - lib::scaled_ui_metric(all_tree_reduction);
            const float height = minimum > available && minimum > 0.0F
                ? available * target_minimum / minimum
                : std::clamp(requested, target_minimum,
                             std::max(target_minimum, available - expression_minimum));
            // 初期化途中のゼロや小さすぎる配置サイズで、保存済みの比率を上書きしない。
            if (!*target_height && available > 0.0F && available >= minimum)
                *target_height = height / lib::scaled_ui_metric(1.0F);
            const float expression_height = available - height;
            return std::vector<lib::PreparedChildBounds>{
                {expression_group, {bounds.x, bounds.y, bounds.width, expression_height}},
                {target, {bounds.x, bounds.y + expression_height + gap, bounds.width, height}}};
        });
    const VariableType radioTypes[] = {
        VariableType::Variant, VariableType::Step, VariableType::Flag};
    for (const auto type : radioTypes) {
        auto button = std::make_shared<lib::RadioButton>();
        button->text = variableTypeName(type);
        button->group = "expression-target";
        button->on_changed = [this, type](const bool checked) {
            if (checked) selectedType(type);
        };
        variable_types_[type] = button;
        variable_group_->add(button);
    }

    const auto variable_root = summary_ ? summary_->flagDirRoot() : flag::FlagDirPtr{};
    refFlagsAndSteps(flag::allVars<flag::Flag>(variable_root, counter_),
                    flag::allVars<flag::Step>(variable_root, counter_),
                    flag::allVars<flag::Variant>(variable_root, counter_));
    if (event_) {
        expr_->expression(event_->expression());
        if (!event_->flag().empty()) {
            selected_type_ = VariableType::Flag;
            selected_name_ = event_->flag();
        } else if (!event_->step().empty()) {
            selected_type_ = VariableType::Step;
            selected_name_ = event_->step();
        } else {
            selected_type_ = VariableType::Variant;
            selected_name_ = event_->variant();
        }
    } else {
        expr_->expression({});
        selected_type_ = VariableType::Variant;
        selected_name_.clear();
    }
    updateEnabled();
    refreshWarning();

    calculation_button_ = std::make_shared<lib::Button>("演算結果を得る");
    calculation_button_->on_click = [this] {
        showCalculationResult();
        if (comm_.services.dialogs) {
            lib::MessageDialogRequest request;
            request.title = "演算結果";
            request.message = calculation_result_;
            request.buttons = lib::MessageButtons::OK;
            // Web/ALL では、ボタンのコールバックから同期的にメッセージボック
            // スを開くと イベントループに再入して停止するため、非同期で表示する。
            const auto dialogs = comm_.services.dialogs;
            if (dialogs->supports_message_async() &&
                dialogs->message_async(request, [](lib::MessageResult) {}))
                return;
            (void)dialogs->message(request);
        }
    };
    calculation_button_->set_layout_item({.preferred={120.0F,24.0F}});
    area.add(calculation_button_);
}

bool SetVariantDialog::apply() {
    if (!event_) {
        event_ = std::make_shared<Content>(CType::SetVariant, String{});
        if (parent_) event_->parent(parent_);
    }
    if (expr_) event_->expression(expr_->expression());
    event_->flag({});
    event_->step({});
    event_->variant({});
    switch (selected_type_) {
    case VariableType::Flag: event_->flag(selected_name_); break;
    case VariableType::Step: event_->step(selected_name_); break;
    case VariableType::Variant: event_->variant(selected_name_); break;
    }
    return true;
}

ExpressionEditor::ExpressionEditor(
    Commons comm, const Summary* summ, const Skin* forceSkin,
    CounterOwner uc, lib::Container* parent, const int style)
    : comm_(std::move(comm)), summary_(summ), force_skin_(forceSkin),
      counter_(std::move(uc)) {
    static_cast<void>(parent);
    static_cast<void>(style);
    auto root_layout = std::make_shared<lib::GridLayout>();
    root_layout->columns = 1U;
    root_layout->spacing = 4.0F;
    set_layout(root_layout);

    expr_ = std::make_shared<lib::TextEdit>();
    expr_->multiline = true;
    expr_->vertical_scroll = true;
    // ALL仕様: 縦方向節約のため式の欄を1行減らす。
    expr_->minimum_visible_lines =
        lib::editor_window_host_options().all_mode ? 9U : 10U;
    if (comm_.prop) {
        const auto configured = comm_.prop->adjustFont(comm_.prop->looks().textDlgFont(10));
        lib::FontSpec font;
        font.family = configured.name;
        font.points = static_cast<float>(configured.point);
        // ALL の式テキストには、小さい 10pt のテキ
        // ストダイアログ用フォントではなく、 他のダイアログ
        // の文字と同じ共通UIサイズの 12pt を使う。
        if (lib::editor_window_host_options().all_mode)
            font.points = std::max(font.points, 12.0F);
        font.weight = configured.bold ? 700 : 400;
        font.italic = configured.italic;
        expr_->set_font(font);
        expr_->tab_width = static_cast<std::size_t>(std::max(1U, comm_.prop->var().tabs.value));
    }
    expr_->set_layout_item({.row=0U,.column=0U,.preferred={450.0F,0.0F},.grow=1.0F});
    expr_->on_changed = [this, life=std::weak_ptr<int>(checking_lifetime_)](const std::string_view) {
        if (!life.expired()) modified();
    };
    add(expr_);
    dmenu::Commons textCommons;
    textCommons.services = comm_.services;
    if (auto* prop = comm_.prop) {
        // createTextMenu はタブ幅も設定するため、既定値
        // で上書きされないよう、 表示設定を通じて設定済みの値を渡す。
        textCommons.presentation.textTabs = static_cast<std::size_t>(
            std::max(1U, prop->var().tabs.value));
        textCommons.undoMaxEtc = static_cast<std::size_t>(std::max<std::int32_t>(0, prop->var().undoMaxEtc));
        textCommons.presentation.menuText = [prop](auto id) { return prop->buildMenu(id); };
        textCommons.presentation.menuImage = [prop](auto id) { return prop->images().menu(id); };
    }
    expression_text_menu_ = dmenu::createTextMenu(textCommons, dmenu::TMM(expr_));

    auto insert_row = std::make_shared<lib::Panel>();
    auto insert_layout = std::make_shared<lib::GridLayout>();
    insert_layout->columns = 7U;
    insert_layout->spacing = 4.0F;
    insert_row->set_layout(insert_layout);
    insert_row->set_layout_item({.row=1U,.column=0U,.preferred={450.0F,25.0F}});

    const auto insert_at_selection = [this, life=std::weak_ptr<int>(checking_lifetime_)](String value) {
        if (!life.expired()) insertExpressionText(std::move(value));
    };
    const auto* messages = comm_.prop ? &comm_.prop->requiredMsgs() : nullptr;
    const auto definitions = expression::functionDefinitions(
        comm_.prop ? &comm_.prop->parent() : nullptr);
    const auto add_variable_control = [&](const String& fallback,
                                          const std::vector<String>& names,
                                          const VariableType type,
                                          const lib::ResourceId icon,
                                          const String& insert_label,
                                          const String& reference_label,
                                          const std::vector<String>& functions) {
        auto combo = std::make_shared<lib::ComboBox>();
        combo->editable = false;
        combo->accessibility_name = fallback;
        combo->edit_text = names.empty() ? String{} : names.front();
        for (const auto& name : names) combo->items.push_back({name, name});
        if (!names.empty()) combo->selected_index = 0;
        combo->set_enabled(!names.empty());
        variable_choices_.emplace_back(type, combo);
        combo->on_selection_changed = [combo](const auto index) {
            if (index && *index < combo->items.size())
                combo->edit_text = combo->items[*index].value;
        };
        combo->set_layout_item({.preferred={114.0F,23.0F},.grow=1.0F});
        insert_row->add(combo);

        const std::weak_ptr<int> life = checking_lifetime_;
        const std::weak_ptr<lib::ComboBox> weak_combo = combo;
        auto search = std::make_shared<incsearch::IncSearch>(
            comm_.services, combo.get(),
            [weak_combo] {
                const auto value = weak_combo.lock();
                return value && value->enabled();
            }, std::vector<incsearch::AdditionMatcher>{},
            makeIncSearchSettings(comm_.prop));
        const std::weak_ptr<incsearch::IncSearch> weak_search = search;
        variable_inc_searches_.push_back(search);
        search->modEvent.push_back([this, life] {
            if (!life.expired()) refreshVariableChoices();
        });
        search->searchTypeChanged = [this, life](const incsearch::SearchType value) {
            if (!life.expired() && comm_.prop)
                comm_.prop->var().incrementalSearchType.value =
                    static_cast<std::int32_t>(value);
        };
        combo->on_text_input = [life, weak_combo, weak_search](
                                   lib::TextInputEvent& event) {
            if (life.expired() || event.handled || event.composing) return;
            const auto host = weak_combo.lock();
            const auto popup = weak_search.lock();
            if (host && popup && popup->handleHostTextInput(event, host.get()))
                event.handled = true;
        };
        combo->on_key_down = [life, weak_combo, weak_search](lib::KeyEvent& event) {
            if (life.expired() || event.handled) return;
            const auto host = weak_combo.lock();
            const auto popup = weak_search.lock();
            if (host && popup && popup->handleHostKeyDown(event, host.get()))
                event.handled = true;
        };
        combo->on_blur = [life, weak_search, gui = comm_.services.gui] {
            if (life.expired() || !gui) return;
            if (const auto popup = weak_search.lock())
                popup->handleFocusChanged(gui->focused_widget().get());
        };

        auto menu = std::make_shared<lib::Menu>(true);
        lib::MenuItem insert_path;
        insert_path.text = insert_label;
        insert_path.command = "insert-path";
        insert_path.icon = icon;
        insert_path.action = [combo, type, insert_at_selection] {
            if (combo->edit_text.empty()) return;
            insert_at_selection(quoteText(combo->edit_text));
        };
        menu->items.push_back(std::move(insert_path));
        if (type == VariableType::Variant) {
            lib::MenuItem insert_reference;
            insert_reference.text = reference_label;
            insert_reference.command = "insert-reference";
            insert_reference.icon = comm_.prop ? comm_.prop->images().variantRef()
                                               : lib::ResourceId{};
            insert_reference.action = [combo, insert_at_selection] {
                if (!combo->edit_text.empty())
                    insert_at_selection(quoteText(combo->edit_text, "@"));
            };
            menu->items.push_back(std::move(insert_reference));
        }
        menu->items.push_back(lib::MenuItem{.separator=true});
        for (const auto& name : functions) {
            const auto found = std::find_if(definitions.begin(), definitions.end(),
                [&](const FuncDef& value) { return value.name == name; });
            if (found == definitions.end()) continue;
            lib::MenuItem item;
            item.text = found->shortDesc.empty()
                ? found->name : found->name + " - " + found->shortDesc;
            item.command = "function-" + found->name;
            item.icon = comm_.prop ? comm_.prop->images().functions()
                                   : lib::ResourceId{};
            item.action = [this, combo, name = found->name,
                           life=std::weak_ptr<int>(checking_lifetime_)] {
                if (!life.expired()) showFuncWin(name, {{ArgType::String, combo->edit_text}});
            };
            menu->items.push_back(std::move(item));
        }
        menu->on_open = [menu, combo] {
            for (auto& item : menu->items)
                if (item.command == "insert-path" || item.command == "insert-reference")
                    item.enabled = !combo->edit_text.empty();
        };

        auto search_menu = std::make_shared<lib::Menu>(true);
        lib::MenuItem incremental_search;
        incremental_search.text = comm_.prop
            ? comm_.prop->requiredMsgs().menuText(types::MenuID::IncSearch)
            : String{"絞り込み検索"};
        incremental_search.command = "incremental-search";
        incremental_search.icon = comm_.prop
            ? comm_.prop->images().menu(types::MenuID::IncSearch)
            : lib::ResourceId{};
        incremental_search.action = [weak_combo, weak_search,
                                     gui = comm_.services.gui] {
            const auto host = weak_combo.lock();
            const auto search = weak_search.lock();
            if (!host || !search) return;
            if (gui) gui->focus(host);
            search->startIncSearch();
        };
        search_menu->items.push_back(std::move(incremental_search));
        search_menu->items.push_back(lib::MenuItem{.separator = true});
        lib::MenuItem open_variable;
        open_variable.text = comm_.prop
            ? comm_.prop->requiredMsgs().menuText(types::MenuID::OpenAtVarView)
            : String{"変数ビューで開く"};
        open_variable.command = "open-variable-view";
        open_variable.icon = comm_.prop
            ? comm_.prop->images().menu(types::MenuID::OpenAtVarView)
            : lib::ResourceId{};
        open_variable.action = [this, life, weak_combo, type] {
            if (life.expired() || !summary_ || !comm_.open_cwx_path) return;
            const auto host = weak_combo.lock();
            if (!host || !host->selected_index ||
                *host->selected_index >= host->items.size()) return;
            const auto root = summary_->flagDirRoot();
            switch (type) {
            case VariableType::Flag:
                if (const auto value = flag::findVar<flag::Flag>(
                        root, counter_, host->edit_text))
                    (void)comm_.open_cwx_path(value->cwxPath(true), false);
                break;
            case VariableType::Step:
                if (const auto value = flag::findVar<flag::Step>(
                        root, counter_, host->edit_text))
                    (void)comm_.open_cwx_path(value->cwxPath(true), false);
                break;
            case VariableType::Variant:
                if (const auto value = flag::findVar<flag::Variant>(
                        root, counter_, host->edit_text))
                    (void)comm_.open_cwx_path(value->cwxPath(true), false);
                break;
            }
        };
        search_menu->items.push_back(std::move(open_variable));
        const std::weak_ptr<lib::Menu> weak_search_menu = search_menu;
        search_menu->on_open = [this, life, weak_combo, weak_search_menu] {
            if (life.expired()) return;
            const auto menu = weak_search_menu.lock();
            if (!menu) return;
            const auto host = weak_combo.lock();
            const bool has_selection = host && host->selected_index &&
                *host->selected_index < host->items.size();
            for (auto& item : menu->items) {
                if (item.command == "incremental-search")
                    item.enabled = host && host->enabled();
                else if (item.command == "open-variable-view")
                    item.enabled = has_selection && summary_ &&
                        static_cast<bool>(comm_.open_cwx_path);
            }
        };
        combo->on_pointer = [life, weak_combo, search_menu,
                             gui = comm_.services.gui](lib::PointerEvent& event) {
            if (life.expired() || event.handled ||
                event.action != lib::PointerAction::Up ||
                event.button != lib::PointerButton::Secondary || !gui)
                return;
            const auto host = weak_combo.lock();
            if (!host) return;
            gui->show_context_menu(search_menu, host, event.position);
            event.handled = true;
        };

        auto popup = std::make_shared<lib::Button>();
        popup->icon = icon;
        popup->set_tooltip(type == VariableType::Flag
            ? (messages ? messages->insertFlagIntoExpression.value : "フラグ名または関数の挿入")
            : type == VariableType::Step
                ? (messages ? messages->insertStepIntoExpression.value : "ステップ名または関数の挿入")
                : (messages ? messages->insertVariantIntoExpression.value : "コモン名または参照の挿入"));
        popup->set_layout_item({.preferred={24.0F,23.0F}});
        popup->on_click = [this, popup, menu, life=std::weak_ptr<int>(checking_lifetime_)] {
            if (life.expired()) return;
            if (comm_.services.gui)
                // 基準点はウィンドウ座標。ウィン
                // ドウ左上ではなく、 D版と同様
                // にボタンの下端に一覧を開く。
                comm_.services.gui->show_context_menu(menu, popup,
                    {popup->bounds().x, popup->bounds().y + popup->bounds().height});
        };
        popup->set_enabled(!names.empty());
        variable_popup_buttons_.push_back(popup);
        insert_row->add(popup);
    };
    std::vector<String> flag_names;
    std::vector<String> step_names;
    std::vector<String> variant_names;
    const auto variable_root = summary_ ? summary_->flagDirRoot() : flag::FlagDirPtr{};
    for (const auto& value : flag::allVars<flag::Flag>(variable_root, counter_))
        if (value) flag_names.push_back(value->path());
    for (const auto& value : flag::allVars<flag::Step>(variable_root, counter_))
        if (value) step_names.push_back(value->path());
    for (const auto& value : flag::allVars<flag::Variant>(variable_root, counter_))
        if (value) variant_names.push_back(value->path());
    add_variable_control("フラグ", flag_names, VariableType::Flag,
        comm_.prop ? comm_.prop->images().flag() : lib::ResourceId{},
        messages ? messages->insertFlagPath.value : "フラグ名の挿入", {},
        {"FLAGVALUE", "FLAGTEXT"});
    add_variable_control("ステップ", step_names, VariableType::Step,
        comm_.prop ? comm_.prop->images().step() : lib::ResourceId{},
        messages ? messages->insertStepPath.value : "ステップ名の挿入", {},
        {"STEPVALUE", "STEPTEXT", "STEPMAX"});
    add_variable_control("コモン", variant_names, VariableType::Variant,
        comm_.prop ? comm_.prop->images().variant() : lib::ResourceId{},
        messages ? messages->insertVariantPath.value : "コモン名の挿入",
        messages ? messages->insertVariantReference.value : "コモン参照の挿入",
        {"VAR"});
    function_button_ = std::make_shared<lib::ToggleButton>(
        messages ? messages->insertFunctionIntoExpression.value : "関数の挿入");
    if (comm_.prop) function_button_->icon = comm_.prop->images().functions();
    function_button_->on_changed = [this, life=std::weak_ptr<int>(checking_lifetime_)](const bool checked) {
        if (!life.expired()) showFuncWin(checked);
    };
    function_button_->set_layout_item({.preferred={102.0F,23.0F}});
    insert_row->add(function_button_);
    add(insert_row);

    auto operator_row = std::make_shared<lib::Panel>();
    auto operator_layout = std::make_shared<lib::GridLayout>();
    operator_layout->columns = 2U;
    operator_layout->spacing = 4.0F;
    operator_row->set_layout(operator_layout);
    operator_row->set_layout_item({.row=2U,.column=0U,.preferred={450.0F,44.0F}});
    auto operator_label = std::make_shared<lib::Label>(
        messages ? messages->operators.value : "演算子:");
    operator_row->add(operator_label);
    auto operator_text = std::make_shared<lib::TextEdit>(
        messages ? messages->operatorList.value :
        "加算: A + B, 減算: A - B, 乗算: A * B, 除算: A / B, 剰余: A % B");
    operator_text->read_only = true;
    operator_text->multiline = true;
    operator_text->wrap = true;
    operator_text->vertical_scroll = false;
    // 説明本文に基準寸法を持たせ、文字サイズ変更時に共通計測で幅・高さを追従させる。
    operator_text->set_layout_item({.minimum={390.0F,44.0F},.grow=1.0F});
    operator_row->add(operator_text);
    add(operator_row);

    checkThread();
}

ExpressionEditor::~ExpressionEditor() {
    checking_lifetime_.reset();
    if (checking_timer_ && comm_.services.dispatcher)
        comm_.services.dispatcher->cancel_timer(checking_timer_);
    showFuncWin(false);
}

expression::VariableInfo ExpressionEditor::variableInfo() const {
    const auto root = summary_ ? summary_->flagDirRoot() : flag::FlagDirPtr{};
    const auto counter = counter_;
    const auto* prop = comm_.prop;
    const auto system_steps = prop ? messageutils::getPreviewSysSteps(*prop, summary_, true)
                                   : msgutils::VarGetter{};
    const auto flags = [root, counter](const String& path) -> msgutils::VarValue {
        const auto value = flag::findVar<flag::Flag>(root, counter, path);
        return value ? msgutils::VarValue{true, value->onOff() ? value->on() : value->off(), value->expandSPChars()}
                     : msgutils::VarValue{};
    };
    const auto steps = [root, counter, system_steps](const String& path) -> msgutils::VarValue {
        const auto value = flag::findVar<flag::Step>(root, counter, path);
        return value ? msgutils::VarValue{true, value->value(), value->expandSPChars()}
                     : system_steps ? system_steps(lowerCopy(path)) : msgutils::VarValue{};
    };
    const auto variants = [root, counter](const String& path) -> msgutils::VarValue {
        const auto value = flag::findVar<flag::Variant>(root, counter, path);
        return value ? msgutils::VarValue{true, expression::variantValueToPreviewText(*value), false}
                     : msgutils::VarValue{};
    };
    return expression::VariableInfo(prop ? &prop->parent() : nullptr, summary_, counter_,
        prop ? prop->var().targetVersion.value : String{}, {}, flags, steps, variants, system_steps,
        [prop](std::uint32_t) { return prop ? prop->var().messageVarSelected.value : String{}; },
        [prop](std::int32_t, std::uint32_t) { return prop ? prop->var().messageVarCard.value : String{}; },
        [prop] { return prop ? prop->var().messageVarTeam.value : String{}; },
        [prop] { return prop ? prop->var().messageVarYado.value : String{}; },
        [summ = summary_] { return summ ? summ->scenarioName() : String{}; },
        [] { return 0.0; }, [] { return 0.0; },
        [](std::int32_t, std::uint32_t, const String&) { return VariantVal::invalidValue(); },
        [](std::int32_t, std::uint32_t, const String&) { return false; },
        [](std::int32_t, std::uint32_t, const String&) { return std::uint32_t{}; });
}

void ExpressionEditor::checkThread() {
    checkExpression(false);
}

void ExpressionEditor::insertExpressionText(String value) {
    if (!expr_ || value.empty()) return;
    const auto text = expr_; // 通知中はネイティブコントロールの寿命を保持する。
    const auto start = std::min(text->selection_start, text->text.size());
    const auto length = std::min(text->selection_length, text->text.size()-start);
    text->text.replace(start, length, value);
    text->selection_start = start+value.size();
    text->selection_length = 0U;
    // createTextMenu で包んだネイティブの Modify 経路。 D版
    // の Text.insert と同様、変数・関数の挿入を別の編集として記録する。
    const auto changed = text->on_changed;
    const String changed_text = text->text;
    if (changed) changed(changed_text);
    // コールバックがこのエディタを無効化する場合があるため、この後は this にアクセスしない。
}

void ExpressionEditor::modified() {
    const std::weak_ptr<int> life=checking_lifetime_;
    const auto callbacks=modEvent;
    for (const auto& callback : callbacks) {
        if (callback) callback();
        if (life.expired()) return;
    }
    if (!comm_.services.dispatcher) {
        checkExpression(true);
        return;
    }
    if (checking_timer_)
        comm_.services.dispatcher->cancel_timer(checking_timer_);
    const auto delay = std::chrono::milliseconds(std::max<std::int32_t>(
        0, comm_.prop ? comm_.prop->var().expressionCheckingDelay.value : 0));
    const std::weak_ptr<int> lifetime = checking_lifetime_;
    checking_timer_ = comm_.services.dispatcher->post_delayed(delay, [this, lifetime] {
        if (lifetime.expired()) return;
        checking_timer_ = {};
        checkExpression(true);
    });
}

void ExpressionEditor::checkExpression(const bool raiseModEvent) {
    if (raised_expression_ == expr_->text) return;
    checkExpressionImpl(raiseModEvent);
}

void ExpressionEditor::checkExpressionImpl(const bool raiseModEvent) {
    const auto oldWarnings = warnings_;
    const auto oldValue = return_value_;
    raised_expression_ = expr_->text;
    warnings_.clear();

    try {
        expression::Expression expression(expr_->text);
        const auto prop = comm_.prop ? &comm_.prop->parent() : nullptr;
        const auto info = variableInfo();
        const auto errors = expression.getExpressionErrors(prop, info);
        if (prop) {
            warnings_ = ::cwxeditor::cwx::warning::exprErrorToWarnings(*prop, expr_->text, errors);
        } else {
            for (const auto& error : errors) warnings_.push_back(expressionErrorText(error));
        }
        return_value_ = expression.returnValue(prop);
    } catch (const std::exception& error) {
        warnings_.push_back(error.what());
        return_value_ = VariantVal::invalidValue();
    }
    if (!raiseModEvent) return;
    const std::weak_ptr<int> life=checking_lifetime_;
    const bool warnings_changed=warnings_!=oldWarnings;
    const bool value_changed=!sameVariantType(oldValue,return_value_);
    const auto modified_callbacks=modEvent;
    const auto value_callbacks=modReturnValueEvent;
    if (warnings_changed) for (const auto& callback:modified_callbacks) {
        if (callback) callback();
        if (life.expired()) return;
    }
    if (value_changed) for (const auto& callback:value_callbacks) {
        if (callback) callback();
        if (life.expired()) return;
    }
}

void ExpressionEditor::refreshVariableChoices() {
    const auto root = summary_ ? summary_->flagDirRoot() : flag::FlagDirPtr{};
    for (std::size_t choice_index = 0;
         choice_index < variable_choices_.size(); ++choice_index) {
        const auto& [type, combo] = variable_choices_[choice_index];
        const auto search = choice_index < variable_inc_searches_.size()
            ? variable_inc_searches_[choice_index]
            : std::shared_ptr<incsearch::IncSearch>{};
        const auto old = combo->edit_text;
        std::vector<lib::ChoiceItem> items;
        std::size_t available_count{};
        const auto collect = [&items, &search, &available_count](const auto& values) {
            for (const auto& value : values) {
                if (!value) continue;
                ++available_count;
                if (!search || search->match(value->path()))
                    items.push_back({value->path(), value->path()});
            }
        };
        switch (type) {
        case VariableType::Flag: collect(flag::allVars<flag::Flag>(root, counter_)); break;
        case VariableType::Step: collect(flag::allVars<flag::Step>(root, counter_)); break;
        case VariableType::Variant: collect(flag::allVars<flag::Variant>(root, counter_)); break;
        }
        std::optional<std::size_t> selected;
        for (std::size_t i = 0; i < items.size(); ++i) if (items[i].value == old) selected = i;
        if (!selected && !items.empty()) selected = 0U;
        combo->edit_text = selected ? items[*selected].value : String{};
        (void)lib::replace_combo_items(combo, std::move(items), selected);
        combo->set_enabled(available_count != 0U);
        if (choice_index < variable_popup_buttons_.size())
            variable_popup_buttons_[choice_index]->set_enabled(
                !combo->items.empty());
        // D版と同様、ホストが使えなくなったら IncSearch を閉じる。
        if (choice_index < variable_inc_searches_.size() &&
            variable_inc_searches_[choice_index])
            variable_inc_searches_[choice_index]->checkEnabled();
    }
}

void ExpressionEditor::refFlagAndStep(const flag::FlagList&,
                                      const flag::StepList&,
                                      const flag::VariantList&) {
    refreshVariableChoices();
    if (function_editor_) function_editor_->refFlagAndStep();
    raised_expression_.clear();
    checkExpression(true);
}

void ExpressionEditor::refDataVersion() {
    refreshVariableChoices();
    raised_expression_.clear();
    checkExpression(true);
}

void ExpressionEditor::setFocus() {
    // D版の expr 経路と同様、選択状態を変えたり Shell
    // の有効化通知を生成したりせず、 エディタへフォーカスを移す。
    const auto text = expr_;
    const auto gui = comm_.services.gui;
    if (text && gui) gui->focus(text);
}

String ExpressionEditor::expression() const {
    // D版の取得処理に合わせ、OSごとの改行処理は共通の境界部分に任せる。
    return lib::normalize_editor_control_newlines(
        expr_ ? expr_->text : String{});
}

void ExpressionEditor::expression(String value) {
    if (!expr_) return;
    const auto text=expr_;
    const std::weak_ptr<int> life=checking_lifetime_;
    text->text = std::move(value);
    raised_expression_.clear();
    // D版の Text.setText と同様に Modify も通知し、
    // 既存の式への最初の変数・関数挿入より前に テキストの履歴を同期する。
    const auto changed=text->on_changed;
    const String changed_text=text->text;
    if (changed) changed(changed_text);
    if (!life.expired()) checkExpression(true);
}

VariantVal ExpressionEditor::calculateValue() const {
    // 値を利用できない状態と計算失敗は区別する。 通常の SetVariant
    // のプレビュー呼び出し元は、評価時の例外を詳細付きで表示する。
    const auto prop = comm_.prop ? &comm_.prop->parent() : nullptr;
    return expression::eval(prop, expression::EvalMode::All, variableInfo(),
                            expression());
}

void ExpressionEditor::showFuncWin(String name, const FunctionArguments& args) {
    // D版と同様、再利用するモードレスの補助画面を開く前に、指定の関数と引数を選択する。 ネイ
    // ティブの Show により、その所有者が閉じられたり置換・無効化されたりする場合がある。
    const std::weak_ptr<int> lifetime=checking_lifetime_;
    showFuncWinImpl(true,false);
    if (lifetime.expired()) return;
    const auto helper=function_editor_;
    if (!helper) return;
    helper->selectFunction(std::move(name),args);
    if (lifetime.expired() || function_editor_!=helper) return;
    const auto shell=helper->shell();
    const auto gui=comm_.services.gui;
    shell->result=lib::DialogResult::None;
    if (gui) gui->show(shell);
    if (lifetime.expired() || function_editor_!=helper ||
        shell->result==lib::DialogResult::Cancel) return;
    helper->focusToArgs();
}

void ExpressionEditor::showFuncWin(const bool open) { showFuncWinImpl(open,true); }

void ExpressionEditor::showFuncWinImpl(const bool open,const bool show) {
    if (open) {
        const std::weak_ptr<int> lifetime=checking_lifetime_;
        if (!function_editor_) {
            const auto comm=comm_;
            const auto summary=summary_;
            const auto counter=counter_;
            auto helper=std::make_shared<FunctionCallEditor>(
                comm,summary,counter,this,
                [this,lifetime](String value) {
                    if (!lifetime.expired()) insertExpressionText(std::move(value));
                });
            if (lifetime.expired()) return;
            function_editor_=helper;
            const auto shell=helper->shell();
            shell->on_close_requested=[this,weak=std::weak_ptr<lib::Dialog>(shell),
                source=std::weak_ptr<FunctionCallEditor>(helper),lifetime] {
                if (const auto value=weak.lock()) {
                    value->result=lib::DialogResult::Cancel;
                    value->set_visible(false);
                }
                if (lifetime.expired()) return;
                const auto helper=source.lock();
                if (!helper || function_editor_!=helper) return;
                if (function_button_) function_button_->checked=false;
                helper->saveWindowState();
                function_editor_.reset();
            };
        }
        if (function_button_) function_button_->checked=true;
        const auto shell=function_editor_->shell();
        const auto gui=comm_.services.gui;
        shell->result=lib::DialogResult::None;
        if (show && gui) gui->show(shell);
        // ネイティブの Show の通知後は this にアクセスしない。
    } else {
        if (function_button_) function_button_->checked=false;
        if (!function_editor_) return;
        const auto editor=std::move(function_editor_);
        const auto gui=comm_.services.gui;
        editor->saveWindowState();
        const auto shell=editor->shell();
        shell->on_close_requested={};
        shell->result=lib::DialogResult::Cancel;
        shell->set_visible(false);
        if (gui) gui->close(shell);
    }
}

FunctionCallEditor::ArgsUndo::ArgsUndo(FunctionCallEditor* owner)
    : owner_(owner) {
    store();
}

void FunctionCallEditor::ArgsUndo::store() {
    if (!owner_) return;
    selected_ = owner_->selected_row_;
    selected_arg_types_ = owner_->selected_arg_types_;
    values_.clear();
    if (owner_->args_) {
        for (const auto& row : owner_->args_->rows) {
            values_.push_back(row.cells.size() > 2U ? row.cells[2].value : String{});
        }
    }
}

void FunctionCallEditor::ArgsUndo::impl() {
    if (owner_) owner_->applySnapshot(selected_, selected_arg_types_, values_);
}

void FunctionCallEditor::ArgsUndo::undo() {
    impl();
}

void FunctionCallEditor::ArgsUndo::redo() {
    impl();
}

void FunctionCallEditor::ArgsUndo::dispose() noexcept {
    owner_ = nullptr;
    selected_arg_types_.clear();
    values_.clear();
}

FunctionCallEditor::FunctionCallEditor(
    Commons comm, const Summary* summ, CounterOwner uc, lib::Widget* parent,
    std::function<void(String)> insert)
    : comm_(std::move(comm)), summary_(summ), counter_(std::move(uc)),
      insert_(std::move(insert)) {
    (void)parent;
    const auto* messages = comm_.prop ? &comm_.prop->requiredMsgs() : nullptr;
    shell_ = std::make_shared<lib::Dialog>();
    shell_->title = messages
        ? messages->insertFunctionIntoExpression.value : "関数の挿入";
    lib::configure_owned_window(shell_, comm_.dialogOwner,
        lib::OwnedWindowKind::Modeless);
    const float function_width = comm_.prop
        ? static_cast<float>(std::max(320, comm_.prop->var().functionCallEditorWidth.value))
        : 377.0F;
    // 従来の既定サイズ 400 に、16 単位の説明行二つ分を追加する。 古い保
    // 存サイズも下限に収めるが、開くたびにウィンドウが大きくならないようにする。
    const float function_height = comm_.prop
        ? static_cast<float>(std::max(432, comm_.prop->var().functionCallEditorHeight.value))
        : 432.0F;
    shell_->set_bounds({0.0F, 0.0F, function_width, function_height});
    shell_->resizable = true;
    shell_->minimizable = false;
    shell_->maximizable = false;
    auto form_layout = std::make_shared<lib::GridLayout>();
    form_layout->columns = 3U;
    form_layout->spacing = 5.0F;
    form_layout->padding = {6.0F, 6.0F, 6.0F, 6.0F};
    shell_->set_layout(form_layout);
    const auto place = [](const lib::WidgetPtr& widget, const std::size_t row,
                          const std::size_t column,
                          const std::size_t column_span = 1U,
                          const float grow = 0.0F,
                          const lib::Size minimum = lib::Size{}) {
        widget->set_layout_item({row, column, 1U, column_span,
                                 lib::Align::Stretch, lib::Align::Center,
                                 minimum, {}, {}, grow});
    };
    const auto add_label = [&](const String& text, const std::size_t row) {
        auto label = std::make_shared<lib::Label>(text);
        place(label, row, 0U);
        shell_->add(label);
    };

    category_ = std::make_shared<lib::ComboBox>();
    category_->items.push_back(lib::ChoiceItem{
        messages ? messages->allFunctions.value : "全て", {}, {}, true});
    categories_ = {
        types::FunctionCategory::StringOperation,
        types::FunctionCategory::NumberOperation,
        types::FunctionCategory::ListOperation,
        types::FunctionCategory::Conversion,
        types::FunctionCategory::VariableOperation,
        types::FunctionCategory::CastInformation,
        types::FunctionCategory::CardInformation,
        types::FunctionCategory::CouponInformation,
        types::FunctionCategory::PlayingInformation,
        types::FunctionCategory::Etc};
    for (const auto category : categories_) {
        const auto name = messages
            ? messages->functionCategoryName(category)
            : functionCategoryName(category);
        category_->items.push_back(lib::ChoiceItem{name, name, {}, true});
    }
    category_->selected_index = 0U;
    category_->visible_item_count = comm_.prop
        ? static_cast<std::size_t>(std::max(
              1, comm_.prop->var().comboVisibleItemCount.value))
        : 20U;
    category_->on_selection_changed = [this](const std::optional<std::size_t>) {
        categorySelected();
    };

    function_search_ = std::make_shared<lib::TextEdit>();
    function_search_->on_changed = [this](const std::string_view) {
        functionSearchModified();
    };
    reference_ = std::make_shared<lib::Button>(messages
        ? messages->openReferenceLabel.value : "参考を開く");
    reference_->set_enabled(false);
    reference_->on_click = [this] { openFunctionReference(); };
    function_ = std::make_shared<lib::ComboBox>();
    function_->visible_item_count = comm_.prop
        ? static_cast<std::size_t>(std::max(
              1, comm_.prop->var().comboVisibleItemCount.value))
        : 20U;
    function_->on_selection_changed = [this](const std::optional<std::size_t>) {
        functionSelected();
    };
    const auto function_search_settings = makeIncSearchSettings(comm_.prop);
    function_inc_search_ = std::make_shared<incsearch::IncSearch>(
        comm_.services, function_.get(), [] { return true; },
        std::vector<incsearch::AdditionMatcher>{}, function_search_settings);
    const auto function_life = std::weak_ptr<int>(insertion_lifetime_);
    const auto weak_function = std::weak_ptr<lib::ComboBox>(function_);
    const auto weak_search = std::weak_ptr<incsearch::IncSearch>(function_inc_search_);
    function_inc_search_->modEvent.push_back([this, function_life] {
        if (!function_life.expired()) updateFunctions();
    });
    function_->on_text_input = [function_life, weak_function, weak_search](lib::TextInputEvent& event) {
        if (function_life.expired() || event.handled || event.composing) return;
        const auto combo = weak_function.lock();
        const auto search = weak_search.lock();
        if (combo && search && search->handleHostTextInput(event, combo.get())) event.handled = true;
    };
    function_->on_key_down = [function_life, weak_function, weak_search](lib::KeyEvent& event) {
        if (function_life.expired() || event.handled) return;
        const auto combo = weak_function.lock();
        const auto search = weak_search.lock();
        if (combo && search && search->handleHostKeyDown(event, combo.get())) event.handled = true;
    };
    function_->on_blur = [function_life, weak_search, gui=comm_.services.gui] {
        if (function_life.expired() || !gui) return;
        if (const auto search = weak_search.lock()) search->handleFocusChanged(gui->focused_widget().get());
    };
    function_inc_search_->searchTypeChanged = [this, function_life](const incsearch::SearchType type) {
        if (!function_life.expired() && comm_.prop)
            comm_.prop->var().incrementalSearchType.value = static_cast<std::int32_t>(type);
    };
    function_menu_ = std::make_shared<lib::Menu>(true);
    function_menu_->items = {{
        comm_.prop ? comm_.prop->requiredMsgs().menuText(types::MenuID::IncSearch)
                   : String{"絞り込み検索"},
        "incremental-search", {}, 0, true}};
    function_menu_->on_command = [this](const StringView command) {
        if (command == "incremental-search" && function_inc_search_)
            function_inc_search_->startIncSearch();
    };
    function_->on_pointer = [this](lib::PointerEvent& event) {
        if (event.action != lib::PointerAction::Up ||
            event.button != lib::PointerButton::Secondary ||
            !function_menu_ || !comm_.services.gui) return;
        comm_.services.gui->show_context_menu(function_menu_, function_, event.position);
        event.handled = true;
    };

    declaration_ = std::make_shared<lib::TextEdit>();
    declaration_->read_only = true;
    description_ = std::make_shared<lib::TextEdit>();
    description_->read_only = true;
    description_->multiline = true;
    description_->wrap = true;
    description_->vertical_scroll = true;
    example_ = std::make_shared<lib::TextEdit>();
    example_->read_only = true;
    description_->minimum_visible_lines = 4;
    if (comm_.prop) {
        const auto configured = comm_.prop->adjustFont(comm_.prop->looks().textDlgFont(10));
        lib::FontSpec font;
        font.family = configured.name;
        font.points = static_cast<float>(configured.point);
        font.weight = configured.bold ? 700 : 400;
        font.italic = configured.italic;
        declaration_->set_font(font);
        example_->set_font(font);
    }
    args_ = std::make_shared<lib::TableView>();
    // D版と同様、両方の引数編集欄を quickStart=SingleClick と
    // する。 この表のフラグにより、その操作を共通のインライン編集の状態遷移に渡す。
    args_->edit_on_single_click = true;
    // D版と同様、省略可能な引数が省略されていても、提示した既定値を確定する。
    args_->commit_unchanged_inline_edits = true;
    const auto argument_edit_generation=std::make_shared<std::uint64_t>(0);
    args_->on_inline_editor_created =
        [this, life=std::weak_ptr<int>(insertion_lifetime_), settings=function_search_settings,
         argument_edit_generation](const std::size_t row, const std::size_t column,
             const std::shared_ptr<lib::TextEdit>& projection,
             const std::shared_ptr<lib::ComboBox>& combo) {
        const auto generation=++*argument_edit_generation;
        if (life.expired() || column!=2U || !combo || !combo->editable ||
            !summary_ || summary_->scenarioPath().empty()) return;
        const auto table=std::weak_ptr<lib::TableView>(args_);
        const auto order=args_->row_order_revision;
        const auto current=[this,life,table,order,row,argument_edit_generation,generation] {
            if (life.expired() || *argument_edit_generation!=generation) return false;
            const auto active=table.lock();
            return active && active->row_order_revision==order && row<active->rows.size() &&
                shell_ && shell_->visible();
        };
        argument_inc_search_=incsearch::attachComboSearch(comm_.services,combo,projection,
            [this,life,row,current](const incsearch::IncSearch& search) {
                std::vector<lib::ChoiceItem> result;
                if(life.expired() || !current() || args_->rows[row].cells.size()<=2U) return result;
                for(const auto& value:args_->rows[row].cells[2].editor_choices)
                    if(search.match(value)) result.push_back({value,value});
                return result;
            },settings,current,0,[this,life](const incsearch::SearchType type) {
                if(!life.expired() && comm_.prop)
                    comm_.prop->var().incrementalSearchType=static_cast<std::int32_t>(type);
            },true);
    };

    args_menu_ = std::make_shared<lib::Menu>(true);
    args_menu_->on_open = [this] {
        std::vector<lib::MenuItem> items;
        for (const auto id : {types::MenuID::Undo, types::MenuID::Redo}) {
            lib::MenuItem item;
            const bool is_undo = id == types::MenuID::Undo;
            item.text = comm_.prop ? comm_.prop->buildMenu(id) : is_undo ? "元に戻す" : "やり直す";
            item.command = is_undo ? "undo" : "redo";
            item.enabled = is_undo ? canUndo() : canRedo();
            items.push_back(std::move(item));
        }
        (void)lib::replace_menu_items(args_menu_, std::move(items));
    };
    args_menu_->on_command = [this](const StringView command) {
        if (command == "undo") undo();
        else if (command == "redo") redo();
    };
    args_->on_pointer = [this](lib::PointerEvent& event) {
        if (event.button != lib::PointerButton::Secondary || event.action != lib::PointerAction::Up ||
            !comm_.services.gui) return;
        args_menu_->on_open();
        comm_.services.gui->show_context_menu(args_menu_, args_, event.position);
        event.handled = true;
    };
    args_->on_key_down = [this](lib::KeyEvent& event) {
        if (event.handled) return;
        const auto state = static_cast<std::uint32_t>(event.modifiers);
        const auto matches = [&](const types::MenuID id, const lib::Key key) {
            if (comm_.prop) {
                const auto accelerator = dmenu::convertAccelerator(comm_.prop->buildMenu(id));
                if (accelerator) return dmenu::eqAcc(accelerator, static_cast<int>(event.key), 0U, state);
            }
            return state == static_cast<std::uint32_t>(lib::Modifiers::Control) && event.key == key;
        };
        if (matches(types::MenuID::Undo, lib::Key::Z) && canUndo()) { undo(); event.handled = true; }
        else if (matches(types::MenuID::Redo, lib::Key::Y) && canRedo()) { redo(); event.handled = true; }
    };
    const auto saved_column_width = [this](const std::int32_t value,
                                            const float fallback) {
        return comm_.prop ? std::max(20.0F, static_cast<float>(value)) : fallback;
    };
    const auto name_width = comm_.prop
        ? saved_column_width(comm_.prop->var().functionArgumentNameColumn.value, 100.0F)
        : 100.0F;
    const auto type_width = comm_.prop
        ? saved_column_width(comm_.prop->var().functionArgumentTypeColumn.value, 80.0F)
        : 80.0F;
    const auto value_width = comm_.prop
        ? saved_column_width(comm_.prop->var().functionArgumentValueColumn.value, 120.0F)
        : 120.0F;
    args_->columns = {
        lib::TableColumn{messages ? messages->functionArgumentName.value : "引数", name_width},
        lib::TableColumn{messages ? messages->functionArgumentType.value : "タイプ", type_width},
        lib::TableColumn{messages ? messages->functionArgumentValue.value : "値", value_width}};
    args_->on_column_resized = [this](const std::size_t column, const float width) {
        if (!comm_.prop || !std::isfinite(width) || width < 20.0F) return;
        const auto stored = static_cast<std::int32_t>(std::lround(width));
        switch (column) {
        case 0U: comm_.prop->var().functionArgumentNameColumn.value = stored; break;
        case 1U: comm_.prop->var().functionArgumentTypeColumn.value = stored; break;
        case 2U: comm_.prop->var().functionArgumentValueColumn.value = stored; break;
        default: break;
        }
    };
    args_->on_selection_changed =
        [this](const std::vector<std::size_t>& selection) {
            if (!selection.empty()) selected_row_ = selection.front();
        };
    args_->on_cell_edited = [this](const std::size_t row, const std::size_t column,
                                   String value) {
        if (row >= args_->rows.size() || args_->rows[row].cells.size() <= 2U) return;
        if (column == 1U) {
            const auto candidates = argTypes(row);
            const auto found = std::find_if(candidates.begin(), candidates.end(),
                [&](const ArgType type) { return argTypeName(type) == value; });
            if (found == candidates.end()) {
                auto& cell = args_->rows[row].cells[1];
                cell.text = cell.value;
                ++args_->row_update_revision;
                return;
            }
            (void)typeEditEndImpl(row,
                static_cast<std::size_t>(found - candidates.begin()), false);
            return;
        }
        if (column != 2U || row >= selected_arg_types_.size()) return;
        auto selected = selected_arg_types_[row];
        if (selected == ArgType::NoArgument) {
            const auto candidates = argTypes(row);
            const auto found = std::find_if(candidates.begin(), candidates.end(),
                [](const ArgType type) { return type != ArgType::NoArgument; });
            if (found == candidates.end()) return;
            selected = *found;
        }
        const auto valid = validArg(selected, value);
        if (valid.second.empty() && (!value.empty() || selected == ArgType::Boolean || selected == ArgType::Number)) {
            auto& cell = args_->rows[row].cells[2];
            cell.text = cell.value;
            configureValueCell(cell, selected_arg_types_[row], row);
            ++args_->row_update_revision;
            return;
        }
        if (selected_arg_types_[row] != ArgType::NoArgument &&
            valid.second == args_->rows[row].cells[2].value) {
            // 具体的な引数の値を変えずに確定しても、新しい編集とはしない。
            auto& cell = args_->rows[row].cells[2];
            cell.text = cell.value;
            configureValueCell(cell, selected_arg_types_[row], row);
            return;
        }
        // 値の検証が成功してから、省略可能な型を採用する。 キャン
        // セルや入力の拒否では、省略された引数や履歴を変更しない。
        if (selected_arg_types_[row] == ArgType::NoArgument) {
            const auto candidates = argTypes(row);
            const auto found = std::find(candidates.begin(), candidates.end(), selected);
            if (found == candidates.end()) return;
            (void)typeEditEndImpl(row,
                static_cast<std::size_t>(found - candidates.begin()), true, false);
        }
        (void)lib::publish_table_cell_text_value(*args_, row, 2U,
                                                 valid.second, valid.second);
        store();
    };
    insert_button_ = std::make_shared<lib::Button>(messages
        ? messages->insertFunctionIntoExpressionButton.value : "式へ挿入(&I)");
    insert_button_->set_enabled(false);
    insert_button_->on_click = [this, life=std::weak_ptr<int>(insertion_lifetime_)] {
        if (life.expired() || selected_func_name_.empty() || !insert_) return;
        const auto insert=insert_; // 挿入によって、この補助画面やその所有者が無効になる場合がある。
        const auto name=selected_func_name_;
        std::vector<String> values;
        for (std::size_t index = 0; index < selected_arg_types_.size(); ++index) {
            const auto type = selected_arg_types_[index];
            if (type == ArgType::NoArgument) continue;
            const String value =
                index < args_->rows.size() && args_->rows[index].cells.size() > 2U
                    ? args_->rows[index].cells[2].value : String{};
            switch (type) {
            case ArgType::String:
            case ArgType::Flag:
            case ArgType::Step:
            case ArgType::Variant:
                values.push_back(quoteText(value));
                break;
            case ArgType::VariantRef:
                values.push_back(quoteText(value, "@"));
                break;
            case ArgType::Number:
            case ArgType::Boolean:
            case ArgType::List:
            case ArgType::Expression:
            case ArgType::Status:
            case ArgType::CardInfo:
            case ArgType::Any:
            case ArgType::NumberOrString:
            case ArgType::NumberOrList:
            case ArgType::NoArgument:
                values.push_back(value);
                break;
            }
        }
        insert(name + "(" + [&values] {
            String result;
            for (std::size_t index = 0; index < values.size(); ++index) {
                if (index != 0U) result += ", ";
                result += values[index];
            }
            return result;
        }() + ")");
    };

    add_label(messages ? messages->functionCategory.value : "分類", 0U);
    place(category_, 0U, 1U, 1U, 1.0F, {200.0F, 0.0F});
    shell_->add(category_);
    place(reference_, 0U, 2U);
    shell_->add(reference_);
    add_label(messages ? messages->functionSearch.value : "関数検索", 1U);
    place(function_search_, 1U, 1U, 2U, 1.0F);
    shell_->add(function_search_);
    add_label(messages ? messages->selectFunction.value : "関数", 2U);
    place(function_, 2U, 1U, 2U, 1.0F);
    shell_->add(function_);
    add_label(messages ? messages->functionDeclaration.value : "書式", 3U);
    place(declaration_, 3U, 1U, 2U, 1.0F);
    shell_->add(declaration_);
    add_label(messages ? messages->functionDescription.value : "解説", 4U);
    place(description_, 4U, 1U, 2U, 1.0F, {0.0F, 96.0F});
    shell_->add(description_);
    add_label(messages ? messages->functionExample.value : "例", 5U);
    place(example_, 5U, 1U, 2U, 1.0F);
    shell_->add(example_);
    place(args_, 6U, 0U, 3U, 1.0F, {0.0F, 100.0F});
    auto args_item = args_->layout_item();
    args_item.vertical = lib::Align::Stretch;
    args_->set_layout_item(args_item);
    shell_->add(args_);
    auto footer = std::make_shared<lib::Panel>();
    auto footer_layout =
        std::make_shared<lib::LinearLayout>(lib::Orientation::Horizontal);
    footer_layout->cross_axis = lib::Align::Center;
    footer->set_layout(footer_layout);
    place(footer, 7U, 0U, 3U, 1.0F);
    auto footer_spacer = std::make_shared<lib::Spacer>();
    footer_spacer->set_layout_item({0, 0, 1, 1, lib::Align::Stretch,
                                    lib::Align::Center, {}, {}, {}, 1.0F});
    footer->add(footer_spacer);
    footer->add(insert_button_);
    shell_->add(footer);

    // D版と同様、関数の説明と引数編集欄を上下にサイズ変更できる SplitPane
    // に配置する。 準備済みの共通コントロールを、それぞれ対応する二つのペイン
    // へ付け替える。 どのOSでも SplitView が分割比率を管理する。
    const auto form_children = shell_->children();
    shell_->clear();
    auto shell_layout = std::make_shared<lib::GridLayout>();
    shell_layout->columns = 1U;
    shell_layout->padding = {};
    shell_->set_layout(shell_layout);
    function_args_split_ = std::make_shared<lib::SplitView>();
    function_args_split_->orientation = lib::Orientation::Vertical;
    // 解説の文字拡大で必要になった高さを、保存済みの分割比率で押し潰さない。
    function_args_split_->constrain_to_child_minimums = true;
    function_args_split_->weights = {
        comm_.prop ? static_cast<float>(std::max(1,
            comm_.prop->var().expressionFunctionAndArgsSashT.value)) : 3.0F,
        comm_.prop ? static_cast<float>(std::max(1,
            comm_.prop->var().expressionFunctionAndArgsSashB.value)) : 2.0F};
    function_args_split_->on_weights_changed = [this](const std::vector<float>& weights) {
        if (!comm_.prop || weights.size() < 2U) return;
        comm_.prop->var().expressionFunctionAndArgsSashT.value =
            std::max(1, static_cast<std::int32_t>(std::lround(weights[0])));
        comm_.prop->var().expressionFunctionAndArgsSashB.value =
            std::max(1, static_cast<std::int32_t>(std::lround(weights[1])));
    };
    function_args_split_->set_layout_item({.horizontal=lib::Align::Stretch,
        .vertical=lib::Align::Stretch,.grow=1.0F});
    auto top = std::make_shared<lib::Panel>();
    top->set_layout(form_layout);
    auto bottom = std::make_shared<lib::Panel>();
    auto bottom_layout = std::make_shared<lib::GridLayout>();
    bottom_layout->columns = 1U;
    bottom_layout->spacing = 5.0F;
    bottom_layout->padding = {6.0F, 0.0F, 6.0F, 6.0F};
    bottom->set_layout(bottom_layout);
    for (std::size_t index = 0; index < form_children.size(); ++index) {
        const auto& child = form_children[index];
        if (index + 2U < form_children.size()) {
            top->add(child);
        } else {
            bottom->add(child);
        }
    }
    args_->set_layout_item({.row=0U,.column=0U,.horizontal=lib::Align::Stretch,
        .vertical=lib::Align::Stretch,.minimum={0.0F,100.0F},.grow=1.0F});
    footer->set_layout_item({.row=1U,.column=0U,.horizontal=lib::Align::Stretch});
    function_args_split_->add(top);
    function_args_split_->add(bottom);
    shell_->add(function_args_split_);
    shell_->on_deactivated = [this] { saveWindowState(); };
    // D版では共通設定パネルの子コントロールではなく、モードレスの
    // ツールウィンドウとして扱う。 Dialog をパネルに追加す
    // ると、最上位ウィンドウと埋め込み内容の両方として 二重に配
    // 置され、挿入ダイアログの表示が崩れて一部が反応しなくなる。

    const auto prop = comm_.prop ? &comm_.prop->parent() : nullptr;
    all_func_defs_ = expression::functionDefinitions(prop);
    updateFunctions();
    if (!func_defs_.empty()) {
        function_->selected_index = 0U;
        functionSelected();
    }
    if (history_.empty()) store();
    refUndoMax();
    if (comm_.subscribeUndoMax) {
        undo_max_unsubscribe_ = comm_.subscribeUndoMax(
            [this, life = std::weak_ptr<int>{insertion_lifetime_}] {
                if (!life.expired()) refUndoMax();
            });
    }
}

FunctionCallEditor::~FunctionCallEditor() {
    if (reference_) reference_->on_click = {};
    insertion_lifetime_.reset();
    if (undo_max_unsubscribe_) std::exchange(undo_max_unsubscribe_, {})();
    saveWindowState();
    if (args_) { args_->on_pointer = {}; args_->on_key_down = {}; args_->on_inline_editor_created = {}; }
    if (args_menu_) { args_menu_->on_open = {}; args_menu_->on_command = {}; }
    // ウィンドウはホストが保持するモードレス画面・モデルである。 このエディタの状
    // 態を破棄する前にコールバックを切り離し、 遅れて発生した終了や一覧の有効化で
    // 、破棄済みの FunctionCallEditor を呼ばないようにする。
    if (reference_dialog_) reference_dialog_->on_close_requested = {};
    if (reference_info_dialog_) reference_info_dialog_->on_close_requested = {};
    if (reference_error_dialog_) reference_error_dialog_->on_close_requested = {};
    if (reference_list_) reference_list_->on_selection_changed = {};
    if (reference_copy_all_) reference_copy_all_->on_click = {};
    if (reference_close_) reference_close_->on_click = {};
    if (reference_info_close_) reference_info_close_->on_click = {};
    if (reference_error_close_) reference_error_close_->on_click = {};
    if (shell_) shell_->on_close_requested = {};
    if (shell_) shell_->on_deactivated = {};
    if (function_) {
        function_->on_text_input = {};
        function_->on_key_down = {};
        function_->on_blur = {};
        function_->on_pointer = {};
        function_->on_selection_changed = {};
    }
    if (function_menu_) function_menu_->on_command = {};
    if (function_inc_search_) function_inc_search_->close();
}

void FunctionCallEditor::saveWindowState() noexcept {
    if (!comm_.prop || !shell_) return;
    const auto bounds = lib::authored_window_bounds(*shell_);
    if (std::isfinite(bounds.width) && bounds.width > 0.0F)
        comm_.prop->var().functionCallEditorWidth.value =
            std::max(320, static_cast<std::int32_t>(std::lround(bounds.width)));
    if (std::isfinite(bounds.height) && bounds.height > 0.0F)
        comm_.prop->var().functionCallEditorHeight.value =
            std::max(432, static_cast<std::int32_t>(std::lround(bounds.height)));
}

String FunctionCallEditor::functionReferenceDirectory() const {
    if (selected_func_name_.empty() || comm_.prop == nullptr) return {};
    return comm_.prop->toAppAbs(
        "resource/common_functions/" + selected_func_name_);
}

std::vector<String> FunctionCallEditor::functionReferenceFiles() const {
    const String directory = functionReferenceDirectory();
    std::vector<String> result;
    if (directory.empty()) return result;
    std::error_code error;
    const auto path = structs::pathFromUtf8(directory);
    if (!std::filesystem::is_directory(path, error)) return result;
    for (const auto& entry : std::filesystem::directory_iterator(path, error)) {
        if (error) break;
        if (!entry.is_regular_file(error)) continue;
        if (lowerCopy(entry.path().extension().string()) != ".txt") continue;
        result.push_back(structs::pathToUtf8(entry.path().filename()));
    }
    std::sort(result.begin(), result.end(), [](const String& first, const String& second) {
        // D版の baseName().icmp() は、一対多の変換も含む完全なUnicodeのケースフォー
        // ルディングを使う。 Unicodeの小文字化だけでは不足するため、検証済みの比較処理を再利用する。
        return scene_flags::compare(first, second, false) < 0;
    });
    return result;
}

void FunctionCallEditor::store() {
    if (history_pos_ + 1U < history_.size()) {
        history_.erase(history_.begin() + static_cast<std::ptrdiff_t>(history_pos_ + 1U),
                       history_.end());
    }
    history_.emplace_back(this);
    history_pos_ = history_.empty() ? 0U : history_.size() - 1U;
    refUndoMax();
}

void FunctionCallEditor::refUndoMax() {
    // D版と同様、この補助画面には AreaView の Commons が持つイベ
    // ントグラフ用の上限ではなく、 その他のダイアログ用の取り消し回数の上限を使う。
    const auto configured = comm_.prop
        ? comm_.prop->var().undoMaxEtc.value
        : static_cast<std::int32_t>(comm_.undoMaxEvent);
    const std::size_t maximum = configured > 0
        ? static_cast<std::size_t>(configured) + 1U : 1U;
    while (history_.size() > maximum) {
        history_.erase(history_.begin());
        if (history_pos_ > 0U) --history_pos_;
    }
}

void FunctionCallEditor::undo() {
    if (!canUndo()) return;
    --history_pos_;
    history_[history_pos_].undo();
}

void FunctionCallEditor::redo() {
    if (!canRedo()) return;
    ++history_pos_;
    history_[history_pos_].redo();
}

void FunctionCallEditor::categorySelected() {
    updateFunctions();
}

void FunctionCallEditor::functionSearchModified() {
    updateFunctions();
}

void FunctionCallEditor::updateReferenceButton() {
    if (reference_) reference_->set_enabled(!functionReferenceFiles().empty());
}

void FunctionCallEditor::openFunctionReference() {
    // D版と同様、毎回新しいモーダルウィンドウを開く。表示側が以前の
    // モデルを保持している場合があるため、 所有対象を置き換える前に
    // そのコールバックを無効化し、このエディタを指すものを残さない。
    if (reference_dialog_) {
        reference_dialog_->on_close_requested = {};
        reference_dialog_->set_visible(false);
    }
    if (reference_list_) reference_list_->on_selection_changed = {};
    if (reference_copy_all_) reference_copy_all_->on_click = {};
    if (reference_close_) reference_close_->on_click = {};
    if (reference_info_dialog_) {
        reference_info_dialog_->on_close_requested = {};
        reference_info_dialog_->set_visible(false);
    }
    if (reference_info_close_) reference_info_close_->on_click = {};
    if (reference_error_dialog_) {
        reference_error_dialog_->on_close_requested = {};
        reference_error_dialog_->set_visible(false);
    }
    if (reference_error_close_) reference_error_close_->on_click = {};
    const auto directory = functionReferenceDirectory();
    const auto files = functionReferenceFiles();
    if (files.empty()) {
        reference_info_dialog_ = std::make_shared<lib::Dialog>();
        reference_info_dialog_->title = comm_.prop
            ? comm_.prop->requiredMsgs().dlgTitInfo.value : "情報";
        reference_info_dialog_->modal = true;
        reference_info_dialog_->owner = shell_;
        reference_info_dialog_->center_on_owner = true;
        reference_info_dialog_->set_bounds({0.0F, 0.0F, 400.0F, 120.0F});
        reference_info_dialog_->set_layout(
            std::make_shared<lib::LinearLayout>(lib::Orientation::Vertical));
        auto message = std::make_shared<lib::TextEdit>();
        message->text = "参照テキストが見つかりません。\n" + directory;
        message->read_only = true;
        message->multiline = true;
        auto close = std::make_shared<lib::Button>(comm_.prop
            ? comm_.prop->requiredMsgs().dlgTextClose.value : "閉じる(&C)");
        const std::weak_ptr<lib::Dialog> weak = reference_info_dialog_;
        close->on_click = [weak, gui = comm_.services.gui] {
            if (const auto dialog = weak.lock()) {
                dialog->result = lib::DialogResult::Cancel;
                // Web ALL の非ブロッキングのモーダル表示も終了する。
                if (gui) gui->end_modal(dialog);
                dialog->set_visible(false);
            }
        };
        reference_info_dialog_->on_close_requested = close->on_click;
        reference_info_dialog_->add(message);
        reference_info_dialog_->add(close);
        reference_info_close_ = std::move(close);
        if (comm_.services.gui) {
            // 対応する環境では非ブロッキングのモーダル表示を使う。 ブラウザー上の Web
            // ALL エディタでは、入れ子のモーダルループが停止の原因になる。
            if (!comm_.services.gui->begin_modal(reference_info_dialog_, shell_))
                reference_info_dialog_->result = comm_.services.gui->show_modal(
                    reference_info_dialog_, shell_);
        }
        return;
    }

    reference_files_ = files;
    reference_dialog_ = std::make_shared<lib::Dialog>();
    reference_dialog_->title = (comm_.prop
        ? comm_.prop->requiredMsgs().openReferenceLabel.value : "参考を開く") +
        " - " + selected_func_name_;
    reference_dialog_->modal = true;
    reference_dialog_->owner = shell_;
    reference_dialog_->center_on_owner = true;
    reference_dialog_->set_bounds({0.0F, 0.0F, 720.0F, 520.0F});
    reference_dialog_->set_layout(
        std::make_shared<lib::LinearLayout>(lib::Orientation::Vertical));
    auto body = std::make_shared<lib::Panel>();
    body->set_layout(std::make_shared<lib::LinearLayout>(lib::Orientation::Horizontal));
    body->set_layout_item({0, 0, 1, 1, lib::Align::Stretch,
                           lib::Align::Stretch, {}, {}, {}, 1.0F});
    reference_list_ = std::make_shared<lib::ListView>();
    reference_list_->multi_select = false;
    reference_list_->set_layout_item({0, 0, 1, 1, lib::Align::Start,
                                      lib::Align::Stretch, {240.0F, 0.0F}});
    for (const auto& file : reference_files_) {
        // functionReferenceFiles() はUTF-8の末尾ファ
        // イル名を返す。 ネイティブのパス変換をもう一度通すと、Windowsの標
        // 準ライブラリ実装によっては 非ASCIIのラベルが壊れる。D版の baseName()
        // と同様、拡張子を含むUTF-8のファイル名を表示する。
        reference_list_->items.push_back({file, file});
    }
    reference_list_->on_selection_changed = [this](const std::vector<std::size_t>& selection) {
        if (!selection.empty()) selectReferenceFile(selection.front());
    };
    reference_viewer_ = std::make_shared<lib::TextEdit>();
    reference_viewer_->read_only = true;
    reference_viewer_->multiline = true;
    reference_viewer_->wrap = true;
    reference_viewer_->horizontal_scroll = true;
    reference_viewer_->set_layout_item({0, 1, 1, 1, lib::Align::Stretch,
                                        lib::Align::Stretch, {}, {}, {}, 1.0F});
    body->add(reference_list_);
    body->add(reference_viewer_);
    dmenu::Commons textCommons;
    textCommons.services = comm_.services;
    if (auto* prop = comm_.prop) {
        // createTextMenu はタブ幅も設定するため、既定値
        // で上書きされないよう、 表示設定を通じて設定済みの値を渡す。
        textCommons.presentation.textTabs = static_cast<std::size_t>(
            std::max(1, prop->var().textTabs.value));
        textCommons.presentation.menuText = [prop](auto id) { return prop->buildMenu(id); };
        textCommons.presentation.menuImage = [prop](auto id) { return prop->images().menu(id); };
    }
    reference_text_menu_ = dmenu::createTextMenu(textCommons, dmenu::TMM(reference_viewer_));
    auto buttons = std::make_shared<lib::Panel>();
    auto buttons_layout =
        std::make_shared<lib::LinearLayout>(lib::Orientation::Horizontal);
    buttons_layout->spacing = 6.0F;
    buttons_layout->cross_axis = lib::Align::Center;
    buttons->set_layout(buttons_layout);
    auto left_spacer = std::make_shared<lib::Spacer>();
    left_spacer->set_layout_item({0, 0, 1, 1, lib::Align::Stretch,
                                  lib::Align::Center, {}, {}, {}, 1.0F});
    reference_copy_all_ = std::make_shared<lib::Button>(comm_.prop
        ? comm_.prop->requiredMsgs().referenceCopyAllLabel.value : "全体コピー");
    reference_copy_all_->on_click = [this] {
        if (!reference_viewer_ || !comm_.services.clipboard) return;
        lib::ClipboardData data;
        data.text = reference_viewer_->text;
        comm_.services.clipboard->write(data);
    };
    reference_close_ = std::make_shared<lib::Button>(comm_.prop
        ? comm_.prop->requiredMsgs().dlgTextClose.value : "閉じる(&C)");
    reference_close_->default_button = true;
    reference_close_->cancel_button = true;
    reference_close_->on_click = [this] { closeReference(); };
    reference_dialog_->on_close_requested = [this] { closeReference(); };
    auto right_spacer = std::make_shared<lib::Spacer>();
    right_spacer->set_layout_item({0, 3, 1, 1, lib::Align::Stretch,
                                   lib::Align::Center, {}, {}, {}, 1.0F});
    buttons->add(left_spacer);
    buttons->add(reference_copy_all_);
    buttons->add(reference_close_);
    buttons->add(right_spacer);
    reference_dialog_->add(body);
    reference_dialog_->add(buttons);
    reference_list_->selection = {0U};
    reference_list_->selection_anchor = 0U;
    reference_list_->selection_cursor = 0U;
    selectReferenceFile(0U);
    if (comm_.services.gui) {
        // 対応する環境では非ブロッキングのモーダル表示を使う。 show_modal() の
        // 入れ子のモーダルループは、ブラウザー上の Web ALL エディタを停止させる。
        if (!comm_.services.gui->begin_modal(reference_dialog_, shell_))
            reference_dialog_->result = comm_.services.gui->show_modal(reference_dialog_, shell_);
    }
}

void FunctionCallEditor::selectReferenceFile(const std::size_t index) {
    if (!reference_viewer_ || index >= reference_files_.size()) return;
    const auto display_path = functionReferenceDirectory() + "/" + reference_files_[index];
    std::ifstream input;
    String validated_content;
    bool loaded = false;
    try {
        const auto path = structs::pathFromUtf8(functionReferenceDirectory()) /
                          structs::pathFromUtf8(reference_files_[index]);
        input.open(path, std::ios::binary);
        if (input.is_open()) {
            char buffer[4096];
            while (input.read(buffer, sizeof(buffer)) || input.gcount() > 0) {
                validated_content.append(buffer, static_cast<std::size_t>(input.gcount()));
            }
            if (input.bad() || !input.eof()) {
                throw std::runtime_error("Failed to read reference text");
            }
            // D版の readText と同様、viewer.setText の前にUTF-8
            // を検証する。 読み込みと検証がすべて完了してから表示に反映し、不正なテキス
            // トや 選択・オープン・読み込みの競合があっても、以前のページを維持する。
            ::cwxeditor::cwx::enumutils::validateUtf8(validated_content);
            loaded = true;
        }
    } catch (const std::exception&) {
        // 下にある既存の、所有者付き読み込みエラー表示を共用する。
    }
    if (!loaded) {
        // D版と同様、表示済みの文書を維持し、選択とオープンの競
        // 合を 所有者付きのエラーメッセージボックスで通知する。
        if (reference_error_dialog_) {
            reference_error_dialog_->on_close_requested = {};
            reference_error_dialog_->set_visible(false);
        }
        if (reference_error_close_) reference_error_close_->on_click = {};
        reference_error_dialog_ = std::make_shared<lib::Dialog>();
        reference_error_dialog_->title = comm_.prop
            ? comm_.prop->requiredMsgs().dlgTitError.value : "エラー";
        reference_error_dialog_->modal = true;
        reference_error_dialog_->owner = reference_dialog_;
        reference_error_dialog_->center_on_owner = true;
        reference_error_dialog_->set_bounds({0.0F, 0.0F, 440.0F, 130.0F});
        reference_error_dialog_->set_layout(
            std::make_shared<lib::LinearLayout>(lib::Orientation::Vertical));
        auto message = std::make_shared<lib::TextEdit>();
        message->text = "参照テキストの読み込みに失敗しました。\n" + display_path;
        message->read_only = true;
        message->multiline = true;
        auto close = std::make_shared<lib::Button>(comm_.prop
            ? comm_.prop->requiredMsgs().dlgTextClose.value : "閉じる(&C)");
        close->default_button = true;
        close->cancel_button = true;
        const std::weak_ptr<lib::Dialog> weak = reference_error_dialog_;
        close->on_click = [weak, gui = comm_.services.gui] {
            if (const auto dialog = weak.lock()) {
                dialog->result = lib::DialogResult::Cancel;
                // Web ALL の非ブロッキングのモーダル表示も終了する。
                if (gui) gui->end_modal(dialog);
                dialog->set_visible(false);
            }
        };
        reference_error_dialog_->on_close_requested = close->on_click;
        reference_error_dialog_->add(message);
        reference_error_dialog_->add(close);
        reference_error_close_ = std::move(close);
        if (comm_.services.gui) {
            if (!comm_.services.gui->begin_modal(reference_error_dialog_, reference_dialog_))
                reference_error_dialog_->result = comm_.services.gui->show_modal(
                    reference_error_dialog_, reference_dialog_);
        }
        return;
    }
    reference_viewer_->text = std::move(validated_content);
}

void FunctionCallEditor::closeReference() noexcept {
    if (!reference_dialog_) return;
    reference_dialog_->result = lib::DialogResult::Cancel;
    if (comm_.services.gui) {
        comm_.services.gui->end_modal(reference_dialog_);
        comm_.services.gui->close(reference_dialog_);
    } else {
        reference_dialog_->set_visible(false);
    }
}

void FunctionCallEditor::functionSelected() {
    if (!function_ || !function_->selected_index ||
        *function_->selected_index >= func_defs_.size()) {
        selected_func_name_.clear();
        arg_defs_.clear();
        arg_types_.clear();
        selected_arg_types_.clear();
        if (args_)
            (void)lib::replace_table_rows(args_, {}, {}, std::nullopt);
        if (declaration_) declaration_->text.clear();
        if (description_) description_->text.clear();
        if (example_) example_->text.clear();
        if (insert_button_) insert_button_->set_enabled(false);
        updateReferenceButton();
        return;
    }

    const auto& definition = func_defs_[*function_->selected_index];
    selected_func_name_ = definition.name;
    if (insert_button_) insert_button_->set_enabled(true);
    arg_defs_ = definition.args;
    arg_types_.clear();
    selected_arg_types_.clear();
    std::vector<lib::TableRow> rows;

    std::vector<String> declarationParts;
    for (std::size_t index = 0; index < arg_defs_.size(); ++index) {
        const auto& argument = arg_defs_[index];
        arg_types_.push_back(candidateArgTypes(argument));
        auto choices = arg_types_.back();
        if (choices.empty()) choices.push_back(ArgType::NoArgument);
        // 表示する型、編集欄、出力する引数を一致させる。 D版の初
        // 期型 Any は編集可能な選択肢ではなく、引用符で囲ま
        // ない式を表す。 省略可能な引数は、省略状態から始める。
        const auto initial_type = argument.initValueType == ArgType::Any
            ? ArgType::Expression : argument.initValueType;
        const auto selected = argument.optional ? ArgType::NoArgument
            : std::find(choices.begin(), choices.end(), initial_type) != choices.end()
                ? initial_type : choices.front();
        selected_arg_types_.push_back(selected);
        String name = argument.name;
        if (argument.varArg) {
            name = ::cwxeditor::cwx::utils::tryFormat(argument.name, 1);
        }
        if (argument.optional) name = "[" + name + "]";
        declarationParts.push_back(name + (argument.varArg ? " ..." : ""));
        lib::TableRow row;
        row.cells = {
            lib::ListItem{name, name, {}, false, true},
            lib::ListItem{argTypeName(selected), argTypeName(selected), {}, false, true},
            lib::ListItem{argument.optional ? String{} : argument.initValue,
                          argument.optional ? String{} : argument.initValue, {}, false, true}};
        row.cells[0].enabled = false;
        row.cells[1].editor = lib::CellEditorKind::Choice;
        for (const auto type : choices)
            row.cells[1].editor_choices.push_back(argTypeName(type));
        configureValueCell(row.cells[2], selected, index);
        rows.push_back(std::move(row));
        if (argument.varArg) {
            selected_arg_types_.push_back(ArgType::NoArgument);
            lib::TableRow extra;
            const auto extraName = ::cwxeditor::cwx::utils::tryFormat(argument.name, 2);
            extra.cells = {
                lib::ListItem{extraName, extraName, {}, false, true},
                lib::ListItem{argTypeName(ArgType::NoArgument),
                              argTypeName(ArgType::NoArgument), {}, false, true},
                lib::ListItem{"", "", {}, false, true}};
            extra.cells[0].enabled = false;
            extra.cells[1].editor = lib::CellEditorKind::Choice;
            for (const auto type : argTypes(selected_arg_types_.size() - 1U))
                extra.cells[1].editor_choices.push_back(argTypeName(type));
            configureValueCell(extra.cells[2], ArgType::NoArgument, selected_arg_types_.size() - 1U);
            rows.push_back(std::move(extra));
        }
    }
    (void)lib::replace_table_rows(args_, std::move(rows), {}, std::nullopt);
    declaration_->text = definition.name + "(";
    for (std::size_t index = 0; index < declarationParts.size(); ++index) {
        if (index != 0U) declaration_->text += ", ";
        declaration_->text += declarationParts[index];
    }
    declaration_->text += ")";
    if (comm_.prop) {
        declaration_->text = ::cwxeditor::cwx::utils::tryFormat(
            comm_.prop->requiredMsgs().functionAs.value, declaration_->text,
            argTypeName(definition.returnType));
    }
    description_->text = definition.desc;
    example_->text = definition.example;
    selected_row_ = 0U;
    updateReferenceButton();
    history_.clear();
    history_pos_ = 0U;
    store();
}

String FunctionCallEditor::argTypeName(const ArgType argType) const {
    if (comm_.prop) {
        const auto& messages = comm_.prop->requiredMsgs();
        switch (argType) {
        case ArgType::Number: return messages.argNumber.value;
        case ArgType::String: return messages.argString.value;
        case ArgType::Flag: return messages.argFlag.value;
        case ArgType::Step: return messages.argStep.value;
        case ArgType::Variant: return messages.argVariant.value;
        case ArgType::Boolean: return messages.argBoolean.value;
        case ArgType::List: return messages.argList.value;
        case ArgType::Expression: return messages.argExpression.value;
        case ArgType::Status: return messages.argStatus.value;
        case ArgType::CardInfo: return messages.argCardInfo.value;
        case ArgType::Any: return messages.argAny.value;
        case ArgType::NumberOrString: return messages.argNumberOrString.value;
        case ArgType::NumberOrList: return messages.argNumberOrList.value;
        case ArgType::VariantRef: return messages.variantReference.value;
        case ArgType::NoArgument: return messages.noArgument.value;
        }
    }
    return argTypeNameText(argType);
}

void FunctionCallEditor::updateFunctions() {
    if (!function_) return;
    const auto selectedCategory = category_ && category_->selected_index
                                      ? *category_->selected_index : 0U;
    const String search = lowerCopy(trimCopy(function_search_ ? function_search_->text : String{}));
    func_defs_.clear();
    std::vector<lib::ChoiceItem> items;
    for (const auto& definition : all_func_defs_) {
        if (selectedCategory != 0U) {
            const auto categoryIndex = selectedCategory - 1U;
            if (categoryIndex >= categories_.size()) continue;
            if (std::find(definition.category.begin(), definition.category.end(),
                          categories_[categoryIndex]) == definition.category.end()) {
                continue;
            }
        }
        if (function_inc_search_ && !function_inc_search_->match(definition.name))
            continue;
        if (!search.empty() &&
            lowerCopy(definition.name).find(search) == String::npos) continue;
        const auto label = comm_.prop
            ? ::cwxeditor::cwx::utils::tryFormat(
                  comm_.prop->requiredMsgs().functionNameWithDescription.value,
                  definition.name, definition.shortDesc)
            : definition.name;
        items.push_back(lib::ChoiceItem{label, definition.name, {}, true});
        func_defs_.push_back(definition);
    }

    std::optional<std::size_t> selected;
    if (!selected_func_name_.empty()) {
        for (std::size_t index = 0; index < func_defs_.size(); ++index) {
            if (func_defs_[index].name == selected_func_name_) {
                selected = index;
                break;
            }
        }
    }
    const bool keep_arguments = selected.has_value();
    if (!selected && !items.empty()) selected = 0U;
    (void)lib::replace_combo_items(function_, std::move(items), selected);
    if (!keep_arguments) functionSelected();
}

std::vector<ArgType> FunctionCallEditor::argTypes(const std::size_t index) const {
    if (index < arg_types_.size()) return arg_types_[index];
    if (arg_defs_.empty()) return {ArgType::NoArgument};
    auto result = candidateArgTypes(arg_defs_.back());
    if (std::find(result.begin(), result.end(), ArgType::NoArgument) == result.end())
        result.insert(result.begin(), ArgType::NoArgument);
    return result;
}

bool FunctionCallEditor::typeEditEndImpl(const std::size_t row,
                                         const std::size_t typeIndex,
                                         const bool force,
                                         const bool recordHistory) {
    if (row >= selected_arg_types_.size()) return false;
    const auto typesForArg = argTypes(row);
    if (typeIndex >= typesForArg.size()) return false;
    const auto old = selected_arg_types_[row];
    const auto selected = typesForArg[typeIndex];
    if (!force && old == selected) return true;
    auto rows = args_->rows;
    selected_arg_types_[row] = selected;
    if (row >= rows.size()) return false;
    const auto type_name = argTypeName(selected);
    rows[row].cells[1].text = type_name;
    rows[row].cells[1].value = type_name;
    const bool var_arg = !arg_defs_.empty() && arg_defs_.back().varArg &&
                         row >= arg_defs_.size();
    if (selected == ArgType::NoArgument) {
        if (var_arg && row > 0U && row + 1U < rows.size()) {
            rows.erase(rows.begin() + static_cast<std::ptrdiff_t>(row));
            selected_arg_types_.erase(selected_arg_types_.begin() +
                                      static_cast<std::ptrdiff_t>(row));
            for (std::size_t index = row; index < rows.size(); ++index) {
                const auto ordinal = (index - arg_defs_.size()) + 2U;
                rows[index].cells[0].text = rows[index].cells[0].value =
                    ::cwxeditor::cwx::utils::tryFormat(
                        arg_defs_.back().name, static_cast<int>(ordinal));
            }
        } else {
            rows[row].cells[2].text.clear();
            rows[row].cells[2].value.clear();
            configureValueCell(rows[row].cells[2], selected, row);
        }
    } else {
        const auto current = rows[row].cells[2].value;
        const auto valid = validArg(selected, current);
        // 省略された数値引数にも、定義ごとの既定値が必
        // 要。 文字列引数では空文字列も有効とする。
        if (valid.second.empty() &&
            (!current.empty() || selected == ArgType::Number)) {
            const auto definitionIndex = row < arg_defs_.size() ? row : arg_defs_.size() - 1U;
            const auto fallback = arg_defs_.empty() ? String{} : arg_defs_[definitionIndex].initValue;
            rows[row].cells[2].text = rows[row].cells[2].value = fallback;
        } else if (valid.first != selected || valid.second != current) {
            rows[row].cells[2].text = rows[row].cells[2].value = valid.second;
        }
        configureValueCell(rows[row].cells[2], selected, row);
        if (var_arg && old == ArgType::NoArgument) {
            const std::size_t index = rows.size();
            const auto ordinal = (index - arg_defs_.size()) + 2U;
            lib::TableRow sentinel;
            const auto name = ::cwxeditor::cwx::utils::tryFormat(
                arg_defs_.back().name, static_cast<int>(ordinal));
            sentinel.cells = {
                lib::ListItem{name, name, {}, false, false},
                lib::ListItem{argTypeName(ArgType::NoArgument),
                              argTypeName(ArgType::NoArgument), {}, false, true},
                lib::ListItem{"", "", {}, false, true}};
            sentinel.cells[1].editor = lib::CellEditorKind::Choice;
            for (const auto type : argTypes(index))
                sentinel.cells[1].editor_choices.push_back(argTypeName(type));
            configureValueCell(sentinel.cells[2], ArgType::NoArgument, index);
            rows.push_back(std::move(sentinel));
            selected_arg_types_.push_back(ArgType::NoArgument);
        }
    }
    const auto selected_row = std::min(row, rows.empty() ? 0U : rows.size() - 1U);
    const auto selection = rows.empty() ? std::vector<std::size_t>{}
                                        : std::vector<std::size_t>{selected_row};
    (void)lib::replace_table_rows(args_, std::move(rows), selection,
                                  selection.empty() ? std::optional<std::size_t>{}
                                                    : std::optional<std::size_t>{selected_row});
    if (recordHistory) store();
    return true;
}

void FunctionCallEditor::configureValueCell(lib::ListItem& cell,
                                             const ArgType type,
                                             const std::optional<std::size_t> argument) const {
    cell.editor_value.reset();
    cell.editor_choice_values.clear();
    cell.enabled = true;
    cell.editor = lib::CellEditorKind::Text;
    cell.editor_choices.clear();
    cell.editor_minimum = 0.0;
    cell.editor_maximum = 0.0;
    switch (type) {
    case ArgType::Number:
        cell.editor = lib::CellEditorKind::Number;
        cell.editor_minimum = -std::numeric_limits<double>::max();
        cell.editor_maximum = std::numeric_limits<double>::max();
        break;
    case ArgType::Boolean:
        cell.editor = lib::CellEditorKind::Choice;
        cell.editor_choices = {"TRUE", "FALSE"};
        break;
    case ArgType::Status:
        cell.editor = lib::CellEditorKind::Choice;
        cell.editor_choices = {"POISON", "SLEEP", "BIND", "PARALYZE", "CONFUSE",
            "OVERHEAT", "BRAVE", "PANIC", "SILENCE", "FACEUP", "ANTIMAGIC",
            "ENHACTION", "ENHAVOID", "ENHRESIST", "ENHDEFENSE"};
        break;
    case ArgType::Flag:
    case ArgType::Step:
    case ArgType::Variant:
    case ArgType::VariantRef:
        cell.editor = lib::CellEditorKind::EditableChoice;
        {
            const auto root = summary_ ? summary_->flagDirRoot() : flag::FlagDirPtr{};
            if (type == ArgType::Flag) {
                for (const auto& value : flag::allVars<flag::Flag>(root, counter_))
                    if (value) cell.editor_choices.push_back(value->path());
            } else if (type == ArgType::Step) {
                for (const auto& value : flag::allVars<flag::Step>(root, counter_))
                    if (value) cell.editor_choices.push_back(value->path());
            } else {
                for (const auto& value : flag::allVars<flag::Variant>(root, counter_))
                    if (value) cell.editor_choices.push_back(value->path());
            }
        }
        break;
    case ArgType::NoArgument: {
        // D版と同様、省略可能な行が省略状態の間も、最初の具体的な型を提示する。
        // 編集用の付加情報だけで管理し、キャンセル時は元の引数を変更しない。
        if (argument) {
            const auto candidates = argTypes(*argument);
            const auto found = std::find_if(candidates.begin(), candidates.end(),
                [](ArgType value) { return value != ArgType::NoArgument; });
            if (found != candidates.end()) {
                configureValueCell(cell, *found);
                if (!arg_defs_.empty())
                    cell.editor_value = arg_defs_[std::min(*argument, arg_defs_.size()-1U)].initValue;
            }
        }
        break;
    }
    case ArgType::String:
    case ArgType::List:
    case ArgType::CardInfo:
    case ArgType::Expression:
    case ArgType::NumberOrString:
    case ArgType::NumberOrList:
    case ArgType::Any:
        break;
    }
}

std::pair<ArgType, String> FunctionCallEditor::validArg(
    const ArgType argType, const StringView text) const {
    const String value(text);
    double number = 0.0;
    switch (argType) {
    case ArgType::Number:
        return parseNumber(value, number) ? std::make_pair(argType, value)
                                          : std::make_pair(argType, String{});
    case ArgType::Boolean: {
        return (value == "TRUE" || value == "FALSE")
                   ? std::make_pair(argType, value) : std::make_pair(argType, String{});
    }
    case ArgType::NumberOrString:
        return parseNumber(value, number) ? std::make_pair(ArgType::Number, value)
                                          : std::make_pair(ArgType::String, value);
    case ArgType::NumberOrList:
        if (parseNumber(value, number)) return {ArgType::Number, value};
        if (value.size() >= 2U && value.front() == '[' && value.back() == ']') {
            return {ArgType::List, value};
        }
        return {ArgType::List, String{}};
    case ArgType::Expression: {
        // D版の FunctionCallEditor::validArg と同様、Number と Boolean
        // だけを検証する。 Expression の引数は、ここでは追加の構文解析をせず受け付ける。
        return {argType, value};
    }
    case ArgType::NoArgument:
        return value.empty() ? std::make_pair(argType, value)
                             : std::make_pair(argType, String{});
    case ArgType::String:
    case ArgType::List:
    case ArgType::Status:
    case ArgType::CardInfo:
    case ArgType::Any:
    case ArgType::Flag:
    case ArgType::Step:
    case ArgType::Variant:
    case ArgType::VariantRef:
        return {argType, value};
    }
    return {argType, String{}};
}

void FunctionCallEditor::applySnapshot(
    const std::size_t selected, const std::vector<ArgType>& types,
    const std::vector<String>& values) {
    selected_arg_types_ = types;
    selected_row_ = selected;
    if (!args_) return;
    std::vector<lib::TableRow> rows;
    rows.reserve(selected_arg_types_.size());
    for (std::size_t index = 0; index < selected_arg_types_.size(); ++index) {
        const auto definition_index = arg_defs_.empty() ? 0U
            : std::min(index, arg_defs_.size() - 1U);
        String name;
        if (!arg_defs_.empty()) {
            const auto& definition = arg_defs_[definition_index];
            if (definition.varArg) {
                const auto ordinal = index < arg_defs_.size()
                    ? 1U : (index - arg_defs_.size()) + 2U;
                name = ::cwxeditor::cwx::utils::tryFormat(
                    definition.name, static_cast<int>(ordinal));
            } else {
                name = definition.name;
                if (definition.optional) name = "[" + name + "]";
            }
        }
        const auto type_name = argTypeName(selected_arg_types_[index]);
        const String value = index < values.size() ? values[index] : String{};
        lib::TableRow row;
        row.cells = {
            lib::ListItem{name, name, {}, false, false},
            lib::ListItem{type_name, type_name, {}, false, true},
            lib::ListItem{value, value, {}, false, true}};
        row.cells[1].editor = lib::CellEditorKind::Choice;
        for (const auto type : argTypes(index))
            row.cells[1].editor_choices.push_back(argTypeName(type));
        configureValueCell(row.cells[2], selected_arg_types_[index], index);
        rows.push_back(std::move(row));
    }
    if (selected_row_ >= rows.size())
        selected_row_ = rows.empty() ? 0U : rows.size() - 1U;
    const auto selection = rows.empty() ? std::vector<std::size_t>{}
                                        : std::vector<std::size_t>{selected_row_};
    (void)lib::replace_table_rows(args_, std::move(rows), selection,
                                  selection.empty()
                                      ? std::optional<std::size_t>{}
                                      : std::optional<std::size_t>{selected_row_});
}

void FunctionCallEditor::selectFunction(String name, const FunctionArguments& args) {
    if (name.empty()) return;
    // 名前による挿入では、引数を設定する前に移植版の両方のフィルターを解除する。
    if (function_inc_search_) function_inc_search_->close();
    if (function_search_) function_search_->text.clear();
    updateFunctions();
    if (selected_func_name_ != name) {
        (void)lib::select_combo_item(*category_, 0U, false);
        updateFunctions();
        for (std::size_t index = 0; index < func_defs_.size(); ++index) {
            if (func_defs_[index].name == name) {
                (void)lib::select_combo_item(*function_, index, false);
                functionSelected();
                break;
            }
        }
    }
    if (selected_func_name_ != name) return;
    for (std::size_t index = 0; index < args.size() && index < args_->rows.size(); ++index) {
        selected_arg_types_[index] = args[index].first;
        const auto type_name = argTypeName(args[index].first);
        (void)lib::publish_table_cell_text_value(*args_, index, 1U,
                                                 type_name, type_name);
        (void)lib::publish_table_cell_text_value(*args_, index, 2U,
                                                 args[index].second,
                                                 args[index].second);
        if (args_->rows[index].cells.size() > 2U)
            configureValueCell(args_->rows[index].cells[2], args[index].first, index);
    }
    if (!args.empty()) selected_row_ = 0U;
    if (!args.empty()) store();
}

void FunctionCallEditor::refFlagAndStep() {
    if (!args_) return;
    // D版と同様、表の行を置き換えたり編集中の引数をキャンセルしたりせず、開い
    // ている変数コンボを更新する。 変更するのは編集用の付加情報だけとする。
    const auto count = std::min(args_->rows.size(), selected_arg_types_.size());
    for (std::size_t index = 0; index < count; ++index) {
        if (args_->rows[index].cells.size() > 2U)
            configureValueCell(args_->rows[index].cells[2], selected_arg_types_[index], index);
    }
    if (const auto search=argument_inc_search_.lock()) {
        const auto callbacks=search->modEvent;
        for (const auto& callback:callbacks) if(callback) callback();
    }
    if (comm_.services.gui) comm_.services.gui->request_redraw(args_);
}

void FunctionCallEditor::focusToArgs() {
    if (args_ && !args_->rows.empty())
        (void)lib::select_table_rows(*args_, {selected_row_}, false, false);
    if (args_ && comm_.services.gui) comm_.services.gui->focus(args_);
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::exprutils
