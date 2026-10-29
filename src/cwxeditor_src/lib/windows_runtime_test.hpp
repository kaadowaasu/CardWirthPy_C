#pragma once

#include "audio.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace cwxeditor::lib::windows_runtime::testing {

// WinMMのコマンドとデバイスID取得の基本処理を注入する。テストでも実際のメ
// ッセージ専用ウィンドウ、MM_MCINOTIFYの通知経路、監視規則を使う。
struct MciCommandPrimitive final {
    std::function<std::uint32_t(std::wstring_view command,
                                std::wstring* response,
                                std::uintptr_t callback_window)> send;
    std::function<std::uint32_t(std::wstring_view alias)> device_id;
    // バックエンドはWinMMのコマンド呼び出しとデバイスID検索の両方に、単一
    // の所有スレッド用ディスパッチャーを設定する。テストでは空のままにできる。
    std::function<std::uint32_t(std::function<std::uint32_t()> operation)> dispatch;
};

[[nodiscard]] std::shared_ptr<AudioBackend> make_mci_audio_backend(
    MciCommandPrimitive command,
    std::chrono::milliseconds monitor_interval = std::chrono::milliseconds(50));
// 一定の条件でコマンドをテストするため、バックエンドの実際の監視処理を起動する。
[[nodiscard]] bool tick_mci_audio_backend_for_testing(
    const std::shared_ptr<AudioBackend>& backend) noexcept;

}  // namespace cwxeditor::lib::windows_runtime::testing
