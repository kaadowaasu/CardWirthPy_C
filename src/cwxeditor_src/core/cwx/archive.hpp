#pragma once

// cwx/archive.dのC++20移植。DMD
// のマングル名や一覧用のラッパーではなく、 UTF-8
// 文字列・バイト配列・所有権付きのアーカイブ・std::function
// を使う。 ZIP/LHAの解析と書き込み
// は、cwxeditor_src/lib/archive.*
// の共通部品へ委譲する。

#include "cwxeditor_src/core/cwx/filesync.hpp"
#include "cwxeditor_src/lib/portable.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cwxeditor::cwx::archive {

using String = lib::String;
using StringView = lib::StringView;
using Byte = lib::Byte;
using Bytes = lib::Bytes;
using ByteView = lib::ByteView;

/// 共通の書き込み器と読み込み器が対応する、2種類のZIP圧縮方式。
enum class CompressionMethod : std::uint16_t {
    stored = 0,
    deflate = 8,
};

/// std.zipのArchiveMemberに対応するC++
/// の表現。 ZipArchiveへ追加する前にメンバー
/// を組み立てる呼び出し元があるため、 キャメルケースの
/// メンバー名はDのAPIに合わせる。 バイト配列は内容
/// を所有し、DのGC管理ポインターを外へ公開しない。
struct ArchiveMember {
    String name{}; // メンバーのパス表記を維持する、論理的なUTF-8表現。
    Bytes extra{};
    String comment{};
    std::uint16_t flags{};
    std::uint16_t internalAttributes{};
    // 共通の書き込み器へ渡す保存形式の属性。 Dのプロパティア
    // クセサーは、作成元OSの値をこの形式へ暗黙に詰めている。
    std::uint32_t fileAttributes{};
    std::uint64_t time{}; // 共通の書き込み器向けのUnix秒。D版はDOS時刻を使う。 後からタイム
    // ゾーンが変わっても、D版と同様に取得時のローカルDOS日時を保つ。 ネ
    // イティブ側で別のUnix時刻を代入したら、このキャッシュを無効にする。
    std::optional<std::uint32_t> dosTime{};
    std::uint64_t dosTimeSource{};
    CompressionMethod compressionMethod{CompressionMethod::deflate};
    Bytes compressedData{};
    Bytes expandedData{};
    std::uint64_t compressedSize{};
    std::uint64_t expandedSize{};
    std::uint32_t crc32{};
    std::size_t sourceIndex{};
    String rawName{}; // 任意の符号化済みZIPバイト列。区切り文字は正規化しない。 Dのstd.zip
    // では作成元バージョンの既定値は20で、fileAttributesの
    // 代入は 下位のバージョンバイトを保ったままOSの識別値を追加する。
    std::uint16_t madeVersion{20};

    [[nodiscard]] bool isDirectory() const noexcept;
};

using ArchiveMemberPtr = lib::Owner<ArchiveMember>;

/// DのZipArchive APIに対応する、所有
/// 権を持つ小さな窓口。 読み込み済みのアーカイブ
/// は下位のReaderを保持し、圧縮メンバーを必
/// 要時に展開する。 新規作成の場合はメンバーを直
/// 接保持し、build()でバイト列へ変換する。
class ZipArchive {
public:
    std::vector<ArchiveMemberPtr> directory;

    ZipArchive();
    explicit ZipArchive(const std::filesystem::path& path);
    explicit ZipArchive(const Bytes& data);
    explicit ZipArchive(ByteView data);

    ZipArchive(ZipArchive&&) noexcept;
    ZipArchive& operator=(ZipArchive&&) noexcept;
    ZipArchive(const ZipArchive&) = delete;
    ZipArchive& operator=(const ZipArchive&) = delete;
    ~ZipArchive();

    void addMember(ArchiveMemberPtr member);
    void deleteMember(const ArchiveMemberPtr& member);

    [[nodiscard]] bool contains(StringView name) const;
    [[nodiscard]] Bytes expand(const ArchiveMemberPtr& member);
    [[nodiscard]] Bytes expand(const ArchiveMember& member);
    [[nodiscard]] Bytes build();

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;

    void loadFromReader();
};

using ZipArchiveOwner = lib::Owner<ZipArchive>;

/// lhafile.lhafileのLhaInfoに対応する、最小限のLHAメタデータ。
struct LhaInfo {
    String fileName{};
    bool isDirectory{};
    std::uint64_t compressedSize{};
    std::uint64_t fileSize{};
    std::uint32_t crc{};
    String compressType{};
    std::size_t sourceIndex{};
};

/// 所有権付きのLHA読み込み窓口。D版のレベル0/1/2に合わせ、 -lhd-
/// の一覧項目と-lh0-/-lh5-/-lh6-/-lh7-の内容に対応
/// する。 下位の読み込み器のデコーダーと不透明な記録識別子を共用する。
class LhaFile {
public:
    std::vector<LhaInfo> infoList;
    std::vector<String> nameList;

    explicit LhaFile(const std::filesystem::path& path);
    explicit LhaFile(const Bytes& data);
    explicit LhaFile(ByteView data);

    LhaFile(LhaFile&&) noexcept;
    LhaFile& operator=(LhaFile&&) noexcept;
    // DはGC管理のLhaFileハンドルを値で渡し、その一覧は読み取り専用。 ネイ
    // ティブのコピーではメタデータのスナップショットと同じ開いた読み込み器を保持し、
    // コールバックが呼び出し元のハンドルを置き換えても入力が変わらないようにする。
    LhaFile(const LhaFile&) = default;
    LhaFile& operator=(const LhaFile&) = default;
    ~LhaFile();

