#pragma once

#include "capabilities.hpp"
#include "editor_clipboard.hpp"
#include "file_picker.hpp"
#include "lifecycle.hpp"
#include "services.hpp"
#include "text_input.hpp"

#include "platform/storage/app_runtime_context.hpp"
#include "platform/storage/app_paths.hpp"
#include "platform/storage/storage.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace cwxeditor::lib {

// 共通ホストコンテキストは、下記のEditor用セッションラッパーとは
// 分離する。ALLがこのコンテキストとStorageを所有し、Editor
// セッションは借用できるが、暗黙に所有したり終了させたりしない。
using HostRuntimeContext = ::cardwirth::platform::AppRuntimeContext;

// 共通の保存領域の実装はplatform名前空間に置く。
// Editor側ではこれらの別名を使い、具体的なバックエ
// ンドを提供するプラットフォームに依存しないようにする。
using Storage = ::cardwirth::platform::Storage;
using StoragePath = ::cardwirth::platform::StoragePath;
using AppPaths = ::cardwirth::platform::AppPaths;
using LogicalRoot = ::cardwirth::platform::LogicalRoot;

// Editorのサービス名は、個別の実装ではなく機能の境界を表す
// 。デスクトップのダイアログ、ブラウザの選択画面、モバイルのドキ
// ュメントプロバイダーはいずれもFilePickerを実装する。
using AudioService = AudioBackend;
using Clipboard = EditorClipboard;
using TextInput = TextInputService;

enum class RuntimeState : std::uint8_t {
    Booting,
    LoadingStorage,
    Ready,
    SavingStorage,
    StorageError,
    ShuttingDown,
    Shutdown,
};

// 保存領域の同期状態はライフサイクル状態と分ける。バックグラウンドで
// IndexedDBをフラッシュ中でもホストはReadyになり得るた
// め、Editorはこの違いをフロントエンドへ報告できる必要がある。
enum class StorageSyncState : std::uint8_t {
    NotStarted,
    Loading,
    Synchronized,
    Saving,
    Error,
};

// Webのフラッシュは非同期。「受理済みだが未完了」をtrueにまとめない。呼び
// 出し側はCompletedの場合だけ、ドキュメントの変更済み状態を解除できる。
enum class StoragePersistResult : std::uint8_t {
    Completed,
    Pending,
    Failed,
};

// セッションと設定の規則は単体版、ALL、すべてのOSで共有する。DWT
// 側はプロセスのグローバル変数を直接読んだり、ビルド時のプラットフォーム
// マクロでドット付きディレクトリ名を選んだりせず、これらの関数を使う。
[[nodiscard]] bool is_managed_user_data_session(const Services& services) noexcept;
[[nodiscard]] std::string_view editor_settings_directory_name(
    Platform platform, bool settings_disabled = false) noexcept;
[[nodiscard]] std::string_view editor_settings_directory_name(
    const Services& services, bool settings_disabled = false) noexcept;
[[nodiscard]] constexpr std::string_view editor_data_directory_name() noexcept {
    return "editor";
}
// 単体版XEditorは、アプリケーションが所有する書き込み可能なファイル
// を一つのポータブル用ディレクトリ配下にまとめる。デスクトップのポータブル
// 配置では実行ファイルのディレクトリ、バンドル/モバイルではアプリケーショ
// ンデータ、Webでは/userを使う。ホストやテストが明示した管理セッ
// ションまたはCARDWIRTHC_USER_ROOTの指定を優先する。
[[nodiscard]] std::filesystem::path editor_standalone_user_root(
    const Services& services,
    std::filesystem::path current_directory = {});

struct RuntimeValidation {
    bool ok{};
    std::vector<std::string> errors;
    std::vector<std::string> warnings;

    [[nodiscard]] explicit operator bool() const noexcept { return ok; }
};

class EditorRuntimeContext final {
public:
    struct StandaloneOptions {
        // app_rootはインストール先と素材のツリー。user_rootとstorage_root
        // は書き込み可能な状態の保存先。ネイティブ環境の書き込み先が空の場合は、実行フ
        // ァイルのディレクトリ配下のポータブルEditor用ディレクトリに解決する。
        std::filesystem::path app_root;
        std::filesystem::path user_root;
        std::filesystem::path storage_root;
        bool create_storage_root{true};
        bool use_platform_services{true};
        bool start_ready{true};
        Capabilities capabilities{};
        std::function<Services()> services_factory;
    };

    // 借用コンテキストはServicesとStorageの寿命を変
    // えない。ALLがプロセス全体のサービスを所有し、Editor
    // へ注入する場合に、通常ホストはこのコンストラクターを使う。
    EditorRuntimeContext(Services& services,
                         Storage& storage,
                         AppPaths paths,
                         Capabilities capabilities = Capabilities{});

