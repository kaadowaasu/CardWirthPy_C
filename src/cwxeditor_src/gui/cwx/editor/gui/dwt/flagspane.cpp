#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dmenu.hpp"
#include "cwxeditor_src/core/cwx/structs.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/flagspane.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dutils.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/flagtable.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/xmlbytestransfer.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/comment.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/incsearch.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/messageutils.hpp"
#include "cwxeditor_src/core/cwx/summary.hpp"
#include "cwxeditor_src/core/cwx/system.hpp"
#include "cwxeditor_src/core/cwx/versioninfo.hpp"
#include "cwxeditor_src/lib/ui_actions.hpp"
#include "cwxeditor_src/lib/ui_metrics.hpp"

#include <algorithm>
#include <set>
#include <stdexcept>
#include <cctype>
#include <cmath>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace cwxeditor::cwx::editor::gui::dwt::flagspane {

namespace dedicated = ::cwxeditor::cwx::editor::gui::dwt::flagtable;
namespace transfer = ::cwxeditor::cwx::editor::gui::dwt::xmlbytestransfer;
namespace system = ::cwxeditor::cwx::system;
namespace versioninfo = ::cwxeditor::cwx::versioninfo;

// 履歴は、変更可能な名前やパスとは独立して、元のモデルオブジェクトその
// ものを保持する。 値の複製は従属する参照回数カウンターで管理する。
struct DirectorySnapshot {
    FlagDirPtr directory;
    std::unordered_map<const flag::Flag*, FlagPtr> flags;
    std::unordered_map<const flag::Step*, StepPtr> steps;
    std::unordered_map<const flag::Variant*, VariantPtr> variants;
    std::unordered_map<const flag::FlagDir*, FlagDirPtr> directories;
    ~DirectorySnapshot() { if (directory) directory->useCounter({}); }
};

namespace {

// 変数コメントのダイアログと、それが借用する comment::Commons を保持する。
struct CommentHolder {
    comment::Commons comm;
    std::shared_ptr<comment::CommentDialog> dialog;
    lib::Action disconnect;
    ~CommentHolder() { if (disconnect) disconnect(); }
};

using expression::VariantVal;

[[nodiscard]] bool activateExternalPathOwner(
    const std::shared_ptr<lib::GuiBackend>& gui,
    const std::shared_ptr<lib::Widget>& widget,
    const bool shellActivate, const bool focusWidget,
    const std::weak_ptr<int>& life) {
    if (!gui || !widget) return !life.expired();
    const auto owner = gui->window_owner(widget);
    if (life.expired()) return false;
    auto active = gui->active_input_window();
    if (life.expired()) return false;
    if (shellActivate && owner && active != owner) {
        gui->show(owner);
        if (life.expired()) return false;
        active = gui->active_input_window();
        if (life.expired()) return false;
    }
    if (focusWidget && owner && active == owner) {
        gui->focus(widget);
        if (life.expired()) return false;
    }
    return true;
}

[[nodiscard]] String lower(String value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](const unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    return value;
}

[[nodiscard]] const system::System& flagEditorSystem() {
    static const system::System value;
    return value;
}

[[nodiscard]] bool hasDropOperation(const lib::DropOperation value,
                                    const lib::DropOperation bit) noexcept {
    return (static_cast<std::uint32_t>(value) &
            static_cast<std::uint32_t>(bit)) != 0U;
}

template <typename Value>
void appendDropValues(const std::unordered_map<String, std::shared_ptr<Value>>& values,
                      std::vector<std::shared_ptr<Value>>& result) {
    result.reserve(result.size() + values.size());
    for (const auto& [unused, value] : values) {
        (void)unused;
        if (value) result.push_back(value);
    }
}

[[nodiscard]] bool sameObject(const VariableObject& first,
                              const VariableObject& second) {
    return std::visit(
        [](const auto& left, const auto& right) {
            using Left = std::decay_t<decltype(left)>;
            using Right = std::decay_t<decltype(right)>;
            if constexpr (std::is_same_v<Left, Right>) {
                return left.get() == right.get();
            } else {
                return false;
            }
        },
        first, second);
}

[[nodiscard]] String initializationName(
    const types::VariableInitialization value, const Messages& messages) {
    using Init = types::VariableInitialization;
    if (value == Init::None) return messages.variableInitializationShortNameNone;
    String result;
    const auto append = [&result](StringView text) {
        if (!result.empty()) result += ' ';
        result += text;
    };
    const auto bits = static_cast<std::int32_t>(value);
    if ((bits & static_cast<std::int32_t>(Init::BattleStart)) != 0)
        append(messages.variableInitializationShortNameBattleStart);
    if ((bits & static_cast<std::int32_t>(Init::BattleEnd)) != 0)
        append(messages.variableInitializationShortNameBattleEnd);
    if ((bits & static_cast<std::int32_t>(Init::EventExit)) != 0)
        append(messages.variableInitializationShortNameEventExit);
    if ((bits & static_cast<std::int32_t>(Init::Leave)) != 0)
        append(messages.variableInitializationShortNameLeave);
    return result.empty() ? messages.variableInitializationShortNameLeave : result;
}

[[nodiscard]] std::optional<types::VariableInitialization> parseInitialization(
    String value, const Messages& messages) {
    using Init = types::VariableInitialization;
    value = lower(std::move(value));
    value.erase(std::remove_if(value.begin(), value.end(), [](const unsigned char ch) {
        return std::isspace(ch) != 0 || ch == ',';
    }), value.end());
    const auto matches = [&value](String candidate) {
        candidate = lower(std::move(candidate));
        candidate.erase(std::remove_if(candidate.begin(), candidate.end(),
            [](const unsigned char ch) {
                return std::isspace(ch) != 0 || ch == ',';
            }), candidate.end());
        return value == candidate;
    };
    if (value == "none" || matches(messages.variableInitializationShortNameNone))
        return Init::None;
    if (value == "leave" || matches(messages.variableInitializationShortNameLeave))
        return Init::Leave;
    if (value == "eventexit" ||
        matches(messages.variableInitializationShortNameEventExit))
        return Init::EventExit;
    if (value == "battlestart" ||
        matches(messages.variableInitializationShortNameBattleStart))
        return Init::BattleStart;
    if (value == "battleend" ||
        matches(messages.variableInitializationShortNameBattleEnd))
        return Init::BattleEnd;
    return std::nullopt;
}

[[nodiscard]] String panePayload(const StringView xml, const StringView pane_id) {
    if (xml.empty() || pane_id.empty()) return String(xml);
    try {
        auto root = lib::XmlNode::parse(xml);
        root.newAttr("paneId", pane_id);
        return root.text();
    } catch (const std::exception&) {
        return String(xml);
    }
}

[[nodiscard]] bool payloadFromPane(const StringView xml, const StringView pane_id) {
    if (xml.empty() || pane_id.empty()) return false;
    try {
        return lib::XmlNode::parse(xml).attribute("paneId").value_or(String{}) == pane_id;
    } catch (const std::exception&) {
        return false;
    }
}

[[nodiscard]] String numberText(const double value) {
    return expression::variantValueToText(
        expression::VariantVal::numValue(value));
}

[[nodiscard]] String variantValueText(const VariantVal& value);

[[nodiscard]] String variantValuesText(const std::vector<VariantVal>& values) {
    String result{"["};
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) result += ", ";
        result += variantValueText(values[index]);
    }
    result += "]";
    return result;
}

[[nodiscard]] String variantValueText(const VariantVal& value) {
    switch (value.type) {
    case types::VariantType::Number:
        return numberText(value.numVal);
    case types::VariantType::String:
        return value.strVal;
    case types::VariantType::Boolean:
        return value.boolVal ? "true" : "false";
    case types::VariantType::List:
        return variantValuesText(value.listVal);
    case types::VariantType::Structure:
        return value.structName + variantValuesText(value.structVal);
    }
    return {};
}

[[nodiscard]] String flagValueText(const FlagPtr& value) {
    return value && value->onOff() ? value->on() : (value ? value->off() : String{});
}

[[nodiscard]] String stepValueText(const StepPtr& value) {
    return value ? value->value() : String{};
}

[[nodiscard]] String variantValueText(const VariantPtr& value) {
    if (!value) return {};
    VariantVal result;
    result.valid = true;
    result.type = value->type();
    result.numVal = value->numVal();
    result.strVal = value->strVal();
    result.boolVal = value->boolVal();
    result.listVal = value->listVal();
    result.structName = value->structName();
    result.structVal = value->structVal();
    return variantValueText(result);
}

[[nodiscard]] std::uint32_t useCount(const UseCounterPtr& counter,
                                     const FlagPtr& value) {
    return counter && value
               ? counter->get(usecounter::toFlagId(value->path()))
               : 0U;
}

[[nodiscard]] std::uint32_t useCount(const UseCounterPtr& counter,
                                     const StepPtr& value) {
    return counter && value
               ? counter->get(usecounter::toStepId(value->path()))
               : 0U;
}

[[nodiscard]] std::uint32_t useCount(const UseCounterPtr& counter,
                                     const VariantPtr& value) {
    return counter && value
               ? counter->get(usecounter::toVariantId(value->path()))
               : 0U;
}

[[nodiscard]] bool nameLess(StringView first, StringView second, bool logical) {
    // D版の sortedWithName と同様、ncmp と大文字小文字を区別する cmp を切り替える。
    return ::cwxeditor::cwx::utils::NCompare(String(first), false, logical) <
           ::cwxeditor::cwx::utils::NCompare(String(second), false, logical);
}

[[nodiscard]] FlagPtr cloneFlag(const FlagPtr& source,
                                const UseCounterPtr& counter = {}) {
    if (!source) return {};
    auto result = std::make_shared<flag::Flag>(
        source->name(), source->on(), source->off(), source->onOff());
    result->expandSPChars(source->expandSPChars());
    result->initialization(source->initialization());
    result->comment(source->comment());
    result->useCounter(counter ? counter : source->useCounter());
    return result;
}

[[nodiscard]] StepPtr cloneStep(const StepPtr& source,
                                const UseCounterPtr& counter = {}) {
    if (!source) return {};
    auto result = std::make_shared<flag::Step>(source->name(), source->values(),
                                                source->select());
    result->expandSPChars(source->expandSPChars());
    result->initialization(source->initialization());
    result->comment(source->comment());
    result->useCounter(counter ? counter : source->useCounter());
    return result;
}

[[nodiscard]] VariantPtr cloneVariant(const VariantPtr& source,
                                      const UseCounterPtr& counter = {}) {
    if (!source) return {};
    VariantPtr result;
    switch (source->type()) {
    case types::VariantType::Number:
        result = std::make_shared<flag::Variant>(source->name(), source->numVal());
        break;
    case types::VariantType::String:
        result = std::make_shared<flag::Variant>(source->name(), source->strVal());
        break;
    case types::VariantType::Boolean:
        result = std::make_shared<flag::Variant>(source->name(), source->boolVal());
        break;
    case types::VariantType::List:
        result = std::make_shared<flag::Variant>(source->name(), source->listVal());
        break;
    case types::VariantType::Structure:
        result = std::make_shared<flag::Variant>(
            source->name(), source->structName(), source->structVal());
        break;
    }
    if (result) {
        result->initialization(source->initialization());
        result->comment(source->comment());
        result->useCounter(counter ? counter : source->useCounter());
    }
    return result;
}

[[nodiscard]] FlagDirPtr cloneDirectoryValue(const FlagDirPtr& source,
                                             const UseCounterPtr& counter = {}) {
    if (!source) return {};
    const auto chosenCounter = counter ? counter : source->useCounter();
    auto result = std::make_shared<flag::FlagDir>(source->name());
    result->useCounter(chosenCounter);
    for (const auto& value : source->variants()) {
        const auto copy = cloneVariant(value, chosenCounter);
        if (copy) result->add(copy);
    }
    for (const auto& value : source->steps()) {
        const auto copy = cloneStep(value, chosenCounter);
        if (copy) result->add(copy);
    }
    for (const auto& value : source->flags()) {
        const auto copy = cloneFlag(value, chosenCounter);
        if (copy) result->add(copy);
    }
    for (const auto& value : source->subDirs()) {
        const auto copy = cloneDirectoryValue(value, chosenCounter);
        if (copy) result->add(copy);
    }
    return result;
}

// 履歴は従属するカウンターを通じて参照先の改名に追従し、現在のモデ
// ルの参照回数には加算しない。 カウンターは登録された利用側を保持
// する場合があるため、履歴の寿命が終わる時点で明示的に切り離す。
[[nodiscard]] std::shared_ptr<DirectorySnapshot> snapshotDirectoryValue(
    const FlagDirPtr& source) {
    if (!source) return {};
    auto snapshot = std::make_shared<DirectorySnapshot>();
    const auto counter = source->useCounter();
    snapshot->directory = cloneDirectoryValue(source, counter ? counter->sub() : UseCounterPtr{});
    const auto record = [&snapshot](auto&& self, const FlagDirPtr& original,
                                    const FlagDirPtr& clone) -> void {
        snapshot->directories.emplace(clone.get(), original);
        const auto pair = [](const auto& originals, const auto& copies, auto& map) {
            std::size_t index = 0;
            for (const auto& object : originals) {
                if (!object) continue;
                if (index >= copies.size()) throw std::logic_error("Incomplete history clone");
                map.emplace(copies[index++].get(), object);
            }
        };
        pair(original->flags(), clone->flags(), snapshot->flags);
        pair(original->steps(), clone->steps(), snapshot->steps);
        pair(original->variants(), clone->variants(), snapshot->variants);
        std::size_t index = 0;
        for (const auto& child : original->subDirs())
            if (child) self(self, child, clone->subDirs().at(index++));
    };
    record(record, source, snapshot->directory);
    return snapshot;
}

struct DefinitionPaths {
    std::set<String> flags, steps, variants;
};

DefinitionPaths definitionPaths(const FlagDirPtr& root) {
    DefinitionPaths result;
    if (!root) return result;
    for (const auto& value : root->allFlags()) if (value) result.flags.insert(value->path());
    for (const auto& value : root->allSteps()) if (value) result.steps.insert(value->path());
    for (const auto& value : root->allVariants()) if (value) result.variants.insert(value->path());
    return result;
}

void restoreDirectoryValue(const FlagDirPtr& target, const FlagDirPtr& source,
                           const bool reconcileDefinitions = true,
                           const DirectorySnapshot* identity = nullptr) {
    if (!target || !source) return;
    const auto counter = target->useCounter();
    const auto before = definitionPaths(target);
    // スナップショットの復元ではツリー全体を置き換える。途中の
    // 削除・追加を逐一通知すると、 ネイティブの表が切り離し済
    // みモデルの行を参照してしまう。 元のシナリオのハンドラー
    // を保持して一括で再構築し、 親子関係とカウンターの接続
    // をすべて復元してから、整合した変更を一度だけ通知する。
    const auto changed = target->changeHandler();
    target->changeHandler({});
    std::set<const flag::FlagDir*> reusableDirectories;
    if (identity) {
        // 改名や親の変更より先に、現在の子孫をすべて切り離す。 削除後も、
        // スナップショットが保持する切り離し済みフォルダーには子が残る場
        // 合がある。 それらのコンテナーも、変更通知を出さずに空にする。
        std::vector<FlagDirPtr> directories;
        const auto collect = [&directories, &reusableDirectories](auto&& self, const FlagDirPtr& dir) -> void {
            if (!dir || !reusableDirectories.insert(dir.get()).second) return;
            directories.push_back(dir);
            for (const auto& child : dir->subDirs()) self(self, child);
        };
        collect(collect, target);
        for (const auto& [clone, original] : identity->directories) {
            (void)clone;
            if (original && (original->root().get() == target.get() ||
                (!original->parent() && !original->useCounter())))
                collect(collect, original);
        }
        for (const auto& dir : directories) dir->changeHandler({});
        for (const auto& dir : directories) dir->removeAll();
    } else {
        target->removeAll();
    }
    // シナリオのローカル変数のルートは改名できない。
    if (target->parent() && target->name() != source->name()) target->name(source->name());
    target->useCounter(counter);
    const auto restore = [identity, &counter, &reusableDirectories](auto&& self, const FlagDirPtr& to,
                                             const FlagDirPtr& from) -> void {
        const auto restoreValues = [&to](const auto& values, const auto* originals, auto clone) {
            for (const auto& value : values) {
                if (!value) continue;
                std::decay_t<decltype(value)> object;
                if (originals) {
                    const auto found = originals->find(value.get());
                    // 別のツリーに接続済みのオブジェクトを奪わない。
                    if (found != originals->end() && found->second && !found->second->parent()) {
                        object = found->second;
                        object->copyFrom(*value);
                    }
                }
                if (!object) object = clone(value);
                if (object) to->add(object);
            }
        };
        restoreValues(from->variants(), identity ? &identity->variants : nullptr,
                      [&counter](const auto& value) { return cloneVariant(value, counter); });
        restoreValues(from->steps(), identity ? &identity->steps : nullptr,
                      [&counter](const auto& value) { return cloneStep(value, counter); });
        restoreValues(from->flags(), identity ? &identity->flags : nullptr,
                      [&counter](const auto& value) { return cloneFlag(value, counter); });
        for (const auto& child : from->subDirs()) {
            if (!child) continue;
            FlagDirPtr restored;
            if (identity) {
                const auto found = identity->directories.find(child.get());
                if (found != identity->directories.end() && found->second &&
                    reusableDirectories.contains(found->second.get()) && !found->second->parent())
                    restored = found->second;
            }
            if (!restored) restored = std::make_shared<flag::FlagDir>(child->name());
            else if (restored->name() != child->name()) restored->name(child->name());
            restored->useCounter(counter);
            self(self, restored, child);
            to->add(restored);
        }
    };
    restore(restore, target, source);
    if (counter && reconcileDefinitions) {
        const auto after = definitionPaths(target);
        const auto reconcile = [&counter](const auto& oldPaths, const auto& newPaths, auto toId) {
            for (const auto& path : oldPaths)
                if (!newPaths.contains(path)) counter->deleteID(toId(path));
            for (const auto& path : newPaths)
                if (!oldPaths.contains(path)) counter->createID(toId(path));
        };
        reconcile(before.flags, after.flags, [](const auto& path) { return usecounter::toFlagId(path); });
        reconcile(before.steps, after.steps, [](const auto& path) { return usecounter::toStepId(path); });
        reconcile(before.variants, after.variants, [](const auto& path) { return usecounter::toVariantId(path); });
    }
    target->changeHandler(changed);
    if (changed) changed();
}

// スナップショットの走査では、削除コマンドを通さずに新規オブジェクトを取
// り除く場合がある。 復元後に実際に存在しなくなった対象だけを通知する。
void notifyHistoryRemoved(Commons& comm, const FlagDirPtr& root,
                          FlagList flags, StepList steps, VariantList variants) {
    if (!root) return;
    const auto filter = [](auto& before, const auto& after) {
        std::erase_if(before, [&after](const auto& value) {
            return std::any_of(after.begin(), after.end(), [&value](const auto& current) {
                return current.get() == value.get();
            });
        });
    };
    filter(flags, root->allFlags());
    filter(steps, root->allSteps());
    filter(variants, root->allVariants());
    if (!flags.empty() || !steps.empty() || !variants.empty())
        comm.delFlagAndStep.call(flags, steps, variants);
}

[[nodiscard]] VariableObject cloneObject(const VariableObject& source,
                                         const UseCounterPtr& counter = {}) {
    return std::visit(
        [&counter](const auto& value) -> VariableObject {
            using Value = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Value, FlagPtr>) {
                return cloneFlag(value, counter);
            } else if constexpr (std::is_same_v<Value, StepPtr>) {
                return cloneStep(value, counter);
            } else {
                return cloneVariant(value, counter);
            }
        },
        source);
}

[[nodiscard]] String objectPath(const VariableObject& object) {
    return std::visit(
        [](const auto& value) { return value ? value->path() : String{}; },
        object);
}

[[nodiscard]] String objectName(const VariableObject& object) {
    return std::visit(
        [](const auto& value) { return value ? value->name() : String{}; },
        object);
}

[[nodiscard]] VariableKind objectKind(const VariableObject& object) {
    return std::visit(
        [](const auto& value) -> VariableKind {
            using Value = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Value, VariantPtr>)
                return VariableKind::Variant;
            if constexpr (std::is_same_v<Value, StepPtr>)
                return VariableKind::Step;
            return VariableKind::Flag;
        },
        object);
}

template <typename Ptr, typename ToId>
void applyVariableNames(const std::vector<Ptr>& items,
                        const std::vector<String>& names,
                        const UseCounterPtr& counter, ToId to_id) {
    if (items.empty() && names.empty()) return;
    if (items.size() != names.size() || !items.front() || !items.front()->parent())
        throw std::logic_error("Variable rename target is no longer attached");
    const auto dir = items.front()->parent();
    std::set<String> reserved(names.begin(), names.end());
    const auto reserve = [&reserved](const auto& values) {
        for (const auto& value : values) if (value) reserved.insert(value->name());
    };
    if constexpr (std::is_same_v<Ptr, FlagPtr>) reserve(dir->flags());
    else if constexpr (std::is_same_v<Ptr, StepPtr>) reserve(dir->steps());
    else reserve(dir->variants());
    std::vector<String> temporary;
    for (std::size_t suffix = 0; temporary.size() < items.size(); ++suffix) {
        auto name = String{"__cwx_inline_rename"};
        if (suffix) name += " (" + std::to_string(suffix + 1) + ")";
        if (reserved.insert(name).second) temporary.push_back(std::move(name));
    }
    for (const auto& item : items)
        if (!item || item->parent() != dir)
            throw std::logic_error("Variable rename targets have different parents");
    for (std::size_t index = 0; index < items.size(); ++index) {
        const auto old_id = to_id(items[index]->path());
        if (!items[index]->name(temporary[index]))
            throw std::logic_error("Reserved variable rename was rejected");
        if (counter) counter->change(old_id, to_id(items[index]->path()));
    }
    for (std::size_t index = 0; index < items.size(); ++index) {
        const auto old_id = to_id(items[index]->path());
        if (!items[index]->name(names[index]))
            throw std::logic_error("Reserved variable rename was rejected");
        if (counter) counter->change(old_id, to_id(items[index]->path()));
    }
}

template <typename Ptr>
std::vector<std::size_t> variableIndices(const std::vector<Ptr>& items) {
    std::vector<std::size_t> result;
    for (const auto& item : items)
        result.push_back(static_cast<std::size_t>(item->parent()->indexOf(*item)));
    return result;
}

template <typename Ptr>
std::vector<Ptr> liveVariables(const FlagDirPtr& root, const String& directory,
                              const std::vector<std::size_t>& indices,
                              const std::vector<String>& expectedNames) {
    const auto dir = root && root->path() == directory ? root
        : root ? root->findPath(directory) : FlagDirPtr{};
    if (!dir || indices.size() != expectedNames.size())
        throw std::logic_error("Variable rename history directory is missing");
    const auto& values = [&]() -> const auto& {
        if constexpr (std::is_same_v<Ptr, FlagPtr>) return dir->flags();
        else if constexpr (std::is_same_v<Ptr, StepPtr>) return dir->steps();
        else return dir->variants();
    }();
    std::vector<Ptr> result;
    for (std::size_t i = 0; i < indices.size(); ++i) {
        if (indices[i] >= values.size() || !values[indices[i]] ||
            values[indices[i]]->name() != expectedNames[i])
            throw std::logic_error("Variable rename history identity does not match");
        result.push_back(values[indices[i]]);
    }
    return result;
}

[[nodiscard]] String joinPaths(const std::vector<String>& paths) {
    String result;
    for (const auto& path : paths) {
        if (!result.empty()) result += '\n';
        result += path;
    }
    return result;
}

[[nodiscard]] String treePath(const FlagDirPtr& value) {
    return value ? value->path() : String{};
}

