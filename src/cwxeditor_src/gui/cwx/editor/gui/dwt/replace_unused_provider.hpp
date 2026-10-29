#pragma once

// Summaryを使って段階的に処理する、置換画面の未使用
// タブの実装。 ウィンドウは所有せず、ホストがReplaceSource::unusedBatchTargets
// へ接続するときに 現在のSkin
// と除外素材パスを渡す。

#include "cwxeditor_src/gui/cwx/editor/gui/dwt/replacedialog.hpp"

namespace cwxeditor::cwx::editor::gui::dwt::replacedialog {

/// ReplaceQueryContextの外で持つ依存先。画面
/// は検索状態だけを所有する。 素材列挙はSummaryの操作で
/// 、現在のSkinと保存済みの除外パス設定を追加で必要とする。
/// ignorePathsがnullなら除外パスなしとする。
struct UnusedProviderDependencies {
    const Skin* skin{};
    const lib::Array<String>* ignorePaths{};
};

/// Dのreplacedialog.d:3597-3723相当。ReplaceQueryContext
/// 経由で結果を一括通知し、 Summaryは変更しない。ReplaceSource::unusedBatchTargets
/// へそのまま接続できる。
class UnusedListingProvider final {
public:
    [[nodiscard]] static ReplaceProviderResult run(
        ReplaceQueryContext& context,
        const UnusedProviderDependencies& dependencies);
};

} // namespace cwxeditor::cwx::editor::gui::dwt::replacedialog
