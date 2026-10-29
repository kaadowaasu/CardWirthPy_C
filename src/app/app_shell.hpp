#pragma once

#include "process.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace cardwirth::app {

struct UserDataLayout {
    std::filesystem::path root;
    std::filesystem::path data;
    std::filesystem::path scenario;
    std::filesystem::path yado;
    std::filesystem::path config;
    std::filesystem::path backup;
    std::filesystem::path database;
    std::filesystem::path temp;
    std::filesystem::path cache;
    std::filesystem::path trash;

    static UserDataLayout from_root(std::filesystem::path value);
    // ユーザーデータのルートを作成する。アプリケーションのルートが指定された
    // 場合は、 読み取り専用の配布データから互換用のDataツリーも更新す
    // る。 EngineはUserData基準でData/Skinを解決す
    // るが、AppPathsの正式な資産ルートは<app>/Dataとする
    // 。 各サンドボックスで同じ管理ファイル一覧を記録し、アプリ更新時には
    // 未変更の配布資産だけを 更新して、ユーザーが変更したファイルは残す。
    bool ensure(std::string& error,
                const std::filesystem::path& application_root = {},
                bool create_auxiliary_roots = true) const;
    [[nodiscard]] std::vector<std::filesystem::path> missing() const;
};

enum class Page {
    Home,
    CardWirthPy,
    XEditor,
    DataExplorer,
};

// 保留中のEditorへの遷移要求は、保存処理やホストを閉じる途中で
// も保持する。 それらが完了する前に消費すると、遷移要求が失われる。
enum class DeferredEditorRouteAction { Ignore, Wait, Process };
[[nodiscard]] constexpr DeferredEditorRouteAction deferred_editor_route_action(
    const bool requested, const bool editor_page_active, const bool editor_running,
    const bool save_in_progress, const bool host_close_idle) noexcept {
    if (!requested || !editor_page_active || !editor_running)
        return DeferredEditorRouteAction::Ignore;
    if (save_in_progress || !host_close_idle)
        return DeferredEditorRouteAction::Wait;
    return DeferredEditorRouteAction::Process;
}

// デスクトップ版ALLは、Engineを従来の子プロセスまたは同一プロセ
// ス内で動かせる。 既存のWindows配布版のプロセス分離と障害時の
// 隔離を維持するため、 既定値はExternalProcessとする。
enum class EngineBackend {
    ExternalProcess,
    Embedded,
};

class AppShell {
public:
    explicit AppShell(UserDataLayout user_data);

    [[nodiscard]] const UserDataLayout& user_data() const noexcept { return user_data_; }
    [[nodiscard]] Page page() const noexcept { return page_; }
    [[nodiscard]] Page back_page() const noexcept {
        return has_suspended_parent_ ? suspended_parent_
            : navigation_.empty() ? Page::Home : navigation_.back();
    }
    [[nodiscard]] bool suspended() const noexcept { return suspended_; }
    [[nodiscard]] const std::string& status() const noexcept { return status_; }
    [[nodiscard]] EngineBackend engine_backend() const noexcept { return engine_backend_; }

    void set_initial_page(Page page);
    void set_engine_backend(EngineBackend backend) noexcept { engine_backend_ = backend; }
    void navigate(Page page);
    // 親Editorの状態を保持したまま子ページを開く。 go_back()
    // で子を閉じるまで、親は中断状態とする。
    void open_embedded_child(Page page);
    void go_back();
    void begin_child(Page parent_page);
    void end_child(int exit_code);
    void set_status(std::string status) { status_ = std::move(status); }

private:
    UserDataLayout user_data_;
    Page page_{Page::Home};
    std::vector<Page> navigation_{};
    Page suspended_parent_{Page::Home};
    bool has_suspended_parent_{false};
    bool suspended_{false};
    EngineBackend engine_backend_{EngineBackend::ExternalProcess};
    std::string status_;
};

}  // namespace cardwirth::app