[[nodiscard]] FlagDirPtr resolveTreePath(const FlagDirPtr& root, String path) {
    if (!root) return {};
    // ツリーノードはモデルの完全なパスを保持する。D版はディレクトリ自体をノード
    // データに持つため、 C++版の相対検索では、このルートの接頭辞を取り除く。
    // 特に Local はスコープの接頭辞であり、子ディレクトリ名ではない。
    const auto prefix = root->path();
    if (!prefix.empty() && path.starts_with(prefix)) path.erase(0, prefix.size());
    return path.empty() ? root : flag::FlagDir::searchPath(root, std::move(path));
}

}  // namespace

Props::Props() {
    var.etc = etc;
    var.looks = looks;
}

void Props::synchronize() noexcept {
    // 移植版のAPIは直接のメンバーを使う。構築後にD版形式の集
    // 約データを調べる呼び出し元のため、 そちらも同期しておく。
    var.etc = etc;
    var.looks = looks;
}

UndoManager::UndoManager(const std::size_t maximum) : maximum_(maximum) {}

namespace {
class SharedHistoryCommand final : public ::cwxeditor::cwx::editor::gui::dwt::undo::Undo {
public:
    SharedHistoryCommand(lib::Action undo_action, lib::Action redo_action)
        : undo_(std::move(undo_action)), redo_(std::move(redo_action)) {}
    void undo() override { if (undo_) undo_(); }
    void redo() override { if (redo_) redo_(); }
    void dispose() override { undo_ = {}; redo_ = {}; }
private:
    lib::Action undo_, redo_;
};
}

UndoManager::UndoManager(
    std::shared_ptr<::cwxeditor::cwx::editor::gui::dwt::undo::UndoManager> shared)
    : shared_core_(std::move(shared)) {
    if (shared_core_) maximum_ = shared_core_->max();
}


bool UndoManager::canUndo() const noexcept { return shared_core_ ? shared_core_->canUndo() : cursor_ != 0; }

bool UndoManager::canRedo() const noexcept {
    if (shared_core_) return shared_core_->canRedo();
    return cursor_ < commands_.size();
}

void UndoManager::undo() {
    if (shared_core_) { (void)shared_core_->undo(); return; }
    if (!canUndo()) return;
    // D版の共有 UndoManager と同様、コマンドが完了した後にだけカーソル
    // を進める。 イベントやフラグの復元が失敗しても、取り消し可能な状態を維持する。
    const auto index = cursor_ - 1U;
    if (commands_[index].undo) commands_[index].undo();
    cursor_ = index;
}

void UndoManager::redo() {
    if (shared_core_) { (void)shared_core_->redo(); return; }
    if (!canRedo()) return;
    if (commands_[cursor_].redo) commands_[cursor_].redo();
    ++cursor_;
    max(maximum_);
}

void UndoManager::reset() noexcept {
    // 明示的な reset は共通履歴を消去する。ペインの破棄では、共有コ
    // アの reset を呼ばず、 ファサードとその所有権だけを破棄する。
    if (shared_core_) shared_core_->reset();
    commands_.clear();
    cursor_ = 0;
    pendingRoot_.reset();
    pendingBefore_.reset();
    pendingUndoSideEffect_ = {};
    pendingRedoSideEffect_ = {};
    pendingBeforeTraversal_ = {};
    pendingAfterTraversal_ = {};
}

void UndoManager::max(const std::size_t value) {
    maximum_ = value;
    if (shared_core_) { shared_core_->max(value); return; }
    // 上限は適用済みコマンドに対して設け、未適用のやり直し部分には適用しない。
    if (cursor_ <= maximum_) return;
    const auto eraseCount = cursor_ - maximum_;
    commands_.erase(commands_.begin(), commands_.begin() +
                                         static_cast<std::ptrdiff_t>(eraseCount));
    cursor_ = cursor_ < eraseCount ? 0 : cursor_ - eraseCount;
}

void UndoManager::push(lib::Action undoAction, lib::Action redoAction) {
    if (!undoAction || !redoAction) return;
    if (shared_core_) {
        shared_core_->add(std::make_shared<SharedHistoryCommand>(
            std::move(undoAction), std::move(redoAction)));
        return;
    }
    if (cursor_ < commands_.size()) {
        commands_.erase(commands_.begin() + static_cast<std::ptrdiff_t>(cursor_),
                        commands_.end());
    }
    if (maximum_ == 0) return;
    commands_.push_back({std::move(undoAction), std::move(redoAction)});
    cursor_ = commands_.size();
    if (commands_.size() > maximum_) {
        commands_.erase(commands_.begin());
        --cursor_;
    }
}

void UndoManager::beginSnapshot(const FlagDirPtr& root) {
    pendingRoot_ = root;
    pendingBefore_ = snapshotDirectoryValue(root);
    pendingUndoSideEffect_ = {};
    pendingRedoSideEffect_ = {};
    pendingBeforeTraversal_ = {};
    pendingAfterTraversal_ = {};
}

void UndoManager::addSnapshotSideEffect(lib::Action undoAction,
                                        lib::Action redoAction) {
    if (!pendingRoot_ || !pendingBefore_) return;
    pendingUndoSideEffect_ = std::move(undoAction);
    pendingRedoSideEffect_ = std::move(redoAction);
}

void UndoManager::addSnapshotTraversal(std::function<void(bool)> before,
                                        std::function<void(bool)> after) {
    if (!pendingRoot_ || !pendingBefore_) return;
    pendingBeforeTraversal_ = std::move(before);
    pendingAfterTraversal_ = std::move(after);
}

void UndoManager::commitSnapshot(const FlagDirPtr& root) {
    if (!pendingRoot_ || !pendingBefore_ || pendingRoot_.get() != root.get()) {
        pendingRoot_.reset();
        pendingBefore_.reset();
        pendingUndoSideEffect_ = {};
        pendingRedoSideEffect_ = {};
    pendingBeforeTraversal_ = {};
    pendingAfterTraversal_ = {};
        return;
    }
    const auto target = pendingRoot_;
    const auto before = pendingBefore_;
    const auto after = snapshotDirectoryValue(target);
    auto undoSideEffect = std::move(pendingUndoSideEffect_);
    auto redoSideEffect = std::move(pendingRedoSideEffect_);
    auto beforeTraversal = std::move(pendingBeforeTraversal_);
    auto afterTraversal = std::move(pendingAfterTraversal_);
    push([target, before, beforeTraversal, afterTraversal, sideEffect = std::move(undoSideEffect)] {
             if (beforeTraversal) beforeTraversal(true);
             if (sideEffect) sideEffect();
             restoreDirectoryValue(target, before->directory, !sideEffect, before.get());
             if (afterTraversal) afterTraversal(true);
         },
         [target, after, beforeTraversal, afterTraversal, sideEffect = std::move(redoSideEffect)] {
             if (beforeTraversal) beforeTraversal(false);
             restoreDirectoryValue(target, after->directory, !sideEffect, after.get());
             if (sideEffect) sideEffect();
             if (afterTraversal) afterTraversal(false);
         });
    pendingRoot_.reset();
    pendingBefore_.reset();
}

void UndoManager::discardSnapshot() noexcept {
    pendingRoot_.reset();
    pendingBefore_.reset();
    pendingUndoSideEffect_ = {};
    pendingRedoSideEffect_ = {};
    pendingBeforeTraversal_ = {};
    pendingAfterTraversal_ = {};
}

FlagDirPtr UndoManager::cloneDirectory(const FlagDirPtr& source,
                                       const UseCounterPtr& counter) {
    return ::cwxeditor::cwx::editor::gui::dwt::flagspane::cloneDirectoryValue(
        source, counter);
}

void UndoManager::restoreDirectory(const FlagDirPtr& target,
                                   const FlagDirPtr& source) {
    ::cwxeditor::cwx::editor::gui::dwt::flagspane::restoreDirectoryValue(
        target, source);
}

template <typename Component>
std::shared_ptr<Component> FlagTable::createComponent(
    const lib::ComponentKind kind) const {
    if (comm_.services.gui) {
        auto candidate = comm_.services.gui->create_component(kind);
        if (auto result = std::dynamic_pointer_cast<Component>(candidate))
            return result;
    }
    return std::make_shared<Component>();
}

FlagTable::FlagTable(Commons comm, Props prop, UndoManager& undo,
                     const bool local, const bool readOnly)
    : comm_(std::move(comm)), prop_(std::move(prop)), undo_(&undo),
      local_(local), readOnly_(readOnly), uc_(comm_.useCounter) {}

FlagTable::~FlagTable() {
    dispose();
}

void FlagTable::refreshUseCount() {
    if (disposed_ || !table_) return;
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const auto count = std::visit([this](const auto& value) {
            return useCount(uc_, value);
        }, rows_[i].object);
        rows_[i].useCount = count;
        if (i < table_->rows.size() && table_->rows[i].cells.size() > 3U) {
            auto& cell = table_->rows[i].cells[3U];
            cell.text = cell.value = std::to_string(count);
        }
    }
    // 参照回数の更新では、行の順序を変えたり現在のインライン編集欄を置き換えたりしない。
    ++table_->row_update_revision;
    if (comm_.services.gui) comm_.services.gui->request_redraw(table_);
}

PanelPtr FlagTable::createControl(const Parent& parent,
                                  const Parent& incSearchParent,
                                  lib::Action gotFocus) {
    // dispose() はネイティブメニューが保持するコールバックを無効化
    // する。 その後のコントロール構築は、新しい有効期間の開始として扱う。
    if (!callback_lifetime_) callback_lifetime_ = std::make_shared<int>(0);
    disposed_ = false;
    comm_.refUseCount.remove(use_count_connection_);
    use_count_connection_ = comm_.refUseCount.connect(
        [this, life = std::weak_ptr<int>(callback_lifetime_)] {
            if (!life.expired()) refreshUseCount();
        });
    component_ = createComponent<lib::Panel>(lib::ComponentKind::Panel);
    component_->set_layout(std::make_shared<lib::FillLayout>());
    table_ = createComponent<lib::TableView>(lib::ComponentKind::TableView);
    drag_pane_id_ = "flag-table:" + std::to_string(
        reinterpret_cast<std::uintptr_t>(this));
    table_->multi_select = true;
    table_->full_row_selection = true;
    // D版と同様、F2での入力が変わらなくても、残りの選択を含めて nameEditEnd を呼ぶ。
    table_->commit_unchanged_inline_edits = !readOnly_;
    const auto& etc = prop_.etc;
    table_->columns = {
        {prop_.msgs.flagName, std::max(1.0F, local_ ? etc.localFlagNameColumn
                                                   : etc.flagNameColumn),
         lib::TextAlignment::Start, true, true},
        {prop_.msgs.flagInit, std::max(1.0F, local_ ? etc.localFlagInitColumn
                                                   : etc.flagInitColumn),
         lib::TextAlignment::Start, true, false},
        {prop_.msgs.variableInitializationShort,
         std::max(1.0F, local_ ? etc.localVariableInitializationColumn
                              : etc.variableInitializationColumn),
         lib::TextAlignment::Start, true, false},
        {prop_.msgs.flagCount, std::max(1.0F, local_ ? etc.localFlagCountColumn
                                                    : etc.flagCountColumn),
         lib::TextAlignment::End, true, false},
    };
    table_->on_column_resized = [this](const std::size_t column,
                                       const float width) {
        if (column >= 4U || !std::isfinite(width) || width <= 0.0F) return;
        auto& etc = prop_.etc;
        auto assign = [column, width](float& name, float& initial,
                                      float& initialization, float& count) {
            std::array<float*, 4> fields{&name, &initial, &initialization, &count};
            *fields[column] = width;
        };
        if (local_)
            assign(etc.localFlagNameColumn, etc.localFlagInitColumn,
                   etc.localVariableInitializationColumn,
                   etc.localFlagCountColumn);
        else
            assign(etc.flagNameColumn, etc.flagInitColumn,
                   etc.variableInitializationColumn, etc.flagCountColumn);
        prop_.synchronize();
        if (etc.storeFlagColumn) etc.storeFlagColumn(local_, column, width);
    };
    table_->on_selection_changed = [this](const std::vector<std::size_t>& value) {
        selectedIndices_.clear();
        for (const auto index : value)
            if (index < rows_.size()) selectedIndices_.push_back(index);
        for (std::size_t index = 0; index < rows_.size(); ++index)
            rows_[index].selected = std::find(selectedIndices_.begin(),
                                               selectedIndices_.end(), index) !=
                                     selectedIndices_.end();
        const std::weak_ptr<int> status_life = callback_lifetime_;
        refreshStatusLine();
        if (!status_life.expired()) notifySelection();
    };
    table_->on_cell_activate = [this](const std::size_t row,
                                       const std::size_t) {
        if (row >= rows_.size()) return;
        // D版の KListener/MListener と同様、選択全体に edit()
        // を適用する。 アクセシビリティ機能や、先に表の選択を通知しないプロ
        // グラムからの呼び出しのため、 未選択行の直接的な有効化も維持する。
        if (std::find(selectedIndices_.begin(), selectedIndices_.end(), row) !=
            selectedIndices_.end()) {
            edit();
            return;
        }
        std::visit([this](const auto& value) { edit(value); },
                   rows_[row].object);
    };
    table_->on_cell_edited = [this](const std::size_t row,
                                     const std::size_t column, String value) {
        if (row >= rows_.size() || readOnly_) return;
        if (std::find(selectedIndices_.begin(), selectedIndices_.end(), row) ==
            selectedIndices_.end())
            selectedIndices_ = {row};
        const auto object = rows_[row].object;
        if (column == 0U) {
            // D版の nameEditEnd と同様、最初に生成された名前を編集中の行に割り当てる。
            if (!lib::editor_window_host_options().all_mode) {
                const auto active = std::find(selectedIndices_.begin(), selectedIndices_.end(), row);
                if (active != selectedIndices_.end())
                    std::rotate(selectedIndices_.begin(), active, active + 1);
            }
            (void)renameSelected(std::move(value));
            return;
        }
        if (column == 2U) {
            if (const auto initialization =
                    parseInitialization(std::move(value), prop_.msgs))
                (void)setInitialization(object, *initialization);
            return;
        }
        if (column != 1U) return;
        std::visit([this, &value](const auto& item) {
            using Value = std::decay_t<decltype(item)>;
            if (!item) return;
            if constexpr (std::is_same_v<Value, FlagPtr>) {
                // ネイティブ・共通の表の表示側は、固定の選択値とD版の
                // 表示ラベルのどちらを返す場合もある。 両方を受け付け
                // 、どのバックエンドでも同じ複数行の変更処理を使う。
                if (value == "flag:1")
                    (void)setFlagState(item, true);
                else if (value == "flag:0")
                    (void)setFlagState(item, false);
                else if (value == item->on())
                    (void)setFlagState(item, true);
                else if (value == item->off())
                    (void)setFlagState(item, false);
            } else if constexpr (std::is_same_v<Value, StepPtr>) {
                constexpr StringView prefix{"step:"};
                std::uint32_t selected{};
                bool valid = false;
                if (value.rfind(prefix, 0U) == 0U) {
                    const auto first = value.data() + prefix.size();
                    const auto last = value.data() + value.size();
                    const auto parsed = std::from_chars(first, last, selected);
                    valid = parsed.ec == std::errc{} && parsed.ptr == last;
                } else {
                    for (std::uint32_t index = 0; index < item->count(); ++index) {
                        if (item->getValue(index) != value) continue;
                        selected = index;
                        valid = true;
                        break;
                    }
                }
                if (valid && selected < item->count())
                    (void)setStepSelection(item, selected);
            } else {
                const auto parsed = expression::variantValueFromText(value);
                if (parsed.valid) (void)setVariantValue(item, parsed);
            }
        }, object);
    };
    // D版と同様、変更可能なペインにだけ三つのインライン編集欄を設定する
    // 。 コールバックを未設定にすることは、共通バックエンドに対する読み
    // 取り専用の明示でもある。 下のプロパティ表示は引き続き利用できる。
    if (readOnly_) table_->on_cell_edited = {};
    const auto lifetime = std::weak_ptr<int>(callback_lifetime_);
    table_->on_inline_editor_created = [this, lifetime](
        const std::size_t row, const std::size_t column,
        const std::shared_ptr<lib::TextEdit>& text,
        const std::shared_ptr<lib::ComboBox>&) {
        if (lifetime.expired() || readOnly_ || column != 1U || !text ||
            row >= rows_.size() || !std::holds_alternative<VariantPtr>(rows_[row].object)) return;
        messageutils::configureVariantValueEditor(text, prop_.msgs.variantValueHint,
                                                   prop_.msgs.warningInvalidVariantValue);
    };
    table_->on_pointer = [this, lifetime](lib::PointerEvent& event) {
        if (lifetime.expired() || event.button != lib::PointerButton::Secondary ||
            event.action != lib::PointerAction::Up)
            return;
        // DWTのポップアップ規則を維持する。行上で開いた場合はその行を
        // 対象とし、 空白部分で開いた場合は既存の複数選択を保持する。
        if (event.item_hit_test_available && event.item_index >= 0 &&
            static_cast<std::size_t>(event.item_index) < rows_.size())
            (void)lib::select_table_rows(*table_,
                {static_cast<std::size_t>(event.item_index)});
        showContextMenu(table_, event.position);
        event.handled = true;
    };
    table_->on_focus = std::move(gotFocus);
    // D版の FlagTable と同様、ダブルクリック（MListener.mouseDoubleClick
    // ）では変数ダイアログだけを開く。 汎用のダブルクリックによるセル
    // 編集も行うと、ダイアログの上にインライン編集欄が重なってしまう。
    table_->edit_on_double_click = false;
    if (!readOnly_) {
        // D版と同様、名称（NAME）には TableTextEdit
        // 、初期値（VALUE）には TableTCEdit
        // を使い、 選択済みの変数をクリックすると、押した列を
        // 編集する。 初期化（INIT）のコンボ編集は canEdit
        // が false のため無効とし、利用数には編集
        // 欄を設けない。 F2では引き続き名称を編集する
        // （inline_edit_column 0）。
        table_->inline_edit_column = 0U;
        auto pressed_column = std::make_shared<std::optional<std::size_t>>();
        const auto previous_pointer = table_->on_pointer;
        table_->on_pointer = [this, pressed_column, previous_pointer](lib::PointerEvent& event) {
            if (previous_pointer) previous_pointer(event);
            if (event.button == lib::PointerButton::Primary &&
                event.action == lib::PointerAction::Down && table_)
                *pressed_column = lib::hit_test_table_column(*table_, event.position.x);
        };
        const std::weak_ptr<lib::Widget> scope = component_;
        ::cwxeditor::cwx::editor::gui::dwt::dutils::installClickSelectedAction(
            table_, comm_.services,
            [this, pressed_column, scope](const std::size_t row) {
                const auto column = pressed_column->value_or(0U);
                if (column > 1U || !table_ || !table_->on_cell_edited ||
                    !comm_.services.gui)
                    return;
                lib::ProgrammaticInlineEditRequest request;
                request.table = table_;
                request.focus_scope = scope.lock();
                request.row_height = lib::scaled_ui_metric(lib::kEditorUiMetrics.row_height);
                request.matches_stable_row = [row](const std::size_t index) {
                    return index == row;
                };
                // プログラムからの表編集は inline_edit_column
                // を使うため、 今回の要求に限って押された列を設定する。
                table_->inline_edit_column = column;
                (void)comm_.services.gui->request_programmatic_inline_edit(request);
                if (table_) table_->inline_edit_column = 0U;
            },
            [this] { return prop_.etc.editTriggerQuick; }, {}, std::nullopt);
    }
    // D版の FlagTable と同様、変数名を対象にインクリメンタル検索する。
    std::vector<incsearch::AdditionMatcher> matchers;
    for (const auto& entry : std::vector<std::pair<String, VariableKind>>{
             {prop_.msgs.variant, VariableKind::Variant},
             {prop_.msgs.step, VariableKind::Step},
             {prop_.msgs.flag, VariableKind::Flag}}) {
        matchers.push_back({entry.first, [kind = entry.second](const std::any& data) {
            const auto value = std::any_cast<VariableKind>(&data);
            return value && *value == kind;
        }});
    }
    incsearch::IncSearchSettings search_settings;
    search_settings.startIncrementalSearchWhenKeyDown = prop_.etc.startIncrementalSearchWhenKeyDown;
    search_settings.startIncrementalSearchWhenKeyDownProvider =
        prop_.etc.startIncrementalSearchWhenKeyDownProvider;
    search_settings.incrementalSearchBoxWidth = prop_.etc.incrementalSearchBoxWidth;
    search_settings.incrementalSearchType = prop_.etc.incrementalSearchType;
    incSearch_ = std::make_shared<incsearch::IncSearch>(
        comm_.services, incSearchParent ? static_cast<lib::Widget*>(incSearchParent.get())
                                      : static_cast<lib::Widget*>(table_.get()),
        [this, lifetime] { return !lifetime.expired() && dir_ != nullptr; },
        std::move(matchers), search_settings);
    incSearch_->searchTypeChanged = [this, lifetime](incsearch::SearchType type) {
        if (lifetime.expired()) return;
        prop_.etc.incrementalSearchType = static_cast<std::int32_t>(type);
        prop_.synchronize();
        const auto store = prop_.etc.storeIncrementalSearchType;
        if (store) store(static_cast<std::int32_t>(type));
    };
    incSearch_->modEvent.push_back([this, lifetime] {
        if (lifetime.expired() || !incSearch_) return;
        search_ = incSearch_->searchText();
        refresh();
    });
    {
        const auto previous_key = table_->on_key_down;
        table_->on_key_down = [this, lifetime, previous_key](lib::KeyEvent& event) {
            if (!lifetime.expired() && event.key == lib::Key::I &&
                event.modifiers == lib::Modifiers::Control) {
                incSearch(); event.handled = true; return;
            }
            if (!lifetime.expired() && incSearch_ &&
                incSearch_->handleHostKeyDown(event, table_.get())) {
                event.handled = true;
                return;
            }
            if (previous_key) previous_key(event);
        };
        const auto previous_text = table_->on_text_input;
        table_->on_text_input = [this, lifetime, previous_text](lib::TextInputEvent& event) {
            if (!lifetime.expired() && incSearch_ &&
                incSearch_->handleHostTextInput(event, table_.get())) {
                event.handled = true;
                return;
            }
            if (previous_text) previous_text(event);
        };
    }
    component_->add(table_);
    if (parent) parent->add(component_);
    if (comm_.services.drag_drop) {
        const auto drag_lifetime = std::weak_ptr<int>(callback_lifetime_);
        lib::DragSourceBinding source;
        source.source = table_;
        source.allowed = lib::DropOperation::Copy | lib::DropOperation::Move;
        source.origin.pane_id = drag_pane_id_;
        source.provide_data = [this, drag_lifetime] {
            if (drag_lifetime.expired()) return lib::ClipboardData{};
            active_drag_selection_.reset();
            active_drag_consumed_ = false;
            if (!dir_ || selectedIndices_.empty()) return lib::ClipboardData{};
            DragSelection selection{dir_, selectionFlags(), selectionSteps(),
                                    selectionVariants()};
            if (selection.flags.empty() && selection.steps.empty() &&
                selection.variants.empty())
                return lib::ClipboardData{};
            const auto xml = panePayload(flag::getXML(
                 selection.directory,
                 std::span<const FlagPtr>(selection.flags.data(), selection.flags.size()),
                 std::span<const StepPtr>(selection.steps.data(), selection.steps.size()),
                 std::span<const VariantPtr>(selection.variants.data(), selection.variants.size())),
                 drag_pane_id_);
            if (xml.empty()) return lib::ClipboardData{};
            active_drag_selection_ = std::move(selection);
            return transfer::makeXMLPayload(xml);
        };
        source.provide_origin = [this, drag_lifetime] {
            lib::DragOrigin origin;
            if (drag_lifetime.expired()) return origin;
            origin.pane_id = drag_pane_id_;
            origin.selected_keys.reserve(selectedIndices_.size());
            for (const auto& value : selectedObjects())
                origin.selected_keys.push_back(objectPath(value));
            origin.cursor_index = selectedIndices_.empty()
                ? std::optional<std::size_t>{}
                : std::optional<std::size_t>{selectedIndices_.front()};
            return origin;
        };
        source.completed = [this, drag_lifetime](const lib::DropOperation operation) {
            if (drag_lifetime.expired()) return;
            auto selection = std::move(active_drag_selection_);
            const bool delete_source = selection && !active_drag_consumed_ &&
                !readOnly_ && hasDropOperation(operation, lib::DropOperation::Move);
            active_drag_consumed_ = false;
            if (!delete_source) return;
            beginMutation();
            for (const auto& value : selection->flags) if (value && value->parent()) {
                if (uc_) uc_->deleteID(usecounter::toFlagId(value->path()));
                value->parent()->remove(value);
            }
            for (const auto& value : selection->steps) if (value && value->parent()) {
                if (uc_) uc_->deleteID(usecounter::toStepId(value->path()));
                value->parent()->remove(value);
            }
            for (const auto& value : selection->variants) if (value && value->parent()) {
                if (uc_) uc_->deleteID(usecounter::toVariantId(value->path()));
                value->parent()->remove(value);
            }
            commitMutation();
            refresh();
            deleteEvent.call();
            comm_.delFlagAndStep.call(selection->flags, selection->steps,
                                      selection->variants);
            notifyRefresh();
        };
        comm_.services.drag_drop->bind_source(std::move(source));
        if (!readOnly_) {
            lib::DropTargetBinding target;
            target.target = table_;
            target.pane_id = drag_pane_id_;
            target.accepted_formats = {String(transfer::XML_BYTES_FORMAT),
                                       String(transfer::XML_BYTES_MIME)};
            const auto can_accept = [this] {
                const auto summary = comm_.summary.lock();
                return !local_ || !summary || !summary->legacy();
            };
            const auto accept = [this, drag_lifetime, can_accept](lib::DragEvent& event) {
                String xml;
                event.selected = !drag_lifetime.expired() && can_accept() && !event.same_pane() &&
                    transfer::extractXML(event.data, xml) && !xml.empty() &&
                    !payloadFromPane(xml, drag_pane_id_)
                    ? lib::DropOperation::Copy : lib::DropOperation::None;
            };
            target.on_enter = accept;
            target.on_over = accept;
            target.on_drop = [this, drag_lifetime, can_accept](lib::DragEvent& event) {
                String xml;
                const bool copied = !drag_lifetime.expired() && can_accept() && !event.same_pane() &&
                    transfer::extractXML(event.data, xml) &&
                    !payloadFromPane(xml, drag_pane_id_) && pasteXML(xml);
                event.selected = copied ? lib::DropOperation::Copy
                                        : lib::DropOperation::None;
                event.handled = copied;
            };
            comm_.services.drag_drop->bind_target(std::move(target));
        }
    }
    updateIncSearchParent(incSearchParent);
    refresh();
    return component_;
}

