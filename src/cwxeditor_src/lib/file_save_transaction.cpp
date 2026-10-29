#include "file_save_transaction.hpp"

namespace cwxeditor::lib {

FileSaveTransaction::FileSaveTransaction(
    DialogBackend& dialogs, const std::filesystem::path& destination)
    : dialogs_(dialogs), destination_(destination) {}

FileSaveTransaction::~FileSaveTransaction() { abort(); }

bool FileSaveTransaction::commit(std::string& error) {
    if (state_ == State::Committed) { error.clear(); return true; }
    if (state_ != State::Pending) {
        error = "save transaction is not pending";
        return false;
    }
    state_ = State::Publishing;
    bool published{};
    try {
        published = dialogs_.finalize_save_file(destination_, true, error);
    } catch (...) {
        state_ = State::Pending;
        if (abort_requested_) abort();
        throw;
    }
    state_ = published ? State::Committed : State::Pending;
    if (!published && abort_requested_) abort();
    return published;
}

void FileSaveTransaction::abort() noexcept {
    if (state_ == State::Publishing) { abort_requested_ = true; return; }
    if (state_ != State::Pending) return;
    state_ = State::Aborted;
    try {
        std::string ignored;
        (void)dialogs_.finalize_save_file(destination_, false, ignored);
    } catch (...) {
        // 元の書き込み・反映の例外を保持する。 提供側は後処理の失
        // 敗を記録してよいが、消費・公開済みの保存先を削除しない。
    }
}

} // namespace cwxeditor::lib
