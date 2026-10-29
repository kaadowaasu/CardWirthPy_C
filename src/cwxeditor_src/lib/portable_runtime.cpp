#include "portable_runtime.hpp"
#include "storage.hpp"
#include "file_watch.hpp"
#include "window_host.hpp"
#include "ui_control_palette.hpp"

#include <SDL3/SDL.h>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <locale>
#include <mutex>
#include <optional>
#include <sstream>
#include <string_view>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cwxeditor::lib {

Platform native_platform() noexcept {
#if defined(__EMSCRIPTEN__)
    return Platform::Web;
#elif defined(_WIN32)
    return Platform::Windows;
#elif defined(__ANDROID__)
    return Platform::Android;
#elif defined(__APPLE__) && TARGET_OS_IPHONE
    return Platform::IOS;
#elif defined(__APPLE__)
    return Platform::MacOS;
#elif defined(__linux__)
    return Platform::Linux;
#else
    return Platform::Automatic;
#endif
}

namespace {

#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__) && \
    !(defined(__APPLE__) && TARGET_OS_IPHONE)

class PortableIpcServer final : public IpcServer {
public:
    PortableIpcServer(std::filesystem::path endpoint,
                      std::function<std::string(std::string_view)> handler)
        : endpoint_(std::move(endpoint)), handler_(std::move(handler)),
          worker_([this](std::stop_token stop) { run(stop); }) {}

    ~PortableIpcServer() override { stop(); }

