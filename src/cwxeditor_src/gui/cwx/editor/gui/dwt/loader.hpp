#pragma once

#include "cwxeditor_src/core/cwx/cab.hpp"
#include "cwxeditor_src/core/cwx/filesync.hpp"
#include "cwxeditor_src/core/cwx/skin.hpp"
#include "cwxeditor_src/core/cwx/summary.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dprops.hpp"
#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/dialogs.hpp"
#include "cwxeditor_src/lib/services.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::loader {

namespace lib = ::cwxeditor::lib;
namespace dprops = ::cwxeditor::cwx::editor::gui::dwt::dprops;
namespace filesync = ::cwxeditor::cwx::filesync;
namespace skin = ::cwxeditor::cwx::skin;
namespace summary = ::cwxeditor::cwx::summary;
namespace utils = ::cwxeditor::cwx::utils;

using String = lib::String;
using Path = std::filesystem::path;
using Props = dprops::Props;
using LoadOption = summary::LoadOption;
using Summary = summary::Summary;
using FileSync = filesync::FileSync;
using Window = std::shared_ptr<lib::Window>;
using Dispatcher = std::shared_ptr<lib::MainDispatcher>;
using StatusCallback = lib::Callback<void(String)>;
using FailureCallback = lib::Callback<void()>;
using ProgressCallback = lib::Callback<void(std::uint32_t)>;

/// シナリオと、その読み込み中にデコードできなかったファイルの一覧。
struct LoadResult {
    Summary::Owner summary;
    lib::Array<String> errorFiles;
};

enum class ScenarioLoadStatus : std::uint8_t {
    Success,
    Cancelled,
    NotFound,
    Unsupported,
    InvalidSummary,
    ExtractFailed,
    ClassicConvertFailed,
    PartialFailure,
    Failed,
};

struct ScenarioLoadIssue {
    String path;
    String message;
};

struct ScenarioLoadOutcome {
    ScenarioLoadStatus status{ScenarioLoadStatus::Failed};
    Summary::Owner summary;
    lib::Array<String> errorFiles;
    lib::Array<ScenarioLoadIssue> issues;
    String message;

    [[nodiscard]] bool succeeded() const noexcept {
        return status == ScenarioLoadStatus::Success ||
               status == ScenarioLoadStatus::PartialFailure;
    }
    [[nodiscard]] bool cancelled() const noexcept {
        return status == ScenarioLoadStatus::Cancelled;
    }
};
using LoadResults = lib::Array<LoadResult>;
using LoadResultsCallback = lib::Callback<void(const LoadResults&)>;
using ScenarioSelectionCallback =
    std::function<void(std::optional<String>, String)>;
using SummaryLoadedCallback =
    lib::Callback<void(const Summary::Owner&, const lib::Array<String>&)>;
/// 型付きのバックグラウンドシナリオ読み込みの完了通知。 指定のUI
/// ディスパッチャーが受け付け中なら、その上でちょうど一度実行する。
using ScenarioLoadOutcomeCallback = lib::Callback<void(ScenarioLoadOutcome)>;

/// 同期・バックグラウンド読み込みで共用する中止状態。
struct LoadControl {
    std::shared_ptr<std::atomic_bool> cancelled =
        std::make_shared<std::atomic_bool>(false);
    std::shared_ptr<lib::TaskHandle> task;
    mutable std::mutex task_mutex;
    std::atomic_bool completed{false};
    // 選択画面・バックエンドがハンドルを通知する途中でも、UI処理が回る場合がある。
    std::atomic_bool submission_finished{true};
    std::thread::id worker_thread{};

    void cancel() noexcept {
        if (cancelled) cancelled->store(true, std::memory_order_release);
        std::shared_ptr<lib::TaskHandle> current;
        {
            std::scoped_lock lock(task_mutex);
            current = task;
        }
        if (current) current->cancel();
    }
    void attachTask(std::shared_ptr<lib::TaskHandle> value) noexcept {
        if (!value) return;
        const bool stop = isCancelled();
        {
            std::scoped_lock lock(task_mutex);
            task = value;
        }
        if (stop && !value->finished()) value->cancel();
    }
    [[nodiscard]] std::shared_ptr<lib::TaskHandle> taskSnapshot() const noexcept {
        std::scoped_lock lock(task_mutex);
        return task;
    }
    void markCompleted() noexcept {
        completed.store(true, std::memory_order_release);
    }
    void releaseTask() noexcept {
        std::scoped_lock lock(task_mutex);
        task.reset();
    }
    /// 借用Props/FileSyncの寿命が終わる前に、読み込みワ
    /// ーカーを停止・joinする。 そのワーカー自身からは呼べない
    /// 。自身を待つ違反やバックエンドの待機失敗はfalseで返す。
    [[nodiscard]] bool cancelAndWait() noexcept {
        cancel();
        std::shared_ptr<lib::TaskHandle> current;
        {
            std::scoped_lock lock(task_mutex);
            if (worker_thread == std::this_thread::get_id()) return false;
            current = task;
        }
        if (!current) return true;
        try {
            current->wait();
            releaseTask();
            return true;
        } catch (...) {
            return false;
        }
    }
    [[nodiscard]] bool isCancelled() const noexcept {
        return cancelled && cancelled->load(std::memory_order_acquire);
    }
    void enterWorker() noexcept {
        std::scoped_lock lock(task_mutex);
        worker_thread = std::this_thread::get_id();
    }
    void leaveWorker() noexcept {
        std::scoped_lock lock(task_mutex);
        worker_thread = {};
    }
};

