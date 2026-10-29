#include "cwxeditor_src/lib/file_picker.hpp"
#include "cwxeditor_src/lib/file_save_transaction.hpp"
#include "cwxeditor_src/lib/storage.hpp"

#include <algorithm>
#include <atomic>
#include <exception>
#include <fstream>
#include <iterator>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#include <cwchar>
#endif

namespace cwxeditor::lib {
namespace {

// 結果の構築が成功するまで元のパスを借用する。 後処理の設定中にメ
// モリ確保せず、失敗した選択を隠すようなパスのムーブも行わない。
struct ImportPathsGuard {
    DialogBackend& dialogs;
    std::span<const std::filesystem::path> paths;
    bool armed{true};
    ~ImportPathsGuard() {
        if (!armed) return;
        for (const auto& path : paths) {
            try { dialogs.release_import_file(path); } catch (...) {}
        }
    }
};

// 保存先の選択は、文書提供側の一時保存領域の使用権の場合がある。 ト
// ランザクションが引き取るか、有効なパスを呼び出し元へ返すまで中止処
// 理を有効にし、 不正な出力で一時領域が取り残されないようにする。
struct PendingSavePathGuard {
    DialogBackend& dialogs;
    const std::filesystem::path& path;
    bool armed{true};
    ~PendingSavePathGuard() {
        if (!armed) return;
        try {
            std::string ignored;
            (void)dialogs.finalize_save_file(path, false, ignored);
        } catch (...) {}
    }
};

[[nodiscard]] FilePickerCapability capability_for(FilePickerOperation operation) noexcept {
    switch (operation) {
    case FilePickerOperation::OpenFile: return FilePickerCapability::OpenFile;
    case FilePickerOperation::OpenFiles: return FilePickerCapability::OpenFiles;
    case FilePickerOperation::SelectDirectory: return FilePickerCapability::SelectDirectory;
    case FilePickerOperation::SaveFile: return FilePickerCapability::SaveFile;
    }
    return FilePickerCapability::None;
}

[[nodiscard]] bool request_is_valid(const FilePickerRequest& request) noexcept {
    if (request.selected_filter && *request.selected_filter >= request.filters.size()) {
        return false;
    }
    return true;
}

[[nodiscard]] FileDialogRequest make_dialog_request(const FilePickerRequest& request) {
    FileDialogRequest result;
    result.title = request.title;
    result.description = request.description;
    result.initial_directory = request.initial_directory;
    result.initial_name = request.initial_name;
    result.filters = request.filters;
    result.selected_filter = request.selected_filter;
    result.allow_multiple = request.operation == FilePickerOperation::OpenFiles ||
                            request.allow_multiple;
    result.confirm_overwrite = request.confirm_overwrite;
    return result;
}

[[nodiscard]] FilePickerResult unsupported(std::string message) {
    return FilePickerResult::failure(FilePickerError::Unsupported, std::move(message));
}

[[nodiscard]] FilePickerResult invalid_request(std::string message) {
    return FilePickerResult::failure(FilePickerError::InvalidRequest, std::move(message));
}

[[nodiscard]] FilePickerResult backend_failure(std::string message) {
    return FilePickerResult::failure(FilePickerError::BackendFailure, std::move(message));
}

[[nodiscard]] bool is_ascii_space(const char value) noexcept {
    switch (value) {
    case ' ':
    case '\t':
    case '\n':
    case '\r':
    case '\f':
    case '\v': return true;
    default: return false;
    }
}

[[nodiscard]] std::string_view trim_ascii(std::string_view value) noexcept {
    while (!value.empty() && is_ascii_space(value.front())) value.remove_prefix(1);
    while (!value.empty() && is_ascii_space(value.back())) value.remove_suffix(1);
    return value;
}

[[nodiscard]] bool import_path_equal(const std::filesystem::path& left,
                                     const std::filesystem::path& right) {
    const auto normalized_left = left.lexically_normal();
    const auto normalized_right = right.lexically_normal();
#if defined(_WIN32)
    return _wcsicmp(normalized_left.c_str(), normalized_right.c_str()) == 0;
#else
    return normalized_left == normalized_right;
#endif
}

[[nodiscard]] bool is_ascii_extension_character(const char value) noexcept {
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
           (value >= '0' && value <= '9') || value == '_' || value == '-';
}

[[nodiscard]] bool is_dotted_extension(std::string_view value) noexcept {
    if (value.empty()) return false;
    std::size_t segment_begin{};
    while (segment_begin < value.size()) {
        const auto segment_end = value.find('.', segment_begin);
        const auto end = segment_end == std::string_view::npos ? value.size() : segment_end;
        if (end == segment_begin) return false;
        for (std::size_t index = segment_begin; index < end; ++index) {
            if (!is_ascii_extension_character(value[index])) return false;
        }
        if (segment_end == std::string_view::npos) return true;
        segment_begin = segment_end + 1U;
    }
    return false;
}

[[nodiscard]] std::optional<std::string> extension_from_pattern(std::string_view pattern) {
    pattern = trim_ascii(pattern);
    if (pattern.empty() || pattern == "*" || pattern == "*.*" ||
        pattern.find_first_of("?[]\\/;") != std::string_view::npos) {
        return std::nullopt;
    }

    std::string_view extension;
    if (pattern.starts_with("*.")) {
        extension = pattern.substr(2);
    } else if (pattern.starts_with('.')) {
        extension = pattern.substr(1);
    } else {
        // 単独の値は一つの拡張子だけを表す。 ドットがあると、
        // 拡張子の指定ではなく曖昧なファイル名パターンになる。
        if (pattern.find('.') != std::string_view::npos) return std::nullopt;
        extension = pattern;
    }
    if (extension.find('*') != std::string_view::npos || !is_dotted_extension(extension)) {
        return std::nullopt;
    }

    std::string normalized(extension);
    for (auto& character : normalized) {
        if (character >= 'A' && character <= 'Z')
            character = static_cast<char>(character - 'A' + 'a');
    }
    return normalized;
}

[[nodiscard]] std::string exception_message(const std::exception& exception) {
    return exception.what();
}

[[nodiscard]] DialogBackend& require_dialog_backend(
    const std::shared_ptr<DialogBackend>& dialogs) {
    if (!dialogs) throw std::invalid_argument("NativeFilePicker requires a dialog backend");
    return *dialogs;
}

[[nodiscard]] std::string native_name(const std::filesystem::path& path) {
    const auto name = (path.filename().empty() ? path : path.filename()).u8string();
    return {reinterpret_cast<const char*>(name.data()), name.size()};
}

// DialogBackend の空のベクター・optionalはキャンセ
// ルだが、 成功応答の中の空・NULを含むパスは不正な提供結果として扱
// う。 Storage に渡したり、使える選択として公開したりしない。
[[nodiscard]] bool valid_native_dialog_path(const std::filesystem::path& path) {
    if (path.empty()) return false;
    const auto encoded = path.generic_u8string();
    return std::find(encoded.begin(), encoded.end(), u8'\0') == encoded.end();
}

[[nodiscard]] std::filesystem::path safe_leaf_name(std::string name,
                                         std::string fallback = "import.bin") {
    if (name.find('\0') != std::string::npos)
        throw std::invalid_argument("import filename contains NUL");
    // Blobの名前は全OSでUTF-8。ネイティブの名前は上で変換
    // 済み。 POSIXでも両方のスラッシュをパス区切りとして扱う。
    const auto slash = name.find_last_of("/\\");
    auto leaf = slash == std::string::npos ? std::move(name) : name.substr(slash + 1U);
    if (leaf.empty() || leaf == "." || leaf == "..") leaf = std::move(fallback);
    // Blobは一ファイルでも名前にスラッシュを含む場合がある。
    // 保存領域への取り込みAPIは論理的な保存先を受け取るため、
    // その名前によって指定ディレクトリの外へ出ないようにする。
    std::replace(leaf.begin(), leaf.end(), '/', '_');
    std::replace(leaf.begin(), leaf.end(), '\\', '_');
    const auto result = std::filesystem::path(std::u8string(
        reinterpret_cast<const char8_t*>(leaf.data()), leaf.size()));
    if (result.has_root_path())
        throw std::invalid_argument("import filename contains a root");
    return result;
}

[[nodiscard]] std::filesystem::path safe_relative_path(
    const std::filesystem::path& relative) {
    std::filesystem::path result;
    for (const auto& component : relative) {
        if (component.empty() || component == ".") continue;
        if (component == ".." || component.has_root_name() ||
            component.has_root_directory()) {
            throw std::invalid_argument("import path contains a parent or root component");
        }
        result /= safe_leaf_name(native_name(component), "import.bin");
    }
    return result;
}

[[nodiscard]] std::optional<std::vector<std::byte>> read_native_file(
    const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error) return std::nullopt;