    void stop() noexcept override {
        if (stopped_.exchange(true)) return;
        worker_.request_stop();
        if (worker_.joinable()) worker_.join();
        std::error_code error;
        std::filesystem::remove_all(endpoint_, error);
    }

private:
    static bool writeAtomic(const std::filesystem::path& target,
                            const std::string_view value) {
        const auto temporary = target.string() + ".tmp";
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output) return false;
            output.write(value.data(), static_cast<std::streamsize>(value.size()));
            if (!output) return false;
        }
        std::error_code error;
        std::filesystem::rename(temporary, target, error);
        if (!error) return true;
        std::filesystem::remove(temporary, error);
        return false;
    }

    static std::optional<std::string> readFile(const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        if (!input) return std::nullopt;
        std::ostringstream value;
        value << input.rdbuf();
        if (!input.good() && !input.eof()) return std::nullopt;
        return value.str();
    }

    void run(const std::stop_token stop) noexcept {
        while (!stop.stop_requested()) {
            try {
                std::error_code error;
                for (std::filesystem::directory_iterator it(endpoint_, error), end;
                     it != end && !error; it.increment(error)) {
                    if (!it->is_regular_file(error)) continue;
                    const auto name = it->path().filename().string();
                    constexpr std::string_view prefix{"request-"};
                    if (!name.starts_with(prefix) || name.ends_with(".tmp")) continue;
                    const auto request = readFile(it->path());
                    if (!request) continue;
                    std::filesystem::remove(it->path(), error);
                    std::string response;
                    try {
                        response = handler_(*request);
                    } catch (const std::exception& failure) {
                        response = std::string("error ") + failure.what();
                    } catch (...) {
                        response = "error IPC handler failed";
                    }
                    const auto token = name.substr(prefix.size());
                    (void)writeAtomic(endpoint_ / ("response-" + token), response);
                }
            } catch (...) {
                // 一時的なディレクトリの競合で、監視処理を終了させない。
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    std::filesystem::path endpoint_;
    std::function<std::string(std::string_view)> handler_;
    std::atomic_bool stopped_{};
    std::jthread worker_;
};

class PortableIpc final : public IpcBackend {
public:
    std::string allocate_endpoint(const std::string_view application_id) override {
        const auto application = normalizedApplicationId(application_id);
        const auto base = ipcBase();
        std::error_code error;
        std::filesystem::create_directories(base, error);
        if (error) return {};
        for (unsigned attempt = 0U; attempt < 64U; ++attempt) {
            const auto serial = next_id_.fetch_add(1U, std::memory_order_relaxed);
            const auto ticks = std::chrono::system_clock::now().time_since_epoch().count();
            const auto endpoint = base / (application + "-" + std::to_string(ticks) +
                                           "-" + std::to_string(serial));
            if (!std::filesystem::create_directory(endpoint, error)) {
                error.clear();
                continue;
            }
            if (!writeText(endpoint / "application", application)) {
                std::filesystem::remove_all(endpoint, error);
                return {};
            }
            return endpoint.generic_string();
        }
        return {};
    }

    std::unique_ptr<IpcServer> listen(
        std::string endpoint,
        std::function<std::string(std::string_view)> handler,
        std::string& error) override {
        const auto path = checkedEndpoint(endpoint, error);
        if (path.empty()) return {};
        if (!std::filesystem::is_directory(path)) {
            error = "IPC endpoint does not exist";
            return {};
        }
        error.clear();
        return std::make_unique<PortableIpcServer>(path, std::move(handler));
    }

    std::string request(const std::string_view endpoint,
                        const std::string_view message,
                        const std::chrono::milliseconds timeout,
                        std::string& error) override {
        const auto path = checkedEndpoint(endpoint, error);
        if (path.empty()) return {};
        const auto token = requestToken();
        const auto request_path = path / ("request-" + token);
        const auto response_path = path / ("response-" + token);
        if (!writeTextAtomic(request_path, message)) {
            error = "IPC request could not be written";
            return {};
        }
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            if (auto response = readText(response_path)) {
                std::error_code ignored;
                std::filesystem::remove(response_path, ignored);
                error.clear();
                return *response;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        std::error_code ignored;
        std::filesystem::remove(request_path, ignored);
        std::filesystem::remove(response_path, ignored);
        error = "IPC request timed out";
        return {};
    }

    std::vector<std::string> enumerate_endpoints(
        const std::string_view application_id,
        const std::string_view own_endpoint,
        std::string& error) override {
        const auto base = ipcBase();
        if (base.empty()) {
            error = "IPC temporary directory is unavailable";
            return {};
        }
        const auto application = normalizedApplicationId(application_id);
        std::filesystem::path own;
        if (!own_endpoint.empty()) {
            std::string endpoint_error;
            own = checkedEndpoint(own_endpoint, endpoint_error);
            if (own.empty()) {
                error = std::move(endpoint_error);
                return {};
            }
        }
        std::vector<std::string> endpoints;
        std::error_code iteration_error;
        std::filesystem::directory_iterator iterator(base, iteration_error), end;
        if (iteration_error == std::errc::no_such_file_or_directory) {
            error.clear();
            return endpoints;
        }
        for (; iterator != end && !iteration_error; iterator.increment(iteration_error)) {
            std::error_code entry_error;
            if (!iterator->is_directory(entry_error) || entry_error) continue;
            const auto endpoint_path = iterator->path();
            if (!own.empty()) {
                std::error_code canonical_error;
                const auto canonical_endpoint =
                    std::filesystem::weakly_canonical(endpoint_path, canonical_error);
                if ((!canonical_error && canonical_endpoint == own) ||
                    endpoint_path.generic_string() == own_endpoint)
                    continue;
            }
            const auto endpoint = endpoint_path.generic_string();
            const auto identity = readText(iterator->path() / "application");
            if (!identity || *identity != application) continue;
            endpoints.push_back(endpoint);
        }
        error = iteration_error ? iteration_error.message() : std::string{};
        return endpoints;
    }

    std::size_t broadcast(const std::string_view own_endpoint,
                          const std::string_view message,
                          const std::chrono::milliseconds timeout,
                          std::string& error) override {
        const auto own = checkedEndpoint(own_endpoint, error);
        if (own.empty()) return 0U;
        const auto application = readText(own / "application");
        if (!application || application->empty()) {
            error = "IPC endpoint has no application identity";
            return 0U;
        }
        std::string iteration_error;
        const auto endpoints = enumerate_endpoints(*application, own_endpoint,
                                                   iteration_error);
        std::size_t delivered{};
        std::string first_error;
        for (const auto& endpoint : endpoints) {
            std::string request_error;
            (void)request(endpoint, message, timeout, request_error);
            if (request_error.empty()) ++delivered;
            else if (first_error.empty()) first_error = std::move(request_error);
        }
        error = iteration_error.empty() ? std::move(first_error)
                                        : std::move(iteration_error);
        return delivered;
    }

private:
    static std::string normalizedApplicationId(const std::string_view application_id) {
        std::string application;
        application.reserve(application_id.size());
        for (const unsigned char value : application_id)
            application.push_back(std::isalnum(value) || value == '-' || value == '_'
                                      ? static_cast<char>(value) : '_');
        if (application.empty()) application = "cwxeditor";
        return application;
    }

    static std::filesystem::path ipcBase() {
        std::error_code error;
        auto root = std::filesystem::temp_directory_path(error);
        if (error || root.empty()) return {};
        return root / "cwxeditor-ipc";
    }

    static std::filesystem::path checkedEndpoint(
        const std::string_view endpoint, std::string& error) {
        if (endpoint.empty()) { error = "IPC endpoint is empty"; return {}; }
        std::error_code fs_error;
        const auto raw_base = ipcBase();
        if (raw_base.empty()) {
            error = "IPC temporary directory is unavailable";
            return {};
        }
        const auto base = std::filesystem::weakly_canonical(raw_base, fs_error);
        const auto path = std::filesystem::weakly_canonical(
            std::filesystem::u8path(endpoint.begin(), endpoint.end()), fs_error);
        if (fs_error || base.empty() || path.parent_path() != base) {
            error = "IPC endpoint is outside the private IPC directory";
            return {};
        }
        error.clear();
        return path;
    }

    static bool writeText(const std::filesystem::path& path,
                          const std::string_view value) {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(value.data(), static_cast<std::streamsize>(value.size()));
        return static_cast<bool>(output);
    }

    static bool writeTextAtomic(const std::filesystem::path& path,
                                const std::string_view value) {
        const auto temporary = path.string() + ".tmp";
        if (!writeText(temporary, value)) return false;
        std::error_code error;
        std::filesystem::rename(temporary, path, error);
        if (!error) return true;
        std::filesystem::remove(temporary, error);
        return false;
    }

    static std::optional<std::string> readText(const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        if (!input) return std::nullopt;
        std::ostringstream value;
        value << input.rdbuf();
        if (!input.good() && !input.eof()) return std::nullopt;
        return value.str();
    }

    static std::string requestToken() {
        const auto serial = next_id_.fetch_add(1U, std::memory_order_relaxed);
        const auto ticks = std::chrono::system_clock::now().time_since_epoch().count();
        return std::to_string(ticks) + "-" + std::to_string(serial);
    }

    inline static std::atomic_uint64_t next_id_{1U};
};

#endif

class PortableFileSystem final : public FileSystemBackend {
public:
    explicit PortableFileSystem(std::shared_ptr<MainDispatcher> dispatcher)
        : dispatcher_(std::move(dispatcher)) {}
    std::unique_ptr<FileWatchHandle> watch(WatchRequest request) override {
        return make_polling_file_watch(std::move(request), dispatcher_);
    }
    bool durable_write(const std::filesystem::path& target,
                       std::span<const std::byte> data, std::string& error) override {
        return storage_api::write_native_file_atomic(target, data, error);
    }
    bool atomic_replace(const std::filesystem::path& temporary,
                        const std::filesystem::path& target, std::string& error) override {
        std::error_code ec;
        if (target.has_parent_path()) std::filesystem::create_directories(target.parent_path(), ec);
        ec.clear();
        std::filesystem::rename(temporary, target, ec);
        if (ec) { error = ec.message(); return false; }
        error.clear(); return true;
    }
    bool move_to_trash(const std::vector<std::filesystem::path>& paths,
                       std::string& error) override {
        char* pref = SDL_GetPrefPath("CardWirthC", "CWXEditor");
        const std::filesystem::path trash = pref ? std::filesystem::path(pref) / "Trash"
                                                 : std::filesystem::temp_directory_path() / "CWXEditorTrash";
        SDL_free(pref);
        std::error_code ec;
        std::filesystem::create_directories(trash, ec);
        if (ec) { error = ec.message(); return false; }
        for (const auto& path : paths) {
            auto target = trash / path.filename();
            for (unsigned suffix = 2; std::filesystem::exists(target, ec); ++suffix)
                target = trash / (path.stem().string() + "_" + std::to_string(suffix) + path.extension().string());
            std::filesystem::rename(path, target, ec);
            if (ec) { error = ec.message(); return false; }
        }
        error.clear(); return true;
    }
    bool valid_filename_character(char32_t value) const override {
        return value >= U' ' && value != U'/' && value != U'\\' && value != U'\0';
    }
private:
    std::shared_ptr<MainDispatcher> dispatcher_;
};

class SdlLibrary final : public DynamicLibrary {
public:
    explicit SdlLibrary(SDL_SharedObject* handle) : handle_(handle) {}
    ~SdlLibrary() override { if (handle_) SDL_UnloadObject(handle_); }
    void* symbol(std::string_view name) const override {
        return handle_ ? reinterpret_cast<void*>(
            SDL_LoadFunction(handle_, std::string(name).c_str())) : nullptr;
    }
private: SDL_SharedObject* handle_{};
};

#if (defined(__linux__) && !defined(__ANDROID__)) || \
    (defined(__APPLE__) && !TARGET_OS_IPHONE)

class SdlProcessHandle final : public ProcessHandle {
public:
    explicit SdlProcessHandle(SDL_Process* process, const bool capture_output,
                              const bool terminate_on_release)
        : process_(process), terminate_on_release_(terminate_on_release) {
        if (!process_) return;
        const SDL_PropertiesID properties = SDL_GetProcessProperties(process_);
        id_ = static_cast<ProcessId>(SDL_GetNumberProperty(
            properties, SDL_PROP_PROCESS_PID_NUMBER, 0));
        if (!capture_output) return;
        stdout_stream_ = static_cast<SDL_IOStream*>(SDL_GetPointerProperty(
            properties, SDL_PROP_PROCESS_STDOUT_POINTER, nullptr));
        stderr_stream_ = static_cast<SDL_IOStream*>(SDL_GetPointerProperty(
            properties, SDL_PROP_PROCESS_STDERR_POINTER, nullptr));
        if (stdout_stream_) {
            stdout_reader_ = std::jthread([this](const std::stop_token stop) {
                read_stream(stdout_stream_, stdout_text_, stop);
            });
        }
        if (stderr_stream_) {
            stderr_reader_ = std::jthread([this](const std::stop_token stop) {
                read_stream(stderr_stream_, stderr_text_, stop);
            });
        }
    }

    ~SdlProcessHandle() override {
        if (!process_) return;
        bool finished = !running();
        if (!finished && terminate_on_release_) {
            (void)SDL_KillProcess(process_, false);
            finished = wait(std::chrono::seconds(1));
            if (!finished) {
                (void)SDL_KillProcess(process_, true);
                finished = wait(std::chrono::milliseconds::max());
            }
        }
        finish_readers(!finished);
        SDL_DestroyProcess(process_);
        process_ = nullptr;
    }

    SdlProcessHandle(const SdlProcessHandle&) = delete;
    SdlProcessHandle& operator=(const SdlProcessHandle&) = delete;

    [[nodiscard]] ProcessId id() const noexcept override { return id_; }

    [[nodiscard]] bool running() const override {
        std::scoped_lock lock(process_mutex_);
        if (!process_ || exit_code_) return false;
        int code = 0;
        if (!SDL_WaitProcess(process_, false, &code)) return true;
        exit_code_ = code;
        return false;
    }

    [[nodiscard]] std::optional<int> exit_code() const override {
        (void)running();
        std::scoped_lock lock(process_mutex_);
        return exit_code_;
    }

    bool wait(const std::chrono::milliseconds timeout) override {
        if (timeout.count() < 0) return false;
        if (timeout == std::chrono::milliseconds::max()) {
            std::scoped_lock lock(process_mutex_);
            if (!process_ || exit_code_) return exit_code_.has_value();
            int code = 0;
            if (!SDL_WaitProcess(process_, true, &code)) return false;
            exit_code_ = code;
            finish_readers(false);
            return true;
        }
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        do {
            if (!running()) {
                finish_readers(false);
                return true;
            }
            if (timeout == std::chrono::milliseconds::zero()) return false;
            SDL_Delay(5);
        } while (std::chrono::steady_clock::now() < deadline);
        if (running()) return false;
        finish_readers(false);
        return true;
    }

    void terminate() override {
        std::scoped_lock lock(process_mutex_);
        if (process_ && !exit_code_) (void)SDL_KillProcess(process_, false);
    }

    [[nodiscard]] std::string read_stdout() override {
        std::scoped_lock lock(output_mutex_);
        return stdout_text_;
    }

    [[nodiscard]] std::string read_stderr() override {
        std::scoped_lock lock(output_mutex_);
        return stderr_text_;
    }

private:
    void read_stream(SDL_IOStream* stream, std::string& destination,
                     const std::stop_token stop) noexcept {
        std::array<char, 16U * 1024U> buffer{};
        while (!stop.stop_requested()) {
            const std::size_t count = SDL_ReadIO(stream, buffer.data(), buffer.size());
            if (count > 0U) {
                try {
                    std::scoped_lock lock(output_mutex_);
                    destination.append(buffer.data(), count);
                } catch (...) {
                    return;
                }
                continue;
            }
            const SDL_IOStatus status = SDL_GetIOStatus(stream);
            if (status == SDL_IO_STATUS_EOF || status == SDL_IO_STATUS_ERROR) return;
            SDL_Delay(5);
        }
    }

    void finish_readers(const bool cancel) const noexcept {
        if (readers_finished_.exchange(true, std::memory_order_acq_rel)) return;
        if (cancel) {
            stdout_reader_.request_stop();
            stderr_reader_.request_stop();
        }
        if (stdout_reader_.joinable()) stdout_reader_.join();
        if (stderr_reader_.joinable()) stderr_reader_.join();
    }

    SDL_Process* process_{};
    SDL_IOStream* stdout_stream_{};
    SDL_IOStream* stderr_stream_{};
    ProcessId id_{};
    mutable std::mutex process_mutex_;
    mutable std::optional<int> exit_code_;
    mutable std::jthread stdout_reader_;
    mutable std::jthread stderr_reader_;
    mutable std::atomic_bool readers_finished_{};
    mutable std::mutex output_mutex_;
    std::string stdout_text_;
    std::string stderr_text_;
    bool terminate_on_release_{true};
};

class SdlProcessBackend final : public ProcessBackend {
public:
    [[nodiscard]] std::shared_ptr<ProcessHandle> launch(
        const ProcessLaunchRequest& request, std::string& error) override {
        if (request.executable.empty()) {
            error = "process executable is empty";
            return {};
        }

        std::vector<std::string> argument_storage;
        argument_storage.reserve(request.arguments.size() + 1U);
        argument_storage.push_back(path_text(request.executable));
        for (const auto& argument : request.arguments) {
            if (argument.find('\0') != std::string::npos) {
                error = "process argument contains NUL";
                return {};
            }
            argument_storage.push_back(argument);
        }
        std::vector<const char*> arguments;
        arguments.reserve(argument_storage.size() + 1U);
        for (const auto& argument : argument_storage) arguments.push_back(argument.c_str());
        arguments.push_back(nullptr);

        SDL_Environment* environment = nullptr;
        if (!request.inherit_environment || !request.environment.empty()) {
            environment = SDL_CreateEnvironment(request.inherit_environment);
            if (!environment) {
                error = SDL_GetError();
                return {};
            }
            for (const auto& [name, value] : request.environment) {
                if (name.empty() || name.find('=') != std::string::npos ||
                    name.find('\0') != std::string::npos ||
                    value.find('\0') != std::string::npos ||
                    !SDL_SetEnvironmentVariable(environment, name.c_str(), value.c_str(), true)) {
                    error = SDL_GetError();
                    if (error.empty()) error = "invalid process environment variable";
                    SDL_DestroyEnvironment(environment);
                    return {};
                }
            }
        }

        const SDL_PropertiesID properties = SDL_CreateProperties();
        if (!properties) {
            if (environment) SDL_DestroyEnvironment(environment);
            error = SDL_GetError();
            return {};
        }
        SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER,
                               arguments.data());
        if (environment) SDL_SetPointerProperty(
            properties, SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER, environment);
        const std::string working_directory = path_text(request.working_directory);
        if (!working_directory.empty()) SDL_SetStringProperty(
            properties, SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING,
            working_directory.c_str());
        SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER,
                              SDL_PROCESS_STDIO_NULL);
        const SDL_ProcessIO output_mode = request.capture_output
            ? SDL_PROCESS_STDIO_APP
            : (request.show_console ? SDL_PROCESS_STDIO_INHERITED
                                    : SDL_PROCESS_STDIO_NULL);
        SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER,
                              output_mode);
        SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER,
                              output_mode);

        SDL_Process* process = SDL_CreateProcessWithProperties(properties);
        SDL_DestroyProperties(properties);
        if (environment) SDL_DestroyEnvironment(environment);
        if (!process) {
            error = SDL_GetError();
            return {};
        }
        try {
            auto handle = std::make_shared<SdlProcessHandle>(
                process, request.capture_output,
                request.terminate_on_handle_release);
            if (request.wait) (void)handle->wait(std::chrono::milliseconds::max());
            error.clear();
            return handle;
        } catch (const std::exception& exception) {
            SDL_KillProcess(process, true);
            SDL_DestroyProcess(process);
            error = exception.what();
            return {};
        }
    }