// 一つの共有検索コントローラーを、両方の兄弟ビューが使う仕切りの親に対応させる。
void FlagTable::updateIncSearchParent(const Parent& parent) {
    incSearchParent_ = parent;
    if (incSearch_)
        incSearch_->parent(incSearchParent_ ? static_cast<lib::Widget*>(incSearchParent_.get())
                                          : static_cast<lib::Widget*>(table_.get()));
}

void FlagTable::rebuildContextMenu() {
    // D版の FlagTable.createControl のポップアップと同様、 ラベル
    // 、アクセスキー、ショートカット、アイコンは共通の MenuID 表示設定から取得する。
    if (!context_menu_) context_menu_ = std::make_shared<lib::Menu>(true);
    const auto text = [this](const types::MenuID id, String fallback) {
        if (comm_.menuText) {
            if (auto value = comm_.menuText(id); !value.empty()) return value;
        }
        return fallback;
    };
    const auto image = [this](const types::MenuID id) -> lib::ResourceId {
        return comm_.menuImage ? comm_.menuImage(id) : lib::ResourceId{};
    };
    const auto item = [&text, &image](const types::MenuID id, String fallback,
                                      String command, const bool enabled) {
        lib::MenuItem value;
        value.text = text(id, std::move(fallback));
        value.command = std::move(command);
        value.icon = image(id);
        value.enabled = enabled;
        return value;
    };
    const auto contentItem = [this](const types::CType type, String label,
                                    String command, const bool enabled) {
        lib::MenuItem value;
        value.text = std::move(label);
        value.command = std::move(command);
        value.icon = comm_.contentImage ? comm_.contentImage(type) : lib::ResourceId{};
        value.enabled = enabled;
        return value;
    };
    const auto contentName = [this](const types::CType type, String fallback) {
        if (comm_.contentName) {
            if (auto value = comm_.contentName(type); !value.empty()) return value;
        }
        return fallback;
    };
    const auto separator = [] {
        lib::MenuItem value;
        value.separator = true;
        return value;
    };
    const auto summary = comm_.summary.lock();
    const bool legacy = summary && summary->legacy();
    const bool hasSelection = !selectedIndices_.empty();
    // D版と同様、検索可否は絞り込み後の行ではなく、ディレクトリの内容から判定する。
    const bool hasVariables = dir_ &&
        (dir_->hasFlag() || dir_->hasStep() || dir_->hasVariant());
    const bool hasFlags = !selectionFlags().empty();
    const bool hasSteps = !selectionSteps().empty();

    std::vector<lib::MenuItem> items;
    items.push_back(item(types::MenuID::IncSearch, "絞り込み検索(&N)\tCtrl+I",
                         "flag.inc-search", hasVariables));
    items.push_back(separator());
    if (readOnly_) {
        items.push_back(item(types::MenuID::ShowProp, "プロパティの表示", "flag.edit", canEdit()));
    } else {
        items.push_back(item(types::MenuID::EditProp, "編集(&E)...\tEnter", "flag.edit", canEdit()));
        items.push_back(separator());
        items.push_back(item(types::MenuID::NewFlag, "フラグの作成(&F)...\tCtrl+L", "flag.new-flag",
                             dir_ != nullptr && (!local_ || !legacy)));
        items.push_back(item(types::MenuID::NewStep, "ステップの作成(&S)...\tCtrl+P", "flag.new-step",
                             dir_ != nullptr && (!local_ || !legacy)));
        items.push_back(item(types::MenuID::NewVariant, "コモンの作成(&C)...\tCtrl+Shift+O",
                             "flag.new-variant", canCreateVariant()));
        items.push_back(separator());
        items.push_back(item(types::MenuID::Comment, "コメントを記述(&M)\tCtrl+M", "flag.comment",
                             canWriteComment()));
        items.push_back(separator());
        items.push_back(item(types::MenuID::Undo, "元に戻す(&U)\tCtrl+Z", "flag.undo",
                             undo_ && undo_->canUndo()));
        items.push_back(item(types::MenuID::Redo, "やり直し(&R)\tCtrl+Y", "flag.redo",
                             undo_ && undo_->canRedo()));
    }
    items.push_back(separator());
    // D の appendMenuTCPD と同じく、切り取り・コピー・貼り付け・削除、区切り線、複製の順に並べる。
    items.push_back(item(types::MenuID::Cut, "切り取り(&T)\tCtrl+X", "flag.cut", canDoT()));
    items.push_back(item(types::MenuID::Copy, "コピー(&C)\tCtrl+C", "flag.copy", canDoC()));
    items.push_back(item(types::MenuID::Paste, "貼り付け(&P)\tCtrl+V", "flag.paste", canDoP()));
    items.push_back(item(types::MenuID::Delete, "削除(&D)\tDelete", "flag.delete", canDoD()));
    items.push_back(separator());
    items.push_back(item(types::MenuID::Clone, "複製(&L)", "flag.clone", canDoClone()));
    items.push_back(separator());
    items.push_back(item(types::MenuID::SelectAll, "全て選択(&A)\tCtrl+A", "flag.select-all",
                         canSelectAll()));
    items.push_back(separator());

    auto events = item(types::MenuID::CreateVariableEventTree,
                       "イベントツリーとしてコピー(&E)", "flag.events", hasSelection);
    events.children.push_back(item(types::MenuID::InitVariablesTree, "変数を初期化",
                                   "flag.event.init", hasSelection));
    events.children.push_back(separator());
    events.children.push_back(contentItem(
        types::CType::ReverseFlag,
        contentName(types::CType::ReverseFlag, "フラグ反転") + "(&R)",
        "flag.event.flag-reverse", hasFlags));
    events.children.push_back(separator());
    events.children.push_back(contentItem(types::CType::SetFlag, "TRUEを設定(&T)",
                                          "flag.event.flag-on", hasFlags));
    events.children.push_back(contentItem(types::CType::SetFlag, "FALSEを設定(&F)",
                                          "flag.event.flag-off", hasFlags));
    events.children.push_back(separator());
    events.children.push_back(contentItem(
        types::CType::SetStepUp,
        contentName(types::CType::SetStepUp, "ステップ増加") + "(&U)",
        "flag.event.step-up", hasSteps));
    events.children.push_back(contentItem(
        types::CType::SetStepDown,
        contentName(types::CType::SetStepDown, "ステップ減少") + "(&D)",
        "flag.event.step-down", hasSteps));
    events.children.push_back(separator());
    for (std::size_t value = 0; value < prop_.looks.stepMaxCount; ++value) {
        const auto valueName = structs::parseDollarParams(
            prop_.etc.stepValueName, {{'N', std::to_string(value)}});
        auto label = prop_.msgs.setStepValue;
        auto marker = label.find("%1$s");
        if (marker != String::npos) label.replace(marker, 4U, valueName);
        else if ((marker = label.find("%s")) != String::npos)
            label.replace(marker, 2U, valueName);
        else label = valueName + label;
        if (value < 10U) label += "(&" + std::to_string(value) + ")";
        events.children.push_back(contentItem(
            types::CType::SetStep, std::move(label),
            "flag.event.step-value." + std::to_string(value), hasSteps));
    }
    items.push_back(std::move(events));
    if (!readOnly_) {
        items.push_back(separator());
        items.push_back(item(types::MenuID::FindID, "参照を検索(&I)...\tCtrl+Shift+F",
                             "flag.find-id", canReplaceID()));
    }
    items.push_back(separator());
    items.push_back(item(types::MenuID::CopyVariablePath, "状態変数のパスをコピー(&V)",
                         "flag.copy-path", canCopyVariablePath()));
    context_menu_->items = std::move(items);
    ++context_menu_->item_update_revision;
    ++context_menu_->item_order_revision;
    const auto lifetime = std::weak_ptr<int>(callback_lifetime_);
    context_menu_->on_command = [this, lifetime](const StringView command) {
        if (lifetime.expired()) return;
        SelectionEvent event{};
        if (command == "flag.inc-search") incSearch();
        else if (command == "flag.edit") edit();
        else if (command == "flag.new-flag") createFlag();
        else if (command == "flag.new-step") createStep();
        else if (command == "flag.new-variant") createVariant();
        else if (command == "flag.comment") writeComment();
        else if (command == "flag.undo") undo();
        else if (command == "flag.redo") redo();
        else if (command == "flag.cut") cut(event);
        else if (command == "flag.copy") copy(event);
        else if (command == "flag.paste") paste(event);
        else if (command == "flag.delete") del(event);
        else if (command == "flag.clone") clone(event);
        else if (command == "flag.select-all") selectAll();
        else if (command == "flag.event.init") copyInitTree();
        else if (command == "flag.event.flag-on") copyFlagTree(true);
        else if (command == "flag.event.flag-off") copyFlagTree(false);
        else if (command == "flag.event.flag-reverse") copyFlagReverseTree();
        else if (command == "flag.event.step-up") copyStepUpTree();
        else if (command == "flag.event.step-down") copyStepDownTree();
        else if (command.starts_with("flag.event.step-value.")) {
            const auto value = command.substr(StringView{"flag.event.step-value."}.size());
            try {
                copyStepTree(static_cast<std::uint32_t>(std::stoul(String(value))));
            } catch (...) {
                return;
            }
        }
        else if (command == "flag.find-id") replaceID();
        else if (command == "flag.copy-path") copyVariablePath();
    };
}

void FlagTable::incSearch() {
    const auto table = table_;
    const auto search = incSearch_;
    const auto directory = dir_;
    const auto directory_root = directory ? directory->root() : FlagDirPtr{};
    const auto summary = comm_.summary.lock();
    const auto use_counter = uc_;
    const std::weak_ptr<int> life = callback_lifetime_;
    if (!search || !table || disposed_) return;
    const auto services = comm_.services;
    if (services.gui) dutils::forceFocus(table, true, services);
    if (life.expired() || disposed_ || table_ != table || incSearch_ != search ||
        dir_ != directory || comm_.summary.lock() != summary || uc_ != use_counter ||
        (directory && directory->root() != directory_root)) return;
    search->startIncSearch();
}

bool FlagTable::canSelectAll() const noexcept {
    return !rows_.empty() && selectedIndices_.size() != rows_.size();
}

void FlagTable::selectAll() {
    if (!table_ || rows_.empty()) return;
    std::vector<std::size_t> all(rows_.size());
    for (std::size_t index = 0; index < all.size(); ++index) all[index] = index;
    (void)lib::select_table_rows(*table_, all);
    if (table_->on_selection_changed) table_->on_selection_changed(all);
}

bool FlagTable::canWriteComment() const noexcept {
    return !readOnly_ && !selectedIndices_.empty();
}

void FlagTable::writeComment() {
    if (!canWriteComment()) return;
    const auto life = std::weak_ptr<int>(callback_lifetime_);
    const auto stopOpen = comm_.stopOpen;
    const auto objects = selectedObjects();
    enterEdit();
    if (life.expired()) return;
    for (const auto& object : objects) {
        if (life.expired() || disposed_) return;
        if (stopOpen && stopOpen()) {
            if (life.expired()) return;
            break;
        }
        if (life.expired() || disposed_) return;
        std::visit([this](const auto& value) {
            if (!value) return;
            const void* key = value.get();
            if (const auto found = commentDialogs_.find(key);
                found != commentDialogs_.end()) {
                if (const auto holder = std::static_pointer_cast<CommentHolder>(found->second);
                    holder && holder->dialog)
                    holder->dialog->active();
                return;
            }
            auto holder = std::make_shared<CommentHolder>();
            holder->comm.services = comm_.services;
            holder->comm.prop.services = comm_.services;
            if (comm_.editorProps) {
                if (auto* properties = comm_.editorProps())
                    comment::bindEditorImages(holder->comm.prop, *properties);
            }
            holder->comm.prop.bindDialogSize = [get=comm_.editorProps, life=std::weak_ptr(callback_lifetime_)](const std::shared_ptr<lib::Window>& window) -> std::function<void()> {
                if (life.expired() || !get) return {};
                auto* properties = get();
                return !properties ? std::function<void()>{} : properties->bindDialogSize(window, properties->flex().commentDlg);
            };
            holder->dialog = std::make_shared<comment::CommentDialog>(
                holder->comm, comm_.dialogOwner.lock(), value->comment());
            const std::weak_ptr<int> lifetime = callback_lifetime_;
            const auto target = value;
            const std::weak_ptr<CommentHolder> weakHolder = holder;
            const auto refreshTitle = [weakHolder, target, titleFor = comm_.commentTitle] {
                const auto current = weakHolder.lock();
                if (!current || !current->dialog) return;
                if (titleFor) current->dialog->title = titleFor(target->name());
                else {
                    auto title = String{"[ %1$s ] へのコメント"};
                    title.replace(title.find("%1$s"), 4U, target->name());
                    current->dialog->title = std::move(title);
                }
            };
            refreshTitle();
            // D版と同様、各コメントウィンドウをモデルオブジェクトそのものに結び付ける
            // 。 別のディレクトリにある同名変数によって、更新したり閉じたりしない。
            const auto containsTarget = [key](const auto& flags, const auto& steps,
                                               const auto& variants) {
                const auto contains = [key](const auto& values) {
                    return std::any_of(values.begin(), values.end(),
                        [key](const auto& item) { return item.get() == key; });
                };
                return contains(flags) || contains(steps) || contains(variants);
            };
            auto references = comm_.refFlagAndStep;
            auto deletions = comm_.delFlagAndStep;
            auto text = comm_.refreshText;
            const auto refToken = references.connect(
                [containsTarget, refreshTitle](const auto& flags, const auto& steps,
                                               const auto& variants) {
                    if (containsTarget(flags, steps, variants)) refreshTitle();
                });
            const auto delToken = deletions.connect(
                [weakHolder, containsTarget](const auto& flags, const auto& steps,
                                            const auto& variants) {
                    if (!containsTarget(flags, steps, variants)) return;
                    if (const auto current = weakHolder.lock(); current && current->dialog)
                        current->dialog->forceCancel();
                });
            const auto textToken = text.connect(refreshTitle);
            // 複製した Signal はイベント発生元を共有する。表の破棄時に forceCancel()
            // より先に 終了コールバックが消される場合も、RAIIによって接続を解除する。
            holder->disconnect = [references, deletions, text, refToken, delToken,
                                  textToken]() mutable {
                references.remove(refToken);
                deletions.remove(delToken);
                text.remove(textToken);
            };
            holder->dialog->applied = [this, lifetime, target](const StringView next) {
                if (lifetime.expired() || !target || !dir_) return;
                // 切り離された変数や、同名の別オブジェクトに置換された変数は、適用先として扱わない。
                const auto parent = target->parent();
                if (!parent || parent->indexOf(*target) < 0 ||
                    parent->root().get() != dir_->root().get() ||
                    target->comment() == next) return;
                beginMutation();
                target->comment(String(next));
                commitMutation();
                refresh();
                notifyRefresh();
            };
            holder->dialog->closed = [this, lifetime, key] {
                if (!lifetime.expired()) commentDialogs_.erase(key);
            };
            commentDialogs_[key] = holder;
            (void)holder->dialog->open();
        }, object);
        if (life.expired()) return;
    }
}

namespace {
[[nodiscard]] String variableEventTreeXML(
    const FlagList& flags, const StepList& steps, const VariantList& variants,
    const event::CType flagType, const event::CType stepType,
    const event::CType variantType,
    const std::function<void(event::Content&, const FlagPtr&)>& flagValue,
    const std::function<void(event::Content&, const StepPtr&)>& stepValue,
    const bool variantValues) {
    event::ContentPtr root;
    event::ContentPtr tail;
    const auto append = [&root, &tail](const event::ContentPtr& value) {
        if (!value) return;
        if (!root) root = value;
        else tail->add(value);
        tail = value;
    };
    if (flagType != event::CType::Start) {
        for (const auto& value : flags) {
            if (!value) continue;
            auto content = std::make_shared<event::Content>(flagType);
            content->flag(value->path());
            if (flagValue) flagValue(*content, value);
            append(content);
        }
    }
    if (stepType != event::CType::Start) {
        for (const auto& value : steps) {
            if (!value) continue;
            auto content = std::make_shared<event::Content>(stepType);
            content->step(value->path());
            if (stepValue) stepValue(*content, value);
            append(content);
        }
    }
    if (variantType != event::CType::Start) {
        for (const auto& value : variants) {
            if (!value) continue;
            auto content = std::make_shared<event::Content>(variantType);
            content->variant(value->path());
            if (variantValues)
                content->expression(
                    expression::variantValueToText(expression::VariantVal(*value)));
            append(content);
        }
    }
    return root ? root->toXML() : String{};
}
}  // namespace

void FlagTable::copyFlagTree(const bool onOff) {
    const auto xml = variableEventTreeXML(
        selectionFlags(), {}, {}, event::CType::SetFlag, event::CType::Start,
        event::CType::Start,
        [onOff](event::Content& content, const FlagPtr&) { content.flagValue(onOff); },
        {}, false);
    if (!xml.empty()) writeXMLClipboard(xml);
    notifyRefresh();
}

void FlagTable::copyStepTree(const std::uint32_t value) {
    const auto xml = variableEventTreeXML(
        {}, selectionSteps(), {}, event::CType::Start, event::CType::SetStep,
        event::CType::Start, {},
        [value](event::Content& content, const StepPtr&) {
            content.stepValue(static_cast<int>(value));
        }, false);
    if (!xml.empty()) writeXMLClipboard(xml);
    notifyRefresh();
}

void FlagTable::copyInitTree() {
    // D版の createInitVariablesTree
    // と同様、選択した各変数をそれぞれ自身の初期値へ戻す。
    const auto tree = event::createInitVariablesTree(
        selectionFlags(), selectionSteps(), selectionVariants());
    const auto xml = tree ? tree->toXML() : String{};
    if (!xml.empty()) writeXMLClipboard(xml);
    notifyRefresh();
}

void FlagTable::copyFlagReverseTree() {
    const auto xml = variableEventTreeXML(
        selectionFlags(), {}, {}, event::CType::ReverseFlag, event::CType::Start,
        event::CType::Start, {}, {}, false);
    if (!xml.empty()) writeXMLClipboard(xml);
    notifyRefresh();
}

void FlagTable::copyStepUpTree() {
    const auto xml = variableEventTreeXML(
        {}, selectionSteps(), {}, event::CType::Start, event::CType::SetStepUp,
        event::CType::Start, {}, {}, false);
    if (!xml.empty()) writeXMLClipboard(xml);
    notifyRefresh();
}

void FlagTable::copyStepDownTree() {
    const auto xml = variableEventTreeXML(
        {}, selectionSteps(), {}, event::CType::Start, event::CType::SetStepDown,
        event::CType::Start, {}, {}, false);
    if (!xml.empty()) writeXMLClipboard(xml);
    notifyRefresh();
}

void FlagTable::showContextMenu(const lib::WidgetPtr& owner, const lib::Point position) {
    rebuildContextMenu();
    if (context_menu_ && comm_.services.gui)
        comm_.services.gui->show_context_menu(context_menu_, owner, position);
}

void FlagTable::dialogContext(
    const std::shared_ptr<::cwxeditor::cwx::summary::Summary>& summary,
    const std::shared_ptr<lib::Window>& owner) {
    comm_.summary = summary;
    comm_.dialogOwner = owner;
}

void FlagTable::refresh() { refreshImpl(nullptr); }

void FlagTable::refresh(const std::vector<VariableObject>& selected) {
    refreshImpl(&selected);
}

