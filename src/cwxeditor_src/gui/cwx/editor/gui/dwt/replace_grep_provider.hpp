#pragma once

#include "cwxeditor_src/gui/cwx/editor/gui/dwt/replacedialog.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dprops.hpp"
#include "cwxeditor_src/core/cwx/filesync.hpp"
#include "cwxeditor_src/lib/archive.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::replacegrep {

namespace replacedialog = ::cwxeditor::cwx::editor::gui::dwt::replacedialog;
namespace lib = ::cwxeditor::lib;
namespace dprops = ::cwxeditor::cwx::editor::gui::dwt::dprops;
namespace filesync = ::cwxeditor::cwx::filesync;
using String = lib::String;
using StringView = lib::StringView;
using ReplaceOptions = replacedialog::ReplaceOptions;
using ReplaceQueryContext = replacedialog::ReplaceQueryContext;
using ReplaceProviderResult = replacedialog::ReplaceProviderResult;
using TextTarget = replacedialog::TextTarget;
using CWXPath = replacedialog::CWXPath;
using CWXPathPtr = replacedialog::CWXPathPtr;

/// Dの外部検索は、別シナリオのCWXPathまたはJPy1/JPDC
/// を含むファイルを返す。 転送対象の正確な型を再利用
/// し、開く際に識別文字列を解析し直さないようにする。
using TargetKind = replacedialog::ExternalOpenKind;

struct ScenarioCandidate {
    std::filesystem::path path;
    bool directory{};
};

struct Match {
    TargetKind kind{TargetKind::ScenarioCWXPath};
    String scenarioPath;
    String relativePath;
    String cwxPath;
    String description;
    String text;
    std::size_t line{};
    String semanticKind;
    String resultGroup;
    std::vector<String> parts;
    bool wholeScenarioListing{};
    bool filePath{};
};

struct LoadedScenario {
    String title;
    std::vector<Match> matches;
    /// 標準のシナリオは先に読み込み、Dの検索中の段階
    /// を通知してから同期的に走査する。 集約用のアダ
    /// プターは、引き続き上の一致結果を提供できる。
    std::function<bool(const ReplaceQueryContext&,
                       const std::function<bool(Match)>&)> enumerateMatches;
    std::function<std::optional<replacedialog::ResultPresentation>(const Match&)> presentMatch;
};

/// メイン・モデル側から既存Summary読み込みへの接続を渡す。
/// CProps・Skin・一時保存の規則はそちらで所有し、この
/// UI提供元へ持ち込まない。 不正ファイルには値なしを返せる。
using ScenarioLoader = std::function<std::optional<LoadedScenario>(
    const ScenarioCandidate&, const ReplaceQueryContext&)>;
using MatchPredicate = std::function<bool(StringView, StringView,
                                          const ReplaceOptions&)>;

struct ProviderOptions {
    ScenarioLoader loadScenario;
    MatchPredicate matches;
    std::shared_ptr<lib::ArchiveBackend> archives;
    std::size_t batchSize{32U};
};

/// Summary::loadScenarioFromFileを使う製品用の読み込
/// み器を作る。 依存先は呼び出し元から渡すが、モデル構造の走査はここで行い、 MainWindow
/// は所有済みのProps/FileSyncを渡すだけにする。
using SymbolResolver = std::function<bool(const replacedialog::Summary*,
    const CWXPath&, bool, String&, lib::ResourceId&)>;
[[nodiscard]] ScenarioLoader makeStandardScenarioLoader(dprops::Props* props,
    filesync::FileSync* sync = nullptr, SymbolResolver symbols = {});

/// 転送方式に依存しないD形式の検索処理。シナリオディレク
/// トリ・アーカイブ候補は自身で探し、 解析はScenarioLoader
/// 経由で既存のモデル読み込みへ委譲する。
class Provider final {
public:
    explicit Provider(ProviderOptions options = {});

    [[nodiscard]] ReplaceProviderResult operator()(ReplaceQueryContext& context) const;
    [[nodiscard]] static std::vector<ScenarioCandidate> candidates(
        const std::filesystem::path& directory, bool recursive,
        const std::shared_ptr<lib::ArchiveBackend>& archives = {});

private:
    ProviderOptions options_;
};

}  // namespace cwxeditor::cwx::editor::gui::dwt::replacegrep
