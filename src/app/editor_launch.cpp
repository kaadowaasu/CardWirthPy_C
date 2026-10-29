#include "editor_launch.hpp"

#if defined(CARDWIRTH_EMBED_EDITOR)

#include "all/all_layout.hpp"
#include "cwxeditor_src/core/cwx/props.hpp"
#include "cwxeditor_src/core/cwx/system.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dprops.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/mainwindow.hpp"
#include "cwxeditor_src/lib/capabilities.hpp"
#include "cwxeditor_src/lib/editor_diagnostics.hpp"
#include "cwxeditor_src/lib/lifecycle.hpp"
#include "cwxeditor_src/lib/runtime_context.hpp"
#include "cwxeditor_src/lib/services.hpp"
#include "cwxeditor_src/lib/sdl_ui.hpp"
#include "cwxeditor_src/lib/window_host.hpp"
#include "cwxeditor_src/source/editor_session_setup.hpp"
#include "cw/lib/archive.hpp"
#include "platform/app_visibility.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace cardwirth::app {
namespace {

std::string path_to_utf8(const std::filesystem::path& value) {
    const auto encoded = value.u8string();
    return std::string(reinterpret_cast<const char*>(encoded.data()), encoded.size());
}

std::string startup_error(const std::exception& error) {
    return std::string("XEditor_Cの起動に失敗しました: ") + error.what();
}

std::string startup_error() {
    return "XEditor_Cの起動に失敗しました: 不明な例外";
}

}  // namespace

bool editor_embedded_available() noexcept { return true; }

namespace detail {

class HostedEditorSessionImpl final {
public:
    ~HostedEditorSessionImpl() {
        shutdown();
        if (owns_diagnostics_) ::cwxeditor::lib::shutdown_editor_diagnostics();
    }

