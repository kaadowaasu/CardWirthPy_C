#include "preview.hpp"
#include "midi_player.hpp"
#include "cw/lib/encoding.hpp"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_mixer/SDL_mixer.h>
#if defined(__ANDROID__)
#include <SDL3/SDL_system.h>
#include <jni.h>
#endif
#if defined(__APPLE__)
#include <TargetConditionals.h>
#include "platform_transfer_apple.hpp"
#endif
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace cardwirth::explorer {

namespace {

void SDLCALL observe_mixer_signal(void* userdata, MIX_Mixer*, const SDL_AudioSpec*,
                                  float* pcm, int samples) noexcept {
    if (!userdata || !pcm || samples <= 0) return;
    auto& detected = *static_cast<std::atomic_bool*>(userdata);
    if (detected.load(std::memory_order_relaxed)) return;
    for (int index = 0; index < samples; ++index) {
        if (std::abs(pcm[index]) > 0.00001F) {
            detected.store(true, std::memory_order_release);
            return;
        }
    }
}
constexpr std::size_t kMaximumPreviewPixels = 16U * 1024U * 1024U;
constexpr std::size_t kRgbaBytesPerPixel = 4U;
}

std::optional<detail::PreviewImageLayout> detail::checked_preview_image_layout(
    const int width_value, const int height_value, const int pitch_value) noexcept {
    if (width_value <= 0 || height_value <= 0 || pitch_value <= 0) return std::nullopt;
    const auto width = static_cast<std::size_t>(width_value);
    const auto height = static_cast<std::size_t>(height_value);
    const auto pitch = static_cast<std::size_t>(pitch_value);
    constexpr auto maximum = (std::numeric_limits<std::size_t>::max)();
    if (width > maximum / height) return std::nullopt;
    const auto pixels = width * height;
    if (pixels > kMaximumPreviewPixels || width > maximum / kRgbaBytesPerPixel)
        return std::nullopt;
    const auto row_bytes = width * kRgbaBytesPerPixel;
    if (pitch < row_bytes || height > maximum / pitch || height > maximum / row_bytes)
        return std::nullopt;
    return detail::PreviewImageLayout{
        row_bytes,
        pitch * height,
        row_bytes * height,
    };
}

namespace {

constexpr std::size_t kPreviewLimit = 64U * 1024U;

bool supported_preview_dimensions(const int width_value, const int height_value) noexcept {
    if (width_value <= 0 || height_value <= 0) return false;
    const auto width = static_cast<std::size_t>(width_value);
    const auto height = static_cast<std::size_t>(height_value);
    if (width > (std::numeric_limits<std::size_t>::max)() / height) return false;
    return width * height <= kMaximumPreviewPixels;
}

std::string path_text(const std::filesystem::path& path) {
    const auto encoded = path.u8string();
    return std::string(reinterpret_cast<const char*>(encoded.data()), encoded.size());
}

std::string extension_of(const std::filesystem::path& path) {
    auto result = path_text(path.extension());
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return result;
}

#if defined(_WIN32)
bool managed_userdata_session() noexcept {
    wchar_t value[2]{};
    return GetEnvironmentVariableW(L"CARDWIRTHC_MANAGED_USERDATA_SESSION",
                                   value, 2U) == 1U &&
           value[0] == L'1';
}
#endif

std::optional<std::string> decode_text(const std::vector<unsigned char>& data) {
    if (data.empty()) return std::string{};
    const ::cw::lib::Bytes bytes(data.begin(), data.end());
    try {
        auto text = ::cw::lib::encoding::decode_legacy_text(bytes);
        if (text.find('\0') != std::string::npos) return std::nullopt;
        const auto controls = std::count_if(text.begin(), text.end(), [](unsigned char value) {
            return value < 0x20U && value != '\t' && value != '\n' && value != '\r';
        });
        if (controls > (std::max)(std::size_t{2}, text.size() / 100U))
            return std::nullopt;
        return text;
    } catch (const ::cw::lib::encoding::EncodingError&) {
        return std::nullopt;
    }
}

std::vector<unsigned char> read_prefix(const std::filesystem::path& path, bool& truncated) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("ファイルを開けません: " + path_text(path));
    input.seekg(0, std::ios::end);
    const auto length = input.tellg();
    input.seekg(0, std::ios::beg);
    const auto wanted = length < 0 ? kPreviewLimit :
        std::min<std::uintmax_t>(static_cast<std::uintmax_t>(length), kPreviewLimit);
    std::vector<unsigned char> data(static_cast<std::size_t>(wanted));
    if (!data.empty()) input.read(reinterpret_cast<char*>(data.data()),
                                  static_cast<std::streamsize>(data.size()));
    if (!input && !input.eof()) throw std::runtime_error("ファイルを読み取れません");
    truncated = length > static_cast<std::streamoff>(data.size());
    return data;
}

std::vector<unsigned char> read_prefix(const PreviewSource& source,
                                       const cardwirth::platform::StoragePath& path,
                                       bool& truncated) {
    const auto information = source.stat(path);
    if (!information || !information->is_file())
        throw std::invalid_argument("プレビュー対象が通常ファイルではありません");
    auto input = source.open_read(path);
    if (!input) {
        const auto error = source.last_error();
        throw std::runtime_error(error.empty() ? "ファイルを開けません" : error);
    }

    std::vector<unsigned char> data;
    data.reserve(kPreviewLimit);
    std::array<std::byte, kPreviewLimit> buffer{};
    while (data.size() < kPreviewLimit) {
        const auto remaining = kPreviewLimit - data.size();
        const auto count = input->read(std::span<std::byte>(buffer.data(), remaining));
        if (count != 0) {
            const auto* begin = reinterpret_cast<const unsigned char*>(buffer.data());
            data.insert(data.end(), begin, begin + count);
        }
        if (!input->good()) throw std::runtime_error("ファイルを読み取れません");
        if (count == 0) break;
    }
    truncated = information->size > data.size();
    if (!truncated && data.size() == kPreviewLimit) {
        std::array<std::byte, 1> extra{};
        const auto count = input->read(std::span<std::byte>(extra));
        if (!input->good()) throw std::runtime_error("ファイルを読み取れません");
        truncated = count != 0;
    }
    return data;
}

std::string hex_dump(const std::vector<unsigned char>& data, bool truncated) {
    std::ostringstream output;
    output << "Binary preview (" << data.size() << " bytes shown)\n";
    const auto count = std::min<std::size_t>(data.size(), 512);
    for (std::size_t offset = 0; offset < count; offset += 16) {
        output << std::hex << std::setw(8) << std::setfill('0') << offset << "  ";
        for (std::size_t index = 0; index < 16; ++index) {
            if (offset + index < count)
                output << std::setw(2) << static_cast<unsigned>(data[offset + index]) << ' ';
            else output << "   ";
        }
        output << "  ";
        for (std::size_t index = 0; index < 16 && offset + index < count; ++index) {
            const auto character = data[offset + index];
            output << (character >= 0x20 && character < 0x7f ? static_cast<char>(character) : '.');
        }
        output << '\n';
    }
    if (truncated || data.size() > count) output << "...\n";
    return output.str();
}

bool is_image_extension(const std::string& extension) {
    static const std::unordered_set<std::string> values{
        ".png", ".jpg", ".jpeg", ".bmp", ".gif", ".webp"};
    return values.contains(extension);
}

bool is_audio_extension(const std::string& extension) {
    static const std::unordered_set<std::string> values{
        ".mp3", ".ogg", ".wav", ".flac"};
    return values.contains(extension);
}

bool is_midi_extension(const std::string& extension) {
    return extension == ".mid" || extension == ".midi";
}

bool is_video_extension(const std::string& extension) {
    static const std::unordered_set<std::string> values{
        ".mp4", ".m4v", ".mov", ".webm", ".mkv", ".avi", ".mpg", ".mpeg"};
    return values.contains(extension);
}

#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
std::string file_url(const std::filesystem::path& path) {
    std::string raw = path_text(std::filesystem::absolute(path));
    std::replace(raw.begin(), raw.end(), '\\', '/');
    std::string result = raw.starts_with('/') ? "file://" : "file:///";
    constexpr char hex[] = "0123456789ABCDEF";
    for (const unsigned char character : raw) {
        const bool safe = std::isalnum(character) || character == '-' || character == '_' ||
                          character == '.' || character == '~' || character == '/' || character == ':';
        if (safe) result.push_back(static_cast<char>(character));
        else {
            result.push_back('%');
            result.push_back(hex[character >> 4U]);
            result.push_back(hex[character & 0x0FU]);
        }
    }
    return result;
}
#endif

#if defined(__EMSCRIPTEN__)
// web_transfer.jsで実装する。Web版ALLがpthreadでSDLループを動かす場合に
// も DOMへアクセスできるよう、JavaScriptライブラリがブラウザースレッドへ処理を転送する。
extern "C" bool cardwirth_web_open_preview_video(const char* path);
extern "C" void cardwirth_web_close_preview_video();
#endif

#if defined(__ANDROID__)
bool play_android_video(const std::filesystem::path& path) {
    auto* environment = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    auto* activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (!environment || !activity) return false;
    jclass activity_class = environment->GetObjectClass(activity);
    if (!activity_class) return false;
    const jmethodID method = environment->GetMethodID(
        activity_class, "playVideo", "(Ljava/lang/String;)Z");
    if (!method) {
        environment->DeleteLocalRef(activity_class);
        if (environment->ExceptionCheck()) environment->ExceptionClear();
        return false;
    }
    const auto encoded = path_text(std::filesystem::absolute(path));
    jstring argument = environment->NewStringUTF(encoded.c_str());
    const bool opened = argument && environment->CallBooleanMethod(activity, method, argument);
    if (argument) environment->DeleteLocalRef(argument);
    environment->DeleteLocalRef(activity_class);
    if (environment->ExceptionCheck()) {
        environment->ExceptionClear();
        return false;
    }
    return opened;
}
#endif

}  // namespace