private:
    [[nodiscard]] static std::string path_text(const std::filesystem::path& path) {
        const auto encoded = path.u8string();
        return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
    }
};

#endif

class PortableSystem final : public SystemBackend {
public:
    Platform platform() const noexcept override { return native_platform(); }
    std::filesystem::path executable_path() const override {
#if defined(__EMSCRIPTEN__)
        return "/program/CWXEditor";
#elif defined(__ANDROID__)
        if (const char* internal = SDL_GetAndroidInternalStoragePath())
            return std::filesystem::path(internal) / "app" / "CWXEditor";
#endif
        const char* base = SDL_GetBasePath();
        if (!base) return std::filesystem::current_path() / "CWXEditor";
        std::filesystem::path root(base);
#if defined(__APPLE__) && !TARGET_OS_IPHONE
        std::error_code ec;
        const auto resources = (root / ".." / "Resources").lexically_normal();
        if (std::filesystem::is_directory(resources, ec)) root = resources;
#endif
        return root / "CWXEditor";
    }
    std::filesystem::path special_directory(SpecialDirectory directory) const override {
#if defined(__EMSCRIPTEN__)
        if (directory == SpecialDirectory::Application) return "/program";
        if (directory == SpecialDirectory::Temporary) return "/user/Temp";
        if (directory == SpecialDirectory::Home || directory == SpecialDirectory::Documents)
            return "/user";
        return "/user/Config";
#endif
        if (directory == SpecialDirectory::Application) {
            const auto executable = executable_path();
            return executable.empty() ? std::filesystem::path{} : executable.parent_path();
        }
        if (directory == SpecialDirectory::Temporary) return std::filesystem::temp_directory_path();
        if (directory == SpecialDirectory::Documents) {
            if (const char* value = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS)) return value;
        }
        if (directory == SpecialDirectory::Home) {
            if (const char* value = SDL_GetUserFolder(SDL_FOLDER_HOME)) return value;
        }
        char* pref = SDL_GetPrefPath("CardWirthC", "CWXEditor");
        std::filesystem::path result = pref ? pref : executable_path();
        SDL_free(pref);
        return result;
    }
    std::string locale_name() const override {
        int count = 0; SDL_Locale** locales = SDL_GetPreferredLocales(&count);
        std::string result = "en";
        if (locales && count > 0 && locales[0] && locales[0]->language) {
            result = locales[0]->language;
            if (locales[0]->country) result += std::string("-") + locales[0]->country;
        }
        SDL_free(locales); return result;
    }
    ThemeMode theme_mode() const override { return ThemeMode::System; }
    [[nodiscard]] std::chrono::milliseconds double_click_time() const noexcept override {
        // SDL は、共通ホストに設定されたダブルクリックの時間間隔を公開する。
        const char* const value = SDL_GetHint(SDL_HINT_MOUSE_DOUBLE_CLICK_TIME);
        if (!value || !*value) return kDefaultDoubleClickTime;
        const std::string_view text(value);
        std::int32_t milliseconds{};
        const auto [end, error] = std::from_chars(
            text.data(), text.data() + text.size(), milliseconds);
        if (error != std::errc{} || end != text.data() + text.size() || milliseconds < 0)
            return kDefaultDoubleClickTime;
        return std::chrono::milliseconds{milliseconds};
    }
    float scale_factor() const override {
        // ALL はWeb・Androidなど全ホストで、一つの固定された論
        // 理キャンバス上にエディタを構成する。 画面密度でダイアログ配置・
        // 一覧サイズ・画像倍率を変えないため、ALL は常に1.0とする。
        if (editor_window_host_options().all_mode) return 1.0F;
        int count = 0; SDL_DisplayID* displays = SDL_GetDisplays(&count);
        const float result = displays && count > 0 ? SDL_GetDisplayContentScale(displays[0]) : 1.0F;
        SDL_free(displays); return result > 0 ? result : 1.0F;
    }
    std::size_t working_memory() const override {
        return static_cast<std::size_t>(std::max(0, SDL_GetSystemRAM())) * 1024U * 1024U;
    }
    std::unique_ptr<DynamicLibrary> load_library(const std::filesystem::path& path,
                                                  std::string& error) override {
        SDL_SharedObject* handle = SDL_LoadObject(path.string().c_str());
        if (!handle) { error = SDL_GetError(); return {}; }
        error.clear(); return std::make_unique<SdlLibrary>(handle);
    }
    std::vector<std::byte> executable_resource(const std::filesystem::path&,
                                                std::string_view, std::string_view,
                                                std::string& error) override {
        error = "executable resources are not used by the SDL frontend"; return {};
    }
    void set_environment(std::string name, std::string value) override {
        SDL_SetEnvironmentVariable(SDL_GetEnvironment(), name.c_str(), value.c_str(), true);
    }
    std::optional<std::string> environment(std::string_view name) const override {
        const char* value = SDL_GetEnvironmentVariable(
            SDL_GetEnvironment(), std::string(name).c_str());
        return value ? std::optional<std::string>(value) : std::nullopt;
    }
};

