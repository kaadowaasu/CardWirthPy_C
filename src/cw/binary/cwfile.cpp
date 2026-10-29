#include "cw/binary/cwfile.hpp"
#include <iostream>

#include "cw/data.hpp"
#include "cw/lib/encoding.hpp"
#include "platform/storage/storage_mount.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <sstream>
#include <system_error>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#else
#  include <cerrno>
#  include <fcntl.h>
#  include <unistd.h>
#endif

namespace cw::binary::cwfile {
namespace {

::cw::lib::String encode_wrap(const ::cw::lib::String& source) {
    ::cw::lib::String result;
    result.reserve(source.size());
    for (char ch : source) {
        if (ch == '\\') {
            result += "\\\\";
        } else if (ch == '\n') {
            result += "\\n";
        } else if (ch != '\r') {
            result.push_back(ch);
        }
    }
    return result;
}

::cw::lib::String decode_wrap(const ::cw::lib::String& source) {
    ::cw::lib::String result;
    bool backslash = false;
    for (char ch : source) {
        if (backslash) {
            if (ch == 'n') {
                result += "\r\n";
            } else {
                result.push_back(ch);
            }
            backslash = false;
        } else if (ch == '\\') {
            backslash = true;
        } else {
            result.push_back(ch);
        }
    }
    // Pythonの decodewrap と同様、末尾の単独のバックスラッシュも除く。
    return result;
}

std::ios_base::seekdir seek_direction(::cw::lib::Integer whence) {
    switch (whence) {
    case 0: return std::ios::beg;
    case 1: return std::ios::cur;
    case 2: return std::ios::end;
    default: throw std::invalid_argument("invalid seek origin");
    }
}

template <typename T>
T read_little_endian(CWFile& file) {
    const auto bytes = file.read(static_cast<::cw::lib::Integer>(sizeof(T)));
    if (bytes.size() != sizeof(T)) {
        throw std::runtime_error("unexpected end of CardWirth binary file");
    }
    using Unsigned = std::make_unsigned_t<T>;
    Unsigned value = 0;
    for (std::size_t index = 0; index < sizeof(T); ++index) {
        value |= static_cast<Unsigned>(bytes[index]) << (index * 8U);
    }
    T result{};
    std::memcpy(&result, &value, sizeof(T));
    return result;
}

::cw::lib::Bytes read_declared_field(CWFile& file, ::cw::lib::Integer length,
                                     const char* field_name) {
    constexpr ::cw::lib::Integer kPreflightAllocation = 1024 * 1024;
    if (length > kPreflightAllocation && file.seekable()) {
        const auto position = file.tell();
        const auto end = file.seek(0, 2);
        file.seek(position, 0);
        if (end < position || length > end - position) {
            // 大きいフィールドは read(length)
            // 前に検査し、信頼できない32ビットの長さから直
            // 接確保しない。 小さいフィールドは共通の逐次経
            // 路を使い、読み取り後に正確な長さを検査する。
            throw std::runtime_error(::cw::lib::String("unexpected end of CardWirth binary ") +
                                     field_name);
        }
    }
    auto bytes = file.read(length);
    // Pythonの BinaryIO.read(-1) は残
    // り全体を読む意味であり、古いCardWirthデータが引
    // き継いできた動作を保つ。 ただし正の長さ指定は形式上の約
    // 束なので、途中で切れた文字列・画像を黙って受け付けない。
    if (length > 0 && static_cast<std::uint64_t>(bytes.size()) !=
                          static_cast<std::uint64_t>(length)) {
        throw std::runtime_error(::cw::lib::String("unexpected end of CardWirth binary ") +
                                 field_name);
    }
    return bytes;
}

template <typename T>
void write_little_endian(std::fstream& stream, ::cw::lib::Integer value) {
    if (value < static_cast<::cw::lib::Integer>(std::numeric_limits<T>::min()) ||
        value > static_cast<::cw::lib::Integer>(std::numeric_limits<T>::max())) {
        throw std::out_of_range("CardWirth binary integer is out of range");
    }
    using Unsigned = std::make_unsigned_t<T>;
    const auto narrowed = static_cast<Unsigned>(static_cast<T>(value));
    char bytes[sizeof(T)]{};
    for (std::size_t index = 0; index < sizeof(T); ++index) {
        bytes[index] = static_cast<char>((narrowed >> (index * 8U)) & 0xffU);
    }
    stream.write(bytes, static_cast<std::streamsize>(sizeof(T)));
    if (!stream) {
        throw std::runtime_error("failed to write CardWirth binary file");
    }
}

::cw::lib::Integer option_value(const std::shared_ptr<::cw::data::CWPyElement>& data,
                                const ::cw::lib::String& name,
                                ::cw::lib::Integer fallback) {
    if (!data) {
        return fallback;
    }
    return data->getint(".", ::cw::lib::Value(name), fallback);
}

std::ios::openmode writer_mode(const ::cw::lib::String& mode, bool& append) {
    // Pythonの io.FileIO と同様、r/w/a/xの一つだ
    // けと、最大一つの+を許可する。 bの重複は許すが効果は増えない。
    // 排他的な出力作成前に検証し、不正なモードで入出力を起こさない。
    unsigned primary = 0, update = 0;
    for (const char ch : mode) {
        if (ch == 'r' || ch == 'w' || ch == 'a' || ch == 'x') ++primary;
        else if (ch == 'b') continue;
        else if (ch == '+') ++update;
        else throw std::invalid_argument("invalid CWFileWriter mode: " + mode);
    }
    if (primary != 1 || update > 1)
        throw std::invalid_argument("invalid CWFileWriter mode: " + mode);
    std::ios::openmode result = std::ios::binary;
    append = false;
    if (mode.find('r') != ::cw::lib::String::npos) result |= std::ios::in;
    if (mode.find('w') != ::cw::lib::String::npos) result |= std::ios::out | std::ios::trunc;
    // std::fstream で開く前に、パスを排他的に確保する。 in|outを使い
    // 、std::ios::out が確保済みファイルを暗黙に切り詰めることを避ける。
    if (mode.find('x') != ::cw::lib::String::npos) result |= std::ios::in | std::ios::out;
    if (mode.find('a') != ::cw::lib::String::npos) {
        result |= std::ios::out | std::ios::app;
        append = true;
    }
    if (mode.find('+') != ::cw::lib::String::npos) result |= std::ios::in | std::ios::out;
    if (!(result & (std::ios::in | std::ios::out))) {
        throw std::invalid_argument("invalid CWFileWriter mode: " + mode);
    }
    return result;
}

void create_exclusive(const ::cw::lib::String& path) {
    const auto native = ::cw::lib::encoding::path_from_utf8(path);
#if defined(_WIN32)
    const HANDLE handle = ::CreateFileW(native.c_str(), GENERIC_READ | GENERIC_WRITE,
                                        0, nullptr, CREATE_NEW,
                                        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        const auto error = ::GetLastError();
        if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS)
            throw std::runtime_error("CardWirth binary output already exists: " + path);
        throw std::system_error(static_cast<int>(error), std::system_category(),
                                "failed to create CardWirth binary output: " + path);
    }
    ::CloseHandle(handle);
#else
    const int descriptor = ::open(native.c_str(), O_CREAT | O_EXCL | O_RDWR, 0666);
    if (descriptor < 0) {
        if (errno == EEXIST)
            throw std::runtime_error("CardWirth binary output already exists: " + path);
        throw std::system_error(errno, std::generic_category(),
                                "failed to create CardWirth binary output: " + path);
    }
    ::close(descriptor);
#endif
}

}  // namespace

