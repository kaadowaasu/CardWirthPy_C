#pragma once

#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"

#include <fstream>
#include <stdexcept>

namespace cw::binary::cwfile {

// 明示的な変換完了では、バッファーの書き込み・終了エラーを伝播させる。Python
// の file.close と同様に、書き出しが失敗してもストリームを閉じる。
template<class Stream> void finish_binary_output(Stream& stream)
{
    stream.flush();
    const bool flushed = static_cast<bool>(stream);
    stream.close();
    if (!flushed || !stream) throw std::runtime_error("failed to complete CardWirth binary output");
}

// Python の BinaryIO と同じ位置・EOF規約をValue(ObjectPtr)越しに渡す境界。
// read は要求数未満（EOFでは空）を返してよく、seek/tellの例外は呼出側へ伝播する。
class FileLike : public ::cw::lib::Object {
public:
    ~FileLike() override = default;
    virtual void on_enter() {}
    [[nodiscard]] virtual bool seekable() const = 0;
    virtual ::cw::lib::Bytes read(std::optional<::cw::lib::Integer> arg_n) = 0;
    virtual std::size_t write(const ::cw::lib::Bytes& arg_data);
    virtual ::cw::lib::Integer seek(::cw::lib::Integer arg_offset,
                                    ::cw::lib::Integer arg_whence) = 0;
    virtual ::cw::lib::Integer tell() = 0;
    virtual void flush();
    virtual void close() = 0;
};

struct FileLikeCallbacks {
    std::function<bool()> seekable{};
    std::function<::cw::lib::Bytes(std::optional<::cw::lib::Integer>)> read{};
    std::function<std::size_t(const ::cw::lib::Bytes&)> write{};
    std::function<::cw::lib::Integer(::cw::lib::Integer, ::cw::lib::Integer)> seek{};
    std::function<::cw::lib::Integer()> tell{};
    std::function<void()> flush{};
    std::function<void()> close{};
    std::function<void()> enter{};
};

// Python bridgeやSDL hostが既存streamをlambdaで包むための既定typed wrapper。
class CallbackFileLike final : public FileLike {
public:
    explicit CallbackFileLike(FileLikeCallbacks arg_callbacks);
    void on_enter() override;
    [[nodiscard]] bool seekable() const override;
    ::cw::lib::Bytes read(std::optional<::cw::lib::Integer> arg_n) override;
    std::size_t write(const ::cw::lib::Bytes& arg_data) override;
    ::cw::lib::Integer seek(::cw::lib::Integer arg_offset,
                            ::cw::lib::Integer arg_whence) override;
    ::cw::lib::Integer tell() override;
    void flush() override;
    void close() override;
private:
    FileLikeCallbacks m_callbacks{};
    bool m_closed{};
};

class UnsupportedError : public std::runtime_error, public ::cw::lib::Object {
public:
    UnsupportedError(std::optional<::cw::lib::String> message = std::nullopt,
                     ::cw::lib::String function_name = {});
    ::cw::lib::String m_funcname{};
    ::cw::lib::String m_msg{};
    void initialize(std::optional<::cw::lib::String> arg_msg, ::cw::lib::String arg_funcname);
};

class CWFile : public FileLike,
               public std::enable_shared_from_this<CWFile> {
public:
    CWFile() = default;
    explicit CWFile(const ::cw::lib::String& path, bool decodewrap = false);
    CWFile(const ::cw::lib::String& path, const ::cw::lib::String& mode,
           bool decodewrap = false, std::optional<::cw::lib::Value> f = std::nullopt);
    explicit CWFile(::cw::lib::Bytes data, ::cw::lib::String filename = {},
                    bool decodewrap = false);
    virtual ~CWFile();

    ::cw::lib::String m_filename{};
    std::vector<::cw::lib::Bytes> m_filedata{};
    bool m_decodewrap{};

    void initialize(::cw::lib::String arg_path, ::cw::lib::String arg_mode = "rb",
                    bool arg_decodewrap = false,
                    std::optional<::cw::lib::Value> arg_f = std::nullopt);
    std::shared_ptr<CWFile> enter();
    ::cw::lib::Value exit(std::optional<::cw::lib::Value> arg_t,
                          std::optional<::cw::lib::Value> arg_value,
                          std::optional<::cw::lib::Value> arg_traceback);
    [[nodiscard]] bool seekable() const override;
    void close() override;
    ::cw::lib::Integer tell() override;
    ::cw::lib::Integer seek(::cw::lib::Integer arg_offset, ::cw::lib::Integer arg_whence = 0) override;
    bool boolean();
    ::cw::lib::String string(bool arg_multiline = false);
    ::cw::lib::String rawstring();
    ::cw::lib::Integer byte();
    ::cw::lib::Integer ubyte();
    ::cw::lib::Integer dword();
    ::cw::lib::Integer word();
    std::optional<::cw::lib::Bytes> image();
    ::cw::lib::Bytes read(std::optional<::cw::lib::Integer> arg_n = std::nullopt) override;

private:
    std::unique_ptr<std::istream> m_stream{};
    std::shared_ptr<FileLike> m_filelike{};
};

class CWFileWriter : public ::cw::lib::Object,
                     public std::enable_shared_from_this<CWFileWriter> {
public:
    CWFileWriter() = default;
    explicit CWFileWriter(const ::cw::lib::String& path, bool decodewrap = false,
                          std::optional<::cw::lib::Real> targetengine = std::nullopt);
    virtual ~CWFileWriter();

    ::cw::lib::String m_filename{};
    ::cw::lib::String m_mode{"wb"};
    bool m_append{};
    bool m_decodewrap{};
    std::optional<::cw::lib::Real> m_targetengine{};
    std::optional<::cw::lib::Callback> m_write_errorlog{};

    void initialize(::cw::lib::String arg_path, ::cw::lib::String arg_mode = "wb",
                    bool arg_decodewrap = false,
                    std::optional<::cw::lib::Real> arg_targetengine = std::nullopt,
                    std::optional<::cw::lib::Callback> arg_write_errorlog = std::nullopt);
    std::shared_ptr<CWFileWriter> enter();
    ::cw::lib::Value exit(std::optional<::cw::lib::Value> arg_t,
                          std::optional<::cw::lib::Value> arg_value,
                          std::optional<::cw::lib::Value> arg_traceback);
    void close();
    ::cw::lib::Integer tell();
    ::cw::lib::Integer seek(::cw::lib::Integer arg_offset, ::cw::lib::Integer arg_whence = 0);
    ::cw::lib::Integer truncate(std::optional<::cw::lib::Integer> arg_size = std::nullopt);
    void flush();
    void check_version(::cw::lib::Real arg_engineversion, ::cw::lib::String arg_funcname = {});
    void check_version(const ::cw::lib::String& arg_engineversion,
                       ::cw::lib::String arg_funcname = {});
    [[noreturn]] void check_wsnversion(::cw::lib::String arg_wsnversion,
                                       ::cw::lib::String arg_funcname = {});
    void check_bgmoptions(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    void check_soundoptions(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    void write_bool(bool arg_b);
    void write_string(::cw::lib::String arg_s, bool arg_multiline = false);
    void write_rawstring(::cw::lib::String arg_s);
    void write_byte(::cw::lib::Integer arg_b);
    void write_ubyte(::cw::lib::Integer arg_b);
    void write_dword(::cw::lib::Integer arg_dw);
    void write_word(::cw::lib::Integer arg_w);
    void write_image(std::optional<::cw::lib::Bytes> arg_image);
    void log_error(const ::cw::lib::String& message) const;

private:
    std::unique_ptr<std::fstream> m_stream{};
};

void main();

}  // namespace cw::binary::cwfile
