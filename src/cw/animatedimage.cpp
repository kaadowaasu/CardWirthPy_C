#include "cw/animatedimage.hpp"
#include "cw/lib/encoding.hpp"
#include "cw/lib/stdlib.hpp"
#include "platform/storage/storage_mount.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_map>

namespace {
using Integer = ::cw::lib::Integer;
using Real = ::cw::lib::Real;
using String = ::cw::lib::String;
using Bytes = ::cw::lib::Bytes;
using Value = ::cw::lib::Value;
using Decimal = ::cw::lib::Decimal;
using RawFrame = ::cw::animatedimage::RawFrame;
using FrameMetadata = ::cw::animatedimage::FrameMetadata;
using Slot = ::cw::animatedimage::_Slot;
using WorkerPool = ::cw::animatedimage::_WorkerPool;

struct AdapterState {
    std::mutex mutex;
    ::cw::animatedimage::SurfaceFrameAdapter surface;
    ::cw::animatedimage::BitmapFrameAdapter bitmap;
    ::cw::animatedimage::BitmapSubrectAdapter subrect;
};

AdapterState& adapter_state()
{
    static AdapterState state;
    return state;
}

::cw::animatedimage::SurfaceFrameAdapter surface_adapter()
{
    auto& state = adapter_state();
    std::scoped_lock lock(state.mutex);
    return state.surface;
}

::cw::animatedimage::BitmapFrameAdapter bitmap_adapter()
{
    auto& state = adapter_state();
    std::scoped_lock lock(state.mutex);
    return state.bitmap;
}

::cw::animatedimage::BitmapSubrectAdapter subrect_adapter()
{
    auto& state = adapter_state();
    std::scoped_lock lock(state.mutex);
    return state.subrect;
}

std::size_t rgba_size(Integer width, Integer height)
{
    if (width < 0 || height < 0) throw std::invalid_argument("animated image: negative frame size");
    const auto w = static_cast<std::size_t>(width);
    const auto h = static_cast<std::size_t>(height);
    if (w != 0 && h > std::numeric_limits<std::size_t>::max() / w)
        throw std::overflow_error("animated image: frame dimensions overflow");
    const auto pixels = w * h;
    if (pixels > std::numeric_limits<std::size_t>::max() / 4)
        throw std::overflow_error("animated image: RGBA frame size overflow");
    return pixels * 4;
}

template<typename T>
std::shared_ptr<T> make_rgba_frame(const Bytes& data, Integer width, Integer height,
                                   const FrameMetadata& metadata)
{
    const auto expected = rgba_size(width, height);
    if (data.size() != expected) throw std::invalid_argument("animated image: invalid RGBA frame buffer");
    auto result = std::make_shared<T>();
    result->size = {width, height};
    result->rgba = data;
    // compat::Surface はストレートRGBAを持つため、 convert_alpha
    // と FromBufferAndAlpha は二度目の画素複製なしで表す。
    (void)metadata;
    return result;
}

std::shared_ptr<::cw::lib::Surface> make_surface_frame(const Bytes& data, Integer width,
                                                       Integer height, const FrameMetadata& metadata)
{
    return make_rgba_frame<::cw::lib::Surface>(data, width, height, metadata);
}

std::shared_ptr<::cw::lib::Bitmap> make_bitmap_frame(const Bytes& data, Integer width,
                                                     Integer height, const FrameMetadata& metadata)
{
    return make_rgba_frame<::cw::lib::Bitmap>(data, width, height, metadata);
}

std::shared_ptr<::cw::lib::Bitmap> make_bitmap_subrect(const std::shared_ptr<::cw::lib::Bitmap>& source,
                                                       Integer x, Integer y, Integer width, Integer height)
{
    if (!source) return nullptr;
    const auto source_rgba_size = rgba_size(source->size.width, source->size.height);
    if (source->rgba.size() != source_rgba_size)
        throw std::invalid_argument("animated image: invalid cached bitmap buffer");
    if (x < 0 || y < 0 || width < 0 || height < 0 || x > source->size.width - width ||
        y > source->size.height - height)
        throw std::invalid_argument("animated image: bitmap subrect is outside source");
    auto result = std::make_shared<::cw::lib::Bitmap>();
    result->size = {width, height};
    result->rgba.resize(rgba_size(width, height));
    const auto source_width = static_cast<std::size_t>(source->size.width);
    const auto row_bytes = rgba_size(width, 1);
    for (Integer row = 0; row < height; ++row) {
        const auto source_offset = (static_cast<std::size_t>(y + row) * source_width + static_cast<std::size_t>(x)) * 4;
        const auto destination_offset = static_cast<std::size_t>(row) * row_bytes;
        std::copy_n(source->rgba.begin() + static_cast<std::ptrdiff_t>(source_offset),
                    row_bytes,
                    result->rgba.begin() + static_cast<std::ptrdiff_t>(destination_offset));
    }
    return result;
}

FrameMetadata metadata_for(const RawFrame& frame)
{
    return {frame.m_frame_index, frame.m_duration_ms, frame.m_has_alpha, frame.m_has_transparency};
}

std::filesystem::path path_from_utf8(const String& text)
{
    std::u8string encoded;
    encoded.reserve(text.size());
    for (const auto byte : text)
        encoded.push_back(static_cast<char8_t>(static_cast<unsigned char>(byte)));
    return std::filesystem::path(encoded);
}

Real now_seconds()
{
    const auto value = std::chrono::duration<Real>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (!std::isfinite(value)) throw std::runtime_error("animated image clock is not finite");
    return value;
}

String bytes_hex(const Bytes& bytes)
{
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (const auto byte : bytes) out << std::setw(2) << static_cast<unsigned>(byte);
    return out.str();
}

String value_key(const Value& value)
{
    if (const auto* text = std::get_if<String>(&value.storage())) return "s:" + *text;
    if (const auto* integer = std::get_if<Integer>(&value.storage())) return "i:" + std::to_string(*integer);
    if (const auto* decimal = std::get_if<Decimal>(&value.storage())) return "d:" + decimal->to_string(true);
    if (const auto* real = std::get_if<Real>(&value.storage())) {
        if (!std::isfinite(*real)) return "r:nonfinite";
        std::ostringstream text;
        text << std::setprecision(std::numeric_limits<Real>::max_digits10) << *real;
        return "r:" + text.str();
    }
    if (const auto* boolean = std::get_if<bool>(&value.storage())) return *boolean ? "b:1" : "b:0";
    if (const auto* bytes = std::get_if<Bytes>(&value.storage())) return "x:" + bytes_hex(*bytes);
    return "n:";
}

String key_id(const std::vector<Value>& key)
{
    String result;
    for (const auto& value : key) { result += value_key(value); result.push_back('\x1f'); }
    return result;
}

String value_string(const Value& value)
{
    if (const auto* text = std::get_if<String>(&value.storage())) return *text;
    if (const auto* decimal = std::get_if<Decimal>(&value.storage())) return decimal->to_string(true);
    if (const auto* integer = std::get_if<Integer>(&value.storage())) return std::to_string(*integer);
    if (const auto* real = std::get_if<Real>(&value.storage())) {
        if (!std::isfinite(*real)) return {};
        std::ostringstream text;
        text << std::setprecision(std::numeric_limits<Real>::max_digits10) << *real;
        return text.str();
    }
    if (const auto* boolean = std::get_if<bool>(&value.storage())) return *boolean ? "True" : "False";
    return {};
}

Decimal decimal_from_real(Real value);

Integer value_integer(const Value& value, Integer fallback = 0)
{
    if (const auto* integer = std::get_if<Integer>(&value.storage())) return *integer;
    if (const auto* boolean = std::get_if<bool>(&value.storage())) return *boolean ? 1 : 0;
    if (const auto* decimal = std::get_if<Decimal>(&value.storage())) {
        try { return decimal->to_integer(::cw::lib::DecimalRounding::Down); }
        catch (const ::cw::lib::DecimalError&) { return fallback; }
    }
    if (const auto* real = std::get_if<Real>(&value.storage())) {
        if (!std::isfinite(*real)) return fallback;
        try {
            return decimal_from_real(*real).to_integer(::cw::lib::DecimalRounding::Down);
        } catch (const std::exception&) {
            return fallback;
        }
    }
    return fallback;
}

Real decimal_to_real(const Decimal& value, Real fallback = 0.0)
{
    const auto text = value.to_string(false);
    std::size_t consumed = 0;
    try {
        const auto result = std::stod(text, &consumed);
        return consumed == text.size() && std::isfinite(result) ? result : fallback;
    } catch (const std::exception&) {
        return fallback;
    }
}

Decimal decimal_from_real(Real value)
{
    if (!std::isfinite(value)) throw std::invalid_argument("animated image: non-finite Real");
    std::ostringstream text;
    text << std::setprecision(std::numeric_limits<Real>::max_digits10) << value;
    return Decimal::parse(text.str());
}

Real value_real(const Value& value, Real fallback = 0.0)
{
    if (const auto* real = std::get_if<Real>(&value.storage())) return std::isfinite(*real) ? *real : fallback;
    if (const auto* integer = std::get_if<Integer>(&value.storage())) {
        const auto result = static_cast<Real>(*integer);
        return std::isfinite(result) ? result : fallback;
    }
    if (const auto* boolean = std::get_if<bool>(&value.storage())) return *boolean ? 1.0 : 0.0;
    if (const auto* decimal = std::get_if<Decimal>(&value.storage())) return decimal_to_real(*decimal, fallback);
    return fallback;
}

Integer checked_size_to_integer(std::size_t value, const char* context)
{
    if (value > static_cast<std::size_t>((std::numeric_limits<Integer>::max)()))
        throw std::overflow_error(std::string("animated image: ") + context + " exceeds Integer range");
    return static_cast<Integer>(value);
}

Integer delay_to_milliseconds(Real seconds)
{
    if (!std::isfinite(seconds) || seconds < 0.0)
        throw std::invalid_argument("animated image: delay must be finite and non-negative");
    const auto milliseconds = static_cast<long double>(seconds) * 1000.0L;
    if (milliseconds > static_cast<long double>((std::numeric_limits<Integer>::max)()))
        throw std::overflow_error("animated image: delay exceeds Integer range");
    return static_cast<Integer>(milliseconds);
}

Bytes read_file(const String& path)
{
    const auto native = path_from_utf8(path);
    if (::cardwirth::platform::is_mounted_user_path(native)) {
        const auto mounted = ::cardwirth::platform::read_mounted_user_file(native);
        if (!mounted) return {};
        Bytes result;
        result.reserve(mounted->size());
        for (const auto value : *mounted)
            result.push_back(std::to_integer<std::uint8_t>(value));
        return result;
    }
    std::ifstream input(native, std::ios::binary);
    if (!input) return {};
    input.seekg(0, std::ios::end);
    const auto end = input.tellg();
    if (end <= 0) return {};
    if (static_cast<std::uintmax_t>(end) > static_cast<std::uintmax_t>((std::numeric_limits<std::size_t>::max)()) ||
        static_cast<std::uintmax_t>(end) > static_cast<std::uintmax_t>((std::numeric_limits<std::streamsize>::max)()))
        return {};
    input.seekg(0, std::ios::beg);
    Bytes data(static_cast<std::size_t>(end));
    input.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!input && !input.eof()) return {};
    return data;
}

