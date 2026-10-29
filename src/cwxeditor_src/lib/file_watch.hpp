#pragma once

#include "filesystem.hpp"
#include "threading.hpp"

#include <map>
#include <optional>
#include <stop_token>

namespace cwxeditor::lib {

// ネイティブ・定期確認の両実装で、通知の所有と実行スレッドを揃える
// 。 ここではWidget・Summary・表示状態を保持しない。
class FileWatchDelivery final {
public:
    FileWatchDelivery(const WatchRequest&, std::shared_ptr<MainDispatcher>);
    ~FileWatchDelivery();
    FileWatchDelivery(const FileWatchDelivery&) = delete;
    FileWatchDelivery& operator=(const FileWatchDelivery&) = delete;
    bool publish(std::vector<FileChange>) const noexcept;
    bool error(std::string) const noexcept;
    void stop() noexcept;
private:
    struct State;
    std::shared_ptr<State> state_;
};

struct FileWatchEntry {
    std::filesystem::file_type type{};
    std::filesystem::file_time_type modified{};
    std::uintmax_t size{};
    std::filesystem::path symlink_target;
    bool operator==(const FileWatchEntry&) const = default;
};
using FileWatchSnapshot = std::map<std::filesystem::path, FileWatchEntry>;
// ディレクトリのリンクはたどらない。中断・失敗した走査を部分的な成功結果とはし
// ない。 ルートがなければ空として扱い、削除・再作成を検出できるようにする。
std::optional<FileWatchSnapshot> scan_file_watch_directory(
    const std::filesystem::path&, bool recursive, std::stop_token = {});
std::vector<FileChange> diff_file_watch_snapshots(
    const FileWatchSnapshot& before, const FileWatchSnapshot& after);
using FileWatchScan = std::function<std::optional<FileWatchSnapshot>(
    const std::filesystem::path&, bool, std::stop_token)>;
std::unique_ptr<FileWatchHandle> make_polling_file_watch(
    WatchRequest, std::shared_ptr<MainDispatcher>, FileWatchScan = scan_file_watch_directory);

} // namespace cwxeditor::lib
