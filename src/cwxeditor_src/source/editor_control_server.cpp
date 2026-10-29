#include "cwxeditor_src/source/editor_control_server.hpp"

#include "cwxeditor_src/lib/editor_diagnostics.hpp"

#include <atomic>
#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <iomanip>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#endif

namespace cwxeditor::cwxeditor {
namespace {

std::string json_escape(const std::string_view value) {
    std::ostringstream output;
    for (const unsigned char c : value) {
        switch (c) {
        case '"': output << "\\\""; break;
        case '\\': output << "\\\\"; break;
        case '\b': output << "\\b"; break;
        case '\f': output << "\\f"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (c < 0x20U)
                output << "\\u" << std::hex << std::setw(4)
                       << std::setfill('0') << static_cast<int>(c) << std::dec;
            else
                output << static_cast<char>(c);
        }
    }
    return output.str();
}

std::optional<std::string> json_string_at(const std::string_view input,
                                          std::size_t& offset) {
    while (offset < input.size() &&
           std::isspace(static_cast<unsigned char>(input[offset]))) ++offset;
    if (offset >= input.size() || input[offset] != '"') return std::nullopt;
    ++offset;
    std::string result;
    while (offset < input.size()) {
        const char c = input[offset++];
        if (c == '"') return result;
        if (c != '\\') {
            result.push_back(c);
            continue;
        }
        if (offset >= input.size()) return std::nullopt;
        switch (const char escaped = input[offset++]) {
        case '"': case '\\': case '/': result.push_back(escaped); break;
        case 'b': result.push_back('\b'); break;
        case 'f': result.push_back('\f'); break;
        case 'n': result.push_back('\n'); break;
        case 'r': result.push_back('\r'); break;
        case 't': result.push_back('\t'); break;
        default: return std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<std::string> json_string_field(const std::string_view input,
                                             const std::string_view field) {
    const std::string needle = "\"" + std::string(field) + "\"";
    auto offset = input.find(needle);
    if (offset == std::string_view::npos) return std::nullopt;
    offset = input.find(':', offset + needle.size());
    if (offset == std::string_view::npos) return std::nullopt;
    ++offset;
    return json_string_at(input, offset);
}

#if defined(_WIN32)
bool send_all(const SOCKET socket, const std::string_view text) {
    std::size_t sent{};
    while (sent < text.size()) {
        const auto remaining = text.size() - sent;
        const int count = send(socket, text.data() + sent,
                               static_cast<int>(std::min<std::size_t>(
                                   remaining, 0x7FFFFFFFU)), 0);
        if (count <= 0) return false;
        sent += static_cast<std::size_t>(count);
    }
    return true;
}
#endif

std::string json_response(const bool ok, const std::string_view result) {
    return std::string("{\"type\":\"response\",\"ok\":") +
           (ok ? "true" : "false") +
           (ok ? ",\"result\":\"" : ",\"error\":\"") +
           json_escape(result) + "\"}\n";
}

std::string dialog_state_packet(const std::string_view state) {
    return "{\"type\":\"dialog_state\",\"state\":\"" +
           json_escape(state) + "\"}\n";
}

unsigned short parse_port(const std::string_view value) {
    unsigned int port{};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), port);
    if (value.empty() || parsed.ec != std::errc{} ||
        parsed.ptr != value.data() + value.size() || port == 0U || port > 65535U)
        throw std::invalid_argument("Invalid Editor communication port: " +
                                    std::string(value));
    return static_cast<unsigned short>(port);
}

}  // namespace

EditorControlLaunchOptions editor_control_launch_options(
    const std::vector<std::string>& arguments) {
    EditorControlLaunchOptions result;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const auto& argument = arguments[index];
        if (argument == "--communication-debug") {
            result.enabled = true;
            continue;
        }
        if (argument == "--no-communication-debug") {
            result.enabled = false;
            continue;
        }
        if (argument == "--communication-debug-port") {
            if (index + 1U >= arguments.size())
                throw std::invalid_argument(
                    "--communication-debug-port requires a value");
            result.port = parse_port(arguments[++index]);
            continue;
        }
        constexpr std::string_view prefix = "--communication-debug-port=";
        if (argument.starts_with(prefix)) {
            result.port = parse_port(std::string_view(argument).substr(prefix.size()));
            continue;
        }
        result.remaining_arguments.push_back(argument);
    }
    return result;
}

std::vector<std::string> editor_control_receive_channels(
    const std::string_view hello_json) {
    const auto field = hello_json.find("\"receive\"");
    if (field == std::string_view::npos) return {};
    auto offset = hello_json.find('[', field + 9U);
    if (offset == std::string_view::npos)
        throw std::invalid_argument("JSON hello receive must be an array");
    ++offset;
    std::vector<std::string> result;
    for (;;) {
        while (offset < hello_json.size() &&
               std::isspace(static_cast<unsigned char>(hello_json[offset]))) ++offset;
        if (offset >= hello_json.size())
            throw std::invalid_argument("JSON hello receive array is unterminated");
        if (hello_json[offset] == ']') break;
        auto value = json_string_at(hello_json, offset);
        if (!value)
            throw std::invalid_argument("JSON hello receive entries must be strings");
        if (*value == "dialog_state" &&
            std::find(result.begin(), result.end(), *value) == result.end())
            result.push_back(*value);
        while (offset < hello_json.size() &&
               std::isspace(static_cast<unsigned char>(hello_json[offset]))) ++offset;
        if (offset >= hello_json.size())
            throw std::invalid_argument("JSON hello receive array is unterminated");
        if (hello_json[offset] == ']') break;
        if (hello_json[offset++] != ',')
            throw std::invalid_argument("JSON hello receive array is invalid");
    }
    return result;
}

std::string editor_control_hello_json(
    const std::vector<std::string>& receiving) {
    std::ostringstream output;
    output << "{\"type\":\"hello\",\"protocol\":\"cardwirth-debug/1\","
              "\"application\":\"XEditor_C\","
              "\"available\":[\"dialog_state\"],\"receiving\":[";
    for (std::size_t index = 0; index < receiving.size(); ++index) {
        if (index) output << ',';
        output << '"' << json_escape(receiving[index]) << '"';
    }
    output << "]}";
    return output.str();
}

class EditorControlServer::Impl final {
public:
#if defined(_WIN32)
    std::atomic_bool stopping{false};
    SOCKET listener{INVALID_SOCKET};
    SOCKET active_client{INVALID_SOCKET};
    std::mutex client_mutex;
    std::thread worker;
    bool winsock_started{};
    unsigned short port{};
    Handler handler;
#endif
};

EditorControlServer::EditorControlServer() : impl_(std::make_unique<Impl>()) {}
EditorControlServer::~EditorControlServer() { stop(); }

void EditorControlServer::start(Handler handler, const unsigned short port) {
#if defined(_WIN32)
    if (!impl_ || impl_->worker.joinable()) return;
    if (!handler) throw std::invalid_argument("Editor control handler is empty");
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
        throw std::runtime_error("Editor control server WSAStartup failed");
    impl_->winsock_started = true;
    impl_->listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (impl_->listener == INVALID_SOCKET)
        throw std::runtime_error("Editor control server socket creation failed");
    BOOL exclusive = TRUE;
    (void)setsockopt(impl_->listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                     reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(impl_->listener, reinterpret_cast<const sockaddr*>(&address),
             sizeof(address)) == SOCKET_ERROR ||
        listen(impl_->listener, 4) == SOCKET_ERROR) {
        closesocket(impl_->listener);
        impl_->listener = INVALID_SOCKET;
        WSACleanup();
        impl_->winsock_started = false;
        throw std::runtime_error(
            "Editor control server could not listen on 127.0.0.1:" +
            std::to_string(port));
    }
    impl_->handler = std::move(handler);
    impl_->port = port;
    impl_->stopping = false;
    lib::editor_action_log("Editor control server listening on 127.0.0.1:" +
                           std::to_string(port));
    impl_->worker = std::thread([server = impl_.get()] {
        while (!server->stopping.load(std::memory_order_acquire)) {
            const SOCKET client = accept(server->listener, nullptr, nullptr);
            if (client == INVALID_SOCKET) {
                if (server->stopping.load(std::memory_order_acquire)) break;
                continue;
            }
            if (server->stopping.load(std::memory_order_acquire)) {
                shutdown(client, SD_BOTH);
                closesocket(client);
                break;
            }
            {
                std::scoped_lock lock(server->client_mutex);
                server->active_client = client;
            }
            // 継続接続のJSONセッションにも、ダイアログ
            // 状態の変更を通知する。 短い待ち時間で応答を
            // 保ち、購読がなければUIスレッドに触れない。
            DWORD receive_timeout_ms = 100;
            (void)setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
                             reinterpret_cast<const char*>(&receive_timeout_ms),
                             sizeof(receive_timeout_ms));
            std::string input;
            bool connected = true;
            bool json_session = false;
            bool dialog_state_subscribed = false;
            std::string last_dialog_state;
            while (connected &&
                   !server->stopping.load(std::memory_order_acquire)) {
                char buffer[2048];
                const int count = recv(client, buffer,
                                       static_cast<int>(sizeof(buffer)), 0);
                if (count == 0) break;
                if (count == SOCKET_ERROR) {
                    const int error = WSAGetLastError();
                    if (error != WSAETIMEDOUT && error != WSAEWOULDBLOCK) break;
                } else {
                    input.append(buffer, buffer + count);
                    if (input.size() > 65536U) {
                        (void)send_all(client, json_session
                            ? json_response(false, "Editor control packet is too large")
                            : "ERR Editor control packet is too large\n");
                        break;
                    }
                }
                for (;;) {
                    const auto newline = input.find('\n');
                    if (newline == std::string::npos) break;
                    std::string request = input.substr(0, newline);
                    input.erase(0, newline + 1U);
                    if (!request.empty() && request.back() == '\r')
                        request.pop_back();
                    if (request.empty()) continue;
                    if (!json_session && request.front() != '{') {
                        try {
                            connected = send_all(
                                client, "OK " + server->handler(std::move(request)) + "\n");
                        } catch (const std::exception& error) {
                            connected = send_all(client,
                                std::string("ERR ") + error.what() + "\n");
                        } catch (...) {
                            connected = send_all(
                                client, "ERR unknown editor control failure\n");
                        }
                        // 既存のPowerShell・テストクライアント
                        // 用に、一接続一コマンドの従来方式を維持する。
                        connected = false;
                        break;
                    }
                    try {
                        const auto type = json_string_field(request, "type");
                        if (!json_session) {
                            if (!type || *type != "hello")
                                throw std::invalid_argument(
                                    "First JSON packet must be hello");
                            const auto channels =
                                editor_control_receive_channels(request);
                            dialog_state_subscribed =
                                std::find(channels.begin(), channels.end(),
                                          "dialog_state") != channels.end();
                            json_session = true;
                            connected = send_all(
                                client, editor_control_hello_json(channels) + "\n");
                        } else if (type && *type == "command") {
                            const auto command =
                                json_string_field(request, "command");
                            if (!command)
                                throw std::invalid_argument(
                                    "JSON command packet requires command");
                            try {
                                connected = send_all(client, json_response(
                                    true, server->handler(*command)));
                            } catch (const std::exception& error) {
                                connected = send_all(client,
                                    json_response(false, error.what()));
                            } catch (...) {
                                connected = send_all(client, json_response(
                                    false, "unknown editor control failure"));
                            }
                        } else if (type && *type == "subscribe") {
                            const auto channels =
                                editor_control_receive_channels(request);
                            dialog_state_subscribed =
                                std::find(channels.begin(), channels.end(),
                                          "dialog_state") != channels.end();
                            last_dialog_state.clear();
                            connected = send_all(
                                client, editor_control_hello_json(channels) + "\n");
                        } else {
                            connected = send_all(client, json_response(
                                false, "Unsupported JSON packet type"));
                        }
                    } catch (const std::exception& error) {
                        connected = send_all(client,
                            json_response(false, error.what()));
                    }
                    if (!connected) break;
                }
                if (connected && json_session && dialog_state_subscribed) {
                    try {
                        const auto state = server->handler("DIALOGSTATE");
                        if (state != last_dialog_state) {
                            connected = send_all(client,
                                dialog_state_packet(state));
                            last_dialog_state = state;
                        }
                    } catch (const std::exception& error) {
                        connected = send_all(client,
                            json_response(false, error.what()));
                        dialog_state_subscribed = false;
                    } catch (...) {
                        connected = send_all(client, json_response(
                            false, "unknown dialog-state failure"));
                        dialog_state_subscribed = false;
                    }
                }
            }
            {
                std::scoped_lock lock(server->client_mutex);
                if (server->active_client == client)
                    server->active_client = INVALID_SOCKET;
            }
            shutdown(client, SD_BOTH);
            closesocket(client);
        }
    });
#else
    (void)handler;
    (void)port;
#endif
}