struct PreviewService::Impl {
    explicit Impl(std::filesystem::path data_directory)
        : native_data_directory(data_directory), midi(std::move(data_directory)) {}
    Impl(std::shared_ptr<PreviewSource> preview_source,
         cardwirth::platform::StoragePath data_directory)
        : source(std::move(preview_source)),
          storage_data_directory(std::move(data_directory)),
          midi({}) {}

    ~Impl() {
        stop_video();
        stop_audio();
        if (mixer) {
            MIX_DestroyMixer(mixer);
            mixer = nullptr;
        }
        if (mixer_initialized) {
            MIX_Quit();
            mixer_initialized = false;
        }
        for (const auto& path : temporary_paths) {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    }

    std::filesystem::path materialize(
        const cardwirth::platform::StoragePath& logical_path) {
        if (!source) return logical_path;
        static std::atomic_uint64_t sequence{0};
        std::error_code error;
        const auto root = std::filesystem::temp_directory_path(error);
        if (error) throw std::runtime_error("プレビュー一時領域を取得できません: " +
                                            error.message());
        std::filesystem::path directory;
        for (unsigned attempt = 0; attempt < 128; ++attempt) {
            directory = root / ("cardwirth-preview-" +
                std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
            error.clear();
            if (std::filesystem::create_directory(directory, error)) break;
            if (error != std::errc::file_exists)
                throw std::runtime_error("プレビュー一時領域を作成できません: " +
                                         error.message());
            directory.clear();
        }
        if (directory.empty()) throw std::runtime_error("プレビュー一時領域名を確保できません");

        auto name = logical_path.filename();
        if (name.empty() || name == "." || name == "..") name = "preview.bin";
        const auto destination = directory / name;
        auto input = source->open_read(logical_path);
        if (!input) {
            std::filesystem::remove_all(directory, error);
            const auto message = source->last_error();
            throw std::runtime_error(message.empty() ? "プレビュー対象を開けません" : message);
        }
        std::ofstream output(destination, std::ios::binary | std::ios::trunc);
        if (!output) {
            std::filesystem::remove_all(directory, error);
            throw std::runtime_error("プレビュー一時ファイルを作成できません");
        }
        std::array<std::byte, 64U * 1024U> buffer{};
        try {
            for (;;) {
                const auto count = input->read(std::span<std::byte>(buffer));
                if (count != 0) {
                    output.write(reinterpret_cast<const char*>(buffer.data()),
                                 static_cast<std::streamsize>(count));
                    if (!output) throw std::runtime_error("プレビュー一時ファイルを書き込めません");
                }
                if (!input->good()) throw std::runtime_error("プレビュー対象を読み取れません");
                if (count == 0) break;
            }
            output.flush();
            if (!output) throw std::runtime_error("プレビュー一時ファイルを確定できません");
            output.close();
            if (!output) throw std::runtime_error("プレビュー一時ファイルを閉じられません");
        } catch (...) {
            output.close();
            std::filesystem::remove_all(directory, error);
            throw;
        }
        temporary_paths.push_back(directory);
        return destination;
    }

    bool remove_temporary(const std::filesystem::path& path) noexcept {
        if (path.empty() || !source) return true;
        const auto directory = path.parent_path();
        std::error_code error;
        std::filesystem::remove_all(directory, error);
        if (error) return false;
        const bool remains = std::filesystem::exists(directory, error);
        if (error || remains) return false;
        std::erase(temporary_paths, directory);
        return true;
    }

    void cleanup_active_audio_media() noexcept {
        for (auto it = active_audio_media.begin(); it != active_audio_media.end();) {
            if (remove_temporary(*it)) it = active_audio_media.erase(it);
            else ++it;
        }
    }

    std::optional<cardwirth::platform::StoragePath> find_storage_soundfont() const {
        if (!source || storage_data_directory.empty()) return std::nullopt;
        const auto preferred = storage_data_directory / "SoundFont" /
            "005.6mg_Aspirin_Stereo_V1.2_Bank.sf2";
        if (const auto information = source->stat(preferred);
            information && information->is_file())
            return preferred;

        std::vector<cardwirth::platform::StoragePath> pending{storage_data_directory};
        std::vector<cardwirth::platform::StoragePath> candidates;
        std::size_t visited = 0;
        constexpr std::size_t kMaximumVisitedEntries = 100000;
        while (!pending.empty() && visited < kMaximumVisitedEntries) {
            auto directory = std::move(pending.back());
            pending.pop_back();
            auto entries = source->list(directory);
            std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
                return path_text(left.path) < path_text(right.path);
            });
            for (auto& entry : entries) {
                if (++visited > kMaximumVisitedEntries) break;
                if (entry.stat.is_directory()) pending.push_back(std::move(entry.path));
                else if (entry.stat.is_file() && extension_of(entry.path) == ".sf2")
                    candidates.push_back(std::move(entry.path));
            }
        }
        std::sort(candidates.begin(), candidates.end(), [](const auto& left, const auto& right) {
            return path_text(left) < path_text(right);
        });
        return candidates.empty() ? std::nullopt
                                  : std::optional<cardwirth::platform::StoragePath>(
                                        std::move(candidates.front()));
    }