    bool start(::cardwirth::platform::AppRuntimeContext& host_context,
               const EditorLaunchOptions& options,
               const EditorHostOptions& host_options) {
        if (running_) return false;
        if (finished_) shutdown_objects();
        audio_visibility_ = {};
        error_.clear();
        exit_code_ = -1;
        finished_ = false;
        faulted_ = false;
        stop_pending_ = false;
        defer_stop_until_next_poll_ = false;
        shutdown_loads_stop_requested_ = false;
        // 再起動ごとに設定のスナップショットを作り直す。前のセッションの保存完
        // 了状態を 引き継ぐと、終了時の保存やエラー処理が実行されなくなる。
        shutdown_persist_started_ = false;
        external_navigation_pending_ = false;
        covered_ = false;
        input_blocked_ = false;
        blocked_windows_.clear();
        blocked_focus_.reset();
        try {
            owns_diagnostics_ = owns_diagnostics_ || !::cwxeditor::lib::editor_diagnostics_enabled();
            ::cwxeditor::lib::ensure_editor_diagnostics(host_context.paths.app_root);
            ::cwxeditor::lib::editor_action_log("Hosted Editor session started");
            ::cwxeditor::cwxeditor::prepare_editor_resources();
            previous_host_options_ = ::cwxeditor::lib::editor_window_host_options();
            host_options_active_ = true;
            ::cwxeditor::lib::set_editor_window_host_options({
                // HostedEditorSessionは、ネイティブのウィンドウハン
                // ドルがないブラウザーでもALL用のEditorとして扱う。 ハンドル
                // の有無からUIモードを推定すると、単独起動用の枠や部品が混入する。
                true,
                host_options.native_parent,
                host_options.native_sdl_window,
                host_options.native_renderer,
                host_options.keep_top_bar,
                host_options.allow_internal_move,
                {static_cast<float>(host_options.initial_bounds.width),
                 static_cast<float>(host_options.initial_bounds.height)},
                // Web専用のホスト設定は設けず、Web版とAndroid
                // 版のALLで同じEditor UIを表示する。
            });

            services_ = ::cwxeditor::lib::make_platform_services();
            if (services_.gui) services_.gui->set_host_iteration(host_iteration_);
            const auto platform = services_.gui
                ? services_.gui->platform() : ::cwxeditor::lib::Platform::Headless;
            auto capabilities = ::cwxeditor::lib::probe_capabilities(services_, platform);
            // UserDataはホストが所有する。Editor自身の書
            // き込み先はすべてUserData/editor配下とし
            // 、 UserData直下は使わない。編集対象の文書は引
            // き続きホストのScenarioディレクトリを参照する。
            auto editor_paths = ::cardwirth::platform::AppPaths::from_roots(
                host_context.paths.app_root,
                host_context.paths.user_root / "editor");
            editor_paths.scenario_root = host_context.paths.scenario_root;
            editor_context_ = std::make_unique<::cwxeditor::lib::EditorRuntimeContext>(
                services_, host_context.storage_ref(), std::move(editor_paths),
                std::move(capabilities));
            editor_context_->initialize_session_services();
            const auto validation = editor_context_->validate();
            if (!validation.ok) {
                error_ = validation.errors.empty()
                    ? "XEditor_Cを起動できません: Editor runtime contextが不正です"
                    : "XEditor_Cを起動できません: " + validation.errors.front();
                shutdown_objects();
                finished_ = true;
                return false;
            }
            std::string layout_error;
            if (!editor_context_->ensure_storage_layout(&layout_error)) {
                error_ = layout_error.empty()
                    ? "XEditor_Cを起動できません: エディタ保存領域を初期化できません"
                    : "XEditor_Cを起動できません: " + layout_error;
                shutdown_objects();
                finished_ = true;
                return false;
            }
            editor_context_->mark_storage_ready();
            editor_context_->lifecycle().on_persist([this, context = editor_context_.get()] {
                // finish()で最終設定の保存完了を待ってから、StopRequested
                // で資源を解放する。 完了確認後に、結果を確
                // 認できない2回目の保存要求を追加しない。
                if (!shutdown_persist_started_ && context && !context->is_shutdown())
                    (void)context->persist_storage();
            });
            editor_context_->lifecycle().dispatch(::cwxeditor::lib::LifecycleEvent::Ready);

            const auto user_root = host_context.paths.user_root;
            const auto executable_name = options.executable_path.filename().empty()
                ? std::filesystem::path("XEditor_C.exe")
                : options.executable_path.filename();
            const auto application_root = options.application_directory.empty()
                ? user_root : options.application_directory;
            const std::string editor_app_text = path_to_utf8(application_root / executable_name);
            const std::string config_text = path_to_utf8(
                user_root / "editor" / "Config" / "cwxeditor.config");

            system_ = std::make_unique<::cwxeditor::cwx::system::System>();
            auto model = ::cwxeditor::cwxeditor::make_editor_model_session(
                *editor_context_, *system_, editor_app_text, config_text,
                path_to_utf8(user_root / "Data" / "Skin" / "Classic" / "Skin.xml"));
            core_properties_ = std::move(model.core);
            properties_ = std::move(model.properties);

            ::cwxeditor::cwx::editor::gui::dwt::mainwindow::MainWindowContext window_context;
            window_context.runtimeContext = editor_context_.get();
            window_context.confirmClose = [] { return true; };
            window_context.status = [&host_context](std::string message) {
                if (host_context.report_error) host_context.report_error(message);
            };
            window_context.executeEngine = host_options.execute_engine;
            window_context.requestHome = host_options.request_home;
            window_context.requestEngine = host_options.request_engine;
            window_context.requestData = host_options.request_data;
            ::cwxeditor::lib::set_editor_data_directory_opener(
                host_options.request_data_directory);
            ::cwxeditor::cwx::editor::gui::dwt::mainwindow::LaunchOption launch_option;
            launch_option.noload = !host_options.restore_previous_scenario;
            window_ = std::make_unique<
                ::cwxeditor::cwx::editor::gui::dwt::mainwindow::MainWindow>(
                    editor_app_text, *system_, *properties_,
                    launch_option, services_,
                    std::move(window_context));
            shell_ = std::dynamic_pointer_cast<::cwxeditor::lib::Window>(window_->shell());
            if (!services_.gui || !shell_) {
                error_ = "XEditor_Cを起動できません: GUI backendがありません";
                // 起動が正常なEditorセッションに到達しなかった
                // ため、途中まで生成した ウィンドウを解放する前に異
                // 常終了とし、通常の設定・セッション保存を抑止する。
                faulted_ = true;
                shutdown();
                return false;
            }
            const auto& initial = host_options.initial_bounds;
            shell_->set_bounds({static_cast<float>(initial.x), static_cast<float>(initial.y),
                                static_cast<float>(std::max(1, initial.width)),
                                static_cast<float>(std::max(1, initial.height))});
            services_.gui->show(shell_);
            running_ = true;
            startup_pending_ = true;
            return true;
        } catch (const std::exception& exception) {
            error_ = startup_error(exception);
        } catch (...) {
            error_ = startup_error();
        }
        // ここではMainWindowが部分的に生成済みの場合がある。
        // 通常の終了ではなく起動中断なので、 不完全な位置や設定で、
        // 最後に正常終了したEditorのセッションを上書きしない。
        faulted_ = true;
        shutdown();
        return false;
    }

