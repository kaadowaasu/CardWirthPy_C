#include "cwxeditor_src/core/cwx/cab.hpp"
#include "cwxeditor_src/core/cwx/sjis.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"

#include "cwxeditor_src/lib/archive.hpp"
#include "cw/lib/archive.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace cwxeditor::cwx::cab {
namespace {

using Path = std::filesystem::path;
using ArchiveEntry = ::cwxeditor::lib::ArchiveEntry;
using ArchiveFormat = ::cwxeditor::lib::ArchiveFormat;

[[nodiscard]] Path pathFromUtf8(StringView value) {
#if defined(__cpp_char8_t)
    const auto* first = reinterpret_cast<const char8_t*>(value.data());
    return Path(std::u8string(first, first + value.size()));
#else
    return std::filesystem::u8path(value.data(), value.data() + value.size());
#endif
}

[[nodiscard]] String pathToUtf8(const Path& value) {
    const auto encoded = value.generic_u8string();
#if defined(__cpp_char8_t)
    return String(reinterpret_cast<const char*>(encoded.data()), encoded.size());
#else
    return encoded;
#endif
}

[[nodiscard]] Path normalizedAbsolute(const Path& value) {
    std::error_code error;
    const auto result = std::filesystem::absolute(value, error);
    if (error) return value.lexically_normal();
    return result.lexically_normal();
}

[[nodiscard]] bool samePath(const Path& left, const Path& right) {
    return utils::fncmp(pathToUtf8(normalizedAbsolute(left)),
                        pathToUtf8(normalizedAbsolute(right))) == 0;
}

[[nodiscard]] bool pathWithin(const Path& root, const Path& candidate) {
    return utils::hasPath(normalizedAbsolute(root), normalizedAbsolute(candidate));
}
[[nodiscard]] bool fileNameEquals(StringView requested, StringView actual) {
    return utils::cfnmatch(requested, actual);
}

[[nodiscard]] bool pathExists(const Path& path) noexcept {
    std::error_code error;
    return std::filesystem::exists(path, error) && !error;
}

[[nodiscard]] bool backendAvailable() noexcept {
    // make_archive_backend()は現在失敗しな
    // いが、所有権を受け取る境界で確認し、将来のプラットフォーム
    // バックエンドがCabinet未対応を報告できるようにする。
    try {
        return static_cast<bool>(::cwxeditor::lib::make_archive_backend());
    } catch (...) {
        return false;
    }
}

[[nodiscard]] bool createCabinet(const Path& source, const Path& cabinet,
                                 const IncludeCallback& include,
                                 const ::cwxeditor::lib::Array<MemoryFile>& files) {
    const auto sourcePath = normalizedAbsolute(source);
    const auto cabinetPath = normalizedAbsolute(cabinet);
    std::error_code error;
    const auto sourceStatus = std::filesystem::symlink_status(sourcePath, error);
    if (error || !std::filesystem::exists(sourceStatus) ||
        std::filesystem::is_symlink(sourceStatus)) {
        return false;
    }
    const bool sourceDirectory = std::filesystem::is_directory(sourceStatus);
    if (!sourceDirectory && !std::filesystem::is_regular_file(sourceStatus)) {
        return false;
    }

    // DのFCIの巡回はdirname(source)から始まるため、ディレ
    // クトリ自身の名前がCabinet内の先頭要素になる。共通のアーカイブAPI
    // では、そのdirnameを入力元ルートにすることで同じ巡回を表す。
    const auto sourceRoot = sourcePath.parent_path();
    const bool includeSource = !include || include(pathToUtf8(sourcePath));
    auto backend = ::cwxeditor::lib::make_archive_backend();
    if (!backend) return false;

    ::cwxeditor::lib::ArchiveCreateRequest request;
    request.format = ArchiveFormat::Cab;
    request.source_directory = sourceRoot.empty() ? Path(".") : sourceRoot;
    // FCIはCABの入力に読み取りアクセスだけを必要とする。D
    // のコールバックも同じ契約なので、READONLYファイル
    // も一時コピーなしで直接追加し、CABの属性を保持できる。
    request.archive_path = cabinetPath;
    request.include = [sourcePath, cabinetPath, sourceDirectory, includeSource,
                       include](const Path& candidate) {
        if (!includeSource) return false;
        const auto normalizedCandidate = normalizedAbsolute(candidate);
        if (samePath(normalizedCandidate, cabinetPath)) return false;
        if (sourceDirectory) {
            if (!pathWithin(sourcePath, normalizedCandidate)) return false;
        } else if (!samePath(normalizedCandidate, sourcePath)) {
            return false;
        }
        if (samePath(normalizedCandidate, sourcePath)) return true;
        if (!include) return true;
        return include(pathToUtf8(normalizedCandidate));
    };

    if (!files.empty() && !sourceDirectory) return false;
    for (const auto& file : files) {
        const auto relative = pathFromUtf8(file.path);
        if (relative.empty() || relative.is_absolute()) return false;
        ::cwxeditor::lib::ArchiveMemoryFile memory;
        memory.path = sourcePath.filename() / relative;
        memory.data.reserve(file.data.size());
        for (const auto byte : file.data) memory.data.push_back(static_cast<std::byte>(byte));
        request.memory_files.push_back(std::move(memory));
    }
    String errorMessage;
    return backend->create(request, errorMessage);
}

[[nodiscard]] bool extractCabinet(const Path& cabinet, const Path& destination,
                                  const ExpandCallback& expand) {
    auto backend = ::cwxeditor::lib::make_archive_backend();
    if (!backend) return false;

    ::cwxeditor::lib::ArchiveExtractRequest request;
    request.format = ArchiveFormat::Cab;
    request.archive_path = normalizedAbsolute(cabinet);
    request.destination = normalizedAbsolute(destination);
    request.overwrite = true;
    // DのFDIコールバックの展開分岐は&& falseによって到達不能。このAPIの選択コールバックを
    // 有効にし、summaryOnly/textOnlyの読み込みでCABの内容を絞り込めるようにする。
    request.map_entry = [expand](const ArchiveEntry& entry) {
        if (!expand) return entry.path;
        const auto mapped = expand(pathToUtf8(entry.path));
        if (mapped.empty()) return Path();
        return pathFromUtf8(mapped);
    };

    String errorMessage;
    return backend->extract(request, errorMessage);
}

}  // namespace