    void stop_audio() noexcept {
        midi.stop();
        if (track) {
            MIX_StopTrack(track, 0);
            MIX_DestroyTrack(track);
            track = nullptr;
        }
        if (audio) {
            MIX_DestroyAudio(audio);
            audio = nullptr;
        }
        cleanup_active_audio_media();
    }

    void stop_video() noexcept {
        if (decoder_only) return;
#if defined(__EMSCRIPTEN__)
        cardwirth_web_close_preview_video();
#elif defined(__APPLE__) && TARGET_OS_IOS
        apple::stop_preview_video();
#endif
        for (auto it = active_video_media.begin(); it != active_video_media.end();) {
            if (remove_temporary(*it)) it = active_video_media.erase(it);
            else ++it;
        }
    }

    MIX_Mixer* mixer{};
    MIX_Audio* audio{};
    MIX_Track* track{};
    bool mixer_initialized{};
    bool decoder_only{};
    bool audio_suspended{};
    std::atomic_bool audio_signal_detected{};
    std::shared_ptr<PreviewSource> source;
    cardwirth::platform::StoragePath storage_data_directory;
    std::filesystem::path native_data_directory;
    std::vector<std::filesystem::path> temporary_paths;
    // 外部動画プレイヤーの起動ではプロセスハンドルを持たない。
    // 実体化したファイルは音声停止と一緒に削除せず、サービ
    // スの破棄までtemporary_pathsに保持する。
    std::vector<std::filesystem::path> active_audio_media;
    std::vector<std::filesystem::path> active_video_media;
    MidiPlayer midi;
};