    bool tick(bool wait) {
        if (!running_) return false;
        // モーダル待機ではALLのイベント処理が再帰的に呼ばれる。 外側のEditor処
        // 理が戻るまでスタックとセッションを保持し、Editorの処理を再入させない。
        if (ui_depth_ != 0U) return true;
        poll_background();
        if (!running_ || stop_pending_) return running_;
        if (std::exchange(startup_pending_, false)) {
            // 前回のシナリオを読み込む際にモーダル警告が開く場合がある
            // ため、start()が戻ってから復元する。 それまでにホ
            // ストがセッションを登録し、Editorページを選択して入
            // 力と合成を接続しておく。 入れ子の更新で復元処理を繰り返
            // さないよう、呼び出し前にフラグを落とす。 以前の操作状態
            // は破棄するが、読み込み通知のキャンセルは操作できるように
            // する。 文書の操作は共通の非同期モーダル処理で制限し、
            // ダイアログ入力まで遮るホスト全体のブロックは使わない。
            if (services_.gui && shell_) services_.gui->suspend_input(shell_);
            if (!ui_action([&] { window_->doCWX(); })) return running_;
            if (!running_) return false;
        }
        bool event_loop_alive = true;
        if (!ui_action([&] {
                if (window_) {
                    window_->dispatchUiEvent([&] {
                        event_loop_alive = services_.gui->pump_events(wait);
                    });
                    if (window_) window_->uiDispatchComplete();
                }
            })) return running_;
        if (!window_) {
            if (running_) (void)ui_action([&] { finish(0); });
            return running_;
        }
        if (!event_loop_alive || window_->quitRequested() || !shell_->visible()) {
            (void)ui_action([&] { finish(0); });
            return running_; // 読み込み処理が協調的な停止を進めている場合がある。
        }
        // ここでネイティブの子ウィンドウを繰り返しホスト内へ戻さな
        // い。 タイトルバーのドラッグ中はWindowsがWINDOWPOS
        // の更新を管理しており、 SetWindowPosによる
        // 補正が競合するとウィンドウが動かなくなる。 移動範囲
        // はWM_WINDOWPOSCHANGINGで制限し、
        // ALL側はクライアント領域変更時に一度だけ補正する。
        return true;
    }

    bool dispatch_native_event(const void* event) {
        if (input_blocked_ || faulted_) return running_;
        bool handled = running_;
        (void)ui_action([&] {
            if (window_) {
                window_->dispatchUiEvent([&] {
                    handled = running_ && services_.gui &&
                        services_.gui->dispatch_native_event(event);
                });
                if (window_) window_->uiDispatchComplete();
            }
        });
        return window_ && handled;
    }
    void render_hosted() {
        if (!faulted_) (void)ui_action([&] {
            if (running_ && services_.gui) services_.gui->render_hosted();
        });
    }