std::size_t FileLike::write(const ::cw::lib::Bytes&) {
    throw std::runtime_error("binary stream is not writable");
}

void FileLike::flush() {}

CallbackFileLike::CallbackFileLike(FileLikeCallbacks arg_callbacks)
    : m_callbacks(std::move(arg_callbacks)) {
    if (!m_callbacks.read)
        throw std::invalid_argument("file-like stream requires a read callback");
}

void CallbackFileLike::on_enter() {
    if (m_closed) throw std::runtime_error("I/O operation on closed binary stream");
    if (m_callbacks.enter) m_callbacks.enter();
}

bool CallbackFileLike::seekable() const {
    if (m_closed) return false;
    return m_callbacks.seek && m_callbacks.tell &&
           (!m_callbacks.seekable || m_callbacks.seekable());
}

::cw::lib::Bytes CallbackFileLike::read(std::optional<::cw::lib::Integer> arg_n) {
    if (m_closed) throw std::runtime_error("I/O operation on closed binary stream");
    return m_callbacks.read(arg_n);
}

std::size_t CallbackFileLike::write(const ::cw::lib::Bytes& arg_data) {
    if (m_closed) throw std::runtime_error("I/O operation on closed binary stream");
    if (!m_callbacks.write) return FileLike::write(arg_data);
    const auto written = m_callbacks.write(arg_data);
    if (written > arg_data.size()) throw std::runtime_error("binary stream returned invalid write count");
    return written;
}

