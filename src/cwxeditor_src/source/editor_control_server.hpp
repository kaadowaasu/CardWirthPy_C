#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace cwxeditor::cwxeditor {

inline constexpr unsigned short editor_control_default_port = 50433;

struct EditorControlLaunchOptions {
    // 単体版Editorの通信はReleaseでも対応する機能。ループバ
    // ック接続だけを許可し、利用を制限する環境では明示的に無効化できる。
    bool enabled{true};
    unsigned short port{editor_control_default_port};
    std::vector<std::string> remaining_arguments;
};

[[nodiscard]] EditorControlLaunchOptions editor_control_launch_options(
    const std::vector<std::string>& arguments);
[[nodiscard]] std::vector<std::string> editor_control_receive_channels(
    std::string_view hello_json);
[[nodiscard]] std::string editor_control_hello_json(
    const std::vector<std::string>& receiving);

class EditorControlServer final {
public:
    using Handler = std::function<std::string(std::string)>;

    EditorControlServer();
    ~EditorControlServer();
    EditorControlServer(const EditorControlServer&) = delete;
    EditorControlServer& operator=(const EditorControlServer&) = delete;

    void start(Handler handler,
               unsigned short port = editor_control_default_port);
    void stop() noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace cwxeditor::cwxeditor
