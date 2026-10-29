#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace cardwirth::platform {
struct AppRuntimeContext;
}

namespace cardwirth::explorer {

class ArchiveRegistry;

// 古い移動先を先に削除せず、同じ親にある完成済みの一時フ
// ァイルで置き換える。 ファイルシステムをまたがないよう
// 、呼び出し元は一時ファイルを同じディレクトリに置く。
[[nodiscard]] bool replace_file_atomically(
    const std::filesystem::path& temporary,
    const std::filesystem::path& destination,
    std::error_code& error) noexcept;

enum class EntryType {
    File,
    Directory,
    Unsupported,
};

struct FileEntry {
    std::filesystem::path path;
    std::string name;
    EntryType type{EntryType::Unsupported};
    std::uintmax_t size{};
    bool in_trash{};
    std::optional<std::filesystem::file_time_type> modified;
};

class FileSystem {
public:
    virtual ~FileSystem() = default;
    virtual void sync() {}

    [[nodiscard]] virtual std::filesystem::path root() const = 0;
    [[nodiscard]] virtual std::filesystem::path trash() const = 0;
    [[nodiscard]] virtual std::filesystem::path absolute(
        const std::filesystem::path& relative) const = 0;
    [[nodiscard]] virtual bool is_in_trash(const std::filesystem::path& relative) const = 0;
    [[nodiscard]] virtual std::vector<FileEntry> list(
        const std::filesystem::path& directory) const = 0;

    virtual std::filesystem::path copy_to(
        const std::filesystem::path& source,
        const std::filesystem::path& destination_directory) = 0;
    // 具体的なバックエンドで展開し、Explorerからの
    // 相対パスで展開先を返す。 ネイティブとStorage
    // のどちらで公開するかをコントローラー側では選ばない。
    virtual std::filesystem::path extract_archive(
        const ArchiveRegistry& archives,
        const std::filesystem::path& archive,
        const std::filesystem::path& destination_directory) = 0;
    // OSで選択したファイル・フォルダーを、Explorerのルート内のディレクトリへ取り込む。
    virtual std::filesystem::path copy_from_external(
        const std::filesystem::path& source_absolute,
        const std::filesystem::path& destination_directory,
        bool overwrite = false) = 0;
    // Explorerのルート内のファイル・フォルダーを、OSで選択したディレクトリへ書き出す。
    virtual std::filesystem::path copy_to_external(
        const std::filesystem::path& source,
        const std::filesystem::path& destination_directory_absolute) = 0;
    virtual std::filesystem::path copy_to_external_as(
        const std::filesystem::path& source,
        const std::filesystem::path& destination_absolute) = 0;
    // 親と同名の子ディレクトリが一つだけある場合、それを親の位置へ繰り
    // 上げる。 A/A/filesのようなアーカイブの展開結果に使う。
    virtual std::filesystem::path flatten_same_named_child(
        const std::filesystem::path& outer_directory) = 0;
    virtual std::filesystem::path move_to_trash(
        const std::filesystem::path& source) = 0;
    virtual void delete_permanently(const std::filesystem::path& source) = 0;
    // ごみ箱から記録された元の場所へ復元する。
    // 元の親が存在しなければ、 Explorer
    // のルートを使い、衝突しない名前を選ぶ。
    virtual std::filesystem::path restore_from_trash(
        const std::filesystem::path& source) = 0;
    // ごみ箱の中身をすべて削除するが、ごみ箱のディレク
    // トリ自体は残す。 削除前の確認はUI側で行う。
    virtual std::size_t empty_trash() = 0;
    // 同じディレクトリ内で項目の名前を変える。new_nameは一
    // つのパス要素に限り、 変更先はまだ存在していてはならない。
    virtual std::filesystem::path rename_entry(
        const std::filesystem::path& source,
        const std::filesystem::path& new_name);
    // directoryにnameという空のファイルまたはフォルダーを作る。
    // 名前は一つのパス要素に限り、同名の項目がまだ存在していてはならない。
    virtual std::filesystem::path create_entry(
        const std::filesystem::path& directory,
        const std::filesystem::path& name, bool folder);
    // 名前を保ったまま別のディレクトリへ項目を移す。 移動先に同名
    // の項目がある場合や、移動先が自身の内側にある場合は失敗する。
    virtual std::filesystem::path move_to(
        const std::filesystem::path& source,
        const std::filesystem::path& destination_directory);
};

// 移植可能な実装では、すべての公開パスをrootからの相対パ
// スに限定する。 std::filesystemへ渡す前に、
// シンボリックリンクや再解析ポイントをたどる操作を拒否する。
std::shared_ptr<FileSystem> make_local_filesystem(
    const std::filesystem::path& root,
    std::string trash_name = "trash");
// 共通の実行環境に接続するアダプター。Explorerのコントロ
// ーラーは保存方式に依存せず、 一覧の取得と変更をAppRuntimeContext::storage_ref()
// へ委譲する。
std::shared_ptr<FileSystem> make_local_filesystem(
    const ::cardwirth::platform::AppRuntimeContext& context);

// 共通の実行コンテキストが所有するStorage上にExplorer
// の表示を構成する。 OS専用の転送は任意のネイティブ機能として
// 残すが、一覧と変更は常にcontext.storageを使う。
std::shared_ptr<FileSystem> make_storage_filesystem(
    const ::cardwirth::platform::AppRuntimeContext& context);

}  // namespace cardwirth::explorer
