#include "cwxeditor_src/gui/cwx/editor/gui/dwt/undo.hpp"

#include <cstdint>

#include "cwxeditor_src/core/cwx/perf.hpp"

namespace cwxeditor::cwx::editor::gui::dwt::undo {

StrArrUndo::StrArrUndo(lib::Array<String> oldValue,
                       lib::Array<String> newValue, Setter setter)
    : TUndo<lib::Array<String>>(
          oldValue, newValue, std::move(setter),
          [](lib::Array<String> value) { return value; }) {}

UndoArr::UndoArr(UndoArray array, const bool reverse,
                 lib::Callback<void()> after)
    : array_(std::move(array)), reverse_(reverse), after_(std::move(after)) {}

void UndoArr::undo() {
    ::cwxeditor::cwx::perf::TraceScope traceScope(__FILE__, __LINE__);
    if (reverse_) {
        for (auto it = array_.rbegin(); it != array_.rend(); ++it) {
            if (*it) (*it)->undo();
        }
    } else {
        for (const auto& operation : array_) {
            if (operation) operation->undo();
        }
    }
    if (after_) after_();
}

void UndoArr::redo() {
    ::cwxeditor::cwx::perf::TraceScope traceScope(__FILE__, __LINE__);
    for (const auto& operation : array_) {
        if (operation) operation->redo();
    }
    if (after_) after_();
}

void UndoArr::dispose() {
    ::cwxeditor::cwx::perf::TraceScope traceScope(__FILE__, __LINE__);
    for (const auto& operation : array_) {
        if (operation) operation->dispose();
    }
}

UndoManager::UndoManager(const std::size_t max) : max_(max) {}

UndoManager& UndoManager::operator+=(UndoOwner undo) {
    add(std::move(undo));
    return *this;
}

void UndoManager::max(const std::size_t value) {
    ::cwxeditor::cwx::perf::TraceScope traceScope(__FILE__, __LINE__);
    if (operating_) return;
    max_ = value;
    if (max_ == 0U) {
        // D版と同様、適用済みの取り消し記録を無効にしても、 未適
        // 用のやり直し履歴は新しい編集まで使えるようにする。 適用
        // 済みの先頭部分だけを破棄し、やり直しの境界をゼロに移す。
        UndoArray removed(undos_.begin(),
                          undos_.begin() + static_cast<std::ptrdiff_t>(pointer_));
        undos_.erase(undos_.begin(),
                     undos_.begin() + static_cast<std::ptrdiff_t>(pointer_));
        pointer_ = 0U;
        ++revision_;
        disposeAll(std::move(removed));
        return;
    }
    disposeAll(cut());
}

void UndoManager::disposeAll(UndoArray commands) noexcept {
    // dispose() はプラグイン・モデル側のコード。 一つの履歴の終了で、残
    // りのコマンドの解放を漏らしたり、管理側のリセットから例外を出したりしない。
    for (const auto& command : commands) {
        if (!command) continue;
        try {
            command->dispose();
        } catch (...) {
        }
    }
}

UndoArray UndoManager::cut() {
    UndoArray removed;
    // D版と同様、適用済みの取り消し記録を制限し、やり直しの有効な境界は保つ。 max(0)
    // は max() 側で処理し、適用済みを破棄してもやり直しは残す。
    if (max_ >= pointer_) return removed;

    const auto first = pointer_ - max_;
    removed.assign(undos_.begin(),
                   undos_.begin() + static_cast<std::ptrdiff_t>(first));
    undos_.erase(undos_.begin(),
                 undos_.begin() + static_cast<std::ptrdiff_t>(first));
    pointer_ = max_;
    ++revision_;
    return removed;
}

void UndoManager::add(UndoOwner undo) {
    ::cwxeditor::cwx::perf::TraceScope traceScope(__FILE__, __LINE__);
    if (operating_) {
        UndoArray discarded;
        if (undo) discarded.push_back(std::move(undo));
        disposeAll(std::move(discarded));
        return;
    }
    if (max_ == 0U) {
        // 取り消しの記録が無効でも、新しい編集では保持中のやり直し履歴を無効にする。
        auto discarded = std::move(undos_);
        undos_.clear();
        pointer_ = 0U;
        ++revision_;
        if (undo) discarded.push_back(std::move(undo));
        disposeAll(std::move(discarded));
        return;
    }

    UndoArray discarded;

    if (!undos_.empty() && pointer_ < undos_.size()) {
        discarded.assign(undos_.begin() + static_cast<std::ptrdiff_t>(pointer_),
                         undos_.end());
        undos_.erase(undos_.begin() +
                         static_cast<std::ptrdiff_t>(pointer_),
                     undos_.end());
    }

    if (undos_.size() >= max_) {
        const auto count = undos_.size() - max_ + 1;
        discarded.insert(discarded.end(), undos_.begin(),
                         undos_.begin() + static_cast<std::ptrdiff_t>(count));
        undos_.erase(undos_.begin(),
                     undos_.begin() + static_cast<std::ptrdiff_t>(count));
    }

    undos_.push_back(std::move(undo));
    pointer_ = undos_.size();
    ++revision_;
    disposeAll(std::move(discarded));
}

bool UndoManager::undo() {
    ::cwxeditor::cwx::perf::TraceScope traceScope(__FILE__, __LINE__);
    if (operating_ || !canUndo()) return false;
    // 操作が例外を出したら、位置を変更しない。
    // D版の例外に安全な減算順序に合わせる。
    const auto command = undos_[pointer_ - 1U];
    const auto revision = revision_;
    operating_ = true;
    try {
        if (command) command->undo();
    } catch (...) {
        operating_ = false;
        throw;
    }
    operating_ = false;
    // 再入したコマンドのコールバックが履歴をリセット・分岐変更した場合は、その
    // 位置を減らさない。 ここまでは command が自身の寿命を保持する。
    if (revision_ == revision) --pointer_;
    return true;
}

bool UndoManager::redo() {
    ::cwxeditor::cwx::perf::TraceScope traceScope(__FILE__, __LINE__);
    if (operating_ || !canRedo()) return false;
    const auto command = undos_[pointer_];
    const auto revision = revision_;
    operating_ = true;
    try {
        if (command) command->redo();
    } catch (...) {
        operating_ = false;
        throw;
    }
    operating_ = false;
    if (revision_ != revision) return true;
    ++pointer_;
    disposeAll(cut());
    return true;
}

void UndoManager::reset() {
    ::cwxeditor::cwx::perf::TraceScope traceScope(__FILE__, __LINE__);
    if (undos_.empty()) return;
    dispose();
}

void UndoManager::dispose() {
    ::cwxeditor::cwx::perf::TraceScope traceScope(__FILE__, __LINE__);
    // D版への意図的なコールバック安全策。取り消し中にエディタの
    // 値が置換されたら、旧世代を直ちに無効化する。 実行中のコ
    // マンドは undo()/redo() が保持し、変更番号
    // の検査で、新しくリセットされた履歴を進めないようにする。
    if (undos_.empty()) return;
    auto discarded = std::move(undos_);
    undos_.clear();
    pointer_ = 0;
    ++revision_;
    disposeAll(std::move(discarded));
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::undo
