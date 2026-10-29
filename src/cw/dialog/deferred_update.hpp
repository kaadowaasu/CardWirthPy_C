#pragma once

#include "cw/dialog/presentation.hpp"

namespace cw::dialog {

// 入力に応じて更新するダイアログで共有する、UI スレッド専用の CallAfter
// 予約。キュー内のコールバックはダイアログを所有せず、予約の寿命も延ばさない。
class DeferredDialogUpdate {
    struct State { bool reserved{}; std::uint64_t generation{}; };
    std::shared_ptr<State> m_state=std::make_shared<State>();
public:
    DeferredDialogUpdate() = default;
    // モデルの複製・初期化は新しい寿命として扱い、古いコールバ
    // ックが保持する所有者のアドレスを新しいモデルへ移さない。
    DeferredDialogUpdate(const DeferredDialogUpdate&) : DeferredDialogUpdate() {}
    DeferredDialogUpdate& operator=(const DeferredDialogUpdate&) { cancel(); m_state=std::make_shared<State>(); return *this; }
    DeferredDialogUpdate(DeferredDialogUpdate&& other) : DeferredDialogUpdate() { other.cancel(); }
    DeferredDialogUpdate& operator=(DeferredDialogUpdate&& other) { cancel(); other.cancel(); m_state=std::make_shared<State>(); return *this; }
    ~DeferredDialogUpdate() { cancel(); }
    bool pending() const noexcept { return m_state->reserved; }
    void cancel() noexcept { m_state->reserved=false; ++m_state->generation; }

    bool request(std::function<void()> update) {
        if (!update) return false;
        if (m_state->reserved) return true;
        const auto generation=m_state->generation;
        m_state->reserved=true;
        const std::weak_ptr<State> weak=m_state;
        try {
            const bool accepted=post_common_dialog_input_update(
                [weak,generation,update=std::move(update)](const auto&,const auto&)->::cw::lib::Value {
                    const auto state=weak.lock();
                    if (!state || state->generation!=generation || !state->reserved) return {};
                    // 意図的なネイティブ側の寿命・例外境界。Py の wx 所
                    // 有コールバックは終了後まで残ったり、例外後もフラグが立っ
                    // たままになる場合がある。成功・失敗時にこの世代だけを終了
                    // させ、古い呼び出しが新しい世代を解除しないようにする。
                    try { update(); }
                    catch (...) {
                        if (state->generation==generation) state->reserved=false;
                        throw;
                    }
                    if (state->generation==generation) state->reserved=false;
                    return {};
                });
            if (!accepted && m_state->generation==generation) m_state->reserved=false;
            return accepted;
        } catch (...) {
            if (m_state->generation==generation) m_state->reserved=false;
            throw;
        }
    }
};

} // namespace cw::dialog