::cw::lib::Integer CallbackFileLike::seek(::cw::lib::Integer arg_offset,
                                           ::cw::lib::Integer arg_whence) {
    if (m_closed) throw std::runtime_error("I/O operation on closed binary stream");
    if (arg_whence < 0 || arg_whence > 2) throw std::invalid_argument("invalid seek origin");
    if (!m_callbacks.seek) throw std::runtime_error("binary stream is not seekable");
    return m_callbacks.seek(arg_offset, arg_whence);
}

::cw::lib::Integer CallbackFileLike::tell() {
    if (m_closed) throw std::runtime_error("I/O operation on closed binary stream");
    if (!m_callbacks.tell) throw std::runtime_error("binary stream is not tellable");
    return m_callbacks.tell();
}

void CallbackFileLike::flush() {
    if (m_closed) throw std::runtime_error("I/O operation on closed binary stream");
    if (m_callbacks.flush) m_callbacks.flush();
}

void CallbackFileLike::close() {
    if (m_closed) return;
    if (m_callbacks.close) m_callbacks.close();
    m_closed = true;
}

UnsupportedError::UnsupportedError(std::optional<::cw::lib::String> message,
                                   ::cw::lib::String function_name)
    : std::runtime_error(""),
      m_funcname(std::move(function_name)),
      m_msg(message.value_or("")) {}

void UnsupportedError::initialize(std::optional<::cw::lib::String> arg_msg,
                                  ::cw::lib::String arg_funcname) {
    m_msg = arg_msg.value_or("");
    m_funcname = std::move(arg_funcname);
}

CWFile::CWFile(const ::cw::lib::String& path, bool decodewrap) {
    initialize(path, "rb", decodewrap, std::nullopt);
}

CWFile::CWFile(const ::cw::lib::String& path, const ::cw::lib::String& mode,
               bool decodewrap, std::optional<::cw::lib::Value> f) {
    initialize(path, mode, decodewrap, std::move(f));
}

CWFile::CWFile(::cw::lib::Bytes data, ::cw::lib::String filename, bool decodewrap)
    : m_filename(std::move(filename)), m_decodewrap(decodewrap) {
    auto buffer = ::cw::lib::String(reinterpret_cast<const char*>(data.data()), data.size());
    m_stream = std::make_unique<std::istringstream>(std::move(buffer), std::ios::binary);
}

CWFile::~CWFile() { close(); }