void FlagTable::refreshImpl(const std::vector<VariableObject>* selected) {
    const std::weak_ptr<int> life = callback_lifetime_;
    const auto directory = dir_;
    const auto use_counter = uc_;
    const auto undo = undo_;
    const auto gui = comm_.services.gui;
    const auto table = table_;
    const auto summary = comm_.summary.lock();
    const auto dialog_owner = comm_.dialogOwner.lock();
    const auto inc_search_parent = incSearchParent_;
    const bool has_explicit_selection = selected && !selected->empty();
    const auto selected_snapshot = selected
        ? *selected : std::vector<VariableObject>{};

    const auto still_current = [&] {
        if (life.expired()) return false;
        return !disposed_ && dir_ == directory && uc_ == use_counter &&
            undo_ == undo && comm_.services.gui == gui && table_ == table &&
            comm_.summary.lock() == summary &&
            comm_.dialogOwner.lock() == dialog_owner &&
            incSearchParent_ == inc_search_parent;
    };
    // D版のrefreshと同様、既に編集中のセルだけを
    // 確定する。保留中のプロパティ画面要求は別に扱い、
    // 行の更新やセル確定中の再入更新では開き直さない。
    const bool cell_edit_active = gui && table &&
        gui->programmatic_inline_edit_active(table);
    if (!still_current()) return;
    if (cell_edit_active) enterEdit();
    if (!still_current()) return;

    const auto previous = has_explicit_selection
                              ? selected_snapshot : selectedObjects();
    std::vector<VariableRow> rows;
    std::vector<std::size_t> selected_indices;
    if (dir_) {
        auto variants = dir_->variants();
        auto steps = dir_->steps();
        auto flags = dir_->flags();
        const auto* live = comm_.editorProps ? comm_.editorProps() : nullptr;
        const bool logical = live ? live->var().logicalSort.value : prop_.etc.logicalSort;
        const auto sortByName = [logical](const auto& left, const auto& right) {
            return nameLess(left->name(), right->name(), logical);
        };
        std::stable_sort(variants.begin(), variants.end(), sortByName);
        std::stable_sort(steps.begin(), steps.end(), sortByName);
        std::stable_sort(flags.begin(), flags.end(), sortByName);
        for (const auto& value : variants) {
            if (!value) continue;
            addRow(rows, VariableKind::Variant, value, value->name(),
                   variantValueText(value),
                   initializationName(value->initialization(), prop_.msgs),
                   useCount(uc_, value));
        }
        for (const auto& value : steps) {
            if (!value) continue;
            addRow(rows, VariableKind::Step, value, value->name(), stepValueText(value),
                   initializationName(value->initialization(), prop_.msgs),
                   useCount(uc_, value));
        }
        for (const auto& value : flags) {
            if (!value) continue;
            addRow(rows, VariableKind::Flag, value, value->name(), flagValueText(value),
                   initializationName(value->initialization(), prop_.msgs),
                   useCount(uc_, value));
        }
    }
    for (std::size_t index = 0; index < rows.size(); ++index) {
        if (std::any_of(previous.begin(), previous.end(),
                        [&rows, index](const auto& object) {
                            return sameObject(rows[index].object, object);
                        })) {
            rows[index].selected = true;
            selected_indices.push_back(index);
        }
    }
    std::vector<lib::TableRow> table_rows;
    table_rows.reserve(rows.size());
    for (const auto& row : rows) {
        const auto icon = row.kind == VariableKind::Flag
                              ? prop_.flagImage
                              : row.kind == VariableKind::Step
                                    ? prop_.stepImage : prop_.variantImage;
        lib::ListItem name{row.name, row.name, icon, false, true};
        lib::ListItem value{row.value, row.value, {}, false, true};
        lib::ListItem initialization{row.initialization, row.initialization, {}, false, true};
        if (!readOnly_) {
            if (const auto* flag = std::get_if<FlagPtr>(&row.object); flag && *flag) {
                value.editor = lib::CellEditorKind::Choice;
                value.editor_choices = {(*flag)->on(), (*flag)->off()};
                value.editor_choice_values = {"flag:1", "flag:0"};
                value.editor_value = (*flag)->onOff() ? "flag:1" : "flag:0";
            } else if (const auto* step = std::get_if<StepPtr>(&row.object); step && *step) {
                value.editor = lib::CellEditorKind::Choice;
                for (std::uint32_t index = 0; index < (*step)->count(); ++index) {
                    value.editor_choices.push_back((*step)->getValue(index));
                    value.editor_choice_values.push_back(
                        "step:" + std::to_string(index));
                }
                value.editor_value = "step:" + std::to_string((*step)->select());
            }
            // ローカル変数の初期化タイミングは、変数ダイアログだ
            // けで編集する。 セル内のドロップダウンは使わない。
            if (local_) initialization.inline_editable = false;
            initialization.editor = lib::CellEditorKind::Choice;
            initialization.editor_choices = {
                prop_.msgs.variableInitializationShortNameBattleStart,
                prop_.msgs.variableInitializationShortNameBattleEnd,
                prop_.msgs.variableInitializationShortNameEventExit,
                prop_.msgs.variableInitializationShortNameLeave,
                prop_.msgs.variableInitializationShortNameNone};
        }
        table_rows.push_back({
            {{std::move(name), std::move(value), std::move(initialization),
              {std::to_string(row.useCount), std::to_string(row.useCount),
               {}, false, true}}},
            row.selected});
    }
    if (table_) {
        auto publication = lib::prepare_table_rows(
            table_, std::move(table_rows), selected_indices,
            selected && !selected->empty() && !selected_indices.empty()
                ? std::optional<std::size_t>{selected_indices.front()} : std::nullopt);
        if (!publication.commit()) return;
    }
    rows_.swap(rows);
    selectedIndices_.swap(selected_indices);
    const std::weak_ptr<int> status_life = callback_lifetime_;
    refreshStatusLine();
    if (status_life.expired()) return;
    // DWT は TableItem を同期的に更新する。移植版のモデルでは明
    // 示的な再描画が必要なので、 ディレクトリを選択したら表示行を置き換える。
    if (table_ && comm_.services.gui)
        comm_.services.gui->request_redraw(table_);
}

void FlagTable::addRow(std::vector<VariableRow>& rows, const VariableKind kind,
                       const VariableObject& object,
                       String name, String value, String initialization,
                       const std::uint32_t count) {
    VariableRow row{kind, object, std::move(name), std::move(value),
                    std::move(initialization), count, false};
    if (matchesSearch(row)) rows.push_back(std::move(row));
}

bool FlagTable::matchesSearch(const VariableRow& row) const {
    if (incSearch_) {
        const std::any kind = row.kind;
        return incSearch_->match(row.name, kind);
    }
    if (search_.empty()) return true;
    const auto fold = [this](const StringView text) {
        return comm_.services.text ? comm_.services.text->case_fold(text)
                                   : lower(String(text));
    };
    return fold(row.name).find(fold(search_)) != String::npos;
}

void FlagTable::refreshStatusLine() {
    String text;
    const auto append = [&](const String& name, const std::size_t count) {
        if (!count) return;
        if (!text.empty()) text += ' ';
        text += ::cwxeditor::cwx::utils::tryFormat(prop_.msgs.statusFormat, name, count);
    };
    if (dir_) {
        append(prop_.msgs.flag, dir_->flags().size());
        append(prop_.msgs.step, dir_->steps().size());
        append(prop_.msgs.variant, dir_->variants().size());
    }
    const auto selected_count = std::count_if(selectedIndices_.begin(), selectedIndices_.end(),
        [this](std::size_t index) { return index < rows_.size(); });
    if (selected_count)
        text = ::cwxeditor::cwx::utils::tryFormat(prop_.msgs.statusSelectedFormat, text, selected_count);
    statusLine_ = text;
    const auto publish = comm_.setStatusLine;
    const auto owner = table_;
    if (publish) publish(owner, text);
}

void FlagTable::refreshTableSelection(const bool notify) {
    if (!table_) return;
    auto table_rows = table_->rows;
    if (!lib::replace_table_rows(table_, std::move(table_rows),
                                 selectedIndices_, selectedIndices_.empty()
                                     ? std::nullopt
                                     : std::optional<std::size_t>{*std::min_element(
                                           selectedIndices_.begin(), selectedIndices_.end())})) return;
    for (std::size_t index = 0; index < rows_.size(); ++index)
        rows_[index].selected = std::find(selectedIndices_.begin(),
                                           selectedIndices_.end(), index) !=
                                 selectedIndices_.end();
    if (notify) notifySelection();
}

void FlagTable::notifySelection() {
    selectionChanged.call();
    if (comm_.refreshToolBar) comm_.refreshToolBar();
}

const std::vector<std::size_t>& FlagTable::selected() const noexcept {
    return selectedIndices_;
}

std::vector<std::size_t> FlagTable::selectedItems(
    FlagList& flags, StepList& steps, VariantList& variants) const {
    flags.clear();
    steps.clear();
    variants.clear();
    for (const auto index : selectedIndices_) {
        if (index >= rows_.size()) continue;
        std::visit(
            [&flags, &steps, &variants](const auto& value) {
                using Value = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Value, FlagPtr>) flags.push_back(value);
                else if constexpr (std::is_same_v<Value, StepPtr>) steps.push_back(value);
                else variants.push_back(value);
            },
            rows_[index].object);
    }
    return selectedIndices_;
}

FlagList FlagTable::selectionFlags() const {
    FlagList flags;
    StepList steps;
    VariantList variants;
    [[maybe_unused]] const auto indices = selectedItems(flags, steps, variants);
    return flags;
}

StepList FlagTable::selectionSteps() const {
    FlagList flags;
    StepList steps;
    VariantList variants;
    [[maybe_unused]] const auto indices = selectedItems(flags, steps, variants);
    return steps;
}

VariantList FlagTable::selectionVariants() const {
    FlagList flags;
    StepList steps;
    VariantList variants;
    [[maybe_unused]] const auto indices = selectedItems(flags, steps, variants);
    return variants;
}

std::vector<std::ptrdiff_t> FlagTable::selectionFlagIndices() const {
    std::vector<std::ptrdiff_t> result;
    for (const auto& value : selectionFlags())
        if (value && value->parent()) result.push_back(value->parent()->indexOf(*value));
    return result;
}

std::vector<std::ptrdiff_t> FlagTable::selectionStepIndices() const {
    std::vector<std::ptrdiff_t> result;
    for (const auto& value : selectionSteps())
        if (value && value->parent()) result.push_back(value->parent()->indexOf(*value));
    return result;
}

std::vector<std::ptrdiff_t> FlagTable::selectionVariantIndices() const {
    std::vector<std::ptrdiff_t> result;
    for (const auto& value : selectionVariants())
        if (value && value->parent()) result.push_back(value->parent()->indexOf(*value));
    return result;
}

void FlagTable::beginMutation() {
    if (!undo_ || !dir_) return;
    undo_->beginSnapshot(dir_->root());
    // D版の FTVUndo と同様、ディレクトリ操作だけでなく、すべての表操作で型付
    // きのインデックスを記録する。 ツリーも追従するよう、ペインの連携経路を優先する。
    const auto bridge = store_history_selection_;
    if (bridge && bridge(dir_)) return;
    struct SelectionState {
        std::vector<std::ptrdiff_t> flags, steps, variants;
        std::vector<std::ptrdiff_t> restoreFlags, restoreSteps, restoreVariants;
    };
    const auto state = std::make_shared<SelectionState>();
    state->flags = selectionFlagIndices();
    state->steps = selectionStepIndices();
    state->variants = selectionVariantIndices();
    const auto operationDir = dir_;
    const auto root = dir_->root();
    const auto life = std::weak_ptr<int>(callback_lifetime_);
    undo_->addSnapshotTraversal(
        [this, life, state](bool) {
            if (life.expired() || !table_) return;
            state->restoreFlags = std::move(state->flags);
            state->restoreSteps = std::move(state->steps);
            state->restoreVariants = std::move(state->variants);
            state->flags = selectionFlagIndices();
            state->steps = selectionStepIndices();
            state->variants = selectionVariantIndices();
        },
        [this, life, state, operationDir, root](bool) {
            if (life.expired() || !table_ || !dir_ || dir_->root() != root ||
                operationDir->root() != root) return;
            const auto gui = comm_.services.gui;
            const auto table = table_;
            // 現在のフォルダー内での選択復元は、フォルダー移動ではなく行の更新とし
            // て扱う。 無関係なモードレス画面の未確定の編集は開いたままにする。
            if (dir_ == operationDir) refresh();
            else setDir(operationDir);
            if (life.expired()) return;
            deselectAll();
            if (life.expired()) return;
            const auto selectIndices = [this, life](const auto& values, const auto& indices) {
                for (const auto index : indices) {
                    if (life.expired()) return;
                    if (index >= 0 && static_cast<std::size_t>(index) < values.size())
                        select(values[static_cast<std::size_t>(index)]);
                }
            };
            const auto flags = operationDir->flags();
            const auto steps = operationDir->steps();
            const auto variants = operationDir->variants();
            selectIndices(flags, state->restoreFlags);
            selectIndices(steps, state->restoreSteps);
            selectIndices(variants, state->restoreVariants);
            if (!life.expired() && gui) gui->focus(table);
        });
}

void FlagTable::commitMutation() {
    if (undo_ && dir_) undo_->commitSnapshot(dir_->root());
}

void FlagTable::createFlag() {
    if (readOnly_ || !dir_) return;
    editing_ = FlagPtr{};
    hasEditing_ = true;
    enterEdit();
}

void FlagTable::createStep() {
    if (readOnly_ || !dir_) return;
    editing_ = StepPtr{};
    hasEditing_ = true;
    enterEdit();
}

bool FlagTable::canCreateVariant() const {
    const auto summary = comm_.summary.lock();
    return !readOnly_ && dir_ && (!summary || !summary->legacy());
}

void FlagTable::createVariant() {
    if (!canCreateVariant()) return;
    editing_ = VariantPtr{};
    hasEditing_ = true;
    enterEdit();
}

void FlagTable::edit() {
    if (!canEdit()) return;
    const auto objects = selectedObjects();
    const auto life = std::weak_ptr<int>(callback_lifetime_);
    const auto stopOpen = comm_.stopOpen;
    for (const auto& object : objects) {
        if (life.expired() || disposed_) break;
        if (stopOpen && stopOpen()) {
            if (life.expired()) break;
            break;
        }
        if (life.expired() || disposed_) break;
        editing_ = object;
        hasEditing_ = true;
        enterEdit();
    }
}

void FlagTable::edit(const FlagPtr& value) {
    if (!value || value->parent().get() != dir_.get()) return;
    editing_ = value;
    hasEditing_ = true;
    enterEdit();
}

void FlagTable::edit(const StepPtr& value) {
    if (!value || value->parent().get() != dir_.get()) return;
    editing_ = value;
    hasEditing_ = true;
    enterEdit();
}

void FlagTable::edit(const VariantPtr& value) {
    if (!value || value->parent().get() != dir_.get()) return;
    editing_ = value;
    hasEditing_ = true;
    enterEdit();
}

bool FlagTable::canEdit() const noexcept {
    // D版と同様、読み取り専用ペインでも同じプロパティダイアログを表示し、コントロールを無
    // 効にする。 変更処理の各入口でも、それぞれ readOnly_ の検査を維持する。
    return !selectedIndices_.empty();
}

void FlagTable::setDir(const FlagDirPtr& dir, const bool forceRefresh) {
    if (!forceRefresh && dir_.get() == dir.get()) return;
    // D版の setDir と同様、フォルダーの変更や強制更新では、プロパティ・コメ
    // ントの未確定の編集をすべてキャンセルする。 同じシナリオ内の移動も対象にする。
    const auto life = std::weak_ptr<int>(callback_lifetime_);
    cancelEdit();
    if (life.expired()) return;
    closePropertySessions();
    if (life.expired()) return;
    closeCommentDialogs();
    if (life.expired()) return;
    dir_ = dir;
    refresh();
}

bool FlagTable::selectedObject(const VariableObject& object) const {
    return std::any_of(selectedIndices_.begin(), selectedIndices_.end(),
                       [this, &object](const std::size_t index) {
                           return index < rows_.size() &&
                                  sameObject(rows_[index].object, object);
                       });
}

std::vector<VariableObject> FlagTable::selectedObjects() const {
    std::vector<VariableObject> result;
    for (const auto index : selectedIndices_)
        if (index < rows_.size()) result.push_back(rows_[index].object);
    return result;
}

bool FlagTable::selectObject(const VariableObject& object,
                             const bool deselect) {
    if (deselect) selectedIndices_.clear();
    for (std::size_t index = 0; index < rows_.size(); ++index) {
        if (!sameObject(rows_[index].object, object)) continue;
        if (std::find(selectedIndices_.begin(), selectedIndices_.end(), index) ==
            selectedIndices_.end())
            selectedIndices_.push_back(index);
        refreshTableSelection(true);
        return true;
    }
    // D版と同様、絞り込みで対象が見えなくなった場合は、戻る前に選択解除を通知する。
    if (deselect) refreshTableSelection(true);
    return false;
}

void FlagTable::select(const FlagPtr& value, const bool deselect) {
    if (value) (void)selectObject(value, deselect);
}

void FlagTable::select(const StepPtr& value, const bool deselect) {
    if (value) (void)selectObject(value, deselect);
}

void FlagTable::select(const VariantPtr& value, const bool deselect) {
    if (value) (void)selectObject(value, deselect);
}

void FlagTable::deselectAll() {
    selectedIndices_.clear();
    const std::weak_ptr<int> status_life = callback_lifetime_;
    refreshTableSelection(true);
    if (!status_life.expired()) refreshStatusLine();
}

bool FlagTable::canCopyVariablePath() const noexcept {
    return !selectedIndices_.empty();
}

void FlagTable::writeXMLClipboard(StringView xml) {
    if (const auto clipboard = comm_.clipboard()) {
        transfer::ClipData data(*clipboard);
        transfer::XMLtoCB(comm_.xmlCopyProvider && comm_.xmlCopyProvider(), data, xml);
    }
}

void FlagTable::writeClipboard(String text) {
    if (const auto clipboard = comm_.clipboard())
        clipboard->write({std::move(text), {}, {}});
}

String FlagTable::readClipboard() const {
    if (const auto clipboard = comm_.clipboard()) return clipboard->read_text();
    return {};
}

String FlagTable::selectedXML() const {
    if (!dir_) return {};
    FlagList flags;
    StepList steps;
    VariantList variants;
    for (const auto& value : selectedObjects()) {
        if (const auto* item = std::get_if<FlagPtr>(&value)) {
            if (*item) flags.push_back(*item);
        } else if (const auto* item = std::get_if<StepPtr>(&value)) {
            if (*item) steps.push_back(*item);
        } else if (const auto* item = std::get_if<VariantPtr>(&value); item && *item) {
            variants.push_back(*item);
        }
    }
    if (flags.empty() && steps.empty() && variants.empty()) return {};
    const auto xml = panePayload(flag::getXML(
        dir_, std::span<const FlagPtr>(flags.data(), flags.size()),
        std::span<const StepPtr>(steps.data(), steps.size()),
        std::span<const VariantPtr>(variants.data(), variants.size())),
        drag_pane_id_);
    if (xml.empty()) return {};
    return xml;
}

bool FlagTable::copySelectionToMemory() {
    const auto xml = selectedXML();
    if (xml.empty()) return false;
    const auto xmlCopy = comm_.xmlCopyProvider && comm_.xmlCopyProvider();
    if (const auto clipboard = comm_.clipboard()) {
        transfer::ClipData data(*clipboard);
        transfer::XMLtoCB(xmlCopy, data, xml);
        return true;
    }
    return false;
}

void FlagTable::consumeActiveDragSelection() noexcept {
    if (active_drag_selection_) active_drag_consumed_ = true;
}

void FlagTable::copyVariablePath() {
    if (!canCopyVariablePath()) return;
    std::vector<String> paths;
    for (const auto& value : selectedObjects()) paths.push_back(objectPath(value));
    writeClipboard(joinPaths(paths));
    if (comm_.refreshToolBar) comm_.refreshToolBar();
}

void FlagTable::copy(SelectionEvent& event) {
    if (!dir_ || selectedIndices_.empty()) return;
    event.handled=copySelectionToMemory();
    if (comm_.refreshToolBar) comm_.refreshToolBar();
}

void FlagTable::cut(SelectionEvent& event) {
    if (readOnly_) return;
    copy(event);
    if (event.handled) del(event);
}

void FlagTable::paste(SelectionEvent& event) {
    if (readOnly_ || !dir_) return;
    const auto clipboard = comm_.clipboard();
    if (!clipboard) return;
    const auto xml = transfer::CBtoXML(*clipboard);
    if (xml.empty()) return;
    event.handled = pasteXML(xml);
}

bool FlagTable::pasteXML(const StringView xml) {
    if (readOnly_ || !dir_ || xml.empty()) return false;
    const String stableXml(xml);
    const auto target_dir = dir_;
    // 編集中のセルを確定する前に検証する。不正なXMLや、オブジェク
    // トを一つも追加しないデータでは、 未確定の編集に手を加えない。
    auto staged_dir = std::make_shared<flag::FlagDir>(
        flag::CwxPathPtr{}, *target_dir);
    std::unordered_map<String, FlagPtr> staged_flags;
    std::unordered_map<String, StepPtr> staged_steps;
    std::unordered_map<String, VariantPtr> staged_variants;
    String staged_new_path;
    String staged_root_id;
    const auto staged_result = staged_dir->appendFromXML(
        stableXml, system::XMLInfo(flagEditorSystem(), versioninfo::LATEST_VERSION),
        true, false, local_, staged_flags, staged_steps, staged_variants,
        staged_new_path, staged_root_id);
    if (staged_result == flag::FlagDir::AppendXmlResult::FAIL ||
        (staged_flags.empty() && staged_steps.empty() && staged_variants.empty()))
        return false;

    const auto life = std::weak_ptr<int>(callback_lifetime_);
    const auto gui = comm_.services.gui;
    const auto table = table_;
    if (gui && table) (void)gui->finish_programmatic_inline_edit(table, true);
    if (life.expired() || disposed_ || dir_ != target_dir) return false;
    std::unordered_map<String, FlagPtr> copiedFlags;
    std::unordered_map<String, StepPtr> copiedSteps;
    std::unordered_map<String, VariantPtr> copiedVariants;
    String newPath;
    String rootId;
    beginMutation();
    const auto result = dir_->appendFromXML(
        stableXml, system::XMLInfo(flagEditorSystem(), versioninfo::LATEST_VERSION),
        true, false, local_, copiedFlags, copiedSteps, copiedVariants, newPath, rootId);
    if (result == flag::FlagDir::AppendXmlResult::FAIL ||
        (copiedFlags.empty() && copiedSteps.empty() && copiedVariants.empty())) {
        if (undo_) undo_->discardSnapshot();
        return false;
    }
    commitMutation();
    std::vector<VariableObject> added;
    for (const auto& entry : copiedFlags) if (entry.second) {
        if (uc_) uc_->createID(usecounter::toFlagId(entry.second->path()));
        added.emplace_back(entry.second);
    }
    for (const auto& entry : copiedSteps) if (entry.second) {
        if (uc_) uc_->createID(usecounter::toStepId(entry.second->path()));
        added.emplace_back(entry.second);
    }
    for (const auto& entry : copiedVariants) if (entry.second) {
        if (uc_) uc_->createID(usecounter::toVariantId(entry.second->path()));
        added.emplace_back(entry.second);
    }
    if (added.empty()) return false;
    refresh(added);
    createEvent.call();
    if (life.expired() || disposed_) return true;
    comm_.refFlagAndStep.call(selectionFlags(), selectionSteps(),
                              selectionVariants());
    if (life.expired() || disposed_) return true;
    notifyRefresh();
    return true;
}

void FlagTable::del(SelectionEvent& event) {
    if (readOnly_ || !dir_ || selectedIndices_.empty()) return;
    const auto deleting = deletion_in_progress_;
    if (!deleting || *deleting) return;
    *deleting = true;
    struct DeleteGuard final {
        std::shared_ptr<bool> state;
        ~DeleteGuard() { if (state) *state = false; }
    } delete_guard{deleting};

    const std::weak_ptr<int> lifetime = callback_lifetime_;
    const auto operation_dir = dir_;
    const auto operation_root = operation_dir->root();
    if (!operation_root) return;
    const auto can_continue = [this, lifetime] {
        return !lifetime.expired() && !disposed_;
    };
    const auto selected = selectedObjects();
    if (selected.empty()) return;
    beginMutation();
    FlagList removedFlags;
    StepList removedSteps;
    VariantList removedVariants;
    for (const auto& value : selected) {
        std::visit(
            [this, operation_dir, &removedFlags, &removedSteps, &removedVariants](const auto& item) {
                using Value = std::decay_t<decltype(item)>;
                if (!item) return;
                if constexpr (std::is_same_v<Value, FlagPtr>) {
                    if (uc_) uc_->deleteID(usecounter::toFlagId(item->path()));
                    operation_dir->remove(item);
                    removedFlags.push_back(item);
                } else if constexpr (std::is_same_v<Value, StepPtr>) {
                    if (uc_) uc_->deleteID(usecounter::toStepId(item->path()));
                    operation_dir->remove(item);
                    removedSteps.push_back(item);
                } else {
                    if (uc_) uc_->deleteID(usecounter::toVariantId(item->path()));
                    operation_dir->remove(item);
                    removedVariants.push_back(item);
                }
            },
            value);
    }
    event.handled = true;
    deleteEvent.call();
    if (!can_continue()) return;
    if (undo_) undo_->commitSnapshot(operation_root);
    if (!can_continue()) return;
    comm_.delFlagAndStep.call(removedFlags, removedSteps, removedVariants);
    if (!can_continue()) return;
    // D版と同様、TableItem の再構築前に削除を通知する。 そのため
    // MainWindow の削除通知経路は、この更新で切り離し済みの行が
    // 表のモデルから除かれるまで refUseCount を遅延させる。
    refresh();
    if (!can_continue()) return;
    if (comm_.refreshToolBar) comm_.refreshToolBar();
    if (!can_continue()) return;
    // D は所有テーブルの行を更新した後、共有 Commons に通知する。
    if (comm_.publishUseCount)
        comm_.publishUseCount();
    else
        comm_.refUseCount.call();
}