PreviewService::PreviewService(std::filesystem::path data_directory)
    : impl_(std::make_unique<Impl>(std::move(data_directory))) {}

PreviewService::PreviewService(std::shared_ptr<PreviewSource> source,
                               cardwirth::platform::StoragePath data_directory)
    : impl_(std::make_unique<Impl>(std::move(source), std::move(data_directory))) {}

PreviewService::PreviewService(std::shared_ptr<cardwirth::platform::Storage> storage,
                               cardwirth::platform::StoragePath data_directory)
    : PreviewService(storage ? std::make_shared<StoragePreviewSource>(std::move(storage)) :
                              std::shared_ptr<PreviewSource>{},
                     std::move(data_directory)) {}

PreviewService::PreviewService(cardwirth::platform::Storage& storage,
                               cardwirth::platform::StoragePath data_directory)
    : PreviewService(std::make_shared<StoragePreviewSource>(storage),
                     std::move(data_directory)) {}

PreviewService::~PreviewService() = default;

PreviewService::PreviewService(PreviewService&&) noexcept = default;
PreviewService& PreviewService::operator=(PreviewService&&) noexcept = default;

std::shared_ptr<PreviewService> PreviewService::make_decoder() const {
    auto result = impl_->source
        ? std::make_shared<PreviewService>(impl_->source, impl_->storage_data_directory)
        : std::make_shared<PreviewService>(impl_->native_data_directory);
    result->impl_->decoder_only = true;
    return result;
}