/// 複数・単一のシナリオ読み込みで使う共通ワーカー。
template <bool Array>
class LSFFThr final : public std::enable_shared_from_this<LSFFThr<Array>> {
public:
    LSFFThr(Props& prop, const LoadOption& opt, Window owner, FileSync& sync,
            lib::Services services, StatusCallback status,
            FailureCallback failure, std::shared_ptr<LoadControl> control);

    void setFiles(lib::Array<String> files);
    void setScenario(String fname, Summary::Owner old);
    void setReload(Summary::ReloadOperation reload) { reload_ = std::move(reload); }
    void setLoaded(LoadResultsCallback loaded);
    void setLoaded(SummaryLoadedCallback loaded);
    void setLoaded(ScenarioLoadOutcomeCallback loaded);
    void reportOutcomeErrors(bool value) { report_outcome_errors_ = value; }

    [[nodiscard]] std::shared_ptr<lib::TaskHandle> start();
    void run();
    void cancel() noexcept;
    // Dと同じくスレッド開始前にsetWaitCursors(w)、終了時にresetCursorsを行う。
    void setWaitCursor(bool wait) noexcept;
    // 読み込みの中止時も含め、最後の解放はUIスレッドで行う。
    void releaseWaitCursor() noexcept;

private:
    Props* prop_{};
    LoadOption opt_{};
    Window owner_;
    FileSync* sync_{};
    lib::Services services_;
    Dispatcher display_;
    StatusCallback status_;
    StatusCallback progress_status_;
    FailureCallback failure_;
    std::shared_ptr<LoadControl> control_;

    lib::Array<String> files_;
    lib::Array<String> tempPaths_;
    String fname_;
    Summary::Owner old_;
    Summary::ReloadOperation reload_;
    LoadResultsCallback loadedMany_;
    SummaryLoadedCallback loadedOne_;
    ScenarioLoadOutcomeCallback loadedOutcome_;
    bool report_outcome_errors_{true};

    std::atomic<std::uint32_t> worked_{0};
    std::atomic<std::uint32_t> max_{0};
    std::shared_ptr<std::atomic_bool> outcome_delivered_{std::make_shared<std::atomic_bool>(false)};

    void clear() noexcept;
    void setMax(std::uint32_t value);
    void setWork(std::uint32_t value);
    [[nodiscard]] bool isCancelled() const noexcept;
    void startStatus(String file);
    void finishStatus();
    void failedStatus();
    void workingStatus();
    void loadedStatus(const LoadResults& result);
    void loadedStatus(const Summary::Owner& result,
                      const lib::Array<String>& errorFiles);
    static void loadedStatus(ScenarioLoadOutcome outcome, String statusText,
                             String warningTitle, StatusCallback status,
                             ScenarioLoadOutcomeCallback completed,
                             std::shared_ptr<std::atomic_bool> delivered,
                             std::shared_ptr<lib::DialogBackend> dialogs,
                             std::shared_ptr<LoadControl> control);
    [[nodiscard]] ScenarioLoadOutcome loadOutcome();
    void summaryError(String message);
    void completeTask() noexcept;

    void invoke(std::function<void()> task);
    void post(std::function<void()> task);
};

[[nodiscard]] lib::Array<String> scenarioFilter();
[[nodiscard]] lib::Array<String> scenarioFilterDesc(const Props& prop);

[[nodiscard]] LoadResults loadScenarios(
    Props& prop, const LoadOption& opt, Window owner, FileSync& sync,
    StatusCallback status, String dlgTitle,
    LoadResultsCallback loaded = {}, FailureCallback failure = {},
    bool openInThread = true, lib::Services services = {},
    std::shared_ptr<LoadControl> control = {});
[[nodiscard]] bool loadScenariosAsync(
    Props& prop, const LoadOption& opt, Window owner, FileSync& sync,
    StatusCallback status, String dlgTitle,
    LoadResultsCallback loaded = {}, FailureCallback failure = {},
    bool openInThread = true, lib::Services services = {},
    std::shared_ptr<LoadControl> control = {});

[[nodiscard]] LoadResults loadScenariosFromFile(
    Props& prop, const LoadOption& opt, Window owner, FileSync& sync,
    StatusCallback status, const lib::Array<String>& files,
    LoadResultsCallback loaded = {}, FailureCallback failure = {},
    bool openInThread = true, lib::Services services = {},
    std::shared_ptr<LoadControl> control = {});

[[nodiscard]] String scenarioFilterPath(const Props& prop);
[[nodiscard]] std::optional<String> selectScenario(
    const Props& prop, Window owner, String dlgTitle,
    lib::Services services = {});
