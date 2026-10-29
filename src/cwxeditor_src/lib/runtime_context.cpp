#include "runtime_context.hpp"

#include "web_storage.hpp"
#include "platform/storage/storage.hpp"

#include <array>
#include <concepts>
#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

#ifdef __EMSCRIPTEN__
extern "C" int cardwirth_persist_begin();
extern "C" int cardwirth_persist_poll(int);
#endif

namespace cwxeditor::lib {
namespace {

template <typename C>
[[nodiscard]] bool supports_external_process(const C& capabilities) noexcept {
    if constexpr (requires { {
                      capabilities.supports_external_process()
                  } -> std::convertible_to<bool>; }) {
        return static_cast<bool>(capabilities.supports_external_process());
    } else if constexpr (requires { {
                             capabilities.external_process
                         } -> std::convertible_to<bool>; }) {
        return static_cast<bool>(capabilities.external_process);
    } else {
        return false;
    }
}

template <typename C>
[[nodiscard]] bool supports_audio(const C& capabilities) noexcept {
    if constexpr (requires { {
                      capabilities.supports_audio()
                  } -> std::convertible_to<bool>; }) {
        return static_cast<bool>(capabilities.supports_audio());
    } else if constexpr (requires { {
                             capabilities.audio
                         } -> std::convertible_to<bool>; }) {
        return static_cast<bool>(capabilities.audio);
    } else {
        return false;
    }
}

template <typename C>
[[nodiscard]] bool supports_file_picker(const C& capabilities) noexcept {
    if constexpr (requires { {
                      capabilities.supports_file_picker()
                  } -> std::convertible_to<bool>; }) {
        return static_cast<bool>(capabilities.supports_file_picker());
    } else if constexpr (requires { {
                             capabilities.file_picker
                         } -> std::convertible_to<bool>; }) {
        return static_cast<bool>(capabilities.file_picker);
    } else {
        return true;
    }
}

template <typename C>
[[nodiscard]] bool supports_clipboard(const C& capabilities) noexcept {
    if constexpr (requires { {
                      capabilities.supports_clipboard()
                  } -> std::convertible_to<bool>; }) {
        return static_cast<bool>(capabilities.supports_clipboard());
    } else if constexpr (requires { {
                             capabilities.clipboard
                         } -> std::convertible_to<bool>; }) {
        return static_cast<bool>(capabilities.clipboard);
    } else {
        return true;
    }
}

template <typename C>
[[nodiscard]] bool supports_text_input(const C& capabilities) noexcept {
    if constexpr (requires { {
                      capabilities.supports_text_input()
                  } -> std::convertible_to<bool>; }) {
        return static_cast<bool>(capabilities.supports_text_input());
    } else if constexpr (requires { {
                             capabilities.text_input
                         } -> std::convertible_to<bool>; }) {
        return static_cast<bool>(capabilities.text_input);
    } else {
        return true;
    }
}

template <typename C>
[[nodiscard]] bool supports_persistent_storage(const C& capabilities) noexcept {
    if constexpr (requires { {
                      capabilities.supports_persistent_storage()
                  } -> std::convertible_to<bool>; }) {
        return static_cast<bool>(capabilities.supports_persistent_storage());
    } else if constexpr (requires { {
                             capabilities.persistent_storage
                         } -> std::convertible_to<bool>; }) {
        return static_cast<bool>(capabilities.persistent_storage);
    } else {
        return true;
    }
}

template <typename C>
[[nodiscard]] bool supports_drag_drop(const C& capabilities) noexcept {
    if constexpr (requires {
                      { capabilities.supports_drag_drop() } -> std::convertible_to<bool>;
                  }) {
        return static_cast<bool>(capabilities.supports_drag_drop());
    } else if constexpr (requires {
                             { capabilities.drag_drop } -> std::convertible_to<bool>;
                         }) {
        return static_cast<bool>(capabilities.drag_drop);
    } else {
        return false;
    }
}

template <typename C>
[[nodiscard]] bool supports_shell(const C& capabilities) noexcept {
    if constexpr (requires {
                      { capabilities.supports_shell() } -> std::convertible_to<bool>;
                  }) {
        return static_cast<bool>(capabilities.supports_shell());
    } else if constexpr (requires {
                             { capabilities.shell } -> std::convertible_to<bool>;
                         }) {
        return static_cast<bool>(capabilities.shell);
    } else {
        return false;
    }
}

template <typename C>
[[nodiscard]] bool supports_multiple_windows(const C& capabilities) noexcept {
    if constexpr (requires {
                      { capabilities.supports_multiple_windows() } -> std::convertible_to<bool>;
                  }) {
        return static_cast<bool>(capabilities.supports_multiple_windows());
    } else if constexpr (requires {
                             { capabilities.multiple_windows } -> std::convertible_to<bool>;
                         }) {
        return static_cast<bool>(capabilities.multiple_windows);
    } else {
        return false;
    }
}

template <typename C>
[[nodiscard]] bool supports_dispatcher(const C& capabilities) noexcept {
    if constexpr (requires { {
                      capabilities.supports_dispatcher()
                  } -> std::convertible_to<bool>; }) {
        return static_cast<bool>(capabilities.supports_dispatcher());
    } else if constexpr (requires { {
                             capabilities.dispatcher
                         } -> std::convertible_to<bool>; }) {
        return static_cast<bool>(capabilities.dispatcher);
    } else {
        return true;
    }
}

template <typename C>
[[nodiscard]] bool supports_tasks(const C& capabilities) noexcept {
    if constexpr (requires { {
                      capabilities.supports_tasks()
                  } -> std::convertible_to<bool>; }) {
        return static_cast<bool>(capabilities.supports_tasks());
    } else if constexpr (requires { {
                             capabilities.tasks
                         } -> std::convertible_to<bool>; }) {
        return static_cast<bool>(capabilities.tasks);
    } else {
        return true;
    }
}

[[nodiscard]] Capabilities constrain_capabilities(Capabilities available,
                                                  const Capabilities& requested) noexcept {
    available.audio = available.audio && requested.audio;
    available.file_picker = available.file_picker && requested.file_picker;
    available.clipboard = available.clipboard && requested.clipboard;
    available.text_input = available.text_input && requested.text_input;
    available.drag_drop = available.drag_drop && requested.drag_drop;
    available.dispatcher = available.dispatcher && requested.dispatcher;
    available.tasks = available.tasks && requested.tasks;
    available.external_process = available.external_process && requested.external_process;
    available.shell = available.shell && requested.shell;
    available.persistent_storage = available.persistent_storage && requested.persistent_storage;
    available.multiple_windows = available.multiple_windows && requested.multiple_windows;
    available.context_menu = available.context_menu && requested.context_menu;
    available.native_file_picker = available.native_file_picker && requested.native_file_picker;
    available.browser_file_picker = available.browser_file_picker && requested.browser_file_picker;
    available.clipboard_custom_format =
        available.clipboard_custom_format && requested.clipboard_custom_format;
    available.clipboard_internal_fallback =
        available.clipboard_internal_fallback && requested.clipboard_internal_fallback;
    available.touch_navigation = available.touch_navigation && requested.touch_navigation;
    available.long_press_context_menu =
        available.long_press_context_menu && requested.long_press_context_menu;
    available.long_press_drag_drop =
        available.long_press_drag_drop && requested.long_press_drag_drop;
    available.single_screen_navigation =
        available.single_screen_navigation && requested.single_screen_navigation;
    return available;
}

[[nodiscard]] Capabilities effective_capabilities(const Services* services,
                                                  Capabilities requested) noexcept {
    if (services == nullptr) return Capabilities::headless();
    const Platform platform = services->gui ? services->gui->platform()
                                            : Platform::Automatic;
    return constrain_capabilities(probe_capabilities(*services, platform), requested);
}

[[nodiscard]] std::optional<std::string> environment_value(
    const Services* services, const std::string_view name) {
    if (name.empty()) return std::nullopt;
    // 指定されたアダプターは、値がある場合もない場合も管理する。 別のプロセ
    // ス環境の実装へ切り替えると文字コードの方針が分かれるため、代替しない。
    if (services != nullptr && services->system) return services->system->environment(name);
    const std::string key(name);
    const char* value = std::getenv(key.c_str());
    if (value == nullptr || *value == '\0') return std::nullopt;
    return std::string(value);
}

[[nodiscard]] std::filesystem::path environment_path(
    const Services* services, const std::string_view name) {
    const auto value = environment_value(services, name);
    return value ? std::filesystem::u8path(*value) : std::filesystem::path{};
}

[[nodiscard]] bool same_managed_path(const Services& services,
                                     const std::filesystem::path& left,
                                     const std::filesystem::path& right) {
    if (services.filesystem)
        return services.filesystem->filenames_equal(left, right);
    return left.lexically_normal() == right.lexically_normal();
}

[[nodiscard]] std::optional<std::filesystem::path> managed_user_root(
    const Services& services) {
    if (!is_managed_user_data_session(services)) return std::nullopt;
    auto root = environment_path(&services, "CARDWIRTHC_USER_ROOT");
    if (root.empty() || !root.is_absolute()) {
        throw std::runtime_error(
            "managed Editor session has no absolute CARDWIRTHC_USER_ROOT");
    }
    return root.lexically_normal();
}

[[nodiscard]] std::filesystem::path writable_user_root(
    const Services& services,
    const std::filesystem::path& app_root) {
    return editor_standalone_user_root(services, app_root);
}

[[nodiscard]] std::filesystem::path editor_document_root(const AppPaths& paths) {
    const auto& user = paths.user_root;
    const auto& scenario = paths.scenario_root;
    // 組み込みの ALL は、兄弟の UserData/editor と UserData/Scenario
    // をルートにする。 単独版は専用状態を <exe>/editor
    // に置き、文書選択は実行ファイルのディレクトリから始める。 OS
    // 全体の値を再取得せず、解決済みのパスの構成から両方を求める。
    if (user.filename() == std::filesystem::path(editor_data_directory_name()) &&
        scenario.parent_path() == user.parent_path())
        return user.parent_path();
    return paths.app_root;
}

[[nodiscard]] AppPaths default_native_paths(const Services& services,
                                             std::filesystem::path app_root,
                                             std::filesystem::path user_root) {
    std::error_code error;
    if (app_root.empty() && services.system &&
        services.system->platform() != Platform::Headless) {
        const auto executable = services.system->executable_path();
        if (!executable.empty()) app_root = executable.parent_path();
    }
    if (app_root.empty()) app_root = std::filesystem::current_path(error);
    if (app_root.empty()) app_root = ".";
    if (const auto managed_root = managed_user_root(services)) {
        const auto editor_root = (*managed_root / editor_data_directory_name())
                                     .lexically_normal();
        if (!user_root.empty() &&
             (!user_root.is_absolute() ||
             (!same_managed_path(services, user_root, *managed_root) &&
              !same_managed_path(services, user_root, editor_root)))) {
            throw std::runtime_error(
                "managed Editor user root does not match CARDWIRTHC_USER_ROOT");
        }
        auto paths = AppPaths::from_roots(std::move(app_root), editor_root);
        paths.scenario_root = *managed_root / "Scenario";
        return paths;
    }
    if (user_root.empty()) user_root = writable_user_root(services, app_root);
    return AppPaths::from_roots(std::move(app_root), std::move(user_root));
}

[[nodiscard]] std::string storage_error(const Storage& storage,
                                         std::string_view operation,
                                         const StoragePath& path) {
    std::ostringstream message;
    message << operation << " failed for " << path.generic_string();
    const auto diagnostic = storage.last_error();
    if (!diagnostic.empty()) message << ": " << diagnostic;
    return message.str();
}

}  // namespace

bool is_managed_user_data_session(const Services& services) noexcept {
    try {
        const auto value = environment_value(
            &services, "CARDWIRTHC_MANAGED_USERDATA_SESSION");
        return value && *value == "1";
    } catch (...) {
        return false;
    }
}

std::string_view editor_settings_directory_name(
    const Platform platform, const bool settings_disabled) noexcept {
    const bool windows = platform == Platform::Windows;
    if (settings_disabled)
        return windows ? "cwxeditor_cws_no_settings"
                       : ".cwxeditor_cws_no_settings";
    return windows ? "cwxeditor_cws" : ".cwxeditor_cws";
}

std::string_view editor_settings_directory_name(
    const Services& services, const bool settings_disabled) noexcept {
    const auto platform = services.system ? services.system->platform()
                                          : Platform::Automatic;
    return editor_settings_directory_name(platform, settings_disabled);
}

std::filesystem::path editor_standalone_user_root(
    const Services& services, std::filesystem::path current_directory) {
    if (const auto managed_root = managed_user_root(services))
        return (*managed_root / editor_data_directory_name()).lexically_normal();
    if (auto override_root = environment_path(&services, "CARDWIRTHC_USER_ROOT");
        !override_root.empty()) {
        std::error_code error;
        if (!override_root.is_absolute())
            override_root = std::filesystem::absolute(override_root, error);
        if (error || override_root.empty())
            throw std::runtime_error("CARDWIRTHC_USER_ROOT is invalid");
        return (override_root.lexically_normal() /
                editor_data_directory_name()).lexically_normal();
    }

    if (services.system) {
        const auto platform = services.system->platform();
        if (platform == Platform::MacOS || platform == Platform::IOS ||
            platform == Platform::Android) {
            // バンドル形式・モバイルでは、現在の作業ディレクトリはアプリ所有の書き
            // 込み可能な場所とは限らない。 共通の System は ApplicationData
            // を SDL_GetPrefPath に対応させ、 cwxeditor_main
            // が選ぶ起動初期の診断保存先と揃える。
            const auto application_data = services.system->special_directory(
                SpecialDirectory::ApplicationData);
            if (application_data.empty())
                throw std::runtime_error("XEditor application data directory is unavailable");
            return (application_data / editor_data_directory_name()).lexically_normal();
        }
    }

    std::error_code error;
    if (current_directory.empty())
        current_directory = std::filesystem::current_path(error);
    else if (!current_directory.is_absolute())
        current_directory = std::filesystem::absolute(current_directory, error);
    if (error || current_directory.empty())
        throw std::runtime_error("XEditor current directory is unavailable");
    return (current_directory.lexically_normal() /
            editor_data_directory_name()).lexically_normal();
}

EditorRuntimeContext::EditorRuntimeContext(Services& services,
                                           Storage& storage,
                                           AppPaths paths,
                                           Capabilities capabilities)
    : services_(&services),
      storage_(&storage),
      paths_(std::move(paths)),
      capabilities_(effective_capabilities(services_, std::move(capabilities))) {
    if (services_->file_picker)
        services_->file_picker->set_document_root(editor_document_root(paths_));
}

EditorRuntimeContext::EditorRuntimeContext(std::shared_ptr<Services> services,
                                           std::shared_ptr<Storage> storage,
                                           AppPaths paths,
                                           Capabilities capabilities)
    : owned_services_(std::move(services)),
      services_(owned_services_.get()),
      owned_storage_(std::move(storage)),
      storage_(owned_storage_.get()),
      paths_(std::move(paths)),
      capabilities_(effective_capabilities(services_, std::move(capabilities))) {
    if (services_ && services_->file_picker)
        services_->file_picker->set_document_root(editor_document_root(paths_));
}

EditorRuntimeContext::EditorRuntimeContext(EditorRuntimeContext&& other) noexcept {
    move_from(std::move(other));
}

EditorRuntimeContext& EditorRuntimeContext::operator=(EditorRuntimeContext&& other) noexcept {
    if (this != &other) move_from(std::move(other));
    return *this;
}

void EditorRuntimeContext::move_from(EditorRuntimeContext&& other) noexcept {
    owned_services_ = std::move(other.owned_services_);
    services_ = owned_services_ ? owned_services_.get() : other.services_;

    owned_storage_ = std::move(other.owned_storage_);
    storage_ = owned_storage_ ? owned_storage_.get() : other.storage_;
    host_context_ = other.host_context_;

    paths_ = std::move(other.paths_);
    capabilities_ = std::move(other.capabilities_);
    lifecycle_ = std::move(other.lifecycle_);
    if (!lifecycle_) lifecycle_ = std::make_shared<LifecycleService>();

    owned_audio_ = std::move(other.owned_audio_);
    borrowed_audio_ = owned_audio_ ? nullptr : other.borrowed_audio_;
    owned_file_picker_ = std::move(other.owned_file_picker_);
    borrowed_file_picker_ = owned_file_picker_ ? nullptr : other.borrowed_file_picker_;
    owned_clipboard_ = std::move(other.owned_clipboard_);
    borrowed_clipboard_ = owned_clipboard_ ? nullptr : other.borrowed_clipboard_;
    owned_text_input_ = std::move(other.owned_text_input_);
    borrowed_text_input_ = owned_text_input_ ? nullptr : other.borrowed_text_input_;

    state_.store(other.state_.load(std::memory_order_acquire), std::memory_order_release);
    sync_state_.store(other.sync_state_.load(std::memory_order_acquire),
                      std::memory_order_release);
    last_error_ = std::move(other.last_error_);
    persistence_request_ = std::exchange(other.persistence_request_, 0);

    other.services_ = nullptr;
    other.storage_ = nullptr;
    other.host_context_ = nullptr;
    other.borrowed_audio_ = nullptr;
    other.borrowed_file_picker_ = nullptr;
    other.borrowed_clipboard_ = nullptr;
    other.borrowed_text_input_ = nullptr;
    other.state_.store(RuntimeState::Shutdown, std::memory_order_release);
    other.sync_state_.store(StorageSyncState::Error, std::memory_order_release);
}

EditorRuntimeContext EditorRuntimeContext::borrow(Services& services,
                                                  Storage& storage,
                                                  AppPaths paths,
                                                  Capabilities capabilities) {
    return EditorRuntimeContext(services, storage, std::move(paths),
                                std::move(capabilities));
}

EditorRuntimeContext EditorRuntimeContext::borrow(HostRuntimeContext& host,
                                                 Services& services,
                                                 Capabilities capabilities) {
    return EditorRuntimeContext(host, services, std::move(capabilities));
}

EditorRuntimeContext::EditorRuntimeContext(HostRuntimeContext& host,
                                           Services& services,
                                           Capabilities capabilities)
    : EditorRuntimeContext(services, host.storage_ref(), host.paths,
                           std::move(capabilities)) {
    host_context_ = &host;
}

EditorRuntimeContext EditorRuntimeContext::own(Services services,
                                               std::unique_ptr<Storage> storage,
                                               AppPaths paths,
                                               Capabilities capabilities) {
    auto services_owner = std::make_shared<Services>(std::move(services));
    std::shared_ptr<Storage> storage_owner(std::move(storage));
    return EditorRuntimeContext(std::move(services_owner), std::move(storage_owner),
                               std::move(paths), std::move(capabilities));
}

EditorRuntimeContext EditorRuntimeContext::own(std::shared_ptr<Services> services,
                                               std::shared_ptr<Storage> storage,
                                               AppPaths paths,
                                               Capabilities capabilities) {
    return EditorRuntimeContext(std::move(services), std::move(storage),
                                std::move(paths), std::move(capabilities));
}

EditorRuntimeContext EditorRuntimeContext::standalone() {
    return standalone(StandaloneOptions{});
}

EditorRuntimeContext EditorRuntimeContext::standalone(StandaloneOptions options) {
    Services services;
    if (options.services_factory) {
        services = options.services_factory();
    } else if (options.use_platform_services) {
        services = make_platform_services();
    } else {
        services = make_headless_services();
    }

    const Platform service_platform = services.gui
        ? services.gui->platform()
        : services.system ? services.system->platform() : Platform::Automatic;
    AppPaths paths;
    if (service_platform == Platform::Web) {
        if (options.app_root.empty()) options.app_root = "/app";
        if (options.user_root.empty()) options.user_root = "/user";
        paths = AppPaths::from_roots(std::move(options.app_root),
                                     std::move(options.user_root));
    } else {
        paths = default_native_paths(services, std::move(options.app_root),
                                     std::move(options.user_root));
    }

    // 利用可能な機能は、呼び出し元の方針と実際に
    // 登録されたバックエンドの共通部分とする。
    // 特に、画面なしの代替構成に、ビルド対象マク
    // ロだけでデスクトップ機能を引き継がせない。
    options.capabilities = constrain_capabilities(
        probe_capabilities(services, service_platform), options.capabilities);

    // Storage は書き込み可能なマウント先。通常の単独版ではマウント先と
    // エディタのルートを一緒に移す。 管理対象のホストは、マウント先を UserData
    // に保ち、 エディタのルートを UserData/editor、
    // 文書のルートを UserData/Scenario の下に置く。
    auto storage_root = options.storage_root.empty()
        ? paths.user_root
        : std::move(options.storage_root);
    bool managed_storage = false;
    if (const auto managed_root = managed_user_root(services)) {
        const auto editor_root = (*managed_root / editor_data_directory_name())
                                     .lexically_normal();
        if (!storage_root.empty() &&
             (!storage_root.is_absolute() ||
             (!same_managed_path(services, storage_root, *managed_root) &&
              !same_managed_path(services, storage_root, editor_root)))) {
            throw std::runtime_error(
                "managed Editor storage root does not match CARDWIRTHC_USER_ROOT");
        }
        storage_root = *managed_root;
        managed_storage = true;
        paths = AppPaths::from_roots(paths.app_root, editor_root);
        paths.scenario_root = *managed_root / "Scenario";
    }
    if (!storage_root.empty()) {
        std::error_code storage_error_code;
        storage_root = std::filesystem::absolute(std::move(storage_root),
                                                 storage_error_code)
                           .lexically_normal();
        if (!storage_error_code && !managed_storage &&
            storage_root != paths.user_root) {
            paths = AppPaths::from_roots(paths.app_root, storage_root);
        }
    }
    std::unique_ptr<Storage> storage;
    if (service_platform == Platform::Web) {
        ::cardwirth::platform::WebStorageOptions web_options;
        web_options.root = storage_root.empty() ? StoragePath("/user") : storage_root;
        storage = ::cardwirth::platform::make_web_storage(std::move(web_options));
    } else {
        storage = ::cardwirth::platform::make_native_storage(
            storage_root, options.create_storage_root);
    }
    auto result = own(std::move(services), std::move(storage), std::move(paths),
                      std::move(options.capabilities));
    result.initialize_session_services();
    if (options.start_ready) {
        (void)result.begin_storage_load();
        if (dynamic_cast<::cardwirth::platform::WebStorage*>(
                result.storage_ptr()) == nullptr) {
            if (result.complete_storage_load(true)) {
                std::string layout_error;
                if (!result.ensure_storage_layout(&layout_error))
                    result.mark_storage_error(std::move(layout_error));
            }
        } else {
            (void)result.poll_storage();
            if (result.storage_ready()) {
                std::string layout_error;
                if (!result.ensure_storage_layout(&layout_error))
                    result.mark_storage_error(std::move(layout_error));
            }
        }
    }
    return result;
}

void EditorRuntimeContext::initialize_session_services() {
    if (services().file_picker) own_file_picker(services().file_picker);
    own_clipboard(std::make_shared<EditorClipboard>(services().clipboard));
    own_text_input(std::make_shared<TextInputService>());
}

AudioService* EditorRuntimeContext::audio() noexcept {
    if (borrowed_audio_) return borrowed_audio_;
    if (owned_audio_) return owned_audio_.get();
    return services_ && services_->audio ? services_->audio.get() : nullptr;
}

const AudioService* EditorRuntimeContext::audio() const noexcept {
    if (borrowed_audio_) return borrowed_audio_;
    if (owned_audio_) return owned_audio_.get();
    return services_ && services_->audio ? services_->audio.get() : nullptr;
}

FilePicker* EditorRuntimeContext::file_picker() noexcept {
    if (borrowed_file_picker_) return borrowed_file_picker_;
    if (owned_file_picker_) return owned_file_picker_.get();
    return nullptr;
}

const FilePicker* EditorRuntimeContext::file_picker() const noexcept {
    if (borrowed_file_picker_) return borrowed_file_picker_;
    if (owned_file_picker_) return owned_file_picker_.get();
    return nullptr;
}

Clipboard* EditorRuntimeContext::clipboard() noexcept {
    if (borrowed_clipboard_) return borrowed_clipboard_;
    if (owned_clipboard_) return owned_clipboard_.get();
    return nullptr;
}

const Clipboard* EditorRuntimeContext::clipboard() const noexcept {
    if (borrowed_clipboard_) return borrowed_clipboard_;
    if (owned_clipboard_) return owned_clipboard_.get();
    return nullptr;
}

TextInput* EditorRuntimeContext::text_input() noexcept {
    if (borrowed_text_input_) return borrowed_text_input_;
    if (owned_text_input_) return owned_text_input_.get();
    return nullptr;
}

const TextInput* EditorRuntimeContext::text_input() const noexcept {
    if (borrowed_text_input_) return borrowed_text_input_;
    if (owned_text_input_) return owned_text_input_.get();
    return nullptr;
}

void EditorRuntimeContext::own_audio(std::shared_ptr<AudioService> service) {
    owned_audio_ = std::move(service);
    borrowed_audio_ = nullptr;
}

void EditorRuntimeContext::borrow_audio(AudioService& service) noexcept {
    owned_audio_.reset();
    borrowed_audio_ = &service;
}

void EditorRuntimeContext::own_file_picker(std::shared_ptr<FilePicker> service) {
    if (service) service->set_document_root(editor_document_root(paths_));
    owned_file_picker_ = std::move(service);
    borrowed_file_picker_ = nullptr;
}

void EditorRuntimeContext::borrow_file_picker(FilePicker& service) noexcept {
    owned_file_picker_.reset();
    borrowed_file_picker_ = &service;
    try {
        service.set_document_root(editor_document_root(paths_));
    } catch (...) {
        // 借用したサービスは登録したままにする。 提供側・ルー
        // トが使えない場合は、検証と最初の選択操作で報告する。
    }
}

void EditorRuntimeContext::own_clipboard(std::shared_ptr<Clipboard> service) {
    owned_clipboard_ = std::move(service);
    borrowed_clipboard_ = nullptr;
}

void EditorRuntimeContext::borrow_clipboard(Clipboard& service) noexcept {
    owned_clipboard_.reset();
    borrowed_clipboard_ = &service;
}

void EditorRuntimeContext::own_text_input(std::shared_ptr<TextInput> service) {
    owned_text_input_ = std::move(service);
    borrowed_text_input_ = nullptr;
}

void EditorRuntimeContext::borrow_text_input(TextInput& service) noexcept {
    owned_text_input_.reset();
    borrowed_text_input_ = &service;
}

StoragePath EditorRuntimeContext::storage_path(LogicalRoot root,
                                               StoragePath relative) const {
    return paths_.resolve(root, std::move(relative));
}

bool EditorRuntimeContext::ensure_storage_layout(std::string* error) {
    if (error != nullptr) error->clear();
    auto fail = [&](std::string message) {
        if (error != nullptr) *error = std::move(message);
        return false;
    };
    if (storage_ == nullptr) return fail("Editor runtime has no Storage binding");
    if (storage_->root().empty()) return fail("Storage backend returned an empty root");

    // これらは書き込み可能なアプリのルート。 Data/Skin はインストール
    // 済み素材なので、ユーザー用 Storage を通じて意図的に作成しない。
    constexpr std::array writable_roots{
        LogicalRoot::User, LogicalRoot::Config, LogicalRoot::Database, LogicalRoot::Temp,
        LogicalRoot::Cache, LogicalRoot::Trash};
    try {
        for (const auto root : writable_roots) {
            const auto path = storage_path(root);
            if (!storage_->mkdir(path, true))
                return fail(storage_error(*storage_, "Storage::mkdir", path));
        }
        // ALL の文書ディレクトリは、兄弟の UserData/Scenario
        // 。 単独版は意図的に <exe> 自体から開くため、仮の論理的な
        // 代替先 <exe>/editor/Scenario を作らない。
        if (paths_.scenario_root != paths_.user_root / "Scenario" &&
            !storage_->mkdir(paths_.scenario_root, true)) {
            return fail(storage_error(*storage_, "Storage::mkdir",
                                      paths_.scenario_root));
        }
        // D版の従来のプロセス相対の既定値とは異なり、C++製品は一つのエディ
        // タ専用領域を持つ。 AppPaths は ALL で UserData/editor
        // 、単独版で <exe>/editor を選び、 全OSでエ
        // ディタが自動生成するものをその一つのルートの下に収める。
        for (const auto& path : {paths_.user_root / "Log",
                                 paths_.user_root / "Backup",
                                 paths_.user_root / "BackupBeforeSave"}) {
            if (!storage_->mkdir(path, true))
                return fail(storage_error(*storage_, "Storage::mkdir", path));
        }
    } catch (const std::exception& exception) {
        return fail(exception.what());
    } catch (...) {
        return fail("Storage layout initialization failed");
    }
    return true;
}

RuntimeValidation EditorRuntimeContext::validate() const {
    RuntimeValidation result;
    auto error = [&result](std::string message) {
        result.errors.push_back(std::move(message));
    };
    auto warning = [&result](std::string message) {
        result.warnings.push_back(std::move(message));
    };

    if (services_ == nullptr) {
        error("Editor runtime has no Services binding");
    }
    if (storage_ == nullptr) {
        error("Editor runtime has no Storage binding");
    }
    if (paths_.app_root.empty()) error("Editor runtime app root is empty");
    if (paths_.user_root.empty()) error("Editor runtime user root is empty");
    if (!paths_.app_root.empty() && !paths_.app_root.is_absolute())
        error("Editor runtime app root is not absolute");
    if (!paths_.user_root.empty() && !paths_.user_root.is_absolute())
        error("Editor runtime user root is not absolute");

    if (services_ != nullptr) {
        if (!services_->gui) error("GUI backend is unavailable");
        if (!services_->graphics) error("graphics backend is unavailable");
        const bool interactive = services_->gui &&
            services_->gui->platform() != Platform::Headless;
        // エディタはコーデック・アーカイブの機能がなければ文書モ
        // デルを読み込み・描画できない。 対応方針だけでなく実際
        // のサービス登録も確認し、 機能を無効にしただけで、検査
        // が壊れたプラットフォーム構成を見逃さないようにする。
        if (interactive && !services_->image_codecs)
            error("image codec backend is unavailable");
        if (interactive && !services_->drag_drop)
            error("drag-and-drop backend is unavailable");
        if (interactive && !services_->text)
            error("text backend is unavailable");
        if (supports_persistent_storage(capabilities_) && !services_->archives)
            error("archive backend is unavailable");
        if (interactive && !services_->filesystem)
            error("filesystem backend is unavailable");
        if (supports_file_picker(capabilities_) && file_picker() == nullptr)
            error("file picker backend is unavailable");
        if (supports_clipboard(capabilities_) && clipboard() == nullptr)
            error("clipboard backend is unavailable");
        if (supports_text_input(capabilities_) && text_input() == nullptr)
            error("text input backend is unavailable");
        if (supports_audio(capabilities_) && audio() == nullptr)
            error("audio backend is unavailable");
        if (!services_->filesystem) warning("filesystem convenience backend is unavailable");
        if (supports_dispatcher(capabilities_) && !services_->dispatcher)
            error("main-thread dispatcher is unavailable");
        if (supports_tasks(capabilities_) && !services_->tasks)
            error("task backend is unavailable");
        if (supports_external_process(capabilities_) && !services_->processes)
            error("external process capability has no process backend");
        if (supports_drag_drop(capabilities_) && !services_->drag_drop)
            error("drag-and-drop capability has no backend");
        if (supports_shell(capabilities_) && !services_->shell)
            error("shell capability has no shell backend");
        if (supports_multiple_windows(capabilities_) && !services_->ipc)
            error("multiple-window capability has no IPC backend");
        if (supports_text_input(capabilities_) && !services_->text)
            error("text-input capability has no text backend");
        if (!supports_persistent_storage(capabilities_))
            warning("persistent storage is disabled by the host capability set");
    }

    if (storage_ != nullptr) {
        try {
            const auto storage_root = storage_->root();
            if (storage_root.empty()) {
                error("Storage backend returned an empty root");
            } else if (!storage_root.is_absolute()) {
                error("Storage backend root is not absolute");
            } else {
                const auto absolute_root = std::filesystem::absolute(storage_root)
                                               .lexically_normal();
                const auto absolute_user = std::filesystem::absolute(paths_.user_root)
                                               .lexically_normal();
                const auto relative = absolute_user.lexically_relative(absolute_root);
                bool escapes_root = relative == "..";
                if (!escapes_root) {
                    const auto first = relative.begin();
                    escapes_root = first != relative.end() && *first == "..";
                }
                if (relative.has_root_name() || relative.has_root_directory() ||
                    escapes_root) {
                    error("Storage root does not contain the configured user root");
                }
            }
        } catch (...) {
            error("Storage backend could not report its root");
        }
    }

    result.ok = result.errors.empty();
    return result;
}

std::string EditorRuntimeContext::last_error() const {
    std::scoped_lock lock(error_mutex_);
    return last_error_;
}

void EditorRuntimeContext::set_error(std::string error) {
    std::scoped_lock lock(error_mutex_);
    last_error_ = std::move(error);
}

void EditorRuntimeContext::clear_error() {
    std::scoped_lock lock(error_mutex_);
    last_error_.clear();
}

bool EditorRuntimeContext::begin_storage_load() {
    auto current = state_.load(std::memory_order_acquire);
    if (current == RuntimeState::ShuttingDown || current == RuntimeState::Shutdown ||
        current == RuntimeState::SavingStorage)
        return false;
    if (current == RuntimeState::LoadingStorage) return true;
    clear_error();
    sync_state_.store(StorageSyncState::Loading, std::memory_order_release);
    state_.store(RuntimeState::LoadingStorage, std::memory_order_release);
    return true;
}

bool EditorRuntimeContext::complete_storage_load(bool success, std::string error) {
    const auto current = state_.load(std::memory_order_acquire);
    if (current == RuntimeState::ShuttingDown || current == RuntimeState::Shutdown)
        return false;
    if (!success) {
        if (error.empty()) error = "storage synchronization failed";
        set_error(std::move(error));
        sync_state_.store(StorageSyncState::Error, std::memory_order_release);
        state_.store(RuntimeState::StorageError, std::memory_order_release);
        return false;
    }
    clear_error();
    sync_state_.store(StorageSyncState::Synchronized, std::memory_order_release);
    state_.store(RuntimeState::Ready, std::memory_order_release);
    return true;
}

bool EditorRuntimeContext::begin_storage_save() {
    auto current = state_.load(std::memory_order_acquire);
    if (current != RuntimeState::Ready) return false;
    clear_error();
    sync_state_.store(StorageSyncState::Saving, std::memory_order_release);
    state_.store(RuntimeState::SavingStorage, std::memory_order_release);
    return true;
}

bool EditorRuntimeContext::complete_storage_save(bool success, std::string error) {
    const auto current = state_.load(std::memory_order_acquire);
    if (current == RuntimeState::ShuttingDown || current == RuntimeState::Shutdown)
        return false;
    if (!success) {
        if (error.empty()) error = "storage synchronization failed";
        set_error(std::move(error));
        sync_state_.store(StorageSyncState::Error, std::memory_order_release);
        state_.store(RuntimeState::StorageError, std::memory_order_release);
        // 呼び出し元はこの戻り値を操作の成否として使う。 StorageError
        // への遷移自体が完了しても、保存は失敗として扱う。
        return false;
    }
    clear_error();
    sync_state_.store(StorageSyncState::Synchronized, std::memory_order_release);
    state_.store(RuntimeState::Ready, std::memory_order_release);
    return true;
}

void EditorRuntimeContext::mark_storage_ready() {
    (void)complete_storage_load(true);
}

void EditorRuntimeContext::mark_storage_error(std::string error) {
    (void)complete_storage_load(false, std::move(error));
}

bool EditorRuntimeContext::poll_storage() {
#ifdef __EMSCRIPTEN__
    if (persistence_request_) {
        const int result = cardwirth_persist_poll(persistence_request_);
        if (!result) return false;
        persistence_request_ = 0;
        return complete_storage_save(result > 0,
            result == -2
                ? "UserDataの永続化が30秒以内に完了しませんでした。未保存の編集を保持しています。"
                : result < 0
                    ? "UserDataの永続化に失敗しました。未保存の編集を保持しています。"
                    : "");
    }
#endif
    auto* web = dynamic_cast<::cardwirth::platform::WebStorage*>(storage_);
    if (web == nullptr) return storage_ready();
    web->poll();
    switch (web->state()) {
    case ::cardwirth::platform::WebStorageState::Ready:
        if (state() == RuntimeState::LoadingStorage)
            return complete_storage_load(true);
        if (state() == RuntimeState::SavingStorage)
            return complete_storage_save(true);
        return false;
    case ::cardwirth::platform::WebStorageState::StorageError:
        if (state() == RuntimeState::SavingStorage)
            return complete_storage_save(false, web->last_error());
        return complete_storage_load(false, web->last_error());
    case ::cardwirth::platform::WebStorageState::Booting:
    case ::cardwirth::platform::WebStorageState::LoadingStorage:
    case ::cardwirth::platform::WebStorageState::SavingStorage:
        return false;
    }
    return false;
}

bool EditorRuntimeContext::persist_storage() {
    return persist_storage_result() == StoragePersistResult::Completed;
}

StoragePersistResult EditorRuntimeContext::persist_storage_result() {
    if (storage_ == nullptr || is_shutdown()) return StoragePersistResult::Failed;
    auto* web = dynamic_cast<::cardwirth::platform::WebStorage*>(storage_);
    if (web == nullptr) {
        if (!begin_storage_save()) return StoragePersistResult::Failed;
#ifdef __EMSCRIPTEN__
        // ALL はMEMFS上の NativeStorage を借用する。終了・改名は永続化の
        // 書き込みを予約するだけなので、 ブラウザー側のキューをブロックせずに完了応答を待つ。
        if (!persistence_request_) persistence_request_ = cardwirth_persist_begin();
        return StoragePersistResult::Pending;
#else
        return complete_storage_save(true) ? StoragePersistResult::Completed
                                           : StoragePersistResult::Failed;
#endif
    }
    if (!begin_storage_save()) return StoragePersistResult::Failed;
    if (!web->sync_to_persistent()) {
        (void)complete_storage_save(false, web->last_error());
        return StoragePersistResult::Failed;
    }
    (void)poll_storage();
    if (state() == RuntimeState::Ready) return StoragePersistResult::Completed;
    if (state() == RuntimeState::StorageError) return StoragePersistResult::Failed;
    return StoragePersistResult::Pending;
}

bool EditorRuntimeContext::retry_storage() {
    auto* web = dynamic_cast<::cardwirth::platform::WebStorage*>(storage_);
    if (web == nullptr) {
        if (storage_ == nullptr || is_shutdown()) return false;
        std::string error;
        if (!ensure_storage_layout(&error)) {
            set_error(std::move(error));
            sync_state_.store(StorageSyncState::Error, std::memory_order_release);
            state_.store(RuntimeState::StorageError, std::memory_order_release);
            return false;
        }
        clear_error();
        sync_state_.store(StorageSyncState::Synchronized, std::memory_order_release);
        state_.store(RuntimeState::Ready, std::memory_order_release);
        return true;
    }
    if (!web->retry()) {
        set_error(web->last_error());
        return false;
    }
    if (state() == RuntimeState::StorageError) {
        state_.store(RuntimeState::LoadingStorage, std::memory_order_release);
        sync_state_.store(StorageSyncState::Loading, std::memory_order_release);
    }
    return poll_storage();
}

void EditorRuntimeContext::request_shutdown() noexcept {
    auto current = state_.load(std::memory_order_acquire);
    while (current != RuntimeState::Shutdown && current != RuntimeState::ShuttingDown) {
        if (state_.compare_exchange_weak(current, RuntimeState::ShuttingDown,
                                         std::memory_order_acq_rel,
                                         std::memory_order_acquire))
            return;
    }
}

void EditorRuntimeContext::shutdown() noexcept {
    request_shutdown();
    state_.store(RuntimeState::Shutdown, std::memory_order_release);
}

EditorRuntimeContext make_standalone_runtime_context(
    EditorRuntimeContext::StandaloneOptions options) {
    return EditorRuntimeContext::standalone(std::move(options));
}

EditorRuntimeContext make_standalone_editor_runtime(
    EditorRuntimeContext::StandaloneOptions options) {
    return EditorRuntimeContext::standalone(std::move(options));
}

}  // namespace cwxeditor::lib