    // 所有コンテキストは両方の依存先を保持する。Servicesは共有バックエン
    // ドハンドルを持ち、Storageは共有ポインターで保持するため、make_native_storage
    // が返すunique_ptrを直接受け取れる。
    EditorRuntimeContext(std::shared_ptr<Services> services,
                         std::shared_ptr<Storage> storage,
                         AppPaths paths,
                         Capabilities capabilities = Capabilities{});

    // 組み込みEditorセッションは共通ホストコンテキストを
    // 借用する。Storageとプロセス全体のライフサイクルは
    // ホストが所有し、このラッパーはEditorのサービス、機
    // 能の利用規則、製品セッションの同期状態だけを追加する。
    EditorRuntimeContext(HostRuntimeContext& host,
                         Services& services,
                         Capabilities capabilities = Capabilities{});

    EditorRuntimeContext(const EditorRuntimeContext&) = delete;
    EditorRuntimeContext& operator=(const EditorRuntimeContext&) = delete;
    EditorRuntimeContext(EditorRuntimeContext&& other) noexcept;
    EditorRuntimeContext& operator=(EditorRuntimeContext&& other) noexcept;
    ~EditorRuntimeContext() = default;

    [[nodiscard]] static EditorRuntimeContext borrow(Services& services,
                                                      Storage& storage,
                                                      AppPaths paths,
                                                      Capabilities capabilities = Capabilities{});

    [[nodiscard]] static EditorRuntimeContext borrow(HostRuntimeContext& host,
                                                     Services& services,
                                                     Capabilities capabilities = Capabilities{});

    [[nodiscard]] static EditorRuntimeContext own(
        Services services,
        std::unique_ptr<Storage> storage,
        AppPaths paths,
        Capabilities capabilities = Capabilities{});

    [[nodiscard]] static EditorRuntimeContext own(
        std::shared_ptr<Services> services,
        std::shared_ptr<Storage> storage,
        AppPaths paths,
        Capabilities capabilities = Capabilities{});

    // 通常の単体版Editorの実行環境を構築する。NativeStorage
    // とプラットフォームのサービスファクトリーはここで選択し、Editor
    // 本体は上記の抽象契約だけを扱う。ファクトリーはネイティブの
    // ルート設定を同期実行し、既定でReady状態から開始する。
    [[nodiscard]] static EditorRuntimeContext standalone();
    [[nodiscard]] static EditorRuntimeContext standalone(StandaloneOptions options);

    [[nodiscard]] HostRuntimeContext* host_context() noexcept {
        return host_context_;
    }
    [[nodiscard]] const HostRuntimeContext* host_context() const noexcept {
        return host_context_;
    }

    [[nodiscard]] Services* services_ptr() noexcept { return services_; }
    [[nodiscard]] const Services* services_ptr() const noexcept { return services_; }
    [[nodiscard]] Services& services() noexcept { return *services_; }
    [[nodiscard]] const Services& services() const noexcept { return *services_; }

    [[nodiscard]] Storage* storage_ptr() noexcept { return storage_; }
    [[nodiscard]] const Storage* storage_ptr() const noexcept { return storage_; }
    [[nodiscard]] Storage& storage() noexcept { return *storage_; }
    [[nodiscard]] const Storage& storage() const noexcept { return *storage_; }

    [[nodiscard]] AppPaths& paths() noexcept { return paths_; }
    [[nodiscard]] const AppPaths& paths() const noexcept { return paths_; }
    // Storageのマウントを所有する同じAppPathsインスタンス
    // で、論理的なアプリケーションルートを解決する。呼び出し側は作業ディ
    // レクトリからパスを組み立て直さず、この結果をStorageに渡す。
    [[nodiscard]] StoragePath storage_path(
        LogicalRoot root, StoragePath relative = {}) const;
    // Storageを通じて書き込み可能なユーザー領域の構
    // 成を作る。単体版の起動時に使うほか、組み込みホスト
    // も一時シナリオやバックアップの作成前に利用できる。
    [[nodiscard]] bool ensure_storage_layout(std::string* error = nullptr);
    [[nodiscard]] Capabilities& capabilities() noexcept { return capabilities_; }
    [[nodiscard]] const Capabilities& capabilities() const noexcept {
        return capabilities_;
    }
    [[nodiscard]] LifecycleService& lifecycle() noexcept { return *lifecycle_; }
    [[nodiscard]] const LifecycleService& lifecycle() const noexcept {
        return *lifecycle_;
    }

