#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace cardwirth::app {

struct ProcessSpec {
    std::filesystem::path executable;
    std::vector<std::string> arguments;
    std::filesystem::path working_directory;
};

class Process {
public:
    virtual ~Process() = default;
    [[nodiscard]] virtual bool running() const noexcept = 0;
    [[nodiscard]] virtual int exit_code() const noexcept = 0;
    virtual void terminate() noexcept = 0;
    // 停止要求の失敗を、プロセスの終了確認として扱わない。
    [[nodiscard]] virtual std::error_code stop_error() const noexcept { return {}; }
};

using ProcessPtr = std::shared_ptr<Process>;

// プロセス生成はシェルモデルが使うOS依存のサービスにまとめる。
// UTF-8の引数はargv配列で渡し、シェルは経由しない。未
// 対応のサンドボックスでは nullptrと理由を返す。モデルと
// UIはこの契約だけを参照し、プラットフォーム専用の画面を追加せ
// ずに 共通Presentationの該当操作を無効にできる。
ProcessPtr launch_process(const ProcessSpec& spec, std::string& error);

}  // namespace cardwirth::app
