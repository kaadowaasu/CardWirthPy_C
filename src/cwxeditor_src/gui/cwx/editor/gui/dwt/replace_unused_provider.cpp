#include "cwxeditor_src/gui/cwx/editor/gui/dwt/replace_unused_provider.hpp"

#include "cwxeditor_src/core/cwx/area.hpp"
#include "cwxeditor_src/core/cwx/event.hpp"
#include "cwxeditor_src/core/cwx/flag.hpp"
#include "cwxeditor_src/core/cwx/skin.hpp"
#include "cwxeditor_src/core/cwx/summary.hpp"

#include <algorithm>
#include <functional>
#include <memory>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::replacedialog {
namespace {

namespace area = ::cwxeditor::cwx::area;
namespace event = ::cwxeditor::cwx::event;
namespace flag = ::cwxeditor::cwx::flag;
namespace usecounter = ::cwxeditor::cwx::usecounter;

/// 各候補でキャンセルを確認しつつ、ダイアログの境界で
/// は一括反映する。 D版は結果を順次表示するが、ここ
/// でもキャンセル時に確定済みのまとまりは保持する。
class Publisher final {
public:
    explicit Publisher(ReplaceQueryContext& context)
        : context_(context), batch_limit_(std::max<std::size_t>(
                                1U, context.options.searchResultRefreshCount)) {}

    [[nodiscard]] bool checkpoint() {
        if (cancelled_) return false;
        if (context_.checkpoint && !context_.checkpoint()) {
            cancelled_ = true;
            return false;
        }
        return true;
    }

    [[nodiscard]] bool scanned() {
        ++result_.scanned;
        return checkpoint();
    }

    [[nodiscard]] bool add(TextTarget target) {
        batch_.push_back(std::move(target));
        return batch_.size() < batch_limit_ || publish();
    }

    [[nodiscard]] bool publish() {
        if (batch_.empty()) return !cancelled_;
        if (!checkpoint()) return false;
        if (context_.publishBatch && !context_.publishBatch(std::move(batch_))) {
            cancelled_ = true;
            batch_.clear();
            return false;
        }
        batch_.clear();
        return true;
    }

    [[nodiscard]] ReplaceProviderResult finish() {
        if (!cancelled_) (void)publish();
        result_.cancelled = cancelled_;
        result_.completed = !cancelled_;
        return result_;
    }

    [[nodiscard]] bool cancelled() const noexcept { return cancelled_; }

private:
    ReplaceQueryContext& context_;
    std::size_t batch_limit_;
    std::vector<TextTarget> batch_;
    ReplaceProviderResult result_;
    bool cancelled_{};
};

template <typename T>
[[nodiscard]] CWXPathPtr asPath(const std::shared_ptr<T>& value) {
    if (!value) return {};
    // 三種類のAbstractArea
    // リソースは、AbstractArea
    // 側を経由して共通のCWXPath
    // として扱う。
    if constexpr (std::is_base_of_v<area::AbstractArea, T>) {
        return std::static_pointer_cast<CWXPath>(
            std::static_pointer_cast<area::AbstractArea>(value));
    } else {
        return std::static_pointer_cast<CWXPath>(value);
    }
}

[[nodiscard]] TextTarget unusedTarget(const CWXPathPtr& owner, String kind) {
    TextTarget target;
    target.owner = owner;
    target.path = owner ? owner->cwxPath(true) : String{};
    target.kind = std::move(kind);
    target.text = target.path;
    target.replaceable = false;
    return target;
}

template <typename Collection, typename ToId>
bool appendUnused(const Collection& values, bool enabled,
                  ToId&& to_id, StringView kind, const SummaryPtr& summary,
                  Publisher& publisher) {
    if (!enabled) return true;
    const auto counter = summary ? summary->useCounter() : nullptr;
    for (const auto& value : values) {
        if (!publisher.scanned()) return false;
        if (!value || (counter && counter->get(to_id(*value)) != 0U)) continue;
        if (!publisher.add(unusedTarget(asPath(value), String(kind)))) return false;
    }
    return true;
}

template <typename T>
void sortFlagPath(std::vector<std::shared_ptr<T>>& values, bool logical_sort) {
    std::stable_sort(values.begin(), values.end(), [logical_sort](const auto& left,
                                                                    const auto& right) {
        if (!left || !right) return static_cast<bool>(left);
        return flag::pathCompare(left->path(), right->path(), logical_sort) < 0;
    });
}

void collectEventTrees(const CWXPathPtr& root,
                       std::unordered_set<const CWXPath*>& walked,
                       std::unordered_set<const event::EventTree*>& added,
                       std::vector<event::EventTreePtr>& result) {
    if (!root || !walked.insert(root.get()).second) return;
    if (const auto tree = std::dynamic_pointer_cast<event::EventTree>(root);
        tree && added.insert(tree.get()).second)
        result.push_back(tree);
    if (const auto owner = std::dynamic_pointer_cast<event::EventTreeOwner>(root)) {
        for (const auto& tree : owner->trees()) {
            if (tree && added.insert(tree.get()).second) result.push_back(tree);
        }
    }
    for (const auto& child : root->cwxChilds()) {
        if (const auto next = child.lock()) collectEventTrees(next, walked, added, result);
    }
}

[[nodiscard]] std::uint32_t startReferenceCount(const event::EventTreePtr& tree,
                                                  StringView start_name) {
    if (!tree || start_name.empty()) return 0U;
    std::uint32_t result{};
    std::vector<event::ContentPtr> pending(tree->starts().begin(), tree->starts().end());
    std::unordered_set<const event::Content*> visited;
    while (!pending.empty()) {
        const auto content = std::move(pending.back());
        pending.pop_back();
        if (!content || !visited.insert(content.get()).second) continue;
        if ((content->type() == types::CType::LinkStart ||
             content->type() == types::CType::CallStart) &&
            content->start() == start_name)
            ++result;
        pending.insert(pending.end(), content->next().begin(), content->next().end());
    }
    return result;
}

bool appendUnusedStarts(ReplaceQueryContext& context, Publisher& publisher) {
    if (!context.options.unusedStart) return true;
    // D版の searchUnuseStart と同様、選択範囲
    // だけを走査し、インデックス1から始める。 先頭のスタートは
    // イベントの入口なので、未使用スタートとして結果に出さない。
    std::unordered_set<const CWXPath*> walked;
    std::unordered_set<const event::EventTree*> added;
    std::vector<event::EventTreePtr> trees;
    for (const auto& root : context.selectedRange)
        collectEventTrees(root, walked, added, trees);

    for (const auto& tree : trees) {
        if (!publisher.checkpoint()) return false;
        const auto& starts = tree->starts();
        for (std::size_t index = 1U; index < starts.size(); ++index) {
            if (!publisher.scanned()) return false;
            const auto& start = starts[index];
            if (!start || startReferenceCount(tree, start->name()) != 0U) continue;
            if (!publisher.add(unusedTarget(asPath(start), "unusedstart"))) return false;
        }
    }
    return true;
}

bool appendUnusedPaths(ReplaceQueryContext& context,
                       const UnusedProviderDependencies& dependencies,
                       Publisher& publisher) {
    if (!context.options.unusedPath) return true;
    // Summary::notUsedFiles
    // で未使用素材を走査する。Summary
    // とSkinが必要。除外パスが未
    // 指定なら空の除外一覧を使う。
    if (!context.summary || !dependencies.skin) return true;
    const lib::Array<String> no_ignored_paths;
    const auto& ignored = dependencies.ignorePaths ? *dependencies.ignorePaths
                                                   : no_ignored_paths;
    const auto paths = context.summary->notUsedFiles(*dependencies.skin, ignored,
                                                      context.options.logicalSort);
    for (const auto& value : paths) {
        if (!publisher.scanned()) return false;
        TextTarget target;
        target.path = value;
        target.text = value;
        target.kind = "unusedpath";
        target.replaceable = false;
        // D版と同様、FilePathString にシナリオの識別情報を添える。
        // 相対名を CWXPath として開こうとせず、ファイル用の経路を維持する。
        ExternalOpenTarget open;
        open.kind = ExternalOpenKind::File;
        open.scenarioPath = context.summary->scenarioPath();
        open.relativePath = value;
        target.externalOpenTarget = std::move(open);
        if (!publisher.add(std::move(target))) return false;
    }
    return true;
}

} // namespace