    const auto size = std::filesystem::file_size(path, error);
    if (error || size > static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max())) {
        return std::nullopt;
    }

    std::ifstream stream(path, std::ios::binary);
    if (!stream) return std::nullopt;
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty()) {
        stream.read(reinterpret_cast<char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
        if (!stream || stream.gcount() != static_cast<std::streamsize>(bytes.size())) {
            return std::nullopt;
        }
    }
    return bytes;
}

struct ImportItem {
    cardwirth::platform::StoragePath destination;
    std::vector<std::byte> bytes;
};

[[nodiscard]] StorageImportResult import_failure(FilePickerError error,
                                                 std::string message) {
    StorageImportResult result;
    result.status = FilePickerStatus::Error;
    result.error = error;
    result.message = std::move(message);
    return result;
}

[[nodiscard]] StorageImportResult import_cancelled() {
    StorageImportResult result;
    result.status = FilePickerStatus::Cancelled;
    result.error = FilePickerError::None;
    return result;
}

[[nodiscard]] std::optional<ImportItem> make_native_import_item(
    const std::filesystem::path& source,
    const cardwirth::platform::StoragePath& destination,
    bool preserve_directories,
    std::vector<ImportItem>& output,
    std::string& error_message) {
    std::error_code error;
    const auto status = std::filesystem::status(source, error);
    if (error || !std::filesystem::exists(status)) {
        error_message = "selected native path does not exist: " + source.string();
        return std::nullopt;
    }

    if (std::filesystem::is_regular_file(status)) {
        const auto bytes = read_native_file(source);
        if (!bytes) {
            error_message = "unable to read selected file: " + source.string();
            return std::nullopt;
        }
        output.push_back({destination / safe_leaf_name(native_name(source)), *bytes});
        return ImportItem{};
    }

    if (!std::filesystem::is_directory(status)) {
        error_message = "selected native path is not a file or directory: " + source.string();
        return std::nullopt;
    }

    const auto root_name = safe_leaf_name(native_name(source), "import");
    const auto base = preserve_directories ? destination / root_name : destination;
    std::filesystem::recursive_directory_iterator iterator(
        source, std::filesystem::directory_options::skip_permission_denied, error);
    std::filesystem::recursive_directory_iterator end;
    if (error) {
        error_message = "unable to enumerate selected directory: " + source.string();
        return std::nullopt;
    }

    for (; iterator != end; iterator.increment(error)) {
        if (error) {
            error_message = "unable to enumerate selected directory: " + source.string();
            return std::nullopt;
        }
        const auto entry_status = iterator->status(error);
        if (error) {
            error_message = "unable to inspect selected directory entry: " +
                            iterator->path().string();
            return std::nullopt;
        }
        if (!std::filesystem::is_regular_file(entry_status)) continue;

        const auto bytes = read_native_file(iterator->path());
        if (!bytes) {
            error_message = "unable to read selected file: " + iterator->path().string();
            return std::nullopt;
        }
        const auto relative = iterator->path().lexically_relative(source);
        std::filesystem::path safe_relative;
        try {
            safe_relative = safe_relative_path(relative);
        } catch (const std::exception& exception) {
            error_message = exception_message(exception);
            return std::nullopt;
        }
        output.push_back({base / safe_relative, *bytes});
    }

    return ImportItem{};
}