bool canUncab() noexcept {
    return backendAvailable();
}

bool cab(StringView source, StringView cabinet, const IncludeCallback& include) {
    return cab(source, cabinet, include, {});
}

bool cab(StringView source, StringView cabinet, const IncludeCallback& include,
         const ::cwxeditor::lib::Array<MemoryFile>& files) {
    if (!canUncab() || source.empty() || cabinet.empty()) return false;
    try {
        return createCabinet(pathFromUtf8(source), pathFromUtf8(cabinet), include, files);
    } catch (...) {
        return false;
    }
}

bool uncab(StringView cabinet, StringView destination, const ExpandCallback& expand) {
    if (!canUncab() || cabinet.empty() || destination.empty()) return false;
    try {
        if (!pathExists(pathFromUtf8(cabinet))) return false;
        return extractCabinet(pathFromUtf8(cabinet), pathFromUtf8(destination), expand);
    } catch (...) {
        return false;
    }
}

String cabHasFile(StringView cabinet, StringView fileName) {
    if (!canUncab() || cabinet.empty() || fileName.empty()) return String{};
    try {
        String result;
        ::cw::lib::archive::visit_cabinet_name_records(
            pathFromUtf8(cabinet), [&](const std::string_view raw, const bool utf8) {
                // Dの.text(char[])はUTFフラグ付きの名前のバイト列を保持する。従
                // 来形式の名前だけを、touniのCP932変換表と制御文字フィルターに通す。
                const String name = utf8 ? String(raw) : sjis::touni(raw);
                String comparable = name;
                // DのCabinet実装はWindowsのbasename
                // 規則を使う。照合時だけ、全ネイティブホストで両方の区切
                // り文字を適用する。返すCFFILEの表記は変えない。
                std::replace(comparable.begin(), comparable.end(), '\\', '/');
                if (!fileNameEquals(fileName, utils::pathBaseName(comparable))) return true;
                result = name;
                return false;
            });
        return result;
    } catch (...) {
        // DのAPIは、存在しない・不正・未対応のCabinetに空
        // の結果を返す。この境界では例外を送出しない契約を維持する。
    }
    return String{};
}

}  // namespace cwxeditor::cwx::cab