Bytes frame_hash(const Bytes& data)
{
    // Pythonの hashlib.blake2b(data, digest_size=16).digest()
    // に相当する。 この境界のBytesはPILのRGBAバッファとバイト単位で一致し
    // 、 意図的な空バッファもBLAKE2bで定義された空入力のダイジェストとして扱う。
    return ::cw::lib::blake2b(data, 16);
}

std::mutex g_runtime_mutex;
std::mutex g_pool_global_mutex;
struct Job { std::vector<Value> key; Integer generation{}; };
struct PoolRuntime {
    std::mutex mutex;
    // 処理可能な作業を待つ側と期限を待つ側を分け、notify_one が別の利用側を起こさないようにする。
    std::condition_variable work_cv;
    std::condition_variable schedule_cv;
    std::condition_variable stop_cv;
    std::condition_variable finished;
    std::deque<Job> jobs;
    std::multimap<std::chrono::steady_clock::time_point, Job> delayed_jobs;
    // Pythonの Thread.join は複数の同時呼び出しを許すが、C++のthreadは許さない。
    std::mutex shutdown_mutex;
    std::atomic<bool> stop{false};
    std::atomic<int> live_joinable{0};
    std::vector<std::thread> workers;
    std::thread scheduler;
    std::thread monitor;
};

void request_stop(const std::shared_ptr<PoolRuntime>& runtime)
{
    if (!runtime) return;
    runtime->stop.store(true, std::memory_order_release);
    runtime->work_cv.notify_all();
    runtime->schedule_cv.notify_all();
    runtime->stop_cv.notify_all();
}

bool stopped(const std::shared_ptr<PoolRuntime>& runtime)
{
    return runtime && runtime->stop.load(std::memory_order_acquire);
}

// 別の終了要求元がハンドルの終了を待っていてもよいよう、 std::thread
// ハンドルを調べずに、このプール自身のスレッドを識別する。
thread_local PoolRuntime* current_pool_runtime = nullptr;
struct JoinableThread {
    std::shared_ptr<PoolRuntime> runtime;
    PoolRuntime* previous_runtime{};
    explicit JoinableThread(std::shared_ptr<PoolRuntime> arg_runtime)
        : runtime(std::move(arg_runtime)), previous_runtime(current_pool_runtime)
    {
        current_pool_runtime = runtime.get();
    }
    ~JoinableThread()
    {
        current_pool_runtime = previous_runtime;
        if (!runtime) return;
        std::lock_guard lock(runtime->mutex);
        if (runtime->live_joinable.fetch_sub(1, std::memory_order_acq_rel) == 1)
            runtime->finished.notify_all();
    }
};

bool wait_for_stop(const std::shared_ptr<PoolRuntime>& runtime, std::chrono::milliseconds delay)
{
    if (!runtime) return false;
    std::unique_lock lock(runtime->mutex);
    return runtime->stop_cv.wait_for(lock, delay, [&] {
        return runtime->stop.load(std::memory_order_acquire);
    });
}
std::unordered_map<WorkerPool*, std::shared_ptr<PoolRuntime>> g_runtime;

std::shared_ptr<PoolRuntime> runtime_for(WorkerPool* pool)
{
    std::scoped_lock lock(g_runtime_mutex);
    const auto found = g_runtime.find(pool);
    return found == g_runtime.end() ? nullptr : found->second;
}

}