void CWFile::initialize(::cw::lib::String arg_path, ::cw::lib::String arg_mode,
                        bool arg_decodewrap, std::optional<::cw::lib::Value> arg_f) {
    close();
    if (arg_f.has_value() && !arg_f->is_none()) {
        if (const auto* bytes = std::get_if<::cw::lib::Bytes>(&arg_f->storage())) {
            auto buffer = ::cw::lib::String(reinterpret_cast<const char*>(bytes->data()), bytes->size());
            m_filename = std::move(arg_path);
            m_decodewrap = arg_decodewrap;
            m_filedata.clear();
            m_stream = std::make_unique<std::istringstream>(std::move(buffer), std::ios::binary);
            return;
        }
        if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&arg_f->storage()); object && *object) {
            auto filelike = std::dynamic_pointer_cast<FileLike>(*object);
            if (!filelike) throw std::invalid_argument("CWFile f must implement cw::binary::cwfile::FileLike");
            m_filename = std::move(arg_path);
            m_decodewrap = arg_decodewrap;
            m_filedata.clear();
            m_filelike = std::move(filelike);
            return;
        }
        throw std::invalid_argument("CWFile f must be bytes or a file-like object");
    }
    if (arg_mode.find('r') == ::cw::lib::String::npos) {
        throw std::invalid_argument("CWFile must be opened for reading");
    }
    // Pythonの FileIO と同様、パスはUnicodeとして扱う。 読み書きモー
    // ドも含め、ファイル名の文字コードとClassicファイルのCP932の内容を分ける。
    const auto native = ::cw::lib::encoding::path_from_utf8(arg_path);
    if (::cardwirth::platform::is_mounted_user_path(native)) {
        // PythonのBufferedReaderとの既存の違いとして、
        // このClassic解析の std::istream アダプターは
        // 全体のスナップショットを所有する。 名前だけの可変項目も含め、こ
        // れらは存在・拡張子の検査ではなく解析入力なので、この動作を維持す
        // る。 シークを使う処理への変更は、ZIP読み取り側に限定する。
        if (arg_mode.find('+') != ::cw::lib::String::npos)
            throw std::runtime_error("mounted CardWirth binary update mode is unsupported: " + arg_path);
        const auto mounted = ::cardwirth::platform::read_mounted_user_file(native);
        if (!mounted)
            throw std::runtime_error("failed to read mounted CardWirth binary file: " + arg_path);
        ::cw::lib::String buffer;
        buffer.resize(mounted->size());
        for (std::size_t index = 0; index < mounted->size(); ++index)
            buffer[index] = static_cast<char>(std::to_integer<unsigned char>((*mounted)[index]));
        m_filename = std::move(arg_path);
        m_decodewrap = arg_decodewrap;
        m_filedata.clear();
        m_stream = std::make_unique<std::istringstream>(std::move(buffer), std::ios::binary);
        return;
    }
    if (arg_mode.find('+') != ::cw::lib::String::npos) {
        auto stream = std::make_unique<std::fstream>(native, std::ios::binary | std::ios::in | std::ios::out);
        if (!*stream) throw std::runtime_error("failed to open CardWirth binary file: " + arg_path);
        m_stream = std::move(stream);
    } else {
        auto stream = std::make_unique<std::ifstream>(native, std::ios::binary);
        if (!*stream) throw std::runtime_error("failed to open CardWirth binary file: " + arg_path);
        m_stream = std::move(stream);
    }
    m_filename = std::move(arg_path);
    m_decodewrap = arg_decodewrap;
    m_filedata.clear();
}

std::shared_ptr<CWFile> CWFile::enter() {
    if (m_filelike) m_filelike->on_enter();
    return shared_from_this();
}

::cw::lib::Value CWFile::exit(std::optional<::cw::lib::Value>,
                              std::optional<::cw::lib::Value>,
                              std::optional<::cw::lib::Value>) {
    close();
    return false;
}

bool CWFile::seekable() const {
    return m_filelike ? m_filelike->seekable() : static_cast<bool>(m_stream);
}

void CWFile::close() {
    if (m_filelike) {
        m_filelike->close();
        m_filelike.reset();
    }
    if (auto* file = dynamic_cast<std::ifstream*>(m_stream.get())) {
        file->close();
    }
    m_stream.reset();
}

::cw::lib::Integer CWFile::tell() {
    if (m_filelike) return m_filelike->tell();
    if (!m_stream) {
        throw std::runtime_error("CWFile is closed");
    }
    const auto position = m_stream->tellg();
    if (position < 0) throw std::runtime_error("failed to tell CardWirth binary file");
    return static_cast<::cw::lib::Integer>(position);
}

::cw::lib::Integer CWFile::seek(::cw::lib::Integer arg_offset, ::cw::lib::Integer arg_whence) {
    if (m_filelike) {
        if (arg_whence < 0 || arg_whence > 2) throw std::invalid_argument("invalid seek origin");
        return m_filelike->seek(arg_offset, arg_whence);
    }
    if (!m_stream) {
        throw std::runtime_error("CWFile is closed");
    }
    m_stream->clear();
    m_stream->seekg(static_cast<std::streamoff>(arg_offset), seek_direction(arg_whence));
    if (!*m_stream) {
        throw std::runtime_error("failed to seek CardWirth binary file");
    }
    return tell();
}

bool CWFile::boolean() { return byte() != 0; }

::cw::lib::String CWFile::string(bool arg_multiline) {
    auto value = rawstring();
    if (arg_multiline && !m_decodewrap) {
        value = encode_wrap(value);
    }
    return value;
}

