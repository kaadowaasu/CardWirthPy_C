#include "cwxeditor_src/lib/service_defaults.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/loader.hpp"
#include "cwxeditor_src/core/cwx/structs.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dskin.hpp"
#include "cwxeditor_src/lib/editor_diagnostics.hpp"
#include "cwxeditor_src/lib/runtime_context.hpp"
#include "cwxeditor_src/lib/ui_window_lifecycle.hpp"
#include "cwxeditor_src/lib/window_host.hpp"
#include "platform/storage/storage.hpp"
// UIの実装は一つに統一し、
// 別経路の実装を設けない。

#include <algorithm>
#include <cctype>
#include <chrono>
#include <exception>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace cwxeditor::cwx::editor::gui::dwt::loader {
namespace {

// 要素数にかかわらず、ローダーごとに保留する進捗通知は一つとする。 UIの
// クロージャーは、ファイル処理のワーカーからもキャンセル後にも呼ばない。
struct LoadProgress final : std::enable_shared_from_this<LoadProgress> {
    Dispatcher dispatcher;
    std::shared_ptr<LoadControl> control;
    lib::Callback<void(String, String)> callback;
    std::mutex mutex;
    String name, member;
    bool queued{};
    void update(String next_name, String next_member) {
        if (control->isCancelled()) return;
        {
            std::scoped_lock lock(mutex);
            name = std::move(next_name);
            member = std::move(next_member);
            if (queued) return;
            queued = true;
        }
        auto deliver = [self = shared_from_this()] {
            String name, member;
            {
                std::scoped_lock lock(self->mutex);
                name = std::move(self->name);
                member = std::move(self->member);
                self->queued = false;
            }
            if (!self->control->isCancelled() && self->callback)
                self->callback(std::move(name), std::move(member));
        };
        if (dispatcher) dispatcher->post(std::move(deliver));
        else deliver();
    }
};

lib::Services resolveServices(lib::Services supplied) {
    return lib::with_service_defaults({}, std::move(supplied));
}

String pathText(const Path& path) {
    return structs::pathToUtf8(path);
}

Path pathFromText(const lib::StringView value) {
    return structs::pathFromUtf8(value);
}

String lowerExtension(const Path& path) {
    String value = pathText(path.extension());
    std::transform(value.begin(), value.end(), value.begin(),
                   [](const unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    return value;
}

bool supportedScenarioInput(const Path& path) {
    std::error_code error;
    if (std::filesystem::is_directory(path, error) && !error) return true;
    String name = pathText(path.filename());
    std::transform(name.begin(), name.end(), name.begin(),
                   [](const unsigned char value) {
                       return static_cast<char>(std::tolower(value));
                   });
    if (name == "summary.xml" || name == "summary.wsm") return true;
    const auto extension = lowerExtension(path);
    return extension == ".wsn" || extension == ".zip" ||
           extension == ".cab" || extension == ".lzh" ||
           extension == ".lha";
}

ScenarioLoadStatus classifyLoadFailure(const Path& source) noexcept {
    const auto extension = lowerExtension(source);
    if (extension == ".wsn" || extension == ".zip" || extension == ".cab" ||
        extension == ".lzh" || extension == ".lha") {
        return ScenarioLoadStatus::ExtractFailed;
    }
    if (extension == ".wsm") return ScenarioLoadStatus::ClassicConvertFailed;
    if (extension == ".xml") return ScenarioLoadStatus::InvalidSummary;
    std::error_code error;
    if (std::filesystem::is_directory(source, error) && !error) {
        if (std::filesystem::exists(source / "Summary.wsm", error) && !error)
            return ScenarioLoadStatus::ClassicConvertFailed;
        return ScenarioLoadStatus::InvalidSummary;
    }
    return ScenarioLoadStatus::Failed;
}

String fileName(const String& value) {
    return pathText(pathFromText(value).filename());
}

String directoryName(const String& value) {
    return pathText(pathFromText(value).parent_path());
}

String scenarioNameFor(const String& value) {
    std::error_code error;
    const Path path = pathFromText(value);
    const bool directory = std::filesystem::is_directory(path, error) && !error;
    return pathText(directory ? path.filename() : path.parent_path().filename());
}

lib::Array<String> splitPatterns(String value) {
    lib::Array<String> result;
    std::size_t begin = 0;
    while (begin <= value.size()) {
        const auto end = value.find(';', begin);
        result.emplace_back(value.substr(begin, end == String::npos ? end : end - begin));
        if (end == String::npos) break;
        begin = end + 1U;
    }
    return result;
}

lib::Array<lib::FileFilter> makeFilters(const Props& prop) {
    const auto extensions = scenarioFilter();
    const auto descriptions = scenarioFilterDesc(prop);
    lib::Array<lib::FileFilter> result;
    result.reserve(extensions.size());
    for (std::size_t i = 0; i < extensions.size(); ++i) {
        lib::FileFilter filter;
        filter.label = i < descriptions.size() ? descriptions[i] : extensions[i];
        filter.patterns = splitPatterns(extensions[i]);
        result.push_back(std::move(filter));
    }
    return result;
}

void setWaitCursor(const Window& owner, bool wait) noexcept {
    // D版の setWaitCursors(w) / resetCursors
    // に対応する。 共通の待機要求は CURSOR_WAIT を直ちに表示し、所
    // 有者ごとに一度だけ解除する。 UIスレッドがワーカー開始前に設定し、終了
    // コールバックで解除するため、 ローダーのワーカーはウィンドウに触れない。
    if (!owner || !lib::cursor_ui_thread()) return;
    const bool waiting = owner->cursor() == lib::CursorKind::Wait;
    if (wait == waiting) return;
    owner->set_cursor(wait ? lib::CursorKind::Wait : lib::CursorKind::Default);
    try {
        if (wait) lib::begin_wait_cursor();
        else lib::end_wait_cursor();
    } catch (...) {
    }
}

class CursorGuard final {
public:
    explicit CursorGuard(Window owner) : owner_(std::move(owner)) {
        setWaitCursor(owner_, true);
    }
    ~CursorGuard() { setWaitCursor(owner_, false); }

    CursorGuard(const CursorGuard&) = delete;
    CursorGuard& operator=(const CursorGuard&) = delete;

private:
    Window owner_;
};

void reportCallbackError(const lib::Services& services, const Props& prop,
                         String message) {
    try {
        if (services.dialogs) {
            const String& title = prop.requiredMsgs().dlgTitWarning;
            (void)services.dialogs->message({title, std::move(message), {},
                                             lib::MessageSeverity::Warning,
                                             lib::MessageButtons::OK});
        }
    } catch (...) {
    }
}

}  // namespace

template <bool Array>
LSFFThr<Array>::LSFFThr(Props& prop, const LoadOption& opt, Window owner,
                        FileSync& sync, lib::Services services,
                        StatusCallback status, FailureCallback failure,
                        std::shared_ptr<LoadControl> control)
    : prop_(&prop),
      opt_(opt),
      owner_(std::move(owner)),
      sync_(&sync),
      services_(resolveServices(std::move(services))),
      display_(services_.dispatcher),
      status_(std::move(status)),
      failure_(std::move(failure)),
      control_(control ? std::move(control) : std::make_shared<LoadControl>()) {
    auto status_progress = std::make_shared<LoadProgress>();
    status_progress->dispatcher = display_;
    status_progress->control = control_;
    status_progress->callback = [status = status_](String message, String) {
        if (status) status(std::move(message));
    };
    progress_status_ = [status_progress](String message) { status_progress->update(std::move(message), {}); };
    const auto previous_cancel = opt_.cancelled;
    opt_.cancelled = [control = control_, previous_cancel] {
        return control->isCancelled() || (previous_cancel && previous_cancel());
    };
    if (opt_.processFunc) {
        auto progress = std::make_shared<LoadProgress>();
        progress->dispatcher = display_;
        progress->control = control_;
        progress->callback = std::move(opt_.processFunc);
        opt_.processFunc = [progress](String name, String member) {
            progress->update(std::move(name), std::move(member));
        };
    }
    if (opt_.progressDetail) {
        auto progress = std::make_shared<LoadProgress>();
        progress->dispatcher = display_;
        progress->control = control_;
        progress->callback = std::move(opt_.progressDetail);
        opt_.progressDetail = [progress](String stage, String path) {
            progress->update(std::move(stage), std::move(path));
        };
    }
}

template <bool Array>
void LSFFThr<Array>::setFiles(lib::Array<String> files) {
    files_ = std::move(files);
}

template <bool Array>
void LSFFThr<Array>::setScenario(String fname, Summary::Owner old) {
    fname_ = std::move(fname);
    old_ = std::move(old);
}

template <bool Array>
void LSFFThr<Array>::setLoaded(LoadResultsCallback loaded) {
    loadedMany_ = std::move(loaded);
}

template <bool Array>
void LSFFThr<Array>::setLoaded(SummaryLoadedCallback loaded) {
    loadedOne_ = std::move(loaded);
}

template <bool Array>
void LSFFThr<Array>::setLoaded(ScenarioLoadOutcomeCallback loaded) {
    loadedOutcome_ = std::move(loaded);
}

template <bool Array>
std::shared_ptr<lib::TaskHandle> LSFFThr<Array>::start() {
    auto self = this->shared_from_this();
    if (!services_.tasks) {
        self->run();
        return {};
    }
    auto task = services_.tasks->run([self]() { self->run(); });
    if (control_) {
        control_->attachTask(task);
        // 高速な、または画面なしのバックエンドでは、run() がハンドル
        // を返す前に完了する場合がある。 ハンドルを公開してから完了処理へ
        // 入り直し、 完了したものだけをUI側で解放する規則を維持する。
        if (control_->completed.load(std::memory_order_acquire)) completeTask();
    }
    return task;
}

template <bool Array>
void LSFFThr<Array>::cancel() noexcept {
    if (control_) control_->cancel();
}

template <bool Array>
bool LSFFThr<Array>::isCancelled() const noexcept {
    return control_ && control_->isCancelled();
}

template <bool Array>
void LSFFThr<Array>::invoke(std::function<void()> task) {
    if (!task) return;
    auto self = this->shared_from_this();
    auto wrapped = [self = std::move(self), task = std::move(task)]() mutable {
        if (!self->isCancelled()) task();
    };
    if (display_) {
        // 旧方式の通知はすべて一方向とする。ここで待つと、 UIの終了
        // 処理が、そのUI自身を待っているワーカーに依存してしまう。
        display_->post(std::move(wrapped));
    } else {
        wrapped();
    }
}

template <bool Array>
void LSFFThr<Array>::post(std::function<void()> task) {
    if (!task) return;
    auto self = this->shared_from_this();
    auto wrapped = [self = std::move(self), task = std::move(task)]() mutable {
        if (!self->isCancelled()) task();
    };
    if (display_) {
        display_->post(std::move(wrapped));
    } else {
        wrapped();
    }
}

template <bool Array>
void LSFFThr<Array>::releaseWaitCursor() noexcept {
    try {
        auto owner = owner_;
        auto release = [owner]() {
            ::cwxeditor::cwx::editor::gui::dwt::loader::setWaitCursor(owner, false);
        };
        if (display_) display_->post(std::move(release));
        else release();
    } catch (...) {
    }
}

template <bool Array>
void LSFFThr<Array>::setWaitCursor(bool wait) noexcept {
    ::cwxeditor::cwx::editor::gui::dwt::loader::setWaitCursor(owner_, wait);
}

template <bool Array>
void LSFFThr<Array>::clear() noexcept {
    for (const auto& path : tempPaths_) {
        try {
            if (!path.empty()) utils::delAll(pathFromText(path));
        } catch (...) {
        }
    }
    tempPaths_.clear();
}

template <bool Array>
void LSFFThr<Array>::completeTask() noexcept {
    if (!control_) return;
    control_->markCompleted();
    // TaskHandle のデストラクターはスレッドの終了を待つ場合があるた
    // め、自身のワーカーから解放しない。 後のUI処理で、バックエンドが完了扱
    // いにしたハンドルだけを解放する。 切り離した調整用スレッドは追加しない。
    const auto control = control_;
    const auto dispatcher = display_;
    if (!dispatcher) return;
    try {
        (void)dispatcher->post_delayed(std::chrono::milliseconds(1), [control]() mutable {
            const auto handle = control->taskSnapshot();
            if (!handle || handle->finished()) {
                control->releaseTask();
            }
        });
    } catch (...) {
        // UIスレッドで終了待ちを起こす危険を冒すより、完了したハンドルを保持する方を選ぶ。
    }
}

template <bool Array>
void LSFFThr<Array>::setMax(std::uint32_t value) {
    max_.store(value, std::memory_order_release);
    // D版と同様、完了量とは独立して最大値を変更する。 共
    // 通の非同期・集約通知と、最大値ゼロの保護を維持する。
    workingStatus();
}

template <bool Array>
void LSFFThr<Array>::setWork(std::uint32_t value) {
    worked_.store(value, std::memory_order_release);
    workingStatus();
}

template <bool Array>
void LSFFThr<Array>::startStatus(String file) {
    if (!status_ || !prop_) return;
    const String& message = prop_->requiredMsgs().loading;
    status_(utils::tryFormat(message, file));
}

template <bool Array>
void LSFFThr<Array>::finishStatus() {
    setWaitCursor(false);
}

template <bool Array>
void LSFFThr<Array>::failedStatus() {
    if (status_ && prop_) {
        if constexpr (Array) {
            if (files_.size() == 1U) {
                const String& message = prop_->requiredMsgs().loadErrorStatus;
                status_(utils::tryFormat(message, files_.front()));
            } else {
                const String& message = prop_->requiredMsgs().loadErrorStatusCount;
                status_(utils::tryFormat(message, files_.size()));
            }
        } else {
            const String& message = prop_->requiredMsgs().loadErrorStatus;
            status_(utils::tryFormat(message, fname_));
        }
    }
    if (failure_) failure_();
}

template <bool Array>
void LSFFThr<Array>::workingStatus() {
    if (!status_ || !prop_) return;
    const auto maximum = max_.load(std::memory_order_acquire);
    const auto completed = worked_.load(std::memory_order_acquire);
    // 項目数ゼロでの worked/max によるD版の変
    // 換失敗は、意図的に回避する。 空の読み込みでも有効な
    // 進捗0%を通知する。 D版は非負の進捗を最も近い整数
    // に丸めるが、ALL では従来の切り捨てを維持する。
    const auto percent = maximum == 0U
        ? 0U
        : static_cast<std::uint32_t>(
              (static_cast<std::uint64_t>(completed) * 100U +
               (lib::editor_window_host_options().all_mode ? 0U : maximum / 2U)) / maximum);
    const String& message = prop_->requiredMsgs().loadProgress;
    // 通知処理へ渡す前に、ワーカーの現在のファイル名を複製する。 配列ローダー
    // が次のファイルへ進んでいる間、UIから fname_ を直接読まない。
    progress_status_(utils::tryFormat(message, fileName(fname_), percent));
}

template <bool Array>
void LSFFThr<Array>::loadedStatus(const LoadResults& result) {
    try {
        if (!prop_ || result.empty() || isCancelled()) return;
        if (result.size() == 1U) {
            const String& message = prop_->requiredMsgs().loaded;
            status_(utils::tryFormat(message, result.front().summary->scenarioName()));
        } else {
            const String& message = prop_->requiredMsgs().loadedCount;
            status_(utils::tryFormat(message, result.size()));
        }
        if (loadedMany_) loadedMany_(result);
    } catch (const std::exception& error) {
        reportCallbackError(services_, *prop_, error.what());
        if (status_ && prop_) {
            const String& message = prop_->requiredMsgs().loadErrorStatusCount;
            status_(utils::tryFormat(message, result.size()));
        }
    } catch (...) {
        clear();
    }
}

template <bool Array>
void LSFFThr<Array>::loadedStatus(const Summary::Owner& result,
                                  const lib::Array<String>& errorFiles) {
    try {
        if (!prop_ || !result || isCancelled()) return;
        const String& message = prop_->requiredMsgs().loaded;
        status_(utils::tryFormat(message, result->scenarioName()));
        if (loadedOne_) loadedOne_(result, errorFiles);
    } catch (const std::exception& error) {
        reportCallbackError(services_, *prop_, error.what());
        if (status_ && prop_) {
            const String& message = prop_->requiredMsgs().loadErrorStatus;
            status_(utils::tryFormat(message, result ? result->scenarioName() : fname_));
        }
    } catch (...) {
        clear();
    }
}

template <bool Array>
void LSFFThr<Array>::loadedStatus(
    ScenarioLoadOutcome outcome, String statusText, String warningTitle,
    StatusCallback status, ScenarioLoadOutcomeCallback completed,
    std::shared_ptr<std::atomic_bool> delivered,
    std::shared_ptr<lib::DialogBackend> dialogs,
    std::shared_ptr<LoadControl> control) {
    if (!delivered || delivered->exchange(true, std::memory_order_acq_rel)) return;
    if (control && control->isCancelled()) {
        outcome.summary.reset();
        outcome.status = ScenarioLoadStatus::Cancelled;
        statusText.clear();
    }
    try {
        if (status && !statusText.empty()) status(std::move(statusText));
        if (!outcome.cancelled() && !outcome.succeeded() &&
            !outcome.message.empty() && dialogs) {
            (void)dialogs->message({std::move(warningTitle), outcome.message, {},
                                    lib::MessageSeverity::Warning,
                                    lib::MessageButtons::OK});
        }
        if (completed) completed(std::move(outcome));
    } catch (...) {
        // 利用側の完了コールバックの例外を、通知処理の外へ伝播させない。
    }
}

template <bool Array>
ScenarioLoadOutcome LSFFThr<Array>::loadOutcome() {
    ScenarioLoadOutcome outcome;
    if (!prop_ || !sync_) {
        outcome.status = ScenarioLoadStatus::Failed;
        outcome.message = utils::tryFormat(prop_ ? prop_->requiredMsgs().loadError.value
                                                : "%1$sの読み込みに失敗しました。", fname_);
        return outcome;
    }
    if (fname_.empty() || isCancelled()) {
        outcome.status = ScenarioLoadStatus::Cancelled;
        return outcome;
    }

    // この事前確認は、下の型付きキャンセル例外を捕捉する範囲の外で行う。
    if (opt_.progressDetail) opt_.progressDetail("入力ファイルの存在確認", fname_);
    const Path source = pathFromText(fname_);
    std::error_code pathError;
    if (!std::filesystem::exists(source, pathError) || pathError) {
        outcome.status = ScenarioLoadStatus::NotFound;
        outcome.message = utils::tryFormat(prop_->requiredMsgs().loadError.value, fname_);
        outcome.issues.push_back({fname_, outcome.message});
        return outcome;
    }
    if (!supportedScenarioInput(source)) {
        outcome.status = ScenarioLoadStatus::Unsupported;
        outcome.message = utils::tryFormat(prop_->requiredMsgs().notScenario.value, fname_);
        outcome.issues.push_back({fname_, outcome.message});
        return outcome;
    }

    try {
        opt_.reportProgress("保存キューの完了待ち", fname_);
        if (!sync_->syncUntilCancelled([this] { return isCancelled(); })) {
            outcome.status = ScenarioLoadStatus::Cancelled;
            return outcome;
        }
        if (isCancelled()) {
            outcome.status = ScenarioLoadStatus::Cancelled;
            return outcome;
        }
        const auto& var = prop_->var();
        opt_.reportProgress("既定スキンの検索", prop_->enginePath());
        const auto defaultSkin = dskin::findSkin2(
            *prop_, var.defaultSkin.value, var.defaultSkinName.value);
        auto load_option = opt_;
        const auto prior_issue = load_option.issueFunc;
        std::mutex issue_mutex;
        load_option.issueFunc = [&outcome, &issue_mutex, prior_issue](String path, String message) {
            std::scoped_lock lock(issue_mutex);
            if (prior_issue) prior_issue(path, message);
            lib::editor_action_log("Scenario member load failed path=\"" + path +
                                   "\" reason=\"" + message + "\"");
            outcome.issues.push_back({std::move(path), std::move(message)});
        };
        outcome.summary = reload_ ? reload_(prop_->parent(), load_option, outcome.errorFiles)
            : Summary::loadScenarioFromFile(
            prop_->parent(), load_option, outcome.errorFiles, fname_, prop_->tempPath(),
            defaultSkin.get(), {}, old_,
            [this](std::uint32_t value) { setMax(value); },
            [this](std::uint32_t value) { setWork(value); },
            scenarioNameFor(fname_), prop_->storage());
        // 再読み込みは既存の作業ツリーを借用する。 このロ
        // ーダーの展開キャンセル時の後処理で削除しない。
        if (!reload_ && outcome.summary && outcome.summary->useTemp())
            tempPaths_.push_back(outcome.summary->scenarioPath());
        if (isCancelled()) {
            outcome.summary.reset();
            outcome.status = ScenarioLoadStatus::Cancelled;
            clear();
            return outcome;
        }
        if (!outcome.summary) {
            outcome.status = ScenarioLoadStatus::InvalidSummary;
            outcome.message = utils::tryFormat(prop_->requiredMsgs().loadError.value, fname_);
            outcome.issues.push_back({fname_, outcome.message});
            return outcome;
        }
        for (const auto& file : outcome.errorFiles) {
            const auto found = std::find_if(outcome.issues.begin(), outcome.issues.end(),
                [&file](const ScenarioLoadIssue& issue) { return issue.path == file; });
            if (found == outcome.issues.end())
                outcome.issues.push_back({file, "読み込みまたは変換に失敗しました"});
        }
        outcome.status = outcome.errorFiles.empty()
            ? ScenarioLoadStatus::Success : ScenarioLoadStatus::PartialFailure;
        return outcome;
    } catch (const summary::ScenarioLoadCancelled&) {
        outcome.status = ScenarioLoadStatus::Cancelled;
        clear();
        return outcome;
    } catch (const summary::SummaryException& error) {
        outcome.message = error.what();
    } catch (const std::exception& error) {
        outcome.message = error.what();
    } catch (...) {
        outcome.message = utils::tryFormat(prop_->requiredMsgs().loadError.value, fname_);
    }
    outcome.status = classifyLoadFailure(source);
    outcome.issues.push_back({fname_, outcome.message});
    return outcome;
}

template <bool Array>
void LSFFThr<Array>::summaryError(String message) {
    if (!prop_) return;
    try {
        if (services_.dialogs) {
            const String& title = prop_->requiredMsgs().dlgTitWarning;
            (void)services_.dialogs->message({title, message, {},
                                              lib::MessageSeverity::Warning,
                                              lib::MessageButtons::OK});
        }
    } catch (...) {
        clear();
    }
    if (status_) {
        const String& statusMessage = prop_->requiredMsgs().loadErrorStatus;
        status_(utils::tryFormat(statusMessage, fname_));
    }
    if (failure_) failure_();
}

template <bool Array>
void LSFFThr<Array>::run() {
    struct WorkerScope final {
        std::shared_ptr<LoadControl> control;
        explicit WorkerScope(std::shared_ptr<LoadControl> value)
            : control(std::move(value)) { if (control) control->enterWorker(); }
        ~WorkerScope() { if (control) control->leaveWorker(); }
    } worker_scope(control_);
    bool failed = false;
    try {
        if (!sync_) throw std::logic_error("scenario loader has no file synchronizer");
        if (loadedOutcome_) {
            ScenarioLoadOutcome outcome;
            // 型付きの各要求は、必ず終了結果を通知する。 loadOutcome
            // の解析例外を捕捉する範囲外で起きる、事前確認や進捗通知の失敗も含む。
            try {
                outcome = loadOutcome();
            } catch (const summary::ScenarioLoadCancelled&) {
                outcome.status = ScenarioLoadStatus::Cancelled;
            } catch (const std::exception& error) {
                outcome.status = ScenarioLoadStatus::Failed;
                outcome.message = error.what();
            } catch (...) {
                outcome.status = ScenarioLoadStatus::Failed;
                outcome.message = "シナリオの読み込み処理で不明なエラーが発生しました。";
            }
            String statusText;
            String warningTitle;
            if (prop_) {
                warningTitle = prop_->requiredMsgs().dlgTitWarning;
                if (outcome.succeeded() && outcome.summary) {
                    statusText = utils::tryFormat(prop_->requiredMsgs().loaded.value,
                                                  outcome.summary->scenarioName());
                } else if (!outcome.cancelled()) {
                    statusText = utils::tryFormat(prop_->requiredMsgs().loadErrorStatus.value, fname_);
                }
            }
            auto deliver = [outcome = std::move(outcome), statusText = std::move(statusText),
                            warningTitle = std::move(warningTitle),
                            status = std::move(status_), completed = std::move(loadedOutcome_),
                            delivered = outcome_delivered_, dialogs = report_outcome_errors_
                                ? services_.dialogs : std::shared_ptr<lib::DialogBackend>{},
                            control = control_]() mutable {
                LSFFThr<Array>::loadedStatus(
                    std::move(outcome), std::move(statusText), std::move(warningTitle),
                    std::move(status), std::move(completed), std::move(delivered),
                    std::move(dialogs), std::move(control));
            };
            if (display_) {
                try {
                    display_->post(std::move(deliver));
                } catch (...) {
                    // ホストはUIの処理を受け付けなくなっている。 その所有者は Props/FileSync
                    // を破棄する前に、キャンセルとワーカーの終了待ちを済ませる必要がある。
                }
            } else {
                deliver();
            }
            // どの終了経路でも、start() より前に設定した
            // 待機カーソルを解除する。 解除せずに戻ると共有の待
            // 機要求が残り、ポインターが待機表示のままになる。
            releaseWaitCursor();
            completeTask();
            return;
        }
        const bool synchronized = sync_->syncUntilCancelled([this] { return isCancelled(); });
        if (!synchronized || isCancelled()) {
            clear();
            releaseWaitCursor();
            completeTask();
            return;
        }

        if constexpr (Array) {
            LoadResults result;
            for (const auto& path : files_) {
                if (isCancelled()) break;
                fname_ = path;
                invoke([self = this->shared_from_this(), file = fname_]() { self->startStatus(file); });
                lib::Array<String> errorFiles;
                const auto scenario = loadScenarioFromFileImpl(
                    *prop_, opt_, errorFiles, owner_, *sync_, status_, {}, path,
                    {}, {}, false, display_,
                    [self = this->shared_from_this()](std::uint32_t value) {
                        self->setMax(value);
                    },
                    [self = this->shared_from_this()](std::uint32_t value) {
                        self->setWork(value);
                    },
                    services_, control_);
                if (!scenario) break;
                result.push_back({scenario, errorFiles});
                if (scenario->useTemp()) tempPaths_.push_back(scenario->scenarioPath());
            }
            if (!isCancelled()) {
                invoke([self = this->shared_from_this(), result = std::move(result)]() mutable {
                    self->loadedStatus(result);
                });
            } else {
                clear();
            }
        } else {
            invoke([self = this->shared_from_this(), file = fname_]() { self->startStatus(file); });
            try {
                const auto& var = prop_->var();
                const auto defaultSkin = dskin::findSkin2(
                    *prop_, var.defaultSkin.value, var.defaultSkinName.value);
                lib::Array<String> errorFiles;
                const auto scenario = Summary::loadScenarioFromFile(
                    prop_->parent(), opt_, errorFiles, fname_, prop_->tempPath(),
                    defaultSkin.get(), {}, old_,
                    [self = this->shared_from_this()](std::uint32_t value) {
                        self->setMax(value);
                    },
                    [self = this->shared_from_this()](std::uint32_t value) {
                        self->setWork(value);
                    },
                    scenarioNameFor(fname_));
                if (scenario && scenario->useTemp()) {
                    tempPaths_.push_back(scenario->scenarioPath());
                }
                if (!isCancelled()) {
                    invoke([self = this->shared_from_this(), scenario, errorFiles]() {
                        self->loadedStatus(scenario, errorFiles);
                    });
                } else {
                    clear();
                }
            } catch (const summary::SummaryException& error) {
                invoke([self = this->shared_from_this(), message = String(error.what())]() {
                    self->summaryError(message);
                });
            } catch (const std::exception& error) {
                invoke([self = this->shared_from_this(), message = String(error.what())]() {
                    self->summaryError(message);
                });
            }
        }
    } catch (...) {
        failed = true;
        clear();
    }

    if (failed) {
        try {
            invoke([self = this->shared_from_this()]() { self->failedStatus(); });
        } catch (...) {
        }
    }
    try {
        invoke([self = this->shared_from_this()]() { self->finishStatus(); });
    } catch (...) {
    }
    // invoke() はキャンセル後の処理を破棄するが、待機カーソルは必ず解除する。
    releaseWaitCursor();
    completeTask();
}

lib::Array<String> scenarioFilter() {
    // 拡張子だけを扱うフロントエンド（SDL、Web、保存済みファイルのダイアロ
    // グ）は、 Summary.xml のような特定のファイル名を表現できない。
    // フィルターに混ぜると全体の制限が外れ、無関係なファイルまで表示される。
    const String archives = cab::canUncab()
        ? "*.wsn;*.xml;*.cab;*.zip;*.lzh;*.lha;*.wsm"
        : "*.wsn;*.xml;*.zip;*.lzh;*.lha;*.wsm";
    return {archives, "*.xml;*.wid;*.wex"};
}

lib::Array<String> scenarioFilterDesc(const Props& prop) {
    const auto patterns = scenarioFilter();
    const auto& messages = prop.requiredMsgs();
    const String& scenario = messages.filterScenario;
    const String& parts = messages.filterParts;
    return {utils::tryFormat(scenario, patterns[0]),
            utils::tryFormat(parts, patterns[1])};
}

LoadResults loadScenarios(
    Props& prop, const LoadOption& opt, Window owner, FileSync& sync,
    StatusCallback status, String dlgTitle, LoadResultsCallback loaded,
    FailureCallback failure, bool openInThread, lib::Services services,
    std::shared_ptr<LoadControl> control) {
    services = resolveServices(std::move(services));
    if (!services.file_picker) return {};

    lib::FilePickerRequest request;
    request.title = std::move(dlgTitle);
    const auto filterPath = scenarioFilterPath(prop);
    request.initial_directory = filterPath.empty() ? Path{} : pathFromText(filterPath);
    request.allow_multiple = true;
    request.filters = makeFilters(prop);
    const auto picked = services.file_picker->open_files(request);
    if (!picked.succeeded() || picked.selections.empty()) return {};
    if (control && control->isCancelled()) return {};

    lib::Array<String> files;
    files.reserve(picked.selections.size());
    std::unordered_set<String> uniqueFiles;
    String selectedDirectory = filterPath;
    for (const auto& selected : picked.selections) {
        const auto selectedPath=selected.native_path();
        if (!selectedPath) continue;
        Path path = *selectedPath;
        // D版と同様、部品ファイルをシナリオのディレクトリへ正規化
        // する前に、ファイルダイアログの場所を保存する。 選択画面
        // の場所とローダーに渡すパスは別に保持する（単独版のみ）。
        const auto pickerDirectory = utils::nabs(path).parent_path();
        if (lowerExtension(path) == ".wid" || lowerExtension(path) == ".wex") {
            path = path.parent_path();
        }
        path = utils::nabs(path);
        const auto normalized = pathText(path);
        if (uniqueFiles.insert(normalized).second) files.push_back(normalized);
        const auto rememberedDirectory = lib::editor_window_host_options().all_mode
            ? path.parent_path() : pickerDirectory;
        if (!rememberedDirectory.empty()) selectedDirectory = pathText(rememberedDirectory);
    }
    if (files.empty()) return {};

    auto put = [&prop, filterPath = std::move(selectedDirectory), loaded = std::move(loaded)](
                   const LoadResults& results) mutable {
        if (!results.empty() && results.front().summary) {
            const auto& scenario = results.front().summary;
            if (!scenario->readOnlyPath().empty()) {
                prop.var().scenarioPath = pathText(
                    utils::nabs(pathFromText(scenario->readOnlyPath()).parent_path()));
            } else if (scenario->useTemp()) {
                prop.var().scenarioPath = pathText(utils::nabs(pathFromText(filterPath)));
            } else {
                prop.var().scenarioPath = directoryName(pathText(utils::nabs(pathFromText(filterPath))));
            }
        }
        if (loaded) loaded(results);
    };

    const auto results = loadScenariosFromFile(
        prop, opt, std::move(owner), sync, std::move(status), files,
        put, std::move(failure), openInThread, std::move(services), control);
    if (!openInThread && !results.empty()) put(results);
    return results;
}

bool loadScenariosAsync(
    Props& prop, const LoadOption& opt, Window owner, FileSync& sync,
    StatusCallback status, String dlgTitle, LoadResultsCallback loaded,
    FailureCallback failure, bool openInThread, lib::Services services,
    std::shared_ptr<LoadControl> control) {
    services = resolveServices(std::move(services));
    if (!services.file_picker) return false;
    if (!control) control = std::make_shared<LoadControl>();
    control->submission_finished.store(false, std::memory_order_release);

    lib::FilePickerRequest request;
    request.title = std::move(dlgTitle);
    const auto filterPath = scenarioFilterPath(prop);
    request.initial_directory = filterPath.empty() ? Path{} : pathFromText(filterPath);
    request.allow_multiple = true;
    request.filters = makeFilters(prop);
    const auto file_picker = services.file_picker;
    const auto status_on_error = status;
    const auto failure_on_error = failure;
    auto selected = [&prop, &sync, owner = std::move(owner), opt,
                     status = std::move(status), loaded = std::move(loaded),
                     failure = std::move(failure), status_on_error,
                     failure_on_error, openInThread,
                     services = std::move(services), control, filterPath](
                        lib::FilePickerResult picked) mutable {
        struct Submission final {
            std::shared_ptr<LoadControl> control;
            ~Submission() {
                if (control)
                    control->submission_finished.store(true,
                                                       std::memory_order_release);
            }
        } submission{control};
        if (control && control->isCancelled()) return;
        if (!picked.succeeded() || picked.selections.empty()) {
            if (picked.failed() && status && !picked.message.empty())
                status("シナリオを選択できませんでした: " + picked.message);
            return;
        }

        lib::Array<String> files;
        files.reserve(picked.selections.size());
        std::unordered_set<String> uniqueFiles;
        String selectedDirectory = filterPath;
        for (const auto& selection : picked.selections) {
            const auto selectedPath = selection.native_path();
            if (!selectedPath) continue;
            Path path = *selectedPath;
            const auto extension = lowerExtension(path);
            if (extension == ".wid" || extension == ".wex")
                path = path.parent_path();
            path = utils::nabs(path);
            const auto normalized = pathText(path);
            if (uniqueFiles.insert(normalized).second)
                files.push_back(normalized);
            if (!path.parent_path().empty())
                selectedDirectory = pathText(path.parent_path());
        }
        if (files.empty()) return;

        auto put = [&prop, filterPath = std::move(selectedDirectory),
                    loaded = std::move(loaded)](const LoadResults& results) mutable {
            if (!results.empty() && results.front().summary) {
                const auto& scenario = results.front().summary;
                if (!scenario->readOnlyPath().empty()) {
                    prop.var().scenarioPath = pathText(
                        utils::nabs(pathFromText(scenario->readOnlyPath()).parent_path()));
                } else if (scenario->useTemp()) {
                    prop.var().scenarioPath = pathText(
                        utils::nabs(pathFromText(filterPath)));
                } else {
                    prop.var().scenarioPath = directoryName(
                        pathText(utils::nabs(pathFromText(filterPath))));
                }
            }
            if (loaded) loaded(results);
        };
        try {
            const auto results = loadScenariosFromFile(
                prop, opt, std::move(owner), sync, std::move(status), files,
                put, std::move(failure), openInThread, std::move(services), control);
            if (!openInThread && !results.empty()) put(results);
        } catch (const std::exception& error) {
            if (status_on_error)
                status_on_error(std::string("シナリオの読み込みを開始できませんでした: ") +
                                error.what());
            if (failure_on_error) failure_on_error();
        } catch (...) {
            if (status_on_error)
                status_on_error("シナリオの読み込みを開始できませんでした。");
            if (failure_on_error) failure_on_error();
        }
    };
    try {
        const bool started = file_picker->open_files_async(
            std::move(request), std::move(selected));
        if (!started)
            control->submission_finished.store(true, std::memory_order_release);
        return started;
    } catch (...) {
        control->submission_finished.store(true, std::memory_order_release);
        return false;
    }
}

LoadResults loadScenariosFromFile(
    Props& prop, const LoadOption& opt, Window owner, FileSync& sync,
    StatusCallback status, const lib::Array<String>& files,
    LoadResultsCallback loaded, FailureCallback failure, bool openInThread,
    lib::Services services, std::shared_ptr<LoadControl> control) {
    services = resolveServices(std::move(services));
    if (!control) control = std::make_shared<LoadControl>();
    if (openInThread && loaded) {
        auto worker = std::make_shared<LSFFThr<true>>(
            prop, opt, std::move(owner), sync, services, std::move(status),
            std::move(failure), control);
        worker->setFiles(files);
        worker->setLoaded(std::move(loaded));
        // D版と同様、スレッド開始前に thr.cursors = setWaitCursors(w) を設定する。
        worker->setWaitCursor(true);
        (void)worker->start();
        return {};
    }

    CursorGuard cursor(owner);
    LoadResults result;
    if (!sync.syncUntilCancelled([&] {
            return control->isCancelled() || (opt.cancelled && opt.cancelled());
        })) return result;
    for (const auto& path : files) {
        if (control->isCancelled()) break;
        lib::Array<String> errorFiles;
        const auto scenario = loadScenarioFromFileImpl(
            prop, opt, errorFiles, owner, sync, status, {}, path, {}, {}, false,
            services.dispatcher, {}, {}, services, control);
        if (!scenario) break;
        result.push_back({scenario, errorFiles});
    }
    return result;
}

String scenarioFilterPath(const Props& prop) {
    // ALL の文書ルートは UserData/Scenario
    // 。UserData 自体には宿やエディタの専用ディレクト
    // リもあるため、 シナリオディレクトリとして表示しない。
    if (lib::editor_window_host_options().all_mode && prop.storage() &&
        !prop.storage()->root().empty())
        return pathText(prop.storage()->root() / "Scenario");
    if (lib::is_managed_user_data_session(prop.images().services()) &&
        prop.storage() && !prop.storage()->root().empty())
        return pathText(prop.storage()->root() / "Scenario");

    // D版と同様、解決したエンジンを参照する前に、最後のシナリオ選択ディレクト
    // リを保持する。 上の ALL・管理対象の文書ルートへの拡張は維持する。
    if (!prop.var().scenarioPath.value.empty())
        return pathText(utils::nabs(pathFromText(prop.var().scenarioPath.value)));
    const auto engine_path = prop.enginePath();
    if (!engine_path.empty())
        return pathText(utils::nabs(pathFromText(engine_path).parent_path() / "Scenario"));
    return {};

}

std::optional<String> selectScenario(const Props& prop, Window,
                                     String dlgTitle, lib::Services services) {
    services = resolveServices(std::move(services));
    if (!services.file_picker) return std::nullopt;
    lib::FilePickerRequest request;
    request.title = std::move(dlgTitle);
    const auto filterPath = scenarioFilterPath(prop);
    request.initial_directory = filterPath.empty() ? Path{} : pathFromText(filterPath);
    request.allow_multiple = false;
    request.filters = makeFilters(prop);
    const auto picked = services.file_picker->open_file(request);
    if (!picked.succeeded() || picked.selections.empty()) return std::nullopt;
    const auto selected=picked.selections.front().native_path();
    if (!selected) return std::nullopt;
    return pathText(utils::nabs(*selected));
}

bool selectScenarioAsync(const Props& prop, Window, String dlgTitle,
                         ScenarioSelectionCallback completed,
                         lib::Services services) {
    if (!completed) return false;
    services = resolveServices(std::move(services));
    if (!services.file_picker) return false;
    lib::FilePickerRequest request;
    request.title = std::move(dlgTitle);
    const auto filterPath = scenarioFilterPath(prop);
    request.initial_directory = filterPath.empty() ? Path{} : pathFromText(filterPath);
    request.allow_multiple = false;
    request.filters = makeFilters(prop);
    try {
        return services.file_picker->open_file_async(
            std::move(request),
            [completed = std::move(completed)](
                lib::FilePickerResult picked) mutable {
                if (!picked.succeeded()) {
                    completed(std::nullopt, std::move(picked.message));
                    return;
                }
                if (picked.selections.empty()) {
                    completed(std::nullopt, {});
                    return;
                }
                try {
                    const auto selected = picked.selections.front().native_path();
                    if (!selected) {
                        completed(std::nullopt,
                                  "file picker returned no native scenario path");
                        return;
                    }
                    completed(pathText(utils::nabs(*selected)), {});
                } catch (const std::exception& error) {
                    completed(std::nullopt, error.what());
                } catch (...) {
                    completed(std::nullopt, "scenario path could not be resolved");
                }
            });
    } catch (...) {
        return false;
    }
}

ScenarioLoadOutcome loadScenarioOutcomeFromFile(
    Props& prop, const LoadOption& opt, Window owner, FileSync& sync,
    StatusCallback status, String fname, lib::Services services,
    std::shared_ptr<LoadControl> control) {
    ScenarioLoadOutcome outcome;
    services = resolveServices(std::move(services));
    if (!control) control = std::make_shared<LoadControl>();
    if (fname.empty() || control->isCancelled()) {
        outcome.status = ScenarioLoadStatus::Cancelled;
        return outcome;
    }

    const Path source = pathFromText(fname);
    std::error_code pathError;
    if (!std::filesystem::exists(source, pathError) || pathError) {
        outcome.status = ScenarioLoadStatus::NotFound;
        outcome.message = utils::tryFormat(prop.requiredMsgs().loadError.value, fname);
        outcome.issues.push_back({fname, outcome.message});
        reportCallbackError(services, prop, outcome.message);
        return outcome;
    }
    if (!supportedScenarioInput(source)) {
        outcome.status = ScenarioLoadStatus::Unsupported;
        outcome.message = utils::tryFormat(prop.requiredMsgs().notScenario.value, fname);
        outcome.issues.push_back({fname, outcome.message});
        reportCallbackError(services, prop, outcome.message);
        return outcome;
    }

    CursorGuard cursor(owner);
    try {
        if (!sync.syncUntilCancelled([&] {
                return control->isCancelled() || (opt.cancelled && opt.cancelled());
            }) || control->isCancelled()) {
            outcome.status = ScenarioLoadStatus::Cancelled;
            return outcome;
        }
        const auto& var = prop.var();
        const auto defaultSkin = dskin::findSkin2(
            prop, var.defaultSkin.value, var.defaultSkinName.value);
        auto load_option = opt;
        const auto prior_cancel = load_option.cancelled;
        load_option.cancelled = [control, prior_cancel] {
            return control->isCancelled() || (prior_cancel && prior_cancel());
        };
        const auto prior_issue = load_option.issueFunc;
        std::mutex issue_mutex;
        load_option.issueFunc = [&outcome, &issue_mutex, prior_issue](String path, String message) {
            std::scoped_lock lock(issue_mutex);
            if (prior_issue) prior_issue(path, message);
            lib::editor_action_log("Scenario member load failed path=\"" + path +
                                   "\" reason=\"" + message + "\"");
            outcome.issues.push_back({std::move(path), std::move(message)});
        };
        outcome.summary = Summary::loadScenarioFromFile(
            prop.parent(), load_option, outcome.errorFiles, fname, prop.tempPath(),
            defaultSkin.get(), {}, {}, {}, {}, scenarioNameFor(fname),
            prop.storage());
        if (control->isCancelled()) {
            outcome.summary.reset();
            outcome.status = ScenarioLoadStatus::Cancelled;
            return outcome;
        }
        if (!outcome.summary) {
            outcome.status = ScenarioLoadStatus::InvalidSummary;
            outcome.message = utils::tryFormat(prop.requiredMsgs().loadError.value, fname);
            outcome.issues.push_back({fname, outcome.message});
            reportCallbackError(services, prop, outcome.message);
            return outcome;
        }
        for (const auto& file : outcome.errorFiles) {
            const auto found = std::find_if(outcome.issues.begin(), outcome.issues.end(),
                [&file](const ScenarioLoadIssue& issue) { return issue.path == file; });
            if (found == outcome.issues.end())
                outcome.issues.push_back({file, "読み込みまたは変換に失敗しました"});
        }
        outcome.status = outcome.errorFiles.empty()
            ? ScenarioLoadStatus::Success : ScenarioLoadStatus::PartialFailure;
        if (status) {
            const String& message = prop.requiredMsgs().loaded;
            status(utils::tryFormat(message, outcome.summary->scenarioName()));
        }
        return outcome;
    } catch (const summary::ScenarioLoadCancelled&) {
        outcome.summary.reset();
        outcome.status = ScenarioLoadStatus::Cancelled;
        return outcome;
    } catch (const summary::SummaryException& error) {
        outcome.message = error.what();
    } catch (const std::exception& error) {
        outcome.message = error.what();
    } catch (...) {
        outcome.message = utils::tryFormat(prop.requiredMsgs().loadError.value, fname);
    }
    outcome.status = classifyLoadFailure(source);
    outcome.issues.push_back({fname, outcome.message});
    if (status) {
        const String& message = prop.requiredMsgs().loadErrorStatus;
        status(utils::tryFormat(message, fname));
    }
    reportCallbackError(services, prop, outcome.message);
    return outcome;
}

std::shared_ptr<LoadControl> loadScenarioOutcomeFromFileAsync(
    Props& prop, const LoadOption& opt, Window owner, FileSync& sync,
    StatusCallback status, String fname, ScenarioLoadOutcomeCallback completed,
    lib::Services services, std::shared_ptr<LoadControl> control, Summary::ReloadOperation reload,
    bool reportErrors) {
    services = resolveServices(std::move(services));
    if (!control) control = std::make_shared<LoadControl>();

    // LSFFThr は終了の通知コールバックまで操作状態を所有する。
    // LoadControl がバックエンドのハンドルを所有するため
    // 、 呼び出し元はワーカー自身を保持せずに処理をキャンセルできる。
    auto worker = std::make_shared<LSFFThr<false>>(
        prop, opt, std::move(owner), sync, services, std::move(status),
        FailureCallback{}, control);
    worker->setScenario(std::move(fname), {});
    worker->setReload(std::move(reload));
    worker->reportOutcomeErrors(reportErrors);
    worker->setLoaded(std::move(completed));
    worker->setWaitCursor(true);
    (void)worker->start();
    return control;
}

ScenarioLoadOutcome loadScenarioOutcome(
    Props& prop, const LoadOption& opt, Window owner, FileSync& sync,
    StatusCallback status, String dlgTitle, lib::Array<String>& openPaths,
    lib::Services services, std::shared_ptr<LoadControl> control) {
    services = resolveServices(std::move(services));
    const auto selected = selectScenario(prop, owner, std::move(dlgTitle), services);
    if (!selected) {
        ScenarioLoadOutcome outcome;
        outcome.status = ScenarioLoadStatus::Cancelled;
        return outcome;
    }
    auto fname = *selected;
    summary::decScenarioPath(fname, openPaths, prop.var().clickIsOpenEvent.value);
    return loadScenarioOutcomeFromFile(
        prop, opt, std::move(owner), sync, std::move(status), std::move(fname),
        std::move(services), std::move(control));
}

Summary::Owner loadScenario(
    Props& prop, const LoadOption& opt, lib::Array<String>& errorFiles,
    Window owner, FileSync& sync, StatusCallback status,
    const Summary::Owner& old, String dlgTitle, lib::Array<String>& openPaths,
    SummaryLoadedCallback loaded, FailureCallback failure, bool openInThread,
    lib::Services services, std::shared_ptr<LoadControl> control) {
    const auto fname = selectScenario(prop, owner, std::move(dlgTitle), services);
    if (!fname) return {};
    auto selected = *fname;
    summary::decScenarioPath(selected, openPaths,
                             prop.var().clickIsOpenEvent.value);
    // D版と同様、常に非nullの内部 put デリゲートを渡す。 呼び出し元のコ
    // ールバックがないだけで、選択画面のラッパーが同期処理にならないようにする。
    const SummaryLoadedCallback loadedCallback = [loaded = std::move(loaded)](
        const Summary::Owner& value, const lib::Array<String>& errors) {
        if (loaded) loaded(value, errors);
    };
    const auto result = loadScenarioFromFile(
        prop, opt, errorFiles, std::move(owner), sync, std::move(status), old,
        selected, loadedCallback, std::move(failure), openInThread,
        std::move(services), control);
    if (!openInThread && result && loadedCallback) loadedCallback(result, errorFiles);
    return result;
}

Summary::Owner loadScenarioFromFile(
    Props& prop, const LoadOption& opt, lib::Array<String>& errorFiles,
    Window owner, FileSync& sync, StatusCallback status,
    const Summary::Owner& old, String fname, SummaryLoadedCallback loaded,
    FailureCallback failure, bool openInThread, lib::Services services,
    std::shared_ptr<LoadControl> control) {
    return loadScenarioFromFileImpl(
        prop, opt, errorFiles, std::move(owner), sync, std::move(status), old,
        std::move(fname), std::move(loaded), std::move(failure), openInThread,
        {}, {}, {}, std::move(services), std::move(control));
}

Summary::Owner loadScenarioFromFileImpl(
    Props& prop, const LoadOption& opt, lib::Array<String>& errorFiles,
    Window owner, FileSync& sync, StatusCallback status,
    const Summary::Owner& old, String fname, SummaryLoadedCallback loaded,
    FailureCallback failure, bool openInThread, Dispatcher current,
    ProgressCallback setMax, ProgressCallback worked, lib::Services services,
    std::shared_ptr<LoadControl> control) {
    services = resolveServices(std::move(services));
    if (!control) control = std::make_shared<LoadControl>();
    if (openInThread && loaded) {
        auto worker = std::make_shared<LSFFThr<false>>(
            prop, opt, std::move(owner), sync, services, std::move(status),
            std::move(failure), control);
        worker->setScenario(std::move(fname), old);
        worker->setLoaded(std::move(loaded));
        // D: if (!current) thr.cursors = setWaitCursors(w).
        if (!current) worker->setWaitCursor(true);
        (void)worker->start();
        return {};
    }

    std::optional<CursorGuard> cursor;
    if (!current) cursor.emplace(owner);
    if (!sync.syncUntilCancelled([&] {
            return control->isCancelled() || (opt.cancelled && opt.cancelled());
        })) return {};
    const auto report_failure = [&](String message) {
        const auto title = String(prop.requiredMsgs().dlgTitWarning);
        const auto line = utils::tryFormat(static_cast<const String&>(prop.requiredMsgs().loadErrorStatus), fname);
        // D版の同期・配列取り込みと同様、モーダル画面を開かずに状態・失敗を通知
        // する。 単一読み込みのワーカーは、独自の SError 警告を持つ。
        const auto dialogs = lib::editor_window_host_options().all_mode
            ? services.dialogs : decltype(services.dialogs){};
        auto delivery = [control, status, failure, dialogs, title, line,
                         message = std::move(message)] {
            if (control->isCancelled()) return;
            if (status) status(line);
            try {
                if (dialogs) (void)dialogs->message({title, message, {},
                    lib::MessageSeverity::Warning, lib::MessageButtons::OK});
            } catch (...) {}
            if (failure) failure();
        };
        const auto dispatcher = current ? current : services.dispatcher;
        if (dispatcher && !dispatcher->is_main_thread()) dispatcher->post(std::move(delivery));
        else delivery();
    };
    try {
        const auto& var = prop.var();
        const auto defaultSkin = dskin::findSkin2(
            prop, var.defaultSkin.value, var.defaultSkinName.value);
        auto load_option = opt;
        const auto prior_cancel = load_option.cancelled;
        load_option.cancelled = [control, prior_cancel] {
            return control->isCancelled() || (prior_cancel && prior_cancel());
        };
        return Summary::loadScenarioFromFile(
            prop.parent(), load_option, errorFiles, fname, prop.tempPath(),
            defaultSkin.get(), {}, old, std::move(setMax), std::move(worked),
            scenarioNameFor(fname));
    } catch (const summary::ScenarioLoadCancelled&) {
        return {};
    } catch (const std::exception& error) {
        report_failure(error.what());
        return {};
    } catch (...) {
        report_failure("Unknown scenario load error");
        return {};
    }
}

ScenarioErrorFilesDialog::ScenarioErrorFilesDialog(
    const Props& prop, Window owner, const Summary& summary,
    const lib::Array<String>& errorFiles,
    const lib::Array<ScenarioLoadIssue>& issues, lib::Services services,
    lib::Action refreshToolbar)
    : prop_(&prop),
      owner_(std::move(owner)),
      summary_(&summary),
      errorFiles_(errorFiles),
      issues_(issues),
      services_(resolveServices(std::move(services))),
      refreshToolbar_(std::move(refreshToolbar)) {}

void ScenarioErrorFilesDialog::createDialog() {
    if (dialog_ || !services_.gui) return;
    dialog_ = services_.gui->create_dialog();
    if (!dialog_) return;
    dialog_->set_visible(false);
    dialog_->resizable = true;
    lib::configure_owned_window(dialog_, owner_,
        lib::OwnedWindowKind::Modal);
    dialog_->on_close_requested = [this] { close(lib::DialogResult::Cancel); };
    if (prop_) {
        const String& title = prop_->requiredMsgs().dlgTitScenarioLoadErrors;
        dialog_->title = title;
        dialog_->icon = prop_->images().warning();
        const auto& saved = prop_->flex().scenarioLoadErrorsDlg;
        dialog_->set_bounds({0.0F, 0.0F,
                             static_cast<float>(std::max(300, saved.width())),
                             static_cast<float>(std::max(300, saved.height()))});
        dialog_->minimum_size = lib::Size{300.0F, 300.0F};
    }
}

void ScenarioErrorFilesDialog::setup(const std::shared_ptr<lib::Container>& area) {
    if (!area || !prop_ || !summary_ || !services_.gui) return;
    auto layout = std::make_shared<lib::GridLayout>();
    layout->columns = 1;
    layout->equal_width = true;
    area->set_layout(std::move(layout));

    const Path original = !summary_->readOnlyPath().empty()
        ? pathFromText(summary_->readOnlyPath())
        : summary_->useTemp() ? pathFromText(summary_->origZipName())
                              : pathFromText(summary_->scenarioPath());
    const String originalName = pathText(original.filename());
    const String text = originalName.empty()
        ? utils::tryFormat(static_cast<const String&>(prop_->requiredMsgs().scenarioLoadErrorsNoFileName),
                           summary_->scenarioName())
        : utils::tryFormat(static_cast<const String&>(prop_->requiredMsgs().scenarioLoadErrors),
                           summary_->scenarioName(), originalName);
    auto labelWidget = services_.gui->create_component(lib::ComponentKind::Label);
    auto label = std::dynamic_pointer_cast<lib::Label>(labelWidget);
    if (!label) label = std::make_shared<lib::Label>();
    label->text = text;
    label->wrap = true;
    area->add(label);

    auto tableWidget = services_.gui->create_component(lib::ComponentKind::TableView);
    files_ = std::dynamic_pointer_cast<lib::TableView>(tableWidget);
    if (!files_) files_ = std::make_shared<lib::TableView>();
    files_->multi_select = true;
    files_->full_row_selection = true;
    files_->columns.push_back({"ファイル", 0.42F, lib::TextAlignment::Start, true, false});
    files_->columns.push_back({"理由", 0.58F, lib::TextAlignment::Start, true, false});

    auto sorted = errorFiles_;
    if (static_cast<bool>(prop_->var().logicalSort.value)) {
        std::sort(sorted.begin(), sorted.end(), [](const String& left, const String& right) {
            return utils::fnncmp(left, right) < 0;
        });
    } else {
        std::sort(sorted.begin(), sorted.end(), [](const String& left, const String& right) {
            return utils::fncmp(left, right) < 0;
        });
    }
    for (const auto& file : sorted) {
        const auto issue = std::find_if(issues_.begin(), issues_.end(),
            [&file](const ScenarioLoadIssue& value) { return value.path == file; });
        lib::TableRow row;
        row.cells.push_back({file, {}, 0, false, true});
        row.cells.push_back({issue == issues_.end() || issue->message.empty()
                                 ? String{"読み込みまたは変換に失敗しました"}
                                 : issue->message,
                             {}, 0, false, true});
        files_->rows.push_back(std::move(row));
    }
    area->add(files_);

    menu_ = std::make_shared<lib::Menu>(true);
    auto copy_text = prop_->buildMenu(types::MenuID::Copy);
    auto select_all_text = prop_->buildMenu(types::MenuID::SelectAll);
    if (copy_text.empty()) copy_text = "コピー";
    if (select_all_text.empty()) select_all_text = "すべて選択";
    menu_->items.push_back({std::move(copy_text), "copy"});
    menu_->items.push_back({{}, {}, {}, 0, true, false, false, true});
    menu_->items.push_back({std::move(select_all_text), "select_all"});
    menu_->on_open = [this] {
        if (!files_ || !menu_) return;
        menu_->items[0].enabled = !files_->selection.empty();
        menu_->items[2].enabled = files_->selection.size() < files_->rows.size();
    };
    menu_->on_command = [this](std::string_view command) {
        // 保留中のホストコマンドでも、D版と同じメニュー実行条件を適用する。
        if (!files_) return;
        if (command == "copy" && !files_->selection.empty()) copy();
        if (command == "select_all" && files_->selection.size() < files_->rows.size()) selectAll();
    };
    files_->on_pointer = [this](lib::PointerEvent& event) {
        if (event.action != lib::PointerAction::Down ||
            event.button != lib::PointerButton::Secondary || !menu_ ||
            !services_.gui || !files_) {
            return;
        }
        if (menu_->on_open) menu_->on_open();
        services_.gui->show_context_menu(menu_, files_, event.position);
        event.handled = true;
    };

    auto footer = std::make_shared<lib::Panel>();
    auto footer_layout = std::make_shared<lib::LinearLayout>(
        lib::Orientation::Horizontal);
    footer_layout->spacing = 8.0F;
    footer->set_layout(std::move(footer_layout));

    ok_button_ = std::make_shared<lib::Button>("OK");
    ok_button_->default_button = true;
    ok_button_->on_click = [this] { close(lib::DialogResult::OK); };
    footer->add(ok_button_);

    cancel_button_ = std::make_shared<lib::Button>("キャンセル");
    cancel_button_->cancel_button = true;
    cancel_button_->on_click = [this] { close(lib::DialogResult::Cancel); };
    footer->add(cancel_button_);
    area->add(footer);
    // D版の firstFocusIsOK と同様、ウィンドウが表示された後でOKを
    // 選ぶ。 共通のモーダル実行処理でも、その有効範囲内のフォーカス指定を維持する。
    dialog_->on_visibility_changed =
        [gui = std::weak_ptr<lib::GuiBackend>(services_.gui),
         button = std::weak_ptr<lib::Button>(ok_button_)](bool visible) {
            if (!visible) return;
            const auto live_gui = gui.lock();
            const auto live_button = button.lock();
            if (live_gui && live_button) live_gui->focus(live_button);
        };
}

bool ScenarioErrorFilesDialog::open() {
    // D版と同様、エラーファイルの表を専用ダイアログで表示する。 ネイティブ
    // のメッセージ表示は、部品GUIがない場合の最終的な代替手段だけに使う
    // 。 それ以外で使うと、コピー・全選択と完全なファイル一覧が失われる。
    if ((!services_.gui || !prop_ || !summary_) && prop_ && summary_ &&
        services_.dialogs) {
        const Path original = !summary_->readOnlyPath().empty()
            ? pathFromText(summary_->readOnlyPath())
            : summary_->useTemp() ? pathFromText(summary_->origZipName())
                                  : pathFromText(summary_->scenarioPath());
        const String originalName = pathText(original.filename());
        const String message = originalName.empty()
            ? utils::tryFormat(
                  static_cast<const String&>(prop_->requiredMsgs().scenarioLoadErrorsNoFileName),
                  summary_->scenarioName())
            : utils::tryFormat(
                  static_cast<const String&>(prop_->requiredMsgs().scenarioLoadErrors),
                  summary_->scenarioName(), originalName);
        std::ostringstream details;
        auto sorted = errorFiles_;
        if (static_cast<bool>(prop_->var().logicalSort.value)) {
            std::sort(sorted.begin(), sorted.end(), [](const String& left, const String& right) {
                return utils::fnncmp(left, right) < 0;
            });
        } else {
            std::sort(sorted.begin(), sorted.end(), [](const String& left, const String& right) {
                return utils::fncmp(left, right) < 0;
            });
        }
        for (std::size_t index = 0; index < sorted.size(); ++index) {
            if (index) details << '\n';
            details << sorted[index];
            const auto issue = std::find_if(issues_.begin(), issues_.end(),
                [&](const ScenarioLoadIssue& value) { return value.path == sorted[index]; });
            if (issue != issues_.end() && !issue->message.empty())
                details << "\n  理由: " << issue->message;
        }
        const auto result = services_.dialogs->message({
            static_cast<const String&>(prop_->requiredMsgs().dlgTitScenarioLoadErrors),
            message, details.str(), lib::MessageSeverity::Warning,
            lib::MessageButtons::YesNo});
        return result == lib::MessageResult::Yes || result == lib::MessageResult::OK;
    }
    createDialog();
    if (!dialog_ || !services_.gui) return false;
    if (!setup_complete_) {
        auto area = std::make_shared<lib::Panel>();
        setup(area);
        dialog_->add(area);
        setup_complete_ = true;
    }
    dialog_->result = lib::DialogResult::None;
    const auto result = services_.gui->show_modal(dialog_, owner_);
    return result == lib::DialogResult::OK || result == lib::DialogResult::Yes;
}

void ScenarioErrorFilesDialog::close(const lib::DialogResult result) {
    if (!dialog_) return;
    dialog_->result = result;
    if (services_.gui) services_.gui->close(dialog_);
}

void ScenarioErrorFilesDialog::copy() {
    if (!files_ || !services_.clipboard) return;
    std::ostringstream output;
    bool first = true;
    for (const auto index : files_->selection) {
        if (index >= files_->rows.size() || files_->rows[index].cells.empty()) continue;
        if (!first) output << '\n';
        first = false;
        output << files_->rows[index].cells.front().text;
    }
    services_.clipboard->write({output.str(), {}, {}});
    if (refreshToolbar_) refreshToolbar_();
}

void ScenarioErrorFilesDialog::selectAll() {
    if (!files_) return;
    std::vector<std::size_t> selection;
    selection.reserve(files_->rows.size());
    for (std::size_t index = 0; index < files_->rows.size(); ++index) {
        selection.push_back(index);
    }
    (void)lib::select_table_rows(*files_, std::move(selection), false, false);
}

template class LSFFThr<true>;
template class LSFFThr<false>;

}  // namespace cwxeditor::cwx::editor::gui::dwt::loader
