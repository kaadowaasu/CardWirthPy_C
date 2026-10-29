#pragma once

#include <filesystem>
#include <cstddef>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>

namespace cardwirth::platform {

// ネイティブの文書提供元の所有権を管理するもので、ファイルの削除方針は
// 決めない。 提供元は、自分で作った直後のディレクトリだけを登録する。
// 最後の文書の利用が終わった後に返すルートは、その一括取り込み領域の
// 解放対象を表す。 このクラス自身はパスをたどらず、I/Oも行わない。
class DocumentImportLeases final {
public:
    void register_paths(const std::filesystem::path& root,
                        std::span<const std::filesystem::path> paths) {
        if (!root.is_absolute() || root.filename().empty() ||
                root.lexically_normal() != root || paths.empty())
            throw std::invalid_argument("Invalid document import root");
        auto batch = std::make_shared<Batch>(Batch{root, paths.size()});
        std::map<std::filesystem::path, std::shared_ptr<Batch>> incoming;
        for (const auto& path : paths) {
            if (path.filename().empty() || path.parent_path() != root || path.lexically_normal() != path ||
                !incoming.emplace(path, batch).second)
                throw std::invalid_argument("Invalid document import path");
        }
        std::lock_guard lock(mutex_);
        for (const auto& [path, live] : paths_) {
            if (live->root == root || incoming.contains(path))
                throw std::invalid_argument("Document import lease already exists");
        }
        // ノードの移動を使い、メモリ確保に失敗しても途中まで登録された状態を残さない。
        paths_.merge(incoming);
    }

    [[nodiscard]] std::optional<std::filesystem::path> release(
            const std::filesystem::path& path) {
        std::lock_guard lock(mutex_);
        const auto found = paths_.find(path);
        if (found == paths_.end()) return {};
        auto batch = found->second;
        std::optional<std::filesystem::path> root;
        // 所有権を変える前にコピーし、メモリ確保が失敗しても再試行できるようにする。
        if (batch->remaining == 1) root = batch->root;
        --batch->remaining;
        paths_.erase(found);
        return root;
    }

    [[nodiscard]] std::set<std::filesystem::path> active_roots() const {
        std::lock_guard lock(mutex_);
        std::set<std::filesystem::path> roots;
        for (const auto& [path, batch] : paths_) roots.insert(batch->root);
        return roots;
    }

private:
    struct Batch { std::filesystem::path root; std::size_t remaining; };
    mutable std::mutex mutex_;
    std::map<std::filesystem::path, std::shared_ptr<Batch>> paths_;
};

} // namespace cardwirth::platform