    void poll_background() {
        if (!window_ || ui_depth_ != 0U) return;
        (void)ui_action([&] {
            // ホームなどがEditorを覆っている場合や、停止要求で通常の更新
            // が止まっている場合も、 保存完了の処理は進める。この完了通知よ
            // り先にsaveInProgressを確認すると相互待ちになる。
            if (editor_context_) {
                (void)editor_context_->poll_storage();
                if (editor_context_->state() == ::cwxeditor::lib::RuntimeState::StorageError) {
                    const auto diagnostic = editor_context_->last_error();
                    if (diagnostic != storage_error_reported_) {
                        storage_error_reported_ = diagnostic;
                        window_->setStatusLine("保存領域への反映に失敗しました: " + diagnostic);
                    }
                } else if (editor_context_->state() == ::cwxeditor::lib::RuntimeState::Ready) {
                    storage_error_reported_.clear();
                }
                window_->pollStoragePersistence();
            }
            // ALLからホームへの移動や終了が要求された場合、シナリオのワーカーをキ
            // ャンセルしてから完了を待つ。 hasBackgroundLoads()
            // が偽になるのを先に待つと、Editorが終了できなくなる場合がある。
            if (stop_pending_ && !shutdown_loads_stop_requested_) {
                shutdown_loads_stop_requested_ = true;
                (void)window_->stopBackgroundLoads();
            }
            if (window_->saveInProgress()) return;
            window_->pollBackgroundLoads();
            if (!stop_pending_) window_->backupThr();
            if (stop_pending_ && !window_->hasBackgroundLoads()) {
                if (defer_stop_until_next_poll_) {
                    // Editorが終了時の最終設定を書き出す
                    // 前に、ホームを1フレーム描画し終える。
                    defer_stop_until_next_poll_ = false;
                    return;
                }
                stop_pending_ = false;
                finish(pending_exit_code_);
            }
        });
    }
    void set_surface_transform(const all::CoordinateTransform& transform) {
        if (!running_ || !services_.gui) return;
        services_.gui->set_host_coordinate_map([transform](::cwxeditor::lib::Point point)
            -> std::optional<::cwxeditor::lib::Point> {
            if (!transform.to_source_unclipped({transform.destination.x,transform.destination.y})) return {};
            const auto host = transform.to_host(all::Point{point.x,point.y});
            return ::cwxeditor::lib::Point{host.x,host.y};
        });
    }

    void resize_surface(const int width, const int height) {
        if (!running_ || !services_.gui || !shell_ || width <= 0 || height <= 0) return;
        const ::cwxeditor::lib::Size size{static_cast<float>(width),
                                         static_cast<float>(height)};
        services_.gui->set_hosted_canvas_size(size);
        services_.gui->set_window_bounds(shell_, {0.0F, 0.0F, size.width, size.height});
        // ドックがペインの比率を再計算する前に、新しい埋め込み
        // 領域を反映する。 90度回転時には幅が先に変わり、
        // ブラウザーがサイズ変更を複数回通知する場合がある。
        if (window_) window_->hostedCanvasResized();
    }

    void enter_background() {
        set_audio_visibility(::cardwirth::platform::AppVisibility::Background, true);
        if (editor_context_ && !editor_context_->is_shutdown())
            editor_context_->lifecycle().dispatch(
                ::cwxeditor::lib::LifecycleEvent::EnterBackground);
    }

    void enter_foreground() {
        set_audio_visibility(::cardwirth::platform::AppVisibility::Background, false);
        if (editor_context_ && !editor_context_->is_shutdown())
            editor_context_->lifecycle().dispatch(
                ::cwxeditor::lib::LifecycleEvent::EnterForeground);
    }

    void renderer_reset() {
        // 資源の無効化はライフサイクル処理なので、入力がブロックされていても実行する。
        ::cwxeditor::lib::invalidate_sdl_renderer_resources(services_);
    }

    void memory_pressure() {
        if (editor_context_ && !editor_context_->is_shutdown())
            editor_context_->lifecycle().dispatch(
                ::cwxeditor::lib::LifecycleEvent::MemoryPressure);
    }

    void set_audio_visibility(::cardwirth::platform::AppVisibility::Reason reason, bool active) {
        if (audio_visibility_.set(reason, active) && services_.audio)
            services_.audio->set_suspended(audio_visibility_.suspended());
    }
    void set_covered(bool covered) {
        if (covered == covered_) return;
        covered_ = covered;
        // 他のページが共有描画領域を使う間もALLはEditorを保持する。 所
        // 有権の切り替え時に操作中のジェスチャーや文字入力を打ち切り、 後から届
        // くUpやEnterで非表示のEditor部品が動作しないようにする。
        if (covered && services_.gui) {
            const auto input_window = services_.gui->active_input_window();
            if (input_window) services_.gui->suspend_input(input_window);
            if (shell_ && input_window != shell_) services_.gui->suspend_input(shell_);
        }
        set_audio_visibility(::cardwirth::platform::AppVisibility::Covered, covered);
    }

