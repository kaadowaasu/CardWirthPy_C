#include "replace_error_provider.hpp"

#include "cwxeditor_src/core/cwx/jpy.hpp"
#include "cwxeditor_src/core/cwx/structs.hpp"
#include "cwxeditor_src/core/cwx/summary.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/core/cwx/versioninfo.hpp"
#include "cwxeditor_src/core/cwx/warning.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>

namespace cwxeditor::cwx::editor::gui::dwt::replaceerror {
namespace jpy = ::cwxeditor::cwx::jpy;
namespace structs = ::cwxeditor::cwx::structs;
namespace utils = ::cwxeditor::cwx::utils;
namespace versioninfo = ::cwxeditor::cwx::versioninfo;
namespace warning = ::cwxeditor::cwx::warning;
namespace {

String pathText(const std::filesystem::path& value) {
    return structs::pathToUtf8(value.lexically_normal());
}

String lower(String value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](const unsigned char ch) {
                       return static_cast<char>(std::tolower(ch));
                   });
    return value;
}

bool ignored(const std::vector<String>& values, const std::filesystem::path& path,
             StringView relative) {
    const auto name = lower(structs::pathToUtf8(path.filename()));
    const auto absolute = lower(pathText(path));
    const auto relative_path = lower(String(relative));
    return std::any_of(values.begin(), values.end(), [&](const String& value) {
        const auto candidate = lower(pathText(structs::pathFromUtf8(value)));
        // Summary の ignoredPath の規則を
        // 保ち、除外項目は末尾名・完全パスのどちらも指定できる
        // ようにする。 通常の設定はシナリオ相対パスだが、呼
        // び出し元が絶対パスを渡す場合も同じ動作を維持する。
        return candidate == name || candidate == relative_path || candidate == absolute ||
               utils::cglobMatch(structs::pathToUtf8(path.filename()), value) ||
               utils::cglobMatch(relative, value) || utils::cglobMatch(pathText(path), value);
    });
}

struct VersionSelection {
    bool classic{};
    String wsn;
    String target;
};

VersionSelection selectVersion(const ProviderConfig& config,
                               const replacedialog::ReplaceQueryContext& context) {
    VersionSelection result;
    result.classic = context.summary && context.summary->legacy();
    result.wsn = context.summary ? context.summary->dataVersion() : String{};
    result.target = config.defaultTargetVersion;
    if (context.errorVersion.starts_with("classic:")) {
        result.classic = true;
        result.wsn.clear();
        result.target = context.errorVersion.substr(8U);
    } else if (context.errorVersion.starts_with("wsn:")) {
        result.classic = false;
        result.wsn = context.errorVersion.substr(4U);
        result.target = "CardWirthPy";
    }
    return result;
}

replacedialog::TextTarget graphTarget(const replacedialog::CWXPathPtr& owner,
                                      String warning) {
    replacedialog::TextTarget result;
    result.owner = owner;
    result.path = owner ? owner->cwxPath(true) : String{};
    result.parentPath = ::cwxeditor::cwx::path::cpparent(result.path);
    result.kind = "error";
    result.description = std::move(warning);
    result.text = result.description;
    result.replaceable = false;
    return result;
}

replacedialog::TextTarget fileTarget(const replacedialog::SummaryPtr& summary,
                                     String relative, String warning) {
    replacedialog::TextTarget result;
    result.path = relative;
    result.parentPath = summary ? summary->scenarioPath() : String{};
    result.kind = "file-error";
    result.description = std::move(warning);
    result.text = result.description;
    result.replaceable = false;
    if (summary)
        result.externalOpenTarget = replacedialog::ExternalOpenTarget{
            replacedialog::ExternalOpenKind::File, summary->scenarioPath(),
            std::move(relative), {}, 0U};
    return result;
}

}  // namespace

Provider::Provider(ProviderConfig config) : config_(std::move(config)) {}

std::vector<lib::ChoiceItem> versionChoices() {
    std::vector<lib::ChoiceItem> result;
    for (std::size_t index = 0; index < versioninfo::VERSIONS.size(); ++index)
        result.push_back({versioninfo::ENGINES[index],
                          "wsn:" + versioninfo::VERSIONS[index], 0U, true});
    for (std::size_t index = 0; index < versioninfo::CLASSIC_VERSIONS.size(); ++index)
        result.push_back({versioninfo::CLASSIC_ENGINES[index],
                          "classic:" + versioninfo::CLASSIC_VERSIONS[index],
                          0U, true});
    return result;
}