    [[nodiscard]] Bytes read(StringView name) const;

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;

    void loadFromReader();
};

using ExpandCallback = lib::Callback<String(String, bool)>;
using ProgressCallback = lib::Callback<void(std::uint32_t)>;
using FileCallback = lib::Callback<void(String, const Bytes&, bool)>;
using IgnorePathCallback = lib::Callback<bool(String)>;

/// ローカルパスの区切り文字を、アーカイブ用の/へ変換する。
[[nodiscard]] String encodeArchiveName(String name);

void unzip(const std::filesystem::path& parent,
           const std::filesystem::path& zipPath,
           ExpandCallback expand = {},
           ProgressCallback setProgressNum = {},
           ProgressCallback progress = {});
void unzip(const std::filesystem::path& parent,
           ZipArchive& arc,
           ExpandCallback expand = {},
           ProgressCallback setProgressNum = {},
           ProgressCallback progress = {});
void unzip(ZipArchive& arc,
           FileCallback fileProc,
           ProgressCallback setProgressNum = {},
           ProgressCallback progress = {});
void unzip(const std::filesystem::path& parent,
           const ZipArchiveOwner& arc,
           ExpandCallback expand = {},
           ProgressCallback setProgressNum = {},
           ProgressCallback progress = {});
void unzip(const ZipArchiveOwner& arc,
           FileCallback fileProc,
           ProgressCallback setProgressNum = {},
           ProgressCallback progress = {});

/// 従来のDとの接続で使う、UTF-8パス文字列向けのオーバーロード。
void unzip(const String& parent,
           const String& zipPath,
           ExpandCallback expand = {},
           ProgressCallback setProgressNum = {},
           ProgressCallback progress = {});

[[nodiscard]] ArchiveMemberPtr archive(String name,
                                       const Bytes& data,
                                       bool isDir,
                                       bool useSysEnc = false);

[[nodiscard]] ZipArchiveOwner zip(const std::filesystem::path& target,
                                  bool top,
                                  IgnorePathCallback ignorePath,
                                  bool useSysEnc,
                                  std::vector<Bytes>& data);
[[nodiscard]] ZipArchiveOwner zip(const std::filesystem::path& target,
                                  bool top,
                                  const std::vector<String>& excludePath,
                                  bool useSysEnc,
                                  std::vector<Bytes>& data);

[[nodiscard]] String memberName(String name);
[[nodiscard]] String zipHasFile(const std::filesystem::path& zipPath,
                                StringView fileName);
[[nodiscard]] String zipHasFile(const String& zipPath, StringView fileName);

void zip(const std::filesystem::path& target,
         const std::filesystem::path& zipPath,
         bool top,
         IgnorePathCallback ignorePath,
         bool useSysEnc,
         const lib::Owner<filesync::FileSync>& sync = {});
void zip(const std::filesystem::path& target,
         const std::filesystem::path& zipPath,
         bool top,
         const std::vector<String>& excludePath,
         bool useSysEnc,
         const lib::Owner<filesync::FileSync>& sync = {});

/// 呼び出し元がFileSyncを所有済みの場合に使う、所有権を持たないオーバーロード。
void zip(const std::filesystem::path& target,
         const std::filesystem::path& zipPath,
         bool top,
         IgnorePathCallback ignorePath,
         bool useSysEnc,
         filesync::FileSync* sync);
void zip(const std::filesystem::path& target,
         const std::filesystem::path& zipPath,
         bool top,
         const std::vector<String>& excludePath,
         bool useSysEnc,
         filesync::FileSync* sync);

void unlha(const std::filesystem::path& parent,
           LhaFile& arc,
           ExpandCallback expand = {},
           ProgressCallback setProgressNum = {},
           ProgressCallback progress = {});

namespace detail {
void normalizeZipExclusions(std::vector<String>& paths);
}

// Dは変更可能な除外配列をその場で正規化する。 constのコンテ
// ナーやC++の一時引数では、従来どおり読み取り専用で転送する。
template <typename... Args>
decltype(auto) zip(const std::filesystem::path& target, bool top,
                   std::vector<String>& exclusions, Args&&... args) {
    detail::normalizeZipExclusions(exclusions);
    return zip(target, top, std::as_const(exclusions), std::forward<Args>(args)...);
}
template <typename... Args>
decltype(auto) zip(const std::filesystem::path& target, const std::filesystem::path& destination,
                   bool top, std::vector<String>& exclusions, Args&&... args) {
    detail::normalizeZipExclusions(exclusions);
    return zip(target, destination, top, std::as_const(exclusions), std::forward<Args>(args)...);
}
void unlha(LhaFile& arc,
           FileCallback fileProc,
           ProgressCallback setProgressNum = {},
           ProgressCallback progress = {});
void unlha(const String& parent,
           LhaFile& arc,
           ExpandCallback expand = {},
           ProgressCallback setProgressNum = {},
           ProgressCallback progress = {});

[[nodiscard]] String lhaHasFile(const std::filesystem::path& lhaPath,
                                StringView fileName);
[[nodiscard]] String lhaHasFile(const String& lhaPath, StringView fileName);

} // namespace cwxeditor::cwx::archive