    // 各アクセサーは明示的に借用または所有したサービスを優
    // 先し、なければServicesの対応する項目を使う。
    // フロントエンドはサービス一式を作り直さず、Clipboard
    // やFilePickerだけを置き換えられる。
    [[nodiscard]] AudioService* audio() noexcept;
    [[nodiscard]] const AudioService* audio() const noexcept;
    [[nodiscard]] FilePicker* file_picker() noexcept;
    [[nodiscard]] const FilePicker* file_picker() const noexcept;
    [[nodiscard]] Clipboard* clipboard() noexcept;
    [[nodiscard]] const Clipboard* clipboard() const noexcept;
    [[nodiscard]] TextInput* text_input() noexcept;
    [[nodiscard]] const TextInput* text_input() const noexcept;

    void own_audio(std::shared_ptr<AudioService> service);
    void borrow_audio(AudioService& service) noexcept;
    void own_file_picker(std::shared_ptr<FilePicker> service);
    void borrow_file_picker(FilePicker& service) noexcept;
    void own_clipboard(std::shared_ptr<Clipboard> service);
    void initialize_session_services();
    void borrow_clipboard(Clipboard& service) noexcept;
    void own_text_input(std::shared_ptr<TextInput> service);
    void borrow_text_input(TextInput& service) noexcept;

    [[nodiscard]] RuntimeValidation validate() const;
    [[nodiscard]] bool valid() const { return validate().ok; }

    [[nodiscard]] RuntimeState state() const noexcept {
        return state_.load(std::memory_order_acquire);
    }
    [[nodiscard]] StorageSyncState sync_state() const noexcept {
        return sync_state_.load(std::memory_order_acquire);
    }
    [[nodiscard]] std::string last_error() const;
    [[nodiscard]] bool ready() const noexcept { return state() == RuntimeState::Ready; }
    [[nodiscard]] bool storage_ready() const noexcept {
        return sync_state() == StorageSyncState::Synchronized;
    }
    [[nodiscard]] bool shutdown_requested() const noexcept {
        const auto current = state();
        return current == RuntimeState::ShuttingDown || current == RuntimeState::Shutdown;
    }
    [[nodiscard]] bool is_shutdown() const noexcept {
        return state() == RuntimeState::Shutdown;
    }

    // ライフサイクルの遷移は、可能な限り繰り返しても同じ結果になるように
    // する。falseは終了後または不適合な状態からの遷移要求を表す。呼
    // び出し側はstate()/last_error()で確認できる。
    bool begin_storage_load();
    bool complete_storage_load(bool success, std::string error = {});
    bool begin_storage_save();
    bool complete_storage_save(bool success, std::string error = {});
    bool begin_sync() { return begin_storage_load(); }
    bool complete_sync(bool success, std::string error = {}) {
        return complete_storage_load(success, std::move(error));
    }
    void mark_storage_ready();
    void mark_storage_error(std::string error);
    // 非同期バックエンド（特にIDBFS）の処理を進め、その状態をRuntimeState/StorageSyncState
    // に反映する。
    bool poll_storage();
    // 現在のファイルシステムのリビジョンを永続化先へフラッシュする。ネイティブの保存
    // 領域では同期的に完了し、WebStorageではpoll()を通じて完了する。
    bool persist_storage();
    [[nodiscard]] StoragePersistResult persist_storage_result();
    bool retry_storage();
    void request_shutdown() noexcept;
    void shutdown() noexcept;

private:
    void set_error(std::string error);
    void clear_error();
    void move_from(EditorRuntimeContext&& other) noexcept;

    std::shared_ptr<Services> owned_services_;
    Services* services_{};
    std::shared_ptr<Storage> owned_storage_;
    Storage* storage_{};
    HostRuntimeContext* host_context_{};
    AppPaths paths_{};
    Capabilities capabilities_{};
    std::shared_ptr<LifecycleService> lifecycle_{std::make_shared<LifecycleService>()};

    std::shared_ptr<AudioService> owned_audio_;
    AudioService* borrowed_audio_{};
    std::shared_ptr<FilePicker> owned_file_picker_;
    FilePicker* borrowed_file_picker_{};
    std::shared_ptr<Clipboard> owned_clipboard_;
    Clipboard* borrowed_clipboard_{};
    std::shared_ptr<TextInput> owned_text_input_;
    TextInput* borrowed_text_input_{};

    std::atomic<RuntimeState> state_{RuntimeState::Booting};
    std::atomic<StorageSyncState> sync_state_{StorageSyncState::NotStarted};
    mutable std::mutex error_mutex_;
    std::string last_error_;
    int persistence_request_{};
};

using RuntimeContext = EditorRuntimeContext;

[[nodiscard]] EditorRuntimeContext make_standalone_runtime_context(
    EditorRuntimeContext::StandaloneOptions options = {});

[[nodiscard]] EditorRuntimeContext make_standalone_editor_runtime(
    EditorRuntimeContext::StandaloneOptions options = {});

}  // namespace cwxeditor::lib