::cw::lib::String CWFile::rawstring() {
    const auto length = dword();
    if (length == 0) {
        return {};
    }
    auto bytes = read_declared_field(*this, length, "string");
    while (!bytes.empty() && bytes.front() == 0) bytes.erase(bytes.begin());
    while (!bytes.empty() && bytes.back() == 0) bytes.pop_back();
    auto decoded = ::cw::lib::encoding::cp932_to_utf8(
        bytes, ::cw::lib::encoding::ErrorPolicy::Replace);
    while (!decoded.empty() && decoded.front() == '\0') decoded.erase(decoded.begin());
    while (!decoded.empty() && decoded.back() == '\0') decoded.pop_back();
    return decoded;
}

::cw::lib::Integer CWFile::byte() { return read_little_endian<std::int8_t>(*this); }
::cw::lib::Integer CWFile::ubyte() { return read_little_endian<std::uint8_t>(*this); }
::cw::lib::Integer CWFile::dword() { return read_little_endian<std::int32_t>(*this); }
::cw::lib::Integer CWFile::word() { return read_little_endian<std::int16_t>(*this); }

std::optional<::cw::lib::Bytes> CWFile::image() {
    const auto length = dword();
    if (length == 0) {
        return std::nullopt;
    }
    return read_declared_field(*this, length, "image");
}

::cw::lib::Bytes CWFile::read(std::optional<::cw::lib::Integer> arg_n) {
    if (m_filelike) {
        if (arg_n && *arg_n < 0) {
            auto bytes = m_filelike->read(arg_n);
            m_filedata.push_back(bytes);
            return bytes;
        }
        ::cw::lib::Integer length{};
        if (!arg_n) {
            if (!m_filelike->seekable()) throw std::runtime_error("CWFile stream is not seekable");
            const auto position = m_filelike->tell();
            m_filelike->seek(0, 2);
            const auto end = m_filelike->tell();
            m_filelike->seek(position, 0);
            // Python版と同様、end - position をファイル相当
            // のreadへ渡す。 末尾を越えてシークした後の負の残量も含める。
            length = end - position;
        } else {
            length = *arg_n;
            if (length < 0) throw std::invalid_argument("negative CWFile read length");
        }
        auto bytes = m_filelike->read(length);
        if (length >= 0 && bytes.size() > static_cast<std::uint64_t>(length))
            throw std::runtime_error("CWFile stream returned more bytes than requested");
        m_filedata.push_back(bytes);
        return bytes;
    }
    if (!m_stream) {
        throw std::runtime_error("CWFile is closed");
    }
    // 通常のPythonファイルはBytesIOではなく BufferedReader(FileIO)
    // 。 明示的な負のサイズは-1だけを許す。 上の指定されたFileLike
    // は、他の負値も含め自身の読み取り規則を維持する。
    if (arg_n && *arg_n < -1) throw std::invalid_argument("negative CWFile read length");
    ::cw::lib::Integer length = 0;
    if (!arg_n || *arg_n == -1) {
        const auto position = m_stream->tellg();
        if (position < 0) throw std::runtime_error("CWFile stream is not seekable");
        m_stream->clear();
        m_stream->seekg(0, std::ios::end);
        const auto end = m_stream->tellg();
        if (end < 0) throw std::runtime_error("CWFile stream is not seekable");
        const auto remainder = static_cast<::cw::lib::Integer>(end - position);
        m_stream->clear();
        m_stream->seekg(position);
        // Pythonの read(None) と同様、位置を調べて戻し、end-position
        // を BufferedReader.read() に渡す。 -1は全読み取り（末尾
        // 以降なら空）を表し、-1未満は filedata へ何も追加する前に例外とする。
        if (!arg_n && remainder < -1)
            throw std::invalid_argument("negative CWFile read length");
        length = remainder < 0 ? 0 : remainder;
    } else {
        length = *arg_n;
        if (length < 0) throw std::invalid_argument("negative CWFile read length");
    }
    ::cw::lib::Bytes bytes(static_cast<std::size_t>(length));
    m_stream->read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    bytes.resize(static_cast<std::size_t>(m_stream->gcount()));
    m_stream->clear();
    m_filedata.push_back(bytes);
    return bytes;
}

CWFileWriter::CWFileWriter(const ::cw::lib::String& path, bool decodewrap,
                           std::optional<::cw::lib::Real> targetengine) {
    initialize(path, "wb", decodewrap, targetengine, std::nullopt);
}

