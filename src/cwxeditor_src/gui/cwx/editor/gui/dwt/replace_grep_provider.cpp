#include "replace_grep_provider.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dutils.hpp"

#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dskin.hpp"
#include "cwxeditor_src/core/cwx/card.hpp"
#include "cwxeditor_src/core/cwx/event.hpp"
#include "cwxeditor_src/core/cwx/jpy.hpp"
#include "cwxeditor_src/core/cwx/structs.hpp"
#include "cwxeditor_src/core/cwx/summary.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "3rdparty/srell/srell.hpp"
#include "replace_text_targets.hpp"
#include "replace_regex.hpp"

#include <algorithm>
#include <cctype>
#include <system_error>
#include <new>
#include <exception>
#include <utility>

namespace cwxeditor::cwx::editor::gui::dwt::replacegrep {
namespace area = ::cwxeditor::cwx::area;
namespace card = ::cwxeditor::cwx::card;
namespace event = ::cwxeditor::cwx::event;
namespace jpy = ::cwxeditor::cwx::jpy;
namespace structs = ::cwxeditor::cwx::structs;
namespace summary_ns = ::cwxeditor::cwx::summary;
namespace utils = ::cwxeditor::cwx::utils;
namespace dskin = ::cwxeditor::cwx::editor::gui::dwt::dskin;
namespace {

struct LoadedScenarioGuard {
    summary_ns::Summary::Owner summary;
    ~LoadedScenarioGuard() {
        // キャンセルや結果受け取り側の例外でも、スコープ終了時の後処理を行う。
        try { if (summary) summary->delTemp(true); } catch (...) {}
    }
};

String lowerAscii(StringView value) {
    String result(value);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](const unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return result;
}

bool defaultMatches(const StringView query, const StringView text,
                    const ReplaceOptions& options) {
    if (query.empty()) return true;
    String source(text);
    if (options.ignoreReturnCode)
        source.erase(std::remove_if(source.begin(), source.end(),
                                    [](const char ch) {
                                        return ch == '\r' || ch == '\n';
                                    }),
                     source.end());
    if (options.regex) {
        try {
            const auto flags = replacedialog::replaceRegexOptions(options);
            return srell::regex_search(source,
                                       srell::u8cregex(String(query), flags));
        } catch (const srell::regex_error&) {
            return false;
        }
    }
    // D版と最終のダイアログの照合処理は、任意の位置でのワイルドカード
    // 一致を数える。 文字列全体への事前判定で、部分一致を落とさない。
    if (options.wildcard)
        return utils::Wildcard(query, !options.notIgnoreCase).count(source) != 0U;
    const String needle = options.notIgnoreCase ? String(query) : lowerAscii(query);
    const String haystack = options.notIgnoreCase ? source : lowerAscii(source);
    if (options.exact) return haystack == needle;
    return haystack.find(needle) != String::npos;
}

bool isArchiveExtension(const std::filesystem::path& path) {
    const auto extension = lowerAscii(path.extension().generic_string());
    return extension == ".wsn" || extension == ".zip" || extension == ".lzh" ||
           extension == ".lha" || extension == ".cab";
}

bool archiveHasSummary(const std::filesystem::path& path,
                       const std::shared_ptr<lib::ArchiveBackend>& archives) {
    if (!archives) return true;
    std::string error;
    const auto entries = archives->list(path, lib::ArchiveFormat::Automatic, error);
    // 任意のバックエンドが読めないだけで、アーカイブを拒否しない。 形式を
    // 扱えるかの判断は、モデル側の Summary ローダーを正とする。
    if (!error.empty()) return true;
    return std::any_of(entries.begin(), entries.end(), [](const lib::ArchiveEntry& entry) {
        const auto name = lowerAscii(structs::pathToUtf8(entry.path.filename()));
        return name == "summary.xml" || name == "summary.wsm";
    });
}

String pathText(const std::filesystem::path& value) {
    return structs::pathToUtf8(value.lexically_normal());
}

bool extensionMatches(const std::filesystem::path& path,
                      const std::vector<String>& extensions) {
    const auto extension = lowerAscii(path.extension().generic_string());
    return std::any_of(extensions.begin(), extensions.end(), [&](const String& value) {
        String expected = lowerAscii(value);
        if (!expected.empty() && expected.front() != '.') expected.insert(expected.begin(), '.');
        return extension == expected;
    });
}

String targetKindText(const TargetKind kind) {
    switch (kind) {
    case TargetKind::ScenarioCWXPath: return "scenario-cwxpath";
    case TargetKind::File: return "file";
    case TargetKind::JPy1: return "jpy1";
    case TargetKind::JPDC: return "jpdc";
    }
    return "scenario";
}

TextTarget asTextTarget(const Match& match, const LoadedScenario& scenario) {
    TextTarget result;
    result.path = match.kind == TargetKind::ScenarioCWXPath && !match.cwxPath.empty()
                      ? match.cwxPath
                      : (match.relativePath.empty() ? match.scenarioPath : match.relativePath);
    result.parentPath = match.scenarioPath;
    result.kind = match.semanticKind.empty() ? targetKindText(match.kind) : match.semanticKind;
    result.resultGroup = match.resultGroup.empty() ? String{} : match.scenarioPath + ":" + match.resultGroup;
    result.wholeScenarioListing = match.wholeScenarioListing;
    result.filePath = match.filePath;
    if (!match.parts.empty()) result.readParts = [parts = match.parts] { return parts; };
    result.description = match.description.empty() ? scenario.title : match.description;
    result.text = match.text;
    if (scenario.presentMatch) result.presentation = scenario.presentMatch(match);
    result.replaceable = false;
    result.externalOpenTarget = {match.kind, match.scenarioPath, match.relativePath,
                                 match.cwxPath, match.line};
    return result;
}

}  // namespace

Provider::Provider(ProviderOptions options) : options_(std::move(options)) {}

ScenarioLoader makeStandardScenarioLoader(dprops::Props* const props,
                                          filesync::FileSync* const sync,
                                          SymbolResolver symbols) {
    return [props, sync, symbols = std::move(symbols)](const ScenarioCandidate& candidate,
                         const ReplaceQueryContext& context) -> std::optional<LoadedScenario> {
        if (!props) return std::nullopt;
        (void)sync; // 読み取り専用の読み込み中のファイル同期方針は、Summary が管理する。
        summary_ns::LoadOption load;
        load.cardOnly = false;
        load.textOnly = true;
        load.expandXMLs = false;
        load.summaryOnly = false;
        lib::Array<String> errors;
        const auto skin = dskin::findSkin2(*props, props->var().defaultSkin.value,
                                           props->var().defaultSkinName.value);
        auto loaded_summary = summary_ns::Summary::loadScenarioFromFile(
            props->parent(), load, errors, pathText(candidate.path), props->tempPath(),
            skin.get(), {}, {}, {}, {}, {}, props->storage());
        if (!loaded_summary) return std::nullopt;

        LoadedScenario result;
        result.title = loaded_summary->scenarioName();
        const auto guard = std::make_shared<LoadedScenarioGuard>();
        guard->summary = loaded_summary;
        const auto scenario_path = pathText(candidate.path);
        result.presentMatch = [guard, scenario_path, props, symbols, skin,
            text_extensions = context.plainTextFileExtensions,
            show_route = context.options.showRouteOfSearchResult](const Match& match)
            -> std::optional<replacedialog::ResultPresentation> {
            if (!symbols) return std::nullopt;
            const auto& loaded_summary = guard->summary;
            String summary_text;
            lib::ResourceId summary_image{};
            if (!symbols(loaded_summary.get(), *loaded_summary, false, summary_text, summary_image))
                return std::nullopt;
            if (match.kind != TargetKind::ScenarioCWXPath) {
                replacedialog::ResultPresentation display;
                display.main = utils::encodePath(match.relativePath);
                // D版の fimage と同様、種類のアイコンを使い、補助ファイル自体を画像として使わない。
                const auto file = structs::pathFromUtf8(loaded_summary->scenarioPath()) /
                                  structs::pathFromUtf8(match.relativePath);
                const auto file_text = pathText(file);
                String icon = "unknown.png";
                try {
                    if (std::filesystem::exists(file)) {
                        if (std::filesystem::is_directory(file)) icon = "folder.png";
                        else if (skin && skin->isCardImage(file_text, false, false)) icon = "cards.png";
                        else if (skin && skin->isBgImage(file_text)) icon = "backs.png";
                        else if (skin && skin->isBGM(file_text)) icon = "evt_bgm.png";
                        else if (skin && skin->isSE(file_text)) icon = "evt_se.png";
                    }
                    if (icon == "unknown.png") {
                        const auto ext = pathText(file.extension());
                        for (const auto& pattern : text_extensions) {
                            if (utils::cfnmatch(ext, pattern)) { icon = "text.png"; break; }
                        }
                    }
                } catch (const std::exception&) {
                    // ファイル調査に失敗した場合の代替動作をD版に合わせる。
                }
                display.image = props->images().load(dprops::StringView(icon));
                display.description = utils::tryFormat(props->requiredMsgs().grepScenario.value,
                    loaded_summary->scenarioName(), scenario_path);
                std::erase(display.description, '\n');
                display.descriptionImage = summary_image;
                return display;
            }
            TextTarget target;
            target.owner = match.wholeScenarioListing
                ? std::static_pointer_cast<CWXPath>(loaded_summary)
                : loaded_summary->findCWXPath(match.cwxPath);
            if (!target.owner) return std::nullopt;

            replacedialog::ResultPresentation display;
            if (!symbols(loaded_summary.get(), *target.owner, true,
                         display.main, display.image)) return std::nullopt;
            if (display.main.empty()) display.main = match.cwxPath;
            const auto parent = target.owner->cwxParent().lock();
            std::vector<CWXPathPtr> route;
            const bool content = dynamic_cast<const event::Content*>(target.owner.get()) != nullptr;
            if (!content && dynamic_cast<const event::Content*>(parent.get())) route.push_back(parent);
            for (auto current = parent; current && !dynamic_cast<const summary_ns::Summary*>(current.get());) {
                auto next = current->cwxParent().lock();
                if (const auto* c = dynamic_cast<const event::Content*>(current.get())) {
                    if (c->type() != ::cwxeditor::cwx::types::CType::Start) { current = std::move(next); continue; }
                } else if (dynamic_cast<const ::cwxeditor::cwx::flag::FlagDir*>(current.get()) &&
                           next && dynamic_cast<const ::cwxeditor::cwx::flag::FlagDir*>(next.get())) {
                    current = std::move(next); continue;
                }
                route.push_back(current); current = std::move(next);
            }
            if (!show_route && route.size() > 1U)
                route.erase(route.begin(), route.end() - 1);
            for (auto it = route.rbegin(); it != route.rend(); ++it) {
                String part; lib::ResourceId image{};
                if (!symbols(loaded_summary.get(), **it, false, part, image)) return std::nullopt;
                if (display.parent.empty()) display.parentImage = image;
                else display.parent += " > ";
                display.parent += part;
            }
            display.description = utils::tryFormat(props->requiredMsgs().grepScenario.value,
                loaded_summary->scenarioName(), scenario_path);
            display.descriptionImage = summary_image;
            return display;
        };
        // 空の検索語のGrepはシナリオ一覧であり、テキストの各チェック設
        // 定や検索項目に依存しない。 その場合は補助ファイルを調べない。
        if (context.query.empty()) {
            return result;
        }
        result.enumerateMatches = [guard, scenario_path, props](
                const ReplaceQueryContext& context,
                const std::function<bool(Match)>& emit) {
            const auto& loaded_summary = guard->summary;
            const bool completed = replacedialog::replace_text::enumerate(loaded_summary, context.options,
                true, [&](TextTarget target) {
                    if (context.checkpoint && !context.checkpoint()) return false;
                    Match match;
                    match.kind = TargetKind::ScenarioCWXPath;
                    match.scenarioPath = scenario_path;
                    match.cwxPath = target.path.empty() ? "summary" : target.path;
                    match.text = target.value();
                    match.description = target.description;
                    match.semanticKind = target.kind;
                    match.filePath = target.filePath;
                    match.resultGroup = target.resultGroup;
                    if (target.readParts) match.parts = target.readParts();

                    return emit(std::move(match));
                }, &props->parent().sys());
            if (!completed) return false;

            // D版と同様、テキストタブで選択された素材側の文字データも検索す
            // る。 アーカイブの Summary::scenarioPath()
            // は展開したシナリオのルートなので、 提供側や MainWindow
            // にアーカイブの詳細を持ち込まず、同じ走査を使える。
            if (context.options.jptx || context.options.textFile) {
                const auto scenario_root = structs::pathFromUtf8(loaded_summary->scenarioPath());
                std::error_code error;
                const auto directory_options =
                    std::filesystem::directory_options::skip_permission_denied;
                for (std::filesystem::recursive_directory_iterator it(
                         scenario_root, directory_options, error), end;
                     !error && it != end; it.increment(error)) {
                    if (context.checkpoint && !context.checkpoint()) return false;
                    if (!it->is_regular_file(error)) continue;
                    const auto relative = pathText(it->path().lexically_relative(scenario_root));
                    std::optional<Match> file_match;
                    try {
                        if (context.options.jptx &&
                            lowerAscii(it->path().extension().generic_string()) == ".jptx") {
                            bool is_sjis{};
                            jpy::EffectBoosterError errors;
                            auto value = jpy::jptxText(jpy::readJPYFile(
                                pathText(it->path()), props->parent(), errors, is_sjis));
                            file_match = Match{TargetKind::JPy1, scenario_path,
                                                      relative, {}, "JPTX", std::move(value), 0U, "jptx"};
                        } else if (context.options.textFile &&
                                   extensionMatches(it->path(),
                                                    context.plainTextFileExtensions)) {
                            bool is_sjis{};
                            auto value = utils::readTextFile(it->path(), is_sjis);
                            file_match = Match{TargetKind::File, scenario_path,
                                                      relative, {}, "Text file",
                                                      std::move(value), 0U, "text-file"};
                        }
                    } catch (...) {
                        // 既存アダプターのファイル単位の復旧方針を維持する。
                        // D版が局所的に捕捉するのはJPTXだけで、通常ファイ
                        // ルでの続行は 別の未解決の互換性の境界として残る。
                    }
                    // 結果の通知は読み込みの復旧範囲の外に置く。 受け取り
                    // 側の失敗を、読めないファイルとして握りつぶさない。
                    if (file_match && !emit(std::move(*file_match))) return false;
                }
            }
            return true;
        };
        return result;
    };
}

std::vector<ScenarioCandidate> Provider::candidates(
    const std::filesystem::path& directory, const bool recursive,
    const std::shared_ptr<lib::ArchiveBackend>& archives) {
    std::vector<ScenarioCandidate> result;
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error) || error) return result;

    const auto add_directory = [&result](const std::filesystem::path& path) {
        const auto xml = path / "Summary.xml";
        const auto wsm = path / "Summary.wsm";
        std::error_code local_error;
        if (std::filesystem::is_regular_file(xml, local_error) ||
            std::filesystem::is_regular_file(wsm, local_error))
            result.push_back({path, true});
    };
    add_directory(directory);
    const auto options = std::filesystem::directory_options::skip_permission_denied;
    if (recursive) {
        for (std::filesystem::recursive_directory_iterator it(directory, options, error), end;
             !error && it != end; it.increment(error)) {
            if (it->is_directory(error)) {
                add_directory(it->path());
            } else if (it->is_regular_file(error) && isArchiveExtension(it->path()) &&
                       archiveHasSummary(it->path(), archives)) {
                result.push_back({it->path(), false});
            }
        }
    } else {
        for (std::filesystem::directory_iterator it(directory, options, error), end;
             !error && it != end; it.increment(error)) {
            if (it->is_directory(error)) {
                add_directory(it->path());
            } else if (it->is_regular_file(error) && isArchiveExtension(it->path()) &&
                       archiveHasSummary(it->path(), archives)) {
                result.push_back({it->path(), false});
            }
        }
    }
    std::sort(result.begin(), result.end(), [](const ScenarioCandidate& left,
                                                const ScenarioCandidate& right) {
        return pathText(left.path) < pathText(right.path);
    });
    result.erase(std::unique(result.begin(), result.end(),
                             [](const ScenarioCandidate& left,
                                const ScenarioCandidate& right) {
                                 return left.path == right.path;
                             }), result.end());
    return result;
}

