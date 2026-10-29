#pragma once

#include "cwxeditor_src/lib/portable.hpp"

#include <condition_variable>
#include <cstddef>
#include <atomic>
#include <deque>
#include <exception>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

namespace cardwirth::platform {
class Storage;
}

namespace cwxeditor::cwx::filesync {

/// バックグラウンドスレッドで、永続化するファイル書き込みを順番に実行する。
class FileSync {
public:
    /// Storageポインターは借用する。そのルート内の書き込み
    /// はStorage::write_atomicへ送り、 ルー
    /// ト外のパスは従来のネイティブの不可分な置き換え処理を使う。
    explicit FileSync(cardwirth::platform::Storage* storage = nullptr);
    ~FileSync();

    /// ワーカーを一度だけ開始する。DのThreadベースのFileSyncと同じく
    /// 、 構築と開始前のpush()はキューを準備するだけで、I/Oを行わない。
    void start();
    [[nodiscard]] bool isRunning() const noexcept;

    FileSync(const FileSync&) = delete;
    FileSync& operator=(const FileSync&) = delete;
    FileSync(FileSync&&) = delete;
    FileSync& operator=(FileSync&&) = delete;

    /// キューに積まれたすべての書き込みが完了してから、終了するよう要求する。
    void quit();

    /// FIFOへ書き込みを追加する。キューの処理は呼び出し元のバッ
    /// ファーより長く残る場合があるため、 バイト列はコピーする。
    void push(std::filesystem::path file,
              lib::ByteView data,
              lib::Callback<void()> after);

    /// 全書き込みと完了コールバックの終了まで待つ。 キ
    /// ューの処理が終わってから、最初の非同期書き込み失
    /// 敗をここで再送出する。 ワーカーを維持しつつ、
    /// 呼び出し元が保存の失敗を確認できるようにする。
    void sync();
    bool syncUntilCancelled(const lib::Callback<bool()>& cancelled);

    /// 以後にキューへ積む書き込みで使う、借用Storageを置き換える。
    /// 登録済みの書き込みは、追加時に選ばれたStorageを使い続ける。
    void setStorage(cardwirth::platform::Storage* storage) noexcept;

    [[nodiscard]] cardwirth::platform::Storage* storage() const noexcept;

    /// 最後の非同期書き込み失敗を返す。エラーはclearError()
    /// まで保持し、 sync()が例外を投げた
    /// 後も呼び出し元から確認できるようにする。
    [[nodiscard]] bool failed() const noexcept;
    [[nodiscard]] std::string lastError() const;
    void clearError() noexcept;

private:
    struct PendingFile {
        std::filesystem::path file;
        lib::Bytes data;
        lib::Callback<void()> after;
        cardwirth::platform::Storage* storage{};
    };

    mutable std::mutex mutex_;
    std::mutex join_mutex_;
    std::condition_variable condition_;
    std::deque<PendingFile> files_;
    bool quit_requested_{};
    bool draining_{};
    bool started_{};
    std::atomic_bool worker_running_{};
    std::atomic<cardwirth::platform::Storage*> storage_{};
    std::thread worker_;
    std::thread::id worker_id_{};
    std::exception_ptr failure_;
    std::string failure_message_;

    void writeFiles();
    void run();
    void joinWorker();
    void rememberFailure(std::exception_ptr failure) noexcept;
    [[nodiscard]] bool isWorkerThread() const noexcept;
};

/// fsyncがnullなら直接書き込み、指定されていれば永続
/// 化書き込みをキューへ積む。 どちらも書き込み後にafter
/// を呼ぶ。直接書き込みでは任意の借用Storageを渡せ、
/// 保存先がそのルート内ならStorageへ操作を送る。
void writeFile(std::filesystem::path file,
               lib::ByteView data,
               lib::Owner<FileSync> fsync,
               lib::Callback<void()> after = {},
               cardwirth::platform::Storage* storage = nullptr);

/// 一時ファイルへ書き、永続化してから保存先を置き換える。I/O失
/// 敗を報告した場合はfalseを返す。 storageがありfile
/// がstorage->root()内ならStorage::write_atomic
/// を使う。 ルート外ではネイティブの一時ファイル置き換
/// えを使い、従来の「名前を付けて保存」の動作を保つ。
bool writeFileAndSync(
    std::filesystem::path file,
    lib::ByteView data,
    cardwirth::platform::Storage* storage = nullptr);

}  // namespace cwxeditor::cwx::filesync
