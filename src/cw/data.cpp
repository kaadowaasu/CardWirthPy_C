// cw/data.py に対応するデータ管理処理。
#define _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING
#include "cw/lib/exception_origin.hpp"
#include "cw/lib/exception_future.hpp"
#include "cw/data.hpp"
#include "cw/content.hpp"
#include "app/debug_control_server.hpp"
#include "cw/cw.hpp"
#include "cw/event.hpp"
#include "cw/eventhandler.hpp"
#include "cw/lib/python_xml_name.hpp"
#include "cw/lib/python_integer.hpp"
#include "cw/lib/python_float.hpp"
#include "cw/lib/python_percent_string.hpp"
#include "cw/lib/read_stream.hpp"
#include "cw/lib/binary_input.hpp"
#include "cw/lib/element_path.hpp"
#include "cw/lib/python_xml_single_byte.hpp"
#include "cw/header.hpp"
#include "cw/image.hpp"
#include "cw/debug/logging.hpp"
#include "cw/character.hpp"
#include "cw/calculator.hpp"
#include "cw/advlog.hpp"
#include "cw/argparser.hpp"
#include "cw/binary/cwscenario.hpp"
#include "cw/binary/next/next_scenario_converter.hpp"
#include "cw/binary/image.hpp"
#include "cw/binary/util.hpp"
#include "cw/animation.hpp"
#include "cw/setting.hpp"
#include "cw/features.hpp"
#include "cw/frame.hpp"
#include "cw/sprite/card.hpp"
#include "cw/sprite/background.hpp"
#include "cw/sprite/message.hpp"
#include "cw/sprite/statusbar.hpp"
#include "cw/thread.hpp"
#include "cw/util.hpp"
#include "cw/xmlcreater.hpp"
#include "cw/yadodb.hpp"
#include "cw/scenariodb.hpp"
#include "cw/lib/encoding.hpp"
#include "platform/storage/storage_mount.hpp"

#include <expat.h>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <future>
#include <exception>
#include <atomic>
#include <thread>
#include <iterator>
#include <mutex>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <regex>
#include <sstream>
#include <cstdint>
#include <stdexcept>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif !defined(__EMSCRIPTEN__)
#include <sys/resource.h>
#endif

namespace cw::data {
static std::shared_ptr<::cw::data::CWPyElement> xml2element_impl(::cw::lib::String arg_path, ::cw::lib::String arg_tag, std::optional<::cw::lib::Value> arg_stream, std::optional<::cw::lib::String> arg_xmltext, bool arg_nocache, std::optional<std::map<::cw::lib::String, ::cw::lib::String>> arg_rootattrs, std::map<::cw::lib::String, ::cw::lib::String>* output);
static std::shared_ptr<::cw::data::CWPyElement> read_yadoxml_impl(
    ::cw::lib::String path, ::cw::lib::String yado_root, ::cw::lib::String temp_root,
    ::cw::lib::String tag,
    std::optional<std::map<::cw::lib::String, ::cw::lib::String>> rootattrs,
    ::cw::lib::String base_directory, std::map<::cw::lib::String, ::cw::lib::String>* output);
static std::shared_ptr<::cw::data::CWPyElement> yadoxml2element_impl(::cw::lib::String arg_path, ::cw::lib::String arg_tag, std::optional<std::map<::cw::lib::String, ::cw::lib::String>> arg_rootattrs, std::map<::cw::lib::String, ::cw::lib::String>* output);
static std::shared_ptr<::cw::data::CWPyElementTree> yadoxml2etree_impl(::cw::lib::String arg_path, ::cw::lib::String arg_tag, std::optional<std::map<::cw::lib::String, ::cw::lib::String>> arg_rootattrs, ::cw::lib::String saved_root, ::cw::lib::String temp_root, std::map<::cw::lib::String, ::cw::lib::String>* output);
namespace {

thread_local std::map<::cw::lib::String, ::cw::lib::String> s_last_rootattrs;

// Pythonの可変なrootattrs辞書に対応するネイティブの参照保持
// 。 解析中は呼び出し元がマップを保持する。Valueを再利用して解析器の
// 配置と旧optional値のABIを保ち、 全経路で同じ解析器を使う。
class RootAttributesOutput final : public ::cw::lib::Object {
public:
    explicit RootAttributesOutput(std::map<::cw::lib::String, ::cw::lib::String>& value) : attributes(&value) {}
    std::map<::cw::lib::String, ::cw::lib::String>* attributes;
};
std::map<::cw::lib::String, ::cw::lib::String>* borrowed_rootattrs(const ::cw::lib::Value& value) {
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
    const auto output = object && *object ? std::dynamic_pointer_cast<RootAttributesOutput>(*object) : nullptr;
    return output ? output->attributes : nullptr;
}
void merge_rootattrs(std::map<::cw::lib::String, ::cw::lib::String>* output, const std::map<::cw::lib::String, ::cw::lib::String>& attributes) {
    if (output) for (const auto& [key, value] : attributes) (*output)[key] = value;
}


// 宿・パーティの所持金設定はともに money::add を
// 使う。 先に元の残高を制限し、ネイティブの九桁の範囲内で飽
// 和加算する。 Python版は任意精度の加算後に七桁へ制
// 限するため、元の残高が範囲外なら結果が異なる場合がある。
::cw::lib::Integer add_money_clamped(::cw::lib::Integer value,
                                     ::cw::lib::Integer delta)
{
    return ::cw::lib::money::add(value, delta);
}

::cw::lib::String path_utf8(const std::filesystem::path& path)
{
    return ::cw::lib::encoding::path_to_utf8(path);
}

::cw::lib::String relative_path_utf8(const std::filesystem::path& path,
                                     const std::filesystem::path& base)
{
    auto relative = path.lexically_relative(base);
    // Storageが ./Music/... のような現
    // 在ディレクトリの明示要素を先頭に返す場合がある。 Python
    // のrelpathはそれを出さず、素材名は保存・
    // 比較に使うため、 その仮の要素だけを取り除く。
    auto component = relative.begin();
    if (component != relative.end() && *component == ".") {
        std::filesystem::path normalized;
        for (++component; component != relative.end(); ++component)
            normalized /= *component;
        relative = std::move(normalized);
    }
    return path_utf8(relative);
}

std::optional<::cardwirth::platform::StorageStat> mounted_or_native_stat(
    const std::filesystem::path& path)
{
    if (::cardwirth::platform::is_mounted_user_path(path)) {
        const auto status = ::cardwirth::platform::stat_mounted_user_path(path, true);
        return status;
    }
    std::error_code ec;
    const auto status = std::filesystem::status(path, ec);
    if (ec == std::errc::no_such_file_or_directory || status.type() == std::filesystem::file_type::not_found)
        return ::cardwirth::platform::StorageStat{};
    if (ec) throw std::filesystem::filesystem_error("cannot stat inn path", path, ec);
    ::cardwirth::platform::StorageStat result;
    result.type = std::filesystem::is_regular_file(status)
        ? ::cardwirth::platform::StorageEntryType::File
        : std::filesystem::is_directory(status)
            ? ::cardwirth::platform::StorageEntryType::Directory
            : ::cardwirth::platform::StorageEntryType::Other;
    return result;
}

bool mounted_or_native_mkdir(const std::filesystem::path& path)
{
    if (::cardwirth::platform::is_mounted_user_path(path))
        return ::cardwirth::platform::make_mounted_user_directory(path, true);
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    return !ec;
}

std::vector<std::filesystem::path> mounted_or_native_list(
    const std::filesystem::path& path)
{
    if (::cardwirth::platform::is_mounted_user_path(path)) {
        std::vector<std::filesystem::path> result;
        const auto status = ::cardwirth::platform::stat_mounted_user_path(path, true);
        if (!status || !status->exists()) return result;
        if (const auto entries = ::cardwirth::platform::list_mounted_user_path(path, true))
            for (const auto& entry : *entries) result.push_back(entry.path);
        else {
            throw std::runtime_error("failed to enumerate mounted directory: " + ::cw::lib::encoding::path_to_utf8(path));
        }
        return result;
    }
    std::vector<std::filesystem::path> result;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(path, ec), end; !ec && it != end;
         it.increment(ec)) result.push_back(it->path());
    if (ec && ec != std::errc::no_such_file_or_directory)
        throw std::filesystem::filesystem_error("failed to enumerate directory", path, ec);
    return result;
}

// Python版の四つの公開の宿パス列挙と同様、対象ディレクトリ
// を必須とする。 未存在・ディレクトリ以外なら、空・途中の一覧を
// 成功として返さず失敗とする。 両保存経路でこの方針を共有する。
std::vector<std::filesystem::path> required_inn_directory_list(
    const std::filesystem::path& path)
{
    const auto status = mounted_or_native_stat(path);
    if (!status || !status->exists())
        throw std::filesystem::filesystem_error("required inn directory is missing", path,
            std::make_error_code(std::errc::no_such_file_or_directory));
    if (!status->is_directory())
        throw std::filesystem::filesystem_error("required inn path is not a directory", path,
            std::make_error_code(std::errc::not_a_directory));
    return mounted_or_native_list(path);
}

bool mounted_or_native_is_file(const std::filesystem::path& path)
{
    const auto status = mounted_or_native_stat(path);
    return status && status->is_file();
}

bool mounted_or_native_is_directory(const std::filesystem::path& path)
{
    const auto status = mounted_or_native_stat(path);
    return status && status->is_directory();
}

bool copy_into_user_work_area(const std::filesystem::path& source,
                              const std::filesystem::path& destination)
{
    if (!mounted_or_native_is_file(source)) return false;
    const bool source_mounted = ::cardwirth::platform::is_mounted_user_path(source);
    const bool destination_mounted = ::cardwirth::platform::is_mounted_user_path(destination);
    if (source_mounted) {
        // シナリオログの整理で、暗号化ユーザーデータをネイ
        // ティブの平文ファイルに変えない。 暗号化セッショ
        // ンでは、両端ともマウントした論理ツリーに属する。
        if (!destination_mounted) return false;
        return ::cardwirth::platform::copy_mounted_user_path(
            source, destination, true);
    }
    if (destination_mounted) {
        std::ifstream input(source, std::ios::binary);
        if (!input) return false;
        std::vector<char> chars((std::istreambuf_iterator<char>(input)),
                                std::istreambuf_iterator<char>());
        if (input.bad()) return false;
        std::vector<std::byte> bytes(chars.size());
        std::transform(chars.begin(), chars.end(), bytes.begin(),
            [](char value) { return static_cast<std::byte>(static_cast<unsigned char>(value)); });
        return ::cardwirth::platform::write_mounted_user_file(
            destination, bytes, true);
    }
    std::error_code ec;
    std::filesystem::create_directories(destination.parent_path(), ec);
    if (ec) return false;
    std::filesystem::copy_file(source, destination,
        std::filesystem::copy_options::overwrite_existing, ec);
    return !ec;
}

std::vector<std::filesystem::path> mounted_files_recursive(
    const std::filesystem::path& directory)
{
    std::vector<std::filesystem::path> result;
    std::vector<std::filesystem::path> pending{directory};
    while (!pending.empty()) {
        auto current = std::move(pending.back());
        pending.pop_back();
        const auto entries = ::cardwirth::platform::list_mounted_user_path(current);
        if (!entries) throw std::runtime_error("failed to enumerate mounted directory: " + ::cw::lib::encoding::path_to_utf8(current));
        for (const auto& child : *entries) {
            // list() は各行を認証・復号済みなので、そのstatを再利
            // 用する。 一時保存項目ごとの二度目のキー検索・復号を避ける。
            if (child.stat.is_directory()) pending.push_back(child.path);
            else if (child.stat.is_file()) result.push_back(child.path);
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

// 現在ツリーの各結果を整列し、ネイティブの最初の一致・上書き先の選択を安定
// させる。 Pythonのos.walkの発見順を再現するものではない。
std::vector<std::filesystem::path> mounted_or_native_files_recursive(
    const std::filesystem::path& directory)
{
    if (::cardwirth::platform::is_mounted_user_path(directory))
        return mounted_files_recursive(directory);
    std::vector<std::filesystem::path> result;
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(directory, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file(ec)) result.push_back(it->path());
        if (ec) ec.clear();
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::optional<::cw::lib::Bytes> read_mounted_xml(
    const std::filesystem::path& path, bool& handled) {
    handled = ::cardwirth::platform::is_mounted_user_path(path);
    if (!handled) return std::nullopt;
    const auto source = ::cardwirth::platform::read_mounted_user_file(path);
    if (!source) return std::nullopt;
    ::cw::lib::Bytes result;
    result.reserve(source->size());
    for (const auto value : *source)
        result.push_back(std::to_integer<std::uint8_t>(value));
    return result;
}

// ScenarioData の Value 保持フィールドを扱う小さな共通ヘルパー。
// Python 側の動的属性を単に空値へ潰さず、パスと更新時刻を同じ規則で扱う。
::cw::lib::String scenario_value_string(const ::cw::lib::Value& value) {
    if (const auto* text = std::get_if<::cw::lib::String>(&value.storage())) return *text;
    return {};
}

::cw::lib::String scenario_lower(::cw::lib::String value) {
    return ::cw::lib::encoding::lower(value);
}

::cw::lib::String scenario_filename_normcase(::cw::lib::String value) {
#if defined(_WIN32)
    // os.path.normcase はOS固有の処理で、WindowsはUnicode
    // の大小文字と区切りを揃え、POSIXは両方を保つ。 登録・確認に同じキーを使う。
    std::replace(value.begin(), value.end(), '/', '\\');
    return ::cw::lib::encoding::python_lower(value);
#else
    return value;
#endif
}



} // namespace

::cw::lib::Real parse_python_float(const ::cw::lib::String& value) {
    // 元のPython 3.8・UCD12.1のNd分類を、int・Decimal
    // と共有する。 floatは下で独自の構文・空白規則を使う。
    const auto codepoints = ::cw::lib::encoding::utf8_to_utf32(value,
        ::cw::lib::encoding::ByteOrder::Little, ::cw::lib::encoding::ErrorPolicy::Strict, false);
    ::cw::lib::String text;
    for (std::size_t i = 0; i < codepoints.size(); i += 4) {
        const auto cp = static_cast<std::uint32_t>(codepoints[i]) |
            (static_cast<std::uint32_t>(codepoints[i + 1]) << 8) |
            (static_cast<std::uint32_t>(codepoints[i + 2]) << 16) |
            (static_cast<std::uint32_t>(codepoints[i + 3]) << 24);
        if (cp < 128) { text.push_back(static_cast<char>(cp)); continue; }
        if (cp == 0x85 || cp == 0xa0 || cp == 0x1680 ||
            (cp >= 0x2000 && cp <= 0x200a) || cp == 0x2028 || cp == 0x2029 ||
            cp == 0x202f || cp == 0x205f || cp == 0x3000) {
            text.push_back(' '); continue;
        }
        const auto digit = ::cw::lib::python_decimal_digit(cp);
        if (!digit) throw std::invalid_argument("invalid numeric value");
        text.push_back(static_cast<char>('0' + *digit));
    }
    static const std::regex syntax(
        R"(^[ \t\n\r\f\v]*[+-]?(?:(?:[0-9](?:_?[0-9])*(?:\.(?:[0-9](?:_?[0-9])*)?)?|\.[0-9](?:_?[0-9])*)(?:[eE][+-]?[0-9](?:_?[0-9])*)?|inf(?:inity)?|nan)[ \t\n\r\f\v]*$)",
        std::regex::ECMAScript | std::regex::icase);
    if (!std::regex_match(text, syntax)) throw std::invalid_argument("invalid numeric value");
    text.erase(std::remove(text.begin(), text.end(), '_'), text.end());
    const auto begin = text.find_first_not_of(" \t\n\r\f\v");
    const auto end = text.find_last_not_of(" \t\n\r\f\v");
    text = text.substr(begin, end - begin + 1);
    const bool negative = text.front() == '-';
    const auto magnitude = scenario_lower(text.substr(text.front() == '+' || negative ? 1 : 0));
    if (magnitude == "inf" || magnitude == "infinity")
        return std::copysign(std::numeric_limits<double>::infinity(), negative ? -1.0 : 1.0);
    if (magnitude == "nan")
        return std::copysign(std::numeric_limits<double>::quiet_NaN(), negative ? -1.0 : 1.0);
    // from_chars はロケール非依存でWASMでも利用できる
    // 。 UI・音声スレッドが並行変換するため、プロセスのロケールは
    // 使わない。 Pythonで有効な末尾小数点（1.）・先頭プラス
    // を拒否する標準ライブラリもあるため、値を変えずに正規化する。
    auto conversion_text = text;
    if (!conversion_text.empty() && conversion_text.front() == '+')
        conversion_text.erase(conversion_text.begin());
    const auto exponent_offset = conversion_text.find_first_of("eE");
    const auto mantissa_end = exponent_offset == ::cw::lib::String::npos
        ? conversion_text.size() : exponent_offset;
    if (mantissa_end > 0 && conversion_text[mantissa_end - 1] == '.')
        conversion_text.insert(mantissa_end, 1, '0');
    double parsed = 0.0;
    bool conversion_out_of_range = false;
#if defined(__ANDROID__)
    // Android NDKのlibc++は、浮動小数点の std::from_chars
    // を提供しない。 AndroidのCロケールは共通のC数値形
    // 式に固定されるため、 上で正規化したASCII入力には、bionic
    // のstrtodをロケール非依存の代替として使う。
    char* conversion_end = nullptr;
    errno = 0;
    parsed = std::strtod(conversion_text.c_str(), &conversion_end);
    if (conversion_end != conversion_text.data() + conversion_text.size() ||
        errno == EINVAL)
        throw std::runtime_error("numeric conversion failed");
    conversion_out_of_range = errno == ERANGE;
#elif defined(__APPLE__)
    // ここで対応するAppleの配布対象では、libc++に浮動小数点の
    // std::from_chars がない。 共通UIの解析と同じ
    // classicロケールのストリームを使い、ロケール非依存に保つ。
    std::istringstream conversion_stream{conversion_text};
    conversion_stream.imbue(std::locale::classic());
    conversion_stream >> parsed;
    if (conversion_stream.fail()) {
        // 構文は上で検証済みなので、数値取得の失敗は不正入力
        // ではなくIEEE-754の範囲の問題として扱う。
        conversion_out_of_range = true;
    } else {
        char trailing{};
        if (conversion_stream >> trailing)
            throw std::runtime_error("numeric conversion failed");
    }
#else
    const auto converted = std::from_chars(conversion_text.data(),
                                           conversion_text.data() + conversion_text.size(),
                                           parsed, std::chars_format::general);
    if (converted.ptr != conversion_text.data() + conversion_text.size() ||
        converted.ec == std::errc::invalid_argument)
        throw std::runtime_error("numeric conversion failed");
    conversion_out_of_range = converted.ec == std::errc::result_out_of_range;
#endif
    if (conversion_out_of_range) {
        // Pythonのfloatと同様、十進のオーバーフローは無限大、ア
        // ンダーフローは符号付きゼロにする。 from_charsは両方を
        // 同じエラーで返すため、その区別にだけ正規化した十進の桁数を使う。
        const auto unsigned_begin = text.data() + (text.front() == '+' || negative ? 1 : 0);
        const auto unsigned_end = text.data() + text.size();
        const auto exponent_mark = std::find_if(unsigned_begin, unsigned_end,
            [](char c) { return c == 'e' || c == 'E'; });
        const auto decimal_mark = std::find(unsigned_begin, exponent_mark, '.');
        const auto digits_before_decimal = static_cast<long long>(
            (decimal_mark == exponent_mark ? exponent_mark : decimal_mark) - unsigned_begin);
        long long first_nonzero = 0;
        bool nonzero = false;
        for (auto it = unsigned_begin; it != exponent_mark; ++it) {
            if (*it == '.') continue;
            if (*it != '0') { nonzero = true; break; }
            ++first_nonzero;
        }
        if (!nonzero) return std::copysign(0.0, negative ? -1.0 : 1.0);
        long long exponent = 0;
        if (exponent_mark != unsigned_end) {
            // Python版が許す十進指数の明示的なプラスを、整数のfrom_chars
            // は受け付けない。 共通の範囲判定ではその符号を飛ば
            // し、巨大な正の指数をゼロと誤認しないようにする。
            auto exponent_begin = exponent_mark + 1;
            if (*exponent_begin == '+') ++exponent_begin;
            const auto exponent_result = std::from_chars(exponent_begin, unsigned_end, exponent);
            if (exponent_result.ec == std::errc::result_out_of_range)
                exponent = *exponent_begin == '-' ? std::numeric_limits<long long>::min()
                                                 : std::numeric_limits<long long>::max();
            else if (exponent_result.ec != std::errc{} || exponent_result.ptr != unsigned_end)
                throw std::runtime_error("numeric exponent conversion failed");
        }
        const auto decimal_order = exponent > 10000 ? exponent :
            exponent < -10000 ? exponent : exponent + digits_before_decimal - first_nonzero - 1;
        if (decimal_order >= 0)
            return std::copysign(std::numeric_limits<double>::infinity(), negative ? -1.0 : 1.0);
        return std::copysign(0.0, negative ? -1.0 : 1.0);
    }
    return parsed == 0.0 ? std::copysign(0.0, negative ? -1.0 : 1.0) : parsed;
}

::cw::lib::Integer parse_python_integer_float(const ::cw::lib::String& value) {
    const auto parsed = parse_python_float(value);
    if (std::isnan(parsed))
        throw std::invalid_argument("cannot convert float NaN to integer");
    // Pythonの int(float(text)) と同様、ゼロ方向に切り捨
    // てる。 ただしネイティブIntegerは有限範囲なので、C++の未定義な変
    // 換やWASMのトラップより前に拒否し、 全呼び出し元でこの境界を共有する。
    const auto limit = std::ldexp(1.0, std::numeric_limits<::cw::lib::Integer>::digits);
    if (!std::isfinite(parsed) || parsed < -limit || parsed >= limit)
        throw std::out_of_range("integer value is outside the representable range");
    return static_cast<::cw::lib::Integer>(parsed);
}

namespace {

void log_scenario_data(const ::cw::lib::String& message) {
#if defined(CARDWIRTH_LOG_ENABLED)
    ::cardwirth::debug::log("ScenarioData: " + message);
#else
    (void)message;
#endif
}

::cw::lib::Real scenario_mtime(const ::cw::lib::String& path, bool required = false) {
    const auto native = ::cw::lib::encoding::path_from_utf8(path);
    if (::cardwirth::platform::is_mounted_user_path(native)) {
        const auto status = ::cardwirth::platform::stat_mounted_user_path(native);
        if (required && (!status || !status->exists()))
            throw std::runtime_error(path + ": cannot read XML modification time from mounted storage");
        return status && status->exists()
            ? static_cast<::cw::lib::Real>(status->modified) / 1000000000.0 : 0.0;
    }
    std::error_code ec;
    const auto file_time = std::filesystem::last_write_time(native, ec);
    if (ec) {
        if (required) throw std::filesystem::filesystem_error("cannot read XML modification time", native, ec);
        return 0.0;
    }
    // Storage・シナリオデータベースと同じ安定したファイル時計の変換で
    // os.path.getmtime に合わせる。 時計を取り直さない。
    return static_cast<::cw::lib::Real>(
        ::cardwirth::platform::storage_timestamp_from_file_time(file_time)) / 1'000'000'000.0;
}

const ::cw::lib::String& scenario_value_string(const ::cw::lib::String& value) noexcept {
    return value;
}

std::filesystem::path scenario_party_debugdir() {
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_ydata || !*::cw::g_cwpy->m_ydata ||
        !(*::cw::g_cwpy->m_ydata)->m_party) return {};
    const auto party_path = scenario_value_string((*::cw::g_cwpy->m_ydata)->m_party.value()->m_path);
    if (party_path.empty()) return {};
    const auto normalized_party_path = ::cw::util::normalize_path(party_path);
    auto dpath = path_utf8(std::filesystem::u8path(normalized_party_path).parent_path());
    const auto tempdir = ::cw::g_cwpy->m_tempdir;
    const auto yadodir = ::cw::g_cwpy->m_yadodir;
    if (const auto paths = ::cw::util::resolve_yado_paths(dpath, yadodir, tempdir))
        dpath = paths->first;
    const auto result = std::filesystem::u8path(::cw::util::join_paths({
        ::cw::lib::Value(dpath), ::cw::lib::Value("Debug")}));
#if defined(CARDWIRTH_LOG_ENABLED)
    log_scenario_data("party debug path: party=" + normalized_party_path +
                      " path=" + path_utf8(result));
#endif
    return result;
}

std::map<::cw::lib::String, ::cw::lib::String> scenario_datetime_attrs(const ::cw::lib::DateTime& point) {
    const auto tm = point.calendar();
    const auto padded = [](int value, int width) {
        std::ostringstream result;
        result << std::setfill('0') << std::setw(width) << value;
        return result.str();
    };
    return {
        {"year", padded(tm.tm_year + 1900, 4)}, {"month", padded(tm.tm_mon + 1, 2)},
        {"day", padded(tm.tm_mday, 2)}, {"hour", padded(tm.tm_hour, 2)},
        {"minute", padded(tm.tm_min, 2)}, {"second", padded(tm.tm_sec, 2)}
    };
}

::cw::lib::String escape_xml(const ::cw::lib::String& value, bool attribute) {
    ::cw::lib::String result;
    for (char ch : value) {
        switch (ch) {
        case '&': result += "&amp;"; break;
        case '<': result += "&lt;"; break;
        case '>': result += "&gt;"; break;
        case '"': result += attribute ? "&quot;" : "\""; break;
        case '\'': result.push_back('\''); break;
        case '\t': result += attribute ? "&#09;" : "\t"; break;
        case '\n': result += attribute ? "&#10;" : "\n"; break;
        case '\r': result += attribute ? "&#13;" : "\r"; break;
        default: result.push_back(ch); break;
        }
    }
    return result;
}

void write_element_xml(std::ostringstream& output,
                       const std::shared_ptr<CWPyElement>& element) {
    if (!element) return;
    output << '<' << element->m_tag;
    for (const auto& key : element->m_attributes.insertion_keys()) {
        const auto& value = element->m_attributes.at(key);
        output << ' ' << key << "=\"" << escape_xml(value, true) << '\"';
    }
    if (element->m_children.empty() && element->m_text.empty()) {
        output << " />";
    } else {
        output << '>';
        if (!element->m_text.empty()) output << escape_xml(element->m_text, false);
        if (!element->m_children.empty()) {
            for (const auto& child : element->m_children) {
                write_element_xml(output, child);
            }
        }
        output << "</" << element->m_tag << '>';
    }
    if (!element->m_tail.empty()) output << escape_xml(element->m_tail, false);
}

::cw::lib::String normalize_python_xml_declaration(::cw::lib::String source) {
    std::size_t offset = source.starts_with("\xef\xbb\xbf") ? 3 : 0;
    if (source.compare(offset, 5, "<?xml") != 0 || offset + 5 >= source.size() ||
        std::string_view(" \t\r\n").find(source[offset + 5]) == std::string_view::npos)
        return source;
    const auto end = source.find("?>", offset + 5);
    if (end == ::cw::lib::String::npos) return source;

    // PythonのExpatは、空・非標準のバージョン値など、新し
    // いExpatが拒否する宣言も受け付ける。 Python版の項目
    // 順を検証し、文字コードとstandaloneの指定を保ちながら
    // 、 受理したヘッダーを厳しいネイティブ解析器向けに正規化する。
    std::vector<std::pair<::cw::lib::String, ::cw::lib::String>> attributes;
    const auto body = std::string_view(source).substr(offset + 5, end - offset - 5);
    std::size_t position = 0;
    const auto is_space = [](char value) {
        return value == ' ' || value == '\t' || value == '\r' || value == '\n';
    };
    bool valid = true;
    while (position < body.size()) {
        while (position < body.size() && is_space(body[position])) ++position;
        if (position == body.size()) break;
        const auto name_begin = position;
        while (position < body.size() && ((body[position] >= 'A' && body[position] <= 'Z') ||
               (body[position] >= 'a' && body[position] <= 'z'))) ++position;
        if (position == name_begin) { valid = false; break; }
        ::cw::lib::String name(body.substr(name_begin, position - name_begin));
        while (position < body.size() && is_space(body[position])) ++position;
        if (position == body.size() || body[position++] != '=') { valid = false; break; }
        while (position < body.size() && is_space(body[position])) ++position;
        if (position == body.size() || (body[position] != '\'' && body[position] != '"')) { valid = false; break; }
        const char quote = body[position++];
        const auto value_begin = position;
        while (position < body.size() && body[position] != quote) ++position;
        if (position == body.size()) { valid = false; break; }
        ::cw::lib::String value(body.substr(value_begin, position - value_begin));
        ++position;
        if (position < body.size() && !is_space(body[position])) { valid = false; break; }
        attributes.emplace_back(std::move(name), std::move(value));
    }

    if (attributes.empty() || attributes.size() > 3 || attributes.front().first != "version") valid = false;
    if (valid) {
        const auto& version = attributes.front().second;
        if (!std::all_of(version.begin(), version.end(), [](unsigned char value) {
                return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') ||
                    (value >= '0' && value <= '9') || value == '.' || value == '_' || value == '-';
            })) valid = false;
    }
    if (valid && attributes.size() >= 2) {
        const auto& [name, value] = attributes[1];
        if (name == "encoding") {
            if (value.empty() || !((value.front() >= 'A' && value.front() <= 'Z') ||
                                   (value.front() >= 'a' && value.front() <= 'z')) ||
                !std::all_of(value.begin(), value.end(), [](unsigned char ch) {
                    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                        (ch >= '0' && ch <= '9') || ch == '.' || ch == '_' || ch == '-';
                })) valid = false;
        } else if (name != "standalone" || (value != "yes" && value != "no") || attributes.size() != 2) {
            valid = false;
        }
    }
    if (valid && attributes.size() == 3 &&
        (attributes[1].first != "encoding" || attributes[2].first != "standalone" ||
         (attributes[2].second != "yes" && attributes[2].second != "no"))) valid = false;
    if (!valid) return source;

    ::cw::lib::String normalized = "<?xml version=\"";
    const auto& version = attributes.front().second;
    normalized += version == "1.0" || version == "1.1" ? version : "1.0";
    normalized += '"';
    for (std::size_t i = 1; i < attributes.size(); ++i) {
        normalized += attributes[i].first == "encoding" ? " encoding=\"" : " standalone=\"";
        normalized += attributes[i].second;
        normalized += '"';
    }
    normalized += "?>";
    source.replace(offset, end + 2 - offset, normalized);
    return source;
}

// 元の実装と同様、内部DTDの実体・既定属性も含め、全XMLに
// Expatを使う。 XMLバイト列は共通の参照用文字コード・
// 復旧方針を通過済みなので、その後は一つの共通解析器を使う。
class XmlSyntaxError final : public std::invalid_argument {
public: using std::invalid_argument::invalid_argument;
};

class ExpatXmlReader {
public:
    explicit ExpatXmlReader(::cw::lib::String source, SimpleXmlParser* callbacks = nullptr)
        : m_source(normalize_python_xml_declaration(std::move(source))), m_callbacks(callbacks),
          m_parser(XML_ParserCreate("UTF-8"), &XML_ParserFree) {
        if (!m_parser) throw std::bad_alloc{};
        XML_SetUserData(m_parser.get(), this);
        XML_SetElementHandler(m_parser.get(), &start_element, &end_element);
        XML_SetCharacterDataHandler(m_parser.get(), &character_data);
        // Pythonの ParserCreate と同様、外部実体の読み
        // 込み処理を登録せず、外部パラメーター実体の解析も有効にしない。
        // 内部実体とDTDの既定値は、Expatの通常の共通動作に任せる。
    }

    const std::map<::cw::lib::String, ::cw::lib::String>& document_attributes() const {
        return m_documentattrs;
    }

    std::shared_ptr<CWPyElement> parse() {
        std::size_t offset = 0;
        do {
            const auto length = std::min<std::size_t>(m_source.size() - offset,
                static_cast<std::size_t>(std::numeric_limits<int>::max()));
            const bool final = offset + length == m_source.size();
            const auto status = XML_Parse(m_parser.get(), m_source.data() + offset,
                                         static_cast<int>(length), final);
            // ExpatのCの呼び出し枠を越えて、C++例外を出さない。 処理関数が
            // 対象の早期完了・エラーを記録した後は、後続コールバックは何もしない。
            if (m_exception) ::cw::lib::rethrow_exception(m_exception);
            if (status == XML_STATUS_ERROR) {
                const auto error = XML_GetErrorCode(m_parser.get());
                const auto* reason = XML_ErrorString(error);
                throw XmlSyntaxError(::cw::lib::String("XML ") +
                    (reason ? reason : "parse error") + " at byte " +
                    std::to_string(XML_GetCurrentByteIndex(m_parser.get())) +
                    " (line " + std::to_string(XML_GetCurrentLineNumber(m_parser.get())) +
                    ", column " + std::to_string(XML_GetCurrentColumnNumber(m_parser.get()) + 1) + ")");
            }
            offset += length;
        } while (offset < m_source.size());
        if (!m_root) throw std::invalid_argument("XML root is missing");
        return m_root;
    }

private:
    ::cw::lib::String m_source;
    SimpleXmlParser* m_callbacks{};
    std::unique_ptr<XML_ParserStruct, decltype(&XML_ParserFree)> m_parser;
    ::cw::lib::ExceptionPtr m_exception;
    std::map<::cw::lib::String, ::cw::lib::String> m_documentattrs;
    std::shared_ptr<CWPyElement> m_root;
    std::vector<std::shared_ptr<CWPyElement>> m_nodes;

    template<class Function>
    void invoke(Function&& function) noexcept {
        if (m_exception) return;
        try {
            function();
        } catch (...) {
            m_exception = ::cw::lib::capture_exception();
            XML_StopParser(m_parser.get(), XML_FALSE);
        }
    }

    static void XMLCALL start_element(void* owner, const XML_Char* tag,
                                      const XML_Char** attributes) noexcept {
        auto& self = *static_cast<ExpatXmlReader*>(owner);
        self.invoke([&] {
            ::cw::lib::XmlAttributes attrs;
            for (auto values = attributes; *values; values += 2)
                attrs.emplace(values[0], values[1]);
            if (self.m_nodes.empty()) self.m_documentattrs = attrs;
            auto element = std::make_shared<CWPyElement>(tag, "", attrs);
            if (self.m_nodes.empty()) self.m_root = element;
            else self.m_nodes.back()->append(element);
            self.m_nodes.push_back(element);
            if (self.m_callbacks) self.m_callbacks->start_element(tag, std::move(attrs));
        });
    }

    static void XMLCALL end_element(void* owner, const XML_Char* tag) noexcept {
        auto& self = *static_cast<ExpatXmlReader*>(owner);
        self.invoke([&] {
            if (self.m_callbacks) self.m_callbacks->end_element(tag);
            self.m_nodes.pop_back();
        });
    }

    static void XMLCALL character_data(void* owner, const XML_Char* data, int length) noexcept {
        auto& self = *static_cast<ExpatXmlReader*>(owner);
        self.invoke([&] {
            ::cw::lib::String text(data, static_cast<std::size_t>(length));
            if (self.m_callbacks) self.m_callbacks->char_data(text);
            if (!self.m_nodes.empty()) {
                const auto& node = self.m_nodes.back();
                if (node->m_children.empty()) node->m_text += text;
                else node->m_children.back()->m_tail += text;
            }
        });
    }
};

std::shared_ptr<CWPyElement> find_target_tag(const std::shared_ptr<CWPyElement>& root, const ::cw::lib::String& tag) {
    if (!root || tag.empty()) return root;
    // PythonのSimpleXmlParserと同様、位置はルート相対の文字列
    // として比較する。 子孫検索やElementTreeのXPath式ではない。
    const auto slash = tag.find('/');
    const auto name = tag.substr(0, slash);
    for (const auto& child : root->m_children) {
        if (!child || child->m_tag != name) continue;
        if (slash == ::cw::lib::String::npos) return child;
        if (const auto found = find_target_tag(child, tag.substr(slash + 1))) return found;
    }
    return nullptr;
}

std::shared_ptr<CWPyElement> parse_xml_source(
    const ::cw::lib::String& source,
    const ::cw::lib::String& tag,
    std::map<::cw::lib::String, ::cw::lib::String>* rootattrs, SimpleXmlParser* callbacks = nullptr) {
    ExpatXmlReader reader(source, callbacks);
    std::shared_ptr<CWPyElement> root;
    try { root = reader.parse(); }
    catch (const EndTargetTagException&) { if (!callbacks) throw; }
    s_last_rootattrs = callbacks ? reader.document_attributes() : root->m_attributes;
    merge_rootattrs(rootattrs, s_last_rootattrs);
    if (callbacks) return callbacks->m_root.value_or(nullptr);
    return tag.empty() ? root : find_target_tag(root, tag);
}

bool xml_known_encoding(const ::cw::lib::String& name);

std::optional<::cw::lib::String> xml_declared_encoding(const ::cw::lib::String& source)
{
    if (source.size() < 6 || source.compare(0, 5, "<?xml") != 0 ||
        source.find_first_of(" \t\r\n", 5) != 5) return std::nullopt;
    const auto end = source.find("?>", 6);
    if (end == ::cw::lib::String::npos) return std::nullopt;
    const auto declaration = source.substr(0, end);
    static const std::regex attribute(
        R"xml([ \t\r\n]encoding[ \t\r\n]*=[ \t\r\n]*(["'])([A-Za-z][A-Za-z0-9._-]*)\1)xml");
    std::smatch match;
    if (!std::regex_search(declaration, match, attribute)) return std::nullopt;
    auto name = ::cw::lib::encoding::lower(match[2].str());
    // Pythonは文字コードの別名を正規化するが、先にExpat自身のUTF表記
    // を認識する。 XMLでは utf-8 と utf_8 は交換可能ではない。
    if (name != "utf-8" && name != "utf-16" && name != "utf-16le" && name != "utf-16be") {
        ::cw::lib::String normalized;
        bool separator = false;
        for (const char ch : name) {
            if (ch == '-' || ch == '_') { separator = true; continue; }
            if (separator && !normalized.empty()) normalized.push_back('_');
            normalized.push_back(ch);
            separator = false;
        }
        name = std::move(normalized);
    }
    // Python側ではこれらの文字コードはExpatError
    // ではなくValueErrorになる。ネイティブ側でも復旧対
    // 象外の例外として拒否し、UTF-8の再試行には回さない。
    static constexpr const char* unsupported[] = {
        "big5", "big5-tw", "big5_tw", "csbig5", "cp932", "ms932",
        "csshiftjis", "ms-kanji", "ms_kanji", "mskanji", "s-jis", "s_jis",
        "shift-jis", "shift_jis", "shiftjis", "sjis", "euc-jp", "euc_jp", "eucjp",
        "u-jis", "u_jis", "ujis", "gb18030", "gb18030-2000", "gb18030_2000",
        "u16", "utf16", "utf_16", "utf-16-be", "utf-16-le", "utf_16_be", "utf_16_le",
        "utf_16be", "utf_16le", "unicodebigunmarked", "unicodelittleunmarked",
        "u32", "utf32", "utf-32", "utf_32", "utf-32-be", "utf-32-le",
        "utf_32_be", "utf_32_le", "utf-32be", "utf-32le", "utf_32be", "utf_32le",
        "x-mac-japanese", "x_mac_japanese", "x-mac-trad-chinese", "x_mac_trad_chinese"
    };
    for (const auto* codec : unsupported)
        if (name == codec) throw std::runtime_error("XML multibyte codec is not supported by Python Expat: " + name);
    if (!xml_known_encoding(name))
        throw std::runtime_error("XML codec is unsupported or unknown: " + name);
    return name;
}

bool xml_ascii_codec(const ::cw::lib::String& name)
{
    static constexpr const char* aliases[] = {"ansi-x3-4-1968", "ansi-x3.4-1968", "ansi-x3.4-1986", "ansi_x3.4_1968", "ansi_x3.4_1986", "ansi_x3_4_1968", "ascii", "cp367", "csascii", "ibm367", "iso-646.irv-1991", "iso-ir-6", "iso646-us", "iso646_us", "iso_646.irv_1991", "iso_ir_6", "us", "us-ascii", "us_ascii"};
    for (const auto* alias : aliases) if (name == alias) return true;
    return false;
}

enum class XmlExpatByteMap { None, Ascii, Latin1, Hz };
XmlExpatByteMap xml_expat_byte_map(const ::cw::lib::String& name)
{
    // PythonのUnknownEncoding接続と同様、バイトの対応表を作る。 こ
    // れらの状態を持つ・エスケープを使う文字コードを、XML文書全体へ直接適用しない。
    struct Alias { const char* name; XmlExpatByteMap mapping; };
    static constexpr Alias aliases[] = {
        {"charmap", XmlExpatByteMap::Latin1},
        {"csiso2022jp", XmlExpatByteMap::Ascii},
        {"hz", XmlExpatByteMap::Hz},
        {"hz_gb", XmlExpatByteMap::Hz},
        {"hz_gb_2312", XmlExpatByteMap::Hz},
        {"hz-gb", XmlExpatByteMap::Hz},
        {"hz-gb-2312", XmlExpatByteMap::Hz},
        {"hzgb", XmlExpatByteMap::Hz},
        {"iso_2022_jp", XmlExpatByteMap::Ascii},
        {"iso_2022_jp_1", XmlExpatByteMap::Ascii},
        {"iso_2022_jp_2", XmlExpatByteMap::Ascii},
        {"iso_2022_jp_2004", XmlExpatByteMap::Ascii},
        {"iso_2022_jp_3", XmlExpatByteMap::Ascii},
        {"iso_2022_jp_ext", XmlExpatByteMap::Ascii},
        {"iso-2022-jp", XmlExpatByteMap::Ascii},
        {"iso-2022-jp-1", XmlExpatByteMap::Ascii},
        {"iso-2022-jp-2", XmlExpatByteMap::Ascii},
        {"iso-2022-jp-2004", XmlExpatByteMap::Ascii},
        {"iso-2022-jp-3", XmlExpatByteMap::Ascii},
        {"iso-2022-jp-ext", XmlExpatByteMap::Ascii},
        {"iso2022_jp", XmlExpatByteMap::Ascii},
        {"iso2022_jp_1", XmlExpatByteMap::Ascii},
        {"iso2022_jp_2", XmlExpatByteMap::Ascii},
        {"iso2022_jp_2004", XmlExpatByteMap::Ascii},
        {"iso2022_jp_3", XmlExpatByteMap::Ascii},
        {"iso2022_jp_ext", XmlExpatByteMap::Ascii},
        {"iso2022-jp", XmlExpatByteMap::Ascii},
        {"iso2022-jp-1", XmlExpatByteMap::Ascii},
        {"iso2022-jp-2", XmlExpatByteMap::Ascii},
        {"iso2022-jp-2004", XmlExpatByteMap::Ascii},
        {"iso2022-jp-3", XmlExpatByteMap::Ascii},
        {"iso2022-jp-ext", XmlExpatByteMap::Ascii},
        {"iso2022jp", XmlExpatByteMap::Ascii},
        {"iso2022jp_1", XmlExpatByteMap::Ascii},
        {"iso2022jp_2", XmlExpatByteMap::Ascii},
        {"iso2022jp_2004", XmlExpatByteMap::Ascii},
        {"iso2022jp_3", XmlExpatByteMap::Ascii},
        {"iso2022jp_ext", XmlExpatByteMap::Ascii},
        {"iso2022jp-1", XmlExpatByteMap::Ascii},
        {"iso2022jp-2", XmlExpatByteMap::Ascii},
        {"iso2022jp-2004", XmlExpatByteMap::Ascii},
        {"iso2022jp-3", XmlExpatByteMap::Ascii},
        {"iso2022jp-ext", XmlExpatByteMap::Ascii},
        {"raw_unicode_escape", XmlExpatByteMap::Latin1},
        {"raw-unicode-escape", XmlExpatByteMap::Latin1},
        {"unicode_escape", XmlExpatByteMap::Latin1},
        {"unicode-escape", XmlExpatByteMap::Latin1},
    };
    for (const auto& alias : aliases) if (name == alias.name) return alias.mapping;
    return XmlExpatByteMap::None;
}

bool xml_utf8_alias(const ::cw::lib::String& name)
{
    return name == "utf8" || name == "utf_8" || name == "u8" || name == "utf" ||
        name == "cp65001" || name == "utf_8_sig" || name == "utf8_ucs2" || name == "utf8_ucs4";
}

bool xml_known_encoding(const ::cw::lib::String& name)
{
    return name == "utf-8" || name == "utf-16" || name == "utf-16le" || name == "utf-16be" ||
        ::cw::lib::xml_byte_codec_table(name) || xml_ascii_codec(name) || xml_utf8_alias(name) ||
        xml_expat_byte_map(name) != XmlExpatByteMap::None;
}

::cw::lib::String decode_xml_single_byte(const ::cw::lib::Bytes& bytes, std::size_t offset,
                                        const std::uint16_t* high)
{
    ::cw::lib::Bytes utf16;
    for (auto i = offset; i < bytes.size(); ++i) {
        const auto byte = bytes[i];
        const auto cp = byte >= 0x80 ? high[byte - 0x80] : byte;
        if (cp == 0xfffe) throw std::invalid_argument("XML single-byte codec contains an undefined byte");
        utf16.push_back(static_cast<unsigned char>(cp & 0xff));
        utf16.push_back(static_cast<unsigned char>(cp >> 8));
    }
    return ::cw::lib::encoding::utf16_to_utf8(utf16, ::cw::lib::encoding::ByteOrder::Little,
                                             ::cw::lib::encoding::ErrorPolicy::Strict, false);
}

::cw::lib::String decode_xml_bytes_strict(const ::cw::lib::Bytes& bytes)
{
    using namespace ::cw::lib::encoding;
    if (bytes.size() >= 4 &&
        ((bytes[0] == 0xff && bytes[1] == 0xfe && bytes[2] == 0 && bytes[3] == 0) ||
         (bytes[0] == 0 && bytes[1] == 0 && bytes[2] == 0xfe && bytes[3] == 0xff)))
        throw std::invalid_argument("XML UTF-32 byte input is not accepted by Python Expat");
    if (bytes.size() >= 4 && bytes[0] == '<' && bytes[1] == 0 && bytes[2] == 0 && bytes[3] == 0)
        throw std::invalid_argument("XML UTF-32 byte input is not accepted by Python Expat");
    if (bytes.size() >= 4 && bytes[0] == 0 && bytes[1] == 0 && bytes[2] == 0 && bytes[3] == '<')
        throw std::invalid_argument("XML UTF-32 byte input is not accepted by Python Expat");
    const auto decode_utf16_xml = [&](ByteOrder order) {
        auto source = utf16_to_utf8(bytes, order, ErrorPolicy::Strict, true);
        if (const auto name = xml_declared_encoding(source)) {
            const auto expected = order == ByteOrder::Little ? "utf-16le" : "utf-16be";
            if (*name != "utf-16" && *name != expected)
                throw std::invalid_argument("XML declaration contradicts UTF-16 byte encoding");
        }
        return source;
    };
    if (bytes.size() >= 2 &&
        ((bytes[0] == 0xff && bytes[1] == 0xfe) || (bytes[0] == 0xfe && bytes[1] == 0xff)))
        return decode_utf16_xml(bytes[0] == 0xff ? ByteOrder::Little : ByteOrder::Big);
    // Expatと同様、XML宣言だけでなくルートタグから始まるBOM
    // なしUTF-16も認識する。 UTF-32の識別は上で処理済み。
    if (bytes.size() >= 4 && bytes[0] == '<' && bytes[1] == 0 && bytes[2] != 0 && bytes[3] == 0)
        return decode_utf16_xml(ByteOrder::Little);
    if (bytes.size() >= 4 && bytes[0] == 0 && bytes[1] == '<' && bytes[2] == 0 && bytes[3] != 0)
        return decode_utf16_xml(ByteOrder::Big);
    // ASCII互換のXML宣言は本文の復号前に読める。 有効な
    // Latin-1をUTF-8失敗と扱い、Python形式の
    // 復旧でアクセント文字を黙って失わないようにする。 Expat
    // はUTF-8のBOMを消費しても後の単一バイト宣言を尊
    // 重するため、その文字コードの入力にはBOMを含めない。
    const std::size_t bom = bytes.size() >= 3 && bytes[0] == 0xef &&
        bytes[1] == 0xbb && bytes[2] == 0xbf ? 3 : 0;
    if (bytes.size() >= bom + 6 && bytes[bom] == '<' && bytes[bom + 1] == '?' &&
        bytes[bom + 2] == 'x' && bytes[bom + 3] == 'm' && bytes[bom + 4] == 'l' &&
        (bytes[bom + 5] == ' ' || bytes[bom + 5] == '\t' || bytes[bom + 5] == '\r' || bytes[bom + 5] == '\n')) {
        const ::cw::lib::String raw(bytes.begin() + bom, bytes.end());
        if (const auto declared = xml_declared_encoding(raw)) {
            const auto& name = *declared;
            if (const auto* table = ::cw::lib::xml_byte_codec_table(name))
                return decode_xml_single_byte(bytes, bom, table);
            const auto special = xml_expat_byte_map(name);
            if (special == XmlExpatByteMap::Latin1)
                return decode_xml_single_byte(bytes, bom, ::cw::lib::xml_byte_codec_table("latin1"));
            if (special == XmlExpatByteMap::Hz && std::find(bytes.begin() + bom, bytes.end(), '~') != bytes.end())
                throw std::invalid_argument("XML HZ byte map contains an undefined tilde");
            if (name == "utf-16" || name == "utf-16le" || name == "utf-16be")
                throw std::invalid_argument("XML UTF-16 declaration contradicts byte encoding");
            // 大小文字を除いて正確なUTF-8表記だけが、Expatの組み
            // 込みUTF-8復号を使う。 Pythonの文字コードの別名は
            // 、上位バイトが不正となる単一バイトの未知文字コード表を通す。
            const bool utf8_alias = xml_utf8_alias(name);
            if (xml_ascii_codec(name) || utf8_alias || special == XmlExpatByteMap::Ascii || special == XmlExpatByteMap::Hz) {
                if (std::any_of(bytes.begin() + bom, bytes.end(), [](auto value) { return value > 0x7f; }))
                    throw std::invalid_argument("XML single-byte encoding map contains an undefined byte");
            }
        }
    }
    return decode(bytes, TextEncoding::Utf8Bom);
}

bool contains_prohibited_xml_control(const ::cw::lib::String& text)
{
    for (const unsigned char value : text) {
        if (value <= 0x08U || value == 0x0bU || value == 0x0cU ||
            (value >= 0x0eU && value <= 0x1fU)) return true;
    }
    return false;
}

std::vector<std::size_t> xml_element_end_offsets(const ::cw::lib::Bytes& bytes)
{
    std::vector<std::size_t> offsets;
    const bool utf16le = bytes.size() >= 4 &&
        ((bytes[0] == 0xff && bytes[1] == 0xfe && !(bytes[2] == 0 && bytes[3] == 0)) ||
         (bytes[0] == '<' && bytes[1] == 0 && bytes[2] != 0 && bytes[3] == 0));
    const bool utf16be = bytes.size() >= 4 &&
        ((bytes[0] == 0xfe && bytes[1] == 0xff) ||
         (bytes[0] == 0 && bytes[1] == '<' && bytes[2] == 0 && bytes[3] != 0));
    if (utf16le || utf16be) {
        const std::size_t begin = bytes[0] == 0xff || bytes[0] == 0xfe ? 2 : 0;
        for (auto i = begin; i + 1 < bytes.size(); i += 2) {
            if ((utf16le && bytes[i] == '>' && bytes[i + 1] == 0) ||
                (utf16be && bytes[i] == 0 && bytes[i + 1] == '>')) offsets.push_back(i + 2);
        }
        return offsets;
    }
    // PythonのExpatはUTF-32を受け付けないため、 その文字
    // コードエラーより前に対象のコールバックが完了したことにはならない。
    if (bytes.size() >= 4 &&
        ((bytes[0] == 0xff && bytes[1] == 0xfe && bytes[2] == 0 && bytes[3] == 0) ||
         (bytes[0] == 0 && bytes[1] == 0 && bytes[2] == 0xfe && bytes[3] == 0xff) ||
         (bytes[0] == '<' && bytes[1] == 0 && bytes[2] == 0 && bytes[3] == 0) ||
         (bytes[0] == 0 && bytes[1] == 0 && bytes[2] == 0 && bytes[3] == '<'))) return offsets;
    for (std::size_t i = 0; i < bytes.size(); ++i)
        if (bytes[i] == '>') offsets.push_back(i + 1);
    return offsets;
}

std::optional<std::shared_ptr<CWPyElement>> parse_completed_target_prefix(
    const ::cw::lib::Bytes& bytes, const ::cw::lib::String& tag,
    std::map<::cw::lib::String, ::cw::lib::String>* rootattrs, SimpleXmlParser* callbacks)
{
    if (!callbacks || tag.empty()) return std::nullopt;
    const auto* targetonly = std::get_if<bool>(&callbacks->m_targetonly.storage());
    if (!targetonly || !*targetonly) return std::nullopt;

    // Expatは呼び出し元の元バイトを読みながらEndElementHandler
    // を呼ぶため、 同じバッファ内の後の不正バイトよりEndTargetTagException
    // が優先する。 通常のデコーダーは全入力を先に検証するの
    // で、その失敗後は、 次第に長くした完全なマークアップの
    // 先頭部分で再試行する。 エラー時だけの経路であり、選択
    // した部分ツリー内の厳密な復号・制御文字検査は維持する。
    for (const auto end : xml_element_end_offsets(bytes)) {
        try {
            // エラー時の先頭部分の試行は、毎回新しいコールバック状態から組
            // み立てる。 以前の失敗した試行のXMLの子を重複させない。
            callbacks->_clear_attrs();
            const ::cw::lib::Bytes prefix(bytes.begin(), bytes.begin() + end);
            const auto source = decode_xml_bytes_strict(prefix);
            if (contains_prohibited_xml_control(source)) continue;
            auto root = parse_xml_source(source, tag, rootattrs, callbacks);
            if (root && callbacks->m__persed) return root;
        } catch (const ::cw::lib::encoding::EncodingError&) {
        } catch (const std::invalid_argument&) {
        }
    }
    return std::nullopt;
}

::cw::lib::String recover_xml_bytes_like_python(const ::cw::lib::Bytes& bytes)
{
    auto text = ::cw::lib::encoding::decode_utf8(
        bytes, ::cw::lib::encoding::ErrorPolicy::Ignore, true);
    text.erase(std::remove_if(text.begin(), text.end(), [](unsigned char value) {
        return value <= 0x08U || value == 0x0bU || value == 0x0cU ||
               (value >= 0x0eU && value <= 0x1fU) || value == 0x7fU;
    }), text.end());
    return text;
}

std::shared_ptr<CWPyElement> parse_xml_bytes_like_python(
    const ::cw::lib::Bytes& bytes,
    const ::cw::lib::String& tag,
    std::map<::cw::lib::String, ::cw::lib::String>* rootattrs, SimpleXmlParser* callbacks = nullptr)
{
    try {
        const auto source = decode_xml_bytes_strict(bytes);
        if (contains_prohibited_xml_control(source))
            throw std::invalid_argument("XML contains a prohibited control character");
        return parse_xml_source(source, tag, rootattrs, callbacks);
    } catch (const ::cw::lib::encoding::EncodingError&) {
        // Python版と同様、Expatは最初に元のバイト列を受け
        // 取る。 文字コードが不正な場合だけ、XMLで禁止された制御
        // 文字を除いてから、UTF-8の不正バイト無視で再試行する。
        if (const auto partial = parse_completed_target_prefix(bytes, tag, rootattrs, callbacks))
            return *partial;
    } catch (const std::invalid_argument&) {
        // ExpatXmlReader は不正XMLを invalid_argument
        // として返す。 CardWirthPyのExpatError処
        // 理と同様に一度だけ再試行し、その試行自体のエラーは外へ伝える。
        if (const auto partial = parse_completed_target_prefix(bytes, tag, rootattrs, callbacks))
            return *partial;
    }
    // Python版と同様、整えたUTF-8バイトを新しいExpat
    // へ渡し、宣言を再適用する。 文字列を直接解析すると、制御バイ
    // ト除去後のLatin-1・CP437などでその段階が失われる
    // 。 成功済みの解析状態は保持し、ExpatErrorの再試行
    // 時だけ消す。 特に読み取りエラーで以前のルートを捨てない。
    if (callbacks) callbacks->_clear_attrs();
    const auto recovered = recover_xml_bytes_like_python(bytes);
    const ::cw::lib::Bytes recovered_bytes(recovered.begin(), recovered.end());
    const auto source = decode_xml_bytes_strict(recovered_bytes);
    if (contains_prohibited_xml_control(source))
        throw std::invalid_argument("XML contains a prohibited control character after recovery");
    return parse_xml_source(source, tag, rootattrs, callbacks);
}

void assign_fpath(const std::shared_ptr<CWPyElement>& element, const ::cw::lib::String& fpath)
{
    if (!element) return;
    element->m_fpath = fpath;
    for (const auto& child : element->m_children) assign_fpath(child, fpath);
}

template<class Map>
std::vector<::cw::lib::String> ordered_variable_names(const Map& values,
                                                    const std::vector<::cw::lib::String>& order)
{
    // XMLから作った変数は、Python辞書の挿入順を保持
    // する。 直接ネイティブマップへ追加した項目には記録され
    // た位置がないため、既存の末尾キー順の方針を維持する。
    std::vector<::cw::lib::String> names;
    for (const auto& name : order) if (values.contains(name)) names.push_back(name);
    for (const auto& [name, value] : values)
        if (std::find(names.begin(), names.end(), name) == names.end()) names.push_back(name);
    return names;
}

std::shared_ptr<CWPyElement> value_to_element(const ::cw::lib::Value& value) {
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
    if (!object || !*object) return nullptr;
    if (const auto element = std::dynamic_pointer_cast<CWPyElement>(*object)) return element;
    if (const auto tree = std::dynamic_pointer_cast<CWPyElementTree>(*object)) return tree->getroot();
    return nullptr;
}

::cw::lib::String value_to_string(const ::cw::lib::Value& value)
{
    if (const auto* text = std::get_if<::cw::lib::String>(&value.storage())) return *text;
    return {};
}

// expat の ParserCreate が返す動的オブジェクトに相当する薄いプロキシ。
// 実解析は SimpleXmlParser::parse() が行うが、Python 側と同じく生成直後に
// callback を差し替える呼出し元も扱えるよう、3 callback を所有者へ転送する。
class SimpleXmlParserProxy final : public ::cw::lib::Object {
public:
    explicit SimpleXmlParserProxy(SimpleXmlParser* owner) : m_owner(owner) {}
    void start_element(::cw::lib::String name, ::cw::lib::XmlAttributes attrs) {
        if (m_owner) m_owner->start_element(std::move(name), std::move(attrs));
    }
    void end_element(::cw::lib::String name) {
        if (m_owner) m_owner->end_element(std::move(name));
    }
    void char_data(::cw::lib::String text) {
        if (m_owner) m_owner->char_data(std::move(text));
    }
private:
    SimpleXmlParser* m_owner{};
};

::cw::lib::String join_path(const ::cw::lib::String& lhs, const ::cw::lib::String& rhs)
{
    if (lhs.empty()) return rhs;
    if (rhs.empty()) return lhs;
    return ::cw::util::join_paths({::cw::lib::Value(lhs), ::cw::lib::Value(rhs)});
}

// Flag/Step/Variant は Python 版では値の変更を宿データとメニューカードへ
// 即時通知する。C++ 側でも描画層を直接参照せず、既存の CWPy 通知窓口だけを
// 呼ぶことで同じ状態遷移を保つ。
void mark_variable_value_changing()
{
    if (!::cw::g_cwpy) return;
    if (::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata) (*::cw::g_cwpy->m_ydata)->changed();
}

void queue_debugger_refresh()
{
    if (!::cw::g_cwpy) return;
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_frame.storage());
    const auto frame = object && *object
        ? std::dynamic_pointer_cast<::cw::frame::Frame>(*object) : nullptr;
    if (!frame) throw std::logic_error("Debugger refresh requires the UI Frame");
    // Python版と同様、データ更新後にFrameへこのコールバックを予約し、CWPy
    // のゲームキューは使わない。 シナリオの更新・再読み込みも同じ境界を使う。
    frame->exec_func(::cw::lib::Callback([](const ::cw::lib::Arguments&, const ::cw::lib::KeywordArguments&) {
        if (!::cw::g_cwpy) return ::cw::lib::Value{};
        ::cw::g_cwpy->m_is_debuggerprocessing = false;
        if (::cw::g_cwpy->is_showingdebugger() && ::cw::g_cwpy->m_event)
            ::cw::g_cwpy->m_event->refresh_tools();
        return ::cw::lib::Value{};
    }));
}

void mark_variable_parent_edited(const ::cw::lib::Value& parent)
{
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&parent.storage())) {
        if (const auto tree = std::dynamic_pointer_cast<::cw::data::CWPyElementTree>(*object))
            tree->m_is_edited = true;
    }
}

bool variable_values_equal(const ::cw::lib::Value& lhs, const ::cw::lib::Value& rhs)
{
    // Pythonの有効なコモンの単一値は、一覧内でもboolとDecimal
    // を数値比較する。 追加のC++のInteger/Real
    // は元の表現ではないため、それらの既存動作は維持する。
    if (const auto* value = std::get_if<bool>(&lhs.storage()))
        if (const auto* number = std::get_if<::cw::lib::Decimal>(&rhs.storage()))
            return ::cw::lib::Decimal(*value ? 1 : 0) == *number;
    if (const auto* value = std::get_if<::cw::lib::Decimal>(&lhs.storage()))
        if (const auto* boolean = std::get_if<bool>(&rhs.storage()))
            return *value == ::cw::lib::Decimal(*boolean ? 1 : 0);
    if (lhs.storage().index() != rhs.storage().index()) return false;
    if (const auto* value = std::get_if<bool>(&lhs.storage())) return *value == std::get<bool>(rhs.storage());
    if (const auto* value = std::get_if<::cw::lib::Integer>(&lhs.storage())) return *value == std::get<::cw::lib::Integer>(rhs.storage());
    if (const auto* value = std::get_if<::cw::lib::Real>(&lhs.storage())) return *value == std::get<::cw::lib::Real>(rhs.storage());
    // Python版と同様、Decimalは数値比較する。 他の型の処理へ流すと全
    // Decimalの組が等しいことになり、デバッガーの編集が黙って消えてしまう。
    if (const auto* value = std::get_if<::cw::lib::Decimal>(&lhs.storage())) return *value == std::get<::cw::lib::Decimal>(rhs.storage());
    if (const auto* value = std::get_if<::cw::lib::String>(&lhs.storage())) return *value == std::get<::cw::lib::String>(rhs.storage());
    if (const auto* value = std::get_if<::cw::lib::Bytes>(&lhs.storage())) return *value == std::get<::cw::lib::Bytes>(rhs.storage());
    if (const auto* value = std::get_if<::cw::lib::ObjectPtr>(&lhs.storage())) {
        const auto& other = std::get<::cw::lib::ObjectPtr>(rhs.storage());
        if (*value == other) return true;
        const auto left = std::dynamic_pointer_cast<VariantSequence>(*value);
        const auto right = std::dynamic_pointer_cast<VariantSequence>(other);
        // Python版と同様、一覧は要素を比較するが、 元のStructVal
        // には __eq__ がないため、同一オブジェクトかで比較する。
        if (!left || !right || left->m_values.size() != right->m_values.size()) return false;
        for (std::size_t i = 0; i < left->m_values.size(); ++i)
            if (!variable_values_equal(left->m_values[i], right->m_values[i])) return false;
        return true;
    }
    return true;
}

::cw::lib::Integer value_integer_or(const ::cw::lib::Value& value, ::cw::lib::Integer fallback)
{
    if (const auto* integer = std::get_if<::cw::lib::Integer>(&value.storage())) return *integer;
    if (const auto* real = std::get_if<::cw::lib::Real>(&value.storage())) return static_cast<::cw::lib::Integer>(*real);
    return fallback;
}

void rewrite_temp_path(::cw::lib::String& path, const ::cw::lib::String& tempdir, const ::cw::lib::String& yadodir)
{
    if (const auto paths = ::cw::util::resolve_yado_paths(path, yadodir, tempdir))
        path = paths->first;
}

void rewrite_temp_value(::cw::lib::Value& path, const ::cw::lib::String& tempdir, const ::cw::lib::String& yadodir)
{
    if (const auto* text = std::get_if<::cw::lib::String>(&path.storage())) {
        auto rewritten = *text;
        rewrite_temp_path(rewritten, tempdir, yadodir);
        path = std::move(rewritten);
    }
}

::cw::lib::String save_rel_key(const ::cw::lib::String& fpath, const ::cw::lib::String& yadodir, const ::cw::lib::String& tempdir)
{
    return ::cw::util::get_yado_relpath(fpath, yadodir, tempdir);
}

ScenarioData* active_scenario_data()
{
    // ScenarioDataは宿からシナリオ状態へ変わる前
    // に公開され、その間に展開コールバックが動く。 Python
    // の cw.cwpy.sdata 参照と同様、古い画面
    // 状態ではなくオブジェクトの型を寿命確認の正とする。
    if (!::cw::g_cwpy) return nullptr;
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_sdata.storage());
    if (!object || !*object) return nullptr;
    return dynamic_cast<ScenarioData*>(object->get());
}

ScenarioData* playing_scenario_for_yado_history()
{
    // Python版と同様、ゴシップ・終了印の変更を記録するのは is_playingscenario()
    // の間だけ。 F9中はScenarioDataが残っていても is_playing
    // はfalseであり、 復旧操作を記録すると _f9impl
    // が走査中のマップ項目を消して解放済み参照になる。 読み込
    // み・進捗側はプレイ外でもオブジェクトを必要とするため、active_scenario_data()
    // 自体は変えない。
    return ::cw::g_cwpy && ::cw::g_cwpy->is_playingscenario()
        ? active_scenario_data() : nullptr;
}

std::shared_ptr<::cw::setting::ResourceTable> resource_table_from_value(const ::cw::lib::Value& value)
{
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
    if (!object || !*object) return nullptr;
    return std::dynamic_pointer_cast<::cw::setting::ResourceTable>(*object);
}

std::tuple<std::shared_ptr<::cw::lib::Surface>, bool> load_scenario_specialchar(
    const ::cw::lib::String& dpath, const ::cw::lib::String& fname, bool can_loaded_scaledimage)
{
    // Pythonのクロージャーと同様、先にシナリオ内の素材パスを解
    // 決し、 次に util.load_image でマスク・必要な
    // ら倍率付きの画像を読む。 ScenarioLog/TempFile
    // とシナリオディレクトリの優先順を、通常の素材検索と揃える。
    const auto path = ::cw::util::get_materialpath(fname, ::cw::g_M_IMG, dpath, false, false);
    const auto image = ::cw::util::load_image(path, true, {0, 0}, std::nullopt, true, false,
                                              can_loaded_scaledimage, false, std::nullopt, false);
    // シナリオの特殊文字の読み込みは、組の二番目を常にユーザー・シナリオフ
    // ォントとして示す。 Python版は倍率選択によらずtrueを返す。
    return {image, true};
}

}  // namespace
void CacheData::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data, ::cw::lib::Real arg_mtime)
{
    if (!arg_data) throw std::invalid_argument("CacheData requires XML data");
    m_data = std::static_pointer_cast<::cw::lib::Object>(std::move(arg_data));
    m_mtime = arg_mtime;
}

void Flag::initialize(std::optional<::cw::lib::Value> arg_parent, std::optional<std::shared_ptr<::cw::data::CWPyElement>> arg_data, bool arg_value, ::cw::lib::String arg_name, ::cw::lib::String arg_truename, ::cw::lib::String arg_falsename, bool arg_defaultvalue, bool arg_spchars)
{
    m__parent = arg_parent.value_or(::cw::lib::Value{});
    m__data = arg_data.value_or(nullptr);
    m_is_writable = arg_parent.has_value() && arg_data.has_value() && static_cast<bool>(*arg_data);
    m_value = arg_value;
    m_name = std::move(arg_name);
    m_truename = std::move(arg_truename);
    m_falsename = std::move(arg_falsename);
    m_defaultvalue = arg_defaultvalue;
    m_spchars = arg_spchars;
    m_initialization = parse_initialization(m__data ? std::optional<::cw::lib::Value>(m__data->getattr(".", "initialize", "Leave")) : std::optional<::cw::lib::Value>(::cw::lib::Value("Leave")));
}

bool Flag::to_bool()
{
    return m_value;
}

void Flag::redraw_cards(::cw::lib::Integer arg_cardspeed, bool arg_overridecardspeed)
{
    if (!::cw::g_cwpy) return;
    const auto runtime = ::cw::g_cwpy;
    struct RestoreSpeed {
        std::shared_ptr<::cw::thread::CWPy> runtime;
        ::cw::lib::Value override_speed;
        ::cw::lib::Integer force_speed;
        ~RestoreSpeed() {
            runtime->m_override_dealspeed = std::move(override_speed);
            runtime->m_force_dealspeed = force_speed;
        }
    } restore{runtime, runtime->m_override_dealspeed, runtime->m_force_dealspeed};
    // Pythonのfinallyと同様、カードの再描画が例外になっても両スロットを戻す。
    if (arg_cardspeed != -1) {
        if (arg_overridecardspeed) runtime->m_force_dealspeed = arg_cardspeed;
        else runtime->m_override_dealspeed = arg_cardspeed;
    }
    ::cw::data::redraw_cards(m_value, m_name, false);
}

void Flag::set(bool arg_value, bool arg_updatedebugger)
{
    if (Variant::values_equal(m_value, arg_value)) return;
    mark_variable_value_changing();
    m_value = arg_value;
    if (::cw::g_cwpy) ::cw::g_cwpy->update_mcardnames();
    if (arg_updatedebugger && ::cw::g_cwpy && ::cw::g_cwpy->m_event) {
        if (const auto self = weak_from_this().lock())
            ::cw::g_cwpy->m_event->refresh_variable(::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(self)));
    }
}

void Flag::reverse()
{
    set(!m_value);
}

::cw::lib::String Flag::get_valuename(std::optional<bool> arg_value)
{
    return arg_value.value_or(m_value) ? m_truename : m_falsename;
}

void Flag::write_value()
{
    if (!m_is_writable) return;
    if (std::find(m_initialization.begin(), m_initialization.end(), "EventExit") != m_initialization.end()) return;
    // Python版と同様、読み取り専用・EventExitの値は、元XMLの存在確認より先に飛ばす。
    const auto* parent_object = std::get_if<::cw::lib::ObjectPtr>(&m__parent.storage());
    if (m__parent.is_none() || (parent_object && !*parent_object))
        throw std::logic_error("Flag writable value requires a parent");
    if (!m__data) throw std::logic_error("Flag writable value requires XML data");
    m__data->set("value", m_value ? "True" : "False");
    mark_variable_parent_edited(m__parent);
}

void Party::initialize(std::shared_ptr<::cw::header::PartyHeader> arg_header, bool arg_partyinfoonly,
    ::cw::lib::String read_yadodir, ::cw::lib::String read_tempdir)
{
    m_read_yadodir=std::move(read_yadodir); m_read_tempdir=std::move(read_tempdir);
    // Pythonのコンストラクターの処理を、明示的なネイティブ初期化
    // として公開する。 ヘッダー・モードを _init に渡して読み込
    // み実装を一つにし、既定コンストラクターではパーティを読まない。
    _init(std::move(arg_header), arg_partyinfoonly);
}

void Party::_init(std::shared_ptr<::cw::header::PartyHeader> arg_header, bool arg_partyinfoonly)
{
    m__loading = true; m_partyinfoonly = arg_partyinfoonly;
    m_members.clear();
    if (!(arg_header && arg_header->m_data && *arg_header->m_data)) {
        m_backpack.clear(); m_backpack_moved.clear();
    }
    m_vanished_pcards.clear(); m_lastscenario.clear(); m_lastscenariopath = {};
    m_path = {};
    if (!arg_header) { m__loading = false; return; }
    if (const auto* path = std::get_if<::cw::lib::String>(&arg_header->m_fpath.storage())) {
        m_path = *path;
        const auto tree = ::cw::data::yadoxml2etree(*path, "", std::nullopt,m_read_yadodir,m_read_tempdir);
        // Party.xmlは必須だが、nullを返せる読み取り関数
        // は任意ファイルにも使う。 Party以外のルートも意図的に
        // 拒否し、別のXMLを空パーティとして公開しないようにする。
        if (!tree || !tree->getroot() || tree->getroot()->get_tag() != "Party")
            throw std::runtime_error("パーティの必須XMLを読み込めません: " + *path);
        m_data = std::static_pointer_cast<::cw::lib::Object>(tree);
    }
    m_name = arg_header->m_name; m_money = arg_header->m_money;
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&m_data.storage())) {
        if (const auto tree = std::dynamic_pointer_cast<CWPyElementTree>(*object)) {
            // Python版と同様、キャッシュしたヘッダーの名前・所持金が古くても、
            // XMLの既定値を読み直す。 Nameなしは空、Moneyなしはゼロ。
            m_name = tree->gettext("Property/Name", "");
            m_money = tree->getint("Property/Money", ::cw::lib::Value(::cw::lib::Integer{0}), std::nullopt);
            m_is_suspendlevelup = tree->getbool("Property/SuspendLevelUp", ::cw::lib::Value(false), std::nullopt);
            const auto last = tree->find("Property/LastScenario");
            if (last && *last) {
                m_lastscenariopath = (*last)->get("path", "");
                for (const auto& child : (*last)->m_children) if (child) m_lastscenario.emplace_back(child->m_text);
            }
            // Python版と同様、メンバーのツリーはパーティ情報が要求された場合だけ読む。
            // 通常の呼び出しはtrue、falseではPartyのプロパティだけを読む。
            if (arg_partyinfoonly) for (const auto& path : get_memberpaths()) {
                auto member = ::cw::data::yadoxml2etree(path, "", std::nullopt,m_read_yadodir,m_read_tempdir);
                // Pythonの yadoxml2etree と同様、メンバーが読めなければ例外
                // にする。 省略するとPCを黙って隠し、後のMembers再構築で失ってしまう。
                if (!member) throw std::runtime_error("party member XML cannot be read: " + path);
                m_members.emplace_back(std::static_pointer_cast<::cw::lib::Object>(member));
            }
        }
    }
    if (arg_partyinfoonly) {
        // 保存前のPartyHeader.dataを優先し、通常ロードではParty用DBから
        // 荷物袋を復元する。movedカードは別列に保持する。
        if (arg_header->m_data && *arg_header->m_data) {
            // Py との差（言語境界）：Py は header.data の list をそのまま共有する。
            // native の std::vector は値なのでここでコピーになる。通常の reload→_init は
            // data=self の一時 PartyHeader で呼ばれるため内容は同じだが、別の Party が
            // header.data の持ち主のとき、その後の編集はこのコピーに反映されない。
            m_backpack = arg_header->m_data.value()->m_backpack;
            m_backpack_moved = arg_header->m_data.value()->m_backpack_moved;
        } else if (const auto* path = std::get_if<::cw::lib::String>(&m_path.storage())) {
            const auto party_dir = std::filesystem::u8path(*path).parent_path();
            ::cw::yadodb::YadoDB carddb;
            carddb.initialize(::cw::lib::encoding::path_to_utf8(party_dir), ::cw::yadodb::g_PARTY);
            carddb.update(::cw::lib::Value(true), ::cw::lib::Value(false), ::cw::lib::Value(false),
                          std::nullopt, std::nullopt, std::nullopt, ::cw::lib::Value(false), ::cw::lib::Value(false));
            for (const auto& card : carddb.get_cards()) {
                if (card && card->m_moved == 0) m_backpack.push_back(card);
                else if (card) m_backpack_moved.push_back(card);
            }
            carddb.close();
        }
        sort_backpack(std::nullopt, std::nullopt);
    }
    m_sorted_backpack_by_order = false;
    // Python版と同様、CWPy::set_pcards() が古いPC
    // のspriteを置き換えるまで Party._loading を残す。
    // ここで消すと set_sprites() が前のパーティのカードを保
    // 持し、 別のパーティ選択がデータだけを変えて画面上は何も起きなくなる。
}

::cw::lib::String Party::get_showingname()
{
    return m_name;
}

// Python版は初期化済みの実行環境から設定を読むが、独立した
// ネイティブPartyは接続前に整列される場合がある。 その場合
// は元の順序を使い、星の優先を無効にする。 一時的な適性設定でも
// 不在の共有ヘッダーを飛ばし、未読み込みの所有者を参照しない。
void Party::sort_backpack(std::optional<::cw::lib::String> arg_sorttype, std::optional<std::shared_ptr<::cw::character::Character>> arg_test_aptitude)
{
    const auto condition = arg_sorttype.value_or(
        ::cw::g_cwpy && ::cw::g_cwpy->m_setting ? ::cw::g_cwpy->m_setting->m_sort_cards : "None");
    if (condition == "Aptitude" && arg_test_aptitude)
        for (const auto& card : m_backpack) if (card) card->set_testaptitude(arg_test_aptitude);
    const bool withstar = ::cw::g_cwpy && ::cw::g_cwpy->m_setting &&
        ::cw::g_cwpy->m_setting->m_sort_cardswithstar;
    sort_cards(m_backpack, condition, withstar);
    if (condition == "Aptitude" && arg_test_aptitude)
        for (const auto& card : m_backpack) if (card) card->set_testaptitude(std::nullopt);
    m_sorted_backpack_by_order = condition == "order";
}

std::optional<std::shared_ptr<::cw::header::CardHeader>> Party::find_keycode(::cw::lib::String arg_keycode, bool arg_skill, bool arg_item, bool arg_beast, bool arg_hand, ::cw::lib::String arg_condition)
{
    (void)arg_hand;
    if (!m_sorted_backpack_by_order) sort_backpack(std::optional<::cw::lib::String>("order"), std::nullopt);
    for (const auto& card : m_backpack) {
        if (!card) continue;
        const auto type = card->m_type;
        if ((type == "SkillCard" && !arg_skill) || (type == "ItemCard" && !arg_item) ||
            (type == "BeastCard" && !arg_beast)) continue;
        // Python版の引数省略時に合わせtrueを
        // 渡してカード名も含める。1.20以前の互換規
        // 則による除外はCardHeaderが扱う。
        const auto keys = card->get_keycodes(true);
        const bool contains = std::find(keys.begin(), keys.end(), arg_keycode) != keys.end();
        if ((arg_condition == "HasNot" && !contains) || (arg_condition != "HasNot" && contains)) return card;
    }
    return {};
}

::cw::lib::String Party::get_relpath()
{
    const auto* path = std::get_if<::cw::lib::String>(&m_path.storage());
    if (!path || path->empty()) return {};
    const auto parent = std::filesystem::u8path(*path).parent_path();
    const auto yado = ::cw::g_cwpy ? ::cw::g_cwpy->m_yadodir : ::cw::lib::String{};
    const auto temp = ::cw::g_cwpy ? ::cw::g_cwpy->m_tempdir : ::cw::lib::String{};
    // Python版の固定のYado接頭辞を越える絶対・マウントパス用に
    // 、C++の明示的な保存・一時ルートを維持する。 その内部は共通の字
    // 句的な相対パス規則を使う。 filesystem::relative
    // はリンクを解決し、論理的な荷物袋・保存キーを変えるため使わない。
    if (::cw::util::resolve_yado_paths(path_utf8(parent), yado, temp))
        return ::cw::util::get_yado_relpath(path_utf8(parent), yado, temp);
    // 明示的な保存・一時ルートの外では、Python版と同様、tempdirに対す
    // る字句的なパス全体を保つ。 ファイル名だけにすると親ディレクトリが失われる。
    return ::cw::util::join_paths({::cw::util::relpath(path_utf8(parent), temp)});
}

::cw::lib::String Party::get_yadodir()
{
    if (!::cw::g_cwpy) return {};
    return join_path(::cw::g_cwpy->m_yadodir, get_relpath());
}

::cw::lib::String Party::get_tempdir()
{
    if (!::cw::g_cwpy) return {};
    return join_path(::cw::g_cwpy->m_tempdir, get_relpath());
}

bool Party::is_loading()
{
    return m__loading;
}

void Party::reload()
{
    if (::cw::g_cwpy && ::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata) (*::cw::g_cwpy->m_ydata)->changed();
    auto header = std::make_shared<::cw::header::PartyHeader>();
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&m_data.storage())) {
        if (const auto tree = std::dynamic_pointer_cast<CWPyElementTree>(*object)) {
            const auto property = tree->find("Property");
            // ネイティブの再読み込みはPropertyの子がない場合、ルート
            // をヘッダー入力に使う。 Pythonの find_exists
            // は拒否するため、この代替は任意ヘッダーの境界だけに限定する。
            header->initialize(property ? *property : tree->getroot(), std::nullopt);
        }
    }
    header->m_fpath = m_path;
    if (const auto self = weak_from_this().lock()) header->m_data = self;
    _init(std::move(header), true);
}

bool Party::add(std::shared_ptr<::cw::header::AdventurerHeader> arg_header, std::optional<std::shared_ptr<::cw::data::CWPyElementTree>> arg_data)
{
    if (!arg_header || m_members.size() >= 6) return false;
    // Python版と同様、メンバーXMLの読み込み前に宿を変更済
    // みにする。 読み込み失敗でもその状態は残し、所属は変えない。
    if (::cw::g_cwpy && ::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata) (*::cw::g_cwpy->m_ydata)->changed();
    std::shared_ptr<CWPyElementTree> data;
    if (arg_data && *arg_data) data = *arg_data;
    else if (const auto* path = std::get_if<::cw::lib::String>(&arg_header->m_fpath.storage())) data = ::cw::data::yadoxml2etree(*path, "", std::nullopt);
    if (!data || !data->getroot()) return false;
    ::cw::lib::Integer index = 0;
    if (::cw::g_cwpy && !::cw::g_cwpy->m_pcards.empty()) {
        bool found_gap = false;
        for (std::size_t i = 0; i < ::cw::g_cwpy->m_pcards.size(); ++i) {
            const auto& pcard = ::cw::g_cwpy->m_pcards[i];
            if (!pcard || pcard->m_index_typed != static_cast<::cw::lib::Integer>(i)) {
                index = static_cast<::cw::lib::Integer>(i);
                found_gap = true;
                break;
            }
        }
        if (!found_gap) index = ::cw::g_cwpy->m_pcards.back() ? ::cw::g_cwpy->m_pcards.back()->m_index_typed + 1 : 0;
    }
    const auto insert_at = std::clamp<::cw::lib::Integer>(index, 0, static_cast<::cw::lib::Integer>(m_members.size()));
    m_members.insert(m_members.begin() + static_cast<std::ptrdiff_t>(insert_at),
                     ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(data)));
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&m_data.storage())) {
        if (const auto tree = std::dynamic_pointer_cast<CWPyElementTree>(*object)) {
            auto members = tree->find("Property/Members");
            if (!members || !*members) { tree->append("Property", make_element("Members")); members = tree->find("Property/Members"); }
            if (members && *members) {
                const auto fpath = std::get_if<::cw::lib::String>(&arg_header->m_fpath.storage());
                const auto name = fpath ? std::get<0>(::cw::util::splitext(path_utf8(std::filesystem::u8path(*fpath).filename()))) : arg_header->m_name;
                tree->insert("Property/Members", make_element("Member", name), insert_at);
            }
        }
    }
    if (!::cw::g_cwpy) return true;
    auto pcard = std::make_shared<::cw::sprite::card::PlayerCard>();
    pcard->initialize(data, {9 + 95 * index + 9 * index, 285}, "deal", index);
    // Pythonの Party.add と同様、PlayerCardの初期化が指
    // 定位置へ一度だけ登録する。 共通のネイティブ初期化にも、その登録を任せる。
    pcard->restore_personalpocket(true);
    ::cw::animation::animate_sprite(std::static_pointer_cast<::cw::sprite::base::CWPySprite>(pcard), "deal");
    return true;
}

void Party::remove(std::shared_ptr<::cw::sprite::card::PlayerCard> arg_pcard)
{
    if (!arg_pcard) throw std::invalid_argument("Party::remove requires PlayerCard");
    if (::cw::g_cwpy && ::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata) (*::cw::g_cwpy->m_ydata)->changed();
    arg_pcard->remove_numbercoupon();
    if (!arg_pcard->m_personal_pocket.empty()) arg_pcard->store_personalpocket();
    const auto target_tree = arg_pcard->m_data_typed;
    const auto member = std::find_if(m_members.begin(), m_members.end(), [&](const auto& value) {
        const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
        return object && *object && std::dynamic_pointer_cast<CWPyElementTree>(*object) == target_tree;
    });
    // Pythonの list.remove と同様、一致する文書を
    // 一つだけ除き、なければ例外を出す。 重複を黙って全削除し、壊
    // れたパーティを隠したり無関係な繰り返し項目を消したりしない。
    if (!target_tree || member == m_members.end())
        throw std::logic_error("Party::remove requires party member");
    m_members.erase(member);
    if (::cw::g_cwpy && ::cw::sprite::card::is_in_cardgrp(*arg_pcard)) {
        auto& pcards = ::cw::g_cwpy->m_pcards;
        // Python版と同様、三つの操作はすべて cardgrp.has() で制限する。
        // 消去されたカードは commit_vanish が Party::remove
        // を呼ぶ前に描画グループを離れているため、 m_pcardsへの所属で代用しない。
        arg_pcard->hide();
        ::cw::sprite::card::remove_from_cardgrp(*arg_pcard);
        const auto found = std::find(pcards.begin(), pcards.end(), arg_pcard);
        if (found == pcards.end())
            throw std::logic_error("Party::remove card group/member list mismatch");
        pcards.erase(found);
    }
    sync_member_references();
}

void Party::replace_order(::cw::lib::Integer arg_index1, ::cw::lib::Integer arg_index2)
{
    if (!::cw::g_cwpy) return;
    if (::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata) (*::cw::g_cwpy->m_ydata)->changed();
    // Python版と同様、長さの一致・位置の検査より前に宿を変更済み
    // にする。 元の反映時点まではローカルのカード一覧を操作し、 有効
    // な負の位置・同じ位置でもXML再構築とアニメーション要求を行う。
    auto pcards = ::cw::g_cwpy->m_pcards;
    if (pcards.size() != m_members.size())
        throw std::logic_error("Party::replace_order card/member count mismatch");
    const auto size = static_cast<::cw::lib::Integer>(pcards.size());
    const auto checked_index = [size](::cw::lib::Integer index) {
        if (index < -size || index >= size)
            throw std::out_of_range("Party::replace_order index");
        return static_cast<std::size_t>(index < 0 ? size + index : index);
    };
    const auto i1 = checked_index(arg_index1);
    const auto i2 = checked_index(arg_index2);
    std::swap(pcards[i1], pcards[i2]);
    if (pcards[i1] && pcards[i2]) std::swap(pcards[i1]->m_index_typed, pcards[i2]->m_index_typed);
    std::swap(m_members[i1], m_members[i2]);
    for (const auto& pcard : pcards) {
        if (!pcard) continue;
        pcard->m_tlayer = {std::get<0>(pcard->m_tlayer), std::get<1>(pcard->m_tlayer), pcard->m_index_typed, std::get<3>(pcard->m_tlayer)};
        ::cw::sprite::card::refresh_card_layer(*pcard);
        pcard->update_personalownerindex(-1);
    }
    ::cw::g_cwpy->m_pcards = pcards;
    sync_member_references();
    auto pcard1 = pcards[i1];
    auto pcard2 = pcards[i2];
    if (!pcard1 || !pcard2) return;
    ::cw::animation::animate_sprites({
        std::static_pointer_cast<::cw::sprite::base::CWPySprite>(pcard1),
        std::static_pointer_cast<::cw::sprite::base::CWPySprite>(pcard2)
    }, "hide");
    const auto pos = pcard1->get_pos_noscale();
    pcard1->set_pos_noscale(pcard2->get_pos_noscale(), std::nullopt);
    pcard2->set_pos_noscale(pos, std::nullopt);
    ::cw::animation::animate_sprites({
        std::static_pointer_cast<::cw::sprite::base::CWPySprite>(pcard1),
        std::static_pointer_cast<::cw::sprite::base::CWPySprite>(pcard2)
    }, "deal");
}

void Party::set_name(::cw::lib::String arg_name)
{
    if (m_name == arg_name) return;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata) (*::cw::g_cwpy->m_ydata)->changed();
    const auto oldname = m_name;
    m_name = std::move(arg_name);
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&m_data.storage()))
        if (const auto tree = std::dynamic_pointer_cast<CWPyElementTree>(*object)) {
            auto e = tree->find("Property/Name");
            // ネイティブ版ではNameの子がなければ、既存Propertyの
            // 下に追加して補う。 Pythonのeditは未存在で失敗する
            // が、Property自体がない場合まで修復するものではない。
            if (e && *e) (*e)->set_text(m_name); else tree->append("Property", make_element("Name", m_name));
            tree->m_is_edited = true;
        }
    if (::cw::g_cwpy && ::cw::g_cwpy->m_advlog) ::cw::g_cwpy->m_advlog->rename_party(m_name, oldname);
    if (::cw::g_cwpy && ::cw::g_cwpy->m_background)
        ::cw::g_cwpy->m_background->reload(false, {"Default", ::cw::lib::Value("Default")},
                                           true, {}, {}, {}, false, true);
}

void Party::set_money(::cw::lib::Integer arg_value, bool arg_fromevent, bool arg_blink)
{
    if (arg_value == 0) return;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata)
        (*::cw::g_cwpy->m_ydata)->changed();
    // Python版は任意精度整数を加算してから制限する。 ネイティブ
    // ・Wasmでも、制限に達する前の符号付きオーバーフローを避ける。
    m_money = add_money_clamped(m_money, arg_value);
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&m_data.storage()))
        if (const auto tree = std::dynamic_pointer_cast<CWPyElementTree>(*object)) {
            auto e = tree->find("Property/Money");
            // Nameと同様、ネイティブのMoney更新は既存Propertyの下
            // に不足する子を追加する。 PythonのeditはMoneyの存在を
            // 前提とする。 任意のツリーの検査は、ツリー自体の不在を修復しない。
            if (e && *e) (*e)->set_text(std::to_string(m_money)); else tree->append("Property", make_element("Money", std::to_string(m_money)));
            tree->m_is_edited = true;
        }
    // Python版と同様、ステータスバーの再構築・入力の反映より先に点滅を始める。
    if (arg_blink && ::cw::g_cwpy && ::cw::g_cwpy->m_statusbar &&
        ::cw::g_cwpy->m_statusbar->m_partymoney && *::cw::g_cwpy->m_statusbar->m_partymoney)
        ::cw::animation::start_animation(*::cw::g_cwpy->m_statusbar->m_partymoney, "blink");
    if (::cw::g_cwpy && !arg_fromevent) {
        const bool showbuttons = !::cw::g_cwpy->is_playingscenario() || !::cw::g_cwpy->is_runningevent();
        if (::cw::g_cwpy->m_statusbar) ::cw::g_cwpy->m_statusbar->change(showbuttons, false);
        ::cw::g_cwpy->m_has_inputevent = true;
    }
}

// Party値の更新時も、任意のネイティブ実行環境・ツリーへの参
// 照を保護する。 Python版は両方の存在を前提とする。Property
// がなければXMLを書かず、修復済みとは扱わない。
void Party::suspend_levelup(bool arg_suspend)
{
    if (m_is_suspendlevelup == arg_suspend) return;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata) (*::cw::g_cwpy->m_ydata)->changed();
    m_is_suspendlevelup = arg_suspend;
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&m_data.storage())) {
        if (const auto tree = std::dynamic_pointer_cast<CWPyElementTree>(*object)) {
            auto e = tree->find("Property/SuspendLevelUp");
            if (e && *e) (*e)->set_text(arg_suspend ? "True" : "False");
            else if (const auto prop = tree->find("Property"); prop && *prop) (*prop)->append(make_element("SuspendLevelUp", arg_suspend ? "True" : "False"));
            tree->m_is_edited = true;
        }
    }
}

void Party::set_numbercoupon()
{
    if (!::cw::g_cwpy) return;
    // 不正・不完全なパーティのspriteへの保護として、対応する六つ
    // の番号枠だけを使い、nullカードを飛ばす。 Python版は
    // 全カードで六要素の名前一覧を参照し、七枚目があれば例外になる。
    const std::vector<::cw::lib::String> fallback{"＿１", "＿２", "＿３", "＿４", "＿５", "＿６"};
    for (std::size_t i = 0; i < ::cw::g_cwpy->m_pcards.size() && i < fallback.size(); ++i) {
        const auto& pcard = ::cw::g_cwpy->m_pcards[i];
        if (!pcard) continue;
        pcard->remove_numbercoupon();
        auto name = fallback[i];
        if (i == 0 && ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_msgs)
            name = ::cw::g_cwpy->m_setting->m_msgs->get("number_1_coupon", name);
        pcard->set_coupon(name, 0);
        if (pcard->m_level < pcard->get_limitlevel()) pcard->set_coupon("：レベル補正中", 0);
    }
}

// 終了中のネイティブsprite一覧は不完全な場合がある。 全カードを直接
// 呼ぶPython版とは異なり、不在の実行環境・nullカードを飛ばす。
void Party::remove_numbercoupon()
{
    if (!::cw::g_cwpy) return;
    for (const auto& pcard : ::cw::g_cwpy->m_pcards) if (pcard) pcard->remove_numbercoupon();
}

// ネイティブのメンバーValueには、XMLツリーでない準備中のオブジェ
// クトもあり得るため、XMLツリーだけを書く。 Python版のパーティ
// メンバーは型付きの書き込み可能ツリーで、write_xmlを直接呼ぶ。
void Party::write()
{
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&m_data.storage()))
        if (const auto tree = std::dynamic_pointer_cast<CWPyElementTree>(*object)) tree->write_xml(false);
    for (const auto& member : m_members) if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&member.storage()))
        if (const auto tree = std::dynamic_pointer_cast<CWPyElementTree>(*object)) tree->write_xml(false);
}

void Party::lost()
{
    // Python版と同様、実行環境・必須の宿がなければ失敗させる。
    // 黙って戻って、ゲームオーバーの破棄処理が成功したように見せない。
    if (!::cw::g_cwpy) throw std::logic_error("Party.lost: CWPy is unavailable");
    if (!::cw::g_cwpy->m_ydata || !*::cw::g_cwpy->m_ydata)
        throw std::logic_error("Party.lost: YadoData is unavailable");
    // 有効なネイティブのパーティは共有所有される。Pythonのbound
    // selfと同様、 入れ子の破棄で宿の参照が外れても、呼び出し全
    // 体で自身を保持する。 未対応の非共有インスタンスは、荷物袋・XML
    // の破棄を黙って省略せず bad_weak_ptr を出す。
    const auto self = shared_from_this();
    (*::cw::g_cwpy->m_ydata)->changed();
    const auto pcards = ::cw::g_cwpy->get_pcards(::cw::lib::Value{});
    for (const auto& pcard : pcards) if (pcard) pcard->lost();
    m_members.clear();
    const auto cards = m_backpack;
    for (const auto& card : cards) if (card) {
        ::cw::g_cwpy->trade("TRASHBOX", std::nullopt, card, true, nullptr, -1, -1,
                            false, false, std::optional<std::shared_ptr<Party>>(self),
                            false, false, false, true, 0);
    }
    ::cw::g_cwpy->remove_xml(::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(self)));
    if ((*::cw::g_cwpy->m_ydata)->m_deletedpaths) {
        const auto party_path = std::get_if<::cw::lib::String>(&m_path.storage());
        if (party_path) (*::cw::g_cwpy->m_ydata)->m_deletedpaths->add(path_utf8(std::filesystem::u8path(*party_path).parent_path()));
    }
}

// ネイティブの Party::write と同様、XMLツリーを持つメンバーだけが
// 称号を提供する。 任意の称号グループがなければ何も加えず、Pythonのgetfind
// の必須条件とは異なる。 get_couponsも同じ方針で集合を求める。
std::map<::cw::lib::String, ::cw::lib::Integer> Party::get_coupontable()
{
    std::map<::cw::lib::String, ::cw::lib::Integer> result;
    for (const auto& member : m_members) if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&member.storage()))
        if (const auto tree = std::dynamic_pointer_cast<CWPyElementTree>(*object)) {
            const auto coupons = tree->find("Property/Coupons");
            if (coupons && *coupons) for (const auto& coupon : (*coupons)->m_children) if (coupon) ++result[coupon->m_text];
        }
    return result;
}

std::set<::cw::lib::String> Party::get_coupons()
{
    std::set<::cw::lib::String> result;
    for (const auto& [coupon, count] : get_coupontable()) { (void)count; result.insert(coupon); }
    return result;
}

// ネイティブの実行環境がなければ、保存された荷物袋のヘッダーだけを利用する
// 。 Python版は有効な環境・項目を前提とするが、 ここではnullの
// sprite・ポケットヘッダーを飛ばし、不完全な準備中の項目を出さない。
std::vector<std::shared_ptr<::cw::header::CardHeader>> Party::get_allcardheaders()
{
    auto result = m_backpack;
    if (::cw::g_cwpy) for (const auto& pcard : ::cw::g_cwpy->m_pcards) if (pcard) {
        for (const auto& card : std::get<0>(pcard->m_cardpocket_typed)) if (card) result.push_back(card);
        for (const auto& card : std::get<1>(pcard->m_cardpocket_typed)) if (card) result.push_back(card);
        for (const auto& card : std::get<2>(pcard->m_cardpocket_typed)) if (card) result.push_back(card);
    }
    return result;
}

bool Party::is_adventuring()
{
    // Python版と同様、物理的な存在ではなく宿の変更処理を反映した見え方で確認する
    // 。 帰還・F9後、保存までParty.wslがディスクに残っていても、 deletedpaths
    // によって所持金編集・スキル交換からは見えなくする必要がある。
    auto path = scenario_value_string(m_path);
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&m_data.storage()))
        if (const auto tree = std::dynamic_pointer_cast<CWPyElementTree>(*object);
            tree && !tree->m_fpath.empty()) path = tree->m_fpath;
    if (path.empty()) return false;
    auto wsl = std::filesystem::u8path(path);
    wsl.replace_extension(".wsl");
    return !::cw::util::get_yadofilepath(path_utf8(wsl)).empty();
}

std::optional<std::shared_ptr<::cw::header::ScenarioHeader>> Party::get_sceheader()
{
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&m_data.storage());
    const auto tree = object ? std::dynamic_pointer_cast<CWPyElementTree>(*object) : nullptr;
    // 既定構築のネイティブPartyはXML未読なので、その境界ではヘッダー
    // なしを返す。 読み込み済みはPython版と同様、現在の data.fpath
    // を使う。 m_path設定後に一時領域へ移る場合があるため。
    if (!tree) return std::nullopt;
    auto wsl = std::filesystem::u8path(tree->m_fpath); wsl.replace_extension(".wsl");
    const auto resolved = ::cw::util::get_yadofilepath(path_utf8(wsl));
    if (resolved.empty()) return std::nullopt;
    auto property = ::cw::util::get_elementfromzip(resolved, "ScenarioLog.xml", "Property");
    if (!property) return std::nullopt;
    const auto wsnp = property->gettext("WsnPath", "");
    if (wsnp.empty()) return std::nullopt;
    ::cw::scenariodb::Scenariodb db;
    auto result = db.search_path(wsnp, "");
    db.close();
    return result;
}

std::vector<::cw::lib::String> Party::get_memberpaths()
{
    std::vector<::cw::lib::String> result;
    if (::cw::g_fsync) ::cw::g_fsync->sync();
    const auto* path = std::get_if<::cw::lib::String>(&m_path.storage());
    if (!path) return result;
    const auto partyfile=std::filesystem::u8path(*path);
    auto partyroot=partyfile.parent_path().parent_path();
    // Party/<name>/Party.xml と旧Party/<name>.xml
    // は既知の構造を持つ。 一時保存したばかりのパーティには冒険者ディレクトリが
    // ない場合があるため、 Adventurerの存在から宿ルートを推測しない。
    if (partyroot.filename()=="Party") partyroot=partyroot.parent_path();
    const auto member_path=[&](const ::cw::lib::String& name) {
        const auto relative="Adventurer/"+name+".xml";
        if (!m_read_yadodir.empty())
            return std::filesystem::u8path(::cw::util::join_yadodir(relative,m_read_yadodir,m_read_tempdir));
        return ::cw::g_cwpy ? std::filesystem::u8path(::cw::util::join_yadodir(relative)) :
            partyroot/std::filesystem::u8path(relative);
    };
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&m_data.storage()))
        if (const auto tree = std::dynamic_pointer_cast<CWPyElementTree>(*object)) {
            const auto members = tree->find("Property/Members");
            if (members && *members) for (const auto& member : (*members)->m_children) if (member && !member->m_text.empty()) {
                auto candidate = member_path(member->m_text);
                // Pythonの os.path.isfile に相当し、path_is_file は暗号化保存領域も扱う。
                if (!::cw::util::path_is_file(::cw::lib::encoding::path_to_utf8(candidate))) {
                    // Python版と同様、元の表記のファイルが見つからない場合だけ str.strip を適用する。
                    const auto trimmed = ::cw::binary::util::strip_python_spaces(member->m_text);
                    candidate = member_path(trimmed);
                }
                result.push_back(::cw::lib::encoding::path_to_utf8(candidate));
            }
        }
    return result;
}

std::vector<::cw::lib::String> Party::get_loaded_memberpaths() const
{
    std::vector<::cw::lib::String> paths;
    paths.reserve(m_members.size());
    // Python版と同様、Party.membersはcharacter::PlayerではなくCWPyElementTree
    // を持つ。 Party.xmlから組み直すと一時保存先を失う場合があるため、実際のfpathを保持する。
    for (const auto& value : m_members) {
        const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
        const auto tree = object ? std::dynamic_pointer_cast<CWPyElementTree>(*object) : nullptr;
        if (tree && !tree->m_fpath.empty()) paths.push_back(tree->m_fpath);
    }
    return paths;
}

void Party::sync_member_references()
{
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&m_data.storage());
    const auto tree = object ? std::dynamic_pointer_cast<CWPyElementTree>(*object) : nullptr;
    if (!tree) return;
    auto members = tree->find("Property/Members");
    if (members && *members) (*members)->clear();
    // Python版と同様、remove/replace_orderは読み込み済みXML
    // 文書ではなく、現在の get_pcards() から参照を作り直す。 消去され
    // たカードにより両集合は異なり得る。Membersなしでの追加エラーも維持する。
    if (::cw::g_cwpy) for (const auto& card : ::cw::g_cwpy->get_pcards({})) {
        if (!card || !card->m_data_typed)
            throw std::logic_error("Party member reference requires live card data");
        const auto path = card->m_data_typed->m_fpath;
        tree->append("Property/Members", make_element("Member", std::get<0>(::cw::util::splitext(path_utf8(
            std::filesystem::u8path(path).filename())))));
    }
}

// 値で受けるネイティブのベクターは、Pythonの渡された一覧への別
// 参照ではなく、記録したパス一覧を所有する。 後の呼び出し元の編集は
// Partyへ反映せず、XMLには渡されたパスを元の順序で反映する。
void Party::set_lastscenario(std::vector<::cw::lib::String> arg_lastscenario, ::cw::lib::String arg_lastscenariopath)
{
    m_lastscenario.clear(); for (const auto& path : arg_lastscenario) m_lastscenario.emplace_back(path); m_lastscenariopath = arg_lastscenariopath;
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&m_data.storage()))
        if (const auto tree = std::dynamic_pointer_cast<CWPyElementTree>(*object)) {
            auto e = tree->find("Property/LastScenario");
            if (!e || !*e) { tree->append("Property", make_element("LastScenario")); e = tree->find("Property/LastScenario"); }
            if (e && *e) { (*e)->clear(); (*e)->set("path", arg_lastscenariopath); for (const auto& path : arg_lastscenario) (*e)->append(make_element("Path", path)); }
            tree->m_is_edited = true;
        }
}

void SimpleXmlParser::initialize(::cw::lib::String arg_fpath, ::cw::lib::String arg_targettag, std::optional<::cw::lib::Value> arg_stream, bool arg_targetonly, std::optional<std::map<::cw::lib::String, ::cw::lib::String>> arg_rootattrs)
{
    std::replace(arg_fpath.begin(), arg_fpath.end(), '\\', '/');
    m_fpath = std::move(arg_fpath);
    const auto first = arg_targettag.find_first_not_of('/');
    arg_targettag = first == ::cw::lib::String::npos ? "" :
        arg_targettag.substr(first, arg_targettag.find_last_not_of('/') - first + 1);
    m_targettag = std::move(arg_targettag);
    m_targetonly = arg_targetonly;
    m_rootattrs = {};
    m_rootattrs_typed = std::move(arg_rootattrs);
    m_root.reset();
    m__persed = false;
    m_currenttags.clear(); m_node_stack.clear(); m_parsetags.clear();
    m_file = arg_stream.value_or(::cw::lib::Value{});
    _clear_attrs();
}

void SimpleXmlParser::initialize(::cw::lib::String arg_fpath, ::cw::lib::String arg_targettag, std::optional<::cw::lib::Value> arg_stream, bool arg_targetonly, std::map<::cw::lib::String, ::cw::lib::String>& arg_rootattrs)
{
    initialize(std::move(arg_fpath), std::move(arg_targettag), std::move(arg_stream), arg_targetonly, std::nullopt);
    m_rootattrs = ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(std::make_shared<RootAttributesOutput>(arg_rootattrs)));
    arg_rootattrs.clear();
}

void SimpleXmlParser::_clear_attrs()
{
    m_root.reset();
    m_node_stack.clear();
    m_parsetags.clear();
    m_currenttags.clear();
    m__persed = false;
    if (m_rootattrs_typed) m_rootattrs_typed->clear();
    if (auto* output = borrowed_rootattrs(m_rootattrs)) output->clear();
}

void SimpleXmlParser::start_element(::cw::lib::String arg_name, ::cw::lib::XmlAttributes arg_attrs)
{
    if (m_currenttags.empty()) {
        merge_rootattrs(m_rootattrs_typed ? &*m_rootattrs_typed : nullptr, arg_attrs);
        merge_rootattrs(borrowed_rootattrs(m_rootattrs), arg_attrs);
    }
    m_currenttags.push_back(arg_name);
    if (!m__persed && get_currentpath() == value_to_string(m_targettag)) m_parsetags.push_back(arg_name);
    if (m_parsetags.empty()) return;
    auto element = std::make_shared<CWPyElement>(std::move(arg_name), "", std::move(arg_attrs));
    element->m_fpath = value_to_string(m_fpath);
    if (m_node_stack.empty()) m_root = element;
    else m_node_stack.back()->append(element);
    m_node_stack.push_back(element);
}

void SimpleXmlParser::end_element(::cw::lib::String arg_name)
{
    if (m_currenttags.empty() || m_currenttags.back() != arg_name)
        throw std::invalid_argument("SimpleXmlParser tag mismatch");
    bool completed_target = false;
    if (!m_parsetags.empty()) {
        if (m_node_stack.empty()) throw std::invalid_argument("SimpleXmlParser node stack is empty");
        m_node_stack.pop_back();
    }
    if (!m__persed && get_currentpath() == value_to_string(m_targettag)) {
        if (m_parsetags.empty()) throw std::invalid_argument("SimpleXmlParser target stack is empty");
        m_parsetags.pop_back();
        if (m_parsetags.empty()) {
            m__persed = true;
            completed_target = true;
        }
    }
    m_currenttags.pop_back();
    const auto* targetonly = std::get_if<bool>(&m_targetonly.storage());
    // 選択したルート相対の対象自体が完了したときだけ止
    // める。 閉じる末尾名だけの比較では、先にある同名
    // の子孫で止まり、複合パスでは止まらなくなる。
    // 空の対象は文書全体を選ぶため、後続も検証する。
    if (targetonly && *targetonly && !value_to_string(m_targettag).empty() && completed_target)
        throw EndTargetTagException{};
}

void SimpleXmlParser::char_data(::cw::lib::String arg_data)
{
    // Python版の部分解析と同様、最初の子より前の文字列だけを記録し、子の後のテキストは記録しない。
    if (!m_parsetags.empty() && !m_node_stack.empty() && m_node_stack.back()->m_children.empty())
        m_node_stack.back()->m_text += std::move(arg_data);
}

std::shared_ptr<::cw::data::CWPyElement> SimpleXmlParser::parse()
{
    // Python版と同様、解析のたびにread()を呼び、成功後のコールバック状態を保
    // 持する。 XMLエラーの復旧時だけ状態を消し、読み取りエラーはそのまま外へ伝える。
    ::cw::lib::Bytes bytes;
    if (auto input = ::cw::lib::read_binary_input(m_file)) bytes = std::move(*input);
    else if (const auto* path = std::get_if<::cw::lib::String>(&m_fpath.storage())) {
        const auto native = ::cw::lib::encoding::path_from_utf8(*path);
        bool handled = false;
        if (const auto mounted = read_mounted_xml(native, handled)) {
            bytes = *mounted;
        } else if (handled) {
            throw std::runtime_error("XML file cannot be opened from encrypted UserData: " + *path);
        } else {
            std::ifstream input(native, std::ios::binary);
            if (!input) throw std::runtime_error("XML file cannot be opened: " + *path);
            bytes = ::cw::lib::read_stream_bytes(input);
        }
    }
    const auto target = std::get_if<::cw::lib::String>(&m_targettag.storage())
        ? *std::get_if<::cw::lib::String>(&m_targettag.storage()) : "";
    try {
        m_root = parse_xml_bytes_like_python(
            bytes, target, borrowed_rootattrs(m_rootattrs) ? borrowed_rootattrs(m_rootattrs) :
                (m_rootattrs_typed ? &*m_rootattrs_typed : nullptr), this);
    } catch (const XmlSyntaxError& error) {
        // ネイティブのバイト位置・1始まりの列の診断を保ち、Python版の元ファイル情報
        // を加える。 文字コード・オープン・コールバックのエラーを別の種類に付け替えない。
        throw XmlSyntaxError(::cw::lib::String(error.what()) + ". file: " + value_to_string(m_fpath));
    }
    // Pythonのparse()と同様、選択されたルート
    // を必須とする。 対象なしを任意リソースの検索成功とは
    // 扱わず、空入力も上の通常のXMLエラー経路を通す。
    if (!m_root.value_or(nullptr))
        throw std::invalid_argument(value_to_string(m_fpath) + ": XML target not found: " + target);
    assign_fpath(m_root.value_or(nullptr), value_to_string(m_fpath));
    m__persed = true;
    return m_root.value_or(nullptr);
}

// ネイティブの共有XML呼び出しはBinaryInputのほか、
// パス・Bytesも渡せる。 明示入力を同じアダプターに結び付け
// 、以後のparse()で使う。 Python版はここでfname
// を読むが、構築時のファイル・パスは置き換えない。 この入力
// 接続の拡張と、成功後も保持するコールバック状態は別に扱う。
void SimpleXmlParser::parse_file(::cw::lib::Value arg_fname)
{
    // ネイティブの意図的な拡張として、ここでのStringはファイル名、Bytesは読み込み済み
    // データ。 BinaryInputはPythonの呼び出し元所有のread()を実装する。
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&arg_fname.storage());
    if (std::holds_alternative<::cw::lib::Bytes>(arg_fname.storage()) ||
        (object && std::dynamic_pointer_cast<::cw::lib::BinaryInput>(*object)))
        m_file = std::move(arg_fname);
    else if (std::holds_alternative<::cw::lib::String>(arg_fname.storage())) {
        m_fpath = std::move(arg_fname); m_file = {};
    } else {
        // Python版はread()のないfnameを拒否する。 Object
        // を文字列化してパスにしたり、黙って空XMLとして扱ったりしない。
        throw std::invalid_argument("XML input does not support read()");
    }
    parse();
}

::cw::lib::Value SimpleXmlParser::_create_parser()
{
    return ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(
        std::make_shared<SimpleXmlParserProxy>(this)));
}

void SimpleXmlParser::_parse_file(::cw::lib::Value arg_fname)
{
    parse_file(std::move(arg_fname));
}

::cw::lib::String SimpleXmlParser::get_currentpath()
{
    ::cw::lib::String path;
    for (std::size_t i = 1; i < m_currenttags.size(); ++i) {
        if (i > 1) path += '/';
        path += m_currenttags[i];
    }
    return path;
}

void Step::initialize(std::optional<::cw::lib::Value> arg_parent, std::optional<std::shared_ptr<::cw::data::CWPyElement>> arg_data, ::cw::lib::Integer arg_value, ::cw::lib::String arg_name, std::vector<::cw::lib::String> arg_valuenames, ::cw::lib::Integer arg_defaultvalue, bool arg_spchars)
{
    m__parent = arg_parent.value_or(::cw::lib::Value{});
    m__data = arg_data.value_or(nullptr);
    m_is_writable = arg_parent.has_value() && arg_data.has_value() && static_cast<bool>(*arg_data);
    m_value = arg_value;
    m_name = std::move(arg_name);
    // ネイティブのラベル一覧はStep自身が所有し、呼び出し元のコンテ
    // ナーを別参照しない。 Python版は指定一覧を保持するが、値で
    // 受けるC++ベクターでは、後の挿入・削除を意図的に反映しない。
    m_valuenames = std::move(arg_valuenames);
    m_defaultvalue = arg_defaultvalue;
    m_spchars = arg_spchars;
    m_initialization = parse_initialization(m__data ? std::optional<::cw::lib::Value>(m__data->getattr(".", "initialize", "Leave")) : std::optional<::cw::lib::Value>(::cw::lib::Value("Leave")));
}

void Step::set(::cw::lib::Integer arg_value, bool arg_updatedebugger)
{
    // Python版と同様、値名一覧が空でも上限-1でnumwrapを使
    // う。 共通処理で、上下限が逆でも下限を先に判定する動作を維持する。
    const auto value = ::cw::util::numwrap(arg_value, 0,
        static_cast<::cw::lib::Integer>(m_valuenames.size()) - 1);
    if (m_value == value) return;
    mark_variable_value_changing();
    m_value = value;
    if (::cw::g_cwpy) ::cw::g_cwpy->update_mcardnames();
    if (arg_updatedebugger && ::cw::g_cwpy && ::cw::g_cwpy->m_event) {
        if (const auto self = weak_from_this().lock())
            ::cw::g_cwpy->m_event->refresh_variable(::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(self)));
    }
}

void Step::up()
{
    if (m_value < static_cast<::cw::lib::Integer>(m_valuenames.size()) - 1) set(m_value + 1);
}

void Step::down()
{
    if (m_value > 0) set(m_value - 1);
}

::cw::lib::String Step::get_valuename(std::optional<::cw::lib::Integer> arg_value)
{
    const auto value = ::cw::util::numwrap(arg_value.value_or(m_value), ::cw::lib::Integer{0},
        static_cast<::cw::lib::Integer>(m_valuenames.size()) - 1);
    // Python版と同様、numwrap後に空一覧を参照するため、IndexErrorの境界を維持する。
    return m_valuenames.at(static_cast<std::size_t>(value));
}

void Step::write_value()
{
    if (!m_is_writable) return;
    if (std::find(m_initialization.begin(), m_initialization.end(), "EventExit") != m_initialization.end()) return;
    // Python版と同様、読み取り専用・EventExitの値は、元XMLの存在確認より先に飛ばす。
    const auto* parent_object = std::get_if<::cw::lib::ObjectPtr>(&m__parent.storage());
    if (m__parent.is_none() || (parent_object && !*parent_object))
        throw std::logic_error("Step writable value requires a parent");
    if (!m__data) throw std::logic_error("Step writable value requires XML data");
    m__data->set("value", std::to_string(m_value));
    mark_variable_parent_edited(m__parent);
}

void StructVal::initialize(::cw::lib::String arg_name, std::vector<::cw::lib::Value> arg_members)
{
    m_name = std::move(arg_name);
    // 値で持つネイティブのメンバーと公開Value列は、Python
    // の呼び出し元の列への別参照ではなく、構造の複製。 呼び出し
    // 元のコンテナーの寿命を保持しないが、ObjectPtrの値
    // はオブジェクトを共有するため、再帰的な深い複製ではない。
    m_member_values = std::move(arg_members);
    auto sequence = std::make_shared<VariantSequence>();
    sequence->m_values = m_member_values;
    m_members = ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(sequence));
}

namespace {
// 型付きの保存領域は両コンストラクターより前に存在するため、リセットだけを共有
// する。 ScenarioDataがカード専用の早期終了へ進む前に、 SystemData
// の構築経由でスキン変数を初期化・デバッガーへ通知してはならない。
void reset_system_data_storage(SystemData& state)
{
    state.m_wsn_version.clear();
    state.m_data.reset();
    state.m_name.clear();
    state.m_sdata.clear();
    state.m_author.clear();
    state.m_fpath.clear();
    state.m_mtime = 0.0;
    state.m_tempdir.clear();
    state.m_scedir.clear();
    state.m_summarypath.clear();
    state.m_summary.reset();
    state.m__filenum = 0;
    state.m__paused_time = 0.0;
    state.m__paused_time_integer_zero = false;
    state.m_startid = 0;
    state.m__areas.clear(); state.m__battles.clear(); state.m__packs.clear(); state.m__casts.clear();
    state.m__infos.clear(); state.m__items.clear(); state.m__skills.clear(); state.m__beasts.clear();
    state.m_sparea_mcards.clear();
    state.m_is_playing = true;
    state.m_events.reset(); state.m_playerevents.reset();
    state.m_lostadventurers.clear(); state.m_gossips.clear(); state.m_compstamps.clear();
    state.m_friendcards.clear(); state.m_infocards.clear(); state.m_infocard_maxindex = 0;
    state.m__infocard_cache.clear(); state.m_flags.clear(); state.m_steps.clear(); state.m_variants.clear();
    state.m_labels.clear(); state.m_ignorecase_table.clear();
    state.m_notice_infoview = false;
    state.m_infocards_beforeevent.reset();
    state.m_party_environment_backpack = true;
    state.m_party_environment_gameover = true;
    state.m_party_environment_runaway = true;
    state.m_pre_battleareadata.reset();
    state.m_data_cache.clear(); state.m_path_cache.clear(); state.m_resource_cache.clear(); state.m_resource_cache_size = 0;
    state.m_autostart_round = false; state.m_breakpoints.clear(); state.m_in_f9 = false; state.m_in_endprocess = false;
    state.m_background_image_mtime.clear(); state.m_moved_mcards.clear(); state.m_instructions.clear();
    state.m_debuglog.reset(); state.m_notice_debuglog = 0; state.m_can_loaded_scaledimage = true;
    state.m_backlog.clear(); state.m_uselimit_table.clear(); state.m_versionhint.assign(4, std::nullopt);
    state.m__fastLoadCache = false;
}
}

void SystemData::initialize(bool arg_init)
{
    // Python版は init=False の場合、空のオブジェクトを作って初期化を中断する。
    if (!arg_init) return;
    // SystemDataの直接構築はシステムのデバッグ状態をリセットする。 ScenarioData
    // は共通の保存領域のリセットと、独立したPython互換の構築処理だけを使う。
    const bool system_constructor = !dynamic_cast<ScenarioData*>(this);
    if (system_constructor && ::cw::g_cwpy && ::cw::g_cwpy->m_setting)
        ::cw::g_cwpy->m_debug = ::cw::g_cwpy->m_setting->m_debug;
    if (::cw::g_cwpy && !dynamic_cast<ScenarioData*>(this)) ::cw::g_cwpy->m_classicdata.reset();
    reset_system_data_storage(*this);
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting) {
        const auto* object = std::get_if<::cw::lib::ObjectPtr>(
            &::cw::g_cwpy->m_setting->m_skindata.storage());
        if (object && *object)
            m_summary = std::dynamic_pointer_cast<CWPyElementTree>(*object);
    }
    // ScenarioDataは展開先とclassicdataの準備後に独自のXML/WID走査を行う。
    // ここで仮想呼出しすると、Summary.wsmをCWScenario未初期化のまま
    // XMLとして解析してしまうため、基底のスキン資源だけを初期化する。
    SystemData::_init_xmlpaths(false);
    _init_sparea_mcards();
    if (system_constructor) {
        auto specialchars = std::make_shared<::cw::setting::ResourceTable>();
        specialchars->initialize("Resource/Image/Font");
        m_specialchars = ::cw::lib::Value(
            std::static_pointer_cast<::cw::lib::Object>(specialchars));
        m_ex_cache.clear();
        // Python版と同様、新しいシステムデータのスキン変数・デ
        // バッガーを初期化する前に、前のシナリオの停止点を保存する。
        if (::cw::g_cwpy) {
            const auto* old = std::get_if<::cw::lib::ObjectPtr>(
                &::cw::g_cwpy->m_sdata.storage());
            if (old && *old)
                if (const auto scenario = std::dynamic_pointer_cast<ScenarioData>(*old))
                    scenario->save_breakpoints();
        }
    }
    if (m_summary && *m_summary) {
        const auto key = (*m_summary)->gettext("Property/VariablesKey", "");
        if (!key.empty()) {
            _init_flags();
            _init_steps();
            _init_variants();
        }
    }
    _init_debugger();
}

// Python版のassertと同様、変数を変える前にSummary
// を必須とする。 ネイティブはlogic_errorでassert
// 無効の構成でも守り、 Summaryなしで有効な状態を消さない。
void SystemData::_init_flags()
{
    if (!m_summary || !*m_summary) throw std::logic_error("Variable initialization requires Summary");
    auto values = init_flags(::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(*m_summary)), false);
    std::vector<::cw::lib::String> order;
    if (const auto elements = (*m_summary)->find("Flags"); elements && *elements)
        for (const auto& element : (*elements)->m_children) {
            if (!element) continue;
            const auto name = element->gettext("Name", "");
            if (std::find(order.begin(), order.end(), name) == order.end()) order.push_back(name);
        }
    // Python版の代入と同様、解析成功後にだけ辞書を置き換える。
    m_flags = std::move(values);
    m_flags_order = std::move(order);
}

// Python版のassertと同様、変数を変える前にSummary
// を必須とする。 ネイティブはlogic_errorでassert
// 無効の構成でも守り、 Summaryなしで有効な状態を消さない。
void SystemData::_init_steps()
{
    if (!m_summary || !*m_summary) throw std::logic_error("Variable initialization requires Summary");
    auto values = init_steps(::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(*m_summary)), false);
    std::vector<::cw::lib::String> order;
    if (const auto elements = (*m_summary)->find("Steps"); elements && *elements)
        for (const auto& element : (*elements)->m_children) {
            if (!element) continue;
            const auto name = element->gettext("Name", "");
            if (std::find(order.begin(), order.end(), name) == order.end()) order.push_back(name);
        }
    // Python版の代入と同様、解析成功後にだけ辞書を置き換える。
    m_steps = std::move(values);
    m_steps_order = std::move(order);
}

// Python版のassertと同様、変数を変える前にSummary
// を必須とする。 ネイティブはlogic_errorでassert
// 無効の構成でも守り、Summaryなしで有効な状態を消さない。
void SystemData::_init_variants()
{
    if (!m_summary || !*m_summary) throw std::logic_error("Variable initialization requires Summary");
    auto values = init_variants(::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(*m_summary)), false);
    std::vector<::cw::lib::String> order;
    if (const auto elements = (*m_summary)->find("Variants"); elements && *elements)
        for (const auto& element : (*elements)->m_children) {
            if (!element) continue;
            const auto name = element->gettext("Name", "");
            if (std::find(order.begin(), order.end(), name) == order.end()) order.push_back(name);
        }
    // Python版の代入と同様、解析成功後にだけ辞書を置き換える。
    m_variants = std::move(values);
    m_variants_order = std::move(order);
}

void SystemData::save_variables()
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_ydata || !*::cw::g_cwpy->m_ydata) return;
    const bool has_variables = !m_flags.empty() || !m_steps.empty() || !m_variants.empty();
    const auto path = ::cw::util::join_yadodir("SkinVariables.xml");
    const bool exists = ::cw::util::path_is_file(path);
    if (!exists && !has_variables) return;
    if (!m_summary || !*m_summary) throw std::logic_error("Skin variables require Summary");
    auto root = exists ? xml2element(path, "", std::nullopt, std::nullopt, true, std::nullopt)
                       : std::make_shared<CWPyElement>("SkinVariables");
    if (!root) throw std::runtime_error("SkinVariables XML has no root");
    const auto key = (*m_summary)->gettext("Property/VariablesKey", "");
    const auto skins = std::get<0>(get_skinkeys());
    std::shared_ptr<CWPyElement> vars;
    const auto children = root->m_children;
    for (const auto& child : children) {
        if (!child || child->m_tag != "Variables") continue;
        const auto child_key = child->get("key", "");
        if (child_key == key) vars = child;
        if (!skins.contains(child_key)) {
            root->remove(child);
            if (vars == child) vars.reset();
        }
    }
    if (!vars) {
        vars = std::make_shared<CWPyElement>("Variables");
        vars->set("key", key);
        root->append(vars);
    }
    if (has_variables) {
        const auto attributes = vars->m_attributes;
        vars->clear();
        vars->m_attributes = attributes;
        if (!m_flags.empty()) {
            auto group = std::make_shared<CWPyElement>("Flags");
            for (const auto& name : ordered_variable_names(m_flags, m_flags_order)) {
                const auto& flag = m_flags.at(name);
                if (!flag) throw std::logic_error("Null skin flag");
                auto e = std::make_shared<CWPyElement>("Flag", name);
                e->set("value", flag->m_value ? "True" : "False"); group->append(e);
            }
            vars->append(group);
        }
        if (!m_steps.empty()) {
            auto group = std::make_shared<CWPyElement>("Steps");
            for (const auto& name : ordered_variable_names(m_steps, m_steps_order)) {
                const auto& step = m_steps.at(name);
                if (!step) throw std::logic_error("Null skin step");
                auto e = std::make_shared<CWPyElement>("Step", name);
                e->set("value", std::to_string(step->m_value)); group->append(e);
            }
            vars->append(group);
        }
        if (!m_variants.empty()) {
            auto group = std::make_shared<CWPyElement>("Variants");
            for (const auto& name : ordered_variable_names(m_variants, m_variants_order)) {
                const auto& variant = m_variants.at(name);
                if (!variant) throw std::logic_error("Null skin variant");
                auto e = std::make_shared<CWPyElement>("Variant");
                const auto* type = std::get_if<::cw::lib::String>(&variant->m_type.storage());
                if (type && (*type == "List" || *type == "Structure")) {
                    e->append(std::make_shared<CWPyElement>("Name", name));
                    Variant::value_to_element(variant->m_value, e, "type");
                } else {
                    e->m_text = name; Variant::value_to_element(variant->m_value, e, "type");
                }
                group->append(e);
            }
            vars->append(group);
        }
    } else {
        root->remove(vars);
        if (root->m_children.empty()) {
            if ((*::cw::g_cwpy->m_ydata)->m_deletedpaths)
                (*::cw::g_cwpy->m_ydata)->m_deletedpaths->add(path);
            return;
        }
    }
    CWPyElementTree tree(root, path);
    tree.m_is_edited = true;
    tree.write_xml(false);
    if ((*::cw::g_cwpy->m_ydata)->m_deletedpaths)
        (*::cw::g_cwpy->m_ydata)->m_deletedpaths->discard(tree.m_fpath);
}

void SystemData::load_variables()
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_ydata || !*::cw::g_cwpy->m_ydata) return;
    if (m_flags.empty() && m_steps.empty() && m_variants.empty()) return;
    const auto path = ::cw::util::join_yadodir("SkinVariables.xml");
    if (!::cw::util::path_is_file(path)) return;
    {
        const auto root = xml2element(path, "", std::nullopt, std::nullopt, true, std::nullopt);
        if (!root) return;
        const auto key = m_summary && *m_summary
            ? (*m_summary)->gettext("Property/VariablesKey", "") : "";
        for (const auto& vars : root->m_children) {
            if (!vars) continue;
            if (vars->m_tag != "Variables") return;
            if (vars->get("key", "") == key) {
                _load_variables(::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(vars)));
                break;
            }
        }
    }}

void SystemData::_load_variables(::cw::lib::Value arg_data)
{
    std::shared_ptr<CWPyElement> data;
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&arg_data.storage()); object && *object) {
        data = std::dynamic_pointer_cast<CWPyElement>(*object);
        if (!data) if (const auto tree = std::dynamic_pointer_cast<CWPyElementTree>(*object)) data = tree->m_root_node;
    }
    if (!data) return;
    auto load_group = [&](const ::cw::lib::String& tag) -> std::shared_ptr<CWPyElement> {
        auto found = data->find(tag);
        // 移植版の旧式の入れ子のVariablesコンテナーを、入力互換の拡張と
        // して残す。 通常の書き込みはPython版と同じ直接のグループを使う。
        if (!found) found = data->find("Variables/" + tag);
        return found ? *found : nullptr;
    };
    // Python版と同様、スナップショットを直接復元する。 変更用の設定関数はStep
    // の制限、宿の変更済み設定、カード名更新を行うため、読み込みには使わない。
    if (const auto group = load_group("Flags"); group) {
        for (const auto& e : group->m_children) if (e) {
            const auto it = m_flags.find(e->m_text);
            if (it == m_flags.end() || !it->second) continue;
            it->second->m_value = e->getbool(".", ::cw::lib::Value("value"), std::nullopt);
        }
    }
    if (const auto group = load_group("Steps"); group) {
        for (const auto& e : group->m_children) if (e) {
            const auto it = m_steps.find(e->m_text);
            if (it == m_steps.end() || !it->second) continue;
            it->second->m_value = e->getint(".", ::cw::lib::Value("value"), std::nullopt);
        }
    }
    if (const auto group = load_group("Variants"); group) {
        for (const auto& e : group->m_children) if (e) {
            auto name = e->m_text;
            if (auto name_e = e->find("Name"); name_e && *name_e) name = (*name_e)->m_text;
            const auto it = m_variants.find(name);
            if (it == m_variants.end() || !it->second) continue;
            try {
                const auto value = Variant::value_from_element(
                    e, "type", std::optional<::cw::lib::String>("value"));
                it->second->m_type = Variant::value_to_type(value);
                it->second->m_value = value;
            } catch (const std::exception&) {
                ::cw::util::print_ex(std::nullopt);
                const auto text = "コモン「" + name + "」の値が正しくありません。\n"
                    "このエラーが発生した場合、誤って使用中のCardWirthPyのバージョンに対応していない"
                    "シナリオやスキンを使用したか、シナリオやスキンに手動で編集したことによる不具合が発生している可能性があります。\n"
                    "いずれにも該当していない場合はCardWirthPyの開発者へお知らせください。";
                std::cerr << text << "\n\n";
                if (::cw::g_cwpy) ::cw::g_cwpy->call_modaldlg("ERROR", {{"text", ::cw::lib::Value(text)}});
            }
        }
    }
}

void SystemData::reset_variables()
{
    if (!m_summary || !*m_summary) throw std::logic_error("Variable reset requires Summary");
    for (const auto& e : (*m_summary)->find_exists("Steps")->m_children) {
        const auto value = e->getint(".", std::optional<::cw::lib::String>("default"), std::nullopt);
        m_steps.at(e->gettext("Name", ""))->set(value, true);
    }
    for (const auto& e : (*m_summary)->find_exists("Flags")->m_children) {
        const auto value = e->getbool(".", ::cw::lib::Value("default"), std::nullopt);
        const auto flag = m_flags.at(e->gettext("Name", ""));
        flag->set(value, true);
        flag->redraw_cards(-1, false);
    }
    if (const auto variants = (*m_summary)->find("Variants"); variants && *variants)
        for (const auto& e : (*variants)->m_children) {
            const auto value = Variant::value_from_element(e, "defaulttype", "defaultvalue");
            m_variants.at(e->gettext("Name", ""))->set(value, true);
        }
}

void SystemData::_init_debugger()
{
    if (::cw::g_cwpy && ::cw::g_cwpy->m_event)
        ::cw::g_cwpy->m_event->refresh_variablelist();
}

void SystemData::update_skin()
{
    _init_xmlpaths(false);
    _init_sparea_mcards();
    save_variables();
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting) {
        const auto* object = std::get_if<::cw::lib::ObjectPtr>(
            &::cw::g_cwpy->m_setting->m_skindata.storage());
        m_summary = object && *object
            ? std::dynamic_pointer_cast<CWPyElementTree>(*object)
            : nullptr;
    }
    if (m_summary && *m_summary && (*m_summary)->gettext("Property/VariablesKey", "") != "") {
        _init_flags(); _init_steps(); _init_variants();
        load_variables();
    } else {
        m_flags.clear(); m_steps.clear(); m_variants.clear();
    }
    _init_debugger();
    queue_debugger_refresh();
}

void SystemData::_init_xmlpaths(bool arg_xmlonly)
{
    (void)arg_xmlonly;
    if (::cw::g_fsync) ::cw::g_fsync->sync();
    m__areas.clear();
    m__battles.clear();
    m__packs.clear();
    m__casts.clear();
    m__infos.clear();
    m__items.clear();
    m__skills.clear();
    m__beasts.clear();
    if (!::cw::g_cwpy) return;

    const auto skin = value_to_string(::cw::g_cwpy->m_skindir);
    const auto status = ::cw::g_cwpy->m_status;
    const std::vector<::cw::lib::String> dpaths{
        join_path(join_path(skin, "Resource/Xml"), status),
        join_path(join_path("Data/SkinBase", "Resource/Xml"), status),
        join_path(skin, "Resource/Xml/Package"),
        "Data/SkinBase/Resource/Xml/Package",
        join_path(skin, "Resource/Xml/Area"),
        "Data/SkinBase/Resource/Xml/Area"};

    const auto in_title_areas = [](const ::cw::lib::Integer id) {
        return std::get<0>(::cw::g_AREAS_TITLE) == id;
    };
    const auto area_value = [](const ::cw::lib::Value& value) -> ::cw::lib::Integer {
        if (const auto* integer = std::get_if<::cw::lib::Integer>(&value.storage()))
            return *integer;
        if (const auto* real = std::get_if<::cw::lib::Real>(&value.storage()))
            return static_cast<::cw::lib::Integer>(*real);
        if (const auto* boolean = std::get_if<bool>(&value.storage()))
            return *boolean ? 1 : 0;
        return 0;
    };
    const auto in_yado_areas = [&area_value](const ::cw::lib::Integer id) {
        return area_value(std::get<0>(::cw::g_AREAS_YADO)) == id ||
               area_value(std::get<1>(::cw::g_AREAS_YADO)) == id ||
               area_value(std::get<2>(::cw::g_AREAS_YADO)) == id ||
               std::get<3>(::cw::g_AREAS_YADO) == id ||
               std::get<4>(::cw::g_AREAS_YADO) == id ||
               std::get<5>(::cw::g_AREAS_YADO) == id ||
               std::get<6>(::cw::g_AREAS_YADO) == id;
    };
    for (const auto& dpath : dpaths) {
        const auto dir = std::filesystem::u8path(dpath);
        const auto directory_status = mounted_or_native_stat(dir);
        if (!directory_status || !directory_status->is_directory()) continue;
        const auto dirname = path_utf8(dir.filename());
        for (const auto& entry : mounted_or_native_list(dir)) {
            const auto entry_status = mounted_or_native_stat(entry);
            if (!entry_status || !entry_status->is_file()) continue;
            const auto path = path_utf8(entry);
            auto ext = entry.extension().generic_string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (ext != ".xml") continue;
            auto property = xml2element(path, "Property", std::nullopt, std::nullopt, false, std::nullopt);
            // Python版と同様、必須のPropertyを直ちに読む。 読み取り
            // 側が選択要素なしを返しても、不正なスキンXMLを黙って省略しない。
            if (!property) throw std::invalid_argument("skin XML has no Property: " + path);
            const auto resid = property->getint("Id", std::nullopt, std::nullopt);
            const auto name = property->gettext("Name", std::nullopt);
            if (dirname == "Package") {
                m__packs.emplace(resid, std::make_tuple(name, path));
            } else if (dirname == "Title" && !in_title_areas(resid)) {
                continue;
            } else if (dirname == "Yado" && !in_yado_areas(resid)) {
                continue;
            } else {
                m__areas.emplace(resid, std::make_tuple(name, path));
            }
        }
    }
}

void SystemData::update_scenariopath(::cw::lib::String arg_normpath, ::cw::lib::String arg_dst, bool arg_dstisfile)
{
    if (m_fpath.empty() || ::cw::util::get_keypath(m_fpath) != arg_normpath) return;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_ydata.has_value() &&
        ::cw::g_cwpy->m_ydata.value())
        ::cw::g_cwpy->m_ydata.value()->changed();
    m_fpath = arg_dst;
    if (!arg_dstisfile) {
        m_tempdir = arg_dst;
        m_scedir = arg_dst;
    }
    const auto update_table = [&](auto& table) {
        const auto old = table;
        table.clear();
        for (const auto& [resid, item] : old) {
            auto path = std::get<1>(item);
            const auto relative = ::cw::util::is_descendant(path, arg_normpath);
            if (!relative.empty()) path = join_path(arg_dst, relative);
            table.emplace(resid, std::make_tuple(std::get<0>(item), path));
        }
    };
    update_table(m__areas); update_table(m__battles); update_table(m__packs);
    update_table(m__casts); update_table(m__infos); update_table(m__items);
    update_table(m__skills); update_table(m__beasts);
    m_data_cache.clear();
    m_path_cache.clear();
    m_resource_cache.clear();
    m_resource_cache_size = 0;
}

void SystemData::update_scenariopath2(::cw::lib::String arg_normpath, ::cw::lib::String arg_dst, bool arg_dstisfile)
{
    if (m_fpath.empty() || arg_dstisfile) return;
    if (::cw::util::get_keypath(arg_dst) != ::cw::util::get_keypath(m_fpath)) return;
    (void)arg_normpath;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_rsrc) {
        if (const auto table = resource_table_from_value(::cw::g_cwpy->m_rsrc->m_specialchars)) table->reset();
    }
    update_scale();
}

void SystemData::_init_sparea_mcards()
{
    if (!::cw::g_cwpy) return;
    decltype(m_sparea_mcards) prepared;
    const auto is_trade = [](const ::cw::lib::Integer id) {
        return id == std::get<0>(::cw::g_AREAS_TRADE) ||
               id == std::get<1>(::cw::g_AREAS_TRADE) ||
               id == std::get<2>(::cw::g_AREAS_TRADE);
    };
    for (const auto& [areaid, unused] : m__areas) {
        (void)unused;
        if (!is_trade(areaid)) continue;
        const auto data = get_mcarddata(areaid, false, std::nullopt);
        const auto saved_area = ::cw::g_cwpy->m_areaid;
        ::cw::g_cwpy->m_areaid = areaid;
        const auto cards = ::cw::g_cwpy->set_mcards(
            data, false, false, false, std::nullopt);
        // Python版と同様、現在エリアに依存し得る次の get_mcarddata
        // より前に状態を復元する。 取得後に毎回状態を保存する。
        ::cw::g_cwpy->m_areaid = saved_area;
        prepared[areaid] = cards;
    }
    // Python版と同様、全エリアが成功してからローカル辞書を公開する。 検
    // 索・set_mcardsが例外になっても、公開済みのカードは維持する。
    m_sparea_mcards = std::move(prepared);
}

bool SystemData::is_wsnversion(::cw::lib::String arg_wsn_version, std::optional<::cw::lib::String> arg_cardversion)
{
    const auto version = arg_cardversion.value_or(m_wsn_version);
    if (version.empty()) return arg_wsn_version.empty();
    const auto available = ::cw::lib::normalize_python_integer(version);
    const auto requested = ::cw::lib::normalize_python_integer(arg_wsn_version);
    if (!available || !requested) return false;
    // Python版の int(str) 比較に合わせ、数値の先頭部分だけは受け付けな
    // い。 大きい有効なバージョン番号も、ネイティブIntegerへ狭めず保持する。
    const auto canonical = [](const ::cw::lib::String& number) {
        const bool negative = number.front() == '-';
        const auto start = number.front() == '-' || number.front() == '+' ? 1U : 0U;
        const auto first = number.find_first_not_of('0', start);
        const auto magnitude = first == ::cw::lib::String::npos ? ::cw::lib::String("0") : number.substr(first);
        return std::make_pair(negative && magnitude != "0", magnitude);
    };
    const auto [available_negative, available_digits] = canonical(*available);
    const auto [requested_negative, requested_digits] = canonical(*requested);
    if (available_negative != requested_negative) return !available_negative;
    const auto magnitude_order = available_digits.size() != requested_digits.size()
        ? (available_digits.size() < requested_digits.size() ? -1 : 1)
        : available_digits.compare(requested_digits);
    return available_negative ? magnitude_order <= 0 : magnitude_order >= 0;
}

std::optional<std::tuple<::cw::lib::String, ::cw::lib::String, bool, bool, bool>> SystemData::get_versionhint(::cw::lib::Integer arg_frompos)
{
    (void)arg_frompos;
    return std::nullopt;
}

void SystemData::set_versionhint(::cw::lib::Integer arg_pos, std::optional<std::tuple<::cw::lib::String, ::cw::lib::String, bool, bool, bool>> arg_hint)
{
    (void)arg_pos;
    (void)arg_hint;
}

void SystemData::update_scale()
{
    for (auto& [unused, cards] : m_sparea_mcards) {
        (void)unused;
        for (const auto& value : cards) {
            const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
            if (!object || !*object) continue;
            if (const auto card = std::dynamic_pointer_cast<::cw::sprite::card::CWPyCard>(*object))
                card->update_scale();
        }
    }
    for (const auto& value : m_backlog) {
        const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
        if (!object || !*object) continue;
        const auto backlog = std::dynamic_pointer_cast<::cw::sprite::message::BacklogData>(*object);
        if (!backlog) continue;
        if (const auto table = resource_table_from_value(backlog->m_specialchars)) table->reset();
    }
}

void SystemData::sweep_resourcecache(::cw::lib::Integer arg_size)
{
    constexpr std::uint64_t hard_limit = 2ULL * 1024ULL * 1024ULL * 1024ULL;
    // デスクトップPOSIXでは、取得できた有限のRLIMIT_DATA
    // の半分に基づいて上限を調整する。
#if defined(__EMSCRIPTEN__)
    std::uint64_t limit = 128ULL * 1024ULL * 1024ULL;
#elif defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
    std::uint64_t limit = 256ULL * 1024ULL * 1024ULL;
#else
    std::uint64_t limit = 1ULL * 1024ULL * 1024ULL * 1024ULL;
#endif
#if defined(_WIN32)
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (::GlobalMemoryStatusEx(&status) != 0) limit = status.ullTotalVirtual / 2ULL;
#elif !defined(__EMSCRIPTEN__)
    struct rlimit status{};
    if (::getrlimit(RLIMIT_DATA, &status) == 0 && status.rlim_cur != RLIM_INFINITY)
        limit = static_cast<std::uint64_t>(status.rlim_cur) / 2ULL;
#endif
    limit = (std::min)(limit, hard_limit);
    // ネイティブの使用量管理は、負の確保量をゼロに制限し、Python
    // 版のようにキャッシュ使用量を減らさない。 RLIMIT_DATA
    // が無制限ならRLIM_INFINITYを実際の容量
    // として割らず、上の保守的なホスト上限を維持する。
    if (arg_size < 0) arg_size = 0;
    const auto size = static_cast<std::uint64_t>(arg_size);
    const auto current = static_cast<std::uint64_t>((std::max)(m_resource_cache_size, ::cw::lib::Integer{0}));
    if (current > limit || size > limit - current) {
        m_resource_cache.clear();
        m_resource_cache_size = 0;
    }
    m_resource_cache_size += static_cast<::cw::lib::Integer>(size);
}

std::optional<std::shared_ptr<::cw::data::Flag>> SystemData::find_flag(::cw::lib::String arg_path, bool arg_is_differentscenario, std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    if (arg_event && *arg_event) {
        const auto& local = (*arg_event)->m_flags;
        if (const auto it = local.find(arg_path); it != local.end()) return it->second;
    }
    if (arg_is_differentscenario) return std::nullopt;
    if (const auto it = m_flags.find(arg_path); it != m_flags.end()) return it->second;
    return std::nullopt;
}

std::optional<std::shared_ptr<::cw::data::Step>> SystemData::find_step(::cw::lib::String arg_path, bool arg_is_differentscenario, std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    if (arg_event && *arg_event) {
        const auto& local = (*arg_event)->m_steps;
        if (const auto it = local.find(arg_path); it != local.end()) return it->second;
    }
    if (arg_is_differentscenario) return std::nullopt;
    if (const auto it = m_steps.find(arg_path); it != m_steps.end()) return it->second;
    return std::nullopt;
}

std::optional<std::shared_ptr<::cw::data::Variant>> SystemData::find_variant(::cw::lib::String arg_path, bool arg_is_differentscenario, std::optional<std::shared_ptr<::cw::event::Event>> arg_event)
{
    if (arg_event && *arg_event) {
        const auto& local = (*arg_event)->m_variants;
        if (const auto it = local.find(arg_path); it != local.end()) return it->second;
    }
    if (arg_is_differentscenario) return std::nullopt;
    if (const auto it = m_variants.find(arg_path); it != m_variants.end()) return it->second;
    return std::nullopt;
}

bool SystemData::get_flagvalue(::cw::lib::String arg_path)
{
    if (arg_path.empty()) return true;
    const auto it = m_flags.find(arg_path);
    // Python版は未定義フラグをTrueとして扱う。
    return it == m_flags.end() || (it->second && it->second->m_value);
}

void SystemData::start()
{
    // Python版の基底SystemDataは開始時処理を持たない。
}

void SystemData::end(bool arg_showdebuglog, bool arg_complete, bool arg_failure)
{
    (void)arg_showdebuglog;
    (void)arg_complete;
    (void)arg_failure;
    // Python版の基底SystemDataは終了処理を持たない。
}

void SystemData::save_breakpoints()
{
    // Python版の基底SystemDataはシナリオ別保存を行わない。
}

::cw::lib::Real SystemData::get_totalpyaingtime()
{
    return {};
}

::cw::lib::Real SystemData::get_pausedtime()
{
    return {};
}

void SystemData::start_timekeeper()
{
    // Python版の基底SystemDataは時間計測を行わない。
}

void SystemData::resume_timekeeper()
{
    // Python版の基底SystemDataは時間計測を行わない。
}

void SystemData::sleep_timekeeper()
{
    // Python版の基底SystemDataは時間計測を行わない。
}

// Python版はset_log内でPCardsを準備し、宿・Partyの
// 存在をassertで要求する。 ネイティブは任意の所有者・Partyパス
// がなければ (false, None) を返す。 CWPy::set_scenario
// では呼び出し元が復旧・開始ログの取得前にPCardsを準備す
// るが、 このメソッドは他の呼び出し元も準備するとは保証しない。
std::tuple<bool, std::optional<std::vector<std::tuple<::cw::lib::String, ::cw::lib::Integer, ::cw::lib::Integer, bool, ::cw::lib::String>>>> SystemData::set_log(bool arg_force_create)
{
    log_scenario_data(::cw::lib::String("set_log begin: force=") + (arg_force_create ? "1" : "0") +
                      " tempdir=" + scenario_value_string(::cw::g_tempdir));
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_ydata || !*::cw::g_cwpy->m_ydata ||
        !(*::cw::g_cwpy->m_ydata)->m_party) return {false, std::nullopt};
    const auto party = (*::cw::g_cwpy->m_ydata)->m_party.value();
    const auto* party_path = party ? std::get_if<::cw::lib::String>(&party->m_path.storage()) : nullptr;
    if (!party_path || party_path->empty()) return {false, std::nullopt};
    const auto tempdir = scenario_value_string(::cw::g_tempdir);
    const auto scenario_log_path = join_path(tempdir, "ScenarioLog");
    log_scenario_data("set_log cleanup: path=" + scenario_log_path +
                      " party=" + *party_path);
    ::cw::util::remove(scenario_log_path, false);
    const auto [stem, unused_ext] = ::cw::util::splitext(*party_path);
    (void)unused_ext;
    const auto path = ::cw::util::get_yadofilepath(stem + ".wsl");
    log_scenario_data("set_log archive: path=" + path +
                      " exists=" + (!path.empty() && mounted_or_native_is_file(std::filesystem::u8path(path)) ? "1" : "0"));
    if (!arg_force_create && !path.empty() && mounted_or_native_is_file(std::filesystem::u8path(path))) {
        const auto extracted = ::cw::util::decompress_zip(
            path, tempdir, "ScenarioLog", std::nullopt, std::nullopt, false, std::nullopt);
        const auto extracted_log = join_path(extracted, "ScenarioLog.xml");
        log_scenario_data("set_log extracted: path=" + extracted +
                          " log_exists=" +
                           (!extracted.empty() && mounted_or_native_is_file(
                               std::filesystem::u8path(extracted_log)) ? "1" : "0"));
        const auto music = load_log(extracted_log, false);
        log_scenario_data("set_log loaded existing ScenarioLog");
        return {true, music};
    }
    create_log();
    log_scenario_data("set_log created new ScenarioLog");
    return {false, std::nullopt};
}

void SystemData::create_log()
{
    // Python版も現状はpass。ログディレクトリはset_log()の展開側が準備する。
}

void SystemData::validate_recovery_log(const ::cw::lib::String& directory,
    const ::cw::lib::String& party_filename)
{
    const auto root = std::filesystem::u8path(directory);
    const auto filename = std::filesystem::u8path(party_filename);
    if (directory.empty() || filename.empty() || filename != filename.filename() ||
        filename == "." || filename == "..")
        throw std::runtime_error("復旧用ログのパスが不正です。");
    const auto read = [](const std::filesystem::path& path, const char* tag) {
        const auto utf8 = ::cw::lib::encoding::path_to_utf8(path);
        if (!mounted_or_native_is_file(path))
            throw std::runtime_error("復旧用ログがありません: " + utf8);
        const auto tree = xml2etree(utf8, "", std::nullopt, std::nullopt, true);
        if (!tree || !tree->getroot() || tree->getroot()->get_tag() != tag)
            throw std::runtime_error("復旧用ログの形式が不正です: " + utf8);
        return tree;
    };
    (void)read(root / "ScenarioLog.xml", "ScenarioLog");
    const auto party = read(root / "Party" / filename, "Party");
    (void)read(root / "Backpack.xml", "BackpackFiles");
    const auto members = root / "Members";
    if (!mounted_or_native_is_directory(members)) throw std::runtime_error("復旧用メンバーのログがありません。");
    for (const auto& entry : mounted_or_native_list(members))
        if (mounted_or_native_is_file(entry) && entry.extension() == ".xml") (void)read(entry, "Adventurer");
    if (const auto list = party->find("Property/Members"); list && *list) {
        for (const auto& member : (*list)->m_children) if (member) {
            auto name = std::filesystem::u8path(member->m_text + ".xml");
            if (member->m_text.empty() || name != name.filename())
                throw std::runtime_error("復旧用メンバーの名前が不正です。");
            if (!mounted_or_native_is_file(members / name)) {
                // Python・PartyHeader::get_memberpaths と同様に扱
                // う。 Windowsでは、ファイルの前後の空白がすでに取り除かれている場合がある。
                const auto first = member->m_text.find_first_not_of(" \t\r\n");
                const auto last = member->m_text.find_last_not_of(" \t\r\n");
                if (first == ::cw::lib::String::npos) throw std::runtime_error("復旧用メンバーの名前が空です。");
                name = std::filesystem::u8path(member->m_text.substr(first, last - first + 1) + ".xml");
            }
            (void)read(members / name, "Adventurer");
        }
    }
}

void SystemData::remove_log(std::optional<std::shared_ptr<::cw::debug::logging::DebugLog>> arg_debuglog)
{
    log_scenario_data(::cw::lib::String("remove_log begin: debuglog=") + (arg_debuglog && *arg_debuglog ? "1" : "0") +
                      " tempdir=" + scenario_value_string(::cw::g_tempdir));
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_ydata || !*::cw::g_cwpy->m_ydata ||
        !(*::cw::g_cwpy->m_ydata)->m_party) return;
    const auto party = (*::cw::g_cwpy->m_ydata)->m_party.value();
    const auto* party_path = party ? std::get_if<::cw::lib::String>(&party->m_path.storage()) : nullptr;
    if (!party_path || party_path->empty()) return;
    if (::cw::g_fsync) ::cw::g_fsync->sync();
    if (arg_debuglog && *arg_debuglog) {
        const auto debuglog = *arg_debuglog;
        const auto logbase = std::filesystem::u8path(scenario_value_string(::cw::g_tempdir)) / "ScenarioLog";
        const auto members_dir = logbase / "Members";
        for (const auto& pcard : ::cw::g_cwpy->m_pcards) if (pcard && pcard->m_data_typed) {
            const auto member_path = members_dir / std::filesystem::u8path(pcard->m_data_typed->m_fpath).filename();
            std::set<::cw::lib::String> old_coupons;
            std::vector<std::tuple<::cw::lib::String, ::cw::lib::Integer>> lost;
            if (const auto old_tree = xml2etree(path_utf8(member_path), "", std::nullopt, std::nullopt, true)) {
                if (const auto coupons = old_tree->find("Property/Coupons"); coupons && *coupons) {
                    // Python版と同様、失った称号は重複名も含めXML順に記
                    // 録する。 集合を使うのは、新たに得た称号の所属判定だけ。
                    for (const auto& coupon : (*coupons)->m_children) if (coupon) {
                        old_coupons.insert(coupon->m_text);
                        const auto value = coupon->getint(".", ::cw::lib::Value("value"), 0);
                        if (!pcard->has_coupon(coupon->m_text)) lost.emplace_back(coupon->m_text, value);
                    }
                }
            }
            std::vector<std::tuple<::cw::lib::String, ::cw::lib::Integer>> got;
            for (const auto& name : pcard->get_coupons()) if (!old_coupons.contains(name))
                got.emplace_back(name, pcard->get_couponvalue(name, false).value_or(0));
            debuglog->add_player(pcard, std::move(got), std::move(lost));
        }

        const auto party_dir = logbase / "Party";
        if (mounted_or_native_is_directory(party_dir)) {
            for (const auto& entry : mounted_or_native_list(party_dir)) {
                if (!mounted_or_native_is_file(entry) || scenario_lower(entry.extension().generic_string()) != ".xml") continue;
                if (const auto old_party = xml2etree(path_utf8(entry), "", std::nullopt, std::nullopt, true))
                    debuglog->set_money(old_party->getint("Property/Money", std::nullopt, party->m_money), party->m_money);
                break;
            }
        }
        const auto append_sorted = [&](const auto& entries, const auto& append) {
            std::vector<::cw::lib::String> names;
            for (const auto& [name, unused] : entries) names.push_back(name);
            std::sort(names.begin(), names.end(), [](const auto& lhs, const auto& rhs) {
                return ::cw::util::logical_compare(lhs, rhs) < 0;
            });
            for (const auto& name : names) append(name, entries.at(name));
        };
        append_sorted(m_gossips, [&](const auto& name, bool acquired) { debuglog->add_gossip(name, acquired); });
        append_sorted(m_compstamps, [&](const auto& name, bool acquired) { debuglog->add_compstamp(name, acquired); });
        for (const auto& type : {::cw::lib::String("SkillCard"), ::cw::lib::String("ItemCard"), ::cw::lib::String("BeastCard")}) {
            const auto deleted_dir = party_dir / ("Deleted" + type);
            if (!mounted_or_native_is_directory(deleted_dir)) continue;
            for (const auto& entry : mounted_or_native_list(deleted_dir)) {
                if (!mounted_or_native_is_file(entry) || scenario_lower(entry.extension().generic_string()) != ".xml") continue;
                const auto card = xml2etree(path_utf8(entry), "", std::nullopt, std::nullopt, true);
                if (!card) continue;
                const bool attachment = card->getbool("Property/Attachment", std::nullopt, false);
                if (type == "BeastCard" && !attachment) continue;
                debuglog->add_lostcard(type, card->gettext("Property/Name", ""),
                    ::cw::util::decodewrap(card->gettext("Property/Description", "")), card->gettext("Property/Scenario", ""),
                    card->gettext("Property/Author", ""), card->gettext("Property/Premium", "Normal"));
            }
        }
    }
    const auto scenario_log_path = join_path(scenario_value_string(::cw::g_tempdir), "ScenarioLog");
    log_scenario_data("remove_log cleanup: path=" + scenario_log_path);
    ::cw::util::remove(scenario_log_path, false);
    const auto [stem, unused_ext] = ::cw::util::splitext(*party_path);
    (void)unused_ext;
    if ((*::cw::g_cwpy->m_ydata)->m_deletedpaths)
        (*::cw::g_cwpy->m_ydata)->m_deletedpaths->add(stem + ".wsl");
    log_scenario_data("remove_log completed: archive=" + stem + ".wsl");
}

std::vector<std::tuple<::cw::lib::String, ::cw::lib::Integer, ::cw::lib::Integer, bool, ::cw::lib::String>> SystemData::load_log(::cw::lib::String arg_path, bool arg_recording)
{
    (void)arg_recording;
    std::vector<std::tuple<::cw::lib::String, ::cw::lib::Integer, ::cw::lib::Integer, bool, ::cw::lib::String>> result;
    {
        const auto tree = xml2etree(arg_path, "", std::nullopt, std::nullopt, true);
        if (!tree) return result;
        // ログ形式への意図的な許容として、Python版の正確なTrue/False
        // に加え、小文字true/falseと数値1/0を受け付ける。
        // 一般的な大小文字無視ではなく、他の表記は無視する。 グループな
        // しも、Pythonのgetfindのように必須とせず飛ばす。
        const auto load_bool_group = [&](const ::cw::lib::String& tag, auto& destination) {
            const auto group = tree->find(tag);
            if (!group || !*group) return;
            for (const auto& element : (*group)->m_children) {
                if (!element) continue;
                const auto value = element->get("value", "");
                if (value == "True" || value == "true" || value == "1") destination[element->m_text] = true;
                else if (value == "False" || value == "false" || value == "0") destination[element->m_text] = false;
            }
        };
        load_bool_group("Gossips", m_gossips);
        load_bool_group("CompleteStamps", m_compstamps);
    }return result;
}

std::optional<std::shared_ptr<::cw::data::CWPyElement>> SystemData::get_resdata(bool arg_isbattle, ::cw::lib::Integer arg_resid)
{
    return arg_isbattle ? get_battledata(arg_resid, "", false, std::nullopt) : get_areadata(arg_resid, "", false, std::nullopt);
}

std::optional<std::shared_ptr<::cw::data::CWPyElement>> SystemData::get_carddata(std::shared_ptr<::cw::data::CWPyElement> arg_linkdata, bool arg_in_inusecard, std::optional<std::shared_ptr<::cw::header::CardHeader>> arg_inusecardheader)
{
    (void)arg_in_inusecard; (void)arg_inusecardheader;
    return arg_linkdata ? std::optional<std::shared_ptr<::cw::data::CWPyElement>>(std::move(arg_linkdata)) : std::nullopt;
}

bool SystemData::is_updatedfilenames()
{
    return {};
}

std::optional<std::shared_ptr<::cw::data::CWPyElement>> SystemData::_get_resdata_impl(const std::map<::cw::lib::Integer, std::tuple<::cw::lib::String, ::cw::lib::String>>& arg_table, ::cw::lib::Integer arg_resid, ::cw::lib::String arg_tag, bool arg_nocache, ::cw::lib::String arg_resname, std::optional<std::map<::cw::lib::String, ::cw::lib::String>> arg_rootattrs, std::map<::cw::lib::String, ::cw::lib::String>* output)
{
    const auto original = arg_table.find(arg_resid);
    const auto original_path = original == arg_table.end()
        ? "(未定義の" + arg_resname + " ID:" + std::to_string(arg_resid) + ")"
        : std::get<1>(original->second);
    const auto path = _get_resfpath(arg_table, arg_resid);
    if (!path) return std::nullopt;
    if (dynamic_cast<const ScenarioData*>(this)) {
        log_scenario_data("_get_resdata: resource=" + arg_resname +
                          " id=" + std::to_string(arg_resid) +
                          " path=" + *path +
                          " nocache=" + (arg_nocache ? "1" : "0"));
    }
    try {
        // Python版と同様、シナリオのキャッシュ方針は xml2element だけが管理
        // する。 外側にリソースキャッシュを置くと、無効設定でも可変XMLを共有してしまう。
        return xml2element_impl(*path, std::move(arg_tag), std::nullopt, std::nullopt, arg_nocache, std::move(arg_rootattrs), output);
    } catch (const std::exception& error) {
        // 未定義IDは上で黙って戻るが、存在するリソースの読み込み
        // 失敗では、 表の更新より前に元のファイル名を報告する。
        ::cw::util::print_ex(std::nullopt);
        const auto text = path_utf8(std::filesystem::u8path(original_path).filename()) + " の読込に失敗しました。";
        if (::cw::g_cwpy) ::cw::g_cwpy->call_modaldlg("ERROR", {{"text", ::cw::lib::Value(text)}});
        if (dynamic_cast<const ScenarioData*>(this)) {
            log_scenario_data("_get_resdata failed: resource=" + arg_resname +
                              " id=" + std::to_string(arg_resid) +
                              " path=" + *path +
                              " error=" + ::cw::lib::String(error.what()));
        }
        return std::nullopt;
    }
}

std::optional<std::shared_ptr<::cw::data::CWPyElement>> SystemData::_get_resdata(const std::map<::cw::lib::Integer, std::tuple<::cw::lib::String, ::cw::lib::String>>& arg_table, ::cw::lib::Integer arg_resid, ::cw::lib::String arg_tag, bool arg_nocache, ::cw::lib::String arg_resname, std::optional<std::map<::cw::lib::String, ::cw::lib::String>> arg_rootattrs)
{
    return _get_resdata_impl(std::move(arg_table), std::move(arg_resid), std::move(arg_tag), std::move(arg_nocache), std::move(arg_resname), std::move(arg_rootattrs), nullptr);
}

std::optional<std::shared_ptr<::cw::data::CWPyElement>> SystemData::_get_resdata(const std::map<::cw::lib::Integer, std::tuple<::cw::lib::String, ::cw::lib::String>>& arg_table, ::cw::lib::Integer arg_resid, ::cw::lib::String arg_tag, bool arg_nocache, ::cw::lib::String arg_resname, std::map<::cw::lib::String, ::cw::lib::String>& arg_rootattrs)
{
    return _get_resdata_impl(std::move(arg_table), std::move(arg_resid), std::move(arg_tag), std::move(arg_nocache), std::move(arg_resname), std::nullopt, &arg_rootattrs);
}


std::optional<::cw::lib::String> SystemData::_get_resname(std::map<::cw::lib::Integer, std::tuple<::cw::lib::String, ::cw::lib::String>> arg_table, ::cw::lib::Integer arg_resid)
{
    const auto it = arg_table.find(arg_resid);
    return it == arg_table.end() ? std::nullopt : std::optional<::cw::lib::String>(std::get<0>(it->second));
}

std::optional<::cw::lib::String> SystemData::_get_resfpath(const std::map<::cw::lib::Integer, std::tuple<::cw::lib::String, ::cw::lib::String>>& arg_table, ::cw::lib::Integer arg_resid)
{
    const auto it = arg_table.find(arg_resid);
    if (it == arg_table.end()) return std::nullopt;
    const auto path = std::get<1>(it->second);
    if (::cw::g_fsync && ::cw::g_fsync->is_waiting(path)) ::cw::g_fsync->sync();
    if (!mounted_or_native_is_file(std::filesystem::u8path(path)) && is_updatedfilenames()) {
        // Python版と同様、現在の辞書を渡し、再走査で消去・再設定する。
        // マップ自体は参照で保つが、無効になったイテレーターは再利用しない。
        _init_xmlpaths(true);
        const auto refreshed = arg_table.find(arg_resid);
        if (refreshed == arg_table.end()) return std::nullopt;
        return std::get<1>(refreshed->second);
    }
    return path;
}

// ネイティブのリソース表は順序付きマップなので、公開ID一覧はPython
// 辞書の挿入順ではなく数値キー順。 この安定した検索境界を維
// 持する。 デバッガーの選択肢とイベントのリソースツリーは
// 、Python版と同様、表示前にIDを明示的に整列する。
std::vector<::cw::lib::Integer> SystemData::_get_resids(std::map<::cw::lib::Integer, std::tuple<::cw::lib::String, ::cw::lib::String>> arg_table)
{
    std::vector<::cw::lib::Integer> result;
    result.reserve(arg_table.size());
    for (const auto& [id, unused] : arg_table) { (void)unused; result.push_back(id); }
    return result;
}

std::optional<std::shared_ptr<::cw::data::CWPyElement>> SystemData::get_areadata(::cw::lib::Integer arg_resid, ::cw::lib::String arg_tag, bool arg_nocache, std::optional<std::map<::cw::lib::String, ::cw::lib::String>> arg_rootattrs)
{
    return _get_resdata(m__areas, arg_resid, std::move(arg_tag), arg_nocache, "Area", std::move(arg_rootattrs));
}

std::optional<std::shared_ptr<::cw::data::CWPyElement>> SystemData::get_areadata(::cw::lib::Integer arg_resid, ::cw::lib::String arg_tag, bool arg_nocache, std::map<::cw::lib::String, ::cw::lib::String>& arg_rootattrs)
{
    return _get_resdata(m__areas, arg_resid, std::move(arg_tag), arg_nocache, "Area", arg_rootattrs);
}

std::optional<::cw::lib::String> SystemData::get_areaname(::cw::lib::Integer arg_resid)
{
    return _get_resname(m__areas, arg_resid);
}

std::optional<::cw::lib::String> SystemData::get_areafpath(::cw::lib::Integer arg_resid)
{
    return _get_resfpath(m__areas, arg_resid);
}

std::vector<::cw::lib::Integer> SystemData::get_areaids()
{
    return _get_resids(m__areas);
}

std::optional<std::shared_ptr<::cw::data::CWPyElement>> SystemData::get_battledata(::cw::lib::Integer arg_resid, ::cw::lib::String arg_tag, bool arg_nocache, std::optional<std::map<::cw::lib::String, ::cw::lib::String>> arg_rootattrs)
{
    return _get_resdata(m__battles, arg_resid, std::move(arg_tag), arg_nocache, "Battle", std::move(arg_rootattrs));
}

std::optional<std::shared_ptr<::cw::data::CWPyElement>> SystemData::get_battledata(::cw::lib::Integer arg_resid, ::cw::lib::String arg_tag, bool arg_nocache, std::map<::cw::lib::String, ::cw::lib::String>& arg_rootattrs)
{
    return _get_resdata(m__battles, arg_resid, std::move(arg_tag), arg_nocache, "Battle", arg_rootattrs);
}

std::optional<::cw::lib::String> SystemData::get_battlename(::cw::lib::Integer arg_resid)
{
    return _get_resname(m__battles, arg_resid);
}

std::optional<::cw::lib::String> SystemData::get_battlefpath(::cw::lib::Integer arg_resid)
{
    return _get_resfpath(m__battles, arg_resid);
}

std::vector<::cw::lib::Integer> SystemData::get_battleids()
{
    return _get_resids(m__battles);
}

std::optional<std::shared_ptr<::cw::data::CWPyElement>> SystemData::get_packagedata(::cw::lib::Integer arg_resid, ::cw::lib::String arg_tag, bool arg_nocache, std::optional<std::map<::cw::lib::String, ::cw::lib::String>> arg_rootattrs)
{
    return _get_resdata(m__packs, arg_resid, std::move(arg_tag), arg_nocache, "Package", std::move(arg_rootattrs));
}

std::optional<std::shared_ptr<::cw::data::CWPyElement>> SystemData::get_packagedata(::cw::lib::Integer arg_resid, ::cw::lib::String arg_tag, bool arg_nocache, std::map<::cw::lib::String, ::cw::lib::String>& arg_rootattrs)
{
    return _get_resdata(m__packs, arg_resid, std::move(arg_tag), arg_nocache, "Package", arg_rootattrs);
}

std::optional<::cw::lib::String> SystemData::get_packagename(::cw::lib::Integer arg_resid)
{
    return _get_resname(m__packs, arg_resid);
}

std::optional<::cw::lib::String> SystemData::get_packagefpath(::cw::lib::Integer arg_resid)
{
    return _get_resfpath(m__packs, arg_resid);
}

std::vector<::cw::lib::Integer> SystemData::get_packageids()
{
    return _get_resids(m__packs);
}

std::optional<std::shared_ptr<::cw::data::CWPyElement>> SystemData::get_castdata(::cw::lib::Integer arg_resid, ::cw::lib::String arg_tag, bool arg_nocache, std::optional<std::map<::cw::lib::String, ::cw::lib::String>> arg_rootattrs)
{
    return _get_resdata(m__casts, arg_resid, std::move(arg_tag), arg_nocache, "CastCard", std::move(arg_rootattrs));
}

std::optional<std::shared_ptr<::cw::data::CWPyElement>> SystemData::get_castdata(::cw::lib::Integer arg_resid, ::cw::lib::String arg_tag, bool arg_nocache, std::map<::cw::lib::String, ::cw::lib::String>& arg_rootattrs)
{
    return _get_resdata(m__casts, arg_resid, std::move(arg_tag), arg_nocache, "CastCard", arg_rootattrs);
}

std::optional<::cw::lib::String> SystemData::get_castname(::cw::lib::Integer arg_resid)
{
    return _get_resname(m__casts, arg_resid);
}

std::optional<::cw::lib::String> SystemData::get_castfpath(::cw::lib::Integer arg_resid)
{
    return _get_resfpath(m__casts, arg_resid);
}

std::vector<::cw::lib::Integer> SystemData::get_castids()
{
    return _get_resids(m__casts);
}

std::optional<std::shared_ptr<::cw::data::CWPyElement>> SystemData::get_skilldata(::cw::lib::Integer arg_resid, ::cw::lib::String arg_tag, bool arg_nocache, std::optional<std::map<::cw::lib::String, ::cw::lib::String>> arg_rootattrs)
{
    return _get_resdata(m__skills, arg_resid, std::move(arg_tag), arg_nocache, "SkillCard", std::move(arg_rootattrs));
}

std::optional<std::shared_ptr<::cw::data::CWPyElement>> SystemData::get_skilldata(::cw::lib::Integer arg_resid, ::cw::lib::String arg_tag, bool arg_nocache, std::map<::cw::lib::String, ::cw::lib::String>& arg_rootattrs)
{
    return _get_resdata(m__skills, arg_resid, std::move(arg_tag), arg_nocache, "SkillCard", arg_rootattrs);
}

std::optional<::cw::lib::String> SystemData::get_skillname(::cw::lib::Integer arg_resid)
{
    return _get_resname(m__skills, arg_resid);
}

std::optional<::cw::lib::String> SystemData::get_skillfpath(::cw::lib::Integer arg_resid)
{
    return _get_resfpath(m__skills, arg_resid);
}

std::vector<::cw::lib::Integer> SystemData::get_skillids()
{
    return _get_resids(m__skills);
}

std::optional<std::shared_ptr<::cw::data::CWPyElement>> SystemData::get_itemdata(::cw::lib::Integer arg_resid, ::cw::lib::String arg_tag, bool arg_nocache, std::optional<std::map<::cw::lib::String, ::cw::lib::String>> arg_rootattrs)
{
    return _get_resdata(m__items, arg_resid, std::move(arg_tag), arg_nocache, "ItemCard", std::move(arg_rootattrs));
}

std::optional<std::shared_ptr<::cw::data::CWPyElement>> SystemData::get_itemdata(::cw::lib::Integer arg_resid, ::cw::lib::String arg_tag, bool arg_nocache, std::map<::cw::lib::String, ::cw::lib::String>& arg_rootattrs)
{
    return _get_resdata(m__items, arg_resid, std::move(arg_tag), arg_nocache, "ItemCard", arg_rootattrs);
}

std::optional<::cw::lib::String> SystemData::get_itemname(::cw::lib::Integer arg_resid)
{
    return _get_resname(m__items, arg_resid);
}

std::optional<::cw::lib::String> SystemData::get_itemfpath(::cw::lib::Integer arg_resid)
{
    return _get_resfpath(m__items, arg_resid);
}

std::vector<::cw::lib::Integer> SystemData::get_itemids()
{
    return _get_resids(m__items);
}

std::optional<std::shared_ptr<::cw::data::CWPyElement>> SystemData::get_beastdata(::cw::lib::Integer arg_resid, ::cw::lib::String arg_tag, bool arg_nocache, std::optional<std::map<::cw::lib::String, ::cw::lib::String>> arg_rootattrs)
{
    return _get_resdata(m__beasts, arg_resid, std::move(arg_tag), arg_nocache, "BeastCard", std::move(arg_rootattrs));
}

std::optional<std::shared_ptr<::cw::data::CWPyElement>> SystemData::get_beastdata(::cw::lib::Integer arg_resid, ::cw::lib::String arg_tag, bool arg_nocache, std::map<::cw::lib::String, ::cw::lib::String>& arg_rootattrs)
{
    return _get_resdata(m__beasts, arg_resid, std::move(arg_tag), arg_nocache, "BeastCard", arg_rootattrs);
}

std::optional<::cw::lib::String> SystemData::get_beastname(::cw::lib::Integer arg_resid)
{
    return _get_resname(m__beasts, arg_resid);
}

std::optional<::cw::lib::String> SystemData::get_beastfpath(::cw::lib::Integer arg_resid)
{
    return _get_resfpath(m__beasts, arg_resid);
}

std::vector<::cw::lib::Integer> SystemData::get_beastids()
{
    return _get_resids(m__beasts);
}

std::optional<std::shared_ptr<::cw::data::CWPyElement>> SystemData::get_infodata(::cw::lib::Integer arg_resid, ::cw::lib::String arg_tag, bool arg_nocache, std::optional<std::map<::cw::lib::String, ::cw::lib::String>> arg_rootattrs)
{
    return _get_resdata(m__infos, arg_resid, std::move(arg_tag), arg_nocache, "InfoCard", std::move(arg_rootattrs));
}

std::optional<std::shared_ptr<::cw::data::CWPyElement>> SystemData::get_infodata(::cw::lib::Integer arg_resid, ::cw::lib::String arg_tag, bool arg_nocache, std::map<::cw::lib::String, ::cw::lib::String>& arg_rootattrs)
{
    return _get_resdata(m__infos, arg_resid, std::move(arg_tag), arg_nocache, "InfoCard", arg_rootattrs);
}

std::optional<::cw::lib::String> SystemData::get_infoname(::cw::lib::Integer arg_resid)
{
    return _get_resname(m__infos, arg_resid);
}

std::optional<::cw::lib::String> SystemData::get_infofpath(::cw::lib::Integer arg_resid)
{
    return _get_resfpath(m__infos, arg_resid);
}

std::vector<::cw::lib::Integer> SystemData::get_infoids()
{
    return _get_resids(m__infos);
}

::cw::lib::String SystemData::_get_carddatapath(::cw::lib::String arg_rtype, ::cw::lib::Integer arg_resid, ::cw::lib::String arg_dpath)
{
    if (::cw::g_fsync) ::cw::g_fsync->sync();
    const auto directory = std::filesystem::u8path(join_path(arg_dpath, arg_rtype));
    if (!mounted_or_native_is_directory(directory)) return {};
    for (const auto& entry : mounted_or_native_list(directory)) {
        if (!mounted_or_native_is_file(entry)) continue;
        auto ext = entry.extension().generic_string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext != ".xml") continue;
        {
            ::cw::header::GetName reader;
            reader.initialize(path_utf8(entry), "Id");
            const auto& idtext = reader.m_name;
            // GetNameのIdは、XMLのgetintの int(float) ではなく int(str) で変換する。
            if (!idtext.empty() && ::cw::lib::require_python_integer(idtext) == arg_resid)
                return path_utf8(entry);
        }}
    return {};
}

void SystemData::copy_carddata(std::shared_ptr<::cw::data::CWPyElement> arg_linkdata, ::cw::lib::String arg_dstdir, bool arg_from_scenario, ::cw::lib::String arg_scedir, std::map<::cw::lib::String, ::cw::lib::String>& arg_imgpaths)
{
    if (!arg_linkdata || arg_linkdata->m_tag != "BeastCard")
        throw std::invalid_argument("copy_carddata requires BeastCard");
    const auto resid = arg_linkdata->getint("Property/LinkId", ::cw::lib::Value(::cw::lib::Integer{0}), std::nullopt);
    if (resid == 0) return;

    ::cw::lib::String path;
    std::shared_ptr<CWPyElementTree> data;
    ::cw::lib::String dstrelative;
    if (arg_scedir == m_scedir) {
        const auto source = get_beastfpath(resid);
        if (!source || !mounted_or_native_is_file(std::filesystem::u8path(*source))) return;
        path = *source;
        const auto element = get_beastdata(resid, "", false, std::nullopt);
        if (!element || !*element) return;
        data = std::make_shared<CWPyElementTree>(*element, path);
        dstrelative = ::cw::util::relpath(path, m_tempdir);
    } else {
        path = _get_carddatapath(arg_linkdata->m_tag, resid, arg_scedir);
        try { data = xml2etree(path, "", std::nullopt, std::nullopt, false); }
        catch (const std::exception&) { ::cw::util::print_ex(std::nullopt); return; }
        dstrelative = ::cw::util::relpath(path, arg_scedir);
    }
    if (!data || path.empty() || arg_imgpaths.contains(path)) return;
    data = copytree(data, true);
    if (!data) return;
    const auto dstpath = join_path(arg_dstdir, dstrelative);
    arg_imgpaths[path] = dstpath;
    const auto can_loaded_scaledimage = data->getbool(".", ::cw::lib::Value("scaledimage"), false);

    if (::cw::g_cwpy) {
        ::cw::g_cwpy->copy_materials(
            ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(data)),
            arg_dstdir, arg_from_scenario, arg_scedir, std::nullopt, std::nullopt, false,
            // Python版と同様、importimageは省略時のfalseを使う。
            // リンクした召喚獣の符号化済み素材は、通常のコピーで外部ファイルにする。
            arg_imgpaths, false, can_loaded_scaledimage);
    }
    // CWPyが存在しない単体データ変換では、画像は解決せずXML本体だけを保存する。
    data->m_fpath = dstpath;
    data->write_xml(true);
}

bool SystemData::change_data(::cw::lib::Integer arg_resid, std::optional<std::shared_ptr<::cw::data::CWPyElement>> arg_data)
{
    auto data = arg_data.value_or(nullptr);
    if (!data && ::cw::g_cwpy) {
        const auto loaded = get_resdata(::cw::g_cwpy->is_battlestatus(), arg_resid);
        if (loaded && *loaded) data = *loaded;
    }
    if (!data) return false;
    // Python版と同様、データ、シナリオのヒント、エリア名を反映してからイベ
    // ントエンジンを作る。 シナリオ・システムの全呼び出し元で同じ経路を使う。
    m_data = data;
    if (auto* scenario = dynamic_cast<ScenarioData*>(this);
        scenario && ::cw::g_cwpy && ::cw::g_cwpy->m_sct) {
        scenario->set_versionhint(::cw::g_HINT_AREA,
            ::cw::g_cwpy->m_sct->from_basehint(data->getattr("Property", "versionHint", "")));
    }
    if (::cw::g_cwpy && ::cw::g_cwpy->m_event)
        ::cw::g_cwpy->m_event->refresh_areaname();
    std::vector<std::shared_ptr<CWPyElement>> events;
    if (const auto group = value_to_element(data->getfind("Events", true))) events = group->m_children;
    auto engine = std::make_shared<::cw::event::EventEngine>();
    engine->initialize(std::move(events), "Main");
    m_events = engine;
    std::vector<std::shared_ptr<CWPyElement>> player_events;
    if (const auto group = value_to_element(data->getfind("PlayerCardEvents/Events", false)))
        player_events = group->m_children;
    auto player_engine = std::make_shared<::cw::event::EventEngine>();
    player_engine->initialize(std::move(player_events), "Main");
    m_playerevents = player_engine;
    return true;
}

void SystemData::start_event(std::optional<::cw::lib::Integer> arg_keynum, std::optional<std::vector<::cw::lib::String>> arg_keycodes, bool arg_redraw)
{
#if defined(CARDWIRTH_LOG_ENABLED)
    ::cardwirth::debug::log(
        "SystemData start_event begin: key=" +
        (arg_keynum ? std::to_string(*arg_keynum) : ::cw::lib::String("none")) +
        " keycodes=" +
        (arg_keycodes ? std::to_string(arg_keycodes->size()) : ::cw::lib::String("none")) +
        " events=" +
        (m_events && *m_events ? std::to_string((*m_events)->m_event_list.size()) : ::cw::lib::String("none")));
#endif
    if (!m_events || !*m_events) {
#if defined(CARDWIRTH_LOG_ENABLED)
        ::cardwirth::debug::log("SystemData start_event: event engine is missing");
#endif
        throw std::logic_error("SystemData.start_event requires an event engine");
    }
    if (::cw::g_cwpy) {
        if (::cw::g_cwpy->m_statusbar) ::cw::g_cwpy->m_statusbar->change(false, false);
        // change() はステータスバーのボタンを再構築する。 マウ
        // ス選択は意図的な非所有の別参照なので、保持するとEventEngine
        // に無効なTableButton/CampButtonが残る
        // 。 イベント開始経路は下でキーボード・メニュー位置を保持済
        // みのため、再構築後は一時的なsprite参照だけを消す。
        ::cw::g_cwpy->m_selection.reset();
        ::cw::g_cwpy->m_lazy_selection.reset();
        if (auto* handler = ::cw::eventhandler::runtime_context()) {
            handler->selection.reset();
            handler->pointed_tile_selection.reset();
            handler->index = -1;
        }
    }
    const auto keycodes = arg_keycodes.value_or(std::vector<::cw::lib::String>{});
    const bool started = (*m_events)->start(arg_keynum, keycodes, false, false);
#if defined(CARDWIRTH_LOG_ENABLED)
    ::cardwirth::debug::log("SystemData start_event: key=" +
        (arg_keynum ? std::to_string(*arg_keynum) : ::cw::lib::String("none")) +
        " events=" + std::to_string((*m_events)->m_event_list.size()) +
        " started=" + (started ? "true" : "false"));
#endif
    // Python版と同様、イベント後の再描画は状態文字列ではなく戦闘
    // オブジェクトの有無で判定する。 戦闘遷移ではScenarioBattle
    // の反映前後にオブジェクトが設定されるため、 状態だけを見ると
    // 古いパーティを再描画し、無効な操作部品を見せる場合がある。
    const bool battle_active = ::cw::g_cwpy && ::cw::g_cwpy->m_battle && *::cw::g_cwpy->m_battle;
    if (::cw::g_cwpy && !::cw::g_cwpy->is_dealing() && !battle_active) {
        if (::cw::g_cwpy->m_statusbar) ::cw::g_cwpy->m_statusbar->change(true, false);
        // Python版と同様、ここで調べるのはFORCE_USEREVENTではなくpygame.USEREVENT。
        const bool pending = ::cw::g_cwpy->has_pending_game_commands(false);
        if (!pending) {
            ::cw::g_cwpy->show_party();
            if (arg_redraw) ::cw::g_cwpy->disposition_pcards();
        }
    }
}

void SystemData::reload_variables()
{
    std::vector<std::pair<::cw::lib::String, bool>> flags;
    std::vector<std::pair<::cw::lib::String, ::cw::lib::Integer>> steps;
    std::vector<std::pair<::cw::lib::String, ::cw::lib::Value>> variants;
    for (const auto& name : ordered_variable_names(m_flags, m_flags_order))
        if (const auto& value = m_flags.at(name)) flags.emplace_back(name, value->m_value);
    for (const auto& name : ordered_variable_names(m_steps, m_steps_order))
        if (const auto& value = m_steps.at(name)) steps.emplace_back(name, value->m_value);
    for (const auto& name : ordered_variable_names(m_variants, m_variants_order))
        if (const auto& value = m_variants.at(name)) variants.emplace_back(name, value->m_value);
    _init_flags(); _init_steps(); _init_variants();
    // 新しいXMLやマップの整列順ではなく、以前の辞書の順序で復元する。
    for (const auto& [name, value] : flags) { const auto it = m_flags.find(name); if (it != m_flags.end() && it->second) it->second->set(value, false); }
    for (const auto& [name, value] : steps) { const auto it = m_steps.find(name); if (it != m_steps.end() && it->second) it->second->set(value, false); }
    for (const auto& [name, value] : variants) { const auto it = m_variants.find(name); if (it != m_variants.end() && it->second) it->second->set(value, false); }
}

::cw::lib::String SystemData::get_currentareaname()
{
    const auto areaid = ::cw::g_cwpy ? ::cw::g_cwpy->m_areaid : m_startid;
    if (::cw::g_cwpy && ::cw::g_cwpy->is_battlestatus()) {
        if (const auto name = get_battlename(areaid)) return *name;
    } else if (const auto name = get_areaname(areaid)) {
        return *name;
    }
    return "(読込失敗)";
}

std::vector<std::shared_ptr<::cw::data::CWPyElement>> SystemData::get_bgdata(std::optional<std::vector<std::shared_ptr<::cw::data::CWPyElement>>> arg_e)
{
    if (arg_e) return *arg_e;
    // 現在エリアのデータなしをassertで拒否するPython版
    // とは異なり、 この任意の背景問い合わせはツリーが使えなければ
    // 空を返す。 BgImagesグループがない場合は両方とも空。
    if (!m_data || !*m_data) return {};
    const auto bg = (*m_data)->find("BgImages");
    if (!bg || !*bg) return {};
    return (*bg)->m_children;
}

std::tuple<::cw::lib::String, std::vector<std::shared_ptr<::cw::data::CWPyElement>>> SystemData::get_mcarddata(std::optional<::cw::lib::Integer> arg_resid, std::optional<bool> arg_battlestatus, std::optional<std::shared_ptr<::cw::data::CWPyElement>> arg_data)
{
    const bool battlestatus = arg_battlestatus.value_or(::cw::g_cwpy && ::cw::g_cwpy->is_battlestatus());
    std::shared_ptr<::cw::data::CWPyElement> data;
    if (arg_data && *arg_data) data = *arg_data;
    else if (!arg_resid && m_data && *m_data) data = *m_data;
    else if (arg_resid) {
        const auto loaded = battlestatus ? get_battledata(*arg_resid, "", false, std::nullopt)
                                         : get_areadata(*arg_resid, "", false, std::nullopt);
        if (loaded && *loaded) data = *loaded;
    }
    // ネイティブの意図的な境界として、現在エリアのデータがなければ空のCustom
    // カードグループを返す。 Python版はデータの存在をassertで要求する
    // が、MenuCards/EnemyCardsグループなしは両方とも空になる。
    if (!data) return {"Custom", {}};
    auto cards = data->find("MenuCards");
    if (!cards || !*cards) cards = data->find("EnemyCards");
    if (!cards || !*cards) return {"Custom", {}};
    return {(*cards)->get("spreadtype", "Auto"), (*cards)->m_children};
}

std::vector<::cw::lib::String> SystemData::get_bgmpaths()
{
    std::vector<::cw::lib::String> result;
    const std::set<::cw::lib::String> extensions{".ogg", ".mp3", ".mid", ".wav"};
    std::vector<std::filesystem::path> roots;
    if (::cw::g_fsync) ::cw::g_fsync->sync();
    if (::cw::g_cwpy) {
        const auto skin = value_to_string(::cw::g_cwpy->m_skindir);
        roots.emplace_back(std::filesystem::u8path(join_path(skin, "Bgm")));
        roots.emplace_back(std::filesystem::u8path(join_path(skin, "BgmAndSound")));
    }
    // ディレクトリ不足への意図的な許容として、Data/Materialsがなくても利用可能なスキン
    // のBGMルートを残す。 Pythonのos.listdirは、そのルートがないと例外になる。
    const auto materials = std::filesystem::u8path("Data/Materials");
    if (mounted_or_native_is_directory(materials)) {
        for (const auto& entry : mounted_or_native_list(materials)) {
            if (!mounted_or_native_is_directory(entry)) continue;
            roots.emplace_back(entry / "Bgm");
            roots.emplace_back(entry / "BgmAndSound");
        }
    }
    for (const auto& root : roots) {
        const auto base = ::cardwirth::platform::resolve_mounted_user_path(root).value_or(root);
        if (!mounted_or_native_is_directory(base)) continue;
        for (const auto& entry : mounted_or_native_files_recursive(base)) {
            auto ext = entry.extension().generic_string(); std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (!extensions.contains(ext)) continue;
            result.push_back(relative_path_utf8(entry, base));
        }
    }
    return result;
}

void SystemData::fullrecovery_fcards()
{
}

bool SystemData::has_infocards()
{
    return std::any_of(m_infocards.begin(), m_infocards.end(), [](auto value) { return value != 0; });
}

void SystemData::_tidying_infocards()
{
    std::vector<::cw::lib::Integer> nums;
    nums.reserve(m_infocards.size());
    for (const auto value : m_infocards) if (value > 0) nums.push_back(value);
    std::sort(nums.begin(), nums.end());
    std::map<::cw::lib::Integer, ::cw::lib::Integer> indexes;
    for (std::size_t i = 0; i < nums.size(); ++i) indexes[nums[i]] = static_cast<::cw::lib::Integer>(i + 1);
    m_infocard_maxindex = static_cast<::cw::lib::Integer>(nums.size());
    for (auto& value : m_infocards) {
        // Python版のKeyErrorに対応する欠落キーの例
        // 外は、カウンターと先行スロットを更新した後に発生する。
        value = indexes.at(value);
    }
}

std::vector<::cw::lib::Integer> SystemData::get_infocards(bool arg_order)
{
    std::vector<::cw::lib::Integer> result;
    if (arg_order) {
        std::vector<std::pair<::cw::lib::Integer, ::cw::lib::Integer>> table;
        for (std::size_t resid = 0; resid < m_infocards.size(); ++resid) {
            if (m_infocards[resid] > 0) table.emplace_back(m_infocards[resid], static_cast<::cw::lib::Integer>(resid));
        }
        std::sort(table.begin(), table.end(), std::greater<>());
        for (const auto& [unused, resid] : table) { (void)unused; result.push_back(resid); }
    } else {
        for (std::size_t resid = 0; resid < m_infocards.size(); ++resid)
            if (m_infocards[resid] > 0) result.push_back(static_cast<::cw::lib::Integer>(resid));
    }
    return result;
}

void SystemData::append_infocard(::cw::lib::Integer arg_resid)
{
    if (m_infocard_maxindex >= 0x7fffffff) _tidying_infocards();
    if (arg_resid >= 0 && m_infocards.size() <= static_cast<std::size_t>(arg_resid))
        m_infocards.resize(static_cast<std::size_t>(arg_resid) + 1, 0);
    ++m_infocard_maxindex;
    // Python版では範囲外参照でIndexErrorとなる。ネイティブ
    // はat()の範囲外例外を使い、いずれもカウンター更新後に失敗する。
    const auto index = arg_resid < 0 ? arg_resid + static_cast<::cw::lib::Integer>(m_infocards.size()) : arg_resid;
    m_infocards.at(static_cast<std::size_t>(index)) = m_infocard_maxindex;
}

void SystemData::remove_infocard(::cw::lib::Integer arg_resid)
{
    if (arg_resid < static_cast<::cw::lib::Integer>(m_infocards.size())) {
        const auto index = arg_resid < 0 ? arg_resid + static_cast<::cw::lib::Integer>(m_infocards.size()) : arg_resid;
        m_infocards.at(static_cast<std::size_t>(index)) = 0;
    }
}

bool SystemData::has_infocard(::cw::lib::Integer arg_resid)
{
    if (arg_resid >= static_cast<::cw::lib::Integer>(m_infocards.size())) return false;
    const auto index = arg_resid < 0 ? arg_resid + static_cast<::cw::lib::Integer>(m_infocards.size()) : arg_resid;
    return m_infocards.at(static_cast<std::size_t>(index)) != 0;
}

::cw::lib::Integer SystemData::count_infocards()
{
    return static_cast<::cw::lib::Integer>(std::count_if(m_infocards.begin(), m_infocards.end(), [](auto value) { return value != 0; }));
}

std::vector<std::shared_ptr<::cw::header::InfoCardHeader>> SystemData::get_infocardheaders()
{
    std::vector<std::shared_ptr<::cw::header::InfoCardHeader>> result;
    const auto ids = get_infocards(true);
    const auto infoids = get_infoids();
    for (const auto resid : ids) {
        if (const auto it = m__infocard_cache.find(resid); it != m__infocard_cache.end()) {
            result.push_back(it->second);
            continue;
        }
        if (std::find(infoids.begin(), infoids.end(), resid) == infoids.end()) continue;
        std::map<::cw::lib::String, ::cw::lib::String> rootattrs;
        auto root = get_infodata(resid, "Property", false, rootattrs);
        if (!root || !*root) continue;
        auto header = std::make_shared<::cw::header::InfoCardHeader>();
        const auto scaled = rootattrs.find("scaledimage");
        // Python版と同様、このカードのルート属性を既定falseで読む
        // 。 シナリオ全体の画像フラグを、ここで継承する既定値にはしない。
        const bool can_scaled = ::cw::util::str2bool(
            scaled == rootattrs.end() ? "False" : scaled->second);
        header->initialize(*root, can_scaled);
        m__infocard_cache[resid] = header;
        result.push_back(std::move(header));
    }
    return result;
}

bool SystemData::can_gameover()
{
    if (::cw::g_cwpy && ::cw::g_cwpy->m_in_actionevent) return false;
    if (m_party_environment_gameover) return true;
    if (::cw::g_cwpy) return ::cw::g_cwpy->get_pcards(::cw::lib::Value{}).empty();
    return false;
}

void SystemData::enableFastLoadCache()
{
    m__fastLoadCache = true;
}

void SystemData::disableFastLoadCache()
{
    m__fastLoadCache = false;
}

bool SystemData::fastLoadCacheEnabled()
{
    return m__fastLoadCache;
}

void Variant::initialize(std::optional<::cw::lib::Value> arg_parent, std::optional<std::shared_ptr<::cw::data::CWPyElement>> arg_data, ::cw::lib::Value arg_value, ::cw::lib::String arg_name, ::cw::lib::Value arg_defaultvalue)
{
    m__parent = arg_parent.value_or(::cw::lib::Value{});
    if (arg_data && *arg_data) m__data = ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(*arg_data));
    else m__data = {};
    m_value = std::move(arg_value);
    m_defaultvalue = std::move(arg_defaultvalue);
    m_name = std::move(arg_name);
    m_type = value_to_type(m_value);
    m_is_writable = arg_parent.has_value() && arg_data.has_value() && static_cast<bool>(*arg_data);
    m_initialization = parse_initialization(std::optional<::cw::lib::Value>(::cw::lib::Value("Leave")));
    if (arg_data && *arg_data) m_initialization = parse_initialization(std::optional<::cw::lib::Value>((*arg_data)->get("initialize", "Leave")));
}

void Variant::set(::cw::lib::Value arg_value, bool arg_updatedebugger)
{
    if (Variant::values_equal(m_value, arg_value)) return;
    mark_variable_value_changing();
    m_value = std::move(arg_value);
    m_type = value_to_type(m_value);
    if (::cw::g_cwpy) ::cw::g_cwpy->update_mcardnames();
    if (arg_updatedebugger && ::cw::g_cwpy && ::cw::g_cwpy->m_event) {
        if (const auto self = weak_from_this().lock())
            ::cw::g_cwpy->m_event->refresh_variable(::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(self)));
    }
}

bool Variant::values_equal(const ::cw::lib::Value& lhs, const ::cw::lib::Value& rhs)
{
    return variable_values_equal(lhs, rhs);
}

::cw::lib::String Variant::value_to_type(::cw::lib::Value arg_value)
{
    if (std::holds_alternative<bool>(arg_value.storage())) return "Boolean";
    // ネイティブの算術はPythonのDecimalに加えてInteger/Real
    // も使う。 保存形式では、三つとも同じNumber分類にする。
    if (std::holds_alternative<::cw::lib::Real>(arg_value.storage()) || std::holds_alternative<::cw::lib::Integer>(arg_value.storage()) || std::holds_alternative<::cw::lib::Decimal>(arg_value.storage())) return "Number";
    if (std::holds_alternative<::cw::lib::String>(arg_value.storage())) return "String";
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&arg_value.storage())) {
        if (std::dynamic_pointer_cast<VariantSequence>(*object)) return "List";
        if (std::dynamic_pointer_cast<StructVal>(*object)) return "Structure";
    }
    // Bytesは旧生成Value表現を使う呼び出し元用の単一
    // 値の代替として残す。 有効なCWのコモン一覧ではない。
    if (std::holds_alternative<::cw::lib::Bytes>(arg_value.storage())) return "List";
    // Python版と同様、None・未知のオブジェクトも最後のList
    // 分岐で分類する。 その分類で、後の値の利用側の検証を省略しない。
    return "List";
}

::cw::lib::Value Variant::value_from_element(std::shared_ptr<::cw::data::CWPyElement> arg_e, ::cw::lib::String arg_typeattr, std::optional<::cw::lib::String> arg_valueattr)
{
    if (!arg_e) return {};
    const auto type_it = arg_e->m_attributes.find(arg_typeattr);
    if (type_it == arg_e->m_attributes.end())
        throw std::invalid_argument("Missing variant type attribute: " + arg_typeattr);
    const auto type = type_it->second;
    // Python版と同様、value属性を持つ形式では、その属性を必須とする。
    // List/Structureコンテナーには、単一値のvalue属性はない。
    const auto scalar_text = [&] {
        return arg_valueattr ? arg_e->getattr(".", *arg_valueattr, std::nullopt) : arg_e->m_text;
    };
    if (type == "Boolean") return ::cw::lib::Value(::cw::util::str2bool(scalar_text()));
    if (type == "Number") {
        { return ::cw::lib::Value(::cw::lib::Decimal::parse(scalar_text())); }}
    if (type == "List") {
        auto sequence = std::make_shared<VariantSequence>();
        for (const auto& child : arg_e->m_children) {
            if (child && child->m_tag == "Value")
                sequence->m_values.push_back(value_from_element(
                    child, "type", std::optional<::cw::lib::String>("value")));
        }
        return ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(sequence));
    }
    if (type == "Structure") {
        auto structure = std::make_shared<StructVal>();
        ::cw::lib::String name;
        for (const auto& child : arg_e->m_children) {
            if (!child) continue;
            if (child->m_tag == "StructureName") name = child->m_text;
            else if (child->m_tag == "Member") {
                const auto value = value_from_element(
                    child, "type", std::optional<::cw::lib::String>("value"));
                const auto member_name = child->getattr(".", "name", std::nullopt);
                structure->m_member_names.push_back(member_name);
                structure->m_member_values.push_back(value);
            }
        }
        std::vector<std::tuple<::cw::lib::String, ::cw::lib::Value>> check;
        for (std::size_t i = 0; i < structure->m_member_values.size(); ++i)
            check.emplace_back(structure->m_member_names[i], structure->m_member_values[i]);
        const auto normalized_name = scenario_lower(name);
        if (!::cw::calculator::is_valid_structure(normalized_name, check)) {
            ::cw::lib::String detail;
            for (std::size_t i = 0; i < check.size(); ++i) {
                if (i) detail += ", ";
                detail += std::get<0>(check[i]) + "=" + value_to_str(std::get<1>(check[i]), false);
            }
            throw std::invalid_argument("Invalid structure " + (normalized_name.empty() ? "<No Name>" : normalized_name) + "(" + detail + ")");
        }
        structure->m_name = normalized_name;
        const auto info = ::cw::calculator::struct_info(normalized_name);
        std::vector<::cw::lib::String> member_names(info->m__index_table.size());
        for (const auto& [member_name, raw_index] : info->m__index_table) {
            const auto index = value_integer_or(raw_index, -1);
            if (index >= 0 && static_cast<std::size_t>(index) < member_names.size())
                member_names[static_cast<std::size_t>(index)] = member_name;
        }
        structure->m_member_values = ::cw::calculator::complete_optionalmembers(
            normalized_name, std::move(structure->m_member_values));
        structure->initialize(normalized_name, structure->m_member_values);
        structure->m_member_names = std::move(member_names);
        return ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(structure));
    }
    if (type != "String") throw std::invalid_argument("Invalid variant type: " + type);
    return ::cw::lib::Value(scalar_text());
}

::cw::lib::String Variant::value_to_str(::cw::lib::Value arg_value, bool arg_splitlines)
{
    if (const auto* p = std::get_if<::cw::lib::String>(&arg_value.storage())) {
        if (arg_splitlines && p->find('\n') != ::cw::lib::String::npos) {
            ::cw::lib::String result;
            bool first = true;
            // Python版と同様、LFがある場合だけ式の整形を有効にする。 その後の
            // str.splitlines は、他のすべての改行区切りも分割する。
            for (const auto& line : ::cw::lib::encoding::splitlines(*p)) {
                ::cw::lib::String quoted = "\"";
                for (const auto ch : line) { quoted += ch; if (ch == '\"') quoted += '\"'; }
                quoted += "\"";
                if (!first) result += " ~ NEWLINE ~ ";
                result += quoted;
                first = false;
            }
            return result;
        }
        return *p;
    }
    if (const auto* p = std::get_if<bool>(&arg_value.storage())) return *p ? "TRUE" : "FALSE";
    if (const auto* p = std::get_if<::cw::lib::Integer>(&arg_value.storage())) return std::to_string(*p);
    if (const auto* p = std::get_if<::cw::lib::Real>(&arg_value.storage())) {
        std::ostringstream stream;
        stream << std::fixed << std::setprecision(8) << *p;
        auto result = stream.str();
        while (result.size() > 1 && result.back() == '0') result.pop_back();
        if (!result.empty() && result.back() == '.') result.pop_back();
        return result.empty() ? "0" : result;
    }
    if (const auto* p = std::get_if<::cw::lib::Decimal>(&arg_value.storage())) {
        std::ostringstream stream;
        stream.imbue(std::locale::classic());
        stream << std::fixed << std::setprecision(8);
        // 計算式の整形と同じDecimalからfloatへの境界を使う。
        // アンダーフローは無限大ではなく、符号付きゼロ・非正規化数にする。
        stream << p->to_real();
        auto result = stream.str();
        while (!result.empty() && result.back() == '0') result.pop_back();
        if (!result.empty() && result.back() == '.') result.pop_back();
        return result.empty() ? "0" : result;
    }
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&arg_value.storage())) {
        if (const auto sequence = std::dynamic_pointer_cast<VariantSequence>(*object)) {
            std::vector<::cw::lib::String> values;
            values.reserve(sequence->m_values.size());
            for (const auto& value : sequence->m_values) {
                if (const auto* text = std::get_if<::cw::lib::String>(&value.storage());
                    text && !(arg_splitlines && text->find('\n') != ::cw::lib::String::npos)) {
                    ::cw::lib::String quoted = "\"";
                    for (const char ch : *text) { quoted += ch; if (ch == '"') quoted += '"'; }
                    values.push_back(quoted + "\"");
                } else values.push_back(value_to_str(value, arg_splitlines));
            }
            return "LIST(" + [&]() { ::cw::lib::String s; for (std::size_t i = 0; i < values.size(); ++i) { if (i) s += ", "; s += values[i]; } return s; }() + ")";
        }
        if (const auto structure = std::dynamic_pointer_cast<StructVal>(*object)) {
            const auto* name = std::get_if<::cw::lib::String>(&structure->m_name.storage());
            ::cw::lib::String result = name ? *name : "<No Name>";
            if (name && !name->empty()) {
                result = scenario_lower(*name);
                std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
                    return static_cast<char>('a' <= c && c <= 'z' ? c - ('a' - 'A') : c);
                });
            }
            std::vector<::cw::lib::Value> members = structure->m_member_values;
            { members = ::cw::calculator::cut_optionalmembers(result, std::move(members)); }result += "(";
            for (std::size_t i = 0; i < members.size(); ++i) {
                if (i) result += ", ";
                if (const auto* text = std::get_if<::cw::lib::String>(&members[i].storage());
                    text && !(arg_splitlines && text->find('\n') != ::cw::lib::String::npos)) {
                    result += '"';
                    for (const char ch : *text) { result += ch; if (ch == '"') result += '"'; }
                    result += '"';
                } else result += value_to_str(members[i], arg_splitlines);
            }
            result += ")";
            return result;
        }
    }
    return {};
}

void Variant::value_to_element(::cw::lib::Value arg_value, std::shared_ptr<::cw::data::CWPyElement> arg_e, ::cw::lib::String arg_typeattr)
{
    if (!arg_e) return;
    arg_e->set(arg_typeattr, value_to_type(arg_value));
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&arg_value.storage())) {
        if (const auto sequence = std::dynamic_pointer_cast<VariantSequence>(*object)) {
            for (const auto& value : sequence->m_values) {
                auto child = make_element("Value");
                value_to_element(value, child, "type");
                arg_e->append(std::move(child));
            }
            return;
        }
        if (const auto structure = std::dynamic_pointer_cast<StructVal>(*object)) {
            const auto* name = std::get_if<::cw::lib::String>(&structure->m_name.storage());
            const auto struct_name = name ? scenario_lower(*name) : ::cw::lib::String{};
            arg_e->append(make_element("StructureName", struct_name.empty() ? "" : [&]() { auto n = struct_name; std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); }); return n; }()));
            std::vector<::cw::lib::Value> members = structure->m_member_values;
            const auto member_names = ::cw::calculator::structure_member_names(struct_name);
            { members = ::cw::calculator::cut_optionalmembers(struct_name, std::move(members)); }for (std::size_t i = 0; i < members.size(); ++i) {
                std::map<::cw::lib::String, ::cw::lib::String> attrs;
                if (i < member_names.size()) {
                    auto member_name = member_names[i];
                    std::transform(member_name.begin(), member_name.end(), member_name.begin(), [](unsigned char c) {
                        return static_cast<char>('a' <= c && c <= 'z' ? c - ('a' - 'A') : c);
                    });
                    attrs["name"] = std::move(member_name);
                }
                auto child = make_element("Member", "", attrs);
                value_to_element(members[i], child, "type");
                arg_e->append(std::move(child));
            }
            return;
        }
    }
    arg_e->set("value", value_to_str(arg_value, false));
}

::cw::lib::String Variant::string_value()
{
    return value_to_str(m_value, false);
}

void Variant::write_value()
{
    if (!m_is_writable) return;
    if (std::find(m_initialization.begin(), m_initialization.end(), "EventExit") != m_initialization.end()) return;
    // Python版と同様、書き込み先の状態の失敗を黙った成功に変えない。
    const auto* parent_object = std::get_if<::cw::lib::ObjectPtr>(&m__parent.storage());
    if (m__parent.is_none() || (parent_object && !*parent_object))
        throw std::logic_error("Variant writable value requires a parent");
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&m__data.storage());
    if (!object || !*object) throw std::logic_error("Variant writable value requires XML data");
    auto data = *object;
    auto element = std::dynamic_pointer_cast<::cw::data::CWPyElement>(data);
    if (!element) throw std::logic_error("Variant writable value requires an XML element");
    _write_value(element, m_value);
    mark_variable_parent_edited(m__parent);
}

void Variant::_write_value(std::shared_ptr<::cw::data::CWPyElement> arg_e, ::cw::lib::Value arg_val)
{
    if (!arg_e) return;
    // Python版はName要素だけを残して子要素を全削除する。属性は保持する。
    std::shared_ptr<::cw::data::CWPyElement> name;
    for (const auto& child : arg_e->m_children) if (child && child->m_tag == "Name") { name = child; break; }
    for (const auto& child : arg_e->m_children) if (child) { child->m_cwxparent.reset(); child->m_parent.reset(); }
    arg_e->m_children.clear();
    if (name) arg_e->append(name);
    value_to_element(std::move(arg_val), std::move(arg_e), "type");
}

void YadoData::initialize(::cw::lib::String arg_yadodir, ::cw::lib::String arg_tempdir, bool arg_loadparty)
{
    m_yadodir = std::move(arg_yadodir);
    m_tempdir = std::move(arg_tempdir);
    m__loading = true;
    m__changed = false;
    m_lastparty.clear();
    m_money = 0;
    m_name.clear();
    m_skindirname.clear();
    m_party.reset();
    m_losted_sdata.reset();
    m_standbys.clear(); m_album.clear(); m_storehouse.clear(); m_partys.clear(); m_partyrecord.clear();
    m_bookmarks.clear(); m_imgpaths.clear(); m_saved_variables.clear(); m_savedjpdcimage.clear();
    m_recenthistory.reset();
    m_deletedpaths = std::make_shared<::cw::data::YadoDeletedPathSet>();
    m_deletedpaths->initialize(m_yadodir, m_tempdir);
    if (::cw::g_fsync) ::cw::g_fsync->sync();
    // Pythonの YadoData.__init__ と同様、_retry_save()
    // の後に remove_temp() を行う。 中断した保存転送を先に確定し、残
    // った未保存のTempを捨て、 その後の読み込みがYadoだけを見るようにする。
    _retry_save();
    ::cw::util::remove_temp();
    (void)mounted_or_native_mkdir(std::filesystem::u8path(m_tempdir));
    m_environment = ::cw::data::yadoxml2etree(
        ::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(m_yadodir) / "Environment.xml"),
        "", std::nullopt);
    // Python版と同様、既存のEnvironmentを読み、不足・不正なXML
    // の失敗を伝える。 壊れた宿を読む際に代替Environmentを作っ
    // たり、 スキン・素材の失敗を握りつぶして空の宿として続けたりしない。
    if (!m_environment)
        throw std::runtime_error("inn Environment XML is missing or invalid: " +
            ::cw::util::join_paths({::cw::lib::Value(m_yadodir),
                                   ::cw::lib::Value("Environment.xml")}));
    if (m_environment) {
        auto root = m_environment->getroot();
        auto property = root ? root->find("Property") : std::nullopt;
        auto name = m_environment->find("Property/Name");
        if (name && *name) {
            // 存在する空のNameと、Nameの不在を区別する。
            m_name = (*name)->m_text;
        } else {
            m_name = ::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(m_yadodir).filename());
            m_environment->insert("Property", ::cw::data::make_element("Name", m_name), 0);
        }
        m_money = m_environment->getint("Property/Cashbox", std::nullopt, 0);
        m_skindirname = m_environment->gettext("Property/Skin",
            (::cw::g_cwpy && ::cw::g_cwpy->m_setting) ? ::cw::g_cwpy->m_setting->m_skindirname : "");
        if (property && *property) m_imgpaths = ::cw::image::get_imageinfos(*property, false);
        auto skintype = m_environment->gettext("Property/Type",
            (::cw::g_cwpy && ::cw::g_cwpy->m_setting) ? ::cw::g_cwpy->m_setting->m_skintype : "");
        const auto skin_root = std::filesystem::u8path("Data") / "Skin";
        // Python版と同様、force_skin適用前にこのパスを作り
        // 、 後でディレクトリ名が変わっても初回のバージョン検査に使う。
        const auto skinxml = skin_root / std::filesystem::u8path(m_skindirname) / "Skin.xml";
        const auto read_skin = [&](const std::filesystem::path& path) {
            const auto tree = xml2etree(path_utf8(path), "", std::nullopt, std::nullopt, true);
            if (!tree || !tree->getroot()) throw std::runtime_error("skin XML is missing or invalid: " + path_utf8(path));
            return tree;
        };
        ::cw::lib::String forced_skin;
        if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_OPTIONS.storage()); object && *object) {
            if (const auto options = std::dynamic_pointer_cast<::cw::argparser::ArgResult>(*object)) {
                forced_skin = options->getstr("force_skin");
                options->setstr("force_skin", "");
            }
        }
        if (!forced_skin.empty()) {
            if (mounted_or_native_is_file(skin_root / std::filesystem::u8path(forced_skin) / "Skin.xml")) m_skindirname = forced_skin;
            else if (::cw::g_cwpy) ::cw::g_cwpy->call_modaldlg("ERROR",
                {{"text", ::cw::lib::Value("スキン「" + forced_skin + "」が見つかりません。")}});
        }
        bool supported_skin = false;
        if (!m_skindirname.empty()) {
            if (!mounted_or_native_is_file(skinxml)) {
                if (::cw::g_cwpy) ::cw::g_cwpy->call_modaldlg("ERROR",
                    {{"text", ::cw::lib::Value("スキン「" + m_skindirname + "」が見つかりません。")}});
            } else {
                const auto tree = read_skin(skinxml);
                const auto version = tree->getroot()->get("dataVersion", "0");
                supported_skin = std::apply([&](const auto&... value) { return ((value == version) || ...); }, ::cw::g_SUPPORTED_SKIN);
                if (!supported_skin && ::cw::g_cwpy) {
                    const auto name = tree->gettext("Property/Name", m_skindirname);
                    ::cw::g_cwpy->call_modaldlg("ERROR", {{"text", ::cw::lib::Value("「" + name + "」は対応していないバージョンのスキンです。" + ::cw::g_APP_NAME + "をアップデートしてください。")}});
                }
            }
        }
        if (!supported_skin) {
            std::error_code ec;
            if (std::filesystem::is_directory(skin_root, ec)) {
                for (const auto& entry : std::filesystem::directory_iterator(skin_root, ec)) {
                    if (ec) break;
                    if (!entry.is_directory(ec)) continue;
                    const auto name = ::cw::lib::encoding::path_to_utf8(entry.path().filename());
                    const auto candidate = entry.path() / "Skin.xml";
                    if (!mounted_or_native_is_file(candidate)) continue;
                    try {
                        const auto tree = read_skin(candidate);
                        if (!skintype.empty() && tree->gettext("Property/Type", "") != skintype) continue;
                        m_skindirname = name;
                        supported_skin = true;
                        break;
                    } catch (const std::exception&) {
                        // Python版と同様、代替スキンの探索では不正な候補を飛ばす。
                    }
                }
            }
            if (!supported_skin && ::cw::g_cwpy && ::cw::g_cwpy->m_setting) {
                m_skindirname = ::cw::g_cwpy->m_setting->m_skindirname;
                skintype = ::cw::g_cwpy->m_setting->m_skintype;
            }
            set_skinname(m_skindirname, skintype);
        }
        // Environment.xmlのBookmarkは、UIを生成しない段階でも型付き状態へ復元する。
        const auto bookmarks = m_environment->getroot()->find("Bookmarks");
        if (bookmarks && *bookmarks) {
            for (const auto& bookmark : (*bookmarks)->m_children) {
                if (!bookmark || bookmark->m_tag != "Bookmark") continue;
                std::vector<::cw::lib::String> spaths;
                for (const auto& child : bookmark->m_children) {
                    if (child && child->m_tag == "Path") spaths.push_back(child->m_text);
                }
                auto bookmark_path = bookmark->get("path", "");
                if (bookmark_path.empty() && !spaths.empty() && ::cw::g_cwpy && ::cw::g_cwpy->m_setting) {
                    bookmark_path = ::cw::data::find_scefullpath(::cw::g_cwpy->m_setting->get_scedir(std::nullopt), spaths);
                    if (!bookmark_path.empty()) { bookmark->set("path", bookmark_path); m_environment->m_is_edited = true; }
                }
                m_bookmarks.emplace_back(std::move(spaths), std::move(bookmark_path));
            }
        }
    } else {
        m_name = ::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(m_yadodir).filename());
        // Skin/resource 自体が壊れて生成にも失敗した場合だけ空状態を保持する。
        // 通常の欠落ファイルは上の xmlcreater.create_environment で復元済み。
    }
    if (m_environment && m_environment->getint(".", ::cw::lib::Value("dataVersion"), 0) < 1) {
        update_version();
        m_environment->getroot()->set("dataVersion", "1");
        m_environment->m_is_edited = true;
        m_environment->write_xml(true);
    }
    // Yado.dbを正規経路にする。Python同様、更新失敗は宿読込の失敗。
    // update()はカード／冒険者／パーティ／PartyRecord／SavedJPDCImageを
    // ファイル時刻と照合してSQLiteへ同期するため、起動直後のキャッシュ欠落も復元できる。
    try {
        m_yadodb = std::make_shared<::cw::yadodb::YadoDB>();
        m_yadodb->initialize(m_yadodir, ::cw::yadodb::g_YADO);
        m_yadodb->update(true, true, true, std::nullopt, std::nullopt, std::nullopt, true, true);
        m_partys = m_yadodb->get_parties();
        m_standbys = m_yadodb->get_standbys();
        m_album = m_yadodb->get_album();
        m_storehouse = m_yadodb->get_cards();
        m_partyrecord = m_yadodb->get_partyrecord();
        m_savedjpdcimage = m_yadodb->get_savedjpdcimage();
    } catch (const std::exception&) {
        // ここで代替へ切り替えるとStorageの入出力失敗まで隠し、 一部だけ置換
        // したパーティ・カード一覧を読み込み成功として公開する場合がある。 Python
        // 版と同様、YadoDB.update/get_*の失敗は抑止しない。
        m_yadodb.reset();
        throw;
    }
    // Python版と同様、先にYadoDBを更新し、返されたパーティ
    // だけのTimekeeperを復元する。 別経路でXMLを先に走査
    // し、データベースの不正ファイルの省略・エラー方針を迂回しない。
    const auto normalize_yado_path = [](const ::cw::lib::String& value) {
        std::error_code path_ec;
        auto path = std::filesystem::absolute(
            ::cw::lib::encoding::path_from_utf8(value), path_ec);
        if (path_ec) path = ::cw::lib::encoding::path_from_utf8(value);
        return scenario_filename_normcase(
            ::cw::lib::encoding::path_to_utf8(path.lexically_normal()));
    };
    std::set<::cw::lib::String> partypaths;
    for (const auto& header : m_partys) if (header) {
        const auto debugdir = std::filesystem::u8path(scenario_value_string(header->m_fpath)).parent_path() / "Debug";
        const auto previous = debugdir / "OldTimekeeper.xml";
        if (::cw::g_fsync && ::cw::g_fsync->is_waiting(path_utf8(previous))) ::cw::g_fsync->sync();
        if (mounted_or_native_is_file(previous)) {
            auto destination = debugdir / "Timekeeper.xml";
            // shutil.move と同様、既存ディレクトリが保存
            // 先ならその中へ移し、 中の同名項目の上書きは拒否する。
            if (mounted_or_native_is_directory(destination)) {
                destination /= previous.filename();
                if (const auto status = mounted_or_native_stat(destination); status && status->exists())
                    throw std::runtime_error("Timekeeper restore: destination already exists");
            }
            // FileSyncのネイティブ置換は共通のOS改名処理
            // を使い、論理保存領域は同等の移動を提供する。 上のパ
            // ス選択と上書き・エラー方針は全バックエンドで共通。
            if (::cardwirth::platform::is_mounted_user_path(previous)) {
                if (!::cardwirth::platform::move_mounted_user_path(previous, destination, true))
                    throw std::runtime_error("Timekeeper restore: logical move failed");
            } else {
                std::error_code error;
                ::cw::util::FileSync::replace_native_file(previous, destination, error);
                if (error) throw std::filesystem::filesystem_error("Timekeeper restore", previous, destination, error);
            }
        }
        for (const auto& member : header->get_memberpaths(m_yadodir))
            partypaths.insert(normalize_yado_path(member));
    }
    sort_parties();
    m_standbys.erase(std::remove_if(m_standbys.begin(), m_standbys.end(),
        [&](const auto& header) {
            // nullはネイティブの値の境界だけの問題。 共通DBは、待機中の
            // 冒険者を返す前にアルバム・消去済み・未存在ファイルを除外済み。
            if (!header) return true;
            const auto path = scenario_value_string(header->m_fpath);
            return partypaths.contains(normalize_yado_path(path));
        }), m_standbys.end());
    sort_standbys();
    std::stable_sort(m_album.begin(), m_album.end(), [](const auto& lhs, const auto& rhs) { return lhs && rhs ? lhs->m_name < rhs->m_name : static_cast<bool>(rhs); });
    sort_storehouse(std::nullopt);
    sort_partyrecord();
    // Python版と同様、保存変数の復元・現在パーティの選択より前
    // に初期化用DBを閉じる。 閉じたオブジェクト自体は保持する。
    m_yadodb->close();
    m_saved_variables = get_savedvariables(m_environment);
    ::cw::lib::String optparty;
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_OPTIONS.storage()); object && *object) {
        if (const auto options = std::dynamic_pointer_cast<::cw::argparser::ArgResult>(*object)) {
            optparty = options->getstr("party");
            options->setstr("party", "");
        }
    }
    const auto selected = m_environment ? m_environment->gettext("Property/NowSelectingParty", "") : ::cw::lib::String{};
    const bool autoload = !m_environment ||
        m_environment->getbool("Property/NowSelectingParty", ::cw::lib::Value("autoload"), true);
    const bool loadparty = arg_loadparty && autoload;
    if (loadparty || !optparty.empty()) {
        auto party_name = selected;
        if (!optparty.empty()) {
            const auto party_dir = std::filesystem::u8path(m_yadodir) / "Party" /
                ::cw::lib::encoding::path_from_utf8(optparty);
            const auto party_dir_status = mounted_or_native_stat(party_dir);
            if (party_dir_status && party_dir_status->is_directory()) {
                auto party_file = party_dir / "Party.xml";
                const auto party_file_status = mounted_or_native_stat(party_file);
                if (!party_file_status || !party_file_status->is_file()) {
                    for (const auto& entry : mounted_or_native_list(party_dir)) {
                        const auto entry_status = mounted_or_native_stat(entry);
                        if (entry_status && entry_status->is_file() &&
                            scenario_lower(::cw::lib::encoding::path_to_utf8(entry.extension())) == ".xml") {
                            party_file = entry;
                            break;
                        }
                    }
                }
                const auto resolved_status = mounted_or_native_stat(party_file);
                if (resolved_status && resolved_status->is_file()) {
                    if (::cardwirth::platform::is_mounted_user_path(party_file))
                        party_name = path_utf8(party_file.lexically_relative(
                            std::filesystem::u8path(m_yadodir)));
                    else {
                        std::error_code party_ec;
                        const auto relative = std::filesystem::relative(
                            party_file, std::filesystem::u8path(m_yadodir), party_ec);
                        if (!party_ec) party_name = path_utf8(relative);
                    }
                }
            }
        }
        if (party_name.empty()) {
            m__loading = false;
            return;
        }
        const auto path = ::cw::lib::encoding::path_from_utf8(m_yadodir) /
            ::cw::lib::encoding::path_from_utf8(party_name);
        const auto selected_normalized = path.lexically_normal();
        const auto selected_relative = ::cw::lib::encoding::path_from_utf8(party_name).lexically_normal();
        const auto selected_absolute = std::filesystem::absolute(selected_normalized);
        const auto yado_root = ::cw::lib::encoding::path_from_utf8(m_yadodir).lexically_normal();
        auto matches_selected = [&](const auto& header) {
            if (!header) return false;
            const auto stored = scenario_value_string(header->m_fpath);
            if (stored.empty()) return false;
            const auto stored_path = ::cw::lib::encoding::path_from_utf8(stored).lexically_normal();
            if (stored_path == selected_normalized) return true;
            std::error_code path_ec;
            if (std::filesystem::absolute(stored_path, path_ec) == selected_absolute && !path_ec) return true;
            path_ec.clear();
            const auto stored_relative = std::filesystem::relative(stored_path, yado_root, path_ec);
            return !path_ec && stored_relative.lexically_normal() == selected_relative;
        };
        auto it = std::find_if(m_partys.begin(), m_partys.end(), matches_selected);
        if (it != m_partys.end()) load_party(*it);
    }
    m__loading = false;
}

void YadoData::update_version()
{
    if (!m_environment) return;
    if (::cw::g_fsync) ::cw::g_fsync->sync();
    const auto party_root = std::filesystem::u8path(m_yadodir) / "Party";
    const auto selected = m_environment->gettext("Property/NowSelectingParty", "");
    std::error_code ec;
    // Pythonのos.listdirと同様、Partyパスの未存在
    // ・ディレクトリ以外を、 更新済みにできる空の宿として扱わない。
    if (!mounted_or_native_is_directory(party_root))
        throw std::runtime_error("party directory cannot be enumerated: " + path_utf8(party_root));
    for (const auto& oldpath : mounted_or_native_list(party_root)) {
        if (!mounted_or_native_is_file(oldpath) || scenario_lower(oldpath.extension().generic_string()) != ".xml") continue;
        auto stempath = oldpath; stempath.replace_extension("");
        const auto dpath = std::filesystem::u8path(::cw::binary::util::check_duplicate(path_utf8(stempath)));
        // Pythonのos.makedirsと同様、NowSelectingParty
        // の変更・変換後のパーティDBのオープンより先に失敗させる。
        if (!mounted_or_native_mkdir(dpath))
            throw std::runtime_error("converted party directory cannot be created: " + path_utf8(dpath));
        if (selected == path_utf8(oldpath.stem()) && m_environment) {
            const auto relative = path_utf8(std::filesystem::u8path("Party") / dpath.filename() / "Party.xml");
            m_environment->edit("Property/NowSelectingParty", relative, std::nullopt);
        }
        ::cw::yadodb::YadoDB carddb;
        carddb.initialize(path_utf8(dpath), ::cw::yadodb::g_PARTY);
        ::cw::lib::Integer order = 0;
        const auto oldwsl = oldpath.parent_path() / (path_utf8(oldpath.stem()) + ".wsl");
        const bool haswsl = mounted_or_native_is_file(oldwsl);
        if (haswsl) {
            const auto global_tempdir = scenario_value_string(::cw::g_tempdir);
            const auto logroot = !global_tempdir.empty() ? global_tempdir : m_tempdir;
            const auto unpacked = ::cw::util::decompress_zip(path_utf8(oldwsl), logroot, "ScenarioLog", std::nullopt, std::nullopt, false, std::nullopt);
            // Python版と同様、次に展開済みのParty XMLを
            // 読み、アーカイブを開けなければ失敗する。 ログ変換を省略
            // したのに成功扱いで、唯一の元の.wslを後で削除しない。
            if (unpacked.empty())
                throw std::runtime_error("scenario log extraction failed: " + path_utf8(oldwsl));
            if (!unpacked.empty()) {
                auto files = make_element("BackpackFiles");
                const auto log_party_path = path_utf8(std::filesystem::u8path(unpacked) / "Party" / oldpath.filename());
                auto party = xml2etree(log_party_path, "", std::nullopt, std::nullopt, false);
                if (!party || !party->getroot()) throw std::runtime_error("scenario log Party XML is missing or invalid: " + log_party_path);
                {
                    // Python版と同様、getfind("Backpack") は raiseerror=True で走査する。
                    (void)party->getfind("Backpack", true);
                    const auto log_backpack = party->getroot()->find("Backpack");
                    if (log_backpack && *log_backpack) for (const auto& card : (*log_backpack)->m_children) if (card) {
                        auto cardtree = std::make_shared<CWPyElementTree>(card, "");
                        cardtree->edit("Property", "2", std::optional<::cw::lib::String>("moved"));
                        const auto name = cardtree->gettext("Property/Name", "");
                        auto cardpath = join_path(join_path(path_utf8(dpath), card->m_tag.empty() ? ::cw::lib::String("ItemCard") : card->m_tag),
                                                  ::cw::binary::util::check_filename(name + ".xml"));
                        cardpath = ::cw::binary::util::check_duplicate(cardpath);
                        card->m_fpath = cardpath;
                        cardtree->m_fpath = cardpath;
                        cardtree->write_file(cardpath);
                        auto header = std::make_shared<::cw::header::CardHeader>();
                        header->initialize(std::nullopt, std::nullopt, card, false, {}, false, std::nullopt, std::nullopt, {}, {});
                        header->m_fpath = cardpath;
                        carddb.insert_cardheader(header, false, order++);
                        files->append(make_element("File", ::cw::util::relpath(cardpath, path_utf8(dpath))));
                    }
                    auto backpack_log = std::make_shared<CWPyElementTree>(files, path_utf8(std::filesystem::u8path(unpacked) / "Backpack.xml"));
                    backpack_log->write_file(backpack_log->m_fpath);
                    if (log_backpack && *log_backpack) party->getroot()->remove(*log_backpack);
                    party->write_file(party->m_fpath);
                    const auto dest_party = std::filesystem::u8path(unpacked) / "Party" / "Party.xml";
                    if (std::filesystem::u8path(party->m_fpath) != dest_party) {
                        const auto source_party = std::filesystem::u8path(party->m_fpath);
                        // Pythonのshutil.moveと同様、失敗を
                        // 伝える。 移動失敗後に展開ログを圧縮・削除しない。
                        if (::cardwirth::platform::is_mounted_user_path(source_party)) {
                            if (!::cardwirth::platform::rename_mounted_user_path(source_party, dest_party, false))
                                throw std::runtime_error("scenario log party move failed: " + path_utf8(source_party));
                        } else {
                            std::filesystem::rename(source_party, dest_party);
                        }
                    }
                }
                ::cw::util::compress_zip(unpacked, path_utf8(dpath / "Party.wsl"), true);
                ::cw::util::remove(unpacked, false);
            }
        }
        auto data = ::cw::data::xml2etree(path_utf8(oldpath), "", std::nullopt, std::nullopt, false);
        if (!data || !data->getroot()) continue;
        (void)data->getfind("Backpack", true);
        const auto backpack = data->getroot()->find("Backpack");
        if (backpack && *backpack) for (const auto& card : (*backpack)->m_children) if (card) {
            auto cardtree = std::make_shared<CWPyElementTree>(card, "");
            const auto name = cardtree->gettext("Property/Name", "");
            auto cardpath = join_path(join_path(path_utf8(dpath), card->m_tag.empty() ? ::cw::lib::String("ItemCard") : card->m_tag),
                                      ::cw::binary::util::check_filename(name + ".xml"));
            cardpath = ::cw::binary::util::check_duplicate(cardpath);
            card->m_fpath = cardpath;
            cardtree->m_fpath = cardpath;
            auto header = std::make_shared<::cw::header::CardHeader>();
            header->initialize(std::nullopt, std::nullopt, card, false, {}, false, std::nullopt, std::nullopt, {}, {});
            if (haswsl && !cardtree->getbool(".", ::cw::lib::Value("scenariocard"), false)) {
                cardtree->edit(".", "True", std::optional<::cw::lib::String>("scenariocard"));
                header->m_scenariocard = true;
                for (const auto& e2 : cardtree->iter()) {
                    if (!e2 || e2->m_tag != "ImagePath") continue;
                    const auto e2text = ::cw::util::validate_filepath(e2->m_text);
                    if (e2text.empty() || ::cw::binary::image::path_is_code(e2text)) continue;
                    const auto image_path = join_path(m_yadodir, e2text);
                    const auto image_native = std::filesystem::u8path(image_path);
                    if (!mounted_or_native_is_file(image_native)) continue;
                    ::cw::lib::Bytes bytes;
                    if (::cardwirth::platform::is_mounted_user_path(image_native)) {
                        const auto source = ::cardwirth::platform::read_mounted_user_file(image_native);
                        // Python版と同様、isfile成功後のオープン・読み取
                        // り失敗は外へ伝え、 一部だけ変換したカードを保存しない。
                        if (!source) throw std::runtime_error("image file cannot be read: " + image_path);
                        bytes.reserve(source->size());
                        for (const auto value : *source)
                            bytes.push_back(std::to_integer<std::uint8_t>(value));
                    } else {
                        std::ifstream input(image_native, std::ios::binary);
                        if (!input) throw std::runtime_error("image file cannot be opened: " + image_path);
                        bytes = ::cw::lib::read_stream_bytes(input);
                    }
                    e2->m_text = ::cw::binary::image::data_to_code(std::move(bytes));
                }
                const auto infos = ::cw::image::get_imageinfos(cardtree->find_exists("Property"), false);
                auto sequence = std::make_shared<VariantSequence>();
                for (const auto& info : infos) if (info)
                    sequence->m_values.emplace_back(::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(info)));
                header->m_imgpaths = ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(sequence));
            }
            cardtree->write_file(cardpath);
            header->m_fpath = cardpath;
            carddb.insert_cardheader(header, false, order++);
        }
        carddb.commit(); carddb.close();
        if (backpack && *backpack) data->getroot()->remove(*backpack);
        data->write_file(path_utf8(dpath / "Party.xml"));
        if (haswsl) ::cw::util::remove(path_utf8(oldwsl), false);
        ::cw::util::remove(path_utf8(oldpath), false);
    }
    m_environment->getroot()->set("dataVersion", "1"); m_environment->m_is_edited = true; changed();
}

void YadoData::changed()
{
    if (!m__loading) m__changed = true;
}

bool YadoData::is_changed()
{
    return m__changed;
}

bool YadoData::is_empty()
{
    // Python版の短絡評価の順序と、集合だけの空判定を保つ
    // 。 現在のパーティを、元APIにない追加条件にしない。
    return m_partys.empty() && m_standbys.empty() && m_storehouse.empty() &&
           m_album.empty() && m_partyrecord.empty() && m_savedjpdcimage.empty() &&
           get_gossips().empty() && get_compstamps().empty() && m_saved_variables.empty();
}

bool YadoData::is_loading()
{
    return m__loading;
}

// Python版はPropertyの存在を前提とする。 ネイティブの部分的なEnvironmentでは、Skin/Type
// を保存できるようPropertyを補うが、 Environment自体がなければ触れない。
void YadoData::set_skinname(::cw::lib::String arg_skindirname, ::cw::lib::String arg_skintype)
{
    m_skindirname = std::move(arg_skindirname);
    if (m_environment) {
        auto root = m_environment->getroot();
        auto property = root ? root->find("Property") : std::nullopt;
        if ((!property || !*property) && root) { root->append(::cw::data::make_element("Property")); property = root->find("Property"); }
        if (property && *property) {
            auto skin = (*property)->find("Skin"); if (skin && *skin) (*skin)->set_text(m_skindirname); else (*property)->append(::cw::data::make_element("Skin", m_skindirname));
            auto type = (*property)->find("Type"); if (type && *type) (*type)->set_text(arg_skintype); else (*property)->append(::cw::data::make_element("Type", arg_skintype));
            m_environment->m_is_edited = true;
        }
    }
}

::cw::lib::String YadoData::get_showingname()
{
    return m_name;
}

void YadoData::load_party(std::optional<std::shared_ptr<::cw::header::PartyHeader>> arg_header)
{
    load_party_value(arg_header && *arg_header
        ? ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(*arg_header)) : ::cw::lib::Value{});
}

void YadoData::load_party_value(::cw::lib::Value arg_header)
{
    if (m_party) {
        m_party.value()->write();
        if (!m_party.value()->m_members.empty()) add_party(m_party.value(), true);
        m_lastparty = m_party.value()->m_members.empty() ? ::cw::lib::String{} : scenario_value_string(m_party.value()->m_path);
    }
    if (!arg_header.as_bool()) {
        m_party.reset();
        if (m_environment) { m_environment->edit("Property/NowSelectingParty", "", std::nullopt); m_environment->m_is_edited = true; }
        return;
    }
    const auto* header_object = std::get_if<::cw::lib::ObjectPtr>(&arg_header.storage());
    const auto header = header_object
        ? std::dynamic_pointer_cast<::cw::header::PartyHeader>(*header_object) : nullptr;
    // Pythonの Party(header) の header.fpath
    // 参照に対応する型付き境界。 以前のパーティの書き込み・追加・
    // lastparty操作の後で検証し、 単一値を渡した場合に黙
    // ってパーティを解除したり、失敗時の変更順序を変えたりしない。
    if (!header) throw std::invalid_argument("load_party header has no fpath");
    // Python版と同様、常にParty(header)を構築する。 Party::_init
    // はXMLを読み直し、header.dataの未保存の荷物袋だけを再利用する。 header.data
    // を直接代入して、戻ったときに古いメンバー・読み込み状態を残さない。
    auto party = std::make_shared<::cw::data::Party>();
    party->initialize(header, true);
    m_party = party;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting &&
        (!party->m_lastscenario.empty() || !scenario_value_string(party->m_lastscenariopath).empty())) {
        // Python版は Party.lastscenario を設定から別参照す
        // る。 ネイティブ設定はStringのベクター、PartyはValue項目を
        // 持つため、 文字列を複製して型付きの所有関係を保ち、可変一覧を共有しない。
        ::cw::g_cwpy->m_setting->m_lastscenario.clear();
        for (const auto& scenario : m_party.value()->m_lastscenario)
            if (const auto* text = std::get_if<::cw::lib::String>(&scenario.storage())) ::cw::g_cwpy->m_setting->m_lastscenario.push_back(*text);
        ::cw::g_cwpy->m_setting->m_lastscenariopath = scenario_value_string(m_party.value()->m_lastscenariopath);
    }
    const auto relative = ::cw::util::get_yado_relpath(
        scenario_value_string(header->m_fpath), m_yadodir, m_tempdir);
    if (m_environment) { m_environment->edit("Property/NowSelectingParty", relative, std::nullopt); m_environment->m_is_edited = true; }
    auto it = std::find(m_partys.begin(), m_partys.end(), header);
    if (it != m_partys.end()) m_partys.erase(it);
}

std::vector<::cw::lib::String> YadoData::prepare_party_removal(const std::shared_ptr<Party>& party) const
{
    namespace fs = std::filesystem;
    const auto fail = []() -> void {
        throw std::runtime_error("解散するパーティの保存先を安全に確認できません。");
    };
    if (!party || !m_party || *m_party != party || !m_deletedpaths ||
        m_yadodir.empty() || m_tempdir.empty()) fail();
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&party->m_data.storage());
    const auto tree = object ? std::dynamic_pointer_cast<CWPyElementTree>(*object) : nullptr;
    if (!tree || !tree->getroot() || tree->getroot()->get_tag() != "Party" || tree->m_fpath.empty()) fail();
    const auto text = [](const fs::path& path) { return ::cw::lib::encoding::path_to_utf8(path); };
    const auto absolute = [](const fs::path& path) { return fs::absolute(path).lexically_normal(); };
    const auto same_path = [&](const fs::path& a, const fs::path& b) {
        return ::cw::util::path_starts_with(text(a), text(b)) &&
               ::cw::util::path_starts_with(text(b), text(a));
    };
    const auto file = absolute(fs::u8path(tree->m_fpath));
    fs::path relative;
    for (const auto& base : {m_tempdir, m_yadodir}) {
        const auto root = absolute(fs::u8path(base) / "Party");
        if (!same_path(file, root) && ::cw::util::path_starts_with(text(file), text(root))) {
            relative = fs::u8path(text(file).substr(text(root).size() + 1));
            break;
        }
    }
    const auto depth = std::distance(relative.begin(), relative.end());
    if (relative.empty() || relative.is_absolute() || (depth != 1 && depth != 2) ||
        scenario_lower(text(relative.extension())) != ".xml") fail();
    for (const auto& part : relative)
        if (part == "." || part == ".." || part.empty()) fail();
    // 新形式の Party/<name>/<file>.xml は専用ディレクトリ
    // を所有するが、 旧Party/<name>.xmlが所有するのはそのXMLと
    // 同名のシナリオログだけ。 共有のParty/ディレクトリを削除予定にしない。
    std::vector<fs::path> targets{depth == 2 ? relative.parent_path() : relative};
    if (depth == 1) { auto log = relative; log.replace_extension(".wsl"); targets.push_back(log); }
    for (const auto& base : {m_yadodir, m_tempdir}) {
        const auto root = absolute(fs::u8path(base) / "Party");
        const auto canonical_root = fs::weakly_canonical(root);
        if (!same_path(canonical_root, fs::weakly_canonical(absolute(fs::u8path(base))) / "Party")) fail();
        for (const auto& part : targets) {
            const auto target = root / part;
            std::error_code ec;
            const auto status = fs::symlink_status(target, ec);
            if (ec && ec != std::errc::no_such_file_or_directory) fail();
            if (fs::is_symlink(status) ||
                !same_path(fs::weakly_canonical(target), canonical_root / part)) fail();
            if (fs::exists(status) && (depth == 2 ? !fs::is_directory(status) : !fs::is_regular_file(status))) fail();
        }
        // パーティXMLのシンボリックリンク・ジャンクションで、別パーティのフォルダーを選ばない。
        const auto candidate = root / relative;
        std::error_code ec;
        const auto status = fs::symlink_status(candidate, ec);
        if (ec && ec != std::errc::no_such_file_or_directory) fail();
        if (fs::is_symlink(status) || !same_path(fs::weakly_canonical(candidate), canonical_root / relative) ||
            (fs::exists(status) && !fs::is_regular_file(status))) fail();
    }
    std::vector<::cw::lib::String> result;
    for (const auto& target : targets)
        result.push_back(text((fs::u8path(m_yadodir) / "Party" / target).lexically_normal()));
    return result;
}

// ネイティブの候補生成はヘッダーなしを返せるため、nullの待機PC
// は追加しない。 Python版は生成側がヘッダーを返す前提。
// 変更通知は全体の実行環境の宿を要求せず、この宿を対象にする。
std::shared_ptr<::cw::header::AdventurerHeader> YadoData::add_standbys(::cw::lib::String arg_path, bool arg_sort)
{
    changed();
    auto header = create_advheader(arg_path, false, std::nullopt);
    if (!header) return {};
    // Python版には無い確認（意図的な追加）: 同じ冒険者ファイルを待機者へ
    // 二重に登録しない。二重登録が残ると、同じPCを別々のパーティへ入れられ、
    // 1人のPCが2つのパーティに所属してしまう（以前 restore_party で発生）。
    // Yado側とTemp側のどちらのパスでも同じ冒険者として比べる。
    const auto standby_key = [this](const ::cw::lib::String& path) {
        if (path.empty()) return ::cw::lib::String{};
        ::cw::lib::String key = path;
        if (!m_yadodir.empty() && !m_tempdir.empty())
            if (const auto paths = ::cw::util::resolve_yado_paths(path, m_yadodir, m_tempdir))
                key = paths->first;
        std::error_code ec;
        auto native = std::filesystem::absolute(::cw::lib::encoding::path_from_utf8(key), ec);
        if (ec) native = ::cw::lib::encoding::path_from_utf8(key);
        return scenario_lower(::cw::lib::encoding::path_to_utf8(native.lexically_normal()));
    };
    const auto key = standby_key(scenario_value_string(header->m_fpath));
    const auto existing = key.empty() ? m_standbys.end() :
        std::find_if(m_standbys.begin(), m_standbys.end(), [&](const auto& standby) {
            return standby && standby_key(scenario_value_string(standby->m_fpath)) == key;
        });
    if (existing != m_standbys.end()) {
        // 既存の並び順を保ったまま、最新のXMLから作ったヘッダに差し替える。
        header->m_order = (*existing)->m_order;
        *existing = header;
        if (arg_sort) sort_standbys();
        return header;
    }
    auto sequence = std::make_shared<VariantSequence>();
    for (const auto& standby : m_standbys) if (standby)
        sequence->m_values.emplace_back(::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(standby)));
    // Python版と同様、既存の全ヘッダーの保存Orderをずらして先頭に追加する。
    header->m_order = ::cw::util::new_order(
        ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(sequence)), 1);
    m_standbys.push_back(header);
    if (arg_sort) sort_standbys();
    return header;
}

// ネイティブの未解決ヘッダーはアルバム項目を作らず、Python版はヘ
// ッダーを必須とする。 アルバムには保存Orderがないため、追加後
// に論理名順を保つ。 実行環境の宿がなくても、この宿へ直接通知する。
std::shared_ptr<::cw::header::AdventurerHeader> YadoData::add_album(::cw::lib::String arg_path)
{
    changed();
    auto header = create_advheader(arg_path, true, std::nullopt);
    if (!header) return {};
    m_album.push_back(header);
    std::stable_sort(m_album.begin(), m_album.end(), [](const auto& lhs, const auto& rhs) {
        if (!lhs || !rhs) return static_cast<bool>(rhs);
        return ::cw::util::logical_compare(lhs->m_name, rhs->m_name) < 0;
    });
    return header;
}

    std::shared_ptr<::cw::header::PartyHeader> YadoData::add_party(std::shared_ptr<::cw::data::Party> arg_party, bool arg_sort)
{
    if (!arg_party) return {};
    auto header = create_partyheader(scenario_value_string(arg_party->m_path), std::nullopt);
    // Python版と同様、外へ渡すPartyキャッシュの反映前にXML情報
    // を読み直す。 失敗時は、宿の一覧・順序・現在の選択をそのままにする。
    if (!header) throw std::invalid_argument("outgoing Party XML cannot be loaded");
    header->m_data = arg_party;
    // create_partyheader はXMLからメンバーのファイル名を読み込み済み。
    // Python版と同様、その文字列を残し、Partyはheader.dataだけに格納する
    // 。 PartyHeader.membersへXMLツリーを入れると、後の選択が壊れる。
    header->m_fpath = arg_party->m_path;
    m_partys.push_back(header);
    auto sequence = std::make_shared<VariantSequence>();
    for (const auto& party_header : m_partys) if (party_header)
        sequence->m_values.emplace_back(::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(party_header)));
    // Python版と同様、このヘッダーも含め、new_orderの既定の先頭追加モードを使う。
    header->m_order = ::cw::util::new_order(::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(sequence)), 1);
    if (arg_sort) sort_parties(); return header;
}

// ネイティブの任意記録は、不在なら何もしない。Python版は記録
// を前提とする。 追加は重複検索なしで末尾へ入れる。 書き込み前と
// 追加・整列後にこの宿を変更済みにするが、永続保存の確定ではない。
std::shared_ptr<::cw::header::PartyRecordHeader> YadoData::add_partyrecord(std::shared_ptr<::cw::thread::StoredParty> arg_partyrecord)
{
    if (!arg_partyrecord) return {};
    // Python版と同様、書き込み・ヘッダーの失敗が外へ出る前に宿を変更済みにする。
    changed();
    arg_partyrecord->m_fpath = ::cw::xmlcreater::create_partyrecord(arg_partyrecord, m_yadodir, m_tempdir);
    auto header = std::make_shared<::cw::header::PartyRecordHeader>(); header->initialize(std::nullopt, std::nullopt, arg_partyrecord); m_partyrecord.push_back(header); sort_partyrecord(); changed(); return header;
}

// ネイティブは入力なしなら記録を加えず、Python版は入力を必須とす
// る。 パスは比較せず、最初の同名項目を置き換え、なければ追加する。
std::shared_ptr<::cw::header::PartyRecordHeader> YadoData::replace_partyrecord(std::shared_ptr<::cw::thread::StoredParty> arg_partyrecord)
{
    if (!arg_partyrecord) return {};
    changed();
    for (std::size_t i = 0; i < m_partyrecord.size(); ++i) if (m_partyrecord[i] && m_partyrecord[i]->m_name == scenario_value_string(arg_partyrecord->m_name))
        return set_partyrecord(static_cast<::cw::lib::Integer>(i), std::move(arg_partyrecord));
    return add_partyrecord(std::move(arg_partyrecord));
}

// 型付きのネイティブ入力がなければ何も加えず、Python版はStoredPartyを必須とする
// 。 有効な記録では、Python版の変更済み設定・削除・書き込み・ヘッダー変更の順序を保つ。
std::shared_ptr<::cw::header::PartyRecordHeader> YadoData::set_partyrecord(::cw::lib::Integer arg_index, std::shared_ptr<::cw::thread::StoredParty> arg_partyrecord)
{
    if (!arg_partyrecord) return {};
    changed();
    const auto size = static_cast<::cw::lib::Integer>(m_partyrecord.size());
    if (arg_index < -size || arg_index >= size)
        throw std::out_of_range("party record index");
    const auto index = static_cast<std::size_t>(arg_index < 0 ? size + arg_index : arg_index);
    if (!m_partyrecord[index])
        throw std::out_of_range("party record no longer exists");
    const auto old = scenario_value_string(m_partyrecord[index]->m_fpath);
    if (!m_deletedpaths) throw std::logic_error("party record deletion set is required");
    // Python版と同様、書き込み前に削除を予定する。 後の失敗でも
    // その変更は残り、メモリ上の古いヘッダーは選択されたままにする。
    m_deletedpaths->add(old);
    arg_partyrecord->m_fpath = ::cw::xmlcreater::create_partyrecord(arg_partyrecord, m_yadodir, m_tempdir);
    auto header = std::make_shared<::cw::header::PartyRecordHeader>(); header->initialize(std::nullopt, std::nullopt, arg_partyrecord);
    m_partyrecord[index] = header; changed(); return header;
}

void YadoData::remove_partyrecord(std::shared_ptr<::cw::header::PartyRecordHeader> arg_header)
{
    changed();
    const auto it = std::find(m_partyrecord.begin(), m_partyrecord.end(), arg_header);
    // Pythonの list.remove と同様、不在なら例外、一致は一つだけ除く。
    if (it == m_partyrecord.end()) throw std::logic_error("party record no longer exists");
    m_partyrecord.erase(it);
    if (arg_header) {
        const auto path = scenario_value_string(arg_header->m_fpath);
        if (m_deletedpaths && !path.empty()) m_deletedpaths->add(path);
    }
}

void YadoData::remove_emptypartyrecord()
{
    // 消去されたメンバーの枠は空の名前として残す。 Python版と同様
    // 、名前が一つも残らない記録は一覧からだけ除き、XMLは削除しない。
    m_partyrecord.erase(std::remove_if(m_partyrecord.begin(), m_partyrecord.end(), [](const auto& header) {
        return !header || std::none_of(header->m_members.begin(), header->m_members.end(), [](const auto& member) {
            const auto* name = std::get_if<::cw::lib::String>(&member.storage());
            return name && !name->empty();
        });
    }), m_partyrecord.end());
}

// ネイティブの任意記録は、不在ならfalse、文字列以外の値は飛ば
// す。 Python版は名前を含む記録を前提とする。 これは一人で
// も利用可能かの確認であり、全メンバー・記録XMLの検証ではない。
bool YadoData::can_restoreparty(std::shared_ptr<::cw::header::PartyRecordHeader> arg_partyrecordheader)
{
    if (!arg_partyrecordheader) return false;
    return std::any_of(arg_partyrecordheader->m_members.begin(), arg_partyrecordheader->m_members.end(), [&](const auto& member) {
        const auto* name = std::get_if<::cw::lib::String>(&member.storage());
        return name && can_restore(*name);
    });
}

// ネイティブのPartyメンバーには、パスを持つ複数の型がある。 Python
// の member.fpath と同様、共通の元パスを読み、不在の値・
// パスは飛ばす。 待機中または現在のパーティという利用条件は維持する。
bool YadoData::can_restore(::cw::lib::String arg_member)
{
    const auto available = [&](const auto& path) {
        return !path.empty() && std::get<0>(::cw::util::splitext(
            path_utf8(std::filesystem::u8path(path).filename()))) == arg_member;
    };
    if (std::any_of(m_standbys.begin(),m_standbys.end(),[&](const auto& h) {
        return h && available(scenario_value_string(h->m_fpath));
    })) return true;
    if (m_party && *m_party) for (const auto& member:(*m_party)->m_members) {
        // Party.membersはパス文字列だけでなく、XMLツリーと読
        // み込み済みキャラクターを含む。 復元前に現在のパーティを解散する。
        if (const auto* path=std::get_if<::cw::lib::String>(&member.storage())) {
            if (available(*path)) return true;
        } else if (const auto* object=std::get_if<::cw::lib::ObjectPtr>(&member.storage());object && *object) {
            if (const auto tree=std::dynamic_pointer_cast<CWPyElementTree>(*object)) {
                if (available(tree->m_fpath)) return true;
            } else if (const auto element=std::dynamic_pointer_cast<CWPyElement>(*object)) {
                if (available(element->m_fpath)) return true;
            } else if (const auto card=std::dynamic_pointer_cast<::cw::sprite::card::PlayerCard>(*object);
                       card && card->m_data_typed) {
                if (available(card->m_data_typed->m_fpath)) return true;
            } else if (const auto character=std::dynamic_pointer_cast<::cw::character::Character>(*object);
                       character && character->m_data_typed) {
                if (available(character->m_data_typed->m_fpath)) return true;
            }
        }
    }
    // Python版と同様、待機中PCと現在のパーティだけを許す。 ディスク上のフ
    // ァイルは、別パーティ・消去済みキャラクター・古いDB項目に属する場合がある。
    return false;
}

// ネイティブの記録なし・文字列以外の値は結果を加えない。Python版は有
// 効な名前付き記録を前提とする。 記録された名前順と最初の一致する待機PC
// を保ち、現在パーティの解散は restore_party に任せる。
std::vector<std::shared_ptr<::cw::header::AdventurerHeader>> YadoData::get_restoremembers(std::shared_ptr<::cw::header::PartyRecordHeader> arg_partyrecordheader)
{
    std::vector<std::shared_ptr<::cw::header::AdventurerHeader>> result; if (!arg_partyrecordheader) return result;
    for (const auto& member : arg_partyrecordheader->m_members) {
        const auto* name = std::get_if<::cw::lib::String>(&member.storage());
        if (!name) continue;
        for (const auto& standby : m_standbys) {
            if (!standby) continue;
            if (std::get<0>(::cw::util::splitext(path_utf8(
                    std::filesystem::u8path(scenario_value_string(standby->m_fpath)).filename()))) == *name) {
                result.push_back(standby);
                break;
            }
        }
    }
    return result;
}

void YadoData::restore_partyrecord_backpack(
    const std::shared_ptr<CWPyElement>& record,
    const std::shared_ptr<Party>& party,
    const std::function<void(std::shared_ptr<::cw::header::CardHeader>)>& transfer)
{
    if (!record || !party) return;
    if (!transfer) throw std::invalid_argument("party record restore requires card transfer");
    for (const auto& item:record->m_children) {
        if (!item || item->m_tag!="CardRecord") continue;
        const auto name=item->get("name",""),desc=item->get("desc","");
        const auto author=item->get("author",""),scenario=item->get("scenario","");
        const auto uselimit=item->getint(".",::cw::lib::Value("uselimit"),0);
        std::shared_ptr<::cw::header::CardHeader> found;
        for (const auto& card:m_storehouse) {
            if (card && card->m_name==name && card->m_desc==desc && card->m_author==author &&
                card->m_scenario==scenario && card->m_uselimit==uselimit) {found=card;break;}
        }
        if (!found) for (const auto& card:m_storehouse) {
            if (card && card->m_name==name && card->m_desc==desc) {found=card;break;}
        }
        if (!found) continue;
        // trade が元からの削除と荷物袋への挿入を管理する。 ここで再び消
        // すと erase(end()) となり、カード置場を壊してしまう。
        transfer(found);
        if (std::find(m_storehouse.begin(),m_storehouse.end(),found)!=m_storehouse.end() ||
            std::find(party->m_backpack.begin(),party->m_backpack.end(),found)==party->m_backpack.end())
            throw std::runtime_error("party record card transfer did not complete");
    }
    // trade は先頭に挿入するため、記録順を復元してから、 Pythonの Party.sort_backpack()
    // と同じユーザー指定の整列を行う。
    std::reverse(party->m_backpack.begin(),party->m_backpack.end());
    for (std::size_t i=0;i<party->m_backpack.size();++i)
        if (party->m_backpack[i]) party->m_backpack[i]->m_order=static_cast<::cw::lib::Integer>(i);
    party->sort_backpack(std::nullopt,std::nullopt);
}

std::vector<std::shared_ptr<::cw::header::AdventurerHeader>> YadoData::restore_party(std::shared_ptr<::cw::header::PartyRecordHeader> arg_partyrecordheader)
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_ydata || !*::cw::g_cwpy->m_ydata)
        throw std::logic_error("party restoration requires inn data");
    (*::cw::g_cwpy->m_ydata)->changed();
    if (m_party && *m_party) ::cw::g_cwpy->dissolve_party(std::nullopt,false);
    if (m_party && *m_party) throw std::logic_error("current party was not dissolved");
    if (!arg_partyrecordheader) throw std::invalid_argument("party record is required");
    auto result=get_restoremembers(arg_partyrecordheader);
    if (result.empty()) return result;
    for (const auto& header:result) {
        const auto found=std::find(m_standbys.begin(),m_standbys.end(),header);
        if(found==m_standbys.end()) throw std::invalid_argument("restore member is not a standby");
        m_standbys.erase(found);
    }
    const auto record_path=scenario_value_string(arg_partyrecordheader->m_fpath);
    ::cw::header::GetProperty property;
    property.initialize(record_path,std::nullopt,std::nullopt);
    const bool suspendlevelup=::cw::util::str2bool(property.m_properties.at("SuspendLevelUp"));
    const auto money=std::min(m_money,arg_partyrecordheader->m_money);
    set_money(-money,false);
    const auto party_path=::cw::xmlcreater::create_party(result,money,arg_partyrecordheader->m_name,suspendlevelup);
    const auto party_header=create_partyheader(::cw::lib::encoding::path_to_utf8(
        std::filesystem::u8path(party_path)/"Party.xml"),std::nullopt);
    if (!party_header) throw std::runtime_error("restored party XML could not be loaded");
    if (party_header) {
        const bool chgarea = !::cw::g_cwpy || (::cw::g_cwpy->m_areaid != value_integer_or(::cw::g_AREA_BREAKUP, -100) && ::cw::g_cwpy->m_areaid != 2);
        if (::cw::g_cwpy) ::cw::g_cwpy->load_party(party_header, chgarea, true, true);
        else {
            auto party = std::make_shared<Party>(); party->initialize(party_header, true); m_party = party;
        }
    }
    const auto record=::cw::data::yadoxml2etree(record_path,"BackpackRecord",std::nullopt);
    if (!record || !record->getroot()) throw std::runtime_error("party backpack record is missing");
    const auto backpack=record->getroot();
    if (::cw::g_cwpy && m_party && *m_party && backpack) {
        const auto party=*m_party;
        restore_partyrecord_backpack(backpack,party,[party](const auto& card) {
            ::cw::g_cwpy->trade("BACKPACK",std::nullopt,card,false,nullptr,-1,-1,false,false,
                party,false,false,false,true,0);
        });
    }
    if (::cw::g_cwpy && ::cw::g_cwpy->m_statusbar) ::cw::g_cwpy->m_statusbar->change(false, false);
    if (m_party && *m_party) (*m_party)->m__loading = false;
    return result;
}

// ネイティブはPythonのElementの真偽変換ではな
// く、ポインターの有無を調べる。 明示された要素は子がなく
// ても有効とし、解決できなければ不在データからヘッダーを
// 作らず候補なしを返す。 読み込み例外はそのまま伝える。
std::shared_ptr<::cw::header::AdventurerHeader> YadoData::create_advheader(::cw::lib::String arg_path, bool arg_album, std::optional<std::shared_ptr<::cw::data::CWPyElement>> arg_element)
{
    auto element = arg_element.value_or(nullptr);
    std::map<::cw::lib::String, ::cw::lib::String> rootattrs;
    if (!element && !arg_path.empty()) {
        auto tree = ::cw::data::yadoxml2etree(arg_path, "Property", rootattrs);
        if (tree) {
            element = tree->getroot();
        }
    }
    // 未解決のネイティブ要素はヘッダー候補なしを返す。 これはポインター結果の
    // 保護であり、Pythonの例外・真偽変換との同等性を示すものではない。
    if (!element) return {};

    // Python版と同様、渡されたPropertyの元パスは保つ。 この
    // 生成処理が自分で要素を読んだ場合だけ arg_path を付ける。
    if (!arg_element || !*arg_element) element->m_fpath = arg_path;
    auto header = std::make_shared<::cw::header::AdventurerHeader>();
    header->initialize(element, arg_album, std::nullopt, std::nullopt, {}, rootattrs);
    return header;
}

// ネイティブは共通CardHeader経路で、単独のProperty
// だけのヘッダーも受け付ける。 Python版は構築側がcarddata
// を要求しても、ここではdataだけを渡す。 要素が未解決なら
// 候補なしを返すが、読み込みエラーを空ヘッダーへ変換しない。
std::shared_ptr<::cw::header::CardHeader> YadoData::create_cardheader(::cw::lib::String arg_path, std::optional<std::shared_ptr<::cw::data::CWPyElement>> arg_element, std::optional<std::shared_ptr<::cw::character::Character>> arg_owner)
{
    auto element = arg_element.value_or(nullptr);
    if (!element && !arg_path.empty()) {
        auto tree = ::cw::data::yadoxml2etree(arg_path, "Property", std::nullopt);
        if (tree) element = tree->getroot();
    }
    if (!element) return {};
    // Python版と同様、渡されたPropertyの元パスは保つ。 この
    // 生成処理が自分で要素を読んだ場合だけ arg_path を付ける。
    if (!arg_element || !*arg_element) element->m_fpath = arg_path;
    auto header = std::make_shared<::cw::header::CardHeader>();
    // Pythonの create_cardheader と同様、Property
    // は単独のdataとしてだけ渡す。 carddataにも渡すと種類がProperty
    // になり、格納ファイルと埋め込みのカード本文を混同する。
    header->initialize(element, arg_owner, std::nullopt, false, {}, false,
                       std::nullopt, std::nullopt, {}, {});
    return header;
}

// ネイティブの任意入力はPropertyなしに解決される場合
// がある。 不在データからPartyHeaderを作ろうと
// せず候補なしを返すが、通常のXML読み込み例外は伝える。
std::shared_ptr<::cw::header::PartyHeader> YadoData::create_partyheader(::cw::lib::String arg_path, std::optional<std::shared_ptr<::cw::data::CWPyElement>> arg_element)
{
    auto element = arg_element.value_or(nullptr);
    if (!element && !arg_path.empty()) {
        auto tree = ::cw::data::yadoxml2etree(arg_path, "Property", std::nullopt);
        if (tree) element = tree->getroot();
    }
    if (!element) return {};
    // Python版と同様、渡されたPropertyの元パスは保つ。 この
    // 生成処理が自分で要素を読んだ場合だけ arg_path を付ける。
    if (!arg_element || !*arg_element) element->m_fpath = arg_path;
    auto header = std::make_shared<::cw::header::PartyHeader>();
    header->initialize(element, std::nullopt);
    return header;
}

// ネイティブの準備処理はヘッダーなしなら何もしないが、Python版は
// ヘッダーと実行環境を前提とする。 CWPyがなければ初期所持金をゼロ
// とし、生成ヘッダーがあれば、この宿でUI読み込みなしにPartyを
// 作る。 前の変更通知は実行環境の宿・所持金編集に依存するため、最後
// にこの宿へ再通知する。 このフラグは保存確定・復旧の境界ではない。
void YadoData::create_party(std::shared_ptr<::cw::header::AdventurerHeader> arg_header, bool arg_chgarea)
{
    if (!arg_header) return;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata) (*::cw::g_cwpy->m_ydata)->changed();
    auto initial_money = ::cw::g_cwpy && ::cw::g_cwpy->m_setting ? ::cw::g_cwpy->m_setting->m_initmoneyamount : 0;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_initmoneyisinitialcash)
        initial_money = ::cw::g_cwpy->m_setting->m_initialcash;
    // Python版と同様、指定された符号付きの初期所持金をParty XML
    // に保つ。 任意精度の符号反転はIntegerを超えられるため、最小値の
    // 場合は最大額の入金を使い、 有効な宿残高に対して同じ飽和結果にする。
    const auto money = std::min(m_money, initial_money);
    const auto withdrawal = money == (std::numeric_limits<::cw::lib::Integer>::min)()
        ? ::cw::lib::money::maximum : -money;
    set_money(withdrawal, false);
    const auto party_path = ::cw::xmlcreater::create_party({arg_header}, money, std::nullopt, false);
    // Python版と同様、Unicodeのパスを連結する。 生成パーティ
    // 名のネイティブパスを共通XML読み取りへ返すときもUTF-8を保つ。
    auto party_header = create_partyheader(::cw::lib::encoding::path_to_utf8(
        std::filesystem::u8path(party_path) / "Party.xml"), std::nullopt);
    if (party_header && ::cw::g_cwpy) ::cw::g_cwpy->load_party(party_header, arg_chgarea, false, true);
    else if (party_header) { auto party = std::make_shared<Party>(); party->initialize(party_header, true); m_party = party; }
    if (::cw::g_cwpy && ::cw::g_cwpy->m_statusbar) ::cw::g_cwpy->m_statusbar->change(false, false);
    changed();
}

// ネイティブは設定なしならOrderを使い、不在ヘッダーを最後に置く。Python
// 版は実行環境設定を必須とする。 有効なヘッダーは同じ選択キーと保存
// Orderの同順位規則を使い、保存Order自体は書き換えない。
void YadoData::sort_standbys()
{
    const auto condition = ::cw::g_cwpy && ::cw::g_cwpy->m_setting ? ::cw::g_cwpy->m_setting->m_sort_standbys : "Order";
    std::stable_sort(m_standbys.begin(), m_standbys.end(), [&](const auto& lhs, const auto& rhs) {
        if (!lhs || !rhs) return static_cast<bool>(rhs);
        const auto level_l=value_integer_or(lhs->m_level,0), level_r=value_integer_or(rhs->m_level,0);
        const auto name=::cw::util::logical_compare(lhs->m_name,rhs->m_name);
        if (condition=="Level") {if(level_l!=level_r)return level_l<level_r;if(name)return name<0;}
        else if (condition=="Name") {if(name)return name<0;if(level_l!=level_r)return level_l<level_r;}
        return value_integer_or(lhs->m_order,0)<value_integer_or(rhs->m_order,0);
    });
    // 表示の整列では、保存されたOrder値を変えない。
}

// ネイティブは実行環境なしならNameを使い、不在ヘッダーを最後
// に置く。 Python版は設定と全ヘッダーを前提とするが、有効
// なものは同じキー・論理名・保存Orderの同順位規則を使う。
void YadoData::sort_parties()
{
    const auto condition = ::cw::g_cwpy && ::cw::g_cwpy->m_setting ? ::cw::g_cwpy->m_setting->m_sort_parties : "Name";
    std::stable_sort(m_partys.begin(),m_partys.end(),[&](const auto& lhs,const auto& rhs) {
        if(!lhs||!rhs)return static_cast<bool>(rhs);
        if(condition=="HighestLevel"||condition=="AverageLevel") {
            const auto hl=lhs->get_highest_level(),hr=rhs->get_highest_level();
            const auto al=lhs->get_average_level(),ar=rhs->get_average_level();
            if(condition=="HighestLevel") {if(hl!=hr)return hl<hr;if(al!=ar)return al<ar;}
            else {if(al!=ar)return al<ar;if(hl!=hr)return hl<hr;}
        } else if(condition=="Money" && lhs->m_money!=rhs->m_money)return lhs->m_money<rhs->m_money;
        if(condition=="Name"||condition=="Money"||condition=="HighestLevel"||condition=="AverageLevel")
            if(const auto names=::cw::util::logical_compare(lhs->m_name,rhs->m_name);names)return names<0;
        return value_integer_or(lhs->m_order,0)<value_integer_or(rhs->m_order,0);
    });
}

// ネイティブの実行環境設定がなければ、星なしのName順を使う。Python
// 版は設定を必須とする。 利用可能なカードは共通のsort_cards
// を使い、その前後の一時的な適性の設定・解除も同じにする。
void YadoData::sort_storehouse(std::optional<std::shared_ptr<::cw::character::Character>> arg_test_aptitude)
{
    const auto condition = ::cw::g_cwpy && ::cw::g_cwpy->m_setting ? ::cw::g_cwpy->m_setting->m_sort_cards : "Name";
    const auto withstar = ::cw::g_cwpy && ::cw::g_cwpy->m_setting ? ::cw::g_cwpy->m_setting->m_sort_cardswithstar : false;
    if (condition == "Aptitude" && arg_test_aptitude) for (const auto& card : m_storehouse) if (card) card->set_testaptitude(arg_test_aptitude);
    // Noneでも、Python版の個人所有者・星・保存Orderのキーは適用する。
    sort_cards(m_storehouse, condition, withstar);
    if (condition == "Aptitude" && arg_test_aptitude) for (const auto& card : m_storehouse) if (card) card->set_testaptitude(std::nullopt);
}

// パーティ記録はPython版と同様、設定のパーティ順や
// 保存Orderとは独立した固定の論理名順にする。 ネ
// イティブの不在ヘッダーは、比較で参照せず最後に置く。
void YadoData::sort_partyrecord()
{
    std::stable_sort(m_partyrecord.begin(), m_partyrecord.end(), [](const auto& lhs, const auto& rhs) {
        if (!lhs || !rhs) return static_cast<bool>(rhs);
        return ::cw::util::logical_compare(lhs->m_name, rhs->m_name) < 0;
    });
}

namespace {
// 生成された公開のC++補助関数には宿インスタンスがない。 実行
// 時のパス情報だけを明示的に渡し、元の内部の保存方針を共有する。
void commit_party_timekeeper(const ::cw::lib::String& ppath, bool adventuring,
                             const ::cw::lib::String& yadodir,
                             const ::cw::lib::String& tempdir)
{
        if (ppath.empty()) return;
        auto dpath = std::filesystem::u8path(ppath).parent_path();
        if (!yadodir.empty() && !tempdir.empty())
            if (const auto paths = ::cw::util::resolve_yado_paths(path_utf8(dpath), yadodir, tempdir))
            dpath = std::filesystem::u8path(paths->first);
        dpath /= "Debug";
        const auto remove_timekeeper = [&](const std::filesystem::path& file) {
            // Python版と同様、公開の削除処理で通常ファイルだけを
            // 消し、ネイティブの読み取り専用の復旧と失敗伝播も行う。
            // 内部を隠すStorageは既存の削除エラーの規則を維持
            // し、 そのバックエンド外でホストの権限変更を試みない。
            if (mounted_or_native_is_file(file))
                ::cw::util::remove(path_utf8(file), false);
        };
        remove_timekeeper(dpath / "OldTimekeeper.xml");
        const bool enabled = ::cw::g_cwpy && ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_enabled_timekeeper;
        if (!adventuring || !enabled) remove_timekeeper(dpath / "Timekeeper.xml");
        ::cw::util::remove_emptydir(path_utf8(dpath));
    
}
} // namespace

// ネイティブのスナップショットは、失敗復旧用の保存管理情報を保持する。 実ファイルも戻
// すにはStorageの一括トランザクション対応が必要で、通常のホスト転送は宿全体の不
// 可分な保存ではない。 Python版にはこのスナップショット・Storageの仕組み
// はない。 キューの書き出しは、ロックされたマウント先のトランザクションの外で行う。
void YadoData::save()
{
    const auto save_capabilities =
        ::cardwirth::platform::mounted_user_storage_capabilities(
            std::filesystem::u8path(m_yadodir));
    const bool atomic_storage_save = save_capabilities &&
        save_capabilities->atomic_multi_operation_transactions;
    const bool build_native_indexes = !save_capabilities ||
        save_capabilities->requires_native_secondary_indexes;
    // FileSyncはワーカースレッドを持つ。 マウント先のトランザクション前に、ワ
    // ーカーがStorageを取得できる間に保留処理を完了させる。 BEGIN後に待つ
    // と、DatabaseStorageのトランザクションロックとデッドロックする。
    if (atomic_storage_save && ::cw::g_fsync) ::cw::g_fsync->sync();
    // Storageの取り消しでは、メモリ内の保存管理情報も復元する。 save()
    // は成功時にパーティのキャッシュ・遅延XMLを解放するため、所有参照を保持する。
    struct SaveState {
        bool committed{false};
        std::vector<std::function<void()>> restore;
        ~SaveState() { if (!committed) for (auto it = restore.rbegin(); it != restore.rend(); ++it) (*it)(); }
    } save_state;
    {
        // マウントしたStorageには、トランザクション復旧用にこのスナップショットが必要。
        // ネイティブのFileSyncも sync() で保留書き込みの失敗を再送出するた
        // め、 その失敗時には変更パス・遅延ツリーを戻し、次の保存で再試行できるようにする。
        const auto remember = [&save_state](const auto& owner, auto& field) {
            save_state.restore.emplace_back([owner, &field, saved = field]() mutable { field = std::move(saved); });
        };
        const auto remember_tree = [&](const std::shared_ptr<CWPyElementTree>& tree) {
            if (!tree) return;
            remember(tree, tree->m_fpath);
            remember(tree, tree->m_is_edited);
        };
        std::set<const ::cw::header::CardHeader*> remembered_cards;
        const auto remember_card = [&](const std::shared_ptr<::cw::header::CardHeader>& card) {
            if (!card || !remembered_cards.insert(card.get()).second) return;
            const auto tree = card->m__lazy_write ? *card->m__lazy_write : nullptr;
            save_state.restore.emplace_back([card, tree, atomic_storage_save,
                path = card->m_fpath, lazy = card->m__lazy_write,
                unique = card->m_lazy_write_needs_unique_path,
                tree_path = tree ? tree->m_fpath : ::cw::lib::String{},
                edited = tree && tree->m_is_edited]() mutable {
                // トランザクション対応のStorageはファイルも戻すため、
                // 再試行には元の一意性の条件が必要。 ネイティブの一時書き込
                // みはこのカードが所有し、成功して付いたパスを保持する。 再
                // 試行で別の個体を統合したり、孤立した複製を作ったりしない。
                const bool keep_assigned = !atomic_storage_save && unique && tree &&
                    !card->m_lazy_write_needs_unique_path;
                card->m_fpath = keep_assigned ? tree->m_fpath : std::move(path);
                card->m__lazy_write = std::move(lazy);
                card->m_lazy_write_needs_unique_path = keep_assigned ? false : unique;
                if (tree) {
                    if (!keep_assigned) tree->m_fpath = std::move(tree_path);
                    tree->m_is_edited = edited;
                }
            });
        };
        const auto remember_party = [&](const std::shared_ptr<Party>& party) {
            if (!party) return;
            remember(party, party->m_path);
            if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&party->m_data.storage()))
                remember_tree(std::dynamic_pointer_cast<CWPyElementTree>(*object));
            for (const auto& member : party->m_members)
                if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&member.storage()))
                    remember_tree(std::dynamic_pointer_cast<CWPyElementTree>(*object));
            for (const auto& card : party->m_backpack) remember_card(card);
            for (const auto& card : party->m_backpack_moved) remember_card(card);
        };
        remember_tree(m_environment);
        if (m_party) remember_party(*m_party);
        for (const auto& card : m_storehouse) remember_card(card);
        for (const auto& header : m_partys) if (header) {
            remember(header, header->m_fpath);
            remember(header, header->m_data);
            if (header->m_data) remember_party(*header->m_data);
        }
        for (const auto& header : m_standbys) if (header) remember(header, header->m_fpath);
        for (const auto& header : m_album) if (header) remember(header, header->m_fpath);
        for (const auto& header : m_partyrecord) if (header) remember(header, header->m_fpath);
        for (const auto& [key, header] : m_savedjpdcimage) if (header) remember(header, header->m_fpath);
        if (m_deletedpaths) remember(m_deletedpaths, m_deletedpaths->m__s);
        // プレイ中の save_variables は宿の集合ではなく
        // ScenarioLogへ削除予定を入れる場合がある。 Storage
        // の確定失敗では、既存列の可変内容も含め、両方を戻す。
        if (::cw::g_cwpy) {
            if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_sdata.storage())) {
                if (const auto system = std::dynamic_pointer_cast<SystemData>(*object)) {
                    remember(system, system->m_deletedpaths);
                    if (const auto* sequence = std::get_if<::cw::lib::ObjectPtr>(&system->m_deletedpaths.storage()))
                        if (const auto values = std::dynamic_pointer_cast<VariantSequence>(*sequence))
                            remember(values, values->m_values);
                }
            }
        }
    }
    ::cardwirth::platform::ScopedMountedStorageTransaction save_transaction(
        std::filesystem::u8path(m_yadodir));
    if (!save_transaction.good())
        throw std::runtime_error("暗号化宿の保存トランザクションを開始できません: " +
                                 save_transaction.last_error());
    if (::cw::g_cwpy) {
        if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_sdata.storage())) {
            if (const auto system = std::dynamic_pointer_cast<::cw::data::SystemData>(*object)) system->save_variables();
            if (::cw::g_cwpy->is_playingscenario()) {
                if (const auto scenario = std::dynamic_pointer_cast<::cw::data::ScenarioData>(*object)) {
                    scenario->update_log();
                    if (::cw::g_cwpy->m_advlog) ::cw::g_cwpy->m_advlog->resume_scenario(::cw::g_cwpy->m_advlog->m_logfilepath);
                }
            }
        }
    }
    // Temp由来のパスをYado相対へ戻す。DBがない場合でも次回起動時に
    // ファイルを失わないよう、ヘッダの状態とXMLを先にフラッシュする。
    for (const auto& card : m_storehouse) if (card) rewrite_temp_path(card->m_fpath, m_tempdir, m_yadodir);
    for (const auto& adv : m_standbys) if (adv) rewrite_temp_value(adv->m_fpath, m_tempdir, m_yadodir);
    for (const auto& adv : m_album) if (adv) rewrite_temp_value(adv->m_fpath, m_tempdir, m_yadodir);
    for (const auto& party : m_partys) if (party) rewrite_temp_value(party->m_fpath, m_tempdir, m_yadodir);
    for (const auto& record : m_partyrecord) if (record) rewrite_temp_value(record->m_fpath, m_tempdir, m_yadodir);
    for (const auto& [unused, image] : m_savedjpdcimage) { (void)unused; if (image) rewrite_temp_value(image->m_fpath, m_tempdir, m_yadodir); }
    if (m_environment && m_environment->m_is_edited) m_environment->write_xml(true);
    if (m_party) m_party.value()->write();
    for (const auto& header : m_partys) if (header && header->m_data && *header->m_data) (*header->m_data)->write();
    // Python版の保存境界に合わせ、Temp転送前にC++
    // の保留カードツリーを書き出す。 下の索引更新時だけでは
    // 、今回の転送後に新しいXMLがTempへ残ってしまう。
    for (const auto& card : m_storehouse) if (card) card->do_write(false);
    const auto flush_party_cards = [](const std::shared_ptr<Party>& party) {
        if (!party) return;
        for (const auto& card : party->m_backpack) if (card) card->do_write(false);
        for (const auto& card : party->m_backpack_moved) if (card) card->do_write(false);
    };
    if (m_party) flush_party_cards(*m_party);
    for (const auto& header : m_partys)
        if (header && header->m_data) flush_party_cards(*header->m_data);
    // ネイティブ・Pythonの保存は、途中まで移動したツリーを再開す
    // るため DeletedPaths.temp を必要とする。 暗号
    // 化Storageは宿全体を一括保存するため、同じSQLiteト
    // ランザクション内で印を作って消すのは無駄な書き込みになる。 失
    // 敗した削除は、実際に再試行するものがある場合だけ下で保存する。
    if (m_deletedpaths && !atomic_storage_save) m_deletedpaths->write_list();
    if (!atomic_storage_save && ::cw::g_fsync) ::cw::g_fsync->sync();
    _transfer_temp();
    // 上のdo_writeは保留ヘッダーをTempへ向ける。 そのファイ
    // ルは確定済みなので、下の荷物袋索引と同様に保存先パスを反映する。
    for (const auto& card : m_storehouse) if (card) rewrite_temp_path(card->m_fpath, m_tempdir, m_yadodir);
    // Python版saveのcommit_timekeeper。Temp由来のパーティはYado側へ
    // 正規化してからOldTimekeeper/Timekeeperの寿命を確定する。
    const auto commit_timekeeper = [&](const ::cw::lib::String& ppath, bool adventuring) {
        commit_party_timekeeper(ppath, adventuring, m_yadodir, m_tempdir);
    };
    if (m_party) commit_timekeeper(scenario_value_string(m_party.value()->m_path),
                                   ::cw::g_cwpy && ::cw::g_cwpy->is_playingscenario());
    if (m_party && *m_party) YadoData_save_update_backpack(*m_party);
    std::map<::cw::lib::String, ::cw::lib::Integer> partyorder;
    for (const auto& header : m_partys) if (header) {
        const auto path = scenario_value_string(header->m_fpath);
        // 実ファイルの削除失敗で、論理的に終了した冒険を復活させない。
        const bool adventuring = header->is_adventuring();
        commit_timekeeper(path, adventuring);
        if (header->m_data && *header->m_data) {
            YadoData_save_update_backpack(*header->m_data);
            // Python版は保存後にヘッダから一時Partyデータを解放する。
            header->m_data.reset();
        }
        if (!path.empty()) {
            auto logical = path;
            rewrite_temp_path(logical, m_tempdir, m_yadodir);
            header->m_fpath = logical;
            partyorder[save_rel_key(logical, m_yadodir, m_tempdir)] = value_integer_or(header->m_order, 0);
        }
    }
    std::map<::cw::lib::String, ::cw::lib::Integer> cardorder;
    std::map<::cw::lib::String, std::shared_ptr<::cw::header::CardHeader>> cardheaders;
    for (const auto& card : m_storehouse) if (card) {
        const auto key = save_rel_key(card->m_fpath, m_yadodir, m_tempdir);
        cardorder[key] = value_integer_or(card->m_order, 0);
        cardheaders[key] = card;
    }
    std::map<::cw::lib::String, ::cw::lib::Integer> adventurerorder;
    std::map<::cw::lib::String, std::shared_ptr<::cw::header::AdventurerHeader>> adventurerheaders;
    for (const auto& adv : m_standbys) if (adv) {
        const auto key = save_rel_key(scenario_value_string(adv->m_fpath), m_yadodir, m_tempdir);
        adventurerorder[key] = value_integer_or(adv->m_order, 0);
        adventurerheaders[key] = adv;
    }
    std::map<::cw::lib::String, std::shared_ptr<::cw::header::PartyRecordHeader>> partyrecordheaders;
    for (const auto& record : m_partyrecord) if (record) {
        partyrecordheaders[save_rel_key(scenario_value_string(record->m_fpath), m_yadodir, m_tempdir)] = record;
    }
    std::map<::cw::lib::String, std::shared_ptr<::cw::header::SavedJPDCImageHeader>> savedheaders;
    for (const auto& [unused, image] : m_savedjpdcimage) {
        (void)unused;
        if (image) savedheaders[save_rel_key(scenario_value_string(image->m_fpath), m_yadodir, m_tempdir)] = image;
    }
    // DB更新はPython版と同じくファイル転送完了後に行う。メモリ上のHeaderを
    // 渡し、カード置場・宿帳の並びとパーティ記録を XML 再読込なしで登録する。
    if (build_native_indexes) {
        ::cw::yadodb::YadoDB db;
        db.initialize(m_yadodir, ::cw::yadodb::g_YADO);
        db.update(true, true, true, cardorder, adventurerorder, partyorder, true, true,
                  std::move(cardheaders), std::move(adventurerheaders), {}, std::move(partyrecordheaders), std::move(savedheaders));
        db.close();
    }
    if (!save_transaction.commit())
        throw std::runtime_error("暗号化宿の保存トランザクションを確定できません: " +
                                 save_transaction.last_error());
    save_state.committed = true;
    if (::cw::g_cwpy) ::cw::g_cwpy->clear_selection();
    m__changed = false;
}

// ここは削除の印を読める場合だけTemp転送を再試行する。 スナップショット
// 、トランザクション完了、補助索引の更新はsave()が管理する。 ネイティ
// ブの削除集合の所有者がなければ省略し、Python版は存在を前提とする。
void YadoData::_retry_save()
{
    if (m_deletedpaths && m_deletedpaths->read_list()) _transfer_temp();
}

void YadoData::_transfer_temp()
{
    if (::cw::g_fsync) ::cw::g_fsync->sync();
    if (m_tempdir.empty() || m_yadodir.empty()) return;
    // ネイティブの拡張として、保存データを変える前に、重複・不正設定の一時
    // ルートを拒否する。 Python版は異なる宿ルートが渡される前提。
    (void)::cw::util::get_yado_writepath(m_yadodir, m_yadodir, m_tempdir);
    const auto temp_root = std::filesystem::u8path(m_tempdir).lexically_normal();
    const auto yado_root = std::filesystem::u8path(m_yadodir).lexically_normal();
    const auto deleted_list = (temp_root / "DeletedPaths.temp").lexically_normal();
    // Storageのmoveは必要な仮想ファイルシステムの基本
    // 操作。 転送対象、上書き方針、削除、後処理は共通に保つ。
    const auto move_file = [](const std::filesystem::path& source,
                              const std::filesystem::path& destination) {
        const bool from_mounted = ::cardwirth::platform::is_mounted_user_path(source);
        const bool to_mounted = ::cardwirth::platform::is_mounted_user_path(destination);
        if (from_mounted || to_mounted) {
            // 認証済みのユーザーデータを、ネイティブの平文へ書き出さない。
            if (!from_mounted || !to_mounted)
                throw std::runtime_error("inn transfer crosses the mounted storage boundary");
            if (::cw::g_fsync) ::cw::g_fsync->sync();
            if (!mounted_or_native_is_directory(destination.parent_path()) &&
                !mounted_or_native_mkdir(destination.parent_path()))
                throw std::runtime_error("cannot create inn transfer destination");
            if (mounted_or_native_is_file(destination))
                ::cw::util::remove_file(path_utf8(destination), 0, false);
            if (!::cardwirth::platform::move_mounted_user_path(source, destination, false))
                throw std::runtime_error("cannot move inn staging file: " + path_utf8(source));
        } else {
            // rename_file はPython版と同じ権限変更・再
            // 試行で既存の読み取り専用の保存先を除き、転送失敗を伝える。
            ::cw::util::rename_file(path_utf8(source), path_utf8(destination), false);
        }
    };
    if (mounted_or_native_is_directory(temp_root)) {
        for (const auto& source : mounted_or_native_files_recursive(temp_root)) {
            if (source.lexically_normal() == deleted_list) continue;
            if (!mounted_or_native_is_file(source)) continue;
            const auto relative = source.lexically_relative(temp_root);
            if (relative.empty() || relative.is_absolute() ||
                std::find(relative.begin(), relative.end(), std::filesystem::path("..")) != relative.end())
                throw std::runtime_error("inn staging file is outside its temporary directory");
            move_file(source, (yado_root / relative).lexically_normal());
        }
    }
    const auto protected_path = [&](const std::filesystem::path& path) {
        const auto text = path_utf8(path);
        for (const auto& root : {yado_root, temp_root, yado_root / "Adventurer", yado_root / "Party",
                yado_root / "Album", yado_root / "CastCard", yado_root / "SkillCard",
                yado_root / "ItemCard", yado_root / "BeastCard", yado_root / "InfoCard", yado_root / "Material"}) {
            const auto other = path_utf8(root);
            if (::cw::util::path_starts_with(text, other) && ::cw::util::path_starts_with(other, text))
                return true;
        }
        return false;
    };
    const auto material = path_utf8(yado_root / "Material");
    if (m_deletedpaths) {
        for (const auto& item : m_deletedpaths->iterate()) {
            const auto paths = ::cw::util::resolve_yado_paths(item, m_yadodir, m_tempdir);
            // ネイティブの拡張として、認証された保存先の対応付けには
            // 有効な宿相対の削除パスを要求し、壊れた記録を拒否する。
            if (!paths) throw std::runtime_error("invalid inn deletion path: " + item);
            const auto path = std::filesystem::u8path(paths->first).lexically_normal();
            if (protected_path(path)) continue;
            ::cw::util::remove(path_utf8(path), false);
            const auto parent = path.parent_path();
            // Python版と同様、直近の親だけを整理し、無関係なMaterialディレ
            // クトリまで再帰的に掃除しない。 Python版は単純な接頭辞判定を使う。
            if (path_utf8(parent).starts_with(material) &&
                mounted_or_native_is_directory(parent) && mounted_or_native_list(parent).empty())
                ::cw::util::remove(path_utf8(parent), false);
        }
        m_deletedpaths->clear();
    }
    // Python版と同様、上の例外では一時データを消す前に止まる。 読み取
    // り専用ファイルとTempディレクトリの方針は、共通の削除APIが扱う。
    ::cw::util::remove(path_utf8(deleted_list), false);
    ::cw::util::remove(path_utf8(temp_root), false);
}

// Pythonのgetfindと異なり、ネイティブの環境・コンテナ
// ー情報がなければ空の集合を返す。 不完全な宿の保護として維持し、
// has_gossip/has_compstampも別の検索方針
// を持たず同じ結果を使う。 既存の空でない記録の所属は変えない。
std::set<::cw::lib::String> YadoData::get_gossips()
{
    std::set<::cw::lib::String> result;
    if (!m_environment) return result;
    const auto e = m_environment->getroot()->find("Gossips");
    if (e && *e) for (const auto& child : (*e)->m_children) if (child && !child->m_text.empty()) result.insert(child->m_text);
    return result;
}

// Pythonのgetfindと異なり、ネイティブの環境・コンテナ
// ー情報がなければ空の集合を返す。 不完全な宿の保護として維持し、
// has_gossip/has_compstampも別の検索方針
// を持たず同じ結果を使う。 既存の空でない記録の所属は変えない。
std::set<::cw::lib::String> YadoData::get_compstamps()
{
    std::set<::cw::lib::String> result;
    if (!m_environment) return result;
    const auto e = m_environment->getroot()->find("CompleteStamps");
    if (e && *e) for (const auto& child : (*e)->m_children) if (child && !child->m_text.empty()) result.insert(child->m_text);
    return result;
}

// 準備中のネイティブの宿にはEnvironment・履歴グループが
// ない場合があるため、共通の所属判定と同様、空の一覧を返す。 Python
// 版は両者を必須とするが、既存の記録は同じXML順を保つ。
std::vector<::cw::lib::String> YadoData::get_gossiplist()
{
    std::vector<::cw::lib::String> result;
    if (!m_environment) return result;
    const auto e = m_environment->getroot()->find("Gossips");
    if (e && *e) for (const auto& child : (*e)->m_children) if (child && !child->m_text.empty()) result.push_back(child->m_text);
    return result;
}

// 準備中のネイティブの宿にはEnvironment・履歴グループが
// ない場合があるため、共通の所属判定と同様、空の一覧を返す。 Python
// 版は両者を必須とするが、既存の記録は同じXML順を保つ。
std::vector<::cw::lib::String> YadoData::get_compstamplist()
{
    std::vector<::cw::lib::String> result;
    if (!m_environment) return result;
    const auto e = m_environment->getroot()->find("CompleteStamps");
    if (e && *e) for (const auto& child : (*e)->m_children) if (child && !child->m_text.empty()) result.push_back(child->m_text);
    return result;
}

bool YadoData::has_compstamp(::cw::lib::String arg_name)
{
    const auto values = get_compstamps();
    return values.contains(arg_name);
}

bool YadoData::has_gossip(::cw::lib::String arg_name)
{
    const auto values = get_gossips();
    return values.contains(arg_name);
}

// 準備中のネイティブ環境の保護として、Environment
// なしではXML・シナリオ履歴を変えない。 存在する場合は不
// 足する任意の履歴グループを作る。Python版はhas_*
// ・appendでグループを必須とする。 XML、シナリオ履
// 歴、変更通知の順に進め、後段失敗時の巻き戻しは保証しない。
void YadoData::set_compstamp(::cw::lib::String arg_name)
{
    if (has_compstamp(arg_name) || !m_environment) return;
    auto e = m_environment->getroot()->find("CompleteStamps");
    if (!e || !*e) {
        auto group = ::cw::data::make_element("CompleteStamps");
        m_environment->getroot()->append(group); e = m_environment->getroot()->find("CompleteStamps");
    }
    if (e && *e) {
        (*e)->append(::cw::data::make_element("CompleteStamp", arg_name));
        if (auto* sdata = playing_scenario_for_yado_history()) {
            const auto it = sdata->m_compstamps.find(arg_name);
            if (it != sdata->m_compstamps.end() && !it->second) sdata->m_compstamps.erase(it);
            else sdata->m_compstamps[arg_name] = true;
        }
        m_environment->m_is_edited = true;
        changed();
    }
}

// 準備中のネイティブ環境の保護として、Environment
// なしではXML・シナリオ履歴を変えない。 存在する場合は不
// 足する任意の履歴グループを作る。Python版はhas_*
// ・appendでグループを必須とする。 XML、シナリオ履
// 歴、変更通知の順に進め、後段失敗時の巻き戻しは保証しない。
void YadoData::set_gossip(::cw::lib::String arg_name)
{
    if (has_gossip(arg_name) || !m_environment) return;
    auto e = m_environment->getroot()->find("Gossips");
    if (!e || !*e) {
        auto group = ::cw::data::make_element("Gossips");
        m_environment->getroot()->append(group); e = m_environment->getroot()->find("Gossips");
    }
    if (e && *e) {
        (*e)->append(::cw::data::make_element("Gossip", arg_name));
        if (auto* sdata = playing_scenario_for_yado_history()) {
            const auto it = sdata->m_gossips.find(arg_name);
            if (it != sdata->m_gossips.end() && !it->second) sdata->m_gossips.erase(it);
            else sdata->m_gossips[arg_name] = true;
        }
        m_environment->m_is_edited = true;
        changed();
    }
}

// ネイティブの削除はEnvironment・履歴グループ・記録の不在を許
// 容する。 Pythonのgetfindはグループを必須とする。 実際に
// XMLを除いた場合だけ履歴・変更状態を更新し、不在項目を記録しない。
void YadoData::remove_compstamp(::cw::lib::String arg_name)
{
    if (!m_environment) return;
    auto e = m_environment->getroot()->find("CompleteStamps");
    if (!e || !*e) return;
    std::vector<std::shared_ptr<CWPyElement>> removed; for (const auto& child : (*e)->m_children) if (child && child->m_text == arg_name) removed.push_back(child);
    for (const auto& child : removed) (*e)->remove(child);
    if (!removed.empty()) {
        if (auto* sdata = playing_scenario_for_yado_history()) {
            const auto it = sdata->m_compstamps.find(arg_name);
            if (it != sdata->m_compstamps.end() && it->second) sdata->m_compstamps.erase(it);
            else sdata->m_compstamps[arg_name] = false;
        }
        m_environment->m_is_edited = true;
        changed();
    }
}

// ネイティブの削除はEnvironment・履歴グループ・記録の不在を許
// 容する。 Pythonのgetfindはグループを必須とする。 実際に
// XMLを除いた場合だけ履歴・変更状態を更新し、不在項目を記録しない。
void YadoData::remove_gossip(::cw::lib::String arg_name)
{
    if (!m_environment) return;
    auto e = m_environment->getroot()->find("Gossips");
    if (!e || !*e) return;
    std::vector<std::shared_ptr<CWPyElement>> removed; for (const auto& child : (*e)->m_children) if (child && child->m_text == arg_name) removed.push_back(child);
    for (const auto& child : removed) (*e)->remove(child);
    if (!removed.empty()) {
        if (auto* sdata = playing_scenario_for_yado_history()) {
            const auto it = sdata->m_gossips.find(arg_name);
            if (it != sdata->m_gossips.end() && it->second) sdata->m_gossips.erase(it);
            else sdata->m_gossips[arg_name] = false;
        }
        m_environment->m_is_edited = true;
        changed();
    }
}

// 空・準備中のネイティブの宿には消すものがない。PythonのgetfindはEnvironment
// の履歴グループを必須とする。 XMLから実際に除いた記録だけをシナリオ履
// 歴へ反映し、その後で変更を通知する。 逐次処理であり、一括確定ではない。
void YadoData::clear_compstamps()
{
    if (!m_environment) return;
    auto e = m_environment->getroot()->find("CompleteStamps");
    if (e && *e) {
        const auto removed = (*e)->m_children;
        const bool had = !removed.empty();
        for (const auto& child : removed) if (child) { child->m_cwxparent.reset(); child->m_parent.reset(); }
        (*e)->m_children.clear();
        if (had) {
            if (auto* sdata = playing_scenario_for_yado_history()) for (const auto& child : removed) if (child) {
                const auto it = sdata->m_compstamps.find(child->m_text);
                if (it != sdata->m_compstamps.end() && it->second) sdata->m_compstamps.erase(it);
                else sdata->m_compstamps[child->m_text] = false;
            }
            m_environment->m_is_edited = true; changed();
        }
    }
}

// 空・準備中のネイティブの宿には消すものがない。PythonのgetfindはEnvironment
// の履歴グループを必須とする。 XMLから実際に除いた記録だけをシナリオ履
// 歴へ反映し、その後で変更を通知する。 逐次処理であり、一括確定ではない。
void YadoData::clear_gossips()
{
    if (!m_environment) return;
    auto e = m_environment->getroot()->find("Gossips");
    if (e && *e) {
        const auto removed = (*e)->m_children;
        const bool had = !removed.empty();
        for (const auto& child : removed) if (child) { child->m_cwxparent.reset(); child->m_parent.reset(); }
        (*e)->m_children.clear();
        if (had) {
            if (auto* sdata = playing_scenario_for_yado_history()) for (const auto& child : removed) if (child) {
                const auto it = sdata->m_gossips.find(child->m_text);
                if (it != sdata->m_gossips.end() && it->second) sdata->m_gossips.erase(it);
                else sdata->m_gossips[child->m_text] = false;
            }
            m_environment->m_is_edited = true; changed();
        }
    }
}

::cw::lib::Integer YadoData::find_gossip(::cw::lib::Callback arg_matcher, ::cw::lib::Integer arg_startindex)
{
    const auto group = m_environment ? m_environment->find("Gossips") : std::nullopt;
    if (!group || !*group) return -1;
    // Python版と同様、空要素も含む生のXML列をここで参
    // 照する。 get_gossiplistは編集UI用の別の
    // 絞り込み済み一覧。 照合コールバックの前に子の所有権を
    // 複製し、走査中に元XMLへ消去・追加されても保持する。
    const auto values = (*group)->m_children;
    const auto size = static_cast<::cw::lib::Integer>(values.size());
    const auto begin = arg_startindex < 0 ? std::max<::cw::lib::Integer>(0, size + arg_startindex) : arg_startindex;
    for (::cw::lib::Integer i = begin; i < size; ++i) {
        if (!arg_matcher) continue;
        const auto child = values[static_cast<std::size_t>(i)];
        const auto matched = arg_matcher({::cw::lib::Value(child ? child->m_text : ::cw::lib::String{})}, {});
        // Python版と同様、matcher(...)の結果は真偽値変
        // 換で判定し、ネイティブValueのbool型だけに限定しない。
        bool matches = matched.as_bool();
        if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&matched.storage()); object && *object) {
            if (const auto sequence = std::dynamic_pointer_cast<VariantSequence>(*object))
                matches = !sequence->m_values.empty();
            else if (const auto mapping = std::dynamic_pointer_cast<VariantMapping>(*object))
                matches = !mapping->m_values.empty();
            else if (const auto element = std::dynamic_pointer_cast<CWPyElement>(*object))
                matches = element->to_bool();
            else if (const auto flag = std::dynamic_pointer_cast<Flag>(*object))
                matches = flag->to_bool();
        }
        if (matches) return i - begin + arg_startindex;
    }
    return -1;
}

// Environment・グループの不在や位置が不正なら、ネイティブの out_of_range
// を使う。 Python版のグループなしはException、Environment
// なしは検索前に失敗するが、ここでは位置エラーの境界へまとめる。
::cw::lib::String YadoData::get_gossip_at(::cw::lib::Integer arg_index)
{
    const auto group = m_environment ? m_environment->find("Gossips") : std::nullopt;
    if (!group || !*group) throw std::out_of_range("No gossips.");
    const auto& values = (*group)->m_children;
    const auto size = static_cast<::cw::lib::Integer>(values.size());
    const auto index = arg_index < 0 ? size + arg_index : arg_index;
    if (index < 0 || index >= size) throw std::out_of_range("gossip index");
    const auto child = values[static_cast<std::size_t>(index)];
    return child ? child->m_text : ::cw::lib::String{};
}

// ネイティブの任意Environmentがなければ履歴長はゼロ。 Python
// 版もグループなしはゼロだが、Environment自体は必須。
::cw::lib::Integer YadoData::gossips_len()
{
    const auto group = m_environment ? m_environment->find("Gossips") : std::nullopt;
    return group && *group ? static_cast<::cw::lib::Integer>((*group)->m_children.size()) : 0;
}

void YadoData::set_money(::cw::lib::Integer arg_value, bool arg_blink)
{
    if (arg_value == 0) return;
    // Python版と同様、金額変更・UI操作より前に宿を変更済みにす
    // る。 後のXML・UI処理が例外になっても、その状態を保持する。
    changed();
    m_money = add_money_clamped(m_money, arg_value);
    if (!m_environment || !m_environment->getroot())
        throw std::logic_error("YadoData::set_money requires Environment XML");
    auto cash = m_environment->getroot()->find("Property/Cashbox");
    if (!cash || !*cash)
        throw std::logic_error("YadoData::set_money requires Property/Cashbox");
    (*cash)->set_text(std::to_string(m_money));
    m_environment->m_is_edited = true;
    if (::cw::g_cwpy) {
        const bool showbuttons = !::cw::g_cwpy->is_playingscenario() || !::cw::g_cwpy->is_runningevent();
        if (::cw::g_cwpy->m_statusbar) ::cw::g_cwpy->m_statusbar->change(showbuttons, false);
        ::cw::g_cwpy->m_has_inputevent = true;
    }
    if (arg_blink && ::cw::g_cwpy && ::cw::g_cwpy->m_statusbar &&
        ::cw::g_cwpy->m_statusbar->m_yadomoney && *::cw::g_cwpy->m_statusbar->m_yadomoney)
        ::cw::animation::start_animation(*::cw::g_cwpy->m_statusbar->m_yadomoney, "blink");
}

void YadoData::join_npcs()
{
    if (!::cw::g_cwpy) return;
    const auto cards = ::cw::g_cwpy->get_fcards({});
    const auto outdir = std::filesystem::u8path(m_tempdir) / "Adventurer";
    (void)mounted_or_native_mkdir(outdir);
    for (const auto& fcard : cards) if (fcard && fcard->m_data_typed) {
        changed(); fcard->set_fullrecovery();
        if (!fcard->has_age() || !fcard->has_sex()) {
            ::cw::g_cwpy->play_sound("signal", false, 100, 1, 0, 0, false);
            const auto character = std::static_pointer_cast<::cw::character::Character>(fcard);
            ::cw::g_cwpy->call_modaldlg("DATACOMP",
                {{"ccard", ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(character))}});
        }
        fcard->set_coupon("＿" + fcard->m_name,
                          static_cast<::cw::lib::Integer>(fcard->m_level * (fcard->m_level - 1) * fcard->get_levelcoeff()));
        fcard->set_coupon("＠レベル原点", fcard->m_level);
        if (!fcard->has_coupon("＠ＥＰ")) fcard->set_coupon("＠ＥＰ", 0);
        const auto talent = fcard->get_talent();
        ::cw::lib::Integer levelmax = 10;
        for (const auto& nature : ::cw::setting::Setting::s_natures) if (nature && "＿" + nature->m_name == talent) {
            levelmax = nature->m_levelmax;
            break;
        }
        const auto effective_levelmax = std::max<::cw::lib::Integer>(levelmax, fcard->m_level);
        fcard->set_coupon("＠本来の上限", effective_levelmax);
        if (!fcard->has_coupon("＠レベル上限")) fcard->set_coupon("＠レベル上限", effective_levelmax);
        const auto gene_coupon_matches = [](const ::cw::lib::String& coupon) {
            const ::cw::lib::String prefix = "＠Ｇ";
            if (coupon.rfind(prefix, 0) != 0) return false;
            auto digits = coupon.substr(prefix.size());
            // Pythonの re.match(r"＠Ｇ\d{10}$") と同様、Unicode
            // のNd十文字と最後の一改行を許す。 元のPythonのUCD分類を再利用する。
            if (!digits.empty() && digits.back() == '\n') digits.pop_back();
            const auto codepoints = ::cw::lib::encoding::utf8_to_utf32(digits,
                ::cw::lib::encoding::ByteOrder::Little,
                ::cw::lib::encoding::ErrorPolicy::Strict, false);
            if (codepoints.size() != 40) return false;
            for (std::size_t i = 0; i < codepoints.size(); i += 4) {
                const auto cp = static_cast<std::uint32_t>(codepoints[i]) |
                    (static_cast<std::uint32_t>(codepoints[i + 1]) << 8) |
                    (static_cast<std::uint32_t>(codepoints[i + 2]) << 16) |
                    (static_cast<std::uint32_t>(codepoints[i + 3]) << 24);
                if (!::cw::lib::python_decimal_digit(cp)) return false;
            }
            return true;
        };
        bool has_gene = false;
        for (const auto& coupon : fcard->get_coupons()) if (gene_coupon_matches(coupon)) { has_gene = true; break; }
        if (!has_gene) {
            ::cw::header::Gene gene;
            gene.set_talentbit(talent);
            fcard->set_coupon("＠Ｇ" + gene.get_str(), 0);
        }
        // 必須クーポンの補填後、Python版と同じく所持カードと冒険者の
        // 画像・音声素材を宿のMaterial配下へコピーする。
        auto root = fcard->m_data_typed->getroot(); if (!root) continue;
        const auto scenario = active_scenario_data();
        const auto scedir = scenario ? scenario->m_scedir : ::cw::lib::String{};
        const auto copy_materials = [&](const std::shared_ptr<CWPyElement>& element,
                                        const ::cw::lib::String& destination, bool can_scaled) {
            if (!element) return;
            ::cw::g_cwpy->copy_materials(
                ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(element)), destination,
                true, scedir, std::nullopt, std::nullopt, false, std::nullopt, false, can_scaled);
        };
        for (const auto& cardtype : {::cw::lib::String("SkillCard"), ::cw::lib::String("ItemCard"), ::cw::lib::String("BeastCard")}) {
            const auto group = root->find(cardtype + "s");
            if (!group || !*group) continue;
            for (const auto& card : (*group)->m_children) {
                if (!card) continue;
                const auto name = ::cw::util::repl_dischar(card->gettext("Property/Name", "noname"));
                const auto destination = ::cw::lib::encoding::path_to_utf8(
                    ::std::filesystem::u8path(m_yadodir) / "Material" /
                    std::filesystem::u8path(cardtype) / std::filesystem::u8path(name));
                const auto unique_destination = ::cw::util::dupcheck_plus(destination, true, true);
                const auto can_scaled = card->getbool(".", ::cw::lib::Value("scaledimage"), false);
                copy_materials(card, unique_destination, can_scaled);
            }
        }
        const auto property = root->find("Property");
        if (!property || !*property) throw std::out_of_range("Property is missing from joined adventurer");
        {
            const auto name = fcard->m_name.empty() ? ::cw::lib::String("noname") : ::cw::util::repl_dischar(fcard->m_name);
            const auto destination = ::cw::lib::encoding::path_to_utf8(
                ::std::filesystem::u8path(m_yadodir) / "Material" / "Adventurer" /
                std::filesystem::u8path(name));
            const auto unique_destination = ::cw::util::dupcheck_plus(destination, true, true);
            const auto can_scaled = root->getbool(".", ::cw::lib::Value("scaledimage"), false);
            copy_materials(*property, unique_destination, can_scaled);
        }
        root->m_tag = "Adventurer";
        const auto name = ::cw::util::repl_dischar(fcard->m_name.empty() ? ::cw::lib::String("noname") : fcard->m_name);
        const auto path = ::cw::util::dupcheck_plus(
            ::cw::lib::encoding::path_to_utf8(outdir / std::filesystem::u8path(name + ".xml")), true, true);
        fcard->m_data_typed->write_file(path); add_standbys(path, false);
    }
    sort_standbys();
}

std::set<::cw::lib::String> YadoData::get_nowplayingpaths()
{
    std::set<::cw::lib::String> result;
    if (::cw::g_fsync) ::cw::g_fsync->sync();
    for (const auto& base : {std::filesystem::u8path(m_yadodir), std::filesystem::u8path(m_tempdir)}) {
        const auto party = base / "Party";
        if (!mounted_or_native_is_directory(party)) continue;
        for (const auto& dir : mounted_or_native_list(party)) {
            if (!mounted_or_native_is_directory(dir)) continue;
            for (const auto& file : mounted_or_native_list(dir)) {
                if (!mounted_or_native_is_file(file)) continue;
                if (file.extension() == ".wsl" && (!m_deletedpaths || !m_deletedpaths->contains(path_utf8(file)))) {
                    // Python版と同様、.wsl内の項目を正式なデータと
                    // し、不足項目・XMLエラーもその規則に従う。 隣にある
                    // 未確定のScenarioLog.xmlで代用しない。
                    const auto property = ::cw::util::get_elementfromzip(
                        path_utf8(file), "ScenarioLog.xml", "Property");
                    auto path = property->gettext("WsnPath", std::nullopt);
                    path = ::cw::util::get_linktarget(path);
                    result.insert(::cw::util::get_keypath(path));
                }
            }
        }
    }
    return result;
}

std::vector<::cw::lib::String> YadoData::get_partypaths()
{
    std::vector<::cw::lib::String> result;
    if (::cw::g_fsync) ::cw::g_fsync->sync();
    const auto dir = std::filesystem::u8path(m_yadodir) / "Party";
    for (const auto& entry : required_inn_directory_list(dir)) {
        if (!mounted_or_native_is_file(entry)) continue;
        if (path_utf8(entry.filename()).ends_with(".xml")) result.push_back(path_utf8(entry));
    }
    return result;
}

std::vector<::cw::lib::String> YadoData::get_storehousepaths()
{
    std::vector<::cw::lib::String> result;
    if (::cw::g_fsync) ::cw::g_fsync->sync();
    for (const auto& dname : {"BeastCard", "ItemCard", "SkillCard"}) {
    const auto dir = std::filesystem::u8path(m_yadodir) / dname;
        for (const auto& entry : required_inn_directory_list(dir)) {
            if (!mounted_or_native_is_file(entry)) continue;
            if (path_utf8(entry.filename()).ends_with(".xml")) result.push_back(path_utf8(entry));
        }
    }
    return result;
}

// Python版はメンバーパスを文字列のまま比較する。ネイティブはキーを字句
// 的に正規化し、 A/../Adventurer/x.xmlのような同じパ
// スで、所属済みの冒険者が待機中として出ないようにする。 リンクの解決・戻
// りパスの変更は行わず、不在のPartyHeaderはメンバーなしとする。
std::vector<::cw::lib::String> YadoData::get_standbypaths()
{
    std::vector<::cw::lib::String> result;
    if (::cw::g_fsync) ::cw::g_fsync->sync();
    std::set<::cw::lib::String> members;
    for (const auto& party : m_partys) {
        if (!party) continue;
        for (const auto& path : party->get_memberpaths(m_yadodir))
            members.insert(path_utf8(std::filesystem::u8path(path).lexically_normal()));
    }
    const auto dir = std::filesystem::u8path(m_yadodir) / "Adventurer";
    for (const auto& entry : required_inn_directory_list(dir)) {
        if (!mounted_or_native_is_file(entry)) continue;
        const auto path = ::cw::lib::encoding::path_to_utf8(entry);
        if (path_utf8(entry.filename()).ends_with(".xml") && !members.contains(path_utf8(entry.lexically_normal()))) result.push_back(path);
    }
    return result;
}

std::vector<::cw::lib::String> YadoData::get_albumpaths()
{
    std::vector<::cw::lib::String> result;
    if (::cw::g_fsync) ::cw::g_fsync->sync();
    const auto dir = std::filesystem::u8path(m_yadodir) / "Album";
    for (const auto& entry : required_inn_directory_list(dir)) {
        if (!mounted_or_native_is_file(entry)) continue;
        if (path_utf8(entry.filename()).ends_with(".xml")) result.push_back(path_utf8(entry));
    }
    return result;
}

// ネイティブのブックマークは、Pythonの呼び出し元一覧への別参照で
// はなく、ベクターの値を所有する。 Python版と同様、メモリに追
// 加して変更済みにしてからEnvironmentを編集する。 ツリー
// なしならXML側だけを省略し、メモリとXMLの一括確定は行わない。
void YadoData::add_bookmark(std::vector<::cw::lib::String> arg_spaths, ::cw::lib::String arg_path)
{
    changed();
    m_bookmarks.emplace_back(arg_spaths, arg_path);
    if (!m_environment) return;
    auto root = m_environment->getroot();
    auto bookmarks = root->find("Bookmarks");
    if (!bookmarks || !*bookmarks) {
        bookmarks = ::cw::data::make_element("Bookmarks");
        root->append(*bookmarks);
    }
    auto element = ::cw::data::make_element("Bookmark");
    for (const auto& path : arg_spaths) element->append(::cw::data::make_element("Path", path));
    element->set("path", arg_path);
    (*bookmarks)->append(element);
    m_environment->m_is_edited = true;
}

// Pythonの呼び出し元一覧の別参照ではなく、ベクターの
// 値を所有する。 先にメモリを置換して変更済みにし、Environment
// なしならXML側だけを省略する。 前のメモリ更
// 新も後のXML再構築も、一括反映は保証しない。
void YadoData::set_bookmarks(std::vector<std::tuple<std::vector<::cw::lib::String>, ::cw::lib::String>> arg_bookmarks)
{
    changed();
    m_bookmarks = std::move(arg_bookmarks);
    if (!m_environment) return;
    auto root = m_environment->getroot();
    auto bookmarks = root->find("Bookmarks");
    if (!bookmarks || !*bookmarks) {
        bookmarks = ::cw::data::make_element("Bookmarks");
        root->append(*bookmarks);
    } else {
        (*bookmarks)->clear();
    }
    for (const auto& [spaths, path] : m_bookmarks) {
        auto element = ::cw::data::make_element("Bookmark");
        for (const auto& spath : spaths) element->append(::cw::data::make_element("Path", spath));
        element->set("path", path);
        (*bookmarks)->append(element);
    }
    m_environment->m_is_edited = true;
}

// 任意のネイティブEnvironmentがなければ、保存変数の記録は返さな
// い。Python版はツリー引数を必須とする。 SavedVariables
// グループなしは両方とも空で、認識した記録は下の共通の型付き解析を使う。
std::map<std::tuple<::cw::lib::String, ::cw::lib::String>, std::tuple<std::shared_ptr<::cw::data::CWPyElement>, std::map<::cw::lib::String, bool>, std::map<::cw::lib::String, ::cw::lib::Integer>, std::map<::cw::lib::String, ::cw::lib::Value>>> YadoData::get_savedvariables(std::shared_ptr<::cw::data::CWPyElementTree> arg_environment)
{
    if (!arg_environment) return {};
    const auto data = arg_environment->find("SavedVariables");
    return data && *data ? get_savedvariables_with_scenario(*data) : decltype(get_savedvariables(arg_environment)){};
}

// 準備中のネイティブ情報はVariablesがない場合がある。ルートなし
// ・種類違いは空とし、子タグを選別して解析する。 Python版はルー
// トをassertで確認し、最初の同名グループの全子を読むが、ネイティ
// ブは一致する全グループを読む。 認識した名前付き値は型付き解析を使い
// 、不正な内容で失敗し得る。すべての不正XMLを空にするものではない。
std::tuple<std::map<::cw::lib::String, bool>, std::map<::cw::lib::String, ::cw::lib::Integer>, std::map<::cw::lib::String, ::cw::lib::Value>> YadoData::_get_vartables(std::shared_ptr<::cw::data::CWPyElement> arg_e)
{
    std::map<::cw::lib::String, bool> flags;
    std::map<::cw::lib::String, ::cw::lib::Integer> steps;
    std::map<::cw::lib::String, ::cw::lib::Value> variants;
    if (!arg_e || arg_e->m_tag != "Variables") return {std::move(flags), std::move(steps), std::move(variants)};
    for (const auto& group : arg_e->m_children) {
        if (!group) continue;
        if (group->m_tag == "Flags") {
            for (const auto& item : group->m_children) {
                if (!item || item->m_tag != "Flag") continue;
                const auto name = item->get("name", "");
                if (!name.empty()) flags[name] = item->getbool(".", ::cw::lib::Value("value"), false);
            }
        } else if (group->m_tag == "Steps") {
            for (const auto& item : group->m_children) {
                if (!item || item->m_tag != "Step") continue;
                const auto name = item->get("name", "");
                if (!name.empty()) steps[name] = item->getint(".", ::cw::lib::Value("value"), 0);
            }
        } else if (group->m_tag == "Variants") {
            for (const auto& item : group->m_children) {
                if (!item || item->m_tag != "Variant") continue;
                const auto name = item->get("name", "");
                if (!name.empty()) {
                    variants[name] = Variant::value_from_element(
                        item, "type", std::optional<::cw::lib::String>("value"));
                }
            }
        }
    }
    return {std::move(flags), std::move(steps), std::move(variants)};
}

// ネイティブの任意情報がなければ記録なしとし、nullの子は飛ばす。Python
// 版は走査可能なツリーを前提とする。 既存Variablesは同
// じキー・型付き解析経路を使い、不正な認識済みの値は失敗し得る。
std::map<::cw::lib::String, std::tuple<std::shared_ptr<::cw::data::CWPyElement>, std::map<::cw::lib::String, bool>, std::map<::cw::lib::String, ::cw::lib::Integer>, std::map<::cw::lib::String, ::cw::lib::Value>>> YadoData::get_savedvariables_with_skin(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    std::map<::cw::lib::String, std::tuple<std::shared_ptr<::cw::data::CWPyElement>, std::map<::cw::lib::String, bool>, std::map<::cw::lib::String, ::cw::lib::Integer>, std::map<::cw::lib::String, ::cw::lib::Value>>> result;
    if (!arg_data) return result;
    for (const auto& element : arg_data->m_children) {
        if (!element || element->m_tag != "Variables") continue;
        const auto key = element->get("key", "");
        auto [flags, steps, variants] = _get_vartables(element);
        result[key] = {element, std::move(flags), std::move(steps), std::move(variants)};
    }
    return result;
}

// ネイティブの任意情報がなければ記録なしとし、nullの子は飛ばす。Python
// 版は走査可能なツリーを前提とする。 両方とも、型付き解析前にVariables
// 以外と、シナリオ・作者が空のキーを無視する。
std::map<std::tuple<::cw::lib::String, ::cw::lib::String>, std::tuple<std::shared_ptr<::cw::data::CWPyElement>, std::map<::cw::lib::String, bool>, std::map<::cw::lib::String, ::cw::lib::Integer>, std::map<::cw::lib::String, ::cw::lib::Value>>> YadoData::get_savedvariables_with_scenario(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    std::map<std::tuple<::cw::lib::String, ::cw::lib::String>, std::tuple<std::shared_ptr<::cw::data::CWPyElement>, std::map<::cw::lib::String, bool>, std::map<::cw::lib::String, ::cw::lib::Integer>, std::map<::cw::lib::String, ::cw::lib::Value>>> result;
    if (!arg_data) return result;
    for (const auto& element : arg_data->m_children) {
        if (!element || element->m_tag != "Variables") continue;
        const auto scenario = element->get("scenario", "");
        const auto author = element->get("author", "");
        if (scenario.empty() && author.empty()) continue;
        auto [flags, steps, variants] = _get_vartables(element);
        result[{scenario, author}] = {element, std::move(flags), std::move(steps), std::move(variants)};
    }
    return result;
}

// Python版は新規キーならSavedVariablesを作るが、キャッシ
// ュ済みキーでは存在を要求する。 ネイティブの部分的な所有者にEnvironment
// がなければ何もしない。 コンテナー不足はキャッシュ要素を再接続して補い
// 、空の上書きなら孤立キャッシュを除く。 changed() はEnvironment
// の編集ビットだけでなく、所有する宿も保存対象にする。
void YadoData::save_variables(::cw::lib::String arg_scenario, ::cw::lib::String arg_author, bool arg_complete, std::map<::cw::lib::String, std::shared_ptr<::cw::data::Flag>> arg_flags, std::map<::cw::lib::String, std::shared_ptr<::cw::data::Step>> arg_steps, std::map<::cw::lib::String, std::shared_ptr<::cw::data::Variant>> arg_variants, std::optional<std::shared_ptr<::cw::debug::logging::DebugLog>> arg_debuglog)
{
    if (!m_environment) return;
    const auto key = std::make_tuple(arg_scenario, arg_author);
    const std::vector<::cw::lib::String> targets = arg_complete ? std::vector<::cw::lib::String>{"None"} : std::vector<::cw::lib::String>{"Complete", "None"};
    std::map<::cw::lib::String, bool> flags;
    std::map<::cw::lib::String, ::cw::lib::Integer> steps;
    std::map<::cw::lib::String, ::cw::lib::Value> variants;
    std::vector<::cw::lib::String> flag_names, step_names, variant_names;
    auto selected = [&targets](const std::vector<::cw::lib::String>& initialization) {
        return std::any_of(initialization.begin(), initialization.end(), [&targets](const auto& value) { return std::find(targets.begin(), targets.end(), value) != targets.end(); });
    };
    // Python版は辞書の値を走査し、キーではなく各オブジェ
    // クトの名前を保存する。 ネイティブの公開入力はstd::map
    // のキー順だが、その順序の中ではPython辞書と同様
    // 、 最初の名前のXML位置と、最後の値を保持する。
    for (const auto& entry : arg_flags) {
        const auto& flag = entry.second;
        if (!flag || !selected(flag->m_initialization)) continue;
        const auto& name = flag->m_name;
        if (!flags.contains(name)) flag_names.push_back(name);
        flags[name] = flag->m_value;
        if (arg_debuglog && *arg_debuglog) (*arg_debuglog)->add_flag(name);
    }
    for (const auto& entry : arg_steps) {
        const auto& step = entry.second;
        if (!step || !selected(step->m_initialization)) continue;
        const auto& name = step->m_name;
        if (!steps.contains(name)) step_names.push_back(name);
        steps[name] = step->m_value;
        if (arg_debuglog && *arg_debuglog) (*arg_debuglog)->add_step(name);
    }
    for (const auto& entry : arg_variants) {
        const auto& variant = entry.second;
        if (!variant || !selected(variant->m_initialization)) continue;
        // Variant.initialize のname引数は、Value内に保持するString。
        const auto& name = std::get<::cw::lib::String>(variant->m_name.storage());
        if (!variants.contains(name)) variant_names.push_back(name);
        variants[name] = variant->m_value;
        if (arg_debuglog && *arg_debuglog) (*arg_debuglog)->add_variant(name);
    }

    auto existing = m_saved_variables.find(key);
    auto root = m_environment->getroot();
    auto saved = root->find("SavedVariables");
    if (flags.empty() && steps.empty() && variants.empty()) {
        if (existing != m_saved_variables.end()) {
            if (saved && *saved) (*saved)->remove(std::get<0>(existing->second));
            // Python版の不具合への意図的な修正。空の上書きでXMLを消しても古い
            // キャッシュが残ると、 後の消去・削除が失敗するため、キャッシュも除く。
            m_saved_variables.erase(existing);
            if (arg_debuglog && *arg_debuglog) (*arg_debuglog)->remove_variables();
            m_environment->m_is_edited = true;
            changed();
        }
        return;
    }
    const bool rebuilt_container = !saved || !*saved;
    if (rebuilt_container) { saved = ::cw::data::make_element("SavedVariables"); root->append(*saved); }
    std::shared_ptr<::cw::data::CWPyElement> element;
    if (existing != m_saved_variables.end()) {
        element = std::get<0>(existing->second);
        if (rebuilt_container) (*saved)->append(element);
        element->clear();
    }
    else { element = ::cw::data::make_element("Variables"); (*saved)->append(element); }
    element->set("scenario", arg_scenario); element->set("author", arg_author);
    if (!flags.empty()) { auto group = ::cw::data::make_element("Flags"); for (const auto& name : flag_names) group->append(::cw::data::make_element("Flag", "", std::map<::cw::lib::String, ::cw::lib::String>{{"name", name}, {"value", flags.at(name) ? "True" : "False"}})); element->append(group); }
    if (!steps.empty()) { auto group = ::cw::data::make_element("Steps"); for (const auto& name : step_names) group->append(::cw::data::make_element("Step", "", std::map<::cw::lib::String, ::cw::lib::String>{{"name", name}, {"value", std::to_string(steps.at(name))}})); element->append(group); }
    if (!variants.empty()) { auto group = ::cw::data::make_element("Variants"); for (const auto& name : variant_names) { auto item = ::cw::data::make_element("Variant", "", std::map<::cw::lib::String, ::cw::lib::String>{{"name", name}}); Variant::value_to_element(variants.at(name), item, "type"); group->append(item); } element->append(group); }
    // Python版と同様、選択した現在値を直接公開する。 XMLを読み直
    // すと空名の除外、数値の丸め、共有Listの置換が起きるため、 読み
    // 取りの規則は後の再読み込みに残し、他のキャッシュ所有者を変えない。
    m_saved_variables.insert_or_assign(key, std::make_tuple(element, std::move(flags),
        std::move(steps), std::move(variants)));
    m_environment->m_is_edited = true;
    changed();
}

void YadoData::remove_savedvariables(::cw::lib::String arg_scenario, ::cw::lib::String arg_author)
{
    const auto key = std::make_tuple(arg_scenario, arg_author);
    const auto it = m_saved_variables.find(key);
    if (it == m_saved_variables.end() || !m_environment) return;
    const auto saved = m_environment->getroot()->find("SavedVariables");
    if (saved && *saved) (*saved)->remove(std::get<0>(it->second));
    m_saved_variables.erase(it);
    // 保存上の意図的な修正。Python版はキャッシュ・XMLの記録を除いてもEnvironment
    // を変更済みにしないが、 ここでは次の保存で削除を反映する。
    m_environment->m_is_edited = true;
    changed();
}

namespace {
// Pythonの削除集合と同様、正規化した実ファイルの同一性ではなく論理文字列を比較す
// る。 書き換えるのはTempの接頭辞だけ。 実ファイルの範囲内確認は get_yadofilepath/_transfer_temp
// などの入出力側の共通境界で行う。
::cw::lib::String deleted_set_path(::cw::lib::String path,
                                 const ::cw::lib::String& saved_root,
                                 const ::cw::lib::String& temp_root)
{
    if (path.rfind(temp_root, 0) == 0)
        path.replace(0, temp_root.size(), saved_root);
    return path;
}
} // namespace

void YadoDeletedPathSet::initialize(::cw::lib::String arg_yadodir, ::cw::lib::String arg_tempdir)
{
    m_yadodir = std::move(arg_yadodir);
    m_tempdir = std::move(arg_tempdir);
    m__s.clear();
}

void YadoDeletedPathSet::write_list()
{
    const auto directory = std::filesystem::u8path(m_tempdir);
    if (!mounted_or_native_is_directory(directory) && !mounted_or_native_mkdir(directory))
        throw std::runtime_error("DeletedPaths.tempのフォルダを作成できません: " + m_tempdir);
    const auto path = path_utf8(directory / "DeletedPaths.temp");
    ::cw::lib::String text;
    bool first = true;
    for (const auto& item : m__s) {
        if (!first) text.push_back('\n');
        text += item;
        first = false;
    }
    // Python版と同様、共通のFileSyncキューでUTF-8を書き、
    // その完了を待つ。 ネイティブ・マウント領域で同じ経路を使う。 ofstream
    // を閉じる前の同期では、そのストリーム内の保留データは待てない。
    ::cw::util::write_textfile(path, std::move(text), ::cw::g_fsync);
    if (::cw::g_fsync) ::cw::g_fsync->sync();
}

bool YadoDeletedPathSet::read_list()
{
    const auto path = std::filesystem::u8path(m_tempdir) / "DeletedPaths.temp";
    // 印が確実に存在しないと確認できた場合だけ、Tempを整理する
    // 。 入出力失敗は伝え、未確定の宿データを復旧用に保持する。
    const auto read_items = [&](std::istream& stream) {
        // Python版と同じUTF-8テキストの復号・改行・部分変更の順序を使う。
        // マウント領域とネイティブのストリームで、別規則を作らず読み取りを共有する。
        ::cw::lib::for_each_utf8_text_line(stream, [&](::cw::lib::String item) {
            if (!item.empty()) add(std::move(item));
        });
    };
    if (::cardwirth::platform::is_mounted_user_path(path)) {
        const auto status = ::cardwirth::platform::stat_mounted_user_path(path, true);
        // 暗号化の提供側は、未存在を StorageStat::Missing
        // で返す。 nullは経路確認後にマウントが閉じた可能性もある
        // ため、 不確かな復旧状態を整理してよいという判断に変えない。
        if (!status)
            throw std::runtime_error("DeletedPaths.tempの状態を確認できません: " + path_utf8(path));
        if (!status->is_file()) return false;
        const auto bytes = ::cardwirth::platform::read_mounted_user_file(path);
        if (!bytes)
            throw std::runtime_error("DeletedPaths.tempの読込みに失敗しました: " + path_utf8(path));
        ::cw::lib::String text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
        std::istringstream stream(text);
        read_items(stream);
        return true;
    }
    if (!std::filesystem::is_regular_file(path)) return false;
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        throw std::runtime_error("DeletedPaths.tempを開けません: " + path_utf8(path));
    read_items(stream);
    return true;
}

std::vector<::cw::lib::String> YadoDeletedPathSet::iterate()
{
    return {m__s.begin(), m__s.end()};
}

bool YadoDeletedPathSet::contains(::cw::lib::String arg_path)
{
    arg_path = deleted_set_path(std::move(arg_path), m_yadodir, m_tempdir);
    return m__s.find(arg_path) != m__s.end();
}

void YadoDeletedPathSet::add(::cw::lib::String arg_path, bool arg_forceyado)
{
    arg_path = deleted_set_path(std::move(arg_path), m_yadodir, m_tempdir);
    // シナリオ中の削除は宿へ即時反映せず、ScenarioLogのDeletedFiles
    // へ記録する（Python版のforceyado=False）。 ただしPython
    // 版と同様、ScenarioDataへ送るのは is_playingscenario()
    // の間だけ。 F9・終了時は is_playing を消してもsdataが残るた
    // め、 Party.wslの削除は、破棄予定のシナリオではなく宿の変更処理へ渡す。
    if (!arg_forceyado && ::cw::g_cwpy && ::cw::g_cwpy->is_playingscenario()) {
        if (auto* scenario = active_scenario_data()) {
            auto sequence = std::get_if<::cw::lib::ObjectPtr>(&scenario->m_deletedpaths.storage());
            auto values = sequence && *sequence ? std::dynamic_pointer_cast<VariantSequence>(*sequence) : nullptr;
            if (!values) {
                values = std::make_shared<VariantSequence>();
                scenario->m_deletedpaths = ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(values));
            }
            // Python版と同様、ScenarioData.deletedpathsはリストではなく集合。
            const auto found = std::find_if(values->m_values.begin(), values->m_values.end(),
                [&](const auto& value) {
                    const auto* path = std::get_if<::cw::lib::String>(&value.storage());
                    return path && *path == arg_path;
                });
            if (found == values->m_values.end()) values->m_values.emplace_back(std::move(arg_path));
            return;
        }
    }
    m__s.insert(std::move(arg_path));
}

void YadoDeletedPathSet::remove(::cw::lib::String arg_path)
{
    arg_path = deleted_set_path(std::move(arg_path), m_yadodir, m_tempdir);
    if (m__s.erase(arg_path) == 0)
        throw std::out_of_range("削除予定パスが存在しません: " + arg_path);
}

void YadoDeletedPathSet::discard(::cw::lib::String arg_path)
{
    arg_path = deleted_set_path(std::move(arg_path), m_yadodir, m_tempdir);
    m__s.erase(arg_path);
}

void YadoDeletedPathSet::update(std::vector<::cw::lib::String> arg_s)
{
    // Pythonの set.update と同様、渡された論理記録をそのまま反映する。
    m__s.insert(std::make_move_iterator(arg_s.begin()), std::make_move_iterator(arg_s.end()));
}

void YadoDeletedPathSet::clear()
{
    m__s.clear();
}

void CWPyElementParent::retain(std::shared_ptr<CWPyElement> parent)
{
    // 外側の祖先をすべて保持する必要がある。 直近の親だけを所有する
    // と、その上の祖先が弱参照で接続されている場合にルートを失う。
    std::vector<std::shared_ptr<CWPyElement>> ancestors;
    auto current = parent ? *parent->m_cwxparent : nullptr;
    while (current && current != parent &&
           std::find(ancestors.begin(), ancestors.end(), current) == ancestors.end()) {
        ancestors.push_back(current); current = *current->m_cwxparent;
    }
    context_ = std::move(parent); link_ = context_; ancestors_ = std::move(ancestors);
}

void _CWPyElementInterface::_raiseerror(::cw::lib::String arg_path, ::cw::lib::String arg_attr)
{
    auto tag = arg_path;
    auto fpath = s_fpath;
    if (const auto* element = dynamic_cast<CWPyElement*>(this)) {
        tag = element->m_tag + "/" + arg_path; fpath = element->m_fpath;
    } else if (auto* tree = dynamic_cast<CWPyElementTree*>(this)) {
        // 空のネイティブツリーは準備中のオブジェクトなので、ルート初期化前にも使え
        // る診断名を保持する。 通常のツリーはPython版のルートタグに従う。
        if (const auto root = tree->getroot()) tag = root->m_tag + "/" + arg_path;
        fpath = tree->m_fpath;
    }
    throw std::invalid_argument("Invalid XML! (file=\"" + fpath + "\", tag=\"" + tag + "\", attr=\"" + arg_attr + "\")");
}

std::optional<std::shared_ptr<::cw::data::CWPyElement>> _CWPyElementInterface::find(::cw::lib::String arg_match)
{
    // ElementTree/Treeの具象findへ仮想ディスパッチする基底既定値。
    // Pythonではこのクラスを直接生成しないため、基底既定値は未検出とする。
    (void)arg_match;
    return {};
}

bool _CWPyElementInterface::hasfind(::cw::lib::String arg_path, ::cw::lib::String arg_attr)
{
    const auto element = find(std::move(arg_path));
    if (!element) return false;
    return arg_attr.empty() || (*element)->m_attributes.contains(arg_attr);
}

::cw::lib::Value _CWPyElementInterface::getfind(::cw::lib::String arg_path, bool arg_raiseerror)
{
    const auto element = find(arg_path);
    if (!element) {
        if (arg_raiseerror) _raiseerror(std::move(arg_path), {});
        // ネイティブの意図的な表現差として、例外を出さない検索の空結果
        // はNoneにする。 Python版は空タプルだが、ネイティブ
        // 側はValueから任意のElementを取り出すため、 false
        // となり可変XMLコンテナーと混同しない表現を使う。
        return {};
    }
    return ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(*element));
}

::cw::lib::String _CWPyElementInterface::gettext(::cw::lib::String arg_path, std::optional<::cw::lib::String> arg_py_default)
{
    const auto element = find(arg_path);
    if (element) return (*element)->m_text;
    if (arg_py_default) return *arg_py_default;
    _raiseerror(std::move(arg_path), {});
}

::cw::lib::String _CWPyElementInterface::getattr(::cw::lib::String arg_path, ::cw::lib::String arg_attr, std::optional<::cw::lib::String> arg_py_default)
{
    const auto element = find(arg_path);
    if (element) {
        const auto it = (*element)->m_attributes.find(arg_attr);
        if (it != (*element)->m_attributes.end()) return it->second;
    }
    if (arg_py_default) return *arg_py_default;
    _raiseerror(std::move(arg_path), std::move(arg_attr));
}

bool _CWPyElementInterface::getbool(::cw::lib::String arg_path, std::optional<::cw::lib::Value> arg_attr, std::optional<bool> arg_py_default)
{
    ::cw::lib::String attr;
    bool positional_default = false;
    if (arg_attr) {
        if (const auto* b = std::get_if<bool>(&arg_attr->storage())) {
            arg_py_default = *b;
            positional_default = true;
        }
        else if (const auto* s = std::get_if<::cw::lib::String>(&arg_attr->storage())) attr = *s;
    }
    // Python版と同様、位置引数のbool既定値を渡す形式では、変換前
    // にattrを空にする。 空でない不正テキストの診断も同じ規則にする。
    const auto error_attr = positional_default || (arg_attr && std::holds_alternative<::cw::lib::String>(arg_attr->storage())) ? attr : "None";
    const auto value = attr.empty() ? gettext(arg_path, ::cw::lib::String{}) : getattr(arg_path, attr, ::cw::lib::String{});
    if (value.empty()) {
        if (arg_py_default) return *arg_py_default;
        _raiseerror(std::move(arg_path), error_attr);
    }
    const auto normalized = scenario_lower(value);
    if (normalized == "true" || normalized == "1") return true;
    if (normalized == "false" || normalized == "0") return false;
    _raiseerror(std::move(arg_path), error_attr);
}

::cw::lib::Integer _CWPyElementInterface::getint(::cw::lib::String arg_path, std::optional<::cw::lib::Value> arg_attr, std::optional<::cw::lib::Integer> arg_py_default)
{
    ::cw::lib::String attr;
    bool positional_default = false;
    if (arg_attr) {
        if (const auto* n = std::get_if<::cw::lib::Integer>(&arg_attr->storage())) {
            arg_py_default = *n;
            positional_default = true;
        }
        else if (const auto* b = std::get_if<bool>(&arg_attr->storage())) {
            // Pythonのboolはintでもあるため、位置引数の既定値の形式
            // に対応させる。 ネイティブのgetintの戻り型では0/1で表す。
            arg_py_default = *b ? 1 : 0;
            positional_default = true;
        }
        else if (const auto* s = std::get_if<::cw::lib::String>(&arg_attr->storage())) attr = *s;
    }
    // Python版と同様、getint(path, integer_default) ではattr
    // を空にする。 属性を省略し別に既定値を渡す場合は、診断に str(None) を残す。
    const auto error_attr = positional_default || (arg_attr && std::holds_alternative<::cw::lib::String>(arg_attr->storage())) ? attr : "None";
    const auto value = attr.empty() ? gettext(arg_path, ::cw::lib::String{}) : getattr(arg_path, attr, ::cw::lib::String{});
    if (value.empty()) {
        if (arg_py_default) return *arg_py_default;
        _raiseerror(std::move(arg_path), error_attr);
    }
    try {
        return parse_python_integer_float(value);
    }
    catch (...) { _raiseerror(std::move(arg_path), error_attr); }
}

::cw::lib::Real _CWPyElementInterface::getfloat(::cw::lib::String arg_path, std::optional<::cw::lib::Value> arg_attr, std::optional<::cw::lib::Real> arg_py_default)
{
    ::cw::lib::String attr;
    bool positional_default = false;
    if (arg_attr) {
        if (const auto* n = std::get_if<::cw::lib::Real>(&arg_attr->storage())) {
            arg_py_default = *n;
            positional_default = true;
        }
        else if (const auto* s = std::get_if<::cw::lib::String>(&arg_attr->storage())) attr = *s;
    }
    // Python版の位置引数のfloat既定値の形式を、attrが空になる点も含めて維持する。
    const auto error_attr = positional_default || (arg_attr && std::holds_alternative<::cw::lib::String>(arg_attr->storage())) ? attr : "None";
    const auto value = attr.empty() ? gettext(arg_path, ::cw::lib::String{}) : getattr(arg_path, attr, ::cw::lib::String{});
    if (value.empty()) {
        if (arg_py_default) return *arg_py_default;
        _raiseerror(std::move(arg_path), error_attr);
    }
    try { return parse_python_float(value); }
    catch (...) { _raiseerror(std::move(arg_path), error_attr); }
}

std::shared_ptr<::cw::data::CWPyElement> _CWPyElementInterface::make_element(::cw::lib::String arg_name, ::cw::lib::String arg_text, std::optional<std::map<::cw::lib::String, ::cw::lib::String>> arg_attrs, ::cw::lib::String arg_tail)
{
    return std::make_shared<CWPyElement>(std::move(arg_name), std::move(arg_text),
        arg_attrs.value_or(std::map<::cw::lib::String, ::cw::lib::String>{}), std::move(arg_tail));
}

void _CWPyElementIterator::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_e)
{
    m_e = std::move(arg_e);
    m__iter = 0;
}

// ネイティブの走査結果は子ベクターの複製。 新しいイテレーターを返すPython
// の __iter__ と異なり、明示的な next() の位置を戻さない。
std::vector<std::shared_ptr<::cw::data::CWPyElement>> _CWPyElementIterator::iterate()
{
    if (!m_e) return {};
    return m_e->m_children;
}

// ネイティブの明示的な next() はPython版と同様、現在の子ベクタ
// ーを読む。 ただし戻り型はイテレータープロトコルではなく要素なので、終端に
// はStopIterationではなく out_of_range を使う。
std::shared_ptr<::cw::data::CWPyElement> _CWPyElementIterator::next()
{
    if (!m_e || m__iter < 0 || m__iter >= static_cast<::cw::lib::Integer>(m_e->m_children.size()))
        throw std::out_of_range("XML iterator exhausted");
    return m_e->m_children[static_cast<std::size_t>(m__iter++)];
}

void _ElementWrapper::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_wrap, ::cw::lib::String arg_tag, std::map<::cw::lib::String, ::cw::lib::String> arg_attrib)
{
    if (arg_wrap) m_wrap = ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(arg_wrap));
    // ネイティブの互換ラッパーは、タグ・属性を値で複製する。 PythonのcElementTree
    // のビューと異なり、その編集は元のCWPyElementへ書き戻
    // さない。 XMLの変更にはCWPyElementを直接使う。
    m_tag = arg_wrap ? arg_wrap->m_tag : std::move(arg_tag);
    m_attrib = arg_wrap ? arg_wrap->m_attributes : std::move(arg_attrib);
}

CWPyElement::CWPyElement(::cw::lib::String tag, ::cw::lib::String text,
                         ::cw::lib::XmlAttributes attributes,
                         ::cw::lib::String tail)
    : m_tag(std::move(tag)), m_text(std::move(text)), m_tail(std::move(tail)),
      m_attributes(std::move(attributes)) {}

void CWPyElement::initialize(::cw::lib::String arg_tag,
                             std::optional<std::map<::cw::lib::String, ::cw::lib::String>> arg_attrib) {
    m_tag = std::move(arg_tag);
    m_attributes = arg_attrib.value_or(std::map<::cw::lib::String, ::cw::lib::String>{});
    // Pythonの __init__ と同様、要素と全イベント・パス情報を置き換え
    // る。 ネイティブの保存領域がXMLを直接所有し、ラッパーの循環参照は作らない。
    m_text.clear();
    m_tail.clear();
    m_children.clear();
    m_parent.reset();
    m_element.reset();
    m_fpath.clear();
    m__iter = 0;
    m_cwxparent.reset();
    m_content.reset();
    m_nextelements.reset();
    m_needcheck.reset();
    m_cwxpath.reset();
    m__cwxline_index.reset();
}

::cw::lib::String CWPyElement::get_tag() { return m_tag; }
void CWPyElement::set_tag(::cw::lib::String arg_tag) { m_tag = std::move(arg_tag); }

std::optional<std::shared_ptr<::cw::data::CWPyElement>> CWPyElement::find(::cw::lib::String arg_path) {
    const auto found = ::cw::lib::element_path::find(shared_from_this(), arg_path);
    return found ? std::optional<std::shared_ptr<CWPyElement>>(found) : std::nullopt;
}

std::shared_ptr<::cw::data::CWPyElement> CWPyElement::find_exists(::cw::lib::String arg_path) {
    const auto result = find(arg_path);
    if (result) return *result;
    auto tag = m_tag;
    auto ancestor = shared_from_this();
    while (const auto parent = *ancestor->m_cwxparent) {
        ancestor = parent; tag = ancestor->m_tag + "/" + tag;
    }
    throw std::invalid_argument(ancestor->m_fpath + ":" + tag + ":" + arg_path + " is not found.");
}

::cw::lib::XmlAttributes CWPyElement::get_attrib() { return m_attributes; }
::cw::lib::String CWPyElement::get_text() { return m_text; }
void CWPyElement::set_text(::cw::lib::String arg_text) { m_text = std::move(arg_text); }
::cw::lib::String CWPyElement::get_tail() { return m_tail; }
void CWPyElement::set_tail(::cw::lib::String arg_tail) { m_tail = std::move(arg_tail); }

::cw::lib::String CWPyElement::get(::cw::lib::String arg_key,
                                   ::cw::lib::String arg_defaultvalue) {
    const auto found = m_attributes.find(arg_key);
    return found == m_attributes.end() ? std::move(arg_defaultvalue) : found->second;
}

void CWPyElement::set(::cw::lib::String arg_key, ::cw::lib::String arg_value) {
    m_attributes[std::move(arg_key)] = std::move(arg_value);
}

::cw::lib::Value CWPyElement::keys() {
    // ネイティブのValue列は、Pythonの動的な辞書ビューではなくスナップシ
    // ョット。 保持した呼び出し元は、属性変更後に新しい列を取得する必要がある。
    auto result = std::make_shared<VariantSequence>();
    result->m_values.reserve(m_attributes.size());
    for (const auto& key : m_attributes.insertion_keys()) {
        result->m_values.emplace_back(key);
    }
    return ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(result));
}

::cw::lib::Value CWPyElement::items() {
    // 各組の値も含め、keys()と同じネイティブのスナップショット形式を使
    // う。 Pythonの動的なdict_itemsの別参照は保持しない。
    auto result = std::make_shared<VariantSequence>();
    result->m_values.reserve(m_attributes.size());
    for (const auto& key : m_attributes.insertion_keys()) {
        const auto& value = m_attributes.at(key);
        auto pair = std::make_shared<StructVal>();
        pair->initialize({}, {::cw::lib::Value(key), ::cw::lib::Value(value)});
        result->m_values.emplace_back(
            ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(pair)));
    }
    return ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(result));
}

void CWPyElement::append(std::shared_ptr<::cw::data::CWPyElement> arg_subelement) {
    if (!arg_subelement) throw std::invalid_argument("cannot append null XML element");
    arg_subelement->m_cwxparent.attach(shared_from_this());
    arg_subelement->m_parent = shared_from_this();
    m_children.push_back(std::move(arg_subelement));
}

// ネイティブは実体化済みのベクターを受け取り、親の処理と子の挿入で再利用す
// る。 Python版は一度限りの反復可能オブジェクトも受け付け、親の処理
// で使い切る場合があるが、 そのジェネレーター固有の動作はベクターAPIで
// は公開しない。 親を子より先に反映する順序はPython版に合わせる。
void CWPyElement::extend(std::vector<std::shared_ptr<::cw::data::CWPyElement>> arg_subelements) {
    // Python版と同様、子の列を拡張する前に、すべての論理的な親を反映する。
    for (const auto& element : arg_subelements) {
        if (!element) throw std::invalid_argument("cannot extend with null XML element");
        element->m_cwxparent.attach(shared_from_this()); element->m_parent = shared_from_this();
    }
    m_children.insert(m_children.end(), arg_subelements.begin(), arg_subelements.end());
}

void CWPyElement::insert(::cw::lib::Integer arg_index,
                         std::shared_ptr<::cw::data::CWPyElement> arg_subelement) {
    if (!arg_subelement) throw std::invalid_argument("cannot insert null XML element");
    auto index_value = arg_index < 0 ? static_cast<::cw::lib::Integer>(m_children.size()) + arg_index : arg_index;
    index_value = std::clamp<::cw::lib::Integer>(index_value, 0,
                                                 static_cast<::cw::lib::Integer>(m_children.size()));
    arg_subelement->m_cwxparent.attach(shared_from_this());
    arg_subelement->m_parent = shared_from_this();
    m_children.insert(m_children.begin() + static_cast<std::ptrdiff_t>(index_value),
                      std::move(arg_subelement));
}

void CWPyElement::remove(std::shared_ptr<::cw::data::CWPyElement> arg_subelement) {
    // Python版と同様、論理的な親の前提を確認してから、元の子の検索が失敗して
    // も切り離す。 C++版はそのassert違反をlogic_errorで表す。
    if (!arg_subelement || *arg_subelement->m_cwxparent != shared_from_this())
        throw std::logic_error("");
    arg_subelement->m_cwxparent.reset(); arg_subelement->m_parent.reset();
    const auto found = std::find(m_children.begin(), m_children.end(), arg_subelement);
    if (found == m_children.end()) throw std::invalid_argument("list.remove(x): x not in list");
    m_children.erase(found);
}

void CWPyElement::clear() {
    for (auto& child : m_children) if (child) { child->m_cwxparent.reset(); child->m_parent.reset(); }
    m_children.clear();
    m_attributes.clear();
    m_text.clear();
    m_tail.clear();
}

::cw::lib::Integer CWPyElement::index(std::shared_ptr<::cw::data::CWPyElement> arg_subelement,
                                     std::optional<::cw::lib::Integer> arg_start,
                                     std::optional<::cw::lib::Integer> arg_end) {
    // Pythonのラッパーと同様、list.indexではなく range(start,
    // end) を走査する。 元の負の位置を結果に残し、実際に到達した時点だけで例外を出す。
    const auto start = arg_start.value_or(0);
    const auto end = arg_end.value_or(static_cast<::cw::lib::Integer>(m_children.size()));
    for (auto i = start; i < end; ++i)
        if (get_item(i) == arg_subelement) return i;
    return -1;
}

::cw::lib::String CWPyElement::get_cwxpath() {
    if (m_cwxpath) return *m_cwxpath;
    std::vector<::cw::lib::String> parts;
    auto current = shared_from_this();
    bool scenariodata = false;
    while (current) {
        const auto parent = *current->m_cwxparent;
        const auto index_in_parent = [&]() -> ::cw::lib::Integer {
            if (!parent) throw std::invalid_argument(current->m_tag);
            return parent->index(current, std::nullopt, std::nullopt);
        };
        if (current->m_attributes.contains("cwxpath")) {
            parts.push_back(current->get("cwxpath", "")); scenariodata = true; break;
        }
        if (current->m_tag == "Area" || current->m_tag == "Battle" || current->m_tag == "Package" || current->m_tag == "CastCard") {
            const auto prefix = current->m_tag == "Area" ? "area" : current->m_tag == "Battle" ? "battle" : current->m_tag == "Package" ? "package" : "castcard";
            parts.push_back(prefix + ::cw::lib::String(":id:") + current->gettext("Property/Id", "0")); scenariodata = true;
            if (current->m_tag == "CastCard") break;
        } else if (current->m_tag == "SkillCard" || current->m_tag == "ItemCard" || current->m_tag == "BeastCard") {
            const auto prefix = current->m_tag == "SkillCard" ? "skillcard" : current->m_tag == "ItemCard" ? "itemcard" : "beastcard";
            parts.push_back(prefix + ::cw::lib::String(":id:") + current->gettext("Property/Id", "0"));
            if (current->getbool(".", ::cw::lib::Value("scenariocard"), false)) { scenariodata = true; break; }
        } else if (current->m_tag == "MenuCard" || current->m_tag == "LargeMenuCard") {
            parts.push_back("menucard:" + std::to_string(index_in_parent()));
        } else if (current->m_tag == "EnemyCard") {
            parts.push_back("enemycard:" + std::to_string(index_in_parent()));
        } else if (current->m_tag == "Event") {
            parts.push_back("event:" + std::to_string(index_in_parent()));
        } else if (current->m_tag == "Motion") {
            parts.push_back("motion:" + std::to_string(index_in_parent()));
        } else if (current->m_tag == "PlayerCardEvents") {
            parts.push_back("playercard:" + std::to_string(index_in_parent()));
        } else if (current->m_tag == "Adventurer" || current->m_tag == "CastCards" || current->m_tag == "System" || current->m_tag == "ActionCard") {
            break;
        } else if (current->m_tag == "SkillCards" || current->m_tag == "ItemCards" || current->m_tag == "BeastCards" ||
                   current->m_tag == "Beasts" || current->m_tag == "Motions" || current->m_tag == "Contents" ||
                   current->m_tag == "Events" || current->m_tag == "MenuCards" || current->m_tag == "EnemyCards") {
            // コンテナーだけを対象にする。
        } else {
            if (!parent) throw std::invalid_argument(current->m_tag);
            if (parent->m_tag != "Contents" && parent->m_tag != "ContentsLine")
                throw std::invalid_argument(parent->m_tag + "/" + current->m_tag);
            if (parent->m_tag == "ContentsLine") {
                if (!current->m__cwxline_index) for (std::size_t i = 0; i < parent->m_children.size(); ++i)
                    if (parent->m_children[i]) parent->m_children[i]->m__cwxline_index = static_cast<::cw::lib::Integer>(i);
                for (::cw::lib::Integer i = 0; i < current->m__cwxline_index.value_or(0); ++i) parts.emplace_back(":0");
            } else parts.push_back(":" + std::to_string(index_in_parent()));
        }
        current = parent;
    }
    if (!scenariodata) { m_cwxpath = ::cw::lib::String{}; return {}; }
    std::reverse(parts.begin(), parts.end());
    ::cw::lib::String result;
    bool first = true;
    for (const auto& part : parts) { if (!first) result += '/'; first = false; result += part; }
    m_cwxpath = result;
    return result;
}

std::vector<std::shared_ptr<::cw::data::CWPyElement>> CWPyElement::iter(
    std::optional<::cw::lib::String> arg_tag) {
    std::vector<std::shared_ptr<CWPyElement>> result;
    const auto self = shared_from_this();
    if (!arg_tag || m_tag == *arg_tag) result.push_back(self);
    for (const auto& child : m_children) {
        if (!child) continue;
        auto descendants = child->iter(arg_tag);
        result.insert(result.end(), descendants.begin(), descendants.end());
    }
    return result;
}

::cw::lib::String CWPyElement::representation() {
    // Python版がElement.__repr__へ委譲するのと同様、 表現
    // の生成でシナリオパスを解決したり、要素の接続・共有所有を要求したりしない。
    std::ostringstream address;
    address << "0x" << std::hex << reinterpret_cast<std::uintptr_t>(this);
    return "<Element " + ::cw::lib::python_percent_detail::repr(m_tag, false) +
           " at " + address.str() + ">";
}

::cw::lib::Integer CWPyElement::size() {
    return static_cast<::cw::lib::Integer>(m_children.size());
}
bool CWPyElement::to_bool() {
    // 言語境界として、Pythonの __bool__ と同様、子数にかかわらずValueError
    // 相当を出す。 ポインターの存在確認はこのAPIとは分ける。
    throw std::invalid_argument("CWPyElement truth-value testing is forbidden");
}

std::shared_ptr<::cw::data::CWPyElement> CWPyElement::get_item(::cw::lib::Integer arg_index) {
    auto index_value = arg_index < 0 ? static_cast<::cw::lib::Integer>(m_children.size()) + arg_index : arg_index;
    if (index_value < 0 || index_value >= static_cast<::cw::lib::Integer>(m_children.size())) {
        throw std::out_of_range("XML child index out of range");
    }
    return m_children[static_cast<std::size_t>(index_value)];
}

namespace {
std::vector<::cw::lib::Integer> element_slice_positions(const ::cw::lib::Value& value, ::cw::lib::Integer size) {
    // ネイティブの公開スライスは VariantSequence[start,stop,step]
    // で表し、空のValueをPythonのNoneに対応させる。 負のstepの
    // 正規化・方針は削除と共有し、親の解除が実際の選択位置を対象にするようにする。
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
    const auto slice = object && *object ? std::dynamic_pointer_cast<VariantSequence>(*object) : nullptr;
    if (!slice) throw std::invalid_argument("unsupported XML slice type");
    const auto read = [&](std::size_t i) -> std::optional<::cw::lib::Integer> {
        if (i >= slice->m_values.size() || std::holds_alternative<std::monostate>(slice->m_values[i].storage())) return {};
        if (const auto* n = std::get_if<::cw::lib::Integer>(&slice->m_values[i].storage())) return *n;
        throw std::invalid_argument("slice indices must be integers or None");
    };
    const auto step = read(2).value_or(1);
    if (step == 0) throw std::invalid_argument("slice step cannot be zero");
    const auto normalize = [&](std::optional<::cw::lib::Integer> n, bool start) {
        if (!n) return step > 0 ? (start ? ::cw::lib::Integer{0} : size) : (start ? size - 1 : ::cw::lib::Integer{-1});
        auto v = *n; if (v < 0) v += size;
        return step > 0 ? std::clamp(v, ::cw::lib::Integer{0}, size) : std::clamp(v, ::cw::lib::Integer{-1}, size - 1);
    };
    const auto start = normalize(read(0), true), stop = normalize(read(1), false);
    std::vector<::cw::lib::Integer> positions;
    for (auto i = start; step > 0 ? i < stop : i > stop;) {
        positions.push_back(i);
        // 大きい有効なスライス間隔でも、Integerのオーバーフローを避ける。
        if (step > 0 ? step > stop - 1 - i : step < stop + 1 - i) break;
        i += step;
    }
    return positions;
}
}

std::vector<std::shared_ptr<::cw::data::CWPyElement>> CWPyElement::get_item_2(::cw::lib::Value arg_index) {
    std::vector<std::shared_ptr<CWPyElement>> result;
    for (const auto index : element_slice_positions(arg_index, static_cast<::cw::lib::Integer>(m_children.size())))
        result.push_back(m_children[static_cast<std::size_t>(index)]);
    return result;
}

::cw::lib::Value CWPyElement::get_item_3(::cw::lib::Value arg_index) {
    if (const auto* value = std::get_if<::cw::lib::Integer>(&arg_index.storage())) {
        return ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(get_item(*value)));
    }
    throw std::invalid_argument("unsupported XML index type");
}

void CWPyElement::set_item(::cw::lib::Integer arg_index,
                           std::shared_ptr<::cw::data::CWPyElement> arg_element) {
    const auto old = get_item(arg_index);
    if (!arg_element) throw std::invalid_argument("cannot assign null XML element");
    const auto index_value = arg_index < 0 ? static_cast<::cw::lib::Integer>(m_children.size()) + arg_index : arg_index;
    // 二段階の兄弟交換ができるよう、同じ親の子を代入する場合は、置換される子の親リンクを保持する。
    if (*arg_element->m_cwxparent != shared_from_this()) {
        old->m_cwxparent.reset(); old->m_parent.reset();
    }
    arg_element->m_cwxparent.attach(shared_from_this()); arg_element->m_parent = shared_from_this();
    m_children[static_cast<std::size_t>(index_value)] = std::move(arg_element);
}

void CWPyElement::delete_item(::cw::lib::Value arg_index) {
    if (const auto* value = std::get_if<::cw::lib::Integer>(&arg_index.storage())) {
        const auto element = get_item(*value);
        const auto index = *value < 0 ? static_cast<::cw::lib::Integer>(m_children.size()) + *value : *value;
        element->m_cwxparent.reset(); element->m_parent.reset();
        m_children.erase(m_children.begin() + static_cast<std::ptrdiff_t>(index));
        return;
    }
    auto positions = element_slice_positions(arg_index, static_cast<::cw::lib::Integer>(m_children.size()));
    for (const auto index : positions) { const auto& element = m_children[static_cast<std::size_t>(index)]; element->m_cwxparent.reset(); element->m_parent.reset(); }
    std::sort(positions.begin(), positions.end(), std::greater<>{});
    for (const auto index : positions) m_children.erase(m_children.begin() + static_cast<std::ptrdiff_t>(index));
}

// 子の挿入・削除で無効になるベクターのイテレーターを保持しないよ
// う、構造の複製を返す。 Python版はnextごとに現在の
// 要素を読むが、ネイティブの複製内の子オブジェクトは共有する。
std::vector<std::shared_ptr<::cw::data::CWPyElement>> CWPyElement::iterate() { return m_children; }

// 子への参照一覧のスナップショットを作って逆
// 順にする。子オブジェクト自体は複製しない。
std::vector<std::shared_ptr<::cw::data::CWPyElement>> CWPyElement::py_reversed() {
    auto result = m_children;
    std::reverse(result.begin(), result.end());
    return result;
}

CWPyElementTree::CWPyElementTree(std::shared_ptr<::cw::data::CWPyElement> root,
                                 ::cw::lib::String fpath)
    : m_root_node(std::move(root)) {
    // ネイティブの書き込み側が明示的な保存先でツリーを作れるAPI拡張を維持す
    // る。 省略時はPython版と同様に要素の識別情報を引き継ぐ。 空の既
    // 定ツリーは準備中のオブジェクトで、initializeがルートを読む。
    if (!m_root_node && !fpath.empty())
        m_root_node = xml2element(fpath, "", std::nullopt, std::nullopt, false, std::nullopt);
    m_fpath = fpath.empty() && m_root_node ? m_root_node->m_fpath : std::move(fpath);
    const auto path = ::cw::lib::encoding::path_from_utf8(m_fpath);
    m_user_storage_backed = ::cardwirth::platform::is_mounted_user_path(path);
}

void CWPyElementTree::initialize(::cw::lib::String arg_fpath,
                                 std::optional<std::shared_ptr<::cw::data::CWPyElement>> arg_element) {
    auto root = arg_element && *arg_element ? *arg_element
        : xml2element(arg_fpath, "", std::nullopt, std::nullopt, false, std::nullopt);
    if (!root) throw std::runtime_error("XML tree has no root");
    // ルート由来のファイル識別情報も含め、読み込み成功後にだけ反映する。
    const auto fpath = root->m_fpath;
    const bool storage_backed = ::cardwirth::platform::is_mounted_user_path(
        ::cw::lib::encoding::path_from_utf8(fpath));
    m_root_node = std::move(root);
    m_fpath = fpath;
    m_user_storage_backed = storage_backed;
    m_is_edited = false;
}

std::shared_ptr<::cw::data::CWPyElement> CWPyElementTree::getroot() { return m_root_node; }

// ネイティブのツリー走査は、Pythonの遅延反復ではなく
// 、深さ優先で実体化した要素ベクターを返す。 共有する子を
// 深く複製はしない。 空の準備中ツリーには走査対象がない。
std::vector<std::shared_ptr<::cw::data::CWPyElement>> CWPyElementTree::iter() {
    return m_root_node ? m_root_node->iter(std::nullopt) : std::vector<std::shared_ptr<CWPyElement>>{};
}

std::optional<std::shared_ptr<::cw::data::CWPyElement>> CWPyElementTree::find(::cw::lib::String arg_path) {
    return m_root_node ? m_root_node->find(std::move(arg_path)) : std::nullopt;
}

std::shared_ptr<::cw::data::CWPyElement> CWPyElementTree::find_exists(::cw::lib::String arg_path) {
    if (!m_root_node) throw std::out_of_range("XML tree has no root");
    return m_root_node->find_exists(std::move(arg_path));
}

::cw::lib::Bytes CWPyElementTree::write_bytes() {
    // ElementTreeと同様、既存のtext・tailはそのまま書く
    // 。 整形で空の項目は埋めても、混在するXML内容へ空白を挿入しない。
    form_element(m_root_node, 0);
    std::ostringstream output;
    output << "<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n";
    write_element_xml(output, m_root_node);
    const auto text = output.str();
    return ::cw::lib::Bytes(text.begin(), text.end());
}

void CWPyElementTree::write_file(::cw::lib::String arg_path) {
    if (arg_path.empty()) arg_path = m_fpath;
    if (arg_path.empty()) throw std::runtime_error("XML output path is empty");
    const auto bytes = write_bytes();
    const auto output_path = ::cw::lib::encoding::path_from_utf8(arg_path);
    const auto mounted_output =
        ::cardwirth::platform::resolve_mounted_user_path(output_path);
    if (mounted_output) {
        // ツリーはメモリ内で空のm_fpathで作られ、ScenarioLog
        // ・荷物袋・生成カードなど、ここで初めて保存先を得る
        // 場合がある。 構築時にキャッシュしたフラグでバック
        // エンドを決めず、毎回実際の保存先から振り分ける。
        const auto& resolved = *mounted_output;
        const auto source = std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(bytes.data()), bytes.size());
        if (!::cardwirth::platform::write_mounted_user_file(resolved, source, true))
            throw std::runtime_error("failed to write encrypted XML output: " + arg_path);
    } else {
        const std::filesystem::path path = output_path;
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        // Python版と同様、XMLは util.write_file(..., cw.fsync)
        // を通す。 キューを迂回すると、以前に予約した書き込みがこの新しいXMLを上書きしてしまう。
        ::cw::util::write_file(arg_path, ::cw::lib::Value(bytes), ::cw::g_fsync,
                              "wb", std::nullopt);
    }
    // Python版のwrite_fileと同様、バイト列の出力だけを行う。 ツ
    // リーの保存先変更や編集済み状態の解除はせず、write_xmlに任せる。
}

// ネイティブの一時保存は、Pythonのstartswith・最初の部分文字
// 列置換ではなく、 正規化した絶対・マウントルートをディレクトリ境界で照合す
// る。 Yado2などの兄弟名の誤転送を防ぎ、相対宿ルートに対する絶対読み
// 込みパスも扱う。 字句的な判定であり、シンボリックリンクの検査ではない。
void CWPyElementTree::write_xml(bool arg_nocheck_edited) {
    if (!arg_nocheck_edited && !m_is_edited) return;
    if (m_fpath.empty()) throw std::runtime_error("XML output path is empty");
    if (::cw::g_cwpy && !::cw::g_cwpy->m_yadodir.empty() && !::cw::g_cwpy->m_tempdir.empty()) {
        // CWPyの宿ルートが相対でも、読み取りは絶対パスを返し得る。 パーティの
        // 読み込み解除は一時保存であり、永続保存ではない。宿の全XMLに適用する。
        if (const auto paths = ::cw::util::resolve_yado_paths(
                m_fpath, ::cw::g_cwpy->m_yadodir, ::cw::g_cwpy->m_tempdir)) {
            const auto old_path = m_fpath;
            m_fpath = ::cw::util::get_yado_writepath(
                m_fpath, ::cw::g_cwpy->m_yadodir, ::cw::g_cwpy->m_tempdir);
#if defined(CARDWIRTH_LOG_ENABLED)
            if (old_path != m_fpath)
                ::cardwirth::debug::log("XML write redirected to Temp: from=" +
                                        old_path + " to=" + m_fpath);
#endif
        }
    }
    write_file(m_fpath);
    m_is_edited = false;
}

void CWPyElementTree::edit(::cw::lib::String arg_path, ::cw::lib::String arg_value,
                           std::optional<::cw::lib::String> arg_attrname) {
    auto element = find(arg_path).value_or(nullptr);
    if (!element) throw std::invalid_argument(arg_path);
    if (arg_attrname && !arg_attrname->empty()) element->set(*arg_attrname, std::move(arg_value));
    else element->set_text(std::move(arg_value));
    m_is_edited = true;
}

void CWPyElementTree::append(::cw::lib::String arg_path,
                             std::shared_ptr<::cw::data::CWPyElement> arg_element) {
    const auto element = find(arg_path).value_or(nullptr);
    if (!element) throw std::invalid_argument(arg_path);
    element->append(std::move(arg_element));
    m_is_edited = true;
}

void CWPyElementTree::insert(::cw::lib::String arg_path,
                             std::shared_ptr<::cw::data::CWPyElement> arg_element,
                             ::cw::lib::Integer arg_index) {
    const auto element = find(arg_path).value_or(nullptr);
    if (!element) throw std::invalid_argument(arg_path);
    element->insert(arg_index, std::move(arg_element));
    m_is_edited = true;
}

void CWPyElementTree::remove(::cw::lib::String arg_path,
                             std::optional<std::shared_ptr<::cw::data::CWPyElement>> arg_element,
                             std::optional<::cw::lib::String> arg_attrname) {
    auto target = find(arg_path).value_or(nullptr);
    if (!target) throw std::invalid_argument(arg_path);
    if (arg_attrname && !arg_attrname->empty()) {
        // Pythonの del attrib[name] と同様、名前がなければKeyError
        // 相当とし、is_editedは変えない。 空の名前は要素削除の分岐へ進む。
        if (target->m_attributes.erase(*arg_attrname) == 0)
            throw std::out_of_range(*arg_attrname);
    }
    else if (arg_element) target->remove(*arg_element);
    else throw std::invalid_argument("remove requires an element or attribute");
    m_is_edited = true;
}

void CWPyElementTree::form_element(std::shared_ptr<::cw::data::CWPyElement> arg_element,
                                   ::cw::lib::Integer arg_depth) {
    if (!arg_element) return;
    // Python版と同様、負の深さでの文字列反復はゼロ回にする。 tail・
    // 再帰の意味用に符号付きの深さは保つが、巨大なsize_tへ変換しない。
    const auto indent = ::cw::lib::String(static_cast<std::size_t>(std::max<::cw::lib::Integer>(0, arg_depth)), ' ');
    const auto child_indent = "\n" + indent + " ";
    if (!arg_element->m_children.empty()) {
        if (arg_element->m_text.empty()) arg_element->m_text = child_indent;
        if (arg_element->m_tail.empty()) arg_element->m_tail = arg_depth ? "\n" + indent : "";
        for (const auto& child : arg_element->m_children) form_element(child, arg_depth + 1);
        const auto& last_child = arg_element->m_children.back();
        if (last_child && last_child->m_tail.empty()) last_child->m_tail = "\n" + indent;
    } else {
        if (arg_element->m_text.empty()) arg_element->m_text = "";
        if (arg_element->m_tail.empty()) arg_element->m_tail = arg_depth ? "\n" + indent : "";
    }
}

void ScenarioData::initialize(std::shared_ptr<::cw::header::ScenarioHeader> arg_header, bool arg_cardonly)
{
    m_extracted_archive_root.clear();
    reset_system_data_storage(*this);
    if (!arg_header) {
        log_scenario_data("initialize skipped: header=null");
        return;
    }

    // Python版と同様、カードだけのシナリオ構築でも宿の状況を必須と
    // する。 ネイティブは全構成で、型付きの失敗としてその前提を守る。
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_ydata || !*::cw::g_cwpy->m_ydata)
        throw std::logic_error("ScenarioData initialization requires YadoData");
    const auto fpath = ::cw::util::get_linktarget(arg_header->get_fpath());
    const auto name = scenario_value_string(arg_header->m_name);
    const auto author = scenario_value_string(arg_header->m_author);
    log_scenario_data("initialize begin: path=" + fpath +
                      " name=" + name +
                      " type=" + std::to_string(value_integer_or(arg_header->m_type, -1)) +
                      " startid=" + std::to_string(value_integer_or(arg_header->m_startid, -1)) +
                      " cardonly=" + (arg_cardonly ? "1" : "0"));
    m_name = name; m_author = author;
    m_fpath = fpath; m_tempdir = fpath; m_scedir.clear(); m_summary.reset();
    m_summarypath.clear(); m_mtime = scenario_mtime(fpath); m_can_loaded_scaledimage = true;
    m_is_playing = true; m_in_f9 = false; m_in_endprocess = false;
    m__paused_time = 0.0; m__paused_time_integer_zero = false; m__start_datetime.reset(); m__filenum = 0; m__progress = false;
    m__cancel_decompress = false; m__fastLoadCache = false; m_data_cache.clear();
    m_path_cache.clear(); m_resource_cache.clear(); m_resource_cache_size = 0;
    // DeletedFilesはPython版のValueSet相当をVariantSequenceで保持する。
    // 空のdict/文字列へ潰すとScenarioLogの書込み順と削除転送を失うため、
    // 初期化時から必ずリストオブジェクトを用意する。
    m_deletedpaths = ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(std::make_shared<VariantSequence>()));
    m_lostadventurers.clear(); m_gossips.clear(); m_compstamps.clear();
    m_friendcards.clear(); m_infocards.clear(); m_infocard_maxindex = 0; m__infocard_cache.clear();
    m_moved_mcards.clear(); m_backlog.clear(); m_ex_cache.clear(); m_breakpoints.clear();
    m_versionhint.assign(4, std::nullopt);
    if (const auto* value = std::get_if<::cw::lib::Integer>(&arg_header->m_startid.storage())) {
        m_startid = *value; SystemData::m_startid = *value;
    }
    m_can_loaded_scaledimage = true;
    if (!arg_cardonly && ::cw::g_cwpy) ::cw::g_cwpy->m_areaid = m_startid;

    // アーカイブはCWPyの一時領域へ展開し、展開後のディレクトリを以後の
    // XML/resource/cacheの基準にする。展開済みシナリオはそのまま参照する。
    if (mounted_or_native_is_directory(std::filesystem::u8path(fpath))) {
        m_tempdir = fpath;
        _find_summaryintemp();
    } else if (mounted_or_native_is_file(std::filesystem::u8path(fpath))) {
        std::optional<::cw::lib::String> cached_tempdir;
        if (::cw::g_cwpy && ::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata &&
            (*::cw::g_cwpy->m_ydata)->m_recenthistory) {
            cached_tempdir = (*::cw::g_cwpy->m_ydata)->m_recenthistory->check(fpath, std::nullopt);
        }
        // 古いキャッシュは、_decompress()が既存の親ディ
        // レクトリを展開済みシナリオと誤認し、RecentHistory
        // 自体を指す場合がある。 実際にSummaryを含むキャ
        // ッシュだけを再利用し、なければ記録を捨てて再展開する。
        if (cached_tempdir && !cached_tempdir->empty()) {
            bool has_summary = false;
            for (const auto& entry : mounted_or_native_files_recursive(
                     std::filesystem::u8path(*cached_tempdir))) {
                auto filename = scenario_lower(
                    ::cw::lib::encoding::path_to_utf8(entry.filename()));
                if (filename == "summary.xml" || filename == "summary.wsm") {
                    has_summary = true;
                    break;
                }
            }
            if (!has_summary) {
                (*::cw::g_cwpy->m_ydata)->m_recenthistory->remove(
                    fpath, true, std::nullopt);
                cached_tempdir.reset();
            }
        }
        if (cached_tempdir && !cached_tempdir->empty()) {
            m_tempdir = *cached_tempdir;
            (*::cw::g_cwpy->m_ydata)->m_recenthistory->moveend(fpath);
            _find_summaryintemp();
            log_scenario_data("initialize reused recent history: source=" + fpath +
                              " tempdir=" + scenario_value_string(m_tempdir));
        } else {
            const auto base = (::cw::g_cwpy && !scenario_value_string(::cw::g_tempdir).empty())
                ? scenario_value_string(::cw::g_tempdir)
                : path_utf8(std::filesystem::u8path(fpath).parent_path());
            m_tempdir = join_path(base, "Scenario");
            try {
                const auto original_tempdir = _decompress(false);
                if (::cw::g_cwpy && ::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata &&
                    (*::cw::g_cwpy->m_ydata)->m_recenthistory) {
                    (*::cw::g_cwpy->m_ydata)->m_recenthistory->append(
                        name, fpath, original_tempdir, std::nullopt);
                }
            } catch (const std::exception& error) {
                log_scenario_data("initialize decompress failed: source=" + fpath +
                                  " destination=" + scenario_value_string(m_tempdir) +
                                  " error=" + ::cw::lib::String(error.what()));
                throw;
            }
        }
    } else {
        log_scenario_data("initialize source is neither directory nor regular file: path=" + fpath);
    }
    {
        const auto tempdir = scenario_value_string(m_tempdir);
        const auto temp_path = std::filesystem::u8path(tempdir);
        const auto status = mounted_or_native_stat(temp_path);
        const bool exists = status && status->exists();
        log_scenario_data("initialize tempdir: path=" + tempdir +
                          " exists=" + (exists ? "1" : "0") +
                          " directory=" + (status && status->is_directory() ? "1" : "0"));
    }
    // Python版はクラシックシナリオの展開先を決めた後、.wsm/.widを
    // 読み込むCWScenarioを作成してから_init_xmlpaths()を実行する。
    // これを省くとSummary.wsmをXMLとして解析してしまい、読み込みに
    // 失敗する。
    if (::cw::g_cwpy) {
        const auto type = value_integer_or(arg_header->m_type, -1);
        if (type == ::cw::scenariodb::g_TYPE_NEXT) {
            ::cw::g_cwpy->m_classicdata.reset();
            const auto source = scenario_value_string(m_tempdir);
            const auto global_tempdir = scenario_value_string(::cw::g_tempdir);
            const auto parent = join_path(global_tempdir, "NextScenario");
            const auto source_name = path_utf8(std::filesystem::u8path(source).filename());
            const auto destination = ::cw::binary::util::check_duplicate(
                join_path(parent, source_name.empty() ? "Scenario" : source_name));
            const auto skintype = ::cw::g_cwpy->m_setting
                ? ::cw::g_cwpy->m_setting->m_skintype : ::cw::lib::String{};
            const auto skinname = ::cw::g_cwpy->m_setting
                ? ::cw::g_cwpy->m_setting->m_skinname : ::cw::lib::String{};
            log_scenario_data("next conversion begin: source=" + source +
                              " destination=" + destination);
            const auto converted = ::cw::binary::next::convert_scenario(
                source, destination, skintype, skinname);
            m_tempdir = converted.directory;
            log_scenario_data("next conversion completed: version=" +
                              std::to_string(converted.data_version) +
                              " destination=" + converted.directory);
        } else if (type == ::cw::scenariodb::g_TYPE_CLASSIC) {
            ::cw::g_cwpy->m_classicdata.reset();
            auto classic = std::make_shared<::cw::binary::cwscenario::CWScenario>();
            const auto tempdir = scenario_value_string(m_tempdir);
            const auto global_tempdir = scenario_value_string(::cw::g_tempdir);
            const auto skintype = ::cw::g_cwpy->m_setting ? ::cw::g_cwpy->m_setting->m_skintype : ::cw::lib::String{};
            log_scenario_data("classicdata initialize begin: path=" + tempdir +
                              " destination=" + join_path(global_tempdir, "OldScenario") +
                              " skintype=" + skintype);
            try {
                classic->initialize(tempdir, join_path(global_tempdir, "OldScenario"), skintype, "", false);
            } catch (const std::exception& error) {
                log_scenario_data("classicdata initialize failed: " + ::cw::lib::String(error.what()));
                throw;
            }
            ::cw::g_cwpy->m_classicdata = classic;
            if (classic->m_versionhint && m_versionhint.size() > static_cast<std::size_t>(::cw::g_HINT_SCENARIO))
                m_versionhint[static_cast<std::size_t>(::cw::g_HINT_SCENARIO)] = classic->m_versionhint;
            log_scenario_data("classicdata initialized: summary=" +
                              (classic->m_summarypath ? *classic->m_summarypath : ::cw::lib::String{}) +
                              " cwfiles=" + std::to_string(classic->m_cwfiles.size()) +
                              " materials=" + std::to_string(classic->m_materials.size()) +
                              " otherfiles=" + std::to_string(classic->m_otherfiles.size()) +
                              " otherdirs=" + std::to_string(classic->m_otherdirs.size()) +
                              " modeini=" + (classic->m_hasmodeini ? "1" : "0"));
        }
    }
    try {
        _init_xmlpaths(false);
    } catch (const std::exception& error) {
        log_scenario_data("initialize _init_xmlpaths failed: " + ::cw::lib::String(error.what()) +
                          " tempdir=" + scenario_value_string(m_tempdir));
        throw;
    }
    log_scenario_data("initialize paths completed: summary=" + m_summarypath +
                      " scedir=" + m_scedir +
                      " areas=" + std::to_string(m__areas.size()) +
                      " battles=" + std::to_string(m__battles.size()) +
                      " packs=" + std::to_string(m__packs.size()) +
                      " summary_loaded=" + (m_summary && *m_summary ? "1" : "0"));
    if (arg_cardonly) return;
    _init_sparea_mcards();
    {
        // PythonのScenarioDataと同様、_init_xmlpaths()
        // 後は、一時的な現在データのポインターとは独立して全Summary
        // 変数を初期化する。 m_dataの有無で制限すると、未反
        // 映の場合にデバッガーが現在イベントのローカルコモンしか持
        // たなくなる。 Summaryツリーを正式な入力とする。
        _init_flags(); _init_steps(); _init_variants();
        if (::cw::g_cwpy) {
            const auto saved = ::cw::g_cwpy->m_breakpoint_table.find(std::make_tuple(name, author));
            if (saved != ::cw::g_cwpy->m_breakpoint_table.end()) m_breakpoints = saved->second;
        }
        // C++版はPythonのsetの別参照ではなく、型付き集合の値を公開す
        // る。 通常のデバッガー編集は既存のsave_breakpoints境
        // 界で反映し、表・集合の直接の相互別参照は提供しない。 特殊エリアカー
        // ドとデバッガー状態は、CWPyの既存のバックエンド境界で初期化する。
        _init_debugger();
    }
    _init_ignorecase_table();
}

// Python版は os.path.normcase("A") が大小文字を維持する場
// 合だけ、この別名表を作る。 ネイティブは全ホスト・保存経路で現在の一つのtempdir
// を索引化する。 ボリュームごとの大小文字区別の検査や、複数ルートの走査ではない。
void ScenarioData::_init_ignorecase_table()
{
    m_ignorecase_table.clear();
    const auto tempdir = scenario_value_string(m_tempdir);
    if (tempdir.empty() || !mounted_or_native_is_directory(std::filesystem::u8path(tempdir))) return;
    for (const auto& entry : mounted_or_native_files_recursive(std::filesystem::u8path(tempdir))) {
        const auto path = ::cw::lib::encoding::path_to_utf8(entry);
        m_ignorecase_table[::cw::lib::encoding::lower(path)] = path;
    }
}

void ScenarioData::check_archiveupdated(bool arg_reload)
{
    const auto fpath = scenario_value_string(m_fpath);
    const auto tempdir = scenario_value_string(m_tempdir);
    const bool archive = !fpath.empty() && mounted_or_native_is_file(std::filesystem::u8path(fpath));
    // Python版との差異として、圧縮されたシナリオはセッション中に
    // 変更されないものとして扱う。 テーブル・キャンプの切り替えで再展
    // 開しない。 ディレクトリ形式のシナリオは、従来どおり変更を随時読み込む。
    if (archive) return;
    const auto watched = archive ? fpath : m_summarypath;
    const auto mtime = scenario_mtime(watched);
    if (mtime == 0.0 || mtime == m_mtime) return;
    m_mtime = mtime;
    if (arg_reload) _reload(); else _init_ignorecase_table();
}

::cw::lib::String ScenarioData::_decompress(bool arg_overwrite)
{
    // 明示的な再読み込みも含め、全呼び出し元が成功済みの展開を再利
    // 用する。 失敗・キャンセルした試行では、この値を設定しない。
    if (!m_extracted_archive_root.empty()) return m_extracted_archive_root;
    const auto source = scenario_value_string(m_fpath);
    const auto requested = scenario_value_string(m_tempdir);
    if (source.empty()) return {};
    if (mounted_or_native_is_directory(std::filesystem::u8path(source))) return source;
    const auto source_path = std::filesystem::u8path(source);
    std::filesystem::path destination;
    ::cw::lib::String dirname;
    if (!requested.empty()) {
        // Python版と同様、cw.tempdir/Scenarioを親として
        // 渡し、 decompress_zip がその下にアーカイブ名の一意な
        // 子を作る。 親は既存が前提であり、存在だけを展開済みの証拠にしない。
        destination = std::filesystem::u8path(requested);
    } else if (::cw::g_cwpy && !scenario_value_string(::cw::g_tempdir).empty()) {
        destination = std::filesystem::u8path(scenario_value_string(::cw::g_tempdir));
    } else {
        destination = source_path.parent_path();
    }
    if (!mounted_or_native_mkdir(destination))
        throw std::runtime_error("シナリオ展開先を作成できません: " + path_utf8(destination));
    m__progress = true; m__cancel_decompress = false; m__filenum = 0;
    try {
        struct Progress {
            std::atomic<::cw::lib::Integer> total{-1}, current{0};
            std::atomic_bool cancel{false};
        } progress_state;
        const auto startup = ::cw::lib::Callback([&](const ::cw::lib::Arguments& args, const ::cw::lib::KeywordArguments&) {
            progress_state.total.store(args.empty() ? 0 : value_integer_or(args.front(), 0));
            return ::cw::lib::Value{};
        });
        const auto progress = ::cw::lib::Callback([&](const ::cw::lib::Arguments& args, const ::cw::lib::KeywordArguments&) {
            if (!args.empty()) progress_state.current.store(value_integer_or(args.front(), 0));
            return ::cw::lib::Value(progress_state.cancel.load());
        });
        const auto ext = scenario_lower(source_path.extension().generic_string());
        const auto destination_utf8 = ::cw::lib::encoding::path_to_utf8(destination);
        // Python版と同様、展開ワーカーを呼ぶ前にここでZIP/LHA
        // を開く。 オープンの失敗も保持し、utilの任
        // 意の空結果をユーザーのキャンセルと誤認しない。
        const auto archive = ext == ".cab" ? std::optional<::cw::lib::Value>{}
            : std::optional<::cw::lib::Value>{::cw::util::zip_file(source, ::cw::lib::Value("r"))};
        const auto extract = [&] {
            return ext == ".cab"
                ? ::cw::util::decompress_cab(source, destination_utf8, dirname, startup, progress, arg_overwrite)
                : ::cw::util::decompress_zip(source, destination_utf8, dirname, startup, progress, arg_overwrite, archive);
        };
        const auto runtime = ::cw::g_cwpy;
        bool game_thread = false;
        if (runtime) {
            std::lock_guard<std::mutex> lock(runtime->m_game_exec_mutex);
            game_thread = runtime->m_game_thread_id == std::this_thread::get_id();
        }
        ::cw::lib::String result;
        if (game_thread) {
            // イベントループへの再入をまたいで、現在のシナリオを保持する。 初回
            // 読み込み中は、呼び出し元のローカルshared_ptrが所有する。
            [[maybe_unused]] const auto retained_scenario = runtime->m_sdata;
            m__arcname = path_utf8(source_path.filename());
            runtime->m_is_decompressing = true;
            struct RestoreExpansion {
                std::shared_ptr<::cw::thread::CWPy> runtime;
                ~RestoreExpansion() {
                    runtime->m_is_decompressing = false;
                    runtime->m_expanding.clear();
                    runtime->m_expanding_min = runtime->m_expanding_cur = 0;
                    runtime->m_expanding_max = 100;
                    try { if (runtime->m_statusbar) runtime->m_statusbar->change(false, false); }
                    catch (...) { ::cw::util::print_ex(std::nullopt); }
                }
            } restore{runtime};
            ::cw::lib::Future<::cw::lib::String> job;
            ::cw::lib::ExceptionPtr extraction_error;
            try {
                job = ::cw::lib::async(std::launch::async, extract);
                ::cw::lib::Integer shown_total = -1, shown_current = -1;
                const auto update_progress = [&] {
                    const auto total = progress_state.total.load();
                    const auto current = progress_state.current.load();
                    if (total < 0 || (total == shown_total && current == shown_current)) return;
                    m__filenum = total;
                    runtime->m_expanding_min = 0;
                    runtime->m_expanding_max = total;
                    runtime->m_expanding_cur = current;
                    std::ostringstream text;
                    text << m__arcname << "を展開中... (" << std::setw(std::to_string(total).size())
                         << current << "/" << total << ")";
                    runtime->m_expanding = text.str();
                    if (shown_total < 0 && runtime->m_statusbar) runtime->m_statusbar->change(false, false);
                    shown_total = total; shown_current = current;
                };
                while (job.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
                    if (!runtime->is_runningstatus() || m__cancel_decompress)
                        throw ::cw::event::EffectBreakError{};
                    update_progress();
                    // C++版wait_frameは入力とハンドラ
                    // ーの処理も行うため、ここで重ねて配送しない。
                    runtime->wait_frame(1, false, std::nullopt, 0);
                }
                update_progress();
                // Python版と同様、最初の待機フレーム前に展開が終
                // わっても、ワーカー終了後にハンドラーを一度処理する
                // 。 結果・エラーを受け入れる前に、その境界を通す。
                if (auto handler = runtime->get_eventhandler()) handler->run();
                result = job.get();
                if (!runtime->is_runningstatus() || m__cancel_decompress)
                    throw ::cw::event::EffectBreakError{};
            } catch (...) {
                // キャンセル・終了後に、このスタックを参照するコールバッ
                // クをワーカーへ残さない。 ゲーム側の例外を維持する。
                if (job.valid()) {
                    m__cancel_decompress = true;
                    progress_state.cancel.store(true);
                    job.wait();
                }
                extraction_error = ::cw::lib::capture_exception();
            }
            // Pythonのfinallyと同様、キャンセル・復号エラーも含め、
            // 展開モードを離れる前に保留書き込みを待ち、 最後の変更フレームを表
            // 示する。例外を出し得る処理はRAIIのデストラクターへ入れない。
            if (::cw::g_fsync) ::cw::g_fsync->sync();
            runtime->lazy_draw();
            if (extraction_error) ::cw::lib::rethrow_exception(extraction_error);
        } else {
            // オフライン変換・テストにはゲームループがないため、
            // UI処理を作らず同じデコーダー・エラー経路を使う。
            result = extract();
        }
        m__filenum = std::max<::cw::lib::Integer>(0, progress_state.total.load());
        if (result.empty()) {
            if (m__cancel_decompress)
                throw std::runtime_error("シナリオアーカイブの展開がキャンセルされました: " + source);
            throw std::runtime_error("シナリオアーカイブの展開に失敗しました: " + source);
        }
        const auto orig = result;
        m_tempdir = result;
        _find_summaryintemp();
        m__progress = false;
        m_extracted_archive_root = orig;
        return orig;
    } catch (...) {
        m__progress = false;
        throw;
    }
}

// Summaryの完全一致名を、大小文字を揃えた代替より優先する。 各再帰検索は
// 、この一つのシナリオツリーの共通整列順を使い、os.walkの順序ではない。
void ScenarioData::_find_summaryintemp()
{
    // Python版と同様、アーカイブのシナリオルートを選ぶ前に、予約済みSummary
    // の書き込みを見える状態にする。 この補助処理を直接呼ぶ場合も同様。
    if (::cw::g_fsync) ::cw::g_fsync->sync();
    auto tempdir = scenario_value_string(m_tempdir);
    if (tempdir.empty() || !mounted_or_native_is_directory(std::filesystem::u8path(tempdir))) return;
    std::filesystem::path selected = std::filesystem::u8path(tempdir);
    auto find_summary = [&](bool exact) {
        for (const auto& entry : mounted_or_native_files_recursive(selected)) {
            auto name = ::cw::lib::encoding::path_to_utf8(entry.filename());
            if (!exact) name = scenario_lower(name);
            if (name == (exact ? "Summary.xml" : "summary.xml") ||
                name == (exact ? "Summary.wsm" : "summary.wsm")) {
                selected = entry.parent_path(); return true;
            }
        }
        return false;
    };
    if (!mounted_or_native_is_file(selected / "Summary.xml") &&
        !mounted_or_native_is_file(selected / "Summary.wsm")) {
        if (!find_summary(true)) find_summary(false);
    }
    const auto selected_utf8 = ::cw::lib::encoding::path_to_utf8(selected);
    m_tempdir = selected_utf8;
}

std::optional<std::tuple<::cw::lib::String, ::cw::lib::String, bool, bool, bool>> ScenarioData::get_versionhint(::cw::lib::Integer arg_frompos)
{
    // Pythonの versionhint[frompos:] のスライスに合わせるが、
    // イベントの打ち切りには正規化後の位置ではなく、列挙位置＋元のfromposを使う。
    const auto count = static_cast<::cw::lib::Integer>(m_versionhint.size());
    const auto start = arg_frompos < 0 ? std::max<::cw::lib::Integer>(0, count + arg_frompos)
                                     : std::min(arg_frompos, count);
    for (auto i = start; i < count; ++i) {
        const auto offset = i - start;
        // fromposへ加える代わりに非負のオフセットを引き、
        // 固定幅のネイティブ整数でも極端な入力を扱えるようにする。
        if (arg_frompos >= 2 - offset && ::cw::g_cwpy && ::cw::g_cwpy->m_event && ::cw::g_cwpy->m_event->m_in_inusecardevent) break;
        if (m_versionhint[i]) return m_versionhint[i];
    }
    return std::nullopt;
}

void ScenarioData::set_versionhint(::cw::lib::Integer arg_pos, std::optional<std::tuple<::cw::lib::String, ::cw::lib::String, bool, bool, bool>> arg_hint)
{
    if (arg_pos < 0) return;
    const auto before = get_versionhint(0);
    const auto before_base = (::cw::g_cwpy && ::cw::g_cwpy->m_sct)
        ? ::cw::g_cwpy->m_sct->to_basehint(before) : ::cw::lib::String{};
    if (m_versionhint.size() <= static_cast<std::size_t>(arg_pos)) m_versionhint.resize(static_cast<std::size_t>(arg_pos) + 1);
    m_versionhint[static_cast<std::size_t>(arg_pos)] = std::move(arg_hint);
    const auto after = get_versionhint(0);
    const auto after_base = (::cw::g_cwpy && ::cw::g_cwpy->m_sct)
        ? ::cw::g_cwpy->m_sct->to_basehint(after) : ::cw::lib::String{};
    if (arg_pos >= 2 && before_base != after_base && ::cw::g_cwpy) {
        ::cw::g_cwpy->update_titlebar();
    }
}

std::optional<std::shared_ptr<::cw::data::CWPyElement>> ScenarioData::get_carddata(std::shared_ptr<::cw::data::CWPyElement> arg_linkdata, bool arg_in_inusecard, std::optional<std::shared_ptr<::cw::header::CardHeader>> arg_inusecardheader)
{
    if (!arg_linkdata) return std::nullopt;
    const auto resid = arg_linkdata->getint("Property/LinkId", std::nullopt, 0); if (resid == 0) return arg_linkdata;
    std::optional<std::shared_ptr<CWPyElement>> data;
    std::optional<std::shared_ptr<::cw::header::CardHeader>> inuse;
    if (arg_in_inusecard) inuse = arg_inusecardheader ? arg_inusecardheader :
        (::cw::g_cwpy && ::cw::g_cwpy->m_event ? ::cw::g_cwpy->m_event->get_inusecard() : std::nullopt);
    const bool external = inuse && *inuse && ::cw::g_cwpy && ::cw::g_cwpy->m_event &&
        (::cw::g_cwpy->m_event->m_in_inusecardevent || ::cw::g_cwpy->m_event->m_in_cardeffectmotion || arg_inusecardheader) &&
        (!(*inuse)->m_scenariocard || (value_to_element((*inuse)->m_carddata) &&
         !value_to_element((*inuse)->m_carddata)->gettext("Property/Materials", "").empty()));
    if (external) {
        const auto carddata = value_to_element((*inuse)->m_carddata);
        if (!carddata) throw std::logic_error("External card requires carddata");
        const auto materials = carddata->gettext("Property/Materials", "");
        if (materials.empty()) return std::nullopt;
        const auto base = ::cw::util::join_yadodir(materials);
        const auto fpath = _get_carddatapath(arg_linkdata->m_tag, resid, base);
        if (fpath.empty()) return std::nullopt;
        data = ::cw::data::xml2element(fpath, "", std::nullopt, std::nullopt, true, std::nullopt);
    } else if (arg_linkdata->m_tag == "SkillCard") data = get_skilldata(resid, "", true, std::nullopt);
    else if (arg_linkdata->m_tag == "ItemCard") data = get_itemdata(resid, "", true, std::nullopt);
    else if (arg_linkdata->m_tag == "BeastCard") data = get_beastdata(resid, "", true, std::nullopt);
    else throw std::invalid_argument("Unknown linked card type");
    if (!data || !*data) return std::nullopt;
    const auto property = (*data)->find_exists("Property");
    for (const auto& tag : {::cw::lib::String("UseLimit"), ::cw::lib::String("Hold")}) {
        const auto src = arg_linkdata->find("Property/" + tag);
        const auto dst = property->find(tag);
        if (src && *src && dst && *dst) {
            property->remove(*dst);
            property->append(*src);
        }
    }
    return data;
}

void ScenarioData::save_breakpoints()
{
    if (!::cw::g_cwpy) return;
    const auto key = std::make_tuple(scenario_value_string(m_name), scenario_value_string(m_author));
    if (!SystemData::m_breakpoints.empty()) ::cw::g_cwpy->m_breakpoint_table[key] = SystemData::m_breakpoints;
    else ::cw::g_cwpy->m_breakpoint_table.erase(key);
}

std::optional<::cw::lib::DateTime> ScenarioData::get_startdatetime()
{
    return m__start_datetime;
}

// ネイティブの意図的な表示制限として、停止時間や
// 時計の逆行で経過差が負になってもゼロを返す。
// Python版は制限せず符号付きの差を返す。
::cw::lib::Real ScenarioData::get_totalpyaingtime()
{
    if (!m__start_datetime) return 0.0; const auto elapsed = ::cw::lib::DateTime::now().seconds_since(*m__start_datetime); return std::max<::cw::lib::Real>(0.0, elapsed - m__paused_time);
}

::cw::lib::Real ScenarioData::get_pausedtime()
{
    return m__paused_time;
}

void ScenarioData::start_timekeeper()
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_setting || !::cw::g_cwpy->m_setting->m_enabled_timekeeper) return;
    const auto dpath = scenario_party_debugdir(); if (dpath.empty()) return;
    const auto fpath = dpath / "Timekeeper.xml"; const auto oldpath = dpath / "OldTimekeeper.xml";
    if (::cw::g_fsync && ::cw::g_fsync->is_waiting(path_utf8(fpath))) ::cw::g_fsync->sync();
    if (mounted_or_native_is_file(fpath)) {
        if (::cw::g_fsync && ::cw::g_fsync->is_waiting(path_utf8(oldpath))) ::cw::g_fsync->sync();
        if (!mounted_or_native_is_file(oldpath))
            ::cw::util::move_path(path_utf8(fpath), path_utf8(oldpath));
    }
    resume_timekeeper();
}

void ScenarioData::resume_timekeeper()
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_setting || !::cw::g_cwpy->m_setting->m_enabled_timekeeper || m__start_datetime) return;
    const auto dpath = scenario_party_debugdir();
    if (dpath.empty()) return;
    const auto fpath = dpath / "Timekeeper.xml";
    if (::cw::g_fsync && ::cw::g_fsync->is_waiting(path_utf8(fpath))) ::cw::g_fsync->sync();
    auto fallback = [&]() { m__start_datetime = ::cw::lib::DateTime{std::chrono::system_clock::now()}; m__paused_time = 0.0; m__paused_time_integer_zero = false; };
    if (!mounted_or_native_is_file(fpath)) { fallback(); return; }
    try {
        const auto tree = ::cw::data::xml2etree(path_utf8(fpath), "", std::nullopt, std::nullopt, false);
        const auto root = tree ? tree->getroot() : nullptr;
        if (!root) throw std::invalid_argument("Timekeeper XML has no root");
        if (root->getattr(".", "wsnpath", std::nullopt) != scenario_value_string(m_fpath)) {
            fallback();
            m__paused_time_integer_zero = true;
        }
        else {
            auto parse = [&](const ::cw::lib::String& tag) {
                const auto year = root->getint(tag, ::cw::lib::Value("year"), std::nullopt);
                const auto month = root->getint(tag, ::cw::lib::Value("month"), std::nullopt);
                const auto day = root->getint(tag, ::cw::lib::Value("day"), std::nullopt);
                const auto hour = root->getint(tag, ::cw::lib::Value("hour"), std::nullopt);
                const auto minute = root->getint(tag, ::cw::lib::Value("minute"), std::nullopt);
                const auto second = root->getint(tag, ::cw::lib::Value("second"), std::nullopt);
                return ::cw::lib::DateTime::from_calendar(year, month, day, hour, minute, second);
            };
            m__start_datetime = parse("StartTime");
            const auto now = ::cw::lib::DateTime::now();
            const auto paused = root->getint("PauseTime", std::nullopt, std::nullopt);
            m__paused_time = paused + now.seconds_since(parse("PauseTime"));
            m__paused_time_integer_zero = false;
        }
    } catch (const std::exception&) {
        ::cw::util::print_ex(std::nullopt);
        fallback();
    }
    // Pythonのfinallyと同様、状態更新後の後処理の失敗は外へ伝える。
    ::cw::util::remove(path_utf8(fpath), false);
    ::cw::util::remove_emptydir(path_utf8(dpath));
}

void ScenarioData::sleep_timekeeper()
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_setting || !::cw::g_cwpy->m_setting->m_enabled_timekeeper || !m__start_datetime) return;
    const auto dpath = scenario_party_debugdir(); if (dpath.empty()) return;
    const auto fpath = dpath / "Timekeeper.xml";
    if (::cw::g_fsync && ::cw::g_fsync->is_waiting(path_utf8(fpath))) ::cw::g_fsync->sync();
    if (mounted_or_native_is_file(fpath)) {
        ::cw::util::remove(path_utf8(fpath), false);
        ::cw::util::remove_emptydir(path_utf8(dpath));
        return;
    }
    const auto start = *m__start_datetime;
    auto root = ::cw::data::make_element("Timekeeper", "", std::map<::cw::lib::String, ::cw::lib::String>{{"wsnpath", scenario_value_string(m_fpath)}});
    // Python版と同様、残りのXMLの構築・書き込みより前に
    // タイマーを止める。 その後の失敗でも停止状態を維持する。
    m__start_datetime.reset();
    const auto paused_text = m__paused_time_integer_zero && m__paused_time == 0.0
        ? ::cw::lib::String("0") : ::cw::lib::python_float_repr(m__paused_time);
    auto start_attrs = scenario_datetime_attrs(start); start_attrs["paused"] = paused_text;
    root->append(::cw::data::make_element("StartTime", "", start_attrs));
    const auto now = ::cw::lib::DateTime::now();
    root->append(::cw::data::make_element("PauseTime", paused_text, scenario_datetime_attrs(now)));
    auto tree = std::make_shared<::cw::data::CWPyElementTree>(); tree->initialize(path_utf8(fpath), root); tree->write_file(path_utf8(fpath));
    if (::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata && (*::cw::g_cwpy->m_ydata)->m_deletedpaths)
        (*::cw::g_cwpy->m_ydata)->m_deletedpaths->discard(path_utf8(fpath));
}

bool ScenarioData::change_data(::cw::lib::Integer arg_resid, std::optional<std::shared_ptr<::cw::data::CWPyElement>> arg_data)
{
    log_scenario_data("change_data begin: area=" + std::to_string(arg_resid) +
                      " supplied=" + (arg_data && *arg_data ? "1" : "0") +
                      " areas=" + std::to_string(m__areas.size()));
    // 空のoptionalと、nullポインターを持つoptionalの両方をPythonのNoneとして扱う。
    if (!arg_data || !*arg_data) check_archiveupdated(true);
    const auto changed = SystemData::change_data(arg_resid, std::move(arg_data));
    log_scenario_data(::cw::lib::String("change_data ") + (changed ? "completed" : "failed") +
                      ": area=" + std::to_string(arg_resid));
    return changed;
}

void ScenarioData::reload()
{
    check_archiveupdated(false); _reload();
}

void ScenarioData::save_variables()
{
    // Python実装はシナリオ終了時のYadoData::save_variablesに一本化しており、
    // ScenarioData.save_variables自体は意図的なno-opである。
}

void ScenarioData::load_variables()
{
    // 同上。ロード時はload_log/_load_variablesがXMLのスナップショットを適用する。
}

void ScenarioData::update_skin()
{
    _init_xmlpaths(false); _init_sparea_mcards(); _init_debugger();
    queue_debugger_refresh();
}

void ScenarioData::_reload()
{
    reload_variables();
    m_data_cache.clear(); m_path_cache.clear(); m_resource_cache.clear(); m_resource_cache_size = 0;
    _init_xmlpaths(false); _init_ignorecase_table(); _init_debugger();
    queue_debugger_refresh();
}

bool ScenarioData::is_updatedfilenames()
{
    if (::cw::g_fsync) ::cw::g_fsync->sync();
    const auto tempdir = scenario_value_string(m_tempdir);
    std::set<::cw::lib::String> current;
    const std::set<::cw::lib::String> dirs{"area", "battle", "package", "castcard", "skillcard", "itemcard", "beastcard", "infocard"};
    for (const auto& entry : mounted_or_native_files_recursive(std::filesystem::u8path(tempdir))) {
        const auto parent = scenario_lower(path_utf8(entry.parent_path().filename()));
        const auto ext = scenario_lower(entry.extension().generic_string());
        if (ext != ".xml" || !dirs.contains(parent)) continue;
        current.insert(scenario_filename_normcase(::cw::lib::encoding::path_to_utf8(entry)));
    }
    std::set<::cw::lib::String> previous;
    for (const auto& value : m__datafilenames)
        if (const auto* path = std::get_if<::cw::lib::String>(&value.storage()))
            previous.insert(scenario_filename_normcase(*path));
    return current != previous;
}

// 現在のシナリオツリーは共通処理で整列したファイル順を使う。 最初に解
// 析したSummaryを保持し、重複リソースIDは後の値で置き換える。
// Pythonのos.walkの発見順とは結果が異なる場合がある。
void ScenarioData::_init_xmlpaths(bool arg_xmlonly)
{
    if (::cw::g_fsync) ::cw::g_fsync->sync();
    log_scenario_data(::cw::lib::String("_init_xmlpaths begin: xmlonly=") + (arg_xmlonly ? "1" : "0") +
                      " tempdir=" + scenario_value_string(m_tempdir));
    auto clear_tables = [&]() {
        m__areas.clear(); m__battles.clear(); m__packs.clear(); m__casts.clear(); m__infos.clear(); m__items.clear(); m__skills.clear(); m__beasts.clear();
        SystemData::m__areas.clear(); SystemData::m__battles.clear(); SystemData::m__packs.clear(); SystemData::m__casts.clear();
        SystemData::m__infos.clear(); SystemData::m__items.clear(); SystemData::m__skills.clear(); SystemData::m__beasts.clear();
    };
    clear_tables(); m__datafilenames.clear(); m_instructions.clear();
    if (!arg_xmlonly) {
        m_summary.reset(); m_summarypath.clear(); m_scedir.clear(); m_wsn_version.clear();
    }
    const auto tempdir = scenario_value_string(m_tempdir);
    if (tempdir.empty() || !mounted_or_native_is_directory(std::filesystem::u8path(tempdir))) {
        if (!arg_xmlonly) throw std::runtime_error("Summary file is not found: " + tempdir);
        return;
    }
    static const std::map<::cw::lib::String, int> kinds{{"area", 0}, {"battle", 1}, {"package", 2}, {"castcard", 3}, {"infocard", 4}, {"itemcard", 5}, {"skillcard", 6}, {"beastcard", 7}};
    const auto base = std::filesystem::u8path(tempdir);
    const std::regex re_area(R"(area\d+\.wid)");
    const std::regex re_battle(R"(battle\d+\.wid)");
    const std::regex re_package(R"(package\d+\.wid)");
    const std::regex re_mate(R"(mate\d+\.wid)");
    const std::regex re_info(R"(info\d+\.wid)");
    const std::regex re_item(R"(item\d+\.wid)");
    const std::regex re_skill(R"(skill\d+\.wid)");
    const std::regex re_beast(R"(beast\d+\.wid)");
    std::size_t xml_count = 0;
    std::size_t classic_count = 0;
    for (const auto& path : mounted_or_native_files_recursive(base)) {
        const auto filename = ::cw::lib::encoding::path_to_utf8(path.filename());
        const auto path_utf8 = ::cw::lib::encoding::path_to_utf8(path);
        const auto lower = scenario_lower(filename);
        const auto lowerext = scenario_lower(::cw::lib::encoding::path_to_utf8(path.extension()));
        const auto parentdir = scenario_lower(::cw::lib::encoding::path_to_utf8(path.parent_path().filename()));
        const bool isdatadir = kinds.contains(parentdir);
        const bool classic_file = lowerext == ".wid";
        // Python版と同様、xmlonlyでは読み込み済みSummaryを保ってリソー
        // スの下位ディレクトリを更新する。 全走査では上でSummaryをリセットする。
        if (arg_xmlonly && !isdatadir) continue;
        // ScenarioData独自のm_instructionsはなく、両方の名前はSystemDataの一つの一覧を指す。
        if (lower.ends_with(".txt")) m_instructions.push_back(path_utf8);
        if (arg_xmlonly && lowerext != ".xml") continue;
        if (eat_spchar(::cw::lib::encoding::path_to_utf8(path.parent_path()), filename, m_can_loaded_scaledimage)) continue;
        if (lowerext != ".xml" && lowerext != ".wsm" && lowerext != ".wid") continue;
        if ((lowerext == ".wsm" || lowerext == ".wid") && path.parent_path() != base) continue;
        if (lowerext == ".xml") m__datafilenames.emplace_back(scenario_filename_normcase(path_utf8));
        if ((lower == "summary.xml" || lower == "summary.wsm") && (!m_summary || !*m_summary)) {
            m_scedir = ::cw::lib::encoding::path_to_utf8(path.parent_path()); m_summarypath = path_utf8;
            log_scenario_data("_init_xmlpaths summary candidate: path=" + path_utf8 +
                              " classic=" + (lowerext == ".wsm" ? "1" : "0"));
            std::shared_ptr<::cw::data::CWPyElementTree> summary;
            try {
                summary = ::cw::data::xml2etree(path_utf8, "", std::nullopt, std::nullopt, false);
            } catch (const std::exception& error) {
                log_scenario_data("_init_xmlpaths summary failed: path=" + path_utf8 +
                                  " error=" + ::cw::lib::String(error.what()));
                throw;
            }
            if (summary) {
                // Python版と同様、再読み込みでSummaryは置き換えるが、現在のエリ
                // アデータは変えない。 Summaryの利用側はsummaryを参照する。
                m_summary = summary;
                m_can_loaded_scaledimage = summary->getbool(".", ::cw::lib::Value("scaledimage"), false);
                m_wsn_version = summary->getroot() ? summary->getroot()->get("dataVersion", "") : ::cw::lib::String{};
                // Python版はディレクトリ形式だけ、解析前に更新日時を得
                // る。 ネイティブ版は両形式で解析成功後のSummaryの
                // 日時を記録するが、 圧縮形式の check_updated()
                // は再読み込みしない方針により、その参照前に戻る。
                m_mtime = scenario_mtime(m_summarypath);
                log_scenario_data("_init_xmlpaths summary loaded: path=" + path_utf8 +
                                  " root=" + (summary->getroot() ? summary->getroot()->m_tag : ::cw::lib::String{}) +
                                  " version=" + scenario_value_string(m_wsn_version));
            } else {
                log_scenario_data("_init_xmlpaths summary returned null: path=" + path_utf8);
            }
            // SummaryはID・名前によるリソース登録とは別に扱うため
            // 、 この初回読み込みの分岐では常に下のリソース処理を飛ばす。
            continue;
        }
        if (lowerext != ".xml" && !classic_file) continue;
        if (!isdatadir && !classic_file) continue;
        ::cw::lib::Integer id = -1;
        ::cw::lib::String name;
        if (classic_file) {
            if (!::cw::g_cwpy || !::cw::g_cwpy->m_classicdata || !*::cw::g_cwpy->m_classicdata) continue;
            const auto loaded = (*::cw::g_cwpy->m_classicdata)->load_file(path_utf8, true);
            const auto& value = std::get<0>(loaded);
            const auto* object = value ? std::get_if<::cw::lib::ObjectPtr>(&value->storage()) : nullptr;
            const auto binary = object && *object
                ? std::dynamic_pointer_cast<::cw::binary::base::CWBinaryBase>(*object) : nullptr;
            if (!binary) {
                log_scenario_data("_init_xmlpaths classic metadata failed: path=" + path_utf8);
                continue;
            }
            id = binary->m_id;
            name = binary->m_name;
            ++classic_count;
        } else {
            // lib::Stringのパスは全OSでUTF-8。 generic_string()
            // はWindowsの現在のマルチバイトコードページを使い、城砦都
            // 市キーレなどの日本語の名前を壊すため使わない。
            const auto element = ::cw::data::xml2element(path_utf8, "Property", std::nullopt, std::nullopt, false, std::nullopt);
            if (!element) continue;
            id = element->getint("Id", std::nullopt, -1);
            name = element->gettext("Name", "");
            ++xml_count;
        }
        const auto pair = std::make_tuple(name, path_utf8);
        int kind = -1;
        if (isdatadir) {
            kind = kinds.at(parentdir);
        } else if (classic_file) {
            if (std::regex_match(lower, re_area)) kind = 0;
            else if (std::regex_match(lower, re_battle)) kind = 1;
            else if (std::regex_match(lower, re_package)) kind = 2;
            else if (std::regex_match(lower, re_mate)) kind = 3;
            else if (std::regex_match(lower, re_info)) kind = 4;
            else if (std::regex_match(lower, re_item)) kind = 5;
            else if (std::regex_match(lower, re_skill)) kind = 6;
            else if (std::regex_match(lower, re_beast)) kind = 7;
        }
        if (kind < 0) continue;
        auto put = [&](auto& derived, auto& base_table) { derived[id] = pair; base_table[id] = pair; };
        switch (kind) {
        case 0: put(m__areas, SystemData::m__areas); break; case 1: put(m__battles, SystemData::m__battles); break;
        case 2: put(m__packs, SystemData::m__packs); break; case 3: put(m__casts, SystemData::m__casts); break;
        case 4: put(m__infos, SystemData::m__infos); break; case 5: put(m__items, SystemData::m__items); break;
        case 6: put(m__skills, SystemData::m__skills); break; case 7: put(m__beasts, SystemData::m__beasts); break;
        default: break;
        }
    }
    log_scenario_data("_init_xmlpaths resources registered: xml=" + std::to_string(xml_count) +
                      " classic=" + std::to_string(classic_count) +
                      " areas=" + std::to_string(m__areas.size()) +
                      " battles=" + std::to_string(m__battles.size()) +
                      " packs=" + std::to_string(m__packs.size()) +
                      " casts=" + std::to_string(m__casts.size()) +
                      " infos=" + std::to_string(m__infos.size()) +
                      " items=" + std::to_string(m__items.size()) +
                      " skills=" + std::to_string(m__skills.size()) +
                      " beasts=" + std::to_string(m__beasts.size()) +
                      " summary=" + (m_summary && *m_summary ? "1" : "0"));
    if (::cw::g_cwpy && ::cw::g_cwpy->m_rsrc) {
        // Python版と同様、シナリオ内のfont_*を登録してから rsrc.specialchars
        // を複製する。 ここで get_specialchars()
        // を組み直すと、遅延項目が消え、変更フラグも解除される。
        const auto table = resource_table_from_value(::cw::g_cwpy->m_rsrc->m_specialchars);
        m_specialchars = table ? table->copy() : ::cw::g_cwpy->m_rsrc->get_specialchars();
    } else {
        m_specialchars = ::cw::lib::Value{};
    }
    if (!arg_xmlonly && (!m_summary || !*m_summary)) {
        log_scenario_data("_init_xmlpaths failed: Summary file is not found: " + tempdir);
        throw std::runtime_error("Summary file is not found: " + tempdir);
    }

    // スキン付属の特殊エリアはシナリオのAreaテーブルへ上書き登録する。
    const auto skin = ::cw::g_cwpy ? value_to_string(::cw::g_cwpy->m_skindir) : ::cw::lib::String{};
    const auto spdir = std::filesystem::u8path(skin) / "Resource" / "Xml" / "Scenario";
    if (!mounted_or_native_is_directory(spdir)) throw std::runtime_error("scenario skin resource directory is missing: " + path_utf8(spdir));
    for (const auto& entry : mounted_or_native_list(spdir)) {
        if (!mounted_or_native_is_file(entry) || scenario_lower(::cw::lib::encoding::path_to_utf8(entry.extension())) != ".xml") continue;
        const auto resource_path = ::cw::lib::encoding::path_to_utf8(entry);
        const auto element = ::cw::data::xml2element(resource_path, "Property", std::nullopt, std::nullopt, false, std::nullopt);
        if (!element) continue;
        const auto id = element->getint("Id", std::nullopt, std::nullopt);
        const auto pair = std::make_tuple(element->gettext("Name", std::nullopt), resource_path); m__areas[id] = pair; SystemData::m__areas[id] = pair;
    }
}

void ScenarioData::update_scale()
{
    SystemData::update_scale();
    const auto tempdir = scenario_value_string(m_tempdir); if (tempdir.empty()) return;
    if (::cw::g_fsync) ::cw::g_fsync->sync();
    for (const auto& path : mounted_or_native_files_recursive(std::filesystem::u8path(tempdir))) {
        eat_spchar(::cw::lib::encoding::path_to_utf8(path.parent_path()),
                   ::cw::lib::encoding::path_to_utf8(path.filename()),
                   m_can_loaded_scaledimage);
    }
    if (::cw::g_cwpy && ::cw::g_cwpy->m_rsrc) {
        const auto table = resource_table_from_value(::cw::g_cwpy->m_rsrc->m_specialchars);
        m_specialchars = table ? table->copy() : ::cw::g_cwpy->m_rsrc->get_specialchars();
    }
}

bool ScenarioData::eat_spchar(::cw::lib::String arg_dpath, ::cw::lib::String arg_fname, bool arg_can_loaded_scaledimage)
{
    auto lower = scenario_lower(arg_fname);
    // Pythonの正規表現と同様、$は末尾の一改行を許すが、ドットは改行に一致しない。
    if (!lower.empty() && lower.back() == '\n') lower.pop_back();
    if (lower.rfind("font_", 0) != 0 || lower.size() <= 9 ||
        lower.substr(lower.size() - 4) != ".bmp") return false;
    const auto symbol = lower.substr(5, lower.size() - 9);
    // Pythonの正規表現のドットは、一つの書記素群ではなく、一つのUnicodeコードポイントを取得する。
    const auto codepoints = ::cw::lib::encoding::utf8_to_utf32(symbol,
        ::cw::lib::encoding::ByteOrder::Little, ::cw::lib::encoding::ErrorPolicy::Strict, false);
    if (codepoints.size() != 4 || symbol == "\n") return false;
    m__r_specialchar = ::cw::lib::Value(arg_dpath + "/" + arg_fname);
    if (::cw::g_cwpy && ::cw::g_cwpy->m_rsrc) {
        if (const auto table = resource_table_from_value(::cw::g_cwpy->m_rsrc->m_specialchars)) {
            // Pythonのクロージャーと同様、シナリオログのパス書き換えが LazyResource.m_arglist
            // を更新できるよう、引数一覧を保持する。
            const auto key = ::cw::lib::String("#") + symbol;
            const auto load = [arg_can_loaded_scaledimage](const ::cw::lib::Arguments& args,
                                                            const ::cw::lib::KeywordArguments&) {
                const auto dpath = args.size() > 0 ? scenario_value_string(args[0]) : ::cw::lib::String{};
                const auto fname = args.size() > 1 ? scenario_value_string(args[1]) : ::cw::lib::String{};
                const auto [image, userfont] = load_scenario_specialchar(dpath, fname, arg_can_loaded_scaledimage);
                auto sequence = std::make_shared<VariantSequence>();
                sequence->m_values.emplace_back(image ? ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(image)) : ::cw::lib::Value{});
                sequence->m_values.emplace_back(userfont);
                return ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(sequence));
            };
            table->set(::cw::lib::Value(key), load,
                       {::cw::lib::Value(arg_dpath), ::cw::lib::Value(arg_fname)}, {});
            ::cw::g_cwpy->m_rsrc->m_specialchars_is_changed = true;
        }
    }
    return true;
}

// Python版の宿・パーティの必須条件を、明示的なネイティブのlogic_error
// として維持する。 プレイ状態を変える前に、assert無効の構成でも守る。
void ScenarioData::start()
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_ydata || !*::cw::g_cwpy->m_ydata || !(*::cw::g_cwpy->m_ydata)->m_party)
        throw std::logic_error("ScenarioData.start: party is missing");
    m_is_playing = true;
    for (const auto& header : (*::cw::g_cwpy->m_ydata)->m_party.value()->get_allcardheaders()) if (header) header->set_scenariostart();
}

void ScenarioData::end(bool arg_showdebuglog, bool arg_complete, bool arg_failure)
{
    log_scenario_data(::cw::lib::String("end begin: showdebuglog=") + (arg_showdebuglog ? "1" : "0") +
                      " complete=" + (arg_complete ? "1" : "0") +
                      " failure=" + (arg_failure ? "1" : "0") +
                      " tempdir=" + scenario_value_string(m_tempdir));
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_ydata || !*::cw::g_cwpy->m_ydata || !(*::cw::g_cwpy->m_ydata)->m_party)
        throw std::logic_error("ScenarioData.end: party is missing");
    const auto yado = *::cw::g_cwpy->m_ydata; const auto party = yado->m_party.value();
    const bool showlog = arg_showdebuglog && ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_show_debuglogdialog;
    std::shared_ptr<::cw::debug::logging::DebugLog> debuglog;
    if (showlog) { debuglog = std::make_shared<::cw::debug::logging::DebugLog>(); debuglog->initialize(scenario_value_string(m_name)); }
    if (debuglog && ::cw::g_cwpy) for (const auto& fcard : ::cw::g_cwpy->get_fcards({})) if (fcard) debuglog->add_friend(fcard);
    if (!arg_failure) yado->join_npcs();
    m_is_playing = false; party->set_lastscenario({}, "");

    // Lost Adventurers はXMLを読み、アルバム・パーティ記録・削除の順に処理する。
    for (const auto& lost : m_lostadventurers) {
        auto path = lost;
        if (!::cw::util::resolve_yado_paths(path, yado->m_yadodir, yado->m_tempdir))
            path = join_path(yado->m_yadodir, path);
        // 消去した冒険者の項目は宿相対であり、Yado接頭辞で識別しない
        // 。 通常の宿XMLと同様、アルバム化・削除より前に検証する。
        (void)::cw::util::get_yado_relpath(path, yado->m_yadodir, yado->m_tempdir);
        auto tree = ::cw::data::yadoxml2etree(path, "", std::nullopt);
        if (!tree) continue;
        auto character = std::make_shared<::cw::character::Character>(); character->initialize(tree);
        character->remove_numbercoupon(); if (debuglog) debuglog->add_lostplayer(character);
        auto adventurer_path = path;
        if (!character->has_coupon("＿消滅予約")) {
            const auto album = ::cw::xmlcreater::create_albumpage(path, true, false); if (!album.empty()) { path = album; yado->add_album(album); }
        }
        for (const auto& partyrecord : yado->m_partyrecord) if (partyrecord) partyrecord->vanish_member(path);
        ::cw::g_cwpy->remove_xml(::cw::lib::Value(adventurer_path));
    }
    yado->remove_emptypartyrecord();
    for (const auto& header : party->get_allcardheaders()) if (header) {
        if (debuglog && header->m_scenariocard) debuglog->add_gotcard(header->m_type, header->m_name, header->m_desc, header->m_scenario, header->m_author, header->m_premium);
        header->set_scenarioend();
    }
    // backpack_moved の削除順はPythonと同じく、moved==2を完全削除してから残りを予約する。
    for (const auto& header : party->m_backpack_moved) if (header) {
        if (header->m_moved == 2) {
            if (debuglog && !header->m_scenariocard) debuglog->add_lostcard(header->m_type, header->m_name, header->m_desc, header->m_scenario, header->m_author, header->m_premium);
            // Python版と同様、ヘッダーを渡す。 キャッシュXMLが単独ファイル
            // より新しい素材参照を持つ、またはファイル削除後も残る場合があるため。
            ::cw::g_cwpy->remove_xml(::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(header)));
        } else if (yado->m_deletedpaths) yado->m_deletedpaths->add(header->m_fpath);
    }
    party->m_backpack_moved.clear();
    ::cw::header::SavedJPDCImageHeader::create_header(debuglog);
    yado->save_variables(scenario_value_string(m_name), scenario_value_string(m_author), arg_complete, m_flags, m_steps, m_variants, debuglog);
    if (::cw::g_cwpy->m_background) ::cw::g_cwpy->m_background->clear_background();
    remove_log(debuglog);
    // シナリオ中に遅延した削除を宿側へ移し、次回起動でも確実に再試行できる
    // Python版の cw.cwpy.ydata.deletedpaths.update(self.deletedpaths) を再現する。
    if (yado && yado->m_deletedpaths) {
        if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&m_deletedpaths.storage()); object && *object) {
            if (const auto sequence = std::dynamic_pointer_cast<VariantSequence>(*object)) {
                std::vector<::cw::lib::String> deleted;
                deleted.reserve(sequence->m_values.size());
                for (const auto& value : sequence->m_values) {
                    if (const auto* path = std::get_if<::cw::lib::String>(&value.storage()); path && !path->empty())
                        deleted.push_back(*path);
                }
                yado->m_deletedpaths->update(std::move(deleted));
            }
        }
    }
    if (debuglog) { const auto start = get_startdatetime(); const auto paused = get_pausedtime(); sleep_timekeeper(); debuglog->set_times(start, paused); }
    m_debuglog = debuglog; party->remove_numbercoupon();
    // Python版と同様、最後のパーティの称号整理後に保留書き込みを完了する。
    if (::cw::g_fsync) ::cw::g_fsync->sync();
    log_scenario_data("end completed: scenario=" + scenario_value_string(m_name));
}

// ネイティブの終了時の許容として、実行環境がなければin_f9の印は残すが処理を予
// 約しない。 Python版はその場合もcw.cwpyを参照してf9を予約する。
void ScenarioData::f9()
{
    m_in_f9 = true;
    if (::cw::g_cwpy) ::cw::g_cwpy->exec_func(::cw::lib::Callback(
        [](const ::cw::lib::Arguments&, const ::cw::lib::KeywordArguments&) {
            if (::cw::g_cwpy) ::cw::g_cwpy->f9(false, false);
            return ::cw::lib::Value{};
        }));
}

void ScenarioData::create_log()
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_ydata || !*::cw::g_cwpy->m_ydata || !(*::cw::g_cwpy->m_ydata)->m_party) throw std::logic_error("ScenarioData.create_log: party is missing");
    auto yado = *::cw::g_cwpy->m_ydata; auto party = yado->m_party.value();
    if (::cw::g_cwpy->m_advlog) ::cw::g_cwpy->m_advlog->start_scenario();
    std::shared_ptr<ScenarioData> self;
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_sdata.storage()); object && *object)
        self = std::dynamic_pointer_cast<ScenarioData>(*object);
    if (!self || !party) throw std::logic_error("ScenarioData.create_log: scenario is missing");
    const auto logdir = std::filesystem::u8path(scenario_value_string(::cw::g_tempdir)) / "ScenarioLog";
    log_scenario_data("create_log begin: path=" + path_utf8(logdir) +
                      " party=" + scenario_value_string(party->m_path));
    // ネイティブは既存ディレクトリを許す共通の作成処理を使い、
    // exist_okなしのPythonのos.makedirs
    // とは異なり、既存バックアップを残して後で個別に上書きす
    // る。 また、ログ・Partyの書き込み前に両ディレク
    // トリを準備する。 Python版はログ・Partyを
    // 書いた後、各コピー直前に作るため、失敗順序も異なる。
    if (!mounted_or_native_mkdir(logdir / "Party")) {
        log_scenario_data("create_log Party directory failed: path=" +
                          path_utf8(logdir / "Party"));
        throw std::runtime_error("create_log: Party directory could not be created");
    }
    if (!mounted_or_native_mkdir(logdir / "Members")) {
        log_scenario_data("create_log Members directory failed: path=" +
                          path_utf8(logdir / "Members"));
        throw std::runtime_error("create_log: Members directory could not be created");
    }
    ::cw::xmlcreater::create_scenariolog(self, path_utf8(logdir / "ScenarioLog.xml"), false,
        ::cw::g_cwpy->m_advlog ? ::cw::g_cwpy->m_advlog->m_logfilepath : ::cw::lib::String{});
    party->write();
    const auto copy_file = [&](const ::cw::lib::String& source, const std::filesystem::path& destination) {
        auto actual = ::cw::util::get_yadofilepath(source);
        if (actual.empty()) throw std::runtime_error("create_log: missing or deleted inn XML: " + source);
        const auto from = std::filesystem::u8path(actual);
        if (!copy_into_user_work_area(from, destination))
            throw std::runtime_error("create_log: failed to back up inn XML: " + actual);
    };
    const auto party_path = scenario_value_string(party->m_path);
    if (!party_path.empty()) copy_file(party_path, logdir / "Party" / std::filesystem::u8path(party_path).filename());
    // Python版と同様、Party.membersの読み込み済みツリーを実際のfpathでバ
    // ックアップする。 Party XMLからパスを組み直すと、一時保存・改名済みの元を失う。
    for (const auto& member_path : party->get_loaded_memberpaths())
        copy_file(member_path, logdir / "Members" / std::filesystem::u8path(member_path).filename());
    auto backpack = ::cw::data::make_element("BackpackFiles");
    auto sorted = party->m_backpack; std::stable_sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a && b ? value_integer_or(a->m_order, 0) < value_integer_or(b->m_order, 0) : static_cast<bool>(b); });
    const auto yadodir = party->get_yadodir(); const auto tempdir = party->get_tempdir();
    for (const auto& card : sorted) if (card) {
        backpack->append(::cw::data::make_element("File",
            ::cw::util::get_yado_relpath(card->m_fpath, yadodir, tempdir)));
    }
    auto tree = std::make_shared<::cw::data::CWPyElementTree>(); tree->initialize(path_utf8(logdir / "Backpack.xml"), backpack); tree->write_file(path_utf8(logdir / "Backpack.xml"));

    const auto key = std::make_tuple(scenario_value_string(m_name), scenario_value_string(m_author));
    const auto image_it = yado->m_savedjpdcimage.find(key);
    if (image_it != yado->m_savedjpdcimage.end() && image_it->second) {
        const auto header = image_it->second;
        const auto image_root = std::filesystem::u8path(::cw::g_cwpy->m_tempdir) / "SavedJPDCImage" / scenario_value_string(header->m_dpath) / "Materials";
        for (const auto& fpath_value : header->m_fpaths) {
            const auto fpath = scenario_value_string(fpath_value); if (fpath.empty()) continue;
            auto from = ::cw::util::get_yadofilepath(path_utf8(image_root / fpath));
            if (from.empty()) continue; // Python版と同様、保存済みのJPDC画像が未存在・削除済みなら飛ばす。
            const auto to = logdir / "TempFile" / fpath;
            if (!copy_into_user_work_area(std::filesystem::u8path(from), to))
                throw std::runtime_error("create_log: failed to back up JPDC image: " + from);
        }
    }
    const auto vars = yado->m_saved_variables.find(key);
    if (vars != yado->m_saved_variables.end()) {
        const auto& [unused, flags, steps, variants] = vars->second; (void)unused;
        for (const auto& [name, value] : flags) { const auto it = m_flags.find(name); if (it != m_flags.end() && it->second && std::find(it->second->m_initialization.begin(), it->second->m_initialization.end(), "Leave") == it->second->m_initialization.end()) it->second->set(value, false); }
        for (const auto& [name, value] : steps) { const auto it = m_steps.find(name); if (it != m_steps.end() && it->second && std::find(it->second->m_initialization.begin(), it->second->m_initialization.end(), "Leave") == it->second->m_initialization.end()) it->second->set(value, false); }
        for (const auto& [name, value] : variants) { const auto it = m_variants.find(name); if (it != m_variants.end() && it->second && std::find(it->second->m_initialization.begin(), it->second->m_initialization.end(), "Leave") == it->second->m_initialization.end()) it->second->set(value, false); }
    }
    auto wslpath = std::filesystem::u8path(party_path); wslpath.replace_extension(".wsl");
    wslpath = std::filesystem::u8path(::cw::util::get_yado_writepath(
        path_utf8(wslpath), ::cw::g_cwpy->m_yadodir, ::cw::g_cwpy->m_tempdir));
    log_scenario_data("create_log compress: source=" + path_utf8(logdir) +
                      " destination=" + path_utf8(wslpath));
    ::cw::util::compress_zip(path_utf8(logdir), path_utf8(wslpath), true);
    if (yado->m_deletedpaths && !party_path.empty()) {
        yado->m_deletedpaths->discard(path_utf8(wslpath));
    }
    log_scenario_data("create_log completed: destination=" + path_utf8(wslpath));
}

std::vector<std::tuple<::cw::lib::String, ::cw::lib::Integer, ::cw::lib::Integer, bool, ::cw::lib::String>> ScenarioData::load_log(::cw::lib::String arg_path, bool arg_recording)
{
    using MusicPath = std::tuple<::cw::lib::String, ::cw::lib::Integer, ::cw::lib::Integer, bool, ::cw::lib::String>;
    const auto tree = ::cw::data::xml2etree(arg_path, "", std::nullopt, std::nullopt, false);
    // ネイティブの空の位置指定は、ツリーなしを返す場合がある。 何
    // もしない成功とせず、読み込み状態の更新前に失敗を報告する。
    if (!tree) throw std::runtime_error("ScenarioLog.xml could not be loaded: " + arg_path);
    auto root = tree->getroot();
    // 文書全体の解析はルートなしXMLを拒否済み。 このポインター
    // 検査は保護用であり、その文書を受け付ける第二の経路ではない。
    if (!root) throw std::invalid_argument("ScenarioLog.xml has no root: " + arg_path);
    m_autostart_round = root->getbool("Property/RoundAutoStart", ::cw::lib::Value(false), std::nullopt);
    m_notice_infoview = root->getbool("Property/NoticeInfoView", ::cw::lib::Value(false), std::nullopt);
    m_party_environment_backpack = root->gettext("Property/PartyEnvironment/Backpack", "Enable") != "Disable";
    m_party_environment_gameover = root->gettext("Property/PartyEnvironment/GameOver", "Enable") != "Disable";
    m_party_environment_runaway = root->gettext("Property/PartyEnvironment/RunAway", "Enable") != "Disable";
    if (::cw::g_cwpy && ::cw::g_cwpy->m_statusbar) ::cw::g_cwpy->m_statusbar->m_loading = true;
    _load_variables(::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(tree)));
    if (!arg_recording) {
        if (const auto e = value_to_element(root->getfind("Gossips", true)); e) for (const auto& child : e->m_children) if (child && (child->get("value", "") == "True" || child->get("value", "") == "False")) m_gossips[child->m_text] = child->get("value", "") == "True";
        if (const auto e = value_to_element(root->getfind("CompleteStamps", true)); e) for (const auto& child : e->m_children) if (child && (child->get("value", "") == "True" || child->get("value", "") == "False")) m_compstamps[child->m_text] = child->get("value", "") == "True";
    }
    m_infocards.clear(); m_infocard_maxindex = 0;
    if (const auto e = value_to_element(root->getfind("InfoCards", true)); e) for (auto it = e->m_children.rbegin(); it != e->m_children.rend(); ++it) if (*it) {
        // Pythonのint(e.text)と同じ厳密な整数変換を行う。
        // シナリオログのIDには浮動小数点表記や不正な接尾辞を許さない。
        const auto id = ::cw::lib::require_python_integer((*it)->m_text);
        if (m__infos.contains(id)) append_infocard(id);
    }
    m_friendcards.clear();
    if (const auto e = value_to_element(root->getfind("CastCards", true)); e) for (const auto& child : e->m_children) if (child) {
        auto carddata = child;
        if (child->m_tag == "FriendCard") {
            // 変換した宿はキャストIDだけを保存する。 空・不正IDの失敗とUnicode
            // 十進数字も含め、Pythonの int(e.text) に合わせる。
            const auto cast = get_castdata(::cw::lib::require_python_integer(child->m_text), "", true, std::nullopt);
            if (!cast || !*cast) continue;
            carddata = *cast;
        }
        auto friendcard = std::make_shared<::cw::sprite::card::FriendCard>();
        friendcard->initialize(::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(carddata)), 0);
        m_friendcards.push_back(std::move(friendcard));
    }
    // Python版と同様、同行カードの構築後、背景の読み込み
    // 前にこれらの集合を復元する。 削除予定を早く公開しない。
    if (!arg_recording) {
        if (const auto e = value_to_element(root->getfind("DeletedFiles", true)); e) {
            auto object = std::get_if<::cw::lib::ObjectPtr>(&m_deletedpaths.storage());
            auto sequence = object && *object ? std::dynamic_pointer_cast<VariantSequence>(*object) : nullptr;
            if (!sequence) { sequence = std::make_shared<VariantSequence>(); m_deletedpaths = ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(sequence)); }
            for (const auto& child : e->m_children) if (child) {
                const auto found = std::find_if(sequence->m_values.begin(), sequence->m_values.end(),
                    [&](const auto& value) {
                        const auto* path = std::get_if<::cw::lib::String>(&value.storage());
                        return path && *path == child->m_text;
                    });
                if (found == sequence->m_values.end()) sequence->m_values.emplace_back(child->m_text);
            }
        }
        if (const auto e = value_to_element(root->getfind("LostAdventurers", true)); e) for (const auto& child : e->m_children) if (child) m_lostadventurers.insert(child->m_text);
    }
    // Python版と同様、背景セルが復元済みのシナリオ状態に依存
    // する場合があるため、 評価前に情報・同行カードを復元する。
    const auto background_elements = root->find_exists("BgImages");
    if (::cw::g_cwpy && ::cw::g_cwpy->m_background) {
        auto elements = get_bgdata(background_elements->m_children);
        ::cw::g_cwpy->m_background->load(
            std::move(elements), ::cw::lib::Value(false),
            {::cw::lib::String("Default"), ::cw::lib::Value("Default")},
            false, true, true);
    }
    m_moved_mcards.clear();
    if (const auto e = root->find("MovedCards"); e && *e) for (const auto& child : (*e)->m_children) if (child) {
        const auto group = child->get("cardgroup", ""); const auto index = child->getint(".", ::cw::lib::Value("index"), -1); if (group.empty() || index == -1) continue;
        const auto x = child->getint("Location", ::cw::lib::Value("left"), 0);
        const auto y = child->getint("Location", ::cw::lib::Value("top"), 0);
        const auto scale = child->getint("Size", ::cw::lib::Value("scale"), -1);
        const auto width = child->getint("Size", ::cw::lib::Value("width"), -1);
        const auto height = child->getint("Size", ::cw::lib::Value("height"), -1);
        const auto layer = std::clamp(child->getint("Layer", ::cw::lib::Value(::cw::lib::Integer{-1}), std::nullopt), static_cast<::cw::lib::Integer>(-1), ::cw::g_LAYER_MAX);
        m_moved_mcards[std::make_tuple(group, index)] = std::make_tuple(x, y, scale, layer, width, height);
    }
    m_startid = root->getint("Property/AreaId", std::nullopt, std::nullopt); if (::cw::g_cwpy) ::cw::g_cwpy->m_areaid = m_startid;
    const auto logfile = root->gettext("Property/LogFile", "");
    if (::cw::g_cwpy && ::cw::g_cwpy->m_advlog) ::cw::g_cwpy->m_advlog->resume_scenario(logfile);
    std::vector<MusicPath> musicpaths;
    if (::cw::g_cwpy) {
        musicpaths.reserve(::cw::thread::CWPy::s_music.size());
        for (const auto& music : ::cw::thread::CWPy::s_music) {
            // Python版はこれらを直接読み、チャンネルなしでは失敗する。 ネイ
            // ティブは不正ポインターをlogic_errorとして報告し、 null
            // 参照や、成功したように見える既定のスナップショットを作らない。
            if (!music) throw std::logic_error("ScenarioData.load_log: missing music channel");
            musicpaths.emplace_back(
                music->m_path, music->m_subvolume, music->m_loopcount,
                music->m_inusecard, music->m_fpath);
        }
    }
    if (const auto e = root->find("Property/MusicPaths"); e && *e) {
        for (std::size_t i = 0; i < e.value()->m_children.size(); ++i) {
            const auto& child = e.value()->m_children[i];
            if (!child) continue;
            const auto channel = child->getint(
                ".", ::cw::lib::Value("channel"), static_cast<::cw::lib::Integer>(i));
            const auto path = child->m_text;
            const auto subvolume = child->getint(
                ".", ::cw::lib::Value("volume"), 100);
            const auto loopcount = child->getint(
                ".", ::cw::lib::Value("loopcount"), 0);
            const auto inusecard = child->getbool(
                ".", ::cw::lib::Value("inusecard"), false);
            const auto fullpath = child->get(
                "path", "");
            if (channel >= 0 && static_cast<std::size_t>(channel) < musicpaths.size())
                musicpaths[static_cast<std::size_t>(channel)] =
                    MusicPath{path, subvolume, loopcount, inusecard, fullpath};
        }
    } else if (const auto e = root->find("Property/MusicPath"); e && *e) {
        const auto channel = e.value()->getint(
            ".", ::cw::lib::Value("channel"), 0);
        const auto path = e.value()->m_text;
        const auto subvolume = e.value()->getint(
            ".", ::cw::lib::Value("volume"), 100);
        const auto loopcount = e.value()->getint(
            ".", ::cw::lib::Value("loopcount"), 0);
        const auto inusecard = e.value()->getbool(
            ".", ::cw::lib::Value("inusecard"), false);
        const auto fullpath = e.value()->get("path", "");
        if (channel >= 0 && static_cast<std::size_t>(channel) < musicpaths.size())
            musicpaths[static_cast<std::size_t>(channel)] =
                MusicPath{path, subvolume, loopcount, inusecard, fullpath};
    }
    if (::cw::g_fsync) ::cw::g_fsync->sync();
    return musicpaths;
}

void ScenarioData::update_log()
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_ydata || !*::cw::g_cwpy->m_ydata ||
        !(*::cw::g_cwpy->m_ydata)->m_party || !*(*::cw::g_cwpy->m_ydata)->m_party)
        throw std::logic_error("update_log requires an active inn and party");
    const auto party = (*::cw::g_cwpy->m_ydata)->m_party.value();
    if (!::cw::g_cwpy->m_advlog)
        throw std::logic_error("update_log requires an adventure log");
    // 同期の書き込み処理は、この受け手を読むが保持しない。 shared_ptrの
    // APIへ接続するときも、selfを実行環境のm_sdataへ置き換えない。
    auto self = std::shared_ptr<ScenarioData>(this, [](ScenarioData*) {});
    const auto logdir = std::filesystem::u8path(scenario_value_string(::cw::g_tempdir)) / "ScenarioLog";
    ::cw::xmlcreater::create_scenariolog(self, path_utf8(logdir / "ScenarioLog.xml"), false,
        ::cw::g_cwpy->m_advlog->m_logfilepath);
    ::cw::g_cwpy->m_advlog->end_scenario(false, false);
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&party->m_data.storage());
    const auto tree = object && *object ? std::dynamic_pointer_cast<CWPyElementTree>(*object) : nullptr;
    if (!tree) throw std::logic_error("update_log requires the party XML tree");
    auto path = std::filesystem::u8path(tree->m_fpath); path.replace_extension(".wsl");
    // パーティログは共通の宿用書き込み処理を通し、保存
    // ・一時ルートを対応する書き込み先へ解決する。
    // Python版はこのルート置換をその場で行う。
    path = std::filesystem::u8path(::cw::util::get_yado_writepath(
        path_utf8(path), ::cw::g_cwpy->m_yadodir, ::cw::g_cwpy->m_tempdir));
    ::cw::util::compress_zip(path_utf8(logdir), path_utf8(path), true);
}

// システムのパスの後に、共通処理で整列したファイル順でシナリオBGM
// を追加する。 Python版はos.walkの発見順で追加する。
std::vector<::cw::lib::String> ScenarioData::get_bgmpaths()
{
    auto result = SystemData::get_bgmpaths(); const auto tempdir = scenario_value_string(m_tempdir);
    const auto requested = std::filesystem::u8path(tempdir);
    const auto base = ::cardwirth::platform::resolve_mounted_user_path(requested).value_or(requested);
    if (tempdir.empty() || !mounted_or_native_is_directory(base)) return result;
    // ALL はシナリオをマウント領域だけに持つ場合がある。 物理ディレ
    // クトリを必須としたり、相対名をホストのディスクで解決したりしない。
    for (const auto& entry : mounted_or_native_files_recursive(base)) {
        const auto ext = scenario_lower(entry.extension().generic_string()); if (ext != ".ogg" && ext != ".mp3" && ext != ".mid" && ext != ".wav") continue;
        result.push_back(relative_path_utf8(entry, base));
    }
    return result;
}

void ScenarioData::fullrecovery_fcards()
{
    std::vector<std::shared_ptr<::cw::sprite::card::FriendCard>> result;
    for (const auto& card : m_friendcards) if (card) {
        const auto previous = get_versionhint(0); set_versionhint(0, card->m_versionhint_typed);
        {
            auto recovered = card;
            if (::cw::g_cwpy && ::cw::g_cwpy->m_sct &&
                ::cw::g_cwpy->m_sct->lessthan("1.28", get_versionhint(0))) {
                // Python版と同様、この受け手とは異なる場合もある cw.cwpy.sdata
                // から解決する。 FriendCardは、終
                // 了したカードの位置ではなく既定の0で作る。
                std::shared_ptr<SystemData> active_data;
                if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_sdata.storage()); object && *object)
                    active_data = std::dynamic_pointer_cast<SystemData>(*object);
                if (!active_data) throw std::logic_error("fullrecovery_fcards requires active scenario data");
                const auto cast = active_data->get_castdata(card->m_id, "", true, std::nullopt);
                if (cast && *cast) {
                    auto tree = std::make_shared<CWPyElementTree>(*cast, (*cast)->m_fpath);
                    recovered = std::make_shared<::cw::sprite::card::FriendCard>();
                    recovered->initialize(::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(tree)), 0);
                }
            } else {
                recovered->set_fullrecovery();
                recovered->CWPyCard::update_image(false, std::nullopt);
            }
            result.push_back(std::move(recovered));
        }set_versionhint(0, previous);
    }
    m_friendcards = std::move(result);
}

::cw::lib::Value g_VariantValueType{};
std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String> g__WSN_DATA_DIRS{"area", "battle", "package", "castcard", "skillcard", "itemcard", "beastcard", "infocard"};
::cw::lib::Value g__lock{};

std::vector<::cw::lib::String> parse_initialization(std::optional<::cw::lib::Value> arg_value)
{
    using ::cw::lib::Value;
    using ::cw::lib::String;
    std::vector<String> result;
    if (!arg_value || arg_value->is_none()) return result;
    // Python版と同様、文字列を分割するのは外側の列だけ。 入れ子の列
    // はreprに近い一つの文字列に変え、再帰的にトークンへ平坦化しない。
    std::set<const VariantSequence*> active;
    const auto stringify = [&active](const auto& self, const Value& value, bool repr) -> String {
        if (value.is_none()) return "None";
        if (const auto* text = std::get_if<String>(&value.storage()))
            return repr ? ::cw::lib::python_percent_detail::repr(*text, false) : *text;
        if (const auto* boolean = std::get_if<bool>(&value.storage())) return *boolean ? "True" : "False";
        if (const auto* integer = std::get_if<::cw::lib::Integer>(&value.storage())) return std::to_string(*integer);
        if (const auto* real = std::get_if<::cw::lib::Real>(&value.storage())) return ::cw::lib::python_float_repr(*real);
        if (const auto* decimal = std::get_if<::cw::lib::Decimal>(&value.storage())) {
            const auto text = decimal->to_string();
            return repr ? "Decimal(" + ::cw::lib::python_percent_detail::repr(text, false) + ")" : text;
        }
        if (const auto* bytes = std::get_if<::cw::lib::Bytes>(&value.storage())) {
            const char quote = std::find(bytes->begin(), bytes->end(), '\'') != bytes->end() &&
                               std::find(bytes->begin(), bytes->end(), '"') == bytes->end() ? '"' : '\'';
            String text = "b"; text += quote;
            constexpr char hex[] = "0123456789abcdef";
            for (const auto ch : *bytes) {
                if (ch == static_cast<unsigned char>(quote) || ch == '\\') { text += '\\'; text += static_cast<char>(ch); }
                else if (ch == '\n') text += "\\n";
                else if (ch == '\r') text += "\\r";
                else if (ch == '\t') text += "\\t";
                else if (ch < 0x20 || ch >= 0x7f) { text += "\\x"; text += hex[ch >> 4]; text += hex[ch & 15]; }
                else text += static_cast<char>(ch);
            }
            text += quote; return text;
        }
        if (const auto* ptr = std::get_if<::cw::lib::ObjectPtr>(&value.storage()); ptr && *ptr) {
            if (const auto list = std::dynamic_pointer_cast<VariantSequence>(*ptr)) {
                if (!active.insert(list.get()).second) return "[...]";
                String text = "[";
                for (std::size_t i = 0; i < list->m_values.size(); ++i) {
                    if (i) text += ", ";
                    text += self(self, list->m_values[i], true);
                }
                active.erase(list.get());
                return text + "]";
            }
        }
        // ネイティブObjectにはPythonの __str__・クラス
        // ・アドレスの規則がない。 定義された単一値・文字列列の範囲外では
        // 、トークンを黙って捨てたり作ったりせず、その言語境界を報告する。
        throw std::invalid_argument("initialization cannot stringify unsupported native object");
    };
    const auto append = [&result, &stringify](const Value& value) {
        if (value.is_none()) return;
        if (const auto* text = std::get_if<String>(&value.storage())) {
            auto parts = ::cw::lib::python_split_whitespace(*text);
            result.insert(result.end(), parts.begin(), parts.end());
        } else {
            const auto token = stringify(stringify, value, false);
            if (!token.empty()) result.push_back(token);
        }
    };
    if (const auto* ptr = std::get_if<::cw::lib::ObjectPtr>(&arg_value->storage()); ptr && *ptr) {
        if (const auto list = std::dynamic_pointer_cast<VariantSequence>(*ptr)) {
            for (const auto& value : list->m_values) append(value);
            return result;
        }
    }
    append(*arg_value);
    return result;
}


void SystemData_update_skin_func()
{
    if (!::cw::g_cwpy) return;
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_sdata.storage());
    if (object && *object) if (const auto data = std::dynamic_pointer_cast<SystemData>(*object)) data->update_skin();
}

void SystemData_update_scenariopath_update_table(std::map<::cw::lib::Integer, std::tuple<::cw::lib::String, ::cw::lib::String>> arg_table)
{
    // Pythonの内部 update_table に相当する各表の更新は、SystemData::update_scenariopath
    // 本体で行う。 生成された補助関数には、対
    // 象のSystemDataを渡す引数がない。
    (void)arg_table;
}

std::tuple<std::set<::cw::lib::String>, std::map<::cw::lib::String, std::tuple<::cw::lib::String, ::cw::lib::String>>> get_skinkeys()
{
    std::set<::cw::lib::String> skins;
    std::map<::cw::lib::String, std::tuple<::cw::lib::String, ::cw::lib::String>> keytable;
    const std::filesystem::path root("Data/Skin");
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec)) return {std::move(skins), std::move(keytable)};
    for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
        if (ec) { ec.clear(); continue; }
        if (!entry.is_directory(ec)) continue;
        const auto skinpath = entry.path() / "Skin.xml";
        if (!std::filesystem::is_regular_file(skinpath, ec)) continue;
        try {
            auto property = std::make_shared<::cw::header::GetProperty>();
            property->initialize(path_utf8(skinpath), std::nullopt, std::nullopt);
            const auto key = property->m_properties.contains("VariablesKey") ? property->m_properties.at("VariablesKey") : ::cw::lib::String{};
            if (key.empty()) continue;
            const auto name = property->m_properties.contains("Name") ? property->m_properties.at("Name") : ::cw::lib::String{};
            const auto author = property->m_properties.contains("Author") ? property->m_properties.at("Author") : ::cw::lib::String{};
            skins.insert(key);
            const auto candidate = std::make_tuple(name, author);
            const auto it = keytable.find(key);
            if (it == keytable.end() || candidate < it->second) keytable[key] = candidate;
        } catch (...) {
            // 壊れたSkin.xmlはPython版と同じく一覧から除外する。
            ::cw::util::print_ex(std::nullopt);
        }
    }
    return {std::move(skins), std::move(keytable)};
}

void ScenarioData__decompress_startup(::cw::lib::Integer arg_filenum)
{
    if (auto* data = active_scenario_data()) { data->m__filenum = arg_filenum; data->m__progress = true; }
}

void ScenarioData__decompress_startup_func()
{
    if (auto* data = active_scenario_data()) data->m__progress = true;
}

bool ScenarioData__decompress_progress(::cw::lib::Integer arg_cur)
{
    (void)arg_cur; // これは進捗であり、起動時の総数 _filenum ではない。
    if (auto* data = active_scenario_data()) {
        return data->m__cancel_decompress;
    }
    return false;
}

void ScenarioData__decompress_progress_func()
{
    if (auto* data = active_scenario_data()) data->m__progress = true;
}

void ScenarioData__decompress_run_decompress()
{
    if (auto* data = active_scenario_data()) data->_decompress(true);
}

::cw::lib::DateTime ScenarioData_resume_timekeeper_parse_datetime(::cw::lib::String arg_tag)
{
    // 解析本体は ScenarioData::resume_timekeeper() が XML 属性を保持したまま
    // 復元する。互換 callback 単体で呼ばれた場合は、保持済みStartTimeまたは
    // Python版の壊れた/欠落した時刻と同じく現在時刻へフォールバックする。
    if (auto* data = active_scenario_data(); data && arg_tag == "StartTime" && data->m__start_datetime)
        return *data->m__start_datetime;
    return ::cw::lib::DateTime{std::chrono::system_clock::now()};
}

void ScenarioData_update_skin_func()
{
    if (auto* data = active_scenario_data()) data->update_skin();
}

void ScenarioData__reload_func()
{
    if (auto* data = active_scenario_data()) data->_reload();
}

std::tuple<std::shared_ptr<::cw::lib::Surface>, bool> ScenarioData_eat_spchar_load(::cw::lib::String arg_dpath, ::cw::lib::String arg_fname)
{
    const auto can_loaded_scaledimage = active_scenario_data() ? active_scenario_data()->m_can_loaded_scaledimage : true;
    return load_scenario_specialchar(arg_dpath, arg_fname, can_loaded_scaledimage);
}

std::map<::cw::lib::String, std::shared_ptr<::cw::data::Flag>> init_flags(::cw::lib::Value arg_data, bool arg_writable)
{
    std::map<::cw::lib::String, std::shared_ptr<::cw::data::Flag>> result;
    const auto root = value_to_element(arg_data);
    if (!root) return result;
    const auto group = root->find("Flags");
    if (!group || !*group) return result;
    ::cw::lib::Value parent = arg_data;
    for (const auto& element : (*group)->m_children) {
        if (!element) continue;
        const auto defaultvalue = element->getbool(".", ::cw::lib::Value("default"), std::nullopt);
        const auto value = element->getbool(".", ::cw::lib::Value("value"), defaultvalue);
        const auto name = element->gettext("Name", "");
        const auto truename = element->gettext("True", "");
        const auto falsename = element->gettext("False", "");
        const auto spchars = element->getbool(".", ::cw::lib::Value("spchars"), false);
        auto flag = std::make_shared<Flag>();
        flag->initialize(arg_writable ? std::optional<::cw::lib::Value>(parent) : std::nullopt,
                         std::optional<std::shared_ptr<CWPyElement>>(element),
                         value, name, truename, falsename, defaultvalue, spchars);
        // Python版と同様、空のNameも保持し、同じキーは最後の定義を使う。
        result.insert_or_assign(name, std::move(flag));
    }
    return result;
}

std::map<::cw::lib::String, std::shared_ptr<::cw::data::Step>> init_steps(::cw::lib::Value arg_data, bool arg_writable)
{
    std::map<::cw::lib::String, std::shared_ptr<::cw::data::Step>> result;
    const auto root = value_to_element(arg_data);
    if (!root) return result;
    const auto group = root->find("Steps");
    if (!group || !*group) return result;
    ::cw::lib::Value parent = arg_data;
    for (const auto& element : (*group)->m_children) {
        if (!element) continue;
        const auto defaultvalue = element->getint(".", ::cw::lib::Value("default"), std::nullopt);
        const auto value = element->getint(".", ::cw::lib::Value("value"), defaultvalue);
        const auto name = element->gettext("Name", "");
        std::vector<::cw::lib::String> valuenames;
        for (const auto& child : element->m_children) {
            if (child && child->m_tag.rfind("Value", 0) == 0) valuenames.push_back(child->m_text);
        }
        auto step = std::make_shared<Step>();
        step->initialize(arg_writable ? std::optional<::cw::lib::Value>(parent) : std::nullopt,
                         std::optional<std::shared_ptr<CWPyElement>>(element),
                         value, name, std::move(valuenames), defaultvalue,
                         element->getbool(".", ::cw::lib::Value("spchars"), false));
        // Python版と同様、XMLを順番に走査してから辞書へ代入する。
        result.insert_or_assign(name, std::move(step));
    }
    return result;
}

std::map<::cw::lib::String, std::shared_ptr<::cw::data::Variant>> init_variants(::cw::lib::Value arg_data, bool arg_writable)
{
    std::map<::cw::lib::String, std::shared_ptr<::cw::data::Variant>> result;
    const auto root = value_to_element(arg_data);
    if (!root) return result;
    const auto group = root->find("Variants");
    if (!group || !*group) return result;
    ::cw::lib::Value parent = arg_data;
    for (const auto& element : (*group)->m_children) {
        if (!element) continue;
        // 定義名はPython版と同様、Nameから取得する。 _get_vartables
        // がname属性を読む宿の保存変数キャッシュは、別の形式。
        const auto name = element->gettext("Name", "");
        const auto defaultvalue = Variant::value_from_element(element, "defaulttype", std::optional<::cw::lib::String>("defaultvalue"));
        const auto value = element->get("type", "").empty()
            ? defaultvalue
            : Variant::value_from_element(
                element, "type", std::optional<::cw::lib::String>("value"));
        auto variant = std::make_shared<Variant>();
        variant->initialize(arg_writable ? std::optional<::cw::lib::Value>(parent) : std::nullopt,
                            std::optional<std::shared_ptr<CWPyElement>>(element),
                            value, name, defaultvalue);
        result.insert_or_assign(name, std::move(variant));
    }
    return result;
}

void redraw_cards(bool arg_value, ::cw::lib::String arg_flag, bool arg_silent)
{
    if (!::cw::g_cwpy) return;
    const auto* camp = std::get_if<::cw::lib::Integer>(&::cw::g_AREA_CAMP.storage());
    const bool quickdeal = camp && ::cw::g_cwpy->m_areaid == *camp && ::cw::g_cwpy->m_setting &&
        ::cw::g_cwpy->m_setting->m_all_quickdeal;
    if (::cw::g_cwpy->is_autospread()) {
        bool drawflag = false;
        for (const auto& value : ::cw::g_cwpy->get_mcards({}, arg_flag)) {
            const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
            const auto card = object ? std::dynamic_pointer_cast<::cw::sprite::card::CWPyCard>(*object) : nullptr;
            if (!card) continue;
            const bool visible = card->m_status != "hidden";
            if ((card->is_flagtrue() && !visible) || (!card->is_flagtrue() && visible)) {
                drawflag = true;
                break;
            }
        }
        if (drawflag) {
            // ネイティブの型境界として、移動済みカードの状態を持つのはScenarioData
            // 。 該当する型ならアニメーション前に消し、不在
            // ・別のSystemDataには消す状態がない。
            if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_sdata.storage())) {
                if (const auto sdata = std::dynamic_pointer_cast<::cw::data::ScenarioData>(*object)) sdata->m_moved_mcards.clear();
            }
            ::cw::g_cwpy->hide_cards(true, true, quickdeal, true, arg_flag, arg_silent);
            ::cw::g_cwpy->deal_cards(quickdeal, true, arg_flag, false, arg_silent);
        }
    } else if (arg_value) {
        ::cw::g_cwpy->deal_cards(quickdeal, false, arg_flag, false, arg_silent);
    } else {
        ::cw::g_cwpy->hide_cards(false, true, quickdeal, false, arg_flag, arg_silent);
    }
}

::cw::lib::String Variant_value_to_str_to_str(::cw::lib::Value arg_val)
{
    return Variant::value_to_str(std::move(arg_val), false);
}

::cw::lib::Value Variant_value_to_str_lambda_line_2636(::cw::lib::Value arg_s)
{
    return ::cw::lib::Value(Variant::value_to_str(std::move(arg_s), false));
}

void YadoData_save_commit_timekeeper(::cw::lib::String arg_ppath, bool arg_is_adventuring)
{
    const auto yadodir = ::cw::g_cwpy ? ::cw::g_cwpy->m_yadodir : ::cw::lib::String{};
    const auto tempdir = ::cw::g_cwpy ? ::cw::g_cwpy->m_tempdir : ::cw::lib::String{};
    commit_party_timekeeper(arg_ppath, arg_is_adventuring, yadodir, tempdir);
}

void YadoData_save_update_backpack(std::shared_ptr<::cw::data::Party> arg_party)
{
    if (!arg_party) return;
    const auto path = scenario_value_string(arg_party->m_path);
    if (path.empty()) return;
    const auto ppath = path_utf8(std::filesystem::u8path(path).parent_path());
    const auto yadodir = arg_party->get_yadodir();
    const auto tempdir = arg_party->get_tempdir();
    const auto inn_tempdir = ::cw::g_cwpy ? ::cw::g_cwpy->m_tempdir : ::cw::lib::String{};
    const auto inn_yadodir = ::cw::g_cwpy ? ::cw::g_cwpy->m_yadodir : ::cw::lib::String{};
    std::map<::cw::lib::String, ::cw::lib::Integer> cardorder;
    std::map<::cw::lib::String, std::shared_ptr<::cw::header::CardHeader>> cardheaders;
    for (const auto& card : arg_party->m_backpack) if (card) {
        card->do_write(false);
        const auto key = save_rel_key(card->m_fpath, yadodir, tempdir);
        rewrite_temp_path(card->m_fpath, inn_tempdir, inn_yadodir);
        cardorder[key] = value_integer_or(card->m_order, 0);
        cardheaders[key] = card;
    }
    // 外側の宿保存と同様、提供側の索引機能に従う。 ネイテ
    // ィブを使うマウントには、通常の宿と同じ保存されたカー
    // ド順が必要。 暗号化・仮想領域は、平文SQLite索
    // 引を禁じる機能方針と、保存直後に捨てる短命な再構築
    // のため、 意図的にXMLを正式なデータとして扱う。
    const auto index_capabilities = ::cardwirth::platform::mounted_user_storage_capabilities(
        std::filesystem::u8path(ppath));
    if (!index_capabilities || index_capabilities->requires_native_secondary_indexes) {
        ::cw::yadodb::YadoDB db;
        db.initialize(ppath, ::cw::yadodb::g_PARTY);
        db.update(true, false, false, cardorder, std::nullopt, std::nullopt, false, false, std::move(cardheaders));
        db.close();
    }
}

void YadoData_save_update_database(::cw::lib::String arg_yadodir)
{
    if (arg_yadodir.empty()) return;
    {
        ::cw::yadodb::YadoDB db;
        db.initialize(arg_yadodir, ::cw::yadodb::g_YADO);
        db.update(true, true, true, std::nullopt, std::nullopt, std::nullopt, true, true);
        db.close();
    }}

::cw::lib::String find_scefullpath(::cw::lib::String arg_scepath, std::vector<::cw::lib::String> arg_spaths)
{
    for (const auto& part : arg_spaths) {
        arg_scepath = ::cw::util::join_paths({
            ::cw::lib::Value(::cw::util::get_linktarget(arg_scepath)),
            ::cw::lib::Value(part)});
        // 保存領域のパスは共通の存在確認APIを使う。 ネイティブのファイル
        // システム検査では、Web・暗号化のマウント済みシナリオが見えない。
        if (!::cw::util::path_exists(arg_scepath)) return {};
    }
    // Python版の最後のnormpath(scepath)は、以前のabspath
    // 結果を上書きする。 空パスの結果「.」も含め、相対パスは相対のままにする。
    if (arg_scepath.empty()) return ".";
    auto normalized = std::filesystem::u8path(arg_scepath).lexically_normal();
    // C++の字句的な正規化は末尾の空要素を残すが、os.path.normpath
    // は除く。 ルートは保ちつつ、その要素を除く。
    while (!normalized.has_filename() && normalized != normalized.root_path()) {
        const auto parent = normalized.parent_path();
        if (parent == normalized) break;
        normalized = parent;
    }
    return path_utf8(normalized);
}

void sort_cards(std::vector<std::shared_ptr<::cw::header::CardHeader>>& arg_cards, ::cw::lib::String arg_condition, bool arg_withstar)
{
    const bool show_personal = ::cw::g_cwpy && ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_show_personal_cards;
    enum class Key { PersonalOwner, Star, Name, Scenario, Author, Type, Level, Price, Aptitude, Order };
    std::vector<Key> keys;
    if (show_personal) keys.push_back(Key::PersonalOwner);
    if (arg_withstar) keys.push_back(Key::Star);
    bool has_explicit_sort = false;
    if (arg_condition == "Level") { keys.push_back(Key::Level); has_explicit_sort = true; }
    else if (arg_condition == "Name") { keys.push_back(Key::Name); has_explicit_sort = true; }
    else if (arg_condition == "Type") { keys.push_back(Key::Type); has_explicit_sort = true; }
    else if (arg_condition == "Price") { keys.push_back(Key::Price); has_explicit_sort = true; }
    else if (arg_condition == "Scenario") { keys.push_back(Key::Scenario); has_explicit_sort = true; }
    else if (arg_condition == "Author") { keys.push_back(Key::Author); has_explicit_sort = true; }
    else if (arg_condition == "Aptitude") keys.push_back(Key::Aptitude);
    if (has_explicit_sort) {
        for (const auto key : {Key::Name, Key::Scenario, Key::Author, Key::Type, Key::Level, Key::Price}) {
            if (keys.empty() || keys.front() != key) keys.push_back(key);
        }
    }
    keys.push_back(Key::Order);
    const auto compare_integer = [](auto lhs, auto rhs) {
        return lhs < rhs ? -1 : (lhs > rhs ? 1 : 0);
    };
    const auto compare_string = [](const ::cw::lib::String& lhs, const ::cw::lib::String& rhs) {
        // Pythonのsort_cardsと同様、util.sort_by_attr経由のLogicalStr
        // の規則を使う。 通常の型付きカード経路で、同じ共通のUnicode・数値順を維持する。
        return static_cast<int>(::cw::util::logical_compare(lhs, rhs));
    };
    const auto compare = [&](const std::shared_ptr<::cw::header::CardHeader>& lhs,
                             const std::shared_ptr<::cw::header::CardHeader>& rhs) {
        // Python版は各項目にCardHeaderがある前提
        // 。 ネイティブの不在ヘッダーは参照せず安定した先頭位置
        // に置き、 実際のヘッダーは下で同じキー順を維持する。
        if (!lhs || !rhs) return lhs ? 1 : (rhs ? -1 : 0);
        for (const auto key : keys) {
            int result = 0;
            switch (key) {
            case Key::PersonalOwner:
                result = compare_integer(value_integer_or(std::get<0>(lhs->m_personal_owner_index), -1), value_integer_or(std::get<0>(rhs->m_personal_owner_index), -1));
                if (result == 0) result = compare_integer(value_integer_or(std::get<1>(lhs->m_personal_owner_index), -1), value_integer_or(std::get<1>(rhs->m_personal_owner_index), -1));
                break;
            case Key::Star: result = compare_integer(lhs->get_negastar(), rhs->get_negastar()); break;
            case Key::Level: result = compare_integer(lhs->m_level, rhs->m_level); break;
            case Key::Type: result = compare_integer(lhs->m_type_id, rhs->m_type_id); break;
            case Key::Price: result = compare_integer(lhs->get_sellingprice(), rhs->get_sellingprice()); break;
            case Key::Aptitude: result = compare_integer(lhs->m_vocation_for_sort, rhs->m_vocation_for_sort); break;
            case Key::Order: result = compare_integer(value_integer_or(lhs->m_order, 0), value_integer_or(rhs->m_order, 0)); break;
            case Key::Name: result = compare_string(lhs->m_name, rhs->m_name); break;
            case Key::Scenario: result = compare_string(lhs->m_scenario, rhs->m_scenario); break;
            case Key::Author: result = compare_string(lhs->m_author, rhs->m_author); break;
            }
            if (result != 0) return result;
        }
        return 0;
    };
    std::stable_sort(arg_cards.begin(), arg_cards.end(), [&](const auto& lhs, const auto& rhs) { return compare(lhs, rhs) < 0; });
}

void sort_cards_addetckey()
{
    // Pythonの内部 addetckey に相当する処理は、sort_cards の比較キーの構築で行う。
}

::cw::lib::Value CWPyElement_extend_lambda_line_4928(::cw::lib::Value arg_e)
{
    // Pythonのlambdaは親設定後に要素を返すだけで、親設定はextend本体へ
    // inline済み。生成helperも入力要素を保持して返す。
    return arg_e;
}

std::shared_ptr<::cw::data::CWPyElement> make_element(::cw::lib::String arg_name, ::cw::lib::String arg_text, std::optional<std::map<::cw::lib::String, ::cw::lib::String>> arg_attrs, ::cw::lib::String arg_tail)
{
    return std::make_shared<CWPyElement>(std::move(arg_name), std::move(arg_text),
        arg_attrs.value_or(std::map<::cw::lib::String, ::cw::lib::String>{}),
        std::move(arg_tail));
}

static std::shared_ptr<::cw::data::CWPyElementTree> yadoxml2etree_impl(::cw::lib::String arg_path, ::cw::lib::String arg_tag, std::optional<std::map<::cw::lib::String, ::cw::lib::String>> arg_rootattrs, ::cw::lib::String saved_root, ::cw::lib::String temp_root, std::map<::cw::lib::String, ::cw::lib::String>* output)
{
    auto root = saved_root.empty()?yadoxml2element_impl(arg_path, arg_tag, std::move(arg_rootattrs), output)
        : read_yadoxml_impl(arg_path,std::move(saved_root),std::move(temp_root),arg_tag,std::move(arg_rootattrs), ".", output);
    if (!root) return nullptr;
    auto actual = root->m_fpath.empty() ? std::move(arg_path) : root->m_fpath;
    auto tree = std::make_shared<CWPyElementTree>(std::move(root), actual);
    const auto native = ::cw::lib::encoding::path_from_utf8(actual);
    tree->m_user_storage_backed = ::cardwirth::platform::is_mounted_user_path(native);
    return tree;
}

std::shared_ptr<::cw::data::CWPyElementTree> yadoxml2etree(::cw::lib::String arg_path, ::cw::lib::String arg_tag, std::optional<std::map<::cw::lib::String, ::cw::lib::String>> arg_rootattrs, ::cw::lib::String saved_root, ::cw::lib::String temp_root)
{
    return yadoxml2etree_impl(std::move(arg_path), std::move(arg_tag), std::move(arg_rootattrs), std::move(saved_root), std::move(temp_root), nullptr);
}

std::shared_ptr<::cw::data::CWPyElementTree> yadoxml2etree(::cw::lib::String arg_path, ::cw::lib::String arg_tag, std::map<::cw::lib::String, ::cw::lib::String>& arg_rootattrs, ::cw::lib::String saved_root, ::cw::lib::String temp_root)
{
    return yadoxml2etree_impl(std::move(arg_path), std::move(arg_tag), std::nullopt, std::move(saved_root), std::move(temp_root), &arg_rootattrs);
}


static std::shared_ptr<::cw::data::CWPyElement> yadoxml2element_impl(::cw::lib::String arg_path, ::cw::lib::String arg_tag, std::optional<std::map<::cw::lib::String, ::cw::lib::String>> arg_rootattrs, std::map<::cw::lib::String, ::cw::lib::String>* output)
{
    if(::cw::g_cwpy && !::cw::g_cwpy->m_yadodir.empty() && !::cw::g_cwpy->m_tempdir.empty())
        return read_yadoxml_impl(std::move(arg_path),::cw::g_cwpy->m_yadodir,
            ::cw::g_cwpy->m_tempdir,std::move(arg_tag),std::move(arg_rootattrs), ".", output);
    return read_yadoxml_impl(std::move(arg_path),"Yado",
        join_path(scenario_value_string(::cw::g_tempdir),"Yado"),
        std::move(arg_tag),std::move(arg_rootattrs), ".", output);
}

std::shared_ptr<::cw::data::CWPyElement> yadoxml2element(::cw::lib::String arg_path, ::cw::lib::String arg_tag, std::optional<std::map<::cw::lib::String, ::cw::lib::String>> arg_rootattrs)
{
    return yadoxml2element_impl(std::move(arg_path), std::move(arg_tag), std::move(arg_rootattrs), nullptr);
}

std::shared_ptr<::cw::data::CWPyElement> yadoxml2element(::cw::lib::String arg_path, ::cw::lib::String arg_tag, std::map<::cw::lib::String, ::cw::lib::String>& arg_rootattrs)
{
    return yadoxml2element_impl(std::move(arg_path), std::move(arg_tag), std::nullopt, &arg_rootattrs);
}


static std::shared_ptr<::cw::data::CWPyElement> read_yadoxml_impl(
    ::cw::lib::String path, ::cw::lib::String yado_root, ::cw::lib::String temp_root,
    ::cw::lib::String tag,
    std::optional<std::map<::cw::lib::String, ::cw::lib::String>> rootattrs,
    ::cw::lib::String base_directory, std::map<::cw::lib::String, ::cw::lib::String>* output)
{
    const auto requested = path;
    if(path.empty()) throw std::invalid_argument(path + " is not YadoXMLFile.");
    const auto requested_base = std::filesystem::u8path(
        base_directory.empty() ? "." : base_directory);
    const auto base = ::cardwirth::platform::resolve_mounted_user_path(requested_base)
        .value_or(std::filesystem::absolute(requested_base).lexically_normal());
    const auto absolute=[&base](const ::cw::lib::String& value) {
        return ::cw::lib::encoding::path_to_utf8(
            (base / std::filesystem::u8path(value)).lexically_normal());
    };
    auto saved=path;
    ::cw::lib::String temporary;
    if(yado_root.empty() || temp_root.empty())
        throw std::invalid_argument(path + " is not YadoXMLFile.");
    // Python版と同様、ファイルシステム確認より前に文字列の接
    // 頭辞で振り分ける。 Yadoで始まる名前も含め、無関係なファ
    // イルを黙って読んだり、別の区切り検証の規則を加えたりしない。
    const auto route = [&](const ::cw::lib::String& candidate,
                           const ::cw::lib::String& saved_root,
                           const ::cw::lib::String& staging_root) {
        if(candidate.starts_with(saved_root)) {
            saved=candidate;
            temporary=staging_root + candidate.substr(saved_root.size());
            return true;
        }
        if(candidate.starts_with(staging_root)) {
            temporary=candidate;
            saved=saved_root + candidate.substr(staging_root.size());
            return true;
        }
        return false;
    };
    if(!route(path,yado_root,temp_root)) {
        // ネイティブのマウント・移設した保存領域は、絶対パスのファ
        // イル識別情報も保持する。 共通の保存APIに必要なパス形
        // 式の拡張として、全OSで同じ振り分け規則へ対応させる。
        if(!route(absolute(path),absolute(yado_root),absolute(temp_root)))
            throw std::invalid_argument(requested + " is not YadoXMLFile.");
    }
    saved=absolute(saved); temporary=absolute(temporary);
    const auto waiting=[](const ::cw::lib::String& file) {
        return !file.empty() && ::cw::g_fsync && ::cw::g_fsync->is_waiting(file);
    };
    if(waiting(temporary) || waiting(saved)) ::cw::g_fsync->sync();
    const auto exists=[](const ::cw::lib::String& file) {
        if (file.empty()) return false;
        const auto path = ::cw::lib::encoding::path_from_utf8(file);
        if (::cardwirth::platform::is_mounted_user_path(path)) {
            const auto status = ::cardwirth::platform::stat_mounted_user_path(path, true);
            return status && status->is_file();
        }
        std::error_code error;
        const bool found = std::filesystem::is_regular_file(path,error);
        if (error && error != std::errc::no_such_file_or_directory && error != std::errc::not_a_directory)
            throw std::filesystem::filesystem_error("cannot stat XML", path, error);
        return found;
    };
    const auto actual=exists(temporary)?temporary:exists(saved)?saved: ::cw::lib::String{};
    if(actual.empty()) throw std::invalid_argument(requested + " is not found.");
    return xml2element_impl(actual,std::move(tag),std::nullopt,std::nullopt,false,std::move(rootattrs),output);
}

std::shared_ptr<::cw::data::CWPyElement> read_yadoxml(
    ::cw::lib::String path, ::cw::lib::String yado_root, ::cw::lib::String temp_root,
    ::cw::lib::String tag,
    std::optional<std::map<::cw::lib::String, ::cw::lib::String>> rootattrs,
    ::cw::lib::String base_directory)
{
    return read_yadoxml_impl(std::move(path), std::move(yado_root), std::move(temp_root), std::move(tag), std::move(rootattrs), std::move(base_directory), nullptr);
}

std::shared_ptr<::cw::data::CWPyElement> read_yadoxml(
    ::cw::lib::String path, ::cw::lib::String yado_root, ::cw::lib::String temp_root,
    ::cw::lib::String tag,
    std::map<::cw::lib::String, ::cw::lib::String>& rootattrs,
    ::cw::lib::String base_directory)
{
    return read_yadoxml_impl(std::move(path), std::move(yado_root), std::move(temp_root), std::move(tag), std::nullopt, std::move(base_directory), &rootattrs);
}


std::shared_ptr<::cw::data::CWPyElementTree> xml2etree(::cw::lib::String arg_path, ::cw::lib::String arg_tag, std::optional<::cw::lib::Value> arg_stream, std::optional<std::shared_ptr<::cw::data::CWPyElement>> arg_element, bool arg_nocache)
{
    std::shared_ptr<CWPyElement> root;
    if (arg_element && *arg_element) root = *arg_element;
    else root = xml2element(arg_path, std::move(arg_tag), std::move(arg_stream), std::nullopt,
                            arg_nocache, std::nullopt);
    if (!root) return {};
    // Python版と同様、返された要素を変更せずに包む。 特
    // にnocacheの複製はファイル識別情報を持たず、 ツリ
    // ーのラッパーを作るだけで共有する不変の子孫を変更しない。
    const auto fpath = root->m_fpath;
    auto tree = std::make_shared<CWPyElementTree>(std::move(root), fpath);
    const auto native = ::cw::lib::encoding::path_from_utf8(fpath);
    tree->m_user_storage_backed = ::cardwirth::platform::is_mounted_user_path(native);
    return tree;
}

static std::shared_ptr<::cw::data::CWPyElement> xml2element_impl(::cw::lib::String arg_path, ::cw::lib::String arg_tag, std::optional<::cw::lib::Value> arg_stream, std::optional<::cw::lib::String> arg_xmltext, bool arg_nocache, std::optional<std::map<::cw::lib::String, ::cw::lib::String>> arg_rootattrs, std::map<::cw::lib::String, ::cw::lib::String>* output)
{
    ScenarioData* scenario = nullptr;
    if (::cw::g_cwpy) {
        const auto* object = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_sdata.storage());
        if (object && *object) scenario = dynamic_cast<ScenarioData*>(object->get());
    }
    const bool stream_supplied = arg_stream.has_value() || arg_xmltext.has_value();
    // Python版と同様、xmltextではキャッシュを無
    // 効にするが、明示的なバイナリストリームでは無効にしな
    // い。 シナリオパスと組になったストリームは、キャッシ
    // ュを読み書きする。 ただし所属判定は、Pythonの
    // 単純なstartswith(tempdir)ではなく
    // 共通の正規化したパス要素の判定を使う。 同じ接頭辞の
    // 兄弟を除外し、保存境界のOS表記に従う意図的な違い。
    const bool usecache = scenario && !arg_xmltext && !arg_path.empty() &&
        ::cw::util::path_starts_with(arg_path, scenario_value_string(scenario->m_tempdir));
    const auto lower_path = scenario_lower(arg_path);
    const bool classic_path = !stream_supplied && !arg_path.empty() && ::cw::g_cwpy &&
        ::cw::g_cwpy->m_classicdata && *::cw::g_cwpy->m_classicdata &&
        (lower_path.ends_with(".wsm") || lower_path.ends_with(".wid"));
    std::optional<std::tuple<::cw::lib::String, ::cw::lib::String, bool, bool, bool>> versionhint;
    ::cw::lib::Real mtime = 0.0;
    if (usecache) {
        if (scenario->fastLoadCacheEnabled()) {
            const auto it = scenario->m_data_cache.find(arg_path);
            if (it != scenario->m_data_cache.end() && it->second) {
                auto cached = value_to_element(it->second->m_data);
                if (cached) {
                    s_last_rootattrs = cached->m_attributes;
                    merge_rootattrs(arg_rootattrs ? &*arg_rootattrs : nullptr, cached->m_attributes);
                    merge_rootattrs(output, cached->m_attributes);
                    if (!arg_tag.empty()) {
                        const auto selected = cached->find(arg_tag);
                        if (!selected) throw std::invalid_argument(arg_path + ": " + arg_tag);
                        cached = *selected;
                    }
                    return arg_nocache ? copydata(cached, false) : cached;
                }
            }
        }
        if (::cw::g_fsync && ::cw::g_fsync->is_waiting(arg_path)) ::cw::g_fsync->sync();
        // キャッシュ未登録時はストリーム解析前にgetmtimeを呼ぶ
        // 。取得エラーを伝播し、仮のゼロ日時でキャッシュを公開しない。
        mtime = scenario_mtime(arg_path, true);
    }
    ::cw::lib::String source;
    std::optional<::cw::lib::Bytes> source_bytes;
    if (arg_xmltext) source = *arg_xmltext;
    else if (arg_stream) source_bytes = ::cw::lib::read_binary_input(*arg_stream);
    std::shared_ptr<CWPyElement> basedata;
    if (classic_path) {
        const auto classic = *::cw::g_cwpy->m_classicdata;
        log_scenario_data("xml2element classic begin: path=" + arg_path +
                          " tag=" + arg_tag);
        const auto loaded = classic->load_file(arg_path);
        const auto& value = std::get<0>(loaded);
        const auto* object = value ? std::get_if<::cw::lib::ObjectPtr>(&value->storage()) : nullptr;
        const auto binary = object && *object
            ? std::dynamic_pointer_cast<::cw::binary::base::CWBinaryBase>(*object) : nullptr;
        if (!binary) {
            log_scenario_data("xml2element classic failed: binary=null path=" + arg_path);
            throw std::invalid_argument(arg_path + ": " + arg_tag);
        }
        try {
            basedata = binary->get_data();
        } catch (const std::exception& error) {
            log_scenario_data("xml2element classic get_data failed: path=" + arg_path +
                              " error=" + ::cw::lib::String(error.what()));
            throw;
        }
        if (!basedata) {
            log_scenario_data("xml2element classic failed: data=null path=" + arg_path);
            throw std::invalid_argument(arg_path + ": " + arg_tag);
        }
        log_scenario_data("xml2element classic loaded: path=" + arg_path +
                          " id=" + std::to_string(binary->m_id) +
                          " name=" + binary->m_name +
                          " root=" + basedata->m_tag);
        if (::cw::g_cwpy->m_sct) {
            const auto filehint = ::cw::g_cwpy->m_sct->get_versionhint(std::nullopt, std::get<1>(loaded));
            if (classic->m_hasmodeini)
                versionhint = ::cw::g_cwpy->m_sct->merge_versionhints(classic->m_versionhint, filehint);
            else
                versionhint = filehint ? filehint : classic->m_versionhint;
        }
    } else if (!source_bytes && !arg_xmltext) {
        const auto native = ::cw::lib::encoding::path_from_utf8(arg_path);
        bool mounted_handled = false;
        source_bytes = read_mounted_xml(native, mounted_handled);
        std::ifstream input;
        if (!mounted_handled) input.open(native, std::ios::binary);
        // Pythonのxml2elementと同様、元
        // のオープン・getmtimeの失敗を伝える。
        // 空要素を返して半初期化のシナリオを続けさせず、
        // 例外にパスを残して上位のエラー表示・トレー
        // スで失敗したリソースを特定できるようにする。
        if ((!mounted_handled && !input) || (mounted_handled && !source_bytes)) {
            // 任意リソースの意図的な境界として、ネイティブの互換呼び
            // 出し元は空の指定をXML項目なしの意味に使える。 Python
            // のSimpleXmlParserは空パスを開いて例
            // 外を出すが、その未設定値はここで維持する。 空で
            // ない実際の未存在・読み取り不可のパスは診断する。
            if (arg_path.empty()) return {};
            throw std::invalid_argument(arg_path + ": file not found or not readable");
        }
        if (!mounted_handled)
            source_bytes = ::cw::lib::read_stream_bytes(input);
    }
    if (!basedata) {
        try {
            if (!usecache && !arg_tag.empty() && !versionhint) {
                // Python版と同様、この経路は選択した部分ツリーだけを読
                // む。 無関係な後続の解析成功を要求せず、選択済みルートへElement.find()
                // を二度適用しない。 キャッシュ済み・Classic
                // データは引き続き全ツリー経路を使う。
                SimpleXmlParser parser;
                const auto input = source_bytes ? ::cw::lib::Value(*source_bytes) : ::cw::lib::Value(source);
                if (output) parser.initialize(arg_path, arg_tag, input, true, *output);
                else parser.initialize(arg_path, arg_tag, input, true, arg_rootattrs);
                auto selected = parser.parse();
                if (!selected) throw std::invalid_argument("XML target not found: " + arg_tag);
                return selected;
            }
            // Python版と同様、全文書にもSimpleXmlParserを
            // 使い、 子より前のテキストだけを保持して子のtailを読まない規
            // 則を共有する。 明示的なtailの構築・複製・書き込みは変えない
            // 。 外部ストリームは上で一度読み、復旧ではバイト列を再利用する。
            SimpleXmlParser parser;
            parser.initialize(arg_path, "",
                source_bytes ? ::cw::lib::Value(*source_bytes) : ::cw::lib::Value(source),
                false, std::nullopt);
            basedata = parser.parse();
        } catch (const std::invalid_argument& error) {
            // Python版と同様、情報を添えるのはXML構文の失敗
            // だけで、任意の例外ではない。 文字コードの検索・未対
            // 応エラーを分け、復旧対象の不正XMLと誤認させない。
            const auto prefix = arg_path.empty() ? ::cw::lib::String{} : arg_path + ": ";
            throw std::invalid_argument(prefix + error.what());
        }
    } else {
        // ClassicリソースはXML解析を通らないが、 キャッ
        // シュ・解析済みのリソースと同じ文書情報の反映規則を使う。
        s_last_rootattrs = basedata->m_attributes;
    }
    merge_rootattrs(arg_rootattrs ? &*arg_rootattrs : nullptr, basedata->m_attributes);
    merge_rootattrs(output, basedata->m_attributes);
    assign_fpath(basedata, arg_path);
    auto root = arg_tag.empty() ? basedata : basedata->find(arg_tag).value_or(nullptr);
    if (!root && !arg_tag.empty()) throw std::invalid_argument(arg_path + ": " + arg_tag);
    // Python版と同様、basedataの公開前に要求された選択を検
    // 証する。 失敗した選択で、以前のキャッシュを新規作成・置換しない。
    if (usecache && scenario) {
        auto cache = std::make_shared<CacheData>(); cache->initialize(basedata, mtime); scenario->m_data_cache[arg_path] = cache;
        // nocache指定時は編集用コピーを作り、結
        // 果ごとの互換設定でキャッシュ元を変更しない。
        if (arg_nocache) root = copydata(root, false);
    }
    if (versionhint && ::cw::g_cwpy && ::cw::g_cwpy->m_sct && root) {
        const auto basehint = ::cw::g_cwpy->m_sct->to_basehint(versionhint);
        if (!basehint.empty()) {
            const auto property = root->find("Property");
            if (property && *property) (*property)->set("versionHint", basehint);
        }
    }
    return root;
}

std::shared_ptr<::cw::data::CWPyElement> xml2element(::cw::lib::String arg_path, ::cw::lib::String arg_tag, std::optional<::cw::lib::Value> arg_stream, std::optional<::cw::lib::String> arg_xmltext, bool arg_nocache, std::optional<std::map<::cw::lib::String, ::cw::lib::String>> arg_rootattrs)
{
    return xml2element_impl(std::move(arg_path), std::move(arg_tag), std::move(arg_stream), std::move(arg_xmltext), std::move(arg_nocache), std::move(arg_rootattrs), nullptr);
}

std::shared_ptr<::cw::data::CWPyElement> xml2element(::cw::lib::String arg_path, ::cw::lib::String arg_tag, std::optional<::cw::lib::Value> arg_stream, std::optional<::cw::lib::String> arg_xmltext, bool arg_nocache, std::map<::cw::lib::String, ::cw::lib::String>& arg_rootattrs)
{
    return xml2element_impl(std::move(arg_path), std::move(arg_tag), std::move(arg_stream), std::move(arg_xmltext), std::move(arg_nocache), std::nullopt, &arg_rootattrs);
}


std::shared_ptr<::cw::data::CWPyElement> copydata(std::shared_ptr<::cw::data::CWPyElement> arg_data, bool arg_copyall)
{
    if (!arg_data) return nullptr;
    static const std::set<::cw::lib::String> immutable_tags{
        "Motions", "Events", "Id", "Name", "Description", "Scenario", "Author", "Level",
        "Ability", "Target", "EffectType", "ResistType", "SuccessRate", "VisualEffect",
        "KeyCodes", "Premium", "EnhanceOwner", "Price"};
    if (!arg_copyall && immutable_tags.contains(arg_data->m_tag)) return arg_data;
    auto result = std::make_shared<::cw::data::CWPyElement>(arg_data->m_tag, arg_data->m_text,
                                                            arg_data->m_attributes, arg_data->m_tail);
    // Python版と同様、元のXMLの文脈も含めてキャッシュ内容を共有する
    // 。 複製には元ノードの弱い逆参照ではなく、所有権のあるハンドルが必要。
    if (arg_data->m_content && *arg_data->m_content)
        result->m_content = ::cw::content::get_content(arg_data);
    result->m_nextelements = arg_data->m_nextelements;
    result->m_needcheck = arg_data->m_needcheck;
    // Pythonの make_element と同様、fpathと計算済みcwxpath
    // を空にする。 新しく編集可能な要素は、元のファイルに結び付いたXMLとは別物
    // 。 上で明示的に共有したcwxparent・コンテントの文脈は独立して扱う。
    for (const auto& child : arg_data->m_children)
        result->append(copydata(child, false));
    result->m_cwxparent = arg_data->m_cwxparent;
    return result;
}

std::shared_ptr<::cw::data::CWPyElementTree> copytree(std::shared_ptr<::cw::data::CWPyElementTree> arg_data, bool arg_copyall)
{
    if (!arg_data) return nullptr;
    auto root = copydata(arg_data->getroot(), arg_copyall);
    // Python版と同様、複製した要素から新しいツリーを作る。 フ
    // ァイル識別情報はその要素から取り、元ツリーの保存先は使わない。
    return std::make_shared<::cw::data::CWPyElementTree>(root, root ? root->m_fpath : "");
}

void main()
{
    // Python版もモジュールエントリポイントのみを定義し、直接実行時の処理は持たない。
}

}