    void set_input_blocked(const bool blocked) {
        if (blocked == input_blocked_) return;
        if (!blocked) {
            input_blocked_ = false;
            for (auto& [window, was_enabled] : blocked_windows_)
                if (const auto value = window.lock()) {
                    value->set_enabled(was_enabled);
                    if (services_.gui) services_.gui->request_redraw(value);
                }
            blocked_windows_.clear();
            if (services_.gui) {
                const auto restore_focus = blocked_focus_.lock();
                if (restore_focus) services_.gui->focus(restore_focus);
            }
            blocked_focus_.reset();
            return;
        }

        input_blocked_ = true;
        if (services_.gui) blocked_focus_ = services_.gui->focused_widget();
        const auto block = [this](const std::shared_ptr<::cwxeditor::lib::Window>& value) {
            if (!value) return;
            const bool already_recorded = std::ranges::any_of(
                blocked_windows_, [&value](const auto& entry) {
                    return !entry.first.owner_before(value) &&
                           !value.owner_before(entry.first);
                });
            if (already_recorded) return;
            blocked_windows_.emplace_back(value, value->enabled());
            value->set_enabled(false);
            if (services_.gui) services_.gui->request_redraw(value);
        };
        block(shell_);
        if (services_.gui) {
            const auto input_window = services_.gui->active_input_window();
            block(input_window);
            // ALL側のモーダル画面も、ページの被覆やネイティブの非アクティブ化と
            // 同じキャンセル処理を使う。 capture_pointer(false)
            // だけではタッチ・ドラッグ・ポップアップ・IMEの状態が残る。
            if (input_window) services_.gui->suspend_input(input_window);
            if (shell_ && input_window != shell_) services_.gui->suspend_input(shell_);
        }
    }