PreviewData PreviewService::load(const std::filesystem::path& path) const {
    std::error_code error;
    if (impl_->source) {
        const auto information = impl_->source->stat(path);
        if (!information || !information->is_file())
            throw std::invalid_argument("プレビュー対象が通常ファイルではありません");
    } else if (!std::filesystem::is_regular_file(path, error) || error) {
        throw std::invalid_argument("プレビュー対象が通常ファイルではありません");
    }
    PreviewData result;
    result.path = path;
    result.title = path_text(path.filename());
    const auto extension = extension_of(path);
    if (is_midi_extension(extension)) {
        const auto soundfont = impl_->source ? impl_->find_storage_soundfont()
                                             : impl_->midi.find_soundfont();
        if (!soundfont)
            throw std::invalid_argument(
                "Data内にSoundFont (.sf2) がないためMIDIを再生できません");
        result.kind = PreviewKind::Audio;
        result.text = "SoundFont: " + path_text(soundfont->filename());
        return result;
    }
    if (is_image_extension(extension)) {
        const auto native_path = impl_->source ? impl_->materialize(path) : path;
        try {
            using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
            Surface loaded(IMG_Load(path_text(native_path).c_str()), SDL_DestroySurface);
            if (!loaded)
                throw std::runtime_error("画像を開けません: " + std::string(SDL_GetError()));
            if (!supported_preview_dimensions(loaded->w, loaded->h))
                throw std::runtime_error("画像プレビューの寸法が大きすぎるか不正です");
            Surface image(SDL_ConvertSurface(loaded.get(), SDL_PIXELFORMAT_RGBA32),
                          SDL_DestroySurface);
            if (!image) throw std::runtime_error("画像をRGBAへ変換できません");
            const auto layout = detail::checked_preview_image_layout(
                image->w, image->h, image->pitch);
            if (!layout || !image->pixels)
                throw std::runtime_error("画像プレビューの寸法またはpitchが不正です");
            result.kind = PreviewKind::Image;
            result.image_width = image->w;
            result.image_height = image->h;
            const auto* pixels = static_cast<const std::uint8_t*>(image->pixels);
            result.rgba.resize(layout->total_bytes);
            const auto pitch = static_cast<std::size_t>(image->pitch);
            for (std::size_t row = 0; row < static_cast<std::size_t>(image->h); ++row) {
                std::copy_n(pixels + row * pitch, layout->row_bytes,
                            result.rgba.data() + row * layout->row_bytes);
            }
            result.text = std::to_string(result.image_width) + " x " +
                          std::to_string(result.image_height);
        } catch (...) {
            if (impl_->source) impl_->remove_temporary(native_path);
            throw;
        }
        if (impl_->source) impl_->remove_temporary(native_path);
        return result;
    }
    if (is_audio_extension(extension)) {
        result.kind = PreviewKind::Audio;
        result.text = "再生中: " + result.title;
        return result;
    }
    if (is_video_extension(extension)) {
        result.kind = PreviewKind::Video;
        result.text = "端末の動画プレイヤーで再生中: " + result.title;
        return result;
    }

    bool truncated = false;
    const auto bytes = impl_->source
        ? read_prefix(*impl_->source, path, truncated)
        : read_prefix(path, truncated);
    if (const auto text = decode_text(bytes)) {
        result.kind = PreviewKind::Text;
        result.text = *text;
        if (truncated) result.text += "\n... (64 KiBまで表示)";
    } else {
        result.kind = PreviewKind::Binary;
        result.text = hex_dump(bytes, truncated);
    }
    return result;
}

