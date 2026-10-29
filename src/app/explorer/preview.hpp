#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "platform/storage/storage.hpp"

namespace cardwirth::explorer {

enum class PreviewKind {
    Text,
    Binary,
    Image,
    Audio,
    Video,
};

struct PreviewData {
    PreviewKind kind{PreviewKind::Binary};
    std::filesystem::path path;
    std::string title;
    std::string text;
    int image_width{};
    int image_height{};
    std::vector<std::uint8_t> rgba;
};

namespace detail {

struct PreviewImageLayout {
    std::size_t row_bytes{};
    std::size_t source_bytes{};
    std::size_t total_bytes{};
};

// SDLの寸法とピッチは符号付きintなので、size_t
// への変換前に乗算があふれないよう検査する。
// この変換は独立してテストできるようにする。
[[nodiscard]] std::optional<PreviewImageLayout> checked_preview_image_layout(
    int width, int height, int pitch) noexcept;

}  // namespace detail

// プレビューのデコードはこの境界を通じてバイト列を読む。 メディ
// アライブラリがネイティブのファイル名を必要とする環境では、
// PreviewServiceが寿命を管理する一時ファイルを作
// り、Storage側の呼び出し元へパスの要求を持ち込まない。
class PreviewSource {
public:
    virtual ~PreviewSource() = default;
    [[nodiscard]] virtual std::optional<cardwirth::platform::StorageStat> stat(
        const cardwirth::platform::StoragePath& path) const = 0;
    [[nodiscard]] virtual std::unique_ptr<cardwirth::platform::StorageReadHandle>
    open_read(const cardwirth::platform::StoragePath& path) const = 0;
    [[nodiscard]] virtual std::vector<cardwirth::platform::StorageEntry> list(
        const cardwirth::platform::StoragePath&) const {
        return {};
    }
    [[nodiscard]] virtual std::string last_error() const = 0;
};

class StoragePreviewSource final : public PreviewSource {
public:
    explicit StoragePreviewSource(std::shared_ptr<cardwirth::platform::Storage> storage)
        : owner_(std::move(storage)), storage_(owner_.get()) {}
    explicit StoragePreviewSource(cardwirth::platform::Storage& storage)
        : storage_(&storage) {}

    [[nodiscard]] std::optional<cardwirth::platform::StorageStat> stat(
        const cardwirth::platform::StoragePath& path) const override {
        return storage_ ? storage_->stat(path) : std::nullopt;
    }
    [[nodiscard]] std::unique_ptr<cardwirth::platform::StorageReadHandle> open_read(
        const cardwirth::platform::StoragePath& path) const override {
        return storage_ ? storage_->open_read(path) : nullptr;
    }
    [[nodiscard]] std::vector<cardwirth::platform::StorageEntry> list(
        const cardwirth::platform::StoragePath& path) const override {
        return storage_ ? storage_->list(path)
                        : std::vector<cardwirth::platform::StorageEntry>{};
    }
    [[nodiscard]] std::string last_error() const override {
        return storage_ ? storage_->last_error() : std::string{};
    }

private:
    std::shared_ptr<cardwirth::platform::Storage> owner_;
    cardwirth::platform::Storage* storage_{};
};

class PreviewService {
public:
    explicit PreviewService(std::filesystem::path data_directory = {});
    explicit PreviewService(std::shared_ptr<PreviewSource> source,
                            cardwirth::platform::StoragePath data_directory = {});
    explicit PreviewService(std::shared_ptr<cardwirth::platform::Storage> storage,
                            cardwirth::platform::StoragePath data_directory = {});
    explicit PreviewService(cardwirth::platform::Storage& storage,
                            cardwirth::platform::StoragePath data_directory = {});
    ~PreviewService();
    PreviewService(PreviewService&&) noexcept;
    PreviewService& operator=(PreviewService&&) noexcept;
    PreviewService(const PreviewService&) = delete;
    PreviewService& operator=(const PreviewService&) = delete;

    [[nodiscard]] PreviewData load(const std::filesystem::path& path) const;
    // ファイルシステムのワーカー用の独立したデコード状態。 動作
    // 中のプレビューサービスと、再生資源やUI資源を共有しない。
    [[nodiscard]] std::shared_ptr<PreviewService> make_decoder() const;
    void play_audio(const std::filesystem::path& path);
    void play_video(const std::filesystem::path& path);
    // constのサービス参照を持つ呼び出し元向けの互換
    // オーバーロード。 再生は状態を変更する操作なので
    // 、所有者の非constサービスへ処理を転送する。
    void play_video(const std::filesystem::path& path) const;
    // ブラウザーのBlob URL・重ね表示と、実体化したメディアの保持を解
    // 放する。 寿命を所有しないデスクトップの外部プレイヤーには影響しない。
    void stop_video() noexcept;
    void stop_audio() noexcept;
    void set_audio_suspended(bool suspended) noexcept;
    // コールバックで記録された自然終了を、所有者のイベントループで処理する。
    [[nodiscard]] bool collect_finished_audio() noexcept;
    [[nodiscard]] bool audio_has_signal() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace cardwirth::explorer
