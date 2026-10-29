#pragma once

#include <filesystem>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace cwxeditor::lib {
class FileSystemBackend;
}

namespace cwxeditor::lib {

enum class ArchiveFormat { Automatic, Cab, Lha, Zip };

struct ArchiveEntry {
    std::filesystem::path path;
    std::uint64_t uncompressed_size{};
    bool directory{};
};

struct ArchiveMemoryFile {
    std::filesystem::path path; // ArchiveCreateRequest::source_directoryからの相対パス。
    std::vector<std::byte> data;
};

struct ArchiveCreateRequest {
    ArchiveFormat format{ArchiveFormat::Automatic};
    std::filesystem::path source_directory;
    std::filesystem::path archive_path;
    std::function<bool(const std::filesystem::path&)> include;
    // CAB用の保持済みXMLによる差し替え。他のアーカイブ形式は明示的
    // に拒否する。 名前・選択・サイズ制限はディスク入力と共通にし、CAB
    // 出力先はFCIの直接のCREATE_ALWAYS動作に従う。
    std::vector<ArchiveMemoryFile> memory_files;
};

struct ArchiveExtractRequest {
    ArchiveFormat format{ArchiveFormat::Automatic};
    std::filesystem::path archive_path;
    std::filesystem::path destination;
    std::function<std::filesystem::path(const ArchiveEntry&)> map_entry;
    bool overwrite{false};
};

class ArchiveBackend {
public:
    virtual ~ArchiveBackend() = default;
    [[nodiscard]] virtual std::vector<ArchiveEntry> list(const std::filesystem::path& archive,
                                                         ArchiveFormat format,
                                                         std::string& error) = 0;
    virtual bool create(const ArchiveCreateRequest& request, std::string& error) = 0;
    virtual bool extract(const ArchiveExtractRequest& request, std::string& error) = 0;
};

// Editorの既定実装は部品の境界の裏へ置く。
// 解析・展開はcw/lib/archiveへ委譲
// し、 圧縮ZIP出力はその実行時zlibを使う
// 。LHA・Cabinetの作成は下の依存なし
// の書き込み器が 対応する形式に限り、未対応の
// 圧縮を黙って変更せず、呼び出し元へ報告する。
class PortableArchiveBackend final : public ArchiveBackend {
public:
    explicit PortableArchiveBackend(std::shared_ptr<FileSystemBackend> filesystem = {});

    [[nodiscard]] std::vector<ArchiveEntry> list(const std::filesystem::path& archive,
                                                 ArchiveFormat format,
                                                 std::string& error) override;
    bool create(const ArchiveCreateRequest& request, std::string& error) override;
    bool extract(const ArchiveExtractRequest& request, std::string& error) override;

private:
    std::shared_ptr<FileSystemBackend> filesystem_;
};

using DefaultArchiveBackend = PortableArchiveBackend;

[[nodiscard]] std::shared_ptr<ArchiveBackend> make_archive_backend();
[[nodiscard]] std::shared_ptr<ArchiveBackend> make_archive_backend(
    std::shared_ptr<FileSystemBackend> filesystem);

}  // namespace cwxeditor::lib