void PreviewService::play_audio(const std::filesystem::path& path) {
    const auto extension = extension_of(path);
    if (is_midi_extension(extension)) {
        stop_audio();
        if (!impl_->source) {
            impl_->midi.play(path);
            if (impl_->audio_suspended) impl_->midi.pause();
            return;
        }
        const auto soundfont = impl_->find_storage_soundfont();
        if (!soundfont)
            throw std::runtime_error(
                "Data内にSoundFont (.sf2) がないためMIDIを再生できません");
        try {
            const auto native_midi = impl_->materialize(path);
            impl_->active_audio_media.push_back(native_midi);
            const auto native_soundfont = impl_->materialize(*soundfont);
            impl_->active_audio_media.push_back(native_soundfont);
            impl_->midi = MidiPlayer({}, native_soundfont);
            impl_->midi.play(native_midi);
            if (impl_->audio_suspended) impl_->midi.pause();
        } catch (...) {
            stop_audio();
            throw;
        }
        return;
    }
    if (!is_audio_extension(extension)) throw std::invalid_argument("音楽ファイルではありません");
    stop_audio();
    impl_->audio_signal_detected.store(false, std::memory_order_release);
    const auto native_path = impl_->source ? impl_->materialize(path) : path;
    if (impl_->source) impl_->active_audio_media.push_back(native_path);
    if (!impl_->mixer_initialized) {
        if (!MIX_Init()) {
            stop_audio();
            throw std::runtime_error("SDL_mixerを初期化できません");
        }
        impl_->mixer_initialized = true;
    }
    if (!impl_->mixer) {
        impl_->mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
        if (!impl_->mixer) {
            stop_audio();
            throw std::runtime_error("音声デバイスを開けません: " +
                                     std::string(SDL_GetError()));
        }
        if (!MIX_SetPostMixCallback(impl_->mixer, observe_mixer_signal,
                                    &impl_->audio_signal_detected)) {
            stop_audio();
            throw std::runtime_error("音声出力の監視を開始できません");
        }
    }
    impl_->audio = MIX_LoadAudio(impl_->mixer, path_text(native_path).c_str(), false);
    if (!impl_->audio) {
        stop_audio();
        throw std::runtime_error("音声ファイルを開けません");
    }
    impl_->track = MIX_CreateTrack(impl_->mixer);
    if (!impl_->track || !MIX_SetTrackAudio(impl_->track, impl_->audio) ||
        !MIX_PlayTrack(impl_->track, 0)) {
        stop_audio();
        throw std::runtime_error("音声を再生できません");
    }
    if (impl_->audio_suspended) MIX_PauseTrack(impl_->track);
}

