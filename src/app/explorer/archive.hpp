#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "platform/storage/storage.hpp"

namespace cardwirth::explorer {

struct ArchiveExtractLimits {
    std::uint64_t max_file_size{512ULL * 1024ULL * 1024ULL};
    std::uint64_t max_total_size{2ULL * 1024ULL * 1024ULL * 1024ULL};
    std::size_t max_files{100000};
};

class ArchiveExtractor {
public:
    virtual ~ArchiveExtractor() = default;
    virtual void extract(const std::filesystem::path& archive,
                         const std::filesystem::path& destination,
                         const ArchiveExtractLimits& limits) const = 0;
};

// 従来のアーカイブ読み込みは外部ライブラリに合わせてネイ
// ティブパスのみを受け取っていた。 この入力境界により、
// Storageを使うExplorerからストリームを渡
// せる。 互換実装では入力アーカイブだけを専用の一時デ
// ィレクトリへ実体化し、 すべての終了経路で削除する。
class ArchiveSource {
public:
    virtual ~ArchiveSource() = default;
    [[nodiscard]] virtual std::string display_name() const = 0;
    [[nodiscard]] virtual std::unique_ptr<cardwirth::platform::StorageReadHandle>
    open_read() const = 0;
};

class StorageArchiveSource final : public ArchiveSource {
public:
    StorageArchiveSource(cardwirth::platform::Storage& storage,
                         cardwirth::platform::StoragePath path)
        : storage_(storage), path_(std::move(path)) {}

    [[nodiscard]] std::string display_name() const override;
    [[nodiscard]] std::unique_ptr<cardwirth::platform::StorageReadHandle>
    open_read() const override;

private:
    cardwirth::platform::Storage& storage_;
    cardwirth::platform::StoragePath path_;
};

// 拡張子による選択と展開処理を分離する。 形式
// の追加時は展開器を登録すればよく、Explorer
// やGUIに形式ごとの分岐を追加しない。
class ArchiveRegistry {
public:
    using ExtractorPtr = std::shared_ptr<const ArchiveExtractor>;

    void register_extension(std::string extension, ExtractorPtr extractor);
    [[nodiscard]] bool supports(const std::filesystem::path& path) const;
    [[nodiscard]] bool supports(const ArchiveSource& source) const;
    void extract(const std::filesystem::path& archive,
                 const std::filesystem::path& destination,
                 const ArchiveExtractLimits& limits = {}) const;
    void extract(const ArchiveSource& archive,
                 const std::filesystem::path& destination,
                 const ArchiveExtractLimits& limits = {}) const;
    void extract(cardwirth::platform::Storage& storage,
                 const cardwirth::platform::StoragePath& archive,
                 const std::filesystem::path& destination,
                 const ArchiveExtractLimits& limits = {}) const;
    // 展開先をすべてStorageで扱う版。専用のネイティブディ
    // レクトリへ展開してから、 結果全体をStorage上の非表
    // 示の兄弟ディレクトリへ転送し、一度の名前変更で公開する。
    void extract_to_storage(cardwirth::platform::Storage& storage,
                            const cardwirth::platform::StoragePath& archive,
                            const cardwirth::platform::StoragePath& destination,
                            const ArchiveExtractLimits& limits = {}) const;

    static ArchiveRegistry with_builtin_formats();

private:
    std::unordered_map<std::string, ExtractorPtr> extractors_;
};

}  // namespace cardwirth::explorer