void FlagTable::clone(SelectionEvent& event) {
    if (readOnly_) return;
    // D版の memoryMode と同様、xmlCopy の設定にかかわらず、複製用XMLは内部だけで保持する。
    const auto xml = selectedXML();
    event.handled = !xml.empty() && pasteXML(xml);
}

bool FlagTable::canDoTCPD() const noexcept { return dir_ != nullptr; }
bool FlagTable::canDoT() const noexcept {
    return !readOnly_ && !selectedIndices_.empty();
}
bool FlagTable::canDoC() const noexcept {
    return !selectedIndices_.empty() && static_cast<bool>(comm_.clipboard());
}
bool FlagTable::canDoP() const noexcept {
    if (readOnly_ || !dir_) return false;
    const auto summary = comm_.summary.lock();
    if (!summary || (local_ && summary->legacy())) return false;
    if (const auto clipboard = comm_.clipboard()) {
        // メニューの有効化条件をD版に合わせる。実際のデータは pasteXML でも検証する。
        return transfer::CBisXML(*clipboard);
    }
    return false;
}
bool FlagTable::canDoD() const noexcept { return canDoT(); }
bool FlagTable::canDoClone() const noexcept {
    const auto summary = comm_.summary.lock();
    // D版と同様、複製は自身の選択をコピーするため、現在のクリップボードにXML
    // は不要。 読み取り専用と、旧形式のローカル変数に関する制限だけを適用する。
    return !readOnly_ && canDoC() && (!local_ || !summary || !summary->legacy());
}

void FlagTable::undo() {
    if (readOnly_ || !undo_) return;
    const auto life = std::weak_ptr<int>(callback_lifetime_);
    cancelEdit();
    if (life.expired()) return;
    const auto root = dir_ ? dir_->root() : FlagDirPtr{};
    const auto flags = root ? root->allFlags() : FlagList{};
    const auto steps = root ? root->allSteps() : StepList{};
    const auto variants = root ? root->allVariants() : VariantList{};
    const bool notifyLocalOwner = local_ && !lib::editor_window_host_options().all_mode;
    undo_->undo();
    if (life.expired()) return;
    // D版の UndoInsertDelete と同様、deleteEvent を通じて
    // 所有カードを更新対象にする。 空かどうかだけでなく、要素の識別情報を比較する。二つ
    // 目のローカル変数の挿入も 構成要素の変更であり、値の編集は構成要素の変更ではない。
    const auto sameMembers = [](const auto& before, const auto& after) {
        return std::is_permutation(before.begin(), before.end(), after.begin(), after.end());
    };
    const bool membershipChanged = notifyLocalOwner && root &&
        (!sameMembers(flags, root->allFlags()) ||
         !sameMembers(steps, root->allSteps()) ||
         !sameMembers(variants, root->allVariants()));
    notifyHistoryRemoved(comm_, root, flags, steps, variants);
    if (life.expired()) return;
    if (root) {
        comm_.refUseCount.call();
        if (life.expired()) return;
        comm_.refFlagDir.call(root);
        if (life.expired()) return;
        comm_.refFlagAndStep.call(root->allFlags(), root->allSteps(), root->allVariants());
        if (life.expired()) return;
    }
    if (dir_) refresh();
    if (life.expired()) return;
    if (comm_.refreshToolBar) comm_.refreshToolBar();
    if (life.expired()) return;
    if (membershipChanged) deleteEvent.call();
}

void FlagTable::redo() {
    if (readOnly_ || !undo_) return;
    const auto life = std::weak_ptr<int>(callback_lifetime_);
    cancelEdit();
    if (life.expired()) return;
    const auto root = dir_ ? dir_->root() : FlagDirPtr{};
    const auto flags = root ? root->allFlags() : FlagList{};
    const auto steps = root ? root->allSteps() : StepList{};
    const auto variants = root ? root->allVariants() : VariantList{};
    const bool notifyLocalOwner = local_ && !lib::editor_window_host_options().all_mode;
    undo_->redo();
    if (life.expired()) return;
    // D版の UndoInsertDelete と同様、deleteEvent を通じて
    // 所有カードを更新対象にする。 空かどうかだけでなく、要素の識別情報を比較する。二つ
    // 目のローカル変数の挿入も 構成要素の変更であり、値の編集は構成要素の変更ではない。
    const auto sameMembers = [](const auto& before, const auto& after) {
        return std::is_permutation(before.begin(), before.end(), after.begin(), after.end());
    };
    const bool membershipChanged = notifyLocalOwner && root &&
        (!sameMembers(flags, root->allFlags()) ||
         !sameMembers(steps, root->allSteps()) ||
         !sameMembers(variants, root->allVariants()));
    notifyHistoryRemoved(comm_, root, flags, steps, variants);
    if (life.expired()) return;
    if (root) {
        comm_.refUseCount.call();
        if (life.expired()) return;
        comm_.refFlagDir.call(root);
        if (life.expired()) return;
        comm_.refFlagAndStep.call(root->allFlags(), root->allSteps(), root->allVariants());
        if (life.expired()) return;
    }
    if (dir_) refresh();
    if (life.expired()) return;
    if (comm_.refreshToolBar) comm_.refreshToolBar();
    if (life.expired()) return;
    if (membershipChanged) deleteEvent.call();
}

void FlagTable::closePropertySessions() noexcept {
    const auto life = std::weak_ptr<int>(callback_lifetime_);
    auto sessions = std::move(property_sessions_);
    property_sessions_.clear();
    for (const auto& host : sessions) {
        if (host) host->dispose();
        if (life.expired()) return;
    }
}

// D版の cancelEdit と同様、表のインライン編集をキャンセルする。モードレスのプロ
// パティ編集は、 フォルダー移動、対象の削除、ペインの破棄まで、取得済みの対象を保持する。
void FlagTable::cancelEdit() noexcept {
    const auto life = std::weak_ptr<int>(callback_lifetime_);
    const auto gui = comm_.services.gui;
    const auto table = table_;
    if (property_session_host_) {
        closeEditor(lib::DialogResult::Cancel);
    } else if (gui && table) {
        (void)gui->finish_programmatic_inline_edit(table, false);
    }
    if (life.expired()) return;
    if (undo_) undo_->discardSnapshot();
    hasEditing_ = false;
    editing_ = VariableObject{};
}

void FlagTable::enterEdit() noexcept {
    if (disposed_) return;

    // D版と同様、ここで有効な各セルの編集を確定する。 その状態は共通の表
    // 示側が管理するため、プロパティ編集を開く前に、この表の編集を終える。
    const auto life = std::weak_ptr<int>(callback_lifetime_);
    const auto directory = dir_;
    const auto use_counter = uc_;
    const auto undo = undo_;
    const auto gui = comm_.services.gui;
    const auto table = table_;
    const auto summary = comm_.summary.lock();
    const auto dialog_owner = comm_.dialogOwner.lock();
    const auto inc_search_parent = incSearchParent_;
    const auto pending_object = editing_;
    const auto pending_property = hasEditing_;

    const auto resolve_presentation_owner = [&]() {
        auto owner = comm_.dialogOwner.lock();
        if (!owner)
            owner = std::dynamic_pointer_cast<lib::Window>(incSearchParent_);
        if (!owner && comm_.services.gui)
            owner = std::dynamic_pointer_cast<lib::Window>(
                comm_.services.gui->focused_widget());
        return owner;
    };

    std::shared_ptr<lib::Window> presentation_owner;
    std::shared_ptr<lib::Window> table_owner;
    try {
        presentation_owner = resolve_presentation_owner();
        if (life.expired()) return;
        if (gui && table) {
            table_owner = gui->window_owner(table);
            if (life.expired()) return;
            (void)gui->finish_programmatic_inline_edit(table, true);
        }
    } catch (...) {
        // 表示側の失敗を、この noexcept API の外へ出さない。 コール
        // バックが表を無効化した場合は、元の所有者を調べたり後処理したりしない。
        if (life.expired()) return;
        return;
    }

    // 編集のコールバックが成功しても、表のモデル、カウンター、表示
    // 側、ウィンドウが同期的に更新・置換される場合がある。 所有者
    // の状況が変わった後に、それ以前のプロパティ対象を表示しない。
    if (life.expired()) return;
    if (disposed_ || dir_ != directory || uc_ != use_counter || undo_ != undo ||
        comm_.services.gui != gui || table_ != table ||
        comm_.summary.lock() != summary || comm_.dialogOwner.lock() != dialog_owner ||
        incSearchParent_ != inc_search_parent || hasEditing_ != pending_property ||
        editing_ != pending_object)
        return;

    try {
        const auto current_presentation_owner = resolve_presentation_owner();
        if (life.expired()) return;
        if (current_presentation_owner != presentation_owner) return;
        if (gui && table) {
            const auto current_table_owner = gui->window_owner(table);
            if (life.expired()) return;
            if (current_table_owner != table_owner) return;
        }
    } catch (...) {
        if (life.expired()) return;
        return;
    }
    if (life.expired()) return;
    if (!hasEditing_) return;
    const bool target_still_belongs_to_directory = std::visit(
        [&directory](const auto& value) {
            return !value || value->parent().get() == directory.get();
        }, pending_object);
    if (!target_still_belongs_to_directory) return;

    try { openEditor(editing_); }
    catch (...) {
        // openEditor は、この所有者を無効化するコールバックを呼ぶ場合がある。
        if (life.expired()) return;
        closeEditor(lib::DialogResult::Cancel);
    }
}

void FlagTable::closeEditor(const lib::DialogResult result) noexcept {
    if (!edit_shell_) return;
    if (edit_ok_button_) edit_ok_button_->on_click = {};
    if (edit_cancel_button_) edit_cancel_button_->on_click = {};
    if (edit_apply_button_) edit_apply_button_->on_click = {};
    const auto shell = std::move(edit_shell_);
    const auto gui = comm_.services.gui;
    shell->on_close_requested = {};
    shell->result = result;
    shell->set_visible(false);
    edit_ok_button_.reset();
    edit_cancel_button_.reset();
    edit_apply_button_.reset();
    edit_shell_.reset();
    edit_dialog_model_.reset();
    edit_dialog_object_ = VariableObject{};
    // ネイティブの終了処理が、この表を同期的に無効化する場合がある。 呼び出
    // し前に全メンバーの後処理を終え、その後は this にアクセスしない。
    if (gui) {
        try { gui->close(shell); }
        catch (...) { /* noexcept の後処理では、ネイティブの終了処理の失敗を外へ伝えない。 */ }
    }
}

void FlagTable::openEditor(const VariableObject& object) {
    if (!property_session_host_) {
        if (!dir_ || !undo_) return;
        // 閉じたホストの破棄は、自身の onClosed コールバックの実行外で行う。
        std::erase_if(property_sessions_, [](const auto& host) {
            if (host->edit_shell_ && host->edit_shell_->result == lib::DialogResult::None)
                return false;
            host->dispose();
            return true;
        });
        const bool existing = std::visit([](const auto& value) { return bool(value); }, object);
        for (const auto& host : property_sessions_) {
            if (existing && host->edit_shell_ &&
                host->edit_shell_->result == lib::DialogResult::None &&
                sameObject(host->edit_dialog_object_, object)) {
                if (comm_.services.gui) comm_.services.gui->restore_window(host->edit_shell_);
                hasEditing_ = false;
                return;
            }
        }
        auto context = comm_;
        const auto life = std::weak_ptr<int>(callback_lifetime_);
        const auto refresh_toolbar = context.refreshToolBar;
        context.refreshToolBar = [this, life, refresh_toolbar] {
            if (life.expired()) return;
            refresh();
            if (refresh_toolbar) refresh_toolbar();
        };
        auto host = std::make_shared<FlagTable>(context, prop_, *undo_, local_, readOnly_);
        host->property_session_host_ = true;
        host->dir_ = dir_;
        host->uc_ = uc_;
        host->incSearchParent_ = incSearchParent_;
        host->store_history_selection_ = store_history_selection_;
        host->createEvent.add([this, life] { if (!life.expired()) createEvent.call(); });
        host->editing_ = object;
        host->hasEditing_ = true;
        property_sessions_.push_back(host);
        host->openEditor(object);
        if (life.expired()) return;
        hasEditing_ = false;
        return;
    }
    // edit() は editing_ を変更済みなので、表示モデルが別に持
    // つ対象の識別情報を比較し、 同じオブジェクトの未適用の入力を保持する。
    const bool existing = std::visit([](const auto& value) { return bool(value); }, object);
    if (existing && edit_shell_ && edit_shell_->result == lib::DialogResult::None &&
        edit_dialog_model_ && sameObject(edit_dialog_object_, object)) {
        if (comm_.services.gui) comm_.services.gui->restore_window(edit_shell_);
        return;
    }
    closeEditor(lib::DialogResult::Cancel);
    if (!dir_) return;
    edit_dialog_object_ = object;
    auto summary = comm_.summary.lock();
    if (!summary && uc_)
        summary = std::dynamic_pointer_cast<
            ::cwxeditor::cwx::summary::Summary>(uc_->owner());
    std::shared_ptr<lib::Window> owner = comm_.dialogOwner.lock();
    if (!owner)
        owner = std::dynamic_pointer_cast<lib::Window>(incSearchParent_);
    if (!owner && comm_.services.gui)
        owner = std::dynamic_pointer_cast<lib::Window>(
            comm_.services.gui->focused_widget());

    const auto closed = [this, object](lib::DialogResult) {
        if (undo_) undo_->discardSnapshot();
        hasEditing_ = false;
        editing_ = VariableObject{};
        // 削除されたモードレス編集の対象は、所有する FlagTable が行を再構築
        // する前、 削除通知の途中で閉じられる。この終了コールバックから refUseCount
        // は通知しない。 別の FlagTable がまだ切り離し済みのオブジ
        // ェクトを保持しており、Flag::path() を参照してしまうため。
        const bool target_detached = std::visit([](const auto& value) {
            return value && !value->parent();
        }, object);
        // 新規作成時の null の対象は、削除済みモデルではない。従来どおり終了時の更新を行う。
        if (!target_detached) notifyRefresh();
    };

    if (const auto* value = std::get_if<FlagPtr>(&object)) {
        const bool create_mode_initial = !*value;
        auto editor = std::make_shared<dedicated::FlagEditDialog>(
            comm_, summary, owner, readOnly_, local_, dir_, *value);
        editor->presentation(prop_);
        editor->onApplying([this] { beginMutation(); });
        editor->onApplied(
            [this, editor = editor.get(), create_mode = create_mode_initial,
             old_name = *value ? (*value)->name() : String{}]() mutable {
                const auto edited = editor->flag();
                if (!edited) {
                    if (undo_) undo_->discardSnapshot();
                    return;
                }
                if (create_mode) {
                    if (uc_) uc_->createID(usecounter::toFlagId(edited->path()));
                    createEvent.call();
                    comm_.refUseCount.call();
                    create_mode = false;
                } else if (uc_ && old_name != edited->name()) {
                    const auto old_id = usecounter::toFlagId(
                        flag::FlagDir::join(dir_->path(), old_name));
                    const auto new_id = usecounter::toFlagId(edited->path());
                    uc_->change(old_id, new_id, true);
                    if (undo_) {
                        const auto counter = uc_;
                        undo_->addSnapshotSideEffect(
                            [counter, old_id, new_id] {
                                counter->change(new_id, old_id, true);
                            },
                            [counter, old_id, new_id] {
                                counter->change(old_id, new_id, true);
                            });
                    }
                }
                commitMutation();
                old_name = edited->name();
                editing_ = edited;
                edit_dialog_object_ = edited;
                refresh({VariableObject{edited}});
                select(edited, true);
                notifyRefresh();
            });
        editor->onClosed(closed);
        edit_dialog_model_ = editor;
        edit_shell_ = editor->setupDialog(owner);
        edit_ok_button_ = editor->okButton();
        edit_cancel_button_ = editor->cancelButton();
        edit_apply_button_ = editor->applyButton();
        editor->show(owner);
        return;
    }

    if (const auto* value = std::get_if<StepPtr>(&object)) {
        const bool create_mode_initial = !*value;
        auto editor = std::make_shared<dedicated::StepEditDialog>(
            comm_, summary, owner, readOnly_, local_, dir_, *value);
        editor->presentation(prop_);
        editor->onApplying([this] { beginMutation(); });
        editor->onApplied(
            [this, editor = editor.get(), create_mode = create_mode_initial,
             old_name = *value ? (*value)->name() : String{}]() mutable {
                const auto edited = editor->step();
                if (!edited) {
                    if (undo_) undo_->discardSnapshot();
                    return;
                }
                if (create_mode) {
                    if (uc_) uc_->createID(usecounter::toStepId(edited->path()));
                    createEvent.call();
                    comm_.refUseCount.call();
                    create_mode = false;
                } else if (uc_ && old_name != edited->name()) {
                    const auto old_id = usecounter::toStepId(
                        flag::FlagDir::join(dir_->path(), old_name));
                    const auto new_id = usecounter::toStepId(edited->path());
                    uc_->change(old_id, new_id, true);
                    if (undo_) {
                        const auto counter = uc_;
                        undo_->addSnapshotSideEffect(
                            [counter, old_id, new_id] {
                                counter->change(new_id, old_id, true);
                            },
                            [counter, old_id, new_id] {
                                counter->change(old_id, new_id, true);
                            });
                    }
                }
                commitMutation();
                old_name = edited->name();
                editing_ = edited;
                edit_dialog_object_ = edited;
                refresh({VariableObject{edited}});
                select(edited, true);
                notifyRefresh();
            });
        editor->onClosed(closed);
        edit_dialog_model_ = editor;
        edit_shell_ = editor->setupDialog(owner);
        edit_ok_button_ = editor->okButton();
        edit_cancel_button_ = editor->cancelButton();
        edit_apply_button_ = editor->applyButton();
        editor->show(owner);
        return;
    }

    if (const auto* value = std::get_if<VariantPtr>(&object)) {
        const bool create_mode_initial = !*value;
        auto editor = std::make_shared<dedicated::VariantEditDialog>(
            comm_, summary, owner, readOnly_, local_, dir_, *value);
        editor->presentation(prop_);
        editor->onApplying([this] { beginMutation(); });
        editor->onApplied(
            [this, editor = editor.get(), create_mode = create_mode_initial,
             old_name = *value ? (*value)->name() : String{}]() mutable {
                const auto edited = editor->variant();
                if (!edited) {
                    if (undo_) undo_->discardSnapshot();
                    return;
                }
                if (create_mode) {
                    if (uc_) uc_->createID(usecounter::toVariantId(edited->path()));
                    createEvent.call();
                    comm_.refUseCount.call();
                    create_mode = false;
                } else if (uc_ && old_name != edited->name()) {
                    const auto old_id = usecounter::toVariantId(
                        flag::FlagDir::join(dir_->path(), old_name));
                    const auto new_id = usecounter::toVariantId(edited->path());
                    uc_->change(old_id, new_id, true);
                    if (undo_) {
                        const auto counter = uc_;
                        undo_->addSnapshotSideEffect(
                            [counter, old_id, new_id] {
                                counter->change(new_id, old_id, true);
                            },
                            [counter, old_id, new_id] {
                                counter->change(old_id, new_id, true);
                            });
                    }
                }
                commitMutation();
                old_name = edited->name();
                editing_ = edited;
                edit_dialog_object_ = edited;
                refresh({VariableObject{edited}});
                select(edited, true);
                notifyRefresh();
            });
        editor->onClosed(closed);
        edit_dialog_model_ = editor;
        edit_shell_ = editor->setupDialog(owner);
        edit_ok_button_ = editor->okButton();
        edit_cancel_button_ = editor->cancelButton();
        edit_apply_button_ = editor->applyButton();
        editor->show(owner);
    }
}

void FlagTable::replaceID() {
    if (!canReplaceID() || selectedIndices_.empty()) return;
    const auto object = rows_[selectedIndices_.front()].object;
    if (local_ && comm_.findLocalVariableUsers) {
        const auto callback = comm_.findLocalVariableUsers;
        const auto value = std::visit([](const auto& variable) {
            return std::static_pointer_cast<core::path::CWXPath>(variable);
        }, object);
        (void)callback(value);
        return;
    }
    const auto path = objectPath(object);
    if (!comm_.typedReplaceIDRequest.empty())
        comm_.typedReplaceIDRequest.call(
            path, objectKind(rows_[selectedIndices_.front()].object), local_);
    else
        comm_.replaceIDRequest.call(path, local_);
}

bool FlagTable::canReplaceID() const noexcept {
    return !readOnly_ && !selectedIndices_.empty();
}

bool FlagTable::openCWXPath(String path, const bool shellActivate) {
    if (!dir_) return false;
    const auto life = std::weak_ptr<int>(callback_lifetime_);
    const auto category = core::path::cpcategory(path);
    const auto index = core::path::cpindex(path);
    if (category == "") return core::path::cpempty(path);
    if (!core::path::cpempty(core::path::cpbottom(path))) return false;
    const auto focus = [&] {
        // shellActivate があれば切り離された所有者を開くことを許可する。 それ以外で
        // は、このコントロールが入力先のアクティブなウィンドウに属するときだけフォーカスを移す。
        if (core::path::cphasattr(path, "nofocus")) return true;
        const auto table = table_;
        return activateExternalPathOwner(comm_.services.gui, table,
                                         shellActivate, true, life);
    };
    if (category == "flag") {
        if (index >= dir_->flags().size()) return false;
        const auto value = dir_->flags()[index];
        if (!focus() || life.expired()) return false;
        if (core::path::cphasattr(path, "opendialog")) edit(value);
        else select(value, core::path::cphasattr(path, "only"));
        return true;
    }
    if (category == "step") {
        if (index >= dir_->steps().size()) return false;
        const auto value = dir_->steps()[index];
        if (!focus() || life.expired()) return false;
        if (core::path::cphasattr(path, "opendialog")) edit(value);
        else select(value, core::path::cphasattr(path, "only"));
        return true;
    }
    if (category == "variant") {
        if (index >= dir_->variants().size()) return false;
        const auto value = dir_->variants()[index];
        if (!focus() || life.expired()) return false;
        if (core::path::cphasattr(path, "opendialog")) edit(value);
        else select(value, core::path::cphasattr(path, "only"));
        return true;
    }
    return false;
}

std::vector<String> FlagTable::openedCWXPath() const {
    std::vector<String> result;
    const auto objects = selectedObjects();
    result.reserve(objects.size());
    const auto appendPath = [&result](const auto& value) {
        if (!value) return;
        const auto path = value->cwxPath(true);
        if (!path.empty()) result.push_back(path);
    };
    // D版と同様、混在する選択はフラグ、ステップ、コモンの順にまとめ、各種類の中
    // では表の順序を維持する。 直列化の順序に、表示上の種類の順序を流用しない。
    for (const auto& object : objects)
        if (const auto* value = std::get_if<FlagPtr>(&object)) appendPath(*value);
    for (const auto& object : objects)
        if (const auto* value = std::get_if<StepPtr>(&object)) appendPath(*value);
    for (const auto& object : objects)
        if (const auto* value = std::get_if<VariantPtr>(&object)) appendPath(*value);
    return result;
}

