#pragma once

#include "app_paths.hpp"
#include "graphics.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace cwxeditor::lib {

// 取り込み先のドキュメントが画像パスの解釈を決める。プラットフォー
// ムの受け渡し層はCardWirthの場面の意味を知る必要がない。
enum class ImageImportKind { MenuCard, Background };
enum class ImageImportAction { Cancel, Copy, Link };

struct ImageImportTransferRequest {
    std::filesystem::path source_path;
    ImageImportKind kind{ImageImportKind::MenuCard};
    std::filesystem::path scenario_root;
    std::filesystem::path material_directory;
    std::filesystem::path suggested_material_path;
};

// ドキュメントと保存領域のトランザクションはホストが管理する。Copyはシナリ
// オルート配下の素材パスを返し、Linkは正規化した入力元のパスをそのまま返す
// 。共通サービスは、シナリオルート内のLinkをモデル用の相対パスに変換する。
struct ImageImportTransferResult {
    ImageImportAction action{ImageImportAction::Cancel};
    std::filesystem::path material_path;
    std::string error;
};

struct ImageImportRequest {
    // SDLなどの受け渡し層はファイルを直接渡す。ブラウザやドキュメ
    // ント連携からはfile: URIも渡せ、共通コードで解決する。
    std::filesystem::path source_path;
    std::string source_uri;
    ImageImportKind kind{ImageImportKind::MenuCard};
    paths::ScenarioRoot scenario_root;
    // scenario_rootからの相対パス。サービスはディレク
    // トリの遡及や、このディレクトリ外を指す転送結果を拒否する。
    std::filesystem::path material_directory;
    // マスクは復号した入力画素に適用してから、この出力倍率を報告
    // する。保存領域はホストが管理し、再エンコードは行わない。
    float pixel_scale{1.0F};
    bool apply_color_key{true};
    std::function<ImageImportTransferResult(const ImageImportTransferRequest&)> transfer;
};

struct ImageImportResult {
    ImageImportAction action{ImageImportAction::Cancel};
    // Copyとシナリオ内のLinkはシナリオからの相対パス。外部へのLink
    // は、D版Editorで「いいえ」を選んだ場合に受け付ける絶対パス。
    std::filesystem::path model_path;
    std::uint32_t pixel_width{};
    std::uint32_t pixel_height{};
    bool color_key_applied{};
    std::string error;
    [[nodiscard]] bool imported() const noexcept {
        return action != ImageImportAction::Cancel && error.empty() &&
               !model_path.empty() && pixel_width && pixel_height;
    }
};

// file: URIだけを復号する。プロバイダーURIやcontent URIは、ドキュメントのホスト
// が先にファイルとして利用可能にする。不透明な保存先をアダプターが通常のパスとして扱わないようにする。
[[nodiscard]] std::optional<std::filesystem::path> image_import_file_uri_path(
    std::string_view uri, std::string& error);

class ImageImportService {
public:
    explicit ImageImportService(std::shared_ptr<ImageCodecBackend> codecs)
        : codecs_(std::move(codecs)) {}
    [[nodiscard]] ImageImportResult import(const ImageImportRequest& request) const;
private:
    std::shared_ptr<ImageCodecBackend> codecs_;
};

} // namespace cwxeditor::lib
