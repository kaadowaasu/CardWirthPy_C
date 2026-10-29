#pragma once

#include "platform/storage/app_runtime_context.hpp"

#include <filesystem>
#include <functional>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace cardwirth::app {

struct EditorLaunchOptions {
    std::filesystem::path executable_path{};
    std::filesystem::path application_directory{};
};

struct EditorLaunchResult {
    bool started{};
    int exit_code{-1};
    std::string error;
};

struct EditorHostBounds {
    int x{};
    int y{};
    int width{1024};
    int height{720};
};

struct EditorHostOptions {
    std::uintptr_t native_parent{};
    std::uintptr_t native_sdl_window{};
    std::uintptr_t native_renderer{};
    bool keep_top_bar{true};
    bool allow_internal_move{true};
    EditorHostBounds initial_bounds{};
    std::function<bool(const std::string&, const std::vector<std::string>&,
                       const std::filesystem::path&)> execute_engine;
    std::function<void()> request_home;
    std::function<void()> request_engine;
    std::function<void()> request_data;
    // ALLのData Explorerページで、Editorの素材フォルダーを表示する。
    std::function<bool(const std::filesystem::path&)> request_data_directory;
    bool restore_previous_scenario{true};
};

namespace all { struct CoordinateTransform; }
namespace detail { class HostedEditorSessionImpl; }

// CardWirthPy_C_All用の、処理をブロックしないEditorライフサイク
// ル。 すべてのOSで同じEditorとダイアログを共有SDLウィンドウへ合成する。
// 外側のイベント・描画ループはALLが管理し、入れ子のモーダル待機用に更新処理を渡す。
class HostedEditorSession final {
public:
    HostedEditorSession();
    ~HostedEditorSession();
    HostedEditorSession(const HostedEditorSession&) = delete;
    HostedEditorSession& operator=(const HostedEditorSession&) = delete;

    bool start(::cardwirth::platform::AppRuntimeContext& host_context,
               const EditorLaunchOptions& options,
               const EditorHostOptions& host_options);
    // 最初の更新でシナリオを復元する。その時点でホストはEditorページを
    // 選択済みで、 起動時のダイアログへ入力を渡して描画できる必要がある。
    bool tick(bool wait = false);
    bool dispatch_native_event(const void* event);
    void render_hosted();
    void poll_background();
    void set_surface_transform(const all::CoordinateTransform& transform);
    void resize_surface(int width, int height);
    void enter_background();
    void enter_foreground();
    void set_covered(bool covered);
    void memory_pressure();
    void renderer_reset();
    // ALL側の重ね表示はEditorのダイアログを開かず、この
    // 独立した入力制限を使う。 Editorのメイン画面と入力
    // を所有するダイアログの両方を制限し、元の状態は保持する。
    void set_input_blocked(bool blocked);
    // start()より前に設定でき、セッションの再起動後も保持される。
    void set_host_iteration(std::function<bool()> iteration);
    // セッションを破棄せずに、Editorの保存・キャンセル確認を行う。
    [[nodiscard]] bool prepare_stop(bool returning_home = false);
    // ブラウザー内のページ遷移では処理をブロックしないダイアログを使い
    // 、 保存・破棄・キャンセルの選択結果が届いてから遷移を続ける。
    [[nodiscard]] bool prepare_stop_async(
        bool returning_home, std::function<void(bool)> completed);
    // Explorerも同じホストのStorageへ書き込むため、このページの開始と
    // 終了を通知し、 保持中のEditorが後から古いモデルを保存しないようにする。
    [[nodiscard]] bool prepare_external_navigation();
    [[nodiscard]] bool prepare_external_navigation_async(
        std::function<void(bool)> completed);
    void reconcile_external_changes();
    void request_stop();
    void constrain_to_host();
    [[nodiscard]] bool save_in_progress() const noexcept;
    // 非同期の停止処理と、保存失敗によって取り消された停止を区別する。
    [[nodiscard]] bool stop_requested() const noexcept;
    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] bool finished() const noexcept;
    [[nodiscard]] int exit_code() const noexcept;
    [[nodiscard]] std::string error() const;

private:
    std::unique_ptr<detail::HostedEditorSessionImpl> impl_;
};

// ALLホストのStorageを借りて、Editorの1セッシ
// ョンを同期的に実行する。 コンテキストとSDL・TTF・音声
// の寿命はホストが管理する。 Editorがリンクされていな
// い製品では、ダミーページを開かず明示的な未対応結果を返す。
[[nodiscard]] EditorLaunchResult run_editor_embedded(
    ::cardwirth::platform::AppRuntimeContext& host_context,
    const EditorLaunchOptions& options = {});

[[nodiscard]] bool editor_embedded_available() noexcept;

}  // namespace cardwirth::app