[[nodiscard]] bool selectScenarioAsync(
    const Props& prop, Window owner, String dlgTitle,
    ScenarioSelectionCallback completed, lib::Services services = {});

[[nodiscard]] Summary::Owner loadScenario(
    Props& prop, const LoadOption& opt, lib::Array<String>& errorFiles,
    Window owner, FileSync& sync, StatusCallback status,
    const Summary::Owner& old, String dlgTitle, lib::Array<String>& openPaths,
    SummaryLoadedCallback loaded = {}, FailureCallback failure = {},
    bool openInThread = true, lib::Services services = {},
    std::shared_ptr<LoadControl> control = {});

[[nodiscard]] Summary::Owner loadScenarioFromFile(
    Props& prop, const LoadOption& opt, lib::Array<String>& errorFiles,
    Window owner, FileSync& sync, StatusCallback status,
    const Summary::Owner& old, String fname,
    SummaryLoadedCallback loaded = {}, FailureCallback failure = {},
    bool openInThread = true, lib::Services services = {},
    std::shared_ptr<LoadControl> control = {});

[[nodiscard]] Summary::Owner loadScenarioFromFileImpl(
    Props& prop, const LoadOption& opt, lib::Array<String>& errorFiles,
    Window owner, FileSync& sync, StatusCallback status,
    const Summary::Owner& old, String fname,
    SummaryLoadedCallback loaded = {}, FailureCallback failure = {},
    bool openInThread = true, Dispatcher current = {},
    ProgressCallback setMax = {}, ProgressCallback worked = {},
    lib::Services services = {}, std::shared_ptr<LoadControl> control = {});

/// 製品のMainWindowが使う型付きの同期シナリ
/// オ読み込み。 中止・未対応入力・読み込み失敗を、
/// すべて同じnullのSummaryへまとめない。
[[nodiscard]] ScenarioLoadOutcome loadScenarioOutcome(
    Props& prop, const LoadOption& opt, Window owner, FileSync& sync,
    StatusCallback status, String dlgTitle, lib::Array<String>& openPaths,
    lib::Services services = {}, std::shared_ptr<LoadControl> control = {});

[[nodiscard]] ScenarioLoadOutcome loadScenarioOutcomeFromFile(
    Props& prop, const LoadOption& opt, Window owner, FileSync& sync,
    StatusCallback status, String fname, lib::Services services = {},
    std::shared_ptr<LoadControl> control = {});

/// Services::tasksでファイル読み込みを始め、型付きの終了
/// 結果をServices::dispatcherへ届ける。 返された制
/// 御オブジェクトが中止とタスクの寿命を管理する。状態・完了通知は UI
/// 所有者の生ポインターではなく、呼び出し元の寿命保護を使うこと。 借用
/// Props/FileSyncを破棄する前に、セッション所有者はcancelAndWait()
/// を呼ぶ必要がある。 完了通知だけではバックエンドのTaskHandle
/// の終了までは保証しない。 この関数はホストのディスパッ
/// チャー・タスク実装を所有せず、置き換え・終了もしない。
[[nodiscard]] std::shared_ptr<LoadControl> loadScenarioOutcomeFromFileAsync(
    Props& prop, const LoadOption& opt, Window owner, FileSync& sync,
    StatusCallback status, String fname, ScenarioLoadOutcomeCallback completed,
    lib::Services services = {}, std::shared_ptr<LoadControl> control = {},
    Summary::ReloadOperation reload = {}, bool reportErrors = true);

/// シナリオ読み込みエラーダイアログの共通実装。
class ScenarioErrorFilesDialog final {
public:
    ScenarioErrorFilesDialog(const Props& prop, Window owner,
                             const Summary& summary,
                             const lib::Array<String>& errorFiles,
                             const lib::Array<ScenarioLoadIssue>& issues = {},
                             lib::Services services = {},
                             lib::Action refreshToolbar = {});

    void setup(const std::shared_ptr<lib::Container>& area);
    [[nodiscard]] bool open();
    void copy();
    void selectAll();

    [[nodiscard]] const std::shared_ptr<lib::Dialog>& dialog() const noexcept {
        return dialog_;
    }
    [[nodiscard]] const std::shared_ptr<lib::TableView>& files() const noexcept {
        return files_;
    }

private:
    const Props* prop_{};
    Window owner_;
    const Summary* summary_{};
    lib::Array<String> errorFiles_;
    lib::Array<ScenarioLoadIssue> issues_;
    lib::Services services_;
    lib::Action refreshToolbar_;
    std::shared_ptr<lib::Dialog> dialog_;
    std::shared_ptr<lib::TableView> files_;
    std::shared_ptr<lib::Menu> menu_;
    std::shared_ptr<lib::Button> ok_button_;
    std::shared_ptr<lib::Button> cancel_button_;
    bool setup_complete_{};

    void createDialog();
    void close(lib::DialogResult result);
};

}  // namespace cwxeditor::cwx::editor::gui::dwt::loader
