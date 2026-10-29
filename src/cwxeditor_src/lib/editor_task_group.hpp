#pragma once

#include "threading.hpp"
#include "task_stop.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace cwxeditor::lib {

// Editorセッションの所有者が世代を渡す。 ホストが同じディス
// パッチャーを再利用しても、前のセッションのUI処理は拒否する。
using EditorSessionGeneration = std::uint64_t;

enum class EditorTaskTerminal : std::uint8_t {
    Completed,
    Stopped,
    Failed,
    Rejected,
};

struct EditorTaskResult {
    EditorTaskTerminal terminal{EditorTaskTerminal::Completed};
    std::string diagnostic;

    [[nodiscard]] static EditorTaskResult completed();
    [[nodiscard]] static EditorTaskResult stopped(std::string diagnostic = {});
    [[nodiscard]] static EditorTaskResult failed(std::string diagnostic = {});
    [[nodiscard]] static EditorTaskResult rejected(std::string diagnostic = {});
};

struct EditorTaskSnapshot {
    EditorSessionGeneration generation{};
    bool stop_requested{};
    bool finished{};
    EditorTaskResult result{};
};

class EditorTaskUi;

// タスクのコンテキストは、EditorTaskFunctionの一回の呼び出
// し中だけ有効。 ワーカーはstop_token()と下のUIメソッドを使え
// るが、Widget・Storage・ホスト画面の生ポインターは保持しない。
class EditorTaskContext final {
public:
    [[nodiscard]] TaskStopToken stop_token() const noexcept;
    [[nodiscard]] bool stop_requested() const noexcept;
    [[nodiscard]] EditorSessionGeneration generation() const noexcept;

    // コールバックはディスパッチャーのUIスレッドだけで実行する。
    // EditorのUIとは独立して有効な状態だけを捕捉すること。
    [[nodiscard]] bool post_ui(std::function<void()> callback) const;
    // 中止・古い世代の場合は、UI通知を無期限に待
    // たずStopped/Rejectedを返す。
    [[nodiscard]] EditorTaskResult invoke_ui(std::function<void()> callback) const;

private:
    friend class EditorTaskGroup;
    EditorTaskContext(TaskStopToken stop,
                      std::shared_ptr<EditorTaskUi> ui,
                      EditorSessionGeneration generation);

    TaskStopToken stop_;
    std::shared_ptr<EditorTaskUi> ui_;
    EditorSessionGeneration generation_{};
};

using EditorTaskFunction = std::function<EditorTaskResult(EditorTaskContext&)>;

// このハンドルは結果の参照・停止要求だけを行い、ワーカースレッドを所有
// しない。 UI上で破棄してもjoin・自己join・detachは
// 発生せず、EditorTaskGroupだけがワーカーを所有する。
class EditorTaskHandle final {
public:
    EditorTaskHandle() = default;
    void request_stop() noexcept;
    [[nodiscard]] EditorTaskSnapshot snapshot() const;
    [[nodiscard]] bool valid() const noexcept;

private:
    struct State;
    friend class EditorTaskGroup;
    explicit EditorTaskHandle(std::shared_ptr<State> state);
    std::shared_ptr<State> state_;
};

class EditorTaskUi final {
public:
    // 不透明な共有チケット状態。実装内のファイルローカル補助処
    // 理で使うため公開するが、 利用側向けの操作は公開しない。
    struct SharedState;

    ~EditorTaskUi();

    EditorTaskUi(const EditorTaskUi&) = delete;
    EditorTaskUi& operator=(const EditorTaskUi&) = delete;

    [[nodiscard]] bool post(EditorSessionGeneration generation,
                            std::function<void()> callback);
    [[nodiscard]] EditorTaskResult invoke(EditorSessionGeneration generation,
                                          std::function<void()> callback);

    // ワーカーのjoin前に、ディスパッチャーのUIスレッド
    // から呼ぶ。 このTaskUiのチケットだけを中止し、ホ
    // ストのキュー・タイマー・他のセッションには触れない。
    [[nodiscard]] bool cancel_pending_on_ui_thread() noexcept;
    [[nodiscard]] bool accepting() const noexcept;
    [[nodiscard]] bool is_ui_thread() const noexcept;
    [[nodiscard]] EditorSessionGeneration generation() const noexcept;

private:
    friend class EditorTaskGroup;
    // dispatcherは借用する。このクラスからdispatcher->shutdown()は呼ばない。
    EditorTaskUi(std::shared_ptr<MainDispatcher> dispatcher,
                 EditorSessionGeneration generation);
    std::shared_ptr<SharedState> state_;
};

enum class EditorTaskGroupState : std::uint8_t { Accepting, Stopping, Drained };

struct EditorTaskGroupSnapshot {
    EditorTaskGroupState state{EditorTaskGroupState::Accepting};
    std::size_t active_workers{};
    std::size_t pending_ui{};
    std::vector<EditorTaskSnapshot> tasks;
};

// 終了順序を明示し、違反はデストラクターで即座に検出する。 1. close_submission()
// 、2. ui().cancel_pending_on_ui_thread()
// 、 3. request_stop_all()、4. snapshot().state
// == Drainedを待つ、 5. UI以外の終了管理スレッドでjoin_all_after_ui_quiesced()
// を呼ぶ。 停止要求を無視するタスクは動作中のまま保持し、
// タイムアウト・detach・参照先状態の破棄は行わない。
// join前のグループ破棄はプログラムの誤りとして終了し
// 、UIを黙って待たせたりワーカーを切り離したりしない。
class EditorTaskGroup final {
public:
    EditorTaskGroup(std::shared_ptr<MainDispatcher> dispatcher,
                    EditorSessionGeneration generation);
    ~EditorTaskGroup();

    EditorTaskGroup(const EditorTaskGroup&) = delete;
    EditorTaskGroup& operator=(const EditorTaskGroup&) = delete;

    [[nodiscard]] EditorTaskHandle start(EditorTaskFunction task);
    void close_submission() noexcept;
    void request_stop_all();
    [[nodiscard]] EditorTaskGroupSnapshot snapshot() const;
    [[nodiscard]] EditorTaskUi& ui() noexcept;
    [[nodiscard]] const EditorTaskUi& ui() const noexcept;

    // UIスレッドからの呼び出し、UI中止前、または全ワーカー
    // の終了前ならfalseを返す。 detachは行わない。
    [[nodiscard]] bool join_all_after_ui_quiesced();

private:
    struct SharedState;
    std::shared_ptr<SharedState> state_;
    std::shared_ptr<EditorTaskUi> ui_;
};

[[nodiscard]] std::shared_ptr<EditorTaskGroup> make_editor_task_group(
    std::shared_ptr<MainDispatcher> dispatcher,
    EditorSessionGeneration generation);

}  // namespace cwxeditor::lib
