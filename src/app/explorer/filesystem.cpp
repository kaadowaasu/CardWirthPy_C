#include "filesystem.hpp"

#include "archive.hpp"
#include "platform/app_runtime_context.hpp"
#include "platform/storage/storage.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cwctype>
#include <cstring>
#include <fstream>
#include <limits>
#include <new>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <system_error>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace cardwirth::explorer {

bool replace_file_atomically(const std::filesystem::path& temporary,
                             const std::filesystem::path& destination,
                             std::error_code& error) noexcept {
    error.clear();
    try {
        if (temporary.empty() || destination.empty() ||
            temporary.parent_path().lexically_normal() !=
                destination.parent_path().lexically_normal()) {
            error = std::make_error_code(std::errc::invalid_argument);
            return false;
        }
#if defined(_WIN32)
        if (MoveFileExW(temporary.c_str(), destination.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            return true;
        error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
#else
        std::filesystem::rename(temporary, destination, error);
#endif
        return !error;
    } catch (const std::bad_alloc&) {
        error = std::make_error_code(std::errc::not_enough_memory);
        return false;
    } catch (...) {
        error = std::make_error_code(std::errc::io_error);
        return false;
    }
}

namespace {

std::string path_text(const std::filesystem::path& path) {
    const auto encoded = path.u8string();
    return std::string(reinterpret_cast<const char*>(encoded.data()), encoded.size());
}

std::string generic_path_text(const std::filesystem::path& path) {
    const auto encoded = path.generic_u8string();
    return std::string(reinterpret_cast<const char*>(encoded.data()), encoded.size());
}

constexpr char kTrashIndexName[] = ".cardwirth-trash-index";

struct TrashRecord {
    std::string trashed_path;
    std::string original_path;
};

bool is_trash_index_name(const std::filesystem::path& path) {
    const auto name = path_text(path.filename());
    return name == kTrashIndexName || name.starts_with(std::string(kTrashIndexName) + ".tmp-");
}

void write_index_string(std::ostream& output, const std::string& value) {
    output << value.size() << '\n';
    output.write(value.data(), static_cast<std::streamsize>(value.size()));
    output.put('\n');
    if (!output) throw std::runtime_error("trashの復元情報を書き込めません");
}

bool read_index_string(std::istream& input, std::string& value) {
    constexpr std::size_t kMaxIndexField = 1024 * 1024;
    std::string length_text;
    if (!std::getline(input, length_text)) {
        if (input.eof() && length_text.empty()) return false;
        throw std::runtime_error("trashの復元情報が壊れています");
    }
    std::size_t length{};
    try {
        std::size_t consumed{};
        const auto parsed = std::stoull(length_text, &consumed);
        if (consumed != length_text.size()) throw std::invalid_argument("length");
        if (parsed > (std::numeric_limits<std::size_t>::max)() || parsed > kMaxIndexField)
            throw std::out_of_range("length");
        length = static_cast<std::size_t>(parsed);
    } catch (...) {
        throw std::runtime_error("trashの復元情報が壊れています");
    }
    value.resize(length);
    input.read(value.data(), static_cast<std::streamsize>(length));
    if (input.gcount() != static_cast<std::streamsize>(length) || input.get() != '\n')
        throw std::runtime_error("trashの復元情報が壊れています");
    return true;
}

bool path_component_equal(const std::filesystem::path& left,
                          const std::filesystem::path& right) {
#if defined(_WIN32)
    auto fold = [](std::wstring value) {
        std::transform(value.begin(), value.end(), value.begin(), [](wchar_t value) {
            return static_cast<wchar_t>(std::towlower(value));
        });
        return value;
    };
    return fold(left.native()) == fold(right.native());
#else
    return left == right;
#endif
}

bool path_begins_with(const std::filesystem::path& path,
                      const std::filesystem::path& parent) {
    auto child = path.begin();
    for (auto current = parent.begin(); current != parent.end(); ++current, ++child) {
        if (child == path.end() || !path_component_equal(*child, *current)) return false;
    }
    return true;
}

// FileSystem::rename_entryに渡す、ユーザーが入力した項目名を検証する。
void require_valid_entry_name(const std::filesystem::path& new_name) {
    const auto text = generic_path_text(new_name);
    if (text.empty()) throw std::invalid_argument("名前を入力してください");
    if (text == "." || text == ".." ||
        text.find_first_of("/\\:*?\"<>|") != std::string::npos ||
        text.back() == ' ' || text.back() == '.')
        throw std::invalid_argument("この名前は使用できません");
    if (is_trash_index_name(new_name)) throw std::invalid_argument("この名前は使用できません");
}

std::filesystem::path clean_relative(const std::filesystem::path& value) {
    if (value.empty() || value == ".") return {};
    if (value.is_absolute() || value.has_root_name() || value.has_root_directory())
        throw std::invalid_argument("絶対パスはExplorer APIへ渡せません");
    const auto normalized = value.lexically_normal();
    for (const auto& component : normalized) {
        if (component == "..") throw std::invalid_argument("親ディレクトリ参照は使用できません");
    }
    return normalized == "." ? std::filesystem::path{} : normalized;
}

std::filesystem::path archive_destination_name(const std::filesystem::path& source) {
    auto name = source.stem();
    if (name.empty() || name == "." || name == "..") name = "extracted";
    return name;
}

std::filesystem::path normalize_root(std::filesystem::path value) {
    value = std::filesystem::absolute(std::move(value)).lexically_normal();
    // SDL_GetBasePath()は末尾に区切り文字を付けて
    // 返す。std::filesystemではこれが 空のパス要素
    // になり、ルートと、そのルートに空の相対パスを結合したものが一
    // 致しない場合がある。 ここでは実体のあるパス要素だけを残す。
    while (value.has_relative_path() && value.filename().empty())
        value = value.parent_path();
    return value;
}

bool contains_reparse_or_symlink(const std::filesystem::path& path) {
    const auto unsafe = [](const std::filesystem::path& candidate) {
        std::error_code status_error;
        if (std::filesystem::is_symlink(std::filesystem::symlink_status(candidate, status_error)))
            return true;
#if defined(_WIN32)
        const auto attributes = GetFileAttributesW(candidate.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES &&
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
        return false;
#endif
    };
    if (unsafe(path)) return true;
    std::error_code error;
    if (!std::filesystem::is_directory(path, error) || error) return false;
    for (std::filesystem::recursive_directory_iterator it(
             path, std::filesystem::directory_options::skip_permission_denied, error), end;
         it != end; it.increment(error)) {
        if (error) throw std::runtime_error("trash内を安全確認できません: " + error.message());
        if (unsafe(it->path())) return true;
    }
    if (error) throw std::runtime_error("trash内を安全確認できません: " + error.message());
    return false;
}

class LocalFileSystem final : public FileSystem {
public:
    LocalFileSystem(std::filesystem::path root, std::string trash_name)
        : root_(normalize_root(std::move(root))),
          trash_relative_(clean_relative(std::filesystem::u8path(trash_name))) {
        if (trash_relative_.empty() || trash_relative_.has_parent_path())
            throw std::invalid_argument("trash名は単一のファイル名で指定してください");
        std::error_code error;
        std::filesystem::create_directories(root_, error);
        if (error) throw std::runtime_error("Explorer rootを作成できません: " + path_text(root_));
        root_canonical_ = normalize_root(std::filesystem::weakly_canonical(root_, error));
        if (error) throw std::runtime_error("Explorer rootを解決できません: " + path_text(root_));
        std::filesystem::create_directories(root_ / trash_relative_, error);
        if (error) throw std::runtime_error("trashを作成できません: " + path_text(root_ / trash_relative_));
        (void)resolve(trash_relative_, true);
    }

    [[nodiscard]] std::filesystem::path root() const override { return root_; }
    void sync() override {
        auto storage = cardwirth::platform::make_native_storage(root_, false);
        if (!storage->sync()) throw std::runtime_error(storage->last_error());
    }
    [[nodiscard]] std::filesystem::path trash() const override { return trash_relative_; }
    [[nodiscard]] std::filesystem::path absolute(
        const std::filesystem::path& relative) const override {
        return resolve(relative, true);
    }

    [[nodiscard]] bool is_in_trash(const std::filesystem::path& relative) const override {
        const auto clean = clean_relative(relative);
        return !clean.empty() && path_begins_with(clean, trash_relative_);
    }

    [[nodiscard]] std::vector<FileEntry> list(
        const std::filesystem::path& directory) const override {
        const auto clean = clean_relative(directory);
        const auto source = resolve(clean, true);
        std::error_code error;
        if (!std::filesystem::is_directory(source, error) || error)
            throw std::runtime_error("フォルダを開けません: " + path_text(clean));

        std::vector<FileEntry> result;
        for (std::filesystem::directory_iterator it(
                 source, std::filesystem::directory_options::skip_permission_denied, error), end;
             it != end; it.increment(error)) {
            if (error) throw std::runtime_error("フォルダ一覧を取得できません: " + path_text(clean));
            // ごみ箱には専用の固定ナビゲーション項目があるため、 実体のデ
            // ィレクトリをアプリのルートの通常の子として重複表示しない。
            if (clean.empty() && it->path().filename() == trash_relative_) continue;
            // 索引は内部の管理情報であり、ユーザーの項目ではな
            // い。Explorerから 移動・削除できないよう
            // 、一覧には表示しない。不可分な更新の途中で中断
            // した場合に備え、 一時的な索引ファイルも隠す。
            if (clean == trash_relative_ && is_trash_index_name(it->path())) continue;
            const auto status = it->symlink_status(error);
            if (error) continue;
            FileEntry entry;
            entry.path = clean / it->path().filename();
            entry.name = path_text(it->path().filename());
            entry.in_trash = is_in_trash(entry.path);
            if (std::filesystem::is_symlink(status)) entry.type = EntryType::Unsupported;
            else if (std::filesystem::is_directory(status)) entry.type = EntryType::Directory;
            else if (std::filesystem::is_regular_file(status)) {
                entry.type = EntryType::File;
                entry.size = it->file_size(error);
                if (error) entry.size = 0;
            }
            error.clear();
            if (entry.type != EntryType::Unsupported) {
                const auto modified = it->last_write_time(error);
                if (!error) entry.modified = modified;
            }
            error.clear();
            result.push_back(std::move(entry));
        }
        if (error) throw std::runtime_error("フォルダ一覧を取得できません: " + path_text(clean));
        std::sort(result.begin(), result.end(), [](const FileEntry& left, const FileEntry& right) {
            if (left.type != right.type) return left.type == EntryType::Directory;
            auto fold = [](std::string value) {
                std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
                    return static_cast<char>(std::tolower(character));
                });
                return value;
            };
            return fold(left.name) < fold(right.name);
        });
        return result;
    }

    std::filesystem::path extract_archive(
        const ArchiveRegistry& archives,
        const std::filesystem::path& archive,
        const std::filesystem::path& destination_directory) override {
        const auto archive_relative = clean_relative(archive);
        const auto parent_relative = clean_relative(destination_directory);
        const auto source = resolve(archive_relative, true);
        const auto parent = resolve(parent_relative, true);
        std::error_code error;
        if (!std::filesystem::is_regular_file(source, error) || error)
            throw std::runtime_error("展開元が通常のファイルではありません: " +
                                     path_text(archive_relative));
        if (!std::filesystem::is_directory(parent, error) || error)
            throw std::runtime_error("展開先がフォルダではありません: " +
                                     path_text(parent_relative));

        const auto destination = unique_archive_child(
            parent, archive_destination_name(source));
        archives.extract(source, destination);
        const auto relative = destination.lexically_relative(root_);
        if (relative.empty() || relative.is_absolute())
            throw std::runtime_error("展開結果の相対パスを作成できません");
        return relative;
    }

    std::filesystem::path copy_to(
        const std::filesystem::path& source,
        const std::filesystem::path& destination_directory) override {
        const auto source_relative = clean_relative(source);
        if (source_relative.empty()) throw std::invalid_argument("Explorer root自体はコピーできません");
        const auto destination_relative = clean_relative(destination_directory);
        const auto source_absolute = resolve(source_relative, true);
        const auto destination_absolute = resolve(destination_relative, true);
        std::error_code error;
        if (!std::filesystem::is_directory(destination_absolute, error) || error)
            throw std::runtime_error("コピー先がフォルダではありません");
        const auto status = std::filesystem::symlink_status(source_absolute, error);
        if (error || std::filesystem::is_symlink(status) ||
            (!std::filesystem::is_regular_file(status) && !std::filesystem::is_directory(status)))
            throw std::runtime_error("この項目は安全にコピーできません");
        if (std::filesystem::is_directory(status) &&
            path_begins_with(destination_absolute, source_absolute))
            throw std::runtime_error("フォルダ自身の内側へはコピーできません");

        const auto final_path = unique_child(destination_absolute, source_absolute.filename());
        const auto temporary = unique_child(destination_absolute,
            std::filesystem::path(".cw-copy-tmp-" + unique_suffix()));
        try {
            copy_tree(source_absolute, temporary, status);
            std::filesystem::rename(temporary, final_path, error);
            if (error) throw std::runtime_error("コピー結果を確定できません");
        } catch (...) {
            std::filesystem::remove_all(temporary, error);
            throw;
        }
        return std::filesystem::relative(final_path, root_, error);
    }

    std::filesystem::path copy_from_external(
        const std::filesystem::path& source_absolute,
        const std::filesystem::path& destination_directory,
        bool overwrite) override {
        if (!source_absolute.is_absolute())
            throw std::invalid_argument("アップロード元は絶対パスで指定してください");
        const auto source = normalize_root(source_absolute);
        const auto destination = resolve(clean_relative(destination_directory), true);
        std::error_code error;
        if (!std::filesystem::is_directory(destination, error) || error)
            throw std::runtime_error("アップロード先がフォルダではありません");
        const auto status = std::filesystem::symlink_status(source, error);
        if (error || std::filesystem::is_symlink(status) ||
            (!std::filesystem::is_regular_file(status) && !std::filesystem::is_directory(status)))
            throw std::runtime_error("この項目は安全にアップロードできません");
        if (contains_reparse_or_symlink(source))
            throw std::runtime_error("symlinkまたはreparse pointを含む項目はアップロードできません");
        if (std::filesystem::is_directory(status) && path_begins_with(destination, source))
            throw std::runtime_error("フォルダ自身の内側へはアップロードできません");

        const auto fold_name = [](const std::filesystem::path& name) {
            auto text = path_text(name);
            std::transform(text.begin(), text.end(), text.begin(),
                [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            return text;
        };
        const auto requested_name = fold_name(source.filename());
        if (destination == root_ && requested_name == fold_name(trash_relative_))
            throw std::runtime_error("Explorerのtrashフォルダは上書きできません");
        auto final_path = destination / source.filename();
        bool existing = false;
        for (std::filesystem::directory_iterator it(destination, error), end;
             !error && it != end; it.increment(error)) {
            if (fold_name(it->path().filename()) != requested_name) continue;
            if (existing) throw std::runtime_error("同名の項目が複数あり、上書き先を特定できません");
            const auto target_status = it->symlink_status(error);
            if (error || std::filesystem::is_symlink(target_status) ||
                (!std::filesystem::is_regular_file(target_status) &&
                 !std::filesystem::is_directory(target_status)))
                throw std::runtime_error("特殊項目は上書きできません");
            final_path = it->path();
            existing = true;
        }
        if (error) throw std::runtime_error("アップロード先を確認できません: " + error.message());
        if(!overwrite && existing)
            throw std::runtime_error("同名の項目があるためアップロードできません");
        const auto temporary = unique_child(destination,
            std::filesystem::path(".cw-upload-tmp-" + unique_suffix()));
        bool reserved_directory=false;
        try {
            copy_tree(source, temporary, status);
            if (overwrite && existing) {
                const auto backup = unique_child(destination,
                    std::filesystem::path(".cw-upload-backup-" + unique_suffix()));
                std::filesystem::rename(final_path, backup, error);
                if (error) throw std::runtime_error("既存項目を退避できません: " + error.message());
                std::filesystem::rename(temporary, final_path, error);
                if (error) {
                    std::error_code rollback_error;
                    std::filesystem::rename(backup, final_path, rollback_error);
                    if (rollback_error)
                        throw std::runtime_error("上書きに失敗しました。元の項目は " +
                            path_text(backup) + " に残っています");
                    throw std::runtime_error("アップロード結果を確定できません: " + error.message());
                }
                std::filesystem::remove_all(backup, error);
                if (error) throw std::runtime_error("上書きは完了しましたが、旧項目を削除できません。"
                    "退避先: " + path_text(backup));
                return std::filesystem::relative(final_path, root_, error);
            }
            // 同じ親の下に作った完成済み一時ファイル・ディレクトリは再コピーせず、共通の上書きしな
            // い移動処理で公開する。 WebのMEMFSでは再コピーに
            // より転送先のバッファーが二重になる （200MiBのファ
            // イルで400MiB）ため、移動に未対応の環境だけコピー
            // を使う。 外部のコピー元と名前衝突時の扱いは変えない。
            if (!::cardwirth::platform::rename_storage_path_no_replace(temporary, final_path, error)) {
                if (error != std::errc::operation_not_supported)
                    throw std::runtime_error("アップロード結果を確定できません: " + error.message());
                error.clear();
                // 上書きを伴うPOSIXのrenameを代替処理には使わない。
                if(std::filesystem::is_directory(status)){
                    reserved_directory=std::filesystem::create_directory(final_path,error);
                    if(!reserved_directory||error)throw std::runtime_error("同名の項目があるためアップロードできません");
                }
                copy_tree(temporary,final_path,status);
                std::filesystem::remove_all(temporary,error);
            }
        } catch (...) {
            std::filesystem::remove_all(temporary, error);
            if(reserved_directory)std::filesystem::remove_all(final_path,error);
            throw;
        }
        return std::filesystem::relative(final_path, root_, error);
    }

    std::filesystem::path copy_to_external(
        const std::filesystem::path& source,
        const std::filesystem::path& destination_directory_absolute) override {
        const auto source_relative = clean_relative(source);
        if (source_relative.empty()) throw std::invalid_argument("Explorer root自体はダウンロードできません");
        if (!destination_directory_absolute.is_absolute())
            throw std::invalid_argument("ダウンロード先は絶対パスで指定してください");
        const auto source_path = resolve(source_relative, true);
        const auto destination = normalize_root(destination_directory_absolute);
        std::error_code error;
        if (!std::filesystem::is_directory(destination, error) || error)
            throw std::runtime_error("ダウンロード先がフォルダではありません");
        const auto status = std::filesystem::symlink_status(source_path, error);
        if (error || std::filesystem::is_symlink(status) ||
            (!std::filesystem::is_regular_file(status) && !std::filesystem::is_directory(status)))
            throw std::runtime_error("この項目は安全にダウンロードできません");
        if (std::filesystem::is_directory(status) && path_begins_with(destination, source_path))
            throw std::runtime_error("フォルダ自身の内側へはダウンロードできません");

        const auto final_path = unique_child(destination, source_path.filename());
        const auto temporary = unique_child(destination,
            std::filesystem::path(".cw-download-tmp-" + unique_suffix()));
        try {
            copy_tree(source_path, temporary, status);
            std::filesystem::rename(temporary, final_path, error);
            if (error) throw std::runtime_error("ダウンロード結果を確定できません");
        } catch (...) {
            std::filesystem::remove_all(temporary, error);
            throw;
        }
        return final_path;
    }

    std::filesystem::path copy_to_external_as(
        const std::filesystem::path& source,
        const std::filesystem::path& destination_absolute) override {
        const auto source_relative = clean_relative(source);
        if (source_relative.empty()) throw std::invalid_argument("Explorer root自体はダウンロードできません");
        if (!destination_absolute.is_absolute() || destination_absolute.filename().empty())
            throw std::invalid_argument("ダウンロード先の名前が不正です");
        const auto source_path = resolve(source_relative, true);
        const auto destination = destination_absolute.lexically_normal();
        const auto parent = destination.parent_path();
        std::error_code error;
        if (!std::filesystem::is_directory(parent, error) || error)
            throw std::runtime_error("ダウンロード先フォルダがありません");
        const auto status = std::filesystem::symlink_status(source_path, error);
        if (error || std::filesystem::is_symlink(status) ||
            (!std::filesystem::is_regular_file(status) && !std::filesystem::is_directory(status)))
            throw std::runtime_error("この項目は安全にダウンロードできません");
        if (std::filesystem::is_directory(status) && path_begins_with(destination, source_path))
            throw std::runtime_error("フォルダ自身の内側へはダウンロードできません");

        const bool destination_exists = std::filesystem::exists(destination, error) && !error;
        if (destination_exists && std::filesystem::is_directory(status))
            throw std::runtime_error("同名フォルダが既にあります。別の名前を指定してください");
        if (destination_exists && !std::filesystem::is_regular_file(destination, error))
            throw std::runtime_error("同名の特殊項目があるため保存できません");

        const auto temporary = unique_child(parent,
            std::filesystem::path(".cw-download-tmp-" + unique_suffix()));
        try {
            copy_tree(source_path, temporary, status);
            if (!replace_file_atomically(temporary, destination, error))
                throw std::runtime_error("ダウンロード結果を確定できません: " +
                                         error.message());
        } catch (...) {
            std::filesystem::remove_all(temporary, error);
            throw;
        }
        return destination;
    }

    std::filesystem::path flatten_same_named_child(
        const std::filesystem::path& outer_directory) override {
        const auto outer_relative = clean_relative(outer_directory);
        if (outer_relative.empty())
            throw std::invalid_argument("Explorer rootはまとめられません");
        const auto outer = resolve(outer_relative, true);
        std::error_code error;
        if (!std::filesystem::is_directory(outer, error) || error)
            throw std::runtime_error("展開先フォルダがありません");
        if (contains_reparse_or_symlink(outer))
            throw std::runtime_error("symlinkまたはreparse pointを含むためまとめられません");

        std::vector<std::filesystem::path> children;
        for (std::filesystem::directory_iterator it(outer, error), end;
             it != end; it.increment(error)) {
            if (error) throw std::runtime_error("展開先を確認できません");
            children.push_back(it->path());
        }
        if (error || children.size() != 1)
            throw std::runtime_error("二重フォルダではなくなったためまとめられません");
        const auto child_status = std::filesystem::symlink_status(children.front(), error);
        if (error || !std::filesystem::is_directory(child_status) ||
            !path_component_equal(children.front().filename(), outer.filename()))
            throw std::runtime_error("同名の二重フォルダではありません");

        const auto temporary = unique_child(outer.parent_path(),
            std::filesystem::path(".cw-flatten-tmp-" + unique_suffix()));
        std::filesystem::rename(outer, temporary, error);
        if (error) throw std::runtime_error("外側フォルダを一時移動できません");
        const auto promoted = temporary / children.front().filename();
        std::filesystem::rename(promoted, outer, error);
        if (error) {
            std::error_code restore_error;
            std::filesystem::rename(temporary, outer, restore_error);
            throw std::runtime_error("内側フォルダをまとめられません");
        }
        std::filesystem::remove(temporary, error);
        if (error) throw std::runtime_error("空の中間フォルダを削除できません");
        return outer_relative;
    }

    std::filesystem::path move_to_trash(const std::filesystem::path& source) override {
        const auto relative = clean_relative(source);
        if (relative.empty() || relative == trash_relative_ || is_in_trash(relative))
            throw std::invalid_argument("この項目はtrashへ移動できません");
        const auto source_absolute = resolve(relative, true);
        const auto trash_absolute = resolve(trash_relative_, true);
        auto destination_name = source_absolute.filename();
        // 同名のユーザー項目があっても、非表示の索引
        // 名は予約する。 そうしないと、最初の移動
        // で作成中の索引を上書きする可能性がある。
        if (is_trash_index_name(destination_name))
            destination_name = std::filesystem::path(
                destination_name.native() + std::filesystem::path(".item").native());
        const auto destination = unique_child(trash_absolute, destination_name);
        // 移動先はまだ存在しない。std::filesystem::relative()
        // は両方のパスを正規化するため、 未作成のパスでは失敗する場合がある。
        const auto destination_relative = destination.lexically_relative(root_);
        auto records = read_trash_index();
        records.push_back({generic_path_text(destination_relative), generic_path_text(relative)});
        write_trash_index(records);
        std::error_code error;
        std::filesystem::rename(source_absolute, destination, error);
        if (error) {
            records.pop_back();
            try { write_trash_index(records); } catch (...) {}
            throw std::runtime_error("trashへ移動できません: " + error.message());
        }
        return destination_relative;
    }

    void delete_permanently(const std::filesystem::path& source) override {
        const auto relative = clean_relative(source);
        if (relative.empty() || relative == trash_relative_ || !is_in_trash(relative))
            throw std::invalid_argument("完全削除はtrash内の項目だけに使用できます");
        const auto source_absolute = resolve(relative, true);
        if (is_trash_index_name(source_absolute.filename()) ||
            contains_reparse_or_symlink(source_absolute))
            throw std::runtime_error("symlinkまたはreparse pointを含む項目は完全削除できません");
        std::error_code error;
        const auto removed = std::filesystem::remove_all(source_absolute, error);
        if (error || removed == 0) throw std::runtime_error("完全削除できません");
        auto records = read_trash_index();
        const auto key = generic_path_text(relative);
        std::erase_if(records, [&key](const TrashRecord& record) {
            return record.trashed_path == key;
        });
        write_trash_index(records);
    }

    std::filesystem::path restore_from_trash(const std::filesystem::path& source) override {
        const auto relative = clean_relative(source);
        if (relative.empty() || relative == trash_relative_ || !is_in_trash(relative))
            throw std::invalid_argument("復元はtrash内の項目だけに使用できます");
        const auto source_absolute = resolve(relative, true);
        if (is_trash_index_name(source_absolute.filename()) ||
            contains_reparse_or_symlink(source_absolute))
            throw std::runtime_error("symlinkまたはreparse pointを含む項目は復元できません");

        const auto key = generic_path_text(relative);
        auto records = read_trash_index();
        std::optional<std::filesystem::path> original;
        const auto record = std::find_if(records.begin(), records.end(), [&key](const TrashRecord& value) {
            return value.trashed_path == key;
        });
        if (record != records.end()) {
            try {
                const auto candidate = clean_relative(std::filesystem::u8path(record->original_path));
                if (!candidate.empty() && !is_in_trash(candidate)) original = candidate;
            } catch (...) {
                // 不正または古い記録によってルートの外へ出ないようにし、
                // その場合は後続の処理でルートを代わりの復元先にする。
            }
        }

        std::filesystem::path destination_directory;
        if (original && !original->parent_path().empty()) {
            try {
                const auto parent = resolve(original->parent_path(), true);
                if (std::filesystem::is_directory(parent))
                    destination_directory = original->parent_path();
            } catch (...) {}
        }
        if (destination_directory.empty()) destination_directory.clear();
        const auto destination_parent = resolve(destination_directory, true);
        const auto destination = unique_child(destination_parent, original
            ? original->filename() : source_absolute.filename());

        // 項目を移す前に記録を削除する。名前変更が失敗したら索引
        // を戻し、 次の試行でも項目を見つけられるようにする。
        std::optional<TrashRecord> removed_record;
        if (record != records.end()) {
            removed_record = *record;
            records.erase(record);
        }
        try {
            write_trash_index(records);
        } catch (...) {
            throw;
        }
        std::error_code error;
        std::filesystem::rename(source_absolute, destination, error);
        if (error) {
            if (removed_record) records.push_back(*removed_record);
            try { write_trash_index(records); } catch (...) {}
            throw std::runtime_error("trashから復元できません: " + error.message());
        }
        return std::filesystem::relative(destination, root_, error);
    }

    std::filesystem::path rename_entry(const std::filesystem::path& source,
                                       const std::filesystem::path& new_name) override {
        const auto relative = clean_relative(source);
        if (relative.empty() || relative == trash_relative_ || is_in_trash(relative))
            throw std::invalid_argument("この項目は名前を変更できません");
        require_valid_entry_name(new_name);
        const auto source_absolute = resolve(relative, true);
        if (contains_reparse_or_symlink(source_absolute))
            throw std::runtime_error("symlinkまたはreparse pointを含む項目は名前を変更できません");
        const auto destination_relative = relative.parent_path() / new_name;
        if (destination_relative == relative) return relative;
        const auto destination = resolve(destination_relative, false);
        std::error_code error;
        if (std::filesystem::exists(destination, error) &&
            !path_component_equal(destination.filename(), source_absolute.filename()))
            throw std::runtime_error("同じ名前の項目が既にあります");
        std::filesystem::rename(source_absolute, destination, error);
        if (error) throw std::runtime_error("名前を変更できません: " + error.message());
        return destination_relative;
    }

    std::filesystem::path create_entry(const std::filesystem::path& directory,
                                       const std::filesystem::path& name,
                                       const bool folder) override {
        const auto parent = clean_relative(directory);
        if (parent == trash_relative_ || is_in_trash(parent))
            throw std::invalid_argument("trash内には作成できません");
        require_valid_entry_name(name);
        const auto parent_absolute = resolve(parent, true);
        std::error_code error;
        if (!std::filesystem::is_directory(parent_absolute, error) || error)
            throw std::runtime_error("作成先フォルダがありません");
        const auto relative = parent / name;
        const auto destination = resolve(relative, false);
        if (std::filesystem::exists(destination, error))
            throw std::runtime_error("同じ名前の項目が既にあります");
        if (folder) {
            if (!std::filesystem::create_directory(destination, error) || error)
                throw std::runtime_error("フォルダを作成できません");
        } else {
            std::ofstream output(destination, std::ios::binary);
            if (!output) throw std::runtime_error("ファイルを作成できません");
        }
        return relative;
    }

    std::filesystem::path move_to(const std::filesystem::path& source,
                                  const std::filesystem::path& destination_directory) override {
        const auto relative = clean_relative(source);
        const auto target = clean_relative(destination_directory);
        if (relative.empty() || relative == trash_relative_ || is_in_trash(relative))
            throw std::invalid_argument("この項目は移動できません");
        if (target == trash_relative_ || is_in_trash(target))
            throw std::invalid_argument("trashへは「trashへ」で移動してください");
        if (path_begins_with(target, relative))
            throw std::invalid_argument("項目自身の内側へは移動できません");
        if (relative.parent_path() == target)
            throw std::invalid_argument("同じフォルダへは移動できません");
        const auto source_absolute = resolve(relative, true);
        if (contains_reparse_or_symlink(source_absolute))
            throw std::runtime_error("symlinkまたはreparse pointを含む項目は移動できません");
        const auto target_absolute = resolve(target, true);
        std::error_code error;
        if (!std::filesystem::is_directory(target_absolute, error) || error)
            throw std::runtime_error("移動先フォルダがありません");
        const auto destination_relative = target / relative.filename();
        const auto destination = resolve(destination_relative, false);
        if (std::filesystem::exists(destination, error))
            throw std::runtime_error("移動先に同じ名前の項目があります: " +
                                     path_text(relative.filename()));
        std::filesystem::rename(source_absolute, destination, error);
        if (error) throw std::runtime_error("移動できません: " + error.message());
        return destination_relative;
    }

    std::size_t empty_trash() override {
        const auto trash_absolute = resolve(trash_relative_, true);
        std::error_code error;
        std::vector<std::filesystem::path> children;
        for (std::filesystem::directory_iterator it(
             trash_absolute, std::filesystem::directory_options::skip_permission_denied,
                 error), end;
             it != end; it.increment(error)) {
            if (error) throw std::runtime_error("trashの内容を取得できません: " + error.message());
            if (is_trash_index_name(it->path())) continue;
            children.push_back(it->path());
        }
        if (error) throw std::runtime_error("trashの内容を取得できません: " + error.message());

        for (const auto& child : children) {
            if (contains_reparse_or_symlink(child))
                throw std::runtime_error("reparse pointまたはsymlinkを含むため完全削除できません: " +
                                         path_text(child.filename()));
        }
        std::size_t removed_items = 0;
        for (const auto& child : children) {
            std::filesystem::remove_all(child, error);
            if (error) throw std::runtime_error("trashを空にできません: " + error.message());
            ++removed_items;
        }
        write_trash_index({});
        return removed_items;
    }

private:
    std::filesystem::path trash_index_path() const {
        return resolve(trash_relative_, true) / kTrashIndexName;
    }

    std::vector<TrashRecord> read_trash_index() const {
        const auto index = trash_index_path();
        std::error_code error;
        const auto status = std::filesystem::symlink_status(index, error);
        if (error && error != std::errc::no_such_file_or_directory)
            throw std::runtime_error("trashの復元情報を確認できません: " + error.message());
        if (!error && (std::filesystem::is_symlink(status) ||
                       !std::filesystem::is_regular_file(status)))
            throw std::runtime_error("trashの復元情報が不正です");
        if (error || !std::filesystem::exists(index, error)) return {};

        std::ifstream input(index, std::ios::binary);
        if (!input) throw std::runtime_error("trashの復元情報を開けません");
        std::vector<TrashRecord> records;
        while (input.peek() != std::char_traits<char>::eof()) {
            TrashRecord record;
            if (!read_index_string(input, record.trashed_path) ||
                !read_index_string(input, record.original_path))
                throw std::runtime_error("trashの復元情報が壊れています");
            records.push_back(std::move(record));
        }
        return records;
    }

    void write_trash_index(const std::vector<TrashRecord>& records) const {
        const auto directory = resolve(trash_relative_, true);
        const auto index = directory / kTrashIndexName;
        std::error_code error;
        const auto status = std::filesystem::symlink_status(index, error);
        if (!error && (std::filesystem::is_symlink(status) ||
                       !std::filesystem::is_regular_file(status)))
            throw std::runtime_error("trashの復元情報が不正です");
        if (error && error != std::errc::no_such_file_or_directory)
            throw std::runtime_error("trashの復元情報を確認できません: " + error.message());
        const auto temporary = directory /
            (std::string(kTrashIndexName) + ".tmp-" + unique_suffix());
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("trashの復元情報を作成できません");
        for (const auto& record : records) {
            write_index_string(output, record.trashed_path);
            write_index_string(output, record.original_path);
        }
        output.close();
        if (!output) {
            std::filesystem::remove(temporary, error);
            throw std::runtime_error("trashの復元情報を確定できません");
        }
#if defined(_WIN32)
        if (!MoveFileExW(temporary.c_str(), index.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            const auto code = GetLastError();
            std::filesystem::remove(temporary, error);
            throw std::system_error(static_cast<int>(code), std::system_category(),
                                    "trashの復元情報を確定できません");
        }
#else
        std::filesystem::rename(temporary, index, error);
        if (error) {
            std::filesystem::remove(temporary, error);
            throw std::runtime_error("trashの復元情報を確定できません: " + error.message());
        }
#endif
    }

    std::filesystem::path resolve(const std::filesystem::path& relative, bool must_exist) const {
        const auto clean = clean_relative(relative);
        const auto lexical = normalize_root(root_ / clean);
        if (!path_begins_with(lexical, root_)) throw std::invalid_argument("Explorer root外のパスです");
        std::error_code error;
        const auto canonical = std::filesystem::weakly_canonical(lexical, error);
        if (error || !path_begins_with(canonical, root_canonical_))
            throw std::invalid_argument("symlink経由でExplorer root外へ移動できません");
        if (must_exist && !std::filesystem::exists(lexical, error))
            throw std::runtime_error("項目が見つかりません: " + path_text(clean));
        // ルート内を指している場合も、シンボリックリンクは拒否する。
        auto cursor = root_;
        for (const auto& component : clean) {
            cursor /= component;
            const auto status = std::filesystem::symlink_status(cursor, error);
            if (!error && std::filesystem::is_symlink(status))
                throw std::invalid_argument("symlinkはExplorerから操作できません: " + path_text(clean));
            if (error) error.clear();
        }
        return lexical;
    }

    static std::string unique_suffix() {
        return std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    }

    static std::filesystem::path unique_child(const std::filesystem::path& directory,
                                              const std::filesystem::path& requested) {
        std::error_code error;
        auto candidate = directory / requested;
        if (!std::filesystem::exists(candidate, error)) return candidate;
        const auto stem = requested.stem();
        const auto extension = requested.extension();
        for (unsigned index = 1; index < 10000; ++index) {
            const auto suffix = index == 1 ? " (copy)" : " (copy " + std::to_string(index) + ")";
            candidate = directory / (stem.native() + std::filesystem::path(suffix).native() + extension.native());
            if (!std::filesystem::exists(candidate, error)) return candidate;
        }
        throw std::runtime_error("衝突しないファイル名を作成できません");
    }

    static std::filesystem::path unique_archive_child(
        const std::filesystem::path& directory,
        const std::filesystem::path& requested) {
        std::error_code error;
        auto candidate = directory / requested;
        if (!std::filesystem::exists(candidate, error)) {
            if (!error) return candidate;
            throw std::runtime_error("展開先を確認できません: " + error.message());
        }
        for (unsigned index = 2; index < 10000; ++index) {
            candidate = directory / (requested.native() +
                std::filesystem::path("_" + std::to_string(index)).native());
            if (!std::filesystem::exists(candidate, error)) {
                if (!error) return candidate;
                throw std::runtime_error("展開先を確認できません: " + error.message());
            }
        }
        throw std::runtime_error("展開先の空きファイル名を作成できません");
    }

    static void copy_tree(const std::filesystem::path& source,
                          const std::filesystem::path& destination,
                          const std::filesystem::file_status& source_status) {
        std::error_code error;
        if (std::filesystem::is_regular_file(source_status)) {
            if (!std::filesystem::copy_file(source, destination,
                                             std::filesystem::copy_options::none, error) || error)
                throw std::runtime_error("ファイルをコピーできません");
            return;
        }
        std::filesystem::create_directory(destination, error);
        if (error) throw std::runtime_error("コピー先フォルダを作成できません");
        for (std::filesystem::recursive_directory_iterator it(
                 source, std::filesystem::directory_options::skip_permission_denied, error), end;
             it != end; it.increment(error)) {
            if (error) throw std::runtime_error("コピー元フォルダを走査できません");
            const auto status = it->symlink_status(error);
            if (error || std::filesystem::is_symlink(status))
                throw std::runtime_error("symlinkを含むフォルダはコピーできません");
            const auto relative = std::filesystem::relative(it->path(), source, error);
            if (error) throw std::runtime_error("コピー元の相対パスを取得できません");
            const auto output = destination / relative;
            if (std::filesystem::is_directory(status)) std::filesystem::create_directory(output, error);
            else if (std::filesystem::is_regular_file(status))
                std::filesystem::copy_file(it->path(), output, std::filesystem::copy_options::none, error);
            if (error) throw std::runtime_error("フォルダ内容をコピーできません");
        }
    }

    std::filesystem::path root_;
    std::filesystem::path root_canonical_;
    std::filesystem::path trash_relative_;
};

// Explorerの公開パスは論理ルートからの相対パスとし、Storage
// はネイティブ・メモリ・ブラウザーの ファイルシステムを使える
// 。このアダプターは実行コンテキストへ接続する。 上のmake_local_filesystem(path)
// は、OS専用の転送テストやコンテキス
// トを持たない呼び出し元向けに残す。
class StorageFileSystemAdapter final : public FileSystem {
public:
    explicit StorageFileSystemAdapter(const ::cardwirth::platform::AppRuntimeContext& context)
        : storage_(context.storage_ref()),
          root_(context.paths.user_root.empty() ? storage_.root() : context.paths.user_root),
          trash_relative_(trash_relative_path(context.paths, root_)) {
        if (trash_relative_.empty() || trash_relative_.has_parent_path())
            throw std::invalid_argument("AppPaths::trash_rootはuser_root直下に必要です");
        // StorageのAPIはマウントされたユーザーデータをルー
        // トとするため、引数はそこからの相対パスにする。 Android
        // では同じディレクトリが異なる正規パスで見える場合があり、
        // AppPathsの絶対パスを渡す方法は移植できない。
        if (!storage_.mkdir(trash_relative_, true))
            throw std::runtime_error(storage_error("trashを作成できません"));

        // 外部との入出力とアーカイブ・テキストのプレビューには引き続き
        // OSのパスが必要。 NativeStorageで必要になった
        // ときだけ用意し、通常の一覧・コピー・ごみ箱操作を Storage
        // で行う間はLocalFileSystemを生成しない。
        native_backend_ = dynamic_cast<const ::cardwirth::platform::NativeStorage*>(&storage_) !=
                          nullptr;
    }

    [[nodiscard]] std::filesystem::path root() const override { return root_; }
    [[nodiscard]] std::filesystem::path trash() const override { return trash_relative_; }

    [[nodiscard]] std::filesystem::path absolute(
        const std::filesystem::path& relative) const override {
        const auto clean = clean_relative(relative);
        return (root_ / clean).lexically_normal();
    }

    void sync() override {
        if (!storage_.sync()) throw std::runtime_error(storage_error("永続保存に失敗しました"));
    }

    [[nodiscard]] bool is_in_trash(const std::filesystem::path& relative) const override {
        const auto clean = clean_relative(relative);
        return !clean.empty() && path_begins_with(clean, trash_relative_);
    }

    [[nodiscard]] std::vector<FileEntry> list(
        const std::filesystem::path& directory) const override {
        const auto clean = clean_relative(directory);
        const auto directory_stat = storage_.stat(storage_path(clean));
        if (!directory_stat || !directory_stat->is_directory())
            throw std::runtime_error(storage_error("フォルダを開けません: " + path_text(clean)));

        const auto storage_entries = storage_.list(storage_path(clean));
        if (storage_entries.empty() && !storage_.last_error().empty())
            throw std::runtime_error(storage_error("フォルダ一覧を取得できません: " +
                                                  path_text(clean)));

        std::vector<FileEntry> result;
        result.reserve(storage_entries.size());
        for (const auto& source : storage_entries) {
            if (clean.empty() && std::filesystem::u8path(source.name) == trash_relative_)
                continue;
            if (clean == trash_relative_ && is_trash_index_name(std::filesystem::u8path(source.name)))
                continue;
            FileEntry entry;
            entry.path = clean / std::filesystem::u8path(source.name);
            entry.name = source.name;
            entry.in_trash = is_in_trash(entry.path);
            if (source.stat.is_file()) {
                entry.type = EntryType::File;
                entry.size = source.stat.size;
            } else if (source.stat.is_directory()) {
                entry.type = EntryType::Directory;
            } else {
                entry.type = EntryType::Unsupported;
            }
            if (entry.type != EntryType::Unsupported && source.stat.modified != 0) {
                entry.modified = ::cardwirth::platform::storage_timestamp_to_file_time(
                    source.stat.modified);
            }
            result.push_back(std::move(entry));
        }
        sort_entries(result);
        return result;
    }

    std::filesystem::path extract_archive(
        const ArchiveRegistry& archives,
        const std::filesystem::path& archive,
        const std::filesystem::path& destination_directory) override {
        const auto source = clean_relative(archive);
        const auto parent = clean_relative(destination_directory);
        const auto source_stat = require_stat(source, "展開元");
        if (!source_stat.is_file())
            throw std::runtime_error("展開元が通常のファイルではありません: " +
                                     path_text(source));
        require_directory(parent, "展開先");

        const auto destination = unique_archive_child(
            parent, archive_destination_name(source));
        archives.extract_to_storage(storage_, storage_path(source),
                                    storage_path(destination));
        return destination;
    }

    std::filesystem::path copy_to(
        const std::filesystem::path& source,
        const std::filesystem::path& destination_directory) override {
        const auto source_relative = clean_relative(source);
        if (source_relative.empty()) throw std::invalid_argument("Explorer root自体はコピーできません");
        const auto destination_relative = clean_relative(destination_directory);
        const auto source_stat = require_stat(source_relative, "コピー元");
        if (!source_stat.is_file() && !source_stat.is_directory())
            throw std::runtime_error("この項目は安全にコピーできません");
        require_directory(destination_relative, "コピー先");
        if (source_stat.is_directory() && path_begins_with(destination_relative, source_relative))
            throw std::runtime_error("フォルダ自身の内側へはコピーできません");

        const auto final_path = unique_child(destination_relative,
                                             source_relative.filename());
        copy_entry(source_relative, final_path);
        return final_path;
    }

    std::filesystem::path copy_from_external(
        const std::filesystem::path& source_absolute,
        const std::filesystem::path& destination_directory,
        bool overwrite) override {
        return require_external("アップロード", [&] {
            return external_->copy_from_external(source_absolute, destination_directory, overwrite);
        });
    }

    std::filesystem::path copy_to_external(
        const std::filesystem::path& source,
        const std::filesystem::path& destination_directory_absolute) override {
        return require_external("ダウンロード", [&] {
            return external_->copy_to_external(source, destination_directory_absolute);
        });
    }

    std::filesystem::path copy_to_external_as(
        const std::filesystem::path& source,
        const std::filesystem::path& destination_absolute) override {
        return require_external("ダウンロード", [&] {
            return external_->copy_to_external_as(source, destination_absolute);
        });
    }

    std::filesystem::path flatten_same_named_child(
        const std::filesystem::path& outer_directory) override {
        const auto outer_relative = clean_relative(outer_directory);
        if (outer_relative.empty()) throw std::invalid_argument("Explorer rootはまとめられません");
        require_directory(outer_relative, "展開先");
        const auto children = list(outer_relative);
        if (children.size() != 1 || children.front().type != EntryType::Directory ||
            !path_component_equal(std::filesystem::u8path(children.front().name),
                                  outer_relative.filename()))
            throw std::runtime_error("二重フォルダではなくなったためまとめられません");

        const auto temporary = unique_child(outer_relative.parent_path(),
            std::filesystem::path(".cw-flatten-tmp-" + unique_suffix()));
        move_entry(outer_relative, temporary);
        const auto promoted = temporary / children.front().name;
        try {
            move_entry(promoted, outer_relative);
        } catch (...) {
            try { move_entry(temporary, outer_relative); } catch (...) {}
            throw;
        }
        if (storage_.exists(storage_path(temporary))) {
            if (!storage_.remove(storage_path(temporary), false))
                throw std::runtime_error(storage_error("空の中間フォルダを削除できません"));
        }
        return outer_relative;
    }

    std::filesystem::path move_to_trash(const std::filesystem::path& source) override {
        const auto relative = clean_relative(source);
        if (relative.empty() || relative == trash_relative_ || is_in_trash(relative))
            throw std::invalid_argument("この項目はtrashへ移動できません");
        const auto source_stat = require_stat(relative, "移動元");
        if (!source_stat.is_file() && !source_stat.is_directory())
            throw std::runtime_error("この項目はtrashへ移動できません");

        auto destination_name = relative.filename();
        if (is_trash_index_name(destination_name))
            destination_name = std::filesystem::path(
                destination_name.native() + std::filesystem::path(".item").native());
        const auto destination = unique_child(trash_relative_, destination_name);
        auto records = read_trash_index();
        records.push_back({generic_path_text(destination), generic_path_text(relative)});
        write_trash_index(records);
        try {
            move_entry(relative, destination);
        } catch (...) {
            // コピー元の削除が失敗した場合、完全なコピーがごみ箱にしか残っていない
            // 可能性がある。 そのコピーが残っている限り、復元用の索引も保持する。
            if (!storage_.exists(storage_path(destination))) {
                records.pop_back();
                try { write_trash_index(records); } catch (...) {}
            }
            throw;
        }
        return destination;
    }

    void delete_permanently(const std::filesystem::path& source) override {
        const auto relative = clean_relative(source);
        if (relative.empty() || relative == trash_relative_ || !is_in_trash(relative))
            throw std::invalid_argument("完全削除はtrash内の項目だけに使用できます");
        const auto source_stat = require_stat(relative, "削除対象");
        if (!source_stat.is_file() && !source_stat.is_directory())
            throw std::runtime_error("symlinkまたは特殊項目は完全削除できません");
        if (!storage_.remove(storage_path(relative), true))
            throw std::runtime_error(storage_error("完全削除できません"));
        auto records = read_trash_index();
        const auto key = generic_path_text(relative);
        std::erase_if(records, [&key](const TrashRecord& record) {
            return record.trashed_path == key;
        });
        write_trash_index(records);
    }

    std::filesystem::path restore_from_trash(const std::filesystem::path& source) override {
        const auto relative = clean_relative(source);
        if (relative.empty() || relative == trash_relative_ || !is_in_trash(relative))
            throw std::invalid_argument("復元はtrash内の項目だけに使用できます");
        const auto source_stat = require_stat(relative, "復元対象");
        if (!source_stat.is_file() && !source_stat.is_directory())
            throw std::runtime_error("symlinkまたは特殊項目は復元できません");

        const auto key = generic_path_text(relative);
        auto records = read_trash_index();
        std::optional<std::filesystem::path> original;
        const auto record = std::find_if(records.begin(), records.end(), [&key](const TrashRecord& value) {
            return value.trashed_path == key;
        });
        if (record != records.end()) {
            try {
                const auto candidate = clean_relative(std::filesystem::u8path(record->original_path));
                if (!candidate.empty() && !is_in_trash(candidate)) original = candidate;
            } catch (...) {
                // 不正または古い記録によって、APIのルートの外へ出ないようにする。
            }
        }

        std::filesystem::path destination_directory;
        if (original && !original->parent_path().empty()) {
            const auto parent = storage_.stat(storage_path(original->parent_path()));
            if (parent && parent->is_directory()) destination_directory = original->parent_path();
        }
        const auto destination = unique_child(destination_directory,
            original ? original->filename() : relative.filename());

        std::optional<TrashRecord> removed_record;
        if (record != records.end()) {
            removed_record = *record;
            records.erase(record);
        }
        write_trash_index(records);
        try {
            move_entry(relative, destination);
        } catch (...) {
            if (removed_record) records.push_back(*removed_record);
            try { write_trash_index(records); } catch (...) {}
            throw;
        }
        return destination;
    }

    std::filesystem::path rename_entry(const std::filesystem::path& source,
                                       const std::filesystem::path& new_name) override {
        const auto relative = clean_relative(source);
        if (relative.empty() || relative == trash_relative_ || is_in_trash(relative))
            throw std::invalid_argument("この項目は名前を変更できません");
        require_valid_entry_name(new_name);
        const auto source_stat = require_stat(relative, "変更対象");
        if (!source_stat.is_file() && !source_stat.is_directory())
            throw std::runtime_error("symlinkまたは特殊項目は名前を変更できません");
        const auto destination = clean_relative(relative.parent_path() / new_name);
        if (destination == relative) return relative;
        if (storage_.exists(storage_path(destination)) &&
            !path_component_equal(destination.filename(), relative.filename()))
            throw std::runtime_error("同じ名前の項目が既にあります");
        move_entry(relative, destination);
        return destination;
    }

    std::filesystem::path create_entry(const std::filesystem::path& directory,
                                       const std::filesystem::path& name,
                                       const bool folder) override {
        const auto parent = clean_relative(directory);
        if (parent == trash_relative_ || is_in_trash(parent))
            throw std::invalid_argument("trash内には作成できません");
        require_valid_entry_name(name);
        if (!parent.empty()) require_directory(parent, "作成先フォルダ");
        const auto relative = clean_relative(parent / name);
        if (storage_.exists(storage_path(relative)))
            throw std::runtime_error("同じ名前の項目が既にあります");
        if (folder) {
            if (!storage_.mkdir(storage_path(relative), false))
                throw std::runtime_error(storage_error("フォルダを作成できません"));
        } else if (!storage_.write_atomic(storage_path(relative),
                                          std::span<const std::byte>{}, false)) {
            throw std::runtime_error(storage_error("ファイルを作成できません"));
        }
        return relative;
    }

    std::filesystem::path move_to(const std::filesystem::path& source,
                                  const std::filesystem::path& destination_directory) override {
        const auto relative = clean_relative(source);
        const auto target = clean_relative(destination_directory);
        if (relative.empty() || relative == trash_relative_ || is_in_trash(relative))
            throw std::invalid_argument("この項目は移動できません");
        if (target == trash_relative_ || is_in_trash(target))
            throw std::invalid_argument("trashへは「trashへ」で移動してください");
        if (path_begins_with(target, relative))
            throw std::invalid_argument("項目自身の内側へは移動できません");
        if (relative.parent_path() == target)
            throw std::invalid_argument("同じフォルダへは移動できません");
        const auto source_stat = require_stat(relative, "移動元");
        if (!source_stat.is_file() && !source_stat.is_directory())
            throw std::runtime_error("symlinkまたは特殊項目は移動できません");
        if (!target.empty()) require_directory(target, "移動先フォルダ");
        const auto destination = clean_relative(target / relative.filename());
        if (storage_.exists(storage_path(destination)))
            throw std::runtime_error("移動先に同じ名前の項目があります: " +
                                     path_text(relative.filename()));
        move_entry(relative, destination);
        return destination;
    }

    std::size_t empty_trash() override {
        const auto entries = list(trash_relative_);
        for (const auto& entry : entries) {
            if (entry.type != EntryType::File && entry.type != EntryType::Directory)
                throw std::runtime_error("symlinkまたは特殊項目をtrashから削除できません");
        }
        for (const auto& entry : entries) {
            if (!storage_.remove(storage_path(entry.path), true))
                throw std::runtime_error(storage_error("trashを空にできません"));
        }
        write_trash_index({});
        return entries.size();
    }

private:
    static std::filesystem::path trash_relative_path(
        const ::cardwirth::platform::AppPaths& paths,
        const std::filesystem::path& root) {
        if (paths.trash_root.empty())
            throw std::invalid_argument("AppPaths::trash_rootが未設定です");
        const auto relative = root.empty() ? paths.trash_root :
                                              paths.trash_root.lexically_relative(root);
        if (relative.empty() || relative.is_absolute())
            throw std::invalid_argument("AppPaths::trash_rootがuser_root外です");
        return clean_relative(relative);
    }

    [[nodiscard]] std::filesystem::path storage_path(
        const std::filesystem::path& relative) const {
        // StoragePathは選択したマウント内のパス。NativeStorage
        // は互換用に絶対パスも受け付けるが、 ここでは相対パスを使い、Android
        // の/data/dataと/data/user/0の正規化の
        // 不一致を避ける。 WebStorageと同じ扱いにもなる。
        return clean_relative(relative);
    }

    [[nodiscard]] std::string storage_error(const std::string& operation) const {
        const auto detail = storage_.last_error();
        return detail.empty() ? operation : operation + ": " + detail;
    }

    [[nodiscard]] ::cardwirth::platform::StorageStat require_stat(
        const std::filesystem::path& relative,
        const std::string& description) const {
        const auto stat = storage_.stat(storage_path(relative));
        if (!stat) throw std::runtime_error(storage_error(description + "を確認できません"));
        return *stat;
    }

    void require_directory(const std::filesystem::path& relative,
                           const std::string& description) const {
        const auto stat = require_stat(relative, description);
        if (!stat.is_directory()) throw std::runtime_error(description + "がフォルダではありません");
    }

    // Storage::copy/moveは任意実装の操作
    // 。一部のネイティブ実装は新規作成された末端の パス構
    // 造の正規化を拒否するため、共通の代替処理が必要になる
    // 。 Storageの基本操作からルート内に限定した
    // 処理を組み立て、仮想・ネイティブで同じ経路を使う。
    void copy_entry(const std::filesystem::path& source,
                    const std::filesystem::path& destination) {
        const auto source_stat = require_stat(source, "コピー元");
        if (!source_stat.is_file() && !source_stat.is_directory())
            throw std::runtime_error("symlinkまたは特殊項目は安全にコピーできません");
        if (storage_.exists(storage_path(destination)))
            throw std::runtime_error("コピー先に同名の項目があります");

        if (source_stat.is_file()) {
            const auto staging = unique_child(destination.parent_path(),
                std::filesystem::path(".cw-copy-tmp-" + unique_suffix()));
            try {
                auto input = storage_.open_read(storage_path(source));
                if (!input)
                    throw std::runtime_error(storage_error("ファイルを読み取れません"));
                auto output = storage_.open_write(storage_path(staging), true, true);
                if (!output)
                    throw std::runtime_error(storage_error("コピー一時ファイルを作成できません"));
                std::array<std::byte, 64U * 1024U> buffer{};
                for (;;) {
                    const auto count = input->read(std::span<std::byte>(buffer));
                    if (count != 0 &&
                        !output->write(std::span<const std::byte>(buffer.data(), count)))
                        throw std::runtime_error(storage_error("ファイルをコピーできません"));
                    if (!input->good())
                        throw std::runtime_error(storage_error("ファイルを読み取れません"));
                    if (count == 0) break;
                }
                if (!output->flush())
                    throw std::runtime_error(storage_error("コピー結果を確定できません"));
                output.reset();
                if (!storage_.rename(storage_path(staging), storage_path(destination), false))
                    throw std::runtime_error(storage_error("コピー結果を公開できません"));
            } catch (...) {
                (void)storage_.remove(storage_path(staging), false);
                throw;
            }
            return;
        }

        if (!storage_.mkdir(storage_path(destination), true))
            throw std::runtime_error(storage_error("コピー先フォルダを作成できません"));
        try {
            const auto children = storage_.list(storage_path(source));
            if (children.empty() && !storage_.last_error().empty())
                throw std::runtime_error(storage_error("フォルダ内容を取得できません"));
            for (const auto& child : children) {
                const auto child_name = std::filesystem::u8path(child.name);
                copy_entry(source / child_name, destination / child_name);
            }
        } catch (...) {
            (void)storage_.remove(storage_path(destination), true);
            throw;
        }
    }

    void move_entry(const std::filesystem::path& source,
                    const std::filesystem::path& destination) {
        const auto clean_source = clean_relative(source);
        const auto clean_destination = clean_relative(destination);
        if (clean_source.empty() || clean_destination.empty())
            throw std::invalid_argument("Explorer root自体は移動できません");
        if (path_begins_with(clean_destination, clean_source))
            throw std::invalid_argument("項目自身の内側へは移動できません");
        // 名前変更を先に試みる。コピー・削除への代替を許す条件は、後
        // 続のNativeStorageとエラー種別の判定に従う。
        if (storage_.rename(storage_path(clean_source), storage_path(clean_destination), false))
            return;
        // 名前変更の失敗は、配置自体には成功して永続化だけ失敗した可能性もある。
        // この失敗を破壊的なコピー・削除で再試行しない。 代替処理を許すのは、
        // ネイティブの異なるデバイス間の移動または名前変更の未対応時だけとする。
        const auto* native = dynamic_cast<const cardwirth::platform::NativeStorage*>(&storage_);
        if (!native || (native->last_error_code() != std::errc::cross_device_link &&
                        native->last_error_code() != std::errc::operation_not_supported &&
                        native->last_error_code() != std::errc::function_not_supported))
            throw std::runtime_error(storage_error("移動を確定できません"));
        copy_entry(clean_source, clean_destination);
        if (!storage_.remove(storage_path(clean_source), true)) {
            throw std::runtime_error(storage_error("移動元を完全に削除できません。復旧用コピーを保持しました: " +
                generic_path_text(clean_destination) + " / 移動元: " + generic_path_text(clean_source)));
        }
    }

    [[nodiscard]] std::filesystem::path unique_child(
        const std::filesystem::path& directory,
        const std::filesystem::path& requested) const {
        auto candidate = directory / requested;
        if (!storage_.exists(storage_path(candidate))) return candidate;
        const auto stem = requested.stem();
        const auto extension = requested.extension();
        for (unsigned index = 1; index < 10000; ++index) {
            const auto suffix = index == 1 ? " (copy)" : " (copy " + std::to_string(index) + ")";
            candidate = directory /
                std::filesystem::path(stem.native() + std::filesystem::path(suffix).native() +
                                       extension.native());
            if (!storage_.exists(storage_path(candidate))) return candidate;
        }
        throw std::runtime_error("衝突しないファイル名を作成できません");
    }

    [[nodiscard]] std::filesystem::path unique_archive_child(
        const std::filesystem::path& directory,
        const std::filesystem::path& requested) const {
        auto candidate = directory / requested;
        if (!storage_.exists(storage_path(candidate))) return candidate;
        for (unsigned index = 2; index < 10000; ++index) {
            candidate = directory / (requested.native() +
                std::filesystem::path("_" + std::to_string(index)).native());
            if (!storage_.exists(storage_path(candidate))) return candidate;
        }
        throw std::runtime_error("展開先の空きファイル名を作成できません");
    }

    [[nodiscard]] std::vector<TrashRecord> read_trash_index() const {
        const auto index = trash_relative_ / kTrashIndexName;
        if (!storage_.exists(storage_path(index))) return {};
        const auto bytes = storage_.read(storage_path(index));
        if (!bytes) throw std::runtime_error(storage_error("trashの復元情報を開けません"));
        std::string encoded;
        encoded.resize(bytes->size());
        if (!bytes->empty())
            std::memcpy(encoded.data(), bytes->data(), bytes->size());
        std::istringstream input(encoded, std::ios::binary);
        std::vector<TrashRecord> records;
        while (input.peek() != std::char_traits<char>::eof()) {
            TrashRecord record;
            if (!read_index_string(input, record.trashed_path) ||
                !read_index_string(input, record.original_path))
                throw std::runtime_error("trashの復元情報が壊れています");
            records.push_back(std::move(record));
        }
        return records;
    }

    void write_trash_index(const std::vector<TrashRecord>& records) const {
        std::ostringstream output(std::ios::binary);
        for (const auto& record : records) {
            write_index_string(output, record.trashed_path);
            write_index_string(output, record.original_path);
        }
        const auto encoded = output.str();
        const auto* data = reinterpret_cast<const std::byte*>(encoded.data());
        if (!storage_.write_atomic(storage_path(trash_relative_ / kTrashIndexName),
                                   std::span<const std::byte>(data, encoded.size()), true))
            throw std::runtime_error(storage_error("trashの復元情報を保存できません"));
    }

    template <typename Function>
    std::filesystem::path require_external(const char* operation, Function&& function) const {
        if (!external_ && native_backend_)
            external_ = make_local_filesystem(root_, path_text(trash_relative_));
        if (!external_)
            throw std::runtime_error(std::string(operation) + "はこのStorage backendでは利用できません");
        return function();
    }

    static void sort_entries(std::vector<FileEntry>& entries) {
        std::sort(entries.begin(), entries.end(), [](const FileEntry& left, const FileEntry& right) {
            if (left.type != right.type) return left.type == EntryType::Directory;
            auto fold = [](std::string value) {
                std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
                    return static_cast<char>(std::tolower(character));
                });
                return value;
            };
            return fold(left.name) < fold(right.name);
        });
    }

    static std::string unique_suffix() {
        return std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    }

    ::cardwirth::platform::Storage& storage_;
    std::filesystem::path root_;
    std::filesystem::path trash_relative_;
    mutable std::shared_ptr<FileSystem> external_;
    bool native_backend_{};
};

}  // namespace

std::filesystem::path FileSystem::rename_entry(const std::filesystem::path&,
                                               const std::filesystem::path&) {
    throw std::runtime_error("この場所では名前を変更できません");
}

std::filesystem::path FileSystem::move_to(const std::filesystem::path&,
                                          const std::filesystem::path&) {
    throw std::runtime_error("この場所では移動できません");
}

std::filesystem::path FileSystem::create_entry(const std::filesystem::path&,
                                               const std::filesystem::path&, bool) {
    throw std::runtime_error("この場所では作成できません");
}

std::shared_ptr<FileSystem> make_local_filesystem(const std::filesystem::path& root,
                                                  std::string trash_name) {
    return std::make_shared<LocalFileSystem>(root, std::move(trash_name));
}

std::shared_ptr<FileSystem> make_local_filesystem(
    const ::cardwirth::platform::AppRuntimeContext& context) {
    return make_storage_filesystem(context);
}

std::shared_ptr<FileSystem> make_storage_filesystem(
    const ::cardwirth::platform::AppRuntimeContext& context) {
    return std::make_shared<StorageFileSystemAdapter>(context);
}

}  // namespace cardwirth::explorer