    bool prepare_stop(bool returning_home) {
        if (!running_ || !window_) return false;
        return window_->qSave(
            returning_home
                ? ::cwxeditor::cwx::editor::gui::dwt::mainwindow::MainWindow::QSaveType::home
                : ::cwxeditor::cwx::editor::gui::dwt::mainwindow::MainWindow::QSaveType::close);
    }
    bool prepare_stop_async(bool returning_home,
                            std::function<void(bool)> completed) {
        if (!running_ || !window_ || !completed) return false;
        if (returning_home)
            return window_->prepareHomeReturnAsync(std::move(completed));
        return window_->qSaveAsync(
            ::cwxeditor::cwx::editor::gui::dwt::mainwindow::MainWindow::QSaveType::close,
            std::move(completed));
    }
    bool prepare_external_navigation() {
        if (!running_ || !window_ || external_navigation_pending_) return false;
        if (!window_->prepareExternalNavigation()) return false;
        external_navigation_pending_ = true;
        return true;
    }
    bool prepare_external_navigation_async(
        std::function<void(bool)> completed) {
        if (!running_ || !window_ || external_navigation_pending_ || !completed)
            return false;
        return window_->prepareExternalNavigationAsync(
            [this, completed = std::move(completed)](const bool accepted) mutable {
                if (accepted) external_navigation_pending_ = true;
                completed(accepted);
            });
    }
    void reconcile_external_changes() {
        if (!running_ || !window_ ||
            !std::exchange(external_navigation_pending_, false)) return;
        window_->reloadAfterExternalNavigation();
    }
    void request_stop() {
        if (!running_) return;
        // ホームへ切り替えるホストイベント内では、Editorを
        // 同期的に破棄しない。 ホームを1フレーム描画した後、次
        // のAppIterateで終了状態の確認と保存を行う。
        pending_exit_code_ = 0;
        stop_pending_ = true;
        defer_stop_until_next_poll_ = true;
        set_input_blocked(true);
    }
    void set_host_iteration(std::function<bool()> iteration) {
        host_iteration_ = std::move(iteration);
        if (services_.gui) services_.gui->set_host_iteration(host_iteration_);
    }
    void constrain_to_host() {
        if (running_ && services_.gui && shell_)
            services_.gui->constrain_window_to_host(shell_);
    }
    [[nodiscard]] bool save_in_progress() const noexcept {
        return window_ && window_->saveInProgress();
    }
    [[nodiscard]] bool running() const noexcept { return running_; }
    [[nodiscard]] bool stop_requested() const noexcept { return stop_pending_; }
    [[nodiscard]] bool finished() const noexcept { return finished_; }
    [[nodiscard]] int exit_code() const noexcept { return exit_code_; }
    [[nodiscard]] const std::string& error() const noexcept { return error_; }

private:
    template<class Work> bool ui_action(Work&& work) noexcept {
        struct DispatchScope {
            unsigned& depth;
            explicit DispatchScope(unsigned& value) : depth(value) { ++depth; }
            ~DispatchScope() { --depth; }
        } dispatch(ui_depth_);
        try { work(); return true; }
        catch (const ::cwxeditor::lib::RecoverableUiError& error) {
            ::cwxeditor::lib::editor_action_log(error.what());
            try { if (window_) window_->setStatusLine(std::string("操作を取り消しました: ") + error.what()); }
            catch (...) { fail_ui("Editor recovery failed"); }
        }
        catch (const std::exception& error) { fail_ui(error.what()); }
        catch (...) { fail_ui("Unknown Editor UI failure"); }
        return false;
    }
    void fail_ui(const char* message) noexcept {
        // 原因不明の失敗後は自動保存も編集の継続も行わない。 このセッシ
        // ョンだけを隔離し、EngineとALLホストは動作を継続する。
        faulted_ = true;
        ::cwxeditor::lib::editor_action_log(message);
        try {
            error_ = message;
            set_input_blocked(true);
            finish(-1);
        } catch (...) {
            // 特に入れ子のモーダル処理中はfinishedを通知しない。 外側
            // の保存処理が継続中なのに、ホストがセッションを破棄するのを防ぐ。
            input_blocked_ = true;
            stop_pending_ = true;
            pending_exit_code_ = -1;
        }
    }
    void finish(int exit_code) {
        if (!running_ && finished_) return;
        if (ui_depth_ > 1U || (window_ && (window_->saveInProgress() ||
                                          !window_->stopBackgroundLoads()))) {
            stop_pending_ = true;
            pending_exit_code_ = exit_code;
            set_input_blocked(true);
            return;
        }
        const auto defer = [&] {
            stop_pending_ = true;
            pending_exit_code_ = exit_code;
            set_input_blocked(true);
        };
        const auto cancel = [&](const std::string& message) {
            stop_pending_ = false;
            defer_stop_until_next_poll_ = false;
            shutdown_loads_stop_requested_ = false;
            shutdown_persist_started_ = false;
            set_input_blocked(false);
            if (window_) {
                window_->cancelShutdownAfterStorageFailure();
                window_->setStatusLine(message);
            }
        };
        // ネイティブのウィンドウ寸法が失われる前、かつStopRequested
        // でホストの保存を確定する前に設定を書く。 window_.reset()
        // まで待つと、実行環境の終了後に設定保存が行われてしまう。
        if (!faulted_ && window_) {
            if (editor_context_) {
                (void)editor_context_->poll_storage();
                if (editor_context_->state() == ::cwxeditor::lib::RuntimeState::SavingStorage) {
                    defer();
                    return;
                }
                if (shutdown_persist_started_ && editor_context_->state() ==
                        ::cwxeditor::lib::RuntimeState::StorageError) {
                    cancel("ウィンドウ情報の永続化に失敗しました。ホームには戻りません: " +
                           editor_context_->last_error());
                    return;
                }
            }
            if (!shutdown_persist_started_) {
                if (editor_context_ && editor_context_->state() ==
                        ::cwxeditor::lib::RuntimeState::StorageError &&
                        !editor_context_->retry_storage()) {
                    cancel("保存領域を再開できません: " + editor_context_->last_error());
                    return;
                }
                if (!window_->saveSessionStateForShutdown()) {
                    cancel("ウィンドウ情報を保存できませんでした。ホームには戻りません。");
                    return;
                }
                shutdown_persist_started_ = true;
                const auto result = editor_context_ ? editor_context_->persist_storage_result()
                    : ::cwxeditor::lib::StoragePersistResult::Completed;
                if (result == ::cwxeditor::lib::StoragePersistResult::Pending) {
                    window_->setStatusLine("ウィンドウ情報の永続化を待っています…");
                    defer();
                    return;
                }
                if (result == ::cwxeditor::lib::StoragePersistResult::Failed) {
                    cancel("ウィンドウ情報を永続化できません: " + editor_context_->last_error());
                    return;
                }
            }
        }
        if (!faulted_) set_input_blocked(false);
        running_ = false;
        startup_pending_ = false;
        external_navigation_pending_ = false;
        // セッションのウィンドウを閉じる前に、UIを待っているワーカーを
        // 解放する。 再起動時はサービスとディスパッチャーを作り直す。
        if (services_.dispatcher) services_.dispatcher->shutdown();
        if (services_.gui && shell_ && shell_->visible()) services_.gui->close(shell_);
        if (editor_context_ && !editor_context_->is_shutdown()) {
            editor_context_->lifecycle().dispatch(
                ::cwxeditor::lib::LifecycleEvent::StopRequested);
            editor_context_->shutdown();
            editor_context_->lifecycle().dispatch(::cwxeditor::lib::LifecycleEvent::Stopped);
        }
        exit_code_ = exit_code;
        finished_ = true;
    }
    void shutdown_objects() {
        shell_.reset();
        window_.reset();
        properties_.reset();
        core_properties_.reset();
        system_.reset();
        editor_context_.reset();
        services_ = {};
        ::cwxeditor::lib::set_editor_data_directory_opener({});
        if (host_options_active_) {
            ::cwxeditor::lib::set_editor_window_host_options(previous_host_options_);
            host_options_active_ = false;
        }
    }
    void shutdown() noexcept {
        try { finish(exit_code_ < 0 ? 0 : exit_code_); } catch (...) {}
        shutdown_objects();
    }

