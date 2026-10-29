#pragma once
#include "cw/lib/documents.hpp"
#include "app/explorer/platform_transfer_apple.hpp"

namespace cw::lib::documents::apple_adapter {
using Acquire = std::function<void(cardwirth::explorer::apple::ImportOptions,
                                  cardwirth::explorer::NativePathSelectionCallback)>;
using Release = std::function<void(const std::vector<std::filesystem::path>&)>;
struct ImportLease {
    std::vector<std::filesystem::path> paths;
    Release release;
    ~ImportLease() { try { if (release && !paths.empty()) release(paths); } catch (...) {} }
};
// 別のダイアログや設定モデルは作らない。既存の Apple プロバイダーの一時取
// り込みを、SDL と同じ Document の寿命・所有者キューの規則に適合
// させる。境界を注入し、Apple UI を読み込まず CPU 検査も行える。
inline void start_import(const Request& request, Completion complete, Cancelled cancelled,
                         Acquire acquire, Release release) {
    if (cancelled && cancelled()) { complete({Status::Cancelled,{},{}}); return; }
    if (request.operation != Operation::OpenFiles || !request.transient_read_limit) {
        complete({Status::Unsupported,{},"この操作には永続的な文書アクセスまたは書き出し接続が必要です"});
        return;
    }
    if (!acquire || !release) { complete({Status::Failed,{},"文書取得サービスがありません"}); return; }
    cardwirth::explorer::apple::ImportOptions options;
    options.title = request.title;
    options.multiple = request.multiple;
    options.max_file_bytes = request.transient_read_limit;
    options.cancelled = cancelled;
    bool all_files = false;
    for (const auto& filter : request.filters) {
        std::size_t start = 0;
        while (start < filter.extensions.size()) {
            const auto end = filter.extensions.find(';', start);
            const auto extension = filter.extensions.substr(start, end - start);
            if (extension == "*") all_files = true;
            else if (!extension.empty()) options.extensions.push_back(extension);
            if (end == std::string::npos) break;
            start = end + 1;
        }
    }
    if (all_files) options.extensions.clear();
    acquire(std::move(options), [complete = std::move(complete), cancelled = std::move(cancelled),
        release = std::move(release), multiple = request.multiple](cardwirth::explorer::NativePathSelection selection) {
        // システムの選択画面を開いている間にモデルが閉じても、後処理を所有する。
        auto lease = std::make_shared<ImportLease>();
        lease->paths = std::move(selection.paths);
        lease->release = release;
        using TransferStatus = cardwirth::explorer::TransferStatus;
        if ((cancelled && cancelled()) || selection.cancelled || selection.status == TransferStatus::Cancelled) {
            complete({Status::Cancelled,{},{}}); return;
        }
        if (selection.status != TransferStatus::Success) {
            complete({selection.status == TransferStatus::Unsupported ? Status::Unsupported : Status::Failed,
                {}, selection.error}); return;
        }
        if (lease->paths.empty() || (!multiple && lease->paths.size() != 1)) {
            complete({Status::Failed,{},"選択した文書の件数が正しくありません"}); return;
        }
        Result result{Status::Success,{},{}};
        for (const auto& path : lease->paths) {
            if (path.empty()) { complete({Status::Failed,{},"選択した文書の場所が空です"}); return; }
            const auto utf8 = path.u8string();
            result.documents.push_back({LocationKind::NativePath,
                std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size()), lease});
        }
        complete(std::move(result));
    });
}
} // namespace cw::lib::documents::apple_adapter