ReplaceProviderResult Provider::operator()(ReplaceQueryContext& incoming) const {
    ReplaceProviderResult result;
    auto context = incoming;
    // D版と同様、モデルの読み込み失敗は候補ごとに復旧できる。 外部の
    // UI・通知コールバックの例外は、不正なシナリオとは別に扱う。
    // 保持されたラッパーに無効な参照が残らないよう、状態も共有する。
    const auto callback_failure = std::make_shared<std::exception_ptr>();
    const auto protect = [callback_failure](auto callback) {
        return [callback = std::move(callback), callback_failure](auto&&... args) -> decltype(auto) {
            if (*callback_failure) std::rethrow_exception(*callback_failure);
            try { return callback(std::forward<decltype(args)>(args)...); }
            catch (...) { *callback_failure = std::current_exception(); throw; }
        };
    };
    if (context.checkpoint) context.checkpoint = protect(context.checkpoint);
    if (context.publishBatch) context.publishBatch = protect(context.publishBatch);
    if (context.grepProgress) context.grepProgress = protect(context.grepProgress);
    const auto root = structs::pathFromUtf8(context.grepDirectory);
    if (!options_.loadScenario || root.empty()) return result;
    std::size_t successful{};
    const auto progress = [&](replacedialog::GrepPhase phase, String path, bool refresh) {
        return !context.grepProgress ||
            context.grepProgress({phase, successful, std::move(path), refresh});
    };
    std::vector<TextTarget> batch;
    // D版と同様、訪れた各一致を同期的に通知する。 状態通知のコ
    // ールバックからキャンセルしても、訪問済みの結果は捨てない。
    const std::size_t batch_size = context.grepProgress ? 1U
        : std::max<std::size_t>(1U, options_.batchSize);
    const auto publish = [&] {
        if (batch.empty()) return true;
        if (!context.publishBatch || !context.publishBatch(std::move(batch))) return false;
        batch.clear();
        return true;
    };
    const auto predicate = protect(options_.matches ? options_.matches : MatchPredicate{defaultMatches});
    for (const auto& candidate : candidates(root, context.grepSubDirectory, options_.archives)) {
        if (context.checkpoint && !context.checkpoint()) {
            result.cancelled = true;
            break;
        }
        if (!progress(replacedialog::GrepPhase::Loading, pathText(candidate.path), true)) {
            result.cancelled = true;
            break;
        }
        ++result.scanned; // 試行した候補数。D版の読み込み成功数としては使わない。
        std::optional<LoadedScenario> loaded;
        try {
            loaded = options_.loadScenario(candidate, context);
            if (*callback_failure) std::rethrow_exception(*callback_failure);
        } catch (const std::bad_alloc&) {
            throw;
        } catch (const std::exception& error) {
            if (*callback_failure) std::rethrow_exception(*callback_failure);
            utils::debugln("Grep scenario load failed: ", pathText(candidate.path), ": ", error.what());
            if (context.checkpoint && !context.checkpoint()) {
                result.cancelled = true;
                break;
            }
            continue;
        }
        if (loaded) {
            ++successful;
            if (!progress(replacedialog::GrepPhase::Loaded, pathText(candidate.path),
                          !context.query.empty())) {
                result.cancelled = true;
                break;
            }
        }
        if (context.checkpoint && !context.checkpoint()) {
            result.cancelled = true;
            break;
        }
        if (!loaded) continue;
        if (context.query.empty()) {
            Match whole;
            whole.kind = TargetKind::ScenarioCWXPath;
            whole.scenarioPath = pathText(candidate.path);
            whole.cwxPath = "summary";
            whole.wholeScenarioListing = true;
            batch.push_back(asTextTarget(whole, *loaded));
        } else {
            const auto consume = [&](Match match) {
                if (context.checkpoint && !context.checkpoint()) return false;
                const auto matches = [&](const String& part) {
                    return predicate(context.query, match.filePath ? utils::encodePath(part) : part,
                                     context.options);
                };
                const bool matched = match.parts.empty() ? matches(match.text)
                    : std::any_of(match.parts.begin(), match.parts.end(), matches);
                if (!matched) return true;
                batch.push_back(asTextTarget(match, *loaded));
                return batch.size() < batch_size || publish();
            };
            if (loaded->enumerateMatches) {
                try {
                    result.cancelled = !loaded->enumerateMatches(context, consume);
                    if (*callback_failure) std::rethrow_exception(*callback_failure);
                } catch (const std::bad_alloc&) {
                    throw;
                } catch (const std::exception& error) {
                    if (*callback_failure) std::rethrow_exception(*callback_failure);
                    utils::debugln("Grep scenario walk failed: ", pathText(candidate.path), ": ", error.what());
                    // 次の兄弟へ進む前に、訪問済みの結果を保持する。
                    // UIのGrepでは各結果をすでに通知している。
                    if ((context.checkpoint && !context.checkpoint()) || !publish()) {
                        result.cancelled = true;
                        break;
                    }
                    continue;
                }
            } else {
                for (const auto& match : loaded->matches) {
                    if (!consume(match)) { result.cancelled = true; break; }
                }
            }
        }
        if (result.cancelled) break;
        if (!publish()) { result.cancelled = true; break; }
        if (!context.query.empty() &&
            !progress(replacedialog::GrepPhase::Loaded, pathText(candidate.path), true)) {
            result.cancelled = true;
            break;
        }
    }
    (void)progress(replacedialog::GrepPhase::Finished, {}, false);
    result.completed = !result.cancelled;
    return result;
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::replacegrep
