#pragma once

#include "types.hpp"

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace cwxeditor::lib {

struct ProcessLaunchRequest {
    std::filesystem::path executable;
    std::vector<std::string> arguments;
    std::filesystem::path working_directory;
    std::unordered_map<std::string, std::string> environment;
    bool inherit_environment{true};
    bool show_console{};
    bool wait{};
    bool capture_output{};
    // 独立したGUI子プロセス（別のEditor、Engine、
    // ツールのウィンドウなど）は、launch()が返した一時的
    // なProcessHandleの解放後も動作を続ける。管理対
    // 象の補助プロセスは、従来どおり解放時に終了する。これは共通
    // の契約であり、プラットフォームごとに寿命の規則を変えない。
    bool terminate_on_handle_release{true};
};

class ProcessHandle {
public:
    virtual ~ProcessHandle() = default;
    [[nodiscard]] virtual ProcessId id() const noexcept = 0;
    [[nodiscard]] virtual bool running() const = 0;
    [[nodiscard]] virtual std::optional<int> exit_code() const = 0;
    virtual bool wait(std::chrono::milliseconds timeout) = 0;
    virtual void terminate() = 0;
    [[nodiscard]] virtual std::string read_stdout() = 0;
    [[nodiscard]] virtual std::string read_stderr() = 0;
};

class ProcessBackend {
public:
    virtual ~ProcessBackend() = default;
    [[nodiscard]] virtual std::shared_ptr<ProcessHandle> launch(const ProcessLaunchRequest& request,
                                                                std::string& error) = 0;
};

}  // namespace cwxeditor::lib