[[nodiscard]] FilePickerResult validate_picker_request(
    const FilePicker& picker,
    const FilePickerRequest& request) {
    if (!request_is_valid(request)) {
        return invalid_request("selected filter index is outside the filter list");
    }
    const auto required = request.importing ? FilePickerCapability::Import
                                            : capability_for(request.operation);
    if (!has_capability(picker.capabilities(), required)) {
        return unsupported("requested file picker capability is unavailable");
    }
    return FilePickerResult::success();
}

}  // namespace

FilePickerPath::FilePickerPath(std::filesystem::path path) : value_(std::move(path)) {}

FilePickerPath::FilePickerPath(WebVirtualPath path) : value_(std::move(path)) {}

FilePickerPath::FilePickerPath(WebBlob blob) : value_(std::move(blob)) {}

FileExtensionFilter make_file_extension_filter(const std::span<const FileFilter> filters) {
    FileExtensionFilter result;
    if (filters.empty()) return result;

    result.unrestricted = false;
    for (const auto& filter : filters) {
        // MIMEだけ、または空のパターン一覧は、情報を失わずに拡張子形式へ変換できない。
        if (filter.patterns.empty()) return FileExtensionFilter{};
        for (const auto& source : filter.patterns) {
            const auto extension = extension_from_pattern(source);
            if (!extension) return FileExtensionFilter{};
            if (std::find(result.extensions.begin(), result.extensions.end(), *extension) ==
                result.extensions.end()) {
                result.extensions.push_back(*extension);
            }
        }
    }
    if (result.extensions.empty()) return FileExtensionFilter{};
    return result;
}

NativeFileDialogPlan plan_native_file_dialog(
    const FilePickerOperation operation, const FileDialogRequest& request) {
    const bool opening = operation == FilePickerOperation::OpenFile ||
                         operation == FilePickerOperation::OpenFiles;
    if (!opening && operation != FilePickerOperation::SaveFile &&
        operation != FilePickerOperation::SelectDirectory)
        throw std::invalid_argument("invalid native file dialog operation");
    if (request.selected_filter && *request.selected_filter >= request.filters.size())
        throw std::invalid_argument("invalid native file dialog filter index");
    const auto check_text = [](const std::string_view text) {
        if (text.find('\0') != std::string_view::npos)
            throw std::invalid_argument("NUL in native file dialog request");
    };
    check_text(request.title);
    check_text(request.description);
    check_text(request.initial_name);
    const auto directory_utf8 = request.initial_directory.u8string();
    check_text(std::string_view(reinterpret_cast<const char*>(directory_utf8.data()),
                                directory_utf8.size()));
    NativeFileDialogPlan plan;
    plan.title = request.title;
    plan.description = request.description;
    plan.allow_multiple = opening &&
        (operation == FilePickerOperation::OpenFiles || request.allow_multiple);
    plan.confirm_overwrite = request.confirm_overwrite;
    auto location = request.initial_directory;
    if (operation != FilePickerOperation::SelectDirectory && !request.initial_name.empty())
        location /= std::filesystem::u8path(request.initial_name);
    const auto location_utf8 = location.u8string();
    plan.initial_location_utf8.assign(location_utf8.begin(), location_utf8.end());
    const auto append_filter = [&](const std::size_t index) {
        const auto& filter = request.filters[index];
        check_text(filter.label);
        for (const auto& pattern : filter.patterns) check_text(pattern);
        for (const auto& mime : filter.mime_types) check_text(mime);
        plan.filters.push_back({index, filter.label.empty() ? "ファイル" : filter.label,
            make_file_extension_filter(std::span<const FileFilter>{&filter, 1U})});
    };
    plan.filters.reserve(request.filters.size());
    if (request.selected_filter) append_filter(*request.selected_filter);
    for (std::size_t index = 0; index < request.filters.size(); ++index)
        if (!request.selected_filter || index != *request.selected_filter) append_filter(index);
    plan.combined_extensions = make_file_extension_filter(request.filters);
    return plan;
}

std::vector<std::filesystem::path> native_file_dialog_paths(
    const NativeFileDialogPlan& plan, const std::span<const std::string> paths_utf8) {
    std::vector<std::filesystem::path> result;
    result.reserve(plan.allow_multiple ? paths_utf8.size() : std::min(paths_utf8.size(), std::size_t{1}));
    for (const auto& text : paths_utf8) {
        if (text.empty() || text.find('\0') != std::string::npos)
            throw std::invalid_argument("invalid native file dialog result path");
        if (plan.allow_multiple || result.empty())
            result.push_back(std::filesystem::u8path(text));
    }
    return result;
}

std::optional<std::size_t> native_file_dialog_source_filter(
    const NativeFileDialogPlan& plan, const int native_filter_index,
    const std::optional<std::size_t> fallback) noexcept {
    if (native_filter_index < 0) return fallback;
    const auto index = static_cast<std::size_t>(native_filter_index);
    return index < plan.filters.size() ? std::optional<std::size_t>{
        plan.filters[index].source_index} : fallback;
}

FilePickerPath FilePickerPath::native(std::filesystem::path path) {
    return FilePickerPath(std::move(path));
}

FilePickerPath FilePickerPath::web_virtual(WebVirtualPath path) {
    return FilePickerPath(std::move(path));
}

FilePickerPath FilePickerPath::web_blob(WebBlob blob) {
    return FilePickerPath(std::move(blob));
}

