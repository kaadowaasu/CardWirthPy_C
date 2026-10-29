#pragma once

#include "dialogs.hpp"

namespace cwxeditor::lib {

// 選んだ保存先の完了だけを所有し、書き込み器は所有しない。
// 選択直後に作成して提供元の一時領域の利用権を取りこぼさず
// 、commit前に書き込み器を閉じる。 DialogBackend
// はこのコピー不可のスコープガードより長く生存すること。
// 保存先は所有し、ブラウザーの永続化確認後にも完了で
// きるようにする。呼び出しは同じスレッドで直列化する。
class FileSaveTransaction final {
public:
    FileSaveTransaction(DialogBackend& dialogs, const std::filesystem::path& destination);
    FileSaveTransaction(const FileSaveTransaction&) = delete;
    FileSaveTransaction& operator=(const FileSaveTransaction&) = delete;
    ~FileSaveTransaction();

    // commitのfalse・例外後も、一時領域が残り得るためabort
    // を有効に保つ。 成功後の再commitは再公開せず、中止
    // 後・再帰中のcommitはバックエンドを呼ばず失敗する。
    [[nodiscard]] bool commit(std::string& error);
    // 未完了の利用権を一度だけ中止し、ここで保存先を削除し
    // ない。 後処理の例外で元の書き込み失敗を隠さず、公開
    // 中に再入したabortは結果が分かるまで遅らせる。
    void abort() noexcept;

private:
    enum class State { Pending, Publishing, Committed, Aborted };
    DialogBackend& dialogs_;
    std::filesystem::path destination_;
    State state_{State::Pending};
    bool abort_requested_{};
};

} // namespace cwxeditor::lib