replacedialog::ReplaceProviderResult Provider::operator()(
    replacedialog::ReplaceQueryContext& context) const {
    replacedialog::ReplaceProviderResult result;
    if (!context.summary || !config_.properties || !config_.skin) {
        return result;
    }
    const auto version = selectVersion(config_, context);
    std::vector<replacedialog::TextTarget> batch;
    const auto batch_size = std::max<std::size_t>(1U, config_.batchSize);
    const auto checkpoint = [&] {
        if (result.cancelled) return false;
        if (!context.checkpoint || context.checkpoint()) return true;
        result.cancelled = true;
        return false;
    };
    const auto publish = [&] {
        if (batch.empty()) return true;
        if (!context.publishBatch || !context.publishBatch(std::move(batch))) {
            result.cancelled = true;
            return false;
        }
        batch.clear();
        return true;
    };
    auto append = [&](replacedialog::TextTarget target) {
        // 一つのノードから多数の診断が出る場合がある。 ノード
        // 間だけでなく診断間でもキャンセルを確認し、 前の一括
        // 通知後に、大量の警告をすべて処理するまで待たせない。
        if (!checkpoint()) return false;
        batch.push_back(std::move(target));
        return batch.size() < batch_size || publish();
    };

    // selectedRange はチェックされた全範囲ノードを
    // 含み、既定の全選択では親リソースと子孫が重なる。 D版の
    // searchAll は同じノードを一度だけ訪れるため、
    // ここで重複を防ぎ、同じノードの警告を繰り返し通知しない。
    std::unordered_set<const replacedialog::CWXPath*> walked;
    std::function<bool(const replacedialog::CWXPathPtr&)> walk =
        [&](const replacedialog::CWXPathPtr& owner) {
            if (!owner || !walked.insert(owner.get()).second) return true;
            if (!checkpoint()) return false;
            ++result.scanned;
            for (auto& value : warning::warnings(
                     *config_.properties, *config_.skin, context.summary,
                     *owner, version.classic, version.wsn, version.target))
                if (!append(graphTarget(owner, std::move(value)))) return false;
            for (const auto& child : owner->cwxChilds())
                if (const auto value = child.lock(); value && !walk(value)) return false;
            return true;
        };
    for (const auto& owner : context.selectedRange)
        if (!walk(owner)) break;
    const auto root = structs::pathFromUtf8(context.summary->scenarioPath());
    std::unordered_map<String, std::filesystem::path> digests;
    std::error_code error;
    if (!result.cancelled && std::filesystem::exists(root, error)) {
        const bool root_directory = std::filesystem::is_directory(root, error);
        if (!error && root_directory &&
            !context.summary->isSystemFile(pathText(root), true) &&
            // D版と同様、ディレクトリの絞り込みには末尾の名前を使う
            // 。 「.」はルート直下の結果行の表示パスにすぎない。
            !ignored(config_.ignorePaths, root, pathText(root.filename()))) {
            ++result.scanned;
            for (auto& value : warning::sjisWarnings(
                     *config_.properties, context.summary,
                     structs::pathToUtf8(root.filename()),
                     config_.properties->requiredMsgs().dirName.value))
                if (!append(fileTarget(context.summary, ".", std::move(value)))) break;
        }
        const auto options = std::filesystem::directory_options::skip_permission_denied;
        for (std::filesystem::recursive_directory_iterator it(root, options, error), end;
             !error && it != end; it.increment(error)) {
            if (!checkpoint()) break;
            const auto file = it->path();
            const bool directory = it->is_directory(error);
            const auto relative = pathText(file.lexically_relative(root));
            if (context.summary->isSystemFile(pathText(file), directory) ||
                ignored(config_.ignorePaths, file, relative)) {
                if (directory) it.disable_recursion_pending();
                continue;
            }
            ++result.scanned;
            for (auto& value : warning::sjisWarnings(
                     *config_.properties, context.summary,
                     structs::pathToUtf8(file.filename()),
                     directory ? config_.properties->requiredMsgs().dirName.value
                               : config_.properties->requiredMsgs().fileName.value))
                if (!append(fileTarget(context.summary, relative, std::move(value)))) break;
            if (result.cancelled || directory) continue;
            const auto size = std::filesystem::file_size(file, error);
            if (error) { error.clear(); continue; }
            if (size == 0U) {
                if (!append(fileTarget(context.summary, relative,
                                       config_.properties->requiredMsgs().searchErrorEmptyFile.value)))
                    break;
                continue;
            }
            for (auto& value : warning::fileExtensionWarnings(*config_.properties,
                                                               pathText(file)))
                if (!append(fileTarget(context.summary, relative, std::move(value)))) break;
            if (result.cancelled) break;
            const auto hash = utils::fileToMD5Digest(file);
            if (hash.empty()) continue;
            const auto digest = hash + "-" + std::to_string(size);
            if (!digest.empty()) {
                if (const auto found = digests.find(digest); found != digests.end()) {
                    const auto duplicate = pathText(found->second.lexically_relative(root));
                    if (!append(fileTarget(
                            context.summary, relative,
                            utils::tryFormat(config_.properties->requiredMsgs().searchErrorDupFile.value,
                                             duplicate)))) break;
                    continue;
                } else {
                    digests.emplace(digest, file);
                }
            }
            try {
                const auto extension = lower(file.extension().generic_string());
                if (extension == ".jpy1")
                    (void)jpy::Jpy1::load(*config_.properties,
                                          context.summary->scenarioPath(), pathText(file));
                else if (extension == ".jptx")
                    (void)jpy::Jptx::load(*config_.properties, pathText(file));
                else if (extension == ".jpdc")
                    (void)jpy::Jpdc::load(*config_.properties,
                                          context.summary->scenarioPath(), pathText(file));
            } catch (const jpy::EffectBoosterError& exception) {
                for (const auto& item : exception.errors())
                    if (!append(fileTarget(
                            context.summary, relative,
                            utils::tryFormat(
                                config_.properties->requiredMsgs().jpyErrorInfoWithoutFile.value,
                                item.msg, std::to_string(item.line))))) break;
            } catch (...) {
                // D版と同様、不正な補助ファイルは解析処理が診断を
                // 返した場合に通知し、 それ以外は走査を続ける。
            }
            if (result.cancelled) break;
        }
    }
    if (!result.cancelled && !publish()) result.cancelled = true;
    result.completed = !result.cancelled;
    return result;
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::replaceerror