FilePickerPath::Kind FilePickerPath::kind() const noexcept {
    switch (value_.index()) {
    case 0: return Kind::Native;
    case 1: return Kind::WebVirtual;
    case 2: return Kind::WebBlob;
    default: return Kind::Native;
    }
}

std::optional<std::filesystem::path> FilePickerPath::native_path() const {
    if (const auto* path = std::get_if<std::filesystem::path>(&value_)) return *path;
    return std::nullopt;
}

const WebVirtualPath* FilePickerPath::web_virtual_path() const noexcept {
    return std::get_if<WebVirtualPath>(&value_);
}

const WebBlob* FilePickerPath::blob() const noexcept {
    return std::get_if<WebBlob>(&value_);
}

std::string FilePickerPath::display_name() const {
    if (const auto* path = std::get_if<std::filesystem::path>(&value_)) {
        return native_name(*path);
    }
    if (const auto* virtual_path = std::get_if<WebVirtualPath>(&value_)) {
        return virtual_path->name.empty() ? virtual_path->id : virtual_path->name;
    }
    const auto* blob = std::get_if<WebBlob>(&value_);
    return blob->name;
}

FilePickerResult FilePickerResult::success(std::vector<FilePickerPath> selections,
                                           std::optional<std::size_t> selected_filter) {
    FilePickerResult result;
    result.status = FilePickerStatus::Success;
    result.error = FilePickerError::None;
    result.selections = std::move(selections);
    result.selected_filter = selected_filter;
    return result;
}

FilePickerResult FilePickerResult::cancelled_result(
    std::optional<std::size_t> selected_filter) {
    FilePickerResult result;
    result.status = FilePickerStatus::Cancelled;
    result.error = FilePickerError::None;
    result.selected_filter = selected_filter;
    return result;
}

FilePickerResult FilePickerResult::failure(FilePickerError error, std::string message,
                                            std::optional<std::size_t> selected_filter) {
    FilePickerResult result;
    result.status = FilePickerStatus::Error;
    result.error = error;
    result.message = std::move(message);
    result.selected_filter = selected_filter;
    return result;
}

FilePickerResult FilePicker::open_file(FilePickerRequest request) {
    request.operation = FilePickerOperation::OpenFile;
    request.allow_multiple = false;
    request.importing = false;
    return pick(request);
}

FilePickerResult FilePicker::open_files(FilePickerRequest request) {
    request.operation = FilePickerOperation::OpenFiles;
    request.allow_multiple = true;
    request.importing = false;
    return pick(request);
}

FilePickerResult FilePicker::select_directory(FilePickerRequest request) {
    request.operation = FilePickerOperation::SelectDirectory;
    request.allow_multiple = false;
    request.importing = false;
    return pick(request);
}

FilePickerResult FilePicker::save_file(FilePickerRequest request) {
    request.operation = FilePickerOperation::SaveFile;
    request.allow_multiple = false;
    request.importing = false;
    auto result = pick(request);
    if (!result.selected_filter) result.selected_filter = request.selected_filter;
    return result;
}

FilePickerResult FilePicker::export_file(FilePickerExportRequest request) {
    request.picker.operation = FilePickerOperation::SaveFile;
    request.picker.allow_multiple = false;
    request.picker.importing = false;
    if (request.picker.initial_name.empty()) request.picker.initial_name = request.suggested_name;
    return export_blob(request);
}

FilePickerResult FilePicker::import_files(FilePickerRequest request) {
    request.operation = FilePickerOperation::OpenFiles;
    request.allow_multiple = true;
    request.importing = true;
    return pick(request);
}

bool FilePicker::pick_async(const FilePickerRequest& request,
                            Completion completed) {
    if (!completed) return false;
    FilePickerResult result;
    try {
        result = pick(request);
    } catch (const std::exception& error) {
        result = FilePickerResult::failure(FilePickerError::BackendFailure,
                                           exception_message(error));
    } catch (...) {
        result = FilePickerResult::failure(
            FilePickerError::BackendFailure, "file picker failed");
    }
    completed(std::move(result));
    return true;
}

bool FilePicker::open_file_async(FilePickerRequest request,
                                 Completion completed) {
    request.operation = FilePickerOperation::OpenFile;
    request.allow_multiple = false;
    request.importing = false;
    return pick_async(request, std::move(completed));
}

bool FilePicker::open_files_async(FilePickerRequest request,
                                  Completion completed) {
    request.operation = FilePickerOperation::OpenFiles;
    request.allow_multiple = true;
    request.importing = false;
    return pick_async(request, std::move(completed));
}

bool FilePicker::select_directory_async(FilePickerRequest request,
                                        Completion completed) {
    request.operation = FilePickerOperation::SelectDirectory;
    request.allow_multiple = false;
    request.importing = false;
    return pick_async(request, std::move(completed));
}

bool FilePicker::save_file_async(FilePickerRequest request,
                                 Completion completed) {
    request.operation = FilePickerOperation::SaveFile;
    request.allow_multiple = false;
    request.importing = false;
    return pick_async(request, std::move(completed));
}

NativeFilePicker::NativeFilePicker(DialogBackend& dialogs,
                                    FilePickerCapabilities capabilities)
    : dialogs_(dialogs), capabilities_(capabilities) {}

NativeFilePicker::NativeFilePicker(std::shared_ptr<DialogBackend> dialogs,
                                   FilePickerCapabilities capabilities)
    : owned_dialogs_(std::move(dialogs)), dialogs_(require_dialog_backend(owned_dialogs_)),
      capabilities_(capabilities) {}

void NativeFilePicker::set_document_root(std::filesystem::path root) {
    dialogs_.set_document_root(std::move(root));
}

