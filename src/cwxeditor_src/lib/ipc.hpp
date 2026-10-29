#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace cwxeditor::lib {

class IpcServer {
public:
    virtual ~IpcServer() = default;
    virtual void stop() noexcept = 0;
};

class IpcBackend {
public:
    virtual ~IpcBackend() = default;
    [[nodiscard]] virtual std::string allocate_endpoint(std::string_view application_id) = 0;
    [[nodiscard]] virtual std::unique_ptr<IpcServer> listen(
        std::string endpoint,
        std::function<std::string(std::string_view)> handler,
        std::string& error) = 0;
    [[nodiscard]] virtual std::string request(std::string_view endpoint,
                                              std::string_view message,
                                              std::chrono::milliseconds timeout,
                                              std::string& error) = 0;

    // 一つのアプリケーションの接続先一覧を返す。呼び出し側
    // は順に問い合わせ、最初の一致した応答で停止できる。
    // 接続先を列挙できないバックエンドでは未対応とする。
    [[nodiscard]] virtual std::vector<std::string> enumerate_endpoints(
        std::string_view application_id, std::string_view own_endpoint,
        std::string& error) {
        static_cast<void>(application_id);
        static_cast<void>(own_endpoint);
        error = "IPC peer enumeration is unavailable";
        return {};
    }

    // own_endpointと同じアプリケーションに
    // 属する有効な接続先すべてに、送信元を除いて通知す
    // る。列挙できないバックエンドでは未対応とし、現在
    // のEditor自身に誤って一斉通知を返さない。
    [[nodiscard]] virtual std::size_t broadcast(
        std::string_view own_endpoint, std::string_view message,
        std::chrono::milliseconds timeout, std::string& error) {
        static_cast<void>(own_endpoint);
        static_cast<void>(message);
        static_cast<void>(timeout);
        error = "IPC peer enumeration is unavailable";
        return 0U;
    }
};

}  // namespace cwxeditor::lib