CWFileWriter::~CWFileWriter() {
    // C++のデストラクターは例外処理中に例外を伝播できない。 明示的なclose/exit
    // は引き続き例外を出し、暗黙の破棄では異常終了せず失敗を報告する。
    try { close(); } catch (const std::exception& error) { std::cerr << error.what() << '\n'; }
}

void CWFileWriter::initialize(::cw::lib::String arg_path, ::cw::lib::String arg_mode,
                              bool arg_decodewrap,
                              std::optional<::cw::lib::Real> arg_targetengine,
                              std::optional<::cw::lib::Callback> arg_write_errorlog) {
    bool append = false;
    const bool exclusive = arg_mode.find('x') != ::cw::lib::String::npos;
    const auto mode = writer_mode(arg_mode, append);
    if (exclusive) create_exclusive(arg_path);
    auto stream = std::make_unique<std::fstream>(::cw::lib::encoding::path_from_utf8(arg_path), mode);
    if (!*stream) {
        if (exclusive) {
            std::error_code ignored;
            std::filesystem::remove(::cw::lib::encoding::path_from_utf8(arg_path), ignored);
        }
        throw std::runtime_error("failed to open CardWirth binary output: " + arg_path);
    }
    m_filename = std::move(arg_path);
    m_mode = std::move(arg_mode);
    m_append = append;
    m_decodewrap = arg_decodewrap;
    m_targetengine = arg_targetengine;
    m_write_errorlog = std::move(arg_write_errorlog);
    m_stream = std::move(stream);
}

std::shared_ptr<CWFileWriter> CWFileWriter::enter() { return shared_from_this(); }

::cw::lib::Value CWFileWriter::exit(std::optional<::cw::lib::Value>,
                                    std::optional<::cw::lib::Value>,
                                    std::optional<::cw::lib::Value>) {
    close();
    return false;
}

void CWFileWriter::close() {
    if (m_stream) {
        auto stream = std::move(m_stream);
        finish_binary_output(*stream);
    }
}

::cw::lib::Integer CWFileWriter::tell() {
    if (!m_stream) throw std::runtime_error("CWFileWriter is closed");
    return static_cast<::cw::lib::Integer>(m_stream->tellp());
}

::cw::lib::Integer CWFileWriter::seek(::cw::lib::Integer arg_offset,
                                      ::cw::lib::Integer arg_whence) {
    if (!m_stream) throw std::runtime_error("CWFileWriter is closed");
    m_stream->clear();
    m_stream->seekp(static_cast<std::streamoff>(arg_offset), seek_direction(arg_whence));
    if (!*m_stream) throw std::runtime_error("failed to seek CardWirth binary output");
    return tell();
}

::cw::lib::Integer CWFileWriter::truncate(std::optional<::cw::lib::Integer> arg_size) {
    if (!m_stream) throw std::runtime_error("CWFileWriter is closed");
    const auto position = tell();
    const auto size = arg_size.value_or(position);
    if (size < 0) throw std::invalid_argument("negative truncate size");
    m_stream->flush();
    if (!*m_stream) throw std::runtime_error("failed to flush CardWirth binary output");
    m_stream->close();
    const auto native = ::cw::lib::encoding::path_from_utf8(m_filename);
    std::filesystem::resize_file(native, static_cast<std::uintmax_t>(size));
    auto mode = std::ios::binary | std::ios::in | std::ios::out;
    if (m_append) mode |= std::ios::app;
    m_stream = std::make_unique<std::fstream>(native, mode);
    if (!*m_stream) throw std::runtime_error("failed to reopen truncated output");
    m_stream->seekp(static_cast<std::streamoff>(position), std::ios::beg);
    if (!*m_stream) throw std::runtime_error("failed to restore output position");
    return size;
}

void CWFileWriter::flush() {
    if (!m_stream) throw std::runtime_error("CWFileWriter is closed");
    m_stream->flush();
    if (!*m_stream) throw std::runtime_error("failed to flush CardWirth binary output");
}

void CWFileWriter::check_version(::cw::lib::Real arg_engineversion,
                                 ::cw::lib::String arg_funcname) {
    if (m_targetengine && *m_targetengine < arg_engineversion) {
        throw UnsupportedError(std::nullopt, std::move(arg_funcname));
    }
}

void CWFileWriter::check_version(const ::cw::lib::String&,
                                 ::cw::lib::String arg_funcname) {
    if (m_targetengine) throw UnsupportedError(std::nullopt, std::move(arg_funcname));
}