ReplaceProviderResult UnusedListingProvider::run(
    ReplaceQueryContext& context,
    const UnusedProviderDependencies& dependencies) {
    Publisher publisher(context);
    const auto summary = context.summary;
    if (!summary || !publisher.checkpoint()) return publisher.finish();

    if (const auto root = summary->flagDirRoot()) {
        auto flags = root->allFlags();
        auto steps = root->allSteps();
        auto variants = root->allVariants();
        sortFlagPath(flags, context.options.logicalSort);
        sortFlagPath(steps, context.options.logicalSort);
        sortFlagPath(variants, context.options.logicalSort);
        if (!appendUnused(flags, context.options.unusedFlag,
                          [](const flag::Flag& value) {
                              return usecounter::toFlagId(value.path());
                          }, "unusedflag", summary, publisher) ||
            !appendUnused(steps, context.options.unusedStep,
                          [](const flag::Step& value) {
                              return usecounter::toStepId(value.path());
                          }, "unusedstep", summary, publisher) ||
            !appendUnused(variants, context.options.unusedVariant,
                          [](const flag::Variant& value) {
                              return usecounter::toVariantId(value.path());
                          }, "unusedvariant", summary, publisher))
            return publisher.finish();
    }

    if (!appendUnused(summary->areas(), context.options.unusedArea,
                      [](const area::Area& value) { return usecounter::toAreaId(value.id()); },
                      "unusedarea", summary, publisher) ||
        !appendUnused(summary->battles(), context.options.unusedBattle,
                      [](const area::Battle& value) { return usecounter::toBattleId(value.id()); },
                      "unusedbattle", summary, publisher) ||
        !appendUnused(summary->packages(), context.options.unusedPackage,
                      [](const area::Package& value) { return usecounter::toPackageId(value.id()); },
                      "unusedpackage", summary, publisher) ||
        !appendUnused(summary->casts(), context.options.unusedCast,
                      [](const card::CastCard& value) { return usecounter::toCastId(value.id()); },
                      "unusedcast", summary, publisher) ||
        !appendUnused(summary->skills(), context.options.unusedSkill,
                      [](const card::SkillCard& value) { return usecounter::toSkillId(value.id()); },
                      "unusedskill", summary, publisher) ||
        !appendUnused(summary->items(), context.options.unusedItem,
                      [](const card::ItemCard& value) { return usecounter::toItemId(value.id()); },
                      "unuseditem", summary, publisher) ||
        !appendUnused(summary->beasts(), context.options.unusedBeast,
                      [](const card::BeastCard& value) { return usecounter::toBeastId(value.id()); },
                      "unusedbeast", summary, publisher) ||
        !appendUnused(summary->infos(), context.options.unusedInfo,
                      [](const card::InfoCard& value) { return usecounter::toInfoId(value.id()); },
                      "unusedinfo", summary, publisher) ||
        !appendUnusedStarts(context, publisher) ||
        !appendUnusedPaths(context, dependencies, publisher))
        return publisher.finish();

    return publisher.finish();
}

} // namespace cwxeditor::cwx::editor::gui::dwt::replacedialog
