#pragma once

// 独立したアーカイブ境界。Python の zipfile/lhafile/util.py に相当するが、
// UI や CWPy の状態には依存しない。実装は archive.cpp に閉じ込める。
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace cw::lib::archive {

enum class Format { Unknown, Zip, Lha, SevenZip, Cab };
enum class Compression { Stored, Deflated, MsZip, LhaStored, Unsupported };

class ArchiveError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class InvalidArchive : public ArchiveError {
public:
    using ArchiveError::ArchiveError;
};

class UnsafePath : public ArchiveError {
public:
    using ArchiveError::ArchiveError;
};

class UnsupportedCompression : public ArchiveError {
public:
    using ArchiveError::ArchiveError;
};

class BackendUnavailable : public ArchiveError {
public:
    using ArchiveError::ArchiveError;
};

struct Entry {
    // Editor 以外の ZIP は ZipInfo の OS 固有の区切り文字規則に
    // 従い、Editor の ZIP は復号した表記を保持する。LHA などは読み込みプ
    // ロファイルに別指定がなければ、スラッシュに正規化した共通 UTF-8 キーを使う。
    std::string name{};
    std::uint64_t compressed_size{};
    std::uint64_t uncompressed_size{};
    std::uint32_t crc32{};
    std::int64_t mtime_unix{};
    Compression compression{Compression::Unsupported};
    bool directory{};
    bool encrypted{};
    bool utf8_name{};
    bool symlink{};
    // Python 互換のシナリオ上書き比較には、ZIP 元来のローカル日時フ
    // ィールドが必要。mtime_unix は拡張フィールドを使う場合がある。
    std::uint16_t zip_dos_date{};
    std::uint16_t zip_dos_time{};
    std::string zip_raw_name{};
    std::uint32_t zip_external_attributes{};
    std::uint16_t zip_internal_attributes{};
    std::uint16_t zip_made_by{45};
    // Py は LHA の不正な DOS 日付をローカルの 1970-01-01
    // に置き換える。時刻値がゼロ・負の場合もあるため、日時の欠落とは区別する。
    bool mtime_valid{};
    bool mtime_conversion_failed{};
    // Editor の展開用に、CAB ヘッダーの DOS 属性を保持する
    // 。属性を適用する方針は呼び出し側が決め、共通リーダーには持たせない。
    std::uint16_t cab_dos_attributes{};
    // 元の CAB のローカル日時フィールドを保持し、Windows の呼び出し側で D の DosDateTimeToFileTime
    // 、LocalFileTimeToFileTime、SetFileTime の手順に合わせられるようにする。
    std::uint16_t cab_dos_date{};
    std::uint16_t cab_dos_time{};
    // ディレクトリー拡張を結合する前の、生の LHA ファイル名要
    // 素。一部の利用側は -lhd- やデコーダーの推定種別とは別
    // に、この要素からディレクトリーを判別する。空も有効な要素。
    std::optional<std::string> lha_raw_filename{};
    std::string lha_raw_directory{};
    std::string lha_method{};
    std::uint16_t lha_crc{};
    std::uint64_t lha_compressed_size{};
    std::uint64_t lha_original_size{};

private:
    // ZipInfo の複製は header_offset を保持する。ネイティブ Entry
    // の複製も内容を隠した記録識別子を保ち、名前・サイズ・CRC の一致だけで項目を
    // 識別しない。所有者への弱参照は Reader の破棄後にアーカイブを保持しない。
    std::weak_ptr<const void> source_;
    std::optional<std::size_t> record_index_;
    friend class Reader;
};

// Editor の D 互換動作では、ディレクトリーの印があってもサイズゼロの場合だけディレクトリーとする。Engine
// の ZIP は ZipInfo.is_dir() に従い、末尾が / の場合だけディレクトリーとする。
enum class ReaderProfile { Default, Editor, EngineScenario };

struct ZipMemberMetadata {
    std::uint64_t mtime_unix{};
    std::uint32_t external_attributes{};
    std::uint16_t internal_attributes{};
    std::uint16_t made_by{45}; // 上位バイトは外部属性の OS を表す。明示的に取得した暦
    // 日時を、後から変わったタイムゾーンで変換してはならない。
    std::optional<std::uint32_t> dos_time{};
};

struct ExtractOptions {
    bool overwrite{false};
    bool reject_symlinks{true};
    // Python のシナリオ ZIP/LHA 処理は危険な項目を省
    // いて継続し、ほかの利用側は既定で厳格な失敗方針を保つ。意図的
    // なネイティブ側の制限として、共通の出力先解決は Python
    // の normpath が許す場合もある内部の親要素・デバイ
    // ス名を拒否する。それらも省略対象とし、解決処理を迂回しない。
    bool skip_unsafe_paths{false};
    std::uint64_t max_file_size{512ULL * 1024ULL * 1024ULL};
    std::uint64_t max_total_size{2ULL * 1024ULL * 1024ULL * 1024ULL};
    std::size_t max_files{100000};
    // 各項目の前にゼロ始まりのインデックスで呼ぶ。true で取り消し
    // 、extract は書き込み済みデータを除かず空のパスを返す。
    std::function<bool(std::size_t)> cancel_before_entry;
    // シナリオの展開では、アーカイブと同じ日時の既存ファイルを保持
    // できる。汎用の展開は、要求された場合は引き続き上書きする。
    bool reuse_matching_time{};
    // Engine の ZIP 展開は汎用 Reader の拡張フィールドの絶対日時ではなく
    // 、ローカル日時の ZipInfo.date_time に従う。各ストレージで共有する。
    bool local_zip_time{};
    // Py の ZIP/LHA は項目に必要となった時点でだけディレクト
    // リーを作る。CAB を含む汎用展開は、従来どおり先に出力先を作る。
    bool lazy_destination{};
    // Python の上書き復旧は古いファイルを退避して一度だけ再試行
    // する。汎用の Editor 展開は従来の不可分な失敗動作を保つ。
    std::function<void(const std::filesystem::path&)> recover_overwrite;
};

struct WriterOptions {
    int compression_level{6};
    bool utf8_filenames{true};
    // 指定時は D の std.zip の論理的な Classic 配置
    // の制限を適用する。空でないアーカイブでは、ローカル記録、中央記録、
    // 22 バイトの終端記録、D が常に予約する 76 バイトの ZIP64
    // 終端領域を数える。ZIP64 の項目サイズ・オフセットは無効にす
    // るが、65535 項目超の件数だけの ZIP64 経路は残す。未
    // 指定なら Engine の Py 互換 ZIP64 予約を使う。
    std::optional<std::uint64_t> d_zip_logical_layout_limit{};
    // D の std.zip は呼び出し側が渡した項目の追加情報だけを書く。アーカ
    // イブアダプターは Engine が自動生成する日時フィールドを無効にする。
    bool emit_extended_timestamps{true};
};

class Reader {
public:
    Reader() = delete;
    explicit Reader(const std::filesystem::path& path);
    Reader(Reader&&) noexcept;
    Reader& operator=(Reader&&) noexcept;
    Reader(const Reader&) = delete;
    Reader& operator=(const Reader&) = delete;
    ~Reader();

    static Reader open(const std::filesystem::path& path, ReaderProfile profile = ReaderProfile::Default);

    Format format() const noexcept { return m_format; }
    const std::filesystem::path& path() const noexcept { return m_path; }
    const std::vector<Entry>& entries() const noexcept { return m_entries; }
    bool contains(const std::string& name) const;
    std::vector<std::byte> read(const std::string& name) const;
    std::vector<std::byte> read(const Entry& entry) const;
    // メタデータ上でディレクトリーでも、選択した記録を復号
    // する。その読み取りを公開する利用側も、通常の read()
    // と同じデコーダーと記録の所有確認を使う。
    std::vector<std::byte> read_payload(const Entry& entry) const;
    // 展開で書き込む前に、宣言されたパス・件数・サイズを検証する。Reader::extract
    // と Python 互換のネイティブ ZIP ループで共有する。
    void validate_extraction(const ExtractOptions& options = {}) const;
    std::filesystem::path extract(const std::filesystem::path& destination,
                                  const ExtractOptions& options = {}) const;

    // 定義は実装ファイルに隠蔽する。public宣言はMSVCでのPImpl定義に必要。
    struct Impl;

private:
    std::vector<std::byte> read_entry(const Entry& entry, bool directory_payload) const;
    explicit Reader(std::filesystem::path path, Format format,
                    std::vector<Entry> entries, std::shared_ptr<const Impl> impl);
    std::filesystem::path m_path;
    Format m_format{Format::Unknown};
    std::vector<Entry> m_entries;
    std::shared_ptr<const Impl> m_impl;
};

// CAB の一覧取得は CFHEADER/CFFILE だけを読む。一致名があれば
// 、後の不正名より前に停止できる。デコーダー・チェックサムの検証は展開時に行う。
std::uint16_t cabinet_file_count(const std::filesystem::path& path);
// 特定のデコーダーを強制せず、生の CFFILE の名前バイトと CAB の UTF
// 名属性を走査する。コールバックに渡す名前のビューは、その呼び出し中だけ有効。
void visit_cabinet_name_records(
    const std::filesystem::path& path,
    const std::function<bool(std::string_view, bool)>& visitor);
void visit_cabinet_names(const std::filesystem::path& path,
                         const std::function<bool(const std::string&)>& visitor);

class Writer {
public:
    Writer() = delete;
    Writer(const std::filesystem::path& path, const WriterOptions& options = {});
    Writer(Writer&&) noexcept;
    Writer& operator=(Writer&&) noexcept;
    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;
    ~Writer() noexcept;

    static Writer create(const std::filesystem::path& path, const WriterOptions& options = {});
    static Writer append(const std::filesystem::path& path, const WriterOptions& options = {});

    // Editor 用に符号化名の境界を明示する。復号したパスを先に検証し
    // 、CP932 の後続バイト 0x5c を区切り文字として扱わない。
    void add_zip_member(const std::string& logical_name, const std::string& raw_name,
                        const std::vector<std::byte>& data, bool directory,
                        Compression compression, bool utf8_filename,
                        const ZipMemberMetadata& metadata);
    void add_directory(const std::string& name);
    // 従来の CardWirth ZIP は項目ごとにファイル名の符号化を選ぶ。既
    // 存の利用側には WriterOptions の値を既定とする。UTF-8
    // の非 ASCII 名はビット 11 を立て、ASCII 名には不要。生の
    // CP932 名を保持するアダプターは、その項目のビット 11 を立てない。
    void add_directory(const std::string& name, bool utf8_filename);
    void add_file(const std::string& name, const std::vector<std::byte>& data,
                  Compression compression = Compression::Deflated);
    void add_file(const std::string& name, const std::vector<std::byte>& data,
                  Compression compression, bool utf8_filename);
    void add_file(const std::string& name, const void* data, std::size_t size,
                  Compression compression = Compression::Deflated);
    void add_file(const std::string& name, const void* data, std::size_t size,
                  Compression compression, bool utf8_filename);
    void add_path(const std::filesystem::path& source,
                  const std::string& archive_prefix = {},
                  Compression compression = Compression::Deflated);
    // 共通走査は Py の compress_zip のディレクトリー
    // 専用モードにも対応し、最初のファイルで内容を読まずに拒否する。
    void add_path(const std::filesystem::path& source,
                  const std::string& archive_prefix,
                  Compression compression, bool allow_files);
    void close();
    bool closed() const noexcept { return m_closed; }
    const std::filesystem::path& path() const noexcept { return m_path; }

private:
    struct Pending;
    struct Output;
    void ensure_output();
    void write_member(Pending& entry,
                      const std::function<std::size_t(std::span<std::byte>)>& read,
                      std::uint64_t expected_size);
    void add_source_file(const std::filesystem::path& source,
                         const std::string& name, Compression compression);
    std::filesystem::path m_path;
    WriterOptions m_options;
    std::vector<Pending> m_pending;
    std::unique_ptr<Output> m_output;
    std::uint64_t m_d_zip_local_layout_size{};
    std::uint64_t m_d_zip_central_layout_size{};
    bool m_closed{};
};

Format detect_format(const std::filesystem::path& path);
std::string decode_legacy_filename(const std::vector<std::byte>& raw);
bool is_safe_relative_path(const std::string& name) noexcept;
// 既存のルート・子のリンクも含む共通のネイティブ展開境界。この
// 検査はハンドル基準ではなく、同時置換に対する防御ではない。
void validate_destination_root(const std::filesystem::path& root);
std::filesystem::path checked_destination(const std::filesystem::path& root,
                                          const std::string& name);
std::uint32_t crc32(const std::vector<std::byte>& data) noexcept;

} // namespace cw::lib::archive
