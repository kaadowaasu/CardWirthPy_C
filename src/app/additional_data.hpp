#pragma once

#include <filesystem>
#include <functional>
#include <string>

namespace cardwirth::app {

// 配信されているALL用パッケージを取得し、UserDataへ統合する。 コー
// ルバックはUIスレッドではなく、呼び出し元のワーカースレッドで実行される。
int install_additional_data(const std::filesystem::path& user_root,
                            const std::string& group,
                            const std::function<void(int, int)>& progress);

} // namespace cardwirth::app