void FlagTable::setSearchText(String value) {
    search_ = value;
    if (incSearch_) {
        incSearch_->setSearchText(std::move(value));
        return;
    }
    refresh();
}

void FlagTable::closeCommentDialogs() noexcept {
    // 先に切り離し、終了コールバックによってこの走査が無効にならないようにする。
    auto dialogs = std::move(commentDialogs_);
    commentDialogs_.clear();
    for (auto& [key, value] : dialogs) {
        (void)key;
        if (const auto holder = std::static_pointer_cast<CommentHolder>(value);
            holder && holder->dialog) {
            holder->dialog->closed = {};
            holder->dialog->forceCancel();
        }
    }
}

void FlagTable::dispose() noexcept {
    if (disposed_) return;
    disposed_ = true; // 子の終了コールバックからの再入による破棄を拒否する。
    const auto life = std::weak_ptr<int>(callback_lifetime_);
    comm_.refUseCount.remove(use_count_connection_);
    use_count_connection_ = 0;
    closePropertySessions();
    if (life.expired()) return;
    cancelEdit();
    if (life.expired()) return;
    closeCommentDialogs();
    if (life.expired()) return;
    disposed_ = true;
    active_drag_selection_.reset();
    active_drag_consumed_ = false;
    callback_lifetime_.reset();
    if (context_menu_) context_menu_->on_command = {};
    context_menu_.reset();
    if (comm_.services.drag_drop && table_)
        comm_.services.drag_drop->unbind(table_);
    if (table_) {
        table_->on_selection_changed = {};
        table_->on_cell_activate = {};
        table_->on_cell_edited = {};
        table_->on_pointer = {};
    }
    table_.reset();
    component_.reset();
    // D版と同様、表の破棄後は選択取得で空を返す。モデル側の公
    // 開状態も無効化し、 共通ウィジェットの解放後に、保持さ
    // れたコントローラーが古い選択変数を返さないようにする。
    selectedIndices_.clear();
    rows_.clear();
}

void FlagTable::useCounter(UseCounterPtr value) {
    uc_ = std::move(value);
    refresh();
}

void FlagTable::notifyRefresh() {
    refresh();
    comm_.refUseCount.call();
    if (comm_.refreshToolBar) comm_.refreshToolBar();
}

bool FlagTable::renameSelected(String name) {
    if (readOnly_ || !dir_ || selectedIndices_.empty()) return false;
    // D版と同様、インライン編集完了時と名前生成時に一度ずつ正規化する。 区
    // 切り文字だけの入力は、二度目の処理で通常のアンダースコアの名前になる。
    name = flag::FlagDir::validName(std::move(name));
    const auto selected = selectedObjects();
    FlagList flags;
    StepList steps;
    VariantList variants;
    for (const auto& value : selected) {
        if (const auto* item = std::get_if<FlagPtr>(&value); item && *item)
            flags.push_back(*item);
        else if (const auto* item = std::get_if<StepPtr>(&value); item && *item)
            steps.push_back(*item);
        else if (const auto* item = std::get_if<VariantPtr>(&value); item && *item)
            variants.push_back(*item);
    }
    const auto names = [](const auto& items) {
        std::vector<String> result;
        for (const auto& item : items) result.push_back(item->name());
        return result;
    };
    const auto old_flag_names = names(flags);
    const auto old_step_names = names(steps);
    const auto old_variant_names = names(variants);
    const auto new_flag_names = dir_->createNewFlagNames(name, flags.size(), old_flag_names);
    const auto new_step_names = dir_->createNewStepNames(name, steps.size(), old_step_names);
    const auto new_variant_names = dir_->createNewVariantNames(name, variants.size(), old_variant_names);
    const bool changed = old_flag_names != new_flag_names ||
                         old_step_names != new_step_names ||
                         old_variant_names != new_variant_names;
    if (!changed) {
        return false;
    }
    const auto flag_id = [](String path) { return usecounter::toFlagId(std::move(path)); };
    const auto step_id = [](String path) { return usecounter::toStepId(std::move(path)); };
    const auto variant_id = [](String path) { return usecounter::toVariantId(std::move(path)); };
    applyVariableNames(flags, new_flag_names, uc_, flag_id);
    applyVariableNames(steps, new_step_names, uc_, step_id);
    applyVariableNames(variants, new_variant_names, uc_, variant_id);
    if (undo_) {
        const auto counter = uc_;
        const auto root = dir_->root();
        const auto directory = dir_->path();
        const auto flag_indices = variableIndices(flags);
        const auto step_indices = variableIndices(steps);
        const auto variant_indices = variableIndices(variants);
        const auto restore = [=](const bool forward) {
            const auto& oldFlags = forward ? old_flag_names : new_flag_names;
            const auto& oldSteps = forward ? old_step_names : new_step_names;
            const auto& oldVariants = forward ? old_variant_names : new_variant_names;
            const auto flags = liveVariables<FlagPtr>(root, directory, flag_indices, oldFlags);
            const auto steps = liveVariables<StepPtr>(root, directory, step_indices, oldSteps);
            const auto variants = liveVariables<VariantPtr>(root, directory, variant_indices, oldVariants);
            applyVariableNames(flags, forward ? new_flag_names : old_flag_names, counter, flag_id);
            applyVariableNames(steps, forward ? new_step_names : old_step_names, counter, step_id);
            applyVariableNames(variants, forward ? new_variant_names : old_variant_names, counter, variant_id);
        };
        undo_->push([restore] { restore(false); }, [restore] { restore(true); });
    }
    refresh(selected);
    comm_.refFlagAndStep.call(selectionFlags(), selectionSteps(), selectionVariants());
    notifyRefresh();
    return true;
}

bool FlagTable::setFlagState(const FlagPtr& value, const bool on) {
    if (readOnly_ || !value || value->parent().get() != dir_.get()) return false;
    auto values = selectedObject(VariableObject{value}) ? selectionFlags() : FlagList{value};
    std::erase_if(values, [on](const auto& item) { return !item || item->onOff() == on; });
    if (values.empty()) return false;
    beginMutation();
    for (const auto& item : values) item->onOff(on);
    commitMutation();
    std::vector<VariableObject> selected;
    for (const auto& item : values) selected.emplace_back(item);
    refresh(selected);
    comm_.refFlagAndStep.call(values, {}, {});
    notifyRefresh();
    return true;
}

bool FlagTable::setStepSelection(const StepPtr& value,
                                 const std::uint32_t selection) {
    if (readOnly_ || !value || value->parent().get() != dir_.get() ||
        selection >= value->count())
        return false;
    auto values = selectedObject(VariableObject{value}) ? selectionSteps() : StepList{value};
    std::erase_if(values, [selection](const auto& item) {
        // D版の Step.select と同様、選択したステップの値の数が少ない場合は、その最後の値を使う。
        return !item || item->count() == 0U || item->select() ==
            std::min<std::uint32_t>(selection, item->count() - 1U);
    });
    if (values.empty()) return false;
    beginMutation();
    for (const auto& item : values) item->select(selection);
    commitMutation();
    std::vector<VariableObject> selected;
    for (const auto& item : values) selected.emplace_back(item);
    refresh(selected);
    comm_.refFlagAndStep.call({}, values, {});
    notifyRefresh();
    return true;
}

bool FlagTable::setVariantValue(const VariantPtr& value, const VariantVal& data) {
    if (readOnly_ || !value || value->parent().get() != dir_.get() || !data.valid)
        return false;
    auto values = selectedObject(VariableObject{value}) ? selectionVariants()
                                                        : VariantList{value};
    if (values.empty()) return false;
    beginMutation();
    for (const auto& item : values) switch (data.type) {
    case types::VariantType::Number:
        item->value(data.numVal);
        break;
    case types::VariantType::String:
        item->value(data.strVal);
        break;
    case types::VariantType::Boolean:
        item->value(data.boolVal);
        break;
    case types::VariantType::List:
        item->value(data.listVal);
        break;
    case types::VariantType::Structure:
        item->setStructValue(data.structName, data.structVal);
        break;
    }
    commitMutation();
    std::vector<VariableObject> selected;
    for (const auto& item : values) selected.emplace_back(item);
    refresh(selected);
    comm_.refFlagAndStep.call({}, {}, values);
    notifyRefresh();
    return true;
}

bool FlagTable::setInitialization(
    const VariableObject& value,
    const types::VariableInitialization initialization) {
    if (readOnly_ || !dir_) return false;
    bool valid = false;
    std::visit([&](const auto& item) {
        valid = item && item->parent().get() == dir_.get();
        (void)item;
    }, value);
    if (!valid) return false;
    auto values = selectedObject(value) ? selectedObjects()
                                        : std::vector<VariableObject>{value};
    std::erase_if(values, [initialization](const auto& candidate) {
        return std::visit([initialization](const auto& item) {
            return !item || item->initialization() == initialization;
        }, candidate);
    });
    if (values.empty()) return false;
    beginMutation();
    FlagList flags;
    StepList steps;
    VariantList variants;
    for (const auto& candidate : values)
        std::visit([&](const auto& item) {
            using Value = std::decay_t<decltype(item)>;
            item->initialization(initialization);
            if constexpr (std::is_same_v<Value, FlagPtr>) flags.push_back(item);
            else if constexpr (std::is_same_v<Value, StepPtr>) steps.push_back(item);
            else variants.push_back(item);
        }, candidate);
    commitMutation();
    refresh(values);
    comm_.refFlagAndStep.call(flags, steps, variants);
    notifyRefresh();
    return true;
}

template <typename Component>
std::shared_ptr<Component> FlagDirTree::createComponent(
    const lib::ComponentKind kind) const {
    if (comm_.services.gui) {
        auto candidate = comm_.services.gui->create_component(kind);
        if (auto result = std::dynamic_pointer_cast<Component>(candidate))
            return result;
    }
    return std::make_shared<Component>();
}

FlagDirTree::FlagDirTree(Commons comm, Props prop, FlagTable& flags,
                          UndoManager& undo)
    : comm_(std::move(comm)), prop_(std::move(prop)), flags_(&flags),
      undo_(&undo), uc_(comm_.useCounter) {
    const auto life = std::weak_ptr<int>(callback_lifetime_);
    flags.store_history_selection_ = [this, life](const FlagDirPtr& dir) {
        if (life.expired()) return false;
        storeSnapshotSelection(dir);
        return true;
    };
}

FlagDirTree::~FlagDirTree() {
    callback_lifetime_.reset();
    if (context_menu_) context_menu_->on_command = {};
    if (comm_.services.drag_drop && tree_)
        comm_.services.drag_drop->unbind(tree_);
    if (tree_) {
        tree_->on_selection_changed = {};
        tree_->on_key_down = {};
        tree_->on_text_input = {};
        tree_->on_activate = {};
        tree_->on_item_edited = {};
        tree_->on_expansion_changed = {};
        tree_->on_pointer = {};
        tree_->on_focus = {};
        tree_->on_inline_editor_created = {};
    }
}

PanelPtr FlagDirTree::createControl(const Parent& parent, lib::Action gotFocus) {
    parent_ = parent;
    component_ = createComponent<lib::Panel>(lib::ComponentKind::Panel);
    component_->set_layout(std::make_shared<lib::FillLayout>());
    tree_ = createComponent<lib::TreeView>(lib::ComponentKind::TreeView);
    tree_->multi_select = false;
    // Data は折り畳めないルートラベルとして扱う。子ディレクトリは通常の
    // 展開・折り畳み表示を持ち、 再構築してもD版と同じ展開状態を維持する。
    tree_->show_expanders = true;
    // SWT と同様、Data はツリーのクライアント領域の左端に
    // 描き、子孫だけを階層に応じて字下げする。 AreaTable
    // のディレクトリツリーと同じ、構造上のルートの表示にする。
    tree_->flush_root_left = true;
    const auto lifetime = std::weak_ptr<int>(callback_lifetime_);
    const auto previous_key = tree_->on_key_down;
    tree_->on_key_down = [this, lifetime, previous_key](lib::KeyEvent& event) {
        if (!lifetime.expired() && event.key == lib::Key::I &&
            event.modifiers == lib::Modifiers::Control && flags_) {
            flags_->incSearch();
            event.handled = true;
            return;
        }
        if (!lifetime.expired() && flags_ && flags_->incSearch_ &&
            flags_->incSearch_->handleHostKeyDown(event, tree_.get())) {
            event.handled = true;
            return;
        }
        if (previous_key) previous_key(event);
    };
    const auto previous_text = tree_->on_text_input;
    tree_->on_text_input = [this, lifetime, previous_text](lib::TextInputEvent& event) {
        if (!lifetime.expired() && flags_ && flags_->incSearch_ &&
            flags_->incSearch_->handleHostTextInput(event, tree_.get())) {
            event.handled = true;
            return;
        }
        if (previous_text) previous_text(event);
    };
    tree_->on_selection_changed = [this](const std::vector<lib::TreeItemId>& ids) {
        if (ids.empty()) return;
        if (const auto value = findNode(ids.front())) current(value);
    };
    tree_->on_activate = [this](const lib::TreeItemId id) {
        if (const auto value = findNode(id)) current(value);
    };
    tree_->on_item_edited = [this](const lib::TreeItemId id, String value) {
        editTreeItem(id, std::move(value));
    };
    tree_->on_expansion_changed = [this](const lib::TreeItemId id,
                                         const bool expanded) {
        if (!tree_) return;
        const auto found = std::find_if(tree_->nodes.begin(), tree_->nodes.end(),
                                        [id](const auto& node) { return node.id == id; });
        if (found != tree_->nodes.end()) expanded_paths_[found->value] = expanded;
    };
    if (!lib::editor_window_host_options().all_mode) {
        const auto previousPaint = tree_->on_paint_context;
        tree_->on_paint_context = [this, lifetime, previousPaint]
            (lib::DrawingContext& context, const lib::Rect area) {
            if (lifetime.expired()) return;
            if (previousPaint) previousPaint(context, area);
            if (lifetime.expired()) return;
            const auto* live = comm_.editorProps ? comm_.editorProps() : nullptr;
            if (lifetime.expired()) return;
            const auto tree = tree_;
            if (!tree) return;
            const auto configuredWidth = live ? live->var().warningImageWidthForTree.value : 80;
            lib::Color color = prop_.etc.warningImageColor;
            if (live) {
                const auto value = live->var().warningImageColor.value;
                color = {static_cast<std::uint8_t>(std::min(value.r, 255U)),
                         static_cast<std::uint8_t>(std::min(value.g, 255U)),
                         static_cast<std::uint8_t>(std::min(value.b, 255U)),
                         static_cast<std::uint8_t>(std::min(value.a, 255U))};
            }
            if (configuredWidth <= 0 || color.alpha == 0U) return;
            const float width = lib::scaled_ui_metric(static_cast<float>(configuredWidth));
            const float height = lib::item_row_height(*tree,
                lib::scaled_ui_metric(lib::kEditorUiMetrics.row_height));
            auto viewport = lib::item_scroll_geometry(*tree, height).viewport;
            viewport.x -= tree->bounds().x;
            viewport.y -= tree->bounds().y;
            if (!(width > 0.0F) || !std::isfinite(width) || !(height > 0.0F) ||
                !std::isfinite(height) || !std::isfinite(viewport.x) ||
                !std::isfinite(viewport.y) || !std::isfinite(viewport.width) ||
                !std::isfinite(viewport.height) || viewport.width <= 0.0F ||
                viewport.height <= 0.0F) return;
            const auto order = lib::visible_tree_indices(*tree);
            const auto nodes = tree->nodes;
            const auto top = std::min(tree->top_index, order.size());
            const float left = viewport.x + viewport.width - width;
            const float icon = lib::scaled_ui_metric(16.0F);
            context.save();
            context.clip(area);
            context.clip(viewport);
            for (std::size_t i = top; i < order.size(); ++i) {
                const float y = viewport.y + static_cast<float>(i - top) * height;
                if (y >= viewport.y + viewport.height) break;
                const auto& node = nodes[order[i]];
                if (node.warnings.empty()) continue;
                // D版の warningImage と同様、水平方向に透明度を変化させ、
                // 行全体に引き伸ばす。 クリップされる場合は、見える列だけを処理する。
                const float right = viewport.x + viewport.width;
                for (float x = std::max(viewport.x, left); x < right;) {
                    const float next = std::min(x + 1.0F, right);
                    if (!(next > x)) break;
                    auto shade = color;
                    shade.alpha = static_cast<std::uint8_t>(std::clamp(
                        static_cast<float>(color.alpha) * (x - left) / width, 0.0F, 255.0F));
                    context.fill_rect({x, y, next - x, height}, shade);
                    x = next;
                }
                // ネイティブの行描画で印は描画済みだが、D版が警告アイコ
                // ンを最後に描くのと同様、 重ねた表示の上に再描画する。
                if (node.warning_icon)
                    context.draw_image(node.warning_icon, {0.0F, 0.0F, 16.0F, 16.0F},
                        {viewport.x + viewport.width - icon - lib::scaled_ui_metric(5.0F),
                         y + (height - icon) / 2.0F, icon, icon}, lib::Interpolation::Nearest);
            }
            context.restore();
        };
    }
    tree_->on_inline_editor_created = [this, lifetime](const std::shared_ptr<lib::TextEdit>& editor) {
        if (lifetime.expired()) return;
        dmenu::Commons text_context;
        text_context.services = comm_.services;
        dmenu::installMissingTextMenus(editor, text_context);
    };
    tree_->on_pointer = [this, lifetime](lib::PointerEvent& event) {
        if (lifetime.expired() || event.button != lib::PointerButton::Secondary ||
            event.action != lib::PointerAction::Up)
            return;
        // 表示側が、編集中のインラインテキスト欄とそのテキストメニューを所有する。
        if (comm_.services.gui && comm_.services.gui->programmatic_inline_edit_active(tree_)) return;
        if (event.item_hit_test_available && event.item_index >= 0) {
            const auto id = static_cast<lib::TreeItemId>(event.item_index);
            if (const auto value = findNode(id)) current(value);
        }
        showContextMenu(tree_, event.position);
        event.handled = true;
    };
    tree_->on_focus = std::move(gotFocus);
    // D版の TreeEdit と同様、選択済みのフォルダー
    // を押すと改名する。 Data ルートは対象にしない。
    ::cwxeditor::cwx::editor::gui::dwt::dutils::installClickSelectedEdit(
        tree_, comm_.services, component_,
        [this] { return prop_.etc.editTriggerQuick; },
        [this](const lib::TreeItemId id) {
            if (flags_ && flags_->readOnly()) return false;
            const auto dir = findNode(id);
            return dir && dir.get() != root_.get();
        });
    component_->add(tree_);
    if (parent) parent->add(component_);
    if (comm_.services.drag_drop) {
        const auto drag_lifetime = std::weak_ptr<int>(callback_lifetime_);
        lib::DragSourceBinding source;
        source.source = tree_;
        source.allowed = lib::DropOperation::Move;
        source.provide_data = [this, drag_lifetime] {
            if (drag_lifetime.expired()) return lib::ClipboardData{};
            const auto focused = comm_.services.gui ? comm_.services.gui->focused_widget() : lib::WidgetPtr{};
            drag_source_dir_ = focused == tree_ && current_ && current_.get() != root_.get()
                ? current_ : FlagDirPtr{};
            return drag_source_dir_
                ? transfer::makeXMLPayload(flag::getXML(prop_.msgs.flagDirRoot,
                                                         drag_source_dir_))
                : lib::ClipboardData{};
        };
        source.completed = [this, drag_lifetime](const lib::DropOperation) {
            if (!drag_lifetime.expired()) drag_source_dir_.reset();
        };
        source.provide_origin = [this, drag_lifetime] {
            lib::DragOrigin origin;
            if (drag_lifetime.expired()) return origin;
            if (!tree_ || !current_ || current_.get() == root_.get()) return origin;
            origin.selected_keys = {current_->path()};
            const auto found = std::find_if(tree_->nodes.begin(), tree_->nodes.end(),
                                            [this](const auto& node) {
                                                return node.value == current_->path();
                                            });
            if (found != tree_->nodes.end())
                origin.cursor_index = static_cast<std::size_t>(found - tree_->nodes.begin());
            return origin;
        };
        comm_.services.drag_drop->bind_source(std::move(source));

        lib::DropTargetBinding target;
        target.target = tree_;
        target.accepted_formats = {String(transfer::XML_BYTES_FORMAT),
                                   String(transfer::XML_BYTES_MIME)};
        target.accepts_target_index = [this, drag_lifetime](const std::optional<std::size_t> index) {
            return !drag_lifetime.expired() && static_cast<bool>(dropTarget(index));
        };
        target.on_enter = [this, drag_lifetime](lib::DragEvent& event) {
            if (drag_lifetime.expired()) { event.selected = lib::DropOperation::None; return; }
            dragEnter(event);
        };
        target.on_over = [this, drag_lifetime](lib::DragEvent& event) {
            if (drag_lifetime.expired()) { event.selected = lib::DropOperation::None; return; }
            dragOver(event);
        };
        target.on_drop = [this, drag_lifetime](lib::DragEvent& event) {
            if (drag_lifetime.expired()) { event.selected = lib::DropOperation::None; return; }
            drop(event);
        };
        // completed まで転送元の識別情報を保持する。ポインターが一度外へ
        // 出て戻っただけで、 同じツリー内の移動がコピーに変わらないようにする。
        target.on_leave = {};
        comm_.services.drag_drop->bind_target(std::move(target));
    }
    refresh();
    return component_;
}

void FlagDirTree::appendNode(std::vector<lib::TreeNode>& nodes,
                             lib::TreeItemId& nextNodeId,
                             const FlagDirPtr& dir,
                             const lib::TreeItemId parent,
                             const bool rootNode) {
    if (!dir) return;
    const auto id = nextNodeId++;
    const auto path = treePath(dir);
    const auto expanded = rootNode ||
        !expanded_paths_.contains(path) || expanded_paths_.at(path);
    lib::TreeNode node{id, parent,
                       rootNode ? prop_.msgs.flagDirRoot : dir->name(),
                       path, prop_.flagDirImage, expanded, false, false,
                       rootNode};
    if (comm_.warnings) node.warnings = comm_.warnings(*dir);
    if (!node.warnings.empty()) node.warning_icon = comm_.warningImage;
    nodes.push_back(std::move(node));
    auto children = dir->subDirs();
    const auto* live = comm_.editorProps ? comm_.editorProps() : nullptr;
    const bool logical = live ? live->var().logicalSort.value : prop_.etc.logicalSort;
    std::stable_sort(children.begin(), children.end(),
                     [logical](const auto& left, const auto& right) {
                         return nameLess(left->name(), right->name(), logical);
                     });
    for (const auto& child : children)
        appendNode(nodes, nextNodeId, child, id);
}

FlagDirPtr FlagDirTree::findNode(const lib::TreeItemId id) const {
    if (!tree_ || !root_) return {};
    const auto found = std::find_if(tree_->nodes.begin(), tree_->nodes.end(),
                                    [id](const auto& node) { return node.id == id; });
    if (found == tree_->nodes.end()) return {};
    return resolveTreePath(root_, found->value);
}

void FlagDirTree::refreshTree() {
    if (!tree_) return;
    for (const auto& node : tree_->nodes)
        expanded_paths_[node.value] = node.expanded;
    std::vector<lib::TreeNode> nodes;
    lib::TreeItemId next_node_id{1};
    if (root_) appendNode(nodes, next_node_id, root_, 0, true);
    if (!lib::replace_tree_nodes(tree_, std::move(nodes), {})) return;
    nextNodeId_ = next_node_id;
}

void FlagDirTree::refresh() {
    refresh(current_ ? current_->path() : String{});
}