FilePickerResult NativeFilePicker::pick(const FilePickerRequest& request) {
    if (!request_is_valid(request)) {
        return invalid_request("selected filter index is outside the filter list");
    }
    const auto required = request.importing ? FilePickerCapability::Import
                                            : capability_for(request.operation);
    // 提供側のコールバックが、呼び出し元の画面と、この選択処理
    // を所有する構成を解放する場合がある。 所有するバックエン
    // ドを保持し、メンバーに依存する判断をすべて呼び出し前に済
    // ませる。 下の結果の後処理では this を参照しない。
    const auto retained_dialogs = owned_dialogs_;
    auto& dialogs = retained_dialogs ? *retained_dialogs : dialogs_;
    const auto capabilities = capabilities_;
    if (!has_capability(capabilities, required)) {
        return unsupported("requested native file picker capability is unavailable");
    }

    try {
        const auto dialog_request = make_dialog_request(request);
        switch (request.operation) {
        case FilePickerOperation::OpenFile:
        case FilePickerOperation::OpenFiles: {
            auto paths = dialogs.open_files(dialog_request);
            if (paths.empty()) return FilePickerResult::cancelled_result();
            ImportPathsGuard pending{dialogs, paths};
            if (std::ranges::any_of(paths, [](const auto& path) {
                    return !valid_native_dialog_path(path);
                })) {
                return backend_failure("native file dialog returned an empty or invalid path");
            }
            auto count = paths.size();
            if (request.operation == FilePickerOperation::OpenFile && !request.allow_multiple &&
                paths.size() > 1) {
                count = 1;
            }
            std::vector<FilePickerPath> selections;
            selections.reserve(count);
            for (std::size_t index = 0; index < count; ++index)
                selections.emplace_back(paths[index]);
            auto result = FilePickerResult::success(std::move(selections));
            // 単一選択を無視した提供側が、余分な一時領域を用意している場
            // 合がある。 その後処理の失敗で、選んだ文書を無効にしない。
            for (auto index = count; index < paths.size(); ++index) {
                try { dialogs.release_import_file(paths[index]); } catch (...) {}
            }
            pending.armed = false;
            return result;
        }
        case FilePickerOperation::SelectDirectory: {
            const auto path = dialogs.select_directory(dialog_request);
            if (!path) return FilePickerResult::cancelled_result();
            ImportPathsGuard pending{dialogs, {&*path, 1}};
            if (!valid_native_dialog_path(*path))
                return backend_failure("native directory dialog returned an invalid path");
            auto result = FilePickerResult::success({FilePickerPath(*path)});
            pending.armed = false;
            return result;
        }
        case FilePickerOperation::SaveFile: {
            auto save = dialogs.save_file_result(dialog_request);
            const auto selected_filter = save.selected_filter ? save.selected_filter
                                                               : request.selected_filter;
            // キャンセルは選択の確定ではない。 アダプタ
            // ーがキャンセルとともに古いネイティブの位
            // 置を返しても、要求の初期形式を維持する。
            if (!save.path) return FilePickerResult::cancelled_result(request.selected_filter);
            PendingSavePathGuard pending{dialogs, *save.path};
            if (!valid_native_dialog_path(*save.path)) {
                auto result = backend_failure("native save dialog returned an invalid path");
                result.selected_filter = selected_filter;
                return result;
            }
            auto result = FilePickerResult::success({FilePickerPath(*save.path)}, selected_filter);
            pending.armed = false;
            return result;
        }
        }
    } catch (const std::exception& exception) {
        return backend_failure(exception_message(exception));
    } catch (...) {
        return backend_failure("native file dialog failed with an unknown exception");
    }
    return invalid_request("unknown file picker operation");
}