void EditorControlServer::stop() noexcept {
#if defined(_WIN32)
    if (!impl_) return;
    impl_->stopping.store(true, std::memory_order_release);
    {
        std::scoped_lock lock(impl_->client_mutex);
        if (impl_->active_client != INVALID_SOCKET)
            (void)shutdown(impl_->active_client, SD_BOTH);
    }
    if (impl_->listener != INVALID_SOCKET) {
        // 別スレッドから待ち受けソケットを閉じても、対応する全Windows
        // 版で accept() の待機が解除されるとは限らない。
        // ループバック接続で一度起こし、終了待ちの後で閉じる。
        const SOCKET wake = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (wake != INVALID_SOCKET) {
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_port = htons(impl_->port);
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            (void)connect(wake, reinterpret_cast<const sockaddr*>(&address),
                          sizeof(address));
            shutdown(wake, SD_BOTH);
            closesocket(wake);
        }
    }
    if (impl_->worker.joinable()) impl_->worker.join();
    if (impl_->listener != INVALID_SOCKET) {
        closesocket(impl_->listener);
        impl_->listener = INVALID_SOCKET;
    }
    impl_->handler = {};
    if (impl_->winsock_started) {
        WSACleanup();
        impl_->winsock_started = false;
    }
#endif
}

}  // namespace cwxeditor::cwxeditor