void FlagDirTree::refresh(String selectedPath) {
    enterEdit();
    refreshTree();
    if (!root_) {
        current_.reset();
        flags_->setDir({}, true);
        return;
    }
    if (selectedPath.empty()) selectedPath = root_->path();
    select(std::move(selectedPath));
}

void FlagDirTree::select(String path) {
    if (!root_) {
        current_.reset();
        flags_->setDir({}, true);
        return;
    }
    auto value = resolveTreePath(root_, std::move(path));
    if (!value) value = root_;
    current(value);
}

void FlagDirTree::current(const FlagDirPtr& value) {
    const auto next_current = value ? value : root_;
    if (tree_) {
        std::vector<lib::TreeItemId> selection;
        for (const auto& node : tree_->nodes) {
            const auto candidate = resolveTreePath(root_, node.value);
            if (candidate && candidate.get() == next_current.get()) {
                selection.push_back(node.id);
                break;
            }
        }
        const auto reveal = selection.empty() ? std::optional<lib::TreeItemId>{}
                                              : std::optional<lib::TreeItemId>{selection.front()};
        auto nodes = tree_->nodes;
        if (!lib::replace_tree_nodes(tree_, std::move(nodes),
                                     std::move(selection), reveal)) return;
    }
    current_ = next_current;
    // D版と同様、同じ対象を選ぶ場合は setDir を強制せずにディレクトリを選択
    // する。 ツリーの再構築では、他の未確定の編集をキャンセルせずに行を更新する。
    if (flags_->dir() == current_) flags_->refresh();
    else flags_->setDir(current_);
    if (tree_ && comm_.services.gui)
        comm_.services.gui->request_redraw(tree_);
    if (comm_.refreshToolBar) comm_.refreshToolBar();
}

void FlagDirTree::rootDir(const FlagDirPtr& value) {
    // D版の rootDir と同様、新しく設定されたツリー全体を展開する。 refreshTree
    // がパスの一致によって、以前のシナリオの展開状態を再利用しないようにする。
    expanded_paths_.clear();
    if (tree_) tree_->nodes.clear();
    root_ = value;
    if (root_) root_->useCounter(uc_);
    current_ = root_;
    refresh();
    if (comm_.refreshToolBar) comm_.refreshToolBar();
}

void FlagDirTree::useCounter(UseCounterPtr value) {
    uc_ = std::move(value);
    if (root_) root_->useCounter(uc_);
    flags_->useCounter(uc_);
    refreshTree();
}

void FlagDirTree::storeSnapshotSelection(const FlagDirPtr& operationDir,
                                         const FlagDirPtr& restoredDir,
                                         const bool restoreOnUndo) {
    if (!undo_ || !flags_ || !operationDir) return;
    struct SelectionState {
        std::vector<std::ptrdiff_t> flags, steps, variants;
        std::vector<std::ptrdiff_t> restoreFlags, restoreSteps, restoreVariants;
    };
    auto state = std::make_shared<SelectionState>();
    state->flags = flags_->selectionFlagIndices();
    state->steps = flags_->selectionStepIndices();
    state->variants = flags_->selectionVariantIndices();
    const auto life = std::weak_ptr<int>(callback_lifetime_);
    const auto tableLife = std::weak_ptr<int>(flags_->callback_lifetime_);
    undo_->addSnapshotTraversal(
        [this, life, tableLife, state](bool) {
            if (life.expired() || tableLife.expired() || !flags_ || !flags_->table()) return;
            // D版の FTVUndo と同様、その時点の表から取得した型付きインデック
            // スを交換する。 その後ユーザーが別のディレクトリへ移動していても同様。
            state->restoreFlags = std::move(state->flags);
            state->restoreSteps = std::move(state->steps);
            state->restoreVariants = std::move(state->variants);
            state->flags = flags_->selectionFlagIndices();
            state->steps = flags_->selectionStepIndices();
            state->variants = flags_->selectionVariantIndices();
        },
        [this, life, tableLife, state, operationDir, restoredDir, restoreOnUndo](const bool undo) {
            if (life.expired() || tableLife.expired() || !flags_ || !flags_->table()) return;
            const bool singleFolderRestore = restoredDir && undo == restoreOnUndo;
            const auto dir = singleFolderRestore ? restoredDir : operationDir;
            if (!dir || dir->root().get() != root_.get()) return;
            // 生成・削除の走査で選択ディレクトリのツリーノードが追加
            // ・削除される場合があるため、 再構築してから選択する。
            const auto table = flags_->table();
            const auto gui = comm_.services.gui;
            const auto toolbar = comm_.refreshToolBar;
            refresh(dir->path());
            if (life.expired() || tableLife.expired()) return;
            flags_->deselectAll();
            if (life.expired() || tableLife.expired()) return;
            if (!singleFolderRestore) {
                const auto select = [this, life, tableLife](const auto& values, const auto& indices) {
                    for (const auto index : indices) {
                        if (life.expired() || tableLife.expired()) return;
                        if (index >= 0 && static_cast<std::size_t>(index) < values.size())
                            flags_->select(values[static_cast<std::size_t>(index)]);
                    }
                };
                const auto flags = dir->flags();
                const auto steps = dir->steps();
                const auto variants = dir->variants();
                select(flags, state->restoreFlags);
                select(steps, state->restoreSteps);
                select(variants, state->restoreVariants);
            }
            if (life.expired() || tableLife.expired()) return;
            if (gui) gui->focus(table);
            if (life.expired() || tableLife.expired()) return;
            if (toolbar) toolbar();
        });
}

void FlagDirTree::createDir() {
    if (!current_) return;
    if (undo_) undo_->beginSnapshot(root_);
    auto value = std::make_shared<flag::FlagDir>(
        current_->createNewDirName(prop_.msgs.flagDirNew));
    value->useCounter(uc_);
    storeSnapshotSelection(current_, value, false);
    current_->add(value);
    if (undo_) undo_->commitSnapshot(root_);
    comm_.refFlagDir.call(value);
    refresh(value->path());
    beginCurrentEdit();
}

bool FlagDirTree::copyDirectoryToMemory() {
    if (!current_) return false;
    const auto xml = flag::getXML(prop_.msgs.flagDirRoot, current_);
    if (const auto clipboard = comm_.clipboard()) {
        transfer::ClipData data(*clipboard);
        transfer::XMLtoCB(comm_.xmlCopyProvider && comm_.xmlCopyProvider(), data, xml);
        return true;
    }
    return false;
}

void FlagDirTree::writeXMLClipboard(StringView xml) {
    const auto life = std::weak_ptr<int>(callback_lifetime_);
    const auto clipboard = comm_.clipboard();
    const auto xml_copy_provider = comm_.xmlCopyProvider;
    const auto refresh_toolbar = comm_.refreshToolBar;
    if (!clipboard) return;

    const bool xml_copy = xml_copy_provider && xml_copy_provider();
    if (life.expired()) return;
    transfer::ClipData data(*clipboard);
    transfer::XMLtoCB(xml_copy, data, xml);
    if (life.expired()) return;
    // D版と同様、六つの共通コピー操作のそれぞれの後でツールバーの状態を更
    // 新する。 共通のクリップボードとの境界で更新し、全コマンドの順序を
    // 揃える。 再入するデータ提供処理の後にはメンバーへアクセスしない。
    if (refresh_toolbar) refresh_toolbar();
}

void FlagDirTree::writeClipboard(String text) {
    if (const auto clipboard = comm_.clipboard())
        clipboard->write({std::move(text), {}, {}});
}

String FlagDirTree::readClipboard() const {
    if (const auto clipboard = comm_.clipboard()) return clipboard->read_text();
    return {};
}

void FlagDirTree::copy(SelectionEvent& event) {
    if (!current_) return;
    event.handled=copyDirectoryToMemory();
    if (comm_.refreshToolBar) comm_.refreshToolBar();
}

void FlagDirTree::cut(SelectionEvent& event) {
    if (!canDoT()) return;
    copy(event);
    if (event.handled) del(event);
}

void FlagDirTree::paste(SelectionEvent& event) {
    if (!current_) return;
    const auto clipboard = comm_.clipboard();
    if (!clipboard) return;
    const auto xml = transfer::CBtoXML(*clipboard);
    pasteXML(event, xml);
}

void FlagDirTree::pasteXML(SelectionEvent& event, const StringView xml) {
    if (!current_ || xml.empty()) return;
    const auto life = std::weak_ptr<int>(callback_lifetime_);
    std::unordered_map<String, FlagPtr> copiedFlags;
    std::unordered_map<String, StepPtr> copiedSteps;
    std::unordered_map<String, VariantPtr> copiedVariants;
    String newPath;
    String rootId;
    if (undo_) undo_->beginSnapshot(root_);
    const auto result = current_->appendFromXML(
        xml, system::XMLInfo(flagEditorSystem(), versioninfo::LATEST_VERSION),
        true, true, false, copiedFlags, copiedSteps, copiedVariants, newPath, rootId);
    if (result != flag::FlagDir::AppendXmlResult::DIR_SUCCESS &&
        result != flag::FlagDir::AppendXmlResult::FLAG_STEP_SUCCESS) {
        if (undo_) undo_->discardSnapshot();
        return;
    }
    // D版の storeInsert と同様、やり直し時は挿入したフォルダーを、それ以外では型付きの行選択を復元する。
    storeSnapshotSelection(current_,
        result == flag::FlagDir::AppendXmlResult::DIR_SUCCESS && root_
            ? root_->findPath(newPath) : FlagDirPtr{}, false);
    if (undo_) undo_->commitSnapshot(root_);
    if (result == flag::FlagDir::AppendXmlResult::DIR_SUCCESS) {
        if (const auto value = root_ ? root_->findPath(newPath) : FlagDirPtr{}) {
            // C++版は削除後もパスごとの展開状態を保持する。D版と同
            // 様に新しく貼り付けたディレクトリを展開するため、 行の再
            // 構築前に、挿入した部分ツリーの古いパス情報を上書きする。
            const auto expand_pasted_subtree = [this](const auto& self,
                                                       const FlagDirPtr& dir) -> void {
                if (!dir) return;
                expanded_paths_[treePath(dir)] = true;
                for (const auto& child : dir->subDirs()) self(self, child);
            };
            expand_pasted_subtree(expand_pasted_subtree, value);
            current(value);
            if (life.expired()) return;
            comm_.refFlagDir.call(value);
            if (life.expired()) return;
        }
    }
    std::vector<FlagPtr> flags;
    std::vector<StepPtr> steps;
    std::vector<VariantPtr> variants;
    for (const auto& entry : copiedFlags)
        if (entry.second) flags.push_back(entry.second);
    for (const auto& entry : copiedSteps)
        if (entry.second) steps.push_back(entry.second);
    for (const auto& entry : copiedVariants)
        if (entry.second) variants.push_back(entry.second);
    comm_.refFlagAndStep.call(flags, steps, variants);
    if (life.expired()) return;
    event.handled = true;
    refresh(current_->path());
}

FlagDirPtr FlagDirTree::dropTarget(
    const std::optional<std::size_t> targetIndex) const {
    if (!tree_ || !targetIndex || *targetIndex >= tree_->nodes.size()) return {};
    return findNode(tree_->nodes[*targetIndex].id);
}

bool FlagDirTree::isDescendant(const FlagDirPtr& possibleChild,
                               const FlagDirPtr& possibleParent) const {
    for (auto value = possibleChild; value; value = value->parent())
        if (possibleParent && value.get() == possibleParent.get()) return true;
    return false;
}

void FlagDirTree::restoreDroppedSource(const FlagDirPtr& source,
                                       const FlagDirPtr& parent,
                                       const std::ptrdiff_t index) {
    if (!source || !parent) return;
    if (index < 0) parent->add(source);
    else parent->insert(index, source);
}

void FlagDirTree::notifyDroppedVariables(
    const std::unordered_map<String, FlagPtr>& flags,
    const std::unordered_map<String, StepPtr>& steps,
    const std::unordered_map<String, VariantPtr>& variants) {
    std::vector<FlagPtr> flag_values;
    std::vector<StepPtr> step_values;
    std::vector<VariantPtr> variant_values;
    appendDropValues(flags, flag_values);
    appendDropValues(steps, step_values);
    appendDropValues(variants, variant_values);
    comm_.refFlagAndStep.call(flag_values, step_values, variant_values);
}

bool FlagDirTree::applyDrop(const FlagDirPtr& target,
                            const lib::ClipboardData& data,
                            lib::DropOperation& operation) {
    String xml;
    if (!transfer::extractXML(data, xml)) return false;
    return applyDrop(target, xml, operation);
}

bool FlagDirTree::applyDrop(const FlagDirPtr& target, const StringView xml,
                            lib::DropOperation& operation) {
    if (!target || xml.empty() || !root_) return false;
    const auto source = drag_source_dir_;
    // D版と同様、元の親への無変化のドロップと、転送元の部分ツリーへのドロップを
    // 拒否する。 移動全体を表す一つのスナップショットを取得する前に検証する。
    if (source && (source->root().get() != root_.get() ||
                   isDescendant(target, source)))
        return false;

    if (undo_) undo_->beginSnapshot(root_);
    FlagDirPtr source_parent;
    std::ptrdiff_t source_index = -1;
    if (source) {
        source_parent = source->parent();
        if (!source_parent || source_parent.get() == target.get()) {
            if (undo_) undo_->discardSnapshot();
            return false;
        }
        source_index = source_parent->indexOf(*source);
        source_parent->remove(source);
    }

    std::unordered_map<String, FlagPtr> moved_flags;
    std::unordered_map<String, StepPtr> moved_steps;
    std::unordered_map<String, VariantPtr> moved_variants;
    String new_path;
    String root_id;
    const auto result = target->appendFromXML(
        xml, system::XMLInfo(flagEditorSystem(), versioninfo::LATEST_VERSION),
        false, true, false, moved_flags, moved_steps, moved_variants,
        new_path, root_id);
    const bool same_tree = root_->id() == root_id;
    if (result == flag::FlagDir::AppendXmlResult::FAIL ||
        result == flag::FlagDir::AppendXmlResult::ON_DIR ||
        result == flag::FlagDir::AppendXmlResult::FLAG_STEP_ON_DIR) {
        restoreDroppedSource(source, source_parent, source_index);
        if (undo_) undo_->discardSnapshot();
        operation = lib::DropOperation::None;
        return false;
    }
    // D版の FlagTable は、同じペイン内の変数移動が成功した場合
    // 、転送先への追加後に転送元を削除する。 ここでは転送先のスナップショ
    // ットが有効なうちに削除し、転送元の完了処理では何もしない。 取り消
    // し・やり直しで、追加済みだが元が未削除の状態を見せないようにする。
    const auto* table_drag = flags_ && flags_->activeDragSelection()
        ? &*flags_->activeDragSelection() : nullptr;
    if (same_tree && result == flag::FlagDir::AppendXmlResult::FLAG_STEP_SUCCESS &&
        table_drag && table_drag->directory &&
        table_drag->directory->root().get() == root_.get()) {
        for (const auto& value : table_drag->flags)
            if (value && value->parent()) value->parent()->remove(value);
        for (const auto& value : table_drag->steps)
            if (value && value->parent()) value->parent()->remove(value);
        for (const auto& value : table_drag->variants)
            if (value && value->parent()) value->parent()->remove(value);
        flags_->consumeActiveDragSelection();
    }
    if (same_tree && uc_ && undo_) {
        std::vector<std::pair<usecounter::FlagId, usecounter::FlagId>> flags;
        std::vector<std::pair<usecounter::StepId, usecounter::StepId>> steps;
        std::vector<std::pair<usecounter::VariantId, usecounter::VariantId>> variants;
        for (const auto& [old_path, value] : moved_flags)
            if (value) flags.emplace_back(usecounter::toFlagId(old_path), usecounter::toFlagId(value->path()));
        for (const auto& [old_path, value] : moved_steps)
            if (value) steps.emplace_back(usecounter::toStepId(old_path), usecounter::toStepId(value->path()));
        for (const auto& [old_path, value] : moved_variants)
            if (value) variants.emplace_back(usecounter::toVariantId(old_path), usecounter::toVariantId(value->path()));
        const auto remap = [counter = uc_, flags, steps, variants](const bool forward) {
            const auto apply = [&counter, forward](const auto& mapping) {
                for (const auto& [old_id, new_id] : mapping)
                    counter->change(forward ? old_id : new_id, forward ? new_id : old_id);
            };
            apply(flags); apply(steps); apply(variants);
        };
        undo_->addSnapshotSideEffect([remap] { remap(false); }, [remap] { remap(true); });
    }
    // D版の UndoMove と同様、両方向の履歴移動で転送元のディレクトリを使う。 内
    // 側の挿入記録の単一フォルダー用 selDir は UndoMove に引き継がない。
    if (same_tree)
        storeSnapshotSelection(source_parent ? source_parent : current_);
    else
        storeSnapshotSelection(target,
            result == flag::FlagDir::AppendXmlResult::DIR_SUCCESS
                ? root_->findPath(new_path) : FlagDirPtr{}, false);
    if (undo_) undo_->commitSnapshot(root_);

    if (same_tree && uc_) {
        for (const auto& [old_path, value] : moved_flags)
            if (value) uc_->change(usecounter::toFlagId(old_path),
                                   usecounter::toFlagId(value->path()));
        for (const auto& [old_path, value] : moved_steps)
            if (value) uc_->change(usecounter::toStepId(old_path),
                                   usecounter::toStepId(value->path()));
        for (const auto& [old_path, value] : moved_variants)
            if (value) uc_->change(usecounter::toVariantId(old_path),
                                   usecounter::toVariantId(value->path()));
        comm_.refUseCount.call();
    }

    operation = same_tree ? lib::DropOperation::Move : lib::DropOperation::Copy;
    if (result == flag::FlagDir::AppendXmlResult::DIR_SUCCESS) {
        const auto value = root_->findPath(new_path);
        if (source) comm_.delFlagDir.call(source);
        if (value) {
            current(value);
            comm_.refFlagDir.call(value);
        }
    }
    if (flags_) flags_->refresh();
    if (table_drag && same_tree &&
        result == flag::FlagDir::AppendXmlResult::FLAG_STEP_SUCCESS)
        comm_.delFlagAndStep.call(table_drag->flags, table_drag->steps,
                                  table_drag->variants);
    notifyDroppedVariables(moved_flags, moved_steps, moved_variants);
    refresh(current_ ? current_->path() : String{});
    notifyRefresh();
    return true;
}

void FlagDirTree::dragEnter(lib::DragEvent& event) {
    String xml;
    if (!transfer::extractXML(event.data, xml) || !dropTarget(event.target_index)) {
        event.selected = lib::DropOperation::None;
        return;
    }
    event.selected = hasDropOperation(event.allowed, lib::DropOperation::Move)
        ? lib::DropOperation::Move : lib::DropOperation::Copy;
}

void FlagDirTree::dragOver(lib::DragEvent& event) { dragEnter(event); }

void FlagDirTree::drop(lib::DragEvent& event) {
    lib::DropOperation operation = lib::DropOperation::None;
    const bool success = applyDrop(dropTarget(event.target_index), event.data, operation);
    event.selected = success ? operation : lib::DropOperation::None;
    event.handled = success;
    if (success) drag_source_dir_.reset();
}

void FlagDirTree::del(SelectionEvent& event) {
    if (!canDoT()) return;
    const auto value = current_;
    const auto parent = value->parent();
    if (!parent) return;
    const auto allFlags = value->allFlags();
    const auto allSteps = value->allSteps();
    const auto allVariants = value->allVariants();
    if (undo_) undo_->beginSnapshot(root_);
    storeSnapshotSelection(parent, value, true);
    parent->remove(value);
    if (undo_) undo_->commitSnapshot(root_);
    current(parent);
    comm_.delFlagDir.call(value);
    comm_.delFlagAndStep.call(allFlags, allSteps, allVariants);
    event.handled = true;
    refresh(parent->path());
}

void FlagDirTree::clone(SelectionEvent& event) {
    if (!current_) return;
    // D版のメモリ内コピー・貼り付けと同様、OSのクリップボードを上書きしない。
    const auto xml = flag::getXML(prop_.msgs.flagDirRoot, current_);
    const auto parent = current_->parent();
    if (parent) current(parent);
    pasteXML(event, xml);
}

bool FlagDirTree::canDoTCPD() const noexcept { return root_ != nullptr; }
bool FlagDirTree::canDoT() const noexcept {
    return current_ && root_ && current_.get() != root_.get();
}
bool FlagDirTree::canDoC() const noexcept {
    return current_ != nullptr && static_cast<bool>(comm_.clipboard());
}
bool FlagDirTree::canDoP() const noexcept {
    if (comm_.summary.expired()) return false;
    if (const auto clipboard = comm_.clipboard()) {
        // 転送形式の段階での受け付け条件をD版に合わせる。変数XMLのルートは pasteXML でも検証する。
        return transfer::CBisXML(*clipboard);
    }
    return false;
}
bool FlagDirTree::canDoD() const noexcept { return canDoT(); }
bool FlagDirTree::canDoClone() const noexcept { return canDoC(); }

String FlagDirTree::eventTreeXML(const event::CType flagType,
                                 const event::CType stepType,
                                 const event::CType variantType,
                                 const bool flagValue,
                                 const std::uint32_t stepValue,
                                 const bool useValues) const {
    if (!current_) return {};
    event::ContentPtr root;
    event::ContentPtr tail;
    const auto append = [&root, &tail](const event::ContentPtr& value) {
        if (!value) return;
        if (!root) root = value;
        else tail->add(value);
        tail = value;
    };
    if (flagType != event::CType::Start) {
        for (const auto& value : current_->allFlags()) {
            if (!value) continue;
            auto content = std::make_shared<event::Content>(flagType);
            content->flag(value->path());
            if (useValues) content->flagValue(flagValue);
            append(content);
        }
    }
    if (stepType != event::CType::Start) {
        for (const auto& value : current_->allSteps()) {
            if (!value) continue;
            auto content = std::make_shared<event::Content>(stepType);
            content->step(value->path());
            if (useValues) content->stepValue(static_cast<int>(stepValue));
            append(content);
        }
    }
    if (variantType != event::CType::Start) {
        for (const auto& value : current_->allVariants()) {
            if (!value) continue;
            auto content = std::make_shared<event::Content>(variantType);
            content->variant(value->path());
            if (useValues)
                content->expression(
                    expression::variantValueToText(expression::VariantVal(*value)));
            append(content);
        }
    }
    return root ? root->toXML() : String{};
}

void FlagDirTree::copyFlagTree(const bool onOff) {
    const auto xml = eventTreeXML(event::CType::SetFlag, event::CType::Start,
                                  event::CType::Start, onOff, 0U, true);
    if (!xml.empty()) writeXMLClipboard(xml);
}

void FlagDirTree::copyStepTree(const std::uint32_t value) {
    const auto xml = eventTreeXML(event::CType::Start, event::CType::SetStep,
                                  event::CType::Start, false, value, true);
    if (!xml.empty()) writeXMLClipboard(xml);
}

void FlagDirTree::copyInitTree() {
    if (!current_) return;
    // 表の初期化経路は共有し、定数を設定するコマンドは別に保つ。
    const auto tree = event::createInitVariablesTree(*current_);
    const auto xml = tree ? tree->toXML() : String{};
    if (!xml.empty()) writeXMLClipboard(xml);
}

void FlagDirTree::copyFlagReverseTree() {
    const auto xml = eventTreeXML(event::CType::ReverseFlag, event::CType::Start,
                                  event::CType::Start, false, 0U, false);
    if (!xml.empty()) writeXMLClipboard(xml);
}

void FlagDirTree::copyStepUpTree() {
    const auto xml = eventTreeXML(event::CType::Start, event::CType::SetStepUp,
                                  event::CType::Start, false, 0U, false);
    if (!xml.empty()) writeXMLClipboard(xml);
}

void FlagDirTree::copyStepDownTree() {
    const auto xml = eventTreeXML(event::CType::Start, event::CType::SetStepDown,
                                  event::CType::Start, false, 0U, false);
    if (!xml.empty()) writeXMLClipboard(xml);
}

