#pragma once

// シェルを使わないプロセス・外部文書のヘルパー。パスと引数は
// UTF-8 とし、Windows で必要なワイド文字変換
// は実装側が行う。利用者データをコマンドシェルに渡さない。

#include <chrono>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace cw::lib {

enum class ProcessErrorKind {
    InvalidArgument,
    Unsupported,
    Spawn,
    Io,
    Timeout,
};

class ProcessError : public std::runtime_error {
public:
    ProcessError(ProcessErrorKind kind, const std::string& message);
    ProcessErrorKind kind() const noexcept { return m_kind; }

private:
    ProcessErrorKind m_kind;
};

struct ProcessOptions {
    // executable は絶対・相対パス、または PATH から解
    // 決する名前。arguments に実行ファイル自身は含めない。
    std::string executable;
    std::vector<std::string> arguments;
    std::string working_directory;
    std::map<std::string, std::string> environment;

    bool capture_stdout = true;
    bool capture_stderr = true;
    std::size_t max_output_bytes = 16U * 1024U * 1024U;
    // ゼロは期限なし。Process::run がこの期限を自動で使う。
    std::chrono::milliseconds timeout{0};
    bool terminate_on_timeout = true;
};

struct ProcessResult {
    int exit_code = -1;
    bool timed_out = false;
    bool terminated = false;
    bool output_truncated = false;
    // 取得したストリームはバイトを変えずに保持する。起動したプログ
    // ラムの符号化で復号し、プロセス境界では推測・再符号化しない。
    std::string stdout_text;
    std::string stderr_text;
};

class ProcessHandle {
public:
    ProcessHandle() noexcept = default;
    ~ProcessHandle();

    ProcessHandle(ProcessHandle&& other) noexcept;
    ProcessHandle& operator=(ProcessHandle&& other) noexcept;
    ProcessHandle(const ProcessHandle&) = delete;
    ProcessHandle& operator=(const ProcessHandle&) = delete;

    bool valid() const noexcept { return static_cast<bool>(m_impl); }
    bool running() const;
    void terminate();

    // timeout==0 なら起動オプションの期限を使い、それも
    // ゼロなら無期限に待つ。期限を過ぎると timed_out
    // の結果を返し、既定で子と子孫を終了する。終了を無効にした場
    // 合は動作中のハンドルを保持し、wait() を再度呼べる。
    ProcessResult wait(std::chrono::milliseconds timeout = std::chrono::milliseconds(0));

private:
    struct Impl;
    explicit ProcessHandle(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;

    friend class ExternalProcess;
};

class ExternalProcess {
public:
    static ProcessHandle start(const ProcessOptions& options);
    static ProcessResult run(const ProcessOptions& options);

    // open_url/open_file 用。子を切り離して一時
    // 的な ProcessHandle には所有させず、文書を開く
    // 処理がローカルな RAII の破棄で終了されないようにする。
    static void start_detached(const ProcessOptions& options);

    // Android/iOS のアダプターは Intent/UIApplication の起動処理をこ
    // こに登録する。open_url/open_file はデスクトップ API より先にこれを使う。
    using OpenCallback = std::function<void(const std::string&)>;
    static void set_platform_open_callback(OpenCallback callback);
    static void open_url(const std::string& utf8_url);
    static void open_file(const std::string& utf8_path);
};

} // namespace cw::lib
