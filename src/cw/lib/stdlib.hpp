#pragma once

// CardWirth のデータ処理が使う Python 標準ライ
// ブラリーの、小さくロケール非依存の代替処理。ハッシュと base64
// は生のバイトを扱い、二進浮動小数点への変換を含めない。

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace cw::lib {

using StdBytes = std::vector<std::uint8_t>;
using StdString = std::string;

enum class StdlibErrorKind {
    InvalidArgument,
    InvalidEncoding,
    Parse,
    Io,
    Duplicate,
    Missing,
};

class StdlibError : public std::runtime_error {
public:
    StdlibError(StdlibErrorKind kind, const StdString& message);
    StdlibErrorKind kind() const noexcept { return m_kind; }

private:
    StdlibErrorKind m_kind;
};

enum class HashAlgorithm { MD5, SHA1, SHA256, BLAKE2B };

class HashContext {
public:
    HashContext();
    explicit HashContext(HashAlgorithm algorithm, std::size_t digest_size = 64);
    ~HashContext();

    HashContext(HashContext&&) noexcept;
    HashContext& operator=(HashContext&&) noexcept;
    HashContext(const HashContext&) = delete;
    HashContext& operator=(const HashContext&) = delete;

    static HashContext md5();
    static HashContext sha1();
    static HashContext sha256();
    static HashContext blake2b(std::size_t digest_size = 64);
    static bool blake2b_self_test();

    void update(const void* data, std::size_t size);
    void update(const StdBytes& data) { update(data.data(), data.size()); }
    StdBytes digest() const;
    StdString hexdigest() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

StdBytes hash_bytes(HashAlgorithm algorithm, const StdBytes& data, std::size_t digest_size = 64);
StdString hash_hex(HashAlgorithm algorithm, const StdBytes& data, std::size_t digest_size = 64);
StdBytes hash_file(HashAlgorithm algorithm, const StdString& utf8_path,
                   std::size_t chunk_size = 32U * 1024U, std::size_t digest_size = 64);
StdString hash_file_hex(HashAlgorithm algorithm, const StdString& utf8_path,
                        std::size_t chunk_size = 32U * 1024U, std::size_t digest_size = 64);

inline StdBytes md5(const StdBytes& data) { return hash_bytes(HashAlgorithm::MD5, data); }
inline StdBytes sha1(const StdBytes& data) { return hash_bytes(HashAlgorithm::SHA1, data); }
inline StdBytes sha256(const StdBytes& data) { return hash_bytes(HashAlgorithm::SHA256, data); }
inline StdString md5_hex(const StdBytes& data) { return hash_hex(HashAlgorithm::MD5, data); }
inline StdString sha1_hex(const StdBytes& data) { return hash_hex(HashAlgorithm::SHA1, data); }
inline StdString sha256_hex(const StdBytes& data) { return hash_hex(HashAlgorithm::SHA256, data); }
inline StdBytes blake2b(const StdBytes& data, std::size_t digest_size = 64) { return hash_bytes(HashAlgorithm::BLAKE2B, data, digest_size); }
inline StdString blake2b_hex(const StdBytes& data, std::size_t digest_size = 64) { return hash_hex(HashAlgorithm::BLAKE2B, data, digest_size); }

StdString base64_encode(const StdBytes& data, bool url_safe = false, bool padding = true);
StdBytes base64_decode(const StdString& text, bool url_safe = false, bool strict = true);

using ColorTriple = std::array<double, 3>;
ColorTriple rgb_to_hsv(const ColorTriple& rgb);
ColorTriple hsv_to_rgb(const ColorTriple& hsv);
ColorTriple rgb_to_hls(const ColorTriple& rgb);
ColorTriple hls_to_rgb(const ColorTriple& hls);
ColorTriple rgb_to_yiq(const ColorTriple& rgb);
ColorTriple yiq_to_rgb(const ColorTriple& yiq);

bool fnmatchcase(const StdString& name, const StdString& pattern);
bool fnmatch(const StdString& name, const StdString& pattern);
StdString fnmatch_translate(const StdString& pattern);

class ConfigParser {
public:
    using Values = std::map<StdString, StdString>;

    explicit ConfigParser(const Values& defaults = Values{});

    void clear();
    bool read(const StdString& utf8_path);
    void read_string(const StdString& utf8_text, const StdString& source = "<string>");

    bool has_section(const StdString& section) const;
    bool has_option(const StdString& section, const StdString& option) const;
    std::vector<StdString> sections() const;
    Values items(const StdString& section) const;
    StdString get(const StdString& section, const StdString& option,
                  const StdString& fallback = StdString()) const;
    StdString get_required(const StdString& section, const StdString& option) const;
    void set(const StdString& section, const StdString& option, const StdString& value);

private:
    Values m_defaults;
    std::map<StdString, Values> m_sections;
};

} // namespace cw::lib
