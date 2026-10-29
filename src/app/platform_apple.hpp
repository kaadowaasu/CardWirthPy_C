#pragma once

#include <filesystem>
#include <cstdint>
#include <string>

namespace cardwirth::app::apple {

// macOSとiOSのNSBundle資源は読み取り専用なので、 すべての
// SDLホストで実行時の資産と書き込み可能なアプリデータのルートを分ける。
[[nodiscard]] std::filesystem::path bundle_resource_root();
[[nodiscard]] std::filesystem::path writable_user_root();
// 互換用コピーなど再作成可能なデータをiCloud・端末バックアップの対象
// 外にする。 繰り返し呼んでも結果は同じで、失敗時は成功として扱わない。
bool exclude_from_backup(const std::filesystem::path& path);
bool download_https_file(const std::string& url,
                         const std::filesystem::path& destination,
                         std::uint64_t limit);

}  // namespace cardwirth::app::apple