bool NativeFilePicker::pick_async(const FilePickerRequest& request,
                                  Completion completed) {
    if (!completed) return false;
    const auto completed_once = std::make_shared<std::atomic_bool>(false);
    const auto finish = [completed_once,
                         completion = std::make_shared<Completion>(
                             std::move(completed))](FilePickerResult result) {
        if (completed_once->exchange(true, std::memory_order_acq_rel)) return;
        try {
            if (*completion) (*completion)(std::move(result));
        } catch (...) {
            // 提供側の完了処理はUIのイベント処理から呼ばれる。 アプリのコール
            // バックの例外を、プラットフォームのコールバック境界の外へ出さない。
        }
    };
    if (!request_is_valid(request)) {
        finish(invalid_request("selected filter index is outside the filter list"));
        return true;
    }
    const auto required = request.importing ? FilePickerCapability::Import
                                            : capability_for(request.operation);
    const auto retained_dialogs = owned_dialogs_;
    auto& dialogs = retained_dialogs ? *retained_dialogs : dialogs_;
    const auto capabilities = capabilities_;
    if (!has_capability(capabilities, required)) {
        finish(unsupported("requested native file picker capability is unavailable"));
        return true;
    }

    if (request.operation == FilePickerOperation::SaveFile) {
        const auto dialog_request = make_dialog_request(request);
        auto* const dialog_service = &dialogs;
        try {
            return dialogs.save_file_result_async(
                dialog_request,
                [retained_dialogs, dialog_service, request, finish](
                    FileDialogSaveResult save) mutable {
                    auto& current_dialogs = retained_dialogs
                        ? *retained_dialogs : *dialog_service;
                    const auto selected_filter = save.selected_filter
                        ? save.selected_filter : request.selected_filter;
                    if (!save.path) {
                        finish(FilePickerResult::cancelled_result(
                            request.selected_filter));
                        return;
                    }
                    try {
                        PendingSavePathGuard pending{current_dialogs, *save.path};
                        if (!valid_native_dialog_path(*save.path)) {
                            auto result = backend_failure(
                                "native save dialog returned an invalid path");
                            result.selected_filter = selected_filter;
                            finish(std::move(result));
                            return;
                        }
                        auto result = FilePickerResult::success(
                            {FilePickerPath(*save.path)}, selected_filter);
                        pending.armed = false;
                        finish(std::move(result));
                    } catch (const std::exception& error) {
                        finish(backend_failure(exception_message(error)));
                    } catch (...) {
                        finish(backend_failure(
                            "native save dialog failed with an unknown exception"));
                    }
                });
        } catch (const std::exception& error) {
            finish(backend_failure(exception_message(error)));
            return true;
        } catch (...) {
            finish(backend_failure(
                "native save dialog failed with an unknown exception"));
            return true;
        }
    }

    if (request.operation != FilePickerOperation::OpenFile &&
        request.operation != FilePickerOperation::OpenFiles &&
        request.operation != FilePickerOperation::SelectDirectory)
        return FilePicker::pick_async(request, [finish](FilePickerResult result) {
            finish(std::move(result));
        });

    const auto dialog_request = make_dialog_request(request);
    auto* const dialog_service = &dialogs;
    try {
        if (request.operation == FilePickerOperation::SelectDirectory) {
            return dialogs.select_directory_async(
                dialog_request,
                [retained_dialogs, dialog_service, finish](
                    std::optional<std::filesystem::path> path) mutable {
                    auto& current_dialogs = retained_dialogs
                        ? *retained_dialogs : *dialog_service;
                    if (!path) {
                        finish(FilePickerResult::cancelled_result());
                        return;
                    }
                    try {
                        ImportPathsGuard pending{current_dialogs, {&*path, 1}};
                        if (!valid_native_dialog_path(*path)) {
                            finish(backend_failure(
                                "native directory dialog returned an invalid path"));
                            return;
                        }
                        auto result = FilePickerResult::success(
                            {FilePickerPath(*path)});
                        pending.armed = false;
                        finish(std::move(result));
                    } catch (const std::exception& error) {
                        finish(backend_failure(exception_message(error)));
                    } catch (...) {
                        finish(backend_failure(
                            "native directory dialog failed with an unknown exception"));
                    }
                });
        }

        return dialogs.open_files_async(
            dialog_request,
            [retained_dialogs, dialog_service, request, finish](
                std::vector<std::filesystem::path> paths) mutable {
                auto& current_dialogs = retained_dialogs
                    ? *retained_dialogs : *dialog_service;
                if (paths.empty()) {
                    finish(FilePickerResult::cancelled_result());
                    return;
                }
                try {
                    ImportPathsGuard pending{current_dialogs, paths};
                    if (std::ranges::any_of(paths, [](const auto& path) {
                            return !valid_native_dialog_path(path);
                        })) {
                        finish(backend_failure(
                            "native file dialog returned an empty or invalid path"));
                        return;
                    }
                    auto count = paths.size();
                    if (request.operation == FilePickerOperation::OpenFile &&
                        !request.allow_multiple && paths.size() > 1)
                        count = 1;
                    std::vector<FilePickerPath> selections;
                    selections.reserve(count);
                    for (std::size_t index = 0; index < count; ++index)
                        selections.emplace_back(paths[index]);
                    auto result = FilePickerResult::success(std::move(selections));
                    for (auto index = count; index < paths.size(); ++index) {
                        try { current_dialogs.release_import_file(paths[index]); }
                        catch (...) {}
                    }
                    pending.armed = false;
                    finish(std::move(result));
                } catch (const std::exception& error) {
                    finish(backend_failure(exception_message(error)));
                } catch (...) {
                    finish(backend_failure(
                        "native file dialog failed with an unknown exception"));
                }
            });
    } catch (const std::exception& error) {
        finish(backend_failure(exception_message(error)));
        return true;
    } catch (...) {
        finish(backend_failure("native file dialog failed with an unknown exception"));
        return true;
    }
}

void NativeFilePicker::release_imports(const FilePickerResult& selection) noexcept {
    for (const auto& item : selection.selections) {
        if (const auto* path = std::get_if<std::filesystem::path>(&item.value())) {
            try { dialogs_.release_import_file(*path); } catch (...) {}
        }
    }
}

FilePickerResult NativeFilePicker::export_blob(const FilePickerExportRequest& request) {
    // ネイティブの選択画面の処理が、このファサードを破棄したり呼び出し
    // 元の要求を変更したりする場合がある。 その後はメンバー・参照を使
    // わず、開く前に所有する提供側と出力に使う全バイト列を保持する。
    const auto retained_dialogs = owned_dialogs_;
    auto& dialogs = retained_dialogs ? *retained_dialogs : dialogs_;
    if (!has_capability(capabilities_, FilePickerCapability::Export)) {
        return unsupported("native export capability is unavailable");
    }
    if (!request_is_valid(request.picker)) {
        return invalid_request("selected filter index is outside the filter list");
    }

    try {
        auto snapshot = request;
        auto picker_request = snapshot.picker;
        picker_request.operation = FilePickerOperation::SaveFile;
        picker_request.allow_multiple = false;
        if (picker_request.initial_name.empty()) picker_request.initial_name = snapshot.suggested_name;
        const auto save = dialogs.save_file_result(make_dialog_request(picker_request));
        const auto selected_filter = save.selected_filter ? save.selected_filter
                                                           : picker_request.selected_filter;
        if (!save.path) return FilePickerResult::cancelled_result(picker_request.selected_filter);
        PendingSavePathGuard pending{dialogs, *save.path};
        if (!valid_native_dialog_path(*save.path)) {
            auto result = backend_failure("native export dialog returned an empty or invalid path");
            result.selected_filter = selected_filter;
            return result;
        }

        FileSaveTransaction finalize(dialogs, *save.path);
        pending.armed = false;
        // 結果を準備する際の確保失敗は、ファイルの変更前に発生させる
        // 。 その場合も、選択された提供側の一時保存領域を解放する。
        auto result = FilePickerResult::success({FilePickerPath(*save.path)}, selected_filter);
        std::string write_error;
        if (!storage_api::write_native_file_atomic(*save.path, snapshot.bytes, write_error, false))
            return FilePickerResult::failure(FilePickerError::WriteFailure, std::move(write_error), selected_filter);
        std::string finalize_error;
        if (!finalize.commit(finalize_error)) {
            return FilePickerResult::failure(
                FilePickerError::WriteFailure,
                finalize_error.empty() ? "unable to publish export destination"
                                       : std::move(finalize_error), selected_filter);
        }
        return result;
    } catch (const std::exception& exception) {
        return FilePickerResult::failure(FilePickerError::WriteFailure,
                                         exception_message(exception));
    } catch (...) {
        return FilePickerResult::failure(FilePickerError::WriteFailure,
                                         "native export failed with an unknown exception");
    }
}