    ::cwxeditor::lib::Services services_{};
    std::unique_ptr<::cwxeditor::lib::EditorRuntimeContext> editor_context_{};
    std::unique_ptr<::cwxeditor::cwx::system::System> system_{};
    std::unique_ptr<::cwxeditor::cwx::props::CProps> core_properties_{};
    std::unique_ptr<::cwxeditor::cwx::editor::gui::dwt::dprops::Props> properties_{};
    std::unique_ptr<::cwxeditor::cwx::editor::gui::dwt::mainwindow::MainWindow> window_{};
    std::shared_ptr<::cwxeditor::lib::Window> shell_{};
    bool running_{};
    bool owns_diagnostics_{};
    bool stop_pending_{};
    bool defer_stop_until_next_poll_{};
    bool shutdown_loads_stop_requested_{};
    bool shutdown_persist_started_{};
    bool faulted_{};
    bool external_navigation_pending_{};
    unsigned ui_depth_{};
    int pending_exit_code_{};
    bool startup_pending_{};
    std::function<bool()> host_iteration_;
    bool finished_{};
    int exit_code_{-1};
    std::string error_{};
    std::string storage_error_reported_{};
    ::cwxeditor::lib::EditorWindowHostOptions previous_host_options_{};
    bool host_options_active_{};
    bool input_blocked_{};
    bool covered_{};
    ::cardwirth::platform::AppVisibility audio_visibility_;
    std::vector<std::pair<std::weak_ptr<::cwxeditor::lib::Window>, bool>>
        blocked_windows_;
    std::weak_ptr<::cwxeditor::lib::Widget> blocked_focus_;
};

}  // namespace detail

HostedEditorSession::HostedEditorSession()
    : impl_(std::make_unique<detail::HostedEditorSessionImpl>()) {}
HostedEditorSession::~HostedEditorSession() = default;
bool HostedEditorSession::start(
    ::cardwirth::platform::AppRuntimeContext& context,
    const EditorLaunchOptions& options,
    const EditorHostOptions& host_options) {
    return impl_->start(context, options, host_options);
}
bool HostedEditorSession::tick(bool wait) { return impl_->tick(wait); }
bool HostedEditorSession::dispatch_native_event(const void* event) {
    return impl_->dispatch_native_event(event);
}
void HostedEditorSession::render_hosted() { impl_->render_hosted(); }
void HostedEditorSession::poll_background() { impl_->poll_background(); }
void HostedEditorSession::set_host_iteration(std::function<bool()> iteration) { impl_->set_host_iteration(std::move(iteration)); }
void HostedEditorSession::set_surface_transform(const all::CoordinateTransform& transform) { impl_->set_surface_transform(transform); }
void HostedEditorSession::resize_surface(const int width, const int height) { impl_->resize_surface(width, height); }
void HostedEditorSession::enter_background() { impl_->enter_background(); }
void HostedEditorSession::enter_foreground() { impl_->enter_foreground(); }
void HostedEditorSession::set_covered(bool covered) { impl_->set_covered(covered); }
void HostedEditorSession::memory_pressure() { impl_->memory_pressure(); }
void HostedEditorSession::renderer_reset() { impl_->renderer_reset(); }
void HostedEditorSession::set_input_blocked(const bool blocked) { impl_->set_input_blocked(blocked); }
bool HostedEditorSession::prepare_stop(bool returning_home) { return impl_->prepare_stop(returning_home); }
bool HostedEditorSession::prepare_stop_async(
    const bool returning_home, std::function<void(bool)> completed) {
    return impl_->prepare_stop_async(returning_home, std::move(completed));
}
bool HostedEditorSession::prepare_external_navigation() {
    return impl_->prepare_external_navigation();
}
bool HostedEditorSession::prepare_external_navigation_async(
    std::function<void(bool)> completed) {
    return impl_->prepare_external_navigation_async(std::move(completed));
}
void HostedEditorSession::reconcile_external_changes() {
    impl_->reconcile_external_changes();
}
void HostedEditorSession::request_stop() { impl_->request_stop(); }
void HostedEditorSession::constrain_to_host() { impl_->constrain_to_host(); }
bool HostedEditorSession::save_in_progress() const noexcept { return impl_->save_in_progress(); }
bool HostedEditorSession::stop_requested() const noexcept { return impl_->stop_requested(); }
bool HostedEditorSession::running() const noexcept { return impl_->running(); }
bool HostedEditorSession::finished() const noexcept { return impl_->finished(); }
int HostedEditorSession::exit_code() const noexcept { return impl_->exit_code(); }
std::string HostedEditorSession::error() const { return impl_->error(); }