class PortableShell final : public ShellBackend {
public:
    bool open_path(const std::filesystem::path& path, std::string& error) override {
        std::error_code filesystem_error;
        const auto absolute = std::filesystem::absolute(path, filesystem_error);
        if (filesystem_error) {
            error = filesystem_error.message();
            return false;
        }
#if defined(_WIN32)
        // WindowsのExplorerにはネイティブのファイルパスを渡す
        // 。 SDL_OpenURL に file:// URLを渡すと、単
        // 独エディタのシナリオフォルダーではなくブラウザーが開く場合がある。
        if (std::filesystem::is_directory(absolute, filesystem_error)) {
            const auto status = reinterpret_cast<std::intptr_t>(
                ::ShellExecuteW(nullptr, L"open", absolute.c_str(), nullptr,
                                nullptr, SW_SHOWNORMAL));
            if (status > 32) { error.clear(); return true; }
            error = "Windows could not open the folder (ShellExecuteW: " +
                    std::to_string(status) + ")";
            return false;
        }
        if (filesystem_error) {
            error = filesystem_error.message();
            return false;
        }
#endif
        const auto encoded_path = absolute.generic_u8string();
        const std::string raw(reinterpret_cast<const char*>(encoded_path.data()),
                              encoded_path.size());
        std::string url = raw.starts_with('/') ? "file://" : "file:///";
        constexpr char hex[] = "0123456789ABCDEF";
        for (const unsigned char character : raw) {
            const bool safe = std::isalnum(character) || character == '-' ||
                character == '_' || character == '.' || character == '~' ||
                character == '/' || character == ':';
            if (safe) {
                url.push_back(static_cast<char>(character));
            } else {
                url.push_back('%');
                url.push_back(hex[character >> 4U]);
                url.push_back(hex[character & 0x0FU]);
            }
        }
        return open_url(url, error);
    }
    bool reveal_path(const std::filesystem::path& path, std::string& error) override {
#if defined(_WIN32)
        if (!editor_window_host_options().all_mode) {
            std::error_code filesystem_error;
            const auto absolute = std::filesystem::absolute(path, filesystem_error);
            if (filesystem_error) { error = filesystem_error.message(); return false; }
            const auto target = absolute.native();
            // D版と同様、Explorerのネイティブの選択引数を使う。 Windows
            // のファイル名には引用符・NULを含められないため拒否し、 不
            // 正なパスがコマンドラインの引数の境界を変えないようにする。
            if (target.empty() || target.find(L'"') != std::wstring::npos ||
                target.find(L'\0') != std::wstring::npos) {
                error = "Invalid Windows reveal target";
                return false;
            }
            std::array<wchar_t, 32768> windows_directory{};
            const auto length = ::GetWindowsDirectoryW(windows_directory.data(),
                static_cast<UINT>(windows_directory.size()));
            if (!length || length >= windows_directory.size()) {
                error = "Windows Explorer directory is unavailable";
                return false;
            }
            const auto explorer = std::filesystem::path(windows_directory.data()) / L"explorer.exe";
            const std::wstring arguments = L"/select,\"" + target + L"\"";
            const auto status = reinterpret_cast<std::intptr_t>(::ShellExecuteW(
                nullptr, L"open", explorer.c_str(), arguments.c_str(), nullptr, SW_SHOWNORMAL));
            if (status > 32) { error.clear(); return true; }
            error = "Windows could not reveal the item (ShellExecuteW: " +
                    std::to_string(status) + ")";
            return false;
        }
#endif
        return open_path(path.has_parent_path() ? path.parent_path() : path, error);
    }
    bool open_url(std::string_view url, std::string& error) override {
        if (SDL_OpenURL(std::string(url).c_str())) { error.clear(); return true; }
        error = SDL_GetError(); return false;
    }
    std::string associated_command(const std::filesystem::path&) const override { return {}; }
};