WebFilePicker::WebFilePicker(WebFilePickerCallbacks callbacks,
                             FilePickerCapabilities capabilities)
    : callbacks_(std::move(callbacks)), capabilities_(capabilities) {
    if (capabilities_ == FilePickerCapability::None) {
        if (callbacks_.pick) {
            capabilities_ |= FilePickerCapability::OpenFile |
                             FilePickerCapability::OpenFiles |
                             FilePickerCapability::SelectDirectory |
                             FilePickerCapability::Import;
        }
        if (callbacks_.export_blob) {
            capabilities_ |= FilePickerCapability::SaveFile |
                             FilePickerCapability::Export;
        }
    }
}

FilePickerResult WebFilePicker::pick(const FilePickerRequest& request) {
    if (!request_is_valid(request)) {
        return invalid_request("selected filter index is outside the filter list");
    }
    if (request.operation == FilePickerOperation::SaveFile) {
        return invalid_request("web save dialogs use export_blob so a Blob can be supplied");
    }
    const auto required = request.importing ? FilePickerCapability::Import
                                            : capability_for(request.operation);
    const auto capabilities = capabilities_;
    const auto pick_callback = callbacks_.pick;
    if (!has_capability(capabilities, required)) {
        return unsupported("requested web file picker capability is unavailable");
    }
    if (!pick_callback) return unsupported("web file picker import callback is not installed");

    try {
        auto result = pick_callback(request);
        if (result.succeeded()) {
            for (const auto& selection : result.selections) {
                if (selection.is_native()) {
                    return backend_failure("web picker callback returned an OS path");
                }
            }
        }
        return result;
    } catch (const std::exception& exception) {
        return backend_failure(exception_message(exception));
    } catch (...) {
        return backend_failure("web file picker callback failed with an unknown exception");
    }
}

FilePickerResult WebFilePicker::export_blob(const FilePickerExportRequest& request) {
    const auto capabilities = capabilities_;
    const auto export_callback = callbacks_.export_blob;
    if (!has_capability(capabilities, FilePickerCapability::Export)) {
        return unsupported("web export capability is unavailable");
    }
    if (!export_callback) {
        return unsupported("web file picker export callback is not installed");
    }
    if (!request_is_valid(request.picker)) {
        return invalid_request("selected filter index is outside the filter list");
    }

    try {
        auto normalized = request;
        normalized.picker.operation = FilePickerOperation::SaveFile;
        normalized.picker.allow_multiple = false;
        if (normalized.suggested_name.empty()) {
            normalized.suggested_name = normalized.picker.initial_name;
        }
        if (normalized.suggested_name.empty()) normalized.suggested_name = "download.bin";
        if (normalized.picker.initial_name.empty()) {
            normalized.picker.initial_name = normalized.suggested_name;
        }

        auto result = export_callback(normalized);
        if (result.succeeded()) {
            if (!result.exported_blob) {
                WebBlob blob;
                blob.name = normalized.suggested_name;
                blob.mime_type = normalized.mime_type;
                blob.bytes = normalized.bytes;
                result.exported_blob = blob;
            }
            if (result.selections.empty() && result.exported_blob) {
                result.selections.emplace_back(*result.exported_blob);
            }
        }
        return result;
    } catch (const std::exception& exception) {
        return backend_failure(exception_message(exception));
    } catch (...) {
        return backend_failure("web export callback failed with an unknown exception");
    }
}