namespace cw::animatedimage {
void _Decoder::initialize(::cw::lib::String arg_path, std::optional<::cw::lib::Bytes> arg_data)
{
    m_path = arg_path;
    m_frame_index = 0;
    m_data = arg_data ? *arg_data : read_file(arg_path);
    if (m_data.empty()) throw std::runtime_error("animated image load failed: " + arg_path);
    // Pillowのシーク型デコーダーと異なり、imagecodec は共通のラン
    // ダムアクセスAnimation API用に復号済みフレームを保持する。 メモ
    // リ・起動時間はフレーム数に応じて増え、ストリーミングのデコーダーではない。
    m_animation = ::cw::lib::imagecodec::decode_animation(
        std::span<const std::uint8_t>(m_data.data(), m_data.size()));
    m_info = m_animation->info();
    if (m_info.frame_count == 0 || m_info.frame_count > static_cast<std::size_t>((std::numeric_limits<Integer>::max)()))
        throw std::runtime_error("animated image frame count is out of range: " + arg_path);
    m_width = m_info.width;
    m_height = m_info.height;
    m_frame_count = checked_size_to_integer(m_info.frame_count, "frame count");
    m_durations = m_info.durations_ms;
    if (m_durations.size() != m_info.frame_count) m_durations.assign(m_info.frame_count, 100);
    m_loop_count = m_info.loop_count;
    m_loop_defined = m_info.loop_defined;
    m_has_alpha = m_info.has_alpha;
    m_has_transparency = m_info.has_transparency;
    m_animation->seek(0);
    // Pillowの Image.open/convert/seek/tell は imagecodec::Animation
    // に接続する。 m_image はValueで表せないPillow Image相
    // 当のため空にし、実データは m_animation が所有する。 倍率はPython
    // 版でもこの層では適用せず、Surface/Bitmap の描画側へ任せる。
    m_image = ::cw::lib::Value{};
}

std::shared_ptr<::cw::animatedimage::RawFrame> _Decoder::next_frame()
{
    if (!m_animation || m_frame_count <= 0) throw std::runtime_error("animated image decoder is not initialized");
    const auto frame_number = std::clamp<Integer>(m_frame_index, 0, m_frame_count - 1);
    const auto frame_index = static_cast<std::size_t>(frame_number);
    const auto rgba = m_animation->convert_rgba(frame_index);
    const auto& decoded_frame = m_animation->frame(frame_index);
    auto result = std::make_shared<RawFrame>();
    result->m_data = rgba.pixels;
    result->m_size = {rgba.width, rgba.height};
    Integer duration = decoded_frame.duration_ms;
    if (frame_index < m_durations.size()) duration = m_durations[frame_index];
    result->m_duration_ms = std::max<Integer>(16, duration);
    result->m_frame_hash = frame_hash(result->m_data);
    result->m_frame_index = frame_number;
    result->m_has_alpha = m_has_alpha;
    result->m_has_transparency = m_has_transparency;
    m_frame_index = frame_number + 1 < m_frame_count ? frame_number + 1 : 0;
    m_animation->seek(static_cast<std::size_t>(m_frame_index));
    return result;
}

std::shared_ptr<::cw::animatedimage::_Decoder> _Decoder::copy() const
{
    auto result = std::make_shared<_Decoder>();
    result->m_data = m_data;
    result->m_frame_index = m_frame_index;
    result->m_frame_count = m_frame_count;
    result->m_width = m_width;
    result->m_height = m_height;
    result->m_durations = m_durations;
    result->m_animation = m_animation ? std::optional<::cw::lib::imagecodec::Animation>(m_animation->copy()) : std::nullopt;
    result->m_info = m_info;
    result->m_loop_count = m_loop_count;
    result->m_loop_defined = m_loop_defined;
    result->m_has_alpha = m_has_alpha;
    result->m_has_transparency = m_has_transparency;
    result->m_image = ::cw::lib::Value{};
    result->m_path = m_path;
    if (result->m_animation) result->m_animation->seek(static_cast<std::size_t>(result->m_frame_index));
    return result;
}

void _Decoder::seek(::cw::lib::Integer arg_frame_index)
{
    if (!m_animation || arg_frame_index < 0 || arg_frame_index >= m_frame_count)
        throw std::out_of_range("animated image frame index is out of range");
    m_animation->seek(static_cast<std::size_t>(arg_frame_index));
    m_frame_index = arg_frame_index;
}

::cw::lib::Integer _Decoder::tell() const noexcept
{
    return m_frame_index;
}

::cw::lib::Integer _Decoder::frame_count() const noexcept
{
    return m_frame_count;
}

::cw::lib::Integer _Decoder::duration_ms(::cw::lib::Integer arg_frame_index) const
{
    if (arg_frame_index < 0 || arg_frame_index >= m_frame_count)
        throw std::out_of_range("animated image frame index is out of range");
    const auto index = static_cast<std::size_t>(arg_frame_index);
    if (index < m_durations.size()) return m_durations[index];
    if (!m_animation) throw std::out_of_range("animated image frame index is out of range");
    return m_animation->frame(index).duration_ms;
}

::cw::lib::imagecodec::RGBAImage _Decoder::convert_rgba(::cw::lib::Integer arg_frame_index) const
{
    if (!m_animation || arg_frame_index < 0 || arg_frame_index >= m_frame_count)
        throw std::out_of_range("animated image frame index is out of range");
    return m_animation->convert_rgba(static_cast<std::size_t>(arg_frame_index));
}

::cw::lib::Integer _Decoder::loop_count() const noexcept
{
    return m_loop_count;
}

bool _Decoder::loop_defined() const noexcept
{
    return m_loop_defined;
}

bool _Decoder::has_alpha() const noexcept
{
    return m_has_alpha;
}

bool _Decoder::has_transparency() const noexcept
{
    return m_has_transparency;
}

void _Instance::initialize(::cw::lib::Integer arg_instance_id, ::cw::lib::String arg_owner, ::cw::lib::String arg_path, std::vector<::cw::lib::Value> arg_key)
{
    m_instance_id = arg_instance_id;
    m_owner = std::move(arg_owner);
    m_path = arg_path;
    m_key = ::cw::lib::Value(key_id(arg_key));
    m_key_id = key_id(arg_key);
    m_last_update_request = now_seconds();
    m_stopped_by_idle = false;
}

// Python版は time.time() のepoch秒を保存する。ネイティブの要求と待機監視は
// steady_clock の秒を共有し、 時計の調整で有効なデコーダーを早く止めないようにする。
// 公開の last_update_request と stop_if_idle(now) も、Unix
// 時刻ではなく steady_clock::time_since_epoch() の秒を使う。
bool _Instance::touch_update_request()
{
    if (g_IDLE_STOP_ENABLED && m_stopped_by_idle) return false;
    m_last_update_request = now_seconds();
    return true;
}

// arg_now には touch_update_request() と同じ steady_clock
// の秒を使う。 Pythonの time.time() を渡すと基準時刻が混ざるため未対応。
bool _Instance::stop_if_idle(::cw::lib::Real arg_now)
{
    if (!g_IDLE_STOP_ENABLED || m_stopped_by_idle) return false;
    const auto last = value_real(m_last_update_request, std::numeric_limits<Real>::quiet_NaN());
    if (!std::isfinite(arg_now) || !std::isfinite(last) || !std::isfinite(g_IDLE_STOP_SECONDS) || g_IDLE_STOP_SECONDS < 0.0 ||
        arg_now - last < g_IDLE_STOP_SECONDS) return false;
    m_stopped_by_idle = true;
    return true;
}

void _Slot::initialize(std::vector<::cw::lib::Value> arg_key, ::cw::lib::String arg_path, ::cw::lib::Integer arg_generation, std::optional<::cw::lib::Bytes> arg_data)
{
    m_key = ::cw::lib::Value(key_id(arg_key));
    m_key_id = key_id(arg_key);
    m_path = arg_path;
    m_path_text = std::move(arg_path);
    m_generation = arg_generation;
    m__data = arg_data ? ::cw::lib::Value(*arg_data) : ::cw::lib::Value{};
    m_decoder.reset();
    m_active = true;
    m_stopped_by_idle = false;
    m_frame_id = 0;
    m__latest.reset();
    m__current.reset();
    m__last_hash.reset();
    m_surface.reset();
    m_wxbmp.reset();
    m_last_update_request = now_seconds();
}

void _Slot::initialize_decoder()
{
    std::optional<Bytes> data;
    String path;
    {
        std::scoped_lock lock(m_state_mutex);
        if (m_decoder) return;
        if (const auto* bytes = std::get_if<Bytes>(&m__data.storage())) data = *bytes;
        path = m_path_text;
    }
    // ファイル・コーデックの初期化は遅い場合があるため、その間はスロ
    // ットのミューテックスを保持しない。 初回のダイアログ描画を止め
    // ず、UIが仮画像・現在フレームを表示し続けられるようにする。
    auto decoder = std::make_shared<_Decoder>();
    decoder->initialize(path, data);
    {
        std::scoped_lock lock(m_state_mutex);
        if (!m_decoder) m_decoder = std::move(decoder);
        m__data = ::cw::lib::Value{};
    }
}

std::shared_ptr<::cw::animatedimage::RawFrame> _Slot::transparent_frame()
{
    Integer width = 0;
    Integer height = 0;
    Bytes data;
    String path;
    {
        std::scoped_lock lock(m_state_mutex);
        path = m_path_text;
        if (m_decoder && *m_decoder) {
            width = (*m_decoder)->m_width;
            height = (*m_decoder)->m_height;
        } else if (const auto* bytes = std::get_if<Bytes>(&m__data.storage())) {
            data = *bytes;
        }
    }

    // _Decoder がInfoを保存する前に初期化が失敗する場合
    // がある。 元のバイト列を再確認し、APNG・WebPとも、有
    // 効なキャンバス・ヘッダーから透明な代替画像のサイズを求める。
    if ((width <= 0 || height <= 0) && data.empty() && !path.empty())
        data = read_file(path);
    if ((width <= 0 || height <= 0) && !data.empty()) {
        try {
            const auto info = ::cw::lib::imagecodec::inspect(
                std::span<const std::uint8_t>(data.data(), data.size()));
            width = info.width;
            height = info.height;
        } catch (const std::exception&) {
            width = 1;
            height = 1;
        }
    }
    width = width > 0 ? width : 1;
    height = height > 0 ? height : 1;

    auto result = std::make_shared<RawFrame>();
    try {
        result->m_data.assign(rgba_size(width, height), 0);
    } catch (const std::exception&) {
        width = 1;
        height = 1;
        result->m_data.assign(4, 0);
    }
    result->m_size = {width, height};
    result->m_duration_ms = 100;
    result->m_frame_hash = frame_hash(result->m_data);
    result->m_frame_index = 0;
    result->m_has_alpha = true;
    result->m_has_transparency = true;
    return result;
}

// Python版は time.time() のepoch秒を保存する。ネイティブの要求と待機監視は
// steady_clock の秒を共有し、 時計の調整で有効なデコーダーを早く止めないようにす
// る。 last_update_request と stop_if_idle(now) もUnix
// 時刻ではなく steady_clock::time_since_epoch() の秒を使う。
bool _Slot::touch_update_request()
{
    std::scoped_lock lock(m_state_mutex);
    if (g_IDLE_STOP_ENABLED && m_stopped_by_idle) return false;
    m_last_update_request = now_seconds();
    return true;
}

// arg_now には touch_update_request() と同じ steady_clock
// の秒を使う。 Pythonの time.time() を渡すと基準時刻が混ざるため未対応。
bool _Slot::stop_if_idle(::cw::lib::Real arg_now)
{
    std::scoped_lock lock(m_state_mutex);
    if (!g_IDLE_STOP_ENABLED || m_stopped_by_idle) return false;
    const auto last = value_real(m_last_update_request, std::numeric_limits<Real>::quiet_NaN());
    if (!std::isfinite(arg_now) || !std::isfinite(last) || !std::isfinite(g_IDLE_STOP_SECONDS) || g_IDLE_STOP_SECONDS < 0.0 ||
        arg_now - last < g_IDLE_STOP_SECONDS) return false;
    m_active = false;
    m_stopped_by_idle = true;
    return true;
}

void _Slot::push_frame(std::shared_ptr<::cw::animatedimage::RawFrame> arg_frame)
{
    std::scoped_lock lock(m_state_mutex);
    if (!m_active || m_stopped_by_idle) return;
    m__latest = std::move(arg_frame);
}

bool _Slot::update_if_new()
{
    std::scoped_lock lock(m_state_mutex);
    if (m_stopped_by_idle || !m__latest || !*m__latest) return false;
    const auto& frame = *m__latest;
    if (m__last_hash && *m__last_hash == frame->m_frame_hash) return false;
    m__last_hash = frame->m_frame_hash;
    m__current = frame;
    m_surface.reset();
    m_wxbmp.reset();
    ++m_frame_id;
    return true;
}

std::optional<std::shared_ptr<::cw::animatedimage::RawFrame>> _Slot::current_frame()
{
    std::scoped_lock lock(m_state_mutex);
    return m__current;
}

bool _Slot::is_active()
{
    std::scoped_lock lock(m_state_mutex);
    return m_active;
}

void _Slot::deactivate()
{
    std::scoped_lock lock(m_state_mutex);
    m_active = false;
}

::cw::lib::Integer _Slot::generation_value()
{
    std::scoped_lock lock(m_state_mutex);
    return value_integer(m_generation, std::numeric_limits<Integer>::min());
}

::cw::lib::Integer _Slot::frame_id()
{
    std::scoped_lock lock(m_state_mutex);
    return m_frame_id;
}

std::shared_ptr<::cw::animatedimage::_Decoder> _Slot::decoder_snapshot()
{
    std::scoped_lock lock(m_state_mutex);
    return m_decoder ? *m_decoder : nullptr;
}

std::tuple<std::optional<std::shared_ptr<::cw::animatedimage::RawFrame>>,
           std::optional<std::shared_ptr<::cw::lib::Surface>>, ::cw::lib::Integer>
_Slot::surface_snapshot()
{
    std::scoped_lock lock(m_state_mutex);
    return {m__current, m_surface, m_frame_id};
}

std::tuple<std::optional<std::shared_ptr<::cw::animatedimage::RawFrame>>,
           std::optional<std::shared_ptr<::cw::lib::Bitmap>>, ::cw::lib::Integer>
_Slot::bitmap_snapshot()
{
    std::scoped_lock lock(m_state_mutex);
    return {m__current, m_wxbmp, m_frame_id};
}

void _Slot::cache_surface(::cw::lib::Integer arg_frame_id,
                          std::shared_ptr<::cw::lib::Surface> arg_surface)
{
    if (!arg_surface) return;
    std::scoped_lock lock(m_state_mutex);
    if (m__current && !m_stopped_by_idle && m_frame_id == arg_frame_id && !m_surface)
        m_surface = std::move(arg_surface);
}

void _Slot::cache_bitmap(::cw::lib::Integer arg_frame_id,
                         std::shared_ptr<::cw::lib::Bitmap> arg_bitmap)
{
    if (!arg_bitmap) return;
    std::scoped_lock lock(m_state_mutex);
    if (m__current && !m_stopped_by_idle && m_frame_id == arg_frame_id && !m_wxbmp)
        m_wxbmp = std::move(arg_bitmap);
}

void _WorkerPool::initialize(::cw::lib::Integer arg_workers)
{
    auto runtime = std::make_shared<PoolRuntime>();
    {
        std::scoped_lock lock(g_runtime_mutex);
        g_runtime[this] = runtime;
    }
    const auto workers = std::max<Integer>(1, arg_workers);
    const auto self = shared_from_this();
    for (Integer index = 0; index < workers; ++index) {
        runtime->live_joinable.fetch_add(1, std::memory_order_relaxed);
        runtime->workers.emplace_back([self, runtime] {
            JoinableThread joinable{runtime};
            self->_worker_loop();
        });
    }
    runtime->live_joinable.fetch_add(1, std::memory_order_relaxed);
    runtime->scheduler = std::thread([runtime] {
        JoinableThread joinable{runtime};
        std::unique_lock lock(runtime->mutex);
        while (!runtime->stop.load(std::memory_order_acquire)) {
            if (runtime->delayed_jobs.empty()) {
                runtime->schedule_cv.wait(lock, [&] {
                    return runtime->stop.load(std::memory_order_acquire) ||
                           !runtime->delayed_jobs.empty();
                });
                continue;
            }
            const auto due = runtime->delayed_jobs.begin()->first;
            runtime->schedule_cv.wait_until(lock, due, [&] {
                return runtime->stop.load(std::memory_order_acquire) ||
                       runtime->delayed_jobs.empty() ||
                       runtime->delayed_jobs.begin()->first < due;
            });
            if (runtime->stop.load(std::memory_order_acquire)) return;
            if (runtime->delayed_jobs.empty() ||
                std::chrono::steady_clock::now() < runtime->delayed_jobs.begin()->first)
                continue;
            runtime->jobs.push_back(std::move(runtime->delayed_jobs.begin()->second));
            runtime->delayed_jobs.erase(runtime->delayed_jobs.begin());
            runtime->work_cv.notify_one();
        }
    });
    if (g_IDLE_STOP_ENABLED) {
        runtime->live_joinable.fetch_add(1, std::memory_order_relaxed);
        runtime->monitor = std::thread([self, runtime] {
            JoinableThread joinable{runtime};
            self->_idle_monitor_loop();
        });
    }
    m__threads = static_cast<Integer>(workers);
    m__queue = ::cw::lib::Value(String("animated-image-queue"));
    m__stop = ::cw::lib::Value(false);
}

void _WorkerPool::_idle_monitor_loop()
{
    const auto runtime = runtime_for(this);
    if (!runtime) return;
    while (true) {
        if (wait_for_stop(runtime, std::chrono::milliseconds(1000))) break;
        stop_idle_slots();
    }
}

std::optional<std::shared_ptr<::cw::animatedimage::_Slot>> _WorkerPool::request(::cw::lib::String arg_path, std::optional<::cw::lib::Bytes> arg_data)
{
    const auto key = _make_key(arg_path, arg_data);
    if (!key) return std::nullopt;
    std::scoped_lock lock(m_state_mutex);
    return _request_locked(*key, std::move(arg_path), std::move(arg_data));
}

std::optional<std::vector<::cw::lib::Value>> _WorkerPool::_make_key(::cw::lib::String arg_path, std::optional<::cw::lib::Bytes> arg_data)
{
    if (arg_data) {
        // Pythonのキーは (path, hashlib.blake2b(data, digest_size=16).digest())
        // 。 Value内でもダイジェストをバイト列として保持し、文字列表現に
        // 識別が依存しないようにする。 空のデータも有効な内容キーとして扱う。
        return std::vector<Value>{Value(arg_path), Value(::cw::lib::blake2b(*arg_data, 16))};
    }
    std::error_code ec;
    const auto path = path_from_utf8(arg_path);
    Real timestamp{};
    if (::cardwirth::platform::is_mounted_user_path(path)) {
        const auto status = ::cardwirth::platform::stat_mounted_user_path(path);
        if (!status || !status->is_file()) return std::nullopt;
        timestamp = static_cast<Real>(status->modified) / 1'000'000'000.0;
    } else {
        if (!std::filesystem::is_regular_file(path, ec)) return std::nullopt;
        const auto stamp = std::filesystem::last_write_time(path, ec);
        if (ec) return std::nullopt;
        timestamp = std::chrono::duration<Real>(stamp.time_since_epoch()).count();
    }
    if (!std::isfinite(timestamp)) return std::nullopt;
    return std::vector<Value>{Value(arg_path), Value(timestamp)};
}

std::optional<std::shared_ptr<::cw::animatedimage::_Slot>> _WorkerPool::_request_locked(std::vector<::cw::lib::Value> arg_key, ::cw::lib::String arg_path, std::optional<::cw::lib::Bytes> arg_data)
{
    const auto id = key_id(arg_key);
    const auto found = m__slots.find(id);
    if (found != m__slots.end()) return found->second;
    const auto generation = ++m__generation[id];
    std::shared_ptr<_Slot> slot;
    auto job_key = arg_key;
    try {
        slot = std::make_shared<_Slot>();
        slot->initialize(std::move(arg_key), arg_path, generation, std::move(arg_data));
    } catch (const std::exception&) {
        std::fprintf(stderr, "AnimatedImage: load_failed=%s\n", arg_path.c_str());
        return std::nullopt;
    }
    if (!slot->is_active()) return std::nullopt;
    m__slots.emplace(id, slot);
    if (const auto runtime = runtime_for(this)) {
        std::scoped_lock runtime_lock(runtime->mutex);
        // A newly opened dialog must not wait behind continuation frames from
        // animations that are already playing.  Prioritise only the first
        // decode of a new slot; scheduled continuation frames still use the
        // normal FIFO path below.  This keeps one common animation pipeline
        // while making poster and other dialog video startup responsive.
        runtime->jobs.push_front(Job{std::move(job_key), generation});
        runtime->work_cv.notify_one();
    }
    return slot;
}

std::optional<::cw::lib::Integer> _WorkerPool::acquire(::cw::lib::String arg_path, ::cw::lib::String arg_owner, bool arg_isolated, std::optional<::cw::lib::Bytes> arg_data)
{
    if (arg_owner.empty() || !is_supported_path(arg_path)) return std::nullopt;
    const auto base_key = _make_key(arg_path, arg_data);
    if (!base_key) return std::nullopt;
    auto key = *base_key;
    if (arg_isolated) key.emplace_back(arg_owner);
    const auto id = key_id(key);
    std::optional<Integer> result;
    bool newly_acquired = false;
    {
        std::scoped_lock lock(m_state_mutex);
        const auto slot = _request_locked(key, arg_path, std::move(arg_data));
        const auto owner_found = m__owner_instances.find(arg_owner);
        if (slot && owner_found != m__owner_instances.end()) {
            for (const auto instance_id : owner_found->second) {
                const auto instance = m__instances.find(instance_id);
                if (instance != m__instances.end() && value_string(instance->second->m_path) == arg_path && instance->second->m_key_id == id) {
                    result = instance_id;
                    break;
                }
            }
        }
        if (!result) {
            if (!slot || !(*slot)->is_active()) return std::nullopt;
            const auto instance_id = ++m__next_instance_id;
            auto instance = std::make_shared<_Instance>();
            instance->initialize(instance_id, arg_owner, arg_path, key);
            m__instances.emplace(instance_id, instance);
            m__owner_instances[arg_owner].insert(instance_id);
            m__path_instances[arg_path].insert(instance_id);
            result = instance_id;
            newly_acquired = true;
        }
    }
    // _print_status takes m_state_mutex.  It must stay outside the registration
    // critical section; taking it from _request_locked/acquire deadlocked the
    // first APNG/WebP registration before any animation frame could advance.
    if (newly_acquired)
        _print_status("acquired", arg_path + "#" + std::to_string(*result));
    return result;
}

void _WorkerPool::release(::cw::lib::String arg_owner, std::optional<::cw::lib::String> arg_path)
{
    if (arg_owner.empty()) return;
    bool changed = false;
    {
        std::scoped_lock lock(m_state_mutex);
        const auto owner_found = m__owner_instances.find(arg_owner);
        if (owner_found == m__owner_instances.end()) return;
        const auto ids = owner_found->second;
        for (const auto instance_id : ids) {
            const auto instance = m__instances.find(instance_id);
            if (instance == m__instances.end() || (arg_path && value_string(instance->second->m_path) != *arg_path)) continue;
            _release_instance_locked(instance_id);
            changed = true;
        }
    }
    if (changed) _print_status("released", arg_path.value_or(arg_owner));
}

void _WorkerPool::_release_instance_locked(::cw::lib::Integer arg_instance_id)
{
    const auto found = m__instances.find(arg_instance_id);
    if (found == m__instances.end()) return;
    const auto instance = found->second;
    const auto owner = value_string(instance->m_owner);
    const auto path = value_string(instance->m_path);
    m__instances.erase(found);
    if (const auto owner_found = m__owner_instances.find(owner); owner_found != m__owner_instances.end()) {
        owner_found->second.erase(arg_instance_id);
        if (owner_found->second.empty()) m__owner_instances.erase(owner_found);
    }
    if (const auto path_found = m__path_instances.find(path); path_found != m__path_instances.end()) {
        path_found->second.erase(arg_instance_id);
        if (path_found->second.empty()) {
            m__path_instances.erase(path_found);
            _drop_path(path);
        }
    }
}

void _WorkerPool::_drop_path(::cw::lib::String arg_path)
{
    for (auto it = m__slots.begin(); it != m__slots.end();) {
        if (it->second && it->second->m_path_text == arg_path) {
            it->second->deactivate();
            it = m__slots.erase(it);
        } else ++it;
    }
}

void _WorkerPool::stop_idle_slots()
{
    if (!g_IDLE_STOP_ENABLED) return;
    const auto now = now_seconds();
    std::vector<std::pair<String, Integer>> stopped;
    {
        std::scoped_lock lock(m_state_mutex);
        for (auto it = m__instances.begin(); it != m__instances.end();) {
            const auto instance = it->second;
            if (instance->stop_if_idle(now)) {
                stopped.emplace_back(value_string(instance->m_path), it->first);
            }
            ++it;
        }
        // Erase in a second pass so _release_instance_locked can update all indexes safely.
        for (const auto& [path, instance_id] : stopped) {
            (void)path;
            _release_instance_locked(instance_id);
        }
    }
    for (const auto& [path, instance_id] : stopped) _print_status("stopped_by_idle", path + "#" + std::to_string(instance_id));
}

std::optional<std::shared_ptr<::cw::animatedimage::_Instance>> _WorkerPool::_get_instance(std::optional<::cw::lib::Integer> arg_instance_id)
{
    if (!arg_instance_id) return std::nullopt;
    std::scoped_lock lock(m_state_mutex);
    const auto found = m__instances.find(*arg_instance_id);
    if (found == m__instances.end()) return std::nullopt;
    return found->second;
}

std::tuple<std::optional<std::shared_ptr<::cw::animatedimage::_Slot>>, bool> _WorkerPool::get_slot_for_update(::cw::lib::String arg_path, std::optional<::cw::lib::Integer> arg_instance_id)
{
    if (arg_instance_id) {
        std::scoped_lock lock(m_state_mutex);
        const auto found = m__instances.find(*arg_instance_id);
        if (found == m__instances.end() || value_string(found->second->m_path) != arg_path)
            return {std::nullopt, false};
        if (!found->second->touch_update_request()) return {std::nullopt, false};
        const auto slot = m__slots.find(found->second->m_key_id);
        return {slot == m__slots.end() ? std::nullopt : std::optional<std::shared_ptr<_Slot>>(slot->second), true};
    }
    return {request(std::move(arg_path), std::nullopt), true};
}

::cw::lib::Integer _WorkerPool::_active_count()
{
    std::scoped_lock lock(m_state_mutex);
    return checked_size_to_integer(m__instances.size(), "active instance count");
}

bool _WorkerPool::has_active()
{
    return _active_count() != 0;
}

bool _WorkerPool::has_instance(::cw::lib::String arg_path, ::cw::lib::Integer arg_instance_id)
{
    std::scoped_lock lock(m_state_mutex);
    const auto found = m__instances.find(arg_instance_id);
    return found != m__instances.end() && found->second &&
           value_string(found->second->m_path) == arg_path;
}

bool _WorkerPool::has_owner_prefix(::cw::lib::String arg_prefix)
{
    std::scoped_lock lock(m_state_mutex);
    return std::any_of(m__owner_instances.begin(), m__owner_instances.end(), [&arg_prefix](const auto& item) {
        return item.first.rfind(arg_prefix, 0) == 0;
    });
}

::cw::lib::Integer _WorkerPool::owner_prefix_frame_token(::cw::lib::String arg_prefix)
{
    std::vector<std::shared_ptr<_Slot>> slots;
    {
        std::scoped_lock lock(m_state_mutex);
        // Native IDs increase on acquisition, so this map's ID order matches
        // 所有者とは独立した、Pythonのインスタンス辞書への登録順。
        for (const auto& [instance_id, instance] : m__instances) {
            (void)instance_id;
            if (!instance || value_string(instance->m_owner).rfind(arg_prefix, 0) != 0 ||
                !instance->touch_update_request()) continue;
            const auto slot = m__slots.find(instance->m_key_id);
            if (slot != m__slots.end()) slots.push_back(slot->second);
        }
    }
    std::uint64_t token = 0;
    for (const auto& slot : slots) {
        if (!slot) continue;
        slot->update_if_new();
        token = (token * UINT64_C(1000003) + static_cast<std::uint64_t>(slot->frame_id())) & UINT64_C(0x7fffffff);
    }
    return static_cast<Integer>(token);
}

void _WorkerPool::_print_status(::cw::lib::String arg_event, ::cw::lib::String arg_path)
{
    const auto runtime = runtime_for(this);
    std::size_t queue_size = 0;
    std::size_t workers = 0;
    if (runtime) {
        std::scoped_lock lock(runtime->mutex);
        queue_size = runtime->jobs.size();
        workers = runtime->workers.size();
    }
    std::scoped_lock pool_lock(m_state_mutex);
    const auto active = checked_size_to_integer(m__instances.size(), "active instance count");
    std::fprintf(stderr, "AnimatedImage: %s=%s total=%zu active=%lld unique_active=%zu workers=%zu queue=%zu\n",
        arg_event.c_str(), arg_path.c_str(), m__slots.size(), static_cast<long long>(active),
        m__path_instances.size(), workers, queue_size);
}

void _WorkerPool::_worker_loop()
{
    const auto runtime = runtime_for(this);
    if (!runtime) return;
    while (true) {
        Job job;
        {
            std::unique_lock lock(runtime->mutex);
            runtime->work_cv.wait_for(lock, std::chrono::milliseconds(500), [&] {
                return runtime->stop.load(std::memory_order_acquire) || !runtime->jobs.empty();
            });
            if (runtime->stop.load(std::memory_order_acquire)) return;
            if (runtime->jobs.empty()) continue;
            job = std::move(runtime->jobs.front());
            runtime->jobs.pop_front();
        }
        const auto id = key_id(job.key);
        std::shared_ptr<_Slot> slot;
        {
            std::scoped_lock lock(m_state_mutex);
            const auto found = m__slots.find(id);
            if (found == m__slots.end() || !found->second || !found->second->is_active() ||
                found->second->generation_value() != job.generation) continue;
            slot = found->second;
        }
        try {
            slot->initialize_decoder();
            const auto decoder = slot->decoder_snapshot();
            const auto frame = decoder ? decoder->next_frame() : nullptr;
            if (!frame) throw std::runtime_error("decoder returned empty frame");
            slot->push_frame(frame);
            if (!stopped(runtime) && slot->is_active()) {
                const auto delay = std::chrono::milliseconds(std::max<Integer>(16, frame->m_duration_ms));
                std::scoped_lock runtime_lock(runtime->mutex);
                if (!runtime->stop.load(std::memory_order_acquire)) {
                    runtime->delayed_jobs.emplace(
                        std::chrono::steady_clock::now() + delay,
                        Job{std::move(job.key), job.generation});
                    runtime->schedule_cv.notify_one();
                }
            }
        } catch (const std::exception& error) {
            try {
                // 復号失敗時にも有効なフレームを反映し、
                // UIが失敗する同期デコーダーへ戻ら
                // ず、透明な仮画像を描けるようにする。
                slot->push_frame(slot->transparent_frame());
            } catch (const std::exception&) {
                // 壊れたヘッダーや確保失敗で仮画像も作れない
                // 場合でも、ワーカー内で失敗を封じ込める。
            }
            slot->deactivate();
            std::fprintf(stderr, "AnimatedImage: decode_failed=%s reason=%s\n",
                slot->m_path_text.c_str(), error.what());
        } catch (...) {
            try {
                slot->push_frame(slot->transparent_frame());
            } catch (...) {
                // このワーカー上で、これ以上安全に復旧できるものはない。
            }
            slot->deactivate();
            std::fprintf(stderr, "AnimatedImage: decode_failed=%s reason=unknown\n",
                slot->m_path_text.c_str());
        }
    }
}

void _WorkerPool::_requeue(std::vector<::cw::lib::Value> arg_key, ::cw::lib::Integer arg_generation)
{
    const auto runtime = runtime_for(this);
    if (!runtime) return;
    const auto id = key_id(arg_key);
    {
        std::scoped_lock pool_lock(m_state_mutex);
        const auto found = m__slots.find(id);
        if (found == m__slots.end() || !found->second || !found->second->is_active()) return;
    }
    {
        std::scoped_lock lock(runtime->mutex);
        if (runtime->stop.load(std::memory_order_acquire)) return;
        runtime->jobs.push_back(Job{std::move(arg_key), arg_generation});
    }
    runtime->work_cv.notify_one();
}

void _WorkerPool::shutdown()
{
    std::shared_ptr<PoolRuntime> runtime;
    {
        std::scoped_lock lock(g_runtime_mutex);
        const auto found = g_runtime.find(this);
        if (found == g_runtime.end()) return;
        runtime = found->second;
    }
    std::unique_lock shutdown_lock(runtime->shutdown_mutex, std::defer_lock);
    const bool owned_caller = current_pool_runtime == runtime.get();
    // このミューテックスを持つ別の呼び出し元が、すでにこちらの終了を待っている
    // 場合がある。 プールのスレッドからロックを待つと、終了待ちが循環する。
    if (owned_caller) {
        if (!shutdown_lock.try_lock()) {
            request_stop(runtime);
            return;
        }
    } else {
        shutdown_lock.lock();
    }
    {
        std::scoped_lock pool_lock(m_state_mutex);
        m__stop = ::cw::lib::Value(true);
    }
    request_stop(runtime);
    const auto current = std::this_thread::get_id();
    auto finish_thread = [&](std::thread& thread) {
        if (!thread.joinable()) return;
        if (thread.get_id() == current) {
            // Python版と同様、current_thread は終了待ちから除く。
            // C++のハンドルを切り離して破棄時の異常終了を防ぎ、 取り込んだself
            // ・runtimeで、実際にスレッドが戻るまで両オブジェクトを保持する。
            thread.detach();
        } else {
            // C++版の意図的な境界として、std::thread には時間制限
            // 付きjoinがないため、 Pythonの二秒の待ちではなく完了を待
            // つ。 実行中のスレッドの結合可能なハンドルを破棄しないようにする。
            thread.join();
        }
    };
    for (auto& thread : runtime->workers) finish_thread(thread);
    finish_thread(runtime->scheduler);
    finish_thread(runtime->monitor);
    {
        std::scoped_lock lock(g_runtime_mutex);
        g_runtime.erase(this);
    }
}

_WorkerPool::~_WorkerPool()
{
    shutdown();
}

bool g_IDLE_STOP_ENABLED{true};
::cw::lib::Real g_IDLE_STOP_SECONDS{100.0};
std::tuple<::cw::lib::String, ::cw::lib::String> g_SUPPORTED_EXTS{".apng", ".webp"};
::cw::lib::Value g__STOP{};
std::optional<std::shared_ptr<::cw::animatedimage::_WorkerPool>> g__pool{};

void _WorkerPool__worker_loop_requeue_after_delay(::cw::lib::Real arg_delay, std::vector<::cw::lib::Value> arg_request_key, ::cw::lib::Integer arg_request_generation)
{
    std::shared_ptr<_WorkerPool> pool;
    {
        std::scoped_lock lock(g_pool_global_mutex);
        if (g__pool) pool = *g__pool;
    }
    const auto delay = std::chrono::milliseconds(delay_to_milliseconds(arg_delay));
    if (pool) {
        if (wait_for_stop(runtime_for(pool.get()), delay)) return;
        pool->_requeue(std::move(arg_request_key), arg_request_generation);
        return;
    }
    std::this_thread::sleep_for(delay);
}

std::shared_ptr<::cw::animatedimage::_WorkerPool> _get_pool()
{
    std::scoped_lock lock(g_pool_global_mutex);
    if (!g__pool) {
        auto pool = std::make_shared<_WorkerPool>();
        pool->initialize(2);
        g__pool = pool;
    }
    return *g__pool;
}

bool is_supported_path(::cw::lib::String arg_path)
{
    auto ext = ::cw::lib::encoding::path_to_utf8(
        ::cw::lib::encoding::path_from_utf8(arg_path).extension());
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return ext == std::get<0>(g_SUPPORTED_EXTS) || ext == std::get<1>(g_SUPPORTED_EXTS);
}

::cw::lib::String get_supported_extension(::cw::lib::String arg_path, std::optional<::cw::lib::Bytes> arg_data)
{
    auto ext = ::cw::lib::encoding::path_to_utf8(
        ::cw::lib::encoding::path_from_utf8(arg_path).extension());
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (ext == std::get<0>(g_SUPPORTED_EXTS) || ext == std::get<1>(g_SUPPORTED_EXTS)) {
        return ext;
    }
    if (!arg_data) return {};
    const auto& data = *arg_data;
    if (data.size() >= 12 &&
        std::equal(data.begin(), data.begin() + 4, "RIFF") &&
        std::equal(data.begin() + 8, data.begin() + 12, "WEBP")) {
        return ".webp";
    }
    static constexpr unsigned char png_signature[] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    if (data.size() >= 8 && std::equal(data.begin(), data.begin() + 8, std::begin(png_signature))) {
        static constexpr unsigned char animation_chunk[] = {'a', 'c', 'T', 'L'};
        if (std::search(data.begin() + 8, data.end(), std::begin(animation_chunk), std::end(animation_chunk)) != data.end()) {
            return ".apng";
        }
    }
    return {};
}

std::optional<::cw::lib::Integer> acquire(::cw::lib::String arg_path, ::cw::lib::String arg_owner, bool arg_isolated, std::optional<::cw::lib::Bytes> arg_data)
{
    if (!is_supported_path(arg_path)) return std::nullopt;
    return _get_pool()->acquire(std::move(arg_path), std::move(arg_owner), arg_isolated, std::move(arg_data));
}

namespace {
std::shared_ptr<_WorkerPool> existing_pool() {
    std::scoped_lock lock(g_pool_global_mutex);
    return g__pool ? *g__pool : nullptr;
}
}

void release(::cw::lib::String arg_owner, std::optional<::cw::lib::String> arg_path)
{
    if (const auto pool=existing_pool()) pool->release(std::move(arg_owner), std::move(arg_path));
}

bool has_owner_prefix(::cw::lib::String arg_prefix)
{
    const auto pool=existing_pool();
    return pool && pool->has_owner_prefix(std::move(arg_prefix));
}

bool has_active()
{
    const auto pool=existing_pool();
    return pool && pool->has_active();
}

bool has_instance(::cw::lib::String arg_path, ::cw::lib::Integer arg_instance_id)
{
    const auto pool=existing_pool();
    return pool && pool->has_instance(std::move(arg_path), arg_instance_id);
}

::cw::lib::Integer owner_prefix_frame_token(::cw::lib::String arg_prefix)
{
    const auto pool=existing_pool();
    return pool ? pool->owner_prefix_frame_token(std::move(arg_prefix)) : 0;
}

std::tuple<std::optional<std::shared_ptr<::cw::lib::Surface>>, ::cw::lib::Integer> get_surface(::cw::lib::String arg_path, std::optional<::cw::lib::Integer> arg_instance_id, std::optional<::cw::lib::Bytes> arg_data)
{
    if (!is_supported_path(arg_path)) return {std::nullopt, 0};
    const auto pool = _get_pool();
    std::optional<std::shared_ptr<_Slot>> slot;
    bool can_update = false;
    if (!arg_instance_id && arg_data) {
        slot = pool->request(arg_path, std::move(arg_data));
        can_update = true;
    } else {
        std::tie(slot, can_update) = pool->get_slot_for_update(arg_path, arg_instance_id);
    }
    if (!slot || !can_update) return {std::nullopt, 0};
    (*slot)->update_if_new();
    auto [frame, cached, frame_id] = (*slot)->surface_snapshot();
    if (frame && !cached) {
        const auto& raw_frame = **frame;
        const auto [width, height] = raw_frame.m_size;
        const auto metadata = metadata_for(raw_frame);
        auto surface_factory = surface_adapter();
        auto surface = surface_factory
            ? surface_factory(raw_frame.m_data, width, height, metadata)
            : make_surface_frame(raw_frame.m_data, width, height, metadata);
        (*slot)->cache_surface(frame_id, std::move(surface));
        std::tie(frame, cached, frame_id) = (*slot)->surface_snapshot();
    }
    if (!cached || !*cached) return {std::nullopt, frame_id};
    auto copy = std::make_shared<::cw::lib::Surface>(**cached);
    return {std::move(copy), frame_id};
}

std::tuple<std::optional<std::shared_ptr<::cw::lib::Bitmap>>, ::cw::lib::Integer> get_wxbmp(::cw::lib::String arg_path, std::optional<::cw::lib::Integer> arg_instance_id, std::optional<::cw::lib::Bytes> arg_data)
{
    if (!is_supported_path(arg_path)) return {std::nullopt, 0};
    const auto pool = _get_pool();
    std::optional<std::shared_ptr<_Slot>> slot;
    bool can_update = false;
    if (!arg_instance_id && arg_data) {
        slot = pool->request(arg_path, std::move(arg_data));
        can_update = true;
    } else {
        std::tie(slot, can_update) = pool->get_slot_for_update(arg_path, arg_instance_id);
    }
    if (!slot || !can_update) return {std::nullopt, 0};
    (*slot)->update_if_new();
    auto [frame, cached, frame_id] = (*slot)->bitmap_snapshot();
    if (frame && !cached) {
        const auto& raw_frame = **frame;
        const auto [width, height] = raw_frame.m_size;
        const auto metadata = metadata_for(raw_frame);
        auto bitmap_factory = bitmap_adapter();
        auto bitmap = bitmap_factory
            ? bitmap_factory(raw_frame.m_data, width, height, metadata)
            : make_bitmap_frame(raw_frame.m_data, width, height, metadata);
        (*slot)->cache_bitmap(frame_id, std::move(bitmap));
        std::tie(frame, cached, frame_id) = (*slot)->bitmap_snapshot();
    }
    if (!cached || !*cached) return {std::nullopt, frame_id};
    const auto& bitmap = *cached;
    auto subrect_factory = subrect_adapter();
    auto copy = subrect_factory
        ? subrect_factory(bitmap, 0, 0, bitmap->size.width, bitmap->size.height)
        : make_bitmap_subrect(bitmap, 0, 0, bitmap->size.width, bitmap->size.height);
    return {std::move(copy), frame_id};
}

::cw::lib::Integer get_frame_id(::cw::lib::String arg_path, std::optional<::cw::lib::Integer> arg_instance_id, std::optional<::cw::lib::Bytes> arg_data)
{
    if (!is_supported_path(arg_path)) return 0;
    const auto pool = _get_pool();
    std::optional<std::shared_ptr<_Slot>> slot;
    bool can_update = false;
    if (!arg_instance_id && arg_data) {
        slot = pool->request(arg_path, std::move(arg_data));
        can_update = true;
    } else {
        std::tie(slot, can_update) = pool->get_slot_for_update(arg_path, arg_instance_id);
    }
    if (!slot || !can_update) return 0;
    (*slot)->update_if_new();
    return (*slot)->frame_id();
}

void set_surface_frame_adapter(SurfaceFrameAdapter arg_adapter)
{
    auto& state = adapter_state();
    std::scoped_lock lock(state.mutex);
    state.surface = std::move(arg_adapter);
}

void set_bitmap_frame_adapter(BitmapFrameAdapter arg_adapter)
{
    auto& state = adapter_state();
    std::scoped_lock lock(state.mutex);
    state.bitmap = std::move(arg_adapter);
}

void set_bitmap_subrect_adapter(BitmapSubrectAdapter arg_adapter)
{
    auto& state = adapter_state();
    std::scoped_lock lock(state.mutex);
    state.subrect = std::move(arg_adapter);
}

void shutdown()
{
    std::shared_ptr<_WorkerPool> pool;
    {
        std::scoped_lock lock(g_pool_global_mutex);
        if (!g__pool) return;
        pool = *g__pool;
    }
    if (pool) pool->shutdown();
    {
        std::scoped_lock lock(g_pool_global_mutex);
        if (g__pool && *g__pool == pool) g__pool.reset();
    }
}

}
