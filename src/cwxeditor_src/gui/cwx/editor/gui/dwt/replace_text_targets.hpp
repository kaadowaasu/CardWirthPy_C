#pragma once
#include "replacedialog.hpp"
#include "cwxeditor_src/core/cwx/filesync.hpp"
#include "cwxeditor_src/core/cwx/props.hpp"
#include "cwxeditor_src/core/cwx/usecounter.hpp"
#include <algorithm>
namespace cwxeditor::cwx::editor::gui::dwt::replacedialog::replace_text {
using Strings = std::vector<String>;
using Emit = std::function<bool(TextTarget)>;

template<class ID> ID referenceId(const lib::Owner<usecounter::IDToUser_t<ID>>& u) {
    if constexpr (std::is_same_v<ID, usecounter::AreaId>) return ID(u->area());
    else if constexpr (std::is_same_v<ID, usecounter::BattleId>) return ID(u->battle());
    else if constexpr (std::is_same_v<ID, usecounter::PackageId>) return ID(u->packages());
    else if constexpr (std::is_same_v<ID, usecounter::CastId>) return ID(u->casts());
    else if constexpr (std::is_same_v<ID, usecounter::SkillId>) return ID(u->skill());
    else if constexpr (std::is_same_v<ID, usecounter::ItemId>) return ID(u->item());
    else if constexpr (std::is_same_v<ID, usecounter::BeastId>) return ID(u->beast());
    else if constexpr (std::is_same_v<ID, usecounter::InfoId>) return ID(u->info());
    else if constexpr (std::is_same_v<ID, usecounter::FlagId>) return ID(u->flag());
    else if constexpr (std::is_same_v<ID, usecounter::StepId>) return ID(u->step());
    else if constexpr (std::is_same_v<ID, usecounter::VariantId>) return ID(u->variant());
    else if constexpr (std::is_same_v<ID, usecounter::CouponId>) return ID(u->coupon());
    else if constexpr (std::is_same_v<ID, usecounter::GossipId>) return ID(u->gossip());
    else if constexpr (std::is_same_v<ID, usecounter::CompleteStampId>) return ID(u->completeStamp());
    else if constexpr (std::is_same_v<ID, usecounter::KeyCodeId>) return ID(u->keyCode());
    else if constexpr (std::is_same_v<ID, usecounter::CellNameId>) return ID(u->cellName());
    else if constexpr (std::is_same_v<ID, usecounter::CardGroupId>) return ID(u->cardGroup());
}

// 単一参照の変更では、全Userと子カウンターに作用するcounter.change()
// を呼ばない。 通知側が実際のプロパティへ書いた後、その参
// 照の接続先カウンターで登録を合わせる。 特にグローバルの
// xからローカル宣言のyへ変えた場合、ローカルとして保つ。
template<class ID> bool replaceReference(
    const lib::Owner<usecounter::IDToUser_t<ID>>& user, const ID& next) {
    if (!user) return false;
    const auto before = referenceId<ID>(user);
    if (before == next) return true;
    const auto route = user->useCounter();
    if (!route) return false;
    if (!user->change(next)) return false;
    // ネイティブのモデル通知は、スカラーのキャッシュ反映中に対象の登録を移す場
    // 合がある。 Dの直接のUser.changeにはこの処理がないため、残っ
    // ている旧登録だけを除く。 ローカル限定のvalues()でなく、UseCounter::remove
    // と同じローカル・グローバルの振り分けを使う。
    const auto old_route = route->hasID(before) ? route : route->global();
    if (old_route) {
        const auto old_users = old_route->values(before);
        if (std::find(old_users.begin(), old_users.end(), user) != old_users.end())
            route->remove(before, user);
    }
    // 通知で式のUserが終了・再構成される場合がある。古いUserを復活させない。
    if (const auto current = user->useCounter()) {
        if constexpr (std::is_integral_v<decltype(next.id)>) { if (next.id > 0) current->add(next, user); }
        else if (!next.id.empty()) current->add(next, user);
    }
    return true;
}

bool enumerate(const SummaryPtr& summary, const ReplaceOptions& options,
               bool definitionsSelected, const Emit& emit,
               const system::System* system = nullptr);
bool enumerateFiles(const SummaryPtr& summary, const ReplaceOptions& options,
                    const Strings& extensions, const props::CProps& props,
                    const lib::Owner<filesync::FileSync>& sync, const Emit& emit);
} // namespace