void PreviewService::play_video(const std::filesystem::path& path) {
    if (!is_video_extension(extension_of(path)))
        throw std::invalid_argument("動画ファイルではありません");
#if defined(_WIN32)
    // SDL_OpenURLは、専用ランチャーのJob Object外にあるプレイ
    // ヤーを再利用する場合がある。 その場合、ランチャーは一時セッションのパスが解
    // 放されたかを確認できず、 保存の確定と平文ファイルの削除を安全に待てない。
    if (managed_userdata_session())
        throw std::runtime_error(
            "managed UserData sessionでは外部動画プレイヤーを起動できません");
#endif
    stop_audio();
    stop_video();
    const auto native_path = impl_->source ? impl_->materialize(path) : path;
#if defined(__ANDROID__)
    if (!play_android_video(native_path)) {
        if (impl_->source) impl_->remove_temporary(native_path);
        throw std::runtime_error("Android動画プレイヤーを開けません");
    }
#elif defined(__EMSCRIPTEN__)
    if (!cardwirth_web_open_preview_video(path_text(native_path).c_str())) {
        if (impl_->source) impl_->remove_temporary(native_path);
        throw std::runtime_error("Web動画プレイヤーを開けません");
    }
    if (impl_->source) impl_->active_video_media.push_back(native_path);
#elif defined(__APPLE__) && TARGET_OS_IOS
    if (!apple::play_preview_video(native_path)) {
        if (impl_->source) impl_->remove_temporary(native_path);
        throw std::runtime_error("iOS動画プレイヤーを開けません");
    }
    if (impl_->source) impl_->active_video_media.push_back(native_path);
#else
    const auto url = file_url(native_path);
    if (!SDL_OpenURL(url.c_str())) {
        if (impl_->source) impl_->remove_temporary(native_path);
        throw std::runtime_error("動画プレイヤーを開けません: " + std::string(SDL_GetError()));
    }
#endif
    // 外部のデスクトッププレイヤーはこの呼び出しより長く動作し、SDL_OpenURL
    // はプロセスハンドルを返さない。 materialize()がImplの
    // 破棄までパスを保持するので、stop_audio()には結び付けない。
}

void PreviewService::stop_video() noexcept {
    if (!impl_) return;
    impl_->stop_video();
}

void PreviewService::play_video(const std::filesystem::path& path) const {
    const_cast<PreviewService*>(this)->play_video(path);
}

void PreviewService::stop_audio() noexcept {
    if (!impl_) return;
    impl_->stop_audio();
}

void PreviewService::set_audio_suspended(bool suspended) noexcept {
    if (!impl_ || impl_->audio_suspended == suspended) return;
    impl_->audio_suspended = suspended;
    if (suspended) {
        impl_->midi.pause();
        if (impl_->track) MIX_PauseTrack(impl_->track);
    } else {
        impl_->midi.resume();
        if (impl_->track) MIX_ResumeTrack(impl_->track);
    }
}

bool PreviewService::collect_finished_audio() noexcept {
    if (!impl_ || !impl_->midi.collect_finished()) return false;
    impl_->cleanup_active_audio_media();
    return true;
}

bool PreviewService::audio_has_signal() const noexcept {
    return impl_ && impl_->audio_signal_detected.load(std::memory_order_acquire);
}

}  // namespace cardwirth::explorer
