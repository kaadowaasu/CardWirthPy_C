#include "image_import.hpp"

#include "image_mask.hpp"

#include <cctype>
#include <cmath>
#include <limits>

namespace cwxeditor::lib {
namespace {

std::optional<std::string> percent_decode(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    const auto hex = [](const char value) -> int {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'a' && value <= 'f') return value - 'a' + 10;
        if (value >= 'A' && value <= 'F') return value - 'A' + 10;
        return -1;
    };
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] != '%') { result.push_back(value[index]); continue; }
        if (index + 2U >= value.size()) return std::nullopt;
        const auto high = hex(value[index + 1U]);
        const auto low = hex(value[index + 2U]);
        if (high < 0 || low < 0) return std::nullopt;
        result.push_back(static_cast<char>((high << 4) | low));
        index += 2U;
    }
    return result;
}

bool contained_by(const std::filesystem::path& root,
                  const std::filesystem::path& candidate) {
    const auto relative = candidate.lexically_relative(root);
    if (relative.empty() || relative == "." || relative.is_absolute()) return false;
    const auto first = relative.begin();
    return first == relative.end() || *first != "..";
}

// std::filesystem は実行OSに従うが、エディタは全OSで
// Windowsの保存済みシナリオを読む。 Windows以外で動いて
// いるだけで、C:/... のリンクをPOSIXの相対パスに変えない。
bool portable_absolute_path(const std::filesystem::path& path) {
    if (path.is_absolute() || path.has_root_directory()) return true;
    const auto value = path.generic_string();
    return value.size() >= 3U && std::isalpha(static_cast<unsigned char>(value[0])) &&
           value[1] == ':' && (value[2] == '/' || value[2] == '\\');
}

std::optional<std::uint32_t> scaled_dimension(const std::uint32_t value,
                                               const float scale) {
    const auto scaled = static_cast<double>(value) * scale;
    if (!value || !std::isfinite(scaled) || scaled <= 0.0 ||
        scaled > static_cast<double>(std::numeric_limits<std::uint32_t>::max()))
        return std::nullopt;
    return static_cast<std::uint32_t>(std::lround(scaled));
}

} // namespace

std::optional<std::filesystem::path> image_import_file_uri_path(
    const std::string_view uri, std::string& error) {
    constexpr std::string_view prefix{"file://"};
    if (!uri.starts_with(prefix)) {
        error = "image import source must be a file URI";
        return std::nullopt;
    }
    auto encoded = uri.substr(prefix.size());
    if (encoded.starts_with("localhost/")) encoded.remove_prefix(9U);
    // リモートの接続先は提供側・文書側で扱う情報であり、ファイルシステムのパスではない。
    if (!encoded.starts_with('/')) {
        error = "remote file URI authorities are unsupported";
        return std::nullopt;
    }
    const auto decoded = percent_decode(encoded);
    if (!decoded || decoded->empty()) {
        error = "invalid file URI escaping";
        return std::nullopt;
    }
    error.clear();
    return std::filesystem::u8path(*decoded);
}

ImageImportResult ImageImportService::import(const ImageImportRequest& request) const {
    ImageImportResult result;
    if (!codecs_ || !request.transfer) {
        result.error = "image import service is unavailable";
        return result;
    }
    std::filesystem::path source = request.source_path;
    if (source.empty() && !request.source_uri.empty()) {
        std::string error;
        const auto parsed = image_import_file_uri_path(request.source_uri, error);
        if (!parsed) { result.error = std::move(error); return result; }
        source = *parsed;
    }
    if (source.empty() || source.filename().empty()) {
        result.error = "image import source path is empty";
        return result;
    }
    std::filesystem::path material_root;
    try {
        material_root = request.scenario_root.resolve(request.material_directory);
    } catch (const paths::AppPathError& error) {
        result.error = error.what();
        return result;
    }
    const auto transfer = request.transfer({source, request.kind, request.scenario_root.path(),
                                            request.material_directory,
                                            material_root / source.filename()});
    result.action = transfer.action;
    if (transfer.action == ImageImportAction::Cancel) return result;
    if (transfer.material_path.empty()) {
        result.action = ImageImportAction::Cancel;
        result.error = transfer.error.empty() ? "image transfer returned an invalid material path"
                                              : transfer.error;
        return result;
    }
    if (transfer.action == ImageImportAction::Copy &&
        (!request.scenario_root.contains(transfer.material_path) ||
         !contained_by(material_root, transfer.material_path))) {
        result.action = ImageImportAction::Cancel;
        result.error = transfer.error.empty() ? "image copy escaped the scenario material directory"
                                              : transfer.error;
        return result;
    }
    if (transfer.action == ImageImportAction::Link) {
        const auto normalized_source = source.lexically_normal();
        const auto normalized_link = transfer.material_path.lexically_normal();
        if (!portable_absolute_path(normalized_source) ||
            normalized_link != normalized_source) {
            result.action = ImageImportAction::Cancel;
            result.error = transfer.error.empty() ? "image link must retain the external source path"
                                                  : transfer.error;
            return result;
        }
        // ScenarioRoot::contains は実行OSのファイ
        // ルシステムに従うため、 別ドライブのWindowsパスを他のOS
        // では相対と判定する場合がある。 ドライブ・ルートを考慮する字句的
        // な包含判定を使い、外部リンクをモデルの絶対パスとして保持する。
        if (contained_by(request.scenario_root.path().lexically_normal(),
                         normalized_source)) {
            try {
                result.model_path = request.scenario_root.relative(normalized_source);
            } catch (const paths::AppPathError& error) {
                result.action = ImageImportAction::Cancel;
                result.error = error.what();
                return result;
            }
        } else {
            result.model_path = std::move(normalized_link);
        }
    }
    std::string decode_error;
    auto image = codecs_->decode({transfer.material_path}, decode_error);
    if (image.frames.empty()) {
        result.action = ImageImportAction::Cancel;
        result.error = decode_error.empty() ? "image decoder returned no frames" : decode_error;
        return result;
    }
    if (request.apply_color_key) {
        apply_image_color_key(image);
        result.color_key_applied = true;
    }
    const auto width = scaled_dimension(image.frames.front().pixel_width, request.pixel_scale);
    const auto height = scaled_dimension(image.frames.front().pixel_height, request.pixel_scale);
    if (!width || !height) {
        result.action = ImageImportAction::Cancel;
        result.error = "image import scale is invalid";
        return result;
    }
    if (transfer.action == ImageImportAction::Copy) {
        try {
            result.model_path = request.scenario_root.relative(transfer.material_path);
        } catch (const paths::AppPathError& error) {
            result.action = ImageImportAction::Cancel;
            result.error = error.what();
            return result;
        }
    }
    result.pixel_width = *width;
    result.pixel_height = *height;
    return result;
}

} // namespace cwxeditor::lib