const char* encoding_name(TextEncoding encoding, bool destination) {
    switch (encoding) {
    case TextEncoding::Utf8: return "UTF-8";
    case TextEncoding::Utf16LE: return "UTF-16LE";
    case TextEncoding::Utf16BE: return "UTF-16BE";
    case TextEncoding::ShiftJis: return "SHIFT-JIS";
    case TextEncoding::System: return destination ? "" : "";
    }
    return "UTF-8";
}

class PortableText final : public TextBackend {
public:
    std::string decode(std::span<const std::byte> data, TextEncoding encoding,
                       std::string& error) const override {
        if (encoding == TextEncoding::Utf8) { error.clear(); return {reinterpret_cast<const char*>(data.data()), data.size()}; }
        char* converted = SDL_iconv_string("UTF-8", encoding_name(encoding, false),
            reinterpret_cast<const char*>(data.data()), data.size());
        if (!converted) { error = SDL_GetError(); return {}; }
        std::string result(converted); SDL_free(converted); error.clear(); return result;
    }
    std::vector<std::byte> encode(std::string_view text, TextEncoding encoding,
                                  std::string& error) const override {
        if (encoding == TextEncoding::Utf8) { error.clear(); return {reinterpret_cast<const std::byte*>(text.data()), reinterpret_cast<const std::byte*>(text.data()+text.size())}; }
        // string_view は data()[size()] の読み
        // 取りを保証しない。 SDL_iconv_string は指定バイ
        // ト数だけを読み、返す領域に自分でゼロ埋めするため、 size+1
        // を渡して部分ビューの外を読んだり、隣の文字を加えたりしない。
        char* converted = SDL_iconv_string(encoding_name(encoding, true), "UTF-8",
                                          text.data(), text.size());
        if (!converted) { error = SDL_GetError(); return {}; }
        const std::size_t size = encoding == TextEncoding::Utf16LE || encoding == TextEncoding::Utf16BE
            ? std::char_traits<char16_t>::length(reinterpret_cast<const char16_t*>(converted))*2U
            : std::strlen(converted);
        std::vector<std::byte> result(reinterpret_cast<std::byte*>(converted), reinterpret_cast<std::byte*>(converted)+size);
        SDL_free(converted); error.clear(); return result;
    }
    std::string normalize(std::string_view text, TextNormalization) const override { return std::string(text); }
    std::string case_fold(std::string_view text) const override { std::string result(text);std::transform(result.begin(),result.end(),result.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});return result; }
    int locale_compare(std::string_view left, std::string_view right) const override { return left<right?-1:left>right?1:0; }
};

}  // namespace

