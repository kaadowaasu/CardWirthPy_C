#pragma once
#include <cstdint>
#include <span>
#include <vector>

namespace cwxeditor::lib {
// 可変の論理的な所有ツリーから独立した、物理資源の状態。generation
// は資源の生成世代を識別し、アドレスや再利用されるOSのIDではな
// い。parentは実際のネイティブ親の世代で、外部の親なら0。
// 借用した描画面はowns_surface=falseだが、描画
// 面の所有元を解放する前に、そのテクスチャを破棄する必要がある。
struct SurfaceLifetimeEntry {
    std::uint64_t generation{},parent{};
    std::uintptr_t surface{};
    bool owns_surface{},retired{};
};
// UIスレッド専用の計画。アダプターは有効な資源一覧と描画処
// 理で使用中の描画面を渡し、この順にネイティブ資源を解放す
// る。別描画面の使用中の子孫や有効な借用先も、所有元の解放
// を妨げる。循環や曖昧な世代は安全側に拒否する。循環時は独
// 立した項目を処理できるが、重複または0の世代では一覧全体
// を拒否する。コールバックや変更の後は再計算し、同じネイテ
// ィブアドレスに新規作成された資源へ古い計画を適用しない。
[[nodiscard]] std::vector<std::uint64_t> plan_surface_release(
    std::span<const SurfaceLifetimeEntry> entries,
    std::span<const std::uintptr_t> busy_surfaces);
} // namespace cwxeditor::lib