EditorLaunchResult run_editor_embedded(
    ::cardwirth::platform::AppRuntimeContext& host_context,
    const EditorLaunchOptions& options) {
    HostedEditorSession session;
    EditorLaunchResult result;
    result.started = session.start(host_context, options, {});
    if (!result.started) {
        result.error = session.error();
        return result;
    }
    while (session.running()) session.tick(true);
    result.exit_code = session.exit_code();
    result.error = session.error();
    return result;
}

}  // namespace cardwirth::app

#else

namespace cardwirth::app {

bool editor_embedded_available() noexcept { return false; }

namespace detail { class HostedEditorSessionImpl {}; }

HostedEditorSession::HostedEditorSession()
    : impl_(std::make_unique<detail::HostedEditorSessionImpl>()) {}
HostedEditorSession::~HostedEditorSession() = default;
bool HostedEditorSession::start(::cardwirth::platform::AppRuntimeContext&,
                                const EditorLaunchOptions&,
                                const EditorHostOptions&) { return false; }
bool HostedEditorSession::tick(bool) { return false; }
bool HostedEditorSession::dispatch_native_event(const void*) { return false; }
void HostedEditorSession::render_hosted() {}
void HostedEditorSession::poll_background() {}
void HostedEditorSession::set_host_iteration(std::function<bool()>) {}
void HostedEditorSession::set_surface_transform(const all::CoordinateTransform&) {}
void HostedEditorSession::resize_surface(int, int) {}
void HostedEditorSession::enter_background() {}
void HostedEditorSession::enter_foreground() {}
void HostedEditorSession::set_covered(bool) {}
void HostedEditorSession::memory_pressure() {}
void HostedEditorSession::renderer_reset() {}
void HostedEditorSession::set_input_blocked(bool) {}
bool HostedEditorSession::prepare_stop(bool) { return false; }
bool HostedEditorSession::prepare_stop_async(bool, std::function<void(bool)>) { return false; }
bool HostedEditorSession::prepare_external_navigation() { return false; }
bool HostedEditorSession::prepare_external_navigation_async(std::function<void(bool)>) { return false; }
void HostedEditorSession::reconcile_external_changes() {}
void HostedEditorSession::request_stop() {}
void HostedEditorSession::constrain_to_host() {}
bool HostedEditorSession::save_in_progress() const noexcept { return false; }
bool HostedEditorSession::stop_requested() const noexcept { return false; }
bool HostedEditorSession::running() const noexcept { return false; }
bool HostedEditorSession::finished() const noexcept { return true; }
int HostedEditorSession::exit_code() const noexcept { return -1; }
std::string HostedEditorSession::error() const {
    return "XEditor_CはこのALLビルドに接続されていません (Unsupported)";
}

EditorLaunchResult run_editor_embedded(
    ::cardwirth::platform::AppRuntimeContext&,
    const EditorLaunchOptions&) {
    return EditorLaunchResult{
        false,
        -1,
        "XEditor_CはこのALLビルドに接続されていません (Unsupported)",
    };
}

}  // namespace cardwirth::app

#endif