ControlPalette system_control_palette() noexcept {
#if defined(_WIN32)
    // SWTのWindowsコントロールの既定値と同じシステム色を使う。 システ
    // ム色・高コントラスト設定の変更がキャッシュに隠れないよう、描画ごとに読む。
    const auto color = [](const int index) {
        const auto value = GetSysColor(index);
        return Color{GetRValue(value), GetGValue(value), GetBValue(value), 255U};
    };
    return {color(COLOR_WINDOWTEXT), color(COLOR_BTNFACE)};
#else
    return {};
#endif
}

void install_portable_runtime_services(Services& services) {
    services.filesystem = std::make_shared<PortableFileSystem>(services.dispatcher);
#if (defined(__linux__) && !defined(__ANDROID__)) || \
    (defined(__APPLE__) && !TARGET_OS_IPHONE)
    services.processes = std::make_shared<SdlProcessBackend>();
#endif
    services.system = std::make_shared<PortableSystem>();
#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__) && \
    !(defined(__APPLE__) && TARGET_OS_IPHONE)
    // すべてのデスクトップOSで一つのファイルシステム方式のプロトコルを使う。 OS
    // 固有の名前付きパイプ・ソケットの分岐は、エディタや公開APIに持ち込まない。
    services.ipc = std::make_shared<PortableIpc>();
#endif
#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__) && \
    !(defined(__APPLE__) && TARGET_OS_IPHONE)
    // モバイル・Webの制限された保存領域のパスは、外部から開けるファイル
    // URLではない。 ShellBackend を設けず、SDLが完了
    // できないフォルダー表示・選択表示・外部編集は無効にする。 これらの
    // 環境のデータ交換には FilePickerBackend を使う。
    services.shell = std::make_shared<PortableShell>();
#endif
    services.text = std::make_shared<PortableText>();
}

}  // namespace cwxeditor::lib