[[noreturn]] void CWFileWriter::check_wsnversion(::cw::lib::String,
                                                 ::cw::lib::String arg_funcname) {
    throw UnsupportedError(std::nullopt, std::move(arg_funcname));
}

void CWFileWriter::check_bgmoptions(std::shared_ptr<::cw::data::CWPyElement> arg_data) {
    if (option_value(arg_data, "volume", 100) != 100) check_wsnversion("1", "BGM音量の指定");
    if (option_value(arg_data, "loopcount", 0) != 0) check_wsnversion("1", "BGMループ回数の指定");
    if (option_value(arg_data, "channel", 0) != 0) check_wsnversion("1", "BGM再生チャンネルの指定");
    if (option_value(arg_data, "fadein", 0) != 0) check_wsnversion("1", "BGMフェードイン");
}

void CWFileWriter::check_soundoptions(std::shared_ptr<::cw::data::CWPyElement> arg_data) {
    if (option_value(arg_data, "volume", 100) != 100) check_wsnversion("1", "効果音音量の指定");
    if (option_value(arg_data, "loopcount", 1) != 1) check_wsnversion("1", "効果音ループ回数の指定");
    if (option_value(arg_data, "channel", 0) != 0) check_wsnversion("1", "効果音再生チャンネルの指定");
    if (option_value(arg_data, "fadein", 0) != 0) check_wsnversion("1", "効果音フェードイン");
}

void CWFileWriter::write_bool(bool arg_b) { write_byte(arg_b ? 1 : 0); }

void CWFileWriter::write_string(::cw::lib::String arg_s, bool arg_multiline) {
    if (arg_multiline && !m_decodewrap) {
        arg_s = decode_wrap(arg_s);
    }
    write_rawstring(std::move(arg_s));
}

void CWFileWriter::write_rawstring(::cw::lib::String arg_s) {
    if (!m_stream) throw std::runtime_error("CWFileWriter is closed");
    if (arg_s.empty()) {
        write_dword(1);
        write_byte(0);
        return;
    }
    auto encoded = ::cw::lib::encoding::utf8_to_cp932(
        arg_s, ::cw::lib::encoding::ErrorPolicy::Replace);
    encoded.push_back(0);
    write_dword(static_cast<::cw::lib::Integer>(encoded.size()));
    m_stream->write(reinterpret_cast<const char*>(encoded.data()),
                    static_cast<std::streamsize>(encoded.size()));
    if (!*m_stream) throw std::runtime_error("failed to write CardWirth string");
}

void CWFileWriter::write_byte(::cw::lib::Integer arg_b) {
    if (!m_stream) throw std::runtime_error("CWFileWriter is closed");
    write_little_endian<std::int8_t>(*m_stream, arg_b);
}
void CWFileWriter::write_ubyte(::cw::lib::Integer arg_b) {
    if (!m_stream) throw std::runtime_error("CWFileWriter is closed");
    write_little_endian<std::uint8_t>(*m_stream, arg_b);
}
void CWFileWriter::write_dword(::cw::lib::Integer arg_dw) {
    if (!m_stream) throw std::runtime_error("CWFileWriter is closed");
    write_little_endian<std::int32_t>(*m_stream, arg_dw);
}
void CWFileWriter::write_word(::cw::lib::Integer arg_w) {
    if (!m_stream) throw std::runtime_error("CWFileWriter is closed");
    write_little_endian<std::int16_t>(*m_stream, arg_w);
}

void CWFileWriter::write_image(std::optional<::cw::lib::Bytes> arg_image) {
    if (!m_stream) throw std::runtime_error("CWFileWriter is closed");
    if (!arg_image || arg_image->empty()) {
        write_dword(0);
        return;
    }
    write_dword(static_cast<::cw::lib::Integer>(arg_image->size()));
    m_stream->write(reinterpret_cast<const char*>(arg_image->data()),
                    static_cast<std::streamsize>(arg_image->size()));
    if (!*m_stream) throw std::runtime_error("failed to write CardWirth image");
}

void CWFileWriter::log_error(const ::cw::lib::String& message) const {
    if (m_write_errorlog) {
        (void)(*m_write_errorlog)(::cw::lib::Arguments{::cw::lib::Value(message)}, {});
    }
}

void main() {}

}  // namespace cw::binary::cwfile