bool FlagDirTree::openCWXPathImpl(const FlagDirPtr& dir, String path,
                                  const bool shellActivate) {
    if (!dir) return false;
    const auto life = std::weak_ptr<int>(callback_lifetime_);
    const auto category = core::path::cpcategory(path);
    const auto index = core::path::cpindex(path);
    if (category.empty()) {
        const auto tree = tree_;
        if (!activateExternalPathOwner(comm_.services.gui, tree,
                                       shellActivate, true, life)) return false;
        current(dir);
        return true;
    }
    if (category == "dir") {
        if (index >= dir->subDirs().size()) return false;
        return openCWXPathImpl(dir->subDirs()[index], core::path::cpbottom(path),
                               shellActivate);
    }
    // D版と同様、委譲するパスに nofocus 属性があっても、
    // 末端要素へ移動する前に 所有するフラグのウィンドウを開く。そ
    // の後、表側で自身のコントロールにフォーカスを移すか決める。
    if (!activateExternalPathOwner(comm_.services.gui, tree_, shellActivate,
                                   false, life)) return false;
    current(dir);
    if (life.expired()) return false;
    return flags_->openCWXPath(std::move(path), shellActivate);
}

bool FlagDirTree::openCWXPath(String path, const bool shellActivate) {
    if (!root_ || !root_->findCWXPath(path)) return false;
    return openCWXPathImpl(root_, std::move(path), shellActivate);
}

std::vector<String> FlagDirTree::openedCWXPath() const {
    if (!current_) return {};
    const auto path = current_->cwxPath(true);
    return path.empty() ? std::vector<String>{} : std::vector<String>{path};
}

void FlagDirTree::undo() {
    if (!undo_) return;
    // ツリーのコンテキストメニューは FlagsPane のラッパーを通さず
    // 、ここを直接呼ぶ。 モデルのスナップショットを復元する前に、兄弟の
    // TableView のインライン編集を終了する。 モードレスのプロパ
    // ティ編集は独自に対象を保持し、その対象が無効になったときだけ閉じる。
    if (flags_) flags_->cancelEdit();
    cancelEdit();
    const auto flags = root_ ? root_->allFlags() : FlagList{};
    const auto steps = root_ ? root_->allSteps() : StepList{};
    const auto variants = root_ ? root_->allVariants() : VariantList{};
    undo_->undo();
    notifyHistoryRemoved(comm_, root_, flags, steps, variants);
    refresh(current_ ? current_->path() : String{});
    notifySnapshotRestored();
}

void FlagDirTree::redo() {
    if (!undo_) return;
    // ツリーのコンテキストメニューは FlagsPane のラッパーを通さず
    // 、ここを直接呼ぶ。 モデルのスナップショットを復元する前に、兄弟の
    // TableView のインライン編集を終了する。 モードレスのプロパ
    // ティ編集は独自に対象を保持し、その対象が無効になったときだけ閉じる。
    if (flags_) flags_->cancelEdit();
    cancelEdit();
    const auto flags = root_ ? root_->allFlags() : FlagList{};
    const auto steps = root_ ? root_->allSteps() : StepList{};
    const auto variants = root_ ? root_->allVariants() : VariantList{};
    undo_->redo();
    notifyHistoryRemoved(comm_, root_, flags, steps, variants);
    refresh(current_ ? current_->path() : String{});
    notifySnapshotRestored();
}

void FlagDirTree::cancelEdit() noexcept {
    if (tree_ && comm_.services.gui)
        (void)comm_.services.gui->finish_programmatic_inline_edit(tree_, false);
}

void FlagDirTree::enterEdit() noexcept {
    if (tree_ && comm_.services.gui)
        (void)comm_.services.gui->finish_programmatic_inline_edit(tree_, true);
}

void FlagDirTree::beginCurrentEdit() {
    if (!current_ || current_.get() == root_.get() || !tree_ || !component_ ||
        !comm_.services.gui)
        return;
    const auto path = current_->path();
    lib::ProgrammaticInlineEditRequest request;
    request.tree = tree_;
    request.focus_scope = component_;
    request.row_height = lib::scaled_ui_metric(lib::kEditorUiMetrics.row_height);
    request.matches_stable_tree_node = [path](const lib::TreeNode& node) {
        return node.value == path;
    };
    (void)comm_.services.gui->request_programmatic_inline_edit(request);
}

void FlagDirTree::editTreeItem(const lib::TreeItemId id, String value) {
    const auto dir = findNode(id);
    if (!dir || dir.get() == root_.get()) return;
    // D版の TreeEdit と同様、空または不正な入力で編集を終えた場合は以前
    // の表示テキストを保持する。 FlagDir::rename は、子孫の UseCounter
    // のパス移動も含め、 モデルと共通の重複・パス検証を行う。
    if (value.empty() || value == dir->name()) {
        refresh(dir->path());
        return;
    }
    const auto beforePath = dir->path();
    const auto oldPaths = definitionPaths(dir);
    if (undo_) undo_->beginSnapshot(root_);
    if (!dir->rename(std::move(value), uc_)) {
        if (undo_) undo_->discardSnapshot();
        refresh(beforePath);
        return;
    }
    if (undo_ && uc_) {
        const auto afterPath = dir->path();
        const auto prefix = beforePath.size() + (beforePath.ends_with(flag::FlagDir::SEPARATOR) ? 0U : 1U);
        // D版の UndoEditDir と同様、取り消し・やり直しの両方でディレクトリ
        // を改名する。 移植版のツリーのスナップショットも、外部の参照元とローカル定義
        // IDに対して逆方向の更新が必要。 定義だけを戻すと、古いIDが残ってしまう。
        const auto remap = [counter = uc_, oldPaths, afterPath, prefix](const bool forward) {
            const auto apply = [&](const auto& paths, const auto& id) {
                for (const auto& oldPath : paths) {
                    const auto newPath = flag::FlagDir::join(afterPath, oldPath.substr(prefix));
                    counter->change(id(forward ? oldPath : newPath), id(forward ? newPath : oldPath));
                }
            };
            apply(oldPaths.flags, [](const String& path) { return usecounter::toFlagId(path); });
            apply(oldPaths.steps, [](const String& path) { return usecounter::toStepId(path); });
            apply(oldPaths.variants, [](const String& path) { return usecounter::toVariantId(path); });
        };
        undo_->addSnapshotSideEffect([remap] { remap(false); }, [remap] { remap(true); });
    }
    storeSnapshotSelection(dir);
    if (undo_) undo_->commitSnapshot(root_);
    comm_.refFlagDir.call(dir);
    comm_.refUseCount.call();
    refresh(dir->path());
}

void FlagDirTree::notifySnapshotRestored() {
    if (!root_) return;
    // 復元した値を利用側へ通知する前に、共有カウンターを適用し直す。
    // 履歴は、変更可能なパスとは独立してモデルの識別情報を保持する。
    root_->useCounter(uc_);
    comm_.refUseCount.call();
    comm_.refFlagDir.call(root_);
    comm_.refFlagAndStep.call(root_->allFlags(), root_->allSteps(),
                              root_->allVariants());
}
void FlagDirTree::notifyRefresh() {
    if (comm_.refreshToolBar) comm_.refreshToolBar();
}

void FlagDirTree::rebuildContextMenu() {
    if (!context_menu_) context_menu_ = std::make_shared<lib::Menu>(true);
    const auto text = [this](const types::MenuID id, String fallback) {
        if (comm_.menuText) {
            if (auto value = comm_.menuText(id); !value.empty()) return value;
        }
        return fallback;
    };
    const auto item = [](String text, String command, const bool enabled) {
        return lib::MenuItem{std::move(text), std::move(command), {}, 0U, enabled};
    };
    const auto separator = [] {
        lib::MenuItem value;
        value.separator = true;
        return value;
    };
    const auto contentText = [this](const event::CType type, String fallback) {
        if (comm_.contentName) {
            if (auto value = comm_.contentName(type); !value.empty()) return value;
        }
        return fallback;
    };
    const auto hasFlag = current_ && !current_->allFlags().empty();
    const auto hasStep = current_ && !current_->allSteps().empty();
    const auto hasVariant = current_ && !current_->allVariants().empty();
    const auto hasVariable = hasFlag || hasStep || hasVariant;
    std::vector<lib::MenuItem> items;
    items.push_back(item(text(types::MenuID::NewFlagDir, "新しいフォルダ"), "flagdir.new", current_ != nullptr));
    items.push_back(item(text(types::MenuID::EditProp, "名前の変更"), "flagdir.rename", canDoT()));
    items.push_back(separator());
    items.push_back(item(text(types::MenuID::Undo, "元に戻す"), "flagdir.undo", undo_ && undo_->canUndo()));
    items.push_back(item(text(types::MenuID::Redo, "やり直し"), "flagdir.redo", undo_ && undo_->canRedo()));
    items.push_back(separator());
    items.push_back(item(text(types::MenuID::Cut, "切り取り"), "flagdir.cut", canDoT()));
    items.push_back(item(text(types::MenuID::Copy, "コピー"), "flagdir.copy", canDoC()));
    items.push_back(item(text(types::MenuID::Paste, "貼り付け"), "flagdir.paste", canDoP()));
    items.push_back(item(text(types::MenuID::Delete, "削除"), "flagdir.delete", canDoD()));
    items.push_back(item(text(types::MenuID::Clone, "複製"), "flagdir.clone", canDoClone()));
    items.push_back(separator());
    lib::MenuItem events = item(text(types::MenuID::CreateVariableEventTree, "変数イベントを作成"),
                                 "flagdir.events", hasVariable);
    events.children.push_back(item(
        text(types::MenuID::InitVariablesTree, "変数を初期化"),
        "flagdir.event.init", hasVariable));
    events.children.push_back(separator());
    events.children.push_back(item(
        contentText(event::CType::ReverseFlag, "フラグ反転"),
        "flagdir.event.flag-reverse", hasFlag));
    events.children.push_back(separator());
    events.children.push_back(item(prop_.msgs.setFlagTrue,
                                   "flagdir.event.flag-on", hasFlag));
    events.children.push_back(item(prop_.msgs.setFlagFalse,
                                   "flagdir.event.flag-off", hasFlag));
    events.children.push_back(separator());
    events.children.push_back(item(
        contentText(event::CType::SetStepUp, "ステップ増加"),
        "flagdir.event.step-up", hasStep));
    events.children.push_back(item(
        contentText(event::CType::SetStepDown, "ステップ減少"),
        "flagdir.event.step-down", hasStep));
    events.children.push_back(separator());
    for (std::size_t value = 0; value < prop_.looks.stepMaxCount; ++value) {
        const auto valueName = structs::parseDollarParams(
            prop_.etc.stepValueName, {{'N', std::to_string(value)}});
        auto label = prop_.msgs.setStepValue;
        auto marker = label.find("%1$s");
        if (marker != String::npos) label.replace(marker, 4U, valueName);
        else if ((marker = label.find("%s")) != String::npos)
            label.replace(marker, 2U, valueName);
        else label = valueName + label;
        if (value < 10U) label += "(&" + std::to_string(value) + ")";
        events.children.push_back(item(
            std::move(label), "flagdir.event.step-value." + std::to_string(value),
            hasStep));
    }
    items.push_back(std::move(events));
    context_menu_->items = std::move(items);
    ++context_menu_->item_update_revision;
    ++context_menu_->item_order_revision;
    const auto lifetime = std::weak_ptr<int>(callback_lifetime_);
    context_menu_->on_command = [this, lifetime](const StringView command) {
        if (lifetime.expired()) return;
        SelectionEvent event{};
        if (command == "flagdir.new") createDir();
        else if (command == "flagdir.rename") beginCurrentEdit();
        else if (command == "flagdir.undo") undo();
        else if (command == "flagdir.redo") redo();
        else if (command == "flagdir.cut") cut(event);
        else if (command == "flagdir.copy") copy(event);
        else if (command == "flagdir.paste") paste(event);
        else if (command == "flagdir.delete") del(event);
        else if (command == "flagdir.clone") clone(event);
        else if (command == "flagdir.event.init") copyInitTree();
        else if (command == "flagdir.event.flag-on") copyFlagTree(true);
        else if (command == "flagdir.event.flag-off") copyFlagTree(false);
        else if (command == "flagdir.event.flag-reverse") copyFlagReverseTree();
        else if (command == "flagdir.event.step-up") copyStepUpTree();
        else if (command == "flagdir.event.step-down") copyStepDownTree();
        else if (command.starts_with("flagdir.event.step-value.")) {
            const auto value = command.substr(StringView{"flagdir.event.step-value."}.size());
            try {
                copyStepTree(static_cast<std::uint32_t>(std::stoul(String(value))));
            } catch (...) {
                return;
            }
        }
    };
}

void FlagDirTree::showContextMenu(const lib::WidgetPtr& owner, const lib::Point position) {
    rebuildContextMenu();
    if (context_menu_ && comm_.services.gui)
        comm_.services.gui->show_context_menu(context_menu_, owner, position);
}

FlagsPane::FlagsPane(Commons comm, Props prop)
    : comm_(std::move(comm)), prop_(std::move(prop)),
      undo_(prop_.etc.undoMaxMainView), flags_(comm_, prop_, undo_, false, false),
      dirs_(comm_, prop_, flags_, undo_), vertical_(prop_.etc.flagSashV) {
    scenarioToken_ = comm_.refScenario.connect([this] { onScenarioChanged(); });
    undoMaxToken_ = comm_.refUndoMax.connect([this] { onUndoMaximumChanged(); });
}

FlagsPane::~FlagsPane() {
    sash_lifetime_.reset();
    if (sash_) sash_->removeSelectionListener(sash_selection_token_);
    flags_.dispose();
    comm_.refScenario.remove(scenarioToken_);
    comm_.refUndoMax.remove(undoMaxToken_);
}

Parent FlagsPane::construct(const Parent& parent) {
    if (constructed_) return component_;
    parent_ = parent;
    if (const auto owner = std::dynamic_pointer_cast<lib::Window>(parent))
        dialogContext(comm_.summary.lock(), owner);
    component_ = std::make_shared<lib::Panel>();
    component_->set_layout(std::make_shared<lib::FillLayout>());
    sash_ = std::make_shared<splitpane::SplitPane>(
        vertical_ ? splitpane::Vertical : splitpane::Horizontal);
    sash_->setWeights({prop_.etc.flagSashL, prop_.etc.flagSashR});
    const std::weak_ptr<int> life = sash_lifetime_;
    sash_selection_token_ = sash_->addSelectionListener(
        [this, life](splitpane::SelectionEvent&) { if (!life.expired()) storeSash(); });
    (void)dirs_.createControl(sash_, [this] { rememberFocus(true); });
    (void)flags_.createControl(sash_, sash_, [this] { rememberFocus(false); });
    component_->add(sash_);
    if (parent) parent->add(component_);
    constructed_ = true;
    dirs_.refresh();
    return component_;
}

void FlagsPane::setupTLP(TopLevelPanel& panel) {
    panel.changeVHAction = [this] { changeVHSide(); };
}

const String& FlagsPane::statusLine() const noexcept {
    return flags_.statusLine();
}

void FlagsPane::rememberFocus(const bool directory) {
    focusDirectory_ = directory;
}

void FlagsPane::changeVHSide() {
    dirs_.enterEdit();
    flags_.enterEdit();
    vertical_ = !vertical_;
    prop_.etc.flagSashV = vertical_;
    if (sash_) {
        sash_->setOrientation(vertical_ ? lib::Orientation::Vertical
                                       : lib::Orientation::Horizontal);
        sash_->setWeights({prop_.etc.flagSashL, prop_.etc.flagSashR});
    }
    flags_.updateIncSearchParent(sash_);
    storeSash();
}

void FlagsPane::storeSash() {
    if (sash_) {
        const auto weights = sash_->getWeights();
        if (weights.size() >= 2U) {
            prop_.etc.flagSashL = weights[0];
            prop_.etc.flagSashR = weights[1];
        }
    }
    prop_.etc.flagSashV = vertical_;
    prop_.synchronize();
    const auto store = prop_.etc.storeFlagSash;
    if (store) store(vertical_, prop_.etc.flagSashL, prop_.etc.flagSashR);
}

bool FlagsPane::canCopyVariablePath() const noexcept {
    return flags_.canCopyVariablePath();
}

void FlagsPane::copyVariablePath() {
    flags_.copyVariablePath();
}

void FlagsPane::setFlagDirTree(const FlagDirPtr& root, UseCounterPtr uc) {
    // 同じ文書の更新ではコマンドを維持する。文書の置換に成功した場合は、 新しい
    // ルートの通知前に、以前のグラフを参照するクロージャーをすべて無効化する。
    dirs_.enterEdit();
    flags_.enterEdit();
    if (dirs_.rootDir() != root) {
        flags_.cancelEdit();
        undo_.reset();
    }
    comm_.useCounter = uc;
    auto summary = comm_.summary.lock();
    if (!summary && uc)
        summary = std::dynamic_pointer_cast<
            ::cwxeditor::cwx::summary::Summary>(uc->owner());
    dialogContext(summary, comm_.dialogOwner.lock());
    flags_.useCounter(uc);
    dirs_.useCounter(std::move(uc));
    dirs_.rootDir(root);
}

void FlagsPane::dialogContext(
    const std::shared_ptr<::cwxeditor::cwx::summary::Summary>& summary,
    const std::shared_ptr<lib::Window>& owner) {
    comm_.summary = summary;
    if (owner) comm_.dialogOwner = owner;
    flags_.dialogContext(summary, owner ? owner : comm_.dialogOwner.lock());
    dirs_.scenarioContext(summary);
}

bool FlagsPane::openCWXPath(String path, const bool shellActivate) {
    return dirs_.openCWXPath(std::move(path), shellActivate);
}

std::vector<String> FlagsPane::openedCWXPath() const {
    auto result = dirs_.openedCWXPath();
    const auto tablePaths = flags_.openedCWXPath();
    result.insert(result.end(), tablePaths.begin(), tablePaths.end());
    return result;
}

void FlagsPane::edit() {
    flags_.edit();
}

bool FlagsPane::canEdit() const noexcept {
    return flags_.canEdit();
}

void FlagsPane::addFlagsAndSteps(
    const lib::Dictionary<String, lib::Array<importutils::ImportFlag>>& flags,
    const lib::Dictionary<String, lib::Array<importutils::ImportStep>>& steps,
    const lib::Dictionary<String, lib::Array<importutils::ImportVariant>>& variants) {
    // D版の flagspane.addFlagsAndSteps の外部取り込み条件は、FlagTable
    // のクリップボード貼り付けと意図的に異なる。 コモンだけの取り込みは、この経路に入れない。
    if (flags.empty() && steps.empty()) return;
    const auto root = dirs_.rootDir();
    if (!root) return;
    dirs_.enterEdit();
    this->flags_.enterEdit();
    // D版の storeAll/UndoAllVariables と同様、FTVUndo の選択復元も引き継ぐ。
    this->flags_.beginMutation();
    FlagList selectedFlags;
    StepList selectedSteps;
    VariantList selectedVariants;
    FlagDirPtr lastDir;
    for (const auto& [path, values] : steps) {
        const auto dir = root->findPath(path, true);
        if (!dir) continue;
        lastDir = dir;
        for (const auto& imported : values) {
            if (!imported.step) continue;
            if (const auto existing = dir->getStep(imported.step->name())) {
                existing->copyFrom(*imported.step);
                selectedSteps.push_back(existing);
            } else {
                dir->add(imported.step);
                selectedSteps.push_back(imported.step);
            }
        }
    }
    for (const auto& [path, values] : flags) {
        const auto dir = root->findPath(path, true);
        if (!dir) continue;
        lastDir = dir;
        for (const auto& imported : values) {
            if (!imported.flag) continue;
            if (const auto existing = dir->getFlag(imported.flag->name())) {
                existing->copyFrom(*imported.flag);
                selectedFlags.push_back(existing);
            } else {
                dir->add(imported.flag);
                selectedFlags.push_back(imported.flag);
            }
        }
    }
    for (const auto& [path, values] : variants) {
        const auto dir = root->findPath(path, true);
        if (!dir) continue;
        lastDir = dir;
        for (const auto& imported : values) {
            if (!imported.variant) continue;
            if (const auto existing = dir->getVariant(imported.variant->name())) {
                existing->copyFrom(*imported.variant);
                selectedVariants.push_back(existing);
            } else {
                dir->add(imported.variant);
                selectedVariants.push_back(imported.variant);
            }
        }
    }
    undo_.commitSnapshot(root);
    if (lastDir) {
        dirs_.refresh(lastDir->path());
        this->flags_.deselectAll();
        for (const auto& value : selectedFlags) this->flags_.select(value, false);
        for (const auto& value : selectedSteps) this->flags_.select(value, false);
        for (const auto& value : selectedVariants) this->flags_.select(value, false);
    } else {
        dirs_.refresh();
    }
    comm_.refUseCount.call();
    if (comm_.refreshToolBar) comm_.refreshToolBar();
}

bool FlagsPane::canUndo() const noexcept { return undo_.canUndo(); }
bool FlagsPane::canRedo() const noexcept { return undo_.canRedo(); }

void FlagsPane::undo() {
    dirs_.cancelEdit();
    flags_.cancelEdit();
    // ディレクトリ単位の取り消し処理が、ツリーと表の両方の変更について、 スナップ
    // ショットの再接続と対応する UseCounter・リソースの通知を担当する。
    dirs_.undo();
}

void FlagsPane::redo() {
    dirs_.cancelEdit();
    flags_.cancelEdit();
    dirs_.redo();
}

void FlagsPane::replaceID() {
    dirs_.enterEdit();
    flags_.enterEdit();
    flags_.replaceID();
}

bool FlagsPane::canReplaceID() const noexcept {
    return flags_.canReplaceID();
}

bool FlagsPane::canDoTCPD() const noexcept {
    return focusDirectory_ ? dirs_.canDoTCPD() : flags_.canDoTCPD();
}

bool FlagsPane::canDoT() const noexcept {
    return focusDirectory_ ? dirs_.canDoT() : flags_.canDoT();
}

bool FlagsPane::canDoC() const noexcept {
    return focusDirectory_ ? dirs_.canDoC() : flags_.canDoC();
}

bool FlagsPane::canDoP() const noexcept {
    return focusDirectory_ ? dirs_.canDoP() : flags_.canDoP();
}

bool FlagsPane::canDoD() const noexcept {
    return focusDirectory_ ? dirs_.canDoD() : flags_.canDoD();
}

bool FlagsPane::canDoClone() const noexcept {
    return focusDirectory_ ? dirs_.canDoClone() : flags_.canDoClone();
}

void FlagsPane::cut(SelectionEvent& event) {
    if (focusDirectory_) dirs_.cut(event);
    else flags_.cut(event);
}

void FlagsPane::copy(SelectionEvent& event) {
    if (focusDirectory_) dirs_.copy(event);
    else flags_.copy(event);
}

void FlagsPane::paste(SelectionEvent& event) {
    if (focusDirectory_) dirs_.paste(event);
    else flags_.paste(event);
}

void FlagsPane::del(SelectionEvent& event) {
    if (focusDirectory_) dirs_.del(event);
    else flags_.del(event);
}

void FlagsPane::clone(SelectionEvent& event) {
    if (focusDirectory_) dirs_.clone(event);
    else flags_.clone(event);
}

void FlagsPane::onScenarioChanged() {
    dirs_.enterEdit();
    flags_.enterEdit();
    undo_.reset();
}

void FlagsPane::setUndoLimit(const std::size_t value) {
    prop_.etc.undoMaxMainView = value;
    prop_.synchronize();
    onUndoMaximumChanged();
}

void FlagsPane::onUndoMaximumChanged() {
    undo_.max(prop_.etc.undoMaxMainView);
}

void FlagsPane::setSearchText(String value) {
    flags_.setSearchText(std::move(value));
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::flagspane