StorageImportResult import_to_storage(const FilePickerResult& selection,
                                      cardwirth::platform::Storage& storage,
                                      const StorageImportRequest& request) {
    if (selection.cancelled()) return import_cancelled();
    if (selection.failed()) return import_failure(selection.error, selection.message);
    if (selection.selections.empty()) {
        return import_failure(FilePickerError::InvalidRequest,
                              "file picker returned success without a selection");
    }

    try {
        // Storage・提供側のコールバックを呼ぶ前に、方針を所有して保持する。
        const auto options = request;
        std::vector<ImportItem> items;
        for (const auto& selected : selection.selections) {
            if (selected.is_web_blob()) {
                const auto* blob = selected.blob();
                if (!blob) {
                    return import_failure(FilePickerError::InvalidRequest,
                                          "web Blob selection is empty");
                }
                items.push_back({options.destination_directory /
                                     safe_leaf_name(blob->name),
                                 blob->bytes});
                continue;
            }
            if (selected.is_web_virtual()) {
                return import_failure(
                    FilePickerError::Unsupported,
                    "a WebVirtualPath must be materialized as Blob(s) before Storage import");
            }

            const auto source = selected.native_path();
            if (!source) {
                return import_failure(FilePickerError::InvalidRequest,
                                      "file picker returned an invalid path value");
            }
            std::string error_message;
            if (!make_native_import_item(*source, options.destination_directory,
                                         options.preserve_directories, items,
                                         error_message)) {
                return import_failure(FilePickerError::ReadFailure, std::move(error_message));
            }
        }

        const auto exists = [&storage](const cardwirth::platform::StoragePath& path) {
            const bool present = storage.exists(path);
            if (!present) {
                const auto diagnostic = storage.last_error();
                if (!diagnostic.empty()) throw std::runtime_error(diagnostic);
            }
            return present;
        };
        for (std::size_t index = 0; index < items.size(); ++index) {
            auto& item = items[index];
            item.destination = item.destination.lexically_normal();
            if (options.resolve_destination) {
                const auto original = item.destination;
                const auto allowed = [&original](const cardwirth::platform::StoragePath& path) {
                    if (path.empty() || path.filename().empty() || path.filename() == "." ||
                        path.filename() == ".." || path.native().find(
                            std::filesystem::path::value_type{}) != std::filesystem::path::string_type::npos)
                        return false;
                    return import_path_equal(path.lexically_normal().parent_path(),
                                             original.parent_path());
                };
                const StorageImportRequest::Available available = [&](const auto& candidate) {
                    if (!allowed(candidate)) return false;
                    const auto normalized = candidate.lexically_normal();
                    for (std::size_t prior = 0; prior < index; ++prior)
                        if (import_path_equal(items[prior].destination, normalized)) return false;
                    return !exists(normalized);
                };
                auto resolved = options.resolve_destination(original, available);
                if (!allowed(resolved))
                    return import_failure(FilePickerError::InvalidRequest,
                                          "import naming policy changed the destination directory");
                item.destination = resolved.lexically_normal();
            }
            if (!options.overwrite && exists(item.destination)) {
                return import_failure(FilePickerError::StorageFailure,
                                      "destination already exists: " +
                                          item.destination.string());
            }
            const auto duplicate = std::find_if(
                items.begin(), items.begin() + static_cast<std::ptrdiff_t>(index),
                [&item](const ImportItem& previous) {
                    return import_path_equal(previous.destination, item.destination);
                });
            if (duplicate != items.begin() + static_cast<std::ptrdiff_t>(index)) {
                return import_failure(FilePickerError::StorageFailure,
                                      "multiple selected files map to the same destination: " +
                                          item.destination.string());
            }
        }

        struct Snapshot {
            cardwirth::platform::StoragePath destination;
            bool existed{};
            std::vector<std::byte> bytes;
        };
        std::vector<Snapshot> snapshots;
        snapshots.reserve(items.size());
        for (const auto& item : items) {
            Snapshot snapshot;
            snapshot.destination = item.destination;
            snapshot.existed = exists(item.destination);
            if (snapshot.existed) {
                const auto previous = storage.read(item.destination);
                if (!previous) {
                    const auto diagnostic = storage.last_error();
                    return import_failure(
                        FilePickerError::StorageFailure,
                        diagnostic.empty() ? "unable to snapshot import destination: " +
                                                 item.destination.string()
                                           : diagnostic);
                }
                snapshot.bytes = *previous;
            }
            snapshots.push_back(std::move(snapshot));
        }

        StorageImportResult result;
        result.status = FilePickerStatus::Success;
        result.error = FilePickerError::None;
        // ファイルを変える前に成功結果を構築する。 特に
        // 、結果へのパスの複製が確保に失敗しても、書き込
        // み済みの前半部分が取り残されないようにする。
        result.imported_paths.reserve(items.size());
        for (const auto& item : items)
            result.imported_paths.push_back(item.destination);

        struct Rollback {
            cardwirth::platform::Storage& storage;
            const std::vector<Snapshot>& snapshots;
            std::size_t completed{};
            std::size_t failures{};
            void restore() noexcept {
                while (completed != 0U) {
                    const auto& snapshot = snapshots[--completed];
                    try {
                        const bool restored = snapshot.existed
                            ? storage.write_atomic(snapshot.destination, snapshot.bytes, true)
                            : storage.remove(snapshot.destination, false);
                        if (!restored) ++failures;
                    } catch (...) {
                        // 不正な提供側が、他のファイルの復元を妨げたり、元
                        // の書き込みエラーを置き換えたりしないようにする。
                        ++failures;
                    }
                }
            }
            ~Rollback() { restore(); }
        } rollback{storage, snapshots};
        std::exception_ptr failure;
        try {
            for (const auto& item : items) {
                // write_atomic は、保存先の置換
                // 後に永続化の失敗を返す場合がある。 呼び出
                // し前に現在の項目も復元対象へ加え、 置換前
                // の通常の失敗だけでなく、結果が不確かな
                // false でもスナップショットを戻す。
                ++rollback.completed;
                if (!storage.write_atomic(item.destination, item.bytes, true)) {
                    std::string diagnostic;
                    // last_error は補助情報であり、それ自体も失敗し得
                    // る。 その場合も書き込み失敗を主なエラーとして維持する。
                    try { diagnostic = storage.last_error(); } catch (...) {}
                    throw std::runtime_error(diagnostic.empty()
                        ? "unable to write imported file: " + item.destination.string()
                        : diagnostic);
                }
            }
        } catch (...) {
            failure = std::current_exception();
        }
        if (failure) {
            rollback.restore();
            try { std::rethrow_exception(failure); }
            catch (const std::exception& exception) {
                result = import_failure(FilePickerError::StorageFailure,
                                        exception_message(exception));
            } catch (...) {
                result = import_failure(FilePickerError::StorageFailure,
                                        "Storage import failed with an unknown exception");
            }
            if (rollback.failures != 0U)
                result.message += "; import rollback failed for " +
                    std::to_string(rollback.failures) + " destination(s)";
            return result;
        }
        rollback.completed = 0U;
        return result;
    } catch (const std::exception& exception) {
        return import_failure(FilePickerError::StorageFailure,
                              exception_message(exception));
    } catch (...) {
        return import_failure(FilePickerError::StorageFailure,
                              "Storage import failed with an unknown exception");
    }
}

StorageImportResult pick_and_import_to_storage(
    FilePicker& picker,
    cardwirth::platform::Storage& storage,
    FilePickerRequest picker_request,
    StorageImportRequest storage_request) {
    const auto selection = picker.import_files(std::move(picker_request));
    struct Completion {
        FilePicker& picker;
        const FilePickerResult& selection;
        ~Completion() { picker.release_imports(selection); }
    } completion{picker, selection};
    return import_to_storage(selection, storage, storage_request);
}

}  // namespace cwxeditor::lib
